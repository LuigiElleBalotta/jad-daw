import QtQuick
import Jad

Item {
    id: root
    property real value: 0
    property real from: -1
    property real to: 1
    property real resetValue: 0
    property bool doubleClickEdits: false  // a double click types a value (value * entryScale) instead of resetting
    property real entryScale: 1
    property bool centerMark: false         // a small green tick at the top: the centre position
    property bool fillArc: false            // the ring is coloured from the centre position to the pointer (the pan knob)
    property var format: null               // value -> text; shown above the knob while it is dragged
    signal moved(real value)
    signal released(real value)

    implicitWidth: 28
    implicitHeight: 28
    width: implicitWidth
    height: implicitHeight
    property real dragValue: value
    property bool dragging: false
    property bool changed: false  // a plain click must not send a command
    function cancel() { dragging = false; changed = false; dragValue = value }
    readonly property real shown: dragging ? dragValue : value

    Rectangle {  // the dark ring and the light face
        anchors.fill: parent
        radius: width / 2
        color: Theme.surfaceCanvas
        border.color: Theme.borderStrong
        border.width: 1
        Rectangle {
            anchors.centerIn: parent
            width: parent.width - 6
            height: width
            radius: width / 2
            color: "#7d7d82"
        }
    }
    Canvas {  // the coloured part of the ring, from the centre towards the pointer
        id: arc
        visible: root.fillArc
        anchors.fill: parent
        readonly property real shownValue: root.shown
        onShownValueChanged: requestPaint()
        onWidthChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const centre = (root.from + root.to) / 2
            if (Math.abs(root.shown - centre) < 1e-4) return
            const turn = (root.shown - root.from) / (root.to - root.from) * 270 - 135  // degrees from the top, clockwise
            const r = width / 2 - 1.5
            ctx.lineWidth = 3
            ctx.strokeStyle = Theme.accentPrimary
            ctx.beginPath()
            ctx.arc(width / 2, height / 2, r, -Math.PI / 2, (turn - 90) * Math.PI / 180, turn < 0)
            ctx.stroke()
        }
    }
    Rectangle {  // the centre mark
        visible: root.centerMark
        x: root.width / 2 - 1
        y: -2
        width: 2
        height: 4
        color: Theme.statePlay
    }
    Rectangle {
        // pointer sweeps -135..135 degrees across the range
        width: 2
        height: root.height / 2 - 4
        radius: 1
        color: Theme.textPrimary
        x: root.width / 2 - 1
        y: 4
        transform: Rotation {
            origin.x: 1
            origin.y: root.height / 2 - 4
            angle: -135 + (root.shown - root.from) / (root.to - root.from) * 270
        }
    }
    MouseArea {
        anchors.fill: parent
        property real startY: 0
        property real startValue: 0
        onPressed: (m) => {
            if (m.modifiers & Qt.AltModifier) {  // Option-click: back to the neutral position
                root.cancel()
                root.dragValue = root.resetValue
                if (root.value !== root.resetValue) root.released(root.resetValue)
                return
            }
            root.dragging = true; root.changed = false; startY = m.y; startValue = root.value; root.dragValue = root.value }
        onPositionChanged: (m) => {
            if (!pressed) return
            const span = root.to - root.from
            const travel = (m.modifiers & Qt.ShiftModifier) ? 800 : 200  // Shift: fine
            root.dragValue = Math.max(root.from, Math.min(root.to, startValue + (startY - m.y) / travel * span))
            root.changed = true
            root.moved(root.dragValue)
        }
        onReleased: { const send = root.dragging && root.changed; const v = root.dragValue; root.cancel(); if (send) root.released(v) }
        onCanceled: root.cancel()
        onDoubleClicked: {
            root.cancel()
            if (root.doubleClickEdits) { entry.text = Math.round(root.value * root.entryScale).toString(); entry.visible = true; entry.forceActiveFocus(); entry.selectAll(); return }
            root.dragValue = root.resetValue
            root.released(root.resetValue)
        }
    }
    Rectangle {  // the value while the knob is dragged
        visible: root.dragging && root.changed && root.format !== null
        anchors.horizontalCenter: parent.horizontalCenter
        y: -height - 2
        width: valueText.implicitWidth + 8
        height: valueText.implicitHeight + 4
        radius: 3
        color: Theme.surfaceLcd
        border.color: Theme.borderStrong
        z: 10
        Text {
            id: valueText
            anchors.centerIn: parent
            text: root.format !== null ? root.format(root.shown) : ""
            color: Theme.textValue
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
        }
    }
    TextInput {  // the typed value of a double click
        id: entry
        visible: false
        anchors.centerIn: parent
        width: Math.max(root.width, 28)
        horizontalAlignment: TextInput.AlignHCenter
        color: Theme.textValue
        selectByMouse: true
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
        Rectangle { anchors.fill: parent; anchors.margins: -2; z: -1; radius: 3; color: Theme.surfaceLcd; border.color: Theme.accentPrimary }
        Keys.onShortcutOverride: (event) => { if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Escape) event.accepted = true }
        Keys.onEscapePressed: visible = false
        onAccepted: {
            const v = parseFloat(text.replace(",", "."))
            visible = false
            if (isFinite(v)) {
                const next = Math.max(root.from, Math.min(root.to, v / root.entryScale))
                if (Math.abs(next - root.value) > 1e-6) root.released(next)
            }
        }
        onActiveFocusChanged: if (!activeFocus) visible = false
    }
}
