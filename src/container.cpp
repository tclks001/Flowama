#include "container.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

namespace flowama {

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

void RotateContainerFromScreenDrag(
    ContainerPose& pose,
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
    pose.orientationContainerToWorld = glm::normalize(
        verticalRotation * horizontalRotation * pose.orientationContainerToWorld);
}

} // namespace flowama
