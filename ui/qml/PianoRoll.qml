import QtQuick
import QtQuick.Layouts
import Jad

// The Piano Roll of the Editors area: a keyboard, a ruler and a grid with the notes of the selected MIDI region, a left pane
// with the editor's own settings, and a local bar. Pointer: select, move (drag), resize (right edge); Pencil: draw a note;
// Eraser: delete; Delete key: delete the selection. Every change is one replace_region command (one undo step).
Item {
    id: root
    required property ProjectController project

    readonly property string regionId: project.selectedRegionIds.length > 0 ? project.selectedRegionIds[0] : ""
    readonly property var info: { project.revision; return regionId !== "" ? project.regionInfo(regionId) : ({ found: false }) }
    readonly property bool hasMidi: info.found === true && info.audio !== true
    readonly property real regionStart: hasMidi ? info.startBeats : 0
    readonly property var notes: { project.revision; return hasMidi ? project.regionNotes(regionId) : [] }
    property var working: null                      // the notes while a gesture is under way (shown instead of `notes`)
    readonly property var shown: working !== null ? working : notes
    property var selected: []                       // indexes into the notes
    property string tool: "pointer"                 // "pointer", "pencil" or "eraser"
    property real pixelsPerBeat: 60
    property real scrollBeats: 0
    property real scrollNote: 84                    // the highest note at the top of the grid
    readonly property real rowHeight: 14
    readonly property real keysWidth: 56
    readonly property real gridUnit: project.snapBeats > 0 ? project.snapBeats : 1 / 16
    property string readout: ""

    readonly property var noteNames: ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    function noteName(n) { return noteNames[n % 12] + (Math.floor(n / 12) - 2) }  // Logic: middle C (60) is C3
    function isBlack(n) { return [1, 3, 6, 8, 10].indexOf(n % 12) >= 0 }
    function beatsToX(b) { return (b - scrollBeats) * pixelsPerBeat }
    function xToBeats(x) { return x / pixelsPerBeat + scrollBeats }
    function noteTop(n) { return (scrollNote - n) * rowHeight }
    function noteAt(y) { return Math.max(0, Math.min(127, Math.floor(scrollNote - y / rowHeight))) }
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

    function commit(list) {
        working = null
        if (hasMidi) project.setRegionNotes(regionId, list)
    }
    function deleteSelected() {
        if (!hasMidi || selected.length === 0) return
        const keep = []
        for (let i = 0; i < notes.length; ++i) if (selected.indexOf(i) < 0) keep.push(notes[i])
        selected = []
        commit(keep)
    }
    function copyNotes(list) { return list.map(n => ({ start: n.start, length: n.length, note: n.note, velocity: n.velocity })) }

    focus: true
    Keys.onDeletePressed: deleteSelected()
    Keys.onPressed: (event) => { if (event.key === Qt.Key_Backspace) { deleteSelected(); event.accepted = true } }

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
            InspectorRow { label: qsTr("Quantize"); StubValue { project: root.project; label: qsTr("Time Quantize"); text: qsTr("1/16 Note"); width: parent.width } }
            InspectorRow { label: qsTr("Strength"); StubValue { project: root.project; label: qsTr("Strength"); text: "100"; width: parent.width } }
            InspectorRow { label: qsTr("Swing"); StubValue { project: root.project; label: qsTr("Swing"); text: "50"; width: parent.width } }
            Text { x: Theme.spacing[3]; text: qsTr("Scale Quantize"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            InspectorRow { label: qsTr("Scale"); StubValue { project: root.project; label: qsTr("Scale Quantize"); text: qsTr("Off"); width: parent.width } }
            InspectorRow { label: qsTr("Velocity"); StubValue { project: root.project; label: qsTr("Velocity"); text: "80"; width: parent.width } }
        }
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.borderSubtle }
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
            Repeater {
                model: [qsTr("Edit"), qsTr("Functions"), qsTr("View")]
                delegate: IconButton { required property string modelData; implicitHeight: 20; label: modelData + " ▾"; onClicked: root.project.announceStub(modelData) }
            }
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
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Snap: %1").arg(({ "off": "Off", "bar": "Bar", "half": "1/2", "quarter": "1/4", "eighth": "1/8", "sixteenth": "1/16" })[root.project.snap] ?? root.project.snap)
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeLabelSize
            }
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

        Repeater {  // the lanes, white and black keys alternating
            model: Math.ceil(body.height / root.rowHeight) + 1
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
                height: body.height
                color: beat % root.project.barBeats === 0 ? Theme.borderStrong : Theme.borderSubtle
            }
        }

        // the notes
        Repeater {
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

        // the keyboard
        Rectangle { width: root.keysWidth; height: body.height; color: Theme.surfacePanel }
        Repeater {
            model: Math.ceil(body.height / root.rowHeight) + 1
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
            anchors.centerIn: parent
            text: root.info.found === true ? qsTr("Select a MIDI region") : qsTr("No Regions selected")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeBodySize
        }

        MouseArea {
            id: grid
            x: root.keysWidth
            width: body.width - root.keysWidth
            height: body.height
            hoverEnabled: true
            enabled: root.hasMidi
            property string mode: ""      // "move", "resize" or "draw"
            property real pressBeat: 0
            property int pressNote: 0
            property var origin: []

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
                if (!pressed || mode === "") return
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
                if (root.tool === "eraser") {
                    if (hit >= 0) { root.selected = [hit]; root.deleteSelected() }
                    return
                }
                if (root.tool === "pencil") {
                    if (hit >= 0) { root.selected = [hit]; return }
                    const start = Math.max(0, root.snap(b))
                    const list = root.copyNotes(root.notes)
                    list.push({ start: start, length: root.gridUnit, note: pressNote, velocity: 80 })
                    origin = root.copyNotes(list)
                    root.selected = [list.length - 1]
                    root.working = list
                    mode = "draw"
                    return
                }
                if (hit < 0) { root.selected = []; return }
                if (root.selected.indexOf(hit) < 0) root.selected = (m.modifiers & Qt.ShiftModifier) ? root.selected.concat([hit]) : [hit]
                const n = root.shown[hit]
                const right = root.beatsToX(root.regionStart + n.start + n.length)
                mode = (right - m.x) <= 6 ? "resize" : "move"
            }
            onReleased: {
                if (mode !== "" && root.working !== null) root.commit(root.working)
                else root.working = null
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
                    root.scrollNote = Math.max(body.height / root.rowHeight, Math.min(127, root.scrollNote + w.angleDelta.y / 120 * 3))
                }
            }
        }
    }
}
