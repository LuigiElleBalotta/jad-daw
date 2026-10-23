#pragma once
#include <filesystem>
#include <optional>
#include <vector>

#include "lpc/model.h"
#include "lpc/wav.h"

namespace lpc {

// Live Loops cells made from what lies on a track. The regions given (musical time) are cut to [from, to) ticks and laid into one looping region that starts
// at tick 0: an audio track gives the first region that crosses the window (a cell plays one sound), a MIDI or instrument track gives the notes and controls
// of all of them. Nothing when the window holds nothing of them.
std::optional<Region> cellFromRegions(const Project& project, const Track& track, const std::vector<const Region*>& regions, Ticks from, Ticks to, const Uuid& newId);

// How a cell plays, from its settings (see LiveCell): the cell length, the loop start and the loop end, all clamped inside the cell.
struct CellSpan {
    Ticks length = 0, loopStart = 0, loopEnd = 0;
    bool loop = true;
};
CellSpan cellSpan(const LiveCell& cell);

// The position in the cell `elapsed` ticks after a run that began at `offset` (a loop wraps from the loop end to the loop start; a one-shot stops at its length: the result is
// then the length, and `finished` is true).
Ticks cellPositionAt(const LiveCell& cell, Ticks offset, Ticks elapsed, bool* finished = nullptr);

// The ticks after a run that began at `offset` at which the cell is next at its loop end (the Cell End quantization: another cell waits for it). -1 for a one-shot that ended.
Ticks ticksToCellEnd(const LiveCell& cell, Ticks offset, Ticks elapsed);

// The regions that make a run on the timeline (absolute starts, musical): the first pass from the run's offset, then the loop. `end` of the run (-1: open) cuts them off.
std::vector<Region> playedRegions(const Project& project, const Track& track, const LiveRun& run, const LiveCell& cell);

// File > Import > Live Loops Grid: the cells of another project, with their tracks. The tracks (audio, instrument and MIDI ones that have cells) come with their strip and instrument but
// not their regions, automation or routing; the audio the cells use is copied from `sourceDir` into `targetDir`/audio and described by new media items. Everything has new ids. The caller adds
// `media` and then `tracks` to the project (checkProject must still pass).
struct ImportedGrid {
    std::vector<MediaItem> media;
    std::vector<Track> tracks;
};
ImportedGrid importLiveLoopsGrid(const Project& target, const std::filesystem::path& targetDir, const Project& source, const std::filesystem::path& sourceDir);

// Extract Loops: the loop that suits an audio cell, from its tempo and beats: it starts at the first downbeat and is the longest of 1, 2, 4 or 8 bars that the sound holds. Nothing when the
// sound has no pulse. `audio` is the sound of the cell from its first frame (already offset by the region's sourceOffsetFrames).
struct LoopSuggestion {
    Ticks start = 0, length = 0;
    int bars = 0;
};
std::optional<LoopSuggestion> suggestLoop(const WavData& audio, const Project& project);

// The first scene (from 0) in which none of the tracks has a cell.
int firstEmptyScene(const Project& project);

// Where a cell that is launched while the timeline is at `positionTicks` begins: the next multiple of `quantizeTicks` (the position itself when it is on one).
Ticks launchTick(Ticks positionTicks, Ticks quantizeTicks);

}  // namespace lpc
