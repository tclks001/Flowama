#include <glad/glad.h>

#include <SDL3/SDL.h>

#include <fmt/format.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

constexpr float kFixedDeltaSeconds = 1.0F / 120.0F;
constexpr int kMaximumInteractiveSubsteps = 32;

struct CommandLineOptions {
    bool automation = false;
    int ticks = 0;
};

bool ParseCommandLine(
    const int argumentCount,
    char* arguments[],
    CommandLineOptions& options)
{
    for (int index = 1; index < argumentCount; ++index) {
        const std::string_view argument{arguments[index]};

        if (argument == "--automation") {
            options.automation = true;
            continue;
        }

        if (argument == "--ticks") {
            if (index + 1 >= argumentCount) {
                fmt::print(stderr, "--ticks requires a positive integer.\n");
                return false;
            }

            const std::string_view tickText{arguments[++index]};
            const auto [end, error] = std::from_chars(
                tickText.data(),
                tickText.data() + tickText.size(),
                options.ticks);

            if (error != std::errc{} || end != tickText.data() + tickText.size()
                || options.ticks <= 0) {
                fmt::print(stderr, "--ticks requires a positive integer.\n");
                return false;
            }

            continue;
        }

        fmt::print(stderr, "Unknown argument: {}\n", argument);
        return false;
    }

    if (options.automation && options.ticks == 0) {
        fmt::print(stderr, "--automation requires --ticks.\n");
        return false;
    }

    if (!options.automation && options.ticks != 0) {
        fmt::print(stderr, "--ticks is only valid with --automation.\n");
        return false;
    }

    return true;
}

glm::vec3 RotateVector(
    const glm::vec3& vector,
    const float radians,
    const glm::vec3& axis)
{
    return glm::vec3{
        glm::rotate(glm::mat4{1.0F}, radians, axis) * glm::vec4{vector, 0.0F}};
}

class OrbitCamera {
public:
    void Reset()
    {
        position_ = {4.8F, -9.0F, 4.5F};
        upHint_ = {0.0F, 0.0F, 1.0F};
    }

    void Rotate(const float horizontalPixels, const float verticalPixels)
    {
        constexpr float radiansPerPixel = 0.006F;

        const glm::vec3 currentUp = ScreenUp();
        position_ = RotateVector(
            position_,
            -horizontalPixels * radiansPerPixel,
            currentUp);
        upHint_ = RotateVector(
            upHint_,
            -horizontalPixels * radiansPerPixel,
            currentUp);

        const glm::vec3 right = Right();
        const glm::vec3 candidatePosition = RotateVector(
            position_,
            -verticalPixels * radiansPerPixel,
            right);
        const glm::vec3 candidateUp = RotateVector(
            upHint_,
            -verticalPixels * radiansPerPixel,
            right);

        const float forwardUpAlignment = std::abs(glm::dot(
            glm::normalize(-candidatePosition),
            glm::normalize(candidateUp)));
        if (forwardUpAlignment < 0.995F) {
            position_ = candidatePosition;
            upHint_ = candidateUp;
        }
    }

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

    [[nodiscard]] glm::vec3 Gravity() const
    {
        // This slice deliberately makes gravity screen-relative: visual down
        // is the camera's negative up direction, not a physical inertial model.
        return -ScreenUp() * 9.81F;
    }

private:
    [[nodiscard]] glm::vec3 Forward() const
    {
        return glm::normalize(-position_);
    }

    [[nodiscard]] glm::vec3 Right() const
    {
        return glm::normalize(glm::cross(Forward(), upHint_));
    }

    [[nodiscard]] glm::vec3 ScreenUp() const
    {
        return glm::normalize(glm::cross(Right(), Forward()));
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

} // namespace

int main(int argumentCount, char* arguments[])
{
    CommandLineOptions options;
    if (!ParseCommandLine(argumentCount, arguments, options)) {
        return 1;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fmt::print(stderr, "SDL_Init failed: {}\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_PROFILE_MASK,
        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    const Uint64 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
        | (options.automation ? SDL_WINDOW_HIDDEN : 0);
    SDL_Window* window = SDL_CreateWindow(
        "Flowama - RMB orbit, R reset",
        1280,
        720,
        windowFlags);

    if (window == nullptr) {
        fmt::print(stderr, "SDL_CreateWindow failed: {}\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (context == nullptr) {
        fmt::print(stderr, "SDL_GL_CreateContext failed: {}\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!gladLoadGLLoader(
            reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        fmt::print(stderr, "Failed to load OpenGL functions.\n");
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (!SDL_GL_SetSwapInterval(options.automation ? 0 : 1)) {
        fmt::print(stderr, "SDL_GL_SetSwapInterval failed: {}\n", SDL_GetError());
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    DebugRenderer renderer;
    if (!renderer.Create()) {
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_PROGRAM_POINT_SIZE);

    fmt::print(
        "OpenGL: {}\nRenderer: {}\n",
        reinterpret_cast<const char*>(glGetString(GL_VERSION)),
        reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    OrbitCamera camera;
    ParticleSimulation simulation;
    bool running = true;
    bool rightMouseDragging = false;
    int completedTicks = 0;
    bool automationCompleted = false;
    bool simulationFailed = false;
    float accumulatedTime = 0.0F;
    auto previousFrameTime = std::chrono::steady_clock::now();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                && event.button.button == SDL_BUTTON_RIGHT) {
                rightMouseDragging = true;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP
                && event.button.button == SDL_BUTTON_RIGHT) {
                rightMouseDragging = false;
            } else if (event.type == SDL_EVENT_MOUSE_MOTION && rightMouseDragging) {
                camera.Rotate(event.motion.xrel, event.motion.yrel);
            } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_R) {
                camera.Reset();
                simulation.Reset();
                accumulatedTime = 0.0F;
            }
        }

        if (!running) {
            break;
        }

        const auto currentFrameTime = std::chrono::steady_clock::now();
        const float frameDeltaSeconds = std::chrono::duration<float>(
            currentFrameTime - previousFrameTime)
                                            .count();
        previousFrameTime = currentFrameTime;

        if (options.automation) {
            simulation.Step(camera.Gravity());
            ++completedTicks;
        } else {
            accumulatedTime += std::min(frameDeltaSeconds, 0.25F);
            int substepCount = 0;
            while (accumulatedTime >= kFixedDeltaSeconds
                && substepCount < kMaximumInteractiveSubsteps) {
                simulation.Step(camera.Gravity());
                accumulatedTime -= kFixedDeltaSeconds;
                ++substepCount;
            }

            if (substepCount == kMaximumInteractiveSubsteps) {
                accumulatedTime = 0.0F;
            }
        }

        if (!simulation.IsInsideContainer()) {
            fmt::print(stderr, "Particle escaped the container.\n");
            simulationFailed = true;
            running = false;
            continue;
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        glViewport(0, 0, width, height);

        glClearColor(0.025F, 0.04F, 0.075F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const glm::mat4 viewProjection = camera.ViewProjection(width, height);
        renderer.DrawLines(
            viewProjection,
            BuildDebugLines(simulation.HalfExtents(), camera.Gravity()),
            {0.30F, 0.76F, 0.96F, 1.0F});
        renderer.DrawParticle(
            viewProjection,
            simulation.GetParticle().position,
            {1.0F, 0.72F, 0.22F, 1.0F});

        SDL_GL_SwapWindow(window);

        if (options.automation && completedTicks == options.ticks) {
            automationCompleted = true;
            running = false;
        }
    }

    const Particle finalParticle = simulation.GetParticle();
    renderer.Destroy();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (options.automation) {
        if (!automationCompleted || simulationFailed) {
            fmt::print(
                stderr,
                "Automation was interrupted after {} ticks.\n",
                completedTicks);
            return 1;
        }

        fmt::print(
            "Automation completed: {} fixed ticks; particle = ({:.3f}, {:.3f}, {:.3f})\n",
            completedTicks,
            finalParticle.position.x,
            finalParticle.position.y,
            finalParticle.position.z);
    }

    return simulationFailed ? 1 : 0;
}
