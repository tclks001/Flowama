#include "inertial_frame_verification.h"

#include "container.h"
#include "simulation.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

namespace flowama {

InertialFrameVerificationResult VerifyFreeParticleWorldInertia()
{
    constexpr int tickCount = 480;
    constexpr float durationSeconds = tickCount * kFixedDeltaSeconds;
    constexpr float maximumAngle = 0.35F;
    constexpr float positionTolerance = 0.012F;
    constexpr float velocityTolerance = 0.018F;
    constexpr float angularVelocityTolerance = 0.0001F;
    constexpr float angularAccelerationTolerance = 0.002F;

    const glm::vec3 axis = glm::normalize(glm::vec3{0.2F, 0.9F, -0.3F});
    const glm::vec3 expectedWorldPosition{0.42F, -0.03F, 0.31F};
    glm::vec3 positionLocal = expectedWorldPosition;
    glm::vec3 velocityLocal{0.0F};

    InertialFrameVerificationResult result;
    for (int tick = 0; tick < tickCount; ++tick) {
        const float time = static_cast<float>(tick) * kFixedDeltaSeconds;
        const float phase = glm::two_pi<float>() * time / durationSeconds;
        const float angle = 0.5F * maximumAngle * (1.0F - std::cos(phase));
        const float angularSpeed = maximumAngle * glm::pi<float>()
            * std::sin(phase) / durationSeconds;
        const float angularAcceleration = 2.0F * maximumAngle
            * glm::pi<float>() * glm::pi<float>() * std::cos(phase)
            / (durationSeconds * durationSeconds);

        const SimulationForces forces{
            .gravityLocal = glm::vec3{0.0F},
            .angularVelocityLocal = axis * angularSpeed,
            .angularAccelerationLocal = axis * angularAcceleration,
        };
        velocityLocal += RotatingFrameAcceleration(
            positionLocal,
            velocityLocal,
            forces) * kFixedDeltaSeconds;
        positionLocal += velocityLocal * kFixedDeltaSeconds;

        const float nextTime = static_cast<float>(tick + 1) * kFixedDeltaSeconds;
        const float nextPhase = glm::two_pi<float>() * nextTime / durationSeconds;
        const float nextAngle = 0.5F * maximumAngle * (1.0F - std::cos(nextPhase));
        const float nextAngularSpeed = maximumAngle * glm::pi<float>()
            * std::sin(nextPhase) / durationSeconds;
        const glm::quat orientation = glm::angleAxis(nextAngle, axis);
        const glm::vec3 worldPosition = orientation * positionLocal;
        const glm::vec3 worldVelocity = orientation * (
            velocityLocal + glm::cross(axis * nextAngularSpeed, positionLocal));

        result.maximumWorldPositionError = std::max(
            result.maximumWorldPositionError,
            glm::length(worldPosition - expectedWorldPosition));
        result.maximumWorldVelocityError = std::max(
            result.maximumWorldVelocityError,
            glm::length(worldVelocity));
    }

    constexpr float constantAngularSpeed = 0.6F;
    ContainerMotionEstimator estimator;
    estimator.Reset({});
    for (int tick = 1; tick <= 96; ++tick) {
        const float angle = constantAngularSpeed
            * static_cast<float>(tick) * kFixedDeltaSeconds;
        ContainerPose pose;
        pose.orientationContainerToWorld = glm::angleAxis(angle, axis);
        const ContainerRotationalKinematics estimated = estimator.Update(
            pose,
            kFixedDeltaSeconds);

        result.maximumAngularVelocityError = std::max(
            result.maximumAngularVelocityError,
            glm::length(
                estimated.angularVelocityLocal - axis * constantAngularSpeed));
        if (tick > 1) {
            result.maximumAngularAccelerationError = std::max(
                result.maximumAngularAccelerationError,
                glm::length(estimated.angularAccelerationLocal));
        }
    }

    ContainerMotionEstimator stopEstimator;
    stopEstimator.Reset({});
    ContainerPose rotatingPose;
    rotatingPose.orientationContainerToWorld = glm::angleAxis(
        constantAngularSpeed * kFixedDeltaSeconds,
        axis);
    (void)stopEstimator.Update(rotatingPose, kFixedDeltaSeconds);
    const ContainerRotationalKinematics stopped = stopEstimator.Update(
        rotatingPose,
        kFixedDeltaSeconds);
    const ContainerRotationalKinematics settled = stopEstimator.Update(
        rotatingPose,
        kFixedDeltaSeconds);
    result.stoppedAngularSpeed = glm::length(stopped.angularVelocityLocal);
    result.settledAngularAcceleration = glm::length(settled.angularAccelerationLocal);

    result.passed = result.maximumWorldPositionError <= positionTolerance
        && result.maximumWorldVelocityError <= velocityTolerance
        && result.maximumAngularVelocityError <= angularVelocityTolerance
        && result.maximumAngularAccelerationError <= angularAccelerationTolerance
        && result.stoppedAngularSpeed <= angularVelocityTolerance
        && result.settledAngularAcceleration <= angularAccelerationTolerance;
    return result;
}

} // namespace flowama
