import QtQuick
import QtTest
import Jad

TestCase {
    id: tc
    name: "SmartControls"
    width: 900; height: 300
    visible: true
    when: windowShown

    ProjectController { id: p; audioEnabled: false }
    Component { id: smartC; SmartControls { width: 880; height: 200; project: p } }

    function init() {
        verify(p.newProjectInTempForTest())
        tryVerify(function () { return p.tracks.rowCount() === 0 })  // the new project has replaced the previous one
        p.clearSelection()
        p.addTrack("audio")
        tryVerify(function () { return p.tracks.rowCount() === 1 })
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return p.inspector.hasTrack })
    }

    function test_it_says_so_when_the_track_has_no_patch() {
        var s = createTemporaryObject(smartC, tc)
        verify(s.emptyLabel.visible)
    }
    function test_it_shows_the_controls_of_the_patch_grouped() {
        var s = createTemporaryObject(smartC, tc)
        p.applyPatch("audio.bright-vocal")
        tryVerify(function () { return s.groupList.count === 2 })   // Main and Tone
        verify(!s.emptyLabel.visible)
        compare(s.knobCount, 3)
    }
    function test_releasing_a_knob_moves_the_track_once() {
        var s = createTemporaryObject(smartC, tc)
        p.applyPatch("audio.bright-vocal")
        tryVerify(function () { return s.knobCount === 3 && s.knobFor("level") !== null })
        var k = s.knobFor("level")
        k.released(6)
        tryVerify(function () { return p.inspector.track.gainDb === 6 })
        p.undo()
        tryVerify(function () { return p.inspector.track.gainDb === -2 })
    }
    function test_the_knobs_follow_the_track_when_it_changes_elsewhere() {
        var s = createTemporaryObject(smartC, tc)
        p.applyPatch("audio.bright-vocal")
        tryVerify(function () { return s.knobCount === 3 && s.knobFor("level") !== null })
        p.setGain(p.tracks.trackIdAt(0), -10)
        tryVerify(function () { return s.knobFor("level").value === -10 })
    }
    function test_compare_and_the_eq_tab_are_shown_but_say_they_are_not_there_yet() {
        var s = createTemporaryObject(smartC, tc)
        var got = []
        p.notice.connect(function (m) { got.push(m) })
        wait(200)  // let the layout settle before clicking
        mouseClick(s.compareButton)
        mouseClick(s.eqTab)
        compare(got.length, 2)
    }
}
