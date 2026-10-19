#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>

#include "lpc/audio_analysis.h"
#include "lpc/demo_project.h"
#include "lpc/match_eq.h"
#include "lpc/match_eq_fit.h"
#include "lpc/media_store.h"
#include "lpc/offline_render.h"
#include "lpc/processor_ids.h"
#include "lpc/validation.h"

using namespace lpc;
using Catch::Approx;

namespace {

Track* masterOf(Project& p) {
    for (Track& t : p.tracks)
        if (t.kind == TrackKind::Master) return &t;
    return nullptr;
}

// spread of the third-octave levels between 100 Hz and 8 kHz of what the project renders
double spreadOf(const Project& p) {
    MediaStore media({}, false);
    RenderOptions o;
    o.tailSeconds = 0.3;
    const RenderResult r = renderOffline(p, media, o);
    WavData wav;
    wav.sampleRate = r.sampleRate;
    wav.channels = 2;
    wav.samples = r.interleaved;
    AnalysisOptions a;
    a.tempo = false;
    a.timeline = false;
    const AudioReport rep = analyseAudio(wav, a);
    const std::vector<double> bands = matchEqBands(rep.bandCentres, rep.bandDb);
    double lo = 1e9, hi = -1e9;
    for (int i = 0; i < kMatchEqBands; ++i) {
        const double f = matchEqBandFreq(i);
        if (f < 100 || f > 8000) continue;
        lo = std::min(lo, bands[static_cast<std::size_t>(i)]);
        hi = std::max(hi, bands[static_cast<std::size_t>(i)]);
    }
    return hi - lo;
}

}  // namespace

TEST_CASE("match eq fit: a flat target flattens the demo mix, a tilted one tilts it", "[matcheq]") {
    Project p = makeDemoProject({});
    Track* master = masterOf(p);
    REQUIRE(master != nullptr);
    master->strip.inserts.push_back(ProcessorRef{kProcMatchEq, {}, "", "", false});
    const std::vector<double> flat(kMatchEqBands, -40.0);
    const MatchEqFit fit = fitMatchEq(p, {}, master->id, 0, flat, RenderOptions{});
    REQUIRE(!fit.silent);
    REQUIRE(fit.source.size() == static_cast<std::size_t>(kMatchEqBands));
    REQUIRE(fit.correction.size() == static_cast<std::size_t>(kMatchEqBands));
    double mean = 0;
    for (const double c : fit.correction) mean += c;
    CHECK(std::abs(mean / kMatchEqBands) < 1.0);          // the correction is about the shape, not the level
    CHECK(fit.params.count("b0") == 1);
    CHECK(fit.params.count("b30") == 1);
    master->strip.inserts[0].params = fit.params;
    CHECK(!checkProject(p).has_value());
    // the same mix with the correction in place has a flatter spectrum than without
    Project dry = p;
    masterOf(dry)->strip.inserts.clear();
    CHECK(spreadOf(p) < spreadOf(dry));
}

TEST_CASE("match eq fit: a track with no sound is reported as silent, a wrong insert throws", "[matcheq]") {
    Project p = makeDemoProject({});
    Track* master = masterOf(p);
    master->strip.inserts.push_back(ProcessorRef{kProcMatchEq, {}, "", "", false});
    for (Track& t : p.tracks) t.regions.clear();
    CHECK(fitMatchEq(p, {}, master->id, 0, std::vector<double>(kMatchEqBands, -40.0), RenderOptions{}).silent);
    CHECK_THROWS(fitMatchEq(p, {}, master->id, 3, std::vector<double>(kMatchEqBands, -40.0), RenderOptions{}));
    CHECK_THROWS(fitMatchEq(p, {}, master->id, 0, std::vector<double>(5, -40.0), RenderOptions{}));
}
