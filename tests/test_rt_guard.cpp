#include <catch2/catch_test_macros.hpp>
#include <new>
#include <vector>
#include "lpc/audio/messages.h"
#include "lpc/audio/spsc_queue.h"
#include "rt_guard.h"

using namespace lpc;
using namespace lpc::audio;

TEST_CASE("rt guard: counts allocations only inside a scope", "[rt]") {
    test::rt::reset();
    void* outside = ::operator new(64);
    ::operator delete(outside);
    REQUIRE(test::rt::violations() == 0);
    {
        test::rt::Scope scope;
        void* inside = ::operator new(64);
        ::operator delete(inside);
        std::vector<int> v;
        v.reserve(256);
        REQUIRE(v.capacity() >= 256);
        // libc++ allocates through __builtin_operator_new, which clang may drop when the memory is never used:
        // let the pointer escape so the allocation really happens
        static void* volatile escape;
        escape = v.data();
    }
    REQUIRE(test::rt::violations() >= 2);
    test::rt::reset();
    REQUIRE(test::rt::violations() == 0);
}

TEST_CASE("rt guard: queue operations and message copies do not allocate", "[rt]") {
    auto q = std::make_unique<SpscQueue<AudioMsg, 64>>();  // built outside the real-time scope
    test::rt::reset();
    {
        test::rt::Scope scope;
        AudioMsg m;
        m.kind = MsgKind::SetStrip;
        m.strip.gain = 0.5f;
        for (int i = 0; i < 100; ++i) {
            q->push(m);
            AudioMsg out;
            q->pop(out);
        }
    }
    REQUIRE(test::rt::violations() == 0);
}

TEST_CASE("messages: Owned destroys exactly once", "[messages]") {
    static int destroyed = 0;
    struct Probe { ~Probe() { ++destroyed; } };
    destroyed = 0;
    Owned o = makeOwned(new Probe);
    REQUIRE(o.ptr != nullptr);
    o.destroy();
    REQUIRE(destroyed == 1);
    o.destroy();  // second call is a no-op
    REQUIRE(destroyed == 1);
}
