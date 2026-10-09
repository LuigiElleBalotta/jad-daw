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

struct TrackPatch {
    std::optional<std::string> name;
    std::optional<std::string> color;
    std::optional<bool> showInTracks;
};

CommandPtr makeAddTrack(Track track, int index = -1);  // index -1 appends
CommandPtr makeRemoveTrack(Uuid trackId);
CommandPtr makeSetStrip(Uuid trackId, StripPatch patch);
CommandPtr makeSetTempo(Ticks tick, double bpm);
CommandPtr makeRemoveTempo(Ticks tick);
CommandPtr makeSetTrackProps(Uuid trackId, TrackPatch patch);
CommandPtr makeSetSignature(Ticks tick, int numerator, int denominator);
CommandPtr makeRemoveSignature(Ticks tick);
CommandPtr makeAddMedia(MediaItem item, int index = -1);  // index -1 appends
CommandPtr makeRemoveMedia(Uuid mediaId);
CommandPtr makeAddRegion(Uuid trackId, Region region, int index = -1);  // index -1 appends
CommandPtr makeRemoveRegion(Uuid regionId);
CommandPtr makeMoveRegion(Uuid regionId, std::int64_t newStart);        // ticks or microseconds, as the region's timeBase
CommandPtr makeReplaceRegion(Region region);                                          // swaps the region with the same id
CommandPtr makeResizeRegion(Uuid regionId, std::int64_t start, std::int64_t length);  // ticks or microseconds, as the region's timeBase
CommandPtr makeSplitRegion(Uuid regionId, std::int64_t at, Uuid newRegionId);         // newRegionId names the right part
CommandPtr makeJoinRegions(std::vector<Uuid> regionIds);                              // the earliest region keeps its id
CommandPtr makeAddSend(Uuid trackId, Send send, int index = -1);
CommandPtr makeRemoveSend(Uuid sendId);
CommandPtr makeSetInserts(Uuid trackId, std::vector<ProcessorRef> inserts);
CommandPtr makeSetPatchId(Uuid trackId, std::string patchId);          // "" clears the patch
CommandPtr makeSetInstrument(Uuid trackId, ProcessorRef instrument);   // instrument tracks only
CommandPtr makeSetOutput(Uuid trackId, Uuid output);                   // null output = master
struct SendPatch {
    std::optional<float> levelDb;
    std::optional<bool> preFader;
};
CommandPtr makeSetSend(Uuid sendId, SendPatch patch);  // a patch changes only the fields it carries
CommandPtr makeSetRegionGain(Uuid regionId, float gainDb);
CommandPtr makeAddInsert(Uuid trackId, ProcessorRef insert, int index = -1);  // index -1 appends
CommandPtr makeRemoveInsert(Uuid trackId, int index);
CommandPtr makeSetInsertParam(Uuid trackId, int index, std::string param, std::optional<double> value);  // nullopt removes it
CommandPtr makeSetInsertState(Uuid trackId, int index, std::string state);  // plug-in inserts only; base64 state
CommandPtr makeTransaction(std::vector<CommandPtr> commands);

CommandPtr commandFromJson(const nlohmann::json& j);  // throws std::runtime_error

}  // namespace lpc
