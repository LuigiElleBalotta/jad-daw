#include "rt_guard.h"

#include <atomic>
#include <cstdlib>
#include <new>

#ifdef _WIN32
#include <malloc.h>
#endif

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

// MSVC has no aligned free that matches std::free; elsewhere posix_memalign memory is released with free.
void* alignedAlloc(std::size_t n, std::size_t alignment) {
#ifdef _WIN32
    return _aligned_malloc(n, alignment);
#else
    void* p = nullptr;
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    return posix_memalign(&p, alignment, n) == 0 ? p : nullptr;
#endif
}

void alignedFree(void* p) {
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}

void* allocateAligned(std::size_t n, std::size_t alignment) {
    note();
    void* p = alignedAlloc(n ? n : alignment, alignment);
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
void operator delete(void* p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete[](void* p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { alignedFree(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { alignedFree(p); }
