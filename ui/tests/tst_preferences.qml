import QtQuick
import QtTest
import Jad

TestCase {
    name: "Preferences"
    width: 800; height: 600
    visible: true
    when: windowShown

    Component { id: ctlC; ProjectController { audioEnabled: false } }
    Component { id: dlgC; PreferencesDialog { } }
    Component { id: selC; SelectField { width: 200; height: 24 } }

    function test_the_audio_section_lists_the_devices_and_applies_the_choice() {
        const c = createTemporaryObject(ctlC, this)
        const d = createTemporaryObject(dlgC, this, { project: c, parent: this })
        verify(d)
        d.open()
        tryVerify(function () { return d.opened })
        compare(d.section, 1)
        d.output = "Speakers"; d.input = ""; d.buffer = 512
        d.project.applyAudioSettings(d.output, d.input, d.buffer)
        compare(c.audioOutput, "Speakers")
        compare(c.audioBufferSize, 512)
        d.section = 0
        compare(d.sections.length, 5)
        d.close()
    }

    function test_a_select_field_reports_the_choice() {
        const f = createTemporaryObject(selC, this, { choices: ["a", "b", "c"], value: "a" })
        var got = []
        f.chosen.connect(function (v) { got.push(v) })
        compare(f.textOf("b"), "b")
        f.format = function (v) { return v + "!" }
        compare(f.textOf("b"), "b!")
        f.chosen("c")
        compare(got.length, 1)
    }
}
