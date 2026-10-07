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
CommandPtr makeAddMedia(MediaItem item, int index = -1);  // index -1 appends
CommandPtr makeRemoveMedia(Uuid mediaId);
CommandPtr makeAddRegion(Uuid trackId, Region region, int index = -1);  // index -1 appends
CommandPtr makeRemoveRegion(Uuid regionId);
CommandPtr makeMoveRegion(Uuid regionId, std::int64_t newStart);        // ticks or microseconds, as the region's timeBase
CommandPtr makeAddSend(Uuid trackId, Send send, int index = -1);
CommandPtr makeRemoveSend(Uuid sendId);
CommandPtr makeSetInserts(Uuid trackId, std::vector<ProcessorRef> inserts);
CommandPtr makeTransaction(std::vector<CommandPtr> commands);

CommandPtr commandFromJson(const nlohmann::json& j);  // throws std::runtime_error

}  // namespace lpc
