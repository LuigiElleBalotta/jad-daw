import QtQuick
import QtQuick.Controls.Basic
import Jad

Menu {
    id: root
    delegate: ThemedMenuItem {}  // submenu titles too: the Basic style would draw them in dark text
    background: Rectangle {
        implicitWidth: 220
        color: Theme.surfacePanel
        border.color: Theme.borderStrong
        radius: Theme.radiusControl
    }
}
