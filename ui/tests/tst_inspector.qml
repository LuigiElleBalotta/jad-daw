import QtQuick
import QtTest
import Jad

TestCase {
    id: tc
    name: "Inspector"
    width: 600; height: 900
    visible: true
    when: windowShown

    ProjectController { id: p; audioEnabled: false }
    Component { id: inspC; Inspector { width: 240; height: 880; project: p } }

    function init() {
        verify(p.newProjectInTempForTest())
        tryVerify(function () { return p.tracks.rowCount() === 0 })  // the new project has replaced the previous one
        p.clearSelection()
        p.addTrack("audio")
        p.addTrack("instrument")
        tryVerify(function () { return p.tracks.rowCount() === 2 })
    }

    function test_it_says_so_when_nothing_is_selected() {
        var i = createTemporaryObject(inspC, tc)
        verify(i.emptyLabel.visible)
        verify(!i.trackSection.visible)
        verify(!i.regionSection.visible)
    }
    function test_it_shows_the_selected_track_and_renames_it() {
        var i = createTemporaryObject(inspC, tc)
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return i.trackSection.visible })
        verify(!i.emptyLabel.visible)
        i.trackSection.commitName("Lead")
        tryVerify(function () { return p.inspector.track.name === "Lead" })
    }
    function test_a_colour_swatch_recolours_the_track() {
        var i = createTemporaryObject(inspC, tc)
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return i.trackSection.visible })
        i.trackSection.swatchList.itemAt(6).clicked()   // orange
        tryVerify(function () { return p.inspector.track.color === "orange" })
    }
    function test_the_region_section_edits_the_gain_of_the_region() {
        var i = createTemporaryObject(inspC, tc)
        var id = p.tracks.trackIdAt(1)
        p.createRegion(id, 0, 4)
        tryVerify(function () { return p.regions.rowCount() > 0 })
        p.selectRegion(p.regions.regionIdAt(0), "replace")
        tryVerify(function () { return i.regionSection.visible })
        i.regionSection.gainField.committed(-6)
        tryVerify(function () { return p.inspector.region.gainDb === -6 })
    }
    function test_visual_only_fields_announce_themselves_when_switched_on() {
        var i = createTemporaryObject(inspC, tc)
        var id = p.tracks.trackIdAt(1)
        p.createRegion(id, 0, 4)
        tryVerify(function () { return p.regions.rowCount() > 0 })
        p.selectRegion(p.regions.regionIdAt(0), "replace")
        tryVerify(function () { return i.regionSection.visible })
        var got = []
        p.notice.connect(function (m) { got.push(m) })
        wait(200)  // let the layout settle before clicking
        mouseClick(i.regionSection.loopCheck)
        compare(got.length, 1)
        verify(got[0].indexOf("not implemented yet") >= 0)
        mouseClick(i.regionSection.loopCheck)   // off: no notice
        compare(got.length, 1)
    }
    function test_the_sections_collapse() {
        var i = createTemporaryObject(inspC, tc)
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return i.trackSection.visible })
        var h = i.trackSection.header
        verify(h.expanded)
        wait(200)  // let the layout settle before clicking
        mouseClick(h)
        verify(!h.expanded)
        verify(!i.trackSection.body.visible)
    }
    function test_the_two_strips_show_the_track_and_its_output() {
        var i = createTemporaryObject(inspC, tc)
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return i.trackStrip.visible && i.outputStrip.visible })
        compare(i.trackStrip.trackId, p.tracks.trackIdAt(0))
        verify(i.outputStrip.master)
    }
}
