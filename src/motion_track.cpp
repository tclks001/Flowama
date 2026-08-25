#include "motion_track.h"

#include <array>
#include <charconv>
#include <cmath>
#include <ctime>
#include <string>
#include <string_view>
#include <system_error>

#include <fmt/chrono.h>
#include <fmt/format.h>

namespace flowama {
namespace {

bool ReportParseError(
    const std::filesystem::path& inputPath,
    const int lineNumber,
    const std::string_view message)
{
    fmt::print(
        stderr,
        "Invalid motion track {} at line {}: {}.\n",
        inputPath.string(),
        lineNumber,
        message);
    return false;
}

bool ParseInteger(const std::string_view text, int& value)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

bool ParseFloat(const std::string_view text, float& value)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size()
        && std::isfinite(value);
}

} // namespace

bool MotionTrack::Load(const std::filesystem::path& inputPath)
{
    std::ifstream input(inputPath);
    if (!input) {
        fmt::print(stderr, "Failed to open motion track: {}\n", inputPath.string());
        return false;
    }

    samples_.clear();
    std::string line;
    int expectedTick = 0;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (line.empty() || line.front() == '#') {
            continue;
        }

        std::array<std::string_view, 8> fields{};
        std::string_view remaining{line};
        for (std::size_t fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex) {
            const std::size_t separator = remaining.find(',');
            if (separator == std::string_view::npos) {
                if (fieldIndex + 1 != fields.size()) {
                    return ReportParseError(inputPath, lineNumber, "expected eight comma-separated values");
                }
                fields[fieldIndex] = remaining;
                remaining = {};
                continue;
            }

            fields[fieldIndex] = remaining.substr(0, separator);
            remaining.remove_prefix(separator + 1);
        }
        if (!remaining.empty()) {
            return ReportParseError(inputPath, lineNumber, "expected eight comma-separated values");
        }

        int tick = 0;
        if (!ParseInteger(fields[0], tick) || tick != expectedTick) {
            return ReportParseError(inputPath, lineNumber, "ticks must start at zero and be consecutive");
        }

        std::array<float, 7> values{};
        for (std::size_t valueIndex = 0; valueIndex < values.size(); ++valueIndex) {
            if (!ParseFloat(fields[valueIndex + 1], values[valueIndex])) {
                return ReportParseError(inputPath, lineNumber, "position and quaternion values must be finite numbers");
            }
        }

        ContainerPose pose;
        pose.positionWorld = {values[0], values[1], values[2]};
        pose.orientationContainerToWorld = glm::quat{values[6], values[3], values[4], values[5]};
        const float orientationLength = glm::length(pose.orientationContainerToWorld);
        if (!std::isfinite(orientationLength) || orientationLength < 0.000001F) {
            return ReportParseError(inputPath, lineNumber, "quaternion must have non-zero length");
        }

        pose.orientationContainerToWorld /= orientationLength;
        samples_.push_back(pose);
        ++expectedTick;
    }

    if (!input.eof()) {
        fmt::print(stderr, "Failed while reading motion track: {}\n", inputPath.string());
        return false;
    }
    if (samples_.empty()) {
        fmt::print(stderr, "Motion track has no samples: {}\n", inputPath.string());
        return false;
    }

    return true;
}

bool MotionTrack::HasSample(const int tick) const
{
    return tick >= 0 && static_cast<std::size_t>(tick) < samples_.size();
}

const ContainerPose& MotionTrack::Sample(const int tick) const
{
    return samples_[static_cast<std::size_t>(tick)];
}

bool MotionTrackRecorder::Begin()
{
    Close();

    std::error_code directoryError;
    const std::filesystem::path directory{"data/motion-tracks"};
    std::filesystem::create_directories(directory, directoryError);
    if (directoryError) {
        fmt::print(
            stderr,
            "Failed to create motion track directory '{}': {}\n",
            directory.string(),
            directoryError.message());
        return false;
    }

    outputPath_ = CreateOutputPath(directory);
    output_.open(outputPath_);
    if (!output_) {
        fmt::print(stderr, "Failed to create motion track: {}\n", outputPath_.string());
        return false;
    }

    output_ << "# Flowama container motion track\n"
            << "# tick,px,py,pz,qx,qy,qz,qw\n";
    nextTick_ = 0;
    fmt::print("Recording container motion to {}\n", outputPath_.string());
    return true;
}

bool MotionTrackRecorder::Record(const int tick, const ContainerPose& pose)
{
    if (!output_) {
        return false;
    }
    if (tick != nextTick_) {
        fmt::print(stderr, "Motion recording tick sequence is invalid.\n");
        return false;
    }

    const glm::quat orientation = glm::normalize(pose.orientationContainerToWorld);
    output_ << fmt::format(
        "{},{:.9g},{:.9g},{:.9g},{:.9g},{:.9g},{:.9g},{:.9g}\n",
        tick,
        pose.positionWorld.x,
        pose.positionWorld.y,
        pose.positionWorld.z,
        orientation.x,
        orientation.y,
        orientation.z,
        orientation.w);
    if (!output_) {
        fmt::print(stderr, "Failed to write motion track: {}\n", outputPath_.string());
        return false;
    }

    ++nextTick_;
    return true;
}

bool MotionTrackRecorder::Finalize(const int tick, const ContainerPose& pose)
{
    if (!output_ || tick < nextTick_) {
        return !output_ ? true : tick == nextTick_ - 1;
    }
    return tick == nextTick_ && Record(tick, pose);
}

void MotionTrackRecorder::Close()
{
    if (output_.is_open()) {
        output_.close();
    }
}

std::filesystem::path MotionTrackRecorder::CreateOutputPath(
    const std::filesystem::path& directory)
{
    const std::time_t currentTime = std::time(nullptr);
    std::tm localTime{};
    if (localtime_s(&localTime, &currentTime) != 0) {
        fmt::print(stderr, "Failed to convert the current time for motion recording.\n");
        return directory / "recorded-time-unavailable.csv";
    }

    const std::string timestamp = fmt::format("{:%Y%m%d-%H%M%S}", localTime);
    std::filesystem::path outputPath = directory / fmt::format("recorded-{}.csv", timestamp);
    for (int suffix = 1; std::filesystem::exists(outputPath); ++suffix) {
        outputPath = directory / fmt::format("recorded-{}-{:02}.csv", timestamp, suffix);
    }
    return outputPath;
}

} // namespace flowama
