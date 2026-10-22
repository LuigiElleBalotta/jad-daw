#pragma once
// The spectrum of one insert for the Analyzer of an open plug-in window: the audio thread copies the (mono) samples going into or coming out of the chosen insert
// into a ring, the UI thread takes the last 4096 of them and turns them into band levels.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "lpc/uuid.h"

namespace lpc::audio {

class SpectrumTap {
public:
    static constexpr std::size_t kSize = 16384;  // a power of two
    static constexpr std::size_t kWindow = 4096;

    void arm(const Uuid& track, int insert, bool post) noexcept;
    void disarm() noexcept { armed_.store(false, std::memory_order_release); }
    // audio thread
    bool wants(const Uuid& track, int insert, bool post) const noexcept {
        return armed_.load(std::memory_order_acquire) && insert == insert_.load(std::memory_order_relaxed) && post == post_.load(std::memory_order_relaxed) &&
               track.hi == hi_.load(std::memory_order_relaxed) && track.lo == lo_.load(std::memory_order_relaxed);
    }
    void push(const float* l, const float* r, int frames) noexcept;
    // UI thread: the last kWindow samples, false until that many have come in
    bool latest(std::vector<float>& out) const;
    // UI thread: the samples of both channels that came in since `cursor` (start at 0; it is moved on). If more than the ring holds came in, the oldest are lost.
    // Returns false when nothing is new.
    bool take(std::uint64_t& cursor, std::vector<float>& l, std::vector<float>& r) const;

private:
    std::atomic<bool> armed_{false}, post_{true};
    std::atomic<int> insert_{-1};
    std::atomic<std::uint64_t> hi_{0}, lo_{0};
    std::unique_ptr<std::atomic<float>[]> ring_{new std::atomic<float>[kSize]()};  // on the heap: a RenderGraph lives on the stack in tests
    std::unique_ptr<std::atomic<float>[]> ringL_{new std::atomic<float>[kSize]()}, ringR_{new std::atomic<float>[kSize]()};
    std::atomic<std::uint64_t> written_{0};
};

// `bands` levels in dB, log-spaced from fMin to fMax, of the Hann-windowed power spectrum of `x` (a power-of-two length); about -120 for silence
std::vector<float> spectrumBands(const std::vector<float>& x, double sampleRate, int bands, double fMin = 20.0, double fMax = 20000.0);

}  // namespace lpc::audio
