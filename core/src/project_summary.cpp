#include "lpc/project_summary.h"

#include "lpc/offline_render.h"

#include <algorithm>
#include <cmath>

namespace lpc {

namespace {

using nlohmann::json;

const char* kindName(TrackKind k) {
    static const char* names[] = {"audio", "midi", "instrument", "aux", "bus", "master", "output"};
    return names[static_cast<int>(k)];
}

}  // namespace

json projectSummary(const Project& p) {
    json j;
    j["name"] = p.name;
    j["sampleRate"] = p.sampleRate;
    j["masterVolumeDb"] = p.masterVolumeDb;
    j["tempo"] = json::array();
    for (const auto& e : p.tempoMap.tempos()) j["tempo"].push_back({{"beat", static_cast<double>(e.tick) / kPPQ}, {"bpm", e.bpm}});
    j["timeSignature"] = json::array();
    for (const auto& e : p.tempoMap.signatures()) j["timeSignature"].push_back({{"beat", static_cast<double>(e.tick) / kPPQ}, {"numerator", e.numerator}, {"denominator", e.denominator}});
    j["markers"] = json::array();
    for (const Marker& m : p.markers) j["markers"].push_back({{"id", m.id.toString()}, {"beat", static_cast<double>(m.tick) / kPPQ}, {"name", m.name}, {"text", m.text}});
    if (!p.arrangement.empty()) {
        j["arrangement"] = json::array();
        for (const Marker& m : p.arrangement) j["arrangement"].push_back({{"beat", static_cast<double>(m.tick) / kPPQ}, {"name", m.name}});
    }
    if (!p.chords.empty()) {
        j["chords"] = json::array();
        for (const Marker& m : p.chords) j["chords"].push_back({{"beat", static_cast<double>(m.tick) / kPPQ}, {"chord", m.name}});
    }
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
            // a region on the musical grid is placed in ticks, one in time in microseconds: both are given in beats and in seconds
            const double rate = std::max(1, p.sampleRate);
            auto round3 = [](double v) { return std::round(v * 1000.0) / 1000.0; };
            double startSec, endSec, startBeat, endBeat;
            if (r.timeBase == TimeBase::Absolute) {
                startSec = static_cast<double>(r.start) / 1e6;
                endSec = static_cast<double>(r.start + r.length) / 1e6;
                startBeat = static_cast<double>(p.tempoMap.samplesToTicks(startSec * rate, p.sampleRate)) / kPPQ;
                endBeat = static_cast<double>(p.tempoMap.samplesToTicks(endSec * rate, p.sampleRate)) / kPPQ;
            } else {
                startBeat = static_cast<double>(r.start) / kPPQ;
                endBeat = static_cast<double>(r.start + r.length) / kPPQ;
                startSec = p.tempoMap.ticksToSamples(r.start, p.sampleRate) / rate;
                endSec = p.tempoMap.ticksToSamples(r.start + r.length, p.sampleRate) / rate;
            }
            json rr = {{"id", r.id.toString()}, {"startBeat", round3(startBeat)}, {"lengthBeats", round3(endBeat - startBeat)}, {"startSeconds", round3(startSec)}, {"lengthSeconds", round3(endSec - startSec)},
                       {"muted", r.muted}, {"gainDb", r.gainDb}, {"notes", static_cast<int>(r.notes.size())}};
            if (!r.mediaId.isNull())
                if (const MediaItem* m = p.findMedia(r.mediaId)) rr["file"] = m->path;
            if (r.flex) rr["flex"] = true;
            if (!r.takeGroup.empty()) rr["takeGroup"] = r.takeGroup;
            if (!r.generator.empty()) rr["sessionPlayer"] = true;
            x["regions"].push_back(std::move(rr));
        }
        if (!t.automation.empty()) {
            x["automation"] = json::array();
            for (const AutomationLane& l : t.automation) x["automation"].push_back({{"target", l.target}, {"points", static_cast<int>(l.points.size())}});
        }
        if (t.flexMode.size()) x["flexMode"] = t.flexMode;
        if (t.outputPair != 0) x["outputPair"] = t.outputPair;
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
