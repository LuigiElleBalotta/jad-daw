cmake_minimum_required(VERSION 3.24)
project(lpc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LPC_BUILD_CLI "Build lpc-cli (downloads JUCE)" ON)

include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2 nlohmann_json)

# Project-wide flags are added after the dependencies so they do not warn about third-party code.
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
endif()

enable_testing()
add_subdirectory(core)
if(LPC_BUILD_CLI)
    add_subdirectory(tools/lpc-cli)
endif()
add_subdirectory(tests)
if(LPC_BUILD_UI)
    add_subdirectory(ui)
endif()
