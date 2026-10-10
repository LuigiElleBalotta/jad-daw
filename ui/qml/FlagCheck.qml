import QtQuick
import Jad

// A checkbox of the Inspector: shows `on` and asks to flip it (the owner decides what that does).
Rectangle {
    id: root
    property bool on: false
    signal flipped()
    implicitWidth: 16
    implicitHeight: 16
    radius: 3
    color: on ? Theme.accentPrimary : Theme.surfaceRaised
    border.color: Theme.borderStrong
    MouseArea {
        anchors.fill: parent
        onClicked: root.flipped()
    }
}
