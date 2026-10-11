#pragma once
#include <array>
#include <memory>
#include <string>

#include "lpc/audio/processors.h"

namespace lpc {

// A MIDI output port of the system (a hardware synth, a virtual cable). send() may be called from the audio thread; the implementation must not
// block for long (the JUCE one hands the message to the driver).
class IMidiOutput {
public:
    virtual ~IMidiOutput() = default;
    virtual void send(unsigned char status, unsigned char data1, unsigned char data2) noexcept = 0;
};

namespace audio {

// The instrument of an External MIDI track: it makes no sound itself, the notes (and the controllers, bends and stop messages that reach an
// instrument) go out of a MIDI port on one channel. Notes that are still on when it goes away are turned off.
class ExternalMidiInstrument final : public IInstrument {
public:
    ExternalMidiInstrument(std::shared_ptr<IMidiOutput> out, int channel) : out_(std::move(out)), channel_(static_cast<unsigned char>((channel < 1 ? 1 : (channel > 16 ? 16 : channel)) - 1)) {}
    ~ExternalMidiInstrument() override {
        if (!out_) return;
        for (int n = 0; n < 128; ++n)
            if (held_[static_cast<std::size_t>(n)]) out_->send(static_cast<unsigned char>(0x80 | channel_), static_cast<unsigned char>(n), 0);
        out_->send(static_cast<unsigned char>(0xB0 | channel_), 123, 0);  // all notes off
    }
    void render(float* l, float* r, int frames, const MidiEvent* events, int count) noexcept override {
        for (int i = 0; i < frames; ++i) l[i] = r[i] = 0.0f;
        if (!out_) return;
        for (int i = 0; i < count; ++i) {
            const MidiEvent& e = events[i];
            if (e.status >= 0xF0) continue;  // system messages stay out
            const unsigned char type = e.status & 0xF0;
            if (type == 0x90 && e.data2 > 0) held_[e.data1 & 0x7F] = true;
            else if (type == 0x80 || type == 0x90) held_[e.data1 & 0x7F] = false;
            out_->send(static_cast<unsigned char>(type | channel_), e.data1, e.data2);
        }
    }
    nlohmann::json describe() const override { return {{"external", true}, {"channel", channel_ + 1}}; }

private:
    std::shared_ptr<IMidiOutput> out_;
    unsigned char channel_;
    std::array<bool, 128> held_{};
};

}  // namespace audio
}  // namespace lpc
