#include "lpc/offline_render.h"
#include "lpc/processor_ids.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "lpc/audio/engine.h"
#include "lpc/graph_builder.h"

namespace lpc {

std::int64_t projectEndFrame(const Project& p) {
    std::int64_t end = 0;
    for (const Track& t : p.tracks) {
        for (const Region& r : t.regions) {
            const std::int64_t e = r.timeBase == TimeBase::Musical
                                       ? static_cast<std::int64_t>(std::llround(p.tempoMap.ticksToSamples(r.start + r.length, p.sampleRate)))
                                       : static_cast<std::int64_t>(std::llround(static_cast<double>(r.start + r.length) * p.sampleRate / 1e6));
            end = std::max(end, e);
        }
    }
    return end;
}

namespace {

// The plug-in host as an offline render sees it: the same instances, but no MIDI ports (a bounce must not play a hardware synth).
class NoMidiHost final : public IPluginHost {
public:
    explicit NoMidiHost(IPluginHost& inner) : inner_(inner) {}
    std::vector<PluginDescriptor> catalogue() const override { return inner_.catalogue(); }
    std::shared_ptr<audio::IProcessor> acquire(const InsertSlot& s, const ProcessorRef& r, double sr, int mb) override { return inner_.acquire(s, r, sr, mb); }
    void setWanted(const std::vector<std::pair<InsertSlot, ProcessorRef>>& live) override { inner_.setWanted(live); }
    void prune(const std::vector<std::pair<InsertSlot, ProcessorRef>>& live) override { inner_.prune(live); }
    std::optional<std::string> captureState(const InsertSlot& s) override { return inner_.captureState(s); }
    void setReadyListener(std::function<void(const InsertSlot&)> l) override { inner_.setReadyListener(std::move(l)); }
    std::shared_ptr<audio::IInstrument> acquireInstrument(const InsertSlot& s, const ProcessorRef& r, double sr, int mb) override { return inner_.acquireInstrument(s, r, sr, mb); }

private:
    IPluginHost& inner_;
};

}  // namespace

RenderResult renderOffline(const Project& p, MediaStore& media, const RenderOptions& optionsIn) {
    RenderOptions options = optionsIn;
    std::unique_ptr<NoMidiHost> quiet;
    if (options.plugins && !options.externalMidi) {
        quiet = std::make_unique<NoMidiHost>(*options.plugins);
        options.plugins = quiet.get();
    }
    RenderResult out;
    out.sampleRate = p.sampleRate;
    const std::int64_t start = std::max<std::int64_t>(options.startFrame, 0);
    std::int64_t total = options.frames;
    if (total < 0) {
        total = std::max<std::int64_t>(0, projectEndFrame(p) - start) +
                static_cast<std::int64_t>(std::llround(std::max(0.0, options.tailSeconds) * p.sampleRate));
    }
    constexpr std::int64_t kMaxRenderSeconds = 4 * 3600;  // the whole result is held in memory
    if (total > kMaxRenderSeconds * p.sampleRate)
        throw std::runtime_error("project too long to render (limit is 4 hours; use --seconds to render a part)");
    out.frames = total;
    out.interleaved.assign(static_cast<std::size_t>(total) * 2, 0.0f);

    audio::AudioEngine engine(static_cast<double>(p.sampleRate));
    // a stereo bounce has no speakers: an object panner that feeds speakers is heard as the stereo downmix instead
    Project stereo = p;
    for (Track& t : stereo.tracks)
        for (ProcessorRef& ref : t.strip.inserts)
            if (ref.processorId == kProcObjectPanner) {
                const auto it = ref.params.find("mode");
                if (it != ref.params.end() && it->second >= 4.0) it->second = 3.0;
            }
    for (const audio::AudioMsg& m : initialMessages(stereo, media, options.plugins)) engine.applyDirect(m);
    const std::int64_t skip = computePdc(p, options.plugins).totalLatency;  // frames the plug-ins delay everything by
    audio::AudioMsg locate;
    locate.kind = audio::MsgKind::Locate;
    locate.frame = start;
    engine.applyDirect(locate);
    audio::AudioMsg play;
    play.kind = audio::MsgKind::Play;
    engine.applyDirect(play);

    const int block = std::clamp(options.blockSize, 1, 65536);
    std::vector<float> l(static_cast<std::size_t>(block)), r(static_cast<std::size_t>(block));
    const int pairs = std::clamp(options.outputPair, 0, kMaxOutputPairs);
    std::vector<std::vector<float>> extraBuffers(static_cast<std::size_t>(2 * pairs), std::vector<float>(static_cast<std::size_t>(block)));
    std::vector<float*> extra;
    for (auto& b : extraBuffers) extra.push_back(b.data());
    const float* leftOut = pairs > 0 ? extra[static_cast<std::size_t>(2 * (pairs - 1))] : l.data();
    const float* rightOut = pairs > 0 ? extra[static_cast<std::size_t>(2 * (pairs - 1) + 1)] : r.data();
    for (std::int64_t pos = 0; pos < total + skip; pos += block) {
        const int n = static_cast<int>(std::min<std::int64_t>(block, total + skip - pos));
        engine.processBlock(l.data(), r.data(), n, extra.empty() ? nullptr : extra.data(), pairs);
        for (int i = 0; i < n; ++i) {
            const std::int64_t at = pos + i - skip;
            if (at < 0) continue;
            out.interleaved[static_cast<std::size_t>(at * 2)] = leftOut[i];
            out.interleaved[static_cast<std::size_t>(at * 2 + 1)] = rightOut[i];
        }
    }
    engine.collectGarbage();
    return out;
}

}  // namespace lpc
