import QtQuick
import Jad

Item {
    id: root
    required property ProjectController project
    property real pixelsPerBeat: 40
    property real scrollBeats: 0
    property real scrollY: 0
    readonly property real snapBeats: project.snapBeats   // 0 disables snapping
    readonly property real minPixelsPerBeat: 4
    readonly property real maxPixelsPerBeat: 400
    readonly property real rowHeight: Theme.sizeTrackHeight[project.trackHeightIndex]
    readonly property real rulerHeight: 24
    readonly property real contentHeight: project.tracks.rowCount() * rowHeight

    signal regionMoved(string id, real beats)

    focus: true
    activeFocusOnTab: true

    function beatsToX(b) { return (b - scrollBeats) * pixelsPerBeat }
    function xToBeats(x) { return x / pixelsPerBeat + scrollBeats }

    // keeps the beat under anchorX where it is
    function zoomBy(f, anchorX) {
        const ax = anchorX === undefined ? 0 : anchorX
        const beat = xToBeats(ax)
        pixelsPerBeat = Math.max(minPixelsPerBeat, Math.min(maxPixelsPerBeat, pixelsPerBeat * f))
        scrollBeats = Math.max(0, beat - ax / pixelsPerBeat)
    }
    function scrollByBeats(d) { scrollBeats = Math.max(0, scrollBeats + d) }
    function scrollByPixelsY(d) {
        const maxY = Math.max(0, contentHeight - (height - rulerHeight))
        scrollY = Math.max(0, Math.min(maxY, scrollY + d))
    }

    function deleteSelected() {
        const ids = project.selectedRegionIds
        if (ids.length === 0) return
        project.deleteRegions(ids)
    }
    function trackIdAt(y) {
        return project.tracks.trackIdAt(Math.floor((y - rulerHeight + scrollY) / rowHeight))
    }
    function snapBeat(b) { return snapBeats > 0 ? Math.round(b / snapBeats) * snapBeats : b }

    onRegionMoved: (id, beats) => project.moveSelectedRegions(id, beats)

    // keep the playhead in view while playing: when it leaves the screen the view jumps to put it near the left edge
    Connections {
        target: root.project
        function onPositionChanged() {
            if (!root.project.followPlayhead || !root.project.playing) return
            const x = root.beatsToX(root.project.positionBeats)
            if (x < 0 || x > root.width) root.scrollBeats = Math.max(0, root.project.positionBeats - 0.1 * root.width / root.pixelsPerBeat)
        }
    }

    Keys.onDeletePressed: deleteSelected()
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Backspace) { deleteSelected(); event.accepted = true }
    }

    Ruler {
        id: ruler
        width: parent.width
        height: root.rulerHeight
        pixelsPerBeat: root.pixelsPerBeat
        scrollBeats: root.scrollBeats
        barBeats: root.project.barBeats
        playheadBeats: root.project.positionBeats
        soloActive: root.project.anySolo
        onLocateRequested: (beats) => { root.forceActiveFocus(); root.project.locateBeats(Math.max(0, root.snapBeat(beats))) }
    }

    Item {
        id: body
        y: root.rulerHeight
        width: parent.width
        height: parent.height - root.rulerHeight
        clip: true

        // row backgrounds
        Repeater {
            model: root.project.tracks
            delegate: Rectangle {
                required property int index
                y: index * root.rowHeight - root.scrollY
                width: body.width
                height: root.rowHeight
                color: index % 2 === 0 ? Theme.surfaceCanvas : Theme.surfaceApp
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
            }
        }

        // empty space: clears the selection and takes the keyboard focus; with the pencil it draws a MIDI region
        MouseArea {
            id: emptyArea
            anchors.fill: parent
            property bool pencil: false
            property real startBeats: 0
            property real endBeats: 0
            property int row: 0
            property string trackId
            property bool band: false  // a rectangle selection is being dragged
            property bool zooming: false  // the same rectangle with the Zoom tool
            property real bx0: 0
            property real by0: 0
            property real bx1: 0
            property real by1: 0
            onPressed: (m) => {
                root.forceActiveFocus()
                if (!(m.modifiers & Qt.ShiftModifier)) root.project.clearSelection()
                pencil = root.project.tool === "pencil"
                if (!pencil) {
                    zooming = root.project.tool === "zoom"
                    band = root.project.tool === "pointer" || zooming
                    bx0 = bx1 = m.x
                    by0 = by1 = m.y
                    return
                }
                trackId = root.trackIdAt(m.y + root.rulerHeight)
                row = Math.floor((m.y + root.scrollY) / root.rowHeight)
                startBeats = Math.max(0, root.snapBeat(root.xToBeats(m.x)))
                endBeats = startBeats
            }
            onPositionChanged: (m) => {
                if (pencil && pressed) endBeats = Math.max(0, root.snapBeat(root.xToBeats(m.x)))
                if (band && pressed) { bx1 = m.x; by1 = m.y }
            }
            onReleased: (m) => {
                if (band) {
                    band = false
                    if (zooming) {  // a drag zooms to the rectangle, a click zooms in (Option-click: out)
                        zooming = false
                        if (Math.abs(bx1 - bx0) > 6) {
                            const b0 = root.xToBeats(Math.min(bx0, bx1)), b1 = root.xToBeats(Math.max(bx0, bx1))
                            root.pixelsPerBeat = Math.max(root.minPixelsPerBeat, Math.min(root.maxPixelsPerBeat, root.width / (b1 - b0)))
                            root.scrollBeats = Math.max(0, b0)
                        } else {
                            root.zoomBy((m.modifiers & Qt.AltModifier) ? 0.5 : 2, m.x)
                        }
                        return
                    }
                    if (Math.abs(bx1 - bx0) > 3 || Math.abs(by1 - by0) > 3) {
                        const row = (y) => Math.floor((y + root.scrollY) / root.rowHeight)
                        root.project.selectRegionsIn(root.xToBeats(Math.min(bx0, bx1)), root.xToBeats(Math.max(bx0, bx1)),
                                                     row(Math.min(by0, by1)), row(Math.max(by0, by1)),
                                                     (m.modifiers & Qt.ShiftModifier) ? "extend" : "replace")
                    }
                    return
                }
                if (!pencil) return
                pencil = false
                if (trackId === "") return
                let length = Math.abs(endBeats - startBeats)
                if (length < 1 / 16) length = root.project.barBeats  // a click draws one bar
                root.project.createRegion(trackId, Math.min(startBeats, endBeats), length)
            }
            onCanceled: { pencil = false; band = false; zooming = false }
        }
        Rectangle {  // the rectangle selection being dragged
            visible: emptyArea.band
            x: Math.min(emptyArea.bx0, emptyArea.bx1)
            y: Math.min(emptyArea.by0, emptyArea.by1)
            width: Math.abs(emptyArea.bx1 - emptyArea.bx0)
            height: Math.abs(emptyArea.by1 - emptyArea.by0)
            color: Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.2)
            border.color: Theme.accentPrimary
        }
        Rectangle {  // the region being drawn
            visible: emptyArea.pencil
            x: root.beatsToX(Math.min(emptyArea.startBeats, emptyArea.endBeats))
            y: emptyArea.row * root.rowHeight - root.scrollY + 2
            width: Math.max(2, Math.abs(emptyArea.endBeats - emptyArea.startBeats) * root.pixelsPerBeat)
            height: root.rowHeight - 4
            radius: Theme.radiusRegion
            color: Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, 0.3)
            border.color: Theme.accentPrimary
        }

        Repeater {
            model: root.project.regions
            delegate: RegionItem {
                id: region
                required property var model
                project: root.project
                regionId: model.regionId
                trackId: model.trackId
                trackIndex: model.trackIndex
                startBeats: model.startBeats
                lengthBeats: model.lengthBeats
                isAudio: model.isAudio
                missing: model.missing
                mediaId: model.mediaId
                trackColor: model.trackColor
                pixelsPerBeat: root.pixelsPerBeat
                snapBeats: root.snapBeats
                barBeats: root.project.barBeats
                trackName: root.project.tracks.nameAt(trackIndex)
                tool: root.project.tool
                selected: root.project.selectedRegionIds.indexOf(model.regionId) >= 0
                muted: model.muted
                x: root.beatsToX(startBeats)
                y: trackIndex * root.rowHeight - root.scrollY + 2
                width: lengthBeats * root.pixelsPerBeat
                height: root.rowHeight - 4
                // only what is on screen is drawn
                visible: x + width > 0 && x < body.width
                onSelectRequested: (id, extend) => { root.forceActiveFocus(); root.project.selectRegion(id, extend ? "extend" : "replace") }
                onMoved: (id, beats) => root.regionMoved(id, beats)
                onResized: (id, s, l) => root.project.resizeSelectedRegions(id, s, l)
                onEraseRequested: (id) => root.project.deleteRegions([id])
                onSplitRequested: (id, atBeats) => root.project.splitRegion(id, atBeats)
                onGlueRequested: (id) => root.project.joinWithNext(id)
                onMuteRequested: (id) => { root.project.selectRegion(id, "replace"); root.project.toggleMuteSelectedRegions() }
            }
        }

        Rectangle {
            id: playhead
            x: root.beatsToX(root.project.positionBeats)
            width: Math.max(1, Theme.sizePlayheadWidth)
            height: parent.height
            color: root.project.anySolo ? Theme.stateSolo : Theme.playhead  // a yellow playhead while a solo is active
            visible: x >= 0 && x <= body.width
        }
    }

    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        function wavs(urls) {
            const out = []
            for (const u of urls) if (u.toString().toLowerCase().endsWith(".wav")) out.push(u)
            return out
        }
        onEntered: (drag) => { drag.accepted = drag.hasUrls && wavs(drag.urls).length > 0 }
        onDropped: (drop) => {
            const files = wavs(drop.urls)
            if (files.length === 0) return
            const beats = Math.max(0, root.snapBeat(root.xToBeats(drop.x)))
            // several files go on the same track, back to back
            root.project.importAudioFiles(files, root.trackIdAt(drop.y), beats)
        }
    }

    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (event) => {
            const d = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
            if (event.modifiers & Qt.ControlModifier)
                root.zoomBy(d > 0 ? 1.15 : 1 / 1.15, point.position.x)
            else if (event.modifiers & Qt.ShiftModifier)
                root.scrollByBeats(-d / 120 * 2)
            else
                root.scrollByPixelsY(-d / 120 * 30)
        }
    }
}
