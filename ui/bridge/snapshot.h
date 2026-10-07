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
    bool audio = false, missing = false;
    QString mediaId;
    QString color;  // the colour name of its track
};

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
