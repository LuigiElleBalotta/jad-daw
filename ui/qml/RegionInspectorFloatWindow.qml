import QtQuick
import QtQuick.Window
import Jad

// Window > Show Region Inspector Float: the Region section of the Inspector in a window of its own.
Window {
    id: root
    required property ProjectController project
    width: 320
    height: 300
    title: qsTr("Region Inspector")
    color: Theme.surfacePanel
    flags: Qt.Tool | Qt.WindowStaysOnTopHint
    Text {
        anchors.centerIn: parent
        visible: !root.project.inspector.hasRegion
        text: qsTr("Select a region")
        color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize
    }
    Loader {  // only while the window is shown: the fields of two inspectors would answer to the same names
        objectName: "floatInspectorLoader"
        width: parent.width
        active: root.visible
        visible: root.project.inspector.hasRegion
        sourceComponent: RegionInspector {
            objectName: "floatRegionInspector"
            project: root.project
            region: root.project.inspector.region
        }
    }
}
