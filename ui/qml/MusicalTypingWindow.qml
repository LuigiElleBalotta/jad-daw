import QtQuick
import QtQuick.Window
import Jad

// Window > Show Musical Typing: the computer keyboard plays the live instrument track (and records on an armed one), like Logic's
// Musical Typing. A S D F G H J K L ; are the white keys, W E T Y U O P the black ones, Z and X move the octave, C and V the velocity.
Window {
    id: root
    required property ProjectController project
    property int baseNote: 60          // the C the A key plays
    property int velocity: 80
    property var down: ({})            // key code -> the note it started
    readonly property string target: project.liveTargetTrack()

    width: 560
    height: 190
    minimumWidth: 420
    minimumHeight: 150
    title: qsTr("Musical Typing")
    color: Theme.surfacePanel
    flags: Qt.Tool | Qt.WindowStaysOnTopHint

    // key -> semitones above the base C
    readonly property var layout: ({ [Qt.Key_A]: 0, [Qt.Key_W]: 1, [Qt.Key_S]: 2, [Qt.Key_E]: 3, [Qt.Key_D]: 4, [Qt.Key_F]: 5, [Qt.Key_T]: 6,
                                     [Qt.Key_G]: 7, [Qt.Key_Y]: 8, [Qt.Key_H]: 9, [Qt.Key_U]: 10, [Qt.Key_J]: 11, [Qt.Key_K]: 12, [Qt.Key_O]: 13,
                                     [Qt.Key_L]: 14, [Qt.Key_P]: 15, [Qt.Key_Semicolon]: 16 })
    property var lit: ({})             // note -> true while it sounds

    function press(note) {
        project.playNote(note, velocity, true)
        const m = Object.assign({}, lit); m[note] = true; lit = m
    }
    function release(note) {
        project.playNote(note, 0, false)
        const m = Object.assign({}, lit); delete m[note]; lit = m
    }
    function keyDown(event) {
        if (event.isAutoRepeat) { event.accepted = true; return }
        if (event.key === Qt.Key_Z) { baseNote = Math.max(0, baseNote - 12); event.accepted = true; return }
        if (event.key === Qt.Key_X) { baseNote = Math.min(108, baseNote + 12); event.accepted = true; return }
        if (event.key === Qt.Key_C) { velocity = Math.max(10, velocity - 10); event.accepted = true; return }
        if (event.key === Qt.Key_V) { velocity = Math.min(127, velocity + 10); event.accepted = true; return }
        const off = layout[event.key]
        if (off === undefined) return
        const note = baseNote + off
        if (down[event.key] !== undefined) return
        const d = Object.assign({}, down); d[event.key] = note; down = d
        press(note)
        event.accepted = true
    }
    function keyUp(event) {
        if (event.isAutoRepeat) return
        const note = down[event.key]
        if (note === undefined) return
        const d = Object.assign({}, down); delete d[event.key]; down = d
        release(note)
        event.accepted = true
    }
    onVisibleChanged: if (!visible) for (const k in down) release(down[k])

    FocusScope {
        anchors.fill: parent
        focus: true
        Keys.onPressed: (event) => root.keyDown(event)
        Keys.onReleased: (event) => root.keyUp(event)

        Column {
            anchors.fill: parent
            anchors.margins: Theme.spacing[3]
            spacing: Theme.spacing[2]
            Text {
                width: parent.width
                text: root.target === "" ? qsTr("No instrument track is selected or armed: nothing will sound")
                                         : qsTr("Playing %1").arg(root.project.trackName(root.target))
                color: root.target === "" ? Theme.stateClip : Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeLabelSize
                elide: Text.ElideRight
            }
            Row {
                spacing: Theme.spacing[3]
                Text { text: qsTr("Octave: C%1 (Z / X)").arg(Math.floor(root.baseNote / 12) - 2); color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
                Text { text: qsTr("Velocity: %1 (C / V)").arg(root.velocity); color: Theme.textValue; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeBodySize }
            }
            // the keyboard: 17 keys from the base C
            Item {
                id: board
                width: parent.width
                height: parent.height - 56
                readonly property var whites: [0, 2, 4, 5, 7, 9, 11, 12, 14, 16]
                readonly property var blacks: [[1, 0], [3, 1], [6, 3], [8, 4], [10, 5], [13, 7], [15, 8]]  // semitone, the white key to its left
                readonly property real keyWidth: width / whites.length
                Repeater {
                    model: board.whites
                    delegate: Rectangle {
                        required property int modelData
                        required property int index
                        x: index * board.keyWidth
                        width: board.keyWidth - 1
                        height: board.height
                        radius: 2
                        color: root.lit[root.baseNote + modelData] ? Theme.accentPrimary : "#e8e8ea"
                        MouseArea {
                            anchors.fill: parent
                            onPressed: root.press(root.baseNote + parent.modelData)
                            onReleased: root.release(root.baseNote + parent.modelData)
                        }
                    }
                }
                Repeater {
                    model: board.blacks
                    delegate: Rectangle {
                        required property var modelData
                        x: (modelData[1] + 1) * board.keyWidth - board.keyWidth * 0.3
                        width: board.keyWidth * 0.6
                        height: board.height * 0.6
                        radius: 2
                        color: root.lit[root.baseNote + modelData[0]] ? Theme.accentPrimary : "#202024"
                        MouseArea {
                            anchors.fill: parent
                            onPressed: root.press(root.baseNote + parent.modelData[0])
                            onReleased: root.release(root.baseNote + parent.modelData[0])
                        }
                    }
                }
            }
        }
    }
}
