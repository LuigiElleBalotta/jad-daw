import QtQuick
import Jad

Rectangle {
    id: root
    // the role names of RegionModel
    required property string regionId
    required property string trackId
    required property int trackIndex
    required property real startBeats
    required property real lengthBeats
    required property bool isAudio
    required property bool missing
    required property string mediaId
    required property string trackColor
    property var project

    readonly property string capitalColor: trackColor.charAt(0).toUpperCase() + trackColor.slice(1)
    readonly property color solid: Theme["track" + capitalColor + "Solid"]
    readonly property color fill: Theme["track" + capitalColor + "Fill"]
    property var peaks: []

    radius: Theme.radiusRegion
    color: "transparent"
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.missing ? Theme.surfaceRaised : root.fill
        border.color: root.missing ? Theme.stateMute : root.solid
        border.width: 1
    }

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
        target: root.project
        function onWaveformReady(id) { if (id === root.mediaId) root.requestPeaks() }
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
