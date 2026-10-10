import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Jad

// Mix > Group Settings: the name of the group of a track and what its members share.
Window {
    id: root
    required property ProjectController project
    readonly property string trackId: project.groupSettingsTrack
    property var group: ({})
    function refresh() { group = trackId !== "" ? project.trackGroup(trackId) : ({}) }
    onTrackIdChanged: refresh()
    Connections { target: root.project; function onGroupsChanged() { root.refresh() } }
    Component.onCompleted: refresh()

    visible: trackId !== "" && group.id !== undefined
    width: 360
    height: 330
    minimumWidth: 320
    minimumHeight: 300
    title: qsTr("Group Settings")
    color: Theme.surfacePanel
    flags: Qt.Tool
    onClosing: project.openGroupSettings("")

    component Option: IconButton {
        required property string field
        required property string text2
        implicitHeight: 24
        implicitWidth: 120
        label: text2
        active: root.group[field] === true
        fillActive: true
        fillText: Theme.textPrimary
        onClicked: root.project.setGroupField(root.group.id, field, !(root.group[field] === true))
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing[4]
        spacing: Theme.spacing[3]
        RowLayout {
            spacing: Theme.spacing[2]
            Text { text: qsTr("Name"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 24
                color: Theme.surfaceRaised; radius: Theme.radiusControl - 2
                border.color: nameField.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                TextInput {
                    id: nameField
                    objectName: "groupName"
                    anchors.fill: parent; anchors.margins: 5
                    verticalAlignment: TextInput.AlignVCenter
                    text: root.group.name ?? ""
                    maximumLength: 64
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeBodySize
                    onEditingFinished: root.project.setGroupField(root.group.id, "name", text)
                }
            }
        }
        Text {
            text: qsTr("%1 tracks in the group. Shared by the members:").arg((root.group.members ?? []).length)
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
        }
        Grid {
            columns: 2
            spacing: Theme.spacing[2]
            Option { field: "volume"; text2: qsTr("Volume") }
            Option { field: "pan"; text2: qsTr("Pan") }
            Option { field: "mute"; text2: qsTr("Mute") }
            Option { field: "solo"; text2: qsTr("Solo") }
            Option { field: "selection"; text2: qsTr("Selection") }
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacing[2]
            IconButton { objectName: "deleteGroup"; implicitHeight: 26; label: qsTr("Delete Group"); onClicked: { const id = root.group.id; root.project.openGroupSettings(""); root.project.deleteGroup(id) } }
            IconButton { implicitHeight: 26; label: qsTr("Close"); onClicked: root.project.openGroupSettings("") }
        }
    }
}
