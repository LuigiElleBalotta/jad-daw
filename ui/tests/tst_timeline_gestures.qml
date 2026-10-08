import QtQuick
import QtTest
import Jad

// Mouse gestures on the timeline: pencil, rubber band selection, follow playhead.
TestCase {
    name: "TimelineGestures"
    width: 900; height: 400
    visible: true
    when: windowShown

    Component { id: ctlC; ProjectController { audioEnabled: false } }
    Component { id: tlC; Timeline { width: 900; height: 400 } }

    // A new project in a temporary folder, with one instrument (MIDI) track: its id is the only track.
    function openProjectWithInstrument() {
        const c = createTemporaryObject(ctlC, this)
        verify(c.newProjectInTempForTest())
        c.addTrack("instrument")
        tryVerify(function () { return c.tracks.rowCount() === 1 })
        return c
    }
    // The value of one region field in a row of the regions model; the roles are those of region_model.h.
    function regionField(model, row, name) {
        const roles = { regionId: 257, trackId: 258, startBeats: 260, lengthBeats: 261, isAudio: 262 }
        return model.data(model.index(row, 0), roles[name])
    }

    function test_pencil_drag_draws_a_region_of_the_snapped_length() {
        const c = openProjectWithInstrument()
        const t = createTemporaryObject(tlC, this, { project: c })
        c.tool = "pencil"
        c.snap = "quarter"  // one beat
        const y = t.rulerHeight + t.rowHeight / 2
        mousePress(t, 80, y)        // beat 2
        mouseMove(t, 170, y)
        mouseMove(t, 250, y)        // 6.25 beats: snapped to 6
        compare(c.regions.rowCount(), 0)  // nothing is created before the release
        mouseRelease(t, 250, y)
        tryVerify(function () { return c.regions.rowCount() === 1 })
        compare(regionField(c.regions, 0, "trackId"), c.tracks.trackIdAt(0))
        compare(regionField(c.regions, 0, "startBeats"), 2)
        compare(regionField(c.regions, 0, "lengthBeats"), 4)
        compare(regionField(c.regions, 0, "isAudio"), false)
    }

    function test_pencil_click_draws_one_bar() {
        const c = openProjectWithInstrument()
        const t = createTemporaryObject(tlC, this, { project: c })
        c.tool = "pencil"
        c.snap = "off"
        const y = t.rulerHeight + t.rowHeight / 2
        mouseClick(t, 120, y)
        tryVerify(function () { return c.regions.rowCount() === 1 })
        compare(regionField(c.regions, 0, "lengthBeats"), c.barBeats)
    }

    function test_rubber_band_selects_exactly_the_regions_it_crosses() {
        const c = openProjectWithInstrument()
        const id = c.tracks.trackIdAt(0)
        c.createRegion(id, 0, 4)
        c.createRegion(id, 8, 4)
        c.createRegion(id, 16, 4)
        tryVerify(function () { return c.regions.rowCount() === 3 })
        const t = createTemporaryObject(tlC, this, { project: c })
        c.tool = "pointer"
        const y = t.rulerHeight + t.rowHeight / 2
        mousePress(t, 560, y)       // beat 14: empty space between the second and the third region
        mouseMove(t, 300, y)
        mouseMove(t, 80, y)         // beat 2: the band is now beats 2 to 14
        mouseRelease(t, 80, y)
        const expected = []
        for (let i = 0; i < c.regions.rowCount(); ++i)
            if (regionField(c.regions, i, "startBeats") < 14 && regionField(c.regions, i, "startBeats") + regionField(c.regions, i, "lengthBeats") > 2)
                expected.push(regionField(c.regions, i, "regionId"))
        compare(expected.length, 2)
        compare(c.selectedRegionIds.length, 2)
        for (const wanted of expected) verify(c.selectedRegionIds.indexOf(wanted) >= 0)
    }

    function test_follow_playhead_scrolls_to_keep_the_playhead_in_view() {
        const c = createTemporaryObject(ctlC, this)
        const t = createTemporaryObject(tlC, this, { project: c })
        c.followPlayhead = true
        c.simulatePlaybackForTest(true, 100)  // beat 100 is far right of the view
        let x = t.beatsToX(100)
        verify(x >= 0 && x <= t.width, "playhead in view after moving right: " + x)
        c.simulatePlaybackForTest(true, 10)   // and back to the left of the view
        x = t.beatsToX(10)
        verify(x >= 0 && x <= t.width, "playhead in view after moving left: " + x)
        const scrolled = t.scrollBeats
        c.followPlayhead = false
        c.simulatePlaybackForTest(true, 300)
        compare(t.scrollBeats, scrolled)  // not followed: the view stays where it was
        c.simulatePlaybackForTest(false, 300)
    }
}
