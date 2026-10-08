import QtQuick
import Jad

// The Inspector: Region and Track sections above the channel strips of the selected track and its output. All of it
// scrolls when the column is short; when there is room the strips sit at the bottom.
Rectangle {
    id: root
    required property ProjectController project
    readonly property var insp: project.inspector
    readonly property alias emptyLabel: empty
    readonly property alias regionSection: region
    readonly property alias trackSection: trackSec
    readonly property alias trackStrip: trackStrip
    readonly property alias outputStrip: outputStrip
    readonly property real stripsHeight: insp.hasTrack ? Math.min(480, Math.max(300, root.height * 0.5)) : 0
    color: Theme.surfacePanel
    clip: true

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: Math.max(col.height + root.stripsHeight, height)
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Column {
            id: col
            width: parent.width
            Text {
                id: empty
                visible: !root.insp.hasTrack && !root.insp.hasRegion
                width: parent.width
                height: 80
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: qsTr("No track selected")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeBodySize
            }
            RegionInspector { id: region; width: parent.width; visible: root.insp.hasRegion; project: root.project; region: root.insp.region }
            TrackInspector { id: trackSec; width: parent.width; visible: root.insp.hasTrack; project: root.project; track: root.insp.track }
        }
        Row {
            y: flick.contentHeight - root.stripsHeight
            width: parent.width
            height: root.stripsHeight
            visible: root.insp.hasTrack
            padding: Theme.spacing[2]
            spacing: Theme.spacing[2]
            ProjectStrip {
                id: trackStrip
                width: (parent.width - Theme.spacing[2] * 3) / 2
                height: parent.height - Theme.spacing[2] * 2
                project: root.project
                info: root.insp.track
            }
            ProjectStrip {
                id: outputStrip
                visible: root.insp.hasTrack && Object.keys(root.insp.output).length > 0
                width: (parent.width - Theme.spacing[2] * 3) / 2
                height: parent.height - Theme.spacing[2] * 2
                project: root.project
                info: root.insp.output
                showSlots: false
                peak: info.master ? root.project.masterPeak : 0
            }
        }
    }
}
