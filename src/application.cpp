#include "application.h"

#include "container.h"
#include "debug_renderer.h"
#include "motion_track.h"
#include "simulation.h"

#include <glad/glad.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string_view>
#include <system_error>

#include <fmt/format.h>

namespace flowama {
namespace {

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
    int profileStartTick = 0;
    bool profile = false;
    bool profileStartTickSpecified = false;
    std::filesystem::path outputDirectory;
    std::filesystem::path motionTrackPath;
};

struct PerformanceSummary {
    std::size_t fixedStepCount = 0;
    double fixedStepMilliseconds = 0.0;
    double predictionMilliseconds = 0.0;
    double wallConstraintMilliseconds = 0.0;
    double gridBuildMilliseconds = 0.0;
    double particleContactMilliseconds = 0.0;
    std::size_t candidatePairCount = 0;
    std::size_t maximumParticlesPerCell = 0;
    double maximumFixedStepMilliseconds = 0.0;
    int slowestCompletedTick = 0;
    SimulationPerformanceMetrics slowestTickMetrics;

    void Add(
        const int completedTick,
        const SimulationPerformanceMetrics& metrics,
        const double fixedStepElapsedMilliseconds)
    {
        ++fixedStepCount;
        fixedStepMilliseconds += fixedStepElapsedMilliseconds;
        predictionMilliseconds += metrics.predictionMilliseconds;
        wallConstraintMilliseconds += metrics.wallConstraintMilliseconds;
        gridBuildMilliseconds += metrics.gridBuildMilliseconds;
        particleContactMilliseconds += metrics.particleContactMilliseconds;
        candidatePairCount += metrics.candidatePairCount;
        maximumParticlesPerCell = std::max(
            maximumParticlesPerCell,
            metrics.maximumParticlesPerCell);
        if (fixedStepElapsedMilliseconds > maximumFixedStepMilliseconds) {
            maximumFixedStepMilliseconds = fixedStepElapsedMilliseconds;
            slowestCompletedTick = completedTick;
            slowestTickMetrics = metrics;
        }
    }
};

struct Runtime {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    DebugRenderer renderer;
    PresentationCamera camera;
    GranularSimulation simulation;
    ContainerPose containerPose;
    glm::vec3 displayGravityWorld{0.0F};
    const MotionTrack* motionTrack = nullptr;
    MotionTrackRecorder motionRecorder;
    bool sdlInitialized = false;
    bool recordsMotion = false;
    bool leftMouseDragging = false;
    bool performanceProfilingEnabled = false;
    int performanceProfileStartTick = 0;
    int completedTicks = 0;
    PerformanceSummary performanceSummary;
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
            valueText.data(), valueText.data() + valueText.size(), value);
        if (error != std::errc{} || end != valueText.data() + valueText.size() || value <= 0) {
            fmt::print(stderr, "{} requires a positive integer.\n", optionName);
            return false;
        }
        return true;
    };
    const auto parseNonNegativeInteger = [](const std::string_view optionName,
                                             const std::string_view valueText,
                                             int& value) {
        const auto [end, error] = std::from_chars(
            valueText.data(), valueText.data() + valueText.size(), value);
        if (error != std::errc{} || end != valueText.data() + valueText.size() || value < 0) {
            fmt::print(stderr, "{} requires a non-negative integer.\n", optionName);
            return false;
        }
        return true;
    };

    for (int index = 1; index < argumentCount; ++index) {
        const std::string_view argument{arguments[index]};
        if (argument == "--profile") {
            if (options.profile) {
                fmt::print(stderr, "--profile may only be specified once.\n");
                return false;
            }
            options.profile = true;
        } else if (argument == "--verify") {
            if (!selectMode(RunMode::Verify)) {
                return false;
            }
        } else if (argument == "--capture") {
            if (!selectMode(RunMode::Capture)) {
                return false;
            }
        } else if (argument == "--ticks" || argument == "--capture-every") {
            if (index + 1 >= argumentCount) {
                fmt::print(stderr, "{} requires a positive integer.\n", argument);
                return false;
            }
            int& value = argument == "--ticks" ? options.ticks : options.captureEvery;
            if (!parsePositiveInteger(argument, arguments[++index], value)) {
                return false;
            }
        } else if (argument == "--profile-start-tick") {
            if (index + 1 >= argumentCount) {
                fmt::print(stderr, "--profile-start-tick requires a non-negative integer.\n");
                return false;
            }
            if (!parseNonNegativeInteger(
                    argument,
                    arguments[++index],
                    options.profileStartTick)) {
                return false;
            }
            options.profileStartTickSpecified = true;
        } else if (argument == "--output" || argument == "--motion-track") {
            if (index + 1 >= argumentCount) {
                fmt::print(stderr, "{} requires a path.\n", argument);
                return false;
            }
            std::filesystem::path& value = argument == "--output"
                ? options.outputDirectory
                : options.motionTrackPath;
            value = arguments[++index];
            if (value.empty()) {
                fmt::print(stderr, "{} requires a path.\n", argument);
                return false;
            }
        } else {
            fmt::print(stderr, "Unknown argument: {}\n", argument);
            return false;
        }
    }

    if (options.mode == RunMode::Interactive
        && (options.ticks != 0 || options.captureEvery != 0
            || !options.outputDirectory.empty() || !options.motionTrackPath.empty())) {
        fmt::print(stderr, "Automation arguments require --verify or --capture.\n");
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
    if (options.profile && options.mode != RunMode::Verify) {
        fmt::print(stderr, "--profile requires --verify.\n");
        return false;
    }
    if (options.profileStartTickSpecified && !options.profile) {
        fmt::print(stderr, "--profile-start-tick requires --profile.\n");
        return false;
    }
    if (options.profile && options.profileStartTick >= options.ticks) {
        fmt::print(stderr, "--profile-start-tick must be smaller than --ticks.\n");
        return false;
    }
    return true;
}

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
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    const Uint64 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
        | (hiddenWindow ? SDL_WINDOW_HIDDEN : 0);
    runtime.window = SDL_CreateWindow("Flowama - LMB tilt, R reset", 1280, 720, windowFlags);
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
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
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

    const bool profileThisStep = runtime.performanceProfilingEnabled
        && runtime.completedTicks >= runtime.performanceProfileStartTick;
    SimulationPerformanceMetrics simulationMetrics;
    const auto fixedStepStart = profileThisStep
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    runtime.simulation.Step(
        LocalGravity(runtime.containerPose, runtime.displayGravityWorld),
        profileThisStep ? &simulationMetrics : nullptr);
    if (profileThisStep) {
        runtime.performanceSummary.Add(
            runtime.completedTicks + 1,
            simulationMetrics,
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - fixedStepStart).count());
    }
    if (!runtime.simulation.IsValidState()) {
        fmt::print(stderr, "Granular simulation produced an invalid particle state.\n");
        return false;
    }

    ++runtime.completedTicks;
    return ApplyMotionTrackSample(runtime, runtime.completedTicks);
}

void FramebufferSize(Runtime& runtime, int& width, int& height)
{
    SDL_GetWindowSizeInPixels(runtime.window, &width, &height);
}

bool RenderFrame(Runtime& runtime)
{
    int width = 0;
    int height = 0;
    FramebufferSize(runtime, width, height);
    glViewport(0, 0, width, height);
    glClearColor(0.025F, 0.04F, 0.075F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const glm::mat4 viewProjection = runtime.camera.ViewProjection(width, height);
    const glm::mat4 modelViewProjection = viewProjection
        * ContainerToWorldMatrix(runtime.containerPose);
    runtime.renderer.DrawLines(
        modelViewProjection,
        BuildContainerDebugLines(
            runtime.simulation.HalfExtents(),
            LocalGravity(runtime.containerPose, runtime.displayGravityWorld)),
        {0.30F, 0.76F, 0.96F, 1.0F});
    runtime.renderer.DrawParticles(
        modelViewProjection,
        runtime.simulation.ParticlePositions(),
        {1.0F, 0.72F, 0.22F, 1.0F});

    const GLenum error = glGetError();
    if (error == GL_NO_ERROR) {
        return true;
    }
    fmt::print(stderr, "OpenGL rendering failed with error 0x{:x}.\n", error);
    return false;
}

bool ProcessInteractiveEvents(Runtime& runtime)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            return false;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            runtime.leftMouseDragging = true;
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) {
            runtime.leftMouseDragging = false;
        } else if (event.type == SDL_EVENT_MOUSE_MOTION && runtime.leftMouseDragging) {
            RotateContainerFromScreenDrag(
                runtime.containerPose,
                runtime.camera,
                event.motion.xrel,
                event.motion.yrel);
        } else if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_R) {
            if (!runtime.motionRecorder.Finalize(runtime.completedTicks, runtime.containerPose)) {
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

bool RunFixedTicks(
    Runtime& runtime,
    const int tickCount,
    const std::function<bool(int)>& afterTick)
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
            currentFrameTime - previousFrameTime).count();
        previousFrameTime = currentFrameTime;

        accumulatedTime += std::min(frameDeltaSeconds, 0.25F);
        int substepCount = 0;
        while (accumulatedTime >= kFixedDeltaSeconds && substepCount < kMaximumInteractiveSubsteps) {
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

bool RunVerification(
    Runtime& runtime,
    const int tickCount,
    const bool printPerformanceProfile)
{
    if (!RenderFrame(runtime)
        || !RunFixedTicks(runtime, tickCount, [](const int) { return true; })) {
        return false;
    }

    const SimulationDiagnostics& diagnostics = runtime.simulation.Diagnostics();
    const glm::vec3 center = runtime.simulation.CenterOfMass();
    fmt::print(
        "Verification completed: {} fixed ticks; particles = {}; center = ({:.3f}, {:.3f}, {:.3f}); "
        "max cell occupancy = {}; candidate pairs = {}; overlaps = {}; max penetration = {:.6f}\n",
        tickCount,
        runtime.simulation.ParticlePositions().size(),
        center.x,
        center.y,
        center.z,
        diagnostics.maximumParticlesPerCell,
        diagnostics.candidatePairCount,
        diagnostics.overlappingPairCount,
        diagnostics.maximumPenetration);
    if (printPerformanceProfile) {
        const PerformanceSummary& profile = runtime.performanceSummary;
        const double tickCountAsDouble = static_cast<double>(profile.fixedStepCount);
        fmt::print(
            "Performance profile: {} ticks from tick {}; fixed step avg = {:.4f} ms; prediction avg = {:.4f} ms; "
            "wall avg = {:.4f} ms; grid build avg = {:.4f} ms; contact avg = {:.4f} ms; "
            "candidate pairs avg = {:.1f}; peak cell occupancy = {}\n",
            profile.fixedStepCount,
            runtime.performanceProfileStartTick,
            profile.fixedStepMilliseconds / tickCountAsDouble,
            profile.predictionMilliseconds / tickCountAsDouble,
            profile.wallConstraintMilliseconds / tickCountAsDouble,
            profile.gridBuildMilliseconds / tickCountAsDouble,
            profile.particleContactMilliseconds / tickCountAsDouble,
            static_cast<double>(profile.candidatePairCount) / tickCountAsDouble,
            profile.maximumParticlesPerCell);
        fmt::print(
            "Performance peak: tick {}; fixed step = {:.4f} ms; prediction = {:.4f} ms; "
            "wall = {:.4f} ms; grid build = {:.4f} ms; contact = {:.4f} ms; "
            "candidate pairs = {}; peak cell occupancy = {}\n",
            profile.slowestCompletedTick,
            profile.maximumFixedStepMilliseconds,
            profile.slowestTickMetrics.predictionMilliseconds,
            profile.slowestTickMetrics.wallConstraintMilliseconds,
            profile.slowestTickMetrics.gridBuildMilliseconds,
            profile.slowestTickMetrics.particleContactMilliseconds,
            profile.slowestTickMetrics.candidatePairCount,
            profile.slowestTickMetrics.maximumParticlesPerCell);
    }
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
        int width = 0;
        int height = 0;
        FramebufferSize(runtime, width, height);
        const std::filesystem::path outputPath = options.outputDirectory
            / fmt::format("tick_{:06}.bmp", tick);
        if (!WriteBmpFrame(width, height, outputPath)) {
            return false;
        }
        ++capturedFrames;
        return true;
    };

    if (!captureTick(0)
        || !RunFixedTicks(runtime, options.ticks, [&captureTick, &options](const int tick) {
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

bool RunApplication(const int argumentCount, char* arguments[])
{
    CommandLineOptions options;
    if (!ParseCommandLine(argumentCount, arguments, options)) {
        return false;
    }

    MotionTrack motionTrack;
    const MotionTrack* selectedMotionTrack = nullptr;
    ContainerPose initialContainerPose;
    if (!options.motionTrackPath.empty()) {
        if (!motionTrack.Load(options.motionTrackPath)) {
            return false;
        }
        if (!motionTrack.HasSample(options.ticks)) {
            fmt::print(
                stderr,
                "Motion track must contain samples from tick 0 through tick {}.\n",
                options.ticks);
            return false;
        }
        selectedMotionTrack = &motionTrack;
        initialContainerPose = motionTrack.Sample(0);
    }

    Runtime runtime;
    runtime.motionTrack = selectedMotionTrack;
    runtime.performanceProfilingEnabled = options.profile;
    runtime.performanceProfileStartTick = options.profileStartTick;
    const bool hiddenWindow = options.mode != RunMode::Interactive;
    if (!InitializeRuntime(runtime, hiddenWindow, initialContainerPose)) {
        return false;
    }

    bool succeeded = false;
    switch (options.mode) {
    case RunMode::Interactive:
        succeeded = RunInteractive(runtime);
        break;
    case RunMode::Verify:
        succeeded = RunVerification(runtime, options.ticks, options.profile);
        break;
    case RunMode::Capture:
        succeeded = RunCapture(runtime, options);
        break;
    }
    ShutdownRuntime(runtime);
    return succeeded;
}

} // namespace flowama
