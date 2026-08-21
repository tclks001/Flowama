cmake_minimum_required(VERSION 3.25)

project(Flowama LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(flowama
    src/main.cpp
)

find_package(SDL3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

target_link_libraries(flowama PRIVATE
    SDL3::SDL3
    glm::glm
    fmt::fmt
)