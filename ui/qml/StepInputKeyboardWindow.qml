import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import Jad

// Window > Show Step Input Keyboard: each key puts a note of the chosen length at the playhead (in the selected MIDI region, or a new one) and moves the
// playhead on. Chord keeps it where it is so the next keys join the same step; Rest and Back move it by a step without a note.
Window {
    id: root
    required property ProjectController project
    property int baseNote: 48                    // the C of the first key
    property int velocity: 90
    property real step: 0.25                     // beats
    property bool chord: false
    readonly property var steps: [{ label: "1/4", beats: 1 }, { label: "1/8", beats: 0.5 }, { label: "1/16", beats: 0.25 }, { label: "1/32", beats: 0.125 }, { label: "1/2", beats: 2 }, { label: "1/1", beats: 4 }]

    function enter(note) {
        project.playNote(note, velocity, true)   // heard while it is entered
        releaseTimer.note = note
        releaseTimer.restart()
        project.stepInputNote(note, velocity, step, chord)
    }

    width: 640
    height: 230
    minimumWidth: 480
    minimumHeight: 180
    title: qsTr("Step Input Keyboard")
    color: Theme.surfacePanel
    flags: Qt.Tool | Qt.WindowStaysOnTopHint

    Timer { id: releaseTimer; property int note: 0; interval: 250; onTriggered: root.project.playNote(note, 0, false) }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing[3]
        spacing: Theme.spacing[2]
        RowLayout {
            spacing: Theme.spacing[2]
            Text { text: qsTr("Step"); color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            Repeater {
                model: root.steps
                delegate: IconButton {
                    required property var modelData
                    objectName: "step_" + modelData.label
                    implicitHeight: 24
                    label: modelData.label
                    active: Math.abs(root.step - modelData.beats) < 1e-9
                    onClicked: root.step = modelData.beats
                }
            }
            Item { Layout.fillWidth: true }
            IconButton { objectName: "chordButton"; implicitHeight: 24; label: qsTr("Chord"); active: root.chord; onClicked: root.chord = !root.chord }
            IconButton { objectName: "backButton"; implicitHeight: 24; label: qsTr("Back"); onClicked: root.project.stepInputMove(-root.step) }
            IconButton { objectName: "restButton"; implicitHeight: 24; label: qsTr("Rest"); onClicked: root.project.stepInputMove(root.step) }
        }
        RowLayout {
            spacing: Theme.spacing[2]
            Text { text: qsTr("Octave C%1").arg(Math.floor(root.baseNote / 12) - 2); color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            IconButton { implicitHeight: 24; label: "-"; onClicked: root.baseNote = Math.max(0, root.baseNote - 12) }
            IconButton { implicitHeight: 24; label: "+"; onClicked: root.baseNote = Math.min(96, root.baseNote + 12) }
            Text { text: qsTr("Velocity %1").arg(root.velocity); color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            IconButton { implicitHeight: 24; label: "-"; onClicked: root.velocity = Math.max(10, root.velocity - 10) }
            IconButton { implicitHeight: 24; label: "+"; onClicked: root.velocity = Math.min(127, root.velocity + 10) }
        }
        // two octaves and a note: white keys with the black ones over them
        Item {
            id: board
            Layout.fillWidth: true
            Layout.fillHeight: true
            readonly property var whites: [0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24]
            readonly property var blacks: [[1, 0], [3, 1], [6, 3], [8, 4], [10, 5], [13, 7], [15, 8], [18, 10], [20, 11], [22, 12]]
            readonly property real keyWidth: width / whites.length
            Repeater {
                model: board.whites
                delegate: Rectangle {
                    required property int modelData
                    required property int index
                    objectName: "key_" + (root.baseNote + modelData)
                    x: index * board.keyWidth
                    width: board.keyWidth - 1
                    height: board.height
                    radius: 2
                    color: keyArea.pressed ? Theme.accentPrimary : "#e8e8ea"
                    MouseArea { id: keyArea; anchors.fill: parent; onPressed: root.enter(root.baseNote + parent.modelData) }
                }
            }
            Repeater {
                model: board.blacks
                delegate: Rectangle {
                    required property var modelData
                    objectName: "key_" + (root.baseNote + modelData[0])
                    x: (modelData[1] + 1) * board.keyWidth - board.keyWidth * 0.3
                    width: board.keyWidth * 0.6
                    height: board.height * 0.6
                    radius: 2
                    color: blackArea.pressed ? Theme.accentPrimary : "#202024"
                    MouseArea { id: blackArea; anchors.fill: parent; onPressed: root.enter(root.baseNote + parent.modelData[0]) }
                }
            }
        }
    }
}
