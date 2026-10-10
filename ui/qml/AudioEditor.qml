import QtQuick
import Jad

// The audio editors of the Editors area: the Track tab shows the selected audio region (its own part of the file), the File tab shows the
// whole file with the region marked. Drag in the waveform to select a range; the buttons work on the selected regions.
Rectangle {
    id: root
    required property ProjectController project
    property string regionId: ""
    property bool fileMode: false
    signal processRequested(string op)            // the dialogs of Normalize, Gain, Time Stretch, Pitch Shift, Strip Silence

    readonly property var info: { project.revision; return regionId !== "" ? project.regionInfo(regionId) : ({ found: false }) }
    readonly property bool valid: info.found === true && info.audio === true
    readonly property real rate: project.sampleRateHz
    // the frames shown: the region's part, or the whole file
    readonly property real shownFrom: valid ? (fileMode ? 0 : info.sourceOffsetFrames) : 0
    readonly property real shownFrames: valid ? (fileMode ? Math.max(1, info.mediaFrames) : Math.max(1, info.lengthFrames)) : 1
    property real zoom: 1
    property var peaks: []
    property real selFrom: -1                      // the selected range, in frames from the start of the shown part (-1: none)
    property real selTo: -1
    readonly property bool hasSelection: selFrom >= 0 && selTo > selFrom

    color: Theme.surfaceCanvas
    clip: true

    function seconds(frames) { return (frames / rate).toFixed(2) + " s" }
    function refreshPeaks() {
        if (!valid) { peaks = []; return }
        const buckets = Math.min(4096, Math.max(64, Math.ceil(wave.width / 2)))
        const fresh = fileMode ? project.waveformPeaks(info.mediaId, buckets) : project.regionPeaks(regionId, buckets)
        if (fresh.length > 0) peaks = fresh
    }
    onRegionIdChanged: { selFrom = -1; selTo = -1; peaks = []; refresh.restart() }
    onFileModeChanged: { selFrom = -1; selTo = -1; peaks = []; refresh.restart() }
    onZoomChanged: refresh.restart()
    onInfoChanged: refresh.restart()
    Timer { id: refresh; interval: 80; onTriggered: root.refreshPeaks() }
    Connections {
        target: root.project
        function onRegionPeaksReady(id) { if (id === root.regionId) root.refreshPeaks() }
        function onWaveformReady(id) { if (root.valid && id === root.info.mediaId) root.refreshPeaks() }
    }

    Text {
        anchors.centerIn: parent
        visible: !root.valid
        text: qsTr("Select an audio region")
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeBodySize
    }

    Column {
        anchors.fill: parent
        visible: root.valid
        Rectangle {  // the functions
            width: parent.width
            height: 30
            color: Theme.surfacePanel
            Row {
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.spacing[2]
                spacing: Theme.spacing[1]
                Repeater {
                    model: [{ id: "normalize", label: qsTr("Normalize") }, { id: "reverse", label: qsTr("Reverse") }, { id: "gain", label: qsTr("Gain…") },
                            { id: "stretch", label: qsTr("Stretch…") }, { id: "pitch", label: qsTr("Pitch…") }, { id: "strip", label: qsTr("Strip Silence…") }]
                    delegate: IconButton {
                        required property var modelData
                        implicitHeight: 22
                        label: modelData.label
                        onClicked: root.processRequested(modelData.id)
                    }
                }
                IconButton {
                    objectName: "trimButton"
                    visible: !root.fileMode
                    implicitHeight: 22
                    label: qsTr("Trim to Selection")
                    enabled: root.hasSelection
                    onClicked: { root.project.trimRegionToFrames(root.regionId, root.selFrom, root.selTo); root.selFrom = -1; root.selTo = -1 }
                }
                Item { width: Theme.spacing[3]; height: 1 }
                IconButton { implicitHeight: 22; implicitWidth: 24; label: "−"; onClicked: root.zoom = Math.max(1, root.zoom / 2) }
                IconButton { implicitHeight: 22; implicitWidth: 24; label: "+"; onClicked: root.zoom = Math.min(8, root.zoom * 2) }
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacing[3]
                anchors.verticalCenter: parent.verticalCenter
                text: root.hasSelection ? qsTr("Selection %1 - %2 (%3)").arg(root.seconds(root.selFrom)).arg(root.seconds(root.selTo)).arg(root.seconds(root.selTo - root.selFrom))
                                        : qsTr("%1 on %2, %3").arg(root.seconds(root.shownFrames)).arg(root.info.trackName ?? "").arg(root.fileMode ? qsTr("the whole file") : qsTr("the region"))
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeLabelSize
            }
        }
        Flickable {
            id: flick
            width: parent.width
            height: parent.height - 30
            contentWidth: width * root.zoom
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            interactive: false      // a drag selects; the wheel and the zoom buttons scroll
            Item {
                width: flick.contentWidth
                height: flick.height
                Rectangle {  // the time ruler
                    id: ruler
                    width: parent.width
                    height: 20
                    color: Theme.surfacePanel
                    Repeater {
                        model: Math.max(2, Math.floor(parent.width / 80))
                        delegate: Item {
                            required property int index
                            x: index * 80
                            Rectangle { width: 1; height: ruler.height; color: Theme.borderStrong }
                            Text {
                                x: 4
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.seconds(index * 80 / flick.contentWidth * root.shownFrames)
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontTypeCaptionSize
                            }
                        }
                    }
                }
                Canvas {
                    id: wave
                    y: ruler.height
                    width: parent.width
                    height: parent.height - ruler.height
                    onWidthChanged: { requestPaint(); refresh.restart() }
                    onHeightChanged: requestPaint()
                    Connections { target: root; function onPeaksChanged() { wave.requestPaint() } }
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.fillStyle = Theme.surfaceCanvas
                        ctx.fillRect(0, 0, width, height)
                        ctx.fillStyle = Theme.borderSubtle
                        ctx.fillRect(0, height / 2, width, 1)           // the zero line
                        const n = root.peaks.length
                        if (n === 0) return
                        const mid = height / 2
                        const barW = width / n
                        ctx.fillStyle = Theme.accentPrimary
                        for (let i = 0; i < n; ++i) {
                            const h = Math.max(1, root.peaks[i] * (height - 8))
                            ctx.fillRect(i * barW, mid - h / 2, Math.max(1, barW - 0.3), h)
                        }
                        if (root.fileMode && root.valid) {  // the region's part of the file
                            const x0 = root.info.sourceOffsetFrames / root.shownFrames * width
                            const x1 = (root.info.sourceOffsetFrames + root.info.lengthFrames) / root.shownFrames * width
                            ctx.fillStyle = "rgba(0,0,0,0.45)"
                            ctx.fillRect(0, 0, x0, height)
                            ctx.fillRect(x1, 0, width - x1, height)
                        }
                    }
                    Rectangle {  // the selection
                        visible: root.hasSelection
                        x: root.selFrom / root.shownFrames * parent.width
                        width: (root.selTo - root.selFrom) / root.shownFrames * parent.width
                        height: parent.height
                        color: Qt.rgba(Theme.textPrimary.r, Theme.textPrimary.g, Theme.textPrimary.b, 0.18)
                        border.color: Theme.textPrimary
                    }
                    MouseArea {
                        anchors.fill: parent
                        property real anchorFrame: 0
                        function frameAt(x) { return Math.max(0, Math.min(root.shownFrames, x / width * root.shownFrames)) }
                        onPressed: (m) => { anchorFrame = frameAt(m.x); root.selFrom = -1; root.selTo = -1 }
                        onPositionChanged: (m) => {
                            if (!pressed) return
                            const f = frameAt(m.x)
                            root.selFrom = Math.min(anchorFrame, f)
                            root.selTo = Math.max(anchorFrame, f)
                        }
                        onWheel: (w) => { flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, flick.contentX - w.angleDelta.y - w.angleDelta.x)) }
                    }
                }
            }
        }
    }
}
