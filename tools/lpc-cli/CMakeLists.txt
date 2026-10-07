# CLI without an audio device: demo, render, info. Task 13 adds the JUCE-based `play` command.
add_library(lpc_cli_lib STATIC cli_commands.cpp)
target_include_directories(lpc_cli_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lpc_cli_lib PUBLIC lpc_core)

add_executable(lpc-cli main.cpp play_unavailable.cpp)
target_link_libraries(lpc-cli PRIVATE lpc_cli_lib)
