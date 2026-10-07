import QtQuick
import Jad

// Vertical level meter; `peak` is a linear amplitude (1.0 = 0 dBFS).
Rectangle {
    id: root
    property real peak: 0
    readonly property real db: peak > 0 ? 20 * Math.log(peak) / Math.LN10 : -120
    readonly property real fraction: Math.max(0, Math.min(1, (db + 60) / 66))  // -60 .. +6 dB

    implicitWidth: 8
    color: Theme.surfaceLcd
    radius: 2
    border.color: Theme.borderSubtle
    border.width: 1
    clip: true

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: parent.height * root.fraction
        color: root.db > -3 ? Theme.meterHigh : (root.db > -12 ? Theme.meterMid : Theme.meterLow)
    }
}
