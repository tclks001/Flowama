#pragma once

#include <filesystem>
#include <vector>

#include <glm/glm.hpp>

namespace flowama {

class DebugRenderer {
public:
    [[nodiscard]] bool Create();
    void Destroy();
    void DrawLines(
        const glm::mat4& viewProjection,
        const std::vector<glm::vec3>& vertices,
        const glm::vec4& color);
    void DrawParticles(
        const glm::mat4& viewProjection,
        const std::vector<glm::vec3>& positions,
        const glm::vec4& color);

private:
    void Draw(
        const glm::mat4& viewProjection,
        const glm::vec3* vertices,
        std::size_t vertexCount,
        const glm::vec4& color,
        float pointSize,
        bool roundPoint,
        unsigned int primitive);

    unsigned int program_ = 0;
    unsigned int vertexArray_ = 0;
    unsigned int vertexBuffer_ = 0;
    int viewProjectionLocation_ = -1;
    int colorLocation_ = -1;
    int pointSizeLocation_ = -1;
    int roundPointLocation_ = -1;
};

[[nodiscard]] std::vector<glm::vec3> BuildContainerDebugLines(
    const glm::vec3& halfExtents,
    const glm::vec3& gravity);
[[nodiscard]] bool WriteBmpFrame(
    int width,
    int height,
    const std::filesystem::path& outputPath);

} // namespace flowama
