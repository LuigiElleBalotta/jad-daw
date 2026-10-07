#pragma once
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "lpc/audio/engine.h"
#include "rt_guard.h"

namespace lpc::test {

// Stands in for a real device: a thread that calls processBlock, inside a real-time scope so that any
// allocation on that thread is counted. Runs faster than real time to keep tests short.
class ThreadedDevice {
public:
    explicit ThreadedDevice(audio::AudioEngine& engine, int block = 256) : engine_(engine), block_(block), thread_([this] { run(); }) {}
    ~ThreadedDevice() { stop(); }
    ThreadedDevice(const ThreadedDevice&) = delete;
    ThreadedDevice& operator=(const ThreadedDevice&) = delete;

    void stop() {
        running_.store(false);
        if (thread_.joinable()) thread_.join();
    }

private:
    void run() {
        std::vector<float> l(static_cast<std::size_t>(block_)), r(static_cast<std::size_t>(block_));
        while (running_.load()) {
            {
                rt::Scope scope;
                engine_.processBlock(l.data(), r.data(), block_);
            }
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }

    audio::AudioEngine& engine_;
    int block_;
    std::atomic<bool> running_{true};
    std::thread thread_;
};

}  // namespace lpc::test
