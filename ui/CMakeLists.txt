find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 QuickTest Test Concurrent)
qt_standard_project_setup(REQUIRES 6.8)

set_source_files_properties(qml/Theme.qml qml/ActionHub.qml qml/InsertDrag.qml qml/StripMetrics.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(${CMAKE_SOURCE_DIR}/assets/fonts/InterVariable.ttf PROPERTIES QT_RESOURCE_ALIAS fonts/InterVariable.ttf)
set_source_files_properties(actions/actions.json PROPERTIES QT_RESOURCE_ALIAS actions/actions.json)
set_source_files_properties(${CMAKE_SOURCE_DIR}/core/data/patches.json PROPERTIES QT_RESOURCE_ALIAS patches/patches.json)

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
        bridge/project_controller_edit.cpp
        bridge/waveform_cache.cpp
        bridge/lcd_parse.cpp
        bridge/inspector_model.cpp
        bridge/library_model.cpp
        bridge/plugins_model.cpp
        shortcuts/shortcut_map.cpp
        actions/action_registry.cpp
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/IconButton.qml
        qml/Panel.qml
        qml/Fader.qml
        qml/FaderScale.qml
        qml/StripMetrics.qml
        qml/MeterScale.qml
        qml/MixerLegend.qml
        qml/DbField.qml
        qml/MixerHeader.qml
        qml/Knob.qml
        qml/ErrorBar.qml
        qml/Timeline.qml
        qml/Ruler.qml
        qml/RegionItem.qml
        qml/WaveformPreview.qml
        qml/TrackList.qml
        qml/Meter.qml
        qml/Mixer.qml
        qml/MixerView.qml
        qml/PianoRoll.qml
        qml/EditorArea.qml
        qml/GlobalTracks.qml
        qml/AutomationLane.qml
        qml/MetronomeSettings.qml
        qml/NewTracksDialog.qml
        qml/ParamSlider.qml
        qml/AudioProcessDialog.qml
        qml/AudioEditor.qml
        qml/GroupSettingsWindow.qml
        qml/IoLabelsDialog.qml
        qml/BounceDialog.qml
        qml/ConfirmDialog.qml
        qml/KeyCommandsWindow.qml
        qml/UndoHistoryDialog.qml
        qml/RecentProjectsDialog.qml
        qml/ColorsDialog.qml
        qml/SearchPluginDialog.qml
        qml/TextEntry.qml
        qml/MidiTransformDialog.qml
        qml/StepSequencer.qml
        qml/ScoreView.qml
        qml/TextPromptDialog.qml
        qml/TemplatesDialog.qml
        qml/BrowserWindow.qml
        qml/CustomizeBarsDialog.qml
        qml/ListEditorsWindow.qml
        qml/NumberPromptDialog.qml
        qml/SearchTrackDialog.qml
        qml/FlagCheck.qml
        qml/MarkerListWindow.qml
        qml/ProjectAudioWindow.qml
        qml/NotePadWindow.qml
        qml/ProjectSettingsDialog.qml
        qml/MusicalTypingWindow.qml
        qml/EffectEditorWindow.qml
        qml/PreferencesDialog.qml
        qml/SelectField.qml
        qml/ThemedMenu.qml
        qml/ThemedMenuItem.qml
        qml/ActionTreeMenu.qml
        qml/JadAction.qml
        qml/Toast.qml
        qml/ActionMenuBar.qml
        qml/ActionHub.qml
        qml/InsertDrag.qml
        qml/ActionButton.qml
        qml/Lcd.qml
        qml/ControlBar.qml
        qml/TrackHeader.qml
        qml/ActionMenu.qml
        qml/ToolBar.qml
        qml/AboutDialog.qml
        qml/StripSlot.qml
        qml/ChannelStrip.qml
        qml/ProjectStrip.qml
        qml/TrackIcon.qml
        qml/PanelHeader.qml
        qml/InspectorRow.qml
        qml/NumberField.qml
        qml/StubCheck.qml
        qml/StubValue.qml
        qml/Splitter.qml
        qml/LeftColumn.qml
        qml/Library.qml
        qml/ScreenKnob.qml
        qml/SmartControls.qml
        qml/Inspector.qml
        qml/RegionInspector.qml
        qml/TrackInspector.qml
        qml/PluginManager.qml
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
        qml/icons/app-icon.png
        qml/icons/pointer.svg
        qml/icons/pencil.svg
        qml/icons/eraser.svg
        qml/icons/scissors.svg
        qml/icons/glue.svg
        qml/icons/zoom-in.svg
        qml/icons/zoom-out.svg
        qml/icons/chevron-right.svg
        qml/icons/track-audio.svg
        qml/icons/track-instrument.svg
        qml/icons/track-aux.svg
        qml/icons/track-bus.svg
        ${CMAKE_SOURCE_DIR}/assets/fonts/InterVariable.ttf
        actions/actions.json
        ${CMAKE_SOURCE_DIR}/core/data/patches.json
)
target_link_libraries(jad_ui_lib PUBLIC Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent lpc_core)
target_include_directories(jad_ui_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/bridge ${CMAKE_CURRENT_SOURCE_DIR}/actions)
if(TARGET lpc_juce_device)
    target_link_libraries(jad_ui_lib PUBLIC lpc_juce_device)
    target_compile_definitions(jad_ui_lib PUBLIC JAD_HAVE_JUCE=1)
endif()

qt_add_executable(jad-daw main.cpp)
if(TARGET lpc-plugin-scanner)
    add_dependencies(jad-daw lpc-plugin-scanner)
    add_custom_command(TARGET jad-daw POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:lpc-plugin-scanner> $<TARGET_FILE_DIR:jad-daw>)
endif()
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

# Windows: copy the Qt DLLs and plugins next to the executables right after building, so that they start by
# double click and under ctest without Qt on PATH (a missing DLL makes Windows open an error dialog per process).
if(WIN32)
    set(JAD_QML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/qml")
    get_filename_component(JAD_QT_PLUGINS "${Qt6_DIR}/../../../plugins" ABSOLUTE)
    find_program(JAD_WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin" NO_DEFAULT_PATH)
    function(jad_deploy_qt target)
        if(JAD_WINDEPLOYQT)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${JAD_WINDEPLOYQT} $<IF:$<CONFIG:Debug>,--debug,--release> --no-translations --no-system-d3d-compiler
                        --qmldir ${JAD_QML_DIR} $<TARGET_FILE:${target}>
                COMMENT "windeployqt ${target}")
            # the tests run with QT_QPA_PLATFORM=offscreen, a plugin that windeployqt does not copy
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${JAD_QT_PLUGINS}/platforms/qoffscreen$<$<CONFIG:Debug>:d>.dll" "$<TARGET_FILE_DIR:${target}>/platforms/"
                COMMENT "offscreen platform plugin for ${target}")
        endif()
    endfunction()
    jad_deploy_qt(jad-daw)
endif()

add_subdirectory(tests)
