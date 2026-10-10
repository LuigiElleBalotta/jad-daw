import QtQuick
import QtQuick.Controls.Basic
import Jad

// The Step Sequencer of the Editors area: a grid of drum rows by 16th-note steps for the selected MIDI region. A click turns a step on or off (a note
// of one 16th at the default velocity); the notes of the region that are not on a row or not on a step stay as they are.
Item {
    id: root
    required property ProjectController project
    property string regionId: ""
    readonly property var info: { project.revision; return regionId !== "" ? project.regionInfo(regionId) : ({ found: false }) }
    readonly property bool hasMidi: info.found === true && info.audio !== true
    readonly property var notes: { project.revision; return hasMidi ? project.regionNotes(regionId) : [] }
    property var working: null                 // the notes as sent, until the project has them (a second click right after the first sees the first)
    readonly property var shown: working !== null ? working : notes
    onNotesChanged: working = null
    Timer { id: settle; interval: 500; onTriggered: root.working = null }
    // the rows: General MIDI drum pitches
    readonly property var rows: [{ pitch: 49, name: qsTr("Crash") }, { pitch: 46, name: qsTr("Open Hat") }, { pitch: 42, name: qsTr("Closed Hat") },
        { pitch: 45, name: qsTr("Mid Tom") }, { pitch: 41, name: qsTr("Low Tom") }, { pitch: 39, name: qsTr("Clap") }, { pitch: 38, name: qsTr("Snare") }, { pitch: 36, name: qsTr("Kick") }]
    readonly property real stepBeats: 0.25
    readonly property int steps: hasMidi ? Math.max(1, Math.round(info.lengthBeats / stepBeats)) : 0
    readonly property real cell: 22
    readonly property real labelWidth: 90
    property int defaultVelocity: 100

    function noteAt(pitch, step) {
        const start = step * stepBeats
        for (const n of shown) if (n.note === pitch && Math.abs(n.start - start) < 1e-6) return true
        return false
    }
    function toggle(pitch, step) {
        if (!hasMidi) return
        const start = step * stepBeats
        const list = shown.map(n => ({ start: n.start, length: n.length, note: n.note, velocity: n.velocity, muted: n.muted === true }))
        const kept = list.filter(n => !(n.note === pitch && Math.abs(n.start - start) < 1e-6))
        if (kept.length === list.length) kept.push({ start: start, length: stepBeats, note: pitch, velocity: defaultVelocity, muted: false })
        working = kept
        settle.restart()
        project.setRegionNotes(regionId, kept)
    }

    Rectangle { anchors.fill: parent; color: Theme.surfaceCanvas }
    Text {
        anchors.centerIn: parent
        visible: !root.hasMidi
        text: qsTr("Select a MIDI region to edit its steps")
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeBodySize
    }
    Flickable {
        id: flick
        visible: root.hasMidi
        anchors.fill: parent
        contentWidth: root.labelWidth + root.steps * root.cell + 20
        contentHeight: root.rows.length * root.cell + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: ScrollBar {}
        ScrollBar.vertical: ScrollBar {}
        Repeater {  // the step numbers (a bar number on each bar)
            model: root.steps
            delegate: Text {
                required property int index
                x: root.labelWidth + index * root.cell
                width: root.cell
                height: 16
                horizontalAlignment: Text.AlignHCenter
                text: index % (root.project.barBeats / root.stepBeats) === 0 ? String(Math.floor(index * root.stepBeats / root.project.barBeats) + 1) : (index % 4 === 0 ? "·" : "")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeCaptionSize
            }
        }
        Repeater {
            model: root.rows
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                y: 18 + index * root.cell
                width: flick.contentWidth
                height: root.cell
                Text {
                    width: root.labelWidth - 6
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: Text.AlignRight
                    text: row.modelData.name
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeCaptionSize
                }
                Repeater {
                    model: root.steps
                    delegate: Rectangle {
                        id: stepCell
                        required property int index
                        objectName: "step_" + row.modelData.pitch + "_" + index
                        readonly property bool on: { root.shown; return root.noteAt(row.modelData.pitch, index) }
                        x: root.labelWidth + index * root.cell
                        width: root.cell - 2
                        height: root.cell - 2
                        radius: 3
                        color: on ? Theme.accentPrimary : (Math.floor(index * root.stepBeats / root.project.barBeats * 2) % 2 === 0 ? Theme.surfaceRaised : Theme.surfacePanel)
                        border.color: Theme.borderSubtle
                        MouseArea { anchors.fill: parent; onClicked: root.toggle(row.modelData.pitch, stepCell.index) }
                    }
                }
            }
        }
    }
}
