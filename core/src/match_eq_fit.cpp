#include "lpc/match_eq_fit.h"

#include <cmath>
#include <stdexcept>

#include "lpc/audio_analysis.h"
#include "lpc/effect_specs.h"
#include "lpc/match_eq.h"
#include "lpc/media_store.h"
#include "lpc/processor_ids.h"

namespace lpc {

MatchEqFit fitMatchEq(const Project& withInsert, const std::filesystem::path& projectDir, const Uuid& trackId, int insertIndex, const std::vector<double>& target,
                      const RenderOptions& render) {
    MatchEqFit fit;
    if (target.size() != static_cast<std::size_t>(kMatchEqBands)) throw std::runtime_error("the target curve must have 31 bands");
    Project p = withInsert;
    Track* track = nullptr;
    for (Track& t : p.tracks)
        if (t.id == trackId) track = &t;
    if (!track || insertIndex < 0 || insertIndex >= static_cast<int>(track->strip.inserts.size()) || track->strip.inserts[static_cast<std::size_t>(insertIndex)].processorId != kProcMatchEq)
        throw std::runtime_error("the track has no Match EQ at that place");
    std::map<std::string, double> params = track->strip.inserts[static_cast<std::size_t>(insertIndex)].params;
    track->strip.inserts.erase(track->strip.inserts.begin() + insertIndex, track->strip.inserts.end());  // what it listens to arrives before it
    const bool isSource = track->kind == TrackKind::Audio || track->kind == TrackKind::Instrument;
    if (isSource)  // a track on its own: every other source is muted (buses and the master stay in the path)
        for (Track& t : p.tracks)
            if (t.id != trackId && (t.kind == TrackKind::Audio || t.kind == TrackKind::Instrument)) t.strip.mute = true;
    if (projectEndFrame(p) == 0) {
        fit.silent = true;
        return fit;
    }
    MediaStore media(projectDir, /*streaming=*/false);
    const RenderResult r = renderOffline(p, media, render);
    WavData audio;
    audio.sampleRate = r.sampleRate;
    audio.channels = 2;
    audio.samples = r.interleaved;
    AnalysisOptions options;
    options.tempo = false;
    options.timeline = false;
    const AudioReport report = analyseAudio(audio, options);
    if (report.loudness.integrated <= -69.0) {
        fit.silent = true;
        return fit;
    }
    fit.source = matchEqBands(report.bandCentres, report.bandDb);
    const auto get = [&](const char* name) {
        const EffectSpec* spec = findEffectSpec(kProcMatchEq);
        const EffectParam* prm = spec ? spec->find(name) : nullptr;
        const auto it = params.find(name);
        return it == params.end() ? (prm ? prm->def : 0.0) : it->second;
    };
    // the insert applies its own Amount when it plays: the correction stored in the bands is the full one
    fit.correction = matchEqCorrection(fit.source, target, 1.0, get("fadeLow"), get("fadeHigh"), 18.0);
    for (int i = 0; i < kMatchEqBands; ++i) params["b" + std::to_string(i)] = std::round(fit.correction[static_cast<std::size_t>(i)] * 100.0) / 100.0;
    fit.params = params;
    return fit;
}

}  // namespace lpc
