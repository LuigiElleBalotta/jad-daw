import QtQuick
import Jad

// The automation of one track drawn over its row (Mix > Show Automation, key A): a click adds a point, a drag moves it,
// a double-click or an Option-click on a point deletes it. The lane drives the fader (Volume, dB) or the pan while it exists.
Item {
    id: root
    required property ProjectController project
    required property string trackId
    required property string param            // "volume" or "pan"
    required property real pixelsPerBeat
    required property real scrollBeats
    property real snapBeats: 0
    property color lineColor: param === "volume" ? Theme.accentPrimary : Theme.stateSolo

    readonly property var stored: { project.revision; return project.automationPoints(trackId, param) }
    property var working: null                   // the points while one is dragged
    readonly property var points: working !== null ? working : stored
    property int dragIndex: -1
    readonly property real minDb: -60
    readonly property real maxDb: 6

    function beatsToX(b) { return (b - scrollBeats) * pixelsPerBeat }
    function xToBeats(x) { return Math.max(0, x / pixelsPerBeat + scrollBeats) }
    function snapBeat(b) { return snapBeats > 0 ? Math.round(b / snapBeats) * snapBeats : b }
    function valueToY(v) {
        if (param === "pan") return (1 - v) / 2 * height
        return (maxDb - Math.max(minDb, Math.min(maxDb, v))) / (maxDb - minDb) * height
    }
    function yToValue(y) {
        const f = Math.max(0, Math.min(1, y / height))
        return param === "pan" ? Math.round((1 - 2 * f) * 100) / 100 : Math.round((maxDb - f * (maxDb - minDb)) * 10) / 10
    }
    function pointAt(x, y) {
        for (let i = 0; i < points.length; ++i)
            if (Math.abs(beatsToX(points[i].beats) - x) <= 6 && Math.abs(valueToY(points[i].value) - y) <= 6) return i
        return -1
    }
    function sorted(list) { return list.slice().sort((a, b) => a.beats - b.beats) }
    function commit(list) { working = null; project.setAutomationPoints(trackId, param, sorted(list)) }
    function label(v) { return param === "pan" ? (v === 0 ? "C" : (v < 0 ? "L" : "R") + Math.round(Math.abs(v) * 64)) : v.toFixed(1) + " dB" }

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
            for (const p of root.points) ctx.lineTo(root.beatsToX(p.beats), root.valueToY(p.value))
            ctx.lineTo(width, root.valueToY(root.points[root.points.length - 1].value))
            ctx.stroke()
        }
    }
    Repeater {
        model: root.points
        delegate: Rectangle {
            required property var modelData
            required property int index
            x: root.beatsToX(modelData.beats) - 4
            y: root.valueToY(modelData.value) - 4
            width: 8; height: 8; radius: 4
            color: root.dragIndex === index ? Theme.textPrimary : root.lineColor
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
            const i = root.pointAt(m.x, m.y)
            if (i >= 0 && (m.modifiers & Qt.AltModifier)) {
                const kept = root.points.slice()
                kept.splice(i, 1)
                root.commit(kept)
                return
            }
            const list = root.stored.map(p => ({ beats: p.beats, value: p.value }))
            if (i >= 0) {
                root.dragIndex = i
            } else {
                const added = { beats: root.snapBeat(root.xToBeats(m.x)), value: root.yToValue(m.y) }
                list.push(added)
                list.sort((a, b) => a.beats - b.beats)
                root.dragIndex = list.indexOf(added)
            }
            root.working = list
        }
        onPositionChanged: (m) => {
            if (!pressed || root.dragIndex < 0 || root.working === null) return
            const list = root.working.slice()
            list[root.dragIndex] = { beats: root.snapBeat(root.xToBeats(m.x)), value: root.yToValue(m.y) }
            root.working = list
        }
        onReleased: {
            if (root.working !== null) root.commit(root.working)
            root.dragIndex = -1
        }
        onDoubleClicked: (m) => {
            const i = root.pointAt(m.x, m.y)
            if (i < 0) return
            const list = root.stored.slice()
            list.splice(i, 1)
            root.commit(list)
        }
        onCanceled: { root.working = null; root.dragIndex = -1 }
    }
}
