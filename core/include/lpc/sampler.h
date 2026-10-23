#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "lpc/model.h"

namespace lpc {

// The Sampler instrument ("builtin.sampler"): zones of recorded sound laid over the keyboard. The zones are kept in the instrument's state as JSON:
//   {"zones": [{"mediaId": "...", "name": "Kick", "root": 36, "lo": 36, "hi": 36, "gainDb": 0, "start": 0, "end": 0, "oneShot": false, "loop": false}]}
// The audio is a file of the project's media pool; start and end are frames of it (end 0 is the end of the file).
struct SamplerZone {
    Uuid mediaId;
    std::string name;
    int root = 60;  // the key at which the sound plays at its own pitch
    int lo = 0, hi = 127;  // the keys the zone answers to
    double gainDb = 0.0;
    std::int64_t start = 0, end = 0;
    bool oneShot = false;  // plays to its end whatever the length of the note
    bool loop = false;     // while the key is held the part between start and end repeats
    bool operator==(const SamplerZone&) const = default;
};

std::vector<SamplerZone> samplerZonesOf(const ProcessorRef& instrument);  // empty when the state is empty or damaged
std::string samplerStateOf(const std::vector<SamplerZone>& zones);
std::vector<Uuid> samplerMediaIds(const ProcessorRef& instrument);        // the files the zones play: they count as used
// Empty when the zones are fine, else what is wrong with them (the key ranges, the gain, the frames).
std::string checkSamplerZones(const std::vector<SamplerZone>& zones);

}  // namespace lpc
