#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <thread>
#include "lpc/audio/spsc_queue.h"

using lpc::audio::SpscQueue;

TEST_CASE("spsc: fifo order, capacity and no overwrite", "[spsc]") {
    SpscQueue<int, 8> q;
    int v = -1;
    REQUIRE_FALSE(q.pop(v));
    for (int i = 0; i < 8; ++i) REQUIRE(q.push(i));
    REQUIRE_FALSE(q.push(99));  // full: reported to the caller
    REQUIRE(q.sizeApprox() == 8);
    for (int i = 0; i < 8; ++i) {
        REQUIRE(q.pop(v));
        REQUIRE(v == i);
    }
    REQUIRE_FALSE(q.pop(v));
}

TEST_CASE("spsc: wrap-around keeps order", "[spsc]") {
    SpscQueue<int, 4> q;
    int next = 0, expected = 0, v = 0;
    for (int round = 0; round < 10000; ++round) {
        for (int i = 0; i < 3; ++i) REQUIRE(q.push(next++));
        for (int i = 0; i < 3; ++i) {
            REQUIRE(q.pop(v));
            REQUIRE(v == expected++);
        }
    }
}

TEST_CASE("spsc: one producer thread, one consumer thread", "[spsc][threads]") {
    constexpr std::uint64_t kCount = 300000;
    SpscQueue<std::uint64_t, 1024> q;
    std::thread producer([&] {
        for (std::uint64_t i = 0; i < kCount; ++i)
            while (!q.push(i)) std::this_thread::yield();
    });
    std::uint64_t expected = 0, bad = 0, v = 0;
    while (expected < kCount) {
        if (q.pop(v)) {
            if (v != expected) ++bad;
            ++expected;
        } else {
            std::this_thread::yield();
        }
    }
    producer.join();
    REQUIRE(bad == 0);
}
