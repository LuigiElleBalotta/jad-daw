import QtQuick
import QtQuick.Controls.Basic
import QtQml.Models
import Jad

// A popup menu listing the actions with the given ids (menu entries share their objects, and so their state, with
// the menu bar and the buttons).
ThemedMenu {
    id: menu
    property var ids: []

    Instantiator {
        model: menu.ids
        delegate: ThemedMenuItem {
            required property string modelData
            action: ActionHub.actionFor(modelData)
        }
        onObjectAdded: (index, object) => menu.insertItem(index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
}
