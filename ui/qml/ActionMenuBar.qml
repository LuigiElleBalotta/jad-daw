import QtQuick
import QtQuick.Controls.Basic
import Jad

// The menu bar, built from the action table: entries in table order, separators where the table asks for them, and
// submenus nested by their path (Edit/Select, View/Snap, ...).
MenuBar {
    id: bar
    required property var registry
    required property var actionFor        // id -> JadAction
    required property int actionsVersion   // changes when the actions exist: lets the bindings re-evaluate

    // [{kind, id | title, sep, nodes}] of one top menu
    function tree(top) {
        const root = []
        for (const e of registry.entries(top)) {
            let level = root
            if (e.path !== "") {
                for (const title of e.path.split("/")) {
                    let sub = level.find(n => n.kind === "menu" && n.title === title)
                    if (!sub) {
                        sub = { kind: "menu", title: title, sep: e.subsep, nodes: [] }
                        level.push(sub)
                    }
                    level = sub.nodes
                }
            }
            level.push({ kind: "item", id: e.id, sep: e.sep })
        }
        return root
    }

    background: Rectangle { color: Theme.surfacePanel }
    delegate: MenuBarItem {
        id: barItem
        contentItem: Text {
            text: barItem.text
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeBodySize
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle { color: barItem.highlighted ? Theme.surfaceRaisedHover : "transparent" }
    }

    Instantiator {
        model: bar.registry.topMenus()
        delegate: ActionTreeMenu {
            required property string modelData
            title: qsTr(modelData)
            nodes: bar.tree(modelData)
        }
        onObjectAdded: (index, object) => bar.insertMenu(index, object)
        onObjectRemoved: (index, object) => bar.removeMenu(object)
    }
}
