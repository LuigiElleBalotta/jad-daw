import QtQuick
import QtQuick.Controls.Basic
import Jad

// A CheckBox in the app's colours (the style's own indicator is a white square that the label has to be pushed away from).
CheckBox {
    id: root
    spacing: 8
    indicator: Rectangle {
        x: root.leftPadding
        y: (root.height - height) / 2
        width: 16; height: 16; radius: 3
        color: root.checked ? Theme.accentPrimary : Theme.surfaceRaised
        border.color: Theme.borderStrong
        opacity: root.enabled ? 1 : 0.5
        Text { anchors.centerIn: parent; visible: root.checked; text: "✓"; color: "#111111"; font.pixelSize: 12; font.bold: true }
    }
    contentItem: Text {
        leftPadding: root.indicator.width + root.spacing
        text: root.text
        color: root.enabled ? Theme.textPrimary : Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeBodySize
        verticalAlignment: Text.AlignVCenter
    }
}
