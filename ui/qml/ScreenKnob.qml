import QtQuick
import Jad

// A labelled screen control of a Smart Control: a knob with its name above and its value below.
Item {
    id: root
    property string label
    property real value: 0
    property real from: 0
    property real to: 1
    property real resetValue: 0
    readonly property alias knob: knob
    signal released(real value)
    implicitWidth: 64
    implicitHeight: 84

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.label.toUpperCase()
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeCaptionSize
        font.weight: Theme.fontTypeLabelWeight
    }
    Knob {
        id: knob
        anchors.horizontalCenter: parent.horizontalCenter
        y: 16
        width: 44
        height: 44
        from: root.from
        to: root.to
        resetValue: root.resetValue
        value: root.value
        onReleased: (v) => root.released(v)
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 64
        text: (knob.shown).toFixed(Math.abs(root.to - root.from) > 5 ? 1 : 2)
        color: Theme.textValue
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
    }
}
