import QtQuick
import QtTest
import Jad

TestCase {
    name: "ChannelStrip"
    width: 300; height: 700
    visible: true
    when: windowShown

    readonly property var keys: ({ trackId: "t", name: "Keys", color: "purple", kind: "instrument", master: false,
                                   patchName: "Sine Lead", instrument: "builtin.sine", gainDb: 0, pan: 0, mute: false, solo: false,
                                   outputName: "Stereo Out",
                                   inserts: [{ processorId: "builtin.gain", gainDb: 3 }],
                                   sends: [{ id: "s1", targetId: "b", targetName: "Bus", levelDb: -12, preFader: false }] })
    Component { id: stripC; ChannelStrip { width: 96; height: 640; info: keys; targets: [{ id: "b", name: "Bus" }] } }

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
    function test_mute_and_solo_emit_with_the_new_state() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.muteToggled.connect(function (id, on) { got.push(["m", id, on]) })
        s.soloToggled.connect(function (id, on) { got.push(["s", id, on]) })
        mouseClick(s.muteButton); mouseClick(s.soloButton)
        compare(got, [["m", "t", true], ["s", "t", true]])
    }
    function test_the_plus_slot_requests_an_insert() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.insertAddRequested.connect(function (id) { got.push(id) })
        mouseClick(s.addInsertSlot)
        compare(got, ["t"])
    }
    function test_dragging_an_insert_changes_its_gain_once_on_release() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.insertGainReleased.connect(function (id, index, db) { got.push([id, index, db]) })
        var slot = s.insertList.itemAt(0)
        mousePress(slot, 10, 10)
        mouseMove(slot, 40, 10, 0, Qt.LeftButton)
        compare(got.length, 0)
        mouseRelease(slot, 40, 10)
        compare(got.length, 1)
        compare(got[0][1], 0)
        verify(got[0][2] > 3)
    }
    function test_the_cross_of_an_insert_removes_it() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.insertRemoveRequested.connect(function (id, index) { got.push([id, index]) })
        var slot = s.insertList.itemAt(0)
        mouseMove(slot, 10, 10)
        mouseClick(slot, slot.width - 6, 10)
        compare(got, [["t", 0]])
    }
    function test_a_send_knob_release_sets_its_level_and_the_cross_removes_it() {
        var s = createTemporaryObject(stripC, this)
        var levels = [], removed = []
        s.sendLevelReleased.connect(function (id, db) { levels.push([id, db]) })
        s.sendRemoveRequested.connect(function (id) { removed.push(id) })
        var row = s.sendList.itemAt(0)
        row.knob.released(-6)
        compare(levels, [["s1", -6]])
        var slot = row.slot
        mouseMove(slot, 10, 10)
        mouseClick(slot, slot.width - 6, 10)
        compare(removed, ["s1"])
    }
    function test_output_and_send_menus_emit_the_chosen_target() {
        var s = createTemporaryObject(stripC, this)
        var out = [], snd = []
        s.outputRequested.connect(function (id, o) { out.push([id, o]) })
        s.sendAddRequested.connect(function (id, t) { snd.push([id, t]) })
        s.requestOutput("b"); s.requestOutput("")
        s.requestSend("b")
        compare(out, [["t", "b"], ["t", ""]])
        compare(snd, [["t", "b"]])
    }
    function test_slots_hide_when_asked_and_on_the_master() {
        var s = createTemporaryObject(stripC, this, { showSlots: false })
        verify(!s.addInsertSlot.visible)
        verify(!s.outputSlot.visible)
        var m = createTemporaryObject(stripC, this, { info: { trackId: "m", name: "Stereo Out", color: "purple", kind: "master", master: true, gainDb: 0, pan: 0, inserts: [], sends: [] } })
        verify(!m.addInsertSlot.visible)
    }
    function test_group_and_automation_slots_announce_themselves() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.stubUsed.connect(function (label) { got.push(label) })
        mouseClick(s.groupSlot)
        mouseClick(s.automationSlot)
        compare(got, ["Group", "Automation"])
    }
    function test_changing_track_mid_drag_sends_nothing() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.gainReleased.connect(function (id, v) { got.push([id, v]) })
        mousePress(s.fader, 10, 100); mouseMove(s.fader, 10, 60, 0, Qt.LeftButton)
        var other = JSON.parse(JSON.stringify(keys)); other.trackId = "u"
        s.info = other
        mouseRelease(s.fader, 10, 60)
        compare(got.length, 0)
    }
    function test_a_plain_click_on_a_knob_sends_nothing() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.panReleased.connect(function (id, v) { got.push([id, v]) })
        mouseClick(s.panKnob)
        compare(got.length, 0)
    }
    function test_double_click_on_a_knob_resets_once() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.panReleased.connect(function (id, v) { got.push([id, v]) })
        mouseDoubleClickSequence(s.panKnob)
        compare(got, [["t", 0]])
    }
}
