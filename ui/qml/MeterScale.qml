import QtQuick
import Jad

// The dB labels of a level meter: 0 at the top down to 60 (dB below full scale).
Item {
    id: root
    implicitWidth: 14
    readonly property var marks: [0, 3, 6, 9, 12, 15, 18, 21, 24, 30, 35, 40, 45, 50, 60]
    readonly property var shown: {  // the labels that fit
        const out = []
        let last = -100
        for (const m of marks) {
            const y = m / 60 * height
            if (y - last >= 9) { out.push(m); last = y }
        }
        return out
    }
    Repeater {
        model: root.shown
        delegate: Text {
            required property int modelData
            anchors.right: parent.right
            y: Math.min(root.height - height, Math.max(0, modelData / 60 * root.height - height / 2))
            text: modelData
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeCaptionSize - 2
        }
    }
}
