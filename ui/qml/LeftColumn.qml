import QtQuick
import Jad

// The left column of the window: Library above Inspector, either can be hidden. Resizable on its right edge.
Rectangle {
    id: root
    required property ProjectController project
    readonly property alias inspector: inspector
    readonly property alias library: library
    color: Theme.surfacePanel
    clip: true
    visible: project.inspectorVisible || project.libraryVisible
    width: visible ? project.leftColumnWidth : 0

    Item {
        anchors.fill: parent
        anchors.rightMargin: 5
        Library {
            id: library
            width: parent.width
            height: root.project.inspectorVisible ? parent.height * 0.45 : parent.height
            visible: root.project.libraryVisible
            project: root.project
        }
        Inspector {
            id: inspector
            y: root.project.libraryVisible ? library.height : 0
            width: parent.width
            height: root.project.libraryVisible ? parent.height - library.height : parent.height
            visible: root.project.inspectorVisible
            project: root.project
        }
    }
    Splitter {
        anchors.right: parent.right
        height: parent.height
        onDragged: (dx) => root.project.leftColumnWidth = root.project.leftColumnWidth + dx
    }
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.borderSubtle }
}
