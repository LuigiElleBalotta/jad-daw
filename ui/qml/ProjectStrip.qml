import QtQuick
import Jad

// A ChannelStrip wired to the controller: every edit is a command.
ChannelStrip {
    id: root
    required property ProjectController project
    // what the Core accepts: no loop through outputs and sends (routingRevision makes the list follow every change)
    targets: { project.routingRevision; return project.targetsFor(root.trackId) }
    pluginGroups: project.plugins.menu
    knownPluginIds: project.plugins.knownIds

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
    onMuteAllRequested: (on) => project.muteAll(on)
    onSoloAllRequested: (on) => project.soloAll(on)
    onSoloExclusiveRequested: (id) => project.soloExclusive(id)
    onSoloClearRequested: project.clearSolo()
    onPeakReset: project.announceStub(qsTr("Peak reset"))
    onSelectRequested: (id, modifiers) => project.selectTrack(id, modifiers & Qt.ShiftModifier ? "extend" : (modifiers & Qt.ControlModifier ? "toggle" : "replace"))
    selected: project.selectedTrackIds.indexOf(root.trackId) >= 0
}
