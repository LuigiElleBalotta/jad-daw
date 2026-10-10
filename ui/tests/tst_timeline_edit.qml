import QtQuick
import QtTest
import Jad

TestCase {
    name: "TimelineEdit"
    width: 900; height: 400
    visible: true
    when: windowShown

    Component { id: itemC; RegionItem {
        width: 160; height: 56
        regionId: "r1"; trackId: "t1"; startBeats: 4; lengthBeats: 4; isAudio: false; missing: false
        pixelsPerBeat: 40
    } }

    function test_drag_emits_one_move_on_release() {
        var r = createTemporaryObject(itemC, this)
        var moves = []
        r.moved.connect(function (id, beats) { moves.push([id, beats]) })
        mousePress(r, 10, 10)
        mouseMove(r, 50, 10)
        mouseMove(r, 90, 10)
        compare(moves.length, 0)
        mouseRelease(r, 90, 10)
        compare(moves.length, 1)
        compare(moves[0][0], "r1")
        compare(moves[0][1], 6)          // 4 beats + 80 px / 40 px per beat, snapped to whole beats
    }
    function test_snap_rounds_to_grid() {
        var r = createTemporaryObject(itemC, this)
        r.snapBeats = 1
        compare(r.snap(5.4), 5)
        compare(r.snap(5.6), 6)
        r.snapBeats = 0
        compare(r.snap(5.43), 5.43)
    }
    function test_drag_below_zero_clamps() {
        var r = createTemporaryObject(itemC, this)
        var got
        r.moved.connect(function (id, beats) { got = beats })
        mousePress(r, 10, 10); mouseMove(r, -2000, 10); mouseRelease(r, -2000, 10)
        compare(got, 0)
    }
    function test_click_selects_without_moving() {
        var r = createTemporaryObject(itemC, this)
        var moves = 0, picks = []
        r.moved.connect(function () { moves++ })
        r.selectRequested.connect(function (id, extend) { picks.push([id, extend]) })
        mouseClick(r, 10, 10)
        compare(moves, 0)
        compare(picks.length, 1)
        compare(picks[0][0], "r1")
        compare(picks[0][1], false)
        mouseClick(r, 10, 10, Qt.LeftButton, Qt.ShiftModifier)
        compare(picks[1][1], true)
    }

    function test_tools_send_their_request_instead_of_selecting_or_moving() {
        var r = createTemporaryObject(itemC, this)
        var got = []
        r.eraseRequested.connect(function (id) { got.push(["erase", id]) })
        r.splitRequested.connect(function (id, at) { got.push(["split", id, at]) })
        r.glueRequested.connect(function (id) { got.push(["glue", id]) })
        var moves = 0
        var selects = 0
        r.moved.connect(function () { moves++ })
        r.selectRequested.connect(function () { selects++ })
        r.tool = "eraser"
        mouseClick(r, 10, 10)
        r.tool = "scissors"
        mouseClick(r, 80, 10)  // 80 px at 40 px per beat: two beats after the start at beat 4
        r.tool = "glue"
        mouseClick(r, 10, 10)
        compare(got, [["erase", "r1"], ["split", "r1", 6], ["glue", "r1"]])
        compare(moves, 0)
        compare(selects, 0)
    }

    function test_right_edge_drag_resizes_once_on_release() {
        var r = createTemporaryObject(itemC, this)  // 160 px wide, 4 beats at 40 px per beat, starting at beat 4
        r.tool = "pointer"
        var got = []
        r.resized.connect(function (id, s, l) { got.push([id, s, l]) })
        mousePress(r, 158, 10)
        mouseMove(r, 190, 10)
        mouseMove(r, 238, 10)
        compare(got.length, 0)
        mouseRelease(r, 238, 10)
        compare(got.length, 1)
        compare(got[0], ["r1", 4, 6])  // the right edge moved 80 px = 2 beats, snapped
    }
    function test_left_edge_drag_moves_the_start_and_shortens() {
        var r = createTemporaryObject(itemC, this)
        r.tool = "pointer"
        var got = []
        r.resized.connect(function (id, s, l) { got.push([id, s, l]) })
        mousePress(r, 2, 10)
        mouseMove(r, 42, 10)
        mouseRelease(r, 42, 10)
        compare(got, [["r1", 5, 3]])
    }
    function test_left_edge_cannot_pass_the_right_edge() {
        var r = createTemporaryObject(itemC, this)
        r.tool = "pointer"
        var got = []
        r.resized.connect(function (id, s, l) { got.push([id, s, l]) })
        mousePress(r, 2, 10)
        mouseMove(r, 400, 10)
        mouseRelease(r, 400, 10)
        compare(got.length, 1)
        verify(got[0][2] >= 1)  // never a zero or negative length
        verify(got[0][1] + got[0][2] <= 8 + 1e-9)  // the right edge did not move
    }
    // a region shorter than the snap grid (1 bar here): an edge can only shorten it, never grow it or move the other edge
    function test_a_short_region_right_edge_dragged_left_keeps_its_length() {
        var r = createTemporaryObject(itemC, this, { startBeats: 8, lengthBeats: 2, width: 80, snapBeats: 4 })
        r.tool = "pointer"
        var got = []
        r.resized.connect(function (id, s, l) { got.push([id, s, l]) })
        mousePress(r, 78, 10)
        mouseMove(r, 58, 10)
        mouseRelease(r, 58, 10)
        compare(got.length, 1)
        compare(got[0], ["r1", 8, 2])
    }
    function test_a_short_region_left_edge_dragged_right_keeps_its_end() {
        var r = createTemporaryObject(itemC, this, { startBeats: 8, lengthBeats: 2, width: 80, snapBeats: 4 })
        r.tool = "pointer"
        var got = []
        r.resized.connect(function (id, s, l) { got.push([id, s, l]) })
        mousePress(r, 2, 10)
        mouseMove(r, 22, 10)
        mouseRelease(r, 22, 10)
        compare(got.length, 1)
        compare(got[0], ["r1", 8, 2])
    }
    function test_edges_do_nothing_with_other_tools() {
        var r = createTemporaryObject(itemC, this)
        r.tool = "scissors"
        var got = []
        r.resized.connect(function () { got.push(1) })
        mousePress(r, 158, 10)
        mouseMove(r, 238, 10)
        mouseRelease(r, 238, 10)
        compare(got.length, 0)
    }
    function test_a_tiny_edge_movement_is_a_click_not_a_resize() {
        var r = createTemporaryObject(itemC, this)
        r.tool = "pointer"
        var got = []
        r.resized.connect(function () { got.push(1) })
        mousePress(r, 158, 10)
        mouseMove(r, 159, 10)
        mouseRelease(r, 159, 10)
        compare(got.length, 0)
    }

    function test_a_fade_handle_drag_asks_for_the_new_fades() {
        var r = createTemporaryObject(itemC, this, { isAudio: true, selected: true, tool: "pointer" })
        var got = []
        r.fadesRequested.connect(function (id, fin, fout) { got.push([id, fin, fout]) })
        // the in handle sits at the top-left corner: drag it 80 px (2 beats at 40 px per beat)
        mousePress(r, 4, 5)
        mouseMove(r, 44, 5)
        mouseMove(r, 84, 5)
        mouseRelease(r, 84, 5)
        compare(got.length, 1)
        compare(got[0][0], "r1")
        verify(Math.abs(got[0][1] - 2) < 0.2)
        compare(got[0][2], 0)
    }

    Component { id: audioC; RegionItem {
        width: 160; height: 56
        regionId: "a1"; trackId: "t1"; startBeats: 0; lengthBeats: 4; isAudio: true; missing: false
        pixelsPerBeat: 40
        fadeInBeats: 0; fadeOutBeats: 0; gainDb: 0
    } }

    function test_gain_tool_drags_the_gain_and_asks_once_on_release() {
        var r = createTemporaryObject(audioC, this)
        r.tool = "gain"
        var asked = []
        r.gainRequested.connect(function (id, db) { asked.push([id, db]) })
        mousePress(r, 40, 40)
        mouseMove(r, 40, 30)                                    // 10 px up: +2.5 dB
        compare(r.gainDragDb, 2.5)
        compare(asked.length, 0)
        mouseRelease(r, 40, 30)
        compare(asked.length, 1)
        compare(asked[0][0], "a1")
        compare(asked[0][1], 2.5)
        verify(isNaN(r.gainDragDb))
    }

    function test_fade_tool_sets_the_fade_of_the_side_that_was_pressed() {
        var r = createTemporaryObject(audioC, this)
        r.tool = "fade"
        var fades = []
        r.fadesRequested.connect(function (id, fin, fout) { fades.push([id, fin, fout]) })
        mousePress(r, 10, 20)                                   // the left half: the fade in
        mouseMove(r, 50, 20)                                    // 40 px = one beat
        mouseRelease(r, 50, 20)
        compare(fades.length, 1)
        compare(fades[0][1], 1)
        compare(fades[0][2], 0)
        mousePress(r, 150, 20)                                  // the right half: the fade out
        mouseMove(r, 110, 20)
        mouseRelease(r, 110, 20)
        compare(fades.length, 2)
        compare(fades[1][2], 1)
    }

    function test_solo_tool_asks_for_the_solo_of_the_track() {
        var r = createTemporaryObject(audioC, this)
        r.tool = "solo"
        var asked = []
        r.soloRequested.connect(function (id) { asked.push(id) })
        mouseClick(r, 40, 20)
        compare(asked.length, 1)
        compare(asked[0], "a1")
    }
}
