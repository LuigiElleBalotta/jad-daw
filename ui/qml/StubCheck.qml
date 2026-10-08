import QtQuick
import Jad

// A checkbox with no engine behind it: it flips, and announces itself when switched on.
Rectangle {
    id: root
    required property ProjectController project
    property string label
    property bool on: false
    implicitWidth: 16
    implicitHeight: 16
    radius: 3
    color: on ? Theme.accentPrimary : Theme.surfaceRaised
    border.color: Theme.borderStrong
    MouseArea {
        anchors.fill: parent
        onClicked: {
            root.on = !root.on
            if (root.on) root.project.announceStub(root.label)
        }
    }
}
