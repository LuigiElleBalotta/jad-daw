import QtQuick
import Jad

Item {
    id: root
    property real value: 0
    property real from: -1
    property real to: 1
    property real resetValue: 0
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

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.surfaceRaised
        border.color: Theme.borderStrong
        border.width: 1
    }
    Rectangle {
        // pointer sweeps -135..135 degrees across the range
        width: 2
        height: root.height / 2 - 4
        radius: 1
        color: Theme.accentPrimary
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
        onDoubleClicked: { root.cancel(); root.dragValue = root.resetValue; root.released(root.resetValue) }
    }
}
