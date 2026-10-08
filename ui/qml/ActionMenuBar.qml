import QtQuick
import QtQuick.Controls.Basic
import Jad

// The menu bar, built from the action table. Direct entries come first in each menu, then the submenus
// (an entry whose menu path has a second segment, such as Track/Color, belongs to a submenu).
MenuBar {
    id: bar
    required property var registry
    required property var actionFor        // id -> JadAction
    required property int actionsVersion   // changes when the actions exist: lets the bindings re-evaluate

    function direct(top) {
        return registry.entries(top).filter(e => e.path === "").map(e => e.id)
    }
    function subs(top) {
        const order = []
        const byPath = ({})
        for (const e of registry.entries(top)) {
            if (e.path === "") continue
            if (!byPath[e.path]) { byPath[e.path] = []; order.push(e.path) }
            byPath[e.path].push(e.id)
        }
        return order.map(p => ({ title: p, ids: byPath[p] }))
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
        delegate: ThemedMenu {
            id: menu
            required property string modelData
            title: qsTr(modelData)

            Instantiator {
                model: bar.direct(menu.modelData)
                delegate: ThemedMenuItem {
                    required property string modelData
                    action: { bar.actionsVersion; return bar.actionFor(modelData) }
                }
                onObjectAdded: (index, object) => menu.insertItem(index, object)
                onObjectRemoved: (index, object) => menu.removeItem(object)
            }
            Instantiator {
                model: bar.subs(menu.modelData)
                delegate: ThemedMenu {
                    id: sub
                    required property var modelData
                    title: qsTr(modelData.title)
                    Instantiator {
                        model: sub.modelData.ids
                        delegate: ThemedMenuItem {
                            required property string modelData
                            action: { bar.actionsVersion; return bar.actionFor(modelData) }
                        }
                        onObjectAdded: (index, object) => sub.insertItem(index, object)
                        onObjectRemoved: (index, object) => sub.removeItem(object)
                    }
                }
                onObjectAdded: (index, object) => menu.addMenu(object)
                onObjectRemoved: (index, object) => menu.removeMenu(object)
            }
        }
        onObjectAdded: (index, object) => bar.insertMenu(index, object)
        onObjectRemoved: (index, object) => bar.removeMenu(object)
    }
}
