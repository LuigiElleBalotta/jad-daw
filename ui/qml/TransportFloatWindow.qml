import QtQuick
import QtQuick.Window
import Jad

// Window > Open Transport Float: the transport buttons and the LCD in a small window that stays above the others.
Window {
    id: root
    required property ProjectController project
    width: row.implicitWidth + Theme.spacing[5] * 2
    height: 64
    title: qsTr("Transport")
    color: Theme.surfacePanel
    flags: Qt.Tool | Qt.WindowStaysOnTopHint
    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacing[3]
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacing[2]
            ActionButton { actionId: "transport.toStart"; source: "icons/rewind.svg" }
            ActionButton { actionId: "transport.barBack"; label: "<" }
            ActionButton { actionId: "transport.barForward"; label: ">" }
            ActionButton { actionId: "transport.stop"; source: "icons/stop.svg" }
            ActionButton { actionId: "transport.playStop"; source: "icons/play.svg"; active: root.project.playing }
            ActionButton { actionId: "transport.record"; label: "\u25cf" }
        }
        Loader {  // only while the window is shown: two LCDs at once would answer to the same names
            anchors.verticalCenter: parent.verticalCenter
            active: root.visible
            sourceComponent: Lcd { project: root.project }
        }
    }
}
