import QtQuick
import Jad

// The rotary knob of the stock effect windows: a ring that fills up to the value, a grey face with a pointer, the label below and the value above (double click
// on the value types one). Drag up or down to change it (Shift: fine), Option-click or double click on the knob puts the default back. `moved` goes out while
// dragging, `released` once at the end (that one is the undo step).
Item {
    id: root
    property string label: ""
    property real from: 0
    property real to: 1
    property real defaultValue: 0
    property real value: 0
    property bool logarithmic: false
    property string unit: ""
    property int decimals: -1                 // -1: chosen from the size of the value
    property real knobSize: 46
    property color tint: Theme.accentPrimary
    property bool bipolar: false              // the ring is filled from the middle (gains, pan)
    property var format: null                 // value -> text, instead of the number and the unit
    signal moved(real value)
    signal released(real value)

    implicitWidth: Math.max(knobSize + 12, 60)
    implicitHeight: knobSize + 36
    property real dragValue: value
    property bool dragging: false
    property bool changed: false
    readonly property real shown: dragging ? dragValue : value

    function toUnit(v) { return logarithmic ? Math.log(Math.max(v, from) / from) / Math.log(to / from) : (v - from) / (to - from) }
    function fromUnit(t) { t = Math.max(0, Math.min(1, t)); return logarithmic ? from * Math.pow(to / from, t) : from + t * (to - from) }
    function text(v) {
        if (format) return format(v)
        const a = Math.abs(v)
        const d = decimals >= 0 ? decimals : (a >= 1000 ? 0 : (a >= 100 ? 0 : (a >= 10 ? 1 : 2)))
        return v.toFixed(d) + (unit !== "" ? " " + unit : "")
    }
    function cancel() { dragging = false; changed = false; dragValue = value }

    Text {  // the value
        id: readout
        anchors.horizontalCenter: parent.horizontalCenter
        y: 0
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        text: root.text(root.shown)
        color: root.dragging ? Theme.textPrimary : Theme.textValue
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
        font.features: { "tnum": 1 }
        elide: Text.ElideRight
        MouseArea {
            anchors.fill: parent
            onDoubleClicked: { entry.text = root.text(root.value).replace(/[^0-9.,\-+]/g, ""); entry.visible = true; entry.forceActiveFocus(); entry.selectAll() }
        }
    }
    TextInput {
        id: entry
        visible: false
        anchors.fill: readout
        horizontalAlignment: TextInput.AlignHCenter
        color: Theme.textValue
        selectByMouse: true
        font: readout.font
        Rectangle { anchors.fill: parent; anchors.margins: -1; z: -1; radius: 3; color: Theme.surfaceLcd; border.color: Theme.accentPrimary }
        Keys.onShortcutOverride: (event) => { if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Escape) event.accepted = true }
        Keys.onEscapePressed: visible = false
        onAccepted: {
            const v = parseFloat(text.replace(",", "."))
            visible = false
            if (isFinite(v)) root.released(Math.max(root.from, Math.min(root.to, v)))
        }
        onActiveFocusChanged: if (!activeFocus) visible = false
    }

    Item {
        id: dial
        anchors.horizontalCenter: parent.horizontalCenter
        y: readout.height + 2
        width: root.knobSize
        height: root.knobSize
        Canvas {  // the track of the ring and the part up to the value
            id: ring
            anchors.fill: parent
            readonly property real t: root.toUnit(root.shown)
            onTChanged: requestPaint()
            onWidthChanged: requestPaint()
            Component.onCompleted: requestPaint()
            Connections { target: Theme; function onThemeNameChanged() { ring.requestPaint() } }
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const cx = width / 2, cy = height / 2, r = width / 2 - 3
                const start = 0.75 * Math.PI, sweep = 1.5 * Math.PI  // from 7:30 round the top to 4:30
                ctx.lineWidth = 3
                ctx.lineCap = "round"
                ctx.strokeStyle = Theme.borderStrong
                ctx.beginPath(); ctx.arc(cx, cy, r, start, start + sweep); ctx.stroke()
                const from = root.bipolar ? 0.5 : 0
                const a = start + sweep * Math.min(from, t), b = start + sweep * Math.max(from, t)
                if (b - a < 0.002) return
                ctx.strokeStyle = root.tint
                ctx.beginPath(); ctx.arc(cx, cy, r, a, b); ctx.stroke()
            }
        }
        Rectangle {  // the face
            id: face
            anchors.centerIn: parent
            width: parent.width - 12
            height: width
            radius: width / 2
            gradient: Gradient {
                GradientStop { position: 0; color: "#9a9aa0" }
                GradientStop { position: 1; color: "#5e5e64" }
            }
            border.color: "#2a2a2e"
            Rectangle {  // the pointer
                width: 2
                height: parent.height / 2 - 3
                radius: 1
                color: "#ffffff"
                x: parent.width / 2 - 1
                y: 3
                transform: Rotation { origin.x: 1; origin.y: face.height / 2 - 3; angle: -135 + root.toUnit(root.shown) * 270 }
            }
        }
        MouseArea {
            anchors.fill: parent
            property real startY: 0
            property real startT: 0
            onPressed: (m) => {
                if (m.modifiers & Qt.AltModifier) {
                    root.cancel()
                    if (root.value !== root.defaultValue) root.released(root.defaultValue)
                    return
                }
                root.dragging = true; root.changed = false; startY = m.y; startT = root.toUnit(root.value); root.dragValue = root.value
            }
            onPositionChanged: (m) => {
                if (!pressed || !root.dragging) return
                const travel = (m.modifiers & Qt.ShiftModifier) ? 900 : 220
                root.dragValue = root.fromUnit(startT + (startY - m.y) / travel)
                root.changed = true
                root.moved(root.dragValue)
            }
            onReleased: { const send = root.dragging && root.changed; const v = root.dragValue; root.cancel(); if (send) root.released(v) }
            onCanceled: root.cancel()
            onDoubleClicked: { root.cancel(); root.released(root.defaultValue) }
            onWheel: (w) => root.released(root.fromUnit(root.toUnit(root.value) + (w.angleDelta.y > 0 ? 1 : -1) * ((w.modifiers & Qt.ShiftModifier) ? 0.005 : 0.02)))
        }
    }
    Text {  // the name
        anchors.horizontalCenter: parent.horizontalCenter
        y: dial.y + dial.height + 2
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        text: root.label
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeCaptionSize
        elide: Text.ElideRight
    }
}
