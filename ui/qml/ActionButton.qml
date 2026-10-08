import QtQuick
import QtQuick.Controls.Basic
import Jad

// A button for one action of the table: it shows the state of the action (`on`) and triggers it, so that it always
// agrees with the same action in the menu bar and in the shortcuts.
IconButton {
    id: root
    required property string actionId
    readonly property var action: ActionHub.actionFor(actionId)

    active: action ? action.on : false
    enabled: action ? action.enabled : true
    onClicked: if (action) action.trigger()

    ToolTip.visible: hovered && ToolTip.text !== ""
    ToolTip.delay: 600
    ToolTip.text: {
        const reg = ActionHub.registry
        if (!reg) return ""
        const sc = reg.shortcut(actionId)
        return reg.label(actionId) + (sc ? " (" + sc + ")" : "") + (reg.isStub(actionId) ? " — not implemented yet" : "")
    }
}
