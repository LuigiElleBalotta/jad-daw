import QtQuick
import Jad

Rectangle {
    id: root
    required property real pixelsPerBeat
    required property real scrollBeats
    required property real barBeats  // quarter-note beats in a bar
    property real playheadBeats: 0
    property bool soloActive: false
    property real loopStart: 0                  // the cycle area (a strip along the top edge): yellow while the cycle is on, grey when off
    property real loopEnd: 0
    property bool loopOn: false
    signal cycleRequested(real start, real end)   // the strip was dragged: a new area
    signal locateRequested(real beats)  // a click or a drag in the ruler, or on the playhead handle

    color: Theme.surfacePanel
    clip: true

    readonly property real barWidth: pixelsPerBeat * barBeats
    // label only every N bars when they get too dense
    readonly property int step: Math.max(1, Math.ceil(48 / barWidth))
    readonly property int firstBar: Math.floor(scrollBeats / barBeats / step) * step
    readonly property int barCount: Math.ceil(width / barWidth / step) + 2

    Repeater {
        model: root.barCount
        delegate: Item {
            required property int index
            readonly property int bar: root.firstBar + index * root.step
            x: (bar * root.barBeats - root.scrollBeats) * root.pixelsPerBeat
            height: root.height
            Rectangle { width: 1; height: parent.height; color: Theme.borderStrong }
            Text {
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                text: parent.bar + 1
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeCaptionSize
            }
        }
    }
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }

    // the playhead handle: a small flag at the top of the playhead line
    Rectangle {
        id: thumb
        x: (root.playheadBeats - root.scrollBeats) * root.pixelsPerBeat - width / 2
        width: 9
        height: 10
        radius: 2
        color: root.soloActive ? Theme.stateSolo : Theme.textSecondary
        visible: x + width >= 0 && x <= root.width
    }
    Rectangle {  // the cycle area
        visible: root.loopEnd > root.loopStart
        x: (root.loopStart - root.scrollBeats) * root.pixelsPerBeat
        width: (root.loopEnd - root.loopStart) * root.pixelsPerBeat
        height: 7
        color: root.loopOn ? Theme.stateSolo : Theme.surfaceRaisedHover
        opacity: root.loopOn ? 0.9 : 0.8
        border.color: Theme.borderStrong
        radius: 2
        z: 2
    }
    MouseArea {  // drag the cycle area: its edges resize it, its body moves it, an empty strip draws one
        id: cycleArea
        width: parent.width
        height: 8
        z: 3
        property string mode: ""
        property real origin: 0
        property real s0: 0
        property real e0: 0
        cursorShape: mode === "move" ? Qt.ClosedHandCursor : Qt.ArrowCursor
        function at(m) { return Math.max(0, m.x / root.pixelsPerBeat + root.scrollBeats) }
        onPressed: (m) => {
            const b = at(m)
            const edge = 5 / root.pixelsPerBeat
            s0 = root.loopStart; e0 = root.loopEnd; origin = b
            if (e0 > s0 && Math.abs(b - s0) <= edge) mode = "start"
            else if (e0 > s0 && Math.abs(b - e0) <= edge) mode = "end"
            else if (e0 > s0 && b > s0 && b < e0) mode = "move"
            else { mode = "draw"; s0 = b; e0 = b }
        }
        onPositionChanged: (m) => {
            if (!pressed || mode === "") return
            const b = at(m)
            if (mode === "start") root.cycleRequested(Math.min(b, e0 - 1 / 16), e0)
            else if (mode === "end") root.cycleRequested(s0, Math.max(b, s0 + 1 / 16))
            else if (mode === "move") { const d = Math.max(-s0, b - origin); root.cycleRequested(s0 + d, e0 + d) }
            else if (mode === "draw" && Math.abs(b - s0) > 1 / 16) root.cycleRequested(Math.min(s0, b), Math.max(s0, b))
        }
        onReleased: mode = ""
    }
    MouseArea {  // a press sets the position, dragging follows the pointer
        anchors.fill: parent
        anchors.topMargin: 8
        function at(m) { return m.x / root.pixelsPerBeat + root.scrollBeats }
        onPressed: (m) => root.locateRequested(at(m))
        onPositionChanged: (m) => { if (pressed) root.locateRequested(at(m)) }
    }
}
