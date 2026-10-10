import QtQuick
import QtTest
import Jad

// The floating windows: step input keyboard, event float, region inspector float.
TestCase {
    name: "Floats"
    width: 400; height: 300
    visible: true
    when: windowShown

    Component { id: ctlC; ProjectController { audioEnabled: false } }
    Component { id: stepC; StepInputKeyboardWindow { } }
    Component { id: eventC; EventFloatWindow { } }
    Component { id: inspC; RegionInspectorFloatWindow { } }
    Component {
        id: pianoC
        QtObject {
            property var selected: [0]
            property var notes: [{ start: 1, length: 0.5, note: 60, velocity: 100, muted: false }]
            property real regionStart: 4
            property var committed: null
            function copyNotes(list) { return list.map(n => ({ start: n.start, length: n.length, note: n.note, velocity: n.velocity, muted: n.muted === true })) }
            function commit(list) { committed = list }
            function noteName(n) { return "C3" }
        }
    }

    function test_a_key_of_the_step_input_keyboard_puts_a_note_at_the_playhead_and_steps_on() {
        const c = createTemporaryObject(ctlC, this)
        verify(c.newProjectInTempForTest())
        c.addTrack("instrument")
        tryVerify(function () { return c.tracks.rowCount() === 1 })
        c.selectTrack(c.tracks.trackIdAt(0), "replace")
        const w = createTemporaryObject(stepC, this, { project: c })
        compare(w.step, 0.25)
        w.step = 1
        w.enter(w.baseNote + 7)                       // G
        tryVerify(function () { return c.regions.rowCount() === 1 })
        const id = c.regions.regionIdAt(0)
        tryCompare(c, "positionBeats", 1, 3000, "the playhead moves on by the step")
        w.chord = true
        w.enter(w.baseNote + 11)
        tryVerify(function () { return c.regionNotes(id).length === 2 })
    }

    function test_the_event_float_edits_the_selected_note_through_the_piano_roll() {
        const c = createTemporaryObject(ctlC, this)
        const piano = createTemporaryObject(pianoC, this)
        const w = createTemporaryObject(eventC, this, { project: c, piano: piano })
        compare(w.index, 0)
        compare(w.note.velocity, 100)
        w.setNote("velocity", 50)
        verify(piano.committed !== null)
        compare(piano.committed[0].velocity, 50)
        compare(piano.committed[0].note, 60)          // the rest of the note stays
        piano.selected = []
        compare(w.index, -1)
    }

    function test_the_region_inspector_float_follows_the_selected_region() {
        const c = createTemporaryObject(ctlC, this)
        verify(c.newProjectInTempForTest())
        c.addTrack("instrument")
        tryVerify(function () { return c.tracks.rowCount() === 1 })
        const w = createTemporaryObject(inspC, this, { project: c })
        const holder = findChild(w.contentItem, "floatInspectorLoader")
        verify(holder !== null)
        verify(findChild(w.contentItem, "floatRegionInspector") === null)   // not built while the window is hidden
        w.visible = true
        tryVerify(function () { return findChild(w.contentItem, "floatRegionInspector") !== null })
        verify(!holder.visible)                       // no region yet
        c.createRegion(c.tracks.trackIdAt(0), 0, 4)
        tryVerify(function () { return c.regions.rowCount() === 1 })
        c.selectRegion(c.regions.regionIdAt(0), "replace")
        tryVerify(function () { return holder.visible })
        w.visible = false
    }
}
