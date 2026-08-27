#include "input_timeline.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace flowama {

void RotationInputTimeline::BeginDrag(const double timestampSeconds)
{
    previousEventTimestampSeconds_ = timestampSeconds;
    dragActive_ = true;
}

void RotationInputTimeline::AppendRotation(
    const double timestampSeconds,
    const glm::quat& deltaRotationWorld)
{
    if (!dragActive_) {
        BeginDrag(timestampSeconds);
        return;
    }

    const double beginSeconds = previousEventTimestampSeconds_;
    double endSeconds = std::max(timestampSeconds, beginSeconds);
    previousEventTimestampSeconds_ = endSeconds;
    const glm::quat delta = glm::normalize(deltaRotationWorld);
    if (endSeconds == beginSeconds) {
        if (!segments_.empty() && segments_.back().endSeconds == beginSeconds) {
            segments_.back().deltaRotationWorld = glm::normalize(
                delta * segments_.back().deltaRotationWorld);
            return;
        }
        endSeconds = std::nextafter(beginSeconds, std::numeric_limits<double>::infinity());
        previousEventTimestampSeconds_ = endSeconds;
    }

    segments_.push_back({beginSeconds, endSeconds, delta});
}

void RotationInputTimeline::EndDrag()
{
    dragActive_ = false;
}

void RotationInputTimeline::Clear()
{
    segments_.clear();
    previousEventTimestampSeconds_ = 0.0;
    dragActive_ = false;
}

glm::quat RotationInputTimeline::ConsumeRotation(
    const double intervalBeginSeconds,
    const double intervalEndSeconds)
{
    glm::quat combinedRotation{1.0F, 0.0F, 0.0F, 0.0F};
    if (intervalEndSeconds <= intervalBeginSeconds) {
        return combinedRotation;
    }

    while (!segments_.empty() && segments_.front().endSeconds <= intervalBeginSeconds) {
        segments_.pop_front();
    }

    for (const RotationSegment& segment : segments_) {
        if (segment.beginSeconds >= intervalEndSeconds) {
            break;
        }

        const double overlapBegin = std::max(intervalBeginSeconds, segment.beginSeconds);
        const double overlapEnd = std::min(intervalEndSeconds, segment.endSeconds);
        if (overlapEnd <= overlapBegin) {
            continue;
        }

        const double segmentDuration = segment.endSeconds - segment.beginSeconds;
        const float fraction = static_cast<float>(
            (overlapEnd - overlapBegin) / segmentDuration);
        const glm::quat partialRotation = glm::slerp(
            glm::quat{1.0F, 0.0F, 0.0F, 0.0F},
            segment.deltaRotationWorld,
            fraction);
        combinedRotation = glm::normalize(partialRotation * combinedRotation);
    }

    while (!segments_.empty() && segments_.front().endSeconds <= intervalEndSeconds) {
        segments_.pop_front();
    }
    return combinedRotation;
}

} // namespace flowama
