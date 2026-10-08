import QtQuick
import Jad

// A value field with no engine behind it: a click announces it.
Rectangle {
    id: root
    required property ProjectController project
    property string label
    property string text
    implicitWidth: 72
    implicitHeight: 20
    radius: Theme.radiusControl - 2
    color: area.containsMouse ? Theme.surfaceRaisedHover : Theme.surfaceRaised
    opacity: 0.8
    Text {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing[2]
        verticalAlignment: Text.AlignVCenter
        text: root.text
        elide: Text.ElideRight
        color: Theme.textValue
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
    }
    MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; onClicked: root.project.announceStub(root.label) }
}
