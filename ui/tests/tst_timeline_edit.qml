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
}
