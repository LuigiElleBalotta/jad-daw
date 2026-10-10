import QtQuick
import QtQuick.Controls.Basic
import QtTest
import Jad

// The small dialogs: ask for a number, search a track by name.
TestCase {
    name: "Dialogs"
    width: 800; height: 600
    visible: true
    when: windowShown

    Component { id: ctlC; ProjectController { audioEnabled: false } }
    Component { id: promptC; NumberPromptDialog { parent: Overlay.overlay; heading: "Repeat"; prompt: "Copies"; value: 4; from: 1; to: 64 } }
    Component { id: searchC; SearchTrackDialog { parent: Overlay.overlay } }

    function test_the_number_prompt_returns_a_clamped_number() {
        const d = createTemporaryObject(promptC, this)
        let got = -1
        d.accepted2.connect(function (v) { got = v })
        d.open()
        tryCompare(d, "visible", true)
        const field = findChild(d.contentItem, "numberField")
        compare(field.text, "4")
        field.text = "500"
        keyClick(Qt.Key_Return)
        tryCompare(d, "visible", false)
        compare(got, 64)                              // above the limit: brought back
    }

    function test_search_finds_a_track_by_part_of_its_name_and_selects_it() {
        const c = createTemporaryObject(ctlC, this)
        verify(c.newProjectInTempForTest())
        c.addTrack("audio")
        c.addTrack("instrument")
        tryVerify(function () { return c.tracks.rowCount() === 2 })
        const d = createTemporaryObject(searchC, this, { project: c })
        d.open()
        tryCompare(d, "visible", true)
        compare(d.matches.length, 2)
        const field = findChild(d.contentItem, "searchField")
        field.text = "instr"
        compare(d.matches.length, 1)
        compare(d.matches[0].kind, "instrument")
        keyClick(Qt.Key_Return)
        tryCompare(d, "visible", false)
        tryVerify(function () { return c.selectedTrackIds.length === 1 && c.selectedTrackIds[0] === d.matches[0].id })
    }
}
