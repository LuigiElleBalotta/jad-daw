import QtQuick
import Jad

// A ChannelStrip wired to the controller: every edit is a command.
ChannelStrip {
    id: root
    required property ProjectController project
    signal swiped(string kind, bool on, point scenePos, var from)  // a swipe over M or S reached scenePos
    // what the Core accepts: no loop through outputs and sends (routingRevision makes the list follow every change)
    targets: { project.routingRevision; return project.targetsFor(root.trackId) }
    pluginGroups: project.plugins.menu
    knownPluginIds: project.plugins.knownIds

    onAutomationModeChosen: (id, mode) => project.setAutomationMode(id, mode)
    groupChoices: { project.groupsRevision; return project.groups().map(g => ({ id: g.id, name: g.name })) }
    onGroupChosen: (id, groupId) => project.setTrackGroup(id, groupId)
    onGroupSettingsRequested: (id) => project.openGroupSettings(id)
    instrumentChoices: project.instrumentSpecs().map(s => ({ id: s.id, name: s.name }))
    onInstrumentChosen: (id, processorId) => project.setInstrument(id, processorId)
    instrumentPluginGroups: project.plugins.instrumentMenu
    onInstrumentPluginChosen: (id, pluginId, name) => project.setInstrumentPlugin(id, pluginId, name)
    onInstrumentEditorRequested: (id) => { if ((info.instrument ?? "").startsWith("vst3:")) project.openPluginEditor(id, -1); else project.openEffectEditor(id, -2) }
    effectGroups: {
        const groups = {}, order = []
        for (const s of project.effectSpecs()) {
            if (s.id === "builtin.gain") continue
            if (!groups[s.group]) { groups[s.group] = []; order.push(s.group) }
            groups[s.group].push({ id: s.id, name: s.name })
        }
        return order.map(g => ({ group: g, effects: groups[g] }))
    }
    effectNames: { const m = {}; for (const s of project.effectSpecs()) m[s.id] = s.name; return m }
    // the Channel EQ of the track, if it has one: the index of its insert and its response
    readonly property int eqIndex: { for (let i = 0; i < (info.inserts ?? []).length; ++i) if (info.inserts[i].processorId === "builtin.eq") return i; return -1 }
    eqCurve: eqIndex >= 0 ? project.eqCurve(info.inserts[eqIndex].params ?? ({}), 64, 20, 20000) : []
    onEffectInsertRequested: (id, processorId) => project.addInsert(id, processorId)
    onEffectEditorRequested: (id, index) => project.openEffectEditor(id, index)
    onEqRequested: (id) => { if (eqIndex >= 0) project.openEffectEditor(id, eqIndex); else project.addInsert(id, "builtin.eq") }
    inputChoices: { project.inputChannels; return project.inputChoices() }
    onInputChosen: (id, input) => project.setTrackInput(id, input)
    onGestureStarted: project.beginGesture()
    onGainMoved: (id, db) => project.setGainLive(id, db)
    onPanMoved: (id, pan) => project.setPanLive(id, pan)
    onGainReleased: (id, db) => { project.setGain(id, db); project.endGesture() }
    onPanReleased: (id, pan) => { project.setPan(id, pan); project.endGesture() }
    onMuteToggled: (id, on) => project.setMute(id, on)
    onSoloToggled: (id, on) => project.setSolo(id, on)
    onInsertAddRequested: (id) => project.addInsert(id, "builtin.gain")
    onInsertRemoveRequested: (id, index) => project.removeInsert(id, index)
    onInsertGainReleased: (id, index, db) => project.setInsertParam(id, index, "gainDb", db)
    onSendAddRequested: (id, targetId) => project.addSend(id, targetId)
    onSendRemoveRequested: (sendId) => project.removeSend(sendId)
    onSendLevelReleased: (sendId, db) => project.setSendLevel(sendId, db)
    onOutputRequested: (id, outputId) => project.setOutput(id, outputId)
    onStubUsed: (label) => project.announceStub(label)
    onPluginInsertRequested: (id, pluginId, name) => project.addPlugin(id, pluginId, name)
    onInsertEditorRequested: (id, index) => project.openPluginEditor(id, index)
    onPluginManagerRequested: project.pluginManagerOpen = true
    onRenameRequested: (id, name) => project.renameTrack(id, name)
    onInsertMoveRequested: (id, from, to, toTrackId) => project.moveInsert(id, from, to, toTrackId)
    onInsertBypassToggled: (id, index, on) => project.setInsertBypass(id, index, on)
    onBusViewRequested: (id) => project.showBus(id)
    onSendPreFaderToggled: (sendId, on) => project.setSendPreFader(sendId, on)
    onLibraryRequested: project.libraryVisible = true
    onNewBusRequested: (id, role) => project.newBusFor(id, role)
    onTrackToggled: (id, actionId, on) => project.setTrackToggle(actionId, id, on)
    onSwipeMoved: (kind, on, p) => swiped(kind, on, p, root)
    onMuteAllRequested: (on) => project.muteAll(on)
    onSoloAllRequested: (on) => project.soloAll(on)
    onSoloExclusiveRequested: (id) => project.soloExclusive(id)
    onSoloClearRequested: project.clearSolo()
    level: { project.peaksRevision; return project.trackPeak(root.trackId) }
    peakHold: { project.peaksRevision; return project.trackHold(root.trackId) }
    reduction: { project.peaksRevision; return project.trackReduction(root.trackId) }
    onPeakReset: project.resetPeaks()
    onSelectRequested: (id, modifiers) => project.selectTrack(id, modifiers & Qt.ShiftModifier ? "extend" : (modifiers & Qt.ControlModifier ? "toggle" : "replace"))
    selected: project.selectedTrackIds.indexOf(root.trackId) >= 0
}
