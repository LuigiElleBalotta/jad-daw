import QtQuick
import QtTest
import Jad

TestCase {
    name: "MetronomeSettings"
    width: 700; height: 700
    visible: true
    when: windowShown

    Component { id: ctlC; ProjectController { audioEnabled: false } }
    Component { id: dlgC; MetronomeSettings { } }

    function test_the_dialog_shows_the_mode_and_changes_it() {
        const c = createTemporaryObject(ctlC, this)
        const d = createTemporaryObject(dlgC, this, { project: c, parent: this })
        verify(d)
        d.open()
        tryVerify(function () { return d.opened })
        compare(c.clickMode, "beats")
        c.clickMode = "grouped"
        compare(c.clickMode, "grouped")
        compare(d.slots.length, 17)
        d.close()
    }
}
