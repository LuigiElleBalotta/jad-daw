#pragma once
#include <array>
#include <string>
#include <vector>

#include "lpc/adm_bwf.h"

namespace lpc {

// The renderer of a Dolby Atmos mix to speakers: an object at (x, y, z) (x from the left wall -1 to the right wall 1, y from the back -1 to the front 1, z from the floor 0 to the
// ceiling 1) is spread over the speakers of the monitoring format. Our own renderer (distance-based amplitude panning over a 7.1.4 room, folded to the smaller layouts with the
// downmix rules of Logic's guide), not Dolby's. The channels are always in this container order, a format uses the ones it has and leaves the others silent:
//   0 L, 1 R, 2 C, 3 LFE, 4 Ls (side), 5 Rs, 6 Lrs (rear), 7 Rrs, 8 Ltf (top front, or the top middle of 5.1.2), 9 Rtf, 10 Ltr (top rear), 11 Rtr
constexpr int kSpeakerSlots = 12;
using SpeakerGains = std::array<float, kSpeakerSlots>;

// The monitoring formats. 0 and 1 are for headphones or stereo speakers and are made by the 3D Object Panner itself (a plain pan and the binaural rendering).
enum class MonitorFormat { StereoPan = 0, Binaural = 1, Direct20 = 2, From51To20 = 3, S51 = 4, S71 = 5, S512 = 6, S514 = 7, S714 = 8 };
constexpr int kMonitorFormats = 9;

int monitorChannels(MonitorFormat f);                         // 2 for the stereo formats, 6, 8, 8, 10 and 12 for the speaker layouts
const char* monitorName(MonitorFormat f);                     // "7.1.4"
const std::vector<std::string>& monitorChannelNames(MonitorFormat f);  // the channels in the order of the file

// The downmix algorithms and the trims of Logic's Downmix & Trim pane.
struct AtmosTrim {
    double surroundDb = 0.0;  // the level of the surround channels (0 or less)
    double heightDb = 0.0;    // the level of the height channels
    double overhead = 0.0;    // -1..1: the front/back balance of the height speakers (to the right: the front gets louder)
    double plane = 0.0;       // -1..1: the front/back balance of the ear-level speakers
};
struct AtmosDownmix {
    int to51 = 0;  // 5.1 and 5.1.x: 0 Lo/Ro, 1 Dolby Pro Logic IIx, 2 direct render with room balance, 3 direct render
    int to20 = 0;  // 5.1 to 2.0: 0 Lo/Ro, 1 Lt/Rt (Pro Logic II), 2 Lt/Rt with phase 90
    AtmosTrim trim;
};
// The five channel layouts that have trims of their own (2.0 Direct, 5.1 & 2.0, 5.1.2, 5.1.4, 7.1) and the values that Automatic gives them.
int trimLayoutOf(MonitorFormat f);  // 0..4, -1 for a format without trims (7.1.4, the stereo ones)
AtmosTrim automaticTrim(int layout);

// The gains of one object for the format, in container order. Not for the two panner formats.
SpeakerGains objectGains(MonitorFormat f, double x, double y, double z, double size, const AtmosDownmix& downmix);

// For the 2.0 formats: the gain of the object into the left and the right channel, and (Lt/Rt with phase 90) the gain of its 90-degree-shifted copy, which carries the surround part.
struct StereoGains {
    float l = 0, r = 0, lPhase = 0, rPhase = 0;
};
StereoGains stereoGains(MonitorFormat f, double x, double y, double z, double size, const AtmosDownmix& downmix);

// The whole mix to the format: every object with its movement, and the bed's front pair in L and R; the bed's other channels go where their names say. Interleaved, `monitorChannels(f)`
// channels. The binaural formats are not made here (they need the panner).
std::vector<float> renderSpeakers(const AdmFile& adm, MonitorFormat f, const AtmosDownmix& downmix);

}  // namespace lpc
