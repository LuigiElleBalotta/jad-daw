import QtQuick
import Jad

// The dB labels beside a Fader: the marks of Logic's scale, the 0 mark in bold.
Item {
    id: root
    required property Fader fader
    implicitWidth: 18
    readonly property var marks: [6, 3, 0, -3, -6, -10, -15, -20, -30, -40]
    Repeater {
        model: root.marks
        delegate: Text {
            required property int modelData
            anchors.right: parent.right
            y: root.fader.valueToPos(modelData) - height / 2
            text: modelData > 0 ? "+" + modelData : String(modelData)
            color: modelData === 0 ? Theme.textPrimary : Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeCaptionSize - 1
            font.weight: modelData === 0 ? Font.Bold : Font.Normal
        }
    }
}
