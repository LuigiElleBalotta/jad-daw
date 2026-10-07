#include "lpc/offline_render.h"

#include <algorithm>
#include <cmath>

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

RenderResult renderOffline(const Project& p, MediaStore& media, const RenderOptions& options) {
    RenderResult out;
    out.sampleRate = p.sampleRate;
    const std::int64_t start = std::max<std::int64_t>(options.startFrame, 0);
    std::int64_t total = options.frames;
    if (total < 0) {
        total = std::max<std::int64_t>(0, projectEndFrame(p) - start) +
                static_cast<std::int64_t>(std::llround(std::max(0.0, options.tailSeconds) * p.sampleRate));
    }
    out.frames = total;
    out.interleaved.assign(static_cast<std::size_t>(total) * 2, 0.0f);

    audio::AudioEngine engine(static_cast<double>(p.sampleRate));
    for (const audio::AudioMsg& m : initialMessages(p, media)) engine.applyDirect(m);
    audio::AudioMsg locate;
    locate.kind = audio::MsgKind::Locate;
    locate.frame = start;
    engine.applyDirect(locate);
    audio::AudioMsg play;
    play.kind = audio::MsgKind::Play;
    engine.applyDirect(play);

    const int block = std::clamp(options.blockSize, 1, 65536);
    std::vector<float> l(static_cast<std::size_t>(block)), r(static_cast<std::size_t>(block));
    for (std::int64_t pos = 0; pos < total; pos += block) {
        const int n = static_cast<int>(std::min<std::int64_t>(block, total - pos));
        engine.processBlock(l.data(), r.data(), n);
        for (int i = 0; i < n; ++i) {
            out.interleaved[static_cast<std::size_t>((pos + i) * 2)] = l[static_cast<std::size_t>(i)];
            out.interleaved[static_cast<std::size_t>((pos + i) * 2 + 1)] = r[static_cast<std::size_t>(i)];
        }
    }
    engine.collectGarbage();
    return out;
}

}  // namespace lpc
