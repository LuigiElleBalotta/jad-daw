import QtQuick
import QtQuick.Controls.Basic
import Jad

// The Cell Editor of the Live Loops grid: the sound (audio cell) or the notes (MIDI, Session Player and pattern cell) of the selected cell, with its loop settings on the ruler.
// The start marker (green), the loop start and loop end (the loop area), and the end of the cell can be dragged; the loop area is grabbed by its middle and moved as a whole.
// Control-click (or right-click) the ruler for Set Start Position Here, Set Loop Start Here, Set Loop End Here and Move Loop Forward / Backward by Loop Length.
Rectangle {
    id: root
    objectName: "cellEditor"
    required property ProjectController project
    property string trackId
    property int scene
    property var info: ({ exists: false })      // the settings of the cell (liveCellInfo)
    property var content: ({ exists: false })   // what it holds (liveCellContent)
    readonly property real gutter: 12
    readonly property real rulerH: 22
    readonly property real snapBeats: 0.25
    readonly property real lengthBeats: content.exists ? Math.max(0.25, content.lengthBeats) : 1
    readonly property real pxPerBeat: Math.max(1, (width - 2 * gutter) / lengthBeats)
    // the markers as they are now; while one is dragged it shows where it will land
    property string dragging: ""
    property real dragStart: 0
    property real dragLoopStart: 0
    property real dragLoopLength: 0
    property real dragEnd: 0
    readonly property real startBeats: dragging === "start" ? dragStart : (info.startBeats !== undefined ? info.startBeats : 0)
    readonly property real loopStartBeats: dragging === "loopStart" || dragging === "loop" ? dragLoopStart : (info.loopStartBeats !== undefined ? info.loopStartBeats : 0)
    readonly property real loopLengthBeats: dragging === "loopStart" || dragging === "loopEnd" || dragging === "loop" ? dragLoopLength : (info.loopLengthBeats !== undefined ? info.loopLengthBeats : lengthBeats)
    readonly property real loopEndBeats: loopStartBeats + loopLengthBeats
    readonly property real endBeats: dragging === "end" ? dragEnd : lengthBeats
    color: Theme.surfaceCanvas
    radius: 3
    border.color: Theme.borderSubtle
    clip: true

    function xOf(beat) { return gutter + beat * pxPerBeat }
    function beatAt(x) { return Math.max(0, Math.min(lengthBeats * 4, Math.round((x - gutter) / pxPerBeat / snapBeats) * snapBeats)) }
    function refresh() {
        if (trackId === "") return
        content = project.liveCellContent(trackId, scene, Math.max(64, Math.round(width)))
        peaks.requestPaint()
    }
    // sets one of the markers; the others stay where they are
    function commitMarker(name, beat) {
        if (trackId === "") return
        const t = trackId, s = scene
        if (name === "start") project.setCellSetting(t, s, "startBeats", Math.max(0, Math.min(beat, lengthBeats)))
        else if (name === "loopStart") {
            const end = loopEndBeats
            const b = Math.max(0, Math.min(beat, end - snapBeats))
            project.setCellSetting(t, s, "loopStartBeats", b)
            project.setCellSetting(t, s, "loopLengthBeats", end - b)
        } else if (name === "loopEnd") project.setCellSetting(t, s, "loopLengthBeats", Math.max(snapBeats, Math.min(beat, lengthBeats) - loopStartBeats))
        else if (name === "end") project.setCellSetting(t, s, "lengthBeats", Math.max(snapBeats, beat))
        else if (name === "loop") project.setCellSetting(t, s, "loopStartBeats", Math.max(0, beat))
    }
    function moveLoop(direction) {
        const b = loopStartBeats + direction * loopLengthBeats
        if (b >= 0) project.setCellSetting(trackId, scene, "loopStartBeats", b)
    }
    onTrackIdChanged: refresh()
    onSceneChanged: refresh()
    onWidthChanged: refresh()
    Connections {
        target: root.project
        function onRegionPeaksReady(id) { if (root.content.cellId === id) root.refresh() }
        function onLiveLoopsChanged() { root.refresh() }
    }

    // the ruler: bars and beats
    Rectangle { x: 0; y: 0; width: root.width; height: root.rulerH; color: Theme.surfaceRaised }
    Repeater {
        model: Math.floor(root.lengthBeats / Math.max(1, root.project.barBeats)) + 1
        delegate: Item {
            required property int index
            readonly property real beat: index * root.project.barBeats
            visible: beat <= root.lengthBeats
            Rectangle { x: root.xOf(beat); y: 0; width: 1; height: root.height; color: Theme.borderSubtle }
            Text { x: root.xOf(beat) + 3; y: 3; text: index + 1; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize }
        }
    }

    // the sound
    Canvas {
        id: peaks
        visible: root.content.kind === "audio"
        x: root.gutter; y: root.rulerH; width: root.lengthBeats * root.pxPerBeat; height: root.height - root.rulerH
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const p = root.content.peaks
            if (!p || p.length === 0) return
            ctx.fillStyle = Theme.accentPrimary
            const mid = height / 2
            for (let i = 0; i < p.length; ++i) {
                const h = Math.max(1, Math.min(1, p[i]) * mid)
                ctx.fillRect(i * width / p.length, mid - h, Math.max(1, width / p.length), h * 2)
            }
        }
    }
    // the notes
    Repeater {
        model: root.content.kind !== "audio" && root.content.notes !== undefined ? root.content.notes : []
        delegate: Rectangle {
            required property var modelData
            readonly property real span: Math.max(1, root.content.high - root.content.low + 1)
            x: root.xOf(modelData.start); width: Math.max(2, modelData.length * root.pxPerBeat - 1)
            height: Math.max(3, (root.height - root.rulerH - 8) / span - 1)
            y: root.rulerH + 4 + (root.content.high - modelData.note) * (root.height - root.rulerH - 8) / span
            radius: 1; color: Qt.rgba(0.35, 0.8, 1, 0.45 + 0.55 * modelData.velocity / 127)
        }
    }
    Text {
        visible: !root.content.exists || (root.content.kind === "audio" && !(root.content.peaks && root.content.peaks.length > 0))
        anchors.centerIn: parent
        text: root.content.exists ? qsTr("Reading the sound…") : qsTr("Select a cell")
        color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize
    }

    // outside the loop the cell does not repeat
    Rectangle { x: root.xOf(root.loopEndBeats); y: root.rulerH; width: Math.max(0, root.xOf(root.endBeats) - x); height: root.height - root.rulerH; color: "#60000000"; visible: root.info.loop === true }
    Rectangle { x: root.xOf(0); y: root.rulerH; width: Math.max(0, root.xOf(root.loopStartBeats) - x); height: root.height - root.rulerH; color: "#60000000"; visible: root.info.loop === true }
    Rectangle {   // the loop area
        objectName: "loopArea"
        x: root.xOf(root.loopStartBeats); y: 0; width: Math.max(2, root.loopLengthBeats * root.pxPerBeat); height: root.rulerH
        color: root.info.loop === true ? Theme.accentPrimary : Theme.textSecondary; opacity: 0.55
    }
    Rectangle { x: root.xOf(root.loopStartBeats); y: 0; width: 2; height: root.height; color: Theme.accentPrimary }
    Rectangle { x: root.xOf(root.loopEndBeats) - 2; y: 0; width: 2; height: root.height; color: Theme.accentPrimary }
    Rectangle {   // the start marker
        objectName: "startMarker"
        x: root.xOf(root.startBeats) - 1; y: 0; width: 2; height: root.height; color: Theme.statePlay
        Rectangle { x: -4; y: 0; width: 10; height: 8; color: Theme.statePlay }
    }
    Rectangle { x: root.xOf(root.endBeats) - 1; y: 0; width: 3; height: root.height; color: Theme.textPrimary; opacity: 0.8 }   // the end of the cell

    MouseArea {
        id: area
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: true
        property real clicked: 0
        readonly property real grab: 7
        function nearest(x, y) {
            if (Math.abs(x - root.xOf(root.endBeats)) <= grab) return "end"
            if (Math.abs(x - root.xOf(root.startBeats)) <= grab && y < root.rulerH) return "start"
            if (Math.abs(x - root.xOf(root.loopStartBeats)) <= grab) return "loopStart"
            if (Math.abs(x - root.xOf(root.loopEndBeats)) <= grab) return "loopEnd"
            if (y < root.rulerH && x > root.xOf(root.loopStartBeats) && x < root.xOf(root.loopEndBeats)) return "loop"
            return ""
        }
        property real grabOffset: 0
        cursorShape: root.dragging === "loop" ? Qt.ClosedHandCursor : (nearest(mouseX, mouseY) === "loop" ? Qt.OpenHandCursor : (nearest(mouseX, mouseY) !== "" ? Qt.SizeHorCursor : Qt.ArrowCursor))
        onPressed: (mouse) => {
            if (mouse.button === Qt.RightButton || (mouse.modifiers & Qt.ControlModifier)) {
                clicked = root.beatAt(mouse.x)
                rulerMenu.popup()
                return
            }
            const n = nearest(mouse.x, mouse.y)
            if (n === "") return
            root.dragStart = root.startBeats; root.dragLoopStart = root.loopStartBeats; root.dragLoopLength = root.loopLengthBeats; root.dragEnd = root.endBeats
            grabOffset = root.beatAt(mouse.x) - root.loopStartBeats
            root.dragging = n
        }
        onPositionChanged: (mouse) => {
            if (root.dragging === "") return
            const b = root.beatAt(mouse.x)
            if (root.dragging === "start") root.dragStart = Math.min(b, root.lengthBeats)
            else if (root.dragging === "end") root.dragEnd = Math.max(root.snapBeats, b)
            else if (root.dragging === "loopStart") {
                const end = root.loopEndBeats
                root.dragLoopStart = Math.max(0, Math.min(b, end - root.snapBeats))
                root.dragLoopLength = end - root.dragLoopStart
            } else if (root.dragging === "loopEnd") root.dragLoopLength = Math.max(root.snapBeats, b - root.loopStartBeats)
            else if (root.dragging === "loop") root.dragLoopStart = Math.max(0, b - grabOffset)
        }
        onReleased: (mouse) => {
            const n = root.dragging
            if (n === "") return
            const value = n === "start" ? root.dragStart : n === "end" ? root.dragEnd : n === "loopStart" || n === "loop" ? root.dragLoopStart : root.dragLoopStart + root.dragLoopLength
            root.dragging = ""
            root.commitMarker(n, value)
        }
    }
    Menu {
        id: rulerMenu
        MenuItem { text: qsTr("Set Start Position Here"); onTriggered: root.commitMarker("start", area.clicked) }
        MenuItem { text: qsTr("Set Loop Start Here"); onTriggered: root.commitMarker("loopStart", area.clicked) }
        MenuItem { text: qsTr("Set Loop End Here"); onTriggered: root.commitMarker("loopEnd", area.clicked) }
        MenuSeparator { }
        MenuItem { text: qsTr("Move Loop Forward by Loop Length"); onTriggered: root.moveLoop(1) }
        MenuItem { text: qsTr("Move Loop Backward by Loop Length"); onTriggered: root.moveLoop(-1) }
    }
}
