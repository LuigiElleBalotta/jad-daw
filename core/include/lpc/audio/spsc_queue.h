#pragma once
#include <atomic>
#include <cstddef>
#include <type_traits>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)  // padding added by alignas(64) is intentional (avoids false sharing)
#endif

namespace lpc::audio {

// Single-producer single-consumer lock-free ring buffer. Storage is allocated in the constructor
// (never in push/pop), so push and pop are safe on a real-time thread.
template <typename T, std::size_t Capacity>
class SpscQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>, "queue elements are copied with plain assignment");

public:
    SpscQueue() : buf_(Capacity) {}

    bool push(const T& value) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (tail - head == Capacity) return false;
        buf_[tail & (Capacity - 1)] = value;
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& out) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if (head == tail) return false;
        out = buf_[head & (Capacity - 1)];
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    std::size_t sizeApprox() const noexcept {
        return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire);
    }

private:
    std::vector<T> buf_;
    alignas(64) std::atomic<std::size_t> head_{0};  // consumer position
    alignas(64) std::atomic<std::size_t> tail_{0};  // producer position
};

}  // namespace lpc::audio

#ifdef _MSC_VER
#pragma warning(pop)
#endif
