#include <SDL3/SDL.h>

#include <fmt/format.h>

#include <glm/vec3.hpp>

int main()
{
    const glm::vec3 containerSize{3.6F, 0.2F, 6.0F};

    fmt::print(
        "Flowama bootstrap: container = ({:.1f}, {:.1f}, {:.1f})\n",
        containerSize.x,
        containerSize.y,
        containerSize.z);

    return 0;
}