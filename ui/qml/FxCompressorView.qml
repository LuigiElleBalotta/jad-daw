import QtQuick
import Jad

// Compressor of Logic Pro 11.2: every circuit is a different skin of the same window (Platinum Digital blue metal, Studio VCA red, Studio FET black and amber, Classic
// VCA gunmetal, Vintage VCA silver with blue knobs, Vintage FET silver, Vintage Opto silver with black knobs). The circuit buttons across the top choose both the
// sound and the skin. In the middle a VU meter of the gain reduction that is also a graph of the transfer curve (the Meter and Graph tabs), the six main knobs and the
// Auto Gain keys; at the left the input level and gain, at the right the Limiter, the Distortion, the Mix, the output level and gain.
FxShell {
    id: root
    title: qsTr("Compressor")
    bodyWidth: 1028
    bodyHeight: 649
    withSideChain: true

    // the order of the buttons and the value of the circuit each one sets
    readonly property var circuits: [
        { value: 0, name: "Platinum\nDigital", skin: 0 }, { value: 4, name: "Studio\nVCA", skin: 1 }, { value: 5, name: "Studio\nFET", skin: 2 },
        { value: 8, name: "Classic\nVCA", skin: 3 }, { value: 3, name: "Vintage\nVCA", skin: 4 }, { value: 7, name: "Vintage\nFET", skin: 5 }, { value: 6, name: "Vintage\nOpto", skin: 6 }
    ]
    readonly property var skins: [
        { chassis: ["#33496a", "#243346"], light: false, label: "#d9e2ee", upper: true, knob: "silver", face: ["#b9b9b5", "#8b8b87"], housing: "#171a1f", needle: "#101010", scale: "#2a2a2a", accent: "#4cc2ff", button: "grey", led: "#ff2a2a" },
        { chassis: ["#b8403f", "#6e1520"], light: false, label: "#f6e8cf", upper: false, knob: "chrome", face: ["#b2a45a", "#85772f"], housing: "#7d1b25", needle: "#101010", scale: "#33300f", accent: "#ff4a3a", button: "red", led: "#ff1a1a" },
        { chassis: ["#262626", "#101012"], light: false, label: "#c9c6c0", upper: true, knob: "fet", face: ["#d0a060", "#a8702f"], housing: "#161616", needle: "#101010", scale: "#4a2e10", accent: "#ffbe1a", button: "dark", led: "#ff2a2a" },
        { chassis: ["#2b2a29", "#121213"], light: false, label: "#d4d2cc", upper: true, knob: "ridged", face: ["#bdbdb4", "#8c8c84"], housing: "#161616", needle: "#2c6fc9", scale: "#2d3f55", accent: "#58a6ff", button: "dark", led: "#ff2a2a" },
        { chassis: ["#dad9d6", "#bdbdbf"], light: true, label: "#262626", upper: true, knob: "blue", face: ["#222222", "#171717"], housing: "#171717", needle: "#f2f2f2", scale: "#cfcfcf", accent: "#1e78d6", button: "light", led: "#ff2a2a" },
        { chassis: ["#d4d2cf", "#bdbcc0"], light: true, label: "#262626", upper: true, knob: "fet", face: ["#c8905c", "#a2693a"], housing: "#6e7378", needle: "#101010", scale: "#4a2e10", accent: "#e8780a", button: "light", led: "#ff2a2a" },
        { chassis: ["#cfd0cf", "#bdbebd"], light: true, label: "#262626", upper: true, knob: "ridged", face: ["#e2c88e", "#c4a66e"], housing: "#5d6870", needle: "#101010", scale: "#4a3a1e", accent: "#ff9d1a", button: "light", led: "#ff2a2a" }
    ]
    readonly property int circuit: Math.round(val("circuit"))
    readonly property int current: { for (let i = 0; i < circuits.length; ++i) if (circuits[i].value === circuit) return i; return 0 }
    readonly property var sk: skins[circuits[current].skin]
    property bool graph: false
    readonly property var meters: editor.project.editorMeters
    readonly property real reduction: meters.reductionDb ?? 0

    chassis: sk.chassis[1]
    function val(name) { return editor.valueOf(editor.param(name)) }
    function lbl(t) { return sk.upper ? t.toUpperCase() : t.charAt(0).toUpperCase() + t.slice(1).toLowerCase() }
    function dim(c, a) { const q = Qt.color(c); return Qt.rgba(q.r, q.g, q.b, a) }

    // the chassis
    Rectangle {
        x: 0; y: 0; width: root.bodyWidth; height: root.bodyHeight
        gradient: Gradient { GradientStop { position: 0; color: root.sk.chassis[0] } GradientStop { position: 1; color: root.sk.chassis[1] } }
    }
    Canvas {  // the brushed metal: fine horizontal lines
        x: 0; y: 0; width: root.bodyWidth; height: root.bodyHeight
        readonly property int skinIndex: root.circuits[root.current].skin
        onSkinIndexChanged: requestPaint()
        Component.onCompleted: requestPaint()
        onPaint: {
            const c = getContext("2d")
            c.reset()
            let seed = 7
            const rnd = () => { seed = (seed * 16807) % 2147483647; return seed / 2147483647 }
            for (let y = 0; y < height; y += 1) {
                const a = rnd() * 0.09
                c.fillStyle = root.sk.light ? "rgba(0,0,0," + a * 0.5 + ")" : "rgba(255,255,255," + a + ")"
                c.fillRect(0, y, width, 1)
            }
        }
    }
    // the framed groups of the light skins
    Repeater {
        model: root.sk.light ? [{ x: 10, y: 80, w: 120, h: 568 }, { x: 140, y: 80, w: 610, h: 568 }, { x: 765, y: 80, w: 255, h: 568 }] : []
        delegate: Rectangle { required property var modelData; x: modelData.x; y: modelData.y; width: modelData.w; height: modelData.h; radius: 14; color: "transparent"; border.color: "#8a8a8c"; border.width: 2 }
    }

    // the circuit buttons
    Rectangle {
        x: 146; y: 17; width: 584; height: 56; radius: 7; color: root.dim("#000000", root.sk.light ? 0.22 : 0.5)
    }
    Repeater {
        model: root.circuits
        delegate: Rectangle {
            id: cb
            required property var modelData
            required property int index
            readonly property bool picked: root.current === index && (root.circuit === modelData.value)
            x: 149 + index * 83.6; y: 20; width: 81; height: 50; radius: 5
            objectName: "compCircuit_" + modelData.value
            color: {
                if (root.sk.button === "light") return picked ? "#8a8c8e" : "#e9e9ea"
                if (root.sk.button === "red") return picked ? "#ff3c36" : "#a31622"
                if (root.sk.button === "dark") return picked ? "#1c1c1c" : "#343434"
                return picked ? "#585d63" : "#aaaaaa"
            }
            border.color: root.sk.button === "red" ? "#5a0a12" : "#1d1d1d"
            Text {
                anchors.centerIn: parent
                text: cb.modelData.name
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 0.95
                color: cb.picked ? (root.sk.button === "light" ? root.sk.accent : (root.sk.button === "grey" ? "#59c6ff" : (root.sk.button === "red" ? "#fff0d8" : root.sk.accent))) : (root.sk.button === "red" ? "#ffb0a6" : (root.sk.button === "dark" ? "#7d7d7d" : "#333333"))
                font.family: Theme.fontFamily; font.pixelSize: 15
            }
            MouseArea { anchors.fill: parent; onClicked: root.editor.setNow("circuit", cb.modelData.value) }
        }
    }
    // Side Chain and Output
    Row {
        x: 786; y: 25
        Repeater {
            model: [qsTr("Side Chain"), qsTr("Output")]
            delegate: Rectangle {
                required property string modelData
                required property int index
                width: 83; height: 38; radius: 4
                color: index === 1 ? (root.sk.button === "red" ? "#f4a60a" : (root.sk.light ? "#838587" : "#10141a")) : (root.sk.light ? "#e9e9ea" : (root.sk.button === "red" ? "#704a08" : "#9a9a9a"))
                border.color: "#1d1d1d"
                opacity: index === 0 ? 0.8 : 1
                Text { anchors.centerIn: parent; text: parent.modelData; color: index === 1 ? (root.sk.button === "red" ? "#2a1800" : root.sk.accent === "#4cc2ff" ? "#59c6ff" : (root.sk.light ? root.sk.accent : "#ffffff")) : (root.sk.light ? "#333" : "#303030"); font.family: Theme.fontFamily; font.pixelSize: 15 }
            }
        }
    }

    // the VU meter
    Rectangle {
        x: 150.6; y: 100; width: 588; height: 229; color: root.sk.housing; border.color: "#0a0a0a"
    }
    Rectangle {
        id: faceBox
        x: 170.6; y: 119; width: 549; height: 189
        gradient: Gradient { GradientStop { position: 0; color: root.sk.face[0] } GradientStop { position: 1; color: root.sk.face[1] } }
        border.color: "#0d0d0d"
        clip: true
        Canvas {
            id: vu
            anchors.fill: parent
            readonly property real reduction: root.reduction
            readonly property bool graphMode: root.graph
            readonly property int skinIndex: root.circuits[root.current].skin
            readonly property real threshold: root.val("threshold")
            readonly property real ratio: root.val("ratio")
            readonly property real knee: root.val("knee")
            readonly property real gain: root.val("gain")
            readonly property real auto: root.val("autoGain")
            onReductionChanged: requestPaint()
            onGraphModeChanged: requestPaint()
            onSkinIndexChanged: requestPaint()
            onThresholdChanged: requestPaint()
            onRatioChanged: requestPaint()
            onKneeChanged: requestPaint()
            onGainChanged: requestPaint()
            onAutoChanged: requestPaint()
            Component.onCompleted: requestPaint()
            // the scale: value in dB of gain reduction against its place along the arc (0 to 1 from left to right)
            readonly property var marks: [[-50, 0], [-30, 0.17], [-20, 0.33], [-10, 0.5], [-5, 0.78], [0, 1]]
            function place(db) {
                if (db <= -50) return 0
                for (let i = 0; i + 1 < marks.length; ++i)
                    if (db <= marks[i + 1][0]) return marks[i][1] + (db - marks[i][0]) / (marks[i + 1][0] - marks[i][0]) * (marks[i + 1][1] - marks[i][1])
                return 1
            }
            onPaint: {
                const c = getContext("2d")
                c.reset()
                if (graphMode) { paintGraph(c); return }
                const ox = -170.6, oy = -119                      // window coordinates of the face: the numbers below are those of the real window
                const cxArc = 443.6 + ox, cyArc = 597.7 + oy, R = 360.7
                const half = Math.asin(170.8 / R)
                const at = (t, r) => { const a = (t - 0.5) * 2 * half; return [cxArc + Math.sin(a) * r, cyArc - Math.cos(a) * r] }
                c.strokeStyle = root.sk.scale
                c.fillStyle = root.sk.scale
                c.lineWidth = 2
                c.beginPath(); for (let i = 0; i <= 60; ++i) { const p = at(i / 60, R); if (i === 0) c.moveTo(p[0], p[1]); else c.lineTo(p[0], p[1]) } c.stroke()
                // the ticks: long at the numbers, short between
                const longs = [-50, -30, -20, -10, -5, 0]
                for (let db = -50; db <= 0; db += (db < -10 ? 5 : 1)) {
                    if (db < -50) continue
                    const isLong = longs.indexOf(db) >= 0
                    const p1 = at(place(db), R), p2 = at(place(db), R + (isLong ? 26 : 12))
                    c.beginPath(); c.moveTo(p1[0], p1[1]); c.lineTo(p2[0], p2[1]); c.stroke()
                }
                c.font = "26px sans-serif"
                for (const [db, t] of [[-50, "-50"], [-30, "-30"], [-20, "-20"], [-10, "-10"], [-5, "-5"], [0, "0"]]) {
                    const p = at(place(db), R + 50)
                    c.fillText(t, p[0] - c.measureText(t).width / 2, p[1] + 8)
                }
                // the needle: a reduction of 0 is at the right, the more is taken off the more it swings left
                const shown = Math.max(-50, -reduction)
                const t = place(shown)
                const pivot = [404 + ox, 600 + oy]
                const tip = at(t, R + 14)
                const dx = tip[0] - pivot[0], dy = tip[1] - pivot[1], len = Math.sqrt(dx * dx + dy * dy)
                c.strokeStyle = root.sk.needle
                c.lineWidth = 3
                c.beginPath(); c.moveTo(pivot[0] + dx * 0.78, pivot[1] + dy * 0.78); c.lineTo(tip[0], tip[1]); c.stroke()
            }
            function paintGraph(c) {
                // the transfer curve: input against output, -60 to 0 dB on both
                const m = 14, w = width - 2 * m, h = height - 2 * m
                const px = (v) => m + (v + 60) / 60 * w, py = (v) => m + h - (v + 60) / 60 * h
                c.lineWidth = 1
                c.strokeStyle = root.dim(root.sk.scale, 0.35)
                c.fillStyle = root.sk.scale
                c.font = "11px sans-serif"
                for (let d = -60; d <= 0; d += 12) { c.beginPath(); c.moveTo(px(d), m); c.lineTo(px(d), m + h); c.stroke(); c.beginPath(); c.moveTo(m, py(d)); c.lineTo(m + w, py(d)); c.stroke(); c.fillText(String(d), px(d) + 2, m + h - 2) }
                c.strokeStyle = root.dim(root.sk.scale, 0.6)
                c.beginPath(); c.moveTo(px(-60), py(-60)); c.lineTo(px(0), py(0)); c.stroke()
                c.strokeStyle = root.sk.scale
                c.lineWidth = 2.5
                c.beginPath()
                const t = threshold, r = Math.max(1, ratio), kw = knee * 20
                let makeup = gain
                if (auto >= 0.5) makeup += (1 - 1 / r) * Math.max(0, (auto >= 1.5 ? -12 : 0) - t)
                for (let i = 0; i <= 120; ++i) {
                    const x = -60 + i / 120 * 60
                    let y = x
                    if (2 * (x - t) > kw) y = t + (x - t) / r
                    else if (kw > 0 && 2 * Math.abs(x - t) <= kw) y = x + (1 / r - 1) * (x - t + kw / 2) * (x - t + kw / 2) / (2 * kw)
                    y = Math.max(-60, Math.min(0, y + makeup))
                    if (i === 0) c.moveTo(px(x), py(y)); else c.lineTo(px(x), py(y))
                }
                c.stroke()
            }
        }
    }
    Row {  // Meter | Graph
        x: 325; y: 129.5
        Repeater {
            model: [qsTr("Meter"), qsTr("Graph")]
            delegate: Rectangle {
                required property string modelData
                required property int index
                readonly property bool picked: (index === 1) === root.graph
                width: 120.5; height: 27
                color: picked ? root.dim("#2a2218", 0.82) : "#f1efe6"
                border.color: "#1d1d1d"
                Text { anchors.centerIn: parent; text: parent.modelData; color: parent.picked ? "#ffffff" : "#2a2a2a"; font.family: Theme.fontFamily; font.pixelSize: 20 }
                MouseArea { anchors.fill: parent; onClicked: root.graph = index === 1 }
            }
        }
    }

    // the main knobs
    readonly property var stopsThreshold: [{ v: -50, label: "-50" }, { v: -40, label: "-40" }, { v: -30, label: "-30" }, { v: -20, label: "-20" }, { v: -10, label: "-10" }, { v: 0, label: "0" }]
    readonly property var stopsRatio: [{ v: 1, label: "1" }, { v: 1.4, label: "1.4" }, { v: 2, label: "2" }, { v: 3, label: "3" }, { v: 5, label: "5" }, { v: 8, label: "8" }, { v: 12, label: "12" }, { v: 20, label: "20" }, { v: 30, label: "30" }]
    readonly property var stopsMakeUp: [{ v: -20, label: "-20" }, { v: -15, label: "-15" }, { v: -10, label: "-10" }, { v: -5, label: "-5" }, { v: 0, label: "0" }, { v: 5, label: "5" }, { v: 10, label: "10" }, { v: 15, label: "15" }, { v: 20, label: "20" }, { v: 30, label: "30" }, { v: 40, label: "40" }, { v: 50, label: "50" }]
    readonly property var stopsKnee: [{ v: 0, label: "0" }, { v: 0.2, label: "0.2" }, { v: 0.4, label: "0.4" }, { v: 0.6, label: "0.6" }, { v: 0.8, label: "0.8" }, { v: 1, label: "1.0" }]
    readonly property var stopsAttack: [{ v: 0, label: "0" }, { v: 5, label: "5" }, { v: 10, label: "10" }, { v: 15, label: "15" }, { v: 20, label: "20" }, { v: 50, label: "50" }, { v: 80, label: "80" }, { v: 120, label: "120" }, { v: 160, label: "160" }, { v: 200, label: "200" }]
    readonly property var stopsRelease: [{ v: 5, label: "5" }, { v: 10, label: "10" }, { v: 20, label: "20" }, { v: 50, label: "50" }, { v: 100, label: "100" }, { v: 200, label: "200" }, { v: 500, label: "500" }, { v: 1000, label: "1k" }, { v: 2000, label: "2k" }, { v: 5000, label: "5k" }]
    readonly property var stopsGain: [{ v: -30, label: "-30" }, { v: -20 }, { v: -10 }, { v: 0, label: "0" }, { v: 10 }, { v: 20 }, { v: 30, label: "30" }]
    readonly property var stopsLimiter: [{ v: -10, label: "-10" }, { v: -8, label: "-8" }, { v: -6, label: "-6" }, { v: -4, label: "-4" }, { v: -2, label: "-2" }, { v: 0, label: "0" }]
    readonly property var stopsMix: [{ v: 0, label: "Input" }, { v: 25 }, { v: 50, label: "1:1" }, { v: 75 }, { v: 100, label: "Output" }]
    readonly property var stopsDistortion: [{ v: 0, label: "Off" }, { v: 1, label: "Soft" }, { v: 2, label: "Hard" }, { v: 3, label: "Clip" }]

    component KnobAt: FxSkinKnob {
        id: k
        required property string param
        required property real cxPt
        required property real cyPt
        property bool whole: false
        x: cxPt - width / 2; y: cyPt - height / 2
        style: root.sk.knob
        labelColor: root.sk.label
        tickColor: root.sk.label
        readonly property var spec: root.editor.param(param)
        defaultValue: spec.def
        value: root.val(param)
        onMoved: (v) => root.editor.edit(param, whole ? Math.round(v) : v)
        onReleased: (v) => root.editor.commit(param, whole ? Math.round(v) : v)
    }
    component Cap: Text {
        property real cxPt
        property real cyPt
        x: cxPt - width / 2; y: cyPt - height / 2
        color: root.sk.label
        font.family: Theme.fontFamily
        font.pixelSize: 17
    }

    KnobAt { param: "threshold"; cxPt: 245.7; cyPt: 437; stops: root.stopsThreshold; unit: "dB"; dial: 80 }
    KnobAt { param: "ratio"; cxPt: 406; cyPt: 437; stops: root.stopsRatio; unit: ":1"; dial: 80 }
    KnobAt { param: "gain"; cxPt: 564; cyPt: 437; stops: root.stopsMakeUp; unit: "dB"; dial: 80 }
    KnobAt { param: "knee"; cxPt: 244; cyPt: 596; stops: root.stopsKnee; dial: 60 }
    KnobAt { param: "attack"; cxPt: 405; cyPt: 596; stops: root.stopsAttack; unit: "ms"; dial: 60 }
    KnobAt { param: "release"; cxPt: 564; cyPt: 596; stops: root.stopsRelease; unit: "ms"; dial: 60 }
    KnobAt { param: "inputGain"; cxPt: 67; cyPt: 596; stops: root.stopsGain; unit: "dB"; dial: 60 }
    KnobAt { param: "outputGain"; cxPt: 950; cyPt: 596; stops: root.stopsGain; unit: "dB"; dial: 60 }
    KnobAt { param: "mix"; cxPt: 835; cyPt: 596; stops: root.stopsMix; dial: 60 }
    KnobAt { param: "limiterThreshold"; cxPt: 835; cyPt: 249; stops: root.stopsLimiter; unit: "dB"; dial: 60 }
    KnobAt { param: "outputDistortion"; cxPt: 835; cyPt: 444; stops: root.stopsDistortion; dial: 60; whole: true }
    Cap { text: root.lbl(qsTr("Threshold")); cxPt: 244; cyPt: 353 }
    Cap { text: root.lbl(qsTr("Ratio")); cxPt: 406; cyPt: 353 }
    Cap { text: root.lbl(qsTr("Make Up")); cxPt: 568; cyPt: 353 }
    Cap { text: root.lbl(qsTr("Knee")); cxPt: 244; cyPt: 520 }
    Cap { text: root.lbl(qsTr("Attack")); cxPt: 405; cyPt: 520 }
    Cap { text: root.lbl(qsTr("Release")); cxPt: 565; cyPt: 520 }
    Cap { text: root.lbl(qsTr("Input Gain")); cxPt: 67; cyPt: 520 }
    Cap { text: root.lbl(qsTr("Output Gain")); cxPt: 950; cyPt: 520 }
    Cap { text: root.lbl(qsTr("Mix")); cxPt: 835; cyPt: 520 }
    Cap { text: root.lbl(qsTr("Threshold")); cxPt: 833; cyPt: 186 }
    Cap { text: root.lbl(qsTr("Distortion")); cxPt: 835; cyPt: 373 }
    Cap { text: root.lbl(qsTr("Auto Gain")); cxPt: 685; cyPt: 356 }
    Cap { text: root.lbl(qsTr("Limiter")); cxPt: 832; cyPt: 109 }

    // the keys
    component Key: Rectangle {
        id: k
        property string label
        property bool lit: false
        property real cxPt
        property real cyPt
        property bool withLed: false
        signal clicked()
        readonly property bool round: root.sk.button === "red"
        x: cxPt - width / 2; y: cyPt - height / 2
        width: round ? 38 : 51; height: round ? 38 : 35
        color: {
            if (root.sk.button === "light") return "#ececec"
            if (root.sk.button === "red") return lit ? "#ff1a1a" : "#7a0c14"
            if (root.sk.button === "dark") return "#1a1a1a"
            return "#a8a8a8"
        }
        border.color: "#1d1d1d"
        radius: round ? width / 2 : 4
        Text {  // the red skin: the name beside the round key; the light skin: under it, with a LED at its side; the others: on it
            x: k.round ? k.width + 8 : (k.width - width) / 2
            y: k.round ? (k.height - height) / 2 : (root.sk.light ? k.height + 2 : (k.height - height) / 2)
            text: k.label
            color: k.round ? root.sk.label : (root.sk.light ? "#262626" : (k.lit ? root.sk.accent : "#6e6e6e"))
            font.family: Theme.fontFamily; font.pixelSize: 15
        }
        Rectangle {
            visible: root.sk.light
            x: k.width + 6; y: (k.height - 14) / 2; width: 14; height: 14; radius: 7
            color: k.lit ? root.sk.led : "#4a1010"; border.color: "#1d1d1d"
        }
        Text {
            visible: false
            anchors.centerIn: parent
            text: k.label
            color: k.lit ? (root.sk.button === "light" ? "#e8780a" : root.sk.accent) : (root.sk.light ? "#444" : (root.sk.button === "dark" ? "#6e6e6e" : "#3a3a3a"))
            font.family: Theme.fontFamily; font.pixelSize: 15
        }
        MouseArea { anchors.fill: parent; onClicked: k.clicked() }
    }
    // Auto Gain: Off, 0 dB, -12 dB; Auto (the release)
    Repeater {
        model: [{ label: qsTr("Off"), v: 0, y: 395 }, { label: qsTr("0 dB"), v: 1, y: 445 }, { label: qsTr("-12 dB"), v: 2, y: 495 }]
        delegate: Key {
            required property var modelData
            label: modelData.label
            cxPt: 686; cyPt: modelData.y
            lit: Math.round(root.val("autoGain")) === modelData.v
            onClicked: root.editor.setNow("autoGain", modelData.v)
        }
    }
    Key { label: qsTr("Auto"); cxPt: 686; cyPt: 599; lit: root.val("autoRelease") >= 0.5; onClicked: root.editor.setNow("autoRelease", root.val("autoRelease") >= 0.5 ? 0 : 1) }
    Key { label: qsTr("ON"); cxPt: 834; cyPt: 150; lit: root.val("limiter") >= 0.5; onClicked: root.editor.setNow("limiter", root.val("limiter") >= 0.5 ? 0 : 1) }
    Rectangle {  // the Limiter LED
        x: 876 - 8; y: 108 - 8; width: 16; height: 16; radius: 8
        color: root.val("limiter") >= 0.5 ? root.sk.led : "#3a0a0a"
        border.color: "#1a0505"
    }

    // the level meters
    component LevelMeter: Item {
        id: lm
        property real levelDb: -120
        property real holdDb: -120
        property real centerX
        property bool rightScale: false
        readonly property var labels: [3, 0, -3, -6, -9, -12, -18, -24, -30, -40, -60]
        function frac(db) {
            if (db >= 3) return 1
            if (db <= -60) return 0
            for (let i = 0; i + 1 < labels.length; ++i)
                if (db >= labels[i + 1]) return 1 - (i + (labels[i] - db) / (labels[i] - labels[i + 1])) / (labels.length - 1)
            return 0
        }
        x: 0; y: 0; width: root.bodyWidth; height: root.bodyHeight
        Rectangle {  // the readout
            x: centerX - 28; y: 95; width: 56; height: 21
            color: root.dim("#000000", 0.12); border.color: root.sk.light ? "#5d5d5d" : "#6e6e6e"
            Text { anchors.centerIn: parent; text: lm.holdDb <= -100 ? "-∞ dB" : lm.holdDb.toFixed(1) + " dB"; color: root.sk.light ? "#222" : "#d8d8d8"; font.family: Theme.fontFamily; font.pixelSize: 14 }
        }
        Repeater {
            model: lm.labels
            delegate: Text {
                required property int index
                required property int modelData
                x: lm.rightScale ? lm.centerX + 14 : lm.centerX - 52
                y: 128 + index * 35 - height / 2 + 0
                width: 28
                horizontalAlignment: lm.rightScale ? Text.AlignLeft : Text.AlignRight
                text: modelData > 0 ? "+" + modelData : String(modelData)
                color: root.sk.light ? "#333" : "#9aa4b0"
                font.family: Theme.fontFamily; font.pixelSize: 13
            }
        }
        Repeater {
            model: 2
            delegate: Item {
                required property int index
                x: lm.centerX - 14 + index * 13; y: 128; width: 10; height: 350
                Rectangle { anchors.fill: parent; color: "#0a0a0a"; border.color: "#2a2a2a" }
                Rectangle {
                    width: parent.width - 2; x: 1
                    height: Math.max(0, lm.frac(lm.levelDb) * parent.height)
                    y: parent.height - height
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#ff3a2a" }
                        GradientStop { position: 0.18; color: "#ffd21a" }
                        GradientStop { position: 0.45; color: "#4fe05a" }
                        GradientStop { position: 1.0; color: "#2fb04a" }
                    }
                }
            }
        }
    }
    LevelMeter { levelDb: root.meters.inDb ?? -120; holdDb: root.meters.inDb ?? -120; centerX: 66 }
    LevelMeter { levelDb: root.meters.outDb ?? -120; holdDb: root.meters.outDb ?? -120; centerX: 923; rightScale: true }
}
