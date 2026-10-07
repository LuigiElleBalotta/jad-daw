#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

#include "lpc/wav.h"

namespace lpc::audio {

// Read-only audio media as the audio thread sees it.
class IFrameSource {
public:
    virtual ~IFrameSource() = default;
    virtual std::int64_t frames() const = 0;
    virtual int sampleRate() const = 0;
    virtual int channels() const = 0;
    // Real-time safe. Writes `n` frames starting at `frame` into l and r (mono is duplicated).
    // Frames outside [0, frames()) are zero. Returns false when the data was not ready (underrun);
    // the missing part is then silence.
    virtual bool read(std::int64_t frame, float* l, float* r, int n) const noexcept = 0;
};

// Whole file in memory. Used by offline rendering and tests (always deterministic).
class MemorySource final : public IFrameSource {
public:
    MemorySource(int sampleRate, int channels, const std::vector<float>& interleaved);
    std::int64_t frames() const override { return frames_; }
    int sampleRate() const override { return sampleRate_; }
    int channels() const override { return channels_; }
    bool read(std::int64_t frame, float* l, float* r, int n) const noexcept override;

private:
    int sampleRate_, channels_;
    std::int64_t frames_;
    std::vector<float> l_, r_;
};

// Streams a WAV file from disk. A background thread keeps a window of chunks loaded ahead of the last
// position the audio thread asked for; the audio thread only copies from loaded chunks and never waits.
// Each slot has an atomic tag (the chunk number it holds, -1 while being rewritten); the reader checks the
// tag again after copying and treats a change as an underrun (seqlock pattern).
class StreamingSource final : public IFrameSource {
public:
    static constexpr int kChunkFrames = 16384;
    static constexpr int kSlots = 16;  // the reader keeps kSlots - 1 chunks ahead of the playhead

    explicit StreamingSource(const std::filesystem::path& path, bool startReaderThread = true);
    ~StreamingSource() override;
    StreamingSource(const StreamingSource&) = delete;
    StreamingSource& operator=(const StreamingSource&) = delete;

    std::int64_t frames() const override { return frames_; }
    int sampleRate() const override { return sampleRate_; }
    int channels() const override { return channels_; }
    bool read(std::int64_t frame, float* l, float* r, int n) const noexcept override;

    int takeUnderruns() const noexcept;  // returns the count since the last call and resets it
    void pumpOnce();                     // one reader pass; the reader thread calls this in a loop

private:
    struct Slot {
        std::atomic<std::int64_t> tag{-1};
        std::vector<float> data;  // kChunkFrames of L followed by kChunkFrames of R
    };

    WavFile file_;
    int sampleRate_, channels_;
    std::int64_t frames_;
    std::unique_ptr<Slot[]> slots_;
    std::vector<float> scratch_;
    mutable std::atomic<std::int64_t> lastRead_{0};
    mutable std::atomic<int> underruns_{0};
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

}  // namespace lpc::audio
