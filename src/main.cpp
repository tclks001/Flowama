#include <glad/glad.h>

#include <SDL3/SDL.h>

#include <fmt/format.h>

#include <charconv>
#include <string_view>
#include <system_error>

namespace {

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
        "Flowama",
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

    fmt::print(
        "OpenGL: {}\nRenderer: {}\n",
        reinterpret_cast<const char*>(glGetString(GL_VERSION)),
        reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    bool running = true;
    int completedTicks = 0;
    bool automationCompleted = false;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        if (!running) {
            break;
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        glViewport(0, 0, width, height);

        glClearColor(0.05F, 0.07F, 0.12F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        SDL_GL_SwapWindow(window);

        if (options.automation) {
            ++completedTicks;
            if (completedTicks == options.ticks) {
                automationCompleted = true;
                running = false;
            }
        }
    }

    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (options.automation) {
        if (!automationCompleted) {
            fmt::print(stderr, "Automation was interrupted after {} ticks.\n", completedTicks);
            return 1;
        }

        fmt::print("Automation completed: {} ticks\n", completedTicks);
    }

    return 0;
}
