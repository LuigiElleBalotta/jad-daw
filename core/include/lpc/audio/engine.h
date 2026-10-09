#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

#include <nlohmann/json.hpp>

#include "lpc/audio/click.h"
#include "lpc/audio/messages.h"
#include "lpc/audio/render_graph.h"
#include "lpc/audio/spsc_queue.h"

namespace lpc::audio {

inline constexpr std::size_t kMessageQueueCapacity = 8192;
inline constexpr std::size_t kFeedbackQueueCapacity = 4096;
inline constexpr int kMaxMessagesPerBlock = 1024;  // bounds the work one audio callback does for the project thread

class AudioEngine {
public:
    explicit AudioEngine(double sampleRate);
    ~AudioEngine();  // frees the click objects still held (the audio thread is stopped by then)
    double sampleRate() const { return sampleRate_; }

    // ---- project thread
    bool postMessage(const AudioMsg& m);  // false when full; the caller retries, nothing is dropped
    std::size_t collectGarbage();         // destroys objects handed back by the audio thread
    void applyDirect(const AudioMsg& m);  // only while no audio thread is running (offline rendering, tests)

    // ---- any thread
    std::uint64_t appliedSeq() const { return appliedSeq_.load(std::memory_order_acquire); }
    std::int64_t positionFrames() const { return positionPub_.load(std::memory_order_relaxed); }
    bool playing() const { return playingPub_.load(std::memory_order_relaxed); }
    // the post-fader peak of every track since the last call (linear); for the UI thread only
    void takeTrackPeaks(std::vector<std::pair<Uuid, float>>& out) { graph_.takeTrackPeaks(out); }
    float masterPeak() const { return masterPeakPub_.load(std::memory_order_relaxed); }
    std::size_t pendingMessages() const { return messages_.sizeApprox(); }
    std::uint64_t garbageOverflow() const { return garbageOverflow_.load(std::memory_order_relaxed); }

    // ---- recording: the input of the playing blocks is captured in chunks, in order; read them from one thread
    static constexpr int kMaxInputs = 8;
    struct RecChunk {
        float ch[kMaxInputs][kMaxBlock];
        int channels = 0;
        int frames = 0;
        std::int64_t position = 0;  // the project position (in frames) of the first frame
    };
    bool recording() const { return recordingPub_.load(std::memory_order_relaxed); }
    bool takeRecorded(RecChunk& out) { return rec_.pop(out); }
    // the input level of a channel since the last call (linear peak); any thread, reading resets it
    float takeInputPeak(int channel) { return channel >= 0 && channel < kMaxInputs ? inPeak_[channel].exchange(0.0f, std::memory_order_relaxed) : 0.0f; }
    int inputChannelCount() const { return inChannelsPub_.load(std::memory_order_relaxed); }
    std::uint64_t recordedDropped() const { return recDropped_.load(std::memory_order_relaxed); }  // chunks lost because nobody read them

    // ---- audio thread
    void input(const float* const* channels, int numChannels, int frames) noexcept;  // the input for the next processBlock
    void processBlock(float* outL, float* outR, int frames) noexcept;

    // ---- tests: only while the audio thread is stopped
    nlohmann::json describeForTest() const { return graph_.describe(); }

private:
    void drain() noexcept;
    void handle(const AudioMsg& m) noexcept;

    double sampleRate_;
    RenderGraph graph_;
    SpscQueue<AudioMsg, kMessageQueueCapacity> messages_;
    SpscQueue<Feedback, kFeedbackQueueCapacity> feedback_;

    // owned by the audio thread
    bool playing_ = false;
    ClickTrack* click_ = nullptr;  // the metronome's beats; replaced through SetClick, the old one goes back as garbage
    SpscQueue<RecChunk, 512> rec_;
    std::vector<float> inBuf_[kMaxInputs];
    const float* inPtr_[kMaxInputs] = {};
    int inFrames_ = 0, inChannels_ = 0;
    std::atomic<float> inPeak_[kMaxInputs];
    bool recording_ = false;
    std::int64_t countLeft_ = 0, countPos_ = 0;  // the count-in before a recording: frames left, frames played
    ClickTrack* countClick_ = nullptr;
    std::atomic<bool> recordingPub_{false};
    std::atomic<int> inChannelsPub_{0};
    std::atomic<std::uint64_t> recDropped_{0};
    void mixClickTrack(const ClickTrack* track, float* outL, float* outR, std::int64_t from, int n) noexcept;
    void capture(int offset, int n) noexcept;
    ClickKit* kit_ = nullptr;      // the samples of the click sounds (SetClickKit)
    bool clickOn_ = false;
    struct ClickVoice {            // a sound being played: a sample, or the built-in blip when sample is null
        const std::vector<float>* sample = nullptr;
        int pos = 0, length = 0;
        float freq = 1000.0f, gain = 0.0f;
    };
    static constexpr int kClickVoices = 6;
    ClickVoice voices_[kClickVoices];
    void mixClick(float* outL, float* outR, std::int64_t from, int n) noexcept;
    std::int64_t position_ = 0;
    std::int64_t loopStart_ = 0;
    std::int64_t loopEnd_ = 0;  // loop is on when loopEnd_ > loopStart_

    // published for other threads
    std::atomic<std::uint64_t> appliedSeq_{0};
    std::atomic<std::int64_t> positionPub_{0};
    std::atomic<bool> playingPub_{false};
    std::atomic<float> masterPeakPub_{0.0f};
    std::atomic<std::uint64_t> garbageOverflow_{0};
};

}  // namespace lpc::audio
