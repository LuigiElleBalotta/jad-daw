import QtQuick
import QtQuick.Controls.Basic
import Jad

MenuItem {
    id: root
    implicitHeight: 28
    implicitWidth: 220

    contentItem: Item {
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.enabled ? Theme.textPrimary : Theme.textDisabled
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeBodySize
        }
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: root.action && root.action.shortcut ? String(root.action.shortcut) : ""
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
        }
    }
    background: Rectangle {
        color: root.highlighted && root.enabled ? Theme.surfaceRaisedHover : "transparent"
        radius: Theme.radiusControl
    }
}
