import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Track > Assign Track Color: the colour of every selected track in one step.
Dialog {
    id: root
    required property ProjectController project
    readonly property var palette: ["purple", "indigo", "blue", "teal", "green", "yellow", "orange", "red", "pink", "magenta"]
    modal: true
    anchors.centerIn: parent
    width: 360
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Colors"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Text {
            text: root.project.selectedTrackIds.length === 0 ? qsTr("Select the tracks to colour first.") : qsTr("%n track(s) selected", "", root.project.selectedTrackIds.length)
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacing[2]
            Repeater {
                model: root.palette
                delegate: Rectangle {
                    id: chip
                    required property string modelData
                    width: 28; height: 28; radius: 4
                    color: Theme["track" + modelData.charAt(0).toUpperCase() + modelData.slice(1) + "Solid"]
                    border.width: area.containsMouse ? 2 : 0
                    border.color: Theme.textPrimary
                    objectName: "colorChip_" + modelData
                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: root.project.selectedTrackIds.length > 0
                        onClicked: root.project.setSelectedTracksColor(chip.modelData)
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
        }
    }
}
