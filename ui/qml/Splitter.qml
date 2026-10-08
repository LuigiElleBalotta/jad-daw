import QtQuick
import Jad

// A thin drag handle on the right edge of the left column; `dragged(dx)` is the horizontal movement since the last event.
Rectangle {
    id: root
    signal dragged(real dx)
    width: 5
    color: area.containsMouse || area.pressed ? Theme.accentPrimary : "transparent"
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.SplitHCursor
        property real lastX: 0
        onPressed: (m) => { lastX = mapToItem(null, m.x, 0).x }
        onPositionChanged: (m) => {
            if (!pressed) return
            const x = mapToItem(null, m.x, 0).x
            root.dragged(x - lastX)
            lastX = x
        }
    }
}
