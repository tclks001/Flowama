#include "container.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace flowama {
namespace {
glm::vec3 AngularVelocityWorld(
    const glm::quat& previousOrientation,
    glm::quat currentOrientation,
    const float deltaSeconds)
{
    if (glm::dot(previousOrientation, currentOrientation) < 0.0F) {
        currentOrientation = -currentOrientation;
    }

    glm::quat delta = glm::normalize(
        currentOrientation * glm::conjugate(previousOrientation));
    if (delta.w < 0.0F) {
        delta = -delta;
    }

    const glm::vec3 imaginary{delta.x, delta.y, delta.z};
    const float imaginaryLength = glm::length(imaginary);
    if (imaginaryLength < 0.000001F) {
        return (2.0F / deltaSeconds) * imaginary;
    }

    const float halfAngle = std::atan2(imaginaryLength, glm::clamp(delta.w, -1.0F, 1.0F));
    return imaginary * ((2.0F * halfAngle) / (imaginaryLength * deltaSeconds));
}

} // namespace

void ContainerMotionEstimator::Reset(const ContainerPose& pose)
{
    previousOrientationContainerToWorld_ = glm::normalize(
        pose.orientationContainerToWorld);
    previousAngularVelocityWorld_ = glm::vec3{0.0F};
    initialized_ = true;
}

ContainerRotationalKinematics ContainerMotionEstimator::Update(
    const ContainerPose& pose,
    const float deltaSeconds)
{
    const glm::quat orientation = glm::normalize(pose.orientationContainerToWorld);
    if (!initialized_) {
        Reset(pose);
        return {};
    }

    const glm::vec3 angularVelocityWorld = AngularVelocityWorld(
        previousOrientationContainerToWorld_,
        orientation,
        deltaSeconds);
    const glm::vec3 angularAccelerationWorld = (
        angularVelocityWorld - previousAngularVelocityWorld_) / deltaSeconds;
    previousOrientationContainerToWorld_ = orientation;
    previousAngularVelocityWorld_ = angularVelocityWorld;

    const glm::quat worldToContainer = glm::inverse(orientation);
    return {
        worldToContainer * angularVelocityWorld,
        worldToContainer * angularAccelerationWorld,
    };
}

glm::mat4 PresentationCamera::ViewProjection(const int width, const int height) const
{
    const float aspectRatio = static_cast<float>(width)
        / static_cast<float>(std::max(height, 1));
    const glm::mat4 projection = glm::perspective(
        glm::radians(45.0F),
        aspectRatio,
        0.1F,
        100.0F);
    const glm::mat4 view = glm::lookAt(position_, glm::vec3{0.0F}, ScreenUp());
    return projection * view;
}

glm::vec3 PresentationCamera::Right() const
{
    return glm::normalize(glm::cross(Forward(), upHint_));
}

glm::vec3 PresentationCamera::ScreenUp() const
{
    return glm::normalize(glm::cross(Right(), Forward()));
}

glm::vec3 PresentationCamera::Forward() const
{
    return glm::normalize(-position_);
}

glm::mat4 ContainerToWorldMatrix(const ContainerPose& pose)
{
    return glm::translate(glm::mat4{1.0F}, pose.positionWorld)
        * glm::mat4_cast(pose.orientationContainerToWorld);
}

glm::vec3 LocalGravity(
    const ContainerPose& pose,
    const glm::vec3& displayGravityWorld)
{
    return glm::inverse(pose.orientationContainerToWorld) * displayGravityWorld;
}

glm::quat ScreenDragRotation(
    const PresentationCamera& camera,
    const float horizontalPixels,
    const float verticalPixels)
{
    constexpr float radiansPerPixel = 0.006F;
    const glm::quat horizontalRotation = glm::angleAxis(
        horizontalPixels * radiansPerPixel,
        camera.ScreenUp());
    const glm::quat verticalRotation = glm::angleAxis(
        verticalPixels * radiansPerPixel,
        camera.Right());
    return glm::normalize(verticalRotation * horizontalRotation);
}

} // namespace flowama
