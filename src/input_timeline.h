#pragma once

#include <deque>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace flowama {

class RotationInputTimeline {
public:
    void BeginDrag(double timestampSeconds);
    void AppendRotation(double timestampSeconds, const glm::quat& deltaRotationWorld);
    void EndDrag();
    void Clear();

    [[nodiscard]] glm::quat ConsumeRotation(
        double intervalBeginSeconds,
        double intervalEndSeconds);

private:
    struct RotationSegment {
        double beginSeconds = 0.0;
        double endSeconds = 0.0;
        glm::quat deltaRotationWorld{1.0F, 0.0F, 0.0F, 0.0F};
    };

    std::deque<RotationSegment> segments_;
    double previousEventTimestampSeconds_ = 0.0;
    bool dragActive_ = false;
};

} // namespace flowama
