#include "lpc/graph_builder.h"
#include "lpc/effect_specs.h"
#include "lpc/processor_ids.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "lpc/audio/processors.h"
#include "lpc/processor_ids.h"

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

AudioMsg configMsg(const Project& p, const Track& t, MediaStore& media, IPluginHost* plugins, const PdcPlan* pdc) {
    AudioMsg m;
    m.kind = MsgKind::SetConfig;
    m.track = t.id;
    m.obj = makeOwned(buildConfig(p, t, media, plugins, pdc).release());
    return m;
}

AudioMsg reorderMsg(const Project& p) {
    AudioMsg m;
    m.kind = MsgKind::Reorder;
    m.obj = makeOwned(new std::vector<Uuid>(processingOrder(p)));
    return m;
}

int clampLatency(int v) { return std::clamp(v, 0, kMaxPdcFrames); }

int trackLatency(const Project& p, const Track& t, IPluginHost* plugins) {
    if (!plugins || t.freeze) return 0;
    long long sum = 0;
    if (t.instrument && isVst3Id(t.instrument->processorId) && !t.instrument->bypass)
        if (auto live = plugins->acquireInstrument(InsertSlot{t.id, kInstrumentSlot}, *t.instrument, static_cast<double>(p.sampleRate), kMaxBlock))
            sum += clampLatency(live->latencySamples());
    for (std::size_t i = 0; i < t.strip.inserts.size(); ++i) {
        const ProcessorRef& ref = t.strip.inserts[i];
        if (!isVst3Id(ref.processorId)) continue;
        if (auto live = plugins->acquire(InsertSlot{t.id, static_cast<int>(i)}, ref, static_cast<double>(p.sampleRate), kMaxBlock))
            sum += clampLatency(live->latencySamples());
    }
    return clampLatency(static_cast<int>(std::min<long long>(sum, kMaxPdcFrames)));
}

bool stripChanged(const Track& a, const Track& b) {
    return a.strip.gainDb != b.strip.gainDb || a.strip.pan != b.strip.pan || a.strip.mute != b.strip.mute || a.strip.solo != b.strip.solo;
}

bool configChanged(const Track& a, const Track& b) {
    return a.kind != b.kind || a.midi != b.midi || a.delayMs != b.delayMs || a.freeze != b.freeze || a.automation != b.automation || a.automationMode != b.automationMode || a.regions != b.regions || a.instrument != b.instrument || a.strip.inserts != b.strip.inserts ||
           a.strip.sends != b.strip.sends || a.strip.output != b.strip.output;
}

}  // namespace

audio::SynthParams synthParamsOf(const ProcessorRef& ref) {
    audio::SynthParams sp;  // the defaults are the sine instrument: a 2 ms attack, a 5 ms release
    if (ref.processorId != kProcSynth) return sp;
    const EffectSpec* spec = findInstrumentSpec(kProcSynth);
    auto get = [&](const char* name) {
        const EffectParam* p = spec ? spec->find(name) : nullptr;
        if (!p) return 0.0;
        const auto it = ref.params.find(name);
        return std::clamp(it == ref.params.end() ? p->def : it->second, p->min, p->max);
    };
    sp.wave = static_cast<int>(std::lround(get("wave")));
    sp.attackMs = static_cast<float>(get("attack"));
    sp.decayMs = static_cast<float>(get("decay"));
    sp.sustain = static_cast<float>(get("sustain") / 100.0);
    sp.releaseMs = static_cast<float>(get("release"));
    sp.cutoffHz = static_cast<float>(get("cutoff"));
    sp.level = dbToLinear(static_cast<float>(get("level")));
    return sp;
}

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

PdcPlan computePdc(const Project& p, IPluginHost* plugins) {
    PdcPlan plan;
    const Track* master = p.master();
    const std::vector<Uuid> order = processingOrder(p);
    std::unordered_map<Uuid, int> latency, in;
    for (const Track& t : p.tracks) {
        latency[t.id] = trackLatency(p, t, plugins);
        in[t.id] = 0;
    }
    auto forEachEdge = [&](const Track& t, auto&& fn) {  // fn(isOutput, sendIndex, target)
        const Uuid out = t.strip.output.isNull() && master ? master->id : t.strip.output;
        fn(true, -1, out);
        for (std::size_t i = 0; i < t.strip.sends.size(); ++i) fn(false, static_cast<int>(i), t.strip.sends[i].targetTrackId);
    };
    for (const Uuid& id : order) {  // sources before the buses they feed
        const Track* t = p.findTrack(id);
        if (!t || t->kind == TrackKind::Master) continue;
        const int out = in[id] + latency[id];
        forEachEdge(*t, [&](bool, int, const Uuid& to) {
            if (auto it = in.find(to); it != in.end() && to != id) it->second = std::max(it->second, out);
        });
    }
    for (const Track& t : p.tracks) {
        if (t.kind == TrackKind::Master) continue;
        EdgeDelays e;
        e.sends.assign(t.strip.sends.size(), 0);
        const int out = in[t.id] + latency[t.id];
        forEachEdge(t, [&](bool isOutput, int sendIndex, const Uuid& to) {
            const auto it = in.find(to);
            if (it == in.end() || to == t.id) return;
            const int delay = std::clamp(it->second - out, 0, kMaxPdcFrames);
            if (isOutput) e.output = delay;
            else e.sends[static_cast<std::size_t>(sendIndex)] = delay;
        });
        plan.edges[t.id] = std::move(e);
    }
    if (master) plan.totalLatency = std::clamp(in[master->id] + latency[master->id], 0, kMaxPdcFrames);
    return plan;
}

// The automation lanes of the sends of a track: cfg->sends is in the order of t.strip.sends.
void fillSendAutomation(const Project& p, const Track& t, TrackConfig& cfg) {
    if (t.automationMode == "off") return;
    for (const AutomationLane& lane : t.automation) {
        if (lane.target.rfind("send:", 0) != 0 || lane.points.empty()) continue;
        const auto id = Uuid::parse(lane.target.substr(5));
        if (!id) continue;
        for (std::size_t i = 0; i < t.strip.sends.size() && i < cfg.sends.size(); ++i) {
            if (t.strip.sends[i].id != *id) continue;
            for (const AutomationPoint& pt : lane.points)
                cfg.sends[i].levelAuto.push_back(AutoPoint{toFrames(p.tempoMap.ticksToSamples(pt.tick, p.sampleRate)), dbToLinear(static_cast<float>(pt.value))});
        }
    }
}

std::unique_ptr<TrackConfig> buildConfig(const Project& p, const Track& t, MediaStore& media, IPluginHost* plugins, const PdcPlan* pdc) {
    auto cfg = std::make_unique<TrackConfig>();
    if (t.freeze) {  // the rendered audio replaces the instrument, the regions and the inserts; the strip, the sends and the output stay live
        cfg->frozen = true;
        if (const MediaItem* item = p.findMedia(t.freeze->mediaId))
            if (std::shared_ptr<IFrameSource> src = media.open(*item)) {
                RegionPlayback rp;
                rp.startFrame = t.freeze->startFrame;
                rp.endFrame = t.freeze->startFrame + item->frames;
                rp.source = src.get();
                cfg->regions.push_back(rp);
                cfg->keepAlive.push_back(std::move(src));
            }
        for (const AutomationLane& lane : t.automation) {
            if (t.automationMode == "off") break;
            std::vector<AutoPoint>& out = lane.target == "volume" ? cfg->volumeAuto : cfg->panAuto;
            if (lane.target != "volume" && lane.target != "pan") continue;
            for (const AutomationPoint& pt : lane.points)
                out.push_back(AutoPoint{toFrames(p.tempoMap.ticksToSamples(pt.tick, p.sampleRate)),
                                        lane.target == "volume" ? dbToLinear(static_cast<float>(pt.value)) : static_cast<float>(pt.value)});
        }
        for (const Send& s : t.strip.sends) cfg->sends.push_back(SendPlayback{s.targetTrackId, dbToLinear(s.levelDb), s.preFader, {}, {}});
        fillSendAutomation(p, t, *cfg);
        cfg->output = t.strip.output;
        if (pdc) {
            if (const auto it = pdc->edges.find(t.id); it != pdc->edges.end()) {
                cfg->outputDelay = DelayLine(it->second.output);
                for (std::size_t i = 0; i < cfg->sends.size() && i < it->second.sends.size(); ++i) cfg->sends[i].delay = DelayLine(it->second.sends[i]);
            }
        }
        return cfg;
    }
    const std::int64_t delay = static_cast<std::int64_t>(std::llround(t.delayMs * static_cast<double>(p.sampleRate) / 1000.0));  // Track Delay, in frames
    for (const Region& r : t.regions) {
        if (r.muted) continue;  // Mute Regions: it stays in the project but does not play
        RegionPlayback rp;
        rp.startFrame = regionFrame(p, r, r.start) + delay;
        rp.endFrame = regionFrame(p, r, r.start + r.length) + delay;
        rp.gain = dbToLinear(r.gainDb);
        if (r.fadeIn > 0 || r.fadeOut > 0) {  // the fades in frames, together at most as long as the region
            const std::int64_t length = rp.endFrame - rp.startFrame;
            rp.fadeInFrames = r.fadeIn > 0 ? regionFrame(p, r, r.start + r.fadeIn) - regionFrame(p, r, r.start) : 0;  // the track delay moves the region, not its fades
            rp.fadeOutFrames = r.fadeOut > 0 ? regionFrame(p, r, r.start + r.length) - regionFrame(p, r, r.start + r.length - r.fadeOut) : 0;
            if (rp.fadeInFrames + rp.fadeOutFrames > length && length > 0) {
                const double k = static_cast<double>(length) / static_cast<double>(rp.fadeInFrames + rp.fadeOutFrames);
                rp.fadeInFrames = static_cast<std::int64_t>(static_cast<double>(rp.fadeInFrames) * k);
                rp.fadeOutFrames = length - rp.fadeInFrames;
            }
        }
        if (t.kind == TrackKind::Audio) {
            const MediaItem* item = p.findMedia(r.mediaId);
            std::shared_ptr<IFrameSource> src = item ? media.open(*item) : nullptr;
            if (!src) continue;  // missing media: the region stays silent, MediaStore keeps the warning
            rp.source = src.get();
            rp.sourceOffsetFrames = r.sourceOffsetFrames;
            if (rp.startFrame < 0) {  // an earlier track delay than the start of the project: the first part of the audio is dropped
                rp.sourceOffsetFrames += -rp.startFrame;
                rp.startFrame = 0;
            }
            if (r.loopLength > 0) rp.loopFrames = std::max<std::int64_t>(1, regionFrame(p, r, r.start + r.loopLength) - regionFrame(p, r, r.start));
            cfg->keepAlive.push_back(std::move(src));
        } else {
            const std::int64_t loop = r.loopLength > 0 ? r.loopLength : r.length;  // one pass is the whole region when it does not loop
            const std::int64_t passes = r.loopLength > 0 ? std::min<std::int64_t>((r.length + loop - 1) / loop, 2048) : 1;
            for (std::int64_t pass = 0; pass < passes; ++pass) {
                const std::int64_t shift = pass * loop;
                for (const MidiNote& n : r.notes) {
                    if (n.muted) continue;
                    if (r.loopLength > 0 && (n.start >= loop || shift + n.start >= r.length)) continue;  // outside the loop or the region
                    const int pitch = n.note + r.transpose + t.midi.transpose;
                    if (pitch < std::max(0, t.midi.keyLow) || pitch > std::min(127, t.midi.keyHigh)) continue;  // Key Limit: not played
                    const int velocity = std::clamp(std::clamp(n.velocity + r.velocityOffset + t.midi.velocity, 1, 127), t.midi.velocityLow, t.midi.velocityHigh);
                    std::int64_t start = n.start;
                    if (r.quantize > 0) start = (start + r.quantize / 2) / r.quantize * r.quantize;  // Quantize: the start moves to the grid, the end stays
                    const std::int64_t end = r.loopLength > 0 ? std::min({n.start + n.length, loop, r.length - shift}) : n.start + n.length;
                    const std::int64_t on = toFrames(p.tempoMap.ticksToSamples(r.start + shift + start, p.sampleRate)) + delay;
                    const std::int64_t off = toFrames(p.tempoMap.ticksToSamples(r.start + shift + std::max(end, start + 1), p.sampleRate)) + delay;
                    if (off < 0) continue;
                    rp.notes.push_back(NoteSpan{on, std::max(on, off), static_cast<std::uint8_t>(pitch), static_cast<std::uint8_t>(velocity)});
                }
            }
            std::stable_sort(rp.notes.begin(), rp.notes.end(), [](const NoteSpan& a, const NoteSpan& b) { return a.onFrame < b.onFrame; });
            for (std::int64_t pass = 0; pass < passes; ++pass)
                for (const MidiControl& c : r.controls) {
                    if (r.loopLength > 0 && (c.tick >= loop || pass * loop + c.tick > r.length)) continue;
                    const std::int64_t at = toFrames(p.tempoMap.ticksToSamples(r.start + pass * loop + c.tick, p.sampleRate)) + delay;
                    if (at >= 0) rp.controls.push_back(ControlSpan{at, c.status, c.data1, c.data2});
                }
            std::stable_sort(rp.controls.begin(), rp.controls.end(), [](const ControlSpan& a, const ControlSpan& b) { return a.frame < b.frame; });
        }
        cfg->regions.push_back(std::move(rp));
    }
    if (t.instrument) cfg->synthParams = synthParamsOf(*t.instrument);
    if (t.instrument && isVst3Id(t.instrument->processorId)) {  // the notes go to a plug-in; silence while it is missing, loading or bypassed
        std::shared_ptr<IInstrument> live;
        if (plugins && !t.instrument->bypass)
            live = plugins->acquireInstrument(InsertSlot{t.id, kInstrumentSlot}, *t.instrument, static_cast<double>(p.sampleRate), kMaxBlock);
        cfg->instrument = live ? live : std::make_shared<SilentInstrument>(true);
    }
    for (const AutomationLane& lane : t.automation) {
        if (t.automationMode == "off") break;  // the fader and the pan stay where the strip has them
        std::vector<AutoPoint>& out = lane.target == "volume" ? cfg->volumeAuto : cfg->panAuto;
        if (lane.target != "volume" && lane.target != "pan") continue;
        for (const AutomationPoint& pt : lane.points)
            out.push_back(AutoPoint{toFrames(p.tempoMap.ticksToSamples(pt.tick, p.sampleRate)),
                                    lane.target == "volume" ? dbToLinear(static_cast<float>(pt.value)) : static_cast<float>(pt.value)});
    }
    int slotIndex = 0;
    std::vector<int> builtAt(t.strip.inserts.size(), -1);  // where each insert of the model ended up in the config
    for (const ProcessorRef& ref : t.strip.inserts) {
        if (auto effect = makeInsert(ref, plugins, InsertSlot{t.id, slotIndex}, static_cast<double>(p.sampleRate), kMaxBlock)) {
            builtAt[static_cast<std::size_t>(slotIndex)] = static_cast<int>(cfg->inserts.size());
            cfg->inserts.push_back(std::move(effect));
        }
        ++slotIndex;
    }
    if (t.automationMode != "off")  // the lanes that drive a parameter of a plug-in insert
        for (const AutomationLane& lane : t.automation) {
            const auto target = parseParamTarget(lane.target);
            if (!target || lane.points.empty()) continue;
            int seen = 0, built = -1;
            for (std::size_t i = 0; i < t.strip.inserts.size(); ++i)
                if (t.strip.inserts[i].processorId == target->processorId && seen++ == target->ordinal) built = builtAt[i];
            if (built < 0) continue;
            ParamAuto pa;
            pa.insert = built;
            pa.param = target->index;
            for (const AutomationPoint& pt : lane.points)
                pa.points.push_back(AutoPoint{toFrames(p.tempoMap.ticksToSamples(pt.tick, p.sampleRate)), std::clamp(static_cast<float>(pt.value), 0.0f, 1.0f)});
            cfg->paramAuto.push_back(std::move(pa));
        }
    for (const Send& s : t.strip.sends) cfg->sends.push_back(SendPlayback{s.targetTrackId, dbToLinear(s.levelDb), s.preFader, {}, {}});
    fillSendAutomation(p, t, *cfg);
    cfg->output = t.strip.output;
    if (pdc) {
        if (const auto it = pdc->edges.find(t.id); it != pdc->edges.end()) {
            cfg->outputDelay = DelayLine(it->second.output);
            for (std::size_t i = 0; i < cfg->sends.size() && i < it->second.sends.size(); ++i)
                cfg->sends[i].delay = DelayLine(it->second.sends[i]);
        }
    }
    return cfg;
}

std::unique_ptr<TrackNode> buildNode(const Project& p, const Track& t, MediaStore& media, IPluginHost* plugins, const PdcPlan* pdc) {
    return std::make_unique<TrackNode>(t.id, t.kind, stripParamsOf(t.strip), buildConfig(p, t, media, plugins, pdc).release(),
                                       static_cast<double>(p.sampleRate));
}

std::vector<AudioMsg> initialMessages(const Project& p, MediaStore& media, IPluginHost* plugins, PdcPlan* planOut) {
    const PdcPlan pdc = computePdc(p, plugins);
    if (planOut) *planOut = pdc;
    std::vector<AudioMsg> out;
    for (const Track& t : p.tracks) out.push_back(addMsg(buildNode(p, t, media, plugins, &pdc)));
    out.push_back(reorderMsg(p));
    return out;
}

std::vector<AudioMsg> refreshMessages(const Project& p, MediaStore& media, IPluginHost* plugins, PdcPlan* planOut) {
    const PdcPlan pdc = computePdc(p, plugins);
    if (planOut) *planOut = pdc;
    std::vector<AudioMsg> out;
    for (const Track& t : p.tracks) out.push_back(configMsg(p, t, media, plugins, &pdc));
    return out;
}

std::vector<AudioMsg> diffToMessages(const Project& before, const Project& after, MediaStore& media, IPluginHost* plugins, PdcPlan* plan) {
    std::vector<AudioMsg> out;
    const PdcPlan planBefore = plan ? *plan : computePdc(before, nullptr);
    const PdcPlan planAfter = computePdc(after, plugins);
    bool structural = false;
    const bool timingChanged = before.tempoMap != after.tempoMap || before.sampleRate != after.sampleRate;

    for (const Track& a : after.tracks) {
        if (!before.findTrack(a.id)) {
            out.push_back(addMsg(buildNode(after, a, media, plugins, &planAfter)));
            structural = true;
        }
    }
    for (const Track& a : after.tracks) {
        const Track* b = before.findTrack(a.id);
        if (!b) continue;
        if (stripChanged(*b, a)) out.push_back(stripMsg(a));
        const auto eb = planBefore.edges.find(a.id);
        const auto ea = planAfter.edges.find(a.id);
        const bool delaysChanged = (eb == planBefore.edges.end()) != (ea == planAfter.edges.end()) ||
                                   (eb != planBefore.edges.end() && !(eb->second == ea->second));
        if (timingChanged || delaysChanged || configChanged(*b, a)) out.push_back(configMsg(after, a, media, plugins, &planAfter));
    }
    for (const Track& b : before.tracks) {
        if (!after.findTrack(b.id)) {
            out.push_back(removeMsg(b.id));
            structural = true;
        }
    }
    if (structural || processingOrder(before) != processingOrder(after)) out.push_back(reorderMsg(after));
    if (plan) *plan = planAfter;
    return out;
}

namespace {
// "3+2+2" -> {3, 2, 2}; empty when it does not add up to the bar
std::vector<int> parseGrouping(const std::string& text, int numerator) {
    std::vector<int> groups;
    int sum = 0, value = 0;
    bool have = false;
    for (const char c : text + "+") {
        if (c >= '0' && c <= '9') { value = value * 10 + (c - '0'); have = true; }
        else if (c == '+' || c == ',' || c == ' ') {
            if (have && value > 0) { groups.push_back(value); sum += value; }
            value = 0;
            have = false;
        }
    }
    if (sum != numerator) groups.clear();
    return groups;
}
}  // namespace

audio::ClickTrack buildClickTrack(const Project& p, const audio::ClickSettings& settings, std::size_t maxClicks) {
    audio::ClickTrack click;
    const auto& sigs = p.tempoMap.signatures();
    const std::string& mode = settings.mode;
    Ticks tick = 0;
    auto add = [&](Ticks at, int slot, bool accent) {
        click.frames.push_back(static_cast<std::int64_t>(std::llround(p.tempoMap.ticksToSamples(at, p.sampleRate))));
        click.accent.push_back(accent ? 1 : 0);
        click.slot.push_back(static_cast<std::uint8_t>(slot));
    };
    while (click.frames.size() < maxClicks) {
        const TempoMap::SigEvent* sig = nullptr;
        for (const auto& s : sigs)
            if (s.tick <= tick) sig = &s;
        const int numerator = sig ? sig->numerator : 4, denominator = sig ? sig->denominator : 4;
        const Ticks beatTicks = kPPQ * 4 / denominator;
        std::vector<int> groups;
        if (mode == "grouped") {
            groups = parseGrouping(settings.grouping, numerator);
            if (groups.empty() && denominator >= 8 && numerator >= 6 && numerator % 3 == 0) groups.assign(static_cast<std::size_t>(numerator / 3), 3);
        }
        if (!groups.empty()) {  // 1 la li 2 la li (a group of two: 1 &)
            int beat = 0, number = 1;
            for (const int size : groups) {
                for (int k = 0; k < size; ++k, ++beat) {
                    const int slot = k == 0 ? std::min(number, 32) : (size == 2 ? audio::kSlotAnd : (k == 1 ? audio::kSlotLa : audio::kSlotLi));
                    add(tick + beat * beatTicks, slot, beat == 0);
                }
                ++number;
            }
        } else {
            const int parts = mode == "sixteenths" ? 4 : (mode == "eighths" ? 2 : 1);
            for (int beat = 0; beat < numerator; ++beat)
                for (int part = 0; part < parts; ++part) {
                    int slot = std::min(beat + 1, 32);
                    if (part > 0) slot = parts == 2 ? audio::kSlotAnd : (part == 1 ? audio::kSlotE : (part == 2 ? audio::kSlotAnd : audio::kSlotA));
                    add(tick + beat * beatTicks + part * (beatTicks / parts), slot, beat == 0 && part == 0);
                }
        }
        tick += beatTicks * numerator;
    }
    return click;
}

}  // namespace lpc
