#pragma once
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "lpc/plugin_host.h"

namespace lpc::test {

struct FakeSpec {
    int latency = 0;
    float gain = 1.0f;
};

// A plug-in that delays by `latency` frames and multiplies by `gain`.
class FakeProcessor final : public audio::IProcessor {
public:
    FakeProcessor(FakeSpec spec, std::string state)
        : spec_(spec), state_(std::move(state)), l_(static_cast<std::size_t>(spec.latency), 0.0f), r_(l_) {}
    void process(float* l, float* r, int frames) noexcept override {
        for (int i = 0; i < frames; ++i) {
            if (spec_.latency > 0) {
                const std::size_t p = pos_;
                const float outL = l_[p], outR = r_[p];
                l_[p] = l[i];
                r_[p] = r[i];
                pos_ = (pos_ + 1) % l_.size();
                l[i] = outL * spec_.gain;
                r[i] = outR * spec_.gain;
            } else {
                l[i] *= spec_.gain;
                r[i] *= spec_.gain;
            }
        }
    }
    int latencySamples() const override { return spec_.latency; }
    nlohmann::json describe() const override { return {{"fake", true}, {"latency", spec_.latency}, {"state", state_}}; }
    const std::string& state() const { return state_; }

private:
    FakeSpec spec_;
    std::string state_;
    std::vector<float> l_, r_;
    std::size_t pos_ = 0;
};

// An instrument that remembers every event it is given (with the block's running frame count) and sounds a constant tone while a note is held.
class FakeInstrument final : public audio::IInstrument {
public:
    struct Seen {
        long long frame;
        audio::MidiEvent event;
    };
    explicit FakeInstrument(int latency = 0) : latency_(latency) {}
    int latencySamples() const override { return latency_; }
    void render(float* l, float* r, int frames, const audio::MidiEvent* events, int count) noexcept override {
        int next = 0;
        for (int i = 0; i < frames; ++i) {
            while (next < count && events[next].offset == i) {
                seen.push_back({done_ + i, events[next]});
                if ((events[next].status & 0xf0) == 0x90 && events[next].data2 > 0) ++held_;
                if ((events[next].status & 0xf0) == 0x80 || ((events[next].status & 0xf0) == 0x90 && events[next].data2 == 0)) held_ = held_ > 0 ? held_ - 1 : 0;
                ++next;
            }
            l[i] = r[i] = held_ > 0 ? 0.5f : 0.0f;
        }
        done_ += frames;
    }
    nlohmann::json describe() const override { return {{"fakeInstrument", true}}; }
    std::vector<Seen> seen;

private:
    int latency_;
    long long done_ = 0;
    int held_ = 0;
};

class FakePluginHost final : public IPluginHost {
public:
    std::map<std::string, int> instruments;  // instrument ids this host can load -> their latency
    int createdInstruments = 0;
    std::shared_ptr<FakeInstrument> lastInstrument;

    std::shared_ptr<audio::IInstrument> acquireInstrument(const InsertSlot& slot, const ProcessorRef& ref, double, int) override {
        std::lock_guard lock(mutex_);
        const auto known = instruments.find(ref.processorId);
        if (known == instruments.end()) return nullptr;
        const std::string key = keyOf(slot);
        if (auto it = liveInstruments_.find(key); it != liveInstruments_.end() && it->second.first == ref.processorId + "|" + ref.state) return it->second.second;
        ++createdInstruments;
        auto inst = std::make_shared<FakeInstrument>(known->second);
        liveInstruments_[key] = {ref.processorId + "|" + ref.state, inst};
        lastInstrument = inst;
        return inst;
    }

    std::map<std::string, FakeSpec> known;  // ids this host can load; any other id is "missing"
    bool deferLoads = false;                // true: acquire returns nullptr until finishLoads()
    int created = 0;

    std::vector<PluginDescriptor> catalogue() const override {
        std::vector<PluginDescriptor> out;
        for (const auto& [id, spec] : known) out.push_back(PluginDescriptor{id, "Fake " + id, "Fake", "1", "", ""});
        return out;
    }

    std::shared_ptr<audio::IProcessor> acquire(const InsertSlot& slot, const ProcessorRef& ref, double, int) override {
        std::lock_guard lock(mutex_);
        if (!known.count(ref.processorId)) return nullptr;
        const std::string key = keyOf(slot);
        if (auto it = live_.find(key); it != live_.end() && it->second.id == ref.processorId && it->second.state == ref.state)
            return it->second.proc;
        if (deferLoads) {
            pending_.push_back({slot, ref});
            return nullptr;
        }
        return create(slot, ref);
    }

    void prune(const std::vector<std::pair<InsertSlot, ProcessorRef>>& live) override {
        std::lock_guard lock(mutex_);
        for (auto it = live_.begin(); it != live_.end();) {
            bool keep = false;
            for (const auto& [slot, ref] : live)
                if (keyOf(slot) == it->first && ref.processorId == it->second.id) keep = true;
            it = keep ? std::next(it) : live_.erase(it);
        }
    }

    std::optional<std::string> captureState(const InsertSlot& slot) override {
        std::lock_guard lock(mutex_);
        auto it = live_.find(keyOf(slot));
        if (it == live_.end()) return std::nullopt;
        it->second.state = nextCaptured;
        return nextCaptured;
    }
    std::string nextCaptured = "CAPT";

    void setReadyListener(std::function<void(const InsertSlot&)> l) override {
        std::lock_guard lock(mutex_);
        listener_ = std::move(l);
    }

    // Creates every pending instance and tells the listener (as the real host does after a background load).
    void finishLoads() {
        std::vector<InsertSlot> ready;
        std::function<void(const InsertSlot&)> listener;
        {
            std::lock_guard lock(mutex_);
            for (auto& [slot, ref] : pending_) {
                create(slot, ref);
                ready.push_back(slot);
            }
            pending_.clear();
            listener = listener_;
        }
        if (listener)
            for (const InsertSlot& s : ready) listener(s);
    }

    std::size_t liveCount() const {
        std::lock_guard lock(mutex_);
        return live_.size();
    }

private:
    struct Live {
        std::string id, state;
        std::shared_ptr<audio::IProcessor> proc;
    };
    static std::string keyOf(const InsertSlot& s) { return s.track.toString() + "/" + std::to_string(s.index); }
    std::shared_ptr<audio::IProcessor> create(const InsertSlot& slot, const ProcessorRef& ref) {
        ++created;
        auto proc = std::make_shared<FakeProcessor>(known.at(ref.processorId), ref.state);
        live_[keyOf(slot)] = Live{ref.processorId, ref.state, proc};
        return proc;
    }

    mutable std::mutex mutex_;
    std::map<std::string, std::pair<std::string, std::shared_ptr<FakeInstrument>>> liveInstruments_;
    std::map<std::string, Live> live_;
    std::vector<std::pair<InsertSlot, ProcessorRef>> pending_;
    std::function<void(const InsertSlot&)> listener_;
};

}  // namespace lpc::test
