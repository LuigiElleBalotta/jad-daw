import QtQuick
import Jad

Rectangle {
    id: root
    required property ProjectController project
    property real scrollY: 0
    property real rowHeight: Theme.sizeTrackHeight[project.trackHeightIndex]
    signal stubTriggered(string actionId, bool on)
    property real headerHeight: 24

    color: Theme.surfacePanel

    Rectangle {
        id: header
        width: parent.width
        height: root.headerHeight
        color: Theme.surfacePanel
        Text {
            x: Theme.spacing[4]
            y: 0
            height: 24
            verticalAlignment: Text.AlignVCenter
            text: root.project.hasProject ? root.project.projectName : qsTr("No project")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
            font.weight: Theme.fontTypeLabelWeight
            elide: Text.ElideRight
            width: parent.width - Theme.spacing[4] * 2
        }
        Repeater {  // the names of the global tracks, next to their lanes
            model: root.project.globalTracksVisible ? [qsTr("Marker"), qsTr("Tempo"), qsTr("Signature")] : []
            delegate: Text {
                required property string modelData
                required property int index
                x: Theme.spacing[4]
                y: 24 + index * 18
                height: 18
                verticalAlignment: Text.AlignVCenter
                text: modelData
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeCaptionSize
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
    }

    function beginRename(id) {
        for (let i = 0; i < list.count; ++i) {
            const item = list.itemAtIndex(i)
            if (item && item.trackId === id) { item.beginRename(); return }
        }
    }

    ListView {
        id: list
        y: root.headerHeight
        width: parent.width
        height: parent.height - root.headerHeight
        clip: true
        interactive: false
        contentY: root.scrollY
        model: root.project.tracks
        delegate: TrackHeader {
            id: row
            // the model roles; roles named like properties of TrackHeader mark those properties required
            required property int index
            required property string name
            required property string color
            required trackId
            required kind
            required mute
            required solo
            required recordArm
            required inputMonitor
            required gainDb
            required pan
            width: ListView.view.width
            height: root.rowHeight
            trackName: name
            trackColor: color
            number: index + 1
            selected: root.project.selectedTrackIds.indexOf(trackId) >= 0
            onSelectRequested: (id, mode) => root.project.selectTrack(id, mode)
            onMuteToggled: (id, on) => root.project.setMute(id, on)
            onSoloToggled: (id, on) => root.project.setSolo(id, on)
            onRenamed: (id, newName) => root.project.renameTrack(id, newName)
            onLibraryRequested: (id) => { root.project.selectTrack(id, "replace"); root.project.libraryVisible = true }
            onGestureStarted: root.project.beginGesture()
            onGainMoved: (id, db) => root.project.setGainLive(id, db)
            onPanMoved: (id, p) => root.project.setPanLive(id, p)
            onGainReleased: (id, db) => { root.project.setGain(id, db); root.project.endGesture() }
            onPanReleased: (id, p) => { root.project.setPan(id, p); root.project.endGesture() }
            onTrackToggled: (id, actionId, on) => { root.project.setTrackToggle(actionId, id, on); root.stubTriggered(actionId, on) }
        }
    }
}
