#pragma once

namespace flowama {

struct InputTimelineVerificationResult {
    float endpointRotationErrorRadians = 0.0F;
    float idleRotationRadians = 0.0F;
    bool passed = false;
};

[[nodiscard]] InputTimelineVerificationResult VerifyRotationInputTimeline();

} // namespace flowama
