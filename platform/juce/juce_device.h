#pragma once
#include <memory>

#include "lpc/device.h"

namespace lpc {

std::unique_ptr<IAudioDevice> makeJuceAudioDevice();

// The audio devices of the system, and the sample rates and buffer sizes of `output` (empty: the default output) with `input`.
AudioDeviceChoices listJuceAudioDevices(const std::string& output, const std::string& input);

}  // namespace lpc
