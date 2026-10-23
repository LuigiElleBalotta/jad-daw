#pragma once
#include <memory>

#include "lpc/audio/processors.h"

namespace lpc::audio {

// The 3D Object Panner: the stereo input is summed to one signal and placed in the room at (x, y, z) (see effect_specs.cpp). For headphones the placement is made with
// the cues of the head: a time difference between the ears up to 0.66 ms, a level difference with the head shadow (a low-pass on the far ear), a dull top end for what is
// behind and a lift of the top for what is above. The parameters are smoothed, so that a moving object does not click.
std::unique_ptr<IProcessor> makeObjectPanner(const ProcessorRef& ref, double sampleRate);

// What the binaural rendering does to a sound at this place: the delay of each ear (seconds), its gain and the cutoff (Hz) of its head-shadow filter. For tests and the
// editor's picture.
struct EarCues {
    double delayLeft = 0.0, delayRight = 0.0;
    double gainLeft = 1.0, gainRight = 1.0;
    double cutoffLeft = 20000.0, cutoffRight = 20000.0;
};
EarCues earCues(double x, double y, double z, double size);

}  // namespace lpc::audio
