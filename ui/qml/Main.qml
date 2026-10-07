import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import Jad

ApplicationWindow {
    id: root
    width: 1280
    height: 800
    visible: true
    title: "JAD Daw"
    color: Theme.surfaceApp

    property alias project: controller
    ProjectController { id: controller }

    function togglePlay() { controller.playing ? controller.stop() : controller.play() }
    function toggleLoop() { controller.setLoopBeats(0, controller.loopEnabled ? 0 : controller.beatsPerBar * 4) }
    function forSelectedTracks(fn) { for (const id of timeline.selectedTrackIds()) fn(id) }

    FolderDialog {
        id: openDialog
        title: qsTr("Open project folder")
        onAccepted: controller.openProject(selectedFolder)
    }
    FolderDialog {
        id: newDialog
        title: qsTr("New project folder")
        onAccepted: controller.newProject(selectedFolder)
    }

    // actions that appear in the menus carry their sequence; the shortcut map decides it
    Action { id: newAction; text: qsTr("New…"); onTriggered: newDialog.open() }
    Action { id: openAction; text: qsTr("Open…"); shortcut: controller.shortcut("file.open"); onTriggered: openDialog.open() }
    Action { id: saveAction; text: qsTr("Save"); shortcut: controller.shortcut("file.save"); enabled: controller.hasProject; onTriggered: controller.saveProject() }
    Action { id: quitAction; text: qsTr("Quit"); onTriggered: Qt.quit() }
    Action { id: undoAction; text: qsTr("Undo"); shortcut: controller.shortcut("edit.undo"); onTriggered: controller.undo() }
    Action { id: redoAction; text: qsTr("Redo"); shortcut: controller.shortcut("edit.redo"); onTriggered: controller.redo() }
    Action { id: deleteAction; text: qsTr("Delete"); shortcut: controller.shortcut("edit.delete"); onTriggered: timeline.deleteSelected() }

    menuBar: MenuBar {
        background: Rectangle { color: Theme.surfacePanel }
        delegate: MenuBarItem {
            id: barItem
            contentItem: Text {
                text: barItem.text
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeBodySize
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle { color: barItem.highlighted ? Theme.surfaceRaisedHover : "transparent" }
        }
        ThemedMenu {
            title: qsTr("File")
            ThemedMenuItem { action: newAction }
            ThemedMenuItem { action: openAction }
            ThemedMenuItem { action: saveAction }
            MenuSeparator {}
            ThemedMenuItem { action: quitAction }
        }
        ThemedMenu {
            title: qsTr("Edit")
            ThemedMenuItem { action: undoAction }
            ThemedMenuItem { action: redoAction }
            MenuSeparator {}
            ThemedMenuItem { action: deleteAction }
        }
    }

    Shortcut { sequence: controller.shortcut("transport.playStop"); onActivated: root.togglePlay() }
    Shortcut { sequence: controller.shortcut("transport.toStart"); onActivated: controller.locateBeats(0) }
    Shortcut { sequence: controller.shortcut("transport.loop"); onActivated: root.toggleLoop() }
    Shortcut { sequence: controller.shortcut("track.mute"); onActivated: root.forSelectedTracks((id) => controller.toggleMute(id)) }
    Shortcut { sequence: controller.shortcut("track.solo"); onActivated: root.forSelectedTracks((id) => controller.toggleSolo(id)) }
    Shortcut { sequence: controller.shortcut("view.zoomIn"); onActivated: timeline.zoomBy(1.25, timeline.width / 2) }
    Shortcut { sequence: controller.shortcut("view.zoomOut"); onActivated: timeline.zoomBy(0.8, timeline.width / 2) }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        TransportBar {
            Layout.fillWidth: true
            project: controller
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            TrackList {
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                project: controller
                scrollY: timeline.scrollY
                headerHeight: timeline.rulerHeight
            }
            Timeline {
                id: timeline
                Layout.fillWidth: true
                Layout.fillHeight: true
                project: controller
            }
        }
        Mixer {
            Layout.fillWidth: true
            project: controller
        }
        ErrorBar {
            Layout.fillWidth: true
            message: controller.lastError
            visible: message !== ""
            onDismissed: controller.clearError()
        }
    }
}
