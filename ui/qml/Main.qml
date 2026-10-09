import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import QtQml.Models
import Jad

ApplicationWindow {
    id: root
    width: 1280
    height: 860
    minimumWidth: 900
    // the docked Mixer is never shorter than its strips with their legend: the window leaves room for it and for the rest
    minimumHeight: 200 + (controller.mixerVisible && !controller.mixerDetached ? 584 : 0)
    visible: true
    title: "JAD Daw"
    color: Theme.surfaceApp

    property alias project: controller
    property bool editorsVisible: false
    ProjectController { id: controller }
    ActionRegistry { id: actionRegistry; objectName: "registry" }
    Component.onCompleted: ActionHub.registry = actionRegistry

    function colorSelected(name) { controller.setSelectedColor(name) }
    function setSelectedToggle(actionId, on) { controller.setTrackToggle(actionId, controller.selectedTrackIds[0], on) }
    // for screenshots: opens the menu at `index` of the menu bar
    function showMenu(index) { const m = menuBar.menuAt(index); if (m) m.open() }
    // Smart Controls, the Mixer and the Editors share the lower area of the window, as in Logic: showing one hides the others
    // (a Mixer in its own window is not part of it). Choosing the one that is shown hides it.
    function showLowerPane(which) {
        const mixerDocked = !controller.mixerDetached
        const shown = which === "mixer" ? (controller.mixerVisible && mixerDocked) : (which === "editors" ? root.editorsVisible : controller.smartControlsVisible)
        const on = which === "mixer" && !mixerDocked ? !controller.mixerVisible : !shown
        if (which === "mixer" && !mixerDocked) { controller.mixerVisible = on; return }
        controller.smartControlsVisible = which === "smart" && on
        root.editorsVisible = which === "editors" && on
        if (mixerDocked) controller.mixerVisible = which === "mixer" && on
    }
    function togglePlay() { controller.playing ? controller.stop() : controller.play() }
    function toggleLoop() { controller.toggleLoop() }

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

    FolderDialog {
        id: saveAsDialog
        property bool openCopy: true
        title: openCopy ? qsTr("Save the project as") : qsTr("Save a copy of the project as")
        onAccepted: controller.saveProjectAs(selectedFolder, openCopy)
    }
    FileDialog {
        id: bounceDialog
        title: qsTr("Bounce the project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "wav"
        nameFilters: [qsTr("WAV audio (*.wav)")]
        onAccepted: controller.bounceProject(selectedFile)
    }
    FileDialog {
        id: importDialog
        title: qsTr("Import audio files")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("WAV audio (*.wav)")]
        onAccepted: controller.importAudioFilesHere(selectedFiles)
    }

    MetronomeSettings { id: metronomeDialog; project: controller }
    NewTracksDialog { id: newTracksDialog; project: controller }
    AboutDialog { id: aboutDialog; objectName: "aboutDialog" }
    // the Mixer in a window of its own (View > Mixer when it is detached, Window > Open Mixer)
    Window {
        id: mixerWindow
        objectName: "mixerWindow"
        title: qsTr("JAD Daw - Mixer")
        width: 960
        height: 620
        minimumWidth: 360
        minimumHeight: 24 + 536  // the bar and the shortest strip with its legend
        color: Theme.surfaceCanvas
        visible: controller.mixerVisible && controller.mixerDetached
        onClosing: controller.mixerVisible = false
        MixerView {
            anchors.fill: parent
            project: controller
            detached: true
            onDetachToggled: controller.mixerDetached = false
        }
    }
    PluginManager {
        id: pluginManager
        objectName: "pluginManager"
        project: controller
        visible: controller.pluginManagerOpen
        onVisibleChanged: if (!visible) controller.pluginManagerOpen = false
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
        "help.about": () => aboutDialog.open(),
        "file.new": () => newDialog.open(),
        "file.open": () => openDialog.open(),
        "file.save": () => controller.saveProject(),
        "file.saveAs": () => { saveAsDialog.openCopy = true; saveAsDialog.open() },
        "file.saveACopyAs": () => { saveAsDialog.openCopy = false; saveAsDialog.open() },
        "file.importAudio": () => importDialog.open(),
        "file.bounce": () => bounceDialog.open(),
        "file.quit": () => Qt.quit(),
        "edit.undo": () => controller.undo(),
        "edit.redo": () => controller.redo(),
        "edit.delete": () => editorArea.pianoFocused ? editorArea.piano.deleteSelected() : timeline.deleteSelected(),
        "edit.cut": () => editorArea.pianoFocused ? editorArea.piano.cut() : controller.cutSelectedRegions(),
        "edit.copy": () => editorArea.pianoFocused ? editorArea.piano.copy() : controller.copySelectedRegions(),
        "edit.paste": () => editorArea.pianoFocused ? editorArea.piano.paste() : controller.pasteRegions(false),
        "edit.pasteAtOriginalPosition": () => controller.pasteRegions(true),
        "edit.duplicate": () => editorArea.pianoFocused ? editorArea.piano.duplicate() : controller.duplicateSelectedRegions(),
        "edit.selectAllFollowing": () => editorArea.pianoFocused ? editorArea.piano.selectFollowing() : controller.selectFollowingRegions(false),
        "edit.selectAllFollowingOfSameTrack": () => controller.selectFollowingRegions(true),
        "edit.selectMutedRegions": () => controller.selectMutedRegions(),
        "edit.selectOverlappedRegions": () => controller.selectOverlappedRegions(),
        "edit.selectSameColoredRegions": () => controller.selectSameColoredRegions(),
        "edit.selectSelectEmptyRegions": () => controller.selectEmptyRegions(),
        "edit.selectNextRegion": () => controller.selectNeighbourRegion(1),
        "edit.selectPreviousRegion": () => controller.selectNeighbourRegion(-1),
        "edit.selectInvertSelection": () => editorArea.pianoFocused ? editorArea.piano.invertSelection() : controller.invertRegionSelection(),
        "edit.lengthHalve": () => controller.halveSelectedRegions(),
        "edit.lengthDouble": () => controller.doubleSelectedRegions(),
        "edit.moveNudgeLeft": () => editorArea.pianoFocused ? editorArea.piano.nudge(-editorArea.piano.gridUnit) : controller.nudgeSelectedRegions(-1),
        "edit.moveNudgeRight": () => editorArea.pianoFocused ? editorArea.piano.nudge(editorArea.piano.gridUnit) : controller.nudgeSelectedRegions(1),
        "edit.trimRemoveOverlaps": () => controller.removeOverlaps(),
        "edit.trimRegionEndToNextRegion": () => controller.regionEndToNextRegion(),
        "region.mute": () => controller.toggleMuteSelectedRegions(),
        "transport.playStop": () => root.togglePlay(),
        "transport.toStart": () => controller.locateBeats(0),
        "transport.loop": () => root.toggleLoop(),
        "track.mute": () => controller.toggleMuteSelected(),
        "track.solo": () => controller.toggleSoloSelected(),
        "track.newAudio": () => controller.addTrack("audio"),
        "track.newInstrument": () => controller.addTrack("instrument"),
        "track.newBus": () => controller.addTrack("bus"),
        "track.delete": () => controller.deleteSelectedTracks(),
        "track.showInTracks": (on) => { if (controller.selectedTrackIds.length > 0) controller.setShowInTracks(controller.selectedTrackIds[0], on) },
        "track.rename": () => { if (controller.selectedTrackIds.length > 0) trackList.beginRename(controller.selectedTrackIds[0]) },
        "track.recordArm": (on) => root.setSelectedToggle("track.recordArm", on),
        "track.inputMonitor": (on) => root.setSelectedToggle("track.inputMonitor", on),
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
        "edit.selectAll": () => editorArea.pianoFocused ? editorArea.piano.selectAll() : controller.selectAll(),
        "edit.splitAtPlayhead": () => controller.splitSelectedAtPlayhead(),
        "edit.joinRegions": () => controller.joinSelected(),
        "edit.deselectAll": () => editorArea.pianoFocused ? editorArea.piano.deselectAll() : controller.clearSelection(),
        "transport.stop": () => controller.stop(),
        "transport.barBack": () => controller.barBack(),
        "transport.barForward": () => controller.barForward(),
        "transport.goToPosition": () => controlBar.lcd.editPosition(),
        "view.mixer": () => root.showLowerPane("mixer"),
        "window.openMixer": () => { controller.mixerDetached = true },
        "view.editors": () => root.showLowerPane("editors"),
        "window.pluginManager": () => { controller.pluginManagerOpen = !controller.pluginManagerOpen },
        "view.library": () => { controller.libraryVisible = !controller.libraryVisible },
        "track.globalTracks": () => { controller.globalTracksVisible = !controller.globalTracksVisible },
        "navigate.createMarker": () => controller.createMarkerAtPlayhead(),
        "transport.metronome": () => controller.setMetronome(!controller.metronomeOn),
        "track.newTracks": () => newTracksDialog.open(),
        "transport.record": () => controller.toggleRecording(),
        "transport.countIn": () => { controller.countInEnabled = !controller.countInEnabled },
        "record.countInNone": () => { controller.countInEnabled = false },
        "record.countIn1Bar": () => { controller.countInChoice = 1; controller.countInEnabled = true },
        "record.countIn2Bar": () => { controller.countInChoice = 2; controller.countInEnabled = true },
        "record.countIn3Bar": () => { controller.countInChoice = 3; controller.countInEnabled = true },
        "record.countIn4Bar": () => { controller.countInChoice = 4; controller.countInEnabled = true },
        "record.countIn5Bar": () => { controller.countInChoice = 5; controller.countInEnabled = true },
        "record.countIn6Bar": () => { controller.countInChoice = 6; controller.countInEnabled = true },
        "record.countIn1Of4": () => { controller.countInChoice = -1; controller.countInEnabled = true },
        "record.countIn2Of4": () => { controller.countInChoice = -2; controller.countInEnabled = true },
        "record.countIn3Of4": () => { controller.countInChoice = -3; controller.countInEnabled = true },
        "record.metronomeSettings": () => metronomeDialog.open(),
        "mix.showAutomation": () => { controller.automationVisible = !controller.automationVisible },
        "window.openMainWindow": () => { root.raise(); root.requestActivate() },
        "window.openSmartControls": () => root.showLowerPane("smart"),
        "window.openPianoRoll": () => { root.showLowerPane("editors"); editorArea.tab = 0 },
        "window.openScoreEditor": () => { root.showLowerPane("editors"); editorArea.tab = 1 },
        "window.openStepSequencer": () => { root.showLowerPane("editors"); editorArea.tab = 2 },
        "view.zoomFit": () => timeline.zoomToFit(),
        "view.enterFullScreen": () => { root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen },
        "window.minimize": () => root.showMinimized(),
        "window.zoom": () => { root.visibility = root.visibility === Window.Maximized ? Window.Windowed : Window.Maximized },
        "track.deleteUnusedTracks": () => controller.deleteUnusedTracks(),
        "edit.selectAllInsideLocators": () => controller.selectInsideLocators(),
        "edit.selectDeselectOutsideLocators": () => controller.deselectOutsideLocators(),
        "edit.selectSimilarRegions": () => controller.selectSimilarRegions(),
        "edit.selectEqualRegions": () => controller.selectEqualRegions(),
        "edit.moveToPlayhead": () => controller.moveSelectedToPlayhead(),
        "edit.splitRegionsAtLocators": () => controller.splitAtLocators(),
        "edit.deleteAndMove": () => controller.deleteSelectedAndMove(),
        "navigate.deleteMarkerAtPlayheadPosition": () => controller.deleteMarkerAtPlayhead(),
        "navigate.setLocatorsBySelectionAndEnableCycle": () => controller.setLocatorsBySelection(false),
        "navigate.setRoundedLocatorsBySelectionAndEnableCycle": () => controller.setLocatorsBySelection(true),
        "navigate.moveLocatorsForwardByCycleLength": () => controller.moveLocators(1),
        "navigate.moveLocatorsBackwardsByCycleLength": () => controller.moveLocators(-1),
        "navigate.scrollToSelection": () => timeline.scrollToSelection(),
        "view.inspector": () => { controller.inspectorVisible = !controller.inspectorVisible },
        "view.smartControls": () => root.showLowerPane("smart"),
        "tool.pointer": () => { controller.tool = "pointer" },
        "tool.pencil": () => { controller.tool = "pencil" },
        "tool.eraser": () => { controller.tool = "eraser" },
        "tool.scissors": () => { controller.tool = "scissors" },
        "tool.glue": () => { controller.tool = "glue" },
        "tool.zoom": () => { controller.tool = "zoom" },
        "tool.mute": () => { controller.tool = "mute" },
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
        "view.editors": root.editorsVisible,
        "view.library": controller.libraryVisible,
        "track.globalTracks": controller.globalTracksVisible,
        "transport.metronome": controller.metronomeOn,
        "transport.record": controller.recording,
        "transport.countIn": controller.countInEnabled,
        "record.countInNone": !controller.countInEnabled,
        "record.countIn1Bar": controller.countInEnabled && controller.countInChoice === 1,
        "record.countIn2Bar": controller.countInEnabled && controller.countInChoice === 2,
        "record.countIn3Bar": controller.countInEnabled && controller.countInChoice === 3,
        "record.countIn4Bar": controller.countInEnabled && controller.countInChoice === 4,
        "record.countIn5Bar": controller.countInEnabled && controller.countInChoice === 5,
        "record.countIn6Bar": controller.countInEnabled && controller.countInChoice === 6,
        "record.countIn1Of4": controller.countInEnabled && controller.countInChoice === -1,
        "record.countIn2Of4": controller.countInEnabled && controller.countInChoice === -2,
        "record.countIn3Of4": controller.countInEnabled && controller.countInChoice === -3,
        "mix.showAutomation": controller.automationVisible,
        "view.inspector": controller.inspectorVisible,
        "view.smartControls": controller.smartControlsVisible,
        "tool.pointer": controller.tool === "pointer",
        "tool.pencil": controller.tool === "pencil",
        "tool.eraser": controller.tool === "eraser",
        "tool.scissors": controller.tool === "scissors",
        "tool.glue": controller.tool === "glue",
        "tool.zoom": controller.tool === "zoom",
        "tool.mute": controller.tool === "mute",
        "track.showInTracks": controller.selectedShowInTracks,
        "track.recordArm": controller.selectedRecordArm,
        "track.inputMonitor": controller.selectedInputMonitor,
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
        "file.save": !controller.hasProject,
        "file.saveAs": !controller.hasProject,
        "file.saveACopyAs": !controller.hasProject,
        "file.importAudio": !controller.hasProject,
        "file.bounce": !controller.hasProject,
        "track.showInTracks": !controller.selectedCanHide,
        "track.recordArm": controller.selectedTrackIds.length === 0,
        "track.inputMonitor": controller.selectedTrackIds.length === 0
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
    Connections {
        target: controller
        function onNotice(message) { toast.show(message) }
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
            LeftColumn {
                id: leftColumn
                Layout.fillHeight: true
                Layout.preferredWidth: width
                project: controller
            }
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
                onEditRequested: { if (!root.editorsVisible) root.showLowerPane("editors") }
                Layout.fillWidth: true
                Layout.fillHeight: true
                project: controller
            }
        }
        SmartControls {
            Layout.fillWidth: true
            Layout.preferredHeight: controller.smartControlsHeight
            visible: controller.smartControlsVisible
            project: controller
            Splitter {  // the top edge: dragging up makes the pane taller (the setter keeps the height in range)
                orientation: Qt.Vertical
                anchors.top: parent.top
                width: parent.width
                z: 10
                onDragged: (dy) => controller.smartControlsHeight = controller.smartControlsHeight - dy
            }
        }
        EditorArea {
            id: editorArea
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            visible: root.editorsVisible
            project: controller
        }
        Mixer {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            maxHeight: root.height - 200 - (root.editorsVisible ? 160 : 0)
            visible: controller.mixerVisible && !controller.mixerDetached
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
