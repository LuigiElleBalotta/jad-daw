import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// Track > Create Track Stack…: Logic has one command and chooses the kind in a dialog, a Folder Stack (the tracks keep their own strips and
// the stack is a folder that mutes, solos and moves them together) or a Summing Stack (their outputs go to one new auxiliary strip).
Dialog {
    id: root
    required property ProjectController project
    property string kind: "summing"
    modal: false
    anchors.centerIn: parent
    width: 380
    onAboutToShow: kind = "summing"
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Create Track Stack"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: root.kind === "summing" ? qsTr("The selected tracks are summed into one new auxiliary channel strip: one fader and one set of inserts for the whole stack.")
                                          : qsTr("The selected tracks are put in a folder: they keep their own channel strips and are muted, soloed and moved together.")
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize
        }
        Repeater {
            model: [{ id: "summing", label: qsTr("Summing Stack") }, { id: "folder", label: qsTr("Folder Stack") }]
            delegate: RadioButton {
                required property var modelData
                objectName: "kind_" + modelData.id
                text: modelData.label
                checked: root.kind === modelData.id
                onClicked: root.kind = modelData.id
                contentItem: Text { leftPadding: 24; text: parent.text; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; verticalAlignment: Text.AlignVCenter }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Cancel"); onClicked: root.close() }
            IconButton { objectName: "okButton"; implicitHeight: 26; label: qsTr("Create"); active: true; fillActive: true; fillText: Theme.textPrimary; onClicked: { root.project.createStack(root.kind); root.close() } }
        }
    }
}
