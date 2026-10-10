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
    std::optional<int> input;  // the recording input: 0 stereo 1+2, n mono input n (0..64)
};

struct TrackPatch {
    std::optional<std::string> name;
    std::optional<std::string> color;
    std::optional<bool> showInTracks;
    std::optional<std::string> automationMode;  // off, read, touch, latch or write
    std::optional<double> delayMs;              // -1000..1000 ms (audio and instrument tracks)
    std::optional<MidiShaping> midi;            // transpose, velocity and the limits (instrument tracks)
};

CommandPtr makeAddTrack(Track track, int index = -1);  // index -1 appends
CommandPtr makeRemoveTrack(Uuid trackId);
CommandPtr makeSetStrip(Uuid trackId, StripPatch patch);
CommandPtr makeSetTempo(Ticks tick, double bpm);
CommandPtr makeRemoveTempo(Ticks tick);
CommandPtr makeSetTrackProps(Uuid trackId, TrackPatch patch);
CommandPtr makeSetSignature(Ticks tick, int numerator, int denominator);
CommandPtr makeRemoveSignature(Ticks tick);
// Replaces the points of one automation lane of a track; target is "volume" (dB) or "pan" (-1..1). No points removes the lane.
// The points are sorted by tick; the undo restores the previous points (or the absence of the lane).
CommandPtr makeSetAutomation(Uuid trackId, std::string target, std::vector<AutomationPoint> points);
// Replaces the track groups (members must be existing tracks but the master, none in two groups, unique ids, at most 64 groups).
CommandPtr makeSetGroups(std::vector<Group> groups);
// Puts the tracks in this order (every track but the master, once each; the master stays first). The undo restores the previous order.
CommandPtr makeSetTrackOrder(std::vector<Uuid> order);
CommandPtr makeSetProjectName(std::string name);  // 1 to 128 characters without control characters
CommandPtr makeSetMarkers(std::vector<Marker> markers);  // replaces the whole list (sorted by tick, unique ids); the undo is the previous list
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
CommandPtr makeSetInstrumentState(Uuid trackId, std::string state);  // the saved state (base64) of a plug-in instrument
CommandPtr makeSetInstrument(Uuid trackId, ProcessorRef instrument);   // instrument tracks only
CommandPtr makeSetOutput(Uuid trackId, Uuid output);                   // null output = master
struct SendPatch {
    std::optional<float> levelDb;
    std::optional<bool> preFader;
};
CommandPtr makeSetSend(Uuid sendId, SendPatch patch);  // a patch changes only the fields it carries
// Loops the region: its first `loopLength` (unit of the region, at most the length; 0 = off) repeat until the end.
CommandPtr makeSetRegionLoop(Uuid regionId, std::int64_t loopLength);
CommandPtr makeSetRegionFades(Uuid regionId, std::int64_t fadeIn, std::int64_t fadeOut);  // the unit of the region's start; 0 = none; each at most the length
CommandPtr makeSetRegionGain(Uuid regionId, float gainDb);
CommandPtr makeAddInsert(Uuid trackId, ProcessorRef insert, int index = -1);  // index -1 appends
CommandPtr makeRemoveInsert(Uuid trackId, int index);
CommandPtr makeSetInsertParam(Uuid trackId, int index, std::string param, std::optional<double> value);  // nullopt removes it
CommandPtr makeSetInsertState(Uuid trackId, int index, std::string state);  // plug-in inserts only; base64 state
// Moves one insert. Without toTrackId (null) inside the track's chain: `to` is the index after the move (0..n-1). With another
// track: the insert leaves `trackId` and is inserted into `toTrackId` at `to` (0..m).
CommandPtr makeMoveInsert(Uuid trackId, int from, int to, Uuid toTrackId = {});
CommandPtr makeSetInsertBypass(Uuid trackId, int index, bool bypass);
CommandPtr makeTransaction(std::vector<CommandPtr> commands);

CommandPtr commandFromJson(const nlohmann::json& j);  // throws std::runtime_error

}  // namespace lpc
