import QtQuick
import QtTest
import Jad

TestCase {
    name: "Effects"
    width: 600; height: 400
    visible: true
    when: windowShown

    Component { id: sliderC; ParamSlider { width: 200; height: 18; from: 20; to: 20000; logarithmic: true; defaultValue: 1000; value: 1000 } }
    Component { id: ctlC; ProjectController { audioEnabled: false } }
    Component { id: winC; EffectEditorWindow { } }

    function test_a_log_slider_moves_by_ratio_and_goes_back_to_its_default() {
        const s = createTemporaryObject(sliderC, this)
        compare(s.fromFraction(0), 20)
        compare(Math.round(s.fromFraction(1)), 20000)
        verify(Math.abs(s.fromFraction(0.5) - Math.sqrt(20 * 20000)) < 1)       // the middle is the geometric mean
        verify(Math.abs(s.toFraction(s.fromFraction(0.37)) - 0.37) < 1e-9)
        var got = []
        s.released.connect(function (v) { got.push(v) })
        mouseClick(s, 100, 9, Qt.LeftButton, Qt.AltModifier)
        compare(got, [1000])
    }

    function test_the_editor_window_follows_the_insert_it_edits() {
        const c = createTemporaryObject(ctlC, this)
        verify(c.newProjectInTempForTest())
        c.addTrack("audio")
        tryVerify(function () { return c.tracks.rowCount() === 1 })
        const id = c.tracks.trackIdAt(0)
        c.addInsert(id, "builtin.eq")
        tryVerify(function () { return c.trackInserts(id).length === 1 })
        const w = createTemporaryObject(winC, this, { project: c })
        c.openEffectEditor(id, 0)
        tryVerify(function () { return w.spec !== null })
        compare(w.spec.name, "Channel EQ")
        compare(w.valueOf(w.spec.params[0]), 100)               // the default of the low shelf frequency
        c.setInsertParam(id, 0, "lowGain", 6)
        tryVerify(function () { return w.valueOf(w.spec.params[1]) === 6 })
        verify(w.curve.length > 10)
        c.closeEffectEditor()
    }
}
