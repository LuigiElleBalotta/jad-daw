import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// What to do with the changes that are not saved when the project is about to be closed or replaced: Save, Don't Save or Cancel.
Dialog {
    id: root
    property string projectName: ""
    signal saveChosen()
    signal discardChosen()
    modal: true
    anchors.centerIn: parent
    width: 420
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Save the changes?"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("%1 has changes that are not saved. If you do not save them they are lost.").arg(root.projectName !== "" ? "\u201c" + root.projectName + "\u201d" : qsTr("The project"))
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize
        }
        RowLayout {
            Layout.fillWidth: true
            IconButton { objectName: "dontSaveButton"; implicitHeight: 26; label: qsTr("Don't Save"); onClicked: { root.close(); root.discardChosen() } }
            Item { Layout.fillWidth: true }
            IconButton { objectName: "cancelButton"; implicitHeight: 26; label: qsTr("Cancel"); onClicked: root.close() }
            IconButton { objectName: "saveButton"; implicitHeight: 26; label: qsTr("Save"); active: true; fillActive: true; fillText: Theme.textPrimary; onClicked: { root.close(); root.saveChosen() } }
        }
    }
}
