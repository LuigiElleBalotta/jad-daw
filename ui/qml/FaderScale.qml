import QtQuick
import Jad

// The dB labels beside a Fader: the marks of Logic's scale, the 0 mark in bold.
Item {
    id: root
    required property Fader fader
    implicitWidth: 22
    readonly property var marks: [6, 3, 0, -3, -6, -10, -15, -20, -30, -40]
    // the marks that fit: a label is left out when it would touch the one above
    readonly property var shown: {
        const out = []
        let last = -100
        for (const m of marks) {
            const y = fader.valueToPos(m)
            if (m === 0 || y - last >= 10) { out.push(m); last = y }
        }
        return out
    }
    Repeater {  // the minor ticks: every dB near zero, every 5 dB lower down
        model: [6, 5, 4, 3, 2, 1, 0, -1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -15, -20, -25, -30, -35, -40]
        delegate: Rectangle {
            required property int modelData
            x: root.width - width + 5
            y: root.fader.valueToPos(modelData)
            width: modelData % 3 === 0 || modelData <= -10 ? 4 : 2
            height: 1
            color: Theme.textSecondary
        }
    }
    Repeater {
        model: root.shown
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
