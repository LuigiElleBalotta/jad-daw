import QtQuick
import QtTest
import Jad

// Builds docs/showcase/Showcase.lpc, the project the README pictures are taken from (see tools/screenshots/README.md).
// Run it with the QML test runner:  jad_ui_qmltests -input tools/screenshots/build_showcase.qml -import <build>/ui
TestCase {
    name: "BuildShowcase"
    width: 400; height: 300
    visible: true
    when: windowShown

    Component { id: ctlC; ProjectController { audioEnabled: false } }

    function trackIds(c) {
        const out = []
        for (let i = 0; i < c.tracks.rowCount(); ++i) out.push(c.tracks.trackIdAt(i))
        return out
    }
    function regionIds(c) {
        const out = []
        for (let i = 0; i < c.regions.rowCount(); ++i) out.push(c.regions.regionIdAt(i))
        return out
    }
    // makes an empty MIDI region and returns its id
    function region(c, trackId, start, length) {
        const before = regionIds(c)
        c.createRegion(trackId, start, length)
        tryVerify(function () { return regionIds(c).length === before.length + 1 })
        return regionIds(c).filter(id => before.indexOf(id) < 0)[0]
    }
    function newTrack(c, kind, name, color) {
        const before = trackIds(c)
        c.addTrack(kind)
        tryVerify(function () { return trackIds(c).length === before.length + 1 })
        const id = trackIds(c).filter(t => before.indexOf(t) < 0)[0]
        c.renameTrack(id, name)
        c.setTrackColor(id, color)
        return id
    }
    function n(start, length, note, velocity) { return { start: start, length: length, note: note, velocity: velocity, muted: false } }

    function test_build() {
        const c = createTemporaryObject(ctlC, this)
        const folder = Qt.resolvedUrl("../../docs/showcase/Showcase.lpc")
        verify(c.newProject(folder))
        c.setTempo(100)

        // drums: kick, snare and hats over four bars
        const drums = newTrack(c, "instrument", "Drums", "orange")
        c.setInstrument(drums, "builtin.drums")
        const dr = region(c, drums, 0, 16)
        const dn = []
        for (let bar = 0; bar < 4; ++bar) {
            const b = bar * 4
            for (const k of [0, 1.5, 2.5]) dn.push(n(b + k, 0.25, 36, 112))
            for (const s of [1, 3]) dn.push(n(b + s, 0.25, 38, 104))
            for (let h = 0; h < 8; ++h) dn.push(n(b + h * 0.5, 0.25, 42, h % 2 === 0 ? 92 : 70))
        }
        c.setRegionNotes(dr, dn)

        // bass: a saw line
        const bass = newTrack(c, "instrument", "Bass", "blue")
        c.setInstrument(bass, "builtin.synth")
        c.setInstrumentParam(bass, "wave", 2)
        c.setInstrumentParam(bass, "cutoff", 700)
        const bn = []
        const roots = [45, 41, 36, 43]  // A, F, C, G
        for (let bar = 0; bar < 4; ++bar)
            for (const [off, len] of [[0, 1], [1.5, 0.5], [2, 1], [3.5, 0.5]]) bn.push(n(bar * 4 + off, len, roots[bar], 100))
        c.setRegionNotes(region(c, bass, 0, 16), bn)

        // keys: the chords, one bar each
        const keys = newTrack(c, "instrument", "Keys", "purple")
        c.setInstrument(keys, "builtin.synth")
        c.setInstrumentParam(keys, "wave", 1)
        c.setInstrumentParam(keys, "attack", 40)
        c.setInstrumentParam(keys, "release", 600)
        const chords = [[72, 76, 79], [72, 77, 81], [71, 76, 79], [71, 74, 79]]  // high enough for the piano roll to show them without scrolling
        const kn = []
        for (let bar = 0; bar < 4; ++bar) for (const p of chords[bar]) kn.push(n(bar * 4, 3.75, p, 78))
        c.setRegionNotes(region(c, keys, 0, 16), kn)

        // lead: a short melody
        const lead = newTrack(c, "instrument", "Lead", "green")
        c.setInstrument(lead, "builtin.synth")
        c.setInstrumentParam(lead, "wave", 0)
        const melody = [[0, 1, 72], [1, 0.5, 74], [1.5, 0.5, 76], [2, 2, 79], [4, 1, 72], [5, 1, 69], [6, 2, 72], [8, 1, 76], [9, 1, 79], [10, 1, 76], [11, 1, 72], [12, 2, 74], [14, 2, 67]]
        c.setRegionNotes(region(c, lead, 0, 16), melody.map(m => n(m[0], m[1], m[2], 90)))

        // an audio loop
        const loop = newTrack(c, "audio", "Drum Loop", "red")
        c.importAudioFilesAt([Qt.resolvedUrl("../../docs/showcase/loop.wav")], loop, 0)
        tryVerify(function () { return regionIds(c).length >= 5 }, 8000)

        // effects, so that the Inserts and the plug-in windows have something to show
        c.addInsert(keys, "builtin.eq")
        c.addInsert(bass, "builtin.compressor")
        c.addInsert(lead, "builtin.delay")
        c.addInsert(lead, "builtin.reverb")
        c.addInsert(drums, "builtin.limiter")
        c.addInsert(drums, "builtin.adaptivelimiter")
        c.addInsert(drums, "builtin.matcheq")
        c.addInsert(loop, "builtin.gate")
        c.addInsert(loop, "builtin.mlsaturation")
        c.addInsert(c.masterTrackId(), "builtin.mastering")
        tryVerify(function () { return c.trackInserts(keys).length === 1 && c.trackInserts(lead).length === 2 && c.trackInserts(drums).length === 3 && c.trackInserts(c.masterTrackId()).length === 1 })
        // a visible curve on the EQ, and settings that make the compressor and the delay look used
        c.setInsertParam(keys, 0, "lcOn", 1)
        c.setInsertParam(keys, 0, "lcFreq", 90)
        c.setInsertParam(keys, 0, "lsGain", -2)
        c.setInsertParam(keys, 0, "p1Freq", 220)
        c.setInsertParam(keys, 0, "p1Gain", 3.5)
        c.setInsertParam(keys, 0, "p2Freq", 520)
        c.setInsertParam(keys, 0, "p2Gain", -4)
        c.setInsertParam(keys, 0, "p3Freq", 2400)
        c.setInsertParam(keys, 0, "p3Gain", 2.5)
        c.setInsertParam(keys, 0, "hsGain", 3)
        c.setInsertParam(bass, 0, "threshold", -24)
        c.setInsertParam(bass, 0, "ratio", 4)
        c.setInsertParam(lead, 0, "time", 375)
        c.setInsertParam(lead, 0, "feedback", 40)
        c.setInsertParam(lead, 1, "size", 70)
        c.setInsertParam(loop, 0, "threshold", -40)
        c.setGain(bass, -3)
        c.setGain(keys, -5)
        c.setGain(lead, -6)
        c.setGain(loop, -8)
        c.setPan(keys, -0.25)
        c.setPan(lead, 0.3)
        verify(c.saveProject())
    }
}
