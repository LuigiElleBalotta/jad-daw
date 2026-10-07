import QtQuick
import QtTest
import Jad

TestCase {
    name: "Mixer"
    width: 200; height: 400
    visible: true
    when: windowShown

    Component { id: stripC; MixerStrip { width: 80; height: 360; trackId: "t"; name: "Keys"; gainDb: 0; pan: 0; mute: false; solo: false; master: false } }

    function test_gain_released_once_after_drag() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.gainReleased.connect(function (id, v) { got.push([id, v]) })
        mousePress(s.fader, 10, 100); mouseMove(s.fader, 10, 60)
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
