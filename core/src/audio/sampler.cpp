#include "lpc/audio/sampler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "lpc/audio/frame_source.h"
#include "lpc/effect_specs.h"
#include "lpc/sampler.h"

namespace lpc::audio {
namespace {

struct Sample {
    std::vector<float> l, r;
    double rate = 48000.0;
};

struct Zone {
    std::shared_ptr<const Sample> sample;
    int root = 60, lo = 0, hi = 127;
    float gain = 1.0f;
    std::int64_t start = 0, end = 0;  // frames of the sample, end > start
    bool oneShot = false, loop = false;
};

struct Voice {
    bool active = false, releasing = false;
    const Zone* zone = nullptr;
    int note = 0;
    double pos = 0.0, step = 1.0;  // frames of the sample
    float velocity = 1.0f, env = 0.0f;
    std::uint64_t age = 0;
};

class SamplerInstrument final : public IInstrument {
public:
    SamplerInstrument(std::vector<Zone> zones, double sr, float level, float attackMs, float releaseMs)
        : zones_(std::move(zones)), sr_(sr), level_(level),
          attackStep_(attackMs <= 0.0f ? 1.0f : static_cast<float>(1000.0 / (attackMs * sr))),
          releaseStep_(static_cast<float>(1000.0 / (std::max(1.0f, releaseMs) * sr))) {}

    void render(float* l, float* r, int frames, const MidiEvent* events, int count) noexcept override {
        std::fill_n(l, frames, 0.0f);
        std::fill_n(r, frames, 0.0f);
        int at = 0;
        for (int e = 0; e <= count; ++e) {
            const int until = e < count ? std::clamp(events[e].offset, 0, frames) : frames;
            if (until > at) mix(l + at, r + at, until - at);
            at = std::max(at, until);
            if (e < count) handle(events[e]);
        }
    }
    nlohmann::json describe() const override { return {{"sampler", true}, {"zones", static_cast<int>(zones_.size())}}; }

private:
    void handle(const MidiEvent& e) noexcept {
        const unsigned type = e.status & 0xF0u;
        if (type == 0x90 && e.data2 > 0) noteOn(e.data1 & 0x7F, e.data2);
        else if (type == 0x80 || type == 0x90) noteOff(e.data1 & 0x7F);
        else if (type == 0xB0 && (e.data1 == 123 || e.data1 == 120)) {
            for (Voice& v : voices_)
                if (v.active) v.releasing = true;
        }
    }
    void noteOn(int note, int velocity) noexcept {
        for (const Zone& z : zones_) {
            if (note < z.lo || note > z.hi) continue;
            Voice* slot = nullptr;
            for (Voice& v : voices_)
                if (!v.active) { slot = &v; break; }
            if (!slot) slot = &*std::min_element(voices_.begin(), voices_.end(), [](const Voice& a, const Voice& b) { return a.age < b.age; });
            *slot = Voice{};
            slot->active = true;
            slot->zone = &z;
            slot->note = note;
            slot->pos = static_cast<double>(z.start);
            slot->step = std::pow(2.0, (note - z.root) / 12.0) * z.sample->rate / sr_;
            slot->velocity = static_cast<float>(velocity) / 127.0f;
            slot->env = attackStep_ >= 1.0f ? 1.0f : 0.0f;
            slot->age = ++counter_;
            return;  // the first zone that answers plays
        }
    }
    void noteOff(int note) noexcept {
        for (Voice& v : voices_)
            if (v.active && !v.releasing && v.note == note && !v.zone->oneShot) v.releasing = true;
    }
    void mix(float* l, float* r, int n) noexcept {
        for (Voice& v : voices_) {
            if (!v.active) continue;
            const Zone& z = *v.zone;
            const Sample& s = *z.sample;
            const double length = static_cast<double>(z.end - z.start);
            const float g = z.gain * v.velocity * level_;
            for (int i = 0; i < n; ++i) {
                if (v.pos >= static_cast<double>(z.end)) {
                    if (z.loop && !v.releasing && length > 1.0) v.pos -= length;
                    else { v.active = false; break; }
                }
                const std::int64_t a = static_cast<std::int64_t>(v.pos);
                const std::int64_t b = std::min<std::int64_t>(a + 1, z.end - 1);
                const float f = static_cast<float>(v.pos - static_cast<double>(a));
                const float sl = s.l[static_cast<std::size_t>(a)] + (s.l[static_cast<std::size_t>(b)] - s.l[static_cast<std::size_t>(a)]) * f;
                const float sr = s.r[static_cast<std::size_t>(a)] + (s.r[static_cast<std::size_t>(b)] - s.r[static_cast<std::size_t>(a)]) * f;
                if (v.releasing) {
                    v.env -= releaseStep_;
                    if (v.env <= 0.0f) { v.active = false; break; }
                } else if (v.env < 1.0f) {
                    v.env = std::min(1.0f, v.env + attackStep_);
                }
                l[i] += sl * g * v.env;
                r[i] += sr * g * v.env;
                v.pos += v.step;
            }
        }
    }

    std::vector<Zone> zones_;
    double sr_;
    float level_, attackStep_, releaseStep_;
    std::array<Voice, 32> voices_{};
    std::uint64_t counter_ = 0;
};

}  // namespace

std::shared_ptr<IInstrument> makeSamplerInstrument(const ProcessorRef& instrument, const Project& project, MediaStore& media, double sampleRate) {
    const EffectSpec* spec = findInstrumentSpec(instrument.processorId);
    auto get = [&](const char* name) {
        const EffectParam* p = spec ? spec->find(name) : nullptr;
        if (!p) return 0.0;
        const auto it = instrument.params.find(name);
        return std::clamp(it == instrument.params.end() ? p->def : it->second, p->min, p->max);
    };
    std::map<std::string, std::shared_ptr<const Sample>> loaded;  // a file shared by several zones is read once
    std::vector<Zone> zones;
    constexpr std::int64_t kMaxFrames = 48000LL * 60 * 10;  // ten minutes of a zone are more than a sampler holds
    for (const SamplerZone& sz : samplerZonesOf(instrument)) {
        const std::string key = sz.mediaId.toString();
        auto it = loaded.find(key);
        if (it == loaded.end()) {
            std::shared_ptr<const Sample> sample;
            if (const MediaItem* item = project.findMedia(sz.mediaId)) {
                if (std::shared_ptr<IFrameSource> src = media.open(*item)) {
                    const std::int64_t total = std::min(src->frames(), kMaxFrames);
                    auto s = std::make_shared<Sample>();
                    s->rate = src->sampleRate() > 0 ? src->sampleRate() : sampleRate;
                    s->l.resize(static_cast<std::size_t>(total));
                    s->r.resize(static_cast<std::size_t>(total));
                    for (std::int64_t at = 0; at < total; at += 4096) {
                        const int n = static_cast<int>(std::min<std::int64_t>(4096, total - at));
                        src->read(at, s->l.data() + at, s->r.data() + at, n);
                    }
                    sample = std::move(s);
                }
            }
            it = loaded.emplace(key, std::move(sample)).first;
        }
        if (!it->second || it->second->l.empty()) continue;
        Zone z;
        z.sample = it->second;
        z.root = sz.root;
        z.lo = sz.lo;
        z.hi = sz.hi;
        z.gain = static_cast<float>(std::pow(10.0, sz.gainDb / 20.0));
        const std::int64_t length = static_cast<std::int64_t>(z.sample->l.size());
        z.start = std::clamp<std::int64_t>(sz.start, 0, length - 1);
        z.end = sz.end > 0 ? std::clamp<std::int64_t>(sz.end, z.start + 1, length) : length;
        z.oneShot = sz.oneShot;
        z.loop = sz.loop;
        zones.push_back(std::move(z));
    }
    if (zones.empty()) return std::make_shared<SilentInstrument>(false);
    return std::make_shared<SamplerInstrument>(std::move(zones), sampleRate, static_cast<float>(std::pow(10.0, get("level") / 20.0)), static_cast<float>(get("attack")), static_cast<float>(get("release")));
}

}  // namespace lpc::audio
