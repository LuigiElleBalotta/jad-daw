#include "lpc/project_host.h"

#include <chrono>

#include "lpc/graph_builder.h"

namespace lpc {

ProjectHost::ProjectHost(Project initial, audio::AudioEngine& engine, MediaStore& media)
    : project_(std::move(initial)), engine_(engine), media_(media), thread_([this] { run(); }) {
    enqueue([this] {
        for (audio::AudioMsg& m : initialMessages(project_, media_)) post(m);
    });
}

ProjectHost::~ProjectHost() {
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
        lock.lock();
        if (stop_ && tasks_.empty()) break;
    }
}

void ProjectHost::post(audio::AudioMsg m) {
    m.seq = seq_.load(std::memory_order_relaxed) + 1;
    while (!engine_.postMessage(m)) {
        if (stopping_.load(std::memory_order_acquire)) {
            m.obj.destroy();  // shutting down: this message will never be applied
            return;
        }
        engine_.collectGarbage();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    seq_.store(m.seq, std::memory_order_release);
}

void ProjectHost::publish(const Project& before) {
    for (audio::AudioMsg& m : diffToMessages(before, project_, media_)) post(m);
}

void ProjectHost::postTransport(audio::MsgKind kind, std::int64_t frame, std::int64_t frame2) {
    audio::AudioMsg m;
    m.kind = kind;
    m.frame = frame;
    m.frame2 = frame2;
    post(m);
}

std::future<std::optional<CommandError>> ProjectHost::submit(CommandPtr command) {
    return call([this, cmd = std::move(command)]() mutable -> std::optional<CommandError> {
        const Project before = project_;
        auto err = undo_.execute(project_, std::move(cmd));
        if (!err) publish(before);
        return err;
    });
}

std::future<std::optional<CommandError>> ProjectHost::undo() {
    return call([this]() -> std::optional<CommandError> {
        const Project before = project_;
        auto err = undo_.undo(project_);
        if (!err) publish(before);
        return err;
    });
}

std::future<std::optional<CommandError>> ProjectHost::redo() {
    return call([this]() -> std::optional<CommandError> {
        const Project before = project_;
        auto err = undo_.redo(project_);
        if (!err) publish(before);
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
std::future<void> ProjectHost::setLoop(std::int64_t startFrame, std::int64_t endFrame) {
    return call([this, startFrame, endFrame] { postTransport(audio::MsgKind::SetLoop, startFrame, endFrame); });
}

}  // namespace lpc
