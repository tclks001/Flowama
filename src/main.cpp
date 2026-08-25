#include <glad/glad.h>

#include <SDL3/SDL.h>

#include <fmt/format.h>
#include <fmt/chrono.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr float kFixedDeltaSeconds = 1.0F / 120.0F;
constexpr int kMaximumInteractiveSubsteps = 32;

enum class RunMode {
    Interactive,
    Verify,
    Capture,
};

struct CommandLineOptions {
    RunMode mode = RunMode::Interactive;
    int ticks = 0;
    int captureEvery = 0;
    std::filesystem::path outputDirectory;
    std::filesystem::path motionTrackPath;
};

bool ParseCommandLine(
    const int argumentCount,
    char* arguments[],
    CommandLineOptions& options)
{
    bool modeSpecified = false;

    const auto selectMode = [&options, &modeSpecified](const RunMode mode) {
        if (modeSpecified) {
            fmt::print(stderr, "Only one run mode may be selected.\n");
            return false;
        }

        options.mode = mode;
        modeSpecified = true;
        return true;
    };

    const auto parsePositiveInteger = [](const std::string_view optionName,
                                          const std::string_view valueText,
                                          int& value) {
        const auto [end, error] = std::from_chars(
            valueText.data(),
            valueText.data() + valueText.size(),
            value);
        if (error != std::errc{} || end != valueText.data() + valueText.size()
            || value <= 0) {
            fmt::print(stderr, "{} requires a positive integer.\n", optionName);
            return false;
        }

        return true;
    };

    for (int index = 1; index < argumentCount; ++index) {
        const std::string_view argument{arguments[index]};

        if (argument == "--verify") {
            if (!selectMode(RunMode::Verify)) {
                return false;
            }
            continue;
        }

        if (argument == "--capture") {
            if (!selectMode(RunMode::Capture)) {
                return false;
            }
            continue;
        }

        if (argument == "--ticks") {
            if (index + 1 >= argumentCount) {
                fmt::print(stderr, "--ticks requires a positive integer.\n");
                return false;
            }

            const std::string_view tickText{arguments[++index]};
            if (!parsePositiveInteger("--ticks", tickText, options.ticks)) {
                return false;
            }

            continue;
        }

        if (argument == "--capture-every") {
            if (index + 1 >= argumentCount) {
                fmt::print(stderr, "--capture-every requires a positive integer.\n");
                return false;
            }

            const std::string_view captureEveryText{arguments[++index]};
            if (!parsePositiveInteger(
                    "--capture-every",
                    captureEveryText,
                    options.captureEvery)) {
                return false;
            }

            continue;
        }

        if (argument == "--output") {
            if (index + 1 >= argumentCount) {
                fmt::print(stderr, "--output requires a directory path.\n");
                return false;
            }

            options.outputDirectory = arguments[++index];
            if (options.outputDirectory.empty()) {
                fmt::print(stderr, "--output requires a directory path.\n");
                return false;
            }

            continue;
        }

        if (argument == "--motion-track") {
            if (index + 1 >= argumentCount) {
                fmt::print(stderr, "--motion-track requires a file path.\n");
                return false;
            }

            options.motionTrackPath = arguments[++index];
            if (options.motionTrackPath.empty()) {
                fmt::print(stderr, "--motion-track requires a file path.\n");
                return false;
            }

            continue;
        }

        fmt::print(stderr, "Unknown argument: {}\n", argument);
        return false;
    }

    if (options.mode == RunMode::Interactive
        && (options.ticks != 0 || options.captureEvery != 0
            || !options.outputDirectory.empty() || !options.motionTrackPath.empty())) {
        fmt::print(stderr, "--ticks, --capture-every, --output, and --motion-track require --verify or --capture.\n");
        return false;
    }

    if (options.mode != RunMode::Interactive && options.ticks == 0) {
        fmt::print(stderr, "--verify and --capture require --ticks.\n");
        return false;
    }

    if (options.mode != RunMode::Capture
        && (options.captureEvery != 0 || !options.outputDirectory.empty())) {
        fmt::print(stderr, "--capture-every and --output require --capture.\n");
        return false;
    }

    if (options.mode == RunMode::Capture
        && (options.captureEvery == 0 || options.outputDirectory.empty())) {
        fmt::print(stderr, "--capture requires --capture-every and --output.\n");
        return false;
    }

    return true;
}

struct ContainerPose {
    glm::vec3 positionWorld{0.0F};
    glm::quat orientationContainerToWorld{1.0F, 0.0F, 0.0F, 0.0F};
};

class MotionTrack {
public:
    [[nodiscard]] bool Load(const std::filesystem::path& inputPath)
    {
        std::ifstream input(inputPath);
        if (!input) {
            fmt::print(stderr, "Failed to open motion track: {}\n", inputPath.string());
            return false;
        }

        samples_.clear();
        std::string line;
        int expectedTick = 0;
        int lineNumber = 0;
        while (std::getline(input, line)) {
            ++lineNumber;
            if (line.empty() || line.front() == '#') {
                continue;
            }

            std::array<std::string_view, 8> fields{};
            std::string_view remaining{line};
            for (std::size_t fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex) {
                const std::size_t separator = remaining.find(',');
                if (separator == std::string_view::npos) {
                    if (fieldIndex + 1 != fields.size()) {
                        return ReportParseError(inputPath, lineNumber, "expected eight comma-separated values");
                    }
                    fields[fieldIndex] = remaining;
                    remaining = {};
                    continue;
                }

                fields[fieldIndex] = remaining.substr(0, separator);
                remaining.remove_prefix(separator + 1);
            }

            if (!remaining.empty()) {
                return ReportParseError(inputPath, lineNumber, "expected eight comma-separated values");
            }

            int tick = 0;
            if (!ParseInteger(fields[0], tick) || tick != expectedTick) {
                return ReportParseError(inputPath, lineNumber, "ticks must start at zero and be consecutive");
            }

            std::array<float, 7> values{};
            for (std::size_t valueIndex = 0; valueIndex < values.size(); ++valueIndex) {
                if (!ParseFloat(fields[valueIndex + 1], values[valueIndex])) {
                    return ReportParseError(inputPath, lineNumber, "position and quaternion values must be finite numbers");
                }
            }

            ContainerPose pose;
            pose.positionWorld = {values[0], values[1], values[2]};
            pose.orientationContainerToWorld = glm::quat{
                values[6], values[3], values[4], values[5]};
            const float orientationLength = glm::length(pose.orientationContainerToWorld);
            if (!std::isfinite(orientationLength) || orientationLength < 0.000001F) {
                return ReportParseError(inputPath, lineNumber, "quaternion must have non-zero length");
            }

            pose.orientationContainerToWorld /= orientationLength;
            samples_.push_back(pose);
            ++expectedTick;
        }

        if (!input.eof()) {
            fmt::print(stderr, "Failed while reading motion track: {}\n", inputPath.string());
            return false;
        }
        if (samples_.empty()) {
            fmt::print(stderr, "Motion track has no samples: {}\n", inputPath.string());
            return false;
        }

        return true;
    }

    [[nodiscard]] bool HasSample(const int tick) const
    {
        return tick >= 0 && static_cast<std::size_t>(tick) < samples_.size();
    }

    [[nodiscard]] const ContainerPose& Sample(const int tick) const
    {
        return samples_[static_cast<std::size_t>(tick)];
    }

private:
    static bool ReportParseError(
        const std::filesystem::path& inputPath,
        const int lineNumber,
        const std::string_view message)
    {
        fmt::print(
            stderr,
            "Invalid motion track {} at line {}: {}.\n",
            inputPath.string(),
            lineNumber,
            message);
        return false;
    }

    static bool ParseInteger(const std::string_view text, int& value)
    {
        const auto [end, error] = std::from_chars(
            text.data(), text.data() + text.size(), value);
        return error == std::errc{} && end == text.data() + text.size();
    }

    static bool ParseFloat(const std::string_view text, float& value)
    {
        const auto [end, error] = std::from_chars(
            text.data(), text.data() + text.size(), value);
        return error == std::errc{} && end == text.data() + text.size()
            && std::isfinite(value);
    }

    std::vector<ContainerPose> samples_;
};

class MotionTrackRecorder {
public:
    [[nodiscard]] bool Begin()
    {
        Close();

        std::error_code directoryError;
        const std::filesystem::path directory{"data/motion-tracks"};
        std::filesystem::create_directories(directory, directoryError);
        if (directoryError) {
            fmt::print(
                stderr,
                "Failed to create motion track directory '{}': {}\n",
                directory.string(),
                directoryError.message());
            return false;
        }

        outputPath_ = CreateOutputPath(directory);
        output_.open(outputPath_);
        if (!output_) {
            fmt::print(stderr, "Failed to create motion track: {}\n", outputPath_.string());
            return false;
        }

        output_ << "# Flowama container motion track\n"
                << "# tick,px,py,pz,qx,qy,qz,qw\n";
        nextTick_ = 0;
        fmt::print("Recording container motion to {}\n", outputPath_.string());
        return true;
    }

    [[nodiscard]] bool Record(const int tick, const ContainerPose& pose)
    {
        if (!output_) {
            return false;
        }
        if (tick != nextTick_) {
            fmt::print(stderr, "Motion recording tick sequence is invalid.\n");
            return false;
        }

        const glm::quat orientation = glm::normalize(pose.orientationContainerToWorld);
        output_ << fmt::format(
            "{},{:.9g},{:.9g},{:.9g},{:.9g},{:.9g},{:.9g},{:.9g}\n",
            tick,
            pose.positionWorld.x,
            pose.positionWorld.y,
            pose.positionWorld.z,
            orientation.x,
            orientation.y,
            orientation.z,
            orientation.w);
        if (!output_) {
            fmt::print(stderr, "Failed to write motion track: {}\n", outputPath_.string());
            return false;
        }

        ++nextTick_;
        return true;
    }

    [[nodiscard]] bool Finalize(const int tick, const ContainerPose& pose)
    {
        if (!output_ || tick < nextTick_) {
            return !output_ ? true : tick == nextTick_ - 1;
        }
        return tick == nextTick_ && Record(tick, pose);
    }

    void Close()
    {
        if (output_.is_open()) {
            output_.close();
        }
    }

private:
    static std::filesystem::path CreateOutputPath(const std::filesystem::path& directory)
    {
        const std::time_t currentTime = std::time(nullptr);
        std::tm localTime{};
        if (localtime_s(&localTime, &currentTime) != 0) {
            fmt::print(stderr, "Failed to convert the current time for motion recording.\n");
            return directory / "recorded-time-unavailable.csv";
        }
        const std::string timestamp = fmt::format(
            "{:%Y%m%d-%H%M%S}",
            localTime);
        std::filesystem::path outputPath = directory
            / fmt::format("recorded-{}.csv", timestamp);
        for (int suffix = 1; std::filesystem::exists(outputPath); ++suffix) {
            outputPath = directory / fmt::format("recorded-{}-{:02}.csv", timestamp, suffix);
        }
        return outputPath;
    }

    std::ofstream output_;
    std::filesystem::path outputPath_;
    int nextTick_ = 0;
};

class PresentationCamera {
public:
    [[nodiscard]] glm::mat4 ViewProjection(
        const int width,
        const int height) const
    {
        const float aspectRatio = static_cast<float>(width)
            / static_cast<float>(std::max(height, 1));
        const glm::mat4 projection = glm::perspective(
            glm::radians(45.0F),
            aspectRatio,
            0.1F,
            100.0F);
        const glm::mat4 view = glm::lookAt(
            position_,
            glm::vec3{0.0F},
            ScreenUp());
        return projection * view;
    }

    [[nodiscard]] glm::vec3 Right() const
    {
        return glm::normalize(glm::cross(Forward(), upHint_));
    }

    [[nodiscard]] glm::vec3 ScreenUp() const
    {
        return glm::normalize(glm::cross(Right(), Forward()));
    }

private:
    [[nodiscard]] glm::vec3 Forward() const
    {
        return glm::normalize(-position_);
    }

    glm::vec3 position_{4.8F, -9.0F, 4.5F};
    glm::vec3 upHint_{0.0F, 0.0F, 1.0F};
};

struct Particle {
    glm::vec3 position{0.0F, 0.0F, 1.8F};
    glm::vec3 velocity{0.0F};
};

class ParticleSimulation {
public:
    void Reset()
    {
        particle_ = {};
    }

    void Step(const glm::vec3& gravity)
    {
        particle_.velocity += gravity * kFixedDeltaSeconds;
        particle_.velocity *= std::exp(-fluidDragRate_ * kFixedDeltaSeconds);
        particle_.position += particle_.velocity * kFixedDeltaSeconds;

        ResolveWall(0, halfExtents_.x - particleRadius_, true);
        ResolveWall(0, -halfExtents_.x + particleRadius_, false);
        ResolveWall(1, halfExtents_.y - particleRadius_, true);
        ResolveWall(1, -halfExtents_.y + particleRadius_, false);
        ResolveWall(2, halfExtents_.z - particleRadius_, true);
        ResolveWall(2, -halfExtents_.z + particleRadius_, false);
    }

    [[nodiscard]] const Particle& GetParticle() const
    {
        return particle_;
    }

    [[nodiscard]] const glm::vec3& HalfExtents() const
    {
        return halfExtents_;
    }

    [[nodiscard]] bool IsInsideContainer() const
    {
        const glm::vec3 allowedExtents = halfExtents_ - particleRadius_;
        return glm::all(glm::lessThanEqual(
            glm::abs(particle_.position),
            allowedExtents + 0.0001F));
    }

private:
    void ResolveWall(const int axis, const float limit, const bool upperWall)
    {
        const bool crossedWall = upperWall ? particle_.position[axis] > limit
                                           : particle_.position[axis] < limit;
        if (!crossedWall) {
            return;
        }

        particle_.position[axis] = limit;

        glm::vec3 inwardNormal{0.0F};
        inwardNormal[axis] = upperWall ? -1.0F : 1.0F;
        const float inwardSpeed = glm::dot(particle_.velocity, inwardNormal);
        if (inwardSpeed >= 0.0F) {
            return;
        }

        particle_.velocity -= (1.0F + restitution_) * inwardSpeed * inwardNormal;

        const glm::vec3 normalVelocity = glm::dot(
            particle_.velocity,
            inwardNormal)
            * inwardNormal;
        const glm::vec3 tangentialVelocity = particle_.velocity - normalVelocity;
        particle_.velocity = normalVelocity + tangentialVelocity
            * std::exp(-wallTangentialDragRate_ * kFixedDeltaSeconds);
    }

    Particle particle_;
    glm::vec3 halfExtents_{1.8F, 0.1F, 3.0F};
    float particleRadius_ = 0.08F;
    float fluidDragRate_ = 2.5F;
    float wallTangentialDragRate_ = 0.8F;
    float restitution_ = 0.02F;
};

bool CompileShader(
    const GLenum type,
    const char* source,
    GLuint& shader)
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

class DebugRenderer {
public:
    [[nodiscard]] bool Create()
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

    void Destroy()
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

    void DrawLines(
        const glm::mat4& viewProjection,
        const std::vector<glm::vec3>& vertices,
        const glm::vec4& color)
    {
        Draw(
            viewProjection,
            vertices.data(),
            vertices.size(),
            color,
            1.0F,
            false,
            GL_LINES);
    }

    void DrawParticle(
        const glm::mat4& viewProjection,
        const glm::vec3& position,
        const glm::vec4& color)
    {
        Draw(viewProjection, &position, 1, color, 20.0F, true, GL_POINTS);
    }

private:
    void Draw(
        const glm::mat4& viewProjection,
        const glm::vec3* vertices,
        const std::size_t vertexCount,
        const glm::vec4& color,
        const float pointSize,
        const bool roundPoint,
        const GLenum primitive)
    {
        glUseProgram(program_);
        glUniformMatrix4fv(
            viewProjectionLocation_,
            1,
            GL_FALSE,
            &viewProjection[0][0]);
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

    GLuint program_ = 0;
    GLuint vertexArray_ = 0;
    GLuint vertexBuffer_ = 0;
    GLint viewProjectionLocation_ = -1;
    GLint colorLocation_ = -1;
    GLint pointSizeLocation_ = -1;
    GLint roundPointLocation_ = -1;
};

std::vector<glm::vec3> BuildDebugLines(
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

struct Runtime {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    DebugRenderer renderer;
    PresentationCamera camera;
    ParticleSimulation simulation;
    ContainerPose containerPose;
    glm::vec3 displayGravityWorld{0.0F};
    const MotionTrack* motionTrack = nullptr;
    MotionTrackRecorder motionRecorder;
    bool sdlInitialized = false;
    bool recordsMotion = false;
    bool leftMouseDragging = false;
    int completedTicks = 0;
};

void ShutdownRuntime(Runtime& runtime)
{
    if (runtime.recordsMotion) {
        (void)runtime.motionRecorder.Finalize(runtime.completedTicks, runtime.containerPose);
    }
    runtime.motionRecorder.Close();
    if (runtime.context != nullptr) {
        runtime.renderer.Destroy();
        SDL_GL_DestroyContext(runtime.context);
        runtime.context = nullptr;
    }
    if (runtime.window != nullptr) {
        SDL_DestroyWindow(runtime.window);
        runtime.window = nullptr;
    }
    if (runtime.sdlInitialized) {
        SDL_Quit();
        runtime.sdlInitialized = false;
    }
}

bool InitializeRuntime(
    Runtime& runtime,
    const bool hiddenWindow,
    const ContainerPose& initialContainerPose)
{
    runtime.containerPose = initialContainerPose;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fmt::print(stderr, "SDL_Init failed: {}\n", SDL_GetError());
        return false;
    }
    runtime.sdlInitialized = true;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_PROFILE_MASK,
        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    const Uint64 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
        | (hiddenWindow ? SDL_WINDOW_HIDDEN : 0);
    runtime.window = SDL_CreateWindow(
        "Flowama - LMB tilt, R reset",
        1280,
        720,
        windowFlags);
    if (runtime.window == nullptr) {
        fmt::print(stderr, "SDL_CreateWindow failed: {}\n", SDL_GetError());
        ShutdownRuntime(runtime);
        return false;
    }

    runtime.context = SDL_GL_CreateContext(runtime.window);
    if (runtime.context == nullptr) {
        fmt::print(stderr, "SDL_GL_CreateContext failed: {}\n", SDL_GetError());
        ShutdownRuntime(runtime);
        return false;
    }

    if (!gladLoadGLLoader(
            reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        fmt::print(stderr, "Failed to load OpenGL functions.\n");
        ShutdownRuntime(runtime);
        return false;
    }

    if (!SDL_GL_SetSwapInterval(hiddenWindow ? 0 : 1)) {
        fmt::print(stderr, "SDL_GL_SetSwapInterval failed: {}\n", SDL_GetError());
        ShutdownRuntime(runtime);
        return false;
    }

    if (!runtime.renderer.Create()) {
        ShutdownRuntime(runtime);
        return false;
    }

    if (!hiddenWindow) {
        if (!runtime.motionRecorder.Begin()) {
            ShutdownRuntime(runtime);
            return false;
        }
        runtime.recordsMotion = true;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_PROGRAM_POINT_SIZE);
    runtime.displayGravityWorld = -runtime.camera.ScreenUp() * 9.81F;

    fmt::print(
        "OpenGL: {}\nRenderer: {}\n",
        reinterpret_cast<const char*>(glGetString(GL_VERSION)),
        reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    return true;
}

[[nodiscard]] glm::mat4 ContainerToWorldMatrix(const ContainerPose& pose)
{
    return glm::translate(glm::mat4{1.0F}, pose.positionWorld)
        * glm::mat4_cast(pose.orientationContainerToWorld);
}

[[nodiscard]] glm::vec3 LocalGravity(
    const ContainerPose& pose,
    const glm::vec3& displayGravityWorld)
{
    return glm::inverse(pose.orientationContainerToWorld) * displayGravityWorld;
}

void RotateContainerFromScreenDrag(
    Runtime& runtime,
    const float horizontalPixels,
    const float verticalPixels)
{
    constexpr float radiansPerPixel = 0.006F;
    const glm::quat horizontalRotation = glm::angleAxis(
        horizontalPixels * radiansPerPixel,
        runtime.camera.ScreenUp());
    const glm::quat verticalRotation = glm::angleAxis(
        verticalPixels * radiansPerPixel,
        runtime.camera.Right());
    runtime.containerPose.orientationContainerToWorld = glm::normalize(
        verticalRotation * horizontalRotation
        * runtime.containerPose.orientationContainerToWorld);
}

bool ApplyMotionTrackSample(Runtime& runtime, const int tick)
{
    if (runtime.motionTrack == nullptr) {
        return true;
    }
    if (!runtime.motionTrack->HasSample(tick)) {
        fmt::print(stderr, "Motion track does not contain tick {}.\n", tick);
        return false;
    }

    runtime.containerPose = runtime.motionTrack->Sample(tick);
    return true;
}

bool AdvanceFixedStep(Runtime& runtime)
{
    if (!ApplyMotionTrackSample(runtime, runtime.completedTicks)) {
        return false;
    }
    if (runtime.recordsMotion
        && !runtime.motionRecorder.Record(runtime.completedTicks, runtime.containerPose)) {
        return false;
    }

    runtime.simulation.Step(LocalGravity(
        runtime.containerPose,
        runtime.displayGravityWorld));
    if (runtime.simulation.IsInsideContainer()) {
        ++runtime.completedTicks;
        return ApplyMotionTrackSample(runtime, runtime.completedTicks);
    }

    fmt::print(stderr, "Particle escaped the container.\n");
    return false;
}

bool RenderFrame(Runtime& runtime)
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(runtime.window, &width, &height);
    glViewport(0, 0, width, height);

    glClearColor(0.025F, 0.04F, 0.075F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 viewProjection = runtime.camera.ViewProjection(width, height);
    const glm::mat4 modelViewProjection = viewProjection
        * ContainerToWorldMatrix(runtime.containerPose);
    runtime.renderer.DrawLines(
        modelViewProjection,
        BuildDebugLines(
            runtime.simulation.HalfExtents(),
            LocalGravity(runtime.containerPose, runtime.displayGravityWorld)),
        {0.30F, 0.76F, 0.96F, 1.0F});
    runtime.renderer.DrawParticle(
        modelViewProjection,
        runtime.simulation.GetParticle().position,
        {1.0F, 0.72F, 0.22F, 1.0F});

    const GLenum error = glGetError();
    if (error == GL_NO_ERROR) {
        return true;
    }

    fmt::print(stderr, "OpenGL rendering failed with error 0x{:x}.\n", error);
    return false;
}

void WriteLittleEndian(
    std::ofstream& output,
    const std::uint32_t value,
    const int byteCount)
{
    for (int byteIndex = 0; byteIndex < byteCount; ++byteIndex) {
        output.put(static_cast<char>((value >> (byteIndex * 8)) & 0xFFU));
    }
}

bool WriteBmpFrame(Runtime& runtime, const std::filesystem::path& outputPath)
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(runtime.window, &width, &height);
    if (width <= 0 || height <= 0) {
        fmt::print(stderr, "Cannot capture a {} x {} framebuffer.\n", width, height);
        return false;
    }

    const std::size_t rowBytes = static_cast<std::size_t>(width) * 3;
    std::vector<std::uint8_t> pixels(
        rowBytes * static_cast<std::size_t>(height));

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(
        0,
        0,
        width,
        height,
        GL_RGB,
        GL_UNSIGNED_BYTE,
        pixels.data());

    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        fmt::print(stderr, "OpenGL framebuffer readback failed with error 0x{:x}.\n", error);
        return false;
    }

    const std::size_t unpaddedRowBytes = static_cast<std::size_t>(width) * 3;
    const std::size_t paddedRowBytes = (unpaddedRowBytes + 3U) & ~std::size_t{3U};
    const std::size_t pixelDataSize = paddedRowBytes * static_cast<std::size_t>(height);
    constexpr std::uint32_t fileHeaderSize = 14;
    constexpr std::uint32_t infoHeaderSize = 40;
    const std::uint32_t pixelDataOffset = fileHeaderSize + infoHeaderSize;
    const std::uint32_t fileSize = pixelDataOffset
        + static_cast<std::uint32_t>(pixelDataSize);

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
            const std::size_t sourcePixel = sourceOffset
                + static_cast<std::size_t>(column) * 3;
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

bool ProcessInteractiveEvents(Runtime& runtime)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            return false;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
            && event.button.button == SDL_BUTTON_LEFT) {
            runtime.leftMouseDragging = true;
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP
            && event.button.button == SDL_BUTTON_LEFT) {
            runtime.leftMouseDragging = false;
        } else if (event.type == SDL_EVENT_MOUSE_MOTION && runtime.leftMouseDragging) {
            RotateContainerFromScreenDrag(runtime, event.motion.xrel, event.motion.yrel);
        } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_R) {
            if (!runtime.motionRecorder.Finalize(
                    runtime.completedTicks,
                    runtime.containerPose)) {
                return false;
            }
            runtime.simulation.Reset();
            runtime.containerPose = {};
            runtime.completedTicks = 0;
            if (!runtime.motionRecorder.Begin()) {
                return false;
            }
        }
    }

    return true;
}

template <typename AfterTick>
bool RunFixedTicks(Runtime& runtime, const int tickCount, AfterTick&& afterTick)
{
    for (int completedTicks = 1; completedTicks <= tickCount; ++completedTicks) {
        if (!AdvanceFixedStep(runtime) || !afterTick(completedTicks)) {
            return false;
        }
    }

    return true;
}

bool RunInteractive(Runtime& runtime)
{
    float accumulatedTime = 0.0F;
    auto previousFrameTime = std::chrono::steady_clock::now();

    while (ProcessInteractiveEvents(runtime)) {
        const auto currentFrameTime = std::chrono::steady_clock::now();
        const float frameDeltaSeconds = std::chrono::duration<float>(
            currentFrameTime - previousFrameTime)
                                            .count();
        previousFrameTime = currentFrameTime;

        accumulatedTime += std::min(frameDeltaSeconds, 0.25F);
        int substepCount = 0;
        while (accumulatedTime >= kFixedDeltaSeconds
            && substepCount < kMaximumInteractiveSubsteps) {
            if (!AdvanceFixedStep(runtime)) {
                return false;
            }
            accumulatedTime -= kFixedDeltaSeconds;
            ++substepCount;
        }

        if (substepCount == kMaximumInteractiveSubsteps) {
            accumulatedTime = 0.0F;
        }

        if (!RenderFrame(runtime)) {
            return false;
        }
        SDL_GL_SwapWindow(runtime.window);
    }

    return true;
}

bool RunVerification(Runtime& runtime, const int tickCount)
{
    if (!RenderFrame(runtime)) {
        return false;
    }

    if (!RunFixedTicks(runtime, tickCount, [](const int) { return true; })) {
        return false;
    }

    const Particle& particle = runtime.simulation.GetParticle();
    fmt::print(
        "Verification completed: {} fixed ticks; particle = ({:.3f}, {:.3f}, {:.3f})\n",
        tickCount,
        particle.position.x,
        particle.position.y,
        particle.position.z);
    return true;
}

bool RunCapture(Runtime& runtime, const CommandLineOptions& options)
{
    std::error_code directoryError;
    std::filesystem::create_directories(options.outputDirectory, directoryError);
    if (directoryError) {
        fmt::print(
            stderr,
            "Failed to create capture directory '{}': {}\n",
            options.outputDirectory.string(),
            directoryError.message());
        return false;
    }

    int capturedFrames = 0;
    const auto captureTick = [&runtime, &options, &capturedFrames](const int tick) {
        if (!RenderFrame(runtime)) {
            return false;
        }

        const std::filesystem::path outputPath = options.outputDirectory
            / fmt::format("tick_{:06}.bmp", tick);
        if (!WriteBmpFrame(runtime, outputPath)) {
            return false;
        }

        ++capturedFrames;
        return true;
    };

    if (!captureTick(0)) {
        return false;
    }

    if (!RunFixedTicks(runtime, options.ticks, [&captureTick, &options](const int tick) {
            return tick % options.captureEvery != 0 || captureTick(tick);
        })) {
        return false;
    }

    fmt::print(
        "Capture completed: {} fixed ticks, {} BMP frames in {}\n",
        options.ticks,
        capturedFrames,
        options.outputDirectory.string());
    return true;
}

} // namespace

int main(int argumentCount, char* arguments[])
{
    CommandLineOptions options;
    if (!ParseCommandLine(argumentCount, arguments, options)) {
        return 1;
    }

    MotionTrack motionTrack;
    const MotionTrack* selectedMotionTrack = nullptr;
    ContainerPose initialContainerPose;
    if (!options.motionTrackPath.empty()) {
        if (!motionTrack.Load(options.motionTrackPath)) {
            return 1;
        }
        if (!motionTrack.HasSample(options.ticks)) {
            fmt::print(
                stderr,
                "Motion track must contain samples from tick 0 through tick {}.\n",
                options.ticks);
            return 1;
        }

        selectedMotionTrack = &motionTrack;
        initialContainerPose = motionTrack.Sample(0);
    }

    Runtime runtime;
    runtime.motionTrack = selectedMotionTrack;
    const bool hiddenWindow = options.mode != RunMode::Interactive;
    if (!InitializeRuntime(runtime, hiddenWindow, initialContainerPose)) {
        return 1;
    }

    bool succeeded = false;
    switch (options.mode) {
    case RunMode::Interactive:
        succeeded = RunInteractive(runtime);
        break;
    case RunMode::Verify:
        succeeded = RunVerification(runtime, options.ticks);
        break;
    case RunMode::Capture:
        succeeded = RunCapture(runtime, options);
        break;
    }

    ShutdownRuntime(runtime);
    return succeeded ? 0 : 1;
}
