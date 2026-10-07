find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 QuickTest Test Concurrent)
qt_standard_project_setup(REQUIRES 6.8)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(${CMAKE_SOURCE_DIR}/assets/fonts/InterVariable.ttf PROPERTIES QT_RESOURCE_ALIAS fonts/InterVariable.ttf)
set_source_files_properties(shortcuts/default-shortcuts.json PROPERTIES QT_RESOURCE_ALIAS shortcuts/default-shortcuts.json)

qt_add_library(jad_ui_lib STATIC)
qt_add_qml_module(jad_ui_lib
    URI Jad
    VERSION 1.0
    SOURCES
        bridge/snapshot.cpp
        bridge/track_list_model.cpp
        bridge/region_model.cpp
        bridge/mixer_model.cpp
        bridge/project_controller.cpp
        bridge/waveform_cache.cpp
        shortcuts/shortcut_map.cpp
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/IconButton.qml
        qml/Panel.qml
        qml/Fader.qml
        qml/Knob.qml
        qml/ErrorBar.qml
        qml/TransportBar.qml
        qml/Timeline.qml
        qml/Ruler.qml
        qml/RegionItem.qml
        qml/WaveformPreview.qml
        qml/TrackList.qml
        qml/Meter.qml
        qml/MixerStrip.qml
        qml/Mixer.qml
        qml/ThemedMenu.qml
        qml/ThemedMenuItem.qml
    RESOURCES
        qml/icons/play.svg
        qml/icons/stop.svg
        qml/icons/loop.svg
        qml/icons/rewind.svg
        qml/icons/forward.svg
        qml/icons/plus.svg
        qml/icons/mixer.svg
        qml/icons/mute.svg
        qml/icons/solo.svg
        qml/icons/chevron-down.svg
        qml/icons/chevron-right.svg
        ${CMAKE_SOURCE_DIR}/assets/fonts/InterVariable.ttf
        shortcuts/default-shortcuts.json
)
target_link_libraries(jad_ui_lib PUBLIC Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent lpc_core)
target_include_directories(jad_ui_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/bridge)
if(TARGET lpc_juce_device)
    target_link_libraries(jad_ui_lib PUBLIC lpc_juce_device)
    target_compile_definitions(jad_ui_lib PUBLIC JAD_HAVE_JUCE=1)
endif()

qt_add_executable(jad-daw main.cpp)
if(WIN32)
    enable_language(RC)
    target_sources(jad-daw PRIVATE jad-daw.rc)
endif()
# macOS: the bundle icon (.icns) is not generated yet; set MACOSX_BUNDLE_ICON_FILE once assets/branding has one.
target_link_libraries(jad-daw PRIVATE jad_ui_libplugin jad_ui_lib Qt6::Quick Qt6::QuickControls2)
set_target_properties(jad-daw PROPERTIES WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE)

# Release builds pass the tag (for example 0.0.1 or 1.0.0-1); the bundle versions must be plain numbers.
set(JAD_VERSION "0.0.0-dev" CACHE STRING "Version shown by the app")
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" JAD_VERSION_NUMERIC "${JAD_VERSION}")
if(NOT JAD_VERSION_NUMERIC)
    set(JAD_VERSION_NUMERIC "0.0.0")
endif()
target_compile_definitions(jad-daw PRIVATE JAD_VERSION="${JAD_VERSION}")
set_target_properties(jad-daw PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "JAD Daw"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.jad.daw"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${JAD_VERSION_NUMERIC}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${JAD_VERSION_NUMERIC}")

add_subdirectory(tests)
