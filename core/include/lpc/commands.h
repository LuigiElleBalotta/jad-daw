#pragma once
#include <optional>
#include <vector>

#include "lpc/command.h"

namespace lpc {

struct StripPatch {
    std::optional<float> gainDb;
    std::optional<float> pan;
    std::optional<bool> mute;
    std::optional<bool> solo;
};

CommandPtr makeAddTrack(Track track, int index = -1);  // index -1 appends
CommandPtr makeRemoveTrack(Uuid trackId);
CommandPtr makeSetStrip(Uuid trackId, StripPatch patch);
CommandPtr makeSetTempo(Ticks tick, double bpm);
CommandPtr makeRemoveTempo(Ticks tick);
CommandPtr makeAddMedia(MediaItem item);
CommandPtr makeRemoveMedia(Uuid mediaId);

CommandPtr commandFromJson(const nlohmann::json& j);  // throws std::runtime_error

}  // namespace lpc
