#pragma once
#include <string>

namespace lpc {

// Called on the device's audio thread. Must be real-time safe.
class IAudioCallback {
public:
    virtual ~IAudioCallback() = default;
    virtual void process(float* outL, float* outR, int frames) noexcept = 0;
    // The device's input for the block that process() is called for next (the first two input channels; the right one repeats the
    // left on a mono input). Not called when the device has no input.
    virtual void input(const float* /*inL*/, const float* /*inR*/, int /*frames*/) noexcept {}
};

class IAudioDevice {
public:
    virtual ~IAudioDevice() = default;
    // Opens the default output at exactly `sampleRate` (no resampling in Core). On failure returns false and
    // fills `error` with a message a user can act on.
    virtual bool open(double sampleRate, int bufferSize, IAudioCallback& callback, std::string& error) = 0;
    virtual void close() = 0;
    virtual double sampleRate() const = 0;
    virtual int bufferSize() const = 0;
};

}  // namespace lpc
