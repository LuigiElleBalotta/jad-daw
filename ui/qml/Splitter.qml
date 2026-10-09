import QtQuick
import Jad

// A thin drag handle. Horizontal (the default): a vertical bar on the right edge of a column, `dragged(delta)` is the
// horizontal movement since the last event. Vertical: a horizontal bar on the top edge of a pane, `dragged(delta)` is the
// vertical movement (down is positive). Size the long side and anchor the bar where it belongs.
Rectangle {
    id: root
    property int orientation: Qt.Horizontal
    readonly property real thickness: 5
    signal dragged(real delta)
    width: orientation === Qt.Horizontal ? thickness : implicitWidth
    height: orientation === Qt.Vertical ? thickness : implicitHeight
    color: area.containsMouse || area.pressed ? Theme.accentPrimary : "transparent"
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.orientation === Qt.Horizontal ? Qt.SplitHCursor : Qt.SplitVCursor
        property real last: 0
        function along(m) {
            const p = mapToItem(null, m.x, m.y)
            return root.orientation === Qt.Horizontal ? p.x : p.y
        }
        onPressed: (m) => { last = along(m) }
        onPositionChanged: (m) => {
            if (!pressed) return
            const v = along(m)
            root.dragged(v - last)
            last = v
        }
    }
}
