#include "lpc/audio/engine.h"

#include <algorithm>

namespace lpc::audio {

AudioEngine::AudioEngine(double sampleRate) : sampleRate_(sampleRate), graph_(sampleRate) {}

bool AudioEngine::postMessage(const AudioMsg& m) { return messages_.push(m); }

std::size_t AudioEngine::collectGarbage() {
    std::size_t n = 0;
    Feedback f;
    while (feedback_.pop(f)) {
        f.garbage.destroy();
        ++n;
    }
    return n;
}

void AudioEngine::applyDirect(const AudioMsg& m) {
    handle(m);
    collectGarbage();
}

void AudioEngine::handle(const AudioMsg& m) noexcept {
    switch (m.kind) {
        case MsgKind::Play:
            playing_ = true;
            playingPub_.store(true, std::memory_order_relaxed);
            break;
        case MsgKind::Stop:
            playing_ = false;
            playingPub_.store(false, std::memory_order_relaxed);
            graph_.allNotesOff();
            break;
        case MsgKind::Locate:
            position_ = std::max<std::int64_t>(m.frame, 0);
            positionPub_.store(position_, std::memory_order_relaxed);
            graph_.allNotesOff();
            break;
        case MsgKind::SetLoop:
            loopStart_ = std::max<std::int64_t>(m.frame, 0);
            loopEnd_ = m.frame2;
            break;
        default: {
            Owned garbage = graph_.apply(m);
            if (garbage.ptr && !feedback_.push(Feedback{garbage})) garbageOverflow_.fetch_add(1, std::memory_order_relaxed);  // leaked, counted
            break;
        }
    }
    appliedSeq_.store(m.seq, std::memory_order_release);
}

void AudioEngine::drain() noexcept {
    // Stops early when the feedback queue is full (nothing may be leaked) or after a fixed budget;
    // what is left stays queued for the next block.
    AudioMsg m;
    for (int i = 0; i < kMaxMessagesPerBlock && !feedback_.full() && messages_.pop(m); ++i) handle(m);
}

void AudioEngine::processBlock(float* outL, float* outR, int frames) noexcept {
    drain();
    int done = 0;
    while (done < frames) {
        int n = std::min(frames - done, kMaxBlock);
        if (playing_) {
            if (loopEnd_ > loopStart_) {
                if (position_ >= loopEnd_) {  // wrap exactly at the loop end
                    position_ = loopStart_;
                    graph_.allNotesOff();
                }
                n = static_cast<int>(std::min<std::int64_t>(n, loopEnd_ - position_));
            }
            graph_.render(position_, n, outL + done, outR + done);
            position_ += n;
        } else {
            std::fill_n(outL + done, n, 0.0f);
            std::fill_n(outR + done, n, 0.0f);
        }
        done += n;
    }
    positionPub_.store(position_, std::memory_order_relaxed);
    masterPeakPub_.store(graph_.masterPeak(), std::memory_order_relaxed);
}

}  // namespace lpc::audio
