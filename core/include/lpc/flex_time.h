#pragma once
#include <cstdint>
#include <vector>

#include "lpc/wav.h"

namespace lpc {

// Flex Time: audio whose parts are moved and stretched by flex markers. A marker ties a position of the original sound (src) to the
// position where it must be heard (dst); between two markers the sound is stretched or squeezed to fit, outside them it stays as is.
// The sound keeps its length: the start and the end are fixed. See docs/logic-reference/flex-algorithms.md.

enum class FlexMode { Off, Slicing, Rhythmic, Monophonic, Polyphonic, Tempophone, Speed, Automatic };

const char* flexModeName(FlexMode m);          // "off", "slicing", "rhythmic", "monophonic", "polyphonic", "tempophone", "speed", "automatic"
FlexMode flexModeFromName(const char* name);   // Off for an unknown name

struct FlexAnchor {
    std::int64_t src = 0;
    std::int64_t dst = 0;
};

// The piecewise-linear map between the original sound (srcLength frames) and the flexed one (dstLength frames: the same unless the
// region was trimmed after the markers were set). The anchors are cleaned up on construction: sorted by src, those outside the sound or
// that would not keep the order of dst are dropped.
class TimeMap {
public:
    TimeMap() = default;
    TimeMap(std::int64_t length, std::vector<FlexAnchor> anchors) : TimeMap(length, length, std::move(anchors)) {}
    TimeMap(std::int64_t srcLength, std::int64_t dstLength, std::vector<FlexAnchor> anchors);
    std::int64_t srcLength() const { return srcLength_; }
    std::int64_t dstLength() const { return dstLength_; }
    const std::vector<FlexAnchor>& points() const { return pts_; }  // including (0, 0) and (srcLength, dstLength)
    double toSource(double dst) const;
    double toDest(double src) const;
    bool isIdentity() const { return pts_.size() <= 2; }

    // The same map with a stretch-free window around every transient (`preMs` before it, `postMs` after it), so that the stretch is
    // taken up by the sound between transients and the attacks stay sharp.
    TimeMap withProtectedTransients(const std::vector<std::int64_t>& transients, int sampleRate, double preMs = 4.0, double postMs = 30.0) const;

private:
    std::int64_t srcLength_ = 0, dstLength_ = 0;
    std::vector<FlexAnchor> pts_;
};

// Where the sound begins anew (an attack): frames of `d`. `sensitivity` in [0, 1]: more finds more (and softer) attacks.
std::vector<std::int64_t> findTransients(const WavData& d, double sensitivity = 0.5);

struct FlexTimeParams {
    FlexMode mode = FlexMode::Monophonic;
    double decay = 0.5;         // Slicing, Rhythmic: how fast a slice dies out in the gap that stretching leaves after it (0 quickly, 1 slowly)
    double sliceLength = 1.0;   // Slicing: the part of every slice that plays (0.1 .. 1)
    double loopLengthMs = 20;   // Rhythmic: the length of the piece of a slice's end that repeats to fill a gap
    double loopOffsetMs = 0;    // Rhythmic: where, counted back from the end of the slice, that piece begins
    double grainMs = 30;        // Tempophone: the length of the grains that are repeated or skipped
    bool preserveTransients = true;  // Monophonic, Polyphonic: keep attacks unstretched
};

// The sound as the markers ask: same format as `src` and `outFrames` frames (< 0: as many as `src`). `transients` are the positions
// found by findTransients (used by Slicing, Rhythmic and, if preserveTransients, by the stretching modes).
WavData renderFlexTime(const WavData& src, const std::vector<FlexAnchor>& anchors, const std::vector<std::int64_t>& transients, const FlexTimeParams& params,
                       std::int64_t outFrames = -1);

// A guess of the best mode for this sound: Slicing for short percussive hits, Monophonic for one voice or instrument, Polyphonic otherwise.
FlexMode chooseFlexMode(const WavData& d);

}  // namespace lpc
