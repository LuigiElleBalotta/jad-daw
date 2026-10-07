import QtQuick
import Jad

Rectangle {
    id: root
    required property real pixelsPerBeat
    required property real scrollBeats
    required property int beatsPerBar

    color: Theme.surfacePanel
    clip: true

    readonly property real barWidth: pixelsPerBeat * beatsPerBar
    // label only every N bars when they get too dense
    readonly property int step: Math.max(1, Math.ceil(48 / barWidth))
    readonly property int firstBar: Math.floor(scrollBeats / beatsPerBar / step) * step
    readonly property int barCount: Math.ceil(width / barWidth / step) + 2

    Repeater {
        model: root.barCount
        delegate: Item {
            required property int index
            readonly property int bar: root.firstBar + index * root.step
            x: (bar * root.beatsPerBar - root.scrollBeats) * root.pixelsPerBeat
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
}
