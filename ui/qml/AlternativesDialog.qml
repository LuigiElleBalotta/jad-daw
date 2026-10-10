import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// File > Project Alternatives: named copies of the project to go back to or try something else.
Dialog {
    id: root
    required property ProjectController project
    property var names: []
    modal: false
    anchors.centerIn: parent
    width: 440
    onAboutToShow: refresh()
    function refresh() { names = project.projectAlternatives() }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Project Alternatives"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        RowLayout {
            spacing: Theme.spacing[2]
            TextEntry { id: nameField; objectName: "alternativeName"; Layout.fillWidth: true; onEdited: (t) => {} }
            IconButton {
                objectName: "newAlternative"
                implicitHeight: 24
                label: qsTr("Save Current as New")
                enabled: nameField.text.trim() !== ""
                onClicked: { if (root.project.newProjectAlternative(nameField.text)) { nameField.text = ""; root.refresh() } }
            }
        }
        Text { visible: root.names.length === 0; text: qsTr("No alternatives yet."); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
        Repeater {
            model: root.names
            delegate: RowLayout {
                required property string modelData
                Layout.fillWidth: true
                spacing: Theme.spacing[2]
                Text { Layout.fillWidth: true; text: modelData; elide: Text.ElideRight; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                IconButton { objectName: "open_" + modelData; implicitHeight: 24; label: qsTr("Open"); onClicked: { root.close(); root.project.openProjectAlternative(modelData) } }
                IconButton { objectName: "delete_" + modelData; implicitHeight: 24; label: qsTr("Delete"); onClicked: { root.project.deleteProjectAlternative(modelData); root.refresh() } }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
        }
    }
}
