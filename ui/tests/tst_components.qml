import QtQuick
import QtTest
import Jad

TestCase {
    name: "Components"
    width: 400; height: 300
    visible: true
    when: windowShown

    Component { id: faderC; Fader { width: 28; height: 200 } }
    Component { id: buttonC; IconButton { width: 28; height: 28; toggle: true } }

    function test_fader_released_reports_value_once() {
        var f = createTemporaryObject(faderC, this)
        var released = []
        f.released.connect(function (v) { released.push(v) })
        mousePress(f, 14, 100)
        mouseMove(f, 14, 60)
        mouseMove(f, 14, 40)
        compare(released.length, 0)           // nothing while dragging
        mouseRelease(f, 14, 40)
        compare(released.length, 1)
        verify(released[0] <= f.to && released[0] >= f.from)
    }
    function test_fader_clamps_to_range() {
        var f = createTemporaryObject(faderC, this)
        var last
        f.released.connect(function (v) { last = v })
        mousePress(f, 14, 100); mouseMove(f, 14, -500); mouseRelease(f, 14, -500)
        compare(last, f.to)
        mousePress(f, 14, 100); mouseMove(f, 14, 900); mouseRelease(f, 14, 900)
        compare(last, f.from)
    }
    function test_toggle_button_flips_active() {
        var b = createTemporaryObject(buttonC, this)
        compare(b.active, false)
        mouseClick(b)
        compare(b.active, true)
        mouseClick(b)
        compare(b.active, false)
    }
}
