import QtQuick
import QtTest
import Jad

TestCase {
    name: "Flex"
    width: 700; height: 500
    visible: true
    when: windowShown

    Component { id: hdr; TrackHeader {
        width: 260; height: 40
        trackId: "t1"; trackName: "Voice"; kind: "audio"; trackColor: "purple"; number: 1
    } }
    Component { id: overlayC; FlexOverlay {
        width: 400; height: 80
        regionId: "r1"
    } }
    Component { id: audioC; RegionItem {
        regionId: "r1"; trackId: "t1"; isAudio: true; trackColor: "teal"; trackName: "Voice"
        startBeats: 0; lengthBeats: 4; pixelsPerBeat: 100; width: 400; height: 60
    } }
    Component { id: ctlC; ProjectController { audioEnabled: false } }
    Component { id: edC; AudioEditor { width: 900; height: 300 } }
    Component { id: winC; FlexEditorWindow { visible: false } }

    function test_the_flex_button_shows_only_when_flex_is_shown_and_its_menu_picks_the_mode() {
        const h = createTemporaryObject(hdr, this)
        verify(!h.flexButton.visible)
        h.flexShown = true
        verify(h.flexButton.visible)
        verify(!h.flexButton.active)
        h.flexMode = "polyphonic"
        verify(h.flexButton.active)
        const got = []
        h.flexModeRequested.connect(function (id, mode) { got.push([id, mode]) })
        compare(h.flexModes.length, 9)
        compare(h.flexModes[0].id, "")
        compare(h.flexModes[8].id, "pitch")
        h.flexModeRequested("t1", "slicing")
        compare(got[0], ["t1", "slicing"])
    }

    function test_a_flex_marker_is_dragged_sideways_and_deleted_by_a_double_click() {
        const o = createTemporaryObject(overlayC, this, { mode: "monophonic", markers: [{ index: 0, frac: 0.25 }, { index: 1, frac: 0.75 }], transients: [0.1, 0.25, 0.5, 0.75] })
        verify(o)
        const moves = [], deletes = []
        o.markerMoved.connect(function (index, fraction, slide) { moves.push([index, fraction, slide]) })
        o.markerDeleted.connect(function (index) { deletes.push(index) })
        mousePress(o, 300, 6)                      // the handle of the second marker, at 0.75 of 400
        mouseMove(o, 340, 6)
        mouseRelease(o, 340, 6)
        compare(moves.length, 1)
        compare(moves[0][0], 1)
        verify(Math.abs(moves[0][1] - 0.85) < 0.02)
        compare(moves[0][2], false)
        mouseDoubleClickSequence(o, 100, 6)       // the first marker, at 0.25
        compare(deletes.length, 1)
        compare(deletes[0], 0)
    }

    function test_in_flex_pitch_the_overlay_draws_notes_not_markers() {
        const o = createTemporaryObject(overlayC, this, { mode: "pitch", markers: [{ index: 0, frac: 0.25 }],
                                                          notes: [{ index: 0, start: 0.1, end: 0.4, pitch: 57 }, { index: 1, start: 0.5, end: 0.9, pitch: 62 }] })
        verify(o.pitchMode)
        verify(o.lowPitch < 57 && o.highPitch > 62)
        verify(findChild(o, "flexNote0") !== null)
        verify(findChild(o, "flexNote1") !== null)
        compare(findChild(o, "flexMarker0"), null)
    }

    function test_the_flex_tool_adds_a_marker_one_in_the_upper_half_three_in_the_lower() {
        const r = createTemporaryObject(audioC, this, { flexView: true, flexMode: "monophonic", tool: "flex" })
        verify(r.flexActive)
        const got = []
        r.flexAddRequested.connect(function (id, fraction, three) { got.push([id, fraction, three]) })
        mouseClick(r, 100, 10)
        mouseClick(r, 300, 50)
        compare(got.length, 2)
        compare(got[0][0], "r1")
        verify(Math.abs(got[0][1] - 0.25) < 0.01)
        compare(got[0][2], false)
        verify(Math.abs(got[1][1] - 0.75) < 0.01)
        compare(got[1][2], true)
        r.flexView = false
        verify(!r.flexActive)
        mouseClick(r, 100, 10)
        compare(got.length, 2)                    // with the Flex view off the tool does nothing
    }

    function test_flex_pitch_in_the_audio_editor_needs_an_audio_region_on_a_flex_pitch_track() {
        const c = createTemporaryObject(ctlC, this)
        const e = createTemporaryObject(edC, this, { project: c })
        verify(!e.pitchMode)
        compare(e.flexNotes.length, 0)
        verify(findChild(e, "flexNoteStrip") !== null)
        verify(!findChild(e, "flexNoteStrip").visible)
    }

    function test_the_flex_editor_window_holds_the_audio_editor_and_follows_the_selection() {
        const c = createTemporaryObject(ctlC, this)
        const w = createTemporaryObject(winC, this, { project: c })
        verify(w !== null)
        compare(w.regionId, "")
        verify(w.editor !== null)
        verify(!w.editor.valid)
        const bar = findChild(w.editor, "flexBar")
        verify(bar !== null)
        verify(!bar.visible)                       // no audio region: no Flex bar
    }

    Component { id: probeC; Item {
        property var project
        property string regionId
        readonly property var info: { project.revision; return regionId !== "" ? project.regionInfo(regionId) : ({ found: false }) }
    } }

    function test_a_binding_on_the_region_info_follows_a_change_of_its_track() {
        const c = createTemporaryObject(ctlC, this)
        verify(c.newProjectInTempForTest())
        c.addTrack("instrument")
        tryVerify(function () { return c.tracks.rowCount() > 0 })
        const track = c.tracks.trackIdAt(c.tracks.rowCount() - 1)
        c.createRegion(track, 0, 4)
        tryVerify(function () { return c.regions.rowCount() > 0 })
        const id = c.regions.regionIdAt(0)
        const probe = createTemporaryObject(probeC, this, { project: c, regionId: id })
        const before = probe.info.trackName
        c.renameTrack(track, "Renamed")
        tryVerify(function () { return probe.info.trackName === "Renamed" }, 5000)
        verify(before !== "Renamed")
    }

    function test_a_binding_on_the_region_info_follows_a_change_of_the_region() {
        const c = createTemporaryObject(ctlC, this)
        verify(c.newProjectInTempForTest())
        c.addTrack("instrument")
        tryVerify(function () { return c.tracks.rowCount() > 0 })
        const track = c.tracks.trackIdAt(c.tracks.rowCount() - 1)
        c.createRegion(track, 0, 4)
        tryVerify(function () { return c.regions.rowCount() > 0 })
        const id = c.regions.regionIdAt(0)
        const probe = createTemporaryObject(probeC, this, { project: c, regionId: id })
        compare(probe.info.lengthBeats, 4)
        c.selectRegion(id, "replace")
        c.resizeSelectedRegions(id, 0, 8)
        tryVerify(function () { return probe.info.lengthBeats === 8 }, 5000)
    }
}
