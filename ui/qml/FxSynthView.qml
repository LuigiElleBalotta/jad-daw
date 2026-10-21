import QtQuick
import Jad

// The built-in synth: the four waveforms as keys that show their shape, the envelope drawn from Attack, Decay, Sustain and Release, and the knobs.
FxShell {
    id: root
    title: qsTr("Synth")
    bodyWidth: 860
    bodyHeight: 510
    chassis: "#14202e"
    readonly property color tint: "#58d6a0"
    function val(name) { return editor.valueOf(editor.param(name)) }

    Rectangle { x: 0; y: 0; width: root.bodyWidth; height: root.bodyHeight; gradient: Gradient { GradientStop { position: 0; color: "#1c2e42" } GradientStop { position: 1; color: "#0e1822" } } }

    // oscillator
    Text { x: 28; y: 16; text: qsTr("Oscillator"); color: "#8da2b8"; font.family: Theme.fontFamily; font.pixelSize: 17 }
    Row {
        x: 28; y: 44; spacing: 10
        Repeater {
            model: [qsTr("Sine"), qsTr("Triangle"), qsTr("Saw"), qsTr("Square")]
            delegate: Rectangle {
                id: wk
                required property string modelData
                required property int index
                readonly property bool picked: Math.round(root.val("wave")) === index
                width: 94; height: 78; radius: 6
                color: picked ? "#1f4a3c" : "#14232f"
                border.color: picked ? root.tint : "#22364a"
                Canvas {
                    x: 12; y: 10; width: 70; height: 38
                    readonly property bool lit: wk.picked
                    onLitChanged: requestPaint()
                    Component.onCompleted: requestPaint()
                    onPaint: {
                        const c = getContext("2d")
                        c.reset()
                        c.strokeStyle = lit ? "#58d6a0" : "#5c7a96"; c.lineWidth = 2
                        c.beginPath()
                        for (let x = 0; x <= width; ++x) {
                            const t = (x / width) * 2 % 1
                            let v
                            switch (wk.index) {
                                case 0: v = Math.sin(t * 2 * Math.PI); break
                                case 1: v = t < 0.5 ? (t * 4 - 1) : (3 - t * 4); break
                                case 2: v = 1 - 2 * t; break
                                default: v = t < 0.5 ? 1 : -1; break
                            }
                            const y = height / 2 - v * (height / 2 - 2)
                            if (x === 0) c.moveTo(x, y); else c.lineTo(x, y)
                        }
                        c.stroke()
                    }
                }
                Text { anchors.horizontalCenter: parent.horizontalCenter; y: 54; text: wk.modelData; color: wk.picked ? "#d8f6ea" : "#8da2b8"; font.family: Theme.fontFamily; font.pixelSize: 16 }
                MouseArea { anchors.fill: parent; onClicked: root.editor.setNow("wave", wk.index) }
            }
        }
    }

    // envelope
    Text { x: 28; y: 150; text: qsTr("Envelope"); color: "#8da2b8"; font.family: Theme.fontFamily; font.pixelSize: 17 }
    Canvas {
        id: env
        x: 28; y: 176; width: 804; height: 120
        readonly property real a: root.val("attack")
        readonly property real d: root.val("decay")
        readonly property real s: root.val("sustain")
        readonly property real r: root.val("release")
        onAChanged: requestPaint()
        onDChanged: requestPaint()
        onSChanged: requestPaint()
        onRChanged: requestPaint()
        Component.onCompleted: requestPaint()
        onPaint: {
            const c = getContext("2d")
            c.reset()
            c.fillStyle = "#0d1823"; c.fillRect(0, 0, width, height)
            // the stages share the width by the square root of their times, so a 5 ms attack is as visible as a 2 s release
            const wa = Math.sqrt(a), wd = Math.sqrt(d), wr = Math.sqrt(r), hold = 40
            const total = wa + wd + hold + wr
            const px = (v) => v / total * (width - 20) + 10
            const top = 10, bottom = height - 10, sy = bottom - (bottom - top) * s / 100
            c.beginPath()
            c.moveTo(10, bottom); c.lineTo(px(wa), top); c.lineTo(px(wa + wd), sy); c.lineTo(px(wa + wd + hold), sy); c.lineTo(px(total), bottom)
            c.strokeStyle = "#58d6a0"; c.lineWidth = 2.5; c.stroke()
            c.lineTo(10, bottom); c.closePath(); c.fillStyle = "rgba(88,214,160,0.14)"; c.fill()
        }
    }

    component Knob: FxArcKnob {
        required property string param
        required property real cxPt
        x: cxPt - width / 2; y: 308
        tint: root.tint; showHeader: true
        readonly property var spec: root.editor.param(param)
        from: spec.min; to: spec.max; defaultValue: spec.def; logarithmic: spec.logarithmic
        value: root.val(param)
        onMoved: (v) => root.editor.edit(param, v)
        onReleased: (v) => root.editor.commit(param, v)
    }
    Knob { param: "attack"; cxPt: 90; label: qsTr("Attack"); unit: "ms"; decimals: 0; dial: 56; minLabel: "0.5"; maxLabel: "2 k" }
    Knob { param: "decay"; cxPt: 235; label: qsTr("Decay"); unit: "ms"; decimals: 0; dial: 56; minLabel: "1"; maxLabel: "5 k" }
    Knob { param: "sustain"; cxPt: 380; label: qsTr("Sustain"); unit: "%"; decimals: 0; dial: 56; minLabel: "0"; maxLabel: "100" }
    Knob { param: "release"; cxPt: 525; label: qsTr("Release"); unit: "ms"; decimals: 0; dial: 56; minLabel: "1"; maxLabel: "5 k" }
    Knob { param: "cutoff"; cxPt: 670; label: qsTr("Cutoff"); unit: "Hz"; decimals: 0; dial: 56; minLabel: "100"; maxLabel: "20 k" }
    Knob { param: "level"; cxPt: 800; label: qsTr("Level"); unit: "dB"; decimals: 1; dial: 56; minLabel: "-24"; maxLabel: "6" }
}
