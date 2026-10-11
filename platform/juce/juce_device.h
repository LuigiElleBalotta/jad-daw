#pragma once
#include <memory>
#include <string>
#include <vector>

#include "lpc/audio/external_midi.h"
#include "lpc/device.h"

namespace lpc {

std::unique_ptr<IAudioDevice> makeJuceAudioDevice();

// The MIDI input devices of the system, and an open set of them: every event goes to `sink` (on the device's thread) until it is destroyed.
std::vector<std::string> listJuceMidiInputs();
class IMidiInputs {
public:
    virtual ~IMidiInputs() = default;
    virtual int count() const = 0;  // how many of the wanted devices could be opened
};
// names empty: every input
std::unique_ptr<IMidiInputs> openJuceMidiInputs(const std::vector<std::string>& names, IMidiSink& sink);

// The MIDI output ports of the system, and one opened by name (shared: the same name gives the same port while someone holds it; nullptr when it
// cannot be opened).
std::vector<std::string> listJuceMidiOutputs();
std::shared_ptr<IMidiOutput> openJuceMidiOutput(const std::string& name);

// The audio devices of the system, and the sample rates and buffer sizes of `output` (empty: the default output) with `input`.
AudioDeviceChoices listJuceAudioDevices(const std::string& output, const std::string& input);

}  // namespace lpc
