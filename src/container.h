#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace flowama {

struct ContainerPose {
    glm::vec3 positionWorld{0.0F};
    glm::quat orientationContainerToWorld{1.0F, 0.0F, 0.0F, 0.0F};
};

struct ContainerRotationalKinematics {
    glm::vec3 angularVelocityLocal{0.0F};
    glm::vec3 angularAccelerationLocal{0.0F};
};

class ContainerMotionEstimator {
public:
    void Reset(const ContainerPose& pose);
    [[nodiscard]] ContainerRotationalKinematics Update(
        const ContainerPose& pose,
        float deltaSeconds);

private:
    glm::quat previousOrientationContainerToWorld_{1.0F, 0.0F, 0.0F, 0.0F};
    glm::vec3 previousAngularVelocityWorld_{0.0F};
    bool initialized_ = false;
};

class PresentationCamera {
public:
    [[nodiscard]] glm::mat4 ViewProjection(int width, int height) const;
    [[nodiscard]] glm::vec3 Right() const;
    [[nodiscard]] glm::vec3 ScreenUp() const;

private:
    [[nodiscard]] glm::vec3 Forward() const;

    glm::vec3 position_{4.8F, -9.0F, 4.5F};
    glm::vec3 upHint_{0.0F, 0.0F, 1.0F};
};

[[nodiscard]] glm::mat4 ContainerToWorldMatrix(const ContainerPose& pose);
[[nodiscard]] glm::vec3 LocalGravity(
    const ContainerPose& pose,
    const glm::vec3& displayGravityWorld);
[[nodiscard]] glm::quat ScreenDragRotation(
    const PresentationCamera& camera,
    float horizontalPixels,
    float verticalPixels);

} // namespace flowama
