#pragma once
#include <string>
#include <vector>

namespace lpc {

// Called on the device's audio thread. Must be real-time safe.
class IAudioCallback {
public:
    virtual ~IAudioCallback() = default;
    virtual void process(float* outL, float* outR, int frames) noexcept = 0;
    // The device's input for the block that process() is called for next: one pointer per input channel (as many as the device has
    // open). Not called when the device has no input.
    virtual void input(const float* const* /*channels*/, int /*numChannels*/, int /*frames*/) noexcept {}
};

// What the user can choose in Preferences > Audio. Empty names stand for the system's default device.
struct AudioDeviceRequest {
    std::string output, input;
    double sampleRate = 48000.0;
    int bufferSize = 256;
};

// The devices of the system and what the chosen output device offers.
struct AudioDeviceChoices {
    std::vector<std::string> outputs, inputs;
    std::string currentOutput, currentInput;  // the ones the lists below describe
    std::vector<double> rates;
    std::vector<int> buffers;
    int inputChannels = 0, outputChannels = 0;  // of the chosen devices
};

class IAudioDevice {
public:
    virtual ~IAudioDevice() = default;
    // Opens the named devices (see AudioDeviceRequest); the default implementation ignores the names.
    virtual bool openWith(const AudioDeviceRequest& request, IAudioCallback& callback, std::string& error) {
        return open(request.sampleRate, request.bufferSize, callback, error);
    }
    virtual int inputChannels() const { return 0; }
    virtual int roundTripLatency() const { return 0; }  // input + output latency in samples, as the device reports it
    // Opens the default output at exactly `sampleRate` (no resampling in Core). On failure returns false and
    // fills `error` with a message a user can act on.
    virtual bool open(double sampleRate, int bufferSize, IAudioCallback& callback, std::string& error) = 0;
    virtual void close() = 0;
    virtual double sampleRate() const = 0;
    virtual int bufferSize() const = 0;
};

}  // namespace lpc
