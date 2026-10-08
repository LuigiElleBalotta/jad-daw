import QtQuick
import QtQuick.Controls.Basic

// One action of the table. A real action calls `handler(checked)`; a stub flips its visual state and announces itself.
Action {
    id: root
    required property var registry
    required property string actionId
    property var handler: null
    // The displayed state of toggles and radio entries. Bind it for a real action; a stub flips it by itself.
    property bool on: false
    readonly property bool stub: registry.isStub(actionId)

    text: registry.label(actionId)
    shortcut: registry.shortcut(actionId)
    checkable: false  // `on` carries the state, so that a bound state is never broken by a click

    onTriggered: {
        const toggle = registry.isToggle(actionId)
        if (stub) {
            const next = toggle ? !on : on
            if (handler) handler(next)  // an owner that keeps the state (a track's R, I)
            else if (toggle) on = next
            registry.stubTriggered(actionId, next)
            return
        }
        if (handler) handler(toggle ? !on : true)
    }
    Component.onCompleted: if (!stub && handler) registry.noteHandler(actionId)
}
