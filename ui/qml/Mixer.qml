import QtQuick
import QtQuick.Layouts
import Jad

// The Mixer docked at the bottom of the main window: a thin title row (collapse), a drag handle on its top edge for the
// height, and the Mixer itself. The Mixer window (Main.qml) shows the same MixerView when the Mixer is detached.
Rectangle {
    id: root
    required property ProjectController project
    property bool expanded: true
    property real maxHeight: 100000  // what the window can give it (the rest of the window keeps a little room)
    readonly property real headerHeight: 24
    readonly property alias view: view

    color: Theme.surfaceCanvas
    implicitHeight: expanded ? Math.min(Math.max(project.mixerHeight, 584), Math.max(584, maxHeight)) : headerHeight
    clip: true

    Rectangle {
        id: header
        width: parent.width
        height: root.headerHeight
        color: Theme.surfacePanel
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.borderSubtle }
        Row {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacing[3]
            spacing: Theme.spacing[3]
            IconButton {
                implicitWidth: 20
                implicitHeight: 20
                source: root.expanded ? "icons/chevron-down.svg" : "icons/chevron-right.svg"
                onClicked: root.expanded = !root.expanded
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Mixer")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeLabelSize
                font.weight: Theme.fontTypeLabelWeight
            }
        }
    }

    MixerView {
        id: view
        visible: root.expanded
        y: root.headerHeight
        width: parent.width
        height: parent.height - root.headerHeight
        project: root.project
        onDetachToggled: root.project.mixerDetached = true
    }

    Splitter {  // the top edge: dragging up makes the Mixer taller
        orientation: Qt.Vertical
        visible: root.expanded
        anchors.top: parent.top
        width: parent.width
        z: 10
        onDragged: (dy) => root.project.mixerHeight = Math.min(root.project.mixerHeight - dy, Math.max(584, root.maxHeight))
    }
}
