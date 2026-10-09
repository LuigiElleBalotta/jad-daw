cmake_minimum_required(VERSION 3.24)
project(lpc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LPC_BUILD_CLI "Build lpc-cli (downloads JUCE)" ON)
option(LPC_BUILD_UI "Build the Qt Quick UI (needs Qt 6)" OFF)
option(LPC_WITH_JUCE "Build the audio device backend (downloads JUCE, AGPLv3)" ON)
option(LPC_BUILD_PLUGIN_TESTS "Build the test VST3, the scanner tests and the platform plug-in tests (needs JUCE)" OFF)

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
if(LPC_WITH_JUCE AND (LPC_BUILD_CLI OR LPC_BUILD_UI))
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
    if(LPC_BUILD_PLUGIN_TESTS)
        add_subdirectory(tools/test-plugin)
    endif()
    add_subdirectory(platform/juce)
    add_subdirectory(tools/plugin-scanner)
endif()
if(LPC_BUILD_CLI)
    add_subdirectory(tools/lpc-cli)
endif()
add_subdirectory(tests)
if(LPC_BUILD_UI)
    add_subdirectory(ui)
endif()
