import QtQuick
import QtTest
import Jad

TestCase {
    name: "MusicalTyping"
    width: 400; height: 300
    visible: true
    when: windowShown

    Component { id: ctlC; ProjectController { audioEnabled: false } }
    Component { id: winC; MusicalTypingWindow { } }

    function test_keys_press_and_release_notes_and_z_x_move_the_octave() {
        const c = createTemporaryObject(ctlC, this)
        const w = createTemporaryObject(winC, this, { project: c })
        compare(w.baseNote, 60)
        w.keyDown({ key: Qt.Key_A, isAutoRepeat: false, accepted: false })
        compare(w.lit[60], true)
        w.keyDown({ key: Qt.Key_A, isAutoRepeat: true, accepted: false })    // a held key repeats: nothing new
        w.keyDown({ key: Qt.Key_W, isAutoRepeat: false, accepted: false })
        compare(w.lit[61], true)
        w.keyUp({ key: Qt.Key_A, isAutoRepeat: false, accepted: false })
        verify(w.lit[60] === undefined)
        w.keyUp({ key: Qt.Key_W, isAutoRepeat: false, accepted: false })
        w.keyDown({ key: Qt.Key_X, isAutoRepeat: false, accepted: false })
        compare(w.baseNote, 72)
        w.keyDown({ key: Qt.Key_Z, isAutoRepeat: false, accepted: false })
        w.keyDown({ key: Qt.Key_Z, isAutoRepeat: false, accepted: false })
        compare(w.baseNote, 48)
        w.keyDown({ key: Qt.Key_V, isAutoRepeat: false, accepted: false })
        compare(w.velocity, 90)
        w.keyDown({ key: Qt.Key_Q, isAutoRepeat: false, accepted: false })    // not a key of the keyboard
        compare(Object.keys(w.lit).length, 0)
    }
}
