#include "rt_guard.h"

#include <malloc.h>

#include <atomic>
#include <cstdlib>
#include <new>

namespace {

thread_local int tlDepth = 0;
std::atomic<long> gViolations{0};

void note() {
    if (tlDepth > 0) gViolations.fetch_add(1, std::memory_order_relaxed);
}

void* allocate(std::size_t n) {
    note();
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

void* allocateAligned(std::size_t n, std::size_t alignment) {
    note();
    void* p = _aligned_malloc(n ? n : alignment, alignment);
    if (!p) throw std::bad_alloc();
    return p;
}

}  // namespace

namespace lpc::test::rt {
void enter() { ++tlDepth; }
void leave() { --tlDepth; }
long violations() { return gViolations.load(); }
void reset() { gViolations.store(0); }
}  // namespace lpc::test::rt

// Replacing the global allocation functions affects the whole test executable on purpose.
void* operator new(std::size_t n) { return allocate(n); }
void* operator new[](std::size_t n) { return allocate(n); }
void* operator new(std::size_t n, std::align_val_t a) { return allocateAligned(n, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t n, std::align_val_t a) { return allocateAligned(n, static_cast<std::size_t>(a)); }

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { _aligned_free(p); }
