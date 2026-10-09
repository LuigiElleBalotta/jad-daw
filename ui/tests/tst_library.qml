import QtQuick
import QtTest
import Jad

TestCase {
    id: tc
    name: "Library"
    width: 400; height: 700
    visible: true
    when: windowShown

    ProjectController { id: p; audioEnabled: false }
    Component { id: libC; Library { width: 240; height: 680; project: p } }

    function init() {
        verify(p.newProjectInTempForTest())
        tryVerify(function () { return p.tracks.rowCount() === 0 })  // the new project has replaced the previous one
        p.clearSelection()
        p.addTrack("audio")
        tryVerify(function () { return p.tracks.rowCount() === 1 })
    }

    function test_it_asks_for_a_track_when_none_is_selected() {
        var l = createTemporaryObject(libC, tc)
        verify(l.emptyLabel.visible)
    }
    function test_it_lists_the_categories_and_patches_of_the_track_kind() {
        var l = createTemporaryObject(libC, tc)
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return l.categoryList.count >= 2 })
        verify(l.patchList.count >= 2)
        verify(!l.emptyLabel.visible)
    }
    function test_clicking_a_patch_applies_it_and_it_becomes_current() {
        var l = createTemporaryObject(libC, tc)
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return l.patchList.itemAtIndex(1) !== null })
        l.patchList.itemAtIndex(1).clicked()
        tryVerify(function () { return p.library.currentPatchId !== "" })
        tryVerify(function () { return p.inspector.track.patchId === p.library.currentPatchId })
    }
    function test_arrow_keys_step_through_the_patches() {
        var l = createTemporaryObject(libC, tc)
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return l.patchList.count >= 2 })
        l.step(1)
        tryVerify(function () { return p.library.currentPatchId !== "" })
        var first = p.library.currentPatchId
        l.step(1)
        tryVerify(function () { return p.library.currentPatchId !== first })
        l.step(-1)
        tryVerify(function () { return p.library.currentPatchId === first })
    }
    function test_search_filters_and_revert_restores_the_patch() {
        var l = createTemporaryObject(libC, tc)
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return l.patchList.count >= 2 })
        l.searchField.text = "guitar"
        tryVerify(function () { return l.patchList.count === 2 && l.patchList.itemAtIndex(0) !== null })
        l.patchList.itemAtIndex(0).clicked()
        tryVerify(function () { return p.inspector.track.patchId === "audio.warm-guitar" })
        p.setGain(p.tracks.trackIdAt(0), 5)
        tryVerify(function () { return p.inspector.track.gainDb === 5 })
        l.revertButton.clicked()
        tryVerify(function () { return p.inspector.track.gainDb === -4 })
    }
    function test_save_and_delete_are_disabled_for_now() {
        var l = createTemporaryObject(libC, tc)
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return l.saveButton.visible })
        verify(!l.saveButton.enabled)
        verify(!l.deleteButton.enabled)
        var got = []
        p.notice.connect(function (m) { got.push(m) })
        wait(200)
        verify(l.saveButton.x + l.saveButton.width <= l.width)  // the buttons still fit in the column
        mouseClick(l.saveButton)
        mouseClick(l.deleteButton)
        compare(got.length, 0)
    }
    SignalSpy { id: patchesSpy; target: p.library; signalName: "patchesChanged" }
    function test_applying_a_patch_does_not_rebuild_the_patch_list() {
        var l = createTemporaryObject(libC, tc)
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return l.patchList.count >= 2 && l.patchList.itemAtIndex(1) !== null })
        patchesSpy.clear()
        l.patchList.itemAtIndex(1).clicked()
        tryVerify(function () { return p.library.currentPatchId !== "" })
        compare(patchesSpy.count, 0)  // the list only moves its highlight: it keeps its scroll position
    }
    function test_the_footer_counts_the_patches_and_mentions_problems_only_when_there_are_some() {
        var l = createTemporaryObject(libC, tc)
        verify(l.patchCountLabel.text.indexOf("Built-in patches") === 0)
        verify(l.patchCountLabel.text.indexOf("problem") < 0)
    }
}
