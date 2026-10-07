import QtQuick
import QtQuick.Layouts
import Jad

Panel {
    id: root
    property string trackId
    property string name
    property string trackColor: "purple"
    property real gainDb: 0
    property real pan: 0
    property bool mute: false
    property bool solo: false
    property bool master: false
    property real peak: 0       // shown on the master strip only

    property alias fader: fader
    property alias muteButton: muteButton
    property alias soloButton: soloButton

    signal gainReleased(string id, real db)
    signal panReleased(string id, real pan)
    signal muteToggled(string id, bool on)
    signal soloToggled(string id, bool on)

    readonly property string capitalColor: trackColor.charAt(0).toUpperCase() + trackColor.slice(1)

    implicitWidth: 80
    radius: Theme.radiusRegion

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing[3]
        spacing: Theme.spacing[3]

        Knob {
            Layout.alignment: Qt.AlignHCenter
            value: root.pan
            onReleased: (v) => root.panReleased(root.trackId, v)
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.spacing[2]
            IconButton {
                id: muteButton
                implicitWidth: Theme.sizeControlCompact
                implicitHeight: Theme.sizeControlCompact
                source: "icons/mute.svg"
                active: root.mute
                onClicked: root.muteToggled(root.trackId, !root.mute)
            }
            IconButton {
                id: soloButton
                implicitWidth: Theme.sizeControlCompact
                implicitHeight: Theme.sizeControlCompact
                source: "icons/solo.svg"
                active: root.solo
                onClicked: root.soloToggled(root.trackId, !root.solo)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing[3]
            Item { Layout.fillWidth: true }
            Fader {
                id: fader
                Layout.preferredWidth: 28
                Layout.fillHeight: true
                value: root.gainDb
                onReleased: (v) => root.gainReleased(root.trackId, v)
            }
            Meter {
                visible: root.master
                Layout.preferredWidth: 8
                Layout.fillHeight: true
                peak: root.peak
            }
            Item { Layout.fillWidth: true }
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: root.gainDb <= -96 ? "-∞" : root.gainDb.toFixed(1)
            color: Theme.textValue
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 3
            radius: 1
            color: Theme["track" + root.capitalColor + "Solid"]
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: root.name
            elide: Text.ElideRight
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
            font.weight: Theme.fontTypeLabelWeight
        }
    }
}
