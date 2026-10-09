#include "bridge/snapshot.h"

#include <algorithm>
#include <cmath>

#include <nlohmann/json.hpp>

#include "lpc/model_json.h"
#include "lpc/patch_library.h"
#include "lpc/processor_ids.h"

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

Snapshot makeSnapshot(const lpc::Project& p, std::uint64_t revision, const std::function<bool(const lpc::MediaItem&)>& mediaPresent,
                      const lpc::PatchLibrary* patches) {
    Snapshot s;
    s.revision = revision;
    s.name = QString::fromStdString(p.name);
    s.sampleRate = p.sampleRate;
    s.bpm = p.tempoMap.tempos().empty() ? 120.0 : p.tempoMap.tempos().front().bpm;
    s.beatsPerBar = p.tempoMap.signatures().empty() ? 4 : p.tempoMap.signatures().front().numerator;
    s.beatUnit = p.tempoMap.signatures().empty() ? 4 : p.tempoMap.signatures().front().denominator;
    s.tempoMap = p.tempoMap;
    for (const lpc::Marker& m : p.markers)
        s.markers.push_back({QString::fromStdString(m.id.toString()), QString::fromStdString(m.name), static_cast<double>(m.tick) / lpc::kPPQ});
    for (const lpc::MediaItem& m : p.mediaPool) s.mediaPaths.insert(QString::fromStdString(m.id.toString()), QString::fromStdString(m.path));

    int row = 0;       // colour rotation: counts every non-master track
    int shownRow = 0;  // the timeline row: counts the tracks shown in the Tracks area
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
        tr.showInTracks = t.showInTracks;
        tr.patchId = QString::fromStdString(t.patchId);
        tr.instrument = t.instrument ? QString::fromStdString(t.instrument->processorId) : QString();
        if (!master) {
            const lpc::Track* out = t.strip.output.isNull() ? p.master() : p.findTrack(t.strip.output);
            if (out) {
                tr.outputId = QString::fromStdString(out->id.toString());
                tr.outputName = QString::fromStdString(out->name);
            }
        }
        for (const lpc::ProcessorRef& ins : t.strip.inserts) {
            const auto g = ins.params.find("gainDb");
            tr.inserts.push_back({QString::fromStdString(ins.processorId), g == ins.params.end() ? 0.0 : g->second,
                                  QString::fromStdString(ins.label), lpc::isVst3Id(ins.processorId), ins.bypass});
        }
        for (const lpc::Send& s : t.strip.sends) {
            const lpc::Track* target = p.findTrack(s.targetTrackId);
            tr.sends.push_back({QString::fromStdString(s.id.toString()), QString::fromStdString(s.targetTrackId.toString()),
                                target ? QString::fromStdString(target->name) : QString(), s.levelDb, s.preFader});
        }
        if (patches && !t.patchId.empty()) {
            if (const lpc::Patch* patch = patches->find(t.patchId)) {
                tr.patchName = QString::fromStdString(patch->name);
                for (const lpc::SmartControl& c : patch->smartControls) {
                    const auto value = lpc::PatchLibrary::smartControlValue(t, c);
                    if (!value) continue;  // the insert it drives is gone
                    SmartRow sr;
                    sr.id = QString::fromStdString(c.id);
                    sr.label = QString::fromStdString(c.label);
                    sr.group = QString::fromStdString(c.group);
                    sr.min = c.min;
                    sr.max = c.max;
                    sr.value = *value;
                    sr.def = c.def;
                    tr.smart.push_back(sr);
                }
            }
        }
        s.tracks.push_back(tr);
        if (master) continue;
        for (const lpc::Region& r : t.regions) {
            RegionRow rr;
            rr.id = QString::fromStdString(r.id.toString());
            rr.trackId = tr.id;
            rr.trackIndex = shownRow;
            rr.color = tr.color;
            rr.gainDb = r.gainDb;
            rr.json = nlohmann::json(r).dump();
            rr.absolute = r.timeBase == lpc::TimeBase::Absolute;
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
        if (t.showInTracks) ++shownRow;
    }
    return s;
}

}  // namespace jad
