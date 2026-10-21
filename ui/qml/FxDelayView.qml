import QtQuick
import Jad

// Delay: the echoes drawn where they fall (Time apart, each one quieter by Feedback, alternating sides with Ping-Pong) over the knobs in the dark blue style of the dynamics windows.
FxShell {
    id: root
    title: qsTr("Delay")
    bodyWidth: 760
    bodyHeight: 400
    chassis: "#1f3249"
    function val(name) { return editor.valueOf(editor.param(name)) }

    Rectangle { x: 0; y: 0; width: root.bodyWidth; height: 150; color: "#0d1823" }
    Canvas {
        id: taps
        x: 20; y: 10; width: root.bodyWidth - 40; height: 130
        readonly property real time: root.val("time")
        readonly property real feedback: root.val("feedback")
        readonly property real mix: root.val("mix")
        readonly property bool ping: root.val("pingPong") >= 0.5
        onTimeChanged: requestPaint()
        onFeedbackChanged: requestPaint()
        onMixChanged: requestPaint()
        onPingChanged: requestPaint()
        Component.onCompleted: requestPaint()
        onPaint: {
            const c = getContext("2d")
            c.reset()
            const span = 2000                                     // ms across the graph
            c.fillStyle = "#18293a"; c.fillRect(0, height - 1, width, 1)
            let level = 1
            c.fillStyle = "#8dffa0"; c.fillRect(0, 10, 4, height - 20)   // the dry sound
            for (let n = 1; n < 40; ++n) {
                const x = n * time / span * width
                if (x > width) break
                level *= feedback / 100
                const h = (height - 20) * level * (mix / 100 + 0.15)
                if (h < 1) break
                const side = ping ? (n % 2 === 1 ? 0 : 1) : 0.5
                c.fillStyle = ping ? (side === 0 ? "#8dffa0" : "#5cc8ff") : "#8dffa0"
                c.fillRect(x, height - 10 - h, 4, h)
            }
        }
    }
    component Knob: FxArcKnob {
        required property string param
        required property real cxPt
        x: cxPt - width / 2; y: 168
        showHeader: true
        readonly property var spec: root.editor.param(param)
        from: spec.min; to: spec.max; defaultValue: spec.def; logarithmic: spec.logarithmic
        value: root.val(param)
        onMoved: (v) => root.editor.edit(param, v)
        onReleased: (v) => root.editor.commit(param, v)
    }
    Rectangle { x: 0; y: 150; width: root.bodyWidth; height: root.bodyHeight - 150; gradient: Gradient { GradientStop { position: 0; color: "#2a4663" } GradientStop { position: 1; color: "#1d3148" } } }
    Knob { param: "time"; cxPt: 130; label: qsTr("Time"); unit: "ms"; decimals: 0; dial: 72; minLabel: "1"; maxLabel: "2000" }
    Knob { param: "feedback"; cxPt: 290; label: qsTr("Feedback"); unit: "%"; decimals: 0; dial: 72; minLabel: "0"; maxLabel: "95" }
    Knob { param: "mix"; cxPt: 450; label: qsTr("Mix"); unit: "%"; decimals: 0; dial: 72; minLabel: "0"; maxLabel: "100" }
    Item {
        readonly property bool on: root.val("pingPong") >= 0.5
        x: 560; y: 190; width: 160; height: 70
        Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: qsTr("Ping-Pong"); color: "#9db2c8"; font.family: Theme.fontFamily; font.pixelSize: 19 }
        Rectangle {
            x: (parent.width - 52) / 2; y: 34; width: 52; height: 36; radius: 4; color: "#12202e"
            Text { anchors.centerIn: parent; text: parent.parent.on ? qsTr("ON") : qsTr("OFF"); color: parent.parent.on ? "#8dffa0" : "#6c8196"; font.family: Theme.fontFamily; font.pixelSize: 21 }
            MouseArea { anchors.fill: parent; onClicked: root.editor.setNow("pingPong", parent.parent.on ? 0 : 1) }
        }
    }
}
