#include "lpc/audio/engine.h"

#include <algorithm>
#include <cmath>

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
        case MsgKind::SetClickKit: {
            if (m.obj.ptr) {
                for (ClickVoice& c : voices_) c = ClickVoice{};  // no sound may point into the old kit
                Owned old = makeOwned(kit_);
                kit_ = static_cast<ClickKit*>(m.obj.ptr);
                if (old.ptr && !feedback_.push(Feedback{old})) garbageOverflow_.fetch_add(1, std::memory_order_relaxed);
            }
            break;
        }
        case MsgKind::SetClick: {
            clickOn_ = m.frame != 0;
            if (m.obj.ptr) {
                Owned old = makeOwned(click_);
                click_ = static_cast<ClickTrack*>(m.obj.ptr);
                if (old.ptr && !feedback_.push(Feedback{old})) garbageOverflow_.fetch_add(1, std::memory_order_relaxed);
            }
            break;
        }
        default: {
            Owned garbage = graph_.apply(m);
            if (garbage.ptr && !feedback_.push(Feedback{garbage})) garbageOverflow_.fetch_add(1, std::memory_order_relaxed);  // leaked, counted
            break;
        }
    }
    appliedSeq_.store(m.seq, std::memory_order_release);
}

// Every beat starts a sound: the sample of its slot, or a short sine blip (higher on the first beat of a bar, quieter on
// subdivisions) when the slot has none. Sounds overlap, up to a few at once, and are added to the master output.
void AudioEngine::mixClick(float* outL, float* outR, std::int64_t from, int n) noexcept {
    const auto& frames = click_->frames;
    std::size_t next = static_cast<std::size_t>(std::lower_bound(frames.begin(), frames.end(), from) - frames.begin());
    constexpr float kTwoPi = 6.2831853f;
    const int blipLength = static_cast<int>(sampleRate_ * 0.03);
    for (int i = 0; i < n; ++i) {
        while (next < frames.size() && frames[next] == from + i) {  // a beat starts here
            const int slot = next < click_->slot.size() ? click_->slot[next] : 0;
            ClickVoice* v = nullptr;
            for (ClickVoice& c : voices_)
                if (c.pos >= c.length) { v = &c; break; }
            if (!v) v = &voices_[0];  // all busy: the oldest sound is cut
            const bool sub = slot >= kSlotE && slot <= kSlotLi;
            if (kit_ && slot > 0 && slot < kClickSlots && !kit_->sample[static_cast<std::size_t>(slot)].empty()) {
                v->sample = &kit_->sample[static_cast<std::size_t>(slot)];
                v->length = static_cast<int>(v->sample->size());
                v->gain = kit_->gain[static_cast<std::size_t>(slot)];
            } else {
                v->sample = nullptr;
                v->length = blipLength;
                v->freq = click_->accent[next] ? 1600.0f : (sub ? 1300.0f : 1000.0f);
                v->gain = sub ? 0.15f : 0.35f;
            }
            v->pos = 0;
            ++next;
        }
        float sum = 0.0f;
        for (ClickVoice& c : voices_) {
            if (c.pos >= c.length) continue;
            if (c.sample) {
                sum += c.gain * (*c.sample)[static_cast<std::size_t>(c.pos)];
            } else {
                const float env = static_cast<float>(c.length - c.pos) / static_cast<float>(c.length);
                sum += c.gain * env * std::sin(kTwoPi * c.freq * static_cast<float>(c.pos) / static_cast<float>(sampleRate_));
            }
            ++c.pos;
        }
        outL[i] += sum;
        outR[i] += sum;
    }
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
            if (clickOn_ && click_) mixClick(outL + done, outR + done, position_, n);
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
