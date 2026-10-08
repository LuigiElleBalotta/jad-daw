import QtQuick
import QtQuick.Effects
import Jad

Item {
    id: root
    property url source
    property string label  // shown instead of an icon when there is no source
    property bool active: false
    property bool toggle: false
    readonly property bool hovered: area.containsMouse
    signal clicked()

    implicitWidth: Theme.sizeControlDefault + (source.toString() === "" ? Math.max(0, labelText.implicitWidth - 12) : 0)
    implicitHeight: Theme.sizeControlDefault
    width: implicitWidth
    height: implicitHeight
    opacity: enabled ? 1 : 0.4

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusControl
        color: area.containsMouse ? Theme.surfaceRaisedHover : Theme.surfaceRaised
    }
    Image {
        id: glyph
        anchors.centerIn: parent
        source: root.source
        sourceSize: Qt.size(Theme.sizeIcon, Theme.sizeIcon)
        width: Theme.sizeIcon
        height: Theme.sizeIcon
        visible: false
    }
    MultiEffect {
        anchors.fill: glyph
        source: glyph
        visible: root.source.toString() !== ""
        brightness: 1.0
        colorization: 1.0
        colorizationColor: root.active ? Theme.accentPrimary : Theme.textPrimary
    }
    Text {
        id: labelText
        anchors.centerIn: parent
        visible: root.source.toString() === ""
        text: root.label
        color: root.active ? Theme.accentPrimary : Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
        font.weight: Theme.fontTypeLabelWeight
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            if (root.toggle) root.active = !root.active
            root.clicked()
        }
    }
}
