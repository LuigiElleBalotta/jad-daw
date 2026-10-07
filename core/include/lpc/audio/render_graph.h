#pragma once
#include <array>
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
    std::vector<NoteSpan> notes;  // MIDI regions, sorted by onFrame
};

struct SendPlayback {
    Uuid target;
    float gain = 1.0f;
    bool preFader = false;
};

struct TrackConfig {
    std::vector<RegionPlayback> regions;
    std::vector<std::unique_ptr<IProcessor>> inserts;
    std::vector<SendPlayback> sends;
    Uuid output;  // null = master
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
    SineSynth synth;
    float smoothL = 1.0f;
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
    void render(std::int64_t blockStart, int frames, float* outL, float* outR) noexcept;
    void allNotesOff() noexcept;
    float masterPeak() const noexcept { return masterPeak_; }
    int trackCount() const noexcept { return count_; }
    nlohmann::json describe() const;  // not real-time; tracks in processing order

private:
    TrackNode* find(const Uuid& id) const noexcept;
    void processNode(TrackNode& t, std::int64_t blockStart, int n, bool anySolo) noexcept;
    void renderAudio(TrackNode& t, const TrackConfig& cfg, std::int64_t blockStart, int n) noexcept;
    void renderInstrument(TrackNode& t, const TrackConfig& cfg, std::int64_t blockStart, int n) noexcept;
    static void deleteNode(void* p);
    static void deleteConfig(void* p);

    double sampleRate_;
    std::array<TrackNode*, kMaxTracks> nodes_{};
    std::array<TrackNode*, kMaxTracks> tmp_{};
    int count_ = 0;
    TrackNode* master_ = nullptr;
    std::vector<float> scratchL_, scratchR_, preL_, preR_;
    float masterPeak_ = 0.0f;
};

}  // namespace lpc::audio
