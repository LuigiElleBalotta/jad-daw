import QtQuick
import QtTest
import Jad

TestCase {
    name: "ControlBar"
    width: 800; height: 200
    visible: true
    when: windowShown

    Component { id: lcdC; Lcd { project: ProjectController { audioEnabled: false } } }

    function enter(cell, text) {
        mouseDoubleClickSequence(cell)
        verify(cell.editing)
        cell.input.text = text
        keyClick(Qt.Key_Return)
    }

    function test_invalid_tempo_emits_a_message_and_keeps_the_value() {
        var l = createTemporaryObject(lcdC, this)
        var msgs = []
        l.message.connect(function (t) { msgs.push(t) })
        var before = l.tempoCell.shown
        enter(l.tempoCell, "abc")
        compare(msgs.length, 1)
        compare(l.tempoCell.shown, before)
        verify(!l.tempoCell.editing)
    }
    function test_out_of_range_tempo_is_refused() {
        var l = createTemporaryObject(lcdC, this)
        var msgs = []
        l.message.connect(function (t) { msgs.push(t) })
        enter(l.tempoCell, "1000")
        enter(l.tempoCell, "19")
        compare(msgs.length, 2)
    }
    function test_valid_tempo_is_accepted_without_a_message() {
        var l = createTemporaryObject(lcdC, this)
        var msgs = []
        l.message.connect(function (t) { msgs.push(t) })
        enter(l.tempoCell, "95")
        compare(msgs.length, 0)
        verify(!l.tempoCell.editing)
    }
    function test_invalid_signature_and_position_are_refused() {
        var l = createTemporaryObject(lcdC, this)
        var msgs = []
        l.message.connect(function (t) { msgs.push(t) })
        enter(l.signatureCell, "7/9")
        enter(l.signatureCell, "")
        enter(l.positionCell, "x")
        compare(msgs.length, 3)
    }
    function test_the_signature_and_key_cells_open_lists_to_choose_from() {
        var l = createTemporaryObject(lcdC, this)
        verify(l.project.newProjectInTempForTest())
        const sigMenu = findChild(l, "signatureMenu")
        const keyMenu = findChild(l, "keyMenu")
        verify(sigMenu !== null && keyMenu !== null)
        compare(sigMenu.count, 6 + 1 + 7 + 1 + 1)   // common, separator, other, separator, Custom…
        compare(keyMenu.count, 12 + 1 + 12)
        // a key from the list is the project key; the one that is current is checked
        keyMenu.itemAt(5).triggered()   // C major
        tryCompare(l.project, "projectKey", "C maj")
        keyMenu.itemAt(13 + 5).triggered()   // A minor
        tryCompare(l.project, "projectKey", "A min")
    }
    function test_a_signature_from_the_list_asks_whether_to_change_or_insert() {
        var l = createTemporaryObject(lcdC, this)
        verify(l.project.newProjectInTempForTest())
        const sigMenu = findChild(l, "signatureMenu")
        const ask = findChild(l, "signatureAsk")
        sigMenu.itemAt(0).triggered()   // 3/4
        tryVerify(function () { return ask.opened })
        findChild(ask.contentItem, "signatureChange").clicked()
        tryVerify(function () { return !ask.opened })
        tryCompare(l.project, "signatureText", "3/4")
        // "Don't ask again" goes straight to changing the project signature
        sigMenu.itemAt(2).triggered()   // 5/4
        tryVerify(function () { return ask.opened })
        ask.dontAsk = true
        findChild(ask.contentItem, "signatureChange").clicked()
        tryCompare(l.project, "signatureText", "5/4")
        sigMenu.itemAt(1).triggered()   // 4/4: no question now
        tryCompare(l.project, "signatureText", "4/4")
        verify(!ask.opened)
    }
    function test_escape_cancels_the_edit() {
        var l = createTemporaryObject(lcdC, this)
        var msgs = []
        l.message.connect(function (t) { msgs.push(t) })
        mouseDoubleClickSequence(l.tempoCell)
        l.tempoCell.input.text = "abc"
        keyClick(Qt.Key_Escape)
        verify(!l.tempoCell.editing)
        compare(msgs.length, 0)
    }
    function test_a_click_on_the_position_switches_between_bars_and_time() {
        var l = createTemporaryObject(lcdC, this)
        verify(!l.showTime)
        mouseClick(l.positionCell)
        tryVerify(function () { return l.showTime })
        mouseClick(l.positionCell)
        tryVerify(function () { return !l.showTime })
    }
}
