import QtQuick
import Jad

Item {
    id: root
    property real value: 0
    property real from: -1
    property real to: 1
    signal moved(real value)
    signal released(real value)

    implicitWidth: 28
    implicitHeight: 28
    width: implicitWidth
    height: implicitHeight
    property real dragValue: value
    property bool dragging: false
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
        onPressed: (m) => { root.dragging = true; startY = m.y; startValue = root.value; root.dragValue = root.value }
        onPositionChanged: (m) => {
            if (!pressed) return
            const span = root.to - root.from
            root.dragValue = Math.max(root.from, Math.min(root.to, startValue + (startY - m.y) / 200 * span))
            root.moved(root.dragValue)
        }
        onReleased: { root.dragging = false; root.released(root.dragValue) }
        onDoubleClicked: { root.dragValue = 0; root.released(0) }
    }
}
