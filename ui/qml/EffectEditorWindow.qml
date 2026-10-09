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

    visible: trackId !== "" && spec !== null
    width: 520
    height: Math.min(Screen.height - 80, 120 + (spec ? spec.params.length : 0) * 30 + (curveBox.visible ? 150 : 0))
    minimumWidth: 420
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
    function setParam(name, value) {
        if (index === -2) project.setInstrumentParam(trackId, name, value)
        else project.setInsertParam(trackId, index, name, value)
    }
    function flush() {
        for (const name in pending) setParam(name, pending[name])
        pending = ({})
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing[3]
        spacing: Theme.spacing[2]

        Rectangle {  // the response of the Channel EQ
            id: curveBox
            visible: root.spec !== null && root.spec.id === "builtin.eq"
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

        Flickable {
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
                    model: root.spec ? root.spec.params : []
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
                        ParamSlider {
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
