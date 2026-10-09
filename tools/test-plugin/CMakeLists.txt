juce_add_plugin(lpc_test_gain
    PRODUCT_NAME "LPC Test Gain"
    COMPANY_NAME "JAD"
    PLUGIN_MANUFACTURER_CODE JadD
    PLUGIN_CODE TgnA
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(lpc_test_gain PRIVATE TestGainPlugin.cpp)
target_compile_definitions(lpc_test_gain PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(lpc_test_gain PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags)
set(LPC_TEST_VST3_DIR "${CMAKE_CURRENT_BINARY_DIR}/lpc_test_gain_artefacts/$<CONFIG>/VST3" PARENT_SCOPE)
