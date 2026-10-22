import QtQuick
import Jad

// Limiter and Adaptive Limiter of Logic Pro 11.2: on the left three vertical meters (Input, Reduction, Output) on a dark panel, on the right the knobs on a blue panel with
// the switches under them. The same frame; the Adaptive Limiter has Gain, Out Ceiling and Lookahead and its own switches.
FxShell {
    id: root
    readonly property bool adaptive: editor.kind === "builtin.adaptivelimiter"
    title: adaptive ? qsTr("Adaptive Limiter") : qsTr("Limiter")
    bodyWidth: 972
    bodyHeight: 336
    chassis: "#1f3249"
    readonly property var meters: editor.project.editorMeters
    function val(name) { return editor.valueOf(editor.param(name)) }

    Rectangle { x: 0; y: 0; width: 398; height: root.bodyHeight; color: "#0d1823" }
    Rectangle {
        x: 398; y: 0; width: root.bodyWidth - 398; height: root.bodyHeight
        gradient: Gradient { GradientStop { position: 0; color: "#2a4663" } GradientStop { position: 1; color: "#1d3148" } }
    }

    // the meters
    component Meter: Item {
        id: m
        property string title
        property real centerX
        property var labels: []
        property real levelDb: -120
        property var channelDb: []           // the levels of the left and the right channel; empty draws levelDb as one bar
        property bool hang: false            // the reduction hangs from the top
        property real scaleTop: 20           // the dB at the first label
        property real scaleBottom: -60
        readonly property real barTop: 41
        readonly property real step: 18
        function frac(db) {  // 0 at the top label, 1 at the bottom one, spread over the labels
            const l = labels
            for (let i = 0; i + 1 < l.length; ++i) {
                const a = l[i], b = l[i + 1]
                if ((a >= b && db <= a && db >= b) || (a < b && db >= a && db <= b)) return (i + (db - a) / (b - a)) / (l.length - 1)
            }
            return (labels[0] >= labels[labels.length - 1]) ? (db > labels[0] ? 0 : 1) : (db < labels[0] ? 0 : 1)
        }
        x: 0; y: 0; width: root.bodyWidth; height: root.bodyHeight
        Text { x: m.centerX - 60; y: 6; width: 120; horizontalAlignment: Text.AlignHCenter; text: m.title; color: "#8da2b8"; font.family: Theme.fontFamily; font.pixelSize: 19 }
        Text { x: m.centerX - 70; y: 38; text: "dB"; color: "#6c8196"; font.family: Theme.fontFamily; font.pixelSize: 13 }
        Repeater {
            model: m.labels
            delegate: Text {
                required property int index
                required property real modelData
                x: m.centerX - 70; y: m.barTop + 17 + index * m.step - height / 2
                width: 26; horizontalAlignment: Text.AlignRight
                text: modelData === 0 ? "0" : String(modelData)
                color: modelData === 0 ? "#d6e2ee" : "#7f93a8"
                font.family: Theme.fontFamily; font.pixelSize: 14
            }
        }
        Repeater {  // the lines of the scale
            model: m.labels
            delegate: Rectangle { required property int index; x: m.centerX - 40; y: m.barTop + 17 + index * m.step; width: 80; height: 1; color: "#18293a" }
        }
        Rectangle { x: m.centerX - 0.5; y: m.barTop + 17; width: 1; height: (m.labels.length - 1) * m.step; color: "#16283a" }
        Rectangle {
            x: m.centerX - 7; width: 14
            y: m.barTop + 17
            height: (m.labels.length - 1) * m.step
            color: "transparent"
            Repeater {
                model: m.channelDb.length > 0 ? m.channelDb.length : 1   // one bar, or the left and the right channel side by side
                delegate: Rectangle {
                    required property int index
                    x: index * parent.width / (m.channelDb.length > 0 ? m.channelDb.length : 1)
                    width: parent.width / (m.channelDb.length > 0 ? m.channelDb.length : 1) - (m.channelDb.length > 1 ? 1 : 0)
                    height: Math.max(0, m.frac(m.channelDb.length > 0 ? m.channelDb[index] : m.levelDb) * parent.height)
                    y: m.hang ? 0 : parent.height - height
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: m.hang ? "#2fb04a" : "#ff3a2a" }
                        GradientStop { position: 0.35; color: "#ffd21a" }
                        GradientStop { position: 1.0; color: m.hang ? "#ff3a2a" : "#2fb04a" }
                    }
                }
            }
        }
    }
    Meter { title: qsTr("INPUT"); centerX: 107; labels: [20, 12, 9, 6, 3, 0, -3, -6, -9, -12, -15, -20, -30, -60]; levelDb: root.meters.inDb ?? -120; channelDb: [root.meters.inLDb ?? -120, root.meters.inRDb ?? -120] }
    Meter { title: qsTr("REDUCTION"); centerX: 211; labels: [0, 1.5, 3, 6, 9, 12, 15, 20, 25, 30, 35, 40, 50, 60]; levelDb: -(root.meters.reductionDb ?? 0); hang: true }
    Meter { title: qsTr("OUTPUT"); centerX: 321; labels: [0, -1.5, -3, -6, -9, -12, -15, -20, -25, -30, -35, -40, -50, -60]; levelDb: root.meters.outDb ?? -120; channelDb: [root.meters.outLDb ?? -120, root.meters.outRDb ?? -120] }

    // the knobs
    component Knob: FxArcKnob {
        id: k
        required property string param
        required property real cxPt
        required property real cyPt
        property real cxOffset: 0
        x: cxPt - width / 2; y: cyPt
        showHeader: true
        readonly property var spec: root.editor.param(param)
        from: spec.min; to: spec.max; defaultValue: spec.def; logarithmic: spec.logarithmic
        value: root.val(param)
        onMoved: (v) => root.editor.edit(param, v)
        onReleased: (v) => root.editor.commit(param, v)
    }
    Knob {
        visible: !root.adaptive
        param: "gain"; cxPt: 482; cyPt: 28; label: qsTr("Gain"); unit: "dB"; bipolar: true; dial: 84; minLabel: "-20"; maxLabel: "20"
    }
    Knob {
        visible: !root.adaptive
        param: "release"; cxPt: 625; cyPt: 28; label: qsTr("Release"); unit: "ms"; dial: 72; minLabel: "2"; maxLabel: "2000"
    }
    Knob {
        visible: !root.adaptive
        param: "ceiling"; cxPt: 760; cyPt: 28; label: qsTr("Output Level"); unit: "dB"; dial: 72; minLabel: "-10"; maxLabel: "1"
    }
    Knob {
        visible: !root.adaptive
        param: "lookahead"; cxPt: 895; cyPt: 28; label: qsTr("Lookahead"); unit: "ms"; dial: 72; minLabel: "0"; maxLabel: "10"
    }
    Knob { visible: root.adaptive; param: "gain"; cxPt: 520; cyPt: 28; label: qsTr("Gain"); unit: "dB"; dial: 84; minLabel: "0"; maxLabel: "12" }
    Knob { visible: root.adaptive; param: "ceiling"; cxPt: 695; cyPt: 28; label: qsTr("Out Ceiling"); unit: "dB"; dial: 84; minLabel: "-2"; maxLabel: "0" }
    Knob { visible: root.adaptive; param: "lookahead"; cxPt: 870; cyPt: 28; label: qsTr("Lookahead"); unit: "ms"; dial: 84; minLabel: "0"; maxLabel: "100"; decimals: 0 }

    // the switches
    component Switch: Item {
        id: sw
        property string label
        property string param
        property real cxPt
        property real cyPt
        readonly property bool on: root.val(param) >= 0.5
        x: cxPt - 110; y: cyPt; width: 220; height: 70
        Text { x: 0; width: parent.width; horizontalAlignment: Text.AlignHCenter; text: sw.label; color: "#9db2c8"; font.family: Theme.fontFamily; font.pixelSize: 19 }
        Rectangle {
            x: (parent.width - 52) / 2; y: 34; width: 52; height: 36; radius: 4
            color: "#12202e"
            Text { anchors.centerIn: parent; text: sw.on ? qsTr("ON") : qsTr("OFF"); color: sw.on ? "#8dffa0" : "#6c8196"; font.family: Theme.fontFamily; font.pixelSize: 21 }
            MouseArea { anchors.fill: parent; onClicked: root.editor.setNow(sw.param, sw.on ? 0 : 1) }
        }
    }
    Switch { visible: !root.adaptive; param: "truePeak"; label: qsTr("True Peak Detection"); cxPt: 625; cyPt: 233 }
    Switch { visible: root.adaptive; param: "removeDc"; label: qsTr("Remove DC Offset"); cxPt: 560; cyPt: 233 }
    Switch { visible: false; param: "truePeak"; label: qsTr("True Peak Detection"); cxPt: 760; cyPt: 233 }
}
