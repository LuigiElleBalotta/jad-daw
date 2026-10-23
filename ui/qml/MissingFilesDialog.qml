import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Jad

// Shown when a project is opened whose audio files are not where it says (they were moved, or the drive is not there): the files are listed, each can be
// located by hand, or a folder can be searched for all of them by name.
Dialog {
    id: root
    required property ProjectController project
    property var missing: []
    property string locating: ""
    modal: true
    anchors.centerIn: parent
    width: 560
    function refresh() { missing = project.missingMedia() }
    onAboutToShow: refresh()
    Connections { target: root.project; function onProjectChanged() { if (root.visible) { root.refresh(); if (root.missing.length === 0) root.close() } } }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }
    FileDialog {
        id: locateDialog
        title: qsTr("Locate the audio file")
        nameFilters: [qsTr("Audio files (*.wav)"), qsTr("All files (*)")]
        onAccepted: { root.project.relinkMedia(root.locating, selectedFile); root.refresh() }
    }
    FolderDialog {
        id: searchDialog
        title: qsTr("Search this folder for the missing files")
        onAccepted: {
            const n = root.project.searchMissingMedia(selectedFolder)
            root.project.notice(n > 0 ? qsTr("%1 file(s) found").arg(n) : qsTr("None of the missing files was found there"))
        }
    }
    contentItem: ColumnLayout {
        spacing: Theme.spacing[3]
        Text { text: qsTr("Missing Audio Files"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("The project refers to %1 audio file(s) that are not where it says. Locate them, or search a folder for them.").arg(root.missing.length)
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize
        }
        ListView {
            objectName: "missingList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(260, contentHeight)
            clip: true
            model: root.missing
            delegate: RowLayout {
                required property var modelData
                width: ListView.view.width
                spacing: Theme.spacing[2]
                Text { text: "!"; color: Theme.stateRecord; font.bold: true; font.pixelSize: Theme.fontTypeBodySize }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text { Layout.fillWidth: true; text: modelData.name; elide: Text.ElideMiddle; color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                    Text { Layout.fillWidth: true; text: modelData.path; elide: Text.ElideMiddle; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize }
                }
                IconButton { implicitHeight: 24; label: qsTr("Locate…"); onClicked: { root.locating = modelData.id; locateDialog.open() } }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            IconButton { objectName: "searchFolder"; implicitHeight: 26; label: qsTr("Search a Folder…"); onClicked: searchDialog.open() }
            Item { Layout.fillWidth: true }
            IconButton { objectName: "skipButton"; implicitHeight: 26; label: qsTr("Skip"); onClicked: root.close() }
        }
    }
}
