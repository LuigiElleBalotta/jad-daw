import QtQuick
import QtTest
import Jad

TestCase {
    id: tc
    name: "ToolBar"
    width: 1200; height: 200
    visible: true
    when: windowShown

    ProjectController { id: p; audioEnabled: false }
    ActionRegistry { id: reg }
    JadAction { id: pointerA; registry: reg; actionId: "tool.pointer"; handler: function () { p.tool = "pointer" }; on: p.tool === "pointer" }
    JadAction { id: scissorsA; registry: reg; actionId: "tool.scissors"; handler: function () { p.tool = "scissors" }; on: p.tool === "scissors" }
    JadAction { id: noOverlapA; registry: reg; actionId: "drag.noOverlap" }   // a stub: no handler
    SignalSpy { id: notices; target: reg; signalName: "notImplemented" }
    Component { id: barC; ToolBar { width: 1200; project: p } }

    function initTestCase() {
        ActionHub.registry = reg
        ActionHub.add(pointerA)
        ActionHub.add(scissorsA)
        ActionHub.add(noOverlapA)
    }

    function test_clicking_a_tool_selects_it() {
        var tb = createTemporaryObject(barC, tc)
        compare(p.tool, "pointer")
        mouseClick(tb.scissorsButton)
        compare(p.tool, "scissors")
        verify(tb.scissorsButton.active)
        mouseClick(tb.pointerButton)
        compare(p.tool, "pointer")
    }
    function test_the_snap_label_follows_the_controller() {
        var tb = createTemporaryObject(barC, tc)
        p.snap = "sixteenth"
        compare(tb.snapLabel, "1/16")
        p.snap = "off"
        compare(tb.snapLabel, "Off")
        p.snap = "quarter"
    }
    function test_an_unimplemented_drag_mode_announces_itself_and_changes_nothing() {
        var before = p.tool
        notices.clear()
        noOverlapA.trigger()
        compare(notices.count, 1)
        compare(p.tool, before)
    }
}
