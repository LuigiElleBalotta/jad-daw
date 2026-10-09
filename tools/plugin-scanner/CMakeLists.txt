juce_add_console_app(lpc-plugin-scanner PRODUCT_NAME "lpc-plugin-scanner")
target_sources(lpc-plugin-scanner PRIVATE main.cpp)
target_compile_definitions(lpc-plugin-scanner PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0 JUCE_PLUGINHOST_VST3=1 JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(lpc-plugin-scanner PRIVATE
    nlohmann_json::nlohmann_json
    juce::juce_audio_processors juce::juce_gui_basics juce::juce_events juce::juce_core
    juce::juce_recommended_config_flags)
