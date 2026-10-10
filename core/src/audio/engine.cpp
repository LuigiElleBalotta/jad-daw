#include "lpc/audio/engine.h"

#include <algorithm>
#include <cmath>

namespace lpc::audio {

AudioEngine::AudioEngine(double sampleRate) : sampleRate_(sampleRate), graph_(sampleRate) {
    for (auto& b : inBuf_) b.assign(8192, 0.0f);
    for (auto& p : inPeak_) p.store(0.0f, std::memory_order_relaxed);
}

AudioEngine::~AudioEngine() {
    delete click_;
    delete kit_;
    delete countClick_;
}

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
            recording_ = false;
            countLeft_ = 0;
            recordingPub_.store(false, std::memory_order_relaxed);
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
        case MsgKind::StopRecord:
            recording_ = false;
            countLeft_ = 0;
            recordingPub_.store(false, std::memory_order_relaxed);
            break;
        case MsgKind::SetMonitor:
            graph_.apply(m);
            break;
        case MsgKind::SetLiveTarget:
            if (!liveTarget_.isNull()) graph_.liveNote(liveTarget_, false, 0, 0);  // nothing keeps ringing on the old target
            liveTarget_ = m.track;
            break;
        case MsgKind::StartRecord: {
            Owned old = makeOwned(countClick_);
            countClick_ = static_cast<ClickTrack*>(m.obj.ptr);
            if (old.ptr && !feedback_.push(Feedback{old})) garbageOverflow_.fetch_add(1, std::memory_order_relaxed);
            recording_ = true;
            countLeft_ = std::max<std::int64_t>(m.frame, 0);
            countPos_ = 0;
            playing_ = playing_ || countLeft_ == 0;  // while the transport already runs (a punch-in) there is no count-in
            if (playing_) countLeft_ = 0;
            recordingPub_.store(true, std::memory_order_relaxed);
            playingPub_.store(true, std::memory_order_relaxed);
            break;
        }
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
void AudioEngine::mixClick(float* outL, float* outR, std::int64_t from, int n) noexcept { mixClickTrack(click_, outL, outR, from, n); }

void AudioEngine::mixClickTrack(const ClickTrack* track, float* outL, float* outR, std::int64_t from, int n) noexcept {
    const auto& frames = track->frames;
    std::size_t next = static_cast<std::size_t>(std::lower_bound(frames.begin(), frames.end(), from) - frames.begin());
    constexpr float kTwoPi = 6.2831853f;
    const int blipLength = static_cast<int>(sampleRate_ * 0.03);
    for (int i = 0; i < n; ++i) {
        while (next < frames.size() && frames[next] == from + i) {  // a beat starts here
            const int slot = next < track->slot.size() ? track->slot[next] : 0;
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
                v->freq = track->accent[next] ? 1600.0f : (sub ? 1300.0f : 1000.0f);
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

// The input that arrived with this block, kept for the frames processBlock is about to play; the levels are measured here.
void AudioEngine::input(const float* const* channels, int numChannels, int frames) noexcept {
    inChannels_ = std::min(numChannels, kMaxInputs);
    inFrames_ = std::min(frames, static_cast<int>(inBuf_[0].size()));
    for (int c = 0; c < inChannels_; ++c) {
        float peak = 0.0f;
        for (int i = 0; i < inFrames_; ++i) {
            const float v = channels[c] ? channels[c][i] : 0.0f;
            inBuf_[c][static_cast<std::size_t>(i)] = v;
            peak = std::max(peak, std::abs(v));
        }
        if (peak > inPeak_[c].load(std::memory_order_relaxed)) inPeak_[c].store(peak, std::memory_order_relaxed);
    }
    inChannelsPub_.store(inChannels_, std::memory_order_relaxed);
}

// Puts the input of frames [offset, offset + n) of this block into the recording queue (silence when the device gave none).
void AudioEngine::capture(int offset, int n) noexcept {
    RecChunk c;
    c.channels = std::max(inChannels_, 1);
    c.frames = n;
    c.position = position_;
    for (int ch = 0; ch < c.channels; ++ch)
        for (int i = 0; i < n; ++i) c.ch[ch][i] = ch < inChannels_ && offset + i < inFrames_ ? inBuf_[ch][static_cast<std::size_t>(offset + i)] : 0.0f;
    if (!rec_.push(c)) recDropped_.fetch_add(1, std::memory_order_relaxed);
}

// The MIDI that arrived since the last block: notes go to the live target and, while a take runs, are kept with their position.
void AudioEngine::drainMidi() noexcept {
    auto handle = [this](const MidiEvent& e) {
        const std::uint8_t type = e.status & 0xF0;
        const bool on = type == 0x90 && e.data2 > 0;
        const bool off = type == 0x80 || (type == 0x90 && e.data2 == 0);
        if (on || off) {
            if (!liveTarget_.isNull()) graph_.liveNote(liveTarget_, on, e.data1, e.data2);
        } else if (type == 0xB0 && (e.data1 == 123 || e.data1 == 120)) {  // all notes off
            if (!liveTarget_.isNull()) graph_.allNotesOff();
        } else if (type == 0xB0 || type == 0xD0 || type == 0xE0) {  // controllers, aftertouch and the pitch wheel go to a plug-in instrument
            if (!liveTarget_.isNull()) graph_.liveControl(liveTarget_, type, e.data1, e.data2);
        }
        if (recording_ && playing_ && (on || off || type == 0xB0 || type == 0xE0)) midiRec_.push(MidiRecEvent{position_, e});
    };
    MidiEvent e;
    while (midiDevice_.pop(e)) handle(e);
    while (midiUi_.pop(e)) handle(e);
}

void AudioEngine::drain() noexcept {
    // Stops early when the feedback queue is full (nothing may be leaked) or after a fixed budget;
    // what is left stays queued for the next block.
    AudioMsg m;
    for (int i = 0; i < kMaxMessagesPerBlock && !feedback_.full() && messages_.pop(m); ++i) handle(m);
}

void AudioEngine::processBlock(float* outL, float* outR, int frames) noexcept {
    drain();
    drainMidi();
    int done = 0;
    while (done < frames) {
        int n = std::min(frames - done, kMaxBlock);
        if (countLeft_ > 0) {  // the count-in: only the click, the transport waits
            n = static_cast<int>(std::min<std::int64_t>(n, countLeft_));
            std::fill_n(outL + done, n, 0.0f);
            std::fill_n(outR + done, n, 0.0f);
            if (countClick_) mixClickTrack(countClick_, outL + done, outR + done, countPos_, n);
            countPos_ += n;
            countLeft_ -= n;
            if (countLeft_ == 0) playing_ = true;
            done += n;
            continue;
        }
        if (playing_) {
            if (loopEnd_ > loopStart_) {
                if (position_ >= loopEnd_) {  // wrap exactly at the loop end
                    position_ = loopStart_;
                    graph_.allNotesOff();
                }
                n = static_cast<int>(std::min<std::int64_t>(n, loopEnd_ - position_));
            }
            for (int c = 0; c < inChannels_; ++c) inPtr_[c] = inBuf_[c].data() + std::min(done, static_cast<int>(inBuf_[c].size()));
            graph_.setInput(inPtr_, inChannels_);
            graph_.render(position_, n, outL + done, outR + done);
            if (clickOn_ && click_) mixClick(outL + done, outR + done, position_, n);
            if (recording_) capture(done, n);
            position_ += n;
        } else if (graph_.liveNeeded()) {  // stopped, but notes ring or an input is monitored
            for (int c = 0; c < inChannels_; ++c) inPtr_[c] = inBuf_[c].data() + std::min(done, static_cast<int>(inBuf_[c].size()));
            graph_.setInput(inPtr_, inChannels_);
            graph_.render(position_, n, outL + done, outR + done, false);
        } else {
            std::fill_n(outL + done, n, 0.0f);
            std::fill_n(outR + done, n, 0.0f);
        }
        done += n;
    }
    inFrames_ = 0;
    inChannels_ = 0;
    positionPub_.store(position_, std::memory_order_relaxed);
    masterPeakPub_.store(graph_.masterPeak(), std::memory_order_relaxed);
}

}  // namespace lpc::audio
