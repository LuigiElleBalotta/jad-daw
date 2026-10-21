import QtQuick
import Jad

// A switch of the dark blue plug-in windows: a flat key that is blue when on. `sup` is a small tag after the label (the POST of Analyzer).
Item {
    id: root
    property string label: ""
    property string sup: ""
    property bool on: false
    property bool enabledKey: true
    property color onColor: "#1e3a5c"
    property color offColor: "#122234"
    property color textColor: "#d3dde8"
    property int pixelSize: 18
    signal toggled(bool on)
    implicitWidth: row.implicitWidth + 24
    implicitHeight: 30
    opacity: enabledKey ? 1 : 0.7
    Rectangle { anchors.fill: parent; radius: 3; color: root.on ? root.onColor : root.offColor; border.color: root.on ? "#2b4d75" : "transparent" }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 2
        Text { id: main; text: root.label; color: root.on ? root.textColor : "#8fa3b8"; font.family: Theme.fontFamily; font.pixelSize: root.pixelSize }
        Text { visible: root.sup !== ""; anchors.baseline: main.baseline; anchors.baselineOffset: -7; text: root.sup; color: root.on ? root.textColor : "#8fa3b8"; font.family: Theme.fontFamily; font.pixelSize: Math.round(root.pixelSize * 0.6) }
    }
    MouseArea { anchors.fill: parent; enabled: root.enabledKey; onClicked: root.toggled(!root.on) }
}
