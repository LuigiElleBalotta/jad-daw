import QtQuick
import Jad

Item {
    id: root
    required property ProjectController project
    property real pixelsPerBeat: 40
    property real scrollBeats: 0
    property real scrollY: 0
    readonly property real minPixelsPerBeat: 4
    readonly property real maxPixelsPerBeat: 400
    readonly property real rowHeight: Theme.sizeTrackHeight[1]
    readonly property real rulerHeight: 24
    readonly property real contentHeight: project.tracks.rowCount() * rowHeight

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

    Ruler {
        id: ruler
        width: parent.width
        height: root.rulerHeight
        pixelsPerBeat: root.pixelsPerBeat
        scrollBeats: root.scrollBeats
        beatsPerBar: root.project.beatsPerBar
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

        Repeater {
            model: root.project.regions
            delegate: RegionItem {
                project: root.project
                x: root.beatsToX(startBeats)
                y: trackIndex * root.rowHeight - root.scrollY + 2
                width: lengthBeats * root.pixelsPerBeat
                height: root.rowHeight - 4
                // only what is on screen is drawn
                visible: x + width > 0 && x < body.width
            }
        }

        Rectangle {
            id: playhead
            x: root.beatsToX(root.project.positionBeats)
            width: Math.max(1, Theme.sizePlayheadWidth)
            height: parent.height
            color: Theme.playhead
            visible: x >= 0 && x <= body.width
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
