#pragma once
#include <QHash>
#include <QString>
#include <cstdint>
#include <functional>
#include <vector>

#include "lpc/model.h"

namespace jad {

struct TrackRow {
    QString id, name, kind, color;
    bool master = false, mute = false, solo = false;
    double gainDb = 0.0, pan = 0.0;
    int regionCount = 0;
};

struct RegionRow {
    QString id, trackId;
    int trackIndex = 0;  // among the tracks without the master: the timeline row
    double startBeats = 0.0, lengthBeats = 0.0;
    bool audio = false, missing = false, absolute = false;  // absolute: positions are in real time, not musical time
    QString mediaId;
    QString color;  // the colour name of its track
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
struct Snapshot {
    std::uint64_t revision = 0;
    QString name;
    int sampleRate = 48000;
    double bpm = 120.0;
    int beatsPerBar = 4;
    std::vector<TrackRow> tracks;  // project order, master included
    std::vector<RegionRow> regions;
    lpc::TempoMap tempoMap;
    QHash<QString, QString> mediaPaths;  // media id -> path relative to the project folder
};

Snapshot makeSnapshot(const lpc::Project& project, std::uint64_t revision,
                      const std::function<bool(const lpc::MediaItem&)>& mediaPresent);

}  // namespace jad
