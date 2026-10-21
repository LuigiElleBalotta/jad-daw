import QtQuick
import Jad

// The thin horizontal slider of the dark blue windows: the label at the left and the value at the right above a track that is filled up to a small upright thumb.
// Drag it, scroll on it, double-click to put the default back.
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
    property color tint: "#8dffa0"
    property bool dimmed: false
    signal moved(real value)
    signal released(real value)
    implicitWidth: 200
    implicitHeight: 52
    opacity: dimmed ? 0.5 : 1
    property real dragValue: value
    property bool dragging: false
    readonly property real shown: dragging ? dragValue : value
    function toUnit(v) { return logarithmic ? Math.log(Math.max(v, from) / from) / Math.log(to / from) : (v - from) / (to - from) }
    function fromUnit(t) { t = Math.max(0, Math.min(1, t)); return logarithmic ? from * Math.pow(to / from, t) : from + t * (to - from) }
    Text { x: 0; y: 0; text: root.label; color: "#9db2c8"; font.family: Theme.fontFamily; font.pixelSize: 19 }
    Row {
        anchors.right: parent.right; y: -2
        spacing: 4
        Text { id: num; text: root.shown.toFixed(root.decimals); color: root.tint; font.family: Theme.fontFamily; font.pixelSize: 26; font.features: { "tnum": 1 } }
        Text { anchors.baseline: num.baseline; text: root.unit; color: root.tint; font.family: Theme.fontFamily; font.pixelSize: 19 }
    }
    Rectangle { x: 0; y: 41; width: parent.width; height: 3; color: "#0a1119" }
    Rectangle { x: 0; y: 41; width: root.toUnit(root.shown) * (parent.width - 6) + 3; height: 3; color: root.tint }
    Rectangle {
        x: root.toUnit(root.shown) * (parent.width - 6) - 1; y: 32; width: 8; height: 22; color: "#22364b"; border.color: "#0a1119"
        Rectangle { x: 3.5; y: 3; width: 1.5; height: 16; color: root.tint }
    }
    MouseArea {
        x: 0; y: 26; width: parent.width; height: 30
        onPressed: (m) => { root.dragging = true; root.dragValue = root.fromUnit(m.x / width); root.moved(root.dragValue) }
        onPositionChanged: (m) => { if (!pressed) return; root.dragValue = root.fromUnit(m.x / width); root.moved(root.dragValue) }
        onReleased: { const v = root.dragValue; root.dragging = false; root.released(v) }
        onCanceled: root.dragging = false
        onDoubleClicked: { root.dragging = false; root.released(root.defaultValue) }
        onWheel: (w) => root.released(root.fromUnit(root.toUnit(root.value) + (w.angleDelta.y > 0 ? 1 : -1) * 0.02))
    }
}
