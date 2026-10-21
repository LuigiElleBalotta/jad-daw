import QtQuick
import QtQuick.Layouts
import Jad

// Compressor of Logic Pro 11.2: the circuit menu, the transfer curve (input against output with the threshold, ratio and knee drawn), the gain reduction meter,
// and a knob for each control. The reduction shown is the one of the track the effect sits on.
Item {
    id: root
    required property var editor
    readonly property var circuits: ["Platinum Digital", "Class A R", "Class A U", "Vintage VCA", "Studio VCA", "Studio FET", "Vintage Opto", "Vintage FET", "Classic VCA"]
    readonly property real dbMin: -60
    implicitHeight: 330
    property real reduction: 0

    function val(name) { return editor.valueOf(editor.param(name)) }
    // the static curve: output level for an input level, from the threshold, ratio, knee and make-up gain
    function outputFor(x) {
        const t = val("threshold"), r = Math.max(1, val("ratio")), w = val("knee") * 20
        let y = x
        if (2 * (x - t) > w) y = t + (x - t) / r
        else if (w > 0 && 2 * Math.abs(x - t) <= w) y = x + (1 / r - 1) * (x - t + w / 2) * (x - t + w / 2) / (2 * w)
        let makeup = val("gain")
        if (val("autoGain") >= 0.5) makeup += -t * (1 - 1 / r)
        return y + makeup
    }
    Timer {
        interval: 50; repeat: true; running: root.visible
        onTriggered: { const r = root.editor.project.trackReduction(root.editor.trackId); root.reduction = Math.max(r, root.reduction - 1.2) }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 14
        ColumnLayout {  // the screen
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            spacing: 6
            SelectField {
                objectName: "compCircuit"
                Layout.fillWidth: true
                choices: root.circuits.map((n, i) => i)
                value: Math.round(root.val("circuit"))
                format: (i) => root.circuits[i]
                onChosen: (c) => root.editor.setNow("circuit", c)
            }
            FxDisplay {
                id: screen
                Layout.preferredWidth: 250
                Layout.preferredHeight: 250
                Canvas {
                    id: canvas
                    anchors.fill: parent
                    anchors.margins: 8
                    function px(v) { return (v - root.dbMin) / (-root.dbMin) * width }
                    function py(v) { return height - (v - root.dbMin) / (-root.dbMin) * height }
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.lineWidth = 1
                        ctx.font = "9px sans-serif"
                        for (let d = -48; d < 0; d += 12) {
                            ctx.strokeStyle = "#232327"
                            ctx.beginPath(); ctx.moveTo(Math.round(px(d)) + 0.5, 0); ctx.lineTo(Math.round(px(d)) + 0.5, height); ctx.stroke()
                            ctx.beginPath(); ctx.moveTo(0, Math.round(py(d)) + 0.5); ctx.lineTo(width, Math.round(py(d)) + 0.5); ctx.stroke()
                            ctx.fillStyle = "#5f5f66"
                            ctx.fillText(String(d), px(d) + 2, height - 2)
                        }
                        ctx.strokeStyle = "#3c3c42"  // no compression: output = input
                        ctx.beginPath(); ctx.moveTo(px(root.dbMin), py(root.dbMin)); ctx.lineTo(px(0), py(0)); ctx.stroke()
                        const t = root.val("threshold")
                        ctx.strokeStyle = "#c9a54a"  // the threshold
                        ctx.setLineDash([3, 3])
                        ctx.beginPath(); ctx.moveTo(px(t) + 0.5, 0); ctx.lineTo(px(t) + 0.5, height); ctx.stroke()
                        ctx.setLineDash([])
                        ctx.strokeStyle = Theme.accentPrimary
                        ctx.lineWidth = 2
                        ctx.beginPath()
                        for (let i = 0; i <= 120; ++i) {
                            const x = root.dbMin + i / 120 * (-root.dbMin)
                            const y = Math.max(root.dbMin, Math.min(0, root.outputFor(x)))
                            if (i === 0) ctx.moveTo(px(x), py(y)); else ctx.lineTo(px(x), py(y))
                        }
                        ctx.stroke()
                    }
                    Connections { target: root.editor; function onPendingChanged() { canvas.requestPaint() }
                        function onInsertChanged() { canvas.requestPaint() } }
                    Component.onCompleted: requestPaint()
                }
            }
        }
        Item {  // the gain reduction meter
            Layout.preferredWidth: 38
            Layout.fillHeight: true
            Text { x: 0; y: 0; width: parent.width; horizontalAlignment: Text.AlignHCenter; text: qsTr("GR"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize }
            FxDisplay {
                id: meter
                x: 8; y: 22
                width: 22
                height: 250
                Rectangle {  // the reduction hangs from the top
                    x: 4; y: 4
                    width: parent.width - 8
                    height: Math.min(1, root.reduction / 30) * (parent.height - 8)
                    radius: 2
                    color: root.reduction > 18 ? "#e5645f" : (root.reduction > 9 ? "#e8c93a" : "#62c460")
                }
                Repeater {
                    model: [0, 6, 12, 18, 24, 30]
                    delegate: Rectangle { required property int modelData; x: 2; y: 4 + modelData / 30 * (meter.height - 8); width: meter.width - 4; height: 1; color: "#2e2e33" }
                }
            }
            Text { x: 0; y: 22 + 250 + 4; width: parent.width; horizontalAlignment: Text.AlignHCenter; text: root.reduction.toFixed(1); color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize }
        }
        ColumnLayout {  // the controls
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            Flow {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: ["threshold", "ratio", "knee", "attack", "release", "gain", "mix"]
                    delegate: FxKnob {
                        required property string modelData
                        readonly property var p: root.editor.param(modelData)
                        label: ({ threshold: qsTr("Threshold"), ratio: qsTr("Ratio"), knee: qsTr("Knee"), attack: qsTr("Attack"), release: qsTr("Release"), gain: qsTr("Make-up"), mix: qsTr("Mix") })[modelData]
                        from: p.min; to: p.max; defaultValue: p.def; logarithmic: p.logarithmic; unit: p.unit
                        bipolar: modelData === "gain"
                        format: modelData === "ratio" ? (v) => v.toFixed(v < 10 ? 1 : 0) + ":1" : (modelData === "knee" ? (v) => Math.round(v * 100) + " %" : null)
                        value: root.val(modelData)
                        onMoved: (v) => root.editor.edit(modelData, v)
                        onReleased: (v) => root.editor.commit(modelData, v)
                    }
                }
            }
            Row {
                spacing: 8
                FxSwitch { label: qsTr("Auto Release"); on: root.val("autoRelease") >= 0.5; onToggled: (v) => root.editor.setNow("autoRelease", v ? 1 : 0) }
                FxSwitch { label: qsTr("Auto Gain"); on: root.val("autoGain") >= 0.5; onToggled: (v) => root.editor.setNow("autoGain", v ? 2 : 0) }
                FxSwitch { label: root.val("peakRms") >= 0.5 ? qsTr("RMS") : qsTr("Peak"); on: root.val("peakRms") >= 0.5; onToggled: (v) => root.editor.setNow("peakRms", v ? 1 : 0) }
            }
            Row {
                spacing: 8
                FxSwitch { label: qsTr("Limiter"); on: root.val("limiter") >= 0.5; onToggled: (v) => root.editor.setNow("limiter", v ? 1 : 0) }
                FxValue {
                    width: 90
                    label: qsTr("Limiter Threshold")
                    readonly property var p: root.editor.param("limiterThreshold")
                    from: p.min; to: p.max; defaultValue: p.def; unit: "dB"
                    value: root.val("limiterThreshold")
                    enabledLook: root.val("limiter") >= 0.5
                    onMoved: (v) => root.editor.edit("limiterThreshold", v)
                    onReleased: (v) => root.editor.commit("limiterThreshold", v)
                }
                Column {
                    spacing: 2
                    Text { text: qsTr("Output Distortion"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeCaptionSize }
                    SelectField {
                        width: 110
                        choices: [0, 1, 2, 3]
                        value: Math.round(root.val("outputDistortion"))
                        format: (i) => [qsTr("Off"), qsTr("Soft"), qsTr("Hard"), qsTr("Clip")][i]
                        onChosen: (c) => root.editor.setNow("outputDistortion", c)
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }
    }
}
