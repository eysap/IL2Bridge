cmake_minimum_required(VERSION 3.20)
project(Il2Bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(IL2BRIDGE_BUILD_EXAMPLES "Build example programs" ON)
option(IL2BRIDGE_BUILD_TESTS "Build test suite" ON)

include(cmake/Dependencies.cmake)

# Applied after fetching third-party deps above so vendored code (Catch2,
# nlohmann_json) isn't built with our warning flags, only our own targets.
add_compile_options(-Wall -Wextra)

add_subdirectory(broker)
add_subdirectory(loader)

if(IL2BRIDGE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(IL2BRIDGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
