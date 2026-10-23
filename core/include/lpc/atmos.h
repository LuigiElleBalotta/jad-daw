#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "lpc/adm_bwf.h"
#include "lpc/media_store.h"
#include "lpc/model.h"
#include "lpc/plugin_host.h"

namespace lpc {

// Spatial Audio with Dolby Atmos. The project setting "spatialAudio" is "dolbyAtmos" when the mode is on; "atmosMonitor" ("binaural" or "stereo") is how the objects are heard.
// An object track is one with a 3D Object Panner insert (kProcObjectPanner): the track's sound is kept apart from its place in the room, which is the panner's parameters
// and their automation. Every other track is part of the bed, which is the stereo mix of those tracks in the front of the 7.1.2 bed of the file.
constexpr int kMaxAtmosObjects = 118;

bool dolbyAtmosOn(const Project& project);
// The index of the 3D Object Panner among the inserts of a track, -1 when it has none (the track is a bed track).
int objectPannerIndex(const Track& track);
std::vector<Uuid> atmosObjects(const Project& project);

// A place that is the parameters of the panner, or its automation, at `tick` (the lanes of the track, played as the graph plays them).
AdmPosition objectPositionAt(const Project& project, const Track& track, Ticks tick);

struct AtmosExportOptions {
    std::int64_t startFrame = 0;
    std::int64_t frames = -1;  // -1: from startFrame to the end of the last region, plus tailSeconds
    double tailSeconds = 2.0;
    int bitDepth = 24;
    double positionsPerSecond = 10.0;  // how often the movement of an object is written
    IPluginHost* plugins = nullptr;
};

// The master of the project as an ADM BWF: the object tracks one by one (their own sound, without the panner and the master's processing) with their movement, and the bed. Throws
// std::runtime_error when there is nothing to export or too many objects.
AdmFile renderAdm(const Project& project, MediaStore& media, const AtmosExportOptions& options);

// A new project from an ADM BWF: a track for every object (mono audio, with a 3D Object Panner and its movement as automation), and a stereo track with the fold-down of the bed.
// The audio files are written in <folder>/audio. The project is in Dolby Atmos mode.
Project projectFromAdm(const AdmFile& file, const std::filesystem::path& folder);

}  // namespace lpc
