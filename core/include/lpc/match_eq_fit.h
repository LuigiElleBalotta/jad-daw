#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "lpc/model.h"
#include "lpc/offline_render.h"

namespace lpc {

// Match EQ's Learn: the audio that reaches the insert (the track alone, or the mix when the insert is on the master/output; only what comes
// before the insert) is rendered and its spectrum measured on the 31 third-octave bands, then the correction towards `target` (a curve on the same
// bands, a factory reference or another track's spectrum) is worked out. `params` are the insert's parameters with b0..b30 set.
struct MatchEqFit {
    bool silent = false;
    std::vector<double> source;      // the measured spectrum, dB per band
    std::vector<double> correction;  // dB per band (before the insert's Amount)
    std::map<std::string, double> params;
};

MatchEqFit fitMatchEq(const Project& withInsert, const std::filesystem::path& projectDir, const Uuid& trackId, int insertIndex, const std::vector<double>& target,
                      const RenderOptions& render);

}  // namespace lpc
