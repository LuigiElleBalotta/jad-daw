find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 QuickTest Test Concurrent)
qt_standard_project_setup(REQUIRES 6.8)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(${CMAKE_SOURCE_DIR}/assets/fonts/InterVariable.ttf PROPERTIES QT_RESOURCE_ALIAS fonts/InterVariable.ttf)

qt_add_library(jad_ui_lib STATIC)
qt_add_qml_module(jad_ui_lib
    URI Jad
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
    RESOURCES
        ${CMAKE_SOURCE_DIR}/assets/fonts/InterVariable.ttf
)
target_link_libraries(jad_ui_lib PUBLIC Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent lpc_core)
target_include_directories(jad_ui_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

qt_add_executable(jad-daw main.cpp)
target_link_libraries(jad-daw PRIVATE jad_ui_libplugin jad_ui_lib Qt6::Quick Qt6::QuickControls2)
set_target_properties(jad-daw PROPERTIES WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE)

add_subdirectory(tests)
