import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// View > Colors / Track > Assign Track Color: Logic's palette window, 24 hues across and 4 shades down. The arrow at the left puts the default colour back.
Dialog {
    id: root
    required property ProjectController project
    readonly property string current: project.inspector.track.color ?? ""
    readonly property bool hasSelection: project.selectedTrackIds.length > 0
    modal: false
    anchors.centerIn: parent
    padding: 12
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item {
        implicitHeight: 28
        Text {
            anchors.centerIn: parent
            text: qsTr("Color")
            color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; font.weight: 600
        }
        MouseArea { anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; width: 16; height: 16; onClicked: root.close()
            Text { anchors.centerIn: parent; text: "×"; color: Theme.textSecondary; font.pixelSize: 16 } }
    }
    contentItem: RowLayout {
        spacing: 10
        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 4
            width: 24; height: 20; radius: 4
            color: resetArea.containsMouse ? Theme.surfaceRaised : "transparent"
            border.color: Theme.borderSubtle
            objectName: "colorReset"
            opacity: root.hasSelection ? 1 : 0.4
            Text { anchors.centerIn: parent; text: "↺"; color: Theme.textPrimary; font.pixelSize: 13 }
            MouseArea {
                id: resetArea
                anchors.fill: parent
                hoverEnabled: true
                enabled: root.hasSelection
                onClicked: root.project.setSelectedTracksColor("")
            }
        }
        Grid {
            columns: 24
            spacing: 3
            Repeater {
                model: 96
                delegate: Rectangle {
                    id: chip
                    required property int index
                    readonly property string swatch: TrackColors.palette[Math.floor(index / 24)][index % 24]
                    width: 24; height: 18; radius: 3
                    color: swatch
                    border.width: root.current === swatch ? 2 : (area.containsMouse ? 1 : 0)
                    border.color: Theme.textPrimary
                    objectName: "colorChip_" + index
                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: root.hasSelection
                        onClicked: root.project.setSelectedTracksColor(chip.swatch)
                    }
                }
            }
        }
    }
}
