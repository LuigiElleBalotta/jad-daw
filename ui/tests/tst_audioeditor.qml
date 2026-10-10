import QtQuick
import QtTest
import Jad

TestCase {
    name: "AudioEditor"
    width: 900; height: 400
    visible: true
    when: windowShown

    Component { id: ctlC; ProjectController { audioEnabled: false } }
    Component { id: edC; AudioEditor { width: 900; height: 300 } }

    function test_without_an_audio_region_it_asks_for_one_and_the_selection_logic_works() {
        const c = createTemporaryObject(ctlC, this)
        const e = createTemporaryObject(edC, this, { project: c })
        verify(!e.valid)
        compare(e.hasSelection, false)
        e.selFrom = 100
        e.selTo = 50
        compare(e.hasSelection, false)          // backwards: not a selection
        e.selTo = 300
        compare(e.hasSelection, true)
        compare(e.seconds(48000 * 3), "3.00 s")
        e.fileMode = true
        compare(e.hasSelection, false)          // changing the view clears it
    }
}
