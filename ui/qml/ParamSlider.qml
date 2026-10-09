import QtQuick
import Jad

// A horizontal slider for one effect parameter: a drag moves it (a log scale for frequencies and times), Shift is fine, a double click
// or Option-click goes back to the default.
Item {
    id: root
    property real value: 0
    property real from: 0
    property real to: 1
    property real defaultValue: 0
    property bool logarithmic: false
    property bool dragging: false
    property real dragValue: value
    readonly property real shown: dragging ? dragValue : value
    signal moved(real value)
    signal released(real value)

    implicitHeight: 18
    implicitWidth: 160

    function toFraction(v) {
        const c = Math.max(from, Math.min(to, v))
        return logarithmic ? Math.log(c / from) / Math.log(to / from) : (c - from) / (to - from)
    }
    function fromFraction(f) {
        const c = Math.max(0, Math.min(1, f))
        return logarithmic ? from * Math.pow(to / from, c) : from + c * (to - from)
    }

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 4
        radius: 2
        color: Theme.surfaceRaised
        Rectangle {  // from the centre (or the left) to the value
            readonly property real zero: root.from < 0 && root.to > 0 && !root.logarithmic ? root.toFraction(0) : 0
            x: Math.min(zero, root.toFraction(root.shown)) * parent.width
            width: Math.abs(root.toFraction(root.shown) - zero) * parent.width
            height: parent.height
            radius: 2
            color: Theme.accentPrimary
        }
    }
    Rectangle {
        x: root.toFraction(root.shown) * (root.width - width)
        anchors.verticalCenter: parent.verticalCenter
        width: 10; height: 14; radius: 3
        color: "#b9b9be"
    }
    MouseArea {
        anchors.fill: parent
        property real pressX: 0
        property real pressValue: 0
        function at(m, fine) {
            const f = fine ? root.toFraction(pressValue) + (m.x - pressX) / root.width * 0.2 : m.x / root.width
            return root.fromFraction(f)
        }
        onPressed: (m) => {
            if (m.modifiers & Qt.AltModifier) { root.dragValue = root.defaultValue; root.moved(root.defaultValue); root.released(root.defaultValue); return }
            root.dragging = true
            pressX = m.x
            pressValue = root.value
            root.dragValue = at(m, false)
            root.moved(root.dragValue)
        }
        onPositionChanged: (m) => {
            if (!pressed || !root.dragging) return
            root.dragValue = at(m, (m.modifiers & Qt.ShiftModifier) !== 0)
            root.moved(root.dragValue)
        }
        onReleased: { if (root.dragging) { root.dragging = false; root.released(root.dragValue) } }
        onCanceled: root.dragging = false
        onDoubleClicked: { root.dragging = false; root.dragValue = root.defaultValue; root.moved(root.defaultValue); root.released(root.defaultValue) }
    }
}
