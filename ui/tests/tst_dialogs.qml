import QtQuick
import QtQuick.Controls.Basic
import QtTest
import Jad

// The small dialogs: ask for a number, search a track by name.
TestCase {
    name: "Dialogs"
    width: 800; height: 600
    visible: true
    when: windowShown

    Component { id: ctlC; ProjectController { audioEnabled: false } }
    Component { id: promptC; NumberPromptDialog { parent: Overlay.overlay; heading: "Repeat"; prompt: "Copies"; value: 4; from: 1; to: 64 } }
    Component { id: searchC; SearchTrackDialog { parent: Overlay.overlay } }
    Component { id: listC; ListEditorsWindow { parent: Overlay.overlay } }
    Component { id: stepC; StepSequencer { width: 900; height: 300 } }
    Component { id: scoreC; ScoreView { width: 900; height: 300 } }

    function test_the_number_prompt_returns_a_clamped_number() {
        const d = createTemporaryObject(promptC, this)
        let got = -1
        d.accepted2.connect(function (v) { got = v })
        d.open()
        tryCompare(d, "visible", true)
        const field = findChild(d.contentItem, "numberField")
        compare(field.text, "4")
        field.text = "500"
        keyClick(Qt.Key_Return)
        tryCompare(d, "visible", false)
        compare(got, 64)                              // above the limit: brought back
    }

    function test_search_finds_a_track_by_part_of_its_name_and_selects_it() {
        const c = createTemporaryObject(ctlC, this)
        verify(c.newProjectInTempForTest())
        c.addTrack("audio")
        c.addTrack("instrument")
        tryVerify(function () { return c.tracks.rowCount() === 2 })
        const d = createTemporaryObject(searchC, this, { project: c })
        d.open()
        tryCompare(d, "visible", true)
        compare(d.matches.length, 2)
        const field = findChild(d.contentItem, "searchField")
        field.text = "instr"
        compare(d.matches.length, 1)
        compare(d.matches[0].kind, "instrument")
        keyClick(Qt.Key_Return)
        tryCompare(d, "visible", false)
        tryVerify(function () { return c.selectedTrackIds.length === 1 && c.selectedTrackIds[0] === d.matches[0].id })
    }

    function test_list_editors_show_the_events_and_the_global_lists() {
        const c = createTemporaryObject(ctlC, this)
        verify(c.newProjectInTempForTest())
        c.addTrack("instrument")
        tryVerify(function () { return c.tracks.rowCount() === 1 })
        c.createRegion(c.tracks.trackIdAt(0), 0, 4)
        tryVerify(function () { return c.regions.rowCount() === 1 })
        const id = c.regions.data(c.regions.index(0, 0), 257)
        c.selectRegion(id, "replace")
        c.setRegionNotes(id, [{ start: 0, length: 1, note: 60, velocity: 90 }, { start: 1, length: 1, note: 64, velocity: 80 }])
        tryVerify(function () { return c.regionNotes(id).length === 2 })
        c.setRegionControls(id, "cc64", [{ beats: 0.5, value: 127 }])
        tryVerify(function () { return c.regionControlEvents(id).length === 1 })
        const d = createTemporaryObject(listC, this, { project: c })
        d.open()
        tryCompare(d, "visible", true)
        compare(d.events.length, 3)                           // two notes and a control change, in time order
        compare(d.events[1].kind, "cc")
        d.setNote(0, "note", 62)                               // edit the pitch of the first note
        tryVerify(function () { return c.regionNotes(id)[0].note === 62 })
        d.removeNote(1)
        tryVerify(function () { return c.regionNotes(id).length === 1 })
        compare(d.tempos.length, 1)
        compare(d.signatures.length, 1)
        c.setTempoAt(8, 90)
        tryVerify(function () { return d.tempos.length === 2 })
        compare(d.position(8), "3 1 0")
    }

    function test_step_sequencer_toggles_notes_and_the_score_shows_them() {
        const c = createTemporaryObject(ctlC, this)
        verify(c.newProjectInTempForTest())
        c.addTrack("instrument")
        tryVerify(function () { return c.tracks.rowCount() === 1 })
        c.createRegion(c.tracks.trackIdAt(0), 0, 4)
        tryVerify(function () { return c.regions.rowCount() === 1 })
        const id = c.regions.data(c.regions.index(0, 0), 257)
        const seq = createTemporaryObject(stepC, this, { project: c, regionId: id })
        tryVerify(function () { return seq.hasMidi })
        compare(seq.steps, 16)                                  // four beats of 16th notes
        const kick0 = findChild(seq, "step_36_0")
        const kick4 = findChild(seq, "step_36_4")
        verify(kick0 && kick4)
        mouseClick(kick0)
        mouseClick(kick4)
        tryVerify(function () { return c.regionNotes(id).length === 2 })
        compare(c.regionNotes(id)[1].start, 1)                  // the fifth step is one beat in
        tryVerify(function () { return kick0.on })
        mouseClick(kick0)                                       // off again
        tryVerify(function () { return c.regionNotes(id).length === 1 })
        const score = createTemporaryObject(scoreC, this, { project: c, regionId: id })
        tryVerify(function () { return score.hasMidi && score.notes.length === 1 })
        compare(score.diatonic(64), 30)                         // E4 is the bottom line of the treble staff
        compare(score.diatonic(43), 18)                         // G2 is the bottom line of the bass staff
        const canvas = findChild(score, "scoreCanvas")
        verify(canvas)
        canvas.requestPaint()
        wait(50)                                                // painting a staff with a note must not fail
    }
}
