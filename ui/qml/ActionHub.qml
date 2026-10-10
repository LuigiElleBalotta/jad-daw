pragma Singleton
import QtQuick

// Where the action objects of the window are registered, so that any component can find the one for an id
// (a menu entry, a toolbar button, a header button all share the same object and therefore the same state).
QtObject {
    property var registry: null
    property var actions: ({})
    property string hoveredId: ""  // the action of the button the pointer is over (Quick Help)
    property int version: 0  // bumps when an action is added: bindings that read it re-evaluate

    function add(action) {
        actions[action.actionId] = action
        version++
    }
    function actionFor(id) {
        version  // dependency for bindings
        return actions[id] ?? null
    }
}
