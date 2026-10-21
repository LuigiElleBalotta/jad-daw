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

    function openInsert(processor) {
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
        return { c: c, w: w, id: id }
    }

    function test_the_channel_eq_window_draws_eight_handles_and_a_drag_is_one_undo_step() {
        const o = openInsert("builtin.eq")
        verify(o.w.graphical)
        const view = findChild(o.w.contentItem, "graphicView")
        tryVerify(function () { return view.item !== null })
        for (const k of ["lc", "ls", "p1", "p2", "p3", "p4", "hs", "hc"]) verify(findChild(view.item, "eqHandle_" + k) !== null)
        o.w.edit("p2Gain", 6)
        o.w.edit("p2Freq", 500)
        o.w.commitMany({ p2Gain: 6, p2Freq: 500 })
        tryVerify(function () { return o.c.trackInserts(o.id)[0].params.p2Gain === 6 && o.c.trackInserts(o.id)[0].params.p2Freq === 500 })
        verify(o.w.curve.length > 10)
        o.c.undo()
        tryVerify(function () { return o.c.trackInserts(o.id)[0].params.p2Gain === undefined || o.c.trackInserts(o.id)[0].params.p2Gain === 0 })
    }

    function test_the_compressor_window_has_the_circuit_menu_and_the_parameter_list_is_one_click_away() {
        const o = openInsert("builtin.compressor")
        const view = findChild(o.w.contentItem, "graphicView")
        tryVerify(function () { return view.item !== null })
        verify(findChild(view.item, "compCircuit_5") !== null)
        o.w.setNow("circuit", 5)
        tryVerify(function () { return o.c.trackInserts(o.id)[0].params.circuit === 5 })
        verify(findChild(o.w.contentItem, "fxShowParameters") !== null)
        o.w.showList = true
        verify(!o.w.graphical)
        o.w.showList = false
        verify(o.w.graphical)
    }

    function test_the_compressor_side_chain_panel_sets_the_filter() {
        const o = openInsert("builtin.compressor")
        const view = findChild(o.w.contentItem, "graphicView")
        tryVerify(function () { return view.item !== null })
        const panel = findChild(view.item, "compSideChain")
        verify(!panel.visible)
        view.item.scOpen = true
        verify(panel.visible)
        o.w.setNow("scState", 2)
        o.w.setNow("scMode", 3)
        tryVerify(function () { const p = o.c.trackInserts(o.id)[0].params; return p.scState === 2 && p.scMode === 3 })
    }

    function test_other_effects_get_a_knob_for_each_parameter() {
        const o = openInsert("builtin.delay")
        const view = findChild(o.w.contentItem, "graphicView")
        tryVerify(function () { return view.item !== null })
        verify(view.item.implicitHeight > 40)
        o.w.edit("time", 400)
        o.w.commit("time", 400)
        tryVerify(function () { return o.c.trackInserts(o.id)[0].params.time === 400 })
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
        const named = (n) => w.spec.params.find(q => q.name === n)
        compare(w.valueOf(named("lsFreq")), 75)                 // the default of the low shelf frequency
        c.setInsertParam(id, 0, "lsGain", 6)
        tryVerify(function () { return w.valueOf(named("lsGain")) === 6 })
        verify(w.curve.length > 10)
        c.closeEffectEditor()
    }
}
