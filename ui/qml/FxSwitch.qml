import QtQuick
import Jad

// An on/off button of the stock effect windows: a small rounded key that lights up when on.
Item {
    id: root
    property string label: ""
    property bool on: false
    property color tint: Theme.accentPrimary
    signal toggled(bool on)
    implicitWidth: Math.max(44, text.implicitWidth + 16)
    implicitHeight: 22
    Rectangle {
        anchors.fill: parent
        radius: 4
        color: root.on ? Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.85) : (area.containsMouse ? "#4a4a50" : "#3a3a3f")
        border.color: root.on ? root.tint : "#1c1c1f"
        Text {
            id: text
            anchors.centerIn: parent
            text: root.label
            color: root.on ? "#ffffff" : Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
            font.weight: 600
        }
    }
    MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; onClicked: root.toggled(!root.on) }
}
