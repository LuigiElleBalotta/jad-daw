import QtQuick
import Jad

// The left area of the window: Library and Inspector side by side (the Library first), either can be hidden. Each pane is as wide as
// the setting; the area is resizable on its right edge.
Rectangle {
    id: root
    required property ProjectController project
    readonly property alias inspector: inspector
    readonly property alias library: library
    color: Theme.surfacePanel
    clip: true
    visible: project.inspectorVisible || project.libraryVisible
    readonly property int panes: (project.libraryVisible ? 1 : 0) + (project.inspectorVisible ? 1 : 0)
    width: visible ? project.leftColumnWidth * panes : 0

    Item {
        anchors.fill: parent
        anchors.rightMargin: 5
        Library {
            id: library
            width: root.project.leftColumnWidth - (root.project.inspectorVisible ? 0 : 5)
            height: parent.height
            visible: root.project.libraryVisible
            project: root.project
        }
        Rectangle {  // between the two
            visible: root.panes === 2
            x: root.project.leftColumnWidth - 1
            width: 1
            height: parent.height
            color: Theme.borderSubtle
        }
        Inspector {
            id: inspector
            x: root.project.libraryVisible ? root.project.leftColumnWidth : 0
            width: root.project.leftColumnWidth - 5
            height: parent.height
            visible: root.project.inspectorVisible
            project: root.project
        }
    }
    Splitter {
        anchors.right: parent.right
        height: parent.height
        onDragged: (dx) => root.project.leftColumnWidth = root.project.leftColumnWidth + dx / Math.max(1, root.panes)
    }
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.borderSubtle }
}
