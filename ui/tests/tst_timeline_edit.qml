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
}
