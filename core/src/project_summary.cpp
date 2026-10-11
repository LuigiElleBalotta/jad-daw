#include "lpc/project_summary.h"

#include "lpc/offline_render.h"

#include <algorithm>
#include <cmath>

namespace lpc {

namespace {

using nlohmann::json;

const char* kindName(TrackKind k) {
    static const char* names[] = {"audio", "midi", "instrument", "aux", "bus", "master"};
    return names[static_cast<int>(k)];
}

}  // namespace

json projectSummary(const Project& p) {
    json j;
    j["name"] = p.name;
    j["sampleRate"] = p.sampleRate;
    j["tempo"] = json::array();
    for (const auto& e : p.tempoMap.tempos()) j["tempo"].push_back({{"beat", static_cast<double>(e.tick) / kPPQ}, {"bpm", e.bpm}});
    j["timeSignature"] = json::array();
    for (const auto& e : p.tempoMap.signatures()) j["timeSignature"].push_back({{"beat", static_cast<double>(e.tick) / kPPQ}, {"numerator", e.numerator}, {"denominator", e.denominator}});
    j["markers"] = json::array();
    for (const Marker& m : p.markers) j["markers"].push_back({{"id", m.id.toString()}, {"beat", static_cast<double>(m.tick) / kPPQ}, {"name", m.name}, {"text", m.text}});
    j["lengthSeconds"] = std::round(static_cast<double>(projectEndFrame(p)) / std::max(1, p.sampleRate) * 100.0) / 100.0;
    json tracks = json::array();
    for (const Track& t : p.tracks) {
        json x = {{"id", t.id.toString()}, {"name", t.name}, {"kind", kindName(t.kind)}, {"gainDb", t.strip.gainDb}, {"pan", t.strip.pan}, {"mute", t.strip.mute}, {"solo", t.strip.solo}};
        if (!t.strip.output.isNull()) {
            const Track* out = p.findTrack(t.strip.output);
            x["output"] = out ? out->name : t.strip.output.toString();
        } else if (t.kind != TrackKind::Master) {
            x["output"] = "Master";
        }
        if (t.instrument) x["instrument"] = {{"processorId", t.instrument->processorId}, {"params", t.instrument->params}, {"label", t.instrument->label}};
        x["inserts"] = json::array();
        int index = 0;
        for (const ProcessorRef& i : t.strip.inserts) x["inserts"].push_back({{"index", index++}, {"processorId", i.processorId}, {"label", i.label}, {"bypass", i.bypass}, {"params", i.params}});
        x["sends"] = json::array();
        for (const Send& s : t.strip.sends) {
            const Track* target = p.findTrack(s.targetTrackId);
            x["sends"].push_back({{"id", s.id.toString()}, {"target", target ? target->name : s.targetTrackId.toString()}, {"levelDb", s.levelDb}, {"preFader", s.preFader}});
        }
        x["regions"] = json::array();
        for (const Region& r : t.regions) {
            json rr = {{"id", r.id.toString()}, {"startBeat", static_cast<double>(r.start) / kPPQ}, {"lengthBeats", static_cast<double>(r.length) / kPPQ},
                       {"muted", r.muted}, {"gainDb", r.gainDb}, {"notes", static_cast<int>(r.notes.size())}};
            if (!r.mediaId.isNull())
                if (const MediaItem* m = p.findMedia(r.mediaId)) rr["file"] = m->path;
            if (r.flex) rr["flex"] = true;
            if (!r.generator.empty()) rr["sessionPlayer"] = true;
            x["regions"].push_back(std::move(rr));
        }
        if (!t.automation.empty()) {
            x["automation"] = json::array();
            for (const AutomationLane& l : t.automation) x["automation"].push_back({{"target", l.target}, {"points", static_cast<int>(l.points.size())}});
        }
        if (t.flexMode.size()) x["flexMode"] = t.flexMode;
        if (!t.stack.empty()) x["stack"] = t.stack;
        if (!t.parent.isNull()) x["parent"] = t.parent.toString();
        if (!t.alternatives.empty()) {
            x["alternative"] = t.alternative.empty() ? "Main" : t.alternative;
            x["otherAlternatives"] = static_cast<int>(t.alternatives.size());
        }
        tracks.push_back(std::move(x));
    }
    j["tracks"] = std::move(tracks);
    return j;
}

}  // namespace lpc
