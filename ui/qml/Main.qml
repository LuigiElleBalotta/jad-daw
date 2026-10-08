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

    function colorSelected(name) { for (const id of controller.selectedTrackIds) controller.setTrackColor(id, name) }
    function togglePlay() { controller.playing ? controller.stop() : controller.play() }
    function toggleLoop() { controller.setLoopBeats(0, controller.loopEnabled ? 0 : controller.beatsPerBar * 4) }

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
        "track.mute": () => controller.toggleMuteSelected(),
        "track.solo": () => controller.toggleSoloSelected(),
        "track.newAudio": () => controller.addTrack("audio"),
        "track.newInstrument": () => controller.addTrack("instrument"),
        "track.newBus": () => controller.addTrack("bus"),
        "track.delete": () => controller.deleteSelectedTracks(),
        "track.rename": () => { if (controller.selectedTrackIds.length > 0) trackList.beginRename(controller.selectedTrackIds[0]) },
        "track.color.purple": () => root.colorSelected("purple"),
        "track.color.indigo": () => root.colorSelected("indigo"),
        "track.color.blue": () => root.colorSelected("blue"),
        "track.color.teal": () => root.colorSelected("teal"),
        "track.color.green": () => root.colorSelected("green"),
        "track.color.yellow": () => root.colorSelected("yellow"),
        "track.color.orange": () => root.colorSelected("orange"),
        "track.color.red": () => root.colorSelected("red"),
        "track.color.pink": () => root.colorSelected("pink"),
        "track.color.magenta": () => root.colorSelected("magenta"),
        "track.height.compact": () => { controller.trackHeightIndex = 0 },
        "track.height.normal": () => { controller.trackHeightIndex = 1 },
        "track.height.large": () => { controller.trackHeightIndex = 2 },
        "track.height.xlarge": () => { controller.trackHeightIndex = 3 },
        "edit.selectAll": () => controller.selectAll(),
        "edit.deselectAll": () => controller.clearSelection(),
        "transport.stop": () => controller.stop(),
        "transport.barBack": () => controller.barBack(),
        "transport.barForward": () => controller.barForward(),
        "transport.goToPosition": () => controlBar.lcd.editPosition(),
        "view.mixer": () => { controller.mixerVisible = !controller.mixerVisible },
        "tool.pointer": () => { controller.tool = "pointer" },
        "tool.pencil": () => { controller.tool = "pencil" },
        "tool.eraser": () => { controller.tool = "eraser" },
        "tool.scissors": () => { controller.tool = "scissors" },
        "tool.glue": () => { controller.tool = "glue" },
        "snap.off": () => { controller.snap = "off" },
        "snap.bar": () => { controller.snap = "bar" },
        "snap.half": () => { controller.snap = "half" },
        "snap.quarter": () => { controller.snap = "quarter" },
        "snap.eighth": () => { controller.snap = "eighth" },
        "snap.sixteenth": () => { controller.snap = "sixteenth" },
        "drag.overlap": () => {},
        "nav.followPlayhead": () => { controller.followPlayhead = !controller.followPlayhead },
        "view.zoomIn": () => timeline.zoomBy(1.25, timeline.width / 2),
        "view.zoomOut": () => timeline.zoomBy(0.8, timeline.width / 2)
    })
    // the displayed state of real toggles and radio entries (stubs keep their own)
    readonly property var states: ({
        "transport.loop": controller.loopEnabled,
        "view.mixer": controller.mixerVisible,
        "tool.pointer": controller.tool === "pointer",
        "tool.pencil": controller.tool === "pencil",
        "tool.eraser": controller.tool === "eraser",
        "tool.scissors": controller.tool === "scissors",
        "tool.glue": controller.tool === "glue",
        "snap.off": controller.snap === "off",
        "snap.bar": controller.snap === "bar",
        "snap.half": controller.snap === "half",
        "snap.quarter": controller.snap === "quarter",
        "snap.eighth": controller.snap === "eighth",
        "snap.sixteenth": controller.snap === "sixteenth",
        "drag.overlap": true,
        "nav.followPlayhead": controller.followPlayhead,
        "track.height.compact": controller.trackHeightIndex === 0,
        "track.height.normal": controller.trackHeightIndex === 1,
        "track.height.large": controller.trackHeightIndex === 2,
        "track.height.xlarge": controller.trackHeightIndex === 3
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
        ToolBar {
            Layout.fillWidth: true
            project: controller
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            TrackList {
                id: trackList
                Layout.preferredWidth: 230
                Layout.fillHeight: true
                project: controller
                scrollY: timeline.scrollY
                headerHeight: timeline.rulerHeight
                onStubTriggered: (id, on) => actionRegistry.stubTriggered(id, on)
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
