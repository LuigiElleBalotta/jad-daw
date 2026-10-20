import QtQuick
import QtTest
import Jad

TestCase {
    name: "MetronomeSettings"
    width: 700; height: 700
    visible: true
    when: windowShown

    Component { id: ctlC; ProjectController { audioEnabled: false } }
    Component { id: paneC; MetronomeSettings { width: 600 } }

    function test_the_page_shows_the_mode_and_changes_it() {
        const c = createTemporaryObject(ctlC, this)
        const p = createTemporaryObject(paneC, this, { project: c })
        verify(p)
        compare(c.clickMode, "beats")
        c.clickMode = "grouped"
        compare(c.clickMode, "grouped")
        compare(p.levels.length, 4)
        compare(p.noteName(91), "G5")   // Logic's naming: 60 is C3
        compare(p.noteName(60), "C3")
    }
}
