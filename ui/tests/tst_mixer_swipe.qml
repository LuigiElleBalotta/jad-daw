import QtQuick
import QtTest
import Jad

// A press on M and a drag across the other strips sets them all to the same state.
TestCase {
    name: "MixerSwipe"
    width: 1200; height: 700
    visible: true
    when: windowShown

    Component { id: ctlC; ProjectController { audioEnabled: false } }
    Component { id: mvC; MixerView { width: 1200; height: 700 } }

    function test_swipe_over_mute_buttons_mutes_each_strip_once() {
        const c = createTemporaryObject(ctlC, this)
        verify(c.newProjectInTempForTest())
        c.addTrack("audio"); c.addTrack("audio"); c.addTrack("audio")
        tryVerify(function () { return c.tracks.rowCount() === 3 })
        const v = createTemporaryObject(mvC, this, { project: c })
        wait(200)
        const strips = []
        function findButtons(item, out) {
            if (item.muteButton && item.visible && item.width > 0 && !out.some(o => o === item)) out.push(item)
            for (let i = 0; i < item.children.length; ++i) findButtons(item.children[i], out)
        }
        findButtons(v, strips)
        verify(strips.length >= 3)
        const centre = (s) => s.muteButton.mapToItem(v, s.muteButton.width / 2, s.muteButton.height / 2)
        const a = centre(strips[0]), b = centre(strips[1]), d = centre(strips[2])
        mousePress(v, a.x, a.y)
        mouseMove(v, b.x, b.y)
        mouseMove(v, d.x, d.y)
        mouseRelease(v, d.x, d.y)
        tryVerify(function () { return strips[0].mute && strips[1].mute && strips[2].mute })
    }
}
