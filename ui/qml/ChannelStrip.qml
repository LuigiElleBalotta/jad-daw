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
    property var pluginGroups: []      // [{vendor, plugins: [{id, name}]}], from the plug-in catalogue
    property var knownPluginIds: []    // ids of the plug-ins that are installed
    property bool selected: false      // the track is selected in the project (the name is drawn in the accent colour)

    readonly property string trackId: info.trackId ?? ""
    // a gesture in flight belongs to the track it started on: showing another track drops it, no command
    onTrackIdChanged: { fader.cancel(); panKnob.cancel(); dragIndex = -1; renaming = false }
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
    readonly property alias panKnob: panKnob
    readonly property alias muteButton: muteButton
    readonly property alias soloButton: soloButton
    readonly property alias addInsertSlot: addInsertSlot
    readonly property alias outputSlot: outputSlot
    readonly property alias groupSlot: groupSlot
    readonly property alias automationSlot: automationSlot
    readonly property alias insertList: insertRepeater
    readonly property alias sendList: sendRepeater
    readonly property alias insertMenu: insertMenu
    readonly property alias sendMenu: sendMenu
    readonly property alias outputMenu: outputMenu
    readonly property alias stripName: nameLabel
    readonly property alias nameInput: nameInput
    readonly property alias instrumentSlot: instrumentSlot
    property bool renaming: false

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
    signal pluginInsertRequested(string id, string pluginId, string name)
    signal insertEditorRequested(string id, int index)
    signal pluginManagerRequested()
    signal newBusRequested(string id, string role)       // role: "send" or "output"
    signal renameRequested(string id, string name)
    signal insertMoveRequested(string id, int from, int to, string toTrackId)  // toTrackId empty: inside this track
    signal insertBypassToggled(string id, int index, bool on)
    signal busViewRequested(string busId)                   // Shift-click on a send or the output slot
    signal sendPreFaderToggled(string sendId, bool on)
    signal libraryRequested()                              // the instrument slot was clicked
    signal selectRequested(string id, int modifiers)

    function beginRename() {
        if (master) return
        renaming = true
        nameInput.text = trackName
        nameInput.forceActiveFocus()
        nameInput.selectAll()
    }
    function requestOutput(outputId) { outputRequested(trackId, outputId) }
    function requestSend(targetId) { sendAddRequested(trackId, targetId) }
    function requestGainInsert() { insertAddRequested(trackId) }
    function requestPluginInsert(pluginId, name) { pluginInsertRequested(trackId, pluginId, name) }
    function isMissing(ins) { return ins.plugin === true && knownPluginIds.indexOf(ins.processorId) < 0 }
    function insertLabel(ins) {
        if (ins.plugin) return ins.label && ins.label !== "" ? ins.label : qsTr("Plug-in")
        return ins.processorId === "builtin.gain" ? qsTr("Gain") : ins.processorId
    }

    readonly property bool acceptsInserts: slotsVisible && !master && visible  // a place an insert can be dropped
    property real dropLineY: 0
    property bool dropLineVisible: false
    // The place in the chain (0..n) the pointer is at, as the number of inserts whose centre is above `localY` (`skip`: the
    // index of the insert being dragged, which does not count).
    function dropIndexAt(localY, skip) {
        let n = 0
        for (let i = 0; i < insertRepeater.count; ++i) {
            const item = insertRepeater.itemAt(i)
            if (i === skip || !item) continue
            if (root.mapFromItem(item, 0, item.height / 2).y < localY) ++n
        }
        return n
    }
    function showDropLine(place, skip) {
        let others = []
        for (let i = 0; i < insertRepeater.count; ++i)
            if (i !== skip && insertRepeater.itemAt(i)) others.push(insertRepeater.itemAt(i))
        if (others.length === 0) dropLineY = root.mapFromItem(addInsertSlot, 0, 0).y
        else if (place <= 0) dropLineY = root.mapFromItem(others[0], 0, 0).y
        else dropLineY = root.mapFromItem(others[Math.min(place, others.length) - 1], 0, others[0].height).y
        dropLineVisible = true
    }
    function hideDropLine() { dropLineVisible = false }
    Component.onCompleted: InsertDrag.register(root)
    Component.onDestruction: InsertDrag.unregister(root)
    property int dragIndex: -1
    property real dragGain: 0
    // the list changed under a gain drag (an undo, a rebuild): the gesture belongs to a row that may not be there any more
    onInsertsChanged: { dragIndex = -1; InsertDrag.cancelIf(root) }

    implicitWidth: 96
    radius: Theme.radiusRegion

    // a click on the strip outside the name field, or another track becoming the selection, confirms a name being edited
    TapHandler {
        onPressedChanged: {
            if (!pressed || !root.renaming) return
            const p = nameInput.mapFromItem(root, point.position)
            if (p.x < 0 || p.y < 0 || p.x > nameInput.width || p.y > nameInput.height) root.forceActiveFocus()
        }
    }
    onSelectedChanged: { if (renaming && !selected) nameInput.commit() }

    component TargetMenu: ThemedMenu {
        id: menu
        property bool withMaster: false
        property bool withNewBus: false
        property var targets: []
        signal chosen(string targetId)
        signal newBusChosen()
        ThemedMenuItem { visible: menu.withNewBus; height: visible ? implicitHeight : 0; text: qsTr("New Bus"); onTriggered: menu.newBusChosen() }
        MenuSeparator { visible: menu.withNewBus; height: visible ? implicitHeight : 0 }
        Instantiator {
            model: (menu.withMaster ? [{ id: "", name: qsTr("Stereo Out") }] : []).concat(menu.targets)
            delegate: ThemedMenuItem {
                required property var modelData
                text: modelData.name
                onTriggered: menu.chosen(modelData.id)
            }
            onObjectAdded: (index, object) => menu.insertItem(index + (menu.withNewBus ? 2 : 0), object)  // after New Bus and its separator
            onObjectRemoved: (index, object) => menu.removeItem(object)
        }
    }
    TargetMenu {
        id: outputMenu
        withMaster: true
        withNewBus: true
        targets: root.targets
        onChosen: (id) => root.requestOutput(id)
        onNewBusChosen: root.newBusRequested(root.trackId, "output")
    }
    TargetMenu {
        id: sendMenu
        withNewBus: true
        targets: root.targets
        onChosen: (id) => root.requestSend(id)
        onNewBusChosen: root.newBusRequested(root.trackId, "send")
    }
    ThemedMenu {
        id: insertMenu
        signal managerChosen()
        ThemedMenuItem { text: qsTr("Gain"); onTriggered: root.requestGainInsert() }
        Instantiator {
            model: root.pluginGroups
            delegate: ThemedMenu {
                id: vendorMenu
                required property var modelData
                title: modelData.vendor
                Instantiator {
                    model: vendorMenu.modelData.plugins
                    delegate: ThemedMenuItem {
                        required property var modelData
                        text: modelData.name
                        onTriggered: root.requestPluginInsert(modelData.id, modelData.name)
                    }
                    onObjectAdded: (index, object) => vendorMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => vendorMenu.removeItem(object)
                }
            }
            onObjectAdded: (index, object) => insertMenu.insertMenu(index + 1, object)
            onObjectRemoved: (index, object) => insertMenu.removeMenu(object)
        }
        MenuSeparator {}
        ThemedMenuItem { text: qsTr("Plug-in Manager…"); onTriggered: insertMenu.managerChosen() }
    }
    Connections { target: insertMenu; function onManagerChosen() { root.pluginManagerRequested() } }

    Rectangle {  // where a dragged insert would land
        visible: root.dropLineVisible
        x: Theme.spacing[2]
        y: root.dropLineY - 1
        width: root.width - Theme.spacing[2] * 2
        height: 2
        color: Theme.accentPrimary
        z: 50
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing[2]
        spacing: Theme.spacing[0]

        StripSlot {
            Layout.fillWidth: true
            visible: root.slotsVisible
            text: root.info.patchName && root.info.patchName !== "" ? root.info.patchName : qsTr("Setting")
            dim: true
            onClicked: root.stubUsed(qsTr("Setting"))
        }
        StripSlot {
            id: instrumentSlot
            Layout.fillWidth: true
            visible: root.slotsVisible && root.kind === "instrument"
            text: root.info.instrument === "builtin.sine" ? qsTr("Sine") : (root.info.instrument ?? "")
            filled: true
            fillColor: Theme.statePlay
            onClicked: root.libraryRequested()
        }
        Repeater {
            id: insertRepeater
            model: root.slotsVisible ? root.inserts : []
            delegate: StripSlot {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                text: root.insertLabel(modelData)
                value: modelData.plugin ? "" : (root.dragIndex === index ? root.dragGain : modelData.gainDb).toFixed(1)
                missing: root.isMissing(modelData)
                filled: true
                removable: true
                movable: true
                boundsItem: root
                onDragAborted: root.dragIndex = -1
                horizontalDrag: !modelData.plugin
                showBypass: true
                bypassed: modelData.bypass === true
                onBypassToggled: (on) => root.insertBypassToggled(root.trackId, index, on)
                onMoveStarted: InsertDrag.begin(root, index)
                onMoved: (x, y) => InsertDrag.update(x, y)
                onMoveCancelled: InsertDrag.cancel()
                onMoveReleased: (x, y) => {
                    const place = InsertDrag.end(x, y)  // null: the drag was dropped or ended over no strip
                    if (!place || (place.trackId === root.trackId && place.to === index)) return
                    root.insertMoveRequested(root.trackId, index, place.to, place.trackId === root.trackId ? "" : place.trackId)
                }
                onRemoveRequested: root.insertRemoveRequested(root.trackId, index)
                onDoubleClicked: { if (modelData.plugin) root.insertEditorRequested(root.trackId, index) }
                onDragged: (dx) => {
                    if (modelData.plugin) return
                    root.dragIndex = index
                    root.dragGain = Math.max(-96, Math.min(24, modelData.gainDb + dx * 0.1))
                }
                onDragReleased: {
                    if (root.dragIndex !== index) return
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
            onClicked: insertMenu.popup(addInsertSlot, 0, addInsertSlot.height)
        }
        Repeater {
            id: sendRepeater
            model: root.slotsVisible ? root.sends : []
            delegate: RowLayout {
                id: sendRow
                required property var modelData
                property alias knob: sendKnob
                property alias slot: sendSlot
                property alias menu: preMenu
                Layout.fillWidth: true
                spacing: Theme.spacing[1]
                StripSlot {
                    id: sendSlot
                    Layout.fillWidth: true
                    text: sendRow.modelData.targetName
                    filled: true
                    fillColor: Theme.accentPrimaryHover
                    removable: true
                    value: sendRow.modelData.preFader ? qsTr("pre") : ""
                    onRemoveRequested: root.sendRemoveRequested(sendRow.modelData.id)
                    onClicked: (modifiers) => { if (modifiers & Qt.ShiftModifier) root.busViewRequested(sendRow.modelData.targetId) }
                    onRightClicked: preMenu.popup(sendSlot, 0, sendSlot.height)
                }
                ThemedMenu {
                    id: preMenu
                    ThemedMenuItem {
                        text: qsTr("Pre Fader")
                        checkable: true
                        checked: sendRow.modelData.preFader
                        onTriggered: root.sendPreFaderToggled(sendRow.modelData.id, checked)
                    }
                }
                Knob {
                    id: sendKnob
                    width: 20
                    height: 20
                    from: -96
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
            visible: root.slotsVisible
            text: qsTr("Send +")
            onClicked: sendMenu.popup(addSendSlot, 0, addSendSlot.height)
        }
        Item { Layout.fillHeight: true; Layout.minimumHeight: 0 }
        StripSlot {
            id: outputSlot
            Layout.fillWidth: true
            visible: root.slotsVisible
            text: root.info.outputName && root.info.outputName !== "" ? root.info.outputName : qsTr("Stereo Out")
            onClicked: (modifiers) => {
                if (modifiers & Qt.ShiftModifier) root.busViewRequested(root.info.outputId ?? "")
                else outputMenu.popup(outputSlot, 0, outputSlot.height)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.slotsVisible
            spacing: Theme.spacing[1]
            StripSlot { id: groupSlot; Layout.fillWidth: true; text: qsTr("Group"); dim: true; onClicked: root.stubUsed("Group") }
            StripSlot { id: automationSlot; Layout.fillWidth: true; text: qsTr("Read"); dim: true; onClicked: root.stubUsed("Automation") }
        }
        Knob {
            id: panKnob
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
            Layout.minimumHeight: 56
            Layout.fillHeight: false  // the spacer above absorbs the extra height: every strip lines up at the bottom
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
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: nameLabel.implicitHeight
            Text {
                id: nameLabel
                anchors.fill: parent
                visible: !root.renaming
                horizontalAlignment: Text.AlignHCenter
                text: root.trackName
                elide: Text.ElideRight
                color: root.selected ? Theme.accentPrimary : Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeLabelSize
                font.weight: Theme.fontTypeLabelWeight
            }
            MouseArea {
                anchors.fill: parent
                enabled: !root.master && !root.renaming
                onClicked: (m) => root.selectRequested(root.trackId, m.modifiers)
                onDoubleClicked: root.beginRename()
            }
            TextInput {
                id: nameInput
                anchors.fill: parent
                visible: root.renaming
                horizontalAlignment: TextInput.AlignHCenter
                color: Theme.textPrimary
                selectByMouse: true
                clip: true
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeLabelSize
                font.weight: Theme.fontTypeLabelWeight
                // Return and a click elsewhere confirm the name; Escape cancels
                function commit() {
                    const name = text.trim()
                    root.renaming = false
                    if (name !== "" && name !== root.trackName) root.renameRequested(root.trackId, name)
                }
                onAccepted: commit()
                Keys.onShortcutOverride: (event) => { if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) event.accepted = true }
                Keys.onEscapePressed: root.renaming = false
                onActiveFocusChanged: if (!activeFocus && root.renaming) commit()
            }
        }
    }
}
