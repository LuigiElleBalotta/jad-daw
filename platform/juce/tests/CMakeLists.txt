add_executable(lpc_juce_tests test_plugin_host.cpp test_scanner.cpp)
target_link_libraries(lpc_juce_tests PRIVATE lpc_juce_device Catch2::Catch2WithMain juce::juce_recommended_config_flags)
target_compile_definitions(lpc_juce_tests PRIVATE
    LPC_TEST_VST3_DIR="${LPC_TEST_VST3_DIR}"
    LPC_SCANNER_EXE="$<TARGET_FILE:lpc-plugin-scanner>")
add_dependencies(lpc_juce_tests lpc_test_gain_VST3 lpc-plugin-scanner)
add_test(NAME lpc_juce_tests COMMAND lpc_juce_tests)
