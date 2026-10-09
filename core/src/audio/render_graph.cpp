#include "lpc/audio/render_graph.h"

#include <algorithm>
#include <cmath>

namespace lpc::audio {

namespace {
bool isSource(TrackKind k) { return k == TrackKind::Audio || k == TrackKind::Midi || k == TrackKind::Instrument; }
}  // namespace

TrackNode::TrackNode(Uuid id_, TrackKind kind_, const StripParams& strip_, TrackConfig* config_, double sampleRate)
    : id(id_), kind(kind_), strip(strip_), config(config_), l(kMaxBlock, 0.0f), r(kMaxBlock, 0.0f), synth(sampleRate) {}

RenderGraph::RenderGraph(double sampleRate)
    : sampleRate_(sampleRate), scratchL_(kMaxBlock), scratchR_(kMaxBlock), preL_(kMaxBlock), preR_(kMaxBlock), dlyL_(kMaxBlock), dlyR_(kMaxBlock) {}

RenderGraph::~RenderGraph() {
    for (int i = 0; i < count_; ++i) delete nodes_[static_cast<std::size_t>(i)];
}

void RenderGraph::deleteNode(void* p) { delete static_cast<TrackNode*>(p); }
void RenderGraph::deleteConfig(void* p) { delete static_cast<TrackConfig*>(p); }

TrackNode* RenderGraph::find(const Uuid& id) const noexcept {
    for (int i = 0; i < count_; ++i)
        if (nodes_[static_cast<std::size_t>(i)]->id == id) return nodes_[static_cast<std::size_t>(i)];
    return nullptr;
}

Owned RenderGraph::apply(const AudioMsg& m) noexcept {
    switch (m.kind) {
        case MsgKind::AddTrack: {
            auto* node = static_cast<TrackNode*>(m.obj.ptr);
            if (!node) return {};
            if (count_ >= kMaxTracks) return Owned{node, &RenderGraph::deleteNode};  // full: give it back
            nodes_[static_cast<std::size_t>(count_++)] = node;
            return {};
        }
        case MsgKind::RemoveTrack: {
            for (int i = 0; i < count_; ++i) {
                if (nodes_[static_cast<std::size_t>(i)]->id != m.track) continue;
                TrackNode* node = nodes_[static_cast<std::size_t>(i)];
                for (int j = i; j < count_ - 1; ++j) nodes_[static_cast<std::size_t>(j)] = nodes_[static_cast<std::size_t>(j + 1)];
                nodes_[static_cast<std::size_t>(--count_)] = nullptr;
                return Owned{node, &RenderGraph::deleteNode};
            }
            return {};
        }
        case MsgKind::SetStrip: {
            if (TrackNode* n = find(m.track)) n->strip = m.strip;
            return {};
        }
        case MsgKind::SetConfig: {
            auto* cfg = static_cast<TrackConfig*>(m.obj.ptr);
            TrackNode* n = find(m.track);
            if (!n) return Owned{cfg, &RenderGraph::deleteConfig};
            TrackConfig* old = n->config;
            n->config = cfg;
            n->synth.releaseAll();  // the new config only knows its own note-offs: do not leave held notes droning
            return Owned{old, &RenderGraph::deleteConfig};
        }
        case MsgKind::Reorder: {
            const auto* order = static_cast<const std::vector<Uuid>*>(m.obj.ptr);
            if (order && static_cast<int>(order->size()) == count_) {
                bool complete = true;
                for (int k = 0; k < count_ && complete; ++k) {
                    TrackNode* n = find((*order)[static_cast<std::size_t>(k)]);
                    if (!n) complete = false;
                    else tmp_[static_cast<std::size_t>(k)] = n;
                }
                if (complete) {
                    // every listed id exists and the sizes match; reject lists with duplicates
                    for (int a = 0; a < count_ && complete; ++a)
                        for (int b = a + 1; b < count_; ++b)
                            if (tmp_[static_cast<std::size_t>(a)] == tmp_[static_cast<std::size_t>(b)]) {
                                complete = false;
                                break;
                            }
                }
                if (complete)
                    for (int k = 0; k < count_; ++k) nodes_[static_cast<std::size_t>(k)] = tmp_[static_cast<std::size_t>(k)];
            }
            return m.obj;  // the order list is always handed back
        }
        default:
            return {};
    }
}

void RenderGraph::allNotesOff() noexcept {
    for (int i = 0; i < count_; ++i) nodes_[static_cast<std::size_t>(i)]->synth.allNotesOff();
}

void RenderGraph::renderAudio(TrackNode& t, const TrackConfig& cfg, std::int64_t blockStart, int n) noexcept {
    const std::int64_t blockEnd = blockStart + n;
    for (const RegionPlayback& reg : cfg.regions) {
        if (!reg.source) continue;
        const std::int64_t lo = std::max(blockStart, reg.startFrame);
        const std::int64_t hi = std::min(blockEnd, reg.endFrame);
        if (lo >= hi) continue;
        const int count = static_cast<int>(hi - lo);
        const int offset = static_cast<int>(lo - blockStart);
        reg.source->read(reg.sourceOffsetFrames + (lo - reg.startFrame), scratchL_.data(), scratchR_.data(), count);
        for (int i = 0; i < count; ++i) {
            t.l[static_cast<std::size_t>(offset + i)] += scratchL_[static_cast<std::size_t>(i)] * reg.gain;
            t.r[static_cast<std::size_t>(offset + i)] += scratchR_[static_cast<std::size_t>(i)] * reg.gain;
        }
    }
}

void RenderGraph::renderInstrument(TrackNode& t, const TrackConfig& cfg, std::int64_t blockStart, int n) noexcept {
    struct Event {
        std::int64_t frame;
        bool on;
        std::uint8_t note;
        std::uint8_t velocity;
    };
    Event events[kMaxBlockEvents];
    int count = 0;
    const std::int64_t blockEnd = blockStart + n;
    for (const RegionPlayback& reg : cfg.regions) {
        for (const NoteSpan& s : reg.notes) {
            if (s.onFrame >= blockStart && s.onFrame < blockEnd && count < kMaxBlockEvents) events[count++] = {s.onFrame, true, s.note, s.velocity};
            if (s.offFrame >= blockStart && s.offFrame < blockEnd && count < kMaxBlockEvents) events[count++] = {s.offFrame, false, s.note, 0};
        }
    }
    for (int i = 1; i < count; ++i) {  // stable insertion sort by frame (events are few)
        const Event key = events[i];
        int j = i - 1;
        while (j >= 0 && events[j].frame > key.frame) {
            events[j + 1] = events[j];
            --j;
        }
        events[j + 1] = key;
    }
    int pos = 0;
    float* l = t.l.data();
    float* r = t.r.data();
    for (int i = 0; i < count; ++i) {
        const int at = static_cast<int>(events[i].frame - blockStart);
        if (at > pos) {
            t.synth.render(l + pos, r + pos, at - pos);
            pos = at;
        }
        if (events[i].on) t.synth.noteOn(events[i].note, events[i].velocity);
        else t.synth.noteOff(events[i].note);
    }
    if (pos < n) t.synth.render(l + pos, r + pos, n - pos);
}

namespace {
float autoValue(const std::vector<AutoPoint>& pts, std::int64_t frame) noexcept {
    if (frame <= pts.front().frame) return pts.front().value;
    if (frame >= pts.back().frame) return pts.back().value;
    std::size_t hi = 1;
    while (hi < pts.size() && pts[hi].frame <= frame) ++hi;
    const AutoPoint& a = pts[hi - 1];
    const AutoPoint& b = pts[hi];
    const float t = static_cast<float>(frame - a.frame) / static_cast<float>(b.frame - a.frame);
    return a.value + (b.value - a.value) * t;
}
}  // namespace

void RenderGraph::processNode(TrackNode& t, std::int64_t blockStart, int n, bool anySolo) noexcept {
    float* l = t.l.data();
    float* r = t.r.data();
    TrackConfig* cfg = t.config;
    if (cfg) {
        if (t.kind == TrackKind::Audio) renderAudio(t, *cfg, blockStart, n);
        else if (t.kind == TrackKind::Instrument) renderInstrument(t, *cfg, blockStart, n);
        for (const auto& insert : cfg->inserts) insert->process(l, r, n);
    }

    std::copy_n(l, n, preL_.data());  // pre-fader tap for sends
    std::copy_n(r, n, preR_.data());

    const bool muted = t.strip.mute || (anySolo && !t.strip.solo && isSource(t.kind));
    // with automation the lane drives the fader and the pan (the position is the start of this block)
    const float gain = cfg && !cfg->volumeAuto.empty() ? autoValue(cfg->volumeAuto, blockStart) : t.strip.gain;
    const float pan = cfg && !cfg->panAuto.empty() ? autoValue(cfg->panAuto, blockStart) : t.strip.pan;
    const float g = muted ? 0.0f : gain;
    const float targetL = g * (pan > 0.0f ? 1.0f - pan : 1.0f);
    const float targetR = g * (pan < 0.0f ? 1.0f + pan : 1.0f);
    if (!t.smoothInit) {
        t.smoothL = targetL;
        t.smoothR = targetR;
        t.smoothInit = true;
    }
    const float stepL = (targetL - t.smoothL) / static_cast<float>(n);
    const float stepR = (targetR - t.smoothR) / static_cast<float>(n);
    float gl = t.smoothL, gr = t.smoothR;
    for (int i = 0; i < n; ++i) {
        gl += stepL;
        gr += stepR;
        l[i] *= gl;
        r[i] *= gr;
    }
    t.smoothL = targetL;
    t.smoothR = targetR;
    t.blockPeak = 0.0f;
    for (int i = 0; i < n; ++i) t.blockPeak = std::max({t.blockPeak, std::abs(l[i]), std::abs(r[i])});

    if (t.kind == TrackKind::Master || !cfg) return;
    if (!muted) {
        for (SendPlayback& s : cfg->sends) {
            TrackNode* dst = find(s.target);
            if (!dst || dst == &t) continue;
            const float* srcL = s.preFader ? preL_.data() : l;
            const float* srcR = s.preFader ? preR_.data() : r;
            if (s.delay.frames() > 0) {
                s.delay.process(srcL, srcR, dlyL_.data(), dlyR_.data(), n);
                srcL = dlyL_.data();
                srcR = dlyR_.data();
            }
            for (int i = 0; i < n; ++i) {
                dst->l[static_cast<std::size_t>(i)] += srcL[i] * s.gain;
                dst->r[static_cast<std::size_t>(i)] += srcR[i] * s.gain;
            }
        }
    }
    TrackNode* out = cfg->output.isNull() ? master_ : find(cfg->output);
    if (out && out != &t) {
        const float* srcL = l;
        const float* srcR = r;
        if (cfg->outputDelay.frames() > 0) {
            cfg->outputDelay.process(l, r, dlyL_.data(), dlyR_.data(), n);
            srcL = dlyL_.data();
            srcR = dlyR_.data();
        }
        for (int i = 0; i < n; ++i) {
            out->l[static_cast<std::size_t>(i)] += srcL[i];
            out->r[static_cast<std::size_t>(i)] += srcR[i];
        }
    }
}

void RenderGraph::render(std::int64_t blockStart, int frames, float* outL, float* outR) noexcept {
    const int n = std::min(frames, kMaxBlock);
    std::fill_n(outL, frames, 0.0f);
    std::fill_n(outR, frames, 0.0f);
    bool anySolo = false;
    master_ = nullptr;
    for (int i = 0; i < count_; ++i) {
        TrackNode& t = *nodes_[static_cast<std::size_t>(i)];
        std::fill_n(t.l.data(), n, 0.0f);
        std::fill_n(t.r.data(), n, 0.0f);
        if (t.strip.solo && isSource(t.kind)) anySolo = true;
        if (t.kind == TrackKind::Master) master_ = &t;
    }
    for (int i = 0; i < count_; ++i) processNode(*nodes_[static_cast<std::size_t>(i)], blockStart, n, anySolo);

    float peak = 0.0f;
    if (master_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = master_->l[static_cast<std::size_t>(i)];
            outR[i] = master_->r[static_cast<std::size_t>(i)];
            peak = std::max({peak, std::abs(outL[i]), std::abs(outR[i])});
        }
    }
    masterPeak_ = peak;
    for (int i = 0; i < count_; ++i) {
        const TrackNode& t = *nodes_[static_cast<std::size_t>(i)];
        const auto k = static_cast<std::size_t>(i);
        peakHi_[k].store(t.id.hi, std::memory_order_relaxed);
        peakLo_[k].store(t.id.lo, std::memory_order_relaxed);
        peakVal_[k].store(std::max(peakVal_[k].load(std::memory_order_relaxed), t.blockPeak), std::memory_order_relaxed);
    }
    peakCount_.store(count_, std::memory_order_release);
}

void RenderGraph::takeTrackPeaks(std::vector<std::pair<Uuid, float>>& out) noexcept {
    const int n = peakCount_.load(std::memory_order_acquire);
    out.clear();
    for (int i = 0; i < n; ++i) {
        const auto k = static_cast<std::size_t>(i);
        Uuid id;
        id.hi = peakHi_[k].load(std::memory_order_relaxed);
        id.lo = peakLo_[k].load(std::memory_order_relaxed);
        out.emplace_back(id, peakVal_[k].exchange(0.0f, std::memory_order_relaxed));
    }
}

nlohmann::json RenderGraph::describe() const {
    nlohmann::json tracks = nlohmann::json::array();
    for (int i = 0; i < count_; ++i) {
        const TrackNode& t = *nodes_[static_cast<std::size_t>(i)];
        nlohmann::json j = {{"id", t.id.toString()}, {"kind", static_cast<int>(t.kind)}, {"gain", t.strip.gain},
                            {"pan", t.strip.pan},    {"mute", t.strip.mute},              {"solo", t.strip.solo}};
        nlohmann::json regions = nlohmann::json::array(), inserts = nlohmann::json::array(), sends = nlohmann::json::array();
        if (t.config) {
            for (const RegionPlayback& r : t.config->regions) {
                nlohmann::json notes = nlohmann::json::array();
                for (const NoteSpan& s : r.notes) notes.push_back({s.onFrame, s.offFrame, s.note, s.velocity});
                regions.push_back({{"start", r.startFrame}, {"end", r.endFrame}, {"offset", r.sourceOffsetFrames}, {"gain", r.gain},
                                   {"src", reinterpret_cast<std::uintptr_t>(r.source)}, {"notes", notes}});
            }
            for (const auto& p : t.config->inserts) inserts.push_back(p->describe());
            for (const SendPlayback& s : t.config->sends) sends.push_back({{"target", s.target.toString()}, {"gain", s.gain}, {"pre", s.preFader}, {"delay", s.delay.frames()}});
            j["output"] = t.config->output.toString();
            j["outputDelay"] = t.config->outputDelay.frames();
        }
        j["regions"] = regions;
        j["inserts"] = inserts;
        j["sends"] = sends;
        tracks.push_back(j);
    }
    return {{"tracks", tracks}};
}

}  // namespace lpc::audio
