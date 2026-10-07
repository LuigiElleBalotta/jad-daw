#include "lpc/graph_builder.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "lpc/audio/processors.h"

namespace lpc {

using namespace audio;

namespace {

float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }
std::int64_t toFrames(double v) { return static_cast<std::int64_t>(std::llround(v)); }

// `value` is a start or end position in the region's own unit (ticks or microseconds).
std::int64_t regionFrame(const Project& p, const Region& r, std::int64_t value) {
    if (r.timeBase == TimeBase::Musical) return toFrames(p.tempoMap.ticksToSamples(value, p.sampleRate));
    return toFrames(static_cast<double>(value) * p.sampleRate / 1e6);
}

AudioMsg addMsg(std::unique_ptr<TrackNode> node) {
    AudioMsg m;
    m.kind = MsgKind::AddTrack;
    m.track = node->id;
    m.obj = makeOwned(node.release());
    return m;
}

AudioMsg removeMsg(const Uuid& id) {
    AudioMsg m;
    m.kind = MsgKind::RemoveTrack;
    m.track = id;
    return m;
}

AudioMsg stripMsg(const Track& t) {
    AudioMsg m;
    m.kind = MsgKind::SetStrip;
    m.track = t.id;
    m.strip = stripParamsOf(t.strip);
    return m;
}

AudioMsg configMsg(const Project& p, const Track& t, MediaStore& media) {
    AudioMsg m;
    m.kind = MsgKind::SetConfig;
    m.track = t.id;
    m.obj = makeOwned(buildConfig(p, t, media).release());
    return m;
}

AudioMsg reorderMsg(const Project& p) {
    AudioMsg m;
    m.kind = MsgKind::Reorder;
    m.obj = makeOwned(new std::vector<Uuid>(processingOrder(p)));
    return m;
}

bool stripChanged(const Track& a, const Track& b) {
    return a.strip.gainDb != b.strip.gainDb || a.strip.pan != b.strip.pan || a.strip.mute != b.strip.mute || a.strip.solo != b.strip.solo;
}

bool configChanged(const Track& a, const Track& b) {
    return a.kind != b.kind || a.regions != b.regions || a.instrument != b.instrument || a.strip.inserts != b.strip.inserts ||
           a.strip.sends != b.strip.sends || a.strip.output != b.strip.output;
}

}  // namespace

StripParams stripParamsOf(const Strip& s) { return StripParams{dbToLinear(s.gainDb), s.pan, s.mute, s.solo}; }

std::vector<Uuid> processingOrder(const Project& p) {
    std::vector<const Track*> pending;
    const Track* master = nullptr;
    for (const Track& t : p.tracks) {
        if (t.kind == TrackKind::Master) master = &t;
        else pending.push_back(&t);
    }
    std::unordered_map<Uuid, int> indegree;
    for (const Track* t : pending) indegree[t->id] = 0;
    auto forEachTarget = [](const Track& t, auto&& fn) {
        if (!t.strip.output.isNull()) fn(t.strip.output);
        for (const Send& s : t.strip.sends) fn(s.targetTrackId);
    };
    for (const Track* t : pending)
        forEachTarget(*t, [&](const Uuid& to) {
            if (auto it = indegree.find(to); it != indegree.end()) ++it->second;
        });

    std::vector<Uuid> order;
    while (!pending.empty()) {
        // among the tracks nothing feeds any more, sources go before buses; ties keep the project's track order
        auto ready = [&](const Track* t) { return indegree[t->id] == 0; };
        auto isBus = [](const Track* t) { return t->kind == TrackKind::Bus || t->kind == TrackKind::Aux; };
        auto it = std::find_if(pending.begin(), pending.end(), [&](const Track* t) { return ready(t) && !isBus(t); });
        if (it == pending.end()) it = std::find_if(pending.begin(), pending.end(), ready);
        if (it == pending.end()) it = pending.begin();  // a cycle cannot occur in a valid project; stay total anyway
        const Track* t = *it;
        order.push_back(t->id);
        forEachTarget(*t, [&](const Uuid& to) {
            if (auto d = indegree.find(to); d != indegree.end()) --d->second;
        });
        pending.erase(it);
    }
    if (master) order.push_back(master->id);
    return order;
}

std::unique_ptr<TrackConfig> buildConfig(const Project& p, const Track& t, MediaStore& media) {
    auto cfg = std::make_unique<TrackConfig>();
    for (const Region& r : t.regions) {
        RegionPlayback rp;
        rp.startFrame = regionFrame(p, r, r.start);
        rp.endFrame = regionFrame(p, r, r.start + r.length);
        rp.gain = dbToLinear(r.gainDb);
        if (t.kind == TrackKind::Audio) {
            const MediaItem* item = p.findMedia(r.mediaId);
            std::shared_ptr<IFrameSource> src = item ? media.open(*item) : nullptr;
            if (!src) continue;  // missing media: the region stays silent, MediaStore keeps the warning
            rp.source = src.get();
            rp.sourceOffsetFrames = r.sourceOffsetFrames;
            cfg->keepAlive.push_back(std::move(src));
        } else {
            for (const MidiNote& n : r.notes) {
                const std::int64_t on = toFrames(p.tempoMap.ticksToSamples(r.start + n.start, p.sampleRate));
                const std::int64_t off = toFrames(p.tempoMap.ticksToSamples(r.start + n.start + n.length, p.sampleRate));
                rp.notes.push_back(NoteSpan{on, std::max(on, off), n.note, n.velocity});
            }
            std::stable_sort(rp.notes.begin(), rp.notes.end(), [](const NoteSpan& a, const NoteSpan& b) { return a.onFrame < b.onFrame; });
        }
        cfg->regions.push_back(std::move(rp));
    }
    for (const ProcessorRef& ref : t.strip.inserts)
        if (auto effect = makeEffect(ref)) cfg->inserts.push_back(std::move(effect));
    for (const Send& s : t.strip.sends) cfg->sends.push_back(SendPlayback{s.targetTrackId, dbToLinear(s.levelDb), s.preFader});
    cfg->output = t.strip.output;
    return cfg;
}

std::unique_ptr<TrackNode> buildNode(const Project& p, const Track& t, MediaStore& media) {
    return std::make_unique<TrackNode>(t.id, t.kind, stripParamsOf(t.strip), buildConfig(p, t, media).release(),
                                       static_cast<double>(p.sampleRate));
}

std::vector<AudioMsg> initialMessages(const Project& p, MediaStore& media) {
    std::vector<AudioMsg> out;
    for (const Track& t : p.tracks) out.push_back(addMsg(buildNode(p, t, media)));
    out.push_back(reorderMsg(p));
    return out;
}

std::vector<AudioMsg> diffToMessages(const Project& before, const Project& after, MediaStore& media) {
    std::vector<AudioMsg> out;
    bool structural = false;
    const bool timingChanged = before.tempoMap != after.tempoMap || before.sampleRate != after.sampleRate;

    for (const Track& a : after.tracks) {
        if (!before.findTrack(a.id)) {
            out.push_back(addMsg(buildNode(after, a, media)));
            structural = true;
        }
    }
    for (const Track& a : after.tracks) {
        const Track* b = before.findTrack(a.id);
        if (!b) continue;
        if (stripChanged(*b, a)) out.push_back(stripMsg(a));
        if (timingChanged || configChanged(*b, a)) out.push_back(configMsg(after, a, media));
    }
    for (const Track& b : before.tracks) {
        if (!after.findTrack(b.id)) {
            out.push_back(removeMsg(b.id));
            structural = true;
        }
    }
    if (structural || processingOrder(before) != processingOrder(after)) out.push_back(reorderMsg(after));
    return out;
}

}  // namespace lpc
