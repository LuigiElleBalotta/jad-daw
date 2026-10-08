import QtQuick
import Jad

// A ChannelStrip wired to the controller: every edit is a command.
ChannelStrip {
    id: root
    required property ProjectController project
    targets: project.inspector.busTargets.filter((t) => t.id !== root.trackId)

    onGainReleased: (id, db) => project.setGain(id, db)
    onPanReleased: (id, pan) => project.setPan(id, pan)
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
}
