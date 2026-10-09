import QtQuick
import QtQuick.Controls.Basic
import Jad

// A menu built from a tree of action ids, in order, with separators and nested submenus:
// nodes = [{kind: "item", id, sep}, {kind: "menu", title, sep, nodes: [...]}]; `sep` draws a separator before the node.
ThemedMenu {
    id: menu
    property var nodes: []

    Component { id: itemC; ThemedMenuItem { required property string actionId; action: ActionHub.actionFor(actionId) } }
    Component { id: sepC; MenuSeparator {} }
    Component { id: subC; ActionTreeMenu {} }

    function build() {
        for (const n of nodes) {
            if (n.sep) menu.addItem(sepC.createObject(menu))
            if (n.kind === "item") menu.addItem(itemC.createObject(menu, { actionId: n.id }))
            else menu.addMenu(subC.createObject(menu, { title: qsTr(n.title), nodes: n.nodes }))
        }
    }
    Component.onCompleted: build()
}
