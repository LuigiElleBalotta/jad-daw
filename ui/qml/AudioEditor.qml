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
    readonly property real rate: project ? project.sampleRateHz : 48000
    // the frames shown: the region's part, or the whole file
    readonly property real shownFrom: valid ? (fileMode ? 0 : info.sourceOffsetFrames) : 0
    readonly property real shownFrames: valid ? (fileMode ? Math.max(1, info.mediaFrames) : Math.max(1, info.lengthFrames)) : 1
    property real zoom: 1
    // Flex Pitch: the notes of the region are drawn over the waveform, in the pitch range they span
    readonly property bool pitchMode: valid && !fileMode && info.flexMode === "pitch"
    property int flexSelected: -1
    property int flexRefresh: 0
    readonly property var flexNotes: { project.revision; flexRefresh; return pitchMode ? project.flexPitchNotes(regionId) : [] }
    readonly property real lowPitch: { let v = 127; for (const n of flexNotes) v = Math.min(v, n.pitch, n.original); return flexNotes.length ? Math.floor(v) - 3 : 48 }
    readonly property real highPitch: { let v = 0; for (const n of flexNotes) v = Math.max(v, n.pitch, n.original); return flexNotes.length ? Math.max(Math.ceil(v) + 3, lowPitch + 12) : 72 }
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
    onRegionIdChanged: { selFrom = -1; selTo = -1; peaks = []; flexSelected = -1; refresh.restart() }
    onFileModeChanged: { selFrom = -1; selTo = -1; peaks = []; refresh.restart() }
    onZoomChanged: refresh.restart()
    onInfoChanged: refresh.restart()
    Timer { id: refresh; interval: 80; onTriggered: root.refreshPeaks() }
    Connections {
        target: root.project
        function onRegionPeaksReady(id) { if (id === root.regionId) root.refreshPeaks() }
        function onWaveformReady(id) { if (root.valid && id === root.info.mediaId) root.refreshPeaks() }
        function onFlexAnalysisReady(id) { if (id === root.regionId) root.flexRefresh++ }
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
            height: parent.height - 30 - (root.pitchMode ? flexStrip.height : 0)
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
                Item {  // Flex Pitch: a row for every semitone, and the notes
                    id: notesLayer
                    objectName: "flexNotesLayer"
                    y: ruler.height
                    width: parent.width
                    height: parent.height - ruler.height
                    visible: root.pitchMode
                    readonly property real rowHeight: height / (root.highPitch - root.lowPitch)
                    function yOf(pitch) { return (root.highPitch - pitch) * rowHeight }
                    Repeater {  // the C lines, with their names
                        model: root.pitchMode ? Math.ceil(root.highPitch - root.lowPitch) : 0
                        delegate: Item {
                            required property int index
                            readonly property int midi: Math.floor(root.highPitch) - index
                            visible: midi % 12 === 0 || notesLayer.rowHeight >= 10
                            y: notesLayer.yOf(midi + 0.5)
                            width: notesLayer.width
                            Rectangle { width: parent.width; height: 1; color: midi % 12 === 0 ? Theme.borderStrong : Theme.borderSubtle }
                            Text {
                                x: 3; y: 1
                                visible: midi % 12 === 0
                                text: "C" + (Math.floor(midi / 12) - 1)
                                color: Theme.textSecondary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontTypeCaptionSize
                            }
                        }
                    }
                    Repeater {
                        model: root.pitchMode ? root.flexNotes : []
                        delegate: Rectangle {
                            id: noteBar
                            required property var modelData
                            objectName: "flexNote" + modelData.index
                            readonly property bool selectedNote: root.flexSelected === modelData.index
                            property real liveSemitones: NaN     // while it is dragged: the pitch it would get
                            property real liveShift: 0           // while it is dragged sideways, in fractions of the region
                            readonly property real shownPitch: isNaN(liveSemitones) ? modelData.pitch : modelData.original + liveSemitones + modelData.fineCents / 100
                            x: (modelData.start + liveShift) * notesLayer.width
                            width: Math.max(4, (modelData.end - modelData.start) * notesLayer.width)
                            height: Math.max(6, notesLayer.rowHeight - 1)
                            y: notesLayer.yOf(shownPitch + 0.5)
                            radius: 3
                            color: selectedNote ? Theme.textValue : Theme.accentPrimary
                            opacity: 0.9
                            border.color: Theme.textPrimary
                            border.width: selectedNote ? 2 : 1
                            Text {
                                x: 4
                                anchors.verticalCenter: parent.verticalCenter
                                visible: parent.width > 30 && parent.height > 11
                                text: ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][((Math.round(noteBar.shownPitch) % 12) + 12) % 12] + (Math.floor(Math.round(noteBar.shownPitch) / 12) - 1)
                                color: Theme.surfaceCanvas
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontTypeCaptionSize
                            }
                            MouseArea {
                                id: bodyArea
                                anchors.fill: parent
                                anchors.leftMargin: 5
                                anchors.rightMargin: 5
                                preventStealing: true
                                cursorShape: Qt.SizeAllCursor
                                property real pressX: 0
                                property real pressY: 0
                                property string axis: ""
                                onPressed: (m) => {
                                    root.flexSelected = noteBar.modelData.index
                                    if (root.project.tool === "scissors") { root.project.flexNoteSplit(root.regionId, noteBar.modelData.index, notesLayer.mapFromItem(bodyArea, m.x, 0).x / notesLayer.width); axis = "cut"; return }
                                    if (root.project.tool === "glue") { root.project.flexNoteJoin(root.regionId, noteBar.modelData.index); axis = "cut"; return }
                                    pressX = m.x; pressY = m.y; axis = ""
                                    root.project.flexNotePreview(root.regionId, noteBar.modelData.index, {})   // holding a note plays it
                                }
                                onPositionChanged: (m) => {
                                    if (!pressed || axis === "cut") return
                                    const dx = m.x - pressX, dy = m.y - pressY
                                    if (axis === "" && Math.abs(dx) < 4 && Math.abs(dy) < 4) return
                                    if (axis === "") axis = Math.abs(dy) >= Math.abs(dx) ? "pitch" : "time"
                                    if (axis === "pitch") {
                                        const target = noteBar.modelData.original - dy / notesLayer.rowHeight + noteBar.modelData.semitones + noteBar.modelData.fineCents / 100
                                        const snapped = (m.modifiers & Qt.ShiftModifier) ? target : Math.round(target)   // a note lands on a semitone; Shift is free
                                        noteBar.liveSemitones = snapped - noteBar.modelData.original - noteBar.modelData.fineCents / 100
                                        root.project.flexNotePreview(root.regionId, noteBar.modelData.index, { semitones: noteBar.liveSemitones })
                                    } else {
                                        noteBar.liveShift = dx / notesLayer.width
                                    }
                                }
                                onReleased: {
                                    const i = noteBar.modelData.index
                                    if (axis === "pitch" && !isNaN(noteBar.liveSemitones)) root.project.flexNoteEdit(root.regionId, i, { semitones: noteBar.liveSemitones })
                                    else if (axis === "time" && Math.abs(noteBar.liveShift) * notesLayer.width >= 2)
                                        root.project.flexNoteTime(root.regionId, i, noteBar.modelData.start + noteBar.liveShift, noteBar.modelData.end + noteBar.liveShift)
                                    noteBar.liveSemitones = NaN
                                    noteBar.liveShift = 0
                                    axis = ""
                                    root.project.flexStopPreview()
                                }
                                onCanceled: { noteBar.liveSemitones = NaN; noteBar.liveShift = 0; axis = ""; root.project.flexStopPreview() }
                            }
                            Repeater {  // the edges resize the note
                                model: [true, false]
                                delegate: MouseArea {
                                    id: edge
                                    required property bool modelData
                                    x: modelData ? 0 : parent.width - 5
                                    width: 5
                                    height: parent.height
                                    cursorShape: Qt.SizeHorCursor
                                    preventStealing: true
                                    property real pressX: 0
                                    onPressed: (m) => { pressX = edge.mapToItem(notesLayer, m.x, 0).x; root.flexSelected = noteBar.modelData.index }
                                    onReleased: (m) => {
                                        const d = (edge.mapToItem(notesLayer, m.x, 0).x - pressX) / notesLayer.width
                                        if (Math.abs(d) * notesLayer.width < 2) return
                                        root.project.flexNoteTime(root.regionId, noteBar.modelData.index, noteBar.modelData.start + (edge.modelData ? d : 0), noteBar.modelData.end + (edge.modelData ? 0 : d))
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        FlexNoteStrip {
            id: flexStrip
            objectName: "flexNoteStrip"
            visible: root.pitchMode
            width: parent.width
            height: visible ? 50 : 0
            project: root.project
            regionId: root.regionId
            noteIndex: root.flexSelected
            note: root.flexSelected >= 0 && root.flexSelected < root.flexNotes.length ? root.flexNotes[root.flexSelected] : null
        }
    }
}
