import QtQuick
import Jad

// A short, non-blocking message (for example "Metronome Click: not implemented yet").
Rectangle {
    id: root
    property string message
    function show(text) {
        message = text
        opacity = 1
        hideTimer.restart()
    }

    implicitWidth: label.implicitWidth + Theme.spacing[6] * 2
    implicitHeight: 32
    width: implicitWidth
    height: implicitHeight
    radius: Theme.radiusControl
    color: Theme.surfaceRaised
    border.color: Theme.borderStrong
    opacity: 0
    visible: opacity > 0
    z: 100

    Behavior on opacity { NumberAnimation { duration: 150 } }
    Timer { id: hideTimer; interval: 3000; onTriggered: root.opacity = 0 }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.message
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeBodySize
    }
}
