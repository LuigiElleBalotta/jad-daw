import QtQuick
import QtQuick.Controls.Basic
import Jad

MenuItem {
    id: root
    implicitHeight: 28
    implicitWidth: Math.max(240, contentItem.implicitWidth)
    readonly property bool checkedState: root.action ? root.action.on === true : (root.checkable && root.checked)

    contentItem: Item {
        implicitWidth: 16 + labelText.implicitWidth + 28 + shortcutText.implicitWidth + 12
        Text {
            id: labelText
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.enabled ? Theme.textPrimary : Theme.textDisabled
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeBodySize
        }
        Text {  // a check mark for toggles and the selected radio entry
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "✓"
            visible: root.checkedState
            color: Theme.accentPrimary
            font.pixelSize: Theme.fontTypeBodySize
        }
        Text {
            id: shortcutText
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: root.action && root.action.shortcut ? String(root.action.shortcut) : ""
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
        }
    }
    arrow: Text {  // the arrow of an entry that opens a submenu
        visible: root.subMenu !== null
        x: root.width - width - 8
        anchors.verticalCenter: parent.verticalCenter
        text: "\u203A"
        color: Theme.textSecondary
        font.pixelSize: Theme.fontTypeBodySize
    }
    background: Rectangle {
        color: root.highlighted && root.enabled ? Theme.surfaceRaisedHover : "transparent"
        radius: Theme.radiusControl
    }
}
