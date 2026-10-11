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

    // the revision is used (not just read), so that the compiled binding keeps depending on it
    readonly property var info: { const rev = project.revision; const r = regionId !== "" ? project.regionInfo(regionId) : ({ found: false }); r.revision = rev; return r }
    readonly property bool valid: info.found === true && info.audio === true
    readonly property real rate: project ? project.sampleRateHz : 48000
    // the frames shown: the region's part, or the whole file
    readonly property real shownFrom: valid ? (fileMode ? 0 : info.sourceOffsetFrames) : 0
    readonly property real shownFrames: valid ? (fileMode ? Math.max(1, info.mediaFrames) : Math.max(1, info.lengthFrames)) : 1
    property real zoom: 1
    // Flex Pitch: the notes of the region are drawn over the waveform, in the pitch range they span
    readonly property bool pitchMode: valid && !fileMode && flexMode === "pitch"
    property int flexSelected: -1
    // Flex Time: the transients and the flex markers over the waveform (the markers come with the region's info)
    // the mode comes from the track list, which is what the header's Flex button follows (the region info alone can lag behind it)
    property string trackFlexMode: "?"
    readonly property string flexMode: valid ? (trackFlexMode !== "?" ? trackFlexMode : (info.flexMode ?? "")) : ""
    Repeater {
        model: root.project ? root.project.tracks : null
        delegate: Item {
            required property string trackId
            required property string flexMode
            visible: false
            Binding { target: root; property: "trackFlexMode"; value: flexMode; when: root.valid && trackId === root.info.trackId }
        }
    }
    readonly property bool timeFlex: valid && !fileMode && flexMode !== "" && flexMode !== "pitch"
    property var flexTransients: []
    function refreshTransients() { flexTransients = timeFlex ? project.flexTransients(regionId) : [] }
    onTimeFlexChanged: refreshTransients()
    readonly property var flexModes: [{ id: "", label: qsTr("Off") }, { id: "slicing", label: qsTr("Slicing") }, { id: "rhythmic", label: qsTr("Rhythmic") },
                                      { id: "monophonic", label: qsTr("Monophonic") }, { id: "polyphonic", label: qsTr("Polyphonic") },
                                      { id: "tempophone", label: qsTr("Tempophone") }, { id: "speed", label: qsTr("Speed") },
                                      { id: "automatic", label: qsTr("Automatic") }, { id: "pitch", label: qsTr("Flex Pitch") }]
    property int flexRefresh: 0
    // (the revision is used in the expression, not only read: the compiled binding would drop a bare read)
    readonly property var flexNotes: { const rev = project.revision + flexRefresh; return pitchMode && rev >= 0 ? project.flexPitchNotes(regionId) : [] }
    readonly property var flexCurves: { const rev = project.revision + flexRefresh; return pitchMode && rev >= 0 ? project.flexPitchCurves(regionId) : [] }
    // the pitch rows: two octaves at least, the notes in the middle
    readonly property real pitchSpan: { let lo = 127, hi = 0; for (const n of flexNotes) { lo = Math.min(lo, n.pitch, n.original); hi = Math.max(hi, n.pitch, n.original) } return flexNotes.length ? Math.max(24, Math.ceil(hi - lo) + 8) : 24 }
    readonly property real lowPitch: { let lo = 127, hi = 0; for (const n of flexNotes) { lo = Math.min(lo, n.pitch, n.original); hi = Math.max(hi, n.pitch, n.original) } return flexNotes.length ? Math.floor((lo + hi) / 2 - pitchSpan / 2) : 48 }
    readonly property real highPitch: lowPitch + pitchSpan
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
    onInfoChanged: { refresh.restart(); refreshTransients() }
    Timer { id: refresh; interval: 80; onTriggered: root.refreshPeaks() }
    Connections {
        target: root.project
        function onRegionPeaksReady(id) { if (id === root.regionId) root.refreshPeaks() }
        function onWaveformReady(id) { if (root.valid && id === root.info.mediaId) root.refreshPeaks() }
        function onFlexAnalysisReady(id) { if (id === root.regionId) { root.flexRefresh++; root.refreshTransients() } }
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
        Rectangle {  // Flex: the mode, and what can be done with the markers and the notes
            id: flexBar
            objectName: "flexBar"
            visible: root.valid && !root.fileMode && (root.flexMode !== "" || root.project.flexView)
            width: parent.width
            height: visible ? 28 : 0
            color: Theme.surfacePanel
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.borderSubtle }
            Row {
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.spacing[2]
                spacing: Theme.spacing[1]
                IconButton {
                    objectName: "flexModeButton"
                    implicitHeight: 22
                    label: qsTr("Flex: %1").arg((root.flexModes.find(m => m.id === root.flexMode) ?? root.flexModes[0]).label)
                    active: root.flexMode !== ""
                    onClicked: flexModePopup.popup()
                }
                IconButton { visible: root.timeFlex; implicitHeight: 22; label: qsTr("Slice at Transients"); onClicked: { root.project.selectRegion(root.regionId, "replace"); root.project.flexSlice(false) } }
                IconButton { visible: root.timeFlex; implicitHeight: 22; label: qsTr("Quantize"); onClicked: { root.project.selectRegion(root.regionId, "replace"); root.project.flexQuantize(root.project.snapBeats > 0 ? root.project.snapBeats : 0.25, 100) } }
                IconButton {
                    objectName: "followTempoButton"
                    implicitHeight: 22
                    active: root.info.followTempo ?? false
                    label: (root.info.followTempo ?? false) ? qsTr("Following %1 bpm").arg(Math.round((root.info.beatsBpm ?? 0) * 10) / 10) : qsTr("Follow Tempo")
                    onClicked: root.project.smartFollowRegion(root.regionId, !(root.info.followTempo ?? false))
                }
                IconButton {
                    visible: root.info.beatsBpm > 0
                    implicitHeight: 22
                    label: qsTr("Downbeat Here")
                    onClicked: root.project.smartHint(root.regionId, (root.project.positionBeats - root.info.startBeats) / Math.max(0.0001, root.info.lengthBeats), true)
                }
                IconButton {
                    visible: root.info.beatsBpm > 0
                    implicitHeight: 22
                    label: qsTr("Beat Here")
                    onClicked: root.project.smartHint(root.regionId, (root.project.positionBeats - root.info.startBeats) / Math.max(0.0001, root.info.lengthBeats), false)
                }
                IconButton { visible: root.flexMode !== ""; implicitHeight: 22; label: qsTr("Reset All"); onClicked: { root.project.selectRegion(root.regionId, "replace"); root.project.flexResetEdits() } }
                IconButton { visible: root.pitchMode; implicitHeight: 22; label: qsTr("Create MIDI Track"); onClicked: root.project.flexCreateMidiTrack(root.regionId) }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: Theme.spacing[3]
                    visible: root.flexMode === ""
                    text: qsTr("Choose a Flex mode to see the transients and edit the sound")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeLabelSize
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: Theme.spacing[3]
                    visible: root.timeFlex
                    text: qsTr("Double click the waveform to set a flex marker (upper half one, lower half three); drag a marker to stretch, Alt to slide it; double click a marker to delete it")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeLabelSize
                }
            }
            ThemedMenu {
                id: flexModePopup
                Instantiator {
                    model: root.flexModes
                    delegate: ThemedMenuItem {
                        required property var modelData
                        text: modelData.label
                        checkable: true
                        checked: root.flexMode === modelData.id
                        onTriggered: root.project.setTrackFlexMode(root.info.trackId, modelData.id)
                    }
                    onObjectAdded: (index, object) => flexModePopup.insertItem(index, object)
                    onObjectRemoved: (index, object) => flexModePopup.removeItem(object)
                }
            }
        }
        Row {
            id: body
            width: parent.width
            height: parent.height - 30 - flexBar.height
            FlexNoteStrip {  // the inspector of the Audio Track Editor in Flex Pitch
                id: flexStrip
                objectName: "flexNoteStrip"
                visible: root.pitchMode
                width: visible ? 210 : 0
                height: body.height
                project: root.project
                regionId: root.regionId
                noteIndex: root.flexSelected
                note: root.flexSelected >= 0 && root.flexSelected < root.flexNotes.length ? root.flexNotes[root.flexSelected] : null
            }
            Item {  // the keyboard of the pitch rows
                id: piano
                objectName: "flexPiano"
                visible: root.pitchMode
                width: visible ? 40 : 0
                height: body.height
                clip: true
                Rectangle { anchors.fill: parent; color: Theme.surfacePanel }
                Repeater {
                    model: root.pitchMode ? Math.ceil(root.highPitch - root.lowPitch) : 0
                    delegate: Rectangle {
                        required property int index
                        readonly property int midi: Math.floor(root.highPitch) - index
                        readonly property bool black: [1, 3, 6, 8, 10].indexOf(((midi % 12) + 12) % 12) >= 0
                        readonly property real rowH: (body.height - 20) / (root.highPitch - root.lowPitch)
                        y: 20 + (root.highPitch - (midi + 0.5)) * rowH
                        width: parent.width - (black ? 12 : 1)
                        height: Math.max(1, rowH - 1)
                        color: black ? "#1b1b1f" : "#d6d6da"
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 3
                            anchors.verticalCenter: parent.verticalCenter
                            visible: midi % 12 === 0
                            text: "C" + (Math.floor(midi / 12) - 1)
                            color: "#303036"
                            font.family: Theme.fontFamily
                            font.pixelSize: 9
                        }
                    }
                }
            }
            Flickable {
            id: flick
            width: body.width - flexStrip.width - piano.width
            height: body.height
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
                    Connections { target: root; function onPeaksChanged() { wave.requestPaint() } function onPitchModeChanged() { wave.requestPaint() } }
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
                        ctx.fillStyle = root.pitchMode ? "rgba(60,100,160,0.42)" : Theme.accentPrimary
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
                        onPressed: (m) => {
                            if (root.timeFlex && root.project.tool === "flex") { root.project.flexAddMarker(root.regionId, m.x / width, m.y > height / 2); return }
                            anchorFrame = frameAt(m.x); root.selFrom = -1; root.selTo = -1
                        }
                        onDoubleClicked: (m) => { if (root.timeFlex) root.project.flexAddMarker(root.regionId, m.x / width, m.y > height / 2) }
                        onPositionChanged: (m) => {
                            if (!pressed) return
                            const f = frameAt(m.x)
                            root.selFrom = Math.min(anchorFrame, f)
                            root.selTo = Math.max(anchorFrame, f)
                        }
                        onWheel: (w) => { flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, flick.contentX - w.angleDelta.y - w.angleDelta.x)) }
                    }
                }
                FlexOverlay {
                    id: timeOverlay
                    objectName: "flexTimeOverlay"
                    y: ruler.height
                    width: parent.width
                    height: parent.height - ruler.height
                    visible: root.timeFlex
                    project: root.project
                    regionId: root.regionId
                    mode: root.flexMode
                    markers: root.info.flexMarkers ?? []
                    transients: root.flexTransients
                    beats: root.info.beatFracs ?? []
                    onBeatMoved: (index, fraction) => root.project.smartMoveBeat(root.regionId, index, fraction)
                    onBeatDeleted: (index) => root.project.smartDeleteBeat(root.regionId, index)
                    tint: Theme.textPrimary
                    onMarkerMoved: (index, fraction, slide) => root.project.flexMoveMarker(root.regionId, index, fraction, slide)
                    onMarkerDeleted: (index) => root.project.flexDeleteMarker(root.regionId, index)
                    onMarkerDragging: (index, fraction, slide) => root.project.flexMarkerPreview(root.regionId, index, fraction, slide)
                    onMarkerDragEnded: root.project.flexStopPreview()
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
                    Canvas {  // the pitch curve of every note, as it plays
                        id: curveLayer
                        objectName: "flexCurves"
                        anchors.fill: parent
                        z: 2
                        Connections { target: root; function onFlexCurvesChanged() { curveLayer.requestPaint() } }
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.strokeStyle = "#b7d8ff"
                            ctx.lineWidth = 1.4
                            for (const note of root.flexCurves) {
                                const pts = note.points
                                if (pts.length < 4) continue
                                ctx.beginPath()
                                for (let k = 0; k + 1 < pts.length; k += 2) {
                                    const x = pts[k] * width, y = notesLayer.yOf(pts[k + 1])
                                    if (k === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                                }
                                ctx.stroke()
                            }
                        }
                    }
                    Repeater {  // the C lines, with their names
                        model: root.pitchMode ? Math.ceil(root.highPitch - root.lowPitch) : 0
                        delegate: Item {
                            required property int index
                            readonly property int midi: Math.floor(root.highPitch) - index
                            visible: midi % 12 === 0 || notesLayer.rowHeight >= 10
                            y: notesLayer.yOf(midi + 0.5)
                            width: notesLayer.width
                            Rectangle { y: 0; width: parent.width; height: notesLayer.rowHeight; visible: [1, 3, 6, 8, 10].indexOf(((midi % 12) + 12) % 12) >= 0; color: "#0fffffff" }
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
                            color: selectedNote ? "#5aaaff" : "#2f5f97"
                            border.color: selectedNote ? "#cfe6ff" : "#1b3a5c"
                            border.width: 1
                            Rectangle {  // the lower strip of a note: its gain
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 1
                                x: 1
                                width: parent.width - 2
                                height: Math.max(2, Math.min(parent.height / 3, 5))
                                radius: 1
                                color: Qt.rgba(0, 0, 0, 0.38)
                            }
                            Text {
                                x: 4
                                anchors.verticalCenter: parent.verticalCenter
                                visible: false
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
                            Repeater {  // the six hotspots of the selected note (drag: the value is heard as it moves; double click: back to its default)
                                model: noteBar.selectedNote ? [
                                    { key: "driftIn", label: qsTr("Pitch Drift In"), from: 0, to: 1, def: 0, horizontal: true, sign: 1, range: 70, unit: "%", scale: 100, ax: 0, ay: 0 },
                                    { key: "vibrato", label: qsTr("Vibrato"), from: 0, to: 2, def: 1, horizontal: false, sign: 1, range: 70, unit: "%", scale: 100, ax: 0.5, ay: 0 },
                                    { key: "driftOut", label: qsTr("Pitch Drift Out"), from: 0, to: 1, def: 0, horizontal: true, sign: -1, range: 70, unit: "%", scale: 100, ax: 1, ay: 0 },
                                    { key: "formantCents", label: qsTr("Formant Shift"), from: -600, to: 600, def: 0, horizontal: false, sign: 1, range: 120, unit: " ct", scale: 1, ax: 0, ay: 1 },
                                    { key: "fineCents", label: qsTr("Fine Pitch"), from: -50, to: 50, def: 0, horizontal: false, sign: 1, range: 60, unit: " ct", scale: 1, ax: 0.5, ay: 1 },
                                    { key: "gainDb", label: qsTr("Gain"), from: -12, to: 12, def: 0, horizontal: false, sign: 1, range: 80, unit: " dB", scale: 1, ax: 1, ay: 1 }
                                ] : []
                                delegate: Rectangle {
                                    id: spot
                                    required property var modelData
                                    objectName: "flexHotspot_" + modelData.key
                                    property bool dragging: false
                                    property real startValue: 0
                                    property real liveValue: 0
                                    width: 9; height: 9; radius: 5
                                    x: modelData.ax * (noteBar.width - width)
                                    y: modelData.ay * (noteBar.height - height) + (modelData.ay === 0 ? -5 : 5)
                                    z: 3
                                    color: dragging ? "#ffffff" : "#e8f2ff"
                                    border.color: "#1b3a5c"
                                    border.width: 1
                                    function clampValue(v) { return Math.max(modelData.from, Math.min(modelData.to, v)) }
                                    MouseArea {
                                        anchors.fill: parent
                                        anchors.margins: -3
                                        preventStealing: true
                                        cursorShape: spot.modelData.horizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor
                                        property real pressPos: 0
                                        onPressed: (m) => {
                                            root.flexSelected = noteBar.modelData.index
                                            spot.startValue = noteBar.modelData[spot.modelData.key]
                                            spot.liveValue = spot.startValue
                                            spot.dragging = true
                                            const p = mapToItem(notesLayer, m.x, m.y)
                                            pressPos = spot.modelData.horizontal ? p.x : p.y
                                        }
                                        onPositionChanged: (m) => {
                                            if (!pressed) return
                                            const p = mapToItem(notesLayer, m.x, m.y)
                                            const d = spot.modelData.horizontal ? (p.x - pressPos) * spot.modelData.sign : pressPos - p.y
                                            spot.liveValue = spot.clampValue(spot.startValue + d / spot.modelData.range * (spot.modelData.to - spot.modelData.from))
                                            const e = {}; e[spot.modelData.key] = spot.liveValue
                                            root.project.flexNotePreview(root.regionId, noteBar.modelData.index, e)
                                        }
                                        onReleased: {
                                            spot.dragging = false
                                            if (spot.liveValue !== spot.startValue) {
                                                const e = {}; e[spot.modelData.key] = spot.liveValue
                                                root.project.flexNoteEdit(root.regionId, noteBar.modelData.index, e)
                                            }
                                            root.project.flexStopPreview()
                                        }
                                        onCanceled: { spot.dragging = false; root.project.flexStopPreview() }
                                        onDoubleClicked: {
                                            const e = {}; e[spot.modelData.key] = spot.modelData.def
                                            root.project.flexNoteEdit(root.regionId, noteBar.modelData.index, e)
                                        }
                                    }
                                    Rectangle {  // the value while it is dragged
                                        visible: spot.dragging
                                        x: 12; y: -2
                                        width: tip.implicitWidth + 8; height: tip.implicitHeight + 4
                                        radius: 3
                                        color: Theme.surfaceCanvas
                                        border.color: Theme.borderStrong
                                        Text {
                                            id: tip
                                            anchors.centerIn: parent
                                            text: spot.modelData.label + " " + Math.round(spot.liveValue * spot.modelData.scale) + spot.modelData.unit
                                            color: Theme.textPrimary
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontTypeCaptionSize
                                        }
                                    }
                                }
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
        }
    }
}
