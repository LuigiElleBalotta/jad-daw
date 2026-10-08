import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQml.Models
import Jad

// One channel strip: Setting, instrument, inserts, sends, output, group and automation slots, pan, fader with meter,
// M and S, colour bar and name. A pure view: edits leave as signals (ProjectStrip wires them to the controller).
Panel {
    id: root
    property var info: ({})
    property var targets: []     // the buses and auxes a send or the output can go to: [{id, name}]
    property real peak: 0        // shown on the master strip only
    property bool showSlots: true

    readonly property string trackId: info.trackId ?? ""
    readonly property string trackName: info.name ?? ""
    readonly property string trackColor: info.color ?? "purple"
    readonly property string kind: info.kind ?? "audio"
    readonly property bool master: info.master ?? false
    readonly property real gainDb: info.gainDb ?? 0
    readonly property real pan: info.pan ?? 0
    readonly property bool mute: info.mute ?? false
    readonly property bool solo: info.solo ?? false
    readonly property var inserts: info.inserts ?? []
    readonly property var sends: info.sends ?? []
    readonly property bool slotsVisible: showSlots && !master
    readonly property string capitalColor: trackColor.charAt(0).toUpperCase() + trackColor.slice(1)

    readonly property alias fader: fader
    readonly property alias muteButton: muteButton
    readonly property alias soloButton: soloButton
    readonly property alias addInsertSlot: addInsertSlot
    readonly property alias outputSlot: outputSlot
    readonly property alias groupSlot: groupSlot
    readonly property alias automationSlot: automationSlot
    readonly property alias insertList: insertRepeater
    readonly property alias sendList: sendRepeater

    signal gainReleased(string id, real db)
    signal panReleased(string id, real pan)
    signal muteToggled(string id, bool on)
    signal soloToggled(string id, bool on)
    signal insertAddRequested(string id)
    signal insertRemoveRequested(string id, int index)
    signal insertGainReleased(string id, int index, real db)
    signal sendAddRequested(string id, string targetId)
    signal sendRemoveRequested(string sendId)
    signal sendLevelReleased(string sendId, real db)
    signal outputRequested(string id, string outputId)
    signal stubUsed(string label)

    function requestOutput(outputId) { outputRequested(trackId, outputId) }
    function requestSend(targetId) { sendAddRequested(trackId, targetId) }
    function insertLabel(processorId) { return processorId === "builtin.gain" ? qsTr("Gain") : processorId }

    property int dragIndex: -1
    property real dragGain: 0

    implicitWidth: 96
    radius: Theme.radiusRegion

    component TargetMenu: ThemedMenu {
        id: menu
        property bool withMaster: false
        property var targets: []
        signal chosen(string targetId)
        Instantiator {
            model: (menu.withMaster ? [{ id: "", name: qsTr("Stereo Out") }] : []).concat(menu.targets)
            delegate: ThemedMenuItem {
                required property var modelData
                text: modelData.name
                onTriggered: menu.chosen(modelData.id)
            }
            onObjectAdded: (index, object) => menu.insertItem(index, object)
            onObjectRemoved: (index, object) => menu.removeItem(object)
        }
    }
    TargetMenu { id: outputMenu; withMaster: true; targets: root.targets; onChosen: (id) => root.requestOutput(id) }
    TargetMenu { id: sendMenu; targets: root.targets; onChosen: (id) => root.requestSend(id) }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing[2]
        spacing: Theme.spacing[1]

        StripSlot {
            Layout.fillWidth: true
            visible: root.slotsVisible
            text: root.info.patchName && root.info.patchName !== "" ? root.info.patchName : qsTr("Setting")
            dim: true
            onClicked: root.stubUsed(qsTr("Setting"))
        }
        StripSlot {
            Layout.fillWidth: true
            visible: root.slotsVisible && root.kind === "instrument"
            text: root.info.instrument === "builtin.sine" ? qsTr("Sine") : (root.info.instrument ?? "")
            filled: true
            fillColor: Theme.statePlay
        }
        Repeater {
            id: insertRepeater
            model: root.slotsVisible ? root.inserts : []
            delegate: StripSlot {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                text: root.insertLabel(modelData.processorId)
                value: (root.dragIndex === index ? root.dragGain : modelData.gainDb).toFixed(1)
                filled: true
                removable: true
                onRemoveRequested: root.insertRemoveRequested(root.trackId, index)
                onDragged: (dx) => {
                    root.dragIndex = index
                    root.dragGain = Math.max(-96, Math.min(24, modelData.gainDb + dx * 0.1))
                }
                onDragReleased: {
                    const db = root.dragGain
                    root.dragIndex = -1
                    root.insertGainReleased(root.trackId, index, db)
                }
            }
        }
        StripSlot {
            id: addInsertSlot
            Layout.fillWidth: true
            visible: root.slotsVisible
            text: "+"
            onClicked: root.insertAddRequested(root.trackId)
        }
        Repeater {
            id: sendRepeater
            model: root.slotsVisible ? root.sends : []
            delegate: RowLayout {
                id: sendRow
                required property var modelData
                property alias knob: sendKnob
                property alias slot: sendSlot
                Layout.fillWidth: true
                spacing: Theme.spacing[1]
                StripSlot {
                    id: sendSlot
                    Layout.fillWidth: true
                    text: sendRow.modelData.targetName
                    filled: true
                    fillColor: Theme.accentPrimaryHover
                    removable: true
                    onRemoveRequested: root.sendRemoveRequested(sendRow.modelData.id)
                }
                Knob {
                    id: sendKnob
                    width: 20
                    height: 20
                    from: -60
                    to: 12
                    resetValue: 0
                    value: sendRow.modelData.levelDb
                    onReleased: (v) => root.sendLevelReleased(sendRow.modelData.id, v)
                }
            }
        }
        StripSlot {
            id: addSendSlot
            Layout.fillWidth: true
            visible: root.slotsVisible && root.targets.length > 0
            text: qsTr("Send +")
            onClicked: sendMenu.popup(addSendSlot, 0, addSendSlot.height)
        }
        Item { Layout.fillHeight: true; Layout.minimumHeight: 0 }
        StripSlot {
            id: outputSlot
            Layout.fillWidth: true
            visible: root.slotsVisible
            text: root.info.outputName && root.info.outputName !== "" ? root.info.outputName : qsTr("Stereo Out")
            onClicked: outputMenu.popup(outputSlot, 0, outputSlot.height)
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.slotsVisible
            spacing: Theme.spacing[1]
            StripSlot { id: groupSlot; Layout.fillWidth: true; text: qsTr("Group"); dim: true; onClicked: root.stubUsed("Group") }
            StripSlot { id: automationSlot; Layout.fillWidth: true; text: qsTr("Read"); dim: true; onClicked: root.stubUsed("Automation") }
        }
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
            Layout.preferredHeight: 150
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
            text: root.trackName
            elide: Text.ElideRight
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
            font.weight: Theme.fontTypeLabelWeight
        }
    }
}
