import QtQuick
import QtTest
import Jad

// The global tracks: markers, tempo and signature lanes under the ruler.
TestCase {
    name: "GlobalTracks"
    width: 1000; height: 500
    visible: true
    when: windowShown

    Component { id: ctlC; ProjectController { audioEnabled: false } }
    Component { id: tlC; Timeline { width: 1000; height: 500 } }

    function setup() {
        const c = createTemporaryObject(ctlC, this)
        verify(c.newProjectInTempForTest())
        return c
    }

    function test_markers_are_added_renamed_moved_and_removed_with_undo() {
        const c = setup()
        c.addMarker(4, "Verse")
        tryVerify(function () { return c.markers().length === 1 })
        compare(c.markers()[0].beats, 4)
        compare(c.markers()[0].name, "Verse")
        const id = c.markers()[0].id
        c.renameMarker(id, "Chorus")
        tryVerify(function () { return c.markers()[0].name === "Chorus" })
        c.moveMarker(id, 8)
        tryVerify(function () { return c.markers()[0].beats === 8 })
        c.addMarker(0, "Intro")
        tryVerify(function () { return c.markers().length === 2 })
        compare(c.markers()[0].name, "Intro")   // sorted by position
        c.removeMarker(id)
        tryVerify(function () { return c.markers().length === 1 })
        c.undo()
        tryVerify(function () { return c.markers().length === 2 })
    }

    function test_tempo_and_signature_events() {
        const c = setup()
        c.setTempoAt(8, 90)
        tryVerify(function () { return c.tempoEvents().length === 2 })
        compare(c.tempoEvents()[1].bpm, 90)
        c.removeTempoAt(8)
        tryVerify(function () { return c.tempoEvents().length === 1 })
        c.setSignatureAt(8, 3, 4)
        tryVerify(function () { return c.signatureEvents().length === 2 })
        compare(c.signatureEvents()[1].numerator, 3)
    }

    function test_the_lanes_make_the_header_taller_only_while_shown() {
        const c = setup()
        const t = createTemporaryObject(tlC, this, { project: c })
        compare(t.rulerHeight, 24)
        c.globalTracksVisible = true
        compare(t.rulerHeight, 24 + 3 * 18)
        c.globalTracksVisible = false
        compare(t.rulerHeight, 24)
    }

    function test_double_click_on_the_marker_lane_adds_a_marker() {
        const c = setup()
        const t = createTemporaryObject(tlC, this, { project: c })
        c.globalTracksVisible = true
        mouseDoubleClickSequence(t, 100, 24 + 9)   // the Marker lane
        tryVerify(function () { return c.markers().length === 1 })
    }

    function test_cycle_area_is_kept_while_the_cycle_is_off_and_moves_by_its_length() {
        const c = setup()
        c.setLoopBeats(4, 12)
        tryVerify(function () { return c.loopEnabled })
        compare(c.loopStartBeats, 4)
        compare(c.loopEndBeats, 12)
        c.toggleLoop()
        tryVerify(function () { return !c.loopEnabled })
        compare(c.loopEndBeats, 12)          // the area stays
        c.toggleLoop()
        tryVerify(function () { return c.loopEnabled })
        c.moveLocators(1)
        tryVerify(function () { return c.loopStartBeats === 12 })
        compare(c.loopEndBeats, 20)
        c.moveLocators(-1)
        tryVerify(function () { return c.loopStartBeats === 4 })
    }

    function test_dragging_the_ruler_strip_draws_a_cycle_area() {
        const c = setup()
        const t = createTemporaryObject(tlC, this, { project: c })
        c.snap = "quarter"
        mousePress(t, 80, 3)
        mouseMove(t, 200, 3)
        mouseMove(t, 330, 3)
        mouseRelease(t, 330, 3)
        tryVerify(function () { return c.loopEndBeats > c.loopStartBeats })
        verify(c.loopEndBeats - c.loopStartBeats >= 4)
    }
}
