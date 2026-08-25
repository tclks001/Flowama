#include "debug_renderer.h"

#include <glad/glad.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <fmt/format.h>

namespace flowama {
namespace {

bool CompileShader(const GLenum type, const char* source, GLuint& shader)
{
    shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) {
        return true;
    }

    std::array<char, 1024> log{};
    GLsizei logLength = 0;
    glGetShaderInfoLog(
        shader,
        static_cast<GLsizei>(log.size()),
        &logLength,
        log.data());
    fmt::print(
        stderr,
        "OpenGL shader compilation failed: {}\n",
        std::string{log.data(), static_cast<std::size_t>(logLength)});
    glDeleteShader(shader);
    shader = 0;
    return false;
}

bool LinkProgram(const GLuint vertexShader, const GLuint fragmentShader, GLuint& program)
{
    program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE) {
        return true;
    }

    std::array<char, 1024> log{};
    GLsizei logLength = 0;
    glGetProgramInfoLog(
        program,
        static_cast<GLsizei>(log.size()),
        &logLength,
        log.data());
    fmt::print(
        stderr,
        "OpenGL program linking failed: {}\n",
        std::string{log.data(), static_cast<std::size_t>(logLength)});
    glDeleteProgram(program);
    program = 0;
    return false;
}

void WriteLittleEndian(std::ofstream& output, const std::uint32_t value, const int byteCount)
{
    for (int byteIndex = 0; byteIndex < byteCount; ++byteIndex) {
        output.put(static_cast<char>((value >> (byteIndex * 8)) & 0xFFU));
    }
}

} // namespace

bool DebugRenderer::Create()
{
    constexpr const char* vertexShaderSource = R"(
        #version 460 core

        layout(location = 0) in vec3 aPosition;

        uniform mat4 uViewProjection;
        uniform float uPointSize;

        void main()
        {
            gl_Position = uViewProjection * vec4(aPosition, 1.0);
            gl_PointSize = uPointSize;
        }
    )";
    constexpr const char* fragmentShaderSource = R"(
        #version 460 core

        uniform vec4 uColor;
        uniform int uRoundPoint;

        out vec4 outColor;

        void main()
        {
            if (uRoundPoint != 0) {
                const vec2 pointCoordinates = gl_PointCoord * 2.0 - 1.0;
                if (dot(pointCoordinates, pointCoordinates) > 1.0) {
                    discard;
                }
            }

            outColor = uColor;
        }
    )";

    GLuint vertexShader = 0;
    GLuint fragmentShader = 0;
    if (!CompileShader(GL_VERTEX_SHADER, vertexShaderSource, vertexShader)
        || !CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource, fragmentShader)
        || !LinkProgram(vertexShader, fragmentShader, program_)) {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    glGenVertexArrays(1, &vertexArray_);
    glGenBuffers(1, &vertexBuffer_);
    glBindVertexArray(vertexArray_);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        static_cast<GLsizei>(sizeof(glm::vec3)),
        nullptr);
    glEnableVertexAttribArray(0);

    viewProjectionLocation_ = glGetUniformLocation(program_, "uViewProjection");
    colorLocation_ = glGetUniformLocation(program_, "uColor");
    pointSizeLocation_ = glGetUniformLocation(program_, "uPointSize");
    roundPointLocation_ = glGetUniformLocation(program_, "uRoundPoint");
    return true;
}

void DebugRenderer::Destroy()
{
    if (vertexBuffer_ != 0) {
        glDeleteBuffers(1, &vertexBuffer_);
        vertexBuffer_ = 0;
    }
    if (vertexArray_ != 0) {
        glDeleteVertexArrays(1, &vertexArray_);
        vertexArray_ = 0;
    }
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

void DebugRenderer::DrawLines(
    const glm::mat4& viewProjection,
    const std::vector<glm::vec3>& vertices,
    const glm::vec4& color)
{
    Draw(viewProjection, vertices.data(), vertices.size(), color, 1.0F, false, GL_LINES);
}

void DebugRenderer::DrawParticles(
    const glm::mat4& viewProjection,
    const std::vector<glm::vec3>& positions,
    const glm::vec4& color)
{
    Draw(viewProjection, positions.data(), positions.size(), color, 3.5F, true, GL_POINTS);
}

void DebugRenderer::Draw(
    const glm::mat4& viewProjection,
    const glm::vec3* vertices,
    const std::size_t vertexCount,
    const glm::vec4& color,
    const float pointSize,
    const bool roundPoint,
    const unsigned int primitive)
{
    glUseProgram(program_);
    glUniformMatrix4fv(viewProjectionLocation_, 1, GL_FALSE, &viewProjection[0][0]);
    glUniform4fv(colorLocation_, 1, &color[0]);
    glUniform1f(pointSizeLocation_, pointSize);
    glUniform1i(roundPointLocation_, roundPoint ? 1 : 0);

    glBindVertexArray(vertexArray_);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertexCount * sizeof(glm::vec3)),
        vertices,
        GL_DYNAMIC_DRAW);
    glDrawArrays(primitive, 0, static_cast<GLsizei>(vertexCount));
}

std::vector<glm::vec3> BuildContainerDebugLines(
    const glm::vec3& halfExtents,
    const glm::vec3& gravity)
{
    const std::array<glm::vec3, 8> corners{{
        {-halfExtents.x, -halfExtents.y, -halfExtents.z},
        {halfExtents.x, -halfExtents.y, -halfExtents.z},
        {halfExtents.x, halfExtents.y, -halfExtents.z},
        {-halfExtents.x, halfExtents.y, -halfExtents.z},
        {-halfExtents.x, -halfExtents.y, halfExtents.z},
        {halfExtents.x, -halfExtents.y, halfExtents.z},
        {halfExtents.x, halfExtents.y, halfExtents.z},
        {-halfExtents.x, halfExtents.y, halfExtents.z},
    }};
    constexpr std::array<std::array<int, 2>, 12> edges{{
        {{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
        {{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
        {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
    }};

    std::vector<glm::vec3> lines;
    lines.reserve(edges.size() * 2 + 6);
    for (const auto& edge : edges) {
        lines.push_back(corners[edge[0]]);
        lines.push_back(corners[edge[1]]);
    }

    const glm::vec3 direction = glm::normalize(gravity);
    const glm::vec3 arrowStart{0.0F};
    const glm::vec3 arrowEnd = arrowStart + direction * 1.2F;
    const glm::vec3 helper = std::abs(direction.z) < 0.9F
        ? glm::vec3{0.0F, 0.0F, 1.0F}
        : glm::vec3{1.0F, 0.0F, 0.0F};
    const glm::vec3 arrowSide = glm::normalize(glm::cross(direction, helper));
    const glm::vec3 arrowBase = arrowEnd - direction * 0.25F;

    lines.push_back(arrowStart);
    lines.push_back(arrowEnd);
    lines.push_back(arrowEnd);
    lines.push_back(arrowBase + arrowSide * 0.14F);
    lines.push_back(arrowEnd);
    lines.push_back(arrowBase - arrowSide * 0.14F);
    return lines;
}

bool WriteBmpFrame(
    const int width,
    const int height,
    const std::filesystem::path& outputPath)
{
    if (width <= 0 || height <= 0) {
        fmt::print(stderr, "Cannot capture a {} x {} framebuffer.\n", width, height);
        return false;
    }

    const std::size_t rowBytes = static_cast<std::size_t>(width) * 3;
    std::vector<std::uint8_t> pixels(rowBytes * static_cast<std::size_t>(height));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        fmt::print(stderr, "OpenGL framebuffer readback failed with error 0x{:x}.\n", error);
        return false;
    }

    const std::size_t paddedRowBytes = (rowBytes + 3U) & ~std::size_t{3U};
    const std::size_t pixelDataSize = paddedRowBytes * static_cast<std::size_t>(height);
    constexpr std::uint32_t fileHeaderSize = 14;
    constexpr std::uint32_t infoHeaderSize = 40;
    const std::uint32_t pixelDataOffset = fileHeaderSize + infoHeaderSize;
    const std::uint32_t fileSize = pixelDataOffset + static_cast<std::uint32_t>(pixelDataSize);

    std::ofstream output(outputPath, std::ios::binary);
    if (!output) {
        fmt::print(stderr, "Failed to open capture output: {}\n", outputPath.string());
        return false;
    }

    output.put('B');
    output.put('M');
    WriteLittleEndian(output, fileSize, 4);
    WriteLittleEndian(output, 0, 4);
    WriteLittleEndian(output, pixelDataOffset, 4);
    WriteLittleEndian(output, infoHeaderSize, 4);
    WriteLittleEndian(output, static_cast<std::uint32_t>(width), 4);
    WriteLittleEndian(output, static_cast<std::uint32_t>(height), 4);
    WriteLittleEndian(output, 1, 2);
    WriteLittleEndian(output, 24, 2);
    WriteLittleEndian(output, 0, 4);
    WriteLittleEndian(output, static_cast<std::uint32_t>(pixelDataSize), 4);
    WriteLittleEndian(output, 0, 4);
    WriteLittleEndian(output, 0, 4);
    WriteLittleEndian(output, 0, 4);
    WriteLittleEndian(output, 0, 4);

    std::vector<std::uint8_t> bgrRow(paddedRowBytes, 0);
    for (int row = 0; row < height; ++row) {
        const std::size_t sourceOffset = static_cast<std::size_t>(row) * rowBytes;
        for (int column = 0; column < width; ++column) {
            const std::size_t sourcePixel = sourceOffset + static_cast<std::size_t>(column) * 3;
            const std::size_t destinationPixel = static_cast<std::size_t>(column) * 3;
            bgrRow[destinationPixel] = pixels[sourcePixel + 2];
            bgrRow[destinationPixel + 1] = pixels[sourcePixel + 1];
            bgrRow[destinationPixel + 2] = pixels[sourcePixel];
        }
        output.write(
            reinterpret_cast<const char*>(bgrRow.data()),
            static_cast<std::streamsize>(bgrRow.size()));
    }

    if (!output) {
        fmt::print(stderr, "Failed to write capture output: {}\n", outputPath.string());
        return false;
    }
    return true;
}

} // namespace flowama
