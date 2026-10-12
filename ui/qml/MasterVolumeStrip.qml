import QtQuick
import Jad

// The Master strip: one fader that scales Stereo Out and every Output strip together (Logic's Master). It is a setting of the project, not a track.
ChannelStrip {
    id: root
    required property ProjectController project
    volumeOnly: true
    showSlots: false
    info: ({ trackId: "__master", name: qsTr("Master"), color: "purple", kind: "master", master: true, gainDb: project.masterGainDb, pan: 0, mute: false, solo: false, inserts: [], sends: [] })
    level: { project.peaksRevision; return project.masterPeak }
    onGestureStarted: project.beginGesture()
    onGainReleased: (id, db) => { project.setMasterGain(db); project.endGesture() }
    onGainMoved: (id, db) => project.setMasterGain(db)
}
