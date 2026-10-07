import QtQuick
import QtTest
import Jad

TestCase {
    name: "Timeline"
    width: 900; height: 400
    visible: true
    when: windowShown

    Component { id: tlC; Timeline { anchors.fill: parent; project: ProjectController { } } }

    function test_beat_x_roundtrip() {
        var t = createTemporaryObject(tlC, this)
        t.pixelsPerBeat = 40; t.scrollBeats = 4
        compare(t.xToBeats(t.beatsToX(10)), 10)
        compare(t.beatsToX(4), 0)
    }
    function test_zoom_clamps() {
        var t = createTemporaryObject(tlC, this)
        t.zoomBy(1e9); verify(t.pixelsPerBeat <= t.maxPixelsPerBeat)
        t.zoomBy(1e-9); verify(t.pixelsPerBeat >= t.minPixelsPerBeat)
    }
    function test_scroll_never_negative() {
        var t = createTemporaryObject(tlC, this)
        t.scrollByBeats(-100); compare(t.scrollBeats, 0)
    }
}
