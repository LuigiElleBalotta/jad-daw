import QtQuick
import Jad

Item {
    id: root
    // the role names of RegionModel (plain properties so the item can also be created on its own)
    property string regionId
    property string trackId
    property int trackIndex: 0
    property real startBeats: 0
    property real lengthBeats: 1
    property bool isAudio: false
    property bool missing: false
    property string mediaId
    property string trackColor: "purple"
    property var project

    property real pixelsPerBeat: 40
    property real snapBeats: 1
    property string tool: "pointer"
    property bool selected: false
    property real dragDeltaPx: 0
    property real leftEdgePx: 0   // live feedback while an edge is dragged
    property real rightEdgePx: 0
    readonly property bool dragging: area.moving

    signal moved(string id, real beats)
    signal selectRequested(string id, bool extend)
    signal resized(string id, real startBeats, real lengthBeats)
    signal eraseRequested(string id)
    signal splitRequested(string id, real atBeats)
    signal glueRequested(string id)

    function snap(b) { return snapBeats > 0 ? Math.round(b / snapBeats) * snapBeats : b }

    // An edge was dragged by deltaPx: the other edge stays, the length never gets below one grid step (or below its own
    // length when it is already shorter than that, so an edge only ever shortens such a region).
    function finishResize(left, deltaPx) {
        const minLength = Math.min(Math.max(snapBeats, 1 / 16), lengthBeats)
        let s = startBeats, l = lengthBeats
        if (left) {
            const newStart = Math.min(snap(startBeats + deltaPx / pixelsPerBeat), startBeats + lengthBeats - minLength)
            s = Math.max(0, newStart)
            l = startBeats + lengthBeats - s
        } else {
            l = Math.max(minLength, snap(startBeats + lengthBeats + deltaPx / pixelsPerBeat) - startBeats)
        }
        resized(regionId, s, l)
    }

    readonly property string capitalColor: trackColor.charAt(0).toUpperCase() + trackColor.slice(1)
    readonly property color solid: Theme["track" + capitalColor + "Solid"]
    readonly property color fill: Theme["track" + capitalColor + "Fill"]
    property var peaks: []
    readonly property int buckets: Math.min(4096, Math.ceil(width / 2 / 32) * 32)

    function requestPeaks() {
        if (!project || !isAudio || missing || width <= 20 || !visible) return
        peaks = project.waveformPeaks(mediaId, buckets)
    }
    Timer { id: debounce; interval: 120; onTriggered: root.requestPeaks() }
    onWidthChanged: debounce.restart()
    onVisibleChanged: debounce.restart()
    onMediaIdChanged: debounce.restart()
    Component.onCompleted: requestPeaks()
    Connections {
        target: root.project ? root.project : null
        function onWaveformReady(id) { if (id === root.mediaId) root.requestPeaks() }
    }

    // the visuals follow the pointer while dragging; the item itself stays put until the model moves
    Item {
        id: content
        x: root.dragDeltaPx + root.leftEdgePx
        width: Math.max(2, root.width - root.leftEdgePx + root.rightEdgePx)
        height: root.height
        opacity: area.moving ? 0.8 : 1

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusRegion
            color: root.missing ? Theme.surfaceRaised : root.fill
            border.color: root.selected ? Theme.textPrimary : (root.missing ? Theme.stateMute : root.solid)
            border.width: root.selected ? 2 : 1
        }

        Loader {
            anchors.fill: parent
            anchors.topMargin: 14
            active: root.isAudio && !root.missing && root.peaks.length > 0
            sourceComponent: WaveformPreview { peaks: root.peaks; color: root.solid }
        }

        // missing media: diagonal hatch
        Canvas {
            anchors.fill: parent
            visible: root.missing
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = Theme.stateMute
                ctx.globalAlpha = 0.5
                ctx.lineWidth = 1
                for (let x = -height; x < width; x += 8) {
                    ctx.beginPath()
                    ctx.moveTo(x, height)
                    ctx.lineTo(x + height, 0)
                    ctx.stroke()
                }
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
        }

        Text {
            x: 4; y: 1
            width: parent.width - 8
            elide: Text.ElideRight
            text: root.missing ? qsTr("missing media") : ""
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeCaptionSize
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        preventStealing: true
        property real pressSceneX: 0
        property bool moving: false
        property bool toolAction: false  // the press did something with a tool: no select, no drag

        function sceneX(m) { return mapToItem(null, m.x, m.y).x }

        onPressed: (m) => {
            toolAction = true
            if (root.tool === "eraser") { root.eraseRequested(root.regionId); return }
            if (root.tool === "scissors") { root.splitRequested(root.regionId, root.startBeats + m.x / root.pixelsPerBeat); return }
            if (root.tool === "glue") { root.glueRequested(root.regionId); return }
            toolAction = false
            pressSceneX = sceneX(m)
            moving = false
            root.dragDeltaPx = 0
            root.selectRequested(root.regionId, (m.modifiers & Qt.ShiftModifier) !== 0)
        }
        onPositionChanged: (m) => {
            if (!pressed || toolAction) return
            const d = sceneX(m) - pressSceneX
            if (!moving && Math.abs(d) < 3) return  // a click, not a drag
            moving = true
            root.dragDeltaPx = d
        }
        onReleased: (m) => {
            if (toolAction) { toolAction = false; return }
            const wasMoving = moving
            const d = root.dragDeltaPx
            moving = false
            root.dragDeltaPx = 0
            if (wasMoving) root.moved(root.regionId, Math.max(0, root.snap(root.startBeats + d / root.pixelsPerBeat)))
        }
        onCanceled: { moving = false; root.dragDeltaPx = 0 }
    }

    // the edges resize the region (pointer tool only); they sit on top of the body
    component Edge: MouseArea {
        id: edge
        required property bool isLeft
        enabled: root.tool === "pointer"
        width: 6
        height: parent.height
        cursorShape: Qt.SizeHorCursor
        preventStealing: true
        property real pressSceneX: 0
        function sceneX(m) { return mapToItem(null, m.x, m.y).x }
        onPressed: (m) => { pressSceneX = sceneX(m) }
        onPositionChanged: (m) => {
            if (!pressed) return
            const d = sceneX(m) - pressSceneX
            if (isLeft) root.leftEdgePx = d
            else root.rightEdgePx = d
        }
        onReleased: {
            const d = isLeft ? root.leftEdgePx : root.rightEdgePx
            root.leftEdgePx = 0
            root.rightEdgePx = 0
            if (Math.abs(d) >= 3) root.finishResize(isLeft, d)  // a smaller movement is a click
        }
        onCanceled: { root.leftEdgePx = 0; root.rightEdgePx = 0 }
    }
    Edge { isLeft: true; anchors.left: parent.left }
    Edge { isLeft: false; anchors.right: parent.right }
}
