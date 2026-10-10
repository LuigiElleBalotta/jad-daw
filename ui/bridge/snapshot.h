#pragma once
#include <string>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <cstdint>
#include <functional>
#include <vector>

#include "lpc/model.h"

namespace lpc {
class PatchLibrary;
}

namespace jad {

struct InsertRow {
    QString processorId;
    double gainDb = 0.0;  // the "gainDb" parameter (0 when absent)
    QString label;        // a plug-in's display name
    bool plugin = false;  // a hosted plug-in (its gain is not editable from the strip)
    bool bypass = false;  // switched off: the signal passes through
    QVariantMap params;   // every parameter of a built-in effect, by name
};
struct SendRow {
    QString id, targetId, targetName;
    double levelDb = 0.0;
    bool preFader = false;
};
struct SmartRow {
    QString id, label, group;
    double min = 0.0, max = 1.0, value = 0.0, def = 0.0;
};

struct AutoRow {
    double beats = 0, value = 0;  // volume in dB, pan in -1..1
};

struct TrackRow {
    QString id, name, kind, color;
    bool master = false, mute = false, solo = false;
    bool recordArm = false, inputMonitor = false, soloSafe = false;  // the R and I stubs: their state is kept by the controller
    double gainDb = 0.0, pan = 0.0;
    int input = 0;  // the recording input: 0 stereo 1+2, n mono input n
    QString groupId;  // the group the track is in (empty: none)
    QString automationMode = QStringLiteral("read");  // off, read, touch, latch or write
    int regionCount = 0;
    bool showInTracks = true;  // false: hidden from the Tracks area (listed in the Mixer only)
    bool hidden = false;       // the track is hidden but shown anyway (Track > Toggle Hide View): drawn dimmed
    QVariantMap instrumentParams;  // the parameters of the instrument (the synth's)
    QString instrumentLabel;  // the name of a plug-in instrument
    int transpose = 0, velocity = 0, keyLow = 0, keyHigh = 127, velocityLow = 1, velocityHigh = 127;  // the shaping of the notes of an instrument track
    QString patchId, patchName, instrument, outputId, outputName;  // outputId: the master's id when the output is the master ("" on the master)
    std::vector<InsertRow> inserts;
    std::vector<SendRow> sends;
    std::vector<SmartRow> smart;  // the Smart Controls of the track's patch, with their current values
    std::vector<AutoRow> volumeAuto, panAuto;  // the automation lanes
};

struct GroupRow {
    QString id, name;
    QStringList members;
    bool volume = true, pan = false, mute = true, solo = true, selection = true;
};

struct RegionRow {
    QString id, trackId;
    int trackIndex = 0;  // among the shown tracks without the master: the timeline row
    double startBeats = 0.0, lengthBeats = 0.0;
    std::int64_t mediaFrames = 0;  // the whole file (audio)
    std::int64_t sourceOffsetFrames = 0, lengthFrames = 0;  // the part of the media this region plays (audio)
    double fadeInBeats = 0.0, fadeOutBeats = 0.0;  // the fades of an audio region, in beats
    bool audio = false, missing = false, absolute = false;  // absolute: positions are in real time, not musical time
    QString mediaId;
    QString color;  // the colour name of its track
    double gainDb = 0.0;
    std::string json;  // the whole region as the Core stores it (copy and paste)
    bool muted = false;  // Mute Regions (Alt+M): shown grey, silent
    double loopBeats = 0.0;  // > 0: the region loops every loopBeats
    int transpose = 0, velocityOffset = 0;  // MIDI region: how its notes are shaped when they play
    double quantizeBeats = 0.0;             // 0: off
};

// True when `a` and `b` hold the same ids in the same order: the models then update in place (dataChanged) instead of
// resetting, so delegates survive and a drag in progress is not lost.
template <class Row>
bool sameIds(const std::vector<Row>& a, const std::vector<Row>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].id != b[i].id) return false;
    return true;
}

// Everything the UI shows, copied out of a Project on the project thread. Plain data: safe to move to the Qt thread.
struct MarkerRow {
    QString id, name;
    double beats = 0;
};

struct Snapshot {
    std::uint64_t revision = 0;
    QString name;
    int sampleRate = 48000;
    double bpm = 120.0;
    int beatsPerBar = 4;
    int beatUnit = 4;  // the denominator of the first time signature
    std::vector<TrackRow> tracks;  // project order, master included
    std::vector<RegionRow> regions;
    std::vector<MarkerRow> markers;  // sorted by position
    std::vector<GroupRow> groups;
    lpc::TempoMap tempoMap;
    QHash<QString, QString> mediaPaths;  // media id -> path relative to the project folder
};

// showHidden: the tracks hidden from the Tracks area are listed anyway (flagged `hidden`), with their regions
Snapshot makeSnapshot(const lpc::Project& project, std::uint64_t revision,
                      const std::function<bool(const lpc::MediaItem&)>& mediaPresent, const lpc::PatchLibrary* patches = nullptr, bool showHidden = false);

}  // namespace jad
