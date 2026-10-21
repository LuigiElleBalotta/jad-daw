import QtQuick
import Jad

// Neural Saturation: the ten saturation models as a row of keys (the lit one is running), a big Drive knob with Output and Mix beside it.
FxShell {
    id: root
    title: qsTr("Neural Saturation")
    bodyWidth: 760
    bodyHeight: 390
    chassis: "#1c1410"
    readonly property color glow: "#ffa21f"
    readonly property var models: ["RTRST", "RTRHT", "MTC", "MTM", "FEO", "FTO", "FS", "FSQ", "MKBO", "MKGerm"]
    function val(name) { return editor.valueOf(editor.param(name)) }

    Rectangle { x: 0; y: 0; width: root.bodyWidth; height: root.bodyHeight; gradient: Gradient { GradientStop { position: 0; color: "#2a1d14" } GradientStop { position: 1; color: "#150e0a" } } }
    Text { x: 28; y: 16; text: qsTr("Model"); color: "#a8917a"; font.family: Theme.fontFamily; font.pixelSize: 17 }
    Grid {
        x: 28; y: 42; columns: 5; spacing: 8
        Repeater {
            model: root.models
            delegate: Rectangle {
                required property string modelData
                required property int index
                readonly property bool picked: Math.round(root.val("type")) === index
                width: 130; height: 42; radius: 5
                color: picked ? root.glow : "#2d2018"
                Text { anchors.centerIn: parent; text: parent.modelData; color: parent.picked ? "#2a1400" : "#d9b48a"; font.family: Theme.fontFamily; font.pixelSize: 19 }
                MouseArea { anchors.fill: parent; onClicked: root.editor.setNow("type", parent.index) }
            }
        }
    }
    component Knob: FxArcKnob {
        required property string param
        required property real cxPt
        x: cxPt - width / 2; y: 150
        tint: root.glow; labelColor: "#a8917a"; showHeader: true
        readonly property var spec: root.editor.param(param)
        from: spec.min; to: spec.max; defaultValue: spec.def
        value: root.val(param)
        onMoved: (v) => root.editor.edit(param, v)
        onReleased: (v) => root.editor.commit(param, v)
    }
    Knob { param: "drive"; cxPt: 190; label: qsTr("Drive"); unit: "%"; decimals: 0; dial: 80; minLabel: "0"; maxLabel: "100" }
    Knob { param: "output"; cxPt: 400; label: qsTr("Output"); unit: "dB"; decimals: 1; dial: 64; minLabel: "-24"; maxLabel: "12" }
    Knob { param: "mix"; cxPt: 580; label: qsTr("Mix"); unit: "%"; decimals: 0; dial: 64; minLabel: "0"; maxLabel: "100" }
}
