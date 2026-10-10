import QtQuick
import QtTest
import Jad

// Automation lanes: the points live in the project, the overlay edits them.
TestCase {
    name: "Automation"
    width: 1000; height: 500
    visible: true
    when: windowShown

    Component { id: ctlC; ProjectController { audioEnabled: false } }
    Component { id: tlC; Timeline { width: 1000; height: 500 } }
    Component { id: laneC; AutomationLane { width: 800; height: 60; pixelsPerBeat: 40; scrollBeats: 0 } }

    function setup() {
        const c = createTemporaryObject(ctlC, this)
        verify(c.newProjectInTempForTest())
        c.addTrack("audio")
        tryVerify(function () { return c.tracks.rowCount() === 1 })
        return c
    }

    function test_points_are_stored_sorted_and_undone() {
        const c = setup()
        const id = c.tracks.trackIdAt(0)
        c.setAutomationPoints(id, "volume", [{ beats: 4, value: -6 }, { beats: 0, value: 0 }])
        tryVerify(function () { return c.automationPoints(id, "volume").length === 2 })
        compare(c.automationPoints(id, "volume")[0].beats, 0)
        compare(c.automationPoints(id, "volume")[1].value, -6)
        compare(c.automationPoints(id, "pan").length, 0)
        c.undo()
        tryVerify(function () { return c.automationPoints(id, "volume").length === 0 })
    }

    function test_clicking_the_lane_adds_a_point_and_dragging_moves_it() {
        const c = setup()
        const id = c.tracks.trackIdAt(0)
        const lane = createTemporaryObject(laneC, this, { project: c, trackId: id, param: "volume" })
        mouseClick(lane, 80, 30)   // beat 2, about -27 dB
        tryVerify(function () { return c.automationPoints(id, "volume").length === 1 })
        compare(c.automationPoints(id, "volume")[0].beats, 2)
        const y0 = lane.valueToY(c.automationPoints(id, "volume")[0].value)
        mousePress(lane, 80, y0)
        mouseMove(lane, 120, y0 - 20)
        mouseRelease(lane, 120, y0 - 20)
        tryVerify(function () { return c.automationPoints(id, "volume")[0].beats === 3 })
        verify(c.automationPoints(id, "volume")[0].value > -27)
        compare(c.automationPoints(id, "volume").length, 1)
    }

    function test_the_curve_tool_bends_a_segment_and_the_select_tool_moves_and_deletes_a_range() {
        const c = setup()
        const id = c.tracks.trackIdAt(0)
        c.setAutomationPoints(id, "pan", [{ beats: 0, value: -1 }, { beats: 8, value: 1 }, { beats: 12, value: 0 }])
        tryVerify(function () { return c.automationPoints(id, "pan").length === 3 })
        compare(c.automationPoints(id, "pan")[0].curve, 0)
        const lane = createTemporaryObject(laneC, this, { project: c, trackId: id, param: "pan" })
        // Automation Curve: press in the middle of the first segment and drag it down (towards the start value): it starts slowly
        c.tool = "autoCurve"
        const x = lane.beatsToX(4), y = lane.valueToY(0)
        mousePress(lane, x, y)
        mouseMove(lane, x, y + 20)
        mouseRelease(lane, x, y + 20)
        tryVerify(function () { return c.automationPoints(id, "pan")[0].curve !== 0 })
        verify(c.automationPoints(id, "pan")[0].curve > 0)           // the lower the pointer, the slower the start
        compare(c.automationPoints(id, "pan").length, 3)              // no point was added
        compare(c.automationPoints(id, "pan")[1].curve, 0)
        // Automation Select: a range around the second point, moved by two beats, then deleted
        c.tool = "autoSelect"
        mousePress(lane, lane.beatsToX(7), 10)
        mouseMove(lane, lane.beatsToX(9), 10)
        mouseRelease(lane, lane.beatsToX(9), 10)
        verify(lane.hasSelection)
        compare(lane.selFrom, 7)
        compare(lane.selTo, 9)
        mousePress(lane, lane.beatsToX(8), 30)
        mouseMove(lane, lane.beatsToX(10), 30)
        mouseRelease(lane, lane.beatsToX(10), 30)
        tryVerify(function () { return c.automationPoints(id, "pan")[1].beats === 10 })
        compare(c.automationPoints(id, "pan")[0].beats, 0)
        lane.deleteSelection()
        tryVerify(function () { return c.automationPoints(id, "pan").length === 2 })
        c.tool = "pointer"
    }

    function test_the_marquee_tool_drags_a_range_over_rows_and_a_click_clears_it() {
        const c = setup()
        c.addTrack("audio")
        tryVerify(function () { return c.tracks.rowCount() === 2 })
        const t = createTemporaryObject(tlC, this, { project: c })
        c.tool = "marquee"
        const y0 = t.rulerHeight + t.rowHeight * 0.5, y1 = t.rulerHeight + t.rowHeight * 1.5
        mousePress(t, t.beatsToX(2), y0)
        mouseMove(t, t.beatsToX(4), y1)
        mouseMove(t, t.beatsToX(6), y1)
        mouseRelease(t, t.beatsToX(6), y1)
        verify(c.hasMarquee)
        compare(c.marquee.from, 2)
        compare(c.marquee.to, 6)
        compare(c.marquee.row0, 0)
        compare(c.marquee.row1, 1)
        mouseClick(t, t.beatsToX(10), y0)                 // a click without a drag drops it
        verify(!c.hasMarquee)
        c.tool = "pointer"
    }

    function test_the_lane_follows_changes_made_elsewhere() {
        const c = setup()
        const id = c.tracks.trackIdAt(0)
        const lane = createTemporaryObject(laneC, this, { project: c, trackId: id, param: "volume" })
        compare(lane.points.length, 0)
        c.setAutomationPoints(id, "volume", [{ beats: 1, value: -3 }, { beats: 2, value: -9 }])
        tryVerify(function () { return lane.points.length === 2 })
        c.undo()
        tryVerify(function () { return lane.points.length === 0 })
    }

    function test_alt_click_deletes_a_point() {
        const c = setup()
        const id = c.tracks.trackIdAt(0)
        c.setAutomationPoints(id, "pan", [{ beats: 2, value: 0.5 }])
        tryVerify(function () { return c.automationPoints(id, "pan").length === 1 })
        const lane = createTemporaryObject(laneC, this, { project: c, trackId: id, param: "pan" })
        mouseClick(lane, 80, lane.valueToY(0.5), Qt.LeftButton, Qt.AltModifier)
        tryVerify(function () { return c.automationPoints(id, "pan").length === 0 })
    }

    function test_the_lanes_appear_over_the_rows_only_while_shown() {
        const c = setup()
        const t = createTemporaryObject(tlC, this, { project: c })
        c.automationVisible = true
        compare(c.automationParam, "volume")
        c.automationParam = "pan"
        compare(c.automationParam, "pan")
        const lanes = []
        const find = function (item) {
            if (item.pointAt !== undefined && item.valueToY !== undefined) lanes.push(item)
            for (let i = 0; i < item.children.length; ++i) find(item.children[i])
        }
        find(t)
        compare(lanes.length, 1)   // one track, one lane
        c.automationVisible = false
        lanes.length = 0
        tryVerify(function () { lanes.length = 0; find(t); return lanes.length === 0 })
        c.automationVisible = true
        c.automationParam = "cutoff"
        compare(c.automationParam, "pan")   // only volume and pan
        c.automationVisible = false
    }
}
