#pragma once

#include "container.h"

#include <filesystem>
#include <fstream>
#include <vector>

namespace flowama {

class MotionTrack {
public:
    [[nodiscard]] bool Load(const std::filesystem::path& inputPath);
    [[nodiscard]] bool HasSample(int tick) const;
    [[nodiscard]] const ContainerPose& Sample(int tick) const;

private:
    std::vector<ContainerPose> samples_;
};

class MotionTrackRecorder {
public:
    [[nodiscard]] bool Begin();
    [[nodiscard]] bool Record(int tick, const ContainerPose& pose);
    [[nodiscard]] bool Finalize(int tick, const ContainerPose& pose);
    void Close();

private:
    [[nodiscard]] static std::filesystem::path CreateOutputPath(
        const std::filesystem::path& directory);

    std::ofstream output_;
    std::filesystem::path outputPath_;
    int nextTick_ = 0;
};

} // namespace flowama
