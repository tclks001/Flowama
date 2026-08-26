#pragma once

namespace flowama {

struct InertialFrameVerificationResult {
    float maximumWorldPositionError = 0.0F;
    float maximumWorldVelocityError = 0.0F;
    float maximumAngularVelocityError = 0.0F;
    float maximumAngularAccelerationError = 0.0F;
    float stoppedAngularSpeed = 0.0F;
    float settledAngularAcceleration = 0.0F;
    bool passed = false;
};

[[nodiscard]] InertialFrameVerificationResult VerifyFreeParticleWorldInertia();

} // namespace flowama
