import QtQuick
import Jad

// "Region: ..." section of the Inspector. Mute, Loop and Gain are real; Quantize, Transpose and Velocity are not done yet.
Column {
    id: root
    required property ProjectController project
    property var region: ({})
    readonly property alias header: head
    readonly property alias body: content
    readonly property alias gainField: gain
    readonly property alias loopCheck: loop

    PanelHeader {
        id: head
        width: parent.width
        title: qsTr("Region: %1").arg(root.region.audio ? qsTr("Audio") : qsTr("MIDI"))
    }
    Column {
        id: content
        width: parent.width
        visible: head.expanded
        topPadding: Theme.spacing[2]
        bottomPadding: Theme.spacing[2]
        InspectorRow {
            label: qsTr("Mute")
            FlagCheck { objectName: "regionMute"; anchors.verticalCenter: parent.verticalCenter; on: root.region.muted === true; onFlipped: root.project.toggleMuteSelectedRegions() }
        }
        InspectorRow {
            label: qsTr("Loop")
            FlagCheck { id: loop; objectName: "regionLoop"; anchors.verticalCenter: parent.verticalCenter; on: (root.region.loopBeats ?? 0) > 0; onFlipped: root.project.toggleLoopSelectedRegions() }
        }
        InspectorRow { label: qsTr("Quantize"); StubValue { project: root.project; label: qsTr("Quantize"); text: qsTr("Off") } }
        InspectorRow { label: qsTr("Transpose"); StubValue { project: root.project; label: qsTr("Region Transpose"); text: "0" } }
        InspectorRow { label: qsTr("Velocity"); StubValue { project: root.project; label: qsTr("Region Velocity"); text: "0" } }
        InspectorRow {
            label: qsTr("Gain")
            NumberField {
                id: gain
                anchors.verticalCenter: parent.verticalCenter
                value: root.region.gainDb ?? 0
                suffix: " dB"
                onCommitted: (v) => root.project.setRegionGain(root.region.regionId, v)
            }
        }
    }
}
