import QtQuick
import QtTest
import Jad

// Piano Roll mouse gestures: Option-click draws, either end of a note resizes it.
TestCase {
    name: "PianoRoll"
    width: 1000; height: 600
    visible: true
    when: windowShown

    Component { id: ctlC; ProjectController { audioEnabled: false } }
    Component { id: prC; PianoRoll { width: 1000; height: 600 } }

    // a project with an instrument track and an empty MIDI region of 4 beats at 0, selected
    function setup() {
        const c = createTemporaryObject(ctlC, this)
        verify(c.newProjectInTempForTest())
        c.addTrack("instrument")
        tryVerify(function () { return c.tracks.rowCount() === 1 })
        c.createRegion(c.tracks.trackIdAt(0), 0, 4)
        tryVerify(function () { return c.regions.rowCount() === 1 })
        c.selectRegion(c.regions.data(c.regions.index(0, 0), 257), "replace")
        const p = createTemporaryObject(prC, this, { project: c })
        verify(p.hasMidi)
        return p
    }
    function beatX(p, b) { return (b - p.scrollBeats) * p.pixelsPerBeat }
    function noteY(p, n) { return p.noteTop(n) + p.rowHeight / 2 }

    function test_alt_click_with_the_pointer_draws_a_note() {
        const p = setup()
        p.project.tool = "pointer"
        mouseClick(p.grid, beatX(p, 1), noteY(p, 60), Qt.LeftButton, Qt.AltModifier)
        tryVerify(function () { return p.notes.length === 1 })
        compare(p.notes[0].note, 60)
        compare(p.notes[0].start, 1)
    }

    function test_plain_click_with_the_pointer_draws_nothing() {
        const p = setup()
        p.project.tool = "pointer"
        mouseClick(p.grid, beatX(p, 1), noteY(p, 60))
        wait(100)
        compare(p.notes.length, 0)
    }

    function test_drag_either_end_resizes_the_note() {
        const p = setup()
        p.project.tool = "pointer"
        mouseClick(p.grid, beatX(p, 1), noteY(p, 60), Qt.LeftButton, Qt.AltModifier)
        tryVerify(function () { return p.notes.length === 1 })
        // draw made a note of one grid step: 0.25 beat; make it longer first by dragging its right end
        const n0 = p.notes[0]
        const y = noteY(p, 60)
        const right = beatX(p, n0.start + n0.length)
        mousePress(p.grid, right - 1, y)
        mouseMove(p.grid, right + p.pixelsPerBeat - 1, y)
        mouseRelease(p.grid, right + p.pixelsPerBeat - 1, y)
        tryVerify(function () { return p.notes[0].length > n0.length + 0.9 })
        const n1 = p.notes[0]
        compare(n1.start, n0.start)
        // the left end moves the start and keeps the end
        const end = n1.start + n1.length
        const left = beatX(p, n1.start)
        mousePress(p.grid, left + 1, y)
        mouseMove(p.grid, left + 1 + p.pixelsPerBeat / 2, y)
        mouseRelease(p.grid, left + 1 + p.pixelsPerBeat / 2, y)
        tryVerify(function () { return p.notes[0].start > n1.start + 0.4 })
        compare(p.notes[0].start + p.notes[0].length, end)
    }

    function test_mute_notes_toggles_the_selected_notes() {
        const p = setup()
        p.project.tool = "pointer"
        mouseClick(p.grid, beatX(p, 1), noteY(p, 60), Qt.LeftButton, Qt.AltModifier)
        tryVerify(function () { return p.notes.length === 1 })
        p.selected = [0]
        p.muteNotes()
        tryVerify(function () { return p.notes[0].muted === true })
        p.muteNotes()
        tryVerify(function () { return p.notes[0].muted === false })
    }

    function test_scale_quantize_moves_notes_to_the_nearest_scale_pitch() {
        const p = setup()
        p.scaleName = "Major"; p.scaleKey = 0
        compare(p.toScale(61), 60)   // C#: the lower neighbour wins a tie
        compare(p.toScale(63), 62)   // D# -> D
        compare(p.toScale(66), 65)   // F# -> F
        compare(p.toScale(60), 60)
        p.scaleName = "Minor"; p.scaleKey = 9   // A minor has the same notes as C major
        compare(p.toScale(61), 60)
        p.scaleName = "Off"
        compare(p.toScale(61), 61)
    }
}
