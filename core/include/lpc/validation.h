#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "lpc/command.h"
#include "lpc/model.h"

namespace lpc {

// The rules every project must follow, whether it was built by commands or read from disk.
// Commands and the project loader share these checks, so a loaded project never contains something
// a command would have refused (and undo of any command can always be applied).

inline constexpr std::int64_t kMaxPosition = std::int64_t{1} << 40;  // ticks, microseconds or frames; keeps sums far from overflow
inline constexpr std::size_t kMaxProjectTracks = 1024;               // equals the audio engine's track capacity

using MaybeError = std::optional<CommandError>;

inline constexpr std::size_t kMaxPluginStateChars = 22369624;  // base64 of 16 MiB
inline constexpr std::size_t kMaxInsertLabelBytes = 128;
bool validBase64(const std::string& s);  // "" or standard base64 with padding

inline bool isBusLike(TrackKind k) { return k == TrackKind::Bus || k == TrackKind::Aux; }

MaybeError checkStripValues(float gainDb, float pan);
MaybeError checkRegion(const Project& p, TrackKind kind, const Region& r);
MaybeError checkSendFields(const Project& p, const Uuid& owner, const Send& s);
MaybeError checkInsert(const ProcessorRef& insert);
// An instrument: a built-in one (see effect_specs.h) or a "vst3:" plug-in with a base64 state and no parameters.
MaybeError checkInstrument(const ProcessorRef& instrument);
MaybeError checkMidiShaping(const MidiShaping& midi);  // transpose, velocity, key limit and velocity limit of an instrument track
bool isKnownTrackColor(const std::string& color);  // "" (automatic) or a palette name
bool validTrackName(const std::string& name);      // 1 to 64 UTF-8 code points, no control characters
bool validPatchId(const std::string& id);          // 1 to 64 characters of A-Z a-z 0-9 . _ -
MaybeError checkPatchId(const std::string& id);    // "" (no patch) or a valid id
MaybeError checkTrackProps(const std::string& name, const std::string& color);
MaybeError checkShowInTracks(TrackKind kind, bool show);  // only buses and auxes can be hidden
bool validRelativeMediaPath(const std::string& path);
// True when `goal` can be reached from `from` by following outputs and sends.
bool reaches(const Project& p, const Uuid& from, const Uuid& goal);

// Everything above applied to a whole project. Returns the first problem found.
MaybeError checkProject(const Project& p);

}  // namespace lpc
