import QtQuick
import Jad

// Reverb in the look of Logic's ChromaVerb (orange on dark, the light strip of knobs under the graph). The graph shows what the settings do: the damping curve (how long
// each frequency rings, from Damping) over the decay of the room (Room Size) with the pre-delay as the empty stretch before it.
FxShell {
    id: root
    title: qsTr("Reverb")
    bodyWidth: 900
    bodyHeight: 460
    chassis: "#16181c"
    readonly property color orange: "#ff8a1a"
    function val(name) { return editor.valueOf(editor.param(name)) }

    Rectangle { x: 0; y: 0; width: root.bodyWidth; height: 290; gradient: Gradient { GradientStop { position: 0; color: "#1b1d22" } GradientStop { position: 1; color: "#101216" } } }
    Text { x: 24; y: 14; text: qsTr("DAMPING EQ"); color: "#8a8d94"; font.family: Theme.fontFamily; font.pixelSize: 16 }
    Rectangle {
        x: root.bodyWidth / 2 - 90; y: 8; width: 180; height: 34; radius: 6; color: "#25272d"
        Text { anchors.centerIn: parent; text: qsTr("Room"); color: root.orange; font.family: Theme.fontFamily; font.pixelSize: 22 }
    }
    Item {
        id: graph
        x: 40; y: 56; width: 820; height: 220
        Repeater { model: 9; delegate: Rectangle { required property int index; x: 0; y: index * graph.height / 8; width: graph.width; height: 1; color: "#23252b" } }
        Repeater { model: [20, 100, 1000, 10000]; delegate: Rectangle { required property real modelData; x: graph.width * Math.log(modelData / 20) / Math.log(1000); width: 1; height: graph.height; color: "#23252b" } }
        Canvas {
            id: damp
            anchors.fill: parent
            readonly property real size: root.val("size")
            readonly property real damping: root.val("damping")
            readonly property real predelay: root.val("predelay")
            onSizeChanged: requestPaint()
            onDampingChanged: requestPaint()
            onPredelayChanged: requestPaint()
            onWidthChanged: requestPaint()
            Component.onCompleted: requestPaint()
            onPaint: {
                const c = getContext("2d")
                c.reset()
                // decay time against frequency: the high end dies sooner the more damping there is
                c.beginPath()
                for (let px = 0; px <= width; px += 3) {
                    const t = px / width                                  // 0 = 20 Hz, 1 = 20 kHz
                    const rel = 1 - (damping / 100) * Math.pow(t, 1.6) * 0.85
                    const y = height - height * (0.25 + 0.7 * rel * (0.35 + size / 100 * 0.65))
                    if (px === 0) c.moveTo(px, y); else c.lineTo(px, y)
                }
                c.strokeStyle = "#ff8a1a"; c.lineWidth = 2.5; c.stroke()
                c.lineTo(width, height); c.lineTo(0, height); c.closePath()
                c.fillStyle = "rgba(255,138,26,0.12)"; c.fill()
            }
        }
        Text { x: 4; y: graph.height - 22; text: "20"; color: "#5a5d64"; font.family: Theme.fontFamily; font.pixelSize: 13 }
        Text { x: graph.width - 36; y: graph.height - 22; text: "20k"; color: "#5a5d64"; font.family: Theme.fontFamily; font.pixelSize: 13 }
    }

    Rectangle { x: 0; y: 290; width: root.bodyWidth; height: root.bodyHeight - 290; color: "#c9ccd1" }
    component Knob: FxArcKnob {
        required property string param
        required property real cxPt
        x: cxPt - width / 2; y: 296
        tint: "#d9650a"; labelColor: "#2c2f35"; showHeader: true
        readonly property var spec: root.editor.param(param)
        from: spec.min; to: spec.max; defaultValue: spec.def; logarithmic: spec.logarithmic
        value: root.val(param)
        onMoved: (v) => root.editor.edit(param, v)
        onReleased: (v) => root.editor.commit(param, v)
    }
    Knob { param: "size"; cxPt: 110; label: qsTr("Size"); unit: "%"; decimals: 0; dial: 60; minLabel: "0"; maxLabel: "100" }
    Knob { param: "damping"; cxPt: 270; label: qsTr("Damping"); unit: "%"; decimals: 0; dial: 60; minLabel: "0"; maxLabel: "100" }
    Knob { param: "width"; cxPt: 430; label: qsTr("Width"); unit: "%"; decimals: 0; dial: 60; minLabel: "0"; maxLabel: "100" }
    Knob { param: "predelay"; cxPt: 590; label: qsTr("Pre-Delay"); unit: "ms"; decimals: 0; dial: 60; minLabel: "0"; maxLabel: "200" }
    Knob { param: "mix"; cxPt: 770; label: qsTr("Mix"); unit: "%"; decimals: 0; dial: 60; minLabel: "0"; maxLabel: "100" }
}
