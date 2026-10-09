add_library(lpc_cli_lib STATIC cli_commands.cpp)
target_include_directories(lpc_cli_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lpc_cli_lib PUBLIC lpc_core)

if(LPC_WITH_JUCE)
    juce_add_console_app(lpc-cli PRODUCT_NAME "lpc-cli")
    target_sources(lpc-cli PRIVATE main.cpp play_command.cpp)
    target_compile_definitions(lpc-cli PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_STANDALONE_APPLICATION=1
        LPC_CLI_HAS_PLUGINS=1)
    add_dependencies(lpc-cli lpc-plugin-scanner)
    add_custom_command(TARGET lpc-cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:lpc-plugin-scanner> $<TARGET_FILE_DIR:lpc-cli>)
    target_link_libraries(lpc-cli
        PRIVATE
            lpc_cli_lib
            lpc_juce_device
        PUBLIC
            juce::juce_recommended_config_flags)
else()
    add_executable(lpc-cli main.cpp play_unavailable.cpp)
    target_link_libraries(lpc-cli PRIVATE lpc_cli_lib)
endif()
