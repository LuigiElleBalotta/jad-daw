#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>

#include "lpc/audio/engine.h"
#include "lpc/command.h"
#include "lpc/media_store.h"
#include "lpc/undo_stack.h"

namespace lpc {

// Owns the authoritative Project, the undo stack and the project thread. Every request (commands, undo,
// reads, transport) runs on that thread, in submission order. After each accepted change the host
// translates it into audio messages and posts them to the engine. If the engine stops draining its queue,
// the project thread waits (nothing is dropped); destruction cancels the wait.
class ProjectHost {
public:
    ProjectHost(Project initial, audio::AudioEngine& engine, MediaStore& media);
    ~ProjectHost();
    ProjectHost(const ProjectHost&) = delete;
    ProjectHost& operator=(const ProjectHost&) = delete;

    std::future<std::optional<CommandError>> submit(CommandPtr command);
    std::future<std::optional<CommandError>> undo();
    std::future<std::optional<CommandError>> redo();

    template <typename F>
    auto read(F fn) -> std::future<std::invoke_result_t<F, const Project&>> {
        return call([this, fn = std::move(fn)]() mutable -> std::invoke_result_t<F, const Project&> {
            return fn(static_cast<const Project&>(project_));
        });
    }

    std::future<void> play();
    std::future<void> stop();
    std::future<void> locate(std::int64_t frame);
    std::future<void> setLoop(std::int64_t startFrame, std::int64_t endFrame);

    std::uint64_t lastPostedSeq() const { return seq_.load(std::memory_order_acquire); }

private:
    template <typename F>
    auto call(F fn) -> std::future<std::invoke_result_t<F>> {
        using R = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::move(fn));
        std::future<R> future = task->get_future();
        enqueue([task] { (*task)(); });
        return future;
    }

    void enqueue(std::function<void()> task);
    void run();
    void post(audio::AudioMsg m);
    void publish(const Project& before);
    void postTransport(audio::MsgKind kind, std::int64_t frame = 0, std::int64_t frame2 = 0);

    Project project_;
    UndoStack undo_;
    audio::AudioEngine& engine_;
    MediaStore& media_;
    std::atomic<std::uint64_t> seq_{0};
    std::atomic<bool> stopping_{false};

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> tasks_;
    bool stop_ = false;
    std::thread thread_;  // declared last: starts after everything above is constructed
};

}  // namespace lpc
