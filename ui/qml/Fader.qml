import QtQuick
import Jad

Item {
    id: root
    property real value: 0
    property real from: -96
    property real to: 12
    signal moved(real value)
    signal released(real value)

    // a fader is dB-shaped: 0 dB sits at 75% of the travel
    function posToValue(y) {
        const t = 1 - Math.max(0, Math.min(1, y / height))
        return t >= 0.75 ? 0 + (t - 0.75) / 0.25 * to
                         : from + t / 0.75 * (0 - from)
    }
    function valueToPos(v) {
        const t = v >= 0 ? 0.75 + v / to * 0.25 : (v - from) / (0 - from) * 0.75
        return (1 - Math.max(0, Math.min(1, t))) * height
    }
    property real dragValue: value
    property bool dragging: false

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        width: Theme.sizeFaderTrack + 1
        height: parent.height
        radius: 2
        color: Theme.surfaceRaised
    }
    Rectangle {
        id: handle
        width: parent.width
        height: 14
        radius: Theme.radiusControl
        color: Theme.textPrimary
        y: root.valueToPos(root.dragging ? root.dragValue : root.value) - height / 2
    }
    MouseArea {
        anchors.fill: parent
        onPressed: (m) => { root.dragging = true; root.dragValue = root.posToValue(m.y); root.moved(root.dragValue) }
        onPositionChanged: (m) => { if (pressed) { root.dragValue = root.posToValue(m.y); root.moved(root.dragValue) } }
        onReleased: { root.dragging = false; root.released(root.dragValue) }
    }
}
