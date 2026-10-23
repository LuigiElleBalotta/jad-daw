import QtQuick
import Jad

// External MIDI: the instrument that plays a synthesizer outside the computer. The notes of the track go out of a MIDI port on a channel; the window chooses the
// channel (a pad for each of the 16), sends the bank and program of the sound, and keeps the volume and pan that go out as controllers 7 and 10.
FxShell {
    id: root
    title: qsTr("External MIDI")
    bodyWidth: 700
    bodyHeight: 360
    chassis: "#1b2128"
    readonly property color tint: "#7fd1ff"
    function val(name) { return editor.valueOf(editor.param(name)) }
    readonly property int channel: Math.round(val("channel"))

    Rectangle { x: 0; y: 0; width: root.bodyWidth; height: root.bodyHeight; gradient: Gradient { GradientStop { position: 0; color: "#232c35" } GradientStop { position: 1; color: "#141a20" } } }

    Text { x: 28; y: 16; text: qsTr("MIDI Channel"); color: "#8fa4b6"; font.family: Theme.fontFamily; font.pixelSize: 17 }
    Text { x: 438; y: 16; text: qsTr("MIDI Output"); color: "#8fa4b6"; font.family: Theme.fontFamily; font.pixelSize: 17 }
    SelectField {
        objectName: "externalMidiPortView"
        x: 438; y: 44; width: 234
        choices: root.editor.project.midiOutputNames()
        value: root.editor.insert && root.editor.insert.device !== undefined ? root.editor.insert.device : ""
        placeholder: qsTr("No port chosen")
        onChosen: (c) => root.editor.project.setExternalMidiDevice(root.editor.trackId, c)
    }
    Grid {
        x: 28; y: 44; columns: 8; spacing: 8
        Repeater {
            model: 16
            delegate: Rectangle {
                id: pad
                required property int index
                objectName: "externalMidiChannel_" + (index + 1)
                readonly property bool chosen: root.channel === index + 1
                width: 48; height: 40; radius: 6
                color: chosen ? root.tint : "#2c3742"
                border.color: chosen ? "#d6f0ff" : "#3d4b59"; border.width: 1
                Text { anchors.centerIn: parent; text: pad.index + 1; color: pad.chosen ? "#06202f" : "#c3d3e0"; font.family: Theme.fontFamily; font.pixelSize: 19 }
                MouseArea { anchors.fill: parent; onClicked: root.editor.setNow("channel", pad.index + 1) }
            }
        }
    }

    // bank and program: 0 sends nothing, n sends n - 1 (Bank 1 is controller value 0, Program 1 is program 0)
    component Param: FxArcKnob {
        id: k
        required property string name
        required property string caption
        tint: root.tint; labelColor: "#8fa4b6"; showHeader: true
        readonly property var spec: root.editor.param(name)
        from: spec.min; to: spec.max; defaultValue: spec.def
        value: root.val(name)
        label: caption; decimals: 0; dial: 70
        onMoved: (v) => root.editor.edit(name, v)
        onReleased: (v) => { root.editor.commit(name, v); root.editor.project.externalMidiSend(root.editor.trackId, name, v) }
    }
    Param { x: 28; y: 150; name: "bankMsb"; caption: qsTr("Bank MSB"); minLabel: "off"; maxLabel: "128" }
    Param { x: 160; y: 150; name: "bankLsb"; caption: qsTr("Bank LSB"); minLabel: "off"; maxLabel: "128" }
    Param { x: 292; y: 150; name: "program"; caption: qsTr("Program"); minLabel: "off"; maxLabel: "128" }
    Param { x: 440; y: 150; name: "volume"; caption: qsTr("Volume"); minLabel: "0"; maxLabel: "127" }
    Param { x: 570; y: 150; name: "pan"; caption: qsTr("Pan"); minLabel: "0"; maxLabel: "127" }

    Rectangle {
        objectName: "externalMidiTestNote"
        x: 28; y: 290; width: 130; height: 36; radius: 6
        color: testArea.pressed ? root.tint : "#2c3742"; border.color: "#3d4b59"
        Text { anchors.centerIn: parent; text: qsTr("Test Note"); color: testArea.pressed ? "#06202f" : "#c3d3e0"; font.family: Theme.fontFamily; font.pixelSize: 17 }
        MouseArea { id: testArea; anchors.fill: parent; onClicked: root.editor.project.externalMidiTestNote(root.editor.trackId) }
    }
    Text {
        x: 180; y: 292; width: 480; wrapMode: Text.WordWrap
        text: qsTr("Volume and pan go out as controllers 7 and 10, the bank and program when they are set and when the project is loaded (Project Settings > MIDI).")
        color: "#6f8396"; font.family: Theme.fontFamily; font.pixelSize: 14
    }
}
