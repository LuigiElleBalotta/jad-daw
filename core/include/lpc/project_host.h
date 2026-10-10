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
#include <unordered_set>
#include <vector>

#include "lpc/audio/engine.h"
#include "lpc/command.h"
#include "lpc/media_store.h"
#include "lpc/graph_builder.h"
#include "lpc/plugin_host.h"
#include "lpc/undo_stack.h"

namespace lpc {

// Owns the authoritative Project, the undo stack and the project thread. Every request (commands, undo,
// reads, transport) runs on that thread, in submission order. After each accepted change the host
// translates it into audio messages and posts them to the engine. If the engine stops draining its queue
// (device unplugged or closed) the project thread waits only briefly, then stops posting so that reads and
// saves keep working; as soon as the queue has room again the audio graph is rebuilt from the model.
class ProjectHost {
public:
    ProjectHost(Project initial, audio::AudioEngine& engine, MediaStore& media, IPluginHost* plugins = nullptr);
    ~ProjectHost();
    ProjectHost(const ProjectHost&) = delete;
    ProjectHost& operator=(const ProjectHost&) = delete;

    std::future<std::optional<CommandError>> submit(CommandPtr command);
    // A gesture (a fader drag): every command submitted between beginGesture() and endGesture() takes effect at once, and
    // endGesture() turns them into a single undo step. Use it only for commands that set the same thing again and again.
    std::future<void> beginGesture();
    std::future<void> endGesture();
    // The undo history in words: {steps that can be undone, oldest first; steps that can be redone, next first}. clearHistory() forgets both.
    std::future<std::pair<std::vector<std::string>, std::vector<std::string>>> history();
    std::future<void> clearHistory();
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
    std::future<void> setClickSettings(audio::ClickSettings settings);  // how the bar is counted and the click sounds (sample files)
    // Starts recording: the transport is placed at startFrame, the count-in clicks play for countInFrames, then playback starts and
    // the engine captures the input until stop(). The click track is the count-in's beats from frame 0 (may be empty).
    std::future<void> startRecording(std::int64_t startFrame, std::int64_t countInFrames, audio::ClickTrack countIn, bool relocate = true);
    std::future<void> stopRecording();
    std::future<void> setLiveTarget(Uuid track);  // the instrument track that plays the live MIDI (null: none)  // the transport keeps playing
    // Which input channels a track plays through its strip (first/second 1-based, 0 = not monitored)
    std::future<void> setMonitor(Uuid track, int first, int second);
    // Low Latency Monitoring Mode: a monitored track bypasses the inserts whose latency is above `limitFrames` (0 = off).
    std::future<void> setLowLatency(int limitFrames);
    std::future<void> setMetronome(bool on);  // a click on every beat of the tempo map while playing, accent on the first beat of a bar

    // Called on the project thread after every accepted submit/undo/redo and after a rebuild of the audio
    // graph. Keep it short and non-blocking: post to another thread. Replaces any earlier listener.
    std::future<void> setChangeListener(std::function<void(std::uint64_t revision)> listener) {
        return call([this, l = std::move(listener)]() mutable { listener_ = std::move(l); });
    }
    std::uint64_t revision() const { return revision_.load(std::memory_order_acquire); }

    std::uint64_t lastPostedSeq() const { return seq_.load(std::memory_order_acquire); }
    bool degraded() const { return degraded_.load(std::memory_order_acquire); }  // true while the engine has stopped draining

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
    bool post(audio::AudioMsg m);  // false when the engine is stalled (the message is destroyed) or shutting down
    void postAll(std::vector<audio::AudioMsg>& messages);
    void resync();
    void notifyChanged();
    void publish(const Project& before);
    void postClick();
    void postKit();                   // loads the sample files of the click settings
    audio::ClickSettings clickSettings_;                 // the beats for the current tempo map and signature
    bool metronome_ = false;          // project thread only
    std::vector<std::pair<InsertSlot, ProcessorRef>> liveInserts() const;
    void wantInstances();  // tells the plug-in host which slots the project holds, before instances are asked for
    void pruneInstances();
    void rebuildAllConfigs();
    void postTransport(audio::MsgKind kind, std::int64_t frame = 0, std::int64_t frame2 = 0);

    Project project_;
    UndoStack undo_;
    audio::AudioEngine& engine_;
    MediaStore& media_;
    IPluginHost* plugins_;
    PdcPlan plan_;  // project thread only: the delay plan of the graph the engine has
    std::atomic<std::uint64_t> seq_{0};
    std::atomic<bool> stopping_{false};
    std::function<void(std::uint64_t)> listener_;  // project thread only
    std::atomic<std::uint64_t> revision_{0};
    std::atomic<bool> degraded_{false};      // the audio graph may have missed messages; cleared by a rebuild
    bool gestureOpen_ = false;               // project thread only
    std::size_t gestureMark_ = 0;
    std::unordered_set<Uuid> everAdded_;     // project thread only: every track id ever sent to the engine

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> tasks_;
    bool stop_ = false;
    std::thread thread_;  // declared last: starts after everything above is constructed
};

}  // namespace lpc
