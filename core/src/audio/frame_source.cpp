#include "lpc/audio/frame_source.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

namespace lpc::audio {

// ------------------------------------------------------------------ MemorySource

MemorySource::MemorySource(int sampleRate, int channels, const std::vector<float>& interleaved)
    : sampleRate_(sampleRate), channels_(channels), frames_(channels > 0 ? static_cast<std::int64_t>(interleaved.size()) / channels : 0) {
    l_.resize(static_cast<std::size_t>(frames_));
    r_.resize(static_cast<std::size_t>(frames_));
    for (std::int64_t i = 0; i < frames_; ++i) {
        l_[static_cast<std::size_t>(i)] = interleaved[static_cast<std::size_t>(i * channels)];
        r_[static_cast<std::size_t>(i)] = channels > 1 ? interleaved[static_cast<std::size_t>(i * channels + 1)] : l_[static_cast<std::size_t>(i)];
    }
}

bool MemorySource::read(std::int64_t frame, float* l, float* r, int n) const noexcept {
    std::fill(l, l + n, 0.0f);
    std::fill(r, r + n, 0.0f);
    const std::int64_t from = std::max<std::int64_t>(frame, 0);
    const std::int64_t to = std::min<std::int64_t>(frame + n, frames_);
    if (from < to) {
        const std::int64_t count = to - from;
        std::copy_n(l_.data() + from, count, l + (from - frame));
        std::copy_n(r_.data() + from, count, r + (from - frame));
    }
    return true;
}

// ------------------------------------------------------------------ StreamingSource

StreamingSource::StreamingSource(const std::filesystem::path& path, bool startReaderThread)
    : file_(path),
      sampleRate_(file_.sampleRate()),
      channels_(file_.channels()),
      frames_(file_.frames()),
      slots_(new Slot[kSlots]),
      scratch_(static_cast<std::size_t>(kChunkFrames) * static_cast<std::size_t>(channels_)) {
    for (int i = 0; i < kSlots; ++i) slots_[i].data.assign(static_cast<std::size_t>(kChunkFrames) * 2, 0.0f);
    for (auto& c : cursors_) c.store(-1, std::memory_order_relaxed);
    cursors_[0].store(0, std::memory_order_relaxed);
    pumpOnce();  // the first chunks are ready before the audio thread can ask for them
    if (startReaderThread) {
        thread_ = std::thread([this] {
            while (!stop_.load(std::memory_order_acquire)) {
                pumpOnce();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
}

StreamingSource::~StreamingSource() {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
}

void StreamingSource::pumpOnce() {
    if (frames_ == 0) return;
    const std::int64_t lastChunk = (frames_ - 1) / kChunkFrames;
    for (const auto& cursor : cursors_) {
        const std::int64_t position = cursor.load(std::memory_order_relaxed);
        if (position < 0) continue;
        const std::int64_t current = position / kChunkFrames;
        for (std::int64_t c = current; c <= std::min<std::int64_t>(current + kWindow, lastChunk); ++c) {
            Slot& s = slots_[c % kSlots];
            if (s.tag.load(std::memory_order_acquire) == c) continue;
            s.tag.store(-1, std::memory_order_relaxed);  // invalidate before overwriting...
            std::atomic_thread_fence(std::memory_order_release);  // ...and make that visible before any data write
            file_.readFrames(c * kChunkFrames, kChunkFrames, scratch_.data());
            float* dl = s.data.data();
            float* dr = s.data.data() + kChunkFrames;
            for (int i = 0; i < kChunkFrames; ++i) {
                dl[i] = scratch_[static_cast<std::size_t>(i) * channels_];
                dr[i] = channels_ > 1 ? scratch_[static_cast<std::size_t>(i) * channels_ + 1] : dl[i];
            }
            s.tag.store(c, std::memory_order_release);
        }
    }
}

bool StreamingSource::read(std::int64_t frame, float* l, float* r, int n) const noexcept {
    {  // follow this playback position: continue the cursor that expected it, or replace the oldest one
        int slot = -1;
        for (int i = 0; i < kCursors && slot < 0; ++i) {
            const std::int64_t expected = cursors_[static_cast<std::size_t>(i)].load(std::memory_order_relaxed);
            if (expected >= 0 && std::abs(expected - frame) <= kChunkFrames) slot = i;
        }
        if (slot < 0) {
            slot = nextVictim_;
            nextVictim_ = (nextVictim_ + 1) % kCursors;
        }
        cursors_[static_cast<std::size_t>(slot)].store(std::max<std::int64_t>(frame, 0) + n, std::memory_order_relaxed);
    }
    std::fill(l, l + n, 0.0f);
    std::fill(r, r + n, 0.0f);
    bool ok = true;
    std::int64_t pos = std::max<std::int64_t>(frame, 0);
    const std::int64_t end = std::min<std::int64_t>(frame + n, frames_);
    while (pos < end) {
        const std::int64_t chunk = pos / kChunkFrames;
        const int offset = static_cast<int>(pos % kChunkFrames);
        const int len = static_cast<int>(std::min<std::int64_t>(end - pos, kChunkFrames - offset));
        const Slot& s = slots_[chunk % kSlots];
        bool good = s.tag.load(std::memory_order_acquire) == chunk;
        if (good) {
            std::copy_n(s.data.data() + offset, len, l + (pos - frame));
            std::copy_n(s.data.data() + kChunkFrames + offset, len, r + (pos - frame));
            std::atomic_thread_fence(std::memory_order_acquire);  // the copy above must not move past the re-check
            good = s.tag.load(std::memory_order_relaxed) == chunk;  // the reader did not recycle the slot meanwhile
        }
        if (!good) {
            std::fill_n(l + (pos - frame), len, 0.0f);
            std::fill_n(r + (pos - frame), len, 0.0f);
            ok = false;
        }
        pos += len;
    }
    if (!ok) underruns_.fetch_add(1, std::memory_order_relaxed);
    return ok;
}

int StreamingSource::takeUnderruns() const noexcept { return underruns_.exchange(0, std::memory_order_relaxed); }

}  // namespace lpc::audio
