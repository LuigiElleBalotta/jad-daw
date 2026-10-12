#pragma once
#include <string>
#include <vector>

namespace lpc {

// Called on the device's audio thread. Must be real-time safe.
class IAudioCallback {
public:
    virtual ~IAudioCallback() = default;
    virtual void process(float* outL, float* outR, int frames) noexcept = 0;
    // The same for a device with more than two output channels: outputs[0] and [1] are Stereo Out, the next pairs are the Output strips. The default plays
    // Stereo Out and silences the rest.
    virtual void processMulti(float* const* outputs, int numOutputs, int frames) noexcept {
        process(outputs[0], outputs[1], frames);
        for (int c = 2; c < numOutputs; ++c)
            for (int i = 0; i < frames; ++i) outputs[c][i] = 0.0f;
    }
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

// Called on the thread of a MIDI input device. Must be real-time safe.
class IMidiSink {
public:
    virtual ~IMidiSink() = default;
    virtual void midi(unsigned char status, unsigned char data1, unsigned char data2) noexcept = 0;
};

class IAudioDevice {
public:
    virtual ~IAudioDevice() = default;
    // Opens the named devices (see AudioDeviceRequest); the default implementation ignores the names.
    virtual bool openWith(const AudioDeviceRequest& request, IAudioCallback& callback, std::string& error) {
        return open(request.sampleRate, request.bufferSize, callback, error);
    }
    virtual int inputChannels() const { return 0; }
    virtual int outputChannels() const { return 2; }  // the output channels that are open (a multi-channel interface gives Output strips their pairs)
    virtual int roundTripLatency() const { return 0; }  // input + output latency in samples, as the device reports it
    // Opens the default output at exactly `sampleRate` (no resampling in Core). On failure returns false and
    // fills `error` with a message a user can act on.
    virtual bool open(double sampleRate, int bufferSize, IAudioCallback& callback, std::string& error) = 0;
    virtual void close() = 0;
    virtual double sampleRate() const = 0;
    virtual int bufferSize() const = 0;
};

}  // namespace lpc
