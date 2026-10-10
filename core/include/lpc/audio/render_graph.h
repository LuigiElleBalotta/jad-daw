#pragma once
#include <algorithm>
#include <array>
#include <utility>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

#include "lpc/audio/frame_source.h"
#include "lpc/audio/messages.h"
#include "lpc/audio/processors.h"
#include "lpc/model.h"

namespace lpc::audio {

inline constexpr int kMaxTracks = 1024;
inline constexpr int kMaxBlock = 512;         // render() handles at most this many frames per call
inline constexpr int kMaxBlockEvents = 256;   // MIDI events per track per block; extra events are dropped

inline constexpr int kMaxPdcFrames = 1 << 18;  // longest compensation delay of one edge (about 5.4 s at 48 kHz)

// A fixed delay of stereo audio. The buffers are allocated in the constructor (project thread); process() never allocates.
class DelayLine {
public:
    DelayLine() : size_(0) {}
    explicit DelayLine(int frames)
        : size_(frames > 0 ? frames : 0), l_(static_cast<std::size_t>(size_), 0.0f), r_(static_cast<std::size_t>(size_), 0.0f) {}
    int frames() const { return size_; }
    // out = in delayed by frames(). `in` and `out` must not overlap.
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) noexcept {
        if (size_ == 0) {
            std::copy_n(inL, n, outL);
            std::copy_n(inR, n, outR);
            return;
        }
        std::size_t p = pos_;
        const std::size_t size = static_cast<std::size_t>(size_);
        for (int i = 0; i < n; ++i) {
            outL[i] = l_[p];
            outR[i] = r_[p];
            l_[p] = inL[i];
            r_[p] = inR[i];
            if (++p == size) p = 0;
        }
        pos_ = p;
    }

private:
    int size_;
    std::vector<float> l_, r_;
    std::size_t pos_ = 0;
};

struct ControlSpan {
    std::int64_t frame = 0;
    std::uint8_t status = 0xB0, data1 = 0, data2 = 0;
};

struct NoteSpan {
    std::int64_t onFrame = 0;
    std::int64_t offFrame = 0;
    std::uint8_t note = 60;
    std::uint8_t velocity = 100;
};

// Everything that plays on a track, in absolute frames. Built on the project thread, immutable afterwards.
struct RegionPlayback {
    std::int64_t startFrame = 0;  // [startFrame, endFrame)
    std::int64_t endFrame = 0;
    const IFrameSource* source = nullptr;  // audio regions; kept alive by TrackConfig::keepAlive
    std::int64_t sourceOffsetFrames = 0;
    float gain = 1.0f;
    std::int64_t fadeInFrames = 0, fadeOutFrames = 0;  // ramps (a quarter of a sine) at the start and the end
    std::int64_t loopFrames = 0;  // audio: > 0 repeats the first loopFrames of the region until its end
    std::vector<NoteSpan> notes;  // MIDI regions, sorted by onFrame
    std::vector<ControlSpan> controls;  // MIDI regions, sorted by frame
};

struct SendPlayback {
    Uuid target;
    float gain = 1.0f;
    bool preFader = false;
    DelayLine delay;  // plug-in delay compensation
};

// Automation of the fader and the pan: a value (linear gain, or -1..1) from a frame on, linear in between; before the first
// point the first value holds, after the last one the last.
struct AutoPoint {
    std::int64_t frame = 0;
    float value = 0.0f;
};

struct TrackConfig {
    SynthParams synthParams;            // an instrument track: how its built-in synth sounds
    std::shared_ptr<IInstrument> instrument;  // a plug-in instrument (the notes go to it); null: the built-in synth sounds
    std::vector<AutoPoint> volumeAuto;  // linear gain
    std::vector<AutoPoint> panAuto;
    std::vector<RegionPlayback> regions;
    std::vector<std::unique_ptr<IProcessor>> inserts;
    std::vector<SendPlayback> sends;
    Uuid output;  // null = master
    DelayLine outputDelay;  // plug-in delay compensation of the output edge
    std::vector<std::shared_ptr<IFrameSource>> keepAlive;
};

struct TrackNode {
    TrackNode(Uuid id, TrackKind kind, const StripParams& strip, TrackConfig* config, double sampleRate);
    ~TrackNode() { delete config; }
    TrackNode(const TrackNode&) = delete;
    TrackNode& operator=(const TrackNode&) = delete;

    Uuid id;
    TrackKind kind;
    StripParams strip;
    TrackConfig* config;  // owned; replaced through SetConfig messages
    std::vector<float> l, r;
    Synth synth;
    // A plug-in instrument (config->instrument): the notes held, what to send when the music stops, live notes waiting for the next block, and how
    // long the stopped graph keeps rendering so that tails ring out.
    std::uint64_t held[2] = {0, 0};
    bool stopPending = false;
    int tailFrames = 0, tailMax = 0;
    MidiEvent live[64];
    int liveCount = 0;
    int monitorL = -1, monitorR = -1;  // the input channels heard on this track (-1: none)
    float smoothL = 1.0f;
    float blockPeak = 0.0f;  // after the fader, this block
    float blockPeakPre = 0.0f;  // before the fader (after the inserts), this block
    float blockReduction = 0.0f;  // the deepest gain reduction of the inserts, this block (dB)
    float smoothR = 1.0f;
    bool smoothInit = false;
};

// Owned by the audio thread. apply() and render() must be called from the same thread (or from tests
// while no audio thread is running).
class RenderGraph {
public:
    explicit RenderGraph(double sampleRate);
    ~RenderGraph();
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    // Returns an object the caller must send back to the project thread for destruction (ptr is null if none).
    Owned apply(const AudioMsg& m) noexcept;
    // withRegions = false renders only what is live (notes played now, monitored input): the transport is stopped
    void render(std::int64_t blockStart, int frames, float* outL, float* outR, bool withRegions = true) noexcept;
    // A note played live on an instrument track (the synth keeps it until the note-off); false when there is no such track.
    bool liveNote(const Uuid& track, bool on, std::uint8_t note, std::uint8_t velocity) noexcept;
    // A controller, aftertouch or pitch bend message played live: a plug-in instrument gets it, the built-in synth ignores it.
    bool liveControl(const Uuid& track, std::uint8_t status, std::uint8_t data1, std::uint8_t data2) noexcept;
    // true while something needs the graph with the transport stopped: a sounding voice or a monitored input
    bool liveNeeded() const noexcept;
    void allNotesOff() noexcept;
    float masterPeak() const noexcept { return masterPeak_; }
    // The input of the block (one pointer per channel, already moved to the first frame that render() will play); tracks that are
    // monitored add it to their signal.
    void setInput(const float* const* channels, int numChannels) noexcept { input_ = channels; inputChannels_ = numChannels; }
    // The post-fader peak of every track since the last call (linear), for the meters; any thread. Reading resets them.
    void takeTrackPeaks(std::vector<std::pair<Uuid, float>>& out) noexcept;
    // The same before the fader (Pre-Fader Metering).
    void takeTrackPeaksPre(std::vector<std::pair<Uuid, float>>& out) noexcept;
    // The deepest gain reduction (dB) of each track's inserts since the last call; reading resets it.
    void takeTrackReductions(std::vector<std::pair<Uuid, float>>& out) noexcept;
    int trackCount() const noexcept { return count_; }
    nlohmann::json describe() const;  // not real-time; tracks in processing order

private:
    TrackNode* find(const Uuid& id) const noexcept;
    void processNode(TrackNode& t, std::int64_t blockStart, int n, bool anySolo, bool withRegions) noexcept;
    void renderAudio(TrackNode& t, const TrackConfig& cfg, std::int64_t blockStart, int n) noexcept;
    void renderInstrument(TrackNode& t, const TrackConfig& cfg, std::int64_t blockStart, int n) noexcept;
    void renderPluginInstrument(TrackNode& t, const TrackConfig& cfg, std::int64_t blockStart, int n, bool withRegions) noexcept;
    static void deleteNode(void* p);
    static void deleteConfig(void* p);

    double sampleRate_;
    std::array<TrackNode*, kMaxTracks> nodes_{};
    std::array<TrackNode*, kMaxTracks> tmp_{};
    int count_ = 0;
    TrackNode* master_ = nullptr;
    std::vector<float> scratchL_, scratchR_, preL_, preR_, dlyL_, dlyR_;
    float masterPeak_ = 0.0f;
    const float* const* input_ = nullptr;
    int inputChannels_ = 0;
    // written by the audio thread, read and cleared by the UI thread; a torn read only shifts a meter for one frame
    std::array<std::atomic<std::uint64_t>, kMaxTracks> peakHi_{}, peakLo_{};
    std::array<std::atomic<float>, kMaxTracks> peakVal_{};
    std::array<std::atomic<float>, kMaxTracks> reductionVal_{};
    std::array<std::atomic<float>, kMaxTracks> peakValPre_{};
    std::atomic<int> peakCount_{0};
};

}  // namespace lpc::audio
