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
    property real barBeats: 4
    property string trackName
    property string tool: "pointer"
    property bool selected: false
    property bool muted: false
    property real fadeInBeats: 0           // the fades of an audio region
    property real fadeOutBeats: 0
    property real fadeInPx: -1             // live feedback while a fade handle is dragged (-1: not dragged)
    property real fadeOutPx: -1
    property string regionName
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
    signal muteRequested(string id)
    signal fadesRequested(string id, real fadeInBeats, real fadeOutBeats)  // a fade handle was dragged
    signal editRequested(string id)  // a double click opens the region in the editor (Piano Roll)

    // "bar beat" of a position in beats, or the length as "bars beats"
    function barBeat(b, length) {
        const bars = Math.floor(b / barBeats + 1e-9)
        const beats = Math.floor(b - bars * barBeats + 1e-9)
        return length ? bars + " " + beats : (bars + 1) + " " + (beats + 1)
    }
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
        const fresh = project.regionPeaks(regionId, buckets)
        if (fresh.length > 0) peaks = fresh   // until the new stretch is read the old picture stays
    }
    Timer { id: debounce; interval: 120; onTriggered: root.requestPeaks() }
    onWidthChanged: debounce.restart()
    onVisibleChanged: debounce.restart()
    onMediaIdChanged: debounce.restart()
    onLengthBeatsChanged: debounce.restart()   // trimmed: another stretch of the media
    Component.onCompleted: requestPeaks()
    Connections {
        target: root.project ? root.project : null
        function onRegionPeaksReady(id) { if (id === root.regionId) root.requestPeaks() }
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
            color: (root.missing || root.muted) ? Theme.surfaceRaised : root.fill  // a muted region is grey
            border.color: root.selected ? Theme.textPrimary : ((root.missing || root.muted) ? Theme.stateMute : root.solid)
            border.width: root.selected ? 2 : 1
        }

        Loader {
            anchors.fill: parent
            anchors.topMargin: 14
            active: root.isAudio && !root.missing && root.peaks.length > 0
            sourceComponent: WaveformPreview { peaks: root.peaks; color: root.solid; zoom: root.project ? root.project.waveformZoom : 1 }
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

        Canvas {  // the fades: the part of the waveform that is faded away is shaded
            id: fadeShape
            anchors.fill: parent
            visible: root.isAudio && (root.fadeInBeats > 0 || root.fadeOutBeats > 0 || root.fadeInPx >= 0 || root.fadeOutPx >= 0)
            readonly property real fin: root.fadeInPx >= 0 ? root.fadeInPx : root.fadeInBeats * root.pixelsPerBeat
            readonly property real fout: root.fadeOutPx >= 0 ? root.fadeOutPx : root.fadeOutBeats * root.pixelsPerBeat
            onFinChanged: requestPaint()
            onFoutChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = "rgba(0,0,0,0.35)"
                if (fin > 0) { ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(Math.min(fin, width), 0); ctx.lineTo(0, height); ctx.closePath(); ctx.fill() }
                if (fout > 0) { ctx.beginPath(); ctx.moveTo(width, 0); ctx.lineTo(Math.max(0, width - fout), 0); ctx.lineTo(width, height); ctx.closePath(); ctx.fill() }
            }
        }

        Rectangle {  // the help tag while the region is dragged (Logic's "Move Region")
            visible: area.moving
            y: -tagText.implicitHeight - 10
            width: tagText.implicitWidth + 12
            height: tagText.implicitHeight + 6
            radius: 3
            color: Theme.surfaceLcd
            border.color: Theme.borderStrong
            z: 20
            Text {
                id: tagText
                anchors.centerIn: parent
                text: qsTr("Move Region
Position %1
Length %2
Track: %3")
                          .arg(root.barBeat(Math.max(0, root.snap(root.startBeats + root.dragDeltaPx / root.pixelsPerBeat))))
                          .arg(root.barBeat(root.lengthBeats, true)).arg(root.trackName)
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeCaptionSize
            }
        }
        Text {
            x: 4; y: 1
            width: parent.width - 8
            elide: Text.ElideRight
            text: root.missing ? qsTr("missing media") : (root.muted ? "\u2022 " : "")  // a dot before the name of a muted region
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
            if (root.tool === "mute") { root.muteRequested(root.regionId); return }
            if (root.tool === "zoom") return  // the Zoom tool acts on the lane (Timeline), not on a region
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
        onDoubleClicked: { if (root.tool === "pointer" || root.tool === "pencil") root.editRequested(root.regionId) }
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

    // the fade handles at the top corners of a selected audio region (pointer tool): drag sideways to set the fade
    component FadeHandle: Rectangle {
        id: handle
        required property bool isIn
        visible: root.isAudio && !root.missing && root.tool === "pointer" && (root.selected || hover.hovered || handleArea.pressed) && root.width > 40
        width: 9; height: 9; radius: 2
        color: Theme.textPrimary
        border.color: Theme.surfaceCanvas
        x: isIn ? Math.min(root.width - 9, (root.fadeInPx >= 0 ? root.fadeInPx : root.fadeInBeats * root.pixelsPerBeat)) - (isIn ? 0 : 0)
                : Math.max(0, root.width - 9 - (root.fadeOutPx >= 0 ? root.fadeOutPx : root.fadeOutBeats * root.pixelsPerBeat))
        y: 1
        z: 15
        HoverHandler { id: hover }
        MouseArea {
            id: handleArea
            anchors.fill: parent
            anchors.margins: -3
            cursorShape: Qt.SizeHorCursor
            preventStealing: true
            property real pressX: 0
            property real startPx: 0
            onPressed: (m) => {
                pressX = mapToItem(root, m.x, m.y).x
                startPx = handle.isIn ? root.fadeInBeats * root.pixelsPerBeat : root.fadeOutBeats * root.pixelsPerBeat
                if (handle.isIn) root.fadeInPx = startPx; else root.fadeOutPx = startPx
            }
            onPositionChanged: (m) => {
                if (!pressed) return
                const d = mapToItem(root, m.x, m.y).x - pressX
                const px = Math.max(0, Math.min(root.width, startPx + (handle.isIn ? d : -d)))
                if (handle.isIn) root.fadeInPx = px; else root.fadeOutPx = px
            }
            onReleased: {
                const fin = root.fadeInPx >= 0 ? root.fadeInPx / root.pixelsPerBeat : root.fadeInBeats
                const fout = root.fadeOutPx >= 0 ? root.fadeOutPx / root.pixelsPerBeat : root.fadeOutBeats
                root.fadeInPx = -1
                root.fadeOutPx = -1
                root.fadesRequested(root.regionId, fin, fout)
            }
            onCanceled: { root.fadeInPx = -1; root.fadeOutPx = -1 }
        }
    }
    FadeHandle { isIn: true }
    FadeHandle { isIn: false }
}
