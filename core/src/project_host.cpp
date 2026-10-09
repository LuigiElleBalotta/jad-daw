#include "lpc/project_host.h"

#include <chrono>
#include <cmath>
#include <filesystem>

#include "lpc/graph_builder.h"
#include "lpc/wav.h"
#include "lpc/processor_ids.h"

namespace lpc {

ProjectHost::ProjectHost(Project initial, audio::AudioEngine& engine, MediaStore& media, IPluginHost* plugins)
    : project_(std::move(initial)), engine_(engine), media_(media), plugins_(plugins), thread_([this] { run(); }) {
    if (plugins_) plugins_->setReadyListener([this](const InsertSlot&) { enqueue([this] { rebuildAllConfigs(); }); });
    enqueue([this] {
        wantInstances();
        auto messages = initialMessages(project_, media_, plugins_, &plan_);
        postAll(messages);
        pruneInstances();
    });
}

ProjectHost::~ProjectHost() {
    if (plugins_) plugins_->setReadyListener({});  // nothing may enqueue into a host that is going away
    stopping_.store(true, std::memory_order_release);
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void ProjectHost::enqueue(std::function<void()> task) {
    {
        std::lock_guard lock(mutex_);
        tasks_.push_back(std::move(task));
    }
    cv_.notify_one();
}

void ProjectHost::run() {
    std::unique_lock lock(mutex_);
    for (;;) {
        cv_.wait_for(lock, std::chrono::milliseconds(5), [&] { return stop_ || !tasks_.empty(); });
        while (!tasks_.empty()) {
            auto task = std::move(tasks_.front());
            tasks_.pop_front();
            lock.unlock();
            task();
            lock.lock();
        }
        lock.unlock();
        engine_.collectGarbage();  // free what the audio thread handed back
        if (degraded_ && engine_.pendingMessages() < audio::kMessageQueueCapacity / 2) resync();  // rebuilding is expensive: wait for room
        lock.lock();
        if (stop_ && tasks_.empty()) break;
    }
}

bool ProjectHost::post(audio::AudioMsg m) {
    constexpr auto kStallTimeout = std::chrono::milliseconds(500);
    m.seq = seq_.load(std::memory_order_relaxed) + 1;
    engine_.collectGarbage();  // keeps the feedback queue from filling up during bursts
    const auto giveUpAt = std::chrono::steady_clock::now() + kStallTimeout;
    while (!engine_.postMessage(m)) {
        if (stopping_.load(std::memory_order_acquire) || degraded_ || std::chrono::steady_clock::now() >= giveUpAt) {
            m.obj.destroy();  // this message will never be applied
            if (!stopping_.load(std::memory_order_acquire)) degraded_.store(true, std::memory_order_release);
            return false;
        }
        engine_.collectGarbage();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (m.kind == audio::MsgKind::AddTrack) everAdded_.insert(m.track);
    seq_.store(m.seq, std::memory_order_release);
    return true;
}

void ProjectHost::postAll(std::vector<audio::AudioMsg>& messages) {
    for (std::size_t i = 0; i < messages.size(); ++i) {
        if (post(messages[i])) continue;
        for (std::size_t j = i + 1; j < messages.size(); ++j) messages[j].obj.destroy();
        return;
    }
}

// The audio graph may have missed messages: drop every track it could hold and send the whole model again.
void ProjectHost::resync() {
    std::vector<audio::AudioMsg> messages;
    wantInstances();
    for (const Uuid& id : everAdded_) {
        audio::AudioMsg m;
        m.kind = audio::MsgKind::RemoveTrack;
        m.track = id;
        messages.push_back(m);
    }
    for (audio::AudioMsg& m : initialMessages(project_, media_, plugins_, &plan_)) messages.push_back(m);
    degraded_.store(false, std::memory_order_release);
    postAll(messages);
    pruneInstances();
    notifyChanged();  // the audio side was rebuilt: observers may want to refresh
}

void ProjectHost::notifyChanged() {
    const std::uint64_t rev = revision_.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (listener_) listener_(rev);
}

void ProjectHost::publish(const Project& before) {
    if (degraded_) return;  // run() rebuilds the graph once the engine drains its queue again
    wantInstances();
    auto messages = diffToMessages(before, project_, media_, plugins_, &plan_);
    postAll(messages);
    pruneInstances();
    if (metronome_ && before.tempoMap != project_.tempoMap) postClick();  // the beats moved with the tempo
}

void ProjectHost::rebuildAllConfigs() {
    if (degraded_) return;  // the rebuild that follows will pick the live instances up
    wantInstances();
    auto messages = refreshMessages(project_, media_, plugins_, &plan_);
    postAll(messages);
}

std::vector<std::pair<InsertSlot, ProcessorRef>> ProjectHost::liveInserts() const {
    std::vector<std::pair<InsertSlot, ProcessorRef>> live;
    for (const Track& t : project_.tracks)
        for (std::size_t i = 0; i < t.strip.inserts.size(); ++i)
            if (isVst3Id(t.strip.inserts[i].processorId)) live.push_back({InsertSlot{t.id, static_cast<int>(i)}, t.strip.inserts[i]});
    return live;
}

void ProjectHost::wantInstances() {
    if (plugins_) plugins_->setWanted(liveInserts());
}

void ProjectHost::pruneInstances() {
    if (plugins_) plugins_->prune(liveInserts());
}

void ProjectHost::postTransport(audio::MsgKind kind, std::int64_t frame, std::int64_t frame2) {
    audio::AudioMsg m;
    m.kind = kind;
    m.frame = frame;
    m.frame2 = frame2;
    if (degraded_) return;  // a rebuild follows the next change; the transport is not part of it
    post(m);
}

std::future<std::optional<CommandError>> ProjectHost::submit(CommandPtr command) {
    return call([this, cmd = std::move(command)]() mutable -> std::optional<CommandError> {
        const Project before = project_;
        auto err = undo_.execute(project_, std::move(cmd));
        if (!err) {
            publish(before);
            notifyChanged();
        }
        return err;
    });
}

std::future<void> ProjectHost::beginGesture() {
    return call([this] {
        gestureOpen_ = true;
        gestureMark_ = undo_.size();
    });
}

std::future<void> ProjectHost::endGesture() {
    return call([this] {
        if (gestureOpen_) undo_.coalesceFrom(gestureMark_);
        gestureOpen_ = false;
    });
}

std::future<std::optional<CommandError>> ProjectHost::undo() {
    return call([this]() -> std::optional<CommandError> {
        const Project before = project_;
        auto err = undo_.undo(project_);
        if (!err) {
            publish(before);
            notifyChanged();
        }
        return err;
    });
}

std::future<std::optional<CommandError>> ProjectHost::redo() {
    return call([this]() -> std::optional<CommandError> {
        const Project before = project_;
        auto err = undo_.redo(project_);
        if (!err) {
            publish(before);
            notifyChanged();
        }
        return err;
    });
}

std::future<void> ProjectHost::play() {
    return call([this] { postTransport(audio::MsgKind::Play); });
}
std::future<void> ProjectHost::stop() {
    return call([this] { postTransport(audio::MsgKind::Stop); });
}
std::future<void> ProjectHost::locate(std::int64_t frame) {
    return call([this, frame] { postTransport(audio::MsgKind::Locate, frame); });
}
void ProjectHost::postClick() {
    if (degraded_) return;
    audio::AudioMsg m;
    m.kind = audio::MsgKind::SetClick;
    m.frame = metronome_ ? 1 : 0;
    if (metronome_) m.obj = audio::makeOwned(new audio::ClickTrack(buildClickTrack(project_, clickSettings_)));
    post(m);
}

// Reads the sample file of every slot (WAV: any rate and channel count, mixed down to mono and resampled to the engine's rate).
void ProjectHost::postKit() {
    if (degraded_) return;
    auto* kit = new audio::ClickKit;
    for (int slot = 1; slot < audio::kClickSlots; ++slot) {
        const std::string& file = clickSettings_.files[static_cast<std::size_t>(slot)];
        kit->gain[static_cast<std::size_t>(slot)] = clickSettings_.gain[static_cast<std::size_t>(slot)];
        if (file.empty()) continue;
        try {
            const WavData wav = readWav(std::filesystem::u8path(file));
            if (wav.frames() == 0 || wav.sampleRate <= 0 || wav.channels <= 0) continue;
            const std::int64_t frames = wav.frames();
            std::vector<float> mono(static_cast<std::size_t>(frames));
            for (std::int64_t i = 0; i < frames; ++i) {
                float sum = 0.0f;
                for (int c = 0; c < wav.channels; ++c) sum += wav.samples[static_cast<std::size_t>(i * wav.channels + c)];
                mono[static_cast<std::size_t>(i)] = sum / static_cast<float>(wav.channels);
            }
            const double ratio = static_cast<double>(wav.sampleRate) / project_.sampleRate;
            const std::int64_t out = std::min<std::int64_t>(static_cast<std::int64_t>(frames / ratio), static_cast<std::int64_t>(project_.sampleRate) * 5);  // at most 5 s
            std::vector<float>& dst = kit->sample[static_cast<std::size_t>(slot)];
            dst.resize(static_cast<std::size_t>(out));
            for (std::int64_t i = 0; i < out; ++i) {
                const double pos = static_cast<double>(i) * ratio;
                const auto a = static_cast<std::size_t>(pos);
                const std::size_t b = std::min(a + 1, mono.size() - 1);
                const float f = static_cast<float>(pos - static_cast<double>(a));
                dst[static_cast<std::size_t>(i)] = mono[a] + (mono[b] - mono[a]) * f;
            }
        } catch (const std::exception&) {
            // an unreadable file leaves its slot on the built-in blip
        }
    }
    audio::AudioMsg m;
    m.kind = audio::MsgKind::SetClickKit;
    m.obj = audio::makeOwned(kit);
    post(m);
}

std::future<void> ProjectHost::startRecording(std::int64_t startFrame, std::int64_t countInFrames, audio::ClickTrack countIn, bool relocate) {
    return call([this, startFrame, countInFrames, relocate, c = std::move(countIn)]() mutable {
        if (degraded_) return;
        if (relocate) postTransport(audio::MsgKind::Locate, startFrame);
        audio::AudioMsg m;
        m.kind = audio::MsgKind::StartRecord;
        m.frame = countInFrames;
        m.obj = audio::makeOwned(new audio::ClickTrack(std::move(c)));
        post(m);
    });
}

std::future<void> ProjectHost::setLiveTarget(Uuid track) {
    return call([this, track] {
        if (degraded_) return;
        audio::AudioMsg m;
        m.kind = audio::MsgKind::SetLiveTarget;
        m.track = track;
        post(m);
    });
}

std::future<void> ProjectHost::stopRecording() {
    return call([this] { postTransport(audio::MsgKind::StopRecord); });
}

std::future<void> ProjectHost::setMonitor(Uuid track, int first, int second) {
    return call([this, track, first, second] {
        if (degraded_) return;
        audio::AudioMsg m;
        m.kind = audio::MsgKind::SetMonitor;
        m.track = track;
        m.frame = first;
        m.frame2 = second;
        post(m);
    });
}

std::future<void> ProjectHost::setClickSettings(audio::ClickSettings settings) {
    return call([this, s = std::move(settings)]() mutable {
        clickSettings_ = std::move(s);
        postKit();
        if (metronome_) postClick();
    });
}

std::future<void> ProjectHost::setMetronome(bool on) {
    return call([this, on] {
        metronome_ = on;
        postClick();
    });
}

std::future<void> ProjectHost::setLoop(std::int64_t startFrame, std::int64_t endFrame) {
    return call([this, startFrame, endFrame] { postTransport(audio::MsgKind::SetLoop, startFrame, endFrame); });
}

}  // namespace lpc
