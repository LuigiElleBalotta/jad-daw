#include "bridge/snapshot.h"

#include <algorithm>
#include <cmath>

namespace jad {

namespace {

const char* const kColors[] = {"purple", "indigo", "blue", "teal", "green", "yellow", "orange", "red", "pink", "magenta"};

QString kindName(lpc::TrackKind k) {
    switch (k) {
        case lpc::TrackKind::Audio: return "audio";
        case lpc::TrackKind::Midi: return "midi";
        case lpc::TrackKind::Instrument: return "instrument";
        case lpc::TrackKind::Aux: return "aux";
        case lpc::TrackKind::Bus: return "bus";
        case lpc::TrackKind::Master: return "master";
    }
    return "audio";
}

QString colorFor(const std::string& requested, int index) {
    for (const char* c : kColors)
        if (requested == c) return c;
    return kColors[index % 10];
}

double toBeats(const lpc::Project& p, const lpc::Region& r, std::int64_t value) {
    if (r.timeBase == lpc::TimeBase::Musical) return static_cast<double>(value) / lpc::kPPQ;
    const double frames = static_cast<double>(value) * p.sampleRate / 1e6;
    return static_cast<double>(p.tempoMap.samplesToTicks(frames, p.sampleRate)) / lpc::kPPQ;
}

}  // namespace

Snapshot makeSnapshot(const lpc::Project& p, std::uint64_t revision, const std::function<bool(const lpc::MediaItem&)>& mediaPresent) {
    Snapshot s;
    s.revision = revision;
    s.name = QString::fromStdString(p.name);
    s.sampleRate = p.sampleRate;
    s.bpm = p.tempoMap.tempos().empty() ? 120.0 : p.tempoMap.tempos().front().bpm;
    s.beatsPerBar = p.tempoMap.signatures().empty() ? 4 : p.tempoMap.signatures().front().numerator;
    s.tempoMap = p.tempoMap;
    for (const lpc::MediaItem& m : p.mediaPool) s.mediaPaths.insert(QString::fromStdString(m.id.toString()), QString::fromStdString(m.path));

    int row = 0;
    for (const lpc::Track& t : p.tracks) {
        const bool master = t.kind == lpc::TrackKind::Master;
        TrackRow tr;
        tr.id = QString::fromStdString(t.id.toString());
        tr.name = QString::fromStdString(t.name);
        tr.kind = kindName(t.kind);
        tr.color = colorFor(t.color, master ? 0 : row);
        tr.master = master;
        tr.mute = t.strip.mute;
        tr.solo = t.strip.solo;
        tr.gainDb = t.strip.gainDb;
        tr.pan = t.strip.pan;
        tr.regionCount = static_cast<int>(t.regions.size());
        s.tracks.push_back(tr);
        if (master) continue;
        for (const lpc::Region& r : t.regions) {
            RegionRow rr;
            rr.id = QString::fromStdString(r.id.toString());
            rr.trackId = tr.id;
            rr.trackIndex = row;
            rr.color = tr.color;
            rr.startBeats = toBeats(p, r, r.start);
            rr.lengthBeats = toBeats(p, r, r.start + r.length) - rr.startBeats;
            rr.audio = t.kind == lpc::TrackKind::Audio;
            if (rr.audio) {
                rr.mediaId = QString::fromStdString(r.mediaId.toString());
                const lpc::MediaItem* item = p.findMedia(r.mediaId);
                rr.missing = !item || !mediaPresent(*item);
            }
            s.regions.push_back(rr);
        }
        ++row;
    }
    return s;
}

}  // namespace jad
