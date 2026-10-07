import QtQuick
import QtQuick.Effects
import Jad

Item {
    id: root
    property url source
    property bool active: false
    property bool toggle: false
    signal clicked()

    implicitWidth: Theme.sizeControlDefault
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
        colorization: 1.0
        colorizationColor: root.active ? Theme.accentPrimary : Theme.textPrimary
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
