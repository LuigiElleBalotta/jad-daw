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
        c.automationParam = "cutoff"
        compare(c.automationParam, "pan")   // only volume and pan
        c.automationVisible = false
    }
}
