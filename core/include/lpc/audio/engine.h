#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

#include <nlohmann/json.hpp>

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

    // ---- audio thread
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
