import QtQuick
import Jad

// A number of the stock effect windows with its unit in smaller type: drag up and down to change it (Shift: fine), scroll, double-click to type, Option-click or
// double-click on the label... to put the default back. `scaleBy` shows the value times a factor (the slope of a cut: 2 is 12 dB/Oct), `snap` keeps whole numbers.
Item {
    id: root
    property real from: 0
    property real to: 1
    property real defaultValue: 0
    property real value: 0
    property bool logarithmic: false
    property string unit: ""
    property int decimals: 1
    property real scaleBy: 1
    property bool plusSign: false
    property bool snap: false
    property color textColor: "#d5dee8"
    property color unitColor: textColor
    property int pixelSize: 18
    property bool showUnit: true
    signal moved(real value)
    signal released(real value)
    implicitWidth: 100
    implicitHeight: 24
    property real dragValue: value
    property bool dragging: false
    property bool changed: false
    readonly property real shown: dragging ? dragValue : value
    function toUnit(v) { return logarithmic ? Math.log(Math.max(v, from) / from) / Math.log(to / from) : (v - from) / (to - from) }
    function fromUnit(t) { t = Math.max(0, Math.min(1, t)); const v = logarithmic ? from * Math.pow(to / from, t) : from + t * (to - from); return snap ? Math.round(v) : v }
    function numberText(v) { const x = v * scaleBy; return (plusSign && x > 0 ? "+" : "") + x.toFixed(decimals) }
    function cancel() { dragging = false; changed = false; dragValue = value }
    Row {
        anchors.centerIn: parent
        spacing: 3
        Text { id: number; text: root.numberText(root.shown); color: root.textColor; font.family: Theme.fontFamily; font.pixelSize: root.pixelSize; font.features: { "tnum": 1 }; visible: !entry.visible }
        Text { id: unitText; visible: root.unit !== "" && root.showUnit && !entry.visible; anchors.baseline: number.baseline; text: root.unit; color: root.unitColor; opacity: 0.85; font.family: Theme.fontFamily; font.pixelSize: Math.round(root.pixelSize * 0.72) }
    }
    TextInput {
        id: entry
        visible: false
        anchors.fill: parent
        horizontalAlignment: TextInput.AlignHCenter
        verticalAlignment: TextInput.AlignVCenter
        color: "#ffffff"
        selectByMouse: true
        font.family: Theme.fontFamily
        font.pixelSize: root.pixelSize
        Rectangle { anchors.fill: parent; z: -1; color: "#0d1a27"; border.color: Theme.accentPrimary }
        Keys.onShortcutOverride: (event) => { if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Escape) event.accepted = true }
        Keys.onEscapePressed: visible = false
        onAccepted: {
            const v = parseFloat(text.replace(",", ".")) / root.scaleBy
            visible = false
            if (isFinite(v)) root.released(Math.max(root.from, Math.min(root.to, root.snap ? Math.round(v) : v)))
        }
        onActiveFocusChanged: if (!activeFocus) visible = false
    }
    MouseArea {
        anchors.fill: parent
        property real startY: 0
        property real startT: 0
        onPressed: (m) => {
            if (m.modifiers & Qt.AltModifier) { root.cancel(); if (root.value !== root.defaultValue) root.released(root.defaultValue); return }
            root.dragging = true; root.changed = false; startY = m.y; startT = root.toUnit(root.value); root.dragValue = root.value
        }
        onPositionChanged: (m) => {
            if (!pressed || !root.dragging) return
            root.dragValue = root.fromUnit(startT + (startY - m.y) / ((m.modifiers & Qt.ShiftModifier) ? 1000 : 240))
            root.changed = true
            root.moved(root.dragValue)
        }
        onReleased: { const send = root.dragging && root.changed; const v = root.dragValue; root.cancel(); if (send) root.released(v) }
        onCanceled: root.cancel()
        onDoubleClicked: { root.cancel(); entry.text = (root.value * root.scaleBy).toFixed(root.decimals); entry.visible = true; entry.forceActiveFocus(); entry.selectAll() }
        onWheel: (w) => root.released(root.fromUnit(root.toUnit(root.value) + (w.angleDelta.y > 0 ? 1 : -1) * ((w.modifiers & Qt.ShiftModifier) ? 0.004 : (snap ? 0.14 : 0.015))))
    }
}
