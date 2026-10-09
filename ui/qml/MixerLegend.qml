import QtQuick
import Jad

// The label column at the left of the Mixer: one name per row of the strips (heights from StripMetrics).
Item {
    id: root
    implicitWidth: 64
    component Label: Text {
        property real rowHeight
        width: root.width - 6
        height: rowHeight
        horizontalAlignment: Text.AlignRight
        verticalAlignment: Text.AlignVCenter
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeCaptionSize
    }
    Column {
        x: 0
        y: StripMetrics.margin
        spacing: StripMetrics.spacing
        Label { text: qsTr("Setting"); rowHeight: StripMetrics.setting }
        Label { text: qsTr("Gain Reduction"); rowHeight: StripMetrics.gainReduction; font.pixelSize: Theme.fontTypeCaptionSize - 2 }
        Label { text: qsTr("EQ"); rowHeight: StripMetrics.eq }
        Label { text: qsTr("Input"); rowHeight: StripMetrics.input }
        Label { text: qsTr("Audio FX"); rowHeight: StripMetrics.fx; verticalAlignment: Text.AlignTop }
        Label { text: qsTr("Sends"); rowHeight: StripMetrics.sends; verticalAlignment: Text.AlignTop }
        Label { text: qsTr("Output"); rowHeight: StripMetrics.output }
        Label { text: qsTr("Group"); rowHeight: StripMetrics.group }
        Label { text: qsTr("Automation"); rowHeight: StripMetrics.automation }
        Item { width: 1; height: StripMetrics.icon }
        Label { text: qsTr("Pan"); rowHeight: StripMetrics.pan }
        Label { text: qsTr("dB"); rowHeight: StripMetrics.db }
    }
}
