import QtQuick
import QtQml
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Jad

// View > Live Loops: Logic's grid. Tracks are the rows and scenes the columns; a cell is a loop with its own settings (the Cell inspector at the bottom), a scene trigger
// below each column starts the cells of the scene, and the Divider column at the right of every row shows what plays and gives the track to its cells or to its regions.
// Click the play button of a cell to start it (Option-click queues it), the stop button to stop it; a click on an empty cell of a row stops the row. Cells are made with
// Edit > Copy to Live Loops Scene, from the selected regions (shortcut menu of a cell), as Session Player and pattern cells, or by recording into them.
Dialog {
    id: root
    required property ProjectController project
    property var grid: ({ tracks: [], cells: [], scenes: 8, sceneNames: [], sceneQuantize: [], performance: false, recordings: [] })
    property var selected: null                     // {trackId, scene} of the cell shown in the inspector
    property var info: ({ exists: false })
    property int tick: 0
    readonly property int cellW: 104
    readonly property int cellH: 40
    readonly property int labelW: 130
    readonly property int dividerW: 62
    readonly property var quantizeKeys: ["off", "0.25", "0.5", "1", "2", "bar", "2bar", "4bar", "8bar"]
    readonly property var cellQuantizeKeys: ["global", "cellEnd"].concat(quantizeKeys)
    function quantizeLabel(k) {
        const labels = { global: qsTr("Global"), cellEnd: qsTr("Cell End"), off: qsTr("Off"), "0.25": qsTr("1/16 note"), "0.5": qsTr("1/8 note"), "1": qsTr("1/4 note"), "2": qsTr("1/2 note"),
                         bar: qsTr("1 bar"), "2bar": qsTr("2 bars"), "4bar": qsTr("4 bars"), "8bar": qsTr("8 bars") }
        return labels[k] !== undefined ? labels[k] : k
    }
    function quantizeBeats(k) {   // the setting that is saved: "off", "cellEnd", "global" or a length in beats
        if (k === "off" || k === "cellEnd" || k === "global") return k
        if (k === "bar") return String(project.barBeats)
        if (k.endsWith("bar")) return String(parseInt(k) * project.barBeats)
        return k
    }
    function keyOfBeats(setting) {   // back to a key
        if (setting === "" || setting === undefined) return "bar"
        if (setting === "off" || setting === "cellEnd" || setting === "global") return setting
        const b = Number(setting)
        for (const k of quantizeKeys) if (k !== "off" && Math.abs(Number(quantizeBeats(k)) - b) < 1e-6) return k
        return "bar"
    }
    function refresh() {
        grid = project.liveGrid()
        if (selected) info = project.liveCellInfo(selected.trackId, selected.scene)
    }
    function cellAt(trackId, scene) {
        for (let i = 0; i < grid.cells.length; ++i) {
            const c = grid.cells[i]
            if (c.trackId === trackId && c.scene === scene) return c
        }
        return null
    }
    function recordingAt(trackId, scene) {
        for (const r of grid.recordings) if (r.trackId === trackId && r.scene === scene) return r
        return null
    }
    function select(trackId, scene) { selected = { trackId: trackId, scene: scene }; info = project.liveCellInfo(trackId, scene) }
    modal: false
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 40 : 980, Math.max(560, labelW + dividerW + grid.scenes * (cellW + 4) + 48))
    height: Math.min(parent ? parent.height - 40 : 640, 150 + grid.tracks.length * (cellH + 4) + 76 + (selected !== null && info.exists ? 140 : 0))
    onAboutToShow: refresh()
    Connections {
        target: root.project
        function onLiveLoopsChanged() { if (root.visible) root.refresh() }
        function onProjectChanged() { if (root.visible) root.refresh() }
    }
    Timer { interval: 60; repeat: true; running: root.visible; onTriggered: root.tick++ }
    background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
    header: Item { height: 0 }

    component Labeled: RowLayout {
        property string text
        spacing: Theme.spacing[2]
        Text { text: parent.text; Layout.preferredWidth: 78; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize }
    }
    component Setting: RowLayout {
        id: st
        property string label
        property string key
        property var choices: []
        property var format: null
        readonly property string current: root.info[key] !== undefined ? String(root.info[key]) : ""
        spacing: Theme.spacing[2]
        Text { text: st.label; Layout.preferredWidth: 70; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize }
        SelectField {
            objectName: "set_" + st.key
            implicitWidth: 110; implicitHeight: 22
            choices: st.choices
            format: st.format
            value: st.key === "quantize" ? root.keyOfBeats(st.current) : st.current
            onChosen: (c) => root.project.setCellSetting(root.selected.trackId, root.selected.scene, st.key, st.key === "quantize" ? root.quantizeBeats(c) : c)
        }
    }
    component Position: RowLayout {
        id: pos
        property string label
        property string key
        spacing: Theme.spacing[2]
        Text { text: pos.label; Layout.preferredWidth: 70; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize }
        NumberField {
            objectName: "num_" + pos.key
            implicitWidth: 72; implicitHeight: 22
            from: 0; to: 4096; decimals: 2; suffix: ""
            value: root.info[pos.key] !== undefined ? root.info[pos.key] : 0
            onCommitted: (v) => root.project.setCellSetting(root.selected.trackId, root.selected.scene, pos.key, v)
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing[2]
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing[2]
            Text { text: qsTr("Live Loops"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeTitleSize; font.weight: Theme.fontTypeTitleWeight }
            Item { Layout.fillWidth: true }
            Text { text: qsTr("Quantize Start"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            SelectField {
                objectName: "liveQuantize"
                implicitWidth: 96
                choices: root.quantizeKeys
                format: root.quantizeLabel
                value: root.keyOfBeats(root.project.projectSettings.liveQuantize)
                onChosen: (k) => root.project.setProjectSetting("liveQuantize", root.quantizeBeats(k))
            }
            ThemedCheckBox {
                objectName: "smartPickup"
                text: qsTr("Smart Pickup")
                checked: root.project.projectSettings.liveSmartPickup !== "0"
                onToggled: root.project.setProjectSetting("liveSmartPickup", checked ? "1" : "0")
            }
            IconButton { objectName: "livePerformance"; implicitHeight: 24; label: qsTr("Performance Recording"); active: root.project.livePerformance; fillActive: true; fillText: Theme.textPrimary; activeColor: Theme.stateRecord; onClicked: root.project.livePerformance = !root.project.livePerformance }
            IconButton { objectName: "liveTrigger"; implicitHeight: 24; label: qsTr("Trigger"); onClicked: root.project.triggerQueued() }
            IconButton { objectName: "liveStopAll"; implicitHeight: 24; label: qsTr("Grid Stop"); onClicked: root.project.stopAllCells() }
            IconButton { objectName: "liveAddScene"; implicitHeight: 24; label: qsTr("Add Scene"); onClicked: root.project.addLiveScene() }
        }
        Text {
            Layout.fillWidth: true
            visible: root.grid.tracks.length === 0
            wrapMode: Text.Wrap
            text: qsTr("There is no audio, instrument or MIDI track yet.")
            color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize
        }
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: gridColumn.width
            contentHeight: gridColumn.height
            ScrollBar.vertical: ScrollBar { }
            ScrollBar.horizontal: ScrollBar { }
            Column {
                id: gridColumn
                spacing: 4
                Repeater {   // one row per track
                    model: root.grid.tracks
                    delegate: Row {
                        id: trackRow
                        required property var modelData
                        spacing: 4
                        Rectangle {
                            width: root.labelW; height: root.cellH; radius: 3
                            color: TrackColors.solid(trackRow.modelData.color)
                            Text { anchors.fill: parent; anchors.margins: 6; text: trackRow.modelData.name; elide: Text.ElideRight; color: "#101010"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; verticalAlignment: Text.AlignVCenter }
                        }
                        Repeater {   // one cell per scene
                            model: root.grid.scenes
                            delegate: Rectangle {
                                id: cell
                                required property int index
                                readonly property var info: { root.grid; return root.cellAt(trackRow.modelData.id, index) }
                                readonly property var rec: { root.grid; return root.recordingAt(trackRow.modelData.id, index) }
                                readonly property string state: rec ? rec.state : (info ? info.state : "empty")
                                readonly property bool playing: state === "playing" || state === "stopping"
                                readonly property bool isSelected: root.selected !== null && root.selected.trackId === trackRow.modelData.id && root.selected.scene === index
                                readonly property real progress: { root.tick; return playing ? root.project.liveProgress(trackRow.modelData.id, index) : -1 }
                                width: root.cellW; height: root.cellH; radius: 3
                                color: info ? TrackColors.solid(trackRow.modelData.color) : Theme.surfaceCanvas
                                opacity: state === "stopped" ? 0.6 : 1
                                border.width: isSelected || state === "queued" || state === "stopping" || state === "recordQueued" ? 2 : 1
                                border.color: isSelected ? Theme.accentPrimary : state === "playing" ? Theme.textPrimary : state === "recording" || state === "recordQueued" ? Theme.stateRecord
                                              : state === "queued" || state === "stopping" ? Theme.stateSolo : Theme.borderStrong
                                SequentialAnimation on border.color {   // a queued cell flashes
                                    running: cell.state === "queued" || cell.state === "recordQueued"
                                    loops: Animation.Infinite
                                    ColorAnimation { to: Theme.textPrimary; duration: 250 }
                                    ColorAnimation { to: Theme.stateSolo; duration: 250 }
                                }
                                Rectangle {   // where the cell is
                                    visible: cell.progress >= 0
                                    x: 0; y: parent.height - 4; height: 3; width: parent.width * Math.max(0, Math.min(1, cell.progress)); color: "#101010"
                                }
                                Text {
                                    anchors.fill: parent; anchors.leftMargin: 28; anchors.rightMargin: 4
                                    visible: cell.info !== null
                                    text: cell.info ? (cell.info.name !== "" ? cell.info.name : trackRow.modelData.name) : ""
                                    elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
                                    color: "#101010"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize
                                }
                                Text {   // the glyph: play, stop, record
                                    x: 9; anchors.verticalCenter: parent.verticalCenter
                                    text: cell.state === "recording" || cell.state === "recordQueued" ? "●" : cell.playing ? "■" : (cell.info !== null || cell.hovered ? "▶" : "")
                                    color: cell.state === "recording" || cell.state === "recordQueued" ? Theme.stateRecord : "#101010"
                                    font.pixelSize: 13
                                }
                                Text {   // the type of the cell, and the number of takes
                                    anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 3
                                    visible: cell.info !== null && (cell.info.type === "session" || cell.info.type === "pattern" || cell.info.takes > 0)
                                    text: cell.info ? (cell.info.takes > 0 ? "" + (cell.info.activeTake + 1) + "/" + cell.info.takes : (cell.info.type === "session" ? "SP" : "P")) : ""
                                    color: "#101010"; font.family: Theme.fontFamily; font.pixelSize: 10
                                }
                                Text {
                                    anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 3
                                    visible: cell.info !== null && !cell.info.loop
                                    text: "→"
                                    color: "#101010"; font.pixelSize: 11
                                }
                                property bool hovered: area.containsMouse
                                MouseArea {
                                    id: area
                                    objectName: "cell_" + trackRow.modelData.id + "_" + cell.index
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onPressed: (mouse) => {
                                        root.select(trackRow.modelData.id, cell.index)
                                        if (mouse.button === Qt.LeftButton && cell.info !== null && !(mouse.modifiers & Qt.ControlModifier)) {
                                            if (cell.state === "recording" || cell.state === "recordQueued") root.project.recordIntoCell(trackRow.modelData.id, cell.index)
                                            else root.project.launchCell(trackRow.modelData.id, cell.index, (mouse.modifiers & Qt.AltModifier) !== 0)
                                        }
                                    }
                                    onReleased: (mouse) => { if (mouse.button === Qt.LeftButton && cell.info !== null) root.project.releaseCell(trackRow.modelData.id, cell.index) }
                                    onClicked: (mouse) => {
                                        if (mouse.button === Qt.RightButton || (mouse.modifiers & Qt.ControlModifier)) cellMenu.popup()
                                        else if (cell.info === null) {
                                            if (trackRow.modelData.armed && mouse.x < 40) root.project.recordIntoCell(trackRow.modelData.id, cell.index)
                                            else if (trackRow.modelData.running) root.project.stopCell(trackRow.modelData.id)   // the stop button of an empty cell
                                            else cellMenu.popup()
                                        }
                                    }
                                }
                                Menu {
                                    id: cellMenu
                                    readonly property string track: trackRow.modelData.id
                                    readonly property string kind: trackRow.modelData.kind
                                    MenuItem { text: qsTr("Queue Cell Playback"); enabled: cell.info !== null; onTriggered: root.project.launchCell(cellMenu.track, cell.index, true) }
                                    MenuItem { text: qsTr("Record Into Cell"); enabled: trackRow.modelData.armed; onTriggered: root.project.recordIntoCell(cellMenu.track, cell.index) }
                                    MenuSeparator { }
                                    MenuItem { text: qsTr("Add Audio File…"); enabled: cellMenu.kind === "audio"; onTriggered: { audioFile.track = cellMenu.track; audioFile.scene = cell.index; audioFile.open() } }
                                    MenuItem { text: qsTr("Set from Selected Regions"); onTriggered: root.project.setCellFromSelection(cellMenu.track, cell.index) }
                                    MenuItem { text: qsTr("Extract Loops"); enabled: cell.info !== null && cell.info.type === "audio"; onTriggered: root.project.extractLoops(cellMenu.track, cell.index) }
                                    MenuItem { text: qsTr("New Session Player Cell"); enabled: cellMenu.kind === "instrument"; onTriggered: root.project.newSessionCell(cellMenu.track, cell.index, { kind: "drummer" }) }
                                    MenuItem { text: qsTr("New Pattern Cell"); enabled: cellMenu.kind === "instrument"; onTriggered: root.project.newPatternCell(cellMenu.track, cell.index) }
                                    MenuItem { text: qsTr("Convert to MIDI Cell"); enabled: cell.info !== null && (cell.info.type === "session" || cell.info.type === "pattern"); onTriggered: root.project.convertToMidiCell(cellMenu.track, cell.index) }
                                    MenuSeparator { }
                                    MenuItem { text: qsTr("Copy Cell to Tracks Area"); enabled: cell.info !== null; onTriggered: root.project.copyCellToTracks(cellMenu.track, cell.index) }
                                    Menu {
                                        id: takesMenu
                                        title: qsTr("Takes")
                                        enabled: cell.info !== null && cell.info.takes > 0
                                        Instantiator {
                                            model: cell.info ? cell.info.takeNames : []
                                            delegate: MenuItem {
                                                required property int index
                                                required property string modelData
                                                text: (index === cell.info.activeTake ? "✓ " : "   ") + modelData
                                                onTriggered: root.project.takeAction(cellMenu.track, cell.index, "select", index)
                                            }
                                            onObjectAdded: (i, o) => takesMenu.insertItem(i, o)
                                            onObjectRemoved: (i, o) => takesMenu.removeItem(o)
                                        }
                                        MenuSeparator { }
                                        MenuItem { text: qsTr("Rename Take…"); onTriggered: renameBox.openFor(cellMenu.track, cell.index) }
                                        MenuItem { text: qsTr("Delete Take"); onTriggered: root.project.takeAction(cellMenu.track, cell.index, "delete", -1) }
                                        MenuItem { text: qsTr("Flatten"); onTriggered: root.project.takeAction(cellMenu.track, cell.index, "flatten", -1) }
                                        Menu {
                                            title: qsTr("Copy Take to")
                                            MenuItem { text: qsTr("New Scene"); onTriggered: root.project.takeAction(cellMenu.track, cell.index, "copyTo", -1, "newScene") }
                                            MenuItem { text: qsTr("Next Empty Cell Right"); onTriggered: root.project.takeAction(cellMenu.track, cell.index, "copyTo", -1, "nextRight") }
                                            MenuItem { text: qsTr("New Track"); onTriggered: root.project.takeAction(cellMenu.track, cell.index, "copyTo", -1, "newTrack") }
                                            MenuItem { text: qsTr("Next Matching Empty Cell Below"); onTriggered: root.project.takeAction(cellMenu.track, cell.index, "copyTo", -1, "nextBelow") }
                                        }
                                        Menu {
                                            title: qsTr("Move Take to")
                                            MenuItem { text: qsTr("New Scene"); onTriggered: root.project.takeAction(cellMenu.track, cell.index, "moveTo", -1, "newScene") }
                                            MenuItem { text: qsTr("Next Empty Cell Right"); onTriggered: root.project.takeAction(cellMenu.track, cell.index, "moveTo", -1, "nextRight") }
                                            MenuItem { text: qsTr("New Track"); onTriggered: root.project.takeAction(cellMenu.track, cell.index, "moveTo", -1, "newTrack") }
                                            MenuItem { text: qsTr("Next Matching Empty Cell Below"); onTriggered: root.project.takeAction(cellMenu.track, cell.index, "moveTo", -1, "nextBelow") }
                                        }
                                        Menu {
                                            title: qsTr("Unpack Take Cell to")
                                            MenuItem { text: qsTr("New Scenes"); onTriggered: root.project.takeAction(cellMenu.track, cell.index, "unpack", -1, "newScene") }
                                            MenuItem { text: qsTr("Next Empty Cells Right"); onTriggered: root.project.takeAction(cellMenu.track, cell.index, "unpack", -1, "nextRight") }
                                            MenuItem { text: qsTr("New Tracks"); onTriggered: root.project.takeAction(cellMenu.track, cell.index, "unpack", -1, "newTrack") }
                                            MenuItem { text: qsTr("Next Matching Empty Cells Below"); onTriggered: root.project.takeAction(cellMenu.track, cell.index, "unpack", -1, "nextBelow") }
                                        }
                                    }
                                    MenuSeparator { }
                                    MenuItem { text: qsTr("Clear Cell"); enabled: cell.info !== null; onTriggered: root.project.clearCell(cellMenu.track, cell.index) }
                                }
                            }
                        }
                        Rectangle {   // the Divider column: what plays on the track, resume/pause, and Track Activation (cells or regions)
                            width: root.dividerW; height: root.cellH; radius: 3
                            color: Theme.surfaceRaised
                            border.color: Theme.borderStrong
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 4; spacing: 4
                                Rectangle {
                                    Layout.preferredWidth: 14; Layout.preferredHeight: 14; radius: 7
                                    color: trackRow.modelData.running ? Theme.statePlay : trackRow.modelData.queued ? Theme.stateSolo : "transparent"
                                    border.color: Theme.textSecondary
                                }
                                IconButton {
                                    objectName: "pause_" + trackRow.modelData.id
                                    implicitHeight: 22; implicitWidth: 22
                                    label: trackRow.modelData.paused ? "▶" : "❚❚"
                                    onClicked: trackRow.modelData.paused ? root.project.resumeCell(trackRow.modelData.id) : root.project.pauseCell(trackRow.modelData.id)
                                }
                                IconButton {
                                    objectName: "activate_" + trackRow.modelData.id
                                    implicitHeight: 22; implicitWidth: 22
                                    label: trackRow.modelData.cells ? "▲" : "▽"
                                    active: trackRow.modelData.cells
                                    onClicked: trackRow.modelData.cells ? root.project.activateTrackRegions(trackRow.modelData.id) : root.project.activateTrackCells(trackRow.modelData.id)
                                }
                            }
                        }
                    }
                }
                Row {   // the scene triggers, below the columns
                    spacing: 4
                    Item { width: root.labelW; height: 30 }
                    Repeater {
                        model: root.grid.scenes
                        delegate: Rectangle {
                            id: scene
                            required property int index
                            width: root.cellW; height: 30; radius: 3
                            color: Theme.surfaceRaised
                            border.color: Theme.borderStrong
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 3; spacing: 3
                                IconButton {
                                    objectName: "launchScene" + scene.index
                                    implicitHeight: 22; implicitWidth: 22
                                    label: "▶"
                                    onClicked: root.project.launchScene(scene.index, false)
                                }
                                TextInput {
                                    Layout.fillWidth: true
                                    text: root.grid.sceneNames[scene.index] === "" || root.grid.sceneNames[scene.index] === undefined ? qsTr("Scene %1").arg(scene.index + 1) : root.grid.sceneNames[scene.index]
                                    color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize
                                    selectByMouse: true; clip: true
                                    onEditingFinished: root.project.renameLiveScene(scene.index, text === qsTr("Scene %1").arg(scene.index + 1) ? "" : text)
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton
                                onClicked: sceneMenu.popup()
                            }
                            Menu {
                                id: sceneMenu
                                MenuItem { text: qsTr("Queue Scene"); onTriggered: root.project.launchScene(scene.index, true) }
                                MenuItem { text: qsTr("Copy Scene to Tracks Area"); onTriggered: root.project.copySceneToTracks(scene.index) }
                                Menu {
                                    id: sceneQuantizeMenu
                                    title: qsTr("Quantize Start")
                                    Instantiator {
                                        model: root.cellQuantizeKeys.filter(k => k !== "cellEnd")
                                        delegate: MenuItem {
                                            required property string modelData
                                            text: root.quantizeLabel(modelData)
                                            onTriggered: root.project.setProjectSetting("sceneQuantize." + scene.index, root.quantizeBeats(modelData))
                                        }
                                        onObjectAdded: (i, o) => sceneQuantizeMenu.insertItem(i, o)
                                        onObjectRemoved: (i, o) => sceneQuantizeMenu.removeItem(o)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        // the Cell inspector
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.selected !== null && root.info.exists ? 132 : 0
            visible: root.selected !== null && root.info.exists
            color: Theme.surfaceCanvas; radius: 3; border.color: Theme.borderSubtle
            RowLayout {
                anchors.fill: parent; anchors.margins: 8; spacing: 16
                ColumnLayout {
                    spacing: 4
                    Layout.alignment: Qt.AlignTop
                    Text { text: qsTr("Cell"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; font.weight: Font.DemiBold }
                    RowLayout {
                        Text { text: qsTr("Name"); Layout.preferredWidth: 70; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize }
                        Rectangle {
                            implicitWidth: 110; implicitHeight: 22; radius: 3; color: Theme.surfaceRaised
                            TextInput {
                                objectName: "cellName"
                                anchors.fill: parent; anchors.margins: 4
                                text: root.info.name !== undefined ? root.info.name : ""
                                color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize; clip: true; selectByMouse: true
                                onEditingFinished: root.project.setCellSetting(root.selected.trackId, root.selected.scene, "name", text)
                            }
                        }
                    }
                    ThemedCheckBox { objectName: "cellLoop"; text: qsTr("Loop"); checked: root.info.loop === true; onToggled: root.project.setCellSetting(root.selected.trackId, root.selected.scene, "loop", checked) }
                    Position { label: qsTr("Cell Length"); key: "lengthBeats" }
                }
                ColumnLayout {
                    spacing: 4
                    Layout.alignment: Qt.AlignTop
                    Text { text: qsTr("Play From"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; font.weight: Font.DemiBold }
                    Setting { label: qsTr("Play From"); key: "playFrom"; choices: ["start", "stopPosition", "playingCell", "playhead"]; format: (k) => ({ start: qsTr("Start"), stopPosition: qsTr("Stop Position"), playingCell: qsTr("Playing Cell Position"), playhead: qsTr("Playhead Position") })[k] }
                    Position { label: qsTr("Start"); key: "startBeats" }
                    Position { label: qsTr("Loop Start"); key: "loopStartBeats" }
                    Position { label: qsTr("Loop Length"); key: "loopLengthBeats" }
                }
                ColumnLayout {
                    spacing: 4
                    Layout.alignment: Qt.AlignTop
                    Text { text: qsTr("Playback"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; font.weight: Font.DemiBold }
                    Setting { label: qsTr("Trigger Mode"); key: "trigger"; choices: ["startStop", "momentary", "retrigger"]; format: (k) => ({ startStop: qsTr("Start/Stop"), momentary: qsTr("Momentary"), retrigger: qsTr("Retrigger") })[k] }
                    Setting { label: qsTr("Quantize"); key: "quantize"; choices: root.cellQuantizeKeys; format: root.quantizeLabel }
                    Setting { label: qsTr("Q Loop Start"); key: "qLoopStart"; choices: ["off", "on", "onPreserve"]; format: (k) => ({ off: qsTr("Off"), on: qsTr("On"), onPreserve: qsTr("On, Preserve Start") })[k] }
                }
                ColumnLayout {
                    spacing: 4
                    Layout.alignment: Qt.AlignTop
                    Text { text: qsTr("Recording"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; font.weight: Font.DemiBold }
                    Setting { label: qsTr("Recording"); key: "recording"; choices: ["replace", "takes", "merge"]; format: (k) => ({ replace: qsTr("Replace"), takes: qsTr("Takes"), merge: qsTr("Merge") })[k] }
                    Setting { label: qsTr("Rec-Length"); key: "recLength"; choices: ["cell", "bars", "beats"]; format: (k) => ({ cell: qsTr("Cell Length"), bars: qsTr("Automatic (Bars)"), beats: qsTr("Automatic (Beats)") })[k] }
                    Setting { label: qsTr("At Rec-End"); key: "atRecEnd"; choices: ["play", "continue"]; format: (k) => ({ play: qsTr("Play Cell"), continue: qsTr("Continue Recording") })[k] }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            IconButton { implicitHeight: 26; label: qsTr("Close"); onClicked: root.close() }
        }
    }
    FileDialog {
        id: audioFile
        property string track
        property int scene
        title: qsTr("Add Audio File")
        nameFilters: [qsTr("Audio files (*.wav *.aif *.aiff *.mp3 *.flac)"), qsTr("All files (*)")]
        onAccepted: root.project.addAudioFileToCell(selectedFile, track, scene)
    }
    // key commands of the grid: Trigger Live Loops (Return), Queue Cell/Scene Playback (Option-Return), Record Into Cell (Option-R), Stop (the zero key stops everything)
    Shortcut { sequence: "Return"; enabled: root.visible && root.selected !== null; onActivated: root.project.launchCell(root.selected.trackId, root.selected.scene, false) }
    Shortcut { sequence: "Alt+Return"; enabled: root.visible && root.selected !== null; onActivated: root.project.launchCell(root.selected.trackId, root.selected.scene, true) }
    Shortcut { sequence: "Alt+R"; enabled: root.visible && root.selected !== null; onActivated: root.project.recordIntoCell(root.selected.trackId, root.selected.scene) }
    Shortcut { sequence: "Alt+T"; enabled: root.visible; onActivated: root.project.triggerQueued() }
    Dialog {   // the name of a take
        id: renameBox
        property string track
        property int scene
        function openFor(t, s) { track = t; scene = s; nameField.text = ""; open() }
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 280
        background: Rectangle { color: Theme.surfacePanel; border.color: Theme.borderStrong; radius: Theme.radiusDialog }
        header: Item { height: 0 }
        contentItem: ColumnLayout {
            Text { text: qsTr("Rename Take"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 24; radius: 3; color: Theme.surfaceRaised
                TextInput { id: nameField; anchors.fill: parent; anchors.margins: 4; color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize; selectByMouse: true }
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                IconButton { implicitHeight: 24; label: qsTr("Cancel"); onClicked: renameBox.close() }
                IconButton { implicitHeight: 24; label: qsTr("OK"); active: true; onClicked: { root.project.takeAction(renameBox.track, renameBox.scene, "rename", -1, nameField.text); renameBox.close() } }
            }
        }
    }
}
