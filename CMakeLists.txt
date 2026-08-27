cmake_minimum_required(VERSION 3.25)

project(Flowama LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(flowama
    src/application.cpp
    src/container.cpp
    src/debug_renderer.cpp
    src/inertial_frame_verification.cpp
    src/input_timeline.cpp
    src/input_timeline_verification.cpp
    src/main.cpp
    src/motion_track.cpp
    src/simulation.cpp
)

find_package(SDL3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)

target_link_libraries(flowama PRIVATE
    SDL3::SDL3
    glm::glm
    fmt::fmt
    OpenGL::GL
    glad::glad
)
