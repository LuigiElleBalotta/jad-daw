import QtQuick
import Jad

// What Flex draws over an audio region: the transients (thin marks along the top), the flex markers (a line with a handle that is dragged
// sideways: the sound is stretched; with Alt it only slides along the sound; a double click deletes it) and, in Flex Pitch, the notes.
Item {
    id: root
    property var project
    property string regionId
    property string mode: ""               // the flex mode of the track
    property var markers: []               // {index, frac}: where the flex markers play, 0..1 of the region
    property var notes: []                 // {index, start, end, pitch, ...}
    property var transients: []            // fractions of the region
    property color tint: Theme.textPrimary
    readonly property bool pitchMode: mode === "pitch"
    property int dragIndex: -1             // the marker being dragged, and where it is now
    property real dragFrac: 0
    signal markerMoved(int index, real fraction, bool slide)
    signal markerDeleted(int index)

    // the pitch range of the notes, for drawing them
    readonly property real lowPitch: { let v = 127; for (const n of notes) v = Math.min(v, n.pitch); return notes.length ? v - 1 : 0 }
    readonly property real highPitch: { let v = 0; for (const n of notes) v = Math.max(v, n.pitch); return notes.length ? Math.max(v + 1, lowPitch + 4) : 12 }

    Repeater {  // the transients
        model: root.pitchMode ? [] : root.transients
        delegate: Rectangle {
            required property real modelData
            x: Math.round(modelData * root.width)
            y: 14
            width: 1
            height: Math.min(12, root.height - 14)
            color: root.tint
            opacity: 0.5
        }
    }

    Repeater {  // the flex markers
        model: root.pitchMode ? [] : root.markers
        delegate: Item {
            id: marker
            required property var modelData
            readonly property real frac: root.dragIndex === modelData.index ? root.dragFrac : modelData.frac
            objectName: "flexMarker" + modelData.index
            x: Math.round(frac * root.width) - 6
            width: 12
            height: root.height
            z: 5
            Rectangle { x: 5; y: 0; width: 2; height: parent.height; color: Theme.stateSolo }
            Rectangle { x: 1; y: 0; width: 10; height: 10; radius: 2; color: Theme.stateSolo; border.color: Theme.surfaceCanvas }
            MouseArea {
                id: grab
                x: -3; y: 0; width: 18; height: Math.min(parent.height, 26)
                cursorShape: Qt.SizeHorCursor
                preventStealing: true
                acceptedButtons: Qt.LeftButton
                onPressed: { root.dragIndex = marker.modelData.index; root.dragFrac = marker.modelData.frac }
                onPositionChanged: (m) => {
                    if (!pressed) return
                    root.dragFrac = Math.max(0, Math.min(1, root.mapFromItem(grab, m.x, 0).x / Math.max(1, root.width)))
                }
                onReleased: (m) => {
                    const index = root.dragIndex
                    const frac = root.dragFrac
                    root.dragIndex = -1
                    if (Math.abs(frac - marker.modelData.frac) * root.width >= 2) root.markerMoved(index, frac, (m.modifiers & Qt.AltModifier) !== 0)
                }
                onCanceled: root.dragIndex = -1
                onDoubleClicked: root.markerDeleted(marker.modelData.index)
            }
        }
    }

    Repeater {  // Flex Pitch: the notes as bars
        model: root.pitchMode ? root.notes : []
        delegate: Rectangle {
            required property var modelData
            readonly property real noteHeight: Math.max(3, Math.min(10, root.height / (root.highPitch - root.lowPitch + 1)))
            objectName: "flexNote" + modelData.index
            x: modelData.start * root.width
            width: Math.max(2, (modelData.end - modelData.start) * root.width - 1)
            height: noteHeight
            y: 14 + (root.highPitch - modelData.pitch) / (root.highPitch - root.lowPitch) * Math.max(1, root.height - 14 - noteHeight - 2)
            radius: 2
            color: Qt.rgba(root.tint.r, root.tint.g, root.tint.b, 0.85)
            border.color: Theme.surfaceCanvas
            border.width: 1
        }
    }
}
