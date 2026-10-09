import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Jad

// The Piano Roll of the Editors area: a keyboard, a ruler and a grid with the notes of the selected MIDI region, a velocity lane
// below it, a left pane with the editor's settings (quantize, default velocity) and a local bar with its menus.
// Pointer: select (drag on empty space for a rectangle), move (drag), resize (either end of a note); Option-click draws a note. Pencil: draw a note. Eraser: delete.
// Every change is one replace_region command (one undo step).
Item {
    id: root
    required property ProjectController project

    readonly property string regionId: project.selectedRegionIds.length > 0 ? project.selectedRegionIds[0] : ""
    readonly property var info: { project.revision; return regionId !== "" ? project.regionInfo(regionId) : ({ found: false }) }
    readonly property bool hasMidi: info.found === true && info.audio !== true
    readonly property real regionStart: hasMidi ? info.startBeats : 0
    readonly property var notes: { project.revision; return hasMidi ? project.regionNotes(regionId) : [] }
    property var working: null                      // the notes while a gesture is under way (shown instead of `notes`)
    readonly property alias grid: grid
    readonly property var shown: working !== null ? working : notes
    property var selected: []                       // indexes into the notes
    property string tool: "pointer"                 // "pointer", "pencil" or "eraser"
    property real pixelsPerBeat: 60
    property real scrollBeats: 0
    property real scrollNote: 84                    // the highest note at the top of the grid
    readonly property real rowHeight: 14
    readonly property real keysWidth: 56
    readonly property real laneHeight: 64
    property bool laneVisible: true
    property string snapMode: "smart"               // "smart", "bar", "beat", "half", "eighth", "sixteenth", "thirtysecond", "off"
    property real quantizeBeats: 0.25               // Time Quantize: the note value
    property real strength: 100
    property int defaultVelocity: 80
    property real swing: 50                         // 50 = straight; 75 puts the off-beats on the triplet
    property var preQuantize: ({})                  // region id -> the starts before the last Quantize, for Dequantize
    property var noteClip: []                       // copied notes: start relative to the first, length, note, velocity
    property string readout: ""

    // Smart: the finest division that is still at least 10 px wide, like Logic's Snap: Smart that follows the zoom
    readonly property real gridUnit: {
        switch (snapMode) {
        case "bar": return project.barBeats
        case "beat": return 1
        case "half": return 0.5
        case "eighth": return 0.5
        case "sixteenth": return 0.25
        case "thirtysecond": return 0.125
        case "off": return 1 / 64
        }
        const units = [project.barBeats, 1, 0.5, 0.25, 0.125, 0.0625]
        let best = units[0]
        for (const u of units) if (u * pixelsPerBeat >= 10) best = u
        return best
    }
    readonly property var snapLabels: ({ "smart": "Smart", "bar": "Bar", "beat": "Beat", "half": "1/2 Note", "eighth": "1/8 Note", "sixteenth": "1/16 Note", "thirtysecond": "1/32 Note", "off": "Off" })

    readonly property var noteNames: ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    function noteName(n) { return noteNames[n % 12] + (Math.floor(n / 12) - 2) }  // Logic: middle C (60) is C3
    function isBlack(n) { return [1, 3, 6, 8, 10].indexOf(n % 12) >= 0 }
    function beatsToX(b) { return (b - scrollBeats) * pixelsPerBeat }
    function xToBeats(x) { return x / pixelsPerBeat + scrollBeats }
    function noteTop(n) { return (scrollNote - n) * rowHeight }
    function noteAt(y) { return Math.max(0, Math.min(127, Math.ceil(scrollNote - y / rowHeight))) }  // the row drawn at noteTop(n) holds n
    function snap(b) { return Math.round(b / gridUnit) * gridUnit }
    function barBeatText(b) {
        const bb = project.barBeats
        const bar = Math.floor(b / bb + 1e-9)
        const beat = Math.floor(b - bar * bb + 1e-9)
        const sub = Math.floor((b - Math.floor(b)) * 4 + 1e-9)
        return (bar + 1) + " " + (beat + 1) + " " + (sub + 1)
    }

    onRegionIdChanged: { selected = []; working = null; if (hasMidi) scrollBeats = Math.max(0, regionStart - 1) }
    onHasMidiChanged: if (hasMidi) scrollBeats = Math.max(0, regionStart - 1)

    function copyNotes(list) { return list.map(n => ({ start: n.start, length: n.length, note: n.note, velocity: n.velocity })) }
    function commit(list) {
        working = null
        if (hasMidi) project.setRegionNotes(regionId, list)
    }
    // the notes an operation works on: the selection, or every note when nothing is selected
    function targets() { return selected.length > 0 ? selected : notes.map((n, i) => i) }

    // ---- the commands (the Edit and Functions menus, the keys)
    function deleteSelected() {
        if (!hasMidi || selected.length === 0) return
        const keep = []
        for (let i = 0; i < notes.length; ++i) if (selected.indexOf(i) < 0) keep.push(notes[i])
        selected = []
        commit(keep)
    }
    function selectAll() { selected = notes.map((n, i) => i) }
    function deselectAll() { selected = [] }
    function invertSelection() { selected = notes.map((n, i) => i).filter(i => selected.indexOf(i) < 0) }
    function selectFollowing() {
        if (selected.length === 0) return
        let from = Infinity
        for (const i of selected) from = Math.min(from, notes[i].start)
        selected = notes.map((n, i) => i).filter(i => notes[i].start >= from - 1e-6)
    }
    function selectSamePitch() {
        if (selected.length === 0) return
        const pitches = selected.map(i => notes[i].note)
        selected = notes.map((n, i) => i).filter(i => pitches.indexOf(notes[i].note) >= 0)
    }
    function copy() {
        if (selected.length === 0) return
        const list = selected.map(i => notes[i]).sort((a, b) => a.start - b.start)
        const first = list[0].start
        noteClip = list.map(n => ({ start: n.start - first, length: n.length, note: n.note, velocity: n.velocity }))
    }
    function cut() { copy(); deleteSelected() }
    function paste() {
        if (!hasMidi || noteClip.length === 0) return
        const at = Math.max(0, snap(project.positionBeats - regionStart))
        const list = copyNotes(notes)
        const first = list.length
        for (const n of noteClip) list.push({ start: at + n.start, length: n.length, note: n.note, velocity: n.velocity })
        selected = noteClip.map((n, i) => first + i)
        commit(list)
    }
    function duplicate() {
        if (selected.length === 0) return
        const picked = selected.map(i => notes[i])
        let first = Infinity, last = 0
        for (const n of picked) { first = Math.min(first, n.start); last = Math.max(last, n.start + n.length) }
        const list = copyNotes(notes)
        const base = list.length
        for (const n of picked) list.push({ start: n.start + (last - first), length: n.length, note: n.note, velocity: n.velocity })
        selected = picked.map((n, i) => base + i)
        commit(list)
    }
    function transpose(semitones) {
        if (!hasMidi) return
        const list = copyNotes(notes)
        for (const i of targets()) list[i].note = Math.max(0, Math.min(127, list[i].note + semitones))
        commit(list)
    }
    function nudge(beats) {
        if (!hasMidi) return
        const list = copyNotes(notes)
        for (const i of targets()) list[i].start = Math.max(0, list[i].start + beats)
        commit(list)
    }
    function nudgeLength(beats) {
        if (!hasMidi) return
        const list = copyNotes(notes)
        for (const i of targets()) list[i].length = Math.max(1 / 64, list[i].length + beats)
        commit(list)
    }
    function setVelocity(v) {
        if (!hasMidi) return
        const list = copyNotes(notes)
        for (const i of targets()) list[i].velocity = Math.max(1, Math.min(127, Math.round(v)))
        commit(list)
    }
    // Quantize Notes (Q): the start of each note moves towards the grid of the Time Quantize value by the Strength
    function quantize() {
        if (!hasMidi || notes.length === 0) return
        const list = copyNotes(notes)
        const saved = {}
        for (const i of targets()) {
            saved[i] = list[i].start
            // Swing delays every second step of the grid: 50 % is straight, 75 % lands on the triplet (the pair is two steps long)
            const step = Math.round(list[i].start / quantizeBeats)
            let target = step * quantizeBeats
            if (step % 2 !== 0) target += quantizeBeats * (swing - 50) / 50
            list[i].start = Math.max(0, list[i].start + (target - list[i].start) * strength / 100)
        }
        const all = Object.assign({}, preQuantize)
        all[regionId] = saved
        preQuantize = all
        commit(list)
    }
    // Dequantize: put the notes back where they were before the last Quantize of this region
    function dequantize() {
        const saved = preQuantize[regionId]
        if (!hasMidi || !saved) return
        const list = copyNotes(notes)
        for (const k in saved) if (list[k]) list[k].start = saved[k]
        const all = Object.assign({}, preQuantize)
        delete all[regionId]
        preQuantize = all
        commit(list)
    }
    function setLength(beats) {
        if (!hasMidi) return
        const list = copyNotes(notes)
        for (const i of targets()) list[i].length = beats
        commit(list)
    }

    focus: true
    Keys.onPressed: (event) => {
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const alt = (event.modifiers & Qt.AltModifier) !== 0
        if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) { deleteSelected(); event.accepted = true }
        else if (event.key === Qt.Key_Q && !ctrl) { quantize(); event.accepted = true }
        else if (alt && event.key === Qt.Key_Left) { nudge(-gridUnit); event.accepted = true }
        else if (alt && event.key === Qt.Key_Right) { nudge(gridUnit); event.accepted = true }
        else if (alt && event.key === Qt.Key_Up) { transpose(1); event.accepted = true }
        else if (alt && event.key === Qt.Key_Down) { transpose(-1); event.accepted = true }
        else if (event.key === Qt.Key_Up && selected.length > 0) { transpose((event.modifiers & Qt.ShiftModifier) ? 12 : 1); event.accepted = true }
        else if (event.key === Qt.Key_Down && selected.length > 0) { transpose((event.modifiers & Qt.ShiftModifier) ? -12 : -1); event.accepted = true }
        else if (event.key === Qt.Key_Left && selected.length > 0) { nudge(-gridUnit); event.accepted = true }
        else if (event.key === Qt.Key_Right && selected.length > 0) { nudge(gridUnit); event.accepted = true }
    }

    component Entry: ThemedMenuItem {
        property string hint: ""
        implicitWidth: 260
    }

    // ---- the left pane
    Rectangle {
        id: left
        width: 190
        height: parent.height
        color: Theme.surfacePanel
        Column {
            width: parent.width
            spacing: Theme.spacing[2]
            topPadding: Theme.spacing[3]
            Row {
                x: Theme.spacing[3]
                spacing: Theme.spacing[3]
                Rectangle {
                    width: 30; height: 30; radius: Theme.radiusControl - 2
                    color: root.hasMidi ? Theme.trackGreenSolid : Theme.surfaceRaised
                    TrackIcon { anchors.centerIn: parent; size: 18; kind: "instrument"; tint: Theme.textPrimary }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: root.hasMidi ? (root.selected.length === 0 ? root.info.trackName : (root.selected.length === 1 ? qsTr("One Note selected") : qsTr("%1 Notes selected").arg(root.selected.length))) : qsTr("No Regions selected")
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeBodySize
                        font.weight: Theme.fontTypeLabelWeight
                    }
                    Text {
                        visible: root.hasMidi
                        text: qsTr("on Track %1").arg(root.info.trackName ?? "")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeCaptionSize
                    }
                }
            }
            Item { width: 1; height: Theme.spacing[2] }
            Text { x: Theme.spacing[3]; text: qsTr("Time Quantize"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            InspectorRow {
                label: qsTr("Quantize")
                Rectangle {
                    width: parent.width - 30
                    height: 20
                    radius: Theme.radiusControl - 2
                    color: quantizeArea.containsMouse ? Theme.surfaceRaisedHover : Theme.surfaceRaised
                    Text { anchors.fill: parent; anchors.leftMargin: Theme.spacing[2]; verticalAlignment: Text.AlignVCenter; text: root.quantizeLabel; color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                    MouseArea { id: quantizeArea; anchors.fill: parent; hoverEnabled: true; onClicked: quantizeMenu.popup(parent, 0, parent.height) }
                }
                IconButton {  // Q: apply
                    x: parent.width - 26
                    implicitWidth: 24
                    implicitHeight: 20
                    label: "Q"
                    onClicked: root.quantize()
                }
            }
            InspectorRow {
                label: qsTr("Strength")
                SliderField { value: root.strength; from: 0; to: 100; onMoved: (v) => root.strength = Math.round(v) }
            }
            InspectorRow {
                label: qsTr("Swing")
                SliderField { value: root.swing; from: 0; to: 100; onMoved: (v) => root.swing = Math.round(v) }
            }
            Text { x: Theme.spacing[3]; text: qsTr("Scale Quantize"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            InspectorRow { label: qsTr("Scale"); StubValue { project: root.project; label: qsTr("Scale Quantize"); text: qsTr("Off"); width: parent.width } }
            InspectorRow {
                label: qsTr("Velocity")
                SliderField { value: root.defaultVelocity; from: 1; to: 127; onMoved: (v) => root.defaultVelocity = Math.round(v) }
            }
        }
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.borderSubtle }
    }
    readonly property var quantizeValues: [{ label: "1/4 Note", beats: 1 }, { label: "1/8 Note", beats: 0.5 }, { label: "1/16 Note", beats: 0.25 }, { label: "1/32 Note", beats: 0.125 }, { label: "1/8 Triplet", beats: 1 / 3 }, { label: "1/16 Triplet", beats: 1 / 6 }]
    readonly property string quantizeLabel: { for (const q of quantizeValues) if (Math.abs(q.beats - quantizeBeats) < 1e-6) return q.label; return qsTr("Custom") }
    ThemedMenu {
        id: quantizeMenu
        Instantiator {
            model: root.quantizeValues
            delegate: ThemedMenuItem { required property var modelData; text: modelData.label; onTriggered: root.quantizeBeats = modelData.beats }
            onObjectAdded: (index, object) => quantizeMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => quantizeMenu.removeItem(object)
        }
    }
    // a slider with its value next to it, in the style of the Inspector fields
    component SliderField: Item {
        id: field
        property real value: 0
        property real from: 0
        property real to: 100
        signal moved(real value)
        width: parent ? parent.width : 100
        height: 20
        Rectangle { id: track; anchors.verticalCenter: parent.verticalCenter; x: 0; width: parent.width - 32; height: 4; radius: 2; color: Theme.surfaceRaised
            Rectangle { width: parent.width * (field.value - field.from) / (field.to - field.from); height: parent.height; radius: 2; color: Theme.textSecondary }
            Rectangle { x: parent.width * (field.value - field.from) / (field.to - field.from) - 5; anchors.verticalCenter: parent.verticalCenter; width: 10; height: 10; radius: 5; color: Theme.textPrimary }
        }
        Text { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; text: Math.round(field.value); color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
        MouseArea {
            anchors.fill: track
            anchors.topMargin: -8
            anchors.bottomMargin: -8
            function set(m) { field.moved(Math.max(field.from, Math.min(field.to, field.from + m.x / track.width * (field.to - field.from)))) }
            onPressed: (m) => set(m)
            onPositionChanged: (m) => { if (pressed) set(m) }
        }
    }

    // ---- the local bar
    Rectangle {
        id: bar
        x: left.width
        width: parent.width - left.width
        height: 26
        color: Theme.surfacePanel
        Row {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacing[3]
            spacing: Theme.spacing[2]
            IconButton { implicitHeight: 20; label: qsTr("Edit") + " ▾"; onClicked: editMenu.popup(0, height) }
            IconButton { implicitHeight: 20; label: qsTr("Functions") + " ▾"; onClicked: functionsMenu.popup(0, height) }
            IconButton { implicitHeight: 20; label: qsTr("View") + " ▾"; onClicked: viewMenu.popup(0, height) }
            Rectangle { width: 1; height: 16; color: Theme.borderStrong }
            Repeater {
                model: [{ id: "pointer", icon: "icons/pointer.svg" }, { id: "pencil", icon: "icons/pencil.svg" }, { id: "eraser", icon: "icons/eraser.svg" }]
                delegate: IconButton {
                    required property var modelData
                    implicitHeight: 20
                    implicitWidth: 26
                    source: modelData.icon
                    active: root.tool === modelData.id
                    onClicked: root.tool = modelData.id
                }
            }
            Rectangle { width: 1; height: 16; color: Theme.borderStrong }
            IconButton { implicitHeight: 20; label: qsTr("Snap: %1").arg(root.snapLabels[root.snapMode]) + " ▾"; onClicked: snapMenu.popup(0, height) }
        }
        Text {  // the pointer position: note, bar, beat, division
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacing[4]
            anchors.verticalCenter: parent.verticalCenter
            text: root.readout
            color: Theme.textValue
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
    }
    ThemedMenu {
        id: editMenu
        ThemedMenuItem { text: qsTr("Undo"); onTriggered: root.project.undo() }
        ThemedMenuItem { text: qsTr("Redo"); onTriggered: root.project.redo() }
        MenuSeparator {}
        ThemedMenuItem { text: qsTr("Cut"); enabled: root.selected.length > 0; onTriggered: root.cut() }
        ThemedMenuItem { text: qsTr("Copy"); enabled: root.selected.length > 0; onTriggered: root.copy() }
        ThemedMenuItem { text: qsTr("Paste"); enabled: root.noteClip.length > 0; onTriggered: root.paste() }
        ThemedMenuItem { text: qsTr("Duplicate"); enabled: root.selected.length > 0; onTriggered: root.duplicate() }
        ThemedMenuItem { text: qsTr("Delete"); enabled: root.selected.length > 0; onTriggered: root.deleteSelected() }
        MenuSeparator {}
        ThemedMenu {
            title: qsTr("Select")
            ThemedMenuItem { text: qsTr("All"); onTriggered: root.selectAll() }
            ThemedMenuItem { text: qsTr("Deselect All"); onTriggered: root.deselectAll() }
            ThemedMenuItem { text: qsTr("Invert Selection"); onTriggered: root.invertSelection() }
            ThemedMenuItem { text: qsTr("All Following"); onTriggered: root.selectFollowing() }
            ThemedMenuItem { text: qsTr("Same Note Pitch"); onTriggered: root.selectSamePitch() }
        }
        ThemedMenu {
            title: qsTr("Move")
            ThemedMenuItem { text: qsTr("Nudge Position Left"); onTriggered: root.nudge(-root.gridUnit) }
            ThemedMenuItem { text: qsTr("Nudge Position Right"); onTriggered: root.nudge(root.gridUnit) }
            ThemedMenuItem { text: qsTr("Nudge Length Left"); onTriggered: root.nudgeLength(-root.gridUnit) }
            ThemedMenuItem { text: qsTr("Nudge Length Right"); onTriggered: root.nudgeLength(root.gridUnit) }
        }
        ThemedMenu {
            title: qsTr("Transpose")
            ThemedMenuItem { text: qsTr("+12 Semitones (Octave Up)"); onTriggered: root.transpose(12) }
            ThemedMenuItem { text: qsTr("+1 Semitone"); onTriggered: root.transpose(1) }
            ThemedMenuItem { text: qsTr("-1 Semitone"); onTriggered: root.transpose(-1) }
            ThemedMenuItem { text: qsTr("-12 Semitones (Octave Down)"); onTriggered: root.transpose(-12) }
        }
    }
    ThemedMenu {
        id: functionsMenu
        ThemedMenuItem { text: qsTr("Quantize Notes"); onTriggered: root.quantize() }
        ThemedMenuItem { text: qsTr("Dequantize"); enabled: root.preQuantize[root.regionId] !== undefined; onTriggered: root.dequantize() }
        MenuSeparator {}
        ThemedMenu {
            id: velocityMenu
            title: qsTr("Set Velocity")
            Instantiator {
                model: [127, 100, 80, 64, 40, 20]
                delegate: ThemedMenuItem { required property int modelData; text: String(modelData); onTriggered: root.setVelocity(modelData) }
                onObjectAdded: (index, object) => velocityMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => velocityMenu.removeItem(object)
            }
        }
        ThemedMenu {
            id: lengthMenu
            title: qsTr("Set Note Length")
            Instantiator {
                model: root.quantizeValues
                delegate: ThemedMenuItem { required property var modelData; text: modelData.label; onTriggered: root.setLength(modelData.beats) }
                onObjectAdded: (index, object) => lengthMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => lengthMenu.removeItem(object)
            }
        }
        MenuSeparator {}
        ThemedMenuItem { text: qsTr("Mute Notes On/Off"); onTriggered: root.project.announceStub(qsTr("Mute Notes")) }
        ThemedMenuItem { text: qsTr("MIDI Transform"); onTriggered: root.project.announceStub(qsTr("MIDI Transform")) }
    }
    ThemedMenu {
        id: viewMenu
        ThemedMenuItem { text: qsTr("Velocity Lane"); checkable: true; checked: root.laneVisible; onTriggered: root.laneVisible = !root.laneVisible }
        MenuSeparator {}
        ThemedMenuItem { text: qsTr("Zoom In"); onTriggered: root.pixelsPerBeat = Math.min(400, root.pixelsPerBeat * 1.25) }
        ThemedMenuItem { text: qsTr("Zoom Out"); onTriggered: root.pixelsPerBeat = Math.max(8, root.pixelsPerBeat / 1.25) }
        ThemedMenuItem { text: qsTr("Zoom to Region"); enabled: root.hasMidi; onTriggered: { root.pixelsPerBeat = Math.max(8, Math.min(400, (body.width - root.keysWidth - 20) / root.info.lengthBeats)); root.scrollBeats = Math.max(0, root.regionStart - 0.2) } }
        MenuSeparator {}
        ThemedMenuItem { text: qsTr("Note Labels"); onTriggered: root.project.announceStub(qsTr("Note Labels")) }
    }
    ThemedMenu {
        id: snapMenu
        Instantiator {
            model: ["smart", "bar", "beat", "half", "eighth", "sixteenth", "thirtysecond", "off"]
            delegate: ThemedMenuItem {
                required property string modelData
                text: root.snapLabels[modelData]
                checkable: true
                checked: root.snapMode === modelData
                onTriggered: root.snapMode = modelData
            }
            onObjectAdded: (index, object) => snapMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => snapMenu.removeItem(object)
        }
    }

    // ---- the ruler and the region bar
    Ruler {
        id: ruler
        x: left.width + root.keysWidth
        y: bar.height
        width: parent.width - left.width - root.keysWidth
        height: 20
        pixelsPerBeat: root.pixelsPerBeat
        scrollBeats: root.scrollBeats
        barBeats: root.project.barBeats
        playheadBeats: root.project.positionBeats
        onLocateRequested: (beats) => root.project.locateBeats(Math.max(0, root.snap(beats)))
    }
    Rectangle {
        id: regionBar
        visible: root.hasMidi
        x: left.width + root.keysWidth + root.beatsToX(root.regionStart)
        y: bar.height + ruler.height
        width: root.hasMidi ? root.info.lengthBeats * root.pixelsPerBeat : 0
        height: 14
        radius: 2
        color: Theme.trackGreenFill
        border.color: Theme.trackGreenSolid
        Text {
            x: 4
            anchors.verticalCenter: parent.verticalCenter
            text: root.hasMidi ? root.info.trackName : ""
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeCaptionSize
        }
    }

    // ---- the keyboard and the grid
    Item {
        id: body
        x: left.width
        y: bar.height + ruler.height + regionBar.height
        width: parent.width - left.width
        height: parent.height - y
        clip: true
        readonly property real gridHeight: height - (root.laneVisible && root.hasMidi ? root.laneHeight : 0)

        Item {  // the grid and the keyboard, above the velocity lane
            width: body.width
            height: body.gridHeight
            clip: true

            Repeater {  // the lanes, white and black keys alternating
                model: Math.ceil(body.gridHeight / root.rowHeight) + 1
                delegate: Rectangle {
                    required property int index
                    readonly property int n: Math.floor(root.scrollNote) - index
                    visible: n >= 0 && n <= 127
                    x: root.keysWidth
                    y: root.noteTop(n)
                    width: body.width - root.keysWidth
                    height: root.rowHeight
                    color: root.isBlack(n) ? Theme.surfaceApp : Theme.surfaceCanvas
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: n % 12 === 0 ? Theme.borderStrong : Theme.borderSubtle }
                }
            }
            Repeater {  // the beat lines
                model: Math.ceil((body.width - root.keysWidth) / root.pixelsPerBeat) + 2
                delegate: Rectangle {
                    required property int index
                    readonly property real beat: Math.floor(root.scrollBeats) + index
                    x: root.keysWidth + root.beatsToX(beat)
                    width: 1
                    height: body.gridHeight
                    color: beat % root.project.barBeats === 0 ? Theme.borderStrong : Theme.borderSubtle
                }
            }

            Repeater {  // the notes
                model: root.shown
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    readonly property bool picked: root.selected.indexOf(index) >= 0
                    x: root.keysWidth + root.beatsToX(root.regionStart + modelData.start)
                    y: root.noteTop(modelData.note) + 1
                    width: Math.max(3, modelData.length * root.pixelsPerBeat)
                    height: root.rowHeight - 2
                    radius: 2
                    color: Qt.hsla(0.36, 0.55, 0.45 + modelData.velocity / 127 * 0.25, 1)
                    border.color: picked ? Theme.textPrimary : Qt.darker(color, 1.5)
                    border.width: picked ? 2 : 1
                    Rectangle {  // the velocity bar
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: 1
                        width: parent.width - 2
                        height: 2
                        color: Qt.darker(parent.color, 1.7)
                        Rectangle { width: parent.width * modelData.velocity / 127; height: parent.height; color: Theme.textPrimary; opacity: 0.8 }
                    }
                }
            }
            Rectangle {  // the rectangle selection being dragged
                visible: grid.mode === "band"
                x: root.keysWidth + Math.min(grid.bx0, grid.bx1)
                y: Math.min(grid.by0, grid.by1)
                width: Math.abs(grid.bx1 - grid.bx0)
                height: Math.abs(grid.by1 - grid.by0)
                color: Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.2)
                border.color: Theme.accentPrimary
            }

            Rectangle { width: root.keysWidth; height: body.gridHeight; color: Theme.surfacePanel }
            Repeater {  // the keyboard
                model: Math.ceil(body.gridHeight / root.rowHeight) + 1
                delegate: Rectangle {
                    required property int index
                    readonly property int n: Math.floor(root.scrollNote) - index
                    readonly property bool lit: root.selected.some(i => root.shown[i] && root.shown[i].note === n)
                    visible: n >= 0 && n <= 127
                    y: root.noteTop(n)
                    width: root.isBlack(n) ? root.keysWidth * 0.62 : root.keysWidth
                    height: root.rowHeight
                    color: lit ? Theme.accentPrimary : (root.isBlack(n) ? "#1c1c1e" : "#d8d8dc")
                    border.color: "#55000000"
                    border.width: 1
                    Text {
                        visible: n % 12 === 0
                        anchors.right: parent.right
                        anchors.rightMargin: 3
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.noteName(n)
                        color: "#222222"
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeCaptionSize
                    }
                }
            }

            Text {
                visible: !root.hasMidi
                x: root.keysWidth
                width: body.width - root.keysWidth
                height: body.gridHeight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: root.info.found === true ? qsTr("Select a MIDI region") : qsTr("No Regions selected")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeBodySize
            }

            MouseArea {
                id: grid
                x: root.keysWidth
                width: body.width - root.keysWidth
                height: body.gridHeight
                hoverEnabled: true
                enabled: root.hasMidi
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                property string mode: ""      // "move", "resize", "draw" or "band"
                property real pressBeat: 0
                property int pressNote: 0
                property var origin: []
                property real bx0: 0
                property real by0: 0
                property real bx1: 0
                property real by1: 0
                property string hoverZone: ""   // "left" or "right" while the pointer is over a note end
                cursorShape: mode === "resize" || mode === "resizeLeft" || hoverZone !== "" ? Qt.SizeHorCursor : (mode === "move" ? Qt.ClosedHandCursor : Qt.ArrowCursor)

                // which end of the note under the pointer can be dragged: a zone of 6 px, at most a third of a short note
                function zoneAt(x, y) {
                    const i = noteIndexAt(x, y)
                    if (i < 0) return ""
                    const n = root.shown[i]
                    const left = root.beatsToX(root.regionStart + n.start)
                    const w = Math.max(3, n.length * root.pixelsPerBeat)
                    const z = Math.min(6, w / 3)
                    if (x - left <= z) return "left"
                    if (left + w - x <= z) return "right"
                    return ""
                }

                function noteIndexAt(x, y) {
                    for (let i = root.shown.length - 1; i >= 0; --i) {
                        const n = root.shown[i]
                        const nx = root.beatsToX(root.regionStart + n.start)
                        const ny = root.noteTop(n.note)
                        if (x >= nx && x <= nx + Math.max(3, n.length * root.pixelsPerBeat) && y >= ny && y < ny + root.rowHeight) return i
                    }
                    return -1
                }
                function beatAt(x) { return root.xToBeats(x) - root.regionStart }

                onPositionChanged: (m) => {
                    const b = beatAt(m.x)
                    root.readout = root.noteName(root.noteAt(m.y)) + "  " + root.barBeatText(Math.max(0, b + root.regionStart))
                    if (!pressed) hoverZone = (root.tool === "pointer") ? zoneAt(m.x, m.y) : ""
                    if (!pressed || mode === "") return
                    if (mode === "band") { bx1 = m.x; by1 = m.y; return }
                    const list = root.copyNotes(origin)
                    if (mode === "move") {
                        const db = root.snap(b - pressBeat), dn = root.noteAt(m.y) - pressNote
                        for (const i of root.selected) {
                            list[i].start = Math.max(0, origin[i].start + db)
                            list[i].note = Math.max(0, Math.min(127, origin[i].note + dn))
                        }
                    } else if (mode === "resize") {
                        const d = root.snap(b - pressBeat)
                        for (const i of root.selected) list[i].length = Math.max(root.gridUnit, origin[i].length + d)
                    } else if (mode === "resizeLeft") {  // the start moves, the end stays
                        for (const i of root.selected) {
                            const end = origin[i].start + origin[i].length
                            const start = Math.max(0, Math.min(end - root.gridUnit, origin[i].start + root.snap(b - pressBeat)))
                            list[i].start = start
                            list[i].length = end - start
                        }
                    } else if (mode === "draw") {
                        const last = list.length - 1
                        list[last].length = Math.max(root.gridUnit, root.snap(b) - list[last].start)
                    }
                    root.working = list
                }
                onPressed: (m) => {
                    root.forceActiveFocus()
                    const b = beatAt(m.x)
                    const hit = noteIndexAt(m.x, m.y)
                    origin = root.copyNotes(root.notes)
                    pressBeat = b
                    pressNote = root.noteAt(m.y)
                    mode = ""
                    if (m.button === Qt.RightButton) { if (hit >= 0 && root.selected.indexOf(hit) < 0) root.selected = [hit]; editMenu.popup(grid, m.x, m.y); return }
                    if (root.tool === "eraser") {
                        if (hit >= 0) { root.selected = [hit]; root.deleteSelected() }
                        return
                    }
                    // Option (Alt) turns the pointer into the pencil for this click
                    if (root.tool === "pencil" || (root.tool === "pointer" && (m.modifiers & Qt.AltModifier))) {
                        if (hit >= 0) { root.selected = [hit]; return }
                        const start = Math.max(0, root.snap(b))
                        const list = root.copyNotes(root.notes)
                        list.push({ start: start, length: root.gridUnit, note: pressNote, velocity: root.defaultVelocity })
                        origin = root.copyNotes(list)
                        root.selected = [list.length - 1]
                        root.working = list
                        mode = "draw"
                        return
                    }
                    if (hit < 0) {  // empty space: a rectangle selection
                        if (!(m.modifiers & Qt.ShiftModifier)) root.selected = []
                        bx0 = bx1 = m.x
                        by0 = by1 = m.y
                        mode = "band"
                        return
                    }
                    if (root.selected.indexOf(hit) < 0) root.selected = (m.modifiers & Qt.ShiftModifier) ? root.selected.concat([hit]) : [hit]
                    const zone = zoneAt(m.x, m.y)
                    mode = zone === "right" ? "resize" : (zone === "left" ? "resizeLeft" : "move")
                }
                onReleased: (m) => {
                    if (mode === "band") {
                        const x0 = Math.min(bx0, bx1), x1 = Math.max(bx0, bx1), y0 = Math.min(by0, by1), y1 = Math.max(by0, by1)
                        const picked = root.selected.slice()
                        for (let i = 0; i < root.notes.length; ++i) {
                            const n = root.notes[i]
                            const nx = root.beatsToX(root.regionStart + n.start), nx2 = nx + Math.max(3, n.length * root.pixelsPerBeat)
                            const ny = root.noteTop(n.note), ny2 = ny + root.rowHeight
                            if (nx2 >= x0 && nx <= x1 && ny2 >= y0 && ny <= y1 && picked.indexOf(i) < 0) picked.push(i)
                        }
                        root.selected = picked
                    } else if (mode !== "" && root.working !== null) {
                        root.commit(root.working)
                    } else {
                        root.working = null
                    }
                    mode = ""
                }
                onCanceled: { root.working = null; mode = "" }
                onWheel: (w) => {
                    if (w.modifiers & Qt.ControlModifier) {
                        const anchor = root.xToBeats(w.x)
                        root.pixelsPerBeat = Math.max(8, Math.min(400, root.pixelsPerBeat * (w.angleDelta.y > 0 ? 1.15 : 1 / 1.15)))
                        root.scrollBeats = Math.max(0, anchor - w.x / root.pixelsPerBeat)
                    } else if (w.modifiers & Qt.ShiftModifier) {
                        root.scrollBeats = Math.max(0, root.scrollBeats - w.angleDelta.y / 120 * 2)
                    } else {
                        root.scrollNote = Math.max(body.gridHeight / root.rowHeight, Math.min(127, root.scrollNote + w.angleDelta.y / 120 * 3))
                    }
                }
            }
        }

        // ---- the velocity lane: one bar per note, aligned under it; drag a bar to set the velocity
        Rectangle {
            id: lane
            visible: root.laneVisible && root.hasMidi
            y: body.gridHeight
            width: body.width
            height: root.laneHeight
            color: Theme.surfaceCanvas
            Rectangle { width: parent.width; height: 1; color: Theme.borderStrong }
            Rectangle { width: root.keysWidth; height: parent.height; color: Theme.surfacePanel
                Text { anchors.centerIn: parent; text: qsTr("Velocity"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize } }
            Repeater {
                model: root.shown
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    readonly property bool picked: root.selected.indexOf(index) >= 0
                    x: root.keysWidth + root.beatsToX(root.regionStart + modelData.start)
                    width: 5
                    height: Math.max(2, (lane.height - 6) * modelData.velocity / 127)
                    y: lane.height - height - 2
                    radius: 1
                    color: picked ? Theme.textPrimary : Qt.hsla(0.36, 0.55, 0.45 + modelData.velocity / 127 * 0.25, 1)
                }
            }
            MouseArea {
                x: root.keysWidth
                width: parent.width - root.keysWidth
                height: parent.height
                property int target: -1
                property var origin: []
                function nearest(x) {
                    let best = -1, dist = 8
                    for (let i = 0; i < root.shown.length; ++i) {
                        const d = Math.abs(root.beatsToX(root.regionStart + root.shown[i].start) + 2 - x)
                        if (d < dist) { dist = d; best = i }
                    }
                    return best
                }
                function velocityAt(y) { return Math.max(1, Math.min(127, Math.round((lane.height - 2 - y) / (lane.height - 6) * 127))) }
                function apply(y) {
                    const v = velocityAt(y)
                    const list = root.copyNotes(origin)
                    const group = root.selected.indexOf(target) >= 0 ? root.selected : [target]
                    const delta = v - origin[target].velocity
                    for (const i of group) list[i].velocity = Math.max(1, Math.min(127, origin[i].velocity + delta))
                    root.working = list
                }
                onPressed: (m) => {
                    root.forceActiveFocus()
                    target = nearest(m.x)
                    if (target < 0) return
                    origin = root.copyNotes(root.notes)
                    if (root.selected.indexOf(target) < 0) root.selected = [target]
                    apply(m.y)
                }
                onPositionChanged: (m) => { if (pressed && target >= 0) apply(m.y) }
                onReleased: { if (target >= 0 && root.working !== null) root.commit(root.working); target = -1 }
                onCanceled: { root.working = null; target = -1 }
            }
        }
    }
}
