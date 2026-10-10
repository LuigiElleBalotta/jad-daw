import QtQuick
import Jad

// The automation of one track drawn over its row (Mix > Show Automation, key A): a click adds a point, a drag moves it,
// a double-click or an Option-click on a point deletes it. The lane drives the fader (Volume, dB) or the pan while it exists.
Item {
    id: root
    required property ProjectController project
    required property string trackId
    required property string param            // "volume", "pan" or "send1".."send4" (the level of the track's first sends)
    required property real pixelsPerBeat
    required property real scrollBeats
    property real snapBeats: 0
    readonly property bool normalised: param.startsWith("param/")   // a plug-in parameter: 0..1
    signal parameterRequested(string trackId)
    property color lineColor: param === "volume" ? Theme.accentPrimary : (param === "pan" ? Theme.stateSolo : (normalised ? Theme.stateMute : Theme.statePlay))
    property bool available: true                // false: a send the track does not have

    property var stored: []                      // the points in the project, read again whenever it changes
    function refresh() { available = project.automationAvailable(trackId, param); stored = project.automationPoints(trackId, param) }
    visible: available
    onTrackIdChanged: refresh()
    onParamChanged: refresh()
    Component.onCompleted: refresh()
    Connections { target: root.project; function onProjectChanged() { root.refresh() } }
    property var working: null                   // the points while one is dragged
    readonly property var points: working !== null ? working : stored
    property int dragIndex: -1
    readonly property string tool: project.tool
    // Automation Curve tool: the segment being bent. Automation Select tool: the range of points selected (beats) and the drag that moves it.
    property int curveIndex: -1
    property real curveFraction: 0.5
    property real selFrom: -1
    property real selTo: -1
    readonly property bool hasSelection: selFrom >= 0 && selTo > selFrom
    property real moveX: 0
    property real moveY: 0
    property bool moving: false
    property bool ranging: false
    property var moveBase: []
    readonly property real minDb: -60
    readonly property real maxDb: 6

    function beatsToX(b) { return (b - scrollBeats) * pixelsPerBeat }
    function xToBeats(x) { return Math.max(0, x / pixelsPerBeat + scrollBeats) }
    function snapBeat(b) { return snapBeats > 0 ? Math.round(b / snapBeats) * snapBeats : b }
    function valueToY(v) {
        if (normalised) return (1 - Math.max(0, Math.min(1, v))) * (height - 2) + 1
        if (param === "pan") return (1 - v) / 2 * height
        return (maxDb - Math.max(minDb, Math.min(maxDb, v))) / (maxDb - minDb) * height
    }
    function yToValue(y) {
        const f = Math.max(0, Math.min(1, y / height))
        if (normalised) return Math.round((1 - f) * 1000) / 1000
        return param === "pan" ? Math.round((1 - 2 * f) * 100) / 100 : Math.round((maxDb - f * (maxDb - minDb)) * 10) / 10
    }
    function pointAt(x, y) {
        for (let i = 0; i < points.length; ++i)
            if (Math.abs(beatsToX(points[i].beats) - x) <= 6 && Math.abs(valueToY(points[i].value) - y) <= 6) return i
        return -1
    }
    function sorted(list) { return list.slice().sort((a, b) => a.beats - b.beats) }
    function copyPoints(list) { return list.map(p => ({ beats: p.beats, value: p.value, curve: p.curve ?? 0 })) }
    // the shape of a segment: 0 is straight, above 0 it starts slowly, below 0 quickly (the Core's 4^curve)
    function bend(f, c) { return c === 0 ? f : Math.pow(f, Math.pow(4, c)) }
    function segmentAt(x) {
        for (let i = 0; i + 1 < points.length; ++i)
            if (x > beatsToX(points[i].beats) + 5 && x < beatsToX(points[i + 1].beats) - 5) return i
        return -1
    }
    function selected(p) { return hasSelection && p.beats >= selFrom - 1e-9 && p.beats <= selTo + 1e-9 }
    function deleteSelection() {
        if (!hasSelection) return
        commit(points.filter(p => !selected(p)))
        selFrom = -1; selTo = -1
    }
    function commit(list) { working = null; project.setAutomationPoints(trackId, param, sorted(list)) }
    focus: hasSelection
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) { deleteSelection(); event.accepted = true }
        else if (event.key === Qt.Key_Escape) { selFrom = -1; selTo = -1; event.accepted = true }
    }
    function label(v) { return normalised ? Math.round(v * 100) + " %" : param === "pan" ? (v === 0 ? "C" : (v < 0 ? "L" : "R") + Math.round(Math.abs(v) * 64)) : v.toFixed(1) + " dB" }

    onPointsChanged: canvas.requestPaint()
    onPixelsPerBeatChanged: canvas.requestPaint()
    onScrollBeatsChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            if (root.points.length === 0) return
            ctx.lineWidth = 1.5
            ctx.strokeStyle = root.lineColor
            ctx.beginPath()
            ctx.moveTo(0, root.valueToY(root.points[0].value))
            for (let i = 0; i < root.points.length; ++i) {
                const p = root.points[i]
                const x = root.beatsToX(p.beats), y = root.valueToY(p.value)
                if (i > 0) {  // the bent segment from the point before
                    const q = root.points[i - 1], c = q.curve ?? 0
                    if (c !== 0) {
                        const x0 = root.beatsToX(q.beats), y0 = root.valueToY(q.value)
                        for (let k = 1; k < 24; ++k) { const f = k / 24; ctx.lineTo(x0 + (x - x0) * f, y0 + (y - y0) * root.bend(f, c)) }
                    }
                }
                ctx.lineTo(x, y)
            }
            ctx.lineTo(width, root.valueToY(root.points[root.points.length - 1].value))
            ctx.stroke()
        }
    }
    Rectangle {  // the range the Automation Select tool has picked
        visible: root.hasSelection
        x: root.beatsToX(root.selFrom)
        width: root.beatsToX(root.selTo) - x
        height: root.height
        color: "#33ffffff"
        border.color: root.lineColor
    }
    Repeater {
        model: root.points
        delegate: Rectangle {
            required property var modelData
            required property int index
            x: root.beatsToX(modelData.beats) - 4
            y: root.valueToY(modelData.value) - 4
            width: 8; height: 8; radius: 4
            color: root.dragIndex === index || root.selected(modelData) ? Theme.textPrimary : root.lineColor
            border.color: Theme.surfaceCanvas
            visible: x > -8 && x < root.width
        }
    }
    Text {  // the value of the point being dragged
        readonly property bool active: root.dragIndex >= 0 && root.dragIndex < root.points.length
        visible: active
        x: active ? root.beatsToX(root.points[root.dragIndex].beats) + 8 : 0
        y: active ? Math.max(0, root.valueToY(root.points[root.dragIndex].value) - 16) : 0
        text: active ? root.label(root.points[root.dragIndex].value) : ""
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeCaptionSize
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: (m) => {
            if (root.tool === "autoCurve") {  // bend the segment under the pointer
                root.curveIndex = root.segmentAt(m.x)
                if (root.curveIndex >= 0) {
                    const a = root.beatsToX(root.points[root.curveIndex].beats), b = root.beatsToX(root.points[root.curveIndex + 1].beats)
                    root.curveFraction = Math.max(0.05, Math.min(0.95, (m.x - a) / (b - a)))
                    root.working = root.copyPoints(root.stored)
                }
                return
            }
            if (root.tool === "autoSelect") {
                root.forceActiveFocus()
                const beat = root.snapBeat(root.xToBeats(m.x))
                if (root.hasSelection && beat >= root.selFrom && beat <= root.selTo) {  // inside the range: it moves
                    root.moving = true
                    root.moveX = m.x; root.moveY = m.y
                    root.moveBase = root.copyPoints(root.stored)
                    root.working = root.copyPoints(root.stored)
                } else {                                                                  // elsewhere: a new range
                    root.ranging = true
                    root.moveX = beat
                    root.selFrom = beat; root.selTo = beat
                }
                return
            }
            const i = root.pointAt(m.x, m.y)
            if (i >= 0 && (m.modifiers & Qt.AltModifier)) {
                const kept = root.points.slice()
                kept.splice(i, 1)
                root.commit(kept)
                return
            }
            const list = root.copyPoints(root.stored)
            if (i >= 0) {
                root.dragIndex = i
            } else {
                const added = { beats: root.snapBeat(root.xToBeats(m.x)), value: root.yToValue(m.y), curve: 0 }
                list.push(added)
                list.sort((a, b) => a.beats - b.beats)
                root.dragIndex = list.indexOf(added)
            }
            root.working = list
        }
        onPositionChanged: (m) => {
            if (!pressed) return
            if (root.curveIndex >= 0 && root.working !== null) {
                const i = root.curveIndex
                const y0 = root.valueToY(root.working[i].value), y1 = root.valueToY(root.working[i + 1].value)
                if (Math.abs(y1 - y0) < 3) return
                // the curve that passes through the pointer at the place where it was pressed
                const g = Math.max(0.02, Math.min(0.98, (m.y - y0) / (y1 - y0)))
                const e = Math.log(g) / Math.log(root.curveFraction)
                const c = Math.max(-1, Math.min(1, Math.round(Math.log(e) / Math.log(4) * 100) / 100))
                const list = root.working.slice()
                list[i] = { beats: list[i].beats, value: list[i].value, curve: c }
                root.working = list
                return
            }
            if (root.ranging) { root.selFrom = Math.min(root.moveX, root.snapBeat(root.xToBeats(m.x))); root.selTo = Math.max(root.moveX, root.snapBeat(root.xToBeats(m.x))); return }
            if (root.moving && root.working !== null) {
                const db = root.snapBeat((m.x - root.moveX) / root.pixelsPerBeat)
                const list = root.moveBase.map(p => root.selected(p) ? { beats: Math.max(0, p.beats + db), value: root.yToValue(root.valueToY(p.value) + (m.y - root.moveY)), curve: p.curve } : p)
                root.working = list
                return
            }
            if (root.dragIndex < 0 || root.working === null) return
            const list = root.working.slice()
            list[root.dragIndex] = { beats: root.snapBeat(root.xToBeats(m.x)), value: root.yToValue(m.y), curve: list[root.dragIndex].curve ?? 0 }
            root.working = list
        }
        onReleased: (m) => {
            if (root.ranging) { root.ranging = false; if (root.selTo - root.selFrom < 1e-9) { root.selFrom = -1; root.selTo = -1 } return }
            if (root.moving) {
                const db = root.snapBeat((m.x - root.moveX) / root.pixelsPerBeat)
                root.moving = false
                if (root.working !== null) root.commit(root.working)
                root.selFrom = Math.max(0, root.selFrom + db); root.selTo = Math.max(root.selFrom, root.selTo + db)
                return
            }
            if (root.working !== null) root.commit(root.working)
            root.dragIndex = -1
            root.curveIndex = -1
        }
        onDoubleClicked: (m) => {
            if (root.tool === "autoCurve" || root.tool === "autoSelect") return
            const i = root.pointAt(m.x, m.y)
            if (i < 0) return
            const list = root.stored.slice()
            list.splice(i, 1)
            root.commit(list)
        }
        onCanceled: { root.working = null; root.dragIndex = -1; root.curveIndex = -1; root.ranging = false; root.moving = false }
    }

    IconButton {  // the parameter this lane shows: a click chooses another (a send, a plug-in parameter)
        objectName: "laneParam"
        x: 6
        y: 2
        implicitHeight: 16
        label: { root.project.automationRevision; return root.project.automationLabel(root.trackId, root.param) }
        onClicked: root.parameterRequested(root.trackId)
    }
}
