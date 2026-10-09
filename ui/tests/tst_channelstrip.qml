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
    function test_the_plus_slot_opens_the_insert_menu_and_gain_requests_an_insert() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.insertAddRequested.connect(function (id) { got.push(id) })
        mouseClick(s.addInsertSlot)
        verify(s.insertMenu.visible)
        s.insertMenu.close()
        s.requestGainInsert()
        compare(got, ["t"])
    }
    function test_a_plugin_is_requested_with_its_id_and_name() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.pluginInsertRequested.connect(function (id, pluginId, name) { got.push([id, pluginId, name]) })
        s.requestPluginInsert("vst3:aa", "Verb")
        compare(got, [["t", "vst3:aa", "Verb"]])
    }
    function test_a_plugin_slot_shows_its_label_and_marks_a_missing_plugin() {
        var info = JSON.parse(JSON.stringify(keys))
        info.inserts = [{ processorId: "vst3:aa", gainDb: 0, label: "Verb", plugin: true },
                        { processorId: "vst3:bb", gainDb: 0, label: "Gone", plugin: true }]
        var s = createTemporaryObject(stripC, this, { info: info, knownPluginIds: ["vst3:aa"] })
        var ok = s.insertList.itemAt(0), gone = s.insertList.itemAt(1)
        compare(ok.text, "Verb")
        compare(gone.text, "Gone")
        verify(!ok.missing)
        verify(gone.missing)
        compare(ok.value, "")  // a plug-in has no gain value on the strip
    }
    function test_double_click_on_a_plugin_slot_asks_for_its_editor_and_a_plugin_does_not_drag() {
        var info = JSON.parse(JSON.stringify(keys))
        info.inserts = [{ processorId: "vst3:aa", gainDb: 0, label: "Verb", plugin: true }]
        var s = createTemporaryObject(stripC, this, { info: info, knownPluginIds: ["vst3:aa"] })
        var opened = [], gains = []
        s.insertEditorRequested.connect(function (id, index) { opened.push([id, index]) })
        s.insertGainReleased.connect(function (id, index, db) { gains.push(db) })
        var slot = s.insertList.itemAt(0)
        mouseDoubleClickSequence(slot, 10, 10)
        compare(opened, [["t", 0]])
        mousePress(slot, 10, 10)
        mouseMove(slot, 40, 10, 0, Qt.LeftButton)
        mouseRelease(slot, 40, 10)
        compare(gains.length, 0)
    }
    function test_the_manager_entry_of_the_menu_is_requested() {
        var s = createTemporaryObject(stripC, this)
        var n = 0
        s.pluginManagerRequested.connect(function () { ++n })
        s.insertMenu.managerChosen()
        compare(n, 1)
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

    function test_new_bus_entries_lead_the_send_and_output_menus() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.newBusRequested.connect(function (id, role) { got.push([id, role]) })
        compare(s.sendMenu.itemAt(0).text, "New Bus")
        compare(s.outputMenu.itemAt(0).text, "New Bus")
        s.sendMenu.itemAt(0).triggered()
        s.outputMenu.itemAt(0).triggered()
        compare(got, [["t", "send"], ["t", "output"]])
    }
    function test_send_slot_stays_available_without_targets() {
        var s = createTemporaryObject(stripC, this)
        s.targets = []
        verify(s.sendMenu.count >= 1)   // New Bus is always there
    }
    function test_strip_name_selects_the_track_with_the_modifiers() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.selectRequested.connect(function (id, mods) { got.push([id, mods]) })
        mouseClick(s.stripName)
        mouseClick(s.stripName, 5, 5, Qt.LeftButton, Qt.ShiftModifier)
        compare(got.length, 2)
        compare(got[0], ["t", Qt.NoModifier])
        compare(got[1], ["t", Qt.ShiftModifier])
    }
    function test_the_master_name_does_not_select() {
        var s = createTemporaryObject(stripC, this)
        s.info = { trackId: "m", name: "Master", color: "purple", kind: "master", master: true, gainDb: 0, pan: 0, inserts: [], sends: [] }
        var got = []
        s.selectRequested.connect(function (id, mods) { got.push(id) })
        mouseClick(s.stripName)
        compare(got.length, 0)
    }
    function test_double_click_on_the_strip_name_renames() {
        var s = createTemporaryObject(stripC, this)
        var names = []
        s.renameRequested.connect(function (id, name) { names.push([id, name]) })
        mouseDoubleClickSequence(s.stripName)
        verify(s.renaming)
        s.nameInput.text = "Lead"
        keyClick(Qt.Key_Return)
        compare(names, [["t", "Lead"]])
        verify(!s.renaming)
    }
    function test_escape_cancels_and_a_click_elsewhere_confirms_the_strip_name() {
        var s = createTemporaryObject(stripC, this)
        var names = []
        s.renameRequested.connect(function (id, name) { names.push(name) })
        mouseDoubleClickSequence(s.stripName)
        s.nameInput.text = "Nope"
        keyClick(Qt.Key_Escape)
        compare(names.length, 0)
        mouseDoubleClickSequence(s.stripName)
        s.nameInput.text = "Pad"
        s.nameInput.focus = false
        compare(names, ["Pad"])
    }
    function test_the_master_name_cannot_be_renamed() {
        var s = createTemporaryObject(stripC, this)
        s.info = { trackId: "m", name: "Master", color: "purple", kind: "master", master: true, gainDb: 0, pan: 0, inserts: [], sends: [] }
        mouseDoubleClickSequence(s.stripName)
        verify(!s.renaming)
    }
    function test_a_cancelled_knob_gesture_shows_the_model_value_again() {
        var s = createTemporaryObject(stripC, this)
        mousePress(s.panKnob, 14, 14)
        mouseMove(s.panKnob, 14, 2, 0, Qt.LeftButton)
        verify(s.panKnob.shown !== s.panKnob.value)
        s.panKnob.cancel()
        compare(s.panKnob.shown, s.panKnob.value)
        mouseRelease(s.panKnob, 14, 2)
    }
    function test_a_send_knob_spans_what_the_core_accepts() {
        var s = createTemporaryObject(stripC, this)
        compare(s.sendList.itemAt(0).knob.from, -96)
        compare(s.sendList.itemAt(0).knob.to, 12)
    }
    function test_a_gain_drag_in_flight_is_dropped_when_the_inserts_change() {
        var s = createTemporaryObject(stripC, this)
        s.dragIndex = 0
        s.dragGain = 9
        var changed = JSON.parse(JSON.stringify(keys))
        changed.inserts = []
        s.info = changed
        compare(s.dragIndex, -1)
    }
    function test_the_instrument_slot_asks_for_the_library() {
        var s = createTemporaryObject(stripC, this)
        var asked = 0
        s.libraryRequested.connect(function () { asked++ })
        mouseClick(s.instrumentSlot)
        compare(asked, 1)
    }
}
