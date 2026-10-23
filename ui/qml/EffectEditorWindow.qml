import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Jad

// The editor of a built-in effect (opened by a double click on its insert): one slider per parameter, from the effect's spec; the
// Channel EQ also draws its response. Every drag is one undo step; the sound follows while it moves.
Window {
    id: root
    required property ProjectController project
    readonly property string trackId: project.effectEditorTrack
    readonly property int index: project.effectEditorIndex
    property var insert: null           // the insert being edited ({processorId, params, bypass, ...})
    property var spec: null
    property var pending: ({})          // parameter -> the value waiting to be sent while a drag runs
    property var curve: []
    property bool gesture: false
    property var masterStats: ({})
    readonly property bool isMastering: spec !== null && spec.id === "builtin.mastering"
    readonly property bool isMatchEq: spec !== null && spec.id === "builtin.matcheq"
    property bool showList: false       // the parameter list instead of the graphical window
    // the graphical window: a layout of its own for the Channel EQ and the Compressor, knobs for every other effect and instrument; the Mastering
    // Assistant and Match EQ keep their panels above the list
    readonly property bool hasGraphic: spec !== null
    readonly property bool graphical: hasGraphic && !showList
    readonly property string kind: spec ? spec.id : ""
    // the header of the graphical windows (FxShell): power, presets, Compare, Copy and Paste, View
    property int viewPercent: 100
    property string presetName: qsTr("Default Preset")
    property var baseline: ({})         // the parameters as the window opened or a preset set them: what Compare goes back to
    property bool captureBaseline: true
    property bool comparing: false
    property var editedValues: ({})
    readonly property bool bypassed: insert !== null && insert.bypass === true
    readonly property bool edited: { pending; insert; return comparing || differs(valuesOf(), baseline) }
    readonly property bool canPaste: spec !== null && project.effectClipboard.processorId === spec.id
    function differs(a, b) {
        for (const k in a) if (b[k] === undefined || Math.abs(a[k] - b[k]) > 1e-6) return true
        return false
    }
    function toggleBypass() { if (index >= 0) project.setInsertBypass(trackId, index, !bypassed) }
    function openPresetMenu(item) { presetMenu.popup(item, 0, item.height) }
    function flatPresets() {
        const out = []
        for (const c of project.effectPresetMenu(spec.id)) for (const n of c.names) out.push({ category: c.category, name: n })
        return out
    }
    function stepPreset(dir) {
        if (!spec || index === -2) return
        const list = flatPresets()
        if (list.length === 0) return
        let at = -1
        for (let i = 0; i < list.length; ++i) if (list[i].name === presetName) at = i
        const next = list[(at + dir + list.length) % list.length]
        applyPreset(next.category, next.name)
    }
    function applyPreset(category, name) {
        flush()
        comparing = false
        project.applyEffectPreset(trackId, index, spec.id, category, name)
        presetName = name === "" ? qsTr("Default Preset") : name
        captureBaseline = true
    }
    function compare() {
        if (!spec) return
        if (comparing) { comparing = false; commitMany(editedValues); return }
        if (!differs(valuesOf(), baseline)) return
        editedValues = valuesOf()
        comparing = true
        commitMany(baseline)
    }
    function copySettings() { if (spec) project.effectClipboard = { processorId: spec.id, params: valuesOf() } }
    function pasteSettings() { if (canPaste) { comparing = false; commitMany(project.effectClipboard.params); captureBaseline = true } }
    property bool fitted: false
    // a window that is taller than the screen starts at the largest View that fits (50, 75 or 100 %): Logic does the same with its own View menu
    function fitView(extraHeight, bodyHeight, bodyWidth) {
        if (fitted) return
        fitted = true
        const maxH = Screen.desktopAvailableHeight > 0 ? Screen.desktopAvailableHeight - 60 : 900
        const maxW = Screen.desktopAvailableWidth > 0 ? Screen.desktopAvailableWidth - 20 : 1400
        for (const z of [viewPercent, 100, 75, 50]) {
            if (z > viewPercent) continue
            if (extraHeight + bodyHeight * z / 100 <= maxH && bodyWidth * z / 100 <= maxW) { viewPercent = z; return }
        }
        viewPercent = 50
    }
    function stepView(dir) {
        const steps = [50, 75, 100, 125, 150, 200]
        let i = steps.indexOf(viewPercent)
        if (i < 0) i = 2
        viewPercent = steps[Math.max(0, Math.min(steps.length - 1, i + dir))]
    }
    readonly property var defaultView: ({ "builtin.limiter": 75, "builtin.adaptivelimiter": 75, "builtin.gate": 75 })
    onKindChanged: { fitted = false; viewPercent = defaultView[kind] ?? 100; presetName = qsTr("Default Preset"); captureBaseline = true; comparing = false }

    visible: trackId !== "" && spec !== null
    width: graphical ? (graphic.item ? graphic.item.implicitWidth : 700) : 520
    height: Math.min(Screen.height - 40, graphical ? (graphic.item ? graphic.item.implicitHeight : 400)
                                                  : 120 + (spec ? spec.params.length : 0) * 30 + (curveBox.visible ? 150 : 0) + (isMastering ? 80 - 9 * 30 : 0))
    minimumWidth: graphical ? Math.min(width, 480) : 420
    minimumHeight: 200
    title: spec ? qsTr("%1 - %2").arg(project.trackName(trackId)).arg(spec.name) : ""
    color: Theme.surfacePanel
    onClosing: project.closeEffectEditor()

    function refresh() {
        let found = null
        if (index === -2) {  // the track's instrument
            const ins = project.trackInstrument(trackId)
            insert = ins.processorId ? ins : null
            if (insert) for (const s of project.instrumentSpecs()) if (s.id === insert.processorId) found = s
            if (found && found.params.length === 0) found = null  // the sine has nothing to edit
        } else {
            const list = project.trackInserts(trackId)
            insert = index >= 0 && index < list.length ? list[index] : null
            if (insert) for (const s of project.effectSpecs()) if (s.id === insert.processorId) found = s
        }
        spec = found
        if (spec && insert && captureBaseline) {
            const b = {}
            for (const p of spec.params) { const v = insert.params ? insert.params[p.name] : undefined; b[p.name] = v === undefined ? p.def : v }
            baseline = b
            captureBaseline = false
        }
        if (spec && spec.id === "builtin.eq") curve = project.eqCurve(valuesOf(), 160, 20, 20000)
    }
    function valueOf(p) {
        if (pending[p.name] !== undefined) return pending[p.name]
        const v = insert && insert.params ? insert.params[p.name] : undefined
        return v === undefined ? p.def : v
    }
    function valuesOf() {
        const m = {}
        if (spec) for (const p of spec.params) m[p.name] = valueOf(p)
        return m
    }
    onTrackIdChanged: refresh()
    onIndexChanged: refresh()
    Connections { target: root.project; function onProjectChanged() { root.refresh() } }
    Component.onCompleted: refresh()

    Timer {  // the values of a drag go out every 40 ms
        id: sender
        interval: 40
        repeat: true
        running: Object.keys(root.pending).length > 0
        onTriggered: root.flush()
    }
    // the interface of the graphical views: the spec of a parameter by name, a drag (many moves, one undo step at the end) and a single change
    function param(name) {
        if (spec) for (const p of spec.params) if (p.name === name) return p
        return { name: name, label: name, unit: "", min: 0, max: 1, def: 0, logarithmic: false }
    }
    function edit(name, value) {
        if (!gesture) { gesture = true; project.beginGesture() }
        const p = Object.assign({}, pending)
        p[name] = value
        pending = p
        if (spec && spec.id === "builtin.eq") curve = project.eqCurve(valuesOf(), 160, 20, 20000)
    }
    function commit(name, value) { commitMany(({ [name]: value })) }
    function commitMany(values) {
        const p = Object.assign({}, pending)
        for (const n in values) p[n] = values[n]
        pending = p
        flush()
        if (gesture) { gesture = false; project.endGesture() }
    }
    function setNow(name, value) { flush(); setParam(name, value) }
    function setParam(name, value) {
        if (index === -2) project.setInstrumentParam(trackId, name, value)
        else project.setInsertParam(trackId, index, name, value)
    }
    function flush() {
        for (const name in pending) setParam(name, pending[name])
        pending = ({})
    }

    Loader {  // the graphical window: Logic's frame and a layout of the plug-in
        id: graphic
        objectName: "graphicView"
        anchors.fill: parent
        visible: root.graphical
        active: root.graphical
        sourceComponent: root.kind === "builtin.eq" ? eqView : (root.kind === "builtin.compressor" ? compressorView : (root.kind === "builtin.limiter" || root.kind === "builtin.adaptivelimiter" ? limiterView : (root.kind === "builtin.gate" ? gateView : (root.kind === "builtin.mastering" ? masteringView : (root.kind === "builtin.matcheq" ? matchEqView : (root.kind === "builtin.reverb" ? reverbView : (root.kind === "builtin.delay" ? delayView : (root.kind === "builtin.mlsaturation" ? saturationView : (root.kind === "builtin.synth" ? synthView : (root.kind === "builtin.drums" ? drumKitView : (root.kind === "builtin.external" ? externalMidiView : (root.kind === "builtin.sampler" ? samplerView : (root.kind === "builtin.objectpanner" ? objectPannerView : gridView)))))))))))))
    }
    Component { id: eqView; FxChannelEqView { editor: root } }
    Component { id: compressorView; FxCompressorView { editor: root } }
    Component { id: reverbView; FxReverbView { editor: root } }
    Component { id: synthView; FxSynthView { editor: root } }
    Component { id: drumKitView; FxDrumKitView { editor: root } }
    Component { id: externalMidiView; FxExternalMidiView { editor: root } }
    Component { id: samplerView; FxSamplerView { editor: root } }
    Component { id: saturationView; FxSaturationView { editor: root } }
    Component { id: delayView; FxDelayView { editor: root } }
    Component { id: matchEqView; FxMatchEqView { editor: root } }
    Component { id: masteringView; FxMasteringView { editor: root } }
    Component { id: gateView; FxGateView { editor: root } }
    Component { id: limiterView; FxLimiterView { editor: root } }
    Component { id: objectPannerView; FxObjectPannerView { editor: root } }
    Component { id: gridView; FxGenericView { editor: root } }

    ColumnLayout {
        visible: !root.graphical
        anchors.fill: parent
        anchors.margins: Theme.spacing[3]
        spacing: Theme.spacing[2]

        RowLayout {  // Logic's factory presets of this effect (Compressor, Channel EQ, Noise Gate, Limiter, Adaptive Limiter)
            id: presetRow
            objectName: "presetRow"
            readonly property var menuData: root.spec && root.index !== -2 ? root.project.effectPresetMenu(root.spec.id) : []
            visible: menuData.length > 0
            Layout.fillWidth: true
            Text { text: qsTr("Preset"); Layout.preferredWidth: 130; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            IconButton {
                objectName: "presetButton"
                implicitHeight: 24
                label: qsTr("Factory Presets")
                onClicked: presetMenu.popup()
            }
            Item { Layout.fillWidth: true }
            ThemedMenu {
                id: presetMenu
                ThemedMenuItem { text: qsTr("Default"); onTriggered: root.applyPreset("", "") }
                MenuSeparator {}
                Instantiator {
                    model: presetRow.menuData
                    delegate: ThemedMenu {
                        id: categoryMenu
                        required property var modelData
                        title: modelData.category !== "" ? modelData.category : qsTr("Other")
                        Instantiator {
                            model: categoryMenu.modelData.names
                            delegate: ThemedMenuItem {
                                required property string modelData
                                text: modelData
                                onTriggered: root.applyPreset(categoryMenu.modelData.category, modelData)
                            }
                            onObjectAdded: (index, object) => categoryMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => categoryMenu.removeItem(object)
                        }
                    }
                    onObjectAdded: (index, object) => presetMenu.insertMenu(index + 2, object)
                    onObjectRemoved: (index, object) => presetMenu.removeMenu(object)
                }
            }
        }
        RowLayout {  // External MIDI: the port the notes go to
            visible: root.index === -2 && root.insert !== null && root.insert.processorId === "builtin.external"
            Layout.fillWidth: true
            Text { text: qsTr("MIDI Output"); Layout.preferredWidth: 130; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
            SelectField {
                objectName: "externalMidiPort"
                Layout.fillWidth: true
                choices: root.project.midiOutputNames()
                value: root.insert && root.insert.device !== undefined ? root.insert.device : ""
                placeholder: qsTr("No port chosen")
                onChosen: (c) => root.project.setExternalMidiDevice(root.trackId, c)
            }
            IconButton { objectName: "externalMidiTest"; implicitHeight: 24; label: qsTr("Test Note"); onClicked: root.project.externalMidiTestNote(root.trackId) }
        }
        Rectangle {  // the response of the Channel EQ
            id: curveBox
            visible: root.spec !== null && root.spec.id === "builtin.eq" && !root.graphical
            Layout.fillWidth: true
            Layout.preferredHeight: 130
            color: Theme.surfaceCanvas
            radius: Theme.radiusControl
            border.color: Theme.borderSubtle
            Canvas {
                id: canvas
                anchors.fill: parent
                anchors.margins: 4
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = Theme.borderSubtle
                    ctx.lineWidth = 1
                    for (const f of [100, 1000, 10000]) {  // the frequency grid
                        const x = Math.log(f / 20) / Math.log(1000) * width
                        ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
                    }
                    ctx.beginPath(); ctx.moveTo(0, height / 2); ctx.lineTo(width, height / 2); ctx.stroke()  // 0 dB
                    if (root.curve.length < 2) return
                    ctx.strokeStyle = Theme.accentPrimary
                    ctx.lineWidth = 2
                    ctx.beginPath()
                    for (let i = 0; i < root.curve.length; ++i) {
                        const x = i / (root.curve.length - 1) * width
                        const y = height / 2 - Math.max(-24, Math.min(24, root.curve[i])) / 24 * (height / 2)
                        if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                    }
                    ctx.stroke()
                }
                Connections { target: root; function onCurveChanged() { canvas.requestPaint() } }
            }
        }

        Rectangle {  // Match EQ: the reference curve, Learn, and the correction it found
            id: matchBox
            objectName: "matchEqBox"
            visible: root.isMatchEq && !root.graphical
            Layout.fillWidth: true
            Layout.preferredHeight: 190
            color: Theme.surfaceCanvas
            radius: Theme.radiusControl
            border.color: Theme.borderSubtle
            property string curveName: ""
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacing[2]
                spacing: Theme.spacing[2]
                RowLayout {
                    Layout.fillWidth: true
                    SelectField {
                        objectName: "matchEqCurve"
                        Layout.fillWidth: true
                        choices: root.project.matchEqCurveNames()
                        value: matchBox.curveName
                        placeholder: qsTr("Reference curve")
                        onChosen: (c) => matchBox.curveName = c
                    }
                    IconButton {
                        objectName: "matchEqLearn"
                        implicitHeight: 24
                        enabled: !root.project.masteringBusy && matchBox.curveName !== ""
                        label: root.project.masteringBusy ? qsTr("Analysing…") : qsTr("Learn")
                        onClicked: root.project.matchEqLearn(root.trackId, root.index, matchBox.curveName)
                    }
                }
                Canvas {  // the correction of each third-octave band
                    id: matchBars
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.strokeStyle = Theme.borderSubtle
                        ctx.beginPath(); ctx.moveTo(0, height / 2); ctx.lineTo(width, height / 2); ctx.stroke()
                        if (!root.spec) return
                        const n = 31
                        const w = width / n
                        ctx.fillStyle = Theme.accentPrimary
                        for (let i = 0; i < n; ++i) {
                            const p = root.spec.params.find(q => q.name === "b" + i)
                            const v = Math.max(-18, Math.min(18, p ? root.valueOf(p) : 0))
                            const h = v / 18 * (height / 2)
                            ctx.fillRect(i * w + 1, h >= 0 ? height / 2 - h : height / 2, Math.max(1, w - 2), Math.abs(h))
                        }
                    }
                    Connections { target: root; function onInsertChanged() { matchBars.requestPaint() } }
                    onWidthChanged: requestPaint()
                }
            }
        }
        Rectangle {  // Mastering Assistant: what the analysis found, and Reanalyze
            id: masterBox
            objectName: "masteringBox"
            visible: root.isMastering && !root.graphical
            Layout.fillWidth: true
            Layout.preferredHeight: 86
            color: Theme.surfaceCanvas
            radius: Theme.radiusControl
            border.color: Theme.borderSubtle
            Connections { target: root.project; function onMasteringAnalysed() { root.masterStats = root.project.masteringStats() } }
            Row {
                anchors.fill: parent
                anchors.margins: Theme.spacing[2]
                spacing: Theme.spacing[3]
                Column {
                    width: parent.width - reanalyze.width - 130 - Theme.spacing[3] * 2
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: root.masterStats.integratedLufs !== undefined ? qsTr("Analysed: %1 LUFS-I   %2 LU range   %3 dBTP").arg(root.masterStats.integratedLufs.toFixed(1)).arg(root.masterStats.range.toFixed(1)).arg(root.masterStats.truePeakDb.toFixed(1)) : qsTr("Not analysed yet")
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeLabelSize
                    }
                    Text {
                        visible: root.masterStats.outputLufs !== undefined
                        text: root.masterStats.outputLufs !== undefined ? qsTr("Output: %1 LUFS-I   %2 LU range   %3 dBTP").arg(root.masterStats.outputLufs.toFixed(1)).arg(root.masterStats.outputRange.toFixed(1)).arg(root.masterStats.outputTruePeakDb.toFixed(1)) : ""
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeLabelSize
                    }
                    Text {
                        visible: root.masterStats.outputLufs !== undefined && root.masterStats.outputLufs < -16 && root.masterStats.truePeakDb - root.masterStats.integratedLufs > 16
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: qsTr("The mix peaks %1 dB above its loudness: -14 LUFS would need heavy limiting, so it stays at %2 LUFS. Lower the peaks (compression before the master) or accept a quieter master.").arg((root.masterStats.truePeakDb - root.masterStats.integratedLufs).toFixed(0)).arg(root.masterStats.outputLufs !== undefined ? root.masterStats.outputLufs.toFixed(1) : "")
                        color: Theme.stateRecord
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeCaptionSize
                    }
                    Text {
                        text: root.masterStats.gainDb !== undefined ? qsTr("Brings the mix to -14 LUFS (%1 dB), limited at -1 dBTP").arg(root.masterStats.gainDb.toFixed(1)) : qsTr("The Loudness knob centre is -14 LUFS; the output never goes above -1 dB")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeCaptionSize
                    }
                }
                IconButton {
                    objectName: "masteringMeasure"
                    anchors.verticalCenter: parent.verticalCenter
                    enabled: !root.project.masteringBusy
                    label: qsTr("Measure Output")
                    onClicked: root.project.masteringMeasure(root.trackId, root.index)
                }
                IconButton {
                    id: reanalyze
                    objectName: "masteringReanalyze"
                    anchors.verticalCenter: parent.verticalCenter
                    enabled: !root.project.masteringBusy
                    label: root.project.masteringBusy ? qsTr("Analysing…") : (root.project.loopEndBeats > root.project.loopStartBeats ? qsTr("Reanalyze Section") : qsTr("Reanalyze"))
                    onClicked: root.project.masteringAnalyse(root.trackId, root.index)
                }
            }
        }

        RowLayout {
            visible: root.hasGraphic
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            IconButton {
                objectName: "showParameterList"
                implicitHeight: 22
                label: root.showList ? qsTr("Graphic") : qsTr("Parameters")
                onClicked: root.showList = !root.showList
            }
        }

        Flickable {
            visible: !root.graphical
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: grid.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            GridLayout {
                id: grid
                width: parent.width - 12
                columns: 3
                columnSpacing: Theme.spacing[3]
                rowSpacing: Theme.spacing[1]
                Repeater {
                    model: root.spec ? root.spec.params.filter(p => !(root.isMastering && (p.name === "gain" || p.name === "compTrim" || p.name.startsWith("eq"))) && !(root.isMatchEq && /^b[0-9]+$/.test(p.name))) : []
                    delegate: RowLayout {
                        id: row
                        required property var modelData
                        Layout.columnSpan: 3
                        Layout.fillWidth: true
                        spacing: Theme.spacing[3]
                        readonly property real current: root.valueOf(modelData)
                        Text {
                            Layout.preferredWidth: 130
                            text: row.modelData.label
                            color: Theme.textSecondary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontTypeLabelSize
                        }
                        readonly property bool isSwitch: modelData.min === 0 && modelData.max === 1   // Excite, Bypass, Ping-Pong...: on or off
                        Item {
                            visible: row.isSwitch
                            Layout.fillWidth: true
                            Layout.preferredHeight: 22
                            IconButton {
                                objectName: "paramSwitch_" + row.modelData.name
                                implicitHeight: 22
                                label: row.current >= 0.5 ? qsTr("On") : qsTr("Off")
                                active: row.current >= 0.5
                                onClicked: { root.flush(); root.setParam(row.modelData.name, row.current >= 0.5 ? 0 : 1) }
                            }
                        }
                        ParamSlider {
                            visible: !row.isSwitch
                            Layout.fillWidth: true
                            from: row.modelData.min
                            to: row.modelData.max
                            defaultValue: row.modelData.def
                            logarithmic: row.modelData.logarithmic
                            value: row.current
                            onMoved: (v) => {
                                if (!root.gesture) { root.gesture = true; root.project.beginGesture() }
                                const p = Object.assign({}, root.pending)
                                p[row.modelData.name] = v
                                root.pending = p
                                if (root.spec.id === "builtin.eq") root.curve = root.project.eqCurve(root.valuesOf(), 160, 20, 20000)
                            }
                            onReleased: (v) => {
                                root.flush()
                                root.setParam(row.modelData.name, v)
                                if (root.gesture) { root.gesture = false; root.project.endGesture() }
                            }
                        }
                        Text {
                            visible: !row.isSwitch
                            Layout.preferredWidth: 80
                            horizontalAlignment: Text.AlignRight
                            text: (Math.abs(row.current) >= 100 ? row.current.toFixed(0) : (Math.abs(row.current) >= 10 ? row.current.toFixed(1) : row.current.toFixed(2))) + (row.modelData.unit !== "" ? " " + row.modelData.unit : "")
                            color: Theme.textValue
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontTypeLabelSize
                        }
                    }
                }
            }
        }
    }
}
