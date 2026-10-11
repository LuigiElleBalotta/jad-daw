#include "lpc/mastering_fit.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "lpc/media_store.h"
#include "lpc/processor_ids.h"

namespace lpc {

namespace {

Track* trackIn(Project& p, const Uuid& id) {
    for (Track& t : p.tracks)
        if (t.id == id) return &t;
    return nullptr;
}

double round2(double v) { return std::round(v * 100.0) / 100.0; }

}  // namespace

LoudnessStats measureWithInsert(const Project& withInsert, const std::filesystem::path& projectDir, const Uuid& trackId, int insertIndex,
                                const std::map<std::string, double>& params, const RenderOptions& render) {
    Project p = withInsert;
    Track* t = trackIn(p, trackId);
    if (!t || insertIndex < 0 || insertIndex >= static_cast<int>(t->strip.inserts.size())) throw std::runtime_error("no such insert");
    t->strip.inserts[static_cast<std::size_t>(insertIndex)].params = params;
    MediaStore media(projectDir, /*streaming=*/false);
    const RenderResult r = renderOffline(p, media, render);
    WavData out;
    out.sampleRate = r.sampleRate;
    out.channels = 2;
    out.samples = r.interleaved;
    return measureLoudness(out);
}

MasteringFit fitMastering(const Project& withInsert, const std::filesystem::path& projectDir, const Uuid& trackId, int insertIndex, const RenderOptions& render) {
    MasteringFit fit;
    Project without = withInsert;
    Track* t = trackIn(without, trackId);
    if (!t || insertIndex < 0 || insertIndex >= static_cast<int>(t->strip.inserts.size()) || t->strip.inserts[static_cast<std::size_t>(insertIndex)].processorId != kProcMastering)
        throw std::runtime_error("the track has no Mastering Assistant at that place");
    std::map<std::string, double> base = t->strip.inserts[static_cast<std::size_t>(insertIndex)].params;
    t->strip.inserts.erase(t->strip.inserts.begin() + insertIndex);  // what it listens to is the mix before it
    if (projectEndFrame(without) == 0) {
        fit.silent = true;
        return fit;
    }
    {
        MediaStore media(projectDir, /*streaming=*/false);
        const RenderResult r = renderOffline(without, media, render);
        WavData mix;
        mix.sampleRate = r.sampleRate;
        mix.channels = 2;
        mix.samples = r.interleaved;
        fit.analysis = analyseMastering(mix);
    }
    MasteringAnalysis& a = fit.analysis;
    if (a.before.integrated <= -69.0) {
        fit.silent = true;
        return fit;
    }
    base["gain"] = round2(a.gainDb);
    for (int b = 0; b < kMasteringBands; ++b) base["eq" + std::to_string(b + 1)] = round2(a.eqDb[static_cast<std::size_t>(b)]);
    base["bypass"] = 0.0;
    // what the insert does to the loudness at unity gain (correction EQ, compressor, soft clip): Loudness Compensation is the trim that brings it back
    std::map<std::string, double> unity = base;
    unity["compensate"] = 1.0;
    unity["compTrim"] = 0.0;
    const LoudnessStats atUnity = measureWithInsert(withInsert, projectDir, trackId, insertIndex, unity, render);
    const double lost = a.before.integrated - atUnity.integrated;
    fit.compTrim = round2(std::clamp(lost, -12.0, 12.0));
    base["compTrim"] = fit.compTrim;
    // the gain that reaches the target: the first estimate adds what is lost, then it is corrected from what is measured (a limiter that is working
    // hard gives back less than it is asked for, so it stops when more gain does not make it louder)
    double gain = std::clamp(a.gainDb + lost, -24.0, 36.0);
    base["gain"] = round2(gain);
    fit.output = measureWithInsert(withInsert, projectDir, trackId, insertIndex, base, render);
    for (int pass = 0; pass < 3; ++pass) {
        const double err = kMasteringTargetLufs - fit.output.integrated;
        if (std::abs(err) < 0.4) break;
        const double candidate = std::clamp(gain + err, -24.0, 36.0);
        if (candidate == gain) break;
        std::map<std::string, double> trial = base;
        trial["gain"] = round2(candidate);
        const LoudnessStats got = measureWithInsert(withInsert, projectDir, trackId, insertIndex, trial, render);
        if (std::abs(kMasteringTargetLufs - got.integrated) >= std::abs(err) - 0.2) break;
        gain = candidate;
        base = trial;
        fit.output = got;
    }
    a.gainDb = round2(gain);
    fit.params = base;
    return fit;
}

}  // namespace lpc
