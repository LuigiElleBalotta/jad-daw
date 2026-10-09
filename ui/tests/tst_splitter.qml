import QtQuick
import QtTest
import Jad

TestCase {
    name: "Splitter"
    width: 300; height: 300
    visible: true
    when: windowShown

    Component { id: h; Splitter { height: 200 } }
    Component { id: v; Splitter { orientation: Qt.Vertical; width: 200 } }

    function test_a_horizontal_splitter_reports_the_horizontal_movement() {
        var s = createTemporaryObject(h, this)
        var got = []
        s.dragged.connect(function (d) { got.push(d) })
        compare(s.width, 5)
        mousePress(s, 2, 50)
        mouseMove(s, 12, 50, 0, Qt.LeftButton)
        mouseRelease(s, 12, 50)
        verify(got.length > 0)
        verify(got.reduce(function (a, b) { return a + b }, 0) > 0)
    }
    function test_a_vertical_splitter_is_a_thin_bar_and_reports_the_vertical_movement() {
        var s = createTemporaryObject(v, this)
        var got = []
        s.dragged.connect(function (d) { got.push(d) })
        compare(s.height, 5)
        mousePress(s, 50, 2)
        mouseMove(s, 50, 22, 0, Qt.LeftButton)
        mouseRelease(s, 50, 22)
        verify(got.length > 0)
        verify(got.reduce(function (a, b) { return a + b }, 0) > 0)
    }
}
