add_library(lpc_juce_device STATIC juce_device.cpp)
target_include_directories(lpc_juce_device PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lpc_juce_device PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0)
target_link_libraries(lpc_juce_device
    PUBLIC lpc_core juce::juce_audio_devices juce::juce_audio_basics juce::juce_events juce::juce_core
    PRIVATE juce::juce_recommended_config_flags)
