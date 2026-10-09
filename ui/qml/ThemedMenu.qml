import QtQuick
import QtQuick.Controls.Basic
import Jad

Menu {
    id: root
    delegate: ThemedMenuItem {}  // submenu titles too: the Basic style would draw them in dark text
    // the Basic style sizes a menu from its background, not from its entries: it is as wide as the widest entry
    readonly property real widest: {
        let w = 220
        for (let i = 0; i < root.count; ++i) {
            const item = root.itemAt(i)
            if (item && item.implicitWidth) w = Math.max(w, item.implicitWidth)
        }
        return w
    }
    implicitWidth: widest
    background: Rectangle {
        implicitWidth: 220
        color: Theme.surfacePanel
        border.color: Theme.borderStrong
        radius: Theme.radiusControl
    }
}
