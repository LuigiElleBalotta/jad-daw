#pragma once
#include <optional>
#include <vector>

#include "lpc/model.h"

namespace lpc {

// Live Loops cells made from what lies on a track. The regions given (musical time) are cut to [from, to) ticks and laid into one looping region that starts
// at tick 0: an audio track gives the first region that crosses the window (a cell plays one sound), a MIDI or instrument track gives the notes and controls
// of all of them. Nothing when the window holds nothing of them.
std::optional<Region> cellFromRegions(const Project& project, const Track& track, const std::vector<const Region*>& regions, Ticks from, Ticks to, const Uuid& newId);

// The first scene (from 0) in which none of the tracks has a cell.
int firstEmptyScene(const Project& project);

// Where a cell that is launched while the timeline is at `positionTicks` begins: the next multiple of `quantizeTicks` (the position itself when it is on one).
Ticks launchTick(Ticks positionTicks, Ticks quantizeTicks);

}  // namespace lpc
