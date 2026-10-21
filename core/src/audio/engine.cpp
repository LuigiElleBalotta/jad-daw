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
    delete audition_;
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
        case MsgKind::SetLowLatency:
            graph_.apply(m);
            break;
        case MsgKind::SetMasterGain:
            masterGain_ = m.strip.gain;
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
            recNext_ = -1;
            recPass_ = 0;
            countLeft_ = std::max<std::int64_t>(m.frame, 0);
            countPos_ = 0;
            recFrom_ = m.frame2 > 0 ? position_ + m.frame2 : 0;  // a pre-roll: the project plays and the capture starts later
            playing_ = playing_ || countLeft_ == 0;  // while the transport already runs (a punch-in) there is no count-in
            if (playing_) countLeft_ = 0;
            recordingPub_.store(true, std::memory_order_relaxed);
            playingPub_.store(true, std::memory_order_relaxed);
            break;
        }
        case MsgKind::SetAudition: {
            Owned old = makeOwned(audition_);
            audition_ = static_cast<AuditionClip*>(m.obj.ptr);
            if (old.ptr && !feedback_.push(Feedback{old})) garbageOverflow_.fetch_add(1, std::memory_order_relaxed);
            auditionPos_ = std::max<std::int64_t>(0, m.frame);
            auditionLoop_ = m.frame2 != 0;
            if (!audition_) auditionPosPub_.store(-1, std::memory_order_relaxed);
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

namespace {

// Logic's own click ("Klopfgeist", MADSP CKnock*, decompiled): not a sample but a resonant filter that an impulse excites on every tick.
// The filter's frequency comes from the note of the accent level (Bar G5, Beat C5, Division C2 in Logic's defaults), its Q from the Tonality knob
// as Q = clamp(-1.39 reso^2 + 1.4, 0.01, 1.4) with reso = 1 - (1 - tonality)^3. The level crossfades a fixed level and the velocity.
constexpr float kKnockLevelViaVel = 1.0f, kKnockBaseLevel = 0.8f;

float knockQ(float tonality) {
    const float reso = 1.0f - (1.0f - tonality) * (1.0f - tonality) * (1.0f - tonality);
    return std::clamp(-1.39f * reso * reso + 1.4f, 0.01f, 1.4f);
}

}  // namespace

// Every beat starts a sound: the sample of its slot, or the Klopfgeist click (higher on the first beat of a bar, quieter on
// subdivisions) when the slot has none. Sounds overlap, up to a few at once, and are added to the master output.
// Metronome Settings > Options: the click while playing, while recording, or only in the count-in (which has its own click)
bool AudioEngine::clickAllowed() const noexcept {
    if (!kit_) return true;
    if (kit_->onlyCountIn) return false;
    return recording_ ? kit_->whileRecording : kit_->whilePlaying;
}

void AudioEngine::mixClick(float* outL, float* outR, std::int64_t from, int n) noexcept { mixClickTrack(click_, outL, outR, from, n); }

void AudioEngine::mixClickTrack(const ClickTrack* track, float* outL, float* outR, std::int64_t from, int n) noexcept {
    const auto& frames = track->frames;
    std::size_t next = static_cast<std::size_t>(std::lower_bound(frames.begin(), frames.end(), from) - frames.begin());
    constexpr float kTwoPi = 6.2831853f;
    const int blipLength = static_cast<int>(sampleRate_ * 0.03);
    static const ClickKit defaults;  // the Klopfgeist as Logic starts it, until the project sends its own
    const ClickKit& rows = kit_ ? *kit_ : defaults;
    for (int i = 0; i < n; ++i) {
        while (next < frames.size() && frames[next] == from + i) {  // a beat starts here
            const int slot = next < track->slot.size() ? track->slot[next] : 0;
            ClickVoice* v = nullptr;
            if (!rows.polyphonic)
                for (ClickVoice& c : voices_) c.pos = c.length;  // monophonic clicks: a new one cuts the one before
            for (ClickVoice& c : voices_)
                if (c.pos >= c.length) { v = &c; break; }
            if (!v) v = &voices_[0];  // all busy: the oldest sound is cut
            const int rowIndex = next < track->level.size() ? std::min<int>(track->level[next], kClickLevels - 1) : (track->accent[next] ? kLevelBar : kLevelBeat);
            if (kit_ && kit_->midiOut) {  // the MIDI Click: a note on the port, off after 50 ms
                const MidiClickLevel& m = kit_->midiLevels[static_cast<std::size_t>(rowIndex)];
                const unsigned char channel = static_cast<unsigned char>(std::clamp(m.channel, 1, 16) - 1), note = static_cast<unsigned char>(std::clamp(m.note, 0, 127));
                kit_->midiOut->send(static_cast<unsigned char>(0x90 | channel), note, static_cast<unsigned char>(std::clamp(m.velocity, 1, 127)));
                for (MidiOff& o : midiOffs_)
                    if (o.left <= 0) { o = MidiOff{static_cast<int>(sampleRate_ * 0.05), channel, note}; break; }
            }
            const KnockLevel& row = rows.levels[static_cast<std::size_t>(rowIndex)];
            if (kit_ && slot > 0 && slot < kClickSlots && !kit_->sample[static_cast<std::size_t>(slot)].empty()) {
                v->knock = false;
                v->sample = &kit_->sample[static_cast<std::size_t>(slot)];
                v->length = static_cast<int>(v->sample->size());
                v->gain = kit_->gain[static_cast<std::size_t>(slot)];
            } else {
                if (!row.on) {  // this level is switched off in Metronome Settings
                    ++next;
                    continue;
                }
                v->sample = nullptr;
                v->length = blipLength;
                const int note = std::clamp(row.note, 0, 127);
                const int velocity = std::clamp(row.velocity, 0, 127);
                const float freq = 440.0f * std::pow(2.0f, static_cast<float>(note - 69) / 12.0f);
                const float level = (1.0f - kKnockLevelViaVel) * kKnockBaseLevel + kKnockLevelViaVel * static_cast<float>(velocity) / 127.0f;
                const float w = kTwoPi * freq / static_cast<float>(sampleRate_), alpha = std::sin(w) / (2.0f * knockQ(rows.tonality));
                const float a0 = 1.0f + alpha;
                v->knock = true;
                v->b0 = alpha / a0;
                v->a1 = -2.0f * std::cos(w) / a0;
                v->a2 = (1.0f - alpha) / a0;
                v->y1 = v->y2 = 0.0f;
                v->freq = 0.0f;
                v->gain = 0.6f * level * (rows.volume / 0.8f) / v->b0;  // the first output sample is 0.6 x level whatever the note and the Q: the impulse is scaled for it
                v->pos = 0;
                ++next;
                continue;
            }
            v->pos = 0;
            ++next;
        }
        for (MidiOff& o : midiOffs_)
            if (o.left > 0 && --o.left == 0 && kit_ && kit_->midiOut) kit_->midiOut->send(static_cast<unsigned char>(0x80 | o.channel), o.note, 0);
        float sum = 0.0f;
        for (ClickVoice& c : voices_) {
            if (c.pos >= c.length) continue;
            if (c.knock) {
                const float x = c.pos == 0 ? c.gain : 0.0f;  // the impulse
                // band-pass resonator: y = b0 (x - x[-2]) - a1 y[-1] - a2 y[-2]; the impulse is the only input, so x[-2] is zero after two samples
                const float y = c.b0 * (x - (c.pos == 2 ? c.gain : 0.0f)) - c.a1 * c.y1 - c.a2 * c.y2;
                c.y2 = c.y1;
                c.y1 = y;
                sum += y * std::pow(1.0f - static_cast<float>(c.pos) / static_cast<float>(c.length), 2.0f);  // a short fade so that it ends at zero
            } else if (c.sample) {
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
    if (recNext_ >= 0 && position_ != recNext_) ++recPass_;
    recNext_ = position_ + n;
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
        if (recording_ && playing_ && position_ >= recFrom_ && (on || off || type == 0xB0 || type == 0xE0)) midiRec_.push(MidiRecEvent{position_, e, recPass_ + (recNext_ >= 0 && position_ != recNext_ ? 1u : 0u)});
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

// Adds the audition clip to the output; it stops at its end unless it repeats.
void AudioEngine::mixAudition(float* outL, float* outR, int n) noexcept {
    const std::int64_t length = static_cast<std::int64_t>(std::min(audition_->left.size(), audition_->right.size()));
    if (length == 0) {
        auditionPosPub_.store(-1, std::memory_order_relaxed);
        return;
    }
    for (int i = 0; i < n; ++i) {
        if (auditionPos_ >= length) {
            if (!auditionLoop_) {
                auditionPosPub_.store(-1, std::memory_order_relaxed);
                return;
            }
            auditionPos_ = 0;
        }
        outL[i] += audition_->left[static_cast<std::size_t>(auditionPos_)];
        outR[i] += audition_->right[static_cast<std::size_t>(auditionPos_)];
        ++auditionPos_;
    }
    auditionPosPub_.store(auditionPos_, std::memory_order_relaxed);
}

void AudioEngine::applyMasterGain(float* outL, float* outR, float* const* extra, int extraPairs, int offset, int n) noexcept {
    const float from = masterGainApplied_, to = masterGain_;
    masterGainApplied_ = to;
    if (from == 1.0f && to == 1.0f) return;
    const float step = (to - from) / static_cast<float>(n);
    auto scale = [&](float* buf) {
        float g = from;
        for (int i = 0; i < n; ++i) {
            g += step;
            buf[offset + i] *= g;
        }
    };
    scale(outL);
    scale(outR);
    for (int p = 0; p < 2 * extraPairs; ++p) scale(extra[p]);
}

void AudioEngine::processBlock(float* outL, float* outR, int frames, float* const* extra, int extraPairs) noexcept {
    drain();
    drainMidi();
    int done = 0;
    while (done < frames) {
        int n = std::min(frames - done, kMaxBlock);
        if (countLeft_ > 0) {  // the count-in: only the click, the transport waits
            n = static_cast<int>(std::min<std::int64_t>(n, countLeft_));
            std::fill_n(outL + done, n, 0.0f);
            std::fill_n(outR + done, n, 0.0f);
            for (int p = 0; p < 2 * extraPairs; ++p) std::fill_n(extra[p] + done, n, 0.0f);
            if (countClick_) mixClickTrack(countClick_, outL + done, outR + done, countPos_, n);
            countPos_ += n;
            countLeft_ -= n;
            if (countLeft_ == 0) playing_ = true;
            done += n;
            continue;
        }
        const int blockStart = done;
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
            if (extraPairs > 0) graph_.copyOutputPairs(extra, extraPairs, done, n);
            applyMasterGain(outL, outR, extra, extraPairs, done, n);
            if (clickOn_ && click_ && clickAllowed()) mixClick(outL + done, outR + done, position_, n);
            if (recording_) {
                const std::int64_t skip = std::clamp<std::int64_t>(recFrom_ - position_, 0, n);  // the part of the block that is still pre-roll
                if (skip < n) {
                    const std::int64_t at = position_;
                    position_ += skip;
                    capture(done + static_cast<int>(skip), n - static_cast<int>(skip));
                    position_ = at;
                }
            }
            position_ += n;
        } else if (graph_.liveNeeded()) {  // stopped, but notes ring or an input is monitored
            for (int c = 0; c < inChannels_; ++c) inPtr_[c] = inBuf_[c].data() + std::min(done, static_cast<int>(inBuf_[c].size()));
            graph_.setInput(inPtr_, inChannels_);
            graph_.render(position_, n, outL + done, outR + done, false);
            if (extraPairs > 0) graph_.copyOutputPairs(extra, extraPairs, done, n);
            applyMasterGain(outL, outR, extra, extraPairs, done, n);
        } else {
            std::fill_n(outL + done, n, 0.0f);
            std::fill_n(outR + done, n, 0.0f);
            for (int p = 0; p < 2 * extraPairs; ++p) std::fill_n(extra[p] + done, n, 0.0f);
        }
        if (audition_) mixAudition(outL + blockStart, outR + blockStart, n);
        done += n;
    }
    inFrames_ = 0;
    inChannels_ = 0;
    positionPub_.store(position_, std::memory_order_relaxed);
    masterPeakPub_.store(graph_.masterPeak() * masterGain_, std::memory_order_relaxed);
}

}  // namespace lpc::audio
