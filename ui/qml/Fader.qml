import QtQuick
import Jad

Item {
    id: root
    property real value: 0
    property real from: -96
    property real to: 6
    signal moved(real value)
    signal released(real value)

    // Logic's taper: the marks of its scale as [fraction of the travel from the top, dB]; the ends follow `to` and `from`
    readonly property var scale: [[0.00, to], [0.13, 3], [0.26, 0], [0.38, -3], [0.52, -6], [0.60, -10],
                                  [0.71, -15], [0.79, -20], [0.87, -30], [0.91, -40], [1.00, from]]
    function posToValue(y) {
        const f = Math.max(0, Math.min(1, y / height))
        for (let i = 1; i < scale.length; ++i) {
            if (f <= scale[i][0]) {
                const a = scale[i - 1], b = scale[i]
                return a[1] + (f - a[0]) / (b[0] - a[0]) * (b[1] - a[1])
            }
        }
        return from
    }
    function valueToPos(v) {
        const d = Math.max(from, Math.min(to, v))
        for (let i = 1; i < scale.length; ++i) {
            if (d >= scale[i][1]) {
                const a = scale[i - 1], b = scale[i]
                return (a[0] + (d - a[1]) / (b[1] - a[1]) * (b[0] - a[0])) * height
            }
        }
        return height
    }
    property real dragValue: value
    property bool dragging: false
    property real pressY: 0       // where a fine (Shift) drag started, and the value there
    property real pressValue: 0
    function cancel() { dragging = false }
    function dragTo(y, fine) {
        dragValue = fine ? posToValue(valueToPos(pressValue) + (y - pressY) * 0.2) : posToValue(y)
        moved(dragValue)
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        width: Theme.sizeFaderTrack + 1
        height: parent.height
        radius: 2
        color: Theme.surfaceRaised
    }
    Rectangle {  // the short line of the 0 dB mark
        x: 0
        width: parent.width
        height: 1
        y: root.valueToPos(0)
        color: Theme.textSecondary
    }
    Rectangle {
        id: handle
        width: parent.width
        height: 14
        radius: Theme.radiusControl
        color: "#b9b9be"
        y: root.valueToPos(root.dragging ? root.dragValue : root.value) - height / 2
        Repeater {  // the ridges of the cap
            model: [-3, 0, 3]
            delegate: Rectangle { required property int modelData; x: 3; y: handle.height / 2 + modelData - 0.5; width: handle.width - 6; height: 1; color: modelData === 0 ? "#20000000" : "#50ffffff" }
        }
    }
    MouseArea {
        anchors.fill: parent
        onPressed: (m) => {
            root.dragging = true
            if (m.modifiers & Qt.AltModifier) {  // Option-click: back to 0 dB
                root.dragValue = 0
                root.moved(0)
                return
            }
            root.pressY = m.y
            root.pressValue = root.value
            root.dragTo(m.y, m.modifiers & Qt.ShiftModifier)
        }
        onPositionChanged: (m) => { if (pressed) root.dragTo(m.y, m.modifiers & Qt.ShiftModifier) }
        onReleased: { const send = root.dragging; root.cancel(); if (send) root.released(root.dragValue) }
        onCanceled: root.cancel()
        onDoubleClicked: {  // the second press already started a drag: it ends at 0 dB
            root.dragValue = 0
            root.moved(0)
        }
    }
}
