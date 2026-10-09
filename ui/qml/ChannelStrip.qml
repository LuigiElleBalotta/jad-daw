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
    property bool longFader: false    // View > Long Faders
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
    signal trackToggled(string id, string actionId, bool on)   // the R and I buttons
    signal soloExclusiveRequested(string id)                    // Option-click on S: this strip alone
    signal soloClearRequested()                                 // Option-click on a lit S: every solo off
    signal muteAllRequested(bool on)                            // Command-click on M
    signal soloAllRequested(bool on)                            // Command-click on S
    signal gestureStarted()                                     // a fader or knob drag begins: the moves below follow
    signal gainMoved(string id, real db)
    signal panMoved(string id, real pan)
    signal peakReset()                                          // a click on the peak field

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

    implicitWidth: 104
    radius: Theme.radiusRegion
    color: selected ? Theme.surfaceRaised : Theme.surfacePanel  // the selected strip is lighter
    // a strip that is not tall enough (the Inspector with other panes open) drops the gain reduction and EQ rows and shortens the rest
    readonly property bool tight: height < 470
    readonly property bool hasInput: !master && kind === "audio"  // R and I are on audio strips
    readonly property color typeColor: master ? "#8e5bd6" : (kind === "instrument" ? Theme.trackGreenSolid : (kind === "audio" ? Theme.trackBlueSolid : Theme.trackPinkSolid))

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

    // the rows have fixed heights (StripMetrics) so that the legend of the Mixer lines up with every strip
    component FixedRow: Item {
        property real rowHeight
        Layout.fillWidth: true
        Layout.preferredHeight: rowHeight
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: StripMetrics.margin
        spacing: StripMetrics.spacing

        FixedRow {
            rowHeight: StripMetrics.setting
            StripSlot {
                anchors.fill: parent
                visible: root.slotsVisible
                text: root.info.patchName && root.info.patchName !== "" ? root.info.patchName : qsTr("Setting")
                dim: true
                onClicked: root.stubUsed(qsTr("Setting"))
            }
        }
        FixedRow {  // the gain reduction bar
            rowHeight: root.tight ? 0 : StripMetrics.gainReduction
            visible: !root.tight
            Rectangle { anchors.fill: parent; visible: root.slotsVisible; radius: 1; color: Theme.surfaceCanvas }
        }
        FixedRow {  // the EQ display: a click would insert a Channel EQ
            rowHeight: root.tight ? 0 : StripMetrics.eq
            visible: !root.tight
            Rectangle {
                id: eqDisplay
                anchors.fill: parent
                visible: root.slotsVisible
                radius: Theme.radiusControl - 2
                color: Theme.surfaceCanvas
                border.color: Theme.borderSubtle
                MouseArea { anchors.fill: parent; onClicked: root.stubUsed(qsTr("EQ")) }
            }
        }
        FixedRow {  // the input of an audio strip, the instrument of an instrument strip
            rowHeight: StripMetrics.input
            StripSlot {
                id: inputSlot
                anchors.fill: parent
                visible: root.slotsVisible && root.kind === "audio"
                text: qsTr("In 1")
                dim: true
                onClicked: root.stubUsed(qsTr("Input"))
            }
            StripSlot {
                id: instrumentSlot
                anchors.fill: parent
                visible: root.slotsVisible && root.kind === "instrument"
                text: root.info.instrument === "builtin.sine" ? qsTr("Sine") : (root.info.instrument ?? "")
                filled: true
                fillColor: Theme.statePlay
                onClicked: root.libraryRequested()
            }
        }
        FixedRow {  // the audio effects
            rowHeight: Math.max(root.tight ? 44 : StripMetrics.fx, fxColumn.implicitHeight)  // grows with its slots
            ColumnLayout {
                id: fxColumn
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: StripMetrics.spacing
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
            }
        }
        FixedRow {  // the sends
            rowHeight: Math.max(StripMetrics.sends, sendsColumn.implicitHeight)
            ColumnLayout {
                id: sendsColumn
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: StripMetrics.spacing
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
            }
        }
        FixedRow {
            rowHeight: StripMetrics.output
            StripSlot {
                id: outputSlot
                anchors.fill: parent
                visible: root.slotsVisible
                text: root.info.outputName && root.info.outputName !== "" && root.info.outputName !== "Master" ? root.info.outputName : qsTr("St Out")
                onClicked: (modifiers) => {
                    if (modifiers & Qt.ShiftModifier) root.busViewRequested(root.info.outputId ?? "")
                    else outputMenu.popup(outputSlot, 0, outputSlot.height)
                }
            }
        }
        FixedRow {
            rowHeight: StripMetrics.group
            StripSlot { id: groupSlot; anchors.fill: parent; text: root.master ? "" : qsTr("Group"); dim: true; onClicked: root.stubUsed("Group") }
        }
        FixedRow {
            rowHeight: StripMetrics.automation
            StripSlot {
                id: automationSlot
                anchors.fill: parent
                text: qsTr("Read")
                dim: true
                textColor: root.master ? Theme.textPrimary : Theme.statePlay  // Logic: green on tracks, white on the master
                onClicked: root.stubUsed("Automation")
            }
        }
        FixedRow {  // the track icon tile, in the colour of the strip type
            rowHeight: StripMetrics.icon
            Rectangle {
                anchors.centerIn: parent
                width: 28
                height: StripMetrics.icon
                radius: Theme.radiusControl - 2
                color: root.typeColor
                TrackIcon { anchors.centerIn: parent; size: 16; kind: root.master ? "master" : root.kind; tint: Theme.textPrimary }
            }
        }
        FixedRow {
            rowHeight: StripMetrics.pan
            Knob {
                id: panKnob
                visible: !root.master
                anchors.centerIn: parent
                doubleClickEdits: true  // a double click types a position from -64 to +63
                entryScale: 64
                centerMark: true
                value: root.pan
                property bool started: false
                onMoved: (v) => {
                    if (!started) { started = true; root.gestureStarted() }
                    root.panMoved(root.trackId, v)
                }
                onReleased: (v) => { started = false; root.panReleased(root.trackId, v) }
            }
        }
        RowLayout {  // the dB field and the peak field
            Layout.fillWidth: true
            Layout.preferredHeight: StripMetrics.db
            spacing: Theme.spacing[0]
            DbField {
                id: dbField
                Layout.fillWidth: true
                value: fader.dragging ? fader.dragValue : root.gainDb  // the field follows the fader while it is dragged
                onCommitted: (db) => root.gainReleased(root.trackId, db)
            }
            Rectangle {  // the peak field: a darker box, a click resets every peak
                id: peakField
                visible: !root.master
                Layout.preferredWidth: 34
                implicitHeight: StripMetrics.db
                radius: Theme.radiusControl - 2
                color: Theme.surfaceCanvas
                border.color: Theme.borderSubtle
                Text {
                    anchors.centerIn: parent
                    text: root.peak > 0 ? (20 * Math.log(root.peak) / Math.LN10).toFixed(1).replace(".", ",") : ""
                    color: root.peak > 1 ? Theme.stateClip : Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeCaptionSize
                }
                MouseArea { anchors.fill: parent; onClicked: root.peakReset() }
            }
        }
        RowLayout {  // fader with its scale, and the level meter with its own scale: takes the height that is left
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: root.tight ? 28 : (root.longFader ? 260 : StripMetrics.faderMin)
            spacing: Theme.spacing[1]
            FaderScale { fader: fader; Layout.fillHeight: true }
            Fader {
                id: fader
                Layout.preferredWidth: 28
                Layout.fillHeight: true
                value: root.gainDb
                property bool started: false
                onMoved: (v) => {
                    if (!started) { started = true; root.gestureStarted() }
                    root.gainMoved(root.trackId, v)
                }
                onReleased: (v) => { started = false; root.gainReleased(root.trackId, v) }
            }
            MeterScale { Layout.fillHeight: true; visible: !root.master }
            Meter {
                Layout.preferredWidth: 10
                Layout.fillHeight: true
                visible: !root.master
                peak: 0
            }
            Item { Layout.fillWidth: true }
        }
        RowLayout {  // R and I: flat while off, red and orange when on
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredHeight: 18
            spacing: Theme.spacing[1]
            opacity: root.master ? 0 : 1  // the master keeps the row, so that M and the name line up with the other strips
            IconButton {
                id: armButton
                opacity: root.hasInput ? 1 : 0
                enabled: root.hasInput
                implicitWidth: 22
                implicitHeight: 16
                label: "R"
                active: root.info.recordArm === true
                activeColor: Theme.stateRecord
                fillActive: true
                fillText: Theme.textPrimary
                onClicked: root.trackToggled(root.trackId, "track.recordArm", !active)
            }
            IconButton {
                id: monitorButton
                opacity: root.hasInput ? 1 : 0
                enabled: root.hasInput
                implicitWidth: 22
                implicitHeight: 16
                label: "I"
                active: root.info.inputMonitor === true
                activeColor: Theme.trackOrangeSolid
                fillActive: true
                onClicked: root.trackToggled(root.trackId, "track.inputMonitor", !active)
            }
        }
        RowLayout {  // M and S (D on the master)
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.spacing[1]
            IconButton {
                id: muteButton
                implicitWidth: 28
                implicitHeight: Theme.sizeControlCompact
                label: "M"
                active: root.mute
                activeColor: Theme.accentPrimary
                fillActive: true
                fillText: Theme.textPrimary
                onClicked: root.muteToggled(root.trackId, !root.mute)
                MouseArea {  // Command-click (Ctrl): every strip in this state switches
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onPressed: (m) => {
                        if (!(m.modifiers & Qt.ControlModifier)) { m.accepted = false; return }
                        root.muteAllRequested(!root.mute)
                    }
                }
            }
            IconButton {
                id: soloButton
                visible: !root.master
                implicitWidth: 28
                implicitHeight: Theme.sizeControlCompact
                label: "S"
                active: root.solo
                activeColor: Theme.stateSolo
                fillActive: true
                onClicked: root.soloToggled(root.trackId, !root.solo)
                MouseArea {  // Option-click: solo exclusive, or every solo off when this one is lit
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onPressed: (m) => {
                        if (m.modifiers & Qt.ControlModifier) {  // Control-click on the Mac: solo-safe; Ctrl+Shift here, Ctrl alone is Command
                            if (m.modifiers & Qt.ShiftModifier) root.trackToggled(root.trackId, "track.soloSafe", root.info.soloSafe !== true)
                            else root.soloAllRequested(!root.solo)
                            return
                        }
                        if (!(m.modifiers & Qt.AltModifier)) { m.accepted = false; return }
                        if (root.solo) root.soloClearRequested(); else root.soloExclusiveRequested(root.trackId)
                    }
                }
                Rectangle {  // a red slash on the S: solo-safe
                    visible: root.info.soloSafe === true
                    anchors.centerIn: parent
                    width: 2
                    height: parent.height + 2
                    rotation: 45
                    color: Theme.stateRecord
                }
            }
            IconButton {  // dim, on the master strip
                id: dimButton
                visible: root.master
                implicitWidth: 28
                implicitHeight: Theme.sizeControlCompact
                label: "D"
                toggle: true
                fillActive: true
                activeColor: Theme.trackYellowSolid
                onClicked: root.stubUsed(qsTr("Dim"))
            }
        }
        Rectangle {  // the name bar, in the colour of the strip type
            Layout.fillWidth: true
            Layout.preferredHeight: nameLabel.implicitHeight + 6
            radius: Theme.radiusControl - 2
            color: root.typeColor
            Text {
                id: nameLabel
                anchors.fill: parent
                visible: !root.renaming
                horizontalAlignment: Text.AlignHCenter
                text: root.trackName
                elide: Text.ElideRight
                color: Theme.textPrimary
                font.bold: root.selected
                verticalAlignment: Text.AlignVCenter
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
