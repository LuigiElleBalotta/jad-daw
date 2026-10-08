import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQml.Models
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
    ActionRegistry { id: actionRegistry; objectName: "registry" }
    Component.onCompleted: ActionHub.registry = actionRegistry

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

    MessageDialog {
        id: openErrorDialog
        title: qsTr("Cannot open project")
        buttons: MessageDialog.Ok
    }
    Connections {
        target: controller
        function onProjectOpenFailed(message) {
            openErrorDialog.text = message
            openErrorDialog.open()
        }
    }

    // what each real action does; every other id in the table is a stub (see actions/actions.json)
    readonly property var handlers: ({
        "file.new": () => newDialog.open(),
        "file.open": () => openDialog.open(),
        "file.save": () => controller.saveProject(),
        "file.quit": () => Qt.quit(),
        "edit.undo": () => controller.undo(),
        "edit.redo": () => controller.redo(),
        "edit.delete": () => timeline.deleteSelected(),
        "transport.playStop": () => root.togglePlay(),
        "transport.toStart": () => controller.locateBeats(0),
        "transport.loop": () => root.toggleLoop(),
        "track.mute": () => root.forSelectedTracks((id) => controller.toggleMute(id)),
        "track.solo": () => root.forSelectedTracks((id) => controller.toggleSolo(id)),
        "transport.stop": () => controller.stop(),
        "transport.barBack": () => controller.barBack(),
        "transport.barForward": () => controller.barForward(),
        "transport.goToPosition": () => controlBar.lcd.editPosition(),
        "view.mixer": () => { controller.mixerVisible = !controller.mixerVisible },
        "view.zoomIn": () => timeline.zoomBy(1.25, timeline.width / 2),
        "view.zoomOut": () => timeline.zoomBy(0.8, timeline.width / 2)
    })
    // the displayed state of real toggles and radio entries (stubs keep their own)
    readonly property var states: ({
        "transport.loop": controller.loopEnabled,
        "view.mixer": controller.mixerVisible
    })
    readonly property var disabledStates: ({
        "file.save": !controller.hasProject
    })

    property var actionMap: ({})
    property int actionsVersion: 0
    function actionFor(id) { return actionMap[id] ?? null }

    Instantiator {
        id: actions
        model: actionRegistry.ids()
        delegate: JadAction {
            required property string modelData
            registry: actionRegistry
            actionId: modelData
            handler: root.handlers[modelData] ?? null
            on: root.states[modelData] ?? false
            enabled: !(root.disabledStates[modelData] ?? false)
        }
        onObjectAdded: (index, object) => {
            root.actionMap[object.actionId] = object
            root.actionsVersion++
            ActionHub.add(object)
        }
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 56
    }
    Connections {
        target: actionRegistry
        function onNotImplemented(label) { toast.show(qsTr("%1: not implemented yet").arg(label)) }
    }

    menuBar: ActionMenuBar {
        registry: actionRegistry
        actionFor: root.actionFor
        actionsVersion: root.actionsVersion
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ControlBar {
            id: controlBar
            Layout.fillWidth: true
            project: controller
            onMessage: (text) => toast.show(text)
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
            visible: controller.mixerVisible
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
