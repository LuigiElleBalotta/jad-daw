import QtQuick
import Jad

// The knob of the dark blue dynamics windows (Limiter, Adaptive Limiter, Noise Gate, Mastering Assistant): a dark disc with a thin pointer, a ring that fills up to the
// value (from the minimum, or from the middle for a gain), the name above, the value above the disc and the ends of the range under it. Drag up and down (Shift: fine),
// scroll, double-click on the value to type, double-click on the disc or Option-click to put the default back.
Item {
    id: root
    property string label: ""
    property real from: 0
    property real to: 1
    property real defaultValue: 0
    property real value: 0
    property bool logarithmic: false
    property string unit: ""
    property int decimals: 1
    property bool bipolar: false
    property real dial: 84
    property color tint: "#8dffa0"
    property color labelColor: "#9db2c8"
    property string minLabel: String(from)
    property string maxLabel: String(to)
    property bool whole: false
    property bool showHeader: true
    signal moved(real value)
    signal released(real value)

    implicitWidth: Math.max(dial + 40, 120)
    implicitHeight: dial + 100
    property real dragValue: value
    property bool dragging: false
    property bool changed: false
    readonly property real shown: dragging ? dragValue : value
    function toUnit(v) { return logarithmic ? Math.log(Math.max(v, from) / from) / Math.log(to / from) : (v - from) / (to - from) }
    function fromUnit(t) { t = Math.max(0, Math.min(1, t)); const v = logarithmic ? from * Math.pow(to / from, t) : from + t * (to - from); return whole ? Math.round(v) : v }
    function cancel() { dragging = false; changed = false; dragValue = value }
    function numberText(v) { const a = Math.abs(v); const d = whole ? 0 : (decimals >= 0 ? decimals : (a >= 100 ? 0 : 1)); return v.toFixed(d) }
    readonly property real t: toUnit(shown)
    readonly property real cx: width / 2
    readonly property real cy: showHeader ? 84 + dial / 2 : dial / 2

    Text {
        visible: root.showHeader
        x: 0; y: 0; width: parent.width; horizontalAlignment: Text.AlignHCenter
        text: root.label; color: root.labelColor; font.family: Theme.fontFamily; font.pixelSize: 19
    }
    Item {  // the value
        visible: root.showHeader
        x: 0; y: 28; width: parent.width; height: 34
        Row {
            anchors.centerIn: parent
            spacing: 4
            Text { id: num; text: root.numberText(root.shown); color: root.tint; font.family: Theme.fontFamily; font.pixelSize: 29; font.features: { "tnum": 1 }; visible: !entry.visible }
            Text { visible: root.unit !== "" && !entry.visible; anchors.baseline: num.baseline; text: root.unit; color: root.tint; font.family: Theme.fontFamily; font.pixelSize: 22 }
        }
        TextInput {
            id: entry
            visible: false
            anchors.fill: parent
            horizontalAlignment: TextInput.AlignHCenter; verticalAlignment: TextInput.AlignVCenter
            color: "#ffffff"; selectByMouse: true
            font.family: Theme.fontFamily; font.pixelSize: 26
            Rectangle { anchors.fill: parent; z: -1; color: "#0d1a27"; border.color: Theme.accentPrimary }
            Keys.onShortcutOverride: (event) => { if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Escape) event.accepted = true }
            Keys.onEscapePressed: visible = false
            onAccepted: { const v = parseFloat(text.replace(",", ".")); visible = false; if (isFinite(v)) root.released(Math.max(root.from, Math.min(root.to, v))) }
            onActiveFocusChanged: if (!activeFocus) visible = false
        }
        MouseArea { anchors.fill: parent; onDoubleClicked: { entry.text = root.numberText(root.value); entry.visible = true; entry.forceActiveFocus(); entry.selectAll() } }
    }

    Canvas {
        id: dialCanvas
        x: root.cx - root.dial / 2 - 8; y: root.cy - root.dial / 2 - 8
        width: root.dial + 16; height: root.dial + 16
        readonly property real tt: root.t
        onTtChanged: requestPaint()
        onWidthChanged: requestPaint()
        Component.onCompleted: requestPaint()
        Connections { target: root; function onTintChanged() { dialCanvas.requestPaint() } function onBipolarChanged() { dialCanvas.requestPaint() } }
        onPaint: {
            const c = getContext("2d")
            c.reset()
            const mx = width / 2, my = height / 2, r = root.dial / 2
            const start = 0.75 * Math.PI, sweep = 1.5 * Math.PI
            // the disc
            const g = c.createRadialGradient(mx - r * 0.2, my - r * 0.3, r * 0.1, mx, my, r)
            g.addColorStop(0, "#344b66"); g.addColorStop(1, "#1d2e42")
            c.fillStyle = g; c.beginPath(); c.arc(mx, my, r, 0, Math.PI * 2); c.fill()
            c.strokeStyle = "#0b1119"; c.lineWidth = 2; c.stroke()
            // the ring: black track, and the fill up to the value
            c.lineCap = "butt"
            c.lineWidth = 6
            c.strokeStyle = "#0a0f16"
            c.beginPath(); c.arc(mx, my, r + 1.5, start, start + sweep); c.stroke()
            const from = root.bipolar ? 0.5 : 0
            const a = start + sweep * Math.min(from, tt), b = start + sweep * Math.max(from, tt)
            if (b - a > 0.004) {
                c.strokeStyle = root.tint
                c.beginPath(); c.arc(mx, my, r + 1.5, a, b); c.stroke()
            }
        }
    }
    Rectangle {  // the pointer
        x: root.cx - 1; y: root.cy - root.dial * 0.46
        width: 2.5; height: root.dial * 0.46; color: root.tint
        transform: Rotation { origin.x: 1.2; origin.y: root.dial * 0.46; angle: -135 + root.t * 270 }
    }
    Text { x: root.cx - root.dial * 0.5 - 14; y: root.cy + root.dial * 0.5 + 6; text: root.minLabel; color: root.labelColor; font.family: Theme.fontFamily; font.pixelSize: 14 }
    Text { x: root.cx + root.dial * 0.5 - width + 14; y: root.cy + root.dial * 0.5 + 6; text: root.maxLabel; color: root.labelColor; font.family: Theme.fontFamily; font.pixelSize: 14 }
    MouseArea {
        x: root.cx - root.dial / 2; y: root.cy - root.dial / 2; width: root.dial; height: root.dial
        property real startY: 0
        property real startT: 0
        onPressed: (m) => {
            if (m.modifiers & Qt.AltModifier) { root.cancel(); if (root.value !== root.defaultValue) root.released(root.defaultValue); return }
            root.dragging = true; root.changed = false; startY = m.y; startT = root.toUnit(root.value); root.dragValue = root.value
        }
        onPositionChanged: (m) => {
            if (!pressed || !root.dragging) return
            root.dragValue = root.fromUnit(startT + (startY - m.y) / ((m.modifiers & Qt.ShiftModifier) ? 900 : 220))
            root.changed = true
            root.moved(root.dragValue)
        }
        onReleased: { const send = root.dragging && root.changed; const v = root.dragValue; root.cancel(); if (send) root.released(v) }
        onCanceled: root.cancel()
        onDoubleClicked: { root.cancel(); root.released(root.defaultValue) }
        onWheel: (w) => root.released(root.fromUnit(root.toUnit(root.value) + (w.angleDelta.y > 0 ? 1 : -1) * ((w.modifiers & Qt.ShiftModifier) ? 0.004 : 0.02)))
    }
}
