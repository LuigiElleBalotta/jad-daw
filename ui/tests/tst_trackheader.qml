import QtQuick
import QtTest
import Jad

TestCase {
    name: "TrackHeader"
    width: 400; height: 300
    visible: true
    when: windowShown

    Component { id: hdr; TrackHeader {
        width: 230
        trackId: "t1"; trackName: "Keys"; kind: "instrument"; trackColor: "purple"; number: 1
        gainDb: -6; pan: 0; mute: false; solo: false; selected: false
    } }

    function test_compact_height_hides_the_sliders() {
        var h = createTemporaryObject(hdr, this, { height: 40 })
        verify(!h.slidersVisible)
        h.height = 56
        verify(h.slidersVisible)
    }
    function test_the_volume_slider_stops_at_plus_6_like_the_mixer_fader() {
        var h = createTemporaryObject(hdr, this, { height: 72 })
        var found = null
        var find = function (item) {
            if (item.from === -96 && item.to !== undefined && item.moved !== undefined && item.pressed !== undefined) found = item
            for (var i = 0; i < item.children.length; ++i) find(item.children[i])
        }
        find(h)
        verify(found !== null)
        compare(found.to, 6)
    }
    function test_pan_text_is_logics_minus_64_to_plus_63() {
        var h = createTemporaryObject(hdr, this, { height: 72 })
        compare(h.panText(0), "0")
        compare(h.panText(-1), "-64")
        compare(h.panText(1), "+63")
        compare(h.panText(0.5), "+32")
    }
    function test_mute_and_solo_emit_with_the_new_state() {
        var h = createTemporaryObject(hdr, this, { height: 72 })
        var got = []
        h.muteToggled.connect(function (id, on) { got.push(["m", id, on]) })
        h.soloToggled.connect(function (id, on) { got.push(["s", id, on]) })
        mouseClick(h.muteButton)
        mouseClick(h.soloButton)
        compare(got.length, 2)
        compare(got[0], ["m", "t1", true])
        compare(got[1], ["s", "t1", true])
    }
    function test_double_click_renames_and_escape_cancels() {
        var h = createTemporaryObject(hdr, this, { height: 72 })
        var names = []
        h.renamed.connect(function (id, name) { names.push(name) })
        mouseDoubleClickSequence(h.nameLabel)
        verify(h.editing)
        keyClick(Qt.Key_Escape)
        verify(!h.editing)
        compare(names.length, 0)
        mouseDoubleClickSequence(h.nameLabel)
        verify(h.editing)
        h.nameInput.text = "Lead"
        keyClick(Qt.Key_Return)
        compare(names, ["Lead"])
        verify(!h.editing)
    }
    function test_an_empty_or_unchanged_name_is_not_sent() {
        var h = createTemporaryObject(hdr, this, { height: 72 })
        var names = []
        h.renamed.connect(function (id, name) { names.push(name) })
        mouseDoubleClickSequence(h.nameLabel)
        h.nameInput.text = "   "
        keyClick(Qt.Key_Return)
        mouseDoubleClickSequence(h.nameLabel)
        h.nameInput.text = "Keys"
        keyClick(Qt.Key_Return)
        compare(names.length, 0)
    }
    function test_click_selects_with_modifiers() {
        var h = createTemporaryObject(hdr, this, { height: 72 })
        var modes = []
        h.selectRequested.connect(function (id, mode) { modes.push(mode) })
        mouseClick(h, 20, 20, Qt.LeftButton, Qt.NoModifier)
        mouseClick(h, 20, 20, Qt.LeftButton, Qt.ShiftModifier)
        mouseClick(h, 20, 20, Qt.LeftButton, Qt.ControlModifier)
        compare(modes, ["replace", "extend", "toggle"])
    }
    function test_arm_and_monitor_ask_the_owner_for_the_new_state() {
        var h = createTemporaryObject(hdr, this, { height: 72, trackId: "t1" })
        var got = []
        h.trackToggled.connect(function (id, action, on) { got.push([id, action, on]) })
        mouseClick(h.armButton)
        h.recordArm = true  // the owner stores the state and gives it back: the button follows it
        compare(h.armButton.active, true)
        mouseClick(h.armButton)
        mouseClick(h.monitorButton)
        compare(got, [["t1", "track.recordArm", true], ["t1", "track.recordArm", false], ["t1", "track.inputMonitor", true]])
    }
    function test_a_click_elsewhere_confirms_the_new_name() {
        var h = createTemporaryObject(hdr, this, { height: 72 })
        var names = []
        h.renamed.connect(function (id, name) { names.push(name) })
        mouseDoubleClickSequence(h.nameLabel)
        verify(h.editing)
        h.nameInput.text = "Pad"
        h.nameInput.focus = false     // focus moves away: the typed name is kept
        compare(names, ["Pad"])
        verify(!h.editing)
    }
    function test_a_double_click_on_the_header_asks_for_the_library() {
        var h = createTemporaryObject(hdr, this, { height: 72 })
        var asked = []
        h.libraryRequested.connect(function (id) { asked.push(id) })
        mouseDoubleClickSequence(h, 2, 2)
        compare(asked, ["t1"])
        mouseDoubleClickSequence(h.nameLabel)   // the name still renames and does not open the Library
        verify(h.editing)
        compare(asked, ["t1"])
    }
}
