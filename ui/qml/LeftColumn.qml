import QtQuick
import Jad

// The left column of the window: Library above Inspector, either can be hidden. Resizable on its right edge.
Rectangle {
    id: root
    required property ProjectController project
    readonly property alias inspector: inspector
    color: Theme.surfacePanel
    visible: project.inspectorVisible || project.libraryVisible
    width: visible ? project.leftColumnWidth : 0

    Inspector {
        id: inspector
        anchors.fill: parent
        anchors.rightMargin: 5
        visible: root.project.inspectorVisible
        project: root.project
    }
    Splitter {
        anchors.right: parent.right
        height: parent.height
        onDragged: (dx) => root.project.leftColumnWidth = root.project.leftColumnWidth + dx
    }
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.borderSubtle }
}
