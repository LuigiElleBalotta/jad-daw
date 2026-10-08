import QtQuick
import QtTest
import Jad

TestCase {
    name: "Mixer"
    width: 200; height: 700
    visible: true
    when: windowShown

    Component { id: stripC; ChannelStrip { width: 96; height: 640; showSlots: false
        info: ({ trackId: "t", name: "Keys", color: "purple", kind: "instrument", master: false, gainDb: 0, pan: 0,
                 mute: false, solo: false, inserts: [], sends: [] }) } }

    function test_gain_released_once_after_drag() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.gainReleased.connect(function (id, v) { got.push([id, v]) })
        mousePress(s.fader, 10, 100); mouseMove(s.fader, 10, 60, 0, Qt.LeftButton)
        compare(got.length, 0)
        mouseRelease(s.fader, 10, 60)
        compare(got.length, 1)
        compare(got[0][0], "t")
    }
    function test_mute_toggle_emits() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.muteToggled.connect(function (id, on) { got.push([id, on]) })
        mouseClick(s.muteButton)
        compare(got.length, 1)
        compare(got[0][1], true)
    }
}
