import QtQuick
import Jad

// One slot of a channel strip: a bar with a label, an optional value, a cross on hover when removable, and a
// horizontal drag (used for insert gain).
Rectangle {
    id: root
    property string text
    property string value
    property bool filled: false
    property color fillColor: Theme.accentPrimary
    property bool dim: false
    property bool removable: false
    readonly property bool hovered: area.containsMouse
    signal clicked()
    signal removeRequested()
    signal dragged(real dx)
    signal dragReleased()

    implicitHeight: 18
    implicitWidth: 84
    radius: Theme.radiusControl - 2
    color: filled ? fillColor : (area.containsMouse ? Theme.surfaceRaisedHover : Theme.surfaceRaised)
    border.color: Theme.borderSubtle
    opacity: dim ? 0.7 : 1

    Text {
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing[2]
        anchors.right: valueText.visible ? valueText.left : parent.right
        anchors.rightMargin: Theme.spacing[1]
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        elide: Text.ElideRight
        color: root.filled ? Theme.textPrimary : Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
        font.weight: Theme.fontTypeLabelWeight
    }
    Text {
        id: valueText
        visible: root.value !== "" && !(root.removable && area.containsMouse)
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing[2]
        anchors.verticalCenter: parent.verticalCenter
        text: root.value
        color: Theme.textValue
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
    }
    Text {
        visible: root.removable && area.containsMouse
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing[2]
        anchors.verticalCenter: parent.verticalCenter
        text: "×"
        color: Theme.textPrimary
        font.pixelSize: Theme.fontTypeBodySize
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        property real pressX: 0
        property bool moved: false
        onPressed: (m) => { pressX = m.x; moved = false }
        onPositionChanged: (m) => {
            if (!pressed) return
            if (Math.abs(m.x - pressX) > 3) moved = true
            if (moved) root.dragged(m.x - pressX)
        }
        onReleased: { if (moved) root.dragReleased() }
        onClicked: (m) => {
            if (moved) return
            if (root.removable && m.x > root.width - 16) root.removeRequested()
            else root.clicked()
        }
    }
}
