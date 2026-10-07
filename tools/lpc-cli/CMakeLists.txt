option(LPC_WITH_JUCE "Build the audio device backend and the play command (downloads JUCE, AGPLv3)" ON)

add_library(lpc_cli_lib STATIC cli_commands.cpp)
target_include_directories(lpc_cli_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lpc_cli_lib PUBLIC lpc_core)

if(LPC_WITH_JUCE)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_console_app(lpc-cli PRODUCT_NAME "lpc-cli")
    target_sources(lpc-cli PRIVATE main.cpp juce_device.cpp play_command.cpp)
    target_compile_definitions(lpc-cli PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(lpc-cli
        PRIVATE
            lpc_cli_lib
            juce::juce_audio_devices
            juce::juce_audio_basics
            juce::juce_events
            juce::juce_core
        PUBLIC
            juce::juce_recommended_config_flags)
else()
    add_executable(lpc-cli main.cpp play_unavailable.cpp)
    target_link_libraries(lpc-cli PRIVATE lpc_cli_lib)
endif()
