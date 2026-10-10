import QtQuick
import Jad

// "Region: ..." section of the Inspector. Mute, Loop, Gain and, for MIDI regions, Quantize, Transpose and Velocity are real.
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
            label: qsTr("Name")
            TextEntry {
                objectName: "regionName"
                anchors.verticalCenter: parent.verticalCenter
                width: 150
                text: root.region.name ?? ""
                onEdited: (t) => root.project.renameRegion(root.region.regionId, t)
            }
        }
        InspectorRow {
            label: qsTr("Mute")
            FlagCheck { objectName: "regionMute"; anchors.verticalCenter: parent.verticalCenter; on: root.region.muted === true; onFlipped: root.project.toggleMuteSelectedRegions() }
        }
        InspectorRow {
            label: qsTr("Loop")
            FlagCheck { id: loop; objectName: "regionLoop"; anchors.verticalCenter: parent.verticalCenter; on: (root.region.loopBeats ?? 0) > 0; onFlipped: root.project.toggleLoopSelectedRegions() }
        }
        InspectorRow {
            label: qsTr("Quantize")
            visible: !root.region.audio
            SelectField {
                objectName: "regionQuantize"
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 100
                readonly property var grids: [0, 1, 0.5, 0.25, 0.125, 0.0625]
                choices: grids
                value: { for (const g of grids) if (Math.abs(g - (root.region.quantizeBeats ?? 0)) < 1e-6) return g; return 0 }
                format: (g) => g === 0 ? qsTr("Off") : (g === 1 ? qsTr("1/4 Note") : qsTr("1/%1 Note").arg(Math.round(4 / g)))
                onChosen: (g) => root.project.setSelectedRegionsMidi("quantize", g)
            }
        }
        InspectorRow {
            label: qsTr("Transpose")
            visible: !root.region.audio
            NumberField { objectName: "regionTranspose"; anchors.verticalCenter: parent.verticalCenter; from: -48; to: 48; decimals: 0; value: root.region.transpose ?? 0; onCommitted: (v) => root.project.setSelectedRegionsMidi("transpose", v) }
        }
        InspectorRow {
            label: qsTr("Velocity")
            visible: !root.region.audio
            NumberField { objectName: "regionVelocity"; anchors.verticalCenter: parent.verticalCenter; from: -127; to: 127; decimals: 0; value: root.region.velocityOffset ?? 0; onCommitted: (v) => root.project.setSelectedRegionsMidi("velocity", v) }
        }
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
