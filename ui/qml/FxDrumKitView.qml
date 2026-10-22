import QtQuick
import Jad

// The Drum Kit: a pad for each General MIDI drum note the kit plays (press one to hear it, harder lower down the pad), and the Level knob.
FxShell {
    id: root
    title: qsTr("Drum Kit")
    bodyWidth: 700
    bodyHeight: 400
    chassis: "#201814"
    readonly property color tint: "#ffb040"
    // note, name, and the sound of the kit that plays it
    readonly property var pads: [
        { note: 49, name: qsTr("Crash"), sound: qsTr("Hi-hat") }, { note: 51, name: qsTr("Ride"), sound: qsTr("Hi-hat") }, { note: 46, name: qsTr("Open Hat"), sound: qsTr("Hi-hat") }, { note: 42, name: qsTr("Closed Hat"), sound: qsTr("Hi-hat") },
        { note: 48, name: qsTr("High Tom"), sound: qsTr("Tom") }, { note: 45, name: qsTr("Mid Tom"), sound: qsTr("Tom") }, { note: 41, name: qsTr("Low Tom"), sound: qsTr("Tom") }, { note: 39, name: qsTr("Clap"), sound: qsTr("Clap") },
        { note: 37, name: qsTr("Rim"), sound: qsTr("Snare") }, { note: 38, name: qsTr("Snare"), sound: qsTr("Snare") }, { note: 40, name: qsTr("Snare 2"), sound: qsTr("Snare") }, { note: 36, name: qsTr("Kick"), sound: qsTr("Kick") }
    ]
    function val(name) { return editor.valueOf(editor.param(name)) }

    Rectangle { x: 0; y: 0; width: root.bodyWidth; height: root.bodyHeight; gradient: Gradient { GradientStop { position: 0; color: "#2d2119" } GradientStop { position: 1; color: "#17100c" } } }
    Text { x: 28; y: 16; text: qsTr("Pads"); color: "#a8917a"; font.family: Theme.fontFamily; font.pixelSize: 17 }
    Grid {
        x: 28; y: 44; columns: 4; spacing: 10
        Repeater {
            model: root.pads
            delegate: Rectangle {
                id: pad
                required property var modelData
                objectName: "drumPad_" + modelData.note
                property bool down: false
                width: 112; height: 92; radius: 8
                color: down ? root.tint : "#3a2b20"
                border.color: down ? "#ffd89a" : "#4f3a2a"; border.width: 1.5
                Text { x: 10; y: 8; text: pad.modelData.note; color: pad.down ? "#3a2200" : "#8a735d"; font.family: Theme.fontFamily; font.pixelSize: 14 }
                Text { anchors.centerIn: parent; text: pad.modelData.name; color: pad.down ? "#2a1400" : "#f0d9bc"; font.family: Theme.fontFamily; font.pixelSize: 21 }
                Text { anchors.horizontalCenter: parent.horizontalCenter; y: parent.height - 24; text: pad.modelData.sound; color: pad.down ? "#5a3a10" : "#7c664f"; font.family: Theme.fontFamily; font.pixelSize: 14 }
                MouseArea {
                    anchors.fill: parent
                    // the velocity follows where the pad is hit: soft at the top, hard at the bottom
                    onPressed: (m) => { pad.down = true; root.editor.project.playNote(pad.modelData.note, Math.round(40 + 87 * Math.max(0, Math.min(1, m.y / pad.height))), true) }
                    onReleased: { pad.down = false; root.editor.project.playNote(pad.modelData.note, 0, false) }
                    onCanceled: { pad.down = false; root.editor.project.playNote(pad.modelData.note, 0, false) }
                }
            }
        }
    }
    FxArcKnob {
        x: 548; y: 100
        tint: root.tint; labelColor: "#a8917a"; showHeader: true
        readonly property var spec: root.editor.param("level")
        from: spec.min; to: spec.max; defaultValue: spec.def
        value: root.val("level")
        label: qsTr("Level"); unit: "dB"; decimals: 1; dial: 80; minLabel: "-24"; maxLabel: "6"
        onMoved: (v) => root.editor.edit("level", v)
        onReleased: (v) => root.editor.commit("level", v)
    }
}
