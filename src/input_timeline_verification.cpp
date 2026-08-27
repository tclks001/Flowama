#include "input_timeline_verification.h"

#include "input_timeline.h"
#include "simulation.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

namespace flowama {
namespace {

float RotationAngle(const glm::quat& rotation)
{
    const glm::quat normalized = glm::normalize(rotation);
    return 2.0F * std::acos(glm::clamp(std::abs(normalized.w), 0.0F, 1.0F));
}

} // namespace

InputTimelineVerificationResult VerifyRotationInputTimeline()
{
    constexpr float toleranceRadians = 0.0001F;
    const glm::quat firstRotation = glm::angleAxis(
        0.48F,
        glm::normalize(glm::vec3{0.2F, 0.8F, -0.4F}));
    const glm::quat secondRotation = glm::angleAxis(
        0.31F,
        glm::normalize(glm::vec3{-0.7F, 0.1F, 0.6F}));

    RotationInputTimeline timeline;
    timeline.BeginDrag(0.0);
    timeline.AppendRotation(3.0 * kFixedDeltaSeconds, firstRotation);
    timeline.AppendRotation(5.0 * kFixedDeltaSeconds, secondRotation);

    glm::quat combinedRotation{1.0F, 0.0F, 0.0F, 0.0F};
    for (int tick = 0; tick < 5; ++tick) {
        const double begin = static_cast<double>(tick) * kFixedDeltaSeconds;
        const double end = static_cast<double>(tick + 1) * kFixedDeltaSeconds;
        combinedRotation = timeline.ConsumeRotation(begin, end) * combinedRotation;
        combinedRotation = glm::normalize(combinedRotation);
    }

    InputTimelineVerificationResult result;
    const glm::quat expectedRotation = glm::normalize(secondRotation * firstRotation);
    result.endpointRotationErrorRadians = RotationAngle(
        expectedRotation * glm::conjugate(combinedRotation));
    result.idleRotationRadians = RotationAngle(timeline.ConsumeRotation(
        5.0 * kFixedDeltaSeconds,
        6.0 * kFixedDeltaSeconds));
    result.passed = result.endpointRotationErrorRadians <= toleranceRadians
        && result.idleRotationRadians <= toleranceRadians;
    return result;
}

} // namespace flowama
