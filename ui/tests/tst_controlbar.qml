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
        verify(l.showTime)
        mouseClick(l.positionCell)
        verify(!l.showTime)
    }
}
