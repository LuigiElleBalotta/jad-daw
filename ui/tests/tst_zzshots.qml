import QtQuick
import QtTest
import Jad

TestCase {
    name: "Shots"
    width: 900; height: 700
    visible: true
    when: windowShown
    Component { id: ctlC; ProjectController { audioEnabled: false } }
    Component { id: winC; EffectEditorWindow { } }
    function shot(processor, file) {
        const c = createTemporaryObject(ctlC, this)
        verify(c.newProjectInTempForTest())
        c.addTrack("audio")
        tryVerify(function () { return c.tracks.rowCount() === 1 })
        const id = c.tracks.trackIdAt(0)
        c.addInsert(id, processor)
        tryVerify(function () { return c.trackInserts(id).length === 1 })
        const w = createTemporaryObject(winC, this, { project: c })
        c.openEffectEditor(id, 0)
        tryVerify(function () { return w.spec !== null })
        wait(500)
        const img = grabImage(w.contentItem)
        img.save("C:/Users/l.balotta/AppData/Local/Temp/" + file)
    }
    function test_shots() {
        shot("builtin.eq", "shot_eq.png")
        shot("builtin.compressor", "shot_comp.png")
        shot("builtin.delay", "shot_delay.png")
    }
}
