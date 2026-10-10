import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Track > Assign Track Icon: a picture for the header of the selected tracks.
Dialog {
    id: root
    required property ProjectController project
    modal: true
    anchors.centerIn: parent
    width: 360
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    function sourceOf(key) { return (key === "audio" || key === "instrument" ? "icons/icon-" : "icons/track-") + key + ".svg" }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Assign Track Icon"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        GridLayout {
            columns: 6
            columnSpacing: Theme.spacing[2]
            rowSpacing: Theme.spacing[2]
            Repeater {
                model: root.project.trackIconChoices()
                delegate: IconButton {
                    required property string modelData
                    objectName: "icon_" + modelData
                    implicitWidth: 44; implicitHeight: 44
                    source: root.sourceOf(modelData)
                    onClicked: { root.project.setSelectedTracksIcon(modelData); root.close() }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            IconButton { objectName: "noIcon"; implicitHeight: 26; label: qsTr("No Icon"); onClicked: { root.project.setSelectedTracksIcon(""); root.close() } }
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Cancel"); onClicked: root.close() }
        }
    }
}
