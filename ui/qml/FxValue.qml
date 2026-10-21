import QtQuick
import Jad

// A number of the stock effect windows that is dragged up and down (Shift: fine), double-clicked to type, Option-clicked to reset: Logic's parameter fields.
Item {
    id: root
    property string label: ""
    property real from: 0
    property real to: 1
    property real defaultValue: 0
    property real value: 0
    property bool logarithmic: false
    property string unit: ""
    property color tint: Theme.textValue
    property bool enabledLook: true
    property var format: null
    signal moved(real value)
    signal released(real value)
    implicitWidth: 64
    implicitHeight: 34
    property real dragValue: value
    property bool dragging: false
    property bool changed: false
    readonly property real shown: dragging ? dragValue : value
    function toUnit(v) { return logarithmic ? Math.log(Math.max(v, from) / from) / Math.log(to / from) : (v - from) / (to - from) }
    function fromUnit(t) { t = Math.max(0, Math.min(1, t)); return logarithmic ? from * Math.pow(to / from, t) : from + t * (to - from) }
    function text(v) {
        if (format) return format(v)
        const a = Math.abs(v)
        return v.toFixed(a >= 1000 ? 0 : (a >= 100 ? 0 : (a >= 10 ? 1 : 2))) + (unit !== "" ? " " + unit : "")
    }
    function cancel() { dragging = false; changed = false; dragValue = value }
    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        text: root.label
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeCaptionSize
        elide: Text.ElideRight
    }
    Rectangle {
        y: 14
        width: parent.width
        height: 20
        radius: 3
        color: "#1b1b1e"
        border.color: root.dragging ? Theme.accentPrimary : "#2e2e33"
        opacity: root.enabledLook ? 1 : 0.4
        Text {
            id: shownText
            anchors.fill: parent
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: root.text(root.shown)
            color: root.tint
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
            font.features: { "tnum": 1 }
        }
        TextInput {
            id: entry
            visible: false
            anchors.fill: parent
            horizontalAlignment: TextInput.AlignHCenter
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.textValue
            selectByMouse: true
            font: shownText.font
            Keys.onShortcutOverride: (event) => { if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Escape) event.accepted = true }
            Keys.onEscapePressed: visible = false
            onAccepted: {
                const v = parseFloat(text.replace(",", "."))
                visible = false
                if (isFinite(v)) root.released(Math.max(root.from, Math.min(root.to, v)))
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
                const travel = (m.modifiers & Qt.ShiftModifier) ? 1000 : 240
                root.dragValue = root.fromUnit(startT + (startY - m.y) / travel)
                root.changed = true
                root.moved(root.dragValue)
            }
            onReleased: { const send = root.dragging && root.changed; const v = root.dragValue; root.cancel(); if (send) root.released(v) }
            onCanceled: root.cancel()
            onDoubleClicked: { root.cancel(); entry.text = root.text(root.value).replace(/[^0-9.,\-+]/g, ""); entry.visible = true; entry.forceActiveFocus(); entry.selectAll() }
            onWheel: (w) => root.released(root.fromUnit(root.toUnit(root.value) + (w.angleDelta.y > 0 ? 1 : -1) * ((w.modifiers & Qt.ShiftModifier) ? 0.004 : 0.015)))
        }
    }
}
