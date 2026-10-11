#pragma once
#include <filesystem>
#include <map>
#include <string>

#include "lpc/loudness.h"
#include "lpc/mastering.h"
#include "lpc/model.h"
#include "lpc/offline_render.h"

namespace lpc {

// The Mastering Assistant's work, shared by the app and by lpc-cli: the project is rendered without the insert and analysed, then with it (a few
// renders) to find the gain that reaches -14 LUFS and the trim that Loudness Compensation uses.
struct MasteringFit {
    bool silent = false;               // the mix has no sound to analyse
    MasteringAnalysis analysis;        // of the mix before the insert; analysis.gainDb is the final gain
    std::map<std::string, double> params;  // gain, eq1..eq8, compTrim (and the insert's other parameters as they were)
    double compTrim = 0.0;
    LoudnessStats output;              // the project with the insert on, at these settings
};

// `withInsert` has the mastering insert at `insertIndex` of the track `trackId`.
MasteringFit fitMastering(const Project& withInsert, const std::filesystem::path& projectDir, const Uuid& trackId, int insertIndex, const RenderOptions& render);

// The loudness of the project as it sounds with the insert's parameters set to `params`.
LoudnessStats measureWithInsert(const Project& withInsert, const std::filesystem::path& projectDir, const Uuid& trackId, int insertIndex, const std::map<std::string, double>& params,
                                const RenderOptions& render);

}  // namespace lpc
