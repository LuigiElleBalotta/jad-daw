# Core Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A headless C++20 library and CLI that load, edit (through undoable commands), play, render and save a multitrack audio/MIDI project, proven by automated tests.

**Architecture:** Three owners, one datum each. The *project thread* owns the authoritative `Project` model and the undo stack. The *audio thread* owns a `RenderGraph` copy, updated by typed messages over a lock-free SPSC queue (option C of the spec). Objects freed or replaced on the audio side travel back through a feedback queue and are destroyed on the project thread. All client changes are JSON-serializable `Command`s.

**Tech Stack:** C++20, MSVC (Visual Studio 2022), CMake, Catch2 v3.7.1, nlohmann/json v3.11.3 (both via `FetchContent`), JUCE 8.0.4 only inside `lpc-cli` for the Windows audio device.

**Spec:** `docs/superpowers/specs/2026-10-07-core-engine-design.md`

## Global Constraints

- Language/toolchain: C++20, MSVC from Visual Studio 2022 (`cl` is not on PATH; always build through CMake). Generator `Visual Studio 17 2022`, platform `x64`. Ninja and vcpkg are NOT installed and not used.
- Licence: AGPLv3 (JUCE is AGPLv3). No Apple assets, names or artwork anywhere.
- `core` must not depend on JUCE. JUCE is used only in `tools/lpc-cli`, behind CMake option `LPC_BUILD_CLI`.
- Audio-thread code (no allocation, no locks, no I/O, no `std::function`, no exceptions) lives only in `core/include/lpc/audio/` and `core/src/audio/`.
- Time: musical positions in ticks, `kPPQ = 960`. Audio blocks are `float32`. Sample positions are `std::int64_t`.
- IDs are UUIDs, serialized as lowercase `8-4-4-4-12` strings. Clients, undo and persistence refer to IDs, never indices.
- Project on disk is a folder `Name.lpc/` containing `project.json` (with `schemaVersion`), `audio/`, `cache/`. Writes are atomic (temp file, then rename) and keep a `project.json.bak`.
- Windows first. Audio device is WASAPI/DirectSound via JUCE. ASIO is NOT built in this plan (spec section 10: ASIO SDK licence must be checked first).
- Media files must be WAV with the same sample rate as the project. No resampling in Core.
- Commits: end every commit message with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.

## Scope notes (where this plan is narrower than the spec text)

- Automation lanes are part of the model and of `project.json`, but are not played back by the engine (the spec's engine section does not require it).
- Per-track meters are deferred to the UI sub-project. The engine publishes the playhead position, the master peak and an underrun counter.
- The spec asks for a detector for "allocation or lock" on the audio thread. This plan implements an allocation detector (global `operator new` hook, active only inside `rt::Scope`) plus a static check that fails a ctest if lock primitives or `std::function` appear in the audio directories (Task 14).
- Plugin delay compensation (PDC) is deferred to the plugin-host sub-project: no processor in Core reports a latency, so there is nothing to compensate yet. `processingOrder` is the place where it will plug in.
- The spec lists CMake plus vcpkg; this plan uses CMake `FetchContent` instead because vcpkg is not installed on the target machine. The set of dependencies is the same and pinned by tag.
- Loop playback is implemented (`SetLoop`), as the spec's transport list requires. Metronome, punch and cycle-follow are not part of Core.

## Review Focus

Inputs the spec implies but its feature tests would not exercise, most likely first. Each has a test in the owning task.

1. `project.json` truncated, hand-edited or from a newer version: must give a clear error and leave the files on disk untouched, never crash (Task 4).
2. Project folder path containing spaces and non-ASCII characters (`Progetto è 日本`): save and load must work (Task 4).
3. Odd WAV files: mono, 8-bit/24-bit/float32, `WAVE_FORMAT_EXTENSIBLE`, truncated `data` chunk, zero frames, odd chunk padding: decoded correctly or rejected with an error, never an out-of-bounds read (Task 8).
4. Commands with missing IDs, duplicate IDs, out-of-range values, cycles in send routing: structured error, project unchanged (Tasks 5, 6).
5. Block sizes of 1 and 4096 frames, an empty project, regions entirely past the end, and media files missing on disk: output identical to normal block size, silence, or a reported warning, never a crash (Tasks 9, 10, 12).

## File Structure

```
CMakeLists.txt                        root build, FetchContent, subdirectories
.gitignore  THIRD_PARTY.md  LICENSE  .github/workflows/ci.yml
core/CMakeLists.txt                   static library lpc_core (globs src/**/*.cpp)
core/include/lpc/
  uuid.h tempo_map.h model.h model_json.h project_io.h processor_ids.h
  command.h commands.h undo_stack.h
  wav.h media_store.h demo_project.h graph_builder.h
  project_host.h offline_render.h device.h
  audio/spsc_queue.h audio/messages.h audio/frame_source.h audio/processors.h
  audio/render_graph.h audio/engine.h
core/src/                             one .cpp per header above (+ audio/ subfolder)
tests/CMakeLists.txt                  lpc_tests (globs *.cpp), Catch2
tests/rt_guard.h rt_guard.cpp         allocation detector
tests/*.cpp                           one test file per task
tests/golden/*.wav                    reference renders (Task 12)
tools/lpc-cli/                        CLI: demo, render, play (JUCE device)
```

Build and test commands used throughout (run from the repo root, Git Bash):

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64          # configure (once, and when CMakeLists change)
cmake --build build --config Debug --target lpc_tests           # build tests
build/tests/Debug/lpc_tests.exe "[tag]"                         # run tests with a Catch2 tag
ctest --test-dir build -C Debug --output-on-failure             # run everything
```

---

### Task 1: Project scaffold and `Uuid`

**Files:**
- Create: `CMakeLists.txt`, `core/CMakeLists.txt`, `tests/CMakeLists.txt`, `.gitignore`, `THIRD_PARTY.md`
- Create: `core/include/lpc/uuid.h`, `core/src/uuid.cpp`
- Test: `tests/test_uuid.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `lpc::Uuid { std::uint64_t hi, lo; static Uuid random(); static Uuid random(std::mt19937_64&); static std::optional<Uuid> parse(std::string_view); std::string toString() const; bool isNull() const; <=>, == }` and `std::hash<lpc::Uuid>`. Library target `lpc_core` (globs `core/src/**/*.cpp`), test target `lpc_tests` (globs `tests/**/*.cpp`).

- [ ] **Step 1: Initialise git and write build files**

```bash
git init
```

`.gitignore`:

```
build/
out/
.vs/
*.user
CMakeUserPresets.json
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.24)
project(lpc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LPC_BUILD_CLI "Build lpc-cli (downloads JUCE)" ON)

include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2 nlohmann_json)

# Project-wide flags are added after the dependencies so they do not warn about third-party code.
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
endif()

enable_testing()
add_subdirectory(core)
add_subdirectory(tests)
if(LPC_BUILD_CLI)
    add_subdirectory(tools/lpc-cli)
endif()
```

`core/CMakeLists.txt`:

```cmake
file(GLOB_RECURSE LPC_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(lpc_core STATIC ${LPC_CORE_SOURCES})
target_include_directories(lpc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lpc_core PUBLIC nlohmann_json::nlohmann_json)
find_package(Threads REQUIRED)
target_link_libraries(lpc_core PUBLIC Threads::Threads)
```

`tests/CMakeLists.txt`:

```cmake
file(GLOB_RECURSE LPC_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(lpc_tests ${LPC_TEST_SOURCES})
target_link_libraries(lpc_tests PRIVATE lpc_core Catch2::Catch2WithMain)
target_compile_definitions(lpc_tests PRIVATE LPC_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(lpc_tests)
```

Create a stub `tools/lpc-cli/CMakeLists.txt` so configure works before Task 13:

```cmake
# Filled in by Task 13.
```

`THIRD_PARTY.md`:

```markdown
# Third-party software

| Component | Version | Licence | Used in |
|---|---|---|---|
| Catch2 | 3.7.1 | BSL-1.0 | tests |
| nlohmann/json | 3.11.3 | MIT | core |
| JUCE | 8.0.4 | AGPLv3 | lpc-cli audio device only |
```

- [ ] **Step 2: Write the failing test**

`tests/test_uuid.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <random>
#include <unordered_set>
#include "lpc/uuid.h"

using lpc::Uuid;

TEST_CASE("uuid: random ids are unique and non-null", "[uuid]") {
    std::unordered_set<Uuid> seen;
    for (int i = 0; i < 1000; ++i) {
        const Uuid u = Uuid::random();
        REQUIRE_FALSE(u.isNull());
        REQUIRE(seen.insert(u).second);
    }
}

TEST_CASE("uuid: string round trip", "[uuid]") {
    std::mt19937_64 rng(7);
    for (int i = 0; i < 100; ++i) {
        const Uuid u = Uuid::random(rng);
        const std::string s = u.toString();
        REQUIRE(s.size() == 36);
        REQUIRE(s[8] == '-');
        REQUIRE(s[14] == '4');  // version nibble
        const auto back = Uuid::parse(s);
        REQUIRE(back.has_value());
        REQUIRE(*back == u);
    }
}

TEST_CASE("uuid: seeded generator is deterministic", "[uuid]") {
    std::mt19937_64 a(42), b(42);
    REQUIRE(Uuid::random(a) == Uuid::random(b));
}

TEST_CASE("uuid: parse rejects malformed input", "[uuid]") {
    REQUIRE_FALSE(Uuid::parse("").has_value());
    REQUIRE_FALSE(Uuid::parse("not-a-uuid").has_value());
    REQUIRE_FALSE(Uuid::parse("zzzzzzzz-zzzz-zzzz-zzzz-zzzzzzzzzzzz").has_value());
    REQUIRE_FALSE(Uuid::parse("0123456789abcdef0123456789abcdef0123").has_value());  // no dashes
    REQUIRE_FALSE(Uuid::parse("00000000-0000-0000-0000-00000000000").has_value());   // 35 chars
}

TEST_CASE("uuid: null uuid", "[uuid]") {
    const Uuid n;
    REQUIRE(n.isNull());
    REQUIRE(n.toString() == "00000000-0000-0000-0000-000000000000");
    REQUIRE(Uuid::parse(n.toString()) == n);
}
```

- [ ] **Step 3: Run it to verify it fails**

Run: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Debug --target lpc_tests`
Expected: FAIL to compile, `Cannot open include file: 'lpc/uuid.h'` (the first configure also downloads Catch2 and nlohmann/json).

- [ ] **Step 4: Implement**

`core/include/lpc/uuid.h`:

```cpp
#pragma once
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <string_view>

namespace lpc {

struct Uuid {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;

    static Uuid random();                       // thread-local generator seeded from random_device
    static Uuid random(std::mt19937_64& rng);   // deterministic, for tests and demo projects
    static std::optional<Uuid> parse(std::string_view s);

    std::string toString() const;
    bool isNull() const { return hi == 0 && lo == 0; }

    auto operator<=>(const Uuid&) const = default;
};

}  // namespace lpc

template <>
struct std::hash<lpc::Uuid> {
    std::size_t operator()(const lpc::Uuid& u) const noexcept {
        return std::hash<std::uint64_t>{}(u.hi) ^ (std::hash<std::uint64_t>{}(u.lo) * 0x9e3779b97f4a7c15ULL);
    }
};
```

`core/src/uuid.cpp`:

```cpp
#include "lpc/uuid.h"

#include <cstdio>

namespace lpc {

Uuid Uuid::random(std::mt19937_64& rng) {
    Uuid u{rng(), rng()};
    u.hi = (u.hi & 0xffffffffffff0fffULL) | 0x0000000000004000ULL;  // version 4
    u.lo = (u.lo & 0x3fffffffffffffffULL) | 0x8000000000000000ULL;  // RFC 4122 variant
    return u;
}

Uuid Uuid::random() {
    static thread_local std::mt19937_64 rng{[] {
        std::random_device rd;
        return (std::uint64_t(rd()) << 32) ^ std::uint64_t(rd());
    }()};
    return random(rng);
}

std::string Uuid::toString() const {
    char buf[37];
    std::snprintf(buf, sizeof buf, "%08x-%04x-%04x-%04x-%012llx", static_cast<unsigned>(hi >> 32),
                  static_cast<unsigned>((hi >> 16) & 0xffff), static_cast<unsigned>(hi & 0xffff),
                  static_cast<unsigned>(lo >> 48), static_cast<unsigned long long>(lo & 0xffffffffffffULL));
    return buf;
}

std::optional<Uuid> Uuid::parse(std::string_view s) {
    if (s.size() != 36) return std::nullopt;
    Uuid u;
    int digits = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return std::nullopt;
            continue;
        }
        const char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return std::nullopt;
        std::uint64_t& part = digits < 16 ? u.hi : u.lo;
        part = (part << 4) | std::uint64_t(d);
        ++digits;
    }
    return u;
}

}  // namespace lpc
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target lpc_tests && build/tests/Debug/lpc_tests.exe "[uuid]"`
Expected: `All tests passed (...)`.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "build: CMake scaffold, Catch2/json via FetchContent, Uuid" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: `TempoMap` (ticks to samples and back)

**Files:**
- Create: `core/include/lpc/tempo_map.h`, `core/src/tempo_map.cpp`
- Test: `tests/test_tempo_map.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `lpc::Ticks` (= `std::int64_t`), `lpc::kPPQ = 960`, and `lpc::TempoMap`:
  - `TempoMap()` (120 bpm, 4/4 at tick 0); `static TempoMap fromEvents(std::vector<TempoEvent>, std::vector<SigEvent>)` (throws `std::invalid_argument`)
  - `bool setTempo(Ticks, double bpm)`, `bool removeTempo(Ticks)`, `std::optional<double> tempoEventAt(Ticks) const`, `bool setSignature(Ticks, int, int)`
  - `const std::vector<TempoEvent>& tempos() const`, `const std::vector<SigEvent>& signatures() const`
  - `double bpmAt(Ticks) const`, `double ticksToSamples(Ticks, double sampleRate) const`, `Ticks samplesToTicks(double samples, double sampleRate) const`, `operator==`
  - bpm range `[20, 999]`.

- [ ] **Step 1: Write the failing test**

`tests/test_tempo_map.cpp`:

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include "lpc/tempo_map.h"

using lpc::kPPQ;
using lpc::TempoMap;
using lpc::Ticks;

TEST_CASE("tempo map: default is 120 bpm", "[tempo]") {
    const TempoMap m;
    REQUIRE(m.bpmAt(0) == 120.0);
    // one beat at 120 bpm is 0.5 s = 24000 samples at 48 kHz
    REQUIRE(m.ticksToSamples(kPPQ, 48000.0) == Catch::Approx(24000.0));
    REQUIRE(m.samplesToTicks(24000.0, 48000.0) == kPPQ);
}

TEST_CASE("tempo map: tempo change", "[tempo]") {
    TempoMap m;
    REQUIRE(m.setTempo(kPPQ, 60.0));
    // beat 1 -> 24000 samples (120 bpm), beat 2 -> +48000 samples (60 bpm)
    REQUIRE(m.ticksToSamples(2 * kPPQ, 48000.0) == Catch::Approx(72000.0));
    REQUIRE(m.samplesToTicks(72000.0, 48000.0) == 2 * kPPQ);
    REQUIRE(m.samplesToTicks(24000.0, 48000.0) == kPPQ);
    REQUIRE(m.bpmAt(kPPQ - 1) == 120.0);
    REQUIRE(m.bpmAt(kPPQ) == 60.0);
}

TEST_CASE("tempo map: round trip across many tempo changes", "[tempo]") {
    TempoMap m;
    for (int i = 1; i <= 20; ++i) REQUIRE(m.setTempo(i * kPPQ * 4, 60.0 + 7.0 * i));
    for (Ticks t = 0; t < 90 * kPPQ; t += 137) {
        const double s = m.ticksToSamples(t, 44100.0);
        // floor() of a value that is mathematically t may land one tick low because of rounding
        const Ticks back = m.samplesToTicks(s + 1e-6, 44100.0);
        REQUIRE(back == t);
    }
}

TEST_CASE("tempo map: validation", "[tempo]") {
    TempoMap m;
    REQUIRE_FALSE(m.setTempo(0, 19.9));
    REQUIRE_FALSE(m.setTempo(0, 1000.0));
    REQUIRE_FALSE(m.setTempo(0, std::numeric_limits<double>::quiet_NaN()));
    REQUIRE_FALSE(m.setTempo(-1, 120.0));
    REQUIRE_FALSE(m.removeTempo(0));  // the tick-0 event can never be removed
    REQUIRE(m.setTempo(kPPQ, 90.0));
    REQUIRE(m.tempoEventAt(kPPQ).value() == 90.0);
    REQUIRE_FALSE(m.tempoEventAt(kPPQ + 1).has_value());
    REQUIRE(m.removeTempo(kPPQ));
    REQUIRE(m.tempos().size() == 1);
}

TEST_CASE("tempo map: extreme values stay finite", "[tempo]") {
    TempoMap m;
    REQUIRE(m.setTempo(0, 999.0));
    REQUIRE(std::isfinite(m.ticksToSamples(1'000'000'000, 192000.0)));
    REQUIRE(m.ticksToSamples(-kPPQ, 48000.0) < 0.0);  // negative ticks use the first tempo
}

TEST_CASE("tempo map: signatures and fromEvents", "[tempo]") {
    TempoMap m;
    REQUIRE(m.setSignature(4 * kPPQ, 3, 4));
    REQUIRE_FALSE(m.setSignature(0, 0, 4));
    REQUIRE_FALSE(m.setSignature(0, 4, 3));  // denominator must be a power of two
    REQUIRE(m.signatures().size() == 2);

    const auto built = TempoMap::fromEvents({{kPPQ, 100.0}, {0, 120.0}}, {});
    REQUIRE(built.tempos().front().tick == 0);  // sorted
    REQUIRE(built.signatures().size() == 1);
    REQUIRE_THROWS_AS(TempoMap::fromEvents({{kPPQ, 100.0}}, {}), std::invalid_argument);  // no tick-0 event
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build --config Debug --target lpc_tests`
Expected: FAIL to compile, `Cannot open include file: 'lpc/tempo_map.h'`.

- [ ] **Step 3: Implement**

`core/include/lpc/tempo_map.h`:

```cpp
#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace lpc {

using Ticks = std::int64_t;
inline constexpr Ticks kPPQ = 960;
inline constexpr double kMinBpm = 20.0;
inline constexpr double kMaxBpm = 999.0;

class TempoMap {
public:
    struct TempoEvent {
        Ticks tick = 0;
        double bpm = 120.0;
        bool operator==(const TempoEvent&) const = default;
    };
    struct SigEvent {
        Ticks tick = 0;
        int numerator = 4;
        int denominator = 4;
        bool operator==(const SigEvent&) const = default;
    };

    TempoMap();
    static TempoMap fromEvents(std::vector<TempoEvent> tempos, std::vector<SigEvent> sigs);

    bool setTempo(Ticks tick, double bpm);
    bool removeTempo(Ticks tick);
    std::optional<double> tempoEventAt(Ticks tick) const;
    bool setSignature(Ticks tick, int numerator, int denominator);

    const std::vector<TempoEvent>& tempos() const { return tempos_; }
    const std::vector<SigEvent>& signatures() const { return sigs_; }

    double bpmAt(Ticks tick) const;
    double ticksToSamples(Ticks tick, double sampleRate) const;
    Ticks samplesToTicks(double samples, double sampleRate) const;

    bool operator==(const TempoMap&) const = default;

private:
    std::vector<TempoEvent> tempos_;
    std::vector<SigEvent> sigs_;
};

}  // namespace lpc
```

`core/src/tempo_map.cpp`:

```cpp
#include "lpc/tempo_map.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace lpc {

namespace {

double samplesPerTick(double bpm, double sampleRate) { return sampleRate * 60.0 / (bpm * double(kPPQ)); }

bool validSignature(int n, int d) {
    return n >= 1 && n <= 64 && (d == 1 || d == 2 || d == 4 || d == 8 || d == 16 || d == 32);
}

}  // namespace

TempoMap::TempoMap() : tempos_{{0, 120.0}}, sigs_{{0, 4, 4}} {}

TempoMap TempoMap::fromEvents(std::vector<TempoEvent> tempos, std::vector<SigEvent> sigs) {
    TempoMap m;
    m.tempos_.clear();
    m.sigs_.clear();
    for (const auto& e : tempos)
        if (!m.setTempo(e.tick, e.bpm)) throw std::invalid_argument("invalid tempo event");
    for (const auto& e : sigs)
        if (!m.setSignature(e.tick, e.numerator, e.denominator)) throw std::invalid_argument("invalid signature event");
    if (m.sigs_.empty()) m.sigs_.push_back({0, 4, 4});
    if (m.tempos_.empty() || m.tempos_.front().tick != 0)
        throw std::invalid_argument("tempo map needs a tempo event at tick 0");
    if (m.sigs_.front().tick != 0) throw std::invalid_argument("tempo map needs a signature event at tick 0");
    return m;
}

bool TempoMap::setTempo(Ticks tick, double bpm) {
    if (tick < 0 || !(bpm >= kMinBpm && bpm <= kMaxBpm)) return false;
    auto it = std::lower_bound(tempos_.begin(), tempos_.end(), tick,
                               [](const TempoEvent& e, Ticks t) { return e.tick < t; });
    if (it != tempos_.end() && it->tick == tick) it->bpm = bpm;
    else tempos_.insert(it, {tick, bpm});
    return true;
}

bool TempoMap::removeTempo(Ticks tick) {
    if (tick <= 0) return false;
    auto it = std::find_if(tempos_.begin(), tempos_.end(), [&](const TempoEvent& e) { return e.tick == tick; });
    if (it == tempos_.end()) return false;
    tempos_.erase(it);
    return true;
}

std::optional<double> TempoMap::tempoEventAt(Ticks tick) const {
    for (const auto& e : tempos_)
        if (e.tick == tick) return e.bpm;
    return std::nullopt;
}

bool TempoMap::setSignature(Ticks tick, int numerator, int denominator) {
    if (tick < 0 || !validSignature(numerator, denominator)) return false;
    auto it = std::lower_bound(sigs_.begin(), sigs_.end(), tick,
                               [](const SigEvent& e, Ticks t) { return e.tick < t; });
    if (it != sigs_.end() && it->tick == tick) {
        it->numerator = numerator;
        it->denominator = denominator;
    } else {
        sigs_.insert(it, {tick, numerator, denominator});
    }
    return true;
}

double TempoMap::bpmAt(Ticks tick) const {
    double bpm = tempos_.front().bpm;
    for (const auto& e : tempos_) {
        if (e.tick > tick) break;
        bpm = e.bpm;
    }
    return bpm;
}

double TempoMap::ticksToSamples(Ticks tick, double sampleRate) const {
    if (tick <= 0) return double(tick) * samplesPerTick(tempos_.front().bpm, sampleRate);
    double acc = 0.0;
    for (std::size_t i = 0; i < tempos_.size(); ++i) {
        const bool last = i + 1 == tempos_.size();
        const Ticks segEnd = last ? tick : std::min(tick, tempos_[i + 1].tick);
        acc += double(segEnd - tempos_[i].tick) * samplesPerTick(tempos_[i].bpm, sampleRate);
        if (last || tick <= tempos_[i + 1].tick) break;
    }
    return acc;
}

Ticks TempoMap::samplesToTicks(double samples, double sampleRate) const {
    if (samples <= 0.0) return Ticks(std::floor(samples / samplesPerTick(tempos_.front().bpm, sampleRate)));
    double remaining = samples;
    for (std::size_t i = 0; i < tempos_.size(); ++i) {
        const double spt = samplesPerTick(tempos_[i].bpm, sampleRate);
        if (i + 1 == tempos_.size()) return tempos_[i].tick + Ticks(std::floor(remaining / spt));
        const double segSamples = double(tempos_[i + 1].tick - tempos_[i].tick) * spt;
        if (remaining < segSamples) return tempos_[i].tick + Ticks(std::floor(remaining / spt));
        remaining -= segSamples;
    }
    return 0;  // unreachable: the loop always returns on the last event
}

}  // namespace lpc
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target lpc_tests && build/tests/Debug/lpc_tests.exe "[tempo]"`
Expected: `All tests passed`. (`<cmath>` is needed by the test for `std::isfinite`; add `#include <cmath>` to the test if the compiler reports it missing.)

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat(core): TempoMap with tick/sample conversion" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Project model and JSON mapping

**Files:**
- Create: `core/include/lpc/model.h`, `core/src/model.cpp`, `core/include/lpc/model_json.h`, `core/src/model_json.cpp`
- Test: `tests/test_model_json.cpp`

**Interfaces:**
- Consumes: `Uuid`, `Ticks`, `TempoMap` from Tasks 1-2.
- Produces (namespace `lpc`):
  - enums `TrackKind { Audio, Midi, Instrument, Aux, Bus, Master }`, `TimeBase { Musical, Absolute }`
  - structs `MidiNote{Ticks start,length; uint8_t note,velocity}`, `Region{Uuid id; TimeBase timeBase; int64 start,length; Uuid mediaId; int64 sourceOffsetFrames; float gainDb; vector<MidiNote> notes}` (`start`/`length` are ticks when `Musical`, microseconds when `Absolute`), `ProcessorRef{string processorId; map<string,double> params; string state}`, `Send{Uuid id, targetTrackId; float levelDb; bool preFader}`, `Strip{float gainDb, pan; bool mute, solo; vector<ProcessorRef> inserts; vector<Send> sends; Uuid output}` (`output` null = master), `AutomationPoint`, `AutomationLane`, `Track{Uuid id; TrackKind kind; string name, color; Strip strip; optional<ProcessorRef> instrument; vector<Region> regions; vector<AutomationLane> automation}`, `Marker`, `MediaItem{Uuid id; string path, hash; int sampleRate, channels; int64 frames}`
  - `Project{string name; int sampleRate=48000; TempoMap tempoMap; vector<Marker> markers; vector<Track> tracks; vector<MediaItem> mediaPool}` with `Project()`, `explicit Project(Uuid masterId)` (both create the master track), `findTrack(const Uuid&)` (const and non-const), `findTrackOfRegion(const Uuid&, std::size_t* index=nullptr)`, `findMedia(const Uuid&) const`, `master() const`, `operator==`
  - `nlohmann::json toJson(const Project&)`, `Project projectFromJson(const nlohmann::json&)` (throws `std::runtime_error` or `nlohmann::json::exception` on invalid input), ADL `to_json/from_json` for every model type (in `model_json.h`).

- [ ] **Step 1: Write the failing test**

`tests/test_model_json.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "lpc/model.h"
#include "lpc/model_json.h"

using namespace lpc;

namespace {

Project makeRichProject() {
    std::mt19937_64 rng(1);
    Project p(Uuid::random(rng));
    p.name = "Rich";
    p.sampleRate = 44100;
    p.tempoMap.setTempo(4 * kPPQ, 90.0);
    p.tempoMap.setSignature(8 * kPPQ, 3, 4);
    p.markers.push_back({Uuid::random(rng), 2 * kPPQ, "Verse"});

    MediaItem media{Uuid::random(rng), "audio/tone.wav", "abc123", 44100, 2, 88200};
    p.mediaPool.push_back(media);

    Track bus;
    bus.id = Uuid::random(rng);
    bus.kind = TrackKind::Bus;
    bus.name = "Bus 1";
    p.tracks.push_back(bus);

    Track audio;
    audio.id = Uuid::random(rng);
    audio.kind = TrackKind::Audio;
    audio.name = "Audio 1";
    audio.color = "teal";
    audio.strip.gainDb = -6.0f;
    audio.strip.pan = -0.25f;
    audio.strip.solo = true;
    audio.strip.output = bus.id;
    audio.strip.inserts.push_back({"builtin.gain", {{"gainDb", 3.0}}, ""});
    audio.strip.sends.push_back({Uuid::random(rng), bus.id, -12.0f, true});
    Region r;
    r.id = Uuid::random(rng);
    r.timeBase = TimeBase::Absolute;
    r.start = 500000;
    r.length = 1500000;
    r.mediaId = media.id;
    r.sourceOffsetFrames = 100;
    r.gainDb = -1.5f;
    audio.regions.push_back(r);
    audio.automation.push_back({Uuid::random(rng), "strip.gainDb", {{0, 0.0}, {kPPQ, -6.0}}});
    p.tracks.push_back(audio);

    Track inst;
    inst.id = Uuid::random(rng);
    inst.kind = TrackKind::Instrument;
    inst.name = "Keys";
    inst.instrument = ProcessorRef{"builtin.sine", {}, ""};
    Region m;
    m.id = Uuid::random(rng);
    m.start = 0;
    m.length = 4 * kPPQ;
    m.notes.push_back({0, kPPQ, 60, 100});
    m.notes.push_back({kPPQ, kPPQ, 64, 90});
    inst.regions.push_back(m);
    p.tracks.push_back(inst);
    return p;
}

}  // namespace

TEST_CASE("model: new project has exactly one master track", "[model]") {
    const Project p;
    REQUIRE(p.tracks.size() == 1);
    REQUIRE(p.master() != nullptr);
    REQUIRE(p.master()->kind == TrackKind::Master);
}

TEST_CASE("model: lookups", "[model]") {
    const Project p = makeRichProject();
    const Track& audio = p.tracks[2];
    REQUIRE(p.findTrack(audio.id) == &audio);
    REQUIRE(p.findTrack(Uuid{}) == nullptr);
    std::size_t idx = 99;
    const Track* owner = const_cast<Project&>(p).findTrackOfRegion(audio.regions[0].id, &idx);
    REQUIRE(owner == &audio);
    REQUIRE(idx == 0);
    REQUIRE(p.findMedia(p.mediaPool[0].id) == &p.mediaPool[0]);
}

TEST_CASE("model json: round trip keeps every field", "[model][json]") {
    const Project p = makeRichProject();
    const nlohmann::json j = toJson(p);
    REQUIRE(projectFromJson(j) == p);
    // and through text, as it would be on disk
    REQUIRE(projectFromJson(nlohmann::json::parse(j.dump())) == p);
}

TEST_CASE("model json: ids are uuid strings and enums are readable", "[model][json]") {
    const Project p = makeRichProject();
    const nlohmann::json j = toJson(p);
    REQUIRE(j["tracks"][2]["kind"] == "audio");
    REQUIRE(j["tracks"][2]["id"].get<std::string>().size() == 36);
    REQUIRE(j["tracks"][2]["regions"][0]["timeBase"] == "absolute");
}

TEST_CASE("model json: invalid documents throw instead of producing a project", "[model][json]") {
    nlohmann::json j = toJson(makeRichProject());

    nlohmann::json badKind = j;
    badKind["tracks"][1]["kind"] = "banana";
    REQUIRE_THROWS(projectFromJson(badKind));

    nlohmann::json badId = j;
    badId["tracks"][1]["id"] = "not-a-uuid";
    REQUIRE_THROWS(projectFromJson(badId));

    nlohmann::json missing = j;
    missing.erase("tempoMap");
    REQUIRE_THROWS(projectFromJson(missing));

    nlohmann::json noMaster = j;
    noMaster["tracks"].erase(noMaster["tracks"].begin());  // master is tracks[0]
    REQUIRE_THROWS(projectFromJson(noMaster));

    REQUIRE_THROWS(projectFromJson(nlohmann::json::array()));
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build --config Debug --target lpc_tests`
Expected: FAIL to compile, `Cannot open include file: 'lpc/model.h'`.

- [ ] **Step 3: Implement the model**

`core/include/lpc/model.h`:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "lpc/tempo_map.h"
#include "lpc/uuid.h"

namespace lpc {

enum class TrackKind { Audio, Midi, Instrument, Aux, Bus, Master };
enum class TimeBase { Musical, Absolute };

struct MidiNote {
    Ticks start = 0;  // relative to the region start
    Ticks length = 0;
    std::uint8_t note = 60;
    std::uint8_t velocity = 100;
    bool operator==(const MidiNote&) const = default;
};

struct Region {
    Uuid id;
    TimeBase timeBase = TimeBase::Musical;
    std::int64_t start = 0;   // ticks (Musical) or microseconds (Absolute)
    std::int64_t length = 0;  // same unit as start
    Uuid mediaId;             // null for MIDI regions
    std::int64_t sourceOffsetFrames = 0;
    float gainDb = 0.0f;
    std::vector<MidiNote> notes;
    bool operator==(const Region&) const = default;
};

struct ProcessorRef {
    std::string processorId;  // e.g. "builtin.gain"
    std::map<std::string, double> params;
    std::string state;  // opaque, base64 for real plugins
    bool operator==(const ProcessorRef&) const = default;
};

struct Send {
    Uuid id;
    Uuid targetTrackId;
    float levelDb = 0.0f;
    bool preFader = false;
    bool operator==(const Send&) const = default;
};

struct Strip {
    float gainDb = 0.0f;
    float pan = 0.0f;  // -1 (left) .. +1 (right)
    bool mute = false;
    bool solo = false;
    std::vector<ProcessorRef> inserts;
    std::vector<Send> sends;
    Uuid output;  // null = master
    bool operator==(const Strip&) const = default;
};

struct AutomationPoint {
    Ticks tick = 0;
    double value = 0.0;
    bool operator==(const AutomationPoint&) const = default;
};

struct AutomationLane {
    Uuid id;
    std::string target;
    std::vector<AutomationPoint> points;
    bool operator==(const AutomationLane&) const = default;
};

struct Track {
    Uuid id;
    TrackKind kind = TrackKind::Audio;
    std::string name;
    std::string color;
    Strip strip;
    std::optional<ProcessorRef> instrument;
    std::vector<Region> regions;
    std::vector<AutomationLane> automation;
    bool operator==(const Track&) const = default;
};

struct Marker {
    Uuid id;
    Ticks tick = 0;
    std::string name;
    bool operator==(const Marker&) const = default;
};

struct MediaItem {
    Uuid id;
    std::string path;  // relative to the project folder, forward slashes
    std::string hash;
    int sampleRate = 0;
    int channels = 0;
    std::int64_t frames = 0;
    bool operator==(const MediaItem&) const = default;
};

struct Project {
    std::string name = "Untitled";
    int sampleRate = 48000;
    TempoMap tempoMap;
    std::vector<Marker> markers;
    std::vector<Track> tracks;  // tracks[0] is the master track in a new project
    std::vector<MediaItem> mediaPool;

    Project();
    explicit Project(Uuid masterId);

    Track* findTrack(const Uuid& id);
    const Track* findTrack(const Uuid& id) const;
    Track* findTrackOfRegion(const Uuid& regionId, std::size_t* regionIndex = nullptr);
    const MediaItem* findMedia(const Uuid& id) const;
    const Track* master() const;

    bool operator==(const Project&) const = default;
};

}  // namespace lpc
```

`core/src/model.cpp`:

```cpp
#include "lpc/model.h"

#include <algorithm>

namespace lpc {

Project::Project() : Project(Uuid::random()) {}

Project::Project(Uuid masterId) {
    Track m;
    m.id = masterId;
    m.kind = TrackKind::Master;
    m.name = "Master";
    tracks.push_back(std::move(m));
}

Track* Project::findTrack(const Uuid& id) {
    auto it = std::find_if(tracks.begin(), tracks.end(), [&](const Track& t) { return t.id == id; });
    return it == tracks.end() ? nullptr : &*it;
}

const Track* Project::findTrack(const Uuid& id) const { return const_cast<Project*>(this)->findTrack(id); }

Track* Project::findTrackOfRegion(const Uuid& regionId, std::size_t* regionIndex) {
    for (Track& t : tracks) {
        for (std::size_t i = 0; i < t.regions.size(); ++i) {
            if (t.regions[i].id == regionId) {
                if (regionIndex) *regionIndex = i;
                return &t;
            }
        }
    }
    return nullptr;
}

const MediaItem* Project::findMedia(const Uuid& id) const {
    auto it = std::find_if(mediaPool.begin(), mediaPool.end(), [&](const MediaItem& m) { return m.id == id; });
    return it == mediaPool.end() ? nullptr : &*it;
}

const Track* Project::master() const {
    auto it = std::find_if(tracks.begin(), tracks.end(), [](const Track& t) { return t.kind == TrackKind::Master; });
    return it == tracks.end() ? nullptr : &*it;
}

}  // namespace lpc
```

- [ ] **Step 4: Implement the JSON mapping**

`core/include/lpc/model_json.h`:

```cpp
#pragma once
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "lpc/model.h"

namespace lpc {

inline void to_json(nlohmann::json& j, const Uuid& u) { j = u.toString(); }
inline void from_json(const nlohmann::json& j, Uuid& u) {
    const auto parsed = Uuid::parse(j.get<std::string>());
    if (!parsed) throw std::runtime_error("invalid uuid: " + j.dump());
    u = *parsed;
}

void to_json(nlohmann::json& j, TrackKind k);
void from_json(const nlohmann::json& j, TrackKind& k);
void to_json(nlohmann::json& j, TimeBase b);
void from_json(const nlohmann::json& j, TimeBase& b);
void to_json(nlohmann::json& j, const TempoMap& m);
void from_json(const nlohmann::json& j, TempoMap& m);
void to_json(nlohmann::json& j, const Track& t);
void from_json(const nlohmann::json& j, Track& t);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MidiNote, start, length, note, velocity)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Region, id, timeBase, start, length, mediaId, sourceOffsetFrames, gainDb, notes)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ProcessorRef, processorId, params, state)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Send, id, targetTrackId, levelDb, preFader)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Strip, gainDb, pan, mute, solo, inserts, sends, output)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AutomationPoint, tick, value)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AutomationLane, id, target, points)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Marker, id, tick, name)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MediaItem, id, path, hash, sampleRate, channels, frames)

nlohmann::json toJson(const Project& p);
Project projectFromJson(const nlohmann::json& j);  // throws on any invalid or incomplete document

}  // namespace lpc
```

`core/src/model_json.cpp`:

```cpp
#include "lpc/model_json.h"

#include <utility>

namespace lpc {

namespace {

template <typename E>
void enumToJson(nlohmann::json& j, E value, const std::pair<E, const char*>* table, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (table[i].first == value) {
            j = table[i].second;
            return;
        }
    }
    throw std::runtime_error("unknown enum value");
}

template <typename E>
void enumFromJson(const nlohmann::json& j, E& value, const std::pair<E, const char*>* table, std::size_t n) {
    const std::string s = j.get<std::string>();
    for (std::size_t i = 0; i < n; ++i) {
        if (s == table[i].second) {
            value = table[i].first;
            return;
        }
    }
    throw std::runtime_error("unknown enum string: " + s);
}

const std::pair<TrackKind, const char*> kKinds[] = {
    {TrackKind::Audio, "audio"}, {TrackKind::Midi, "midi"}, {TrackKind::Instrument, "instrument"},
    {TrackKind::Aux, "aux"},     {TrackKind::Bus, "bus"},   {TrackKind::Master, "master"}};
const std::pair<TimeBase, const char*> kBases[] = {{TimeBase::Musical, "musical"}, {TimeBase::Absolute, "absolute"}};

}  // namespace

void to_json(nlohmann::json& j, TrackKind k) { enumToJson(j, k, kKinds, std::size(kKinds)); }
void from_json(const nlohmann::json& j, TrackKind& k) { enumFromJson(j, k, kKinds, std::size(kKinds)); }
void to_json(nlohmann::json& j, TimeBase b) { enumToJson(j, b, kBases, std::size(kBases)); }
void from_json(const nlohmann::json& j, TimeBase& b) { enumFromJson(j, b, kBases, std::size(kBases)); }

void to_json(nlohmann::json& j, const TempoMap& m) {
    j = nlohmann::json::object();
    j["tempos"] = nlohmann::json::array();
    for (const auto& e : m.tempos()) j["tempos"].push_back({{"tick", e.tick}, {"bpm", e.bpm}});
    j["signatures"] = nlohmann::json::array();
    for (const auto& e : m.signatures())
        j["signatures"].push_back({{"tick", e.tick}, {"numerator", e.numerator}, {"denominator", e.denominator}});
}

void from_json(const nlohmann::json& j, TempoMap& m) {
    std::vector<TempoMap::TempoEvent> tempos;
    for (const auto& e : j.at("tempos")) tempos.push_back({e.at("tick").get<Ticks>(), e.at("bpm").get<double>()});
    std::vector<TempoMap::SigEvent> sigs;
    for (const auto& e : j.at("signatures"))
        sigs.push_back({e.at("tick").get<Ticks>(), e.at("numerator").get<int>(), e.at("denominator").get<int>()});
    m = TempoMap::fromEvents(std::move(tempos), std::move(sigs));  // throws std::invalid_argument when invalid
}

void to_json(nlohmann::json& j, const Track& t) {
    j = {{"id", t.id},           {"kind", t.kind},       {"name", t.name},       {"color", t.color},
         {"strip", t.strip},     {"regions", t.regions}, {"automation", t.automation}};
    j["instrument"] = t.instrument ? nlohmann::json(*t.instrument) : nlohmann::json(nullptr);
}

void from_json(const nlohmann::json& j, Track& t) {
    j.at("id").get_to(t.id);
    j.at("kind").get_to(t.kind);
    j.at("name").get_to(t.name);
    j.at("color").get_to(t.color);
    j.at("strip").get_to(t.strip);
    j.at("regions").get_to(t.regions);
    j.at("automation").get_to(t.automation);
    const auto& inst = j.at("instrument");
    t.instrument = inst.is_null() ? std::nullopt : std::optional<ProcessorRef>(inst.get<ProcessorRef>());
}

nlohmann::json toJson(const Project& p) {
    return {{"name", p.name},         {"sampleRate", p.sampleRate}, {"tempoMap", p.tempoMap},
            {"markers", p.markers},   {"tracks", p.tracks},         {"mediaPool", p.mediaPool}};
}

Project projectFromJson(const nlohmann::json& j) {
    if (!j.is_object()) throw std::runtime_error("project document must be a JSON object");
    Project p;
    p.tracks.clear();
    j.at("name").get_to(p.name);
    j.at("sampleRate").get_to(p.sampleRate);
    j.at("tempoMap").get_to(p.tempoMap);
    j.at("markers").get_to(p.markers);
    j.at("tracks").get_to(p.tracks);
    j.at("mediaPool").get_to(p.mediaPool);
    const auto masters = std::count_if(p.tracks.begin(), p.tracks.end(),
                                       [](const Track& t) { return t.kind == TrackKind::Master; });
    if (masters != 1) throw std::runtime_error("project must contain exactly one master track");
    if (p.sampleRate < 8000 || p.sampleRate > 384000) throw std::runtime_error("unsupported sample rate");
    return p;
}

}  // namespace lpc
```

Add `#include <algorithm>` and `#include <iterator>` at the top of `model_json.cpp` (for `std::count_if` and `std::size`).

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target lpc_tests && build/tests/Debug/lpc_tests.exe "[model]"`
Expected: `All tests passed`. If `noMaster` does not throw, check that `tracks[0]` is the master in the test project (it is: `Project(Uuid)` pushes it first).

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(core): project model and strict JSON mapping" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

### Task 4: Persistence (project folder, atomic save, migrations)

**Files:**
- Create: `core/include/lpc/project_io.h`, `core/src/project_io.cpp`, `tests/temp_dir.h`
- Test: `tests/test_project_io.cpp`

**Interfaces:**
- Consumes: `Project`, `toJson`, `projectFromJson` (Task 3).
- Produces:
  - `inline constexpr int kCurrentSchemaVersion = 1;`
  - `using Migration = std::function<nlohmann::json(nlohmann::json)>;` (migrates version `n` to `n+1`; `chain[0]` is 1 to 2)
  - `const std::vector<Migration>& builtinMigrations();`
  - `nlohmann::json migrateToCurrent(nlohmann::json doc, const std::vector<Migration>& chain, int currentVersion);`
  - `void saveProject(const Project&, const std::filesystem::path& dir);`
  - `Project loadProject(const std::filesystem::path& dir);` (both throw `std::runtime_error` with a readable message)
  - test helper `TempDir` (RAII temporary directory) in `tests/temp_dir.h`.

- [ ] **Step 1: Write the test helper and the failing tests**

`tests/temp_dir.h`:

```cpp
#pragma once
#include <filesystem>
#include <string>
#include "lpc/uuid.h"

namespace lpc::test {

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const std::string& prefix = "lpc") {
        path = std::filesystem::temp_directory_path() / (prefix + "_" + Uuid::random().toString());
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

}  // namespace lpc::test
```

`tests/test_project_io.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>
#include "lpc/model_json.h"
#include "lpc/project_io.h"
#include "temp_dir.h"

using namespace lpc;
namespace fs = std::filesystem;

namespace {

Project smallProject() {
    std::mt19937_64 rng(3);
    Project p(Uuid::random(rng));
    p.name = "Small";
    Track t;
    t.id = Uuid::random(rng);
    t.kind = TrackKind::Bus;
    t.name = "Bus";
    p.tracks.push_back(t);
    return p;
}

std::string readAll(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void writeAll(const fs::path& f, const std::string& s) {
    std::ofstream out(f, std::ios::binary | std::ios::trunc);
    out << s;
}

}  // namespace

TEST_CASE("io: save then load returns an equal project", "[io]") {
    test::TempDir tmp;
    const Project p = smallProject();
    saveProject(p, tmp.path / "a.lpc");
    REQUIRE(fs::exists(tmp.path / "a.lpc" / "project.json"));
    REQUIRE(fs::is_directory(tmp.path / "a.lpc" / "audio"));
    REQUIRE(fs::is_directory(tmp.path / "a.lpc" / "cache"));
    REQUIRE(loadProject(tmp.path / "a.lpc") == p);
}

TEST_CASE("io: second save keeps the previous version as a backup and leaves no temp file", "[io]") {
    test::TempDir tmp;
    const fs::path dir = tmp.path / "b.lpc";
    Project p = smallProject();
    saveProject(p, dir);
    const std::string first = readAll(dir / "project.json");
    p.name = "Renamed";
    saveProject(p, dir);
    REQUIRE(readAll(dir / "project.json.bak") == first);
    REQUIRE_FALSE(fs::exists(dir / "project.json.tmp"));
    REQUIRE(loadProject(dir).name == "Renamed");
}

TEST_CASE("io: folder names with spaces and non-ASCII characters", "[io]") {
    test::TempDir tmp;
    const fs::path dir = tmp.path / fs::path(u8"Progetto è 日本 con spazi.lpc");
    const Project p = smallProject();
    saveProject(p, dir);
    REQUIRE(loadProject(dir) == p);
}

TEST_CASE("io: truncated or garbage project.json is rejected and left untouched", "[io]") {
    test::TempDir tmp;
    const fs::path dir = tmp.path / "c.lpc";
    saveProject(smallProject(), dir);
    const std::string good = readAll(dir / "project.json");

    const std::string truncated = good.substr(0, good.size() / 2);
    writeAll(dir / "project.json", truncated);
    REQUIRE_THROWS_WITH(loadProject(dir), Catch::Matchers::ContainsSubstring("not valid JSON"));
    REQUIRE(readAll(dir / "project.json") == truncated);

    writeAll(dir / "project.json", "\x01\x02 not json at all");
    REQUIRE_THROWS_AS(loadProject(dir), std::runtime_error);

    writeAll(dir / "project.json", "");
    REQUIRE_THROWS_AS(loadProject(dir), std::runtime_error);
}

TEST_CASE("io: valid JSON with an invalid project is rejected with a clear message", "[io]") {
    test::TempDir tmp;
    const fs::path dir = tmp.path / "d.lpc";
    saveProject(smallProject(), dir);
    nlohmann::json doc = nlohmann::json::parse(readAll(dir / "project.json"));
    doc["project"]["tracks"][1]["kind"] = "banana";
    writeAll(dir / "project.json", doc.dump());
    REQUIRE_THROWS_WITH(loadProject(dir), Catch::Matchers::ContainsSubstring("project.json is invalid"));
}

TEST_CASE("io: missing folder or file", "[io]") {
    test::TempDir tmp;
    REQUIRE_THROWS_AS(loadProject(tmp.path / "nope.lpc"), std::runtime_error);
}

TEST_CASE("io: project from a newer schema is refused", "[io]") {
    test::TempDir tmp;
    const fs::path dir = tmp.path / "e.lpc";
    saveProject(smallProject(), dir);
    nlohmann::json doc = nlohmann::json::parse(readAll(dir / "project.json"));
    doc["schemaVersion"] = kCurrentSchemaVersion + 1;
    writeAll(dir / "project.json", doc.dump());
    REQUIRE_THROWS_WITH(loadProject(dir), Catch::Matchers::ContainsSubstring("newer version"));
}

TEST_CASE("io: migration chain upgrades old documents step by step", "[io]") {
    const std::vector<Migration> chain = {
        [](nlohmann::json d) {  // 1 -> 2: rename "old" to "mid"
            d["mid"] = d["old"];
            d.erase("old");
            return d;
        },
        [](nlohmann::json d) {  // 2 -> 3: rename "mid" to "new"
            d["new"] = d["mid"];
            d.erase("mid");
            return d;
        }};
    nlohmann::json doc = {{"schemaVersion", 1}, {"old", 42}};
    const nlohmann::json out = migrateToCurrent(doc, chain, 3);
    REQUIRE(out["schemaVersion"] == 3);
    REQUIRE(out["new"] == 42);
    REQUIRE_FALSE(out.contains("old"));

    REQUIRE_THROWS(migrateToCurrent({{"old", 1}}, chain, 3));                      // no schemaVersion
    REQUIRE_THROWS(migrateToCurrent({{"schemaVersion", 0}}, chain, 3));            // below 1
    REQUIRE_THROWS(migrateToCurrent({{"schemaVersion", 1}}, {chain[0]}, 3));      // chain too short
}
```

Add `#include <catch2/matchers/catch_matchers_string.hpp>` at the top of the test file (for `ContainsSubstring`).

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build --config Debug --target lpc_tests`
Expected: FAIL to compile, `Cannot open include file: 'lpc/project_io.h'`.

- [ ] **Step 3: Implement**

`core/include/lpc/project_io.h`:

```cpp
#pragma once
#include <filesystem>
#include <functional>
#include <vector>

#include <nlohmann/json.hpp>

#include "lpc/model.h"

namespace lpc {

inline constexpr int kCurrentSchemaVersion = 1;

// chain[i] migrates a document from schema version i+1 to i+2.
using Migration = std::function<nlohmann::json(nlohmann::json)>;

const std::vector<Migration>& builtinMigrations();
nlohmann::json migrateToCurrent(nlohmann::json doc, const std::vector<Migration>& chain, int currentVersion);

void saveProject(const Project& project, const std::filesystem::path& dir);
Project loadProject(const std::filesystem::path& dir);

}  // namespace lpc
```

`core/src/project_io.cpp`:

```cpp
#include "lpc/project_io.h"

#include <fstream>
#include <stdexcept>
#include <string>

#include "lpc/model_json.h"

namespace lpc {

namespace fs = std::filesystem;

namespace {

std::string pathText(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

}  // namespace

const std::vector<Migration>& builtinMigrations() {
    static const std::vector<Migration> none;  // schema 1 is the first version
    return none;
}

nlohmann::json migrateToCurrent(nlohmann::json doc, const std::vector<Migration>& chain, int currentVersion) {
    if (!doc.is_object() || !doc.contains("schemaVersion") || !doc["schemaVersion"].is_number_integer())
        throw std::runtime_error("project document has no schemaVersion");
    int version = doc["schemaVersion"].get<int>();
    if (version < 1) throw std::runtime_error("invalid schemaVersion " + std::to_string(version));
    if (version > currentVersion)
        throw std::runtime_error("project was saved by a newer version (schema " + std::to_string(version) +
                                 "), please update");
    if (static_cast<int>(chain.size()) < currentVersion - 1) throw std::runtime_error("migration chain is incomplete");
    while (version < currentVersion) {
        doc = chain[static_cast<std::size_t>(version - 1)](std::move(doc));
        ++version;
        doc["schemaVersion"] = version;
    }
    return doc;
}

void saveProject(const Project& project, const fs::path& dir) {
    fs::create_directories(dir / "audio");
    fs::create_directories(dir / "cache");
    const fs::path target = dir / "project.json";
    const fs::path tmp = dir / "project.json.tmp";
    const fs::path bak = dir / "project.json.bak";

    const nlohmann::json doc = {{"schemaVersion", kCurrentSchemaVersion}, {"project", toJson(project)}};
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + pathText(tmp));
        out << doc.dump(2);
        out.flush();
        if (!out) throw std::runtime_error("write failed for " + pathText(tmp));
    }
    if (fs::exists(target)) fs::copy_file(target, bak, fs::copy_options::overwrite_existing);
    fs::rename(tmp, target);  // replaces the target atomically on Windows and POSIX
}

Project loadProject(const fs::path& dir) {
    const fs::path file = dir / "project.json";
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + pathText(file));

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(in);
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("project.json is not valid JSON: ") + e.what());
    }
    doc = migrateToCurrent(std::move(doc), builtinMigrations(), kCurrentSchemaVersion);
    try {
        return projectFromJson(doc.at("project"));
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("project.json is invalid: ") + e.what());
    }
}

}  // namespace lpc
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target lpc_tests && build/tests/Debug/lpc_tests.exe "[io]"`
Expected: `All tests passed`.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat(core): atomic project save/load with schema migrations" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Command framework, undo stack, track/strip/tempo/media commands

**Files:**
- Create: `core/include/lpc/processor_ids.h`, `core/include/lpc/command.h`, `core/include/lpc/commands.h`, `core/src/commands.cpp`, `core/include/lpc/undo_stack.h`, `core/src/undo_stack.cpp`
- Test: `tests/test_commands.cpp`, `tests/test_undo_stack.cpp`

**Interfaces:**
- Consumes: model and JSON from Task 3, `TempoMap` from Task 2.
- Produces (namespace `lpc`):
  - `processor_ids.h`: `kProcGain = "builtin.gain"`, `kProcSine = "builtin.sine"`, `bool isKnownEffect(std::string_view)` (true for gain), `bool isKnownInstrument(std::string_view)` (true for sine).
  - `struct CommandError{std::string code, message;}`, `class Command` (`type()`, `toJson()`, `ApplyResult apply(Project&) const`), `using CommandPtr = std::unique_ptr<Command>`, `struct ApplyResult{CommandPtr inverse; std::optional<CommandError> error; bool ok() const;}`. `apply` is all-or-nothing: on error the project is unchanged.
  - Factories: `makeAddTrack(Track, int index=-1)`, `makeRemoveTrack(Uuid)`, `StripPatch{optional<float> gainDb, pan; optional<bool> mute, solo}` and `makeSetStrip(Uuid, StripPatch)`, `makeSetTempo(Ticks, double)`, `makeRemoveTempo(Ticks)`, `makeAddMedia(MediaItem)`, `makeRemoveMedia(Uuid)`.
  - `CommandPtr commandFromJson(const nlohmann::json&)` (throws `std::runtime_error` on unknown type or invalid fields).
  - `class UndoStack { std::optional<CommandError> execute(Project&, CommandPtr); bool canUndo() const; bool canRedo() const; std::optional<CommandError> undo(Project&); std::optional<CommandError> redo(Project&); void clear(); }`.
  - Error codes used: `duplicate_id`, `not_found`, `invalid_kind`, `bad_index`, `bad_output`, `bad_value`, `bad_tempo`, `bad_target`, `in_use`, `bad_media`, `bad_region`, `nothing_to_undo`, `nothing_to_redo`.

- [ ] **Step 1: Write the failing tests**

`tests/test_commands.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include "lpc/commands.h"
#include "lpc/model_json.h"

using namespace lpc;

namespace {

std::mt19937_64 gRng(99);

Track makeTrack(TrackKind kind, const char* name) {
    Track t;
    t.id = Uuid::random(gRng);
    t.kind = kind;
    t.name = name;
    if (kind == TrackKind::Instrument) t.instrument = ProcessorRef{"builtin.sine", {}, ""};
    return t;
}

// Applies cmd, then its inverse, and checks the project is back to the original; leaves cmd applied.
void requireRoundTrip(Project& p, const Command& cmd) {
    const Project before = p;
    ApplyResult r = cmd.apply(p);
    REQUIRE(r.ok());
    REQUIRE(r.inverse != nullptr);
    ApplyResult back = r.inverse->apply(p);
    REQUIRE(back.ok());
    REQUIRE(p == before);
    REQUIRE(cmd.apply(p).ok());
}

void requireRejected(Project& p, const Command& cmd, const char* code) {
    const Project before = p;
    const ApplyResult r = cmd.apply(p);
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error->code == code);
    REQUIRE(r.inverse == nullptr);
    REQUIRE(p == before);
}

}  // namespace

TEST_CASE("commands: add and remove track", "[commands]") {
    Project p(Uuid::random(gRng));
    const Track bus = makeTrack(TrackKind::Bus, "Bus");
    requireRoundTrip(p, *makeAddTrack(bus));
    REQUIRE(p.tracks.size() == 2);
    requireRoundTrip(p, *makeRemoveTrack(bus.id));
    REQUIRE(p.tracks.size() == 1);
}

TEST_CASE("commands: add track keeps the requested index", "[commands]") {
    Project p(Uuid::random(gRng));
    const Track a = makeTrack(TrackKind::Audio, "A");
    const Track b = makeTrack(TrackKind::Audio, "B");
    REQUIRE(makeAddTrack(a)->apply(p).ok());
    REQUIRE(makeAddTrack(b, 0)->apply(p).ok());
    REQUIRE(p.tracks[0].id == b.id);
}

TEST_CASE("commands: add track rejects bad input", "[commands]") {
    Project p(Uuid::random(gRng));
    const Track a = makeTrack(TrackKind::Audio, "A");
    REQUIRE(makeAddTrack(a)->apply(p).ok());

    requireRejected(p, *makeAddTrack(a), "duplicate_id");
    Track nullId = makeTrack(TrackKind::Audio, "N");
    nullId.id = Uuid{};
    requireRejected(p, *makeAddTrack(nullId), "duplicate_id");
    requireRejected(p, *makeAddTrack(makeTrack(TrackKind::Master, "M2")), "invalid_kind");
    requireRejected(p, *makeAddTrack(makeTrack(TrackKind::Audio, "X"), 99), "bad_index");
    requireRejected(p, *makeAddTrack(makeTrack(TrackKind::Audio, "X"), -2), "bad_index");

    Track badOut = makeTrack(TrackKind::Audio, "Out");
    badOut.strip.output = a.id;  // an audio track is not a valid destination
    requireRejected(p, *makeAddTrack(badOut), "bad_output");
    badOut.strip.output = Uuid::random(gRng);  // does not exist
    requireRejected(p, *makeAddTrack(badOut), "bad_output");

    Track noInstrument = makeTrack(TrackKind::Instrument, "I");
    noInstrument.instrument.reset();
    requireRejected(p, *makeAddTrack(noInstrument), "invalid_kind");
    Track strayInstrument = makeTrack(TrackKind::Audio, "S");
    strayInstrument.instrument = ProcessorRef{"builtin.sine", {}, ""};
    requireRejected(p, *makeAddTrack(strayInstrument), "invalid_kind");
    Track badValue = makeTrack(TrackKind::Audio, "V");
    badValue.strip.pan = 5.0f;
    requireRejected(p, *makeAddTrack(badValue), "bad_value");
}

TEST_CASE("commands: remove track rejects master, unknown and referenced tracks", "[commands]") {
    Project p(Uuid::random(gRng));
    const Track bus = makeTrack(TrackKind::Bus, "Bus");
    Track audio = makeTrack(TrackKind::Audio, "A");
    audio.strip.output = bus.id;
    REQUIRE(makeAddTrack(bus)->apply(p).ok());
    REQUIRE(makeAddTrack(audio)->apply(p).ok());

    requireRejected(p, *makeRemoveTrack(p.master()->id), "invalid_kind");
    requireRejected(p, *makeRemoveTrack(Uuid::random(gRng)), "not_found");
    requireRejected(p, *makeRemoveTrack(bus.id), "in_use");  // audio routes to it
    REQUIRE(makeRemoveTrack(audio.id)->apply(p).ok());
    REQUIRE(makeRemoveTrack(bus.id)->apply(p).ok());
}

TEST_CASE("commands: set strip patches only the given fields and undoes them", "[commands]") {
    Project p(Uuid::random(gRng));
    const Track a = makeTrack(TrackKind::Audio, "A");
    REQUIRE(makeAddTrack(a)->apply(p).ok());

    StripPatch patch;
    patch.gainDb = -6.0f;
    patch.mute = true;
    requireRoundTrip(p, *makeSetStrip(a.id, patch));
    const Track* t = p.findTrack(a.id);
    REQUIRE(t->strip.gainDb == -6.0f);
    REQUIRE(t->strip.mute);
    REQUIRE(t->strip.pan == 0.0f);  // untouched
}

TEST_CASE("commands: set strip validates values", "[commands]") {
    Project p(Uuid::random(gRng));
    const Track a = makeTrack(TrackKind::Audio, "A");
    REQUIRE(makeAddTrack(a)->apply(p).ok());
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    StripPatch s;
    s.pan = 1.5f;
    requireRejected(p, *makeSetStrip(a.id, s), "bad_value");
    s = {};
    s.gainDb = nan;
    requireRejected(p, *makeSetStrip(a.id, s), "bad_value");
    s = {};
    s.gainDb = inf;
    requireRejected(p, *makeSetStrip(a.id, s), "bad_value");
    s = {};
    s.gainDb = 100.0f;
    requireRejected(p, *makeSetStrip(a.id, s), "bad_value");
    requireRejected(p, *makeSetStrip(Uuid::random(gRng), StripPatch{}), "not_found");
}

TEST_CASE("commands: tempo", "[commands]") {
    Project p(Uuid::random(gRng));
    requireRoundTrip(p, *makeSetTempo(4 * kPPQ, 90.0));   // new event: inverse removes it
    requireRoundTrip(p, *makeSetTempo(0, 100.0));         // existing event: inverse restores 120
    REQUIRE(p.tempoMap.bpmAt(0) == 100.0);
    requireRoundTrip(p, *makeRemoveTempo(4 * kPPQ));
    requireRejected(p, *makeSetTempo(0, 5.0), "bad_tempo");
    requireRejected(p, *makeSetTempo(-5, 100.0), "bad_tempo");
    requireRejected(p, *makeRemoveTempo(0), "bad_tempo");
    requireRejected(p, *makeRemoveTempo(777), "bad_tempo");
}

TEST_CASE("commands: media pool", "[commands]") {
    Project p(Uuid::random(gRng));
    MediaItem m{Uuid::random(gRng), "audio/a.wav", "h", 48000, 2, 96000};
    requireRoundTrip(p, *makeAddMedia(m));
    requireRoundTrip(p, *makeRemoveMedia(m.id));

    MediaItem wrongRate = m;
    wrongRate.id = Uuid::random(gRng);
    wrongRate.sampleRate = 44100;
    requireRejected(p, *makeAddMedia(wrongRate), "bad_media");
    for (const char* bad : {"", "/abs/a.wav", "C:/a.wav", "../escape.wav", "audio/../../x.wav", "a\\b.wav"}) {
        MediaItem bp = m;
        bp.id = Uuid::random(gRng);
        bp.path = bad;
        requireRejected(p, *makeAddMedia(bp), "bad_media");
    }
    MediaItem sixCh = m;
    sixCh.id = Uuid::random(gRng);
    sixCh.channels = 6;
    requireRejected(p, *makeAddMedia(sixCh), "bad_media");
    REQUIRE(makeAddMedia(m)->apply(p).ok());
    requireRejected(p, *makeAddMedia(m), "duplicate_id");
    requireRejected(p, *makeRemoveMedia(Uuid::random(gRng)), "not_found");
}

TEST_CASE("commands: every command survives a JSON round trip", "[commands][json]") {
    Project p(Uuid::random(gRng));
    const Track t = makeTrack(TrackKind::Instrument, "Keys");
    StripPatch patch;
    patch.gainDb = -3.0f;
    patch.solo = true;
    std::vector<CommandPtr> cmds;
    cmds.push_back(makeAddTrack(t, 0));
    cmds.push_back(makeRemoveTrack(t.id));
    cmds.push_back(makeSetStrip(t.id, patch));
    cmds.push_back(makeSetTempo(kPPQ, 100.0));
    cmds.push_back(makeRemoveTempo(kPPQ));
    cmds.push_back(makeAddMedia({Uuid::random(gRng), "audio/x.wav", "h", 48000, 1, 10}));
    cmds.push_back(makeRemoveMedia(Uuid::random(gRng)));
    for (const auto& c : cmds) {
        const nlohmann::json j = c->toJson();
        REQUIRE(j["type"] == c->type());
        REQUIRE(commandFromJson(j)->toJson() == j);
    }
    REQUIRE_THROWS_AS(commandFromJson({{"type", "no_such_command"}}), std::runtime_error);
    REQUIRE_THROWS(commandFromJson(nlohmann::json::array()));
    REQUIRE_THROWS(commandFromJson({{"type", "remove_track"}}));  // missing trackId
}
```

`tests/test_undo_stack.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "lpc/commands.h"
#include "lpc/undo_stack.h"

using namespace lpc;

namespace {
Track audioTrack(std::mt19937_64& rng, const char* name) {
    Track t;
    t.id = Uuid::random(rng);
    t.kind = TrackKind::Audio;
    t.name = name;
    return t;
}
}  // namespace

TEST_CASE("undo stack: execute, undo, redo", "[undo]") {
    std::mt19937_64 rng(5);
    Project p(Uuid::random(rng));
    const Project initial = p;
    UndoStack s;
    REQUIRE_FALSE(s.canUndo());
    REQUIRE_FALSE(s.canRedo());

    REQUIRE_FALSE(s.execute(p, makeAddTrack(audioTrack(rng, "A"))).has_value());
    REQUIRE_FALSE(s.execute(p, makeAddTrack(audioTrack(rng, "B"))).has_value());
    REQUIRE(p.tracks.size() == 3);
    const Project afterTwo = p;

    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE(p.tracks.size() == 2);
    REQUIRE(s.canRedo());
    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE(p == initial);
    REQUIRE_FALSE(s.canUndo());

    REQUIRE_FALSE(s.redo(p).has_value());
    REQUIRE_FALSE(s.redo(p).has_value());
    REQUIRE(p == afterTwo);
}

TEST_CASE("undo stack: a new command clears the redo history", "[undo]") {
    std::mt19937_64 rng(6);
    Project p(Uuid::random(rng));
    UndoStack s;
    REQUIRE_FALSE(s.execute(p, makeAddTrack(audioTrack(rng, "A"))).has_value());
    REQUIRE_FALSE(s.undo(p).has_value());
    REQUIRE(s.canRedo());
    REQUIRE_FALSE(s.execute(p, makeAddTrack(audioTrack(rng, "B"))).has_value());
    REQUIRE_FALSE(s.canRedo());
}

TEST_CASE("undo stack: a rejected command changes nothing and is not recorded", "[undo]") {
    std::mt19937_64 rng(7);
    Project p(Uuid::random(rng));
    UndoStack s;
    const Project before = p;
    const auto err = s.execute(p, makeRemoveTrack(Uuid::random(rng)));
    REQUIRE(err.has_value());
    REQUIRE(err->code == "not_found");
    REQUIRE(p == before);
    REQUIRE_FALSE(s.canUndo());
}

TEST_CASE("undo stack: undo and redo on empty history report an error", "[undo]") {
    Project p;
    UndoStack s;
    REQUIRE(s.undo(p)->code == "nothing_to_undo");
    REQUIRE(s.redo(p)->code == "nothing_to_redo");
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build --config Debug --target lpc_tests`
Expected: FAIL to compile, `Cannot open include file: 'lpc/commands.h'`.

- [ ] **Step 3: Implement `processor_ids.h`, `command.h`, `commands.h`, `undo_stack.h`**

`core/include/lpc/processor_ids.h`:

```cpp
#pragma once
#include <string_view>

namespace lpc {

inline constexpr const char* kProcGain = "builtin.gain";
inline constexpr const char* kProcSine = "builtin.sine";

inline bool isKnownEffect(std::string_view id) { return id == kProcGain; }
inline bool isKnownInstrument(std::string_view id) { return id == kProcSine; }

}  // namespace lpc
```

`core/include/lpc/command.h`:

```cpp
#pragma once
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "lpc/model.h"

namespace lpc {

struct CommandError {
    std::string code;     // stable, machine-readable (for clients and AIs)
    std::string message;  // human-readable
};

class Command;
using CommandPtr = std::unique_ptr<Command>;

struct ApplyResult {
    CommandPtr inverse;                 // set on success
    std::optional<CommandError> error;  // set on failure; the project is then unchanged
    bool ok() const { return !error.has_value(); }
};

class Command {
public:
    virtual ~Command() = default;
    virtual std::string type() const = 0;
    virtual nlohmann::json toJson() const = 0;
    virtual ApplyResult apply(Project& project) const = 0;  // all-or-nothing
};

}  // namespace lpc
```

`core/include/lpc/commands.h`:

```cpp
#pragma once
#include <optional>
#include <vector>

#include "lpc/command.h"

namespace lpc {

struct StripPatch {
    std::optional<float> gainDb;
    std::optional<float> pan;
    std::optional<bool> mute;
    std::optional<bool> solo;
};

CommandPtr makeAddTrack(Track track, int index = -1);  // index -1 appends
CommandPtr makeRemoveTrack(Uuid trackId);
CommandPtr makeSetStrip(Uuid trackId, StripPatch patch);
CommandPtr makeSetTempo(Ticks tick, double bpm);
CommandPtr makeRemoveTempo(Ticks tick);
CommandPtr makeAddMedia(MediaItem item);
CommandPtr makeRemoveMedia(Uuid mediaId);

CommandPtr commandFromJson(const nlohmann::json& j);  // throws std::runtime_error

}  // namespace lpc
```

`core/include/lpc/undo_stack.h`:

```cpp
#pragma once
#include <memory>
#include <optional>
#include <vector>

#include "lpc/command.h"

namespace lpc {

class UndoStack {
public:
    // Applies the command and records it. On failure nothing is recorded and the project is unchanged.
    std::optional<CommandError> execute(Project& project, CommandPtr command);
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    std::optional<CommandError> undo(Project& project);
    std::optional<CommandError> redo(Project& project);
    void clear();

private:
    struct Entry {
        std::shared_ptr<const Command> forward;
        std::shared_ptr<const Command> inverse;
    };
    std::vector<Entry> undo_;
    std::vector<Entry> redo_;
};

}  // namespace lpc
```

`core/src/undo_stack.cpp`:

```cpp
#include "lpc/undo_stack.h"

namespace lpc {

std::optional<CommandError> UndoStack::execute(Project& project, CommandPtr command) {
    ApplyResult r = command->apply(project);
    if (!r.ok()) return r.error;
    undo_.push_back({std::shared_ptr<const Command>(std::move(command)), std::shared_ptr<const Command>(std::move(r.inverse))});
    redo_.clear();
    return std::nullopt;
}

std::optional<CommandError> UndoStack::undo(Project& project) {
    if (undo_.empty()) return CommandError{"nothing_to_undo", "undo history is empty"};
    ApplyResult r = undo_.back().inverse->apply(project);
    if (!r.ok()) return r.error;
    redo_.push_back(undo_.back());
    undo_.pop_back();
    return std::nullopt;
}

std::optional<CommandError> UndoStack::redo(Project& project) {
    if (redo_.empty()) return CommandError{"nothing_to_redo", "redo history is empty"};
    ApplyResult r = redo_.back().forward->apply(project);
    if (!r.ok()) return r.error;
    undo_.push_back(redo_.back());
    redo_.pop_back();
    return std::nullopt;
}

void UndoStack::clear() {
    undo_.clear();
    redo_.clear();
}

}  // namespace lpc
```

- [ ] **Step 4: Implement `commands.cpp`**

`core/src/commands.cpp`:

```cpp
#include "lpc/commands.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

#include "lpc/model_json.h"
#include "lpc/processor_ids.h"

namespace lpc {

namespace {

using nlohmann::json;

ApplyResult fail(CommandError e) {
    ApplyResult r;
    r.error = std::move(e);
    return r;
}
ApplyResult fail(std::string code, std::string message) { return fail(CommandError{std::move(code), std::move(message)}); }
ApplyResult success(CommandPtr inverse) {
    ApplyResult r;
    r.inverse = std::move(inverse);
    return r;
}

using MaybeError = std::optional<CommandError>;

bool isBusLike(TrackKind k) { return k == TrackKind::Bus || k == TrackKind::Aux; }
bool isSourceKind(TrackKind k) { return k == TrackKind::Audio || k == TrackKind::Midi || k == TrackKind::Instrument; }
bool inRange(float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; }

MaybeError checkStripValues(float gainDb, float pan) {
    if (!inRange(gainDb, -96.0f, 24.0f)) return CommandError{"bad_value", "gainDb must be a number in [-96, 24]"};
    if (!inRange(pan, -1.0f, 1.0f)) return CommandError{"bad_value", "pan must be a number in [-1, 1]"};
    return std::nullopt;
}

// A region is valid for a given track kind and project (media must exist, notes must be sane).
MaybeError checkRegion(const Project& p, TrackKind kind, const Region& r) {
    if (r.id.isNull()) return CommandError{"duplicate_id", "region id is missing"};
    if (r.start < 0 || r.length <= 0) return CommandError{"bad_region", "region needs start >= 0 and length > 0"};
    if (!inRange(r.gainDb, -96.0f, 24.0f)) return CommandError{"bad_value", "region gainDb must be in [-96, 24]"};
    if (kind == TrackKind::Audio) {
        if (r.mediaId.isNull() || !p.findMedia(r.mediaId))
            return CommandError{"bad_region", "audio region needs a mediaId present in the media pool"};
        if (!r.notes.empty()) return CommandError{"bad_region", "audio regions cannot contain notes"};
        if (r.sourceOffsetFrames < 0) return CommandError{"bad_region", "sourceOffsetFrames must be >= 0"};
    } else if (kind == TrackKind::Midi || kind == TrackKind::Instrument) {
        if (!r.mediaId.isNull()) return CommandError{"bad_region", "MIDI regions cannot reference media"};
        if (r.timeBase != TimeBase::Musical) return CommandError{"bad_region", "MIDI regions must be musical"};
        for (const MidiNote& n : r.notes) {
            if (n.start < 0 || n.length <= 0 || n.note > 127 || n.velocity < 1 || n.velocity > 127)
                return CommandError{"bad_region", "invalid MIDI note"};
        }
    } else {
        return CommandError{"invalid_kind", "this track kind cannot hold regions"};
    }
    return std::nullopt;
}

MaybeError checkSendFields(const Project& p, const Uuid& owner, const Send& s) {
    if (s.id.isNull()) return CommandError{"duplicate_id", "send id is missing"};
    if (!inRange(s.levelDb, -96.0f, 12.0f)) return CommandError{"bad_value", "send levelDb must be in [-96, 12]"};
    const Track* target = p.findTrack(s.targetTrackId);
    if (!target || !isBusLike(target->kind) || target->id == owner)
        return CommandError{"bad_target", "send target must be another existing bus or aux track"};
    return std::nullopt;
}

// True when `goal` can be reached from `from` by following outputs and sends.
bool reaches(const Project& p, const Uuid& from, const Uuid& goal) {
    std::vector<Uuid> stack{from};
    std::unordered_set<Uuid> seen;
    while (!stack.empty()) {
        const Uuid cur = stack.back();
        stack.pop_back();
        if (cur == goal) return true;
        if (!seen.insert(cur).second) continue;
        const Track* t = p.findTrack(cur);
        if (!t) continue;
        if (!t->strip.output.isNull()) stack.push_back(t->strip.output);
        for (const Send& s : t->strip.sends) stack.push_back(s.targetTrackId);
    }
    return false;
}

std::unordered_set<Uuid> allRegionIds(const Project& p) {
    std::unordered_set<Uuid> ids;
    for (const Track& t : p.tracks)
        for (const Region& r : t.regions) ids.insert(r.id);
    return ids;
}

std::unordered_set<Uuid> allSendIds(const Project& p) {
    std::unordered_set<Uuid> ids;
    for (const Track& t : p.tracks)
        for (const Send& s : t.strip.sends) ids.insert(s.id);
    return ids;
}

bool validRelativeMediaPath(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.find(':') != std::string::npos ||
        path.find('\\') != std::string::npos)
        return false;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

// ---------------------------------------------------------------- add_track / remove_track

class AddTrackCmd final : public Command {
public:
    AddTrackCmd(Track track, int index) : track_(std::move(track)), index_(index) {}
    std::string type() const override { return "add_track"; }
    json toJson() const override { return {{"type", type()}, {"track", track_}, {"index", index_}}; }

    ApplyResult apply(Project& p) const override {
        if (auto e = validate(p)) return fail(*e);
        const int idx = index_ < 0 ? static_cast<int>(p.tracks.size()) : index_;
        p.tracks.insert(p.tracks.begin() + idx, track_);
        return success(makeRemoveTrack(track_.id));
    }

private:
    MaybeError validate(const Project& p) const {
        if (track_.kind == TrackKind::Master) return CommandError{"invalid_kind", "a master track already exists"};
        if (track_.id.isNull() || p.findTrack(track_.id)) return CommandError{"duplicate_id", "track id missing or already used"};
        if (index_ < -1 || index_ > static_cast<int>(p.tracks.size())) return CommandError{"bad_index", "track index out of range"};
        if (auto e = checkStripValues(track_.strip.gainDb, track_.strip.pan)) return e;
        if (!track_.strip.output.isNull()) {
            const Track* out = p.findTrack(track_.strip.output);
            if (!out || !isBusLike(out->kind))
                return CommandError{"bad_output", "output must be an existing bus or aux track (or null for master)"};
        }
        const bool wantsInstrument = track_.kind == TrackKind::Instrument;
        if (wantsInstrument != track_.instrument.has_value() ||
            (wantsInstrument && !isKnownInstrument(track_.instrument->processorId)))
            return CommandError{"invalid_kind", "exactly instrument tracks need a known instrument"};
        for (const ProcessorRef& ins : track_.strip.inserts)
            if (!isKnownEffect(ins.processorId)) return CommandError{"bad_value", "unknown insert processor: " + ins.processorId};
        auto sendIds = allSendIds(p);
        for (const Send& s : track_.strip.sends) {
            if (auto e = checkSendFields(p, track_.id, s)) return e;
            if (!sendIds.insert(s.id).second) return CommandError{"duplicate_id", "send id already used"};
        }
        auto regionIds = allRegionIds(p);
        for (const Region& r : track_.regions) {
            if (auto e = checkRegion(p, track_.kind, r)) return e;
            if (!regionIds.insert(r.id).second) return CommandError{"duplicate_id", "region id already used"};
        }
        return std::nullopt;
    }

    Track track_;
    int index_;
};

class RemoveTrackCmd final : public Command {
public:
    explicit RemoveTrackCmd(Uuid id) : id_(id) {}
    std::string type() const override { return "remove_track"; }
    json toJson() const override { return {{"type", type()}, {"trackId", id_}}; }

    ApplyResult apply(Project& p) const override {
        auto it = std::find_if(p.tracks.begin(), p.tracks.end(), [&](const Track& t) { return t.id == id_; });
        if (it == p.tracks.end()) return fail("not_found", "no such track");
        if (it->kind == TrackKind::Master) return fail("invalid_kind", "the master track cannot be removed");
        for (const Track& t : p.tracks) {
            if (t.id == id_) continue;
            const bool routed = t.strip.output == id_ ||
                                std::any_of(t.strip.sends.begin(), t.strip.sends.end(),
                                            [&](const Send& s) { return s.targetTrackId == id_; });
            if (routed) return fail("in_use", "another track routes to this track");
        }
        const int index = static_cast<int>(it - p.tracks.begin());
        Track removed = std::move(*it);
        p.tracks.erase(it);
        return success(makeAddTrack(std::move(removed), index));
    }

private:
    Uuid id_;
};

// ---------------------------------------------------------------- set_strip

class SetStripCmd final : public Command {
public:
    SetStripCmd(Uuid id, StripPatch patch) : id_(id), patch_(patch) {}
    std::string type() const override { return "set_strip"; }
    json toJson() const override {
        json j = {{"type", type()}, {"trackId", id_}};
        if (patch_.gainDb) j["gainDb"] = *patch_.gainDb;
        if (patch_.pan) j["pan"] = *patch_.pan;
        if (patch_.mute) j["mute"] = *patch_.mute;
        if (patch_.solo) j["solo"] = *patch_.solo;
        return j;
    }

    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(id_);
        if (!t) return fail("not_found", "no such track");
        if (auto e = checkStripValues(patch_.gainDb.value_or(t->strip.gainDb), patch_.pan.value_or(t->strip.pan)))
            return fail(*e);
        StripPatch prev;
        if (patch_.gainDb) { prev.gainDb = t->strip.gainDb; t->strip.gainDb = *patch_.gainDb; }
        if (patch_.pan) { prev.pan = t->strip.pan; t->strip.pan = *patch_.pan; }
        if (patch_.mute) { prev.mute = t->strip.mute; t->strip.mute = *patch_.mute; }
        if (patch_.solo) { prev.solo = t->strip.solo; t->strip.solo = *patch_.solo; }
        return success(makeSetStrip(id_, prev));
    }

private:
    Uuid id_;
    StripPatch patch_;
};

// ---------------------------------------------------------------- tempo

class SetTempoCmd final : public Command {
public:
    SetTempoCmd(Ticks tick, double bpm) : tick_(tick), bpm_(bpm) {}
    std::string type() const override { return "set_tempo"; }
    json toJson() const override { return {{"type", type()}, {"tick", tick_}, {"bpm", bpm_}}; }

    ApplyResult apply(Project& p) const override {
        const auto previous = p.tempoMap.tempoEventAt(tick_);
        if (!p.tempoMap.setTempo(tick_, bpm_)) return fail("bad_tempo", "tempo must be 20..999 bpm at a tick >= 0");
        return success(previous ? makeSetTempo(tick_, *previous) : makeRemoveTempo(tick_));
    }

private:
    Ticks tick_;
    double bpm_;
};

class RemoveTempoCmd final : public Command {
public:
    explicit RemoveTempoCmd(Ticks tick) : tick_(tick) {}
    std::string type() const override { return "remove_tempo"; }
    json toJson() const override { return {{"type", type()}, {"tick", tick_}}; }

    ApplyResult apply(Project& p) const override {
        const auto previous = p.tempoMap.tempoEventAt(tick_);
        if (!previous || !p.tempoMap.removeTempo(tick_))
            return fail("bad_tempo", "no removable tempo event at this tick (tick 0 cannot be removed)");
        return success(makeSetTempo(tick_, *previous));
    }

private:
    Ticks tick_;
};

// ---------------------------------------------------------------- media

class AddMediaCmd final : public Command {
public:
    explicit AddMediaCmd(MediaItem item) : item_(std::move(item)) {}
    std::string type() const override { return "add_media"; }
    json toJson() const override { return {{"type", type()}, {"item", item_}}; }

    ApplyResult apply(Project& p) const override {
        if (item_.id.isNull() || p.findMedia(item_.id)) return fail("duplicate_id", "media id missing or already used");
        if (!validRelativeMediaPath(item_.path)) return fail("bad_media", "media path must be relative, inside the project, with '/' separators");
        if (item_.sampleRate != p.sampleRate) return fail("bad_media", "media sample rate must equal the project sample rate (no resampling)");
        if (item_.channels < 1 || item_.channels > 2) return fail("bad_media", "media must be mono or stereo");
        if (item_.frames < 0) return fail("bad_media", "media frame count must be >= 0");
        p.mediaPool.push_back(item_);
        return success(makeRemoveMedia(item_.id));
    }

private:
    MediaItem item_;
};

class RemoveMediaCmd final : public Command {
public:
    explicit RemoveMediaCmd(Uuid id) : id_(id) {}
    std::string type() const override { return "remove_media"; }
    json toJson() const override { return {{"type", type()}, {"mediaId", id_}}; }

    ApplyResult apply(Project& p) const override {
        auto it = std::find_if(p.mediaPool.begin(), p.mediaPool.end(), [&](const MediaItem& m) { return m.id == id_; });
        if (it == p.mediaPool.end()) return fail("not_found", "no such media");
        for (const Track& t : p.tracks)
            for (const Region& r : t.regions)
                if (r.mediaId == id_) return fail("in_use", "a region still uses this media");
        MediaItem removed = *it;
        p.mediaPool.erase(it);
        return success(makeAddMedia(std::move(removed)));
    }

private:
    Uuid id_;
};

}  // namespace

CommandPtr makeAddTrack(Track track, int index) { return std::make_unique<AddTrackCmd>(std::move(track), index); }
CommandPtr makeRemoveTrack(Uuid trackId) { return std::make_unique<RemoveTrackCmd>(trackId); }
CommandPtr makeSetStrip(Uuid trackId, StripPatch patch) { return std::make_unique<SetStripCmd>(trackId, patch); }
CommandPtr makeSetTempo(Ticks tick, double bpm) { return std::make_unique<SetTempoCmd>(tick, bpm); }
CommandPtr makeRemoveTempo(Ticks tick) { return std::make_unique<RemoveTempoCmd>(tick); }
CommandPtr makeAddMedia(MediaItem item) { return std::make_unique<AddMediaCmd>(std::move(item)); }
CommandPtr makeRemoveMedia(Uuid mediaId) { return std::make_unique<RemoveMediaCmd>(mediaId); }

CommandPtr commandFromJson(const nlohmann::json& j) {
    try {
        if (!j.is_object()) throw std::runtime_error("command must be a JSON object");
        const std::string type = j.at("type").get<std::string>();
        if (type == "add_track") return makeAddTrack(j.at("track").get<Track>(), j.value("index", -1));
        if (type == "remove_track") return makeRemoveTrack(j.at("trackId").get<Uuid>());
        if (type == "set_strip") {
            StripPatch patch;
            if (j.contains("gainDb")) patch.gainDb = j["gainDb"].get<float>();
            if (j.contains("pan")) patch.pan = j["pan"].get<float>();
            if (j.contains("mute")) patch.mute = j["mute"].get<bool>();
            if (j.contains("solo")) patch.solo = j["solo"].get<bool>();
            return makeSetStrip(j.at("trackId").get<Uuid>(), patch);
        }
        if (type == "set_tempo") return makeSetTempo(j.at("tick").get<Ticks>(), j.at("bpm").get<double>());
        if (type == "remove_tempo") return makeRemoveTempo(j.at("tick").get<Ticks>());
        if (type == "add_media") return makeAddMedia(j.at("item").get<MediaItem>());
        if (type == "remove_media") return makeRemoveMedia(j.at("mediaId").get<Uuid>());
        // Task 6 adds its command types above this line.
        throw std::runtime_error("unknown command type: " + type);
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("invalid command: ") + e.what());
    }
}

}  // namespace lpc
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target lpc_tests && build/tests/Debug/lpc_tests.exe "[commands],[undo]"`
Expected: `All tests passed`. Unused-function warnings for `checkRegion`, `reaches`, `isSourceKind` and the id helpers are expected until Task 6 uses them; do not delete them. (If a warning is treated as noise, it is not an error: `/WX` is not enabled.)

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(core): command framework, undo stack, track/strip/tempo/media commands" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

### Task 6: Region, send, insert and transaction commands, plus the undo property test

**Files:**
- Modify: `core/include/lpc/commands.h`, `core/src/commands.cpp`
- Create: `tests/random_commands.h`
- Test: `tests/test_commands_regions.cpp`, `tests/test_undo_property.cpp`

**Interfaces:**
- Consumes: everything from Task 5 (`checkRegion`, `checkSendFields`, `reaches`, `allRegionIds`, `allSendIds`, `isBusLike`, `isSourceKind` are already in the anonymous namespace of `commands.cpp`).
- Produces (namespace `lpc`, in `commands.h`):
  - `CommandPtr makeAddRegion(Uuid trackId, Region region, int index = -1)`
  - `CommandPtr makeRemoveRegion(Uuid regionId)`
  - `CommandPtr makeMoveRegion(Uuid regionId, std::int64_t newStart)`
  - `CommandPtr makeAddSend(Uuid trackId, Send send, int index = -1)`
  - `CommandPtr makeRemoveSend(Uuid sendId)`
  - `CommandPtr makeSetInserts(Uuid trackId, std::vector<ProcessorRef> inserts)`
  - `CommandPtr makeTransaction(std::vector<CommandPtr> commands)` (all-or-nothing; the inverse is the reversed list of inverses)
  - JSON types: `add_region`, `remove_region`, `move_region`, `add_send`, `remove_send`, `set_inserts`, `transaction`.
  - Error codes added: `cycle`.
  - Test helper `lpc::test::randomCommand(const Project&, std::mt19937_64&)` returning a (possibly invalid, possibly null) command.

- [ ] **Step 1: Write the failing tests**

`tests/test_commands_regions.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "lpc/commands.h"
#include "lpc/model_json.h"

using namespace lpc;

namespace {

std::mt19937_64 gRng(123);

struct Fixture {
    Project p{Uuid::random(gRng)};
    Track audio, midi, inst, bus, bus2;
    MediaItem media{Uuid::random(gRng), "audio/a.wav", "h", 48000, 2, 480000};

    static Track track(TrackKind kind, const char* name) {
        Track t;
        t.id = Uuid::random(gRng);
        t.kind = kind;
        t.name = name;
        if (kind == TrackKind::Instrument) t.instrument = ProcessorRef{"builtin.sine", {}, ""};
        return t;
    }

    Fixture() {
        audio = track(TrackKind::Audio, "Audio");
        midi = track(TrackKind::Midi, "Midi");
        inst = track(TrackKind::Instrument, "Keys");
        bus = track(TrackKind::Bus, "Bus");
        bus2 = track(TrackKind::Aux, "Aux");
        REQUIRE(makeAddMedia(media)->apply(p).ok());
        for (const Track* t : {&audio, &midi, &inst, &bus, &bus2}) REQUIRE(makeAddTrack(*t)->apply(p).ok());
    }

    Region audioRegion() const {
        Region r;
        r.id = Uuid::random(gRng);
        r.start = 0;
        r.length = 4 * kPPQ;
        r.mediaId = media.id;
        return r;
    }
    static Region midiRegion() {
        Region r;
        r.id = Uuid::random(gRng);
        r.start = kPPQ;
        r.length = 4 * kPPQ;
        r.notes.push_back({0, kPPQ, 60, 100});
        return r;
    }
};

void requireRoundTrip(Project& p, const Command& cmd) {
    const Project before = p;
    ApplyResult r = cmd.apply(p);
    REQUIRE(r.ok());
    REQUIRE(r.inverse->apply(p).ok());
    REQUIRE(p == before);
    REQUIRE(cmd.apply(p).ok());
}

void requireRejected(Project& p, const Command& cmd, const char* code) {
    const Project before = p;
    const ApplyResult r = cmd.apply(p);
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error->code == code);
    REQUIRE(p == before);
}

}  // namespace

TEST_CASE("regions: add, move and remove round-trip", "[commands][regions]") {
    Fixture f;
    const Region ar = f.audioRegion();
    const Region mr = Fixture::midiRegion();
    requireRoundTrip(f.p, *makeAddRegion(f.audio.id, ar));
    requireRoundTrip(f.p, *makeAddRegion(f.inst.id, mr));
    requireRoundTrip(f.p, *makeAddRegion(f.midi.id, Fixture::midiRegion()));
    requireRoundTrip(f.p, *makeMoveRegion(ar.id, 2 * kPPQ));
    REQUIRE(f.p.findTrack(f.audio.id)->regions[0].start == 2 * kPPQ);
    requireRoundTrip(f.p, *makeRemoveRegion(ar.id));
    REQUIRE(f.p.findTrack(f.audio.id)->regions.empty());
}

TEST_CASE("regions: removal restores the original position in the list", "[commands][regions]") {
    Fixture f;
    const Region a = f.audioRegion(), b = f.audioRegion(), c = f.audioRegion();
    for (const Region* r : {&a, &b, &c}) REQUIRE(makeAddRegion(f.audio.id, *r)->apply(f.p).ok());
    ApplyResult r = makeRemoveRegion(b.id)->apply(f.p);
    REQUIRE(r.ok());
    REQUIRE(r.inverse->apply(f.p).ok());
    const auto& regions = f.p.findTrack(f.audio.id)->regions;
    REQUIRE(regions[0].id == a.id);
    REQUIRE(regions[1].id == b.id);
    REQUIRE(regions[2].id == c.id);
}

TEST_CASE("regions: invalid regions are rejected and change nothing", "[commands][regions]") {
    Fixture f;
    const Region good = f.audioRegion();
    REQUIRE(makeAddRegion(f.audio.id, good)->apply(f.p).ok());

    requireRejected(f.p, *makeAddRegion(Uuid::random(gRng), f.audioRegion()), "not_found");
    requireRejected(f.p, *makeAddRegion(f.audio.id, good), "duplicate_id");     // same region id
    requireRejected(f.p, *makeAddRegion(f.inst.id, good), "duplicate_id");      // id is global

    Region r = f.audioRegion();
    r.length = 0;
    requireRejected(f.p, *makeAddRegion(f.audio.id, r), "bad_region");
    r = f.audioRegion();
    r.length = -5;
    requireRejected(f.p, *makeAddRegion(f.audio.id, r), "bad_region");
    r = f.audioRegion();
    r.start = -1;
    requireRejected(f.p, *makeAddRegion(f.audio.id, r), "bad_region");
    r = f.audioRegion();
    r.mediaId = Uuid{};
    requireRejected(f.p, *makeAddRegion(f.audio.id, r), "bad_region");
    r = f.audioRegion();
    r.mediaId = Uuid::random(gRng);  // not in the pool
    requireRejected(f.p, *makeAddRegion(f.audio.id, r), "bad_region");
    r = f.audioRegion();
    r.sourceOffsetFrames = -1;
    requireRejected(f.p, *makeAddRegion(f.audio.id, r), "bad_region");
    r = f.audioRegion();
    r.gainDb = 500.0f;
    requireRejected(f.p, *makeAddRegion(f.audio.id, r), "bad_value");

    r = Fixture::midiRegion();
    r.mediaId = f.media.id;
    requireRejected(f.p, *makeAddRegion(f.inst.id, r), "bad_region");
    r = Fixture::midiRegion();
    r.timeBase = TimeBase::Absolute;
    requireRejected(f.p, *makeAddRegion(f.inst.id, r), "bad_region");
    r = Fixture::midiRegion();
    r.notes[0].velocity = 0;
    requireRejected(f.p, *makeAddRegion(f.inst.id, r), "bad_region");
    r = Fixture::midiRegion();
    r.notes[0].note = 200;
    requireRejected(f.p, *makeAddRegion(f.inst.id, r), "bad_region");
    r = Fixture::midiRegion();
    r.notes[0].length = 0;
    requireRejected(f.p, *makeAddRegion(f.inst.id, r), "bad_region");
    requireRejected(f.p, *makeAddRegion(f.bus.id, Fixture::midiRegion()), "invalid_kind");
    requireRejected(f.p, *makeAddRegion(f.audio.id, f.audioRegion(), 7), "bad_index");

    requireRejected(f.p, *makeMoveRegion(good.id, -1), "bad_region");
    requireRejected(f.p, *makeMoveRegion(Uuid::random(gRng), 0), "not_found");
    requireRejected(f.p, *makeRemoveRegion(Uuid::random(gRng)), "not_found");
}

TEST_CASE("regions: media in use cannot be removed until the region is gone", "[commands][regions]") {
    Fixture f;
    const Region ar = f.audioRegion();
    REQUIRE(makeAddRegion(f.audio.id, ar)->apply(f.p).ok());
    requireRejected(f.p, *makeRemoveMedia(f.media.id), "in_use");
    REQUIRE(makeRemoveRegion(ar.id)->apply(f.p).ok());
    REQUIRE(makeRemoveMedia(f.media.id)->apply(f.p).ok());
}

TEST_CASE("sends: add and remove round-trip, and routing is validated", "[commands][sends]") {
    Fixture f;
    const Send s{Uuid::random(gRng), f.bus.id, -6.0f, false};
    requireRoundTrip(f.p, *makeAddSend(f.audio.id, s));
    requireRoundTrip(f.p, *makeRemoveSend(s.id));

    REQUIRE(makeAddSend(f.audio.id, s)->apply(f.p).ok());
    requireRejected(f.p, *makeAddSend(f.audio.id, s), "duplicate_id");
    requireRejected(f.p, *makeAddSend(f.midi.id, s), "duplicate_id");  // id is global
    requireRejected(f.p, *makeAddSend(Uuid::random(gRng), Send{Uuid::random(gRng), f.bus.id, 0, false}), "not_found");
    requireRejected(f.p, *makeAddSend(f.midi.id, Send{Uuid::random(gRng), f.audio.id, 0, false}), "bad_target");
    requireRejected(f.p, *makeAddSend(f.midi.id, Send{Uuid::random(gRng), Uuid::random(gRng), 0, false}), "bad_target");
    requireRejected(f.p, *makeAddSend(f.bus.id, Send{Uuid::random(gRng), f.bus.id, 0, false}), "bad_target");  // itself
    requireRejected(f.p, *makeAddSend(f.midi.id, Send{Uuid::random(gRng), f.bus.id, 50.0f, false}), "bad_value");
    requireRejected(f.p, *makeAddSend(f.p.master()->id, Send{Uuid::random(gRng), f.bus.id, 0, false}), "invalid_kind");
    requireRejected(f.p, *makeRemoveSend(Uuid::random(gRng)), "not_found");
}

TEST_CASE("sends: routing cycles are rejected", "[commands][sends]") {
    Fixture f;
    REQUIRE(makeAddSend(f.bus.id, Send{Uuid::random(gRng), f.bus2.id, 0, false})->apply(f.p).ok());  // bus -> aux
    requireRejected(f.p, *makeAddSend(f.bus2.id, Send{Uuid::random(gRng), f.bus.id, 0, false}), "cycle");

    // a cycle through an output route is also a cycle
    Track chained = Fixture::track(TrackKind::Bus, "Chained");
    chained.strip.output = f.bus.id;  // chained -> bus -> aux
    REQUIRE(makeAddTrack(chained)->apply(f.p).ok());
    requireRejected(f.p, *makeAddSend(f.bus2.id, Send{Uuid::random(gRng), chained.id, 0, false}), "cycle");
}

TEST_CASE("inserts: set, undo and validation", "[commands][inserts]") {
    Fixture f;
    const std::vector<ProcessorRef> chain = {{"builtin.gain", {{"gainDb", -3.0}}, ""}};
    requireRoundTrip(f.p, *makeSetInserts(f.audio.id, chain));
    REQUIRE(f.p.findTrack(f.audio.id)->strip.inserts == chain);
    requireRejected(f.p, *makeSetInserts(f.audio.id, {{"vendor.unknown", {}, ""}}), "bad_value");
    requireRejected(f.p, *makeSetInserts(f.audio.id, {{"builtin.sine", {}, ""}}), "bad_value");  // instrument, not effect
    requireRejected(f.p, *makeSetInserts(Uuid::random(gRng), chain), "not_found");
}

TEST_CASE("transaction: applies all commands as one undo step", "[commands][transaction]") {
    Fixture f;
    const Region ar = f.audioRegion();
    std::vector<CommandPtr> cmds;
    cmds.push_back(makeAddRegion(f.audio.id, ar));
    cmds.push_back(makeMoveRegion(ar.id, kPPQ));  // depends on the previous command
    StripPatch patch;
    patch.gainDb = -9.0f;
    cmds.push_back(makeSetStrip(f.audio.id, patch));
    requireRoundTrip(f.p, *makeTransaction(std::move(cmds)));
}

TEST_CASE("transaction: a failing command rolls everything back", "[commands][transaction]") {
    Fixture f;
    const Project before = f.p;
    std::vector<CommandPtr> cmds;
    cmds.push_back(makeAddRegion(f.audio.id, f.audioRegion()));
    StripPatch patch;
    patch.gainDb = -9.0f;
    cmds.push_back(makeSetStrip(f.audio.id, patch));
    cmds.push_back(makeRemoveTrack(Uuid::random(gRng)));  // fails
    const ApplyResult r = makeTransaction(std::move(cmds))->apply(f.p);
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error->code == "not_found");
    REQUIRE(r.error->message.find("command 2") != std::string::npos);
    REQUIRE(f.p == before);
}

TEST_CASE("transaction: empty transaction is a valid no-op", "[commands][transaction]") {
    Fixture f;
    const Project before = f.p;
    ApplyResult r = makeTransaction({})->apply(f.p);
    REQUIRE(r.ok());
    REQUIRE(f.p == before);
    REQUIRE(r.inverse->apply(f.p).ok());
}

TEST_CASE("new commands survive a JSON round trip", "[commands][json]") {
    Fixture f;
    const Region ar = f.audioRegion();
    std::vector<CommandPtr> inner;
    inner.push_back(makeMoveRegion(ar.id, 5));
    std::vector<CommandPtr> cmds;
    cmds.push_back(makeAddRegion(f.audio.id, ar, 0));
    cmds.push_back(makeRemoveRegion(ar.id));
    cmds.push_back(makeMoveRegion(ar.id, 960));
    cmds.push_back(makeAddSend(f.audio.id, Send{Uuid::random(gRng), f.bus.id, -3.0f, true}, 0));
    cmds.push_back(makeRemoveSend(Uuid::random(gRng)));
    cmds.push_back(makeSetInserts(f.audio.id, {{"builtin.gain", {{"gainDb", 1.5}}, ""}}));
    cmds.push_back(makeTransaction(std::move(inner)));
    for (const auto& c : cmds) {
        const nlohmann::json j = c->toJson();
        REQUIRE(commandFromJson(j)->toJson() == j);
    }
}
```

`tests/random_commands.h`:

```cpp
#pragma once
#include <random>
#include <string>
#include <vector>

#include "lpc/commands.h"
#include "lpc/processor_ids.h"

namespace lpc::test {

// Returns a random command that is valid or invalid depending on the current project (so rejected
// commands are exercised too), or nullptr when the chosen kind cannot be built from the current state.
inline CommandPtr randomCommand(const Project& p, std::mt19937_64& rng) {
    auto pick = [&](std::size_t n) { return static_cast<std::size_t>(rng() % n); };
    auto chance = [&](int percent) { return static_cast<int>(rng() % 100) < percent; };

    std::vector<const Track*> busLike, sources, nonMaster, withRegions, withSends;
    for (const Track& t : p.tracks) {
        if (t.kind == TrackKind::Bus || t.kind == TrackKind::Aux) busLike.push_back(&t);
        if (t.kind == TrackKind::Audio || t.kind == TrackKind::Midi || t.kind == TrackKind::Instrument) sources.push_back(&t);
        if (t.kind != TrackKind::Master) nonMaster.push_back(&t);
        if (!t.regions.empty()) withRegions.push_back(&t);
        if (!t.strip.sends.empty()) withSends.push_back(&t);
    }
    std::vector<const Track*> senders = sources;
    senders.insert(senders.end(), busLike.begin(), busLike.end());

    switch (pick(13)) {
        case 0: {  // add track
            static const TrackKind kinds[] = {TrackKind::Audio, TrackKind::Midi, TrackKind::Instrument, TrackKind::Bus, TrackKind::Aux};
            Track t;
            t.id = Uuid::random(rng);
            t.kind = kinds[pick(5)];
            t.name = "T" + std::to_string(pick(1000));
            if (t.kind == TrackKind::Instrument) t.instrument = ProcessorRef{kProcSine, {}, ""};
            if (!busLike.empty() && chance(30)) t.strip.output = busLike[pick(busLike.size())]->id;
            return makeAddTrack(std::move(t));
        }
        case 1:  // remove track (often rejected with in_use)
            if (nonMaster.empty()) return nullptr;
            return makeRemoveTrack(nonMaster[pick(nonMaster.size())]->id);
        case 2: {  // set strip
            if (nonMaster.empty()) return nullptr;
            StripPatch patch;
            if (chance(50)) patch.gainDb = static_cast<float>(static_cast<int>(pick(60)) - 40);
            if (chance(50)) patch.pan = static_cast<float>(static_cast<int>(pick(21)) - 10) / 10.0f;
            if (chance(30)) patch.mute = chance(50);
            if (chance(30)) patch.solo = chance(50);
            return makeSetStrip(nonMaster[pick(nonMaster.size())]->id, patch);
        }
        case 3:
            return makeSetTempo(static_cast<Ticks>(pick(8)) * kPPQ * 4, 60.0 + static_cast<double>(pick(120)));
        case 4:
            return makeRemoveTempo(static_cast<Ticks>(pick(8)) * kPPQ * 4);
        case 5:
            return makeAddMedia(MediaItem{Uuid::random(rng), "audio/m" + std::to_string(pick(1000)) + ".wav", "h", p.sampleRate, 2, 48000});
        case 6:
            if (p.mediaPool.empty()) return nullptr;
            return makeRemoveMedia(p.mediaPool[pick(p.mediaPool.size())].id);
        case 7: {  // add region
            if (sources.empty()) return nullptr;
            const Track& t = *sources[pick(sources.size())];
            Region r;
            r.id = Uuid::random(rng);
            r.start = static_cast<Ticks>(pick(16)) * kPPQ;
            r.length = static_cast<Ticks>(1 + pick(8)) * kPPQ;
            if (t.kind == TrackKind::Audio) {
                if (p.mediaPool.empty()) return nullptr;
                r.mediaId = p.mediaPool[pick(p.mediaPool.size())].id;
                if (chance(30)) {
                    r.timeBase = TimeBase::Absolute;
                    r.start *= 1000;
                    r.length *= 1000;
                }
            } else {
                for (std::size_t i = 0, n = pick(4); i < n; ++i)
                    r.notes.push_back({static_cast<Ticks>(pick(4)) * kPPQ / 2, kPPQ / 2,
                                       static_cast<std::uint8_t>(36 + pick(48)), static_cast<std::uint8_t>(1 + pick(127))});
            }
            return makeAddRegion(t.id, std::move(r));
        }
        case 8:
            if (withRegions.empty()) return nullptr;
            {
                const Track& t = *withRegions[pick(withRegions.size())];
                return makeRemoveRegion(t.regions[pick(t.regions.size())].id);
            }
        case 9:
            if (withRegions.empty()) return nullptr;
            {
                const Track& t = *withRegions[pick(withRegions.size())];
                return makeMoveRegion(t.regions[pick(t.regions.size())].id, static_cast<Ticks>(pick(32)) * kPPQ);
            }
        case 10: {  // add send (cycles and self-sends are rejected naturally)
            if (senders.empty() || busLike.empty()) return nullptr;
            const Track& from = *senders[pick(senders.size())];
            const Track& to = *busLike[pick(busLike.size())];
            return makeAddSend(from.id, Send{Uuid::random(rng), to.id, static_cast<float>(-static_cast<int>(pick(20))), chance(30)});
        }
        case 11:
            if (withSends.empty()) return nullptr;
            {
                const Track& t = *withSends[pick(withSends.size())];
                return makeRemoveSend(t.strip.sends[pick(t.strip.sends.size())].id);
            }
        case 12: {  // set inserts on any track, master included
            std::vector<ProcessorRef> chain;
            for (std::size_t i = 0, n = pick(3); i < n; ++i)
                chain.push_back(ProcessorRef{kProcGain, {{"gainDb", static_cast<double>(pick(12)) - 6.0}}, ""});
            return makeSetInserts(p.tracks[pick(p.tracks.size())].id, std::move(chain));
        }
        default:
            return nullptr;
    }
}

}  // namespace lpc::test
```

`tests/test_undo_property.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "lpc/commands.h"
#include "lpc/undo_stack.h"
#include "random_commands.h"

using namespace lpc;

TEST_CASE("undo: random command sequences undo and redo exactly", "[undo][property]") {
    for (std::uint64_t seed = 1; seed <= 25; ++seed) {
        CAPTURE(seed);
        std::mt19937_64 rng(seed);
        Project p(Uuid::random(rng));
        std::vector<Project> snapshots{p};  // snapshots[i] = state after i accepted commands
        std::vector<nlohmann::json> log;
        UndoStack stack;
        int rejected = 0;

        for (int i = 0; i < 250; ++i) {
            CommandPtr c = test::randomCommand(p, rng);
            if (!c) continue;
            const nlohmann::json j = c->toJson();
            const Project before = p;
            const auto err = stack.execute(p, std::move(c));
            if (err) {
                ++rejected;
                REQUIRE(p == before);  // a rejected command changes nothing
            } else {
                log.push_back(j);
                snapshots.push_back(p);
            }
        }
        REQUIRE(snapshots.size() > 30);  // the generator must produce mostly valid commands
        const Project finalState = p;

        // undo step by step: every intermediate state matches the recorded snapshot
        for (std::size_t i = snapshots.size() - 1; i > 0; --i) {
            REQUIRE_FALSE(stack.undo(p).has_value());
            REQUIRE(p == snapshots[i - 1]);
        }
        REQUIRE_FALSE(stack.canUndo());

        // and redo walks forward through the same states
        for (std::size_t i = 1; i < snapshots.size(); ++i) {
            REQUIRE_FALSE(stack.redo(p).has_value());
            REQUIRE(p == snapshots[i]);
        }

        // replaying the serialized commands on a fresh project reproduces the final state
        Project replay(finalState.master()->id);
        for (const nlohmann::json& j : log) REQUIRE(commandFromJson(j)->apply(replay).ok());
        REQUIRE(replay == finalState);
    }
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build --config Debug --target lpc_tests`
Expected: FAIL to compile, `'makeAddRegion': identifier not found`.

- [ ] **Step 3: Declare the new factories**

In `core/include/lpc/commands.h`, add after `makeRemoveMedia`:

```cpp
CommandPtr makeAddRegion(Uuid trackId, Region region, int index = -1);  // index -1 appends
CommandPtr makeRemoveRegion(Uuid regionId);
CommandPtr makeMoveRegion(Uuid regionId, std::int64_t newStart);        // ticks or microseconds, as the region's timeBase
CommandPtr makeAddSend(Uuid trackId, Send send, int index = -1);
CommandPtr makeRemoveSend(Uuid sendId);
CommandPtr makeSetInserts(Uuid trackId, std::vector<ProcessorRef> inserts);
CommandPtr makeTransaction(std::vector<CommandPtr> commands);
```

- [ ] **Step 4: Implement the commands**

In `core/src/commands.cpp`, insert the following block immediately before the line `}  // namespace` that closes the anonymous namespace (the line directly above `CommandPtr makeAddTrack(...)`):

```cpp
// ---------------------------------------------------------------- regions

class AddRegionCmd final : public Command {
public:
    AddRegionCmd(Uuid trackId, Region region, int index) : trackId_(trackId), region_(std::move(region)), index_(index) {}
    std::string type() const override { return "add_region"; }
    json toJson() const override { return {{"type", type()}, {"trackId", trackId_}, {"region", region_}, {"index", index_}}; }

    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(trackId_);
        if (!t) return fail("not_found", "no such track");
        if (allRegionIds(p).count(region_.id)) return fail("duplicate_id", "region id already used");
        if (auto e = checkRegion(p, t->kind, region_)) return fail(*e);
        if (index_ < -1 || index_ > static_cast<int>(t->regions.size())) return fail("bad_index", "region index out of range");
        const int idx = index_ < 0 ? static_cast<int>(t->regions.size()) : index_;
        t->regions.insert(t->regions.begin() + idx, region_);
        return success(makeRemoveRegion(region_.id));
    }

private:
    Uuid trackId_;
    Region region_;
    int index_;
};

class RemoveRegionCmd final : public Command {
public:
    explicit RemoveRegionCmd(Uuid id) : id_(id) {}
    std::string type() const override { return "remove_region"; }
    json toJson() const override { return {{"type", type()}, {"regionId", id_}}; }

    ApplyResult apply(Project& p) const override {
        std::size_t idx = 0;
        Track* t = p.findTrackOfRegion(id_, &idx);
        if (!t) return fail("not_found", "no such region");
        Region removed = std::move(t->regions[idx]);
        t->regions.erase(t->regions.begin() + static_cast<std::ptrdiff_t>(idx));
        return success(makeAddRegion(t->id, std::move(removed), static_cast<int>(idx)));
    }

private:
    Uuid id_;
};

class MoveRegionCmd final : public Command {
public:
    MoveRegionCmd(Uuid id, std::int64_t start) : id_(id), start_(start) {}
    std::string type() const override { return "move_region"; }
    json toJson() const override { return {{"type", type()}, {"regionId", id_}, {"start", start_}}; }

    ApplyResult apply(Project& p) const override {
        std::size_t idx = 0;
        Track* t = p.findTrackOfRegion(id_, &idx);
        if (!t) return fail("not_found", "no such region");
        if (start_ < 0) return fail("bad_region", "region start must be >= 0");
        const std::int64_t old = t->regions[idx].start;
        t->regions[idx].start = start_;
        return success(makeMoveRegion(id_, old));
    }

private:
    Uuid id_;
    std::int64_t start_;
};

// ---------------------------------------------------------------- sends

class AddSendCmd final : public Command {
public:
    AddSendCmd(Uuid trackId, Send send, int index) : trackId_(trackId), send_(send), index_(index) {}
    std::string type() const override { return "add_send"; }
    json toJson() const override { return {{"type", type()}, {"trackId", trackId_}, {"send", send_}, {"index", index_}}; }

    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(trackId_);
        if (!t) return fail("not_found", "no such track");
        if (t->kind == TrackKind::Master) return fail("invalid_kind", "the master track cannot have sends");
        if (allSendIds(p).count(send_.id)) return fail("duplicate_id", "send id already used");
        if (auto e = checkSendFields(p, trackId_, send_)) return fail(*e);
        if (reaches(p, send_.targetTrackId, trackId_)) return fail("cycle", "this send would create a routing loop");
        if (index_ < -1 || index_ > static_cast<int>(t->strip.sends.size())) return fail("bad_index", "send index out of range");
        const int idx = index_ < 0 ? static_cast<int>(t->strip.sends.size()) : index_;
        t->strip.sends.insert(t->strip.sends.begin() + idx, send_);
        return success(makeRemoveSend(send_.id));
    }

private:
    Uuid trackId_;
    Send send_;
    int index_;
};

class RemoveSendCmd final : public Command {
public:
    explicit RemoveSendCmd(Uuid id) : id_(id) {}
    std::string type() const override { return "remove_send"; }
    json toJson() const override { return {{"type", type()}, {"sendId", id_}}; }

    ApplyResult apply(Project& p) const override {
        for (Track& t : p.tracks) {
            auto it = std::find_if(t.strip.sends.begin(), t.strip.sends.end(), [&](const Send& s) { return s.id == id_; });
            if (it == t.strip.sends.end()) continue;
            const Send removed = *it;
            const int idx = static_cast<int>(it - t.strip.sends.begin());
            t.strip.sends.erase(it);
            return success(makeAddSend(t.id, removed, idx));
        }
        return fail("not_found", "no such send");
    }

private:
    Uuid id_;
};

// ---------------------------------------------------------------- inserts

class SetInsertsCmd final : public Command {
public:
    SetInsertsCmd(Uuid trackId, std::vector<ProcessorRef> inserts) : trackId_(trackId), inserts_(std::move(inserts)) {}
    std::string type() const override { return "set_inserts"; }
    json toJson() const override { return {{"type", type()}, {"trackId", trackId_}, {"inserts", inserts_}}; }

    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(trackId_);
        if (!t) return fail("not_found", "no such track");
        for (const ProcessorRef& r : inserts_)
            if (!isKnownEffect(r.processorId)) return fail("bad_value", "unknown insert processor: " + r.processorId);
        std::vector<ProcessorRef> previous = std::move(t->strip.inserts);
        t->strip.inserts = inserts_;
        return success(makeSetInserts(trackId_, std::move(previous)));
    }

private:
    Uuid trackId_;
    std::vector<ProcessorRef> inserts_;
};

// ---------------------------------------------------------------- transaction

class TransactionCmd final : public Command {
public:
    explicit TransactionCmd(std::vector<CommandPtr> cmds) : cmds_(std::move(cmds)) {}
    std::string type() const override { return "transaction"; }
    json toJson() const override {
        json arr = json::array();
        for (const auto& c : cmds_) arr.push_back(c->toJson());
        return {{"type", type()}, {"commands", arr}};
    }

    ApplyResult apply(Project& p) const override {
        std::vector<CommandPtr> inverses;
        for (std::size_t i = 0; i < cmds_.size(); ++i) {
            ApplyResult r = cmds_[i]->apply(p);
            if (!r.ok()) {
                for (auto it = inverses.rbegin(); it != inverses.rend(); ++it) (*it)->apply(p);  // inverses cannot fail
                return fail(r.error->code, "command " + std::to_string(i) + ": " + r.error->message);
            }
            inverses.push_back(std::move(r.inverse));
        }
        std::reverse(inverses.begin(), inverses.end());
        return success(makeTransaction(std::move(inverses)));
    }

private:
    std::vector<CommandPtr> cmds_;
};
```

After `makeRemoveMedia` in the same file, add the factories:

```cpp
CommandPtr makeAddRegion(Uuid trackId, Region region, int index) { return std::make_unique<AddRegionCmd>(trackId, std::move(region), index); }
CommandPtr makeRemoveRegion(Uuid regionId) { return std::make_unique<RemoveRegionCmd>(regionId); }
CommandPtr makeMoveRegion(Uuid regionId, std::int64_t newStart) { return std::make_unique<MoveRegionCmd>(regionId, newStart); }
CommandPtr makeAddSend(Uuid trackId, Send send, int index) { return std::make_unique<AddSendCmd>(trackId, send, index); }
CommandPtr makeRemoveSend(Uuid sendId) { return std::make_unique<RemoveSendCmd>(sendId); }
CommandPtr makeSetInserts(Uuid trackId, std::vector<ProcessorRef> inserts) { return std::make_unique<SetInsertsCmd>(trackId, std::move(inserts)); }
CommandPtr makeTransaction(std::vector<CommandPtr> commands) { return std::make_unique<TransactionCmd>(std::move(commands)); }
```

In `commandFromJson`, replace the line `// Task 6 adds its command types above this line.` with:

```cpp
        if (type == "add_region") return makeAddRegion(j.at("trackId").get<Uuid>(), j.at("region").get<Region>(), j.value("index", -1));
        if (type == "remove_region") return makeRemoveRegion(j.at("regionId").get<Uuid>());
        if (type == "move_region") return makeMoveRegion(j.at("regionId").get<Uuid>(), j.at("start").get<std::int64_t>());
        if (type == "add_send") return makeAddSend(j.at("trackId").get<Uuid>(), j.at("send").get<Send>(), j.value("index", -1));
        if (type == "remove_send") return makeRemoveSend(j.at("sendId").get<Uuid>());
        if (type == "set_inserts") return makeSetInserts(j.at("trackId").get<Uuid>(), j.at("inserts").get<std::vector<ProcessorRef>>());
        if (type == "transaction") {
            std::vector<CommandPtr> inner;
            for (const auto& c : j.at("commands")) inner.push_back(commandFromJson(c));
            return makeTransaction(std::move(inner));
        }
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target lpc_tests && build/tests/Debug/lpc_tests.exe "[commands],[undo]"`
Expected: `All tests passed`. If the property test reports `snapshots.size() > 30` failing, the generator is producing too many rejected commands: print `rejected` and adjust the case weights in `randomCommand`, not the assertion.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(core): region, send, insert and transaction commands with undo property tests" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

### Task 7: Lock-free queue, audio messages, real-time allocation detector

**Files:**
- Create: `core/include/lpc/audio/spsc_queue.h`, `core/include/lpc/audio/messages.h`
- Create: `tests/rt_guard.h`, `tests/rt_guard.cpp`
- Test: `tests/test_spsc_queue.cpp`, `tests/test_rt_guard.cpp`

**Interfaces:**
- Consumes: `Uuid` (Task 1).
- Produces:
  - `lpc::audio::SpscQueue<T, Capacity>` (Capacity a power of two, `T` trivially copyable and default-constructible): `bool push(const T&) noexcept` (false when full, never overwrites), `bool pop(T&) noexcept` (false when empty), `std::size_t sizeApprox() const noexcept`.
  - `lpc::audio::StripParams{float gain=1 (linear); float pan=0; bool mute=false; bool solo=false;}`
  - `lpc::audio::Owned{void* ptr; void (*deleter)(void*); void destroy();}` and `template<class T> Owned makeOwned(T*)`.
  - `enum class MsgKind : std::uint8_t { AddTrack, RemoveTrack, SetStrip, SetConfig, Reorder, Play, Stop, Locate, SetLoop }`
  - `struct AudioMsg { MsgKind kind; std::uint64_t seq; Uuid track; StripParams strip; Owned obj; std::int64_t frame; std::int64_t frame2; }` (trivially copyable; `frame` is the `Locate` target or the `SetLoop` start, `frame2` the `SetLoop` end)
  - `struct Feedback { Owned garbage; }`
  - Test helper `lpc::test::rt::{enter(), leave(), violations(), reset()}` and `lpc::test::rt::Scope`: inside a `Scope`, any `operator new` on that thread increments `violations()`.

- [ ] **Step 1: Write the failing tests**

`tests/test_spsc_queue.cpp`:

```cpp
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
```

`tests/test_rt_guard.cpp`:

```cpp
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
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build --config Debug --target lpc_tests`
Expected: FAIL to compile, `Cannot open include file: 'lpc/audio/spsc_queue.h'`.

- [ ] **Step 3: Implement**

`core/include/lpc/audio/spsc_queue.h`:

```cpp
#pragma once
#include <atomic>
#include <cstddef>
#include <type_traits>
#include <vector>

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
```

`core/include/lpc/audio/messages.h`:

```cpp
#pragma once
#include <cstdint>
#include <type_traits>

#include "lpc/uuid.h"

namespace lpc::audio {

// Strip values as the audio thread uses them. gain is linear (not dB).
struct StripParams {
    float gain = 1.0f;
    float pan = 0.0f;
    bool mute = false;
    bool solo = false;
};

// A heap object handed between threads. Whoever receives it last (always the project thread)
// calls destroy(). The audio thread never calls destroy().
struct Owned {
    void* ptr = nullptr;
    void (*deleter)(void*) = nullptr;
    void destroy() {
        if (ptr && deleter) deleter(ptr);
        ptr = nullptr;
    }
};

template <typename T>
Owned makeOwned(T* object) {
    return Owned{object, [](void* p) { delete static_cast<T*>(p); }};
}

enum class MsgKind : std::uint8_t { AddTrack, RemoveTrack, SetStrip, SetConfig, Reorder, Play, Stop, Locate, SetLoop };

// Project thread -> audio thread. obj meaning per kind:
//   AddTrack: TrackNode*, SetConfig: TrackConfig*, Reorder: std::vector<Uuid>* (processing order)
struct AudioMsg {
    MsgKind kind = MsgKind::Stop;
    std::uint64_t seq = 0;
    Uuid track;
    StripParams strip;
    Owned obj;
    std::int64_t frame = 0;   // Locate target; SetLoop start
    std::int64_t frame2 = 0;  // SetLoop end (end <= start switches the loop off)
};

// Audio thread -> project thread.
struct Feedback {
    Owned garbage;
};

static_assert(std::is_trivially_copyable_v<AudioMsg>);
static_assert(std::is_trivially_copyable_v<Feedback>);

}  // namespace lpc::audio
```

`tests/rt_guard.h`:

```cpp
#pragma once

namespace lpc::test::rt {

void enter();
void leave();
long violations();  // allocations seen on threads that were inside a Scope
void reset();

struct Scope {
    Scope() { enter(); }
    ~Scope() { leave(); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

}  // namespace lpc::test::rt
```

`tests/rt_guard.cpp`:

```cpp
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
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target lpc_tests && build/tests/Debug/lpc_tests.exe "[spsc],[rt],[messages]"`
Expected: `All tests passed`. Then run the whole suite once (`ctest --test-dir build -C Debug --output-on-failure`) to confirm the global `operator new` replacement did not break earlier tests.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat(audio): SPSC queue, audio message types, allocation detector for tests" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 8: WAV I/O, frame sources, media store

**Files:**
- Create: `core/include/lpc/wav.h`, `core/src/wav.cpp`
- Create: `core/include/lpc/audio/frame_source.h`, `core/src/audio/frame_source.cpp`
- Create: `core/include/lpc/media_store.h`, `core/src/media_store.cpp`
- Test: `tests/test_wav.cpp`, `tests/test_frame_source.cpp`, `tests/test_media_store.cpp`

**Interfaces:**
- Consumes: `MediaItem`, `Uuid`, `TempDir` helper (Task 4).
- Produces (namespace `lpc`):
  - `class WavFile { explicit WavFile(const std::filesystem::path&); int sampleRate() const; int channels() const; std::int64_t frames() const; void readFrames(std::int64_t start, std::int64_t count, float* interleaved); }` (throws `std::runtime_error` on unsupported or damaged files; reads outside the file give zeros). Supported: PCM 8/16/24/32 and float32, mono or stereo, also `WAVE_FORMAT_EXTENSIBLE`.
  - `struct WavData { int sampleRate; int channels; std::vector<float> samples; std::int64_t frames() const; }`, `WavData readWav(const path&)`.
  - `enum class WavFormat { Pcm16, Pcm24, Float32 }`, `void writeWav(const path&, int sampleRate, int channels, const std::vector<float>& interleaved, WavFormat = WavFormat::Float32)`.
  - namespace `lpc::audio`: `class IFrameSource { frames(); sampleRate(); channels(); bool read(std::int64_t frame, float* l, float* r, int n) const noexcept; }`, `class MemorySource`, `class StreamingSource` (constructor `StreamingSource(const path&, bool startReaderThread = true)`, `int takeUnderruns() const noexcept`, `void pumpOnce()`).
  - `class MediaStore { explicit MediaStore(std::filesystem::path projectDir = {}, bool streaming = true); void registerSource(const Uuid&, std::shared_ptr<audio::IFrameSource>); std::shared_ptr<audio::IFrameSource> open(const MediaItem&); int missingCount() const; std::vector<std::string> warnings() const; int takeUnderruns(); }` (thread-safe, project-thread use).

- [ ] **Step 1: Write the failing WAV tests**

`tests/test_wav.cpp`:

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <fstream>
#include "lpc/wav.h"
#include "temp_dir.h"

using namespace lpc;
namespace fs = std::filesystem;

namespace {

void put16(std::vector<unsigned char>& b, unsigned v) { b.push_back(v & 0xff); b.push_back((v >> 8) & 0xff); }
void put32(std::vector<unsigned char>& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xff); }
void putTag(std::vector<unsigned char>& b, const char* t) { b.insert(b.end(), t, t + 4); }

// Builds a RIFF/WAVE file in memory. `fmtBody` is the content of the "fmt " chunk (>= 16 bytes).
std::vector<unsigned char> riff(const std::vector<unsigned char>& fmtBody, const std::vector<unsigned char>& data,
                                std::uint32_t declaredDataSize, const std::vector<unsigned char>& extraBeforeData = {}) {
    std::vector<unsigned char> b;
    putTag(b, "RIFF");
    put32(b, 0);  // size is not used by the reader
    putTag(b, "WAVE");
    putTag(b, "fmt ");
    put32(b, static_cast<std::uint32_t>(fmtBody.size()));
    b.insert(b.end(), fmtBody.begin(), fmtBody.end());
    b.insert(b.end(), extraBeforeData.begin(), extraBeforeData.end());
    putTag(b, "data");
    put32(b, declaredDataSize);
    b.insert(b.end(), data.begin(), data.end());
    return b;
}

std::vector<unsigned char> fmt(unsigned tag, unsigned channels, unsigned rate, unsigned bits) {
    std::vector<unsigned char> f;
    put16(f, tag);
    put16(f, channels);
    put32(f, rate);
    put32(f, rate * channels * bits / 8);
    put16(f, channels * bits / 8);
    put16(f, bits);
    return f;
}

fs::path writeBytes(const fs::path& dir, const char* name, const std::vector<unsigned char>& bytes) {
    const fs::path p = dir / name;
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return p;
}

std::vector<float> ramp(int frames, int channels) {
    std::vector<float> s(static_cast<std::size_t>(frames) * channels);
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c) s[static_cast<std::size_t>(i) * channels + c] = (c == 0 ? 1.0f : -1.0f) * (float(i % 200) / 250.0f);
    return s;
}

}  // namespace

TEST_CASE("wav: write then read round trip for every output format", "[wav]") {
    test::TempDir tmp;
    const auto samples = ramp(1000, 2);
    struct Case { WavFormat f; double tol; };
    for (const Case c : {Case{WavFormat::Float32, 0.0}, Case{WavFormat::Pcm24, 5e-7}, Case{WavFormat::Pcm16, 6e-5}}) {
        const fs::path p = tmp.path / "r.wav";
        writeWav(p, 44100, 2, samples, c.f);
        const WavData d = readWav(p);
        REQUIRE(d.sampleRate == 44100);
        REQUIRE(d.channels == 2);
        REQUIRE(d.frames() == 1000);
        for (std::size_t i = 0; i < samples.size(); ++i) REQUIRE(std::abs(d.samples[i] - samples[i]) <= c.tol);
    }
}

TEST_CASE("wav: mono files and random access reads", "[wav]") {
    test::TempDir tmp;
    const auto samples = ramp(500, 1);
    writeWav(tmp.path / "m.wav", 48000, 1, samples);
    WavFile f(tmp.path / "m.wav");
    REQUIRE(f.channels() == 1);
    REQUIRE(f.frames() == 500);
    std::vector<float> out(20);
    f.readFrames(100, 20, out.data());
    for (int i = 0; i < 20; ++i) REQUIRE(out[i] == samples[100 + i]);
    // before the start and past the end: zeros, no out-of-bounds access
    std::vector<float> edge(30, 9.0f);
    f.readFrames(-10, 30, edge.data());
    for (int i = 0; i < 10; ++i) REQUIRE(edge[i] == 0.0f);
    REQUIRE(edge[10] == samples[0]);
    std::vector<float> tail(30, 9.0f);
    f.readFrames(490, 30, tail.data());
    REQUIRE(tail[9] == samples[499]);
    for (int i = 10; i < 30; ++i) REQUIRE(tail[i] == 0.0f);
    std::vector<float> far(5, 9.0f);
    f.readFrames(100000, 5, far.data());
    for (float v : far) REQUIRE(v == 0.0f);
}

TEST_CASE("wav: 8-bit PCM is unsigned and centred on 128", "[wav][formats]") {
    test::TempDir tmp;
    const auto p = writeBytes(tmp.path, "u8.wav", riff(fmt(1, 1, 8000, 8), {128, 255, 0, 192}, 4));
    const WavData d = readWav(p);
    REQUIRE(d.frames() == 4);
    REQUIRE(d.samples[0] == Catch::Approx(0.0f));
    REQUIRE(d.samples[1] == Catch::Approx(127.0f / 128.0f));
    REQUIRE(d.samples[2] == Catch::Approx(-1.0f));
    REQUIRE(d.samples[3] == Catch::Approx(0.5f));
}

TEST_CASE("wav: 32-bit integer PCM", "[wav][formats]") {
    test::TempDir tmp;
    std::vector<unsigned char> data;
    put32(data, 0x40000000u);  // +0.5
    put32(data, 0xC0000000u);  // -0.5
    const WavData d = readWav(writeBytes(tmp.path, "i32.wav", riff(fmt(1, 1, 48000, 32), data, 8)));
    REQUIRE(d.samples[0] == Catch::Approx(0.5f));
    REQUIRE(d.samples[1] == Catch::Approx(-0.5f));
}

TEST_CASE("wav: WAVE_FORMAT_EXTENSIBLE float32", "[wav][formats]") {
    test::TempDir tmp;
    std::vector<unsigned char> f = fmt(0xFFFE, 2, 48000, 32);
    put16(f, 22);  // cbSize
    put16(f, 32);  // valid bits
    put32(f, 3);   // channel mask
    put16(f, 3);   // sub-format tag: IEEE float (first two bytes of the GUID)
    f.resize(40, 0);
    std::vector<unsigned char> data;
    const float v[4] = {0.25f, -0.25f, 0.5f, -0.5f};
    for (float x : v) { std::uint32_t u; std::memcpy(&u, &x, 4); put32(data, u); }
    const WavData d = readWav(writeBytes(tmp.path, "ext.wav", riff(f, data, 16)));
    REQUIRE(d.channels == 2);
    REQUIRE(d.frames() == 2);
    REQUIRE(d.samples[2] == 0.5f);
}

TEST_CASE("wav: odd-sized chunks before the data chunk are skipped with padding", "[wav][formats]") {
    test::TempDir tmp;
    std::vector<unsigned char> list;  // "LIST" chunk of 3 bytes + 1 pad byte
    putTag(list, "LIST");
    put32(list, 3);
    list.insert(list.end(), {'a', 'b', 'c', 0});
    std::vector<unsigned char> data;
    put16(data, 16384);  // 0.5 in 16-bit
    const WavData d = readWav(writeBytes(tmp.path, "odd.wav", riff(fmt(1, 1, 8000, 16), data, 2, list)));
    REQUIRE(d.frames() == 1);
    REQUIRE(d.samples[0] == Catch::Approx(0.5f));
}

TEST_CASE("wav: truncated data chunk uses what is really in the file", "[wav][damaged]") {
    test::TempDir tmp;
    std::vector<unsigned char> data(100, 0);  // header claims 1000 bytes, only 100 are present
    const WavData d = readWav(writeBytes(tmp.path, "trunc.wav", riff(fmt(1, 1, 8000, 16), data, 1000)));
    REQUIRE(d.frames() == 50);
}

TEST_CASE("wav: streaming-style data size 0xFFFFFFFF is clamped to the file size", "[wav][damaged]") {
    test::TempDir tmp;
    std::vector<unsigned char> data(40, 0);
    const WavData d = readWav(writeBytes(tmp.path, "stream.wav", riff(fmt(1, 2, 8000, 16), data, 0xFFFFFFFFu)));
    REQUIRE(d.frames() == 10);
}

TEST_CASE("wav: zero-length audio is valid", "[wav][damaged]") {
    test::TempDir tmp;
    const WavData d = readWav(writeBytes(tmp.path, "empty.wav", riff(fmt(1, 2, 48000, 16), {}, 0)));
    REQUIRE(d.frames() == 0);
    WavFile f(tmp.path / "empty.wav");
    std::vector<float> out(8, 5.0f);
    f.readFrames(0, 4, out.data());
    for (float v : out) REQUIRE(v == 0.0f);
}

TEST_CASE("wav: unsupported and damaged files are rejected with an error", "[wav][damaged]") {
    test::TempDir tmp;
    REQUIRE_THROWS_AS(WavFile(tmp.path / "missing.wav"), std::runtime_error);
    REQUIRE_THROWS_AS(WavFile(writeBytes(tmp.path, "garbage.wav", {'n', 'o', 't', ' ', 'a', ' ', 'w', 'a', 'v'})), std::runtime_error);
    REQUIRE_THROWS_AS(WavFile(writeBytes(tmp.path, "empty.bin", {})), std::runtime_error);
    REQUIRE_THROWS_AS(WavFile(writeBytes(tmp.path, "6ch.wav", riff(fmt(1, 6, 48000, 16), std::vector<unsigned char>(24), 24))), std::runtime_error);
    REQUIRE_THROWS_AS(WavFile(writeBytes(tmp.path, "12bit.wav", riff(fmt(1, 1, 48000, 12), std::vector<unsigned char>(8), 8))), std::runtime_error);
    REQUIRE_THROWS_AS(WavFile(writeBytes(tmp.path, "adpcm.wav", riff(fmt(2, 1, 48000, 4), std::vector<unsigned char>(8), 8))), std::runtime_error);
    REQUIRE_THROWS_AS(WavFile(writeBytes(tmp.path, "zero_rate.wav", riff(fmt(1, 1, 0, 16), std::vector<unsigned char>(8), 8))), std::runtime_error);
    // header only, no data chunk at all
    std::vector<unsigned char> noData;
    putTag(noData, "RIFF"); put32(noData, 0); putTag(noData, "WAVE"); putTag(noData, "fmt "); put32(noData, 16);
    for (unsigned char c : fmt(1, 1, 48000, 16)) noData.push_back(c);
    REQUIRE_THROWS_AS(WavFile(writeBytes(tmp.path, "nodata.wav", noData)), std::runtime_error);
}
```

Add `#include <cstring>` to the test file (for `std::memcpy`).

- [ ] **Step 2: Write the failing frame-source and media-store tests**

`tests/test_frame_source.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include "lpc/audio/frame_source.h"
#include "lpc/wav.h"
#include "temp_dir.h"

using namespace lpc;
using namespace lpc::audio;

namespace {

// L = (i % 1000) / 1000, R = -L
std::vector<float> stereoRamp(int frames) {
    std::vector<float> s(static_cast<std::size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        s[static_cast<std::size_t>(i) * 2] = float(i % 1000) / 1000.0f;
        s[static_cast<std::size_t>(i) * 2 + 1] = -float(i % 1000) / 1000.0f;
    }
    return s;
}

}  // namespace

TEST_CASE("memory source: reads, zero outside the file, mono is duplicated", "[source]") {
    MemorySource stereo(48000, 2, stereoRamp(100));
    REQUIRE(stereo.frames() == 100);
    std::vector<float> l(10, 7.0f), r(10, 7.0f);
    REQUIRE(stereo.read(5, l.data(), r.data(), 10));
    REQUIRE(l[0] == 5.0f / 1000.0f);
    REQUIRE(r[0] == -5.0f / 1000.0f);

    REQUIRE(stereo.read(-3, l.data(), r.data(), 10));  // before the start
    for (int i = 0; i < 3; ++i) REQUIRE((l[i] == 0.0f && r[i] == 0.0f));
    REQUIRE(l[3] == 0.0f);
    REQUIRE(l[4] == 1.0f / 1000.0f);

    REQUIRE(stereo.read(95, l.data(), r.data(), 10));  // past the end
    REQUIRE(l[4] == 99.0f / 1000.0f);
    for (int i = 5; i < 10; ++i) REQUIRE(l[i] == 0.0f);

    MemorySource mono(48000, 1, {0.1f, 0.2f, 0.3f});
    mono.read(0, l.data(), r.data(), 3);
    REQUIRE(l[1] == 0.2f);
    REQUIRE(r[1] == 0.2f);
}

TEST_CASE("streaming source: paused reader reports an underrun until the chunk is loaded", "[source][streaming]") {
    test::TempDir tmp;
    constexpr int kFrames = 400000;  // larger than the 15-chunk read-ahead window
    const auto samples = stereoRamp(kFrames);
    writeWav(tmp.path / "long.wav", 48000, 2, samples);

    StreamingSource src(tmp.path / "long.wav", /*startReaderThread=*/false);
    REQUIRE(src.frames() == kFrames);
    std::vector<float> l(256), r(256);

    REQUIRE(src.read(1000, l.data(), r.data(), 256));  // inside the initial window: ready
    REQUIRE(l[0] == samples[2000]);
    REQUIRE(src.takeUnderruns() == 0);

    REQUIRE_FALSE(src.read(350000, l.data(), r.data(), 256));  // far away, not loaded yet
    for (float v : l) REQUIRE(v == 0.0f);                       // silence, never garbage
    REQUIRE(src.takeUnderruns() == 1);
    REQUIRE(src.takeUnderruns() == 0);  // counter resets

    src.pumpOnce();  // the reader notices the new position and loads around it
    REQUIRE(src.read(350000, l.data(), r.data(), 256));
    REQUIRE(l[10] == samples[(350000 + 10) * 2]);
    REQUIRE(r[10] == samples[(350000 + 10) * 2 + 1]);
}

TEST_CASE("streaming source: a read spanning two chunks is continuous", "[source][streaming]") {
    test::TempDir tmp;
    const auto samples = stereoRamp(100000);
    writeWav(tmp.path / "c.wav", 48000, 2, samples);
    StreamingSource src(tmp.path / "c.wav", false);
    std::vector<float> l(64), r(64);
    const int start = StreamingSource::kChunkFrames - 32;  // straddles the chunk 0/1 boundary
    REQUIRE(src.read(start, l.data(), r.data(), 64));
    for (int i = 0; i < 64; ++i) REQUIRE(l[i] == samples[static_cast<std::size_t>(start + i) * 2]);
}

TEST_CASE("streaming source: end of file is zero-filled without an underrun", "[source][streaming]") {
    test::TempDir tmp;
    writeWav(tmp.path / "s.wav", 48000, 2, stereoRamp(1000));
    StreamingSource src(tmp.path / "s.wav", false);
    std::vector<float> l(64, 3.0f), r(64, 3.0f);
    REQUIRE(src.read(980, l.data(), r.data(), 64));
    REQUIRE(l[19] == 999.0f / 1000.0f);
    for (int i = 20; i < 64; ++i) REQUIRE(l[i] == 0.0f);
    REQUIRE(src.takeUnderruns() == 0);
}

TEST_CASE("streaming source: empty file and mono file", "[source][streaming]") {
    test::TempDir tmp;
    writeWav(tmp.path / "e.wav", 48000, 2, {});
    StreamingSource empty(tmp.path / "e.wav", false);
    std::vector<float> l(16, 2.0f), r(16, 2.0f);
    REQUIRE(empty.read(0, l.data(), r.data(), 16));
    for (float v : l) REQUIRE(v == 0.0f);

    writeWav(tmp.path / "m.wav", 48000, 1, {0.5f, 0.25f});
    StreamingSource mono(tmp.path / "m.wav", false);
    REQUIRE(mono.read(0, l.data(), r.data(), 2));
    REQUIRE(r[1] == 0.25f);
}

TEST_CASE("streaming source: the background reader keeps up with a moving playhead", "[source][streaming][threads]") {
    test::TempDir tmp;
    constexpr int kFrames = 600000;
    const auto samples = stereoRamp(kFrames);
    writeWav(tmp.path / "t.wav", 48000, 2, samples);
    StreamingSource src(tmp.path / "t.wav", /*startReaderThread=*/true);
    std::vector<float> l(512), r(512);
    int mismatches = 0;
    for (int pos = 0; pos + 512 <= kFrames; pos += 512 * 40) {
        // after a jump, allow the reader up to two seconds to catch up
        bool ok = false;
        for (int attempt = 0; attempt < 400 && !ok; ++attempt) {
            ok = src.read(pos, l.data(), r.data(), 512);
            if (!ok) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(ok);
        for (int i = 0; i < 512; ++i)
            if (l[i] != samples[static_cast<std::size_t>(pos + i) * 2]) ++mismatches;
    }
    REQUIRE(mismatches == 0);
}
```

`tests/test_media_store.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "lpc/audio/frame_source.h"
#include "lpc/media_store.h"
#include "lpc/wav.h"
#include "temp_dir.h"

using namespace lpc;

namespace {
MediaItem item(const char* path, int rate = 48000, int channels = 2) {
    return MediaItem{Uuid::random(), path, "h", rate, channels, 100};
}
}  // namespace

TEST_CASE("media store: opens files from the project folder (memory and streaming)", "[media]") {
    test::TempDir tmp;
    std::filesystem::create_directories(tmp.path / "audio");
    std::vector<float> samples(200);
    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = float(i) / 400.0f;
    writeWav(tmp.path / "audio" / "a.wav", 48000, 2, samples);
    const MediaItem m = item("audio/a.wav");

    for (const bool streaming : {false, true}) {
        MediaStore store(tmp.path, streaming);
        auto src = store.open(m);
        REQUIRE(src != nullptr);
        REQUIRE(store.open(m) == src);  // cached by id
        std::vector<float> l(4), r(4);
        REQUIRE(src->read(10, l.data(), r.data(), 4));
        REQUIRE(l[0] == samples[20]);
        REQUIRE(store.missingCount() == 0);
    }
}

TEST_CASE("media store: a missing file is reported once and does not throw", "[media]") {
    test::TempDir tmp;
    MediaStore store(tmp.path, false);
    const MediaItem m = item("audio/gone.wav");
    REQUIRE(store.open(m) == nullptr);
    REQUIRE(store.open(m) == nullptr);
    REQUIRE(store.missingCount() == 1);
    REQUIRE(store.warnings().size() == 1);
    REQUIRE(store.warnings()[0].find("gone.wav") != std::string::npos);
}

TEST_CASE("media store: unreadable or mismatching files are reported, not thrown", "[media]") {
    test::TempDir tmp;
    std::filesystem::create_directories(tmp.path / "audio");
    { std::ofstream(tmp.path / "audio" / "bad.wav", std::ios::binary) << "this is not a wav"; }
    writeWav(tmp.path / "audio" / "rate.wav", 44100, 2, std::vector<float>(20, 0.1f));
    MediaStore store(tmp.path, false);
    REQUIRE(store.open(item("audio/bad.wav")) == nullptr);
    REQUIRE(store.open(item("audio/rate.wav", 48000)) == nullptr);  // item says 48 kHz, file is 44.1 kHz
    REQUIRE(store.missingCount() == 2);
}

TEST_CASE("media store: registered sources win and need no file", "[media]") {
    MediaStore store;
    const MediaItem m = item("audio/virtual.wav");
    auto mem = std::make_shared<audio::MemorySource>(48000, 2, std::vector<float>(20, 0.5f));
    store.registerSource(m.id, mem);
    REQUIRE(store.open(m) == mem);
    REQUIRE(store.missingCount() == 0);
}
```

Add `#include <fstream>` to `test_media_store.cpp`.

- [ ] **Step 3: Run them to verify they fail**

Run: `cmake --build build --config Debug --target lpc_tests`
Expected: FAIL to compile, `Cannot open include file: 'lpc/wav.h'`.

- [ ] **Step 4: Implement `wav.h` / `wav.cpp`**

`core/include/lpc/wav.h`:

```cpp
#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace lpc {

class WavFile {
public:
    // Throws std::runtime_error when the file is missing, damaged or in an unsupported format.
    explicit WavFile(const std::filesystem::path& path);

    int sampleRate() const { return sampleRate_; }
    int channels() const { return channels_; }
    std::int64_t frames() const { return frames_; }

    // Reads `count` frames starting at `start` as interleaved floats. Anything outside the file is zero.
    // Not thread-safe: one reader at a time.
    void readFrames(std::int64_t start, std::int64_t count, float* interleaved);

private:
    std::ifstream in_;
    int sampleRate_ = 0;
    int channels_ = 0;
    int bits_ = 0;
    bool isFloat_ = false;
    int bytesPerSample_ = 0;
    int blockAlign_ = 0;
    std::int64_t dataOffset_ = 0;
    std::int64_t frames_ = 0;
};

struct WavData {
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> samples;  // interleaved
    std::int64_t frames() const { return channels ? static_cast<std::int64_t>(samples.size()) / channels : 0; }
};

WavData readWav(const std::filesystem::path& path);

enum class WavFormat { Pcm16, Pcm24, Float32 };
void writeWav(const std::filesystem::path& path, int sampleRate, int channels, const std::vector<float>& interleaved,
              WavFormat format = WavFormat::Float32);

}  // namespace lpc
```

`core/src/wav.cpp`:

```cpp
#include "lpc/wav.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace lpc {

namespace {

std::string pathText(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::uint32_t le32(const unsigned char* b) { return b[0] | (b[1] << 8) | (b[2] << 16) | (std::uint32_t(b[3]) << 24); }
std::uint16_t le16(const unsigned char* b) { return static_cast<std::uint16_t>(b[0] | (b[1] << 8)); }

void put16(std::ofstream& o, unsigned v) { const unsigned char b[2] = {static_cast<unsigned char>(v & 0xff), static_cast<unsigned char>((v >> 8) & 0xff)}; o.write(reinterpret_cast<const char*>(b), 2); }
void put32(std::ofstream& o, std::uint32_t v) { const unsigned char b[4] = {static_cast<unsigned char>(v & 0xff), static_cast<unsigned char>((v >> 8) & 0xff), static_cast<unsigned char>((v >> 16) & 0xff), static_cast<unsigned char>((v >> 24) & 0xff)}; o.write(reinterpret_cast<const char*>(b), 4); }

}  // namespace

WavFile::WavFile(const std::filesystem::path& path) : in_(path, std::ios::binary) {
    const std::string name = pathText(path);
    if (!in_) throw std::runtime_error("cannot open " + name);
    in_.seekg(0, std::ios::end);
    const std::int64_t fileSize = in_.tellg();
    in_.seekg(0, std::ios::beg);

    unsigned char head[12];
    in_.read(reinterpret_cast<char*>(head), 12);
    if (in_.gcount() != 12 || std::memcmp(head, "RIFF", 4) != 0 || std::memcmp(head + 8, "WAVE", 4) != 0)
        throw std::runtime_error(name + " is not a RIFF/WAVE file");

    bool haveFmt = false;
    unsigned formatTag = 0;
    std::int64_t pos = 12;
    while (true) {
        unsigned char ch[8];
        in_.seekg(pos);
        in_.read(reinterpret_cast<char*>(ch), 8);
        if (in_.gcount() != 8) throw std::runtime_error(name + " has no data chunk");
        const std::int64_t size = le32(ch + 4);
        const std::int64_t body = pos + 8;
        if (std::memcmp(ch, "fmt ", 4) == 0) {
            if (size < 16) throw std::runtime_error(name + " has a damaged fmt chunk");
            std::vector<unsigned char> f(static_cast<std::size_t>(std::min<std::int64_t>(size, 40)));
            in_.read(reinterpret_cast<char*>(f.data()), static_cast<std::streamsize>(f.size()));
            if (static_cast<std::size_t>(in_.gcount()) != f.size()) throw std::runtime_error(name + " has a truncated fmt chunk");
            formatTag = le16(f.data());
            channels_ = le16(f.data() + 2);
            sampleRate_ = static_cast<int>(le32(f.data() + 4));
            blockAlign_ = le16(f.data() + 12);
            bits_ = le16(f.data() + 14);
            if (formatTag == 0xFFFE) {
                if (f.size() < 26) throw std::runtime_error(name + " has a damaged extensible fmt chunk");
                formatTag = le16(f.data() + 24);  // first two bytes of the sub-format GUID
            }
            haveFmt = true;
        } else if (std::memcmp(ch, "data", 4) == 0) {
            if (!haveFmt) throw std::runtime_error(name + ": data chunk before fmt chunk");
            dataOffset_ = body;
            const std::int64_t available = std::max<std::int64_t>(0, fileSize - body);
            const std::int64_t dataBytes = std::min(size, available);  // truncated files and 0xFFFFFFFF sizes
            // validate the format only now, so that a missing data chunk is reported as such
            if (channels_ < 1 || channels_ > 2) throw std::runtime_error(name + ": only mono and stereo files are supported");
            if (sampleRate_ < 1) throw std::runtime_error(name + ": invalid sample rate");
            isFloat_ = formatTag == 3;
            if (formatTag != 1 && formatTag != 3) throw std::runtime_error(name + ": unsupported encoding (only PCM and float)");
            if (isFloat_ ? bits_ != 32 : !(bits_ == 8 || bits_ == 16 || bits_ == 24 || bits_ == 32))
                throw std::runtime_error(name + ": unsupported bit depth " + std::to_string(bits_));
            bytesPerSample_ = bits_ / 8;
            blockAlign_ = bytesPerSample_ * channels_;
            frames_ = dataBytes / blockAlign_;
            return;
        }
        pos = body + size + (size & 1);  // chunks are padded to an even size
        if (pos > fileSize) throw std::runtime_error(name + " has no data chunk");
    }
}

void WavFile::readFrames(std::int64_t start, std::int64_t count, float* out) {
    const int ch = channels_;
    std::fill(out, out + count * ch, 0.0f);
    const std::int64_t from = std::max<std::int64_t>(start, 0);
    const std::int64_t to = std::min(start + count, frames_);
    if (from >= to) return;

    std::vector<unsigned char> raw(static_cast<std::size_t>((to - from) * blockAlign_));
    in_.clear();
    in_.seekg(dataOffset_ + from * blockAlign_);
    in_.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    const std::int64_t gotFrames = std::min<std::int64_t>(in_.gcount() / blockAlign_, to - from);  // short read: keep zeros

    float* dst = out + (from - start) * ch;
    const unsigned char* p = raw.data();
    for (std::int64_t i = 0; i < gotFrames * ch; ++i, p += bytesPerSample_) {
        float v;
        switch (bits_) {
            case 8: v = (static_cast<int>(p[0]) - 128) / 128.0f; break;
            case 16: v = static_cast<std::int16_t>(le16(p)) / 32768.0f; break;
            case 24: {
                std::int32_t s = p[0] | (p[1] << 8) | (p[2] << 16);
                if (s & 0x800000) s |= ~0xFFFFFF;
                v = static_cast<float>(s) / 8388608.0f;
                break;
            }
            default:
                if (isFloat_) {
                    const std::uint32_t u = le32(p);
                    std::memcpy(&v, &u, 4);
                } else {
                    v = static_cast<float>(static_cast<std::int32_t>(le32(p)) / 2147483648.0);
                }
        }
        dst[i] = v;
    }
}

WavData readWav(const std::filesystem::path& path) {
    WavFile f(path);
    WavData d;
    d.sampleRate = f.sampleRate();
    d.channels = f.channels();
    d.samples.resize(static_cast<std::size_t>(f.frames() * f.channels()));
    f.readFrames(0, f.frames(), d.samples.data());
    return d;
}

void writeWav(const std::filesystem::path& path, int sampleRate, int channels, const std::vector<float>& samples, WavFormat format) {
    if (channels < 1 || channels > 2) throw std::runtime_error("writeWav: only mono and stereo are supported");
    const int bytes = format == WavFormat::Pcm16 ? 2 : format == WavFormat::Pcm24 ? 3 : 4;
    const std::uint32_t dataSize = static_cast<std::uint32_t>(samples.size() * bytes);
    const bool isFloat = format == WavFormat::Float32;

    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o) throw std::runtime_error("cannot write " + pathText(path));
    o.write("RIFF", 4);
    put32(o, 4 + (8 + 16) + (isFloat ? 12 : 0) + (8 + dataSize));
    o.write("WAVE", 4);
    o.write("fmt ", 4);
    put32(o, 16);
    put16(o, isFloat ? 3 : 1);
    put16(o, channels);
    put32(o, sampleRate);
    put32(o, sampleRate * channels * bytes);
    put16(o, channels * bytes);
    put16(o, bytes * 8);
    if (isFloat) {
        o.write("fact", 4);
        put32(o, 4);
        put32(o, static_cast<std::uint32_t>(samples.size() / channels));
    }
    o.write("data", 4);
    put32(o, dataSize);
    for (const float s : samples) {
        const float c = std::clamp(s, -1.0f, 1.0f);
        if (format == WavFormat::Float32) {
            std::uint32_t u;
            std::memcpy(&u, &c, 4);
            put32(o, u);
        } else if (format == WavFormat::Pcm16) {
            put16(o, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(c * 32767.0f))));
        } else {
            const std::int32_t v = static_cast<std::int32_t>(std::lround(c * 8388607.0f));
            const unsigned char b[3] = {static_cast<unsigned char>(v & 0xff), static_cast<unsigned char>((v >> 8) & 0xff), static_cast<unsigned char>((v >> 16) & 0xff)};
            o.write(reinterpret_cast<const char*>(b), 3);
        }
    }
    if (!o) throw std::runtime_error("write failed for " + pathText(path));
}

}  // namespace lpc
```

- [ ] **Step 5: Implement frame sources**

`core/include/lpc/audio/frame_source.h`:

```cpp
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

#include "lpc/wav.h"

namespace lpc::audio {

// Read-only audio media as the audio thread sees it.
class IFrameSource {
public:
    virtual ~IFrameSource() = default;
    virtual std::int64_t frames() const = 0;
    virtual int sampleRate() const = 0;
    virtual int channels() const = 0;
    // Real-time safe. Writes `n` frames starting at `frame` into l and r (mono is duplicated).
    // Frames outside [0, frames()) are zero. Returns false when the data was not ready (underrun);
    // the missing part is then silence.
    virtual bool read(std::int64_t frame, float* l, float* r, int n) const noexcept = 0;
};

// Whole file in memory. Used by offline rendering and tests (always deterministic).
class MemorySource final : public IFrameSource {
public:
    MemorySource(int sampleRate, int channels, const std::vector<float>& interleaved);
    std::int64_t frames() const override { return frames_; }
    int sampleRate() const override { return sampleRate_; }
    int channels() const override { return channels_; }
    bool read(std::int64_t frame, float* l, float* r, int n) const noexcept override;

private:
    int sampleRate_, channels_;
    std::int64_t frames_;
    std::vector<float> l_, r_;
};

// Streams a WAV file from disk. A background thread keeps a window of chunks loaded ahead of the last
// position the audio thread asked for; the audio thread only copies from loaded chunks and never waits.
// Each slot has an atomic tag (the chunk number it holds, -1 while being rewritten); the reader checks the
// tag again after copying and treats a change as an underrun (seqlock pattern).
class StreamingSource final : public IFrameSource {
public:
    static constexpr int kChunkFrames = 16384;
    static constexpr int kSlots = 16;  // the reader keeps kSlots - 1 chunks ahead of the playhead

    explicit StreamingSource(const std::filesystem::path& path, bool startReaderThread = true);
    ~StreamingSource() override;
    StreamingSource(const StreamingSource&) = delete;
    StreamingSource& operator=(const StreamingSource&) = delete;

    std::int64_t frames() const override { return frames_; }
    int sampleRate() const override { return sampleRate_; }
    int channels() const override { return channels_; }
    bool read(std::int64_t frame, float* l, float* r, int n) const noexcept override;

    int takeUnderruns() const noexcept;  // returns the count since the last call and resets it
    void pumpOnce();                     // one reader pass; the reader thread calls this in a loop

private:
    struct Slot {
        std::atomic<std::int64_t> tag{-1};
        std::vector<float> data;  // kChunkFrames of L followed by kChunkFrames of R
    };

    WavFile file_;
    int sampleRate_, channels_;
    std::int64_t frames_;
    std::unique_ptr<Slot[]> slots_;
    std::vector<float> scratch_;
    mutable std::atomic<std::int64_t> lastRead_{0};
    mutable std::atomic<int> underruns_{0};
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

}  // namespace lpc::audio
```

`core/src/audio/frame_source.cpp`:

```cpp
#include "lpc/audio/frame_source.h"

#include <algorithm>
#include <chrono>

namespace lpc::audio {

// ------------------------------------------------------------------ MemorySource

MemorySource::MemorySource(int sampleRate, int channels, const std::vector<float>& interleaved)
    : sampleRate_(sampleRate), channels_(channels), frames_(channels > 0 ? static_cast<std::int64_t>(interleaved.size()) / channels : 0) {
    l_.resize(static_cast<std::size_t>(frames_));
    r_.resize(static_cast<std::size_t>(frames_));
    for (std::int64_t i = 0; i < frames_; ++i) {
        l_[static_cast<std::size_t>(i)] = interleaved[static_cast<std::size_t>(i * channels)];
        r_[static_cast<std::size_t>(i)] = channels > 1 ? interleaved[static_cast<std::size_t>(i * channels + 1)] : l_[static_cast<std::size_t>(i)];
    }
}

bool MemorySource::read(std::int64_t frame, float* l, float* r, int n) const noexcept {
    std::fill(l, l + n, 0.0f);
    std::fill(r, r + n, 0.0f);
    const std::int64_t from = std::max<std::int64_t>(frame, 0);
    const std::int64_t to = std::min<std::int64_t>(frame + n, frames_);
    if (from < to) {
        const std::int64_t count = to - from;
        std::copy_n(l_.data() + from, count, l + (from - frame));
        std::copy_n(r_.data() + from, count, r + (from - frame));
    }
    return true;
}

// ------------------------------------------------------------------ StreamingSource

StreamingSource::StreamingSource(const std::filesystem::path& path, bool startReaderThread)
    : file_(path),
      sampleRate_(file_.sampleRate()),
      channels_(file_.channels()),
      frames_(file_.frames()),
      slots_(new Slot[kSlots]),
      scratch_(static_cast<std::size_t>(kChunkFrames) * static_cast<std::size_t>(channels_)) {
    for (int i = 0; i < kSlots; ++i) slots_[i].data.assign(static_cast<std::size_t>(kChunkFrames) * 2, 0.0f);
    pumpOnce();  // the first chunks are ready before the audio thread can ask for them
    if (startReaderThread) {
        thread_ = std::thread([this] {
            while (!stop_.load(std::memory_order_acquire)) {
                pumpOnce();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
}

StreamingSource::~StreamingSource() {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
}

void StreamingSource::pumpOnce() {
    if (frames_ == 0) return;
    const std::int64_t current = lastRead_.load(std::memory_order_relaxed) / kChunkFrames;
    const std::int64_t lastChunk = (frames_ - 1) / kChunkFrames;
    for (std::int64_t c = std::max<std::int64_t>(current, 0); c <= std::min<std::int64_t>(current + kSlots - 2, lastChunk); ++c) {
        Slot& s = slots_[c % kSlots];
        if (s.tag.load(std::memory_order_acquire) == c) continue;
        s.tag.store(-1, std::memory_order_release);  // invalidate before overwriting
        file_.readFrames(c * kChunkFrames, kChunkFrames, scratch_.data());
        float* dl = s.data.data();
        float* dr = s.data.data() + kChunkFrames;
        for (int i = 0; i < kChunkFrames; ++i) {
            dl[i] = scratch_[static_cast<std::size_t>(i) * channels_];
            dr[i] = channels_ > 1 ? scratch_[static_cast<std::size_t>(i) * channels_ + 1] : dl[i];
        }
        s.tag.store(c, std::memory_order_release);
    }
}

bool StreamingSource::read(std::int64_t frame, float* l, float* r, int n) const noexcept {
    lastRead_.store(frame, std::memory_order_relaxed);
    std::fill(l, l + n, 0.0f);
    std::fill(r, r + n, 0.0f);
    bool ok = true;
    std::int64_t pos = std::max<std::int64_t>(frame, 0);
    const std::int64_t end = std::min<std::int64_t>(frame + n, frames_);
    while (pos < end) {
        const std::int64_t chunk = pos / kChunkFrames;
        const int offset = static_cast<int>(pos % kChunkFrames);
        const int len = static_cast<int>(std::min<std::int64_t>(end - pos, kChunkFrames - offset));
        const Slot& s = slots_[chunk % kSlots];
        bool good = s.tag.load(std::memory_order_acquire) == chunk;
        if (good) {
            std::copy_n(s.data.data() + offset, len, l + (pos - frame));
            std::copy_n(s.data.data() + kChunkFrames + offset, len, r + (pos - frame));
            good = s.tag.load(std::memory_order_acquire) == chunk;  // the reader did not recycle the slot meanwhile
        }
        if (!good) {
            std::fill_n(l + (pos - frame), len, 0.0f);
            std::fill_n(r + (pos - frame), len, 0.0f);
            ok = false;
        }
        pos += len;
    }
    if (!ok) underruns_.fetch_add(1, std::memory_order_relaxed);
    return ok;
}

int StreamingSource::takeUnderruns() const noexcept { return underruns_.exchange(0, std::memory_order_relaxed); }

}  // namespace lpc::audio
```

- [ ] **Step 6: Implement the media store**

`core/include/lpc/media_store.h`:

```cpp
#pragma once
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lpc/audio/frame_source.h"
#include "lpc/model.h"

namespace lpc {

// Opens and caches the media files of a project. Thread-safe. Problems (missing, unreadable or
// mismatching files) are recorded as warnings and open() returns nullptr: the project still loads and
// the affected regions are silent.
class MediaStore {
public:
    // streaming = false decodes whole files into memory (deterministic; used for offline rendering).
    explicit MediaStore(std::filesystem::path projectDir = {}, bool streaming = true);

    void registerSource(const Uuid& mediaId, std::shared_ptr<audio::IFrameSource> source);
    std::shared_ptr<audio::IFrameSource> open(const MediaItem& item);

    int missingCount() const;
    std::vector<std::string> warnings() const;
    int takeUnderruns();  // total underruns of all streaming sources since the last call

private:
    mutable std::mutex mutex_;
    std::filesystem::path dir_;
    bool streaming_;
    std::unordered_map<Uuid, std::shared_ptr<audio::IFrameSource>> sources_;
    std::unordered_map<Uuid, std::shared_ptr<audio::StreamingSource>> streams_;
    std::unordered_set<Uuid> failed_;
    std::vector<std::string> warnings_;
};

}  // namespace lpc
```

`core/src/media_store.cpp`:

```cpp
#include "lpc/media_store.h"

namespace lpc {

namespace {
std::string pathText(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
}  // namespace

MediaStore::MediaStore(std::filesystem::path projectDir, bool streaming) : dir_(std::move(projectDir)), streaming_(streaming) {}

void MediaStore::registerSource(const Uuid& mediaId, std::shared_ptr<audio::IFrameSource> source) {
    std::lock_guard lock(mutex_);
    sources_[mediaId] = std::move(source);
    failed_.erase(mediaId);
}

std::shared_ptr<audio::IFrameSource> MediaStore::open(const MediaItem& item) {
    std::lock_guard lock(mutex_);
    if (auto it = sources_.find(item.id); it != sources_.end()) return it->second;
    if (failed_.count(item.id)) return nullptr;

    auto fail = [&](const std::string& why) -> std::shared_ptr<audio::IFrameSource> {
        failed_.insert(item.id);
        warnings_.push_back(item.path + ": " + why);
        return nullptr;
    };
    if (dir_.empty()) return fail("no project folder to load media from");

    const std::filesystem::path file = dir_ / std::filesystem::path(std::u8string(item.path.begin(), item.path.end()));
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) return fail("file not found (" + pathText(file) + ")");
    try {
        std::shared_ptr<audio::IFrameSource> source;
        if (streaming_) {
            auto stream = std::make_shared<audio::StreamingSource>(file);
            streams_[item.id] = stream;
            source = stream;
        } else {
            const WavData d = readWav(file);
            source = std::make_shared<audio::MemorySource>(d.sampleRate, d.channels, d.samples);
        }
        if (source->sampleRate() != item.sampleRate) {
            streams_.erase(item.id);
            return fail("sample rate " + std::to_string(source->sampleRate()) + " does not match the project media entry (" +
                        std::to_string(item.sampleRate) + ")");
        }
        sources_[item.id] = source;
        return source;
    } catch (const std::exception& e) {
        return fail(std::string("cannot read file: ") + e.what());
    }
}

int MediaStore::missingCount() const {
    std::lock_guard lock(mutex_);
    return static_cast<int>(failed_.size());
}

std::vector<std::string> MediaStore::warnings() const {
    std::lock_guard lock(mutex_);
    return warnings_;
}

int MediaStore::takeUnderruns() {
    std::lock_guard lock(mutex_);
    int total = 0;
    for (auto& [id, s] : streams_) total += s->takeUnderruns();
    return total;
}

}  // namespace lpc
```

- [ ] **Step 7: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target lpc_tests && build/tests/Debug/lpc_tests.exe "[wav],[source],[media]"`
Expected: `All tests passed`. Note: `MediaStore` uses a mutex. That is fine: it lives on the project thread and is never called from the audio thread (the audio thread only holds raw `IFrameSource` pointers).

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "feat(core): WAV reader/writer, memory and streaming frame sources, media store" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

### Task 9: Processors and the render graph

**Files:**
- Create: `core/include/lpc/audio/processors.h`, `core/src/audio/processors.cpp`
- Create: `core/include/lpc/audio/render_graph.h`, `core/src/audio/render_graph.cpp`
- Test: `tests/test_render_graph.cpp`

**Interfaces:**
- Consumes: `IFrameSource`/`MemorySource` (Task 8), `AudioMsg`/`Owned`/`StripParams` (Task 7), `ProcessorRef`/`TrackKind` (Task 3), `rt::Scope` (Task 7).
- Produces (namespace `lpc::audio`):
  - `class IProcessor { virtual void process(float* l, float* r, int frames) noexcept = 0; virtual nlohmann::json describe() const = 0; }`
  - `class GainProcessor final : IProcessor` (`explicit GainProcessor(float gainDb)`), `class SineSynth` (`SineSynth(double sampleRate)`, `noteOn(uint8_t note, uint8_t vel)`, `noteOff(uint8_t note)`, `allNotesOff()`, `render(float* l, float* r, int n)` adds into l/r), `std::unique_ptr<IProcessor> makeEffect(const ProcessorRef&)` (nullptr when unknown; project thread only)
  - constants `kMaxTracks = 1024`, `kMaxBlock = 512`, `kMaxBlockEvents = 256`
  - `struct NoteSpan{int64 onFrame, offFrame; uint8 note, velocity;}`, `struct RegionPlayback{int64 startFrame, endFrame; const IFrameSource* source; int64 sourceOffsetFrames; float gain; std::vector<NoteSpan> notes;}`, `struct SendPlayback{Uuid target; float gain; bool preFader;}`, `struct TrackConfig{vector<RegionPlayback> regions; vector<unique_ptr<IProcessor>> inserts; vector<SendPlayback> sends; Uuid output; vector<shared_ptr<IFrameSource>> keepAlive;}`
  - `struct TrackNode { TrackNode(Uuid, TrackKind, const StripParams&, TrackConfig* (owned), double sampleRate); Uuid id; TrackKind kind; StripParams strip; TrackConfig* config; ... }`
  - `class RenderGraph { explicit RenderGraph(double sampleRate); Owned apply(const AudioMsg&) noexcept; void render(int64 blockStart, int frames, float* outL, float* outR) noexcept; void allNotesOff() noexcept; float masterPeak() const noexcept; int trackCount() const noexcept; nlohmann::json describe() const; }`
  - `apply` handles `AddTrack`, `RemoveTrack`, `SetStrip`, `SetConfig`, `Reorder` (other kinds are ignored) and returns at most one object that the caller must hand back to the project thread for destruction (an empty `Owned` when there is nothing to free).
  - Pan law: `gainL = g * (pan > 0 ? 1 - pan : 1)`, `gainR = g * (pan < 0 ? 1 + pan : 1)` (unity at centre).

- [ ] **Step 1: Write the failing tests**

`tests/test_render_graph.cpp`:

```cpp
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "lpc/audio/frame_source.h"
#include "lpc/audio/render_graph.h"
#include "rt_guard.h"

using namespace lpc;
using namespace lpc::audio;

namespace {

constexpr double kSr = 48000.0;
constexpr std::uint64_t kMaster = 100;

Uuid id(std::uint64_t n) { return Uuid{0, n}; }

struct Stereo {
    std::vector<float> l, r;
};

// Builds nodes the way the project thread would and keeps media alive.
struct Rig {
    RenderGraph g{kSr};
    std::vector<std::shared_ptr<IFrameSource>> media;

    void addNode(std::uint64_t n, TrackKind kind, TrackConfig* cfg, StripParams strip = {}) {
        AudioMsg m;
        m.kind = MsgKind::AddTrack;
        m.obj = makeOwned(new TrackNode(id(n), kind, strip, cfg, kSr));
        REQUIRE(g.apply(m).ptr == nullptr);
    }
    void addMaster() { addNode(kMaster, TrackKind::Master, new TrackConfig); }

    // A track config with one audio region playing a constant (DC) stereo signal.
    TrackConfig* dcConfig(float dc, std::int64_t start, std::int64_t end, float regionGain = 1.0f) {
        auto* cfg = new TrackConfig;
        auto src = std::make_shared<MemorySource>(48000, 2, std::vector<float>(2 * 2000, dc));
        media.push_back(src);
        RegionPlayback r;
        r.startFrame = start;
        r.endFrame = end;
        r.source = src.get();
        r.gain = regionGain;
        cfg->regions.push_back(r);
        cfg->keepAlive.push_back(src);
        return cfg;
    }

    // A track config with one MIDI region holding note spans in absolute frames.
    static TrackConfig* noteConfig(std::vector<NoteSpan> notes) {
        auto* cfg = new TrackConfig;
        RegionPlayback r;
        r.startFrame = 0;
        r.endFrame = 1'000'000;
        r.notes = std::move(notes);
        cfg->regions.push_back(std::move(r));
        return cfg;
    }

    Stereo render(std::int64_t total, int block = 256) {
        Stereo s{std::vector<float>(static_cast<std::size_t>(total)), std::vector<float>(static_cast<std::size_t>(total))};
        for (std::int64_t pos = 0; pos < total; pos += block) {
            const int n = static_cast<int>(std::min<std::int64_t>(block, total - pos));
            g.render(pos, n, &s.l[static_cast<std::size_t>(pos)], &s.r[static_cast<std::size_t>(pos)]);
        }
        return s;
    }

    void send(const AudioMsg& m) { g.apply(m).destroy(); }
    void setStrip(std::uint64_t n, StripParams p) {
        AudioMsg m;
        m.kind = MsgKind::SetStrip;
        m.track = id(n);
        m.strip = p;
        send(m);
    }
};

StripParams strip(float gain, float pan = 0.0f, bool mute = false, bool solo = false) { return StripParams{gain, pan, mute, solo}; }

void requireAll(const std::vector<float>& v, std::size_t from, std::size_t to, float expected, float tol = 1e-6f) {
    for (std::size_t i = from; i < to; ++i) REQUIRE(std::abs(v[i] - expected) <= tol);
}

}  // namespace

TEST_CASE("graph: an empty graph and a lone master render silence", "[graph]") {
    Rig empty;
    const Stereo a = empty.render(600);
    requireAll(a.l, 0, 600, 0.0f);
    Rig lone;
    lone.addMaster();
    const Stereo b = lone.render(600);
    requireAll(b.l, 0, 600, 0.0f);
    REQUIRE(lone.g.masterPeak() == 0.0f);
}

TEST_CASE("graph: an audio region plays exactly inside its frame range", "[graph]") {
    Rig rig;
    rig.addNode(1, TrackKind::Audio, rig.dcConfig(0.5f, 100, 600));
    rig.addMaster();
    const Stereo s = rig.render(1000);
    requireAll(s.l, 0, 100, 0.0f);
    requireAll(s.l, 100, 600, 0.5f);
    requireAll(s.l, 600, 1000, 0.0f);
    requireAll(s.r, 100, 600, 0.5f);
    REQUIRE(rig.g.masterPeak() == 0.0f);  // the last block (frames 768..999) is silent
}

TEST_CASE("graph: region gain, strip gain and pan", "[graph]") {
    Rig rig;
    rig.addNode(1, TrackKind::Audio, rig.dcConfig(0.5f, 0, 1000, 0.5f), strip(0.5f));
    rig.addMaster();
    requireAll(rig.render(400).l, 0, 400, 0.125f);  // 0.5 * 0.5 * 0.5

    Rig panned;
    panned.addNode(1, TrackKind::Audio, panned.dcConfig(0.5f, 0, 1000), strip(1.0f, 1.0f));
    panned.addMaster();
    const Stereo hardRight = panned.render(300);
    requireAll(hardRight.l, 0, 300, 0.0f);
    requireAll(hardRight.r, 0, 300, 0.5f);

    Rig half;
    half.addNode(1, TrackKind::Audio, half.dcConfig(0.5f, 0, 1000), strip(1.0f, -0.5f));
    half.addMaster();
    const Stereo halfLeft = half.render(300);
    requireAll(halfLeft.l, 0, 300, 0.5f);
    requireAll(halfLeft.r, 0, 300, 0.25f);
}

TEST_CASE("graph: mute and solo", "[graph]") {
    Rig rig;
    rig.addNode(1, TrackKind::Audio, rig.dcConfig(0.5f, 0, 1000));
    rig.addNode(2, TrackKind::Audio, rig.dcConfig(0.25f, 0, 1000));
    rig.addMaster();
    requireAll(rig.render(300).l, 0, 300, 0.75f);

    rig.setStrip(1, strip(1.0f, 0.0f, true));  // mute track 1 (ramps in over one block, so skip the first block)
    rig.render(256);
    requireAll(rig.render(300).l, 0, 300, 0.25f);

    rig.setStrip(1, strip(1.0f));
    rig.setStrip(2, strip(1.0f, 0.0f, false, true));  // solo track 2
    rig.render(256);
    requireAll(rig.render(300).l, 0, 300, 0.25f);

    rig.setStrip(1, strip(1.0f, 0.0f, false, true));  // both soloed
    rig.render(256);
    requireAll(rig.render(300).l, 0, 300, 0.75f);
}

TEST_CASE("graph: solo does not silence buses", "[graph]") {
    Rig rig;
    TrackConfig* a = rig.dcConfig(0.5f, 0, 1000);
    a->output = id(50);
    rig.addNode(1, TrackKind::Audio, a, strip(1.0f, 0.0f, false, true));
    rig.addNode(50, TrackKind::Bus, new TrackConfig);
    rig.addMaster();
    requireAll(rig.render(300).l, 0, 300, 0.5f);
}

TEST_CASE("graph: output routing and sends through a bus", "[graph]") {
    // track -> bus (strip gain 0.5) -> master
    Rig routed;
    TrackConfig* t = routed.dcConfig(0.5f, 0, 1000);
    t->output = id(50);
    routed.addNode(1, TrackKind::Audio, t);
    routed.addNode(50, TrackKind::Bus, new TrackConfig, strip(0.5f));
    routed.addMaster();
    requireAll(routed.render(300).l, 0, 300, 0.25f);

    // post-fader send: direct 0.5 + (0.5 * 0.5 send) = 0.75
    Rig post;
    TrackConfig* p = post.dcConfig(0.5f, 0, 1000);
    p->sends.push_back({id(50), 0.5f, false});
    post.addNode(1, TrackKind::Audio, p);
    post.addNode(50, TrackKind::Aux, new TrackConfig);
    post.addMaster();
    requireAll(post.render(300).l, 0, 300, 0.75f);

    // with strip gain 0.5: post-fader send follows the fader, pre-fader does not
    Rig postHalf;
    TrackConfig* ph = postHalf.dcConfig(0.5f, 0, 1000);
    ph->sends.push_back({id(50), 0.5f, false});
    postHalf.addNode(1, TrackKind::Audio, ph, strip(0.5f));
    postHalf.addNode(50, TrackKind::Aux, new TrackConfig);
    postHalf.addMaster();
    requireAll(postHalf.render(300).l, 0, 300, 0.375f);  // 0.25 direct + 0.125 send

    Rig pre;
    TrackConfig* pr = pre.dcConfig(0.5f, 0, 1000);
    pr->sends.push_back({id(50), 0.5f, true});
    pre.addNode(1, TrackKind::Audio, pr, strip(0.5f));
    pre.addNode(50, TrackKind::Aux, new TrackConfig);
    pre.addMaster();
    requireAll(pre.render(300).l, 0, 300, 0.5f);  // 0.25 direct + 0.25 pre-fader send

    // a muted track sends nothing, pre-fader included
    Rig muted;
    TrackConfig* mu = muted.dcConfig(0.5f, 0, 1000);
    mu->sends.push_back({id(50), 1.0f, true});
    muted.addNode(1, TrackKind::Audio, mu, strip(1.0f, 0.0f, true));
    muted.addNode(50, TrackKind::Aux, new TrackConfig);
    muted.addMaster();
    muted.render(256);
    requireAll(muted.render(300).l, 0, 300, 0.0f);
}

TEST_CASE("graph: sends to a missing track are ignored", "[graph]") {
    Rig rig;
    TrackConfig* cfg = rig.dcConfig(0.5f, 0, 1000);
    cfg->sends.push_back({id(777), 1.0f, false});
    cfg->output = id(778);  // missing output: the signal goes nowhere, nothing crashes
    rig.addNode(1, TrackKind::Audio, cfg);
    rig.addMaster();
    requireAll(rig.render(300).l, 0, 300, 0.0f);
}

TEST_CASE("graph: insert processors run before the strip", "[graph]") {
    Rig rig;
    TrackConfig* cfg = rig.dcConfig(0.5f, 0, 1000);
    cfg->inserts.push_back(std::make_unique<GainProcessor>(-6.0206f));
    rig.addNode(1, TrackKind::Audio, cfg);
    rig.addMaster();
    const Stereo s = rig.render(300);
    for (std::size_t i = 0; i < 300; ++i) REQUIRE(s.l[i] == Catch::Approx(0.25f).margin(1e-4));
}

TEST_CASE("graph: sine instrument plays notes with sample-accurate timing", "[graph][synth]") {
    Rig rig;
    rig.addNode(2, TrackKind::Instrument, Rig::noteConfig({NoteSpan{100, 4900, 69, 127}}));
    rig.addMaster();
    const Stereo s = rig.render(6000);

    requireAll(s.l, 0, 100, 0.0f, 0.0f);  // silent until the note starts
    double sum = 0.0;
    for (std::size_t i = 1000; i < 4000; ++i) sum += double(s.l[i]) * s.l[i];
    const double rms = std::sqrt(sum / 3000.0);
    REQUIRE(rms == Catch::Approx(0.2 / std::sqrt(2.0)).epsilon(0.05));  // amplitude 0.2 at velocity 127
    int crossings = 0;
    for (std::size_t i = 1001; i < 4000; ++i)
        if (s.l[i - 1] < 0.0f && s.l[i] >= 0.0f) ++crossings;
    REQUIRE(crossings >= 26);  // 440 Hz over 3000 samples at 48 kHz is 27.5 cycles
    REQUIRE(crossings <= 29);
    requireAll(s.l, 5200, 6000, 0.0f, 1e-7f);  // released well before frame 5200
    for (std::size_t i = 0; i < 6000; ++i) REQUIRE(s.l[i] == s.r[i]);
}

TEST_CASE("graph: rendering is identical for any block size", "[graph]") {
    auto build = [](Rig& rig) {
        TrackConfig* a = rig.dcConfig(0.4f, 50, 5000);
        a->sends.push_back({id(50), 0.5f, false});
        rig.addNode(1, TrackKind::Audio, a, strip(0.8f, -0.3f));
        rig.addNode(2, TrackKind::Instrument, Rig::noteConfig({NoteSpan{100, 3000, 60, 100}, NoteSpan{1500, 1500, 67, 90}, NoteSpan{2900, 5900, 72, 127}}), strip(0.9f, 0.4f));
        rig.addNode(50, TrackKind::Bus, new TrackConfig, strip(0.7f));
        rig.addMaster();
    };
    Rig reference;
    build(reference);
    const Stereo expected = reference.render(6000, 256);
    for (const int block : {1, 7, 64, 300, 512}) {
        CAPTURE(block);
        Rig rig;
        build(rig);
        const Stereo got = rig.render(6000, block);
        REQUIRE(got.l == expected.l);
        REQUIRE(got.r == expected.r);
    }
}

TEST_CASE("graph: strip changes are ramped, not stepped", "[graph]") {
    Rig rig;
    rig.addNode(1, TrackKind::Audio, rig.dcConfig(0.5f, 0, 1000));
    rig.addMaster();
    rig.render(100, 100);  // first block: gain 1
    rig.setStrip(1, strip(0.0f));
    const Stereo s = rig.render(100, 100);
    // frames 100..199 are rendered by the second call, which starts at position 0 again; only the ramp matters here
    REQUIRE(std::abs(s.l[0] - 0.5f * 0.99f) < 1e-3f);
    for (std::size_t i = 1; i < 100; ++i) REQUIRE(s.l[i] < s.l[i - 1]);
    REQUIRE(std::abs(s.l[99]) < 1e-5f);
}

TEST_CASE("graph: structural messages return what must be freed", "[graph][messages]") {
    Rig rig;
    TrackConfig* a = rig.dcConfig(0.5f, 0, 1000);
    a->output = id(50);
    rig.addNode(1, TrackKind::Audio, a);
    rig.addNode(50, TrackKind::Bus, new TrackConfig);
    rig.addMaster();
    REQUIRE(rig.g.trackCount() == 3);
    requireAll(rig.render(300).l, 0, 300, 0.5f);

    // wrong order: the bus is processed before its input, so nothing reaches the master in the same block
    auto* wrong = new std::vector<Uuid>{id(50), id(1), id(kMaster)};
    AudioMsg reorder;
    reorder.kind = MsgKind::Reorder;
    reorder.obj = makeOwned(wrong);
    Owned back = rig.g.apply(reorder);
    REQUIRE(back.ptr == wrong);  // handed back for destruction on the project thread
    back.destroy();
    requireAll(rig.render(300).l, 0, 300, 0.0f);

    // a reorder that does not list every track is ignored
    auto* partial = new std::vector<Uuid>{id(1)};
    reorder.obj = makeOwned(partial);
    rig.g.apply(reorder).destroy();
    requireAll(rig.render(300).l, 0, 300, 0.0f);  // still the wrong order from before

    auto* right = new std::vector<Uuid>{id(1), id(50), id(kMaster)};
    reorder.obj = makeOwned(right);
    rig.g.apply(reorder).destroy();
    requireAll(rig.render(300).l, 0, 300, 0.5f);

    // SetConfig swaps the config and returns the old one
    TrackConfig* fresh = rig.dcConfig(0.25f, 0, 1000);
    fresh->output = id(50);
    AudioMsg cfg;
    cfg.kind = MsgKind::SetConfig;
    cfg.track = id(1);
    cfg.obj = makeOwned(fresh);
    Owned old = rig.g.apply(cfg);
    REQUIRE(old.ptr != nullptr);
    REQUIRE(old.ptr != fresh);
    old.destroy();
    requireAll(rig.render(300).l, 0, 300, 0.25f);

    // SetConfig for an unknown track gives the new config straight back
    TrackConfig* orphan = new TrackConfig;
    cfg.track = id(999);
    cfg.obj = makeOwned(orphan);
    Owned returned = rig.g.apply(cfg);
    REQUIRE(returned.ptr == orphan);
    returned.destroy();

    // RemoveTrack returns the node
    AudioMsg rm;
    rm.kind = MsgKind::RemoveTrack;
    rm.track = id(1);
    Owned node = rig.g.apply(rm);
    REQUIRE(node.ptr != nullptr);
    node.destroy();
    REQUIRE(rig.g.trackCount() == 2);
    requireAll(rig.render(300).l, 0, 300, 0.0f);

    AudioMsg rmUnknown;
    rmUnknown.kind = MsgKind::RemoveTrack;
    rmUnknown.track = id(12345);
    REQUIRE(rig.g.apply(rmUnknown).ptr == nullptr);
}

TEST_CASE("graph: adding more than kMaxTracks hands the node back instead of overflowing", "[graph][messages]") {
    RenderGraph g(kSr);
    for (int i = 0; i < kMaxTracks; ++i) {
        AudioMsg m;
        m.kind = MsgKind::AddTrack;
        m.obj = makeOwned(new TrackNode(id(1000 + i), TrackKind::Bus, StripParams{}, new TrackConfig, kSr));
        REQUIRE(g.apply(m).ptr == nullptr);
    }
    AudioMsg extra;
    extra.kind = MsgKind::AddTrack;
    extra.obj = makeOwned(new TrackNode(id(5000), TrackKind::Bus, StripParams{}, new TrackConfig, kSr));
    Owned rejected = g.apply(extra);
    REQUIRE(rejected.ptr == extra.obj.ptr);
    rejected.destroy();
    REQUIRE(g.trackCount() == kMaxTracks);
}

TEST_CASE("graph: describe lists tracks in processing order", "[graph]") {
    Rig rig;
    rig.addNode(1, TrackKind::Audio, rig.dcConfig(0.5f, 10, 20));
    rig.addMaster();
    const nlohmann::json d = rig.g.describe();
    REQUIRE(d["tracks"].size() == 2);
    REQUIRE(d["tracks"][0]["id"] == id(1).toString());
    REQUIRE(d["tracks"][0]["regions"][0]["start"] == 10);
    REQUIRE(d["tracks"][1]["id"] == id(kMaster).toString());
}

TEST_CASE("graph: rendering and strip/config messages never allocate", "[graph][rt]") {
    Rig rig;
    TrackConfig* a = rig.dcConfig(0.4f, 0, 5000);
    a->sends.push_back({id(50), 0.5f, true});
    a->inserts.push_back(std::make_unique<GainProcessor>(-3.0f));
    rig.addNode(1, TrackKind::Audio, a, strip(0.8f, -0.3f, false, true));
    rig.addNode(2, TrackKind::Instrument, Rig::noteConfig({NoteSpan{100, 3000, 60, 100}, NoteSpan{500, 900, 64, 100}}));
    rig.addNode(50, TrackKind::Bus, new TrackConfig);
    rig.addMaster();

    TrackConfig* replacement = rig.dcConfig(0.1f, 0, 5000);  // built outside the real-time section
    AudioMsg set;
    set.kind = MsgKind::SetConfig;
    set.track = id(1);
    set.obj = makeOwned(replacement);
    AudioMsg strip2;
    strip2.kind = MsgKind::SetStrip;
    strip2.track = id(1);
    strip2.strip = strip(0.2f, 0.9f);

    std::vector<float> l(256), r(256);
    test::rt::reset();
    Owned garbage;
    {
        test::rt::Scope scope;
        for (int b = 0; b < 20; ++b) {
            if (b == 5) rig.g.apply(strip2);
            if (b == 10) garbage = rig.g.apply(set);
            rig.g.render(b * 256, 256, l.data(), r.data());
        }
        rig.g.allNotesOff();
    }
    garbage.destroy();
    REQUIRE(test::rt::violations() == 0);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build --config Debug --target lpc_tests`
Expected: FAIL to compile, `Cannot open include file: 'lpc/audio/render_graph.h'`.

- [ ] **Step 3: Implement the processors**

`core/include/lpc/audio/processors.h`:

```cpp
#pragma once
#include <array>
#include <cstdint>
#include <memory>

#include <nlohmann/json.hpp>

#include "lpc/model.h"

namespace lpc::audio {

// In-place stereo effect. process() runs on the audio thread.
class IProcessor {
public:
    virtual ~IProcessor() = default;
    virtual void process(float* l, float* r, int frames) noexcept = 0;
    virtual nlohmann::json describe() const = 0;  // not real-time; used by tests
};

class GainProcessor final : public IProcessor {
public:
    explicit GainProcessor(float gainDb);
    void process(float* l, float* r, int frames) noexcept override;
    nlohmann::json describe() const override;

private:
    float gain_;
};

// 16-voice sine synth with 2 ms attack and 5 ms release. Mono voice, written to both channels.
class SineSynth {
public:
    explicit SineSynth(double sampleRate);
    void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept;
    void noteOff(std::uint8_t note) noexcept;
    void allNotesOff() noexcept;                           // immediate silence
    void render(float* l, float* r, int frames) noexcept;  // adds into l and r

private:
    struct Voice {
        bool active = false;
        bool releasing = false;
        std::uint8_t note = 0;
        float amp = 0.0f;
        float env = 0.0f;
        double phase = 0.0;
        double inc = 0.0;
    };
    std::array<Voice, 16> voices_{};
    std::size_t stealNext_ = 0;
    double sampleRate_;
    float attackStep_;
    float releaseStep_;
};

// Creates an insert effect from a model reference; nullptr when the processor id is unknown.
// Project thread only (allocates).
std::unique_ptr<IProcessor> makeEffect(const ProcessorRef& ref);

}  // namespace lpc::audio
```

`core/src/audio/processors.cpp`:

```cpp
#include "lpc/audio/processors.h"

#include <algorithm>
#include <cmath>

#include "lpc/processor_ids.h"

namespace lpc::audio {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
}  // namespace

GainProcessor::GainProcessor(float gainDb) : gain_(std::pow(10.0f, gainDb / 20.0f)) {}

void GainProcessor::process(float* l, float* r, int frames) noexcept {
    for (int i = 0; i < frames; ++i) {
        l[i] *= gain_;
        r[i] *= gain_;
    }
}

nlohmann::json GainProcessor::describe() const { return {{"id", kProcGain}, {"gain", gain_}}; }

SineSynth::SineSynth(double sampleRate)
    : sampleRate_(sampleRate),
      attackStep_(static_cast<float>(1.0 / (0.002 * sampleRate))),
      releaseStep_(static_cast<float>(1.0 / (0.005 * sampleRate))) {}

void SineSynth::noteOn(std::uint8_t note, std::uint8_t velocity) noexcept {
    Voice* v = nullptr;
    for (Voice& candidate : voices_) {
        if (!candidate.active) {
            v = &candidate;
            break;
        }
    }
    if (!v) {  // all voices busy: steal in round-robin order
        v = &voices_[stealNext_];
        stealNext_ = (stealNext_ + 1) % voices_.size();
    }
    v->active = true;
    v->releasing = false;
    v->note = note;
    v->env = 0.0f;
    v->amp = 0.2f * static_cast<float>(velocity) / 127.0f;
    v->phase = 0.0;
    v->inc = kTwoPi * 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0) / sampleRate_;
}

void SineSynth::noteOff(std::uint8_t note) noexcept {
    for (Voice& v : voices_) {
        if (v.active && !v.releasing && v.note == note) {
            v.releasing = true;
            return;
        }
    }
}

void SineSynth::allNotesOff() noexcept {
    for (Voice& v : voices_) v = Voice{};
}

void SineSynth::render(float* l, float* r, int frames) noexcept {
    for (Voice& v : voices_) {
        if (!v.active) continue;
        for (int i = 0; i < frames; ++i) {
            if (v.releasing) {
                v.env -= releaseStep_;
                if (v.env <= 0.0f) {
                    v.active = false;
                    break;
                }
            } else if (v.env < 1.0f) {
                v.env = std::min(1.0f, v.env + attackStep_);
            }
            const float s = static_cast<float>(std::sin(v.phase)) * v.amp * v.env;
            v.phase += v.inc;
            if (v.phase >= kTwoPi) v.phase -= kTwoPi;
            l[i] += s;
            r[i] += s;
        }
    }
}

std::unique_ptr<IProcessor> makeEffect(const ProcessorRef& ref) {
    if (ref.processorId == kProcGain) {
        const auto it = ref.params.find("gainDb");
        return std::make_unique<GainProcessor>(it == ref.params.end() ? 0.0f : static_cast<float>(it->second));
    }
    return nullptr;
}

}  // namespace lpc::audio
```

- [ ] **Step 4: Implement the render graph**

`core/include/lpc/audio/render_graph.h`:

```cpp
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

#include "lpc/audio/frame_source.h"
#include "lpc/audio/messages.h"
#include "lpc/audio/processors.h"
#include "lpc/model.h"

namespace lpc::audio {

inline constexpr int kMaxTracks = 1024;
inline constexpr int kMaxBlock = 512;         // render() handles at most this many frames per call
inline constexpr int kMaxBlockEvents = 256;   // MIDI events per track per block; extra events are dropped

struct NoteSpan {
    std::int64_t onFrame = 0;
    std::int64_t offFrame = 0;
    std::uint8_t note = 60;
    std::uint8_t velocity = 100;
};

// Everything that plays on a track, in absolute frames. Built on the project thread, immutable afterwards.
struct RegionPlayback {
    std::int64_t startFrame = 0;  // [startFrame, endFrame)
    std::int64_t endFrame = 0;
    const IFrameSource* source = nullptr;  // audio regions; kept alive by TrackConfig::keepAlive
    std::int64_t sourceOffsetFrames = 0;
    float gain = 1.0f;
    std::vector<NoteSpan> notes;  // MIDI regions, sorted by onFrame
};

struct SendPlayback {
    Uuid target;
    float gain = 1.0f;
    bool preFader = false;
};

struct TrackConfig {
    std::vector<RegionPlayback> regions;
    std::vector<std::unique_ptr<IProcessor>> inserts;
    std::vector<SendPlayback> sends;
    Uuid output;  // null = master
    std::vector<std::shared_ptr<IFrameSource>> keepAlive;
};

struct TrackNode {
    TrackNode(Uuid id, TrackKind kind, const StripParams& strip, TrackConfig* config, double sampleRate);
    ~TrackNode() { delete config; }
    TrackNode(const TrackNode&) = delete;
    TrackNode& operator=(const TrackNode&) = delete;

    Uuid id;
    TrackKind kind;
    StripParams strip;
    TrackConfig* config;  // owned; replaced through SetConfig messages
    std::vector<float> l, r;
    SineSynth synth;
    float smoothL = 1.0f;
    float smoothR = 1.0f;
    bool smoothInit = false;
};

// Owned by the audio thread. apply() and render() must be called from the same thread (or from tests
// while no audio thread is running).
class RenderGraph {
public:
    explicit RenderGraph(double sampleRate);
    ~RenderGraph();
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    // Returns an object the caller must send back to the project thread for destruction (ptr is null if none).
    Owned apply(const AudioMsg& m) noexcept;
    void render(std::int64_t blockStart, int frames, float* outL, float* outR) noexcept;
    void allNotesOff() noexcept;
    float masterPeak() const noexcept { return masterPeak_; }
    int trackCount() const noexcept { return count_; }
    nlohmann::json describe() const;  // not real-time; tracks in processing order

private:
    TrackNode* find(const Uuid& id) const noexcept;
    void processNode(TrackNode& t, std::int64_t blockStart, int n, bool anySolo) noexcept;
    void renderAudio(TrackNode& t, const TrackConfig& cfg, std::int64_t blockStart, int n) noexcept;
    void renderInstrument(TrackNode& t, const TrackConfig& cfg, std::int64_t blockStart, int n) noexcept;
    static void deleteNode(void* p);
    static void deleteConfig(void* p);

    double sampleRate_;
    std::array<TrackNode*, kMaxTracks> nodes_{};
    std::array<TrackNode*, kMaxTracks> tmp_{};
    int count_ = 0;
    TrackNode* master_ = nullptr;
    std::vector<float> scratchL_, scratchR_, preL_, preR_;
    float masterPeak_ = 0.0f;
};

}  // namespace lpc::audio
```

`core/src/audio/render_graph.cpp`:

```cpp
#include "lpc/audio/render_graph.h"

#include <algorithm>
#include <cmath>

namespace lpc::audio {

namespace {
bool isSource(TrackKind k) { return k == TrackKind::Audio || k == TrackKind::Midi || k == TrackKind::Instrument; }
}  // namespace

TrackNode::TrackNode(Uuid id_, TrackKind kind_, const StripParams& strip_, TrackConfig* config_, double sampleRate)
    : id(id_), kind(kind_), strip(strip_), config(config_), l(kMaxBlock, 0.0f), r(kMaxBlock, 0.0f), synth(sampleRate) {}

RenderGraph::RenderGraph(double sampleRate)
    : sampleRate_(sampleRate), scratchL_(kMaxBlock), scratchR_(kMaxBlock), preL_(kMaxBlock), preR_(kMaxBlock) {}

RenderGraph::~RenderGraph() {
    for (int i = 0; i < count_; ++i) delete nodes_[static_cast<std::size_t>(i)];
}

void RenderGraph::deleteNode(void* p) { delete static_cast<TrackNode*>(p); }
void RenderGraph::deleteConfig(void* p) { delete static_cast<TrackConfig*>(p); }

TrackNode* RenderGraph::find(const Uuid& id) const noexcept {
    for (int i = 0; i < count_; ++i)
        if (nodes_[static_cast<std::size_t>(i)]->id == id) return nodes_[static_cast<std::size_t>(i)];
    return nullptr;
}

Owned RenderGraph::apply(const AudioMsg& m) noexcept {
    switch (m.kind) {
        case MsgKind::AddTrack: {
            auto* node = static_cast<TrackNode*>(m.obj.ptr);
            if (!node) return {};
            if (count_ >= kMaxTracks) return Owned{node, &RenderGraph::deleteNode};  // full: give it back
            nodes_[static_cast<std::size_t>(count_++)] = node;
            return {};
        }
        case MsgKind::RemoveTrack: {
            for (int i = 0; i < count_; ++i) {
                if (nodes_[static_cast<std::size_t>(i)]->id != m.track) continue;
                TrackNode* node = nodes_[static_cast<std::size_t>(i)];
                for (int j = i; j < count_ - 1; ++j) nodes_[static_cast<std::size_t>(j)] = nodes_[static_cast<std::size_t>(j + 1)];
                nodes_[static_cast<std::size_t>(--count_)] = nullptr;
                return Owned{node, &RenderGraph::deleteNode};
            }
            return {};
        }
        case MsgKind::SetStrip: {
            if (TrackNode* n = find(m.track)) n->strip = m.strip;
            return {};
        }
        case MsgKind::SetConfig: {
            auto* cfg = static_cast<TrackConfig*>(m.obj.ptr);
            TrackNode* n = find(m.track);
            if (!n) return Owned{cfg, &RenderGraph::deleteConfig};
            TrackConfig* old = n->config;
            n->config = cfg;
            return Owned{old, &RenderGraph::deleteConfig};
        }
        case MsgKind::Reorder: {
            const auto* order = static_cast<const std::vector<Uuid>*>(m.obj.ptr);
            if (order && static_cast<int>(order->size()) == count_) {
                bool complete = true;
                for (int k = 0; k < count_ && complete; ++k) {
                    TrackNode* n = find((*order)[static_cast<std::size_t>(k)]);
                    if (!n) complete = false;
                    else tmp_[static_cast<std::size_t>(k)] = n;
                }
                if (complete) {
                    // every listed id exists and the sizes match; reject lists with duplicates
                    for (int a = 0; a < count_ && complete; ++a)
                        for (int b = a + 1; b < count_; ++b)
                            if (tmp_[static_cast<std::size_t>(a)] == tmp_[static_cast<std::size_t>(b)]) {
                                complete = false;
                                break;
                            }
                }
                if (complete)
                    for (int k = 0; k < count_; ++k) nodes_[static_cast<std::size_t>(k)] = tmp_[static_cast<std::size_t>(k)];
            }
            return m.obj;  // the order list is always handed back
        }
        default:
            return {};
    }
}

void RenderGraph::allNotesOff() noexcept {
    for (int i = 0; i < count_; ++i) nodes_[static_cast<std::size_t>(i)]->synth.allNotesOff();
}

void RenderGraph::renderAudio(TrackNode& t, const TrackConfig& cfg, std::int64_t blockStart, int n) noexcept {
    const std::int64_t blockEnd = blockStart + n;
    for (const RegionPlayback& reg : cfg.regions) {
        if (!reg.source) continue;
        const std::int64_t lo = std::max(blockStart, reg.startFrame);
        const std::int64_t hi = std::min(blockEnd, reg.endFrame);
        if (lo >= hi) continue;
        const int count = static_cast<int>(hi - lo);
        const int offset = static_cast<int>(lo - blockStart);
        reg.source->read(reg.sourceOffsetFrames + (lo - reg.startFrame), scratchL_.data(), scratchR_.data(), count);
        for (int i = 0; i < count; ++i) {
            t.l[static_cast<std::size_t>(offset + i)] += scratchL_[static_cast<std::size_t>(i)] * reg.gain;
            t.r[static_cast<std::size_t>(offset + i)] += scratchR_[static_cast<std::size_t>(i)] * reg.gain;
        }
    }
}

void RenderGraph::renderInstrument(TrackNode& t, const TrackConfig& cfg, std::int64_t blockStart, int n) noexcept {
    struct Event {
        std::int64_t frame;
        bool on;
        std::uint8_t note;
        std::uint8_t velocity;
    };
    Event events[kMaxBlockEvents];
    int count = 0;
    const std::int64_t blockEnd = blockStart + n;
    for (const RegionPlayback& reg : cfg.regions) {
        for (const NoteSpan& s : reg.notes) {
            if (s.onFrame >= blockStart && s.onFrame < blockEnd && count < kMaxBlockEvents) events[count++] = {s.onFrame, true, s.note, s.velocity};
            if (s.offFrame >= blockStart && s.offFrame < blockEnd && count < kMaxBlockEvents) events[count++] = {s.offFrame, false, s.note, 0};
        }
    }
    for (int i = 1; i < count; ++i) {  // stable insertion sort by frame (events are few)
        const Event key = events[i];
        int j = i - 1;
        while (j >= 0 && events[j].frame > key.frame) {
            events[j + 1] = events[j];
            --j;
        }
        events[j + 1] = key;
    }
    int pos = 0;
    float* l = t.l.data();
    float* r = t.r.data();
    for (int i = 0; i < count; ++i) {
        const int at = static_cast<int>(events[i].frame - blockStart);
        if (at > pos) {
            t.synth.render(l + pos, r + pos, at - pos);
            pos = at;
        }
        if (events[i].on) t.synth.noteOn(events[i].note, events[i].velocity);
        else t.synth.noteOff(events[i].note);
    }
    if (pos < n) t.synth.render(l + pos, r + pos, n - pos);
}

void RenderGraph::processNode(TrackNode& t, std::int64_t blockStart, int n, bool anySolo) noexcept {
    float* l = t.l.data();
    float* r = t.r.data();
    const TrackConfig* cfg = t.config;
    if (cfg) {
        if (t.kind == TrackKind::Audio) renderAudio(t, *cfg, blockStart, n);
        else if (t.kind == TrackKind::Instrument) renderInstrument(t, *cfg, blockStart, n);
        for (const auto& insert : cfg->inserts) insert->process(l, r, n);
    }

    std::copy_n(l, n, preL_.data());  // pre-fader tap for sends
    std::copy_n(r, n, preR_.data());

    const bool muted = t.strip.mute || (anySolo && !t.strip.solo && isSource(t.kind));
    const float g = muted ? 0.0f : t.strip.gain;
    const float targetL = g * (t.strip.pan > 0.0f ? 1.0f - t.strip.pan : 1.0f);
    const float targetR = g * (t.strip.pan < 0.0f ? 1.0f + t.strip.pan : 1.0f);
    if (!t.smoothInit) {
        t.smoothL = targetL;
        t.smoothR = targetR;
        t.smoothInit = true;
    }
    const float stepL = (targetL - t.smoothL) / static_cast<float>(n);
    const float stepR = (targetR - t.smoothR) / static_cast<float>(n);
    float gl = t.smoothL, gr = t.smoothR;
    for (int i = 0; i < n; ++i) {
        gl += stepL;
        gr += stepR;
        l[i] *= gl;
        r[i] *= gr;
    }
    t.smoothL = targetL;
    t.smoothR = targetR;

    if (t.kind == TrackKind::Master || !cfg) return;
    if (!muted) {
        for (const SendPlayback& s : cfg->sends) {
            TrackNode* dst = find(s.target);
            if (!dst || dst == &t) continue;
            const float* srcL = s.preFader ? preL_.data() : l;
            const float* srcR = s.preFader ? preR_.data() : r;
            for (int i = 0; i < n; ++i) {
                dst->l[static_cast<std::size_t>(i)] += srcL[i] * s.gain;
                dst->r[static_cast<std::size_t>(i)] += srcR[i] * s.gain;
            }
        }
    }
    TrackNode* out = cfg->output.isNull() ? master_ : find(cfg->output);
    if (out && out != &t) {
        for (int i = 0; i < n; ++i) {
            out->l[static_cast<std::size_t>(i)] += l[i];
            out->r[static_cast<std::size_t>(i)] += r[i];
        }
    }
}

void RenderGraph::render(std::int64_t blockStart, int frames, float* outL, float* outR) noexcept {
    const int n = std::min(frames, kMaxBlock);
    std::fill_n(outL, frames, 0.0f);
    std::fill_n(outR, frames, 0.0f);
    bool anySolo = false;
    master_ = nullptr;
    for (int i = 0; i < count_; ++i) {
        TrackNode& t = *nodes_[static_cast<std::size_t>(i)];
        std::fill_n(t.l.data(), n, 0.0f);
        std::fill_n(t.r.data(), n, 0.0f);
        if (t.strip.solo && isSource(t.kind)) anySolo = true;
        if (t.kind == TrackKind::Master) master_ = &t;
    }
    for (int i = 0; i < count_; ++i) processNode(*nodes_[static_cast<std::size_t>(i)], blockStart, n, anySolo);

    float peak = 0.0f;
    if (master_) {
        for (int i = 0; i < n; ++i) {
            outL[i] = master_->l[static_cast<std::size_t>(i)];
            outR[i] = master_->r[static_cast<std::size_t>(i)];
            peak = std::max({peak, std::abs(outL[i]), std::abs(outR[i])});
        }
    }
    masterPeak_ = peak;
}

nlohmann::json RenderGraph::describe() const {
    nlohmann::json tracks = nlohmann::json::array();
    for (int i = 0; i < count_; ++i) {
        const TrackNode& t = *nodes_[static_cast<std::size_t>(i)];
        nlohmann::json j = {{"id", t.id.toString()}, {"kind", static_cast<int>(t.kind)}, {"gain", t.strip.gain},
                            {"pan", t.strip.pan},    {"mute", t.strip.mute},              {"solo", t.strip.solo}};
        nlohmann::json regions = nlohmann::json::array(), inserts = nlohmann::json::array(), sends = nlohmann::json::array();
        if (t.config) {
            for (const RegionPlayback& r : t.config->regions) {
                nlohmann::json notes = nlohmann::json::array();
                for (const NoteSpan& s : r.notes) notes.push_back({s.onFrame, s.offFrame, s.note, s.velocity});
                regions.push_back({{"start", r.startFrame}, {"end", r.endFrame}, {"offset", r.sourceOffsetFrames}, {"gain", r.gain},
                                   {"src", reinterpret_cast<std::uintptr_t>(r.source)}, {"notes", notes}});
            }
            for (const auto& p : t.config->inserts) inserts.push_back(p->describe());
            for (const SendPlayback& s : t.config->sends) sends.push_back({{"target", s.target.toString()}, {"gain", s.gain}, {"pre", s.preFader}});
            j["output"] = t.config->output.toString();
        }
        j["regions"] = regions;
        j["inserts"] = inserts;
        j["sends"] = sends;
        tracks.push_back(j);
    }
    return {{"tracks", tracks}};
}

}  // namespace lpc::audio
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target lpc_tests && build/tests/Debug/lpc_tests.exe "[graph]"`
Expected: `All tests passed`.

Two tests deserve a note if they fail:
- `graph: strip changes are ramped`: the test calls `render(100, 100)` twice, so both calls start at frame 0 of the region; this is intentional (only the gain ramp is under test).
- `graph: rendering is identical for any block size`: any difference means a per-block (not per-sample) state leak in `processNode` or the synth; fix the implementation, not the test.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(audio): processors, sine synth and the real-time render graph" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Model-to-graph translator and the equivalence test

**Files:**
- Create: `core/include/lpc/graph_builder.h`, `core/src/graph_builder.cpp`
- Test: `tests/test_graph_builder.cpp`

**Interfaces:**
- Consumes: `Project` model (Task 3), `MediaStore` (Task 8), `TrackNode`/`TrackConfig`/`RegionPlayback`/`AudioMsg` (Tasks 7, 9), `makeEffect` (Task 9), `randomCommand` and `UndoStack` (Tasks 5, 6).
- Produces (namespace `lpc`):
  - `std::vector<Uuid> processingOrder(const Project&)`: sources first, buses after everything feeding them, master last; ties keep the order of `project.tracks`.
  - `audio::StripParams stripParamsOf(const Strip&)` (dB converted to linear gain).
  - `std::unique_ptr<audio::TrackConfig> buildConfig(const Project&, const Track&, MediaStore&)` (regions in absolute frames; regions whose media cannot be opened are skipped).
  - `std::unique_ptr<audio::TrackNode> buildNode(const Project&, const Track&, MediaStore&)`.
  - `std::vector<audio::AudioMsg> initialMessages(const Project&, MediaStore&)`: one `AddTrack` per track, then a `Reorder`. `seq` is not set.
  - `std::vector<audio::AudioMsg> diffToMessages(const Project& before, const Project& after, MediaStore&)`: the single model-to-message translator. Message order: `AddTrack`s, then `SetStrip`/`SetConfig`s, then `RemoveTrack`s, then a `Reorder` when the track set or the processing order changed. A change of tempo map or sample rate yields a `SetConfig` for every existing track. `seq` is not set.

- [ ] **Step 1: Write the failing tests**

`tests/test_graph_builder.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <unordered_set>
#include "lpc/audio/render_graph.h"
#include "lpc/commands.h"
#include "lpc/graph_builder.h"
#include "lpc/undo_stack.h"
#include "random_commands.h"

using namespace lpc;
using namespace lpc::audio;

namespace {

std::mt19937_64 gRng(2024);

Track track(TrackKind kind, const char* name) {
    Track t;
    t.id = Uuid::random(gRng);
    t.kind = kind;
    t.name = name;
    if (kind == TrackKind::Instrument) t.instrument = ProcessorRef{"builtin.sine", {}, ""};
    return t;
}

void add(Project& p, const Track& t) { REQUIRE(makeAddTrack(t)->apply(p).ok()); }

std::shared_ptr<MemorySource> silence(int frames = 4800) {
    return std::make_shared<MemorySource>(48000, 2, std::vector<float>(static_cast<std::size_t>(frames) * 2, 0.1f));
}

void applyAll(RenderGraph& g, std::vector<AudioMsg> msgs) {
    for (AudioMsg& m : msgs) g.apply(m).destroy();
}

// Builds a graph by applying the initial messages; every media item gets a registered source.
struct Scene {
    Project p;
    MediaStore media;
    std::unordered_set<Uuid> registered;

    explicit Scene(Project project) : p(std::move(project)) { registerMedia(); }
    void registerMedia() {
        for (const MediaItem& m : p.mediaPool)
            if (registered.insert(m.id).second) media.registerSource(m.id, silence());
    }
};

const AudioMsg* firstOfKind(const std::vector<AudioMsg>& v, MsgKind k) {
    for (const AudioMsg& m : v)
        if (m.kind == k) return &m;
    return nullptr;
}

int countOfKind(const std::vector<AudioMsg>& v, MsgKind k) {
    int n = 0;
    for (const AudioMsg& m : v) n += m.kind == k;
    return n;
}

void destroyAll(std::vector<AudioMsg>& msgs) {
    // messages that were never applied still own their objects
    for (AudioMsg& m : msgs) m.obj.destroy();
}

}  // namespace

TEST_CASE("builder: processing order puts sources before buses and the master last", "[builder]") {
    Project p(Uuid::random(gRng));
    Track bus2 = track(TrackKind::Bus, "Bus2");
    Track bus1 = track(TrackKind::Bus, "Bus1");
    bus1.strip.output = bus2.id;  // bus1 -> bus2
    Track src = track(TrackKind::Audio, "Src");
    src.strip.output = bus1.id;   // src -> bus1
    add(p, bus2);                 // declared in the "wrong" order on purpose
    add(p, bus1);
    add(p, src);
    const auto order = processingOrder(p);
    REQUIRE(order.size() == 4);
    REQUIRE(order[0] == src.id);
    REQUIRE(order[1] == bus1.id);
    REQUIRE(order[2] == bus2.id);
    REQUIRE(order[3] == p.master()->id);
}

TEST_CASE("builder: sends count as routing edges and independent tracks keep their order", "[builder]") {
    Project p(Uuid::random(gRng));
    const Track bus = track(TrackKind::Aux, "Aux");
    const Track a = track(TrackKind::Audio, "A");
    const Track b = track(TrackKind::Instrument, "B");
    add(p, bus);
    add(p, a);
    add(p, b);
    REQUIRE(makeAddSend(a.id, Send{Uuid::random(gRng), bus.id, 0.0f, false})->apply(p).ok());
    const auto order = processingOrder(p);
    REQUIRE(order[0] == a.id);
    REQUIRE(order[1] == b.id);
    REQUIRE(order[2] == bus.id);
    REQUIRE(order[3] == p.master()->id);
}

TEST_CASE("builder: regions become absolute frames through the tempo map", "[builder]") {
    Scene s(Project(Uuid::random(gRng)));
    const MediaItem media{Uuid::random(gRng), "audio/a.wav", "h", 48000, 2, 4800};
    REQUIRE(makeAddMedia(media)->apply(s.p).ok());
    s.registerMedia();

    Track audio = track(TrackKind::Audio, "A");
    Region musical;
    musical.id = Uuid::random(gRng);
    musical.start = kPPQ;
    musical.length = 2 * kPPQ;
    musical.mediaId = media.id;
    musical.sourceOffsetFrames = 77;
    musical.gainDb = -6.0206f;
    Region absolute;
    absolute.id = Uuid::random(gRng);
    absolute.timeBase = TimeBase::Absolute;
    absolute.start = 500000;   // 0.5 s
    absolute.length = 1000000;  // 1 s
    absolute.mediaId = media.id;
    audio.regions = {musical, absolute};
    add(s.p, audio);

    auto cfg = buildConfig(s.p, *s.p.findTrack(audio.id), s.media);
    REQUIRE(cfg->regions.size() == 2);
    REQUIRE(cfg->regions[0].startFrame == 24000);  // beat 1 at 120 bpm
    REQUIRE(cfg->regions[0].endFrame == 72000);    // beat 3
    REQUIRE(cfg->regions[0].sourceOffsetFrames == 77);
    REQUIRE(cfg->regions[0].gain == Catch::Approx(0.5f).margin(1e-4));
    REQUIRE(cfg->regions[1].startFrame == 24000);
    REQUIRE(cfg->regions[1].endFrame == 72000);
    REQUIRE(cfg->keepAlive.size() == 2);

    // a tempo change moves the musical region but not the absolute one
    REQUIRE(makeSetTempo(kPPQ, 60.0)->apply(s.p).ok());
    cfg = buildConfig(s.p, *s.p.findTrack(audio.id), s.media);
    REQUIRE(cfg->regions[0].startFrame == 24000);
    REQUIRE(cfg->regions[0].endFrame == 24000 + 2 * 48000);  // two beats at 60 bpm
    REQUIRE(cfg->regions[1].startFrame == 24000);
}

TEST_CASE("builder: MIDI notes become sorted frame spans", "[builder]") {
    Scene s(Project(Uuid::random(gRng)));
    Track inst = track(TrackKind::Instrument, "Keys");
    Region r;
    r.id = Uuid::random(gRng);
    r.start = kPPQ;  // beat 1 = frame 24000
    r.length = 4 * kPPQ;
    r.notes.push_back({kPPQ, kPPQ, 64, 90});  // second note listed first on purpose
    r.notes.push_back({0, kPPQ, 60, 100});
    inst.regions.push_back(r);
    add(s.p, inst);

    const auto cfg = buildConfig(s.p, *s.p.findTrack(inst.id), s.media);
    REQUIRE(cfg->regions[0].notes.size() == 2);
    REQUIRE(cfg->regions[0].notes[0].onFrame == 24000);
    REQUIRE(cfg->regions[0].notes[0].offFrame == 48000);
    REQUIRE(cfg->regions[0].notes[0].note == 60);
    REQUIRE(cfg->regions[0].notes[1].onFrame == 48000);
    REQUIRE(cfg->regions[0].notes[1].note == 64);
}

TEST_CASE("builder: a region whose media is missing is skipped and reported", "[builder]") {
    Project p(Uuid::random(gRng));
    const MediaItem media{Uuid::random(gRng), "audio/gone.wav", "h", 48000, 2, 100};
    REQUIRE(makeAddMedia(media)->apply(p).ok());
    Track audio = track(TrackKind::Audio, "A");
    Region r;
    r.id = Uuid::random(gRng);
    r.length = kPPQ;
    r.mediaId = media.id;
    audio.regions.push_back(r);
    add(p, audio);

    MediaStore media_store;  // knows nothing about the file
    const auto cfg = buildConfig(p, *p.findTrack(audio.id), media_store);
    REQUIRE(cfg->regions.empty());
    REQUIRE(media_store.missingCount() == 1);
}

TEST_CASE("builder: diff produces the smallest sensible message set", "[builder][diff]") {
    Scene s(Project(Uuid::random(gRng)));
    const Track a = track(TrackKind::Audio, "A");
    add(s.p, a);

    // nothing changed
    REQUIRE(diffToMessages(s.p, s.p, s.media).empty());

    // strip-only change: exactly one SetStrip with the linear gain
    Project after = s.p;
    StripPatch patch;
    patch.gainDb = -6.0206f;
    patch.mute = true;
    REQUIRE(makeSetStrip(a.id, patch)->apply(after).ok());
    auto msgs = diffToMessages(s.p, after, s.media);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].kind == MsgKind::SetStrip);
    REQUIRE(msgs[0].track == a.id);
    REQUIRE(msgs[0].strip.gain == Catch::Approx(0.5f).margin(1e-4));
    REQUIRE(msgs[0].strip.mute);

    // new track: AddTrack then Reorder
    after = s.p;
    const Track b = track(TrackKind::Instrument, "B");
    add(after, b);
    msgs = diffToMessages(s.p, after, s.media);
    REQUIRE(msgs.size() == 2);
    REQUIRE(msgs[0].kind == MsgKind::AddTrack);
    REQUIRE(msgs[1].kind == MsgKind::Reorder);
    destroyAll(msgs);

    // removed track: RemoveTrack then Reorder
    msgs = diffToMessages(after, s.p, s.media);
    REQUIRE(msgs.size() == 2);
    REQUIRE(msgs[0].kind == MsgKind::RemoveTrack);
    REQUIRE(msgs[0].track == b.id);
    REQUIRE(msgs[1].kind == MsgKind::Reorder);
    destroyAll(msgs);

    // tempo change: SetConfig for every track that exists on both sides (master + A)
    after = s.p;
    REQUIRE(makeSetTempo(kPPQ, 90.0)->apply(after).ok());
    msgs = diffToMessages(s.p, after, s.media);
    REQUIRE(countOfKind(msgs, MsgKind::SetConfig) == 2);
    REQUIRE(firstOfKind(msgs, MsgKind::Reorder) == nullptr);
    destroyAll(msgs);

    // inserts change: a single SetConfig
    after = s.p;
    REQUIRE(makeSetInserts(a.id, {{"builtin.gain", {{"gainDb", -3.0}}, ""}})->apply(after).ok());
    msgs = diffToMessages(s.p, after, s.media);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].kind == MsgKind::SetConfig);
    destroyAll(msgs);
}

TEST_CASE("builder: initial messages build a graph in processing order", "[builder]") {
    Scene s(Project(Uuid::random(gRng)));
    Track bus = track(TrackKind::Bus, "Bus");
    Track src = track(TrackKind::Audio, "Src");
    src.strip.output = bus.id;
    add(s.p, bus);
    add(s.p, src);
    RenderGraph g(48000.0);
    applyAll(g, initialMessages(s.p, s.media));
    const auto d = g.describe();
    REQUIRE(d["tracks"].size() == 3);
    REQUIRE(d["tracks"][0]["id"] == src.id.toString());
    REQUIRE(d["tracks"][1]["id"] == bus.id.toString());
    REQUIRE(d["tracks"][2]["id"] == s.p.master()->id.toString());
}

TEST_CASE("builder: messages keep the live graph identical to a graph built from scratch", "[builder][property]") {
    for (std::uint64_t seed = 1; seed <= 15; ++seed) {
        CAPTURE(seed);
        std::mt19937_64 rng(seed);
        Scene s(Project(Uuid::random(rng)));
        UndoStack stack;
        RenderGraph live(48000.0);
        applyAll(live, initialMessages(s.p, s.media));

        int steps = 0;
        for (int i = 0; i < 150; ++i) {
            const Project before = s.p;
            if (rng() % 100 < 25 && stack.canUndo()) {
                REQUIRE_FALSE(stack.undo(s.p).has_value());
            } else {
                CommandPtr c = test::randomCommand(s.p, rng);
                if (!c || stack.execute(s.p, std::move(c)).has_value()) continue;
            }
            s.registerMedia();
            applyAll(live, diffToMessages(before, s.p, s.media));

            RenderGraph fresh(48000.0);
            applyAll(fresh, initialMessages(s.p, s.media));
            REQUIRE(live.describe() == fresh.describe());
            ++steps;
        }
        REQUIRE(steps > 30);
    }
}
```

Add `#include <catch2/catch_approx.hpp>` to the test file.

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build --config Debug --target lpc_tests`
Expected: FAIL to compile, `Cannot open include file: 'lpc/graph_builder.h'`.

- [ ] **Step 3: Implement**

`core/include/lpc/graph_builder.h`:

```cpp
#pragma once
#include <memory>
#include <vector>

#include "lpc/audio/messages.h"
#include "lpc/audio/render_graph.h"
#include "lpc/media_store.h"
#include "lpc/model.h"

namespace lpc {

std::vector<Uuid> processingOrder(const Project& project);
audio::StripParams stripParamsOf(const Strip& strip);

std::unique_ptr<audio::TrackConfig> buildConfig(const Project& project, const Track& track, MediaStore& media);
std::unique_ptr<audio::TrackNode> buildNode(const Project& project, const Track& track, MediaStore& media);

std::vector<audio::AudioMsg> initialMessages(const Project& project, MediaStore& media);
std::vector<audio::AudioMsg> diffToMessages(const Project& before, const Project& after, MediaStore& media);

}  // namespace lpc
```

`core/src/graph_builder.cpp`:

```cpp
#include "lpc/graph_builder.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "lpc/audio/processors.h"

namespace lpc {

using namespace audio;

namespace {

float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }
std::int64_t toFrames(double v) { return static_cast<std::int64_t>(std::llround(v)); }

// `value` is a start or end position in the region's own unit (ticks or microseconds).
std::int64_t regionFrame(const Project& p, const Region& r, std::int64_t value) {
    if (r.timeBase == TimeBase::Musical) return toFrames(p.tempoMap.ticksToSamples(value, p.sampleRate));
    return toFrames(static_cast<double>(value) * p.sampleRate / 1e6);
}

AudioMsg addMsg(std::unique_ptr<TrackNode> node) {
    AudioMsg m;
    m.kind = MsgKind::AddTrack;
    m.track = node->id;
    m.obj = makeOwned(node.release());
    return m;
}

AudioMsg removeMsg(const Uuid& id) {
    AudioMsg m;
    m.kind = MsgKind::RemoveTrack;
    m.track = id;
    return m;
}

AudioMsg stripMsg(const Track& t) {
    AudioMsg m;
    m.kind = MsgKind::SetStrip;
    m.track = t.id;
    m.strip = stripParamsOf(t.strip);
    return m;
}

AudioMsg configMsg(const Project& p, const Track& t, MediaStore& media) {
    AudioMsg m;
    m.kind = MsgKind::SetConfig;
    m.track = t.id;
    m.obj = makeOwned(buildConfig(p, t, media).release());
    return m;
}

AudioMsg reorderMsg(const Project& p) {
    AudioMsg m;
    m.kind = MsgKind::Reorder;
    m.obj = makeOwned(new std::vector<Uuid>(processingOrder(p)));
    return m;
}

bool stripChanged(const Track& a, const Track& b) {
    return a.strip.gainDb != b.strip.gainDb || a.strip.pan != b.strip.pan || a.strip.mute != b.strip.mute || a.strip.solo != b.strip.solo;
}

bool configChanged(const Track& a, const Track& b) {
    return a.kind != b.kind || a.regions != b.regions || a.instrument != b.instrument || a.strip.inserts != b.strip.inserts ||
           a.strip.sends != b.strip.sends || a.strip.output != b.strip.output;
}

}  // namespace

StripParams stripParamsOf(const Strip& s) { return StripParams{dbToLinear(s.gainDb), s.pan, s.mute, s.solo}; }

std::vector<Uuid> processingOrder(const Project& p) {
    std::vector<const Track*> pending;
    const Track* master = nullptr;
    for (const Track& t : p.tracks) {
        if (t.kind == TrackKind::Master) master = &t;
        else pending.push_back(&t);
    }
    std::unordered_map<Uuid, int> indegree;
    for (const Track* t : pending) indegree[t->id] = 0;
    auto forEachTarget = [](const Track& t, auto&& fn) {
        if (!t.strip.output.isNull()) fn(t.strip.output);
        for (const Send& s : t.strip.sends) fn(s.targetTrackId);
    };
    for (const Track* t : pending)
        forEachTarget(*t, [&](const Uuid& to) {
            if (auto it = indegree.find(to); it != indegree.end()) ++it->second;
        });

    std::vector<Uuid> order;
    while (!pending.empty()) {
        auto it = std::find_if(pending.begin(), pending.end(), [&](const Track* t) { return indegree[t->id] == 0; });
        if (it == pending.end()) it = pending.begin();  // a cycle cannot occur in a valid project; stay total anyway
        const Track* t = *it;
        order.push_back(t->id);
        forEachTarget(*t, [&](const Uuid& to) {
            if (auto d = indegree.find(to); d != indegree.end()) --d->second;
        });
        pending.erase(it);
    }
    if (master) order.push_back(master->id);
    return order;
}

std::unique_ptr<TrackConfig> buildConfig(const Project& p, const Track& t, MediaStore& media) {
    auto cfg = std::make_unique<TrackConfig>();
    for (const Region& r : t.regions) {
        RegionPlayback rp;
        rp.startFrame = regionFrame(p, r, r.start);
        rp.endFrame = regionFrame(p, r, r.start + r.length);
        rp.gain = dbToLinear(r.gainDb);
        if (t.kind == TrackKind::Audio) {
            const MediaItem* item = p.findMedia(r.mediaId);
            std::shared_ptr<IFrameSource> src = item ? media.open(*item) : nullptr;
            if (!src) continue;  // missing media: the region stays silent, MediaStore keeps the warning
            rp.source = src.get();
            rp.sourceOffsetFrames = r.sourceOffsetFrames;
            cfg->keepAlive.push_back(std::move(src));
        } else {
            for (const MidiNote& n : r.notes) {
                const std::int64_t on = toFrames(p.tempoMap.ticksToSamples(r.start + n.start, p.sampleRate));
                const std::int64_t off = toFrames(p.tempoMap.ticksToSamples(r.start + n.start + n.length, p.sampleRate));
                rp.notes.push_back(NoteSpan{on, std::max(on, off), n.note, n.velocity});
            }
            std::stable_sort(rp.notes.begin(), rp.notes.end(), [](const NoteSpan& a, const NoteSpan& b) { return a.onFrame < b.onFrame; });
        }
        cfg->regions.push_back(std::move(rp));
    }
    for (const ProcessorRef& ref : t.strip.inserts)
        if (auto effect = makeEffect(ref)) cfg->inserts.push_back(std::move(effect));
    for (const Send& s : t.strip.sends) cfg->sends.push_back(SendPlayback{s.targetTrackId, dbToLinear(s.levelDb), s.preFader});
    cfg->output = t.strip.output;
    return cfg;
}

std::unique_ptr<TrackNode> buildNode(const Project& p, const Track& t, MediaStore& media) {
    return std::make_unique<TrackNode>(t.id, t.kind, stripParamsOf(t.strip), buildConfig(p, t, media).release(),
                                       static_cast<double>(p.sampleRate));
}

std::vector<AudioMsg> initialMessages(const Project& p, MediaStore& media) {
    std::vector<AudioMsg> out;
    for (const Track& t : p.tracks) out.push_back(addMsg(buildNode(p, t, media)));
    out.push_back(reorderMsg(p));
    return out;
}

std::vector<AudioMsg> diffToMessages(const Project& before, const Project& after, MediaStore& media) {
    std::vector<AudioMsg> out;
    bool structural = false;
    const bool timingChanged = before.tempoMap != after.tempoMap || before.sampleRate != after.sampleRate;

    for (const Track& a : after.tracks) {
        if (!before.findTrack(a.id)) {
            out.push_back(addMsg(buildNode(after, a, media)));
            structural = true;
        }
    }
    for (const Track& a : after.tracks) {
        const Track* b = before.findTrack(a.id);
        if (!b) continue;
        if (stripChanged(*b, a)) out.push_back(stripMsg(a));
        if (timingChanged || configChanged(*b, a)) out.push_back(configMsg(after, a, media));
    }
    for (const Track& b : before.tracks) {
        if (!after.findTrack(b.id)) {
            out.push_back(removeMsg(b.id));
            structural = true;
        }
    }
    if (structural || processingOrder(before) != processingOrder(after)) out.push_back(reorderMsg(after));
    return out;
}

}  // namespace lpc
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target lpc_tests && build/tests/Debug/lpc_tests.exe "[builder]"`
Expected: `All tests passed`.

If the property test reports a mismatch, print both `describe()` documents for the failing seed and step: the message ordering rule (Adds, Sets, Removes, Reorder) and the `configChanged` field list are the usual culprits. Every field of `Track` that influences `buildConfig` must be in `configChanged`; every field that influences `stripParamsOf` must be in `stripChanged`.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat(core): model-to-graph translator with equivalence property test" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

### Task 11: Audio engine (queues, transport, loop) and the project thread host

**Files:**
- Create: `core/include/lpc/audio/engine.h`, `core/src/audio/engine.cpp`
- Create: `core/include/lpc/project_host.h`, `core/src/project_host.cpp`
- Create: `tests/threaded_device.h`
- Test: `tests/test_engine.cpp`, `tests/test_project_host.cpp`

**Interfaces:**
- Consumes: `RenderGraph`, `AudioMsg`, `SpscQueue` (Tasks 7, 9), `initialMessages`/`diffToMessages` (Task 10), `UndoStack`/`Command` (Tasks 5, 6), `MediaStore` (Task 8).
- Produces:
  - `lpc::audio::AudioEngine(double sampleRate)`:
    - project thread: `bool postMessage(const AudioMsg&)` (false when the queue is full, never drops), `std::size_t collectGarbage()` (destroys objects handed back, returns how many), `void applyDirect(const AudioMsg&)` (only while no audio thread runs)
    - any thread: `std::uint64_t appliedSeq() const`, `std::int64_t positionFrames() const`, `bool playing() const`, `float masterPeak() const`, `std::uint64_t garbageOverflow() const`
    - audio thread: `void processBlock(float* outL, float* outR, int frames) noexcept` (any frame count; sliced internally; handles `Play`, `Stop`, `Locate`, `SetLoop` and the graph messages)
    - tests: `nlohmann::json describeForTest() const` (only while the audio thread is stopped)
    - constants `kMessageQueueCapacity = 8192`, `kFeedbackQueueCapacity = 4096`.
  - `lpc::ProjectHost(Project initial, audio::AudioEngine&, MediaStore&)` owning the project thread:
    - `std::future<std::optional<CommandError>> submit(CommandPtr)`, `undo()`, `redo()`
    - `template <class F> std::future<std::invoke_result_t<F, const Project&>> read(F fn)` (runs `fn` on the project thread; `read([](const Project& p) { return p; })` is the snapshot used for saving)
    - `std::future<void> play()`, `stop()`, `locate(std::int64_t frame)`, `setLoop(std::int64_t startFrame, std::int64_t endFrame)` (end <= start disables the loop)
    - `std::uint64_t lastPostedSeq() const`.
  - Test helper `lpc::test::ThreadedDevice(AudioEngine&, int block = 256)`: a thread that calls `processBlock` inside an `rt::Scope`, with `stop()`.

> The loop uses `MsgKind::SetLoop` and the field `AudioMsg::frame2`, both already defined in Task 7's `messages.h`.

- [ ] **Step 1: Write the failing engine tests**

`tests/test_engine.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "lpc/audio/engine.h"
#include "lpc/commands.h"
#include "lpc/graph_builder.h"
#include "rt_guard.h"

using namespace lpc;
using namespace lpc::audio;

namespace {

constexpr double kSr = 48000.0;

AudioMsg msg(MsgKind kind, std::uint64_t seq, std::int64_t frame = 0, std::int64_t frame2 = 0) {
    AudioMsg m;
    m.kind = kind;
    m.seq = seq;
    m.frame = frame;
    m.frame2 = frame2;
    return m;
}

// One audio track playing a ramp (value = frame / 100000) from frame 0; the source is 48000 frames long.
struct Setup {
    std::mt19937_64 rng{5};
    Project p{Uuid::random(rng)};
    MediaStore media;
    Uuid audioId;

    Setup() {
        std::vector<float> ramp(2 * 48000);
        for (int i = 0; i < 48000; ++i) ramp[static_cast<std::size_t>(2 * i)] = ramp[static_cast<std::size_t>(2 * i + 1)] = static_cast<float>(i) / 100000.0f;
        const MediaItem item{Uuid::random(rng), "audio/ramp.wav", "h", 48000, 2, 48000};
        media.registerSource(item.id, std::make_shared<MemorySource>(48000, 2, ramp));
        REQUIRE(makeAddMedia(item)->apply(p).ok());
        Track t;
        t.id = Uuid::random(rng);
        t.kind = TrackKind::Audio;
        t.name = "A";
        Region r;
        r.id = Uuid::random(rng);
        r.length = 8 * kPPQ;
        r.mediaId = item.id;
        t.regions.push_back(r);
        audioId = t.id;
        REQUIRE(makeAddTrack(t)->apply(p).ok());
    }

    void load(AudioEngine& e) {
        for (const AudioMsg& m : initialMessages(p, media)) e.applyDirect(m);
    }
};

float rampAt(std::int64_t frame) { return static_cast<float>(frame) / 100000.0f; }

}  // namespace

TEST_CASE("engine: stopped is silent; play, locate and stop move the transport", "[engine]") {
    Setup s;
    AudioEngine e(kSr);
    s.load(e);
    std::vector<float> l(256), r(256);

    e.processBlock(l.data(), r.data(), 256);
    for (float v : l) REQUIRE(v == 0.0f);
    REQUIRE_FALSE(e.playing());
    REQUIRE(e.positionFrames() == 0);

    REQUIRE(e.postMessage(msg(MsgKind::Play, 1)));
    e.processBlock(l.data(), r.data(), 256);
    REQUIRE(e.playing());
    REQUIRE(l[100] == rampAt(100));
    REQUIRE(e.positionFrames() == 256);
    REQUIRE(e.appliedSeq() == 1);

    REQUIRE(e.postMessage(msg(MsgKind::Locate, 2, 1000)));
    e.processBlock(l.data(), r.data(), 256);
    REQUIRE(l[0] == rampAt(1000));
    REQUIRE(l[255] == rampAt(1255));
    REQUIRE(e.positionFrames() == 1256);
    REQUIRE(e.masterPeak() > 0.0f);

    REQUIRE(e.postMessage(msg(MsgKind::Stop, 3)));
    e.processBlock(l.data(), r.data(), 256);
    for (float v : l) REQUIRE(v == 0.0f);
    REQUIRE(e.positionFrames() == 1256);  // stopped: the playhead stays where it is
    REQUIRE_FALSE(e.playing());
}

TEST_CASE("engine: a full queue reports failure instead of dropping, and draining makes room", "[engine]") {
    AudioEngine e(kSr);
    for (std::size_t i = 0; i < kMessageQueueCapacity; ++i) REQUIRE(e.postMessage(msg(MsgKind::Stop, i + 1)));
    REQUIRE_FALSE(e.postMessage(msg(MsgKind::Stop, 99999)));
    std::vector<float> l(64), r(64);
    e.processBlock(l.data(), r.data(), 64);
    REQUIRE(e.appliedSeq() == kMessageQueueCapacity);  // all of them, in order
    REQUIRE(e.postMessage(msg(MsgKind::Stop, 99999)));
}

TEST_CASE("engine: replaced objects come back for destruction on the project thread", "[engine]") {
    Setup s;
    AudioEngine e(kSr);
    s.load(e);
    AudioMsg set;
    set.kind = MsgKind::SetConfig;
    set.seq = 5;
    set.track = s.audioId;
    set.obj = makeOwned(buildConfig(s.p, *s.p.findTrack(s.audioId), s.media).release());
    REQUIRE(e.postMessage(set));
    std::vector<float> l(64), r(64);
    e.processBlock(l.data(), r.data(), 64);
    REQUIRE(e.appliedSeq() == 5);
    REQUIRE(e.collectGarbage() == 1);
    REQUIRE(e.collectGarbage() == 0);
    REQUIRE(e.garbageOverflow() == 0);
}

TEST_CASE("engine: any block length gives the same samples", "[engine]") {
    Setup s;
    AudioEngine big(kSr), small(kSr);
    s.load(big);
    s.load(small);
    big.postMessage(msg(MsgKind::Play, 1));
    small.postMessage(msg(MsgKind::Play, 1));
    std::vector<float> a(1000), ar(1000), b(1000), br(1000);
    big.processBlock(a.data(), ar.data(), 1000);  // sliced internally (> kMaxBlock)
    for (int i = 0; i < 4; ++i) small.processBlock(b.data() + i * 250, br.data() + i * 250, 250);
    REQUIRE(a == b);
    REQUIRE(a[999] == rampAt(999));
}

TEST_CASE("engine: loop wraps sample-accurately and can be switched off", "[engine][loop]") {
    Setup s;
    AudioEngine e(kSr);
    s.load(e);
    e.postMessage(msg(MsgKind::SetLoop, 1, 100, 300));
    e.postMessage(msg(MsgKind::Locate, 2, 100));
    e.postMessage(msg(MsgKind::Play, 3));
    std::vector<float> l(1100), r(1100);
    e.processBlock(l.data(), r.data(), 1100);
    for (std::int64_t i = 0; i < 1100; ++i) REQUIRE(l[static_cast<std::size_t>(i)] == rampAt(100 + i % 200));
    REQUIRE(e.positionFrames() == 200);  // five full loops plus 100 frames

    e.postMessage(msg(MsgKind::SetLoop, 4, 0, 0));  // end <= start: loop off
    std::vector<float> m(400), mr(400);
    e.processBlock(m.data(), mr.data(), 400);
    REQUIRE(m[0] == rampAt(200));
    REQUIRE(m[399] == rampAt(599));
}

TEST_CASE("engine: playing, message handling and garbage hand-back never allocate", "[engine][rt]") {
    Setup s;
    AudioEngine e(kSr);
    s.load(e);
    AudioMsg set;
    set.kind = MsgKind::SetConfig;
    set.seq = 10;
    set.track = s.audioId;
    set.obj = makeOwned(buildConfig(s.p, *s.p.findTrack(s.audioId), s.media).release());  // built outside the section
    std::vector<float> l(256), r(256);
    e.postMessage(msg(MsgKind::Play, 1));
    e.postMessage(set);
    e.postMessage(msg(MsgKind::SetLoop, 11, 0, 5000));
    test::rt::reset();
    {
        test::rt::Scope scope;
        for (int i = 0; i < 40; ++i) e.processBlock(l.data(), r.data(), 256);
    }
    REQUIRE(test::rt::violations() == 0);
    REQUIRE(e.collectGarbage() == 1);
}
```

`tests/threaded_device.h`:

```cpp
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
```

`tests/test_project_host.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>
#include "lpc/audio/engine.h"
#include "lpc/commands.h"
#include "lpc/graph_builder.h"
#include "lpc/project_host.h"
#include "lpc/project_io.h"
#include "random_commands.h"
#include "temp_dir.h"
#include "threaded_device.h"

using namespace lpc;
using namespace lpc::audio;

namespace {

Track track(std::mt19937_64& rng, TrackKind kind, const char* name) {
    Track t;
    t.id = Uuid::random(rng);
    t.kind = kind;
    t.name = name;
    if (kind == TrackKind::Instrument) t.instrument = ProcessorRef{"builtin.sine", {}, ""};
    return t;
}

// Waits until the audio thread has applied everything the host has posted.
bool settle(AudioEngine& engine, const ProjectHost& host) {
    for (int i = 0; i < 3000; ++i) {
        if (engine.appliedSeq() >= host.lastPostedSeq()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool waitFor(const std::function<bool()>& cond) {
    for (int i = 0; i < 3000; ++i) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// What a graph built from scratch for `p` looks like.
nlohmann::json freshDescription(const Project& p, MediaStore& media) {
    RenderGraph g(static_cast<double>(p.sampleRate));
    for (AudioMsg& m : initialMessages(p, media)) g.apply(m).destroy();
    return g.describe();
}

struct Fixture {
    std::mt19937_64 rng{11};
    Project initial{Uuid::random(rng)};
    MediaStore media;
    MediaItem item{Uuid::random(rng), "audio/x.wav", "h", 48000, 2, 48000};
    AudioEngine engine{48000.0};

    Fixture() {
        media.registerSource(item.id, std::make_shared<MemorySource>(48000, 2, std::vector<float>(2 * 48000, 0.25f)));
        REQUIRE(makeAddMedia(item)->apply(initial).ok());
    }
};

}  // namespace

TEST_CASE("host: commands reach the audio graph and it matches a fresh build", "[host][threads]") {
    test::rt::reset();
    Fixture f;
    test::ThreadedDevice device(f.engine);
    Project finalProject;
    {
        ProjectHost host(f.initial, f.engine, f.media);
        const Track bus = track(f.rng, TrackKind::Bus, "Bus");
        Track audio = track(f.rng, TrackKind::Audio, "Audio");
        audio.strip.output = bus.id;
        Region r;
        r.id = Uuid::random(f.rng);
        r.length = 4 * kPPQ;
        r.mediaId = f.item.id;

        REQUIRE_FALSE(host.submit(makeAddTrack(bus)).get().has_value());
        REQUIRE_FALSE(host.submit(makeAddTrack(audio)).get().has_value());
        REQUIRE_FALSE(host.submit(makeAddRegion(audio.id, r)).get().has_value());
        StripPatch patch;
        patch.gainDb = -6.0f;
        REQUIRE_FALSE(host.submit(makeSetStrip(audio.id, patch)).get().has_value());
        REQUIRE_FALSE(host.submit(makeSetTempo(kPPQ, 90.0)).get().has_value());

        REQUIRE(settle(f.engine, host));
        device.stop();
        finalProject = host.read([](const Project& p) { return p; }).get();
    }
    REQUIRE(f.engine.describeForTest() == freshDescription(finalProject, f.media));
    REQUIRE(test::rt::violations() == 0);
}

TEST_CASE("host: rejected commands report their error and change nothing", "[host][threads]") {
    Fixture f;
    test::ThreadedDevice device(f.engine);
    ProjectHost host(f.initial, f.engine, f.media);
    const auto err = host.submit(makeRemoveTrack(Uuid::random(f.rng))).get();
    REQUIRE(err.has_value());
    REQUIRE(err->code == "not_found");
    REQUIRE(host.read([](const Project& p) { return p; }).get() == f.initial);
    REQUIRE(host.undo().get()->code == "nothing_to_undo");
}

TEST_CASE("host: undo and redo reach the audio graph", "[host][threads]") {
    Fixture f;
    test::ThreadedDevice device(f.engine);
    ProjectHost host(f.initial, f.engine, f.media);
    const Track a = track(f.rng, TrackKind::Audio, "A");
    REQUIRE_FALSE(host.submit(makeAddTrack(a)).get().has_value());
    REQUIRE(settle(f.engine, host));
    REQUIRE_FALSE(host.undo().get().has_value());
    REQUIRE(settle(f.engine, host));
    device.stop();
    REQUIRE(f.engine.describeForTest()["tracks"].size() == 1);  // only the master is left

    test::ThreadedDevice again(f.engine);  // restart the stand-in device for the redo
    REQUIRE_FALSE(host.redo().get().has_value());
    REQUIRE(settle(f.engine, host));
    again.stop();
    REQUIRE(f.engine.describeForTest()["tracks"].size() == 2);
}

TEST_CASE("host: transport commands move the playhead", "[host][threads]") {
    Fixture f;
    test::ThreadedDevice device(f.engine);
    ProjectHost host(f.initial, f.engine, f.media);
    host.locate(1000).get();
    host.play().get();
    REQUIRE(waitFor([&] { return f.engine.playing() && f.engine.positionFrames() > 1000; }));
    host.stop().get();
    REQUIRE(waitFor([&] { return !f.engine.playing(); }));
    const auto stoppedAt = f.engine.positionFrames();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(f.engine.positionFrames() == stoppedAt);
}

TEST_CASE("host: a snapshot can be saved while audio runs and reloads equal", "[host][threads][io]") {
    Fixture f;
    test::ThreadedDevice device(f.engine);
    ProjectHost host(f.initial, f.engine, f.media);
    REQUIRE_FALSE(host.submit(makeAddTrack(track(f.rng, TrackKind::Instrument, "Keys"))).get().has_value());
    const Project snapshot = host.read([](const Project& p) { return p; }).get();
    test::TempDir tmp;
    saveProject(snapshot, tmp.path / "live.lpc");  // runs on this thread: the project thread is not blocked by I/O
    REQUIRE(loadProject(tmp.path / "live.lpc") == snapshot);
}

TEST_CASE("host: many random commands under load stay consistent", "[host][threads][property]") {
    test::rt::reset();
    Fixture f;
    test::ThreadedDevice device(f.engine);
    Project finalProject;
    {
        ProjectHost host(f.initial, f.engine, f.media);
        std::mt19937_64 rng(77);
        std::unordered_set<Uuid> registered{f.item.id};  // register each media id once: a second registration would change the pointer
        int accepted = 0;
        for (int i = 0; i < 300; ++i) {
            const Project current = host.read([](const Project& p) { return p; }).get();
            for (const MediaItem& m : current.mediaPool)
                if (registered.insert(m.id).second) f.media.registerSource(m.id, std::make_shared<MemorySource>(48000, 2, std::vector<float>(2 * 4800, 0.1f)));
            CommandPtr c = test::randomCommand(current, rng);
            if (!c) continue;
            if (!host.submit(std::move(c)).get().has_value()) ++accepted;
            if (i % 9 == 0) host.undo().get();
        }
        REQUIRE(accepted > 40);
        REQUIRE(settle(f.engine, host));
        device.stop();
        finalProject = host.read([](const Project& p) { return p; }).get();
    }
    REQUIRE(f.engine.describeForTest() == freshDescription(finalProject, f.media));
    REQUIRE(test::rt::violations() == 0);
}
```

Add `#include <functional>` and `#include <unordered_set>` to `test_project_host.cpp` (for `std::function` and `std::unordered_set`).

Note on the last test: media registered after a command that adds a media item that is later referenced by regions. `registerSource` is called before the next `submit`, so the media exists when regions are built. Media ids created by `AddMedia` that are never registered make `buildConfig` skip their regions in both graphs, which keeps the comparison valid.

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build build --config Debug --target lpc_tests`
Expected: FAIL to compile, `Cannot open include file: 'lpc/audio/engine.h'`.

- [ ] **Step 3: Implement the engine**

`core/include/lpc/audio/engine.h`:

```cpp
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

#include <nlohmann/json.hpp>

#include "lpc/audio/messages.h"
#include "lpc/audio/render_graph.h"
#include "lpc/audio/spsc_queue.h"

namespace lpc::audio {

inline constexpr std::size_t kMessageQueueCapacity = 8192;
inline constexpr std::size_t kFeedbackQueueCapacity = 4096;

class AudioEngine {
public:
    explicit AudioEngine(double sampleRate);
    double sampleRate() const { return sampleRate_; }

    // ---- project thread
    bool postMessage(const AudioMsg& m);  // false when full; the caller retries, nothing is dropped
    std::size_t collectGarbage();         // destroys objects handed back by the audio thread
    void applyDirect(const AudioMsg& m);  // only while no audio thread is running (offline rendering, tests)

    // ---- any thread
    std::uint64_t appliedSeq() const { return appliedSeq_.load(std::memory_order_acquire); }
    std::int64_t positionFrames() const { return positionPub_.load(std::memory_order_relaxed); }
    bool playing() const { return playingPub_.load(std::memory_order_relaxed); }
    float masterPeak() const { return masterPeakPub_.load(std::memory_order_relaxed); }
    std::uint64_t garbageOverflow() const { return garbageOverflow_.load(std::memory_order_relaxed); }

    // ---- audio thread
    void processBlock(float* outL, float* outR, int frames) noexcept;

    // ---- tests: only while the audio thread is stopped
    nlohmann::json describeForTest() const { return graph_.describe(); }

private:
    void drain() noexcept;
    void handle(const AudioMsg& m) noexcept;

    double sampleRate_;
    RenderGraph graph_;
    SpscQueue<AudioMsg, kMessageQueueCapacity> messages_;
    SpscQueue<Feedback, kFeedbackQueueCapacity> feedback_;

    // owned by the audio thread
    bool playing_ = false;
    std::int64_t position_ = 0;
    std::int64_t loopStart_ = 0;
    std::int64_t loopEnd_ = 0;  // loop is on when loopEnd_ > loopStart_

    // published for other threads
    std::atomic<std::uint64_t> appliedSeq_{0};
    std::atomic<std::int64_t> positionPub_{0};
    std::atomic<bool> playingPub_{false};
    std::atomic<float> masterPeakPub_{0.0f};
    std::atomic<std::uint64_t> garbageOverflow_{0};
};

}  // namespace lpc::audio
```

`core/src/audio/engine.cpp`:

```cpp
#include "lpc/audio/engine.h"

#include <algorithm>

namespace lpc::audio {

AudioEngine::AudioEngine(double sampleRate) : sampleRate_(sampleRate), graph_(sampleRate) {}

bool AudioEngine::postMessage(const AudioMsg& m) { return messages_.push(m); }

std::size_t AudioEngine::collectGarbage() {
    std::size_t n = 0;
    Feedback f;
    while (feedback_.pop(f)) {
        f.garbage.destroy();
        ++n;
    }
    return n;
}

void AudioEngine::applyDirect(const AudioMsg& m) {
    handle(m);
    collectGarbage();
}

void AudioEngine::handle(const AudioMsg& m) noexcept {
    switch (m.kind) {
        case MsgKind::Play:
            playing_ = true;
            playingPub_.store(true, std::memory_order_relaxed);
            break;
        case MsgKind::Stop:
            playing_ = false;
            playingPub_.store(false, std::memory_order_relaxed);
            graph_.allNotesOff();
            break;
        case MsgKind::Locate:
            position_ = std::max<std::int64_t>(m.frame, 0);
            positionPub_.store(position_, std::memory_order_relaxed);
            graph_.allNotesOff();
            break;
        case MsgKind::SetLoop:
            loopStart_ = std::max<std::int64_t>(m.frame, 0);
            loopEnd_ = m.frame2;
            break;
        default: {
            Owned garbage = graph_.apply(m);
            if (garbage.ptr && !feedback_.push(Feedback{garbage})) garbageOverflow_.fetch_add(1, std::memory_order_relaxed);  // leaked, counted
            break;
        }
    }
    appliedSeq_.store(m.seq, std::memory_order_release);
}

void AudioEngine::drain() noexcept {
    AudioMsg m;
    while (messages_.pop(m)) handle(m);
}

void AudioEngine::processBlock(float* outL, float* outR, int frames) noexcept {
    drain();
    int done = 0;
    while (done < frames) {
        int n = std::min(frames - done, kMaxBlock);
        if (playing_) {
            if (loopEnd_ > loopStart_) {
                if (position_ >= loopEnd_) {  // wrap exactly at the loop end
                    position_ = loopStart_;
                    graph_.allNotesOff();
                }
                n = static_cast<int>(std::min<std::int64_t>(n, loopEnd_ - position_));
            }
            graph_.render(position_, n, outL + done, outR + done);
            position_ += n;
        } else {
            std::fill_n(outL + done, n, 0.0f);
            std::fill_n(outR + done, n, 0.0f);
        }
        done += n;
    }
    positionPub_.store(position_, std::memory_order_relaxed);
    masterPeakPub_.store(graph_.masterPeak(), std::memory_order_relaxed);
}

}  // namespace lpc::audio
```

- [ ] **Step 4: Implement the project host**

`core/include/lpc/project_host.h`:

```cpp
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
```

`core/src/project_host.cpp`:

```cpp
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
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build --config Debug --target lpc_tests && build/tests/Debug/lpc_tests.exe "[engine],[host]"`
Expected: `All tests passed`. Run the threaded tests several times (`for i in 1 2 3 4 5; do build/tests/Debug/lpc_tests.exe "[host]"; done`); a flaky result means a real race, not a test problem.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(core): audio engine with transport and loop, project-thread host" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 12: Offline render, demo project, CLI `demo`/`render`/`info`, golden tests

**Files:**
- Create: `core/include/lpc/offline_render.h`, `core/src/offline_render.cpp`
- Create: `core/include/lpc/demo_project.h`, `core/src/demo_project.cpp`
- Create: `tools/lpc-cli/CMakeLists.txt` (replaces the stub), `tools/lpc-cli/cli_commands.h`, `tools/lpc-cli/cli_commands.cpp`, `tools/lpc-cli/main.cpp`, `tools/lpc-cli/play_unavailable.cpp`
- Modify: `CMakeLists.txt` (build tools before tests), `tests/CMakeLists.txt` (link the CLI library)
- Test: `tests/test_offline_render.cpp`, `tests/test_cli.cpp`, `tests/test_golden.cpp`, `tests/golden/demo.wav` (generated in Step 6)
- Create: `tests/play_stub.cpp` (the test executable links `lpc_cli_lib`, which references `runPlay`; the real one lives in the `lpc-cli` executable, so the tests need their own definition)

**Interfaces:**
- Consumes: `AudioEngine` (Task 11), `initialMessages` (Task 10), `MediaStore` (Task 8), `loadProject`/`saveProject` (Task 4), `writeWav`/`readWav` (Task 8).
- Produces:
  - `struct RenderOptions{int64 startFrame=0; int64 frames=-1; double tailSeconds=0.5; int blockSize=256;}`, `struct RenderResult{vector<float> interleaved; int sampleRate; int64 frames;}`, `std::int64_t projectEndFrame(const Project&)`, `RenderResult renderOffline(const Project&, MediaStore&, const RenderOptions& = {})` (stereo; deterministic when the store uses memory sources).
  - `Project makeDemoProject(const std::filesystem::path& dir)`: deterministic ids; when `dir` is not empty the media file `dir/audio/tone.wav` is written.
  - `int lpc::cli::runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err)`: `demo <dir>`, `render <project> <out.wav> [--seconds N] [--bits 16|24|32]`, `info <project>`, `play <project>`. Exit codes: 0 success, 1 usage error, 2 runtime error (message on `err`).
  - `int lpc::cli::runPlay(const std::vector<std::string>& args, std::ostream&, std::ostream&)` (declared in `cli_commands.h`; this task provides a stub that reports the audio device is unavailable, Task 13 provides the real one).

- [ ] **Step 1: Write the failing offline-render and golden tests**

`tests/test_offline_render.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "lpc/commands.h"
#include "lpc/demo_project.h"
#include "lpc/offline_render.h"
#include "temp_dir.h"

using namespace lpc;

namespace {
float peak(const std::vector<float>& v) {
    float p = 0.0f;
    for (float x : v) p = std::max(p, std::abs(x));
    return p;
}
}  // namespace

TEST_CASE("render: an empty project renders silence of the requested length", "[render]") {
    Project p;
    MediaStore media;
    RenderOptions o;
    o.frames = 1000;
    const RenderResult r = renderOffline(p, media, o);
    REQUIRE(r.frames == 1000);
    REQUIRE(r.interleaved.size() == 2000);
    REQUIRE(peak(r.interleaved) == 0.0f);

    const RenderResult tail = renderOffline(p, media);  // default: no regions, so only the tail
    REQUIRE(tail.frames == 24000);                      // 0.5 s at 48 kHz
}

TEST_CASE("render: default length is the end of the last region plus the tail", "[render]") {
    test::TempDir tmp;
    const Project p = makeDemoProject(tmp.path);
    REQUIRE(projectEndFrame(p) == 8 * 24000);  // keys region: 8 beats at 120 bpm
    MediaStore media(tmp.path, false);
    const RenderResult r = renderOffline(p, media);
    REQUIRE(r.frames == 8 * 24000 + 24000);
    REQUIRE(peak(r.interleaved) > 0.05f);
}

TEST_CASE("render: start frame and explicit length", "[render]") {
    test::TempDir tmp;
    const Project p = makeDemoProject(tmp.path);
    MediaStore media(tmp.path, false);
    RenderOptions all;
    all.frames = 96000;
    const RenderResult full = renderOffline(p, media, all);

    // Start exactly on the second chord (frame 48000): notes that began earlier do not sound when playback
    // starts in the middle of them, and the previous chord's 5 ms release tail (240 frames) is only in `full`.
    RenderOptions part;
    part.startFrame = 48000;
    part.frames = 6000;
    const RenderResult cut = renderOffline(p, media, part);
    REQUIRE(cut.frames == 6000);
    for (std::size_t i = 300 * 2; i < 6000 * 2; ++i) REQUIRE(std::abs(cut.interleaved[i] - full.interleaved[48000 * 2 + i]) <= 1e-6f);
}

TEST_CASE("render: identical for any block size and on repeated runs", "[render]") {
    test::TempDir tmp;
    const Project p = makeDemoProject(tmp.path);
    MediaStore media(tmp.path, false);
    RenderOptions base;
    base.frames = 60000;
    const RenderResult expected = renderOffline(p, media, base);
    REQUIRE(renderOffline(p, media, base).interleaved == expected.interleaved);
    for (const int block : {1, 64, 4096, 100000}) {
        CAPTURE(block);
        RenderOptions o = base;
        o.blockSize = block;
        REQUIRE(renderOffline(p, media, o).interleaved == expected.interleaved);
    }
}

TEST_CASE("render: missing media silences only the affected track and is reported", "[render]") {
    test::TempDir tmp;
    const Project p = makeDemoProject(tmp.path);
    std::filesystem::remove(tmp.path / "audio" / "tone.wav");
    MediaStore media(tmp.path, false);
    RenderOptions o;
    o.frames = 48000;
    const RenderResult r = renderOffline(p, media, o);
    REQUIRE(peak(r.interleaved) > 0.05f);  // the keys still play
    REQUIRE(media.missingCount() == 1);
}

TEST_CASE("render: regions entirely past the requested range and absurd options are safe", "[render]") {
    test::TempDir tmp;
    const Project p = makeDemoProject(tmp.path);
    MediaStore media(tmp.path, false);
    RenderOptions late;
    late.startFrame = 10'000'000;
    late.frames = 500;
    REQUIRE(peak(renderOffline(p, media, late).interleaved) == 0.0f);
    RenderOptions none;
    none.frames = 0;
    REQUIRE(renderOffline(p, media, none).interleaved.empty());
    RenderOptions negative;
    negative.frames = -1;
    negative.startFrame = 10'000'000;  // start beyond the project end: only the tail remains, never a negative length
    REQUIRE(renderOffline(p, media, negative).frames >= 0);
    RenderOptions zeroBlock;
    zeroBlock.frames = 100;
    zeroBlock.blockSize = 0;  // clamped to 1
    REQUIRE(renderOffline(p, media, zeroBlock).frames == 100);
}
```

`tests/test_golden.cpp`:

```cpp
#define _CRT_SECURE_NO_WARNINGS
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <string>
#include "lpc/demo_project.h"
#include "lpc/offline_render.h"
#include "lpc/wav.h"
#include "temp_dir.h"

using namespace lpc;
namespace fs = std::filesystem;

namespace {

// Compares a render with tests/golden/<name>.wav. Run with LPC_UPDATE_GOLDEN=1 to (re)write the reference.
void checkGolden(const std::string& name, const RenderResult& actual) {
    const fs::path file = fs::path(LPC_TEST_DIR) / "golden" / (name + ".wav");
    const char* update = std::getenv("LPC_UPDATE_GOLDEN");
    if (update && std::string(update) == "1") {
        fs::create_directories(file.parent_path());
        writeWav(file, actual.sampleRate, 2, actual.interleaved, WavFormat::Pcm16);
        WARN("golden file written: " << file.string());
        return;
    }
    REQUIRE(fs::exists(file));
    const WavData golden = readWav(file);
    REQUIRE(golden.sampleRate == actual.sampleRate);
    REQUIRE(golden.samples.size() == actual.interleaved.size());
    float goldenPeak = 0.0f;
    double maxDiff = 0.0;
    for (std::size_t i = 0; i < golden.samples.size(); ++i) {
        goldenPeak = std::max(goldenPeak, std::abs(golden.samples[i]));
        maxDiff = std::max(maxDiff, static_cast<double>(std::abs(golden.samples[i] - actual.interleaved[i])));
    }
    REQUIRE(goldenPeak > 0.05f);  // a silent reference would make this test meaningless
    REQUIRE(maxDiff <= 2e-4);     // 16-bit quantisation of the reference is about 3e-5
}

}  // namespace

TEST_CASE("golden: the demo project renders as the reference file", "[golden]") {
    test::TempDir tmp;
    const Project p = makeDemoProject(tmp.path);
    MediaStore media(tmp.path, /*streaming=*/false);
    RenderOptions o;
    o.frames = 96000;  // the first two seconds
    checkGolden("demo", renderOffline(p, media, o));
}
```

- [ ] **Step 2: Write the failing CLI tests**

`tests/test_cli.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <fstream>
#include <sstream>

#ifdef LPC_HAVE_CLI_LIB

#include "cli_commands.h"
#include "lpc/project_io.h"
#include "lpc/wav.h"
#include "temp_dir.h"

using namespace lpc;
namespace fs = std::filesystem;

namespace {

struct Run {
    int code;
    std::string out, err;
};

Run cli(std::vector<std::string> args) {
    std::ostringstream out, err;
    const int code = cli::runCli(args, out, err);
    return {code, out.str(), err.str()};
}

std::string path(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

}  // namespace

TEST_CASE("cli: demo, info and render work together", "[cli]") {
    test::TempDir tmp;
    const std::string proj = path(tmp.path / "demo.lpc");
    const std::string wav = path(tmp.path / "out.wav");

    const Run demo = cli({"demo", proj});
    REQUIRE(demo.code == 0);
    REQUIRE(fs::exists(tmp.path / "demo.lpc" / "project.json"));
    REQUIRE(fs::exists(tmp.path / "demo.lpc" / "audio" / "tone.wav"));

    const Run info = cli({"info", proj});
    REQUIRE(info.code == 0);
    REQUIRE_THAT(info.out, Catch::Matchers::ContainsSubstring("Demo"));
    REQUIRE_THAT(info.out, Catch::Matchers::ContainsSubstring("Keys"));
    REQUIRE_THAT(info.out, Catch::Matchers::ContainsSubstring("48000"));

    const Run render = cli({"render", proj, wav, "--seconds", "1"});
    REQUIRE(render.code == 0);
    const WavData d = readWav(tmp.path / "out.wav");
    REQUIRE(d.frames() == 48000);
    REQUIRE(d.channels == 2);
    float peak = 0.0f;
    for (float v : d.samples) peak = std::max(peak, std::abs(v));
    REQUIRE(peak > 0.05f);

    REQUIRE(cli({"render", proj, wav, "--seconds", "0.5", "--bits", "16"}).code == 0);
    REQUIRE(readWav(tmp.path / "out.wav").frames() == 24000);
    REQUIRE(cli({"render", proj, wav, "--bits", "24"}).code == 0);  // default length: project end plus tail
    REQUIRE(readWav(tmp.path / "out.wav").frames() == 8 * 24000 + 24000);
}

TEST_CASE("cli: usage errors give exit code 1 and a usage text", "[cli]") {
    REQUIRE(cli({}).code == 1);
    REQUIRE_THAT(cli({}).err, Catch::Matchers::ContainsSubstring("usage"));
    REQUIRE(cli({"frobnicate"}).code == 1);
    REQUIRE(cli({"demo"}).code == 1);
    REQUIRE(cli({"render", "only-one-arg"}).code == 1);
    REQUIRE(cli({"render", "a", "b", "--seconds"}).code == 1);          // option without a value
    REQUIRE(cli({"render", "a", "b", "--seconds", "abc"}).code == 1);   // not a number
    REQUIRE(cli({"render", "a", "b", "--seconds", "-3"}).code == 1);    // negative
    REQUIRE(cli({"render", "a", "b", "--bits", "12"}).code == 1);
    REQUIRE(cli({"render", "a", "b", "--nope", "1"}).code == 1);
    REQUIRE(cli({"info"}).code == 1);
}

TEST_CASE("cli: runtime errors give exit code 2 and a readable message", "[cli]") {
    test::TempDir tmp;
    const Run missing = cli({"info", path(tmp.path / "nothing.lpc")});
    REQUIRE(missing.code == 2);
    REQUIRE_THAT(missing.err, Catch::Matchers::ContainsSubstring("error:"));

    const std::string proj = path(tmp.path / "p.lpc");
    REQUIRE(cli({"demo", proj}).code == 0);
    const Run badOut = cli({"render", proj, path(tmp.path / "no_such_dir" / "out.wav"), "--seconds", "0.1"});
    REQUIRE(badOut.code == 2);

    std::ofstream(tmp.path / "p.lpc" / "project.json", std::ios::binary | std::ios::trunc) << "{ truncated";
    REQUIRE(cli({"render", proj, path(tmp.path / "x.wav")}).code == 2);
}

TEST_CASE("cli: missing media is a warning, not a failure", "[cli]") {
    test::TempDir tmp;
    const std::string proj = path(tmp.path / "m.lpc");
    REQUIRE(cli({"demo", proj}).code == 0);
    fs::remove(tmp.path / "m.lpc" / "audio" / "tone.wav");
    const Run r = cli({"render", proj, path(tmp.path / "m.wav"), "--seconds", "1"});
    REQUIRE(r.code == 0);
    REQUIRE_THAT(r.err, Catch::Matchers::ContainsSubstring("warning:"));
    REQUIRE_THAT(r.err, Catch::Matchers::ContainsSubstring("tone.wav"));
    const Run info = cli({"info", proj});
    REQUIRE(info.code == 0);
    REQUIRE_THAT(info.err, Catch::Matchers::ContainsSubstring("tone.wav"));
}

TEST_CASE("cli: an empty project renders silence", "[cli]") {
    test::TempDir tmp;
    saveProject(Project{}, tmp.path / "e.lpc");
    REQUIRE(cli({"render", path(tmp.path / "e.lpc"), path(tmp.path / "e.wav"), "--seconds", "0.1"}).code == 0);
    const WavData d = readWav(tmp.path / "e.wav");
    REQUIRE(d.frames() == 4800);
    for (float v : d.samples) REQUIRE(v == 0.0f);
}

TEST_CASE("cli: play is routed to runPlay and its failure is reported", "[cli]") {
    // the test executable uses tests/play_stub.cpp, which reports that audio output is unavailable
    test::TempDir tmp;
    const Run r = cli({"play", path(tmp.path / "none.lpc")});
    REQUIRE(r.code == 2);
    REQUIRE_THAT(r.err, Catch::Matchers::ContainsSubstring("error:"));
}

#endif  // LPC_HAVE_CLI_LIB
```

- [ ] **Step 3: Run them to verify they fail**

Run: `cmake --build build --config Debug --target lpc_tests`
Expected: FAIL to compile, `Cannot open include file: 'lpc/offline_render.h'`.

- [ ] **Step 4: Implement offline render and the demo project**

`core/include/lpc/offline_render.h`:

```cpp
#pragma once
#include <cstdint>
#include <vector>

#include "lpc/media_store.h"
#include "lpc/model.h"

namespace lpc {

struct RenderOptions {
    std::int64_t startFrame = 0;
    std::int64_t frames = -1;  // -1: from startFrame to the end of the last region, plus tailSeconds
    double tailSeconds = 0.5;
    int blockSize = 256;       // clamped to [1, 65536]
};

struct RenderResult {
    std::vector<float> interleaved;  // stereo
    int sampleRate = 0;
    std::int64_t frames = 0;
};

// End of the last region of any track, in frames (0 for a project without regions).
std::int64_t projectEndFrame(const Project& project);

// Renders with the real engine and no device. Use a MediaStore with streaming = false: memory sources make
// the result deterministic (a streaming source could report an underrun when disk is slow).
RenderResult renderOffline(const Project& project, MediaStore& media, const RenderOptions& options = {});

}  // namespace lpc
```

`core/src/offline_render.cpp`:

```cpp
#include "lpc/offline_render.h"

#include <algorithm>
#include <cmath>

#include "lpc/audio/engine.h"
#include "lpc/graph_builder.h"

namespace lpc {

std::int64_t projectEndFrame(const Project& p) {
    std::int64_t end = 0;
    for (const Track& t : p.tracks) {
        for (const Region& r : t.regions) {
            const std::int64_t e = r.timeBase == TimeBase::Musical
                                       ? static_cast<std::int64_t>(std::llround(p.tempoMap.ticksToSamples(r.start + r.length, p.sampleRate)))
                                       : static_cast<std::int64_t>(std::llround(static_cast<double>(r.start + r.length) * p.sampleRate / 1e6));
            end = std::max(end, e);
        }
    }
    return end;
}

RenderResult renderOffline(const Project& p, MediaStore& media, const RenderOptions& options) {
    RenderResult out;
    out.sampleRate = p.sampleRate;
    const std::int64_t start = std::max<std::int64_t>(options.startFrame, 0);
    std::int64_t total = options.frames;
    if (total < 0) {
        total = std::max<std::int64_t>(0, projectEndFrame(p) - start) +
                static_cast<std::int64_t>(std::llround(std::max(0.0, options.tailSeconds) * p.sampleRate));
    }
    out.frames = total;
    out.interleaved.assign(static_cast<std::size_t>(total) * 2, 0.0f);

    audio::AudioEngine engine(static_cast<double>(p.sampleRate));
    for (const audio::AudioMsg& m : initialMessages(p, media)) engine.applyDirect(m);
    audio::AudioMsg locate;
    locate.kind = audio::MsgKind::Locate;
    locate.frame = start;
    engine.applyDirect(locate);
    audio::AudioMsg play;
    play.kind = audio::MsgKind::Play;
    engine.applyDirect(play);

    const int block = std::clamp(options.blockSize, 1, 65536);
    std::vector<float> l(static_cast<std::size_t>(block)), r(static_cast<std::size_t>(block));
    for (std::int64_t pos = 0; pos < total; pos += block) {
        const int n = static_cast<int>(std::min<std::int64_t>(block, total - pos));
        engine.processBlock(l.data(), r.data(), n);
        for (int i = 0; i < n; ++i) {
            out.interleaved[static_cast<std::size_t>((pos + i) * 2)] = l[static_cast<std::size_t>(i)];
            out.interleaved[static_cast<std::size_t>((pos + i) * 2 + 1)] = r[static_cast<std::size_t>(i)];
        }
    }
    engine.collectGarbage();
    return out;
}

}  // namespace lpc
```

`core/include/lpc/demo_project.h`:

```cpp
#pragma once
#include <filesystem>

#include "lpc/model.h"

namespace lpc {

// A small deterministic project (fixed ids): a sine "Keys" instrument playing four chords over two bars at
// 120 bpm with a post-fader send to a "Reverb Bus", and a 220 Hz "Tone" audio track. When `dir` is not empty
// the media file dir/audio/tone.wav is written (create the project folder with saveProject separately).
Project makeDemoProject(const std::filesystem::path& dir);

}  // namespace lpc
```

`core/src/demo_project.cpp`:

```cpp
#include "lpc/demo_project.h"

#include <cmath>
#include <random>

#include "lpc/wav.h"

namespace lpc {

Project makeDemoProject(const std::filesystem::path& dir) {
    std::mt19937_64 rng(42);
    Project p(Uuid::random(rng));
    p.name = "Demo";
    p.sampleRate = 48000;

    // 2 s of 220 Hz at -10 dBFS-ish amplitude, stereo
    MediaItem tone{Uuid::random(rng), "audio/tone.wav", "demo-tone-220hz", 48000, 2, 96000};
    if (!dir.empty()) {
        std::vector<float> samples(96000 * 2);
        for (int i = 0; i < 96000; ++i) {
            const float v = 0.3f * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * 220.0 * i / 48000.0));
            samples[static_cast<std::size_t>(2 * i)] = v;
            samples[static_cast<std::size_t>(2 * i + 1)] = v;
        }
        std::filesystem::create_directories(dir / "audio");
        writeWav(dir / "audio" / "tone.wav", 48000, 2, samples);
    }
    p.mediaPool.push_back(tone);

    Track bus;
    bus.id = Uuid::random(rng);
    bus.kind = TrackKind::Bus;
    bus.name = "Reverb Bus";
    bus.color = "purple";
    bus.strip.gainDb = -3.0f;
    bus.strip.inserts.push_back({"builtin.gain", {{"gainDb", -3.0}}, ""});

    Track keys;
    keys.id = Uuid::random(rng);
    keys.kind = TrackKind::Instrument;
    keys.name = "Keys";
    keys.color = "indigo";
    keys.instrument = ProcessorRef{"builtin.sine", {}, ""};
    keys.strip.pan = -0.3f;
    keys.strip.sends.push_back({Uuid::random(rng), bus.id, -6.0f, false});
    Region chords;
    chords.id = Uuid::random(rng);
    chords.start = 0;
    chords.length = 8 * kPPQ;
    const std::uint8_t progression[4][3] = {{60, 64, 67}, {65, 69, 72}, {67, 71, 74}, {60, 64, 67}};  // C F G C
    for (int chord = 0; chord < 4; ++chord)
        for (const std::uint8_t note : progression[chord]) chords.notes.push_back({chord * 2 * kPPQ, 2 * kPPQ, note, 90});
    keys.regions.push_back(chords);

    Track audio;
    audio.id = Uuid::random(rng);
    audio.kind = TrackKind::Audio;
    audio.name = "Tone";
    audio.color = "teal";
    audio.strip.gainDb = -6.0f;
    audio.strip.pan = 0.3f;
    Region toneRegion;
    toneRegion.id = Uuid::random(rng);
    toneRegion.start = 0;
    toneRegion.length = 4 * kPPQ;  // 2 s at 120 bpm
    toneRegion.mediaId = tone.id;
    audio.regions.push_back(toneRegion);

    p.tracks.push_back(bus);
    p.tracks.push_back(keys);
    p.tracks.push_back(audio);
    return p;
}

}  // namespace lpc
```

- [ ] **Step 5: Implement the CLI and wire the build**

Replace `tools/lpc-cli/CMakeLists.txt` with:

```cmake
# CLI without an audio device: demo, render, info. Task 13 adds the JUCE-based `play` command.
add_library(lpc_cli_lib STATIC cli_commands.cpp)
target_include_directories(lpc_cli_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lpc_cli_lib PUBLIC lpc_core)

add_executable(lpc-cli main.cpp play_unavailable.cpp)
target_link_libraries(lpc-cli PRIVATE lpc_cli_lib)
```

In the root `CMakeLists.txt`, move the CLI before the tests so the tests can link to it:

```cmake
enable_testing()
add_subdirectory(core)
if(LPC_BUILD_CLI)
    add_subdirectory(tools/lpc-cli)
endif()
add_subdirectory(tests)
```

Append to `tests/CMakeLists.txt` (before `include(Catch)`):

```cmake
if(TARGET lpc_cli_lib)
    target_link_libraries(lpc_tests PRIVATE lpc_cli_lib)
    target_compile_definitions(lpc_tests PRIVATE LPC_HAVE_CLI_LIB=1)
endif()
```

`tools/lpc-cli/cli_commands.h`:

```cpp
#pragma once
#include <iosfwd>
#include <string>
#include <vector>

namespace lpc::cli {

// args exclude the program name. Returns the process exit code: 0 ok, 1 usage error, 2 runtime error.
int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

// Implemented by play_command.cpp (with audio device) or play_unavailable.cpp (without).
int runPlay(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace lpc::cli
```

`tools/lpc-cli/main.cpp`:

```cpp
#include <iostream>
#include <string>
#include <vector>

#include "cli_commands.h"

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return lpc::cli::runCli(args, std::cout, std::cerr);
}
```

`tools/lpc-cli/play_unavailable.cpp`:

```cpp
#include <ostream>

#include "cli_commands.h"

namespace lpc::cli {

int runPlay(const std::vector<std::string>&, std::ostream&, std::ostream& err) {
    err << "error: this build has no audio device support (configure with -DLPC_WITH_JUCE=ON)\n";
    return 2;
}

}  // namespace lpc::cli
```

`tests/play_stub.cpp`:

```cpp
#ifdef LPC_HAVE_CLI_LIB

#include <ostream>

#include "cli_commands.h"

namespace lpc::cli {

int runPlay(const std::vector<std::string>&, std::ostream&, std::ostream& err) {
    err << "error: audio output is not available in the test build\n";
    return 2;
}

}  // namespace lpc::cli

#endif
```

`tools/lpc-cli/cli_commands.cpp`:

```cpp
#include "cli_commands.h"

#include <cmath>
#include <exception>
#include <filesystem>
#include <optional>
#include <ostream>

#include "lpc/demo_project.h"
#include "lpc/media_store.h"
#include "lpc/offline_render.h"
#include "lpc/project_io.h"
#include "lpc/wav.h"

namespace lpc::cli {

namespace {

namespace fs = std::filesystem;

constexpr const char* kUsage =
    "usage:\n"
    "  lpc-cli demo <project-dir>\n"
    "  lpc-cli info <project-dir>\n"
    "  lpc-cli render <project-dir> <out.wav> [--seconds N] [--bits 16|24|32]\n"
    "  lpc-cli play <project-dir>\n";

fs::path toPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string text(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

struct UsageError {
    std::string message;
};

// Splits "positional... --option value ..." and validates the options against `allowed`.
struct Parsed {
    std::vector<std::string> positional;
    std::vector<std::pair<std::string, std::string>> options;
    std::optional<std::string> get(const std::string& name) const {
        for (const auto& [k, v] : options)
            if (k == name) return v;
        return std::nullopt;
    }
};

Parsed parse(const std::vector<std::string>& args, std::size_t from, const std::vector<std::string>& allowed) {
    Parsed p;
    for (std::size_t i = from; i < args.size(); ++i) {
        if (args[i].rfind("--", 0) == 0) {
            if (std::find(allowed.begin(), allowed.end(), args[i]) == allowed.end()) throw UsageError{"unknown option " + args[i]};
            if (i + 1 >= args.size()) throw UsageError{"option " + args[i] + " needs a value"};
            p.options.emplace_back(args[i], args[i + 1]);
            ++i;
        } else {
            p.positional.push_back(args[i]);
        }
    }
    return p;
}

int cmdDemo(const std::vector<std::string>& args, std::ostream& out) {
    const Parsed p = parse(args, 1, {});
    if (p.positional.size() != 1) throw UsageError{"demo needs exactly one project directory"};
    const fs::path dir = toPath(p.positional[0]);
    saveProject(makeDemoProject(dir), dir);
    out << "demo project written to " << text(dir) << "\n";
    return 0;
}

int cmdInfo(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    const Parsed p = parse(args, 1, {});
    if (p.positional.size() != 1) throw UsageError{"info needs exactly one project directory"};
    const fs::path dir = toPath(p.positional[0]);
    const Project project = loadProject(dir);
    out << "name: " << project.name << "\n"
        << "sample rate: " << project.sampleRate << "\n"
        << "tempo: " << project.tempoMap.tempos().front().bpm << " bpm (" << project.tempoMap.tempos().size() << " tempo events)\n"
        << "tracks: " << project.tracks.size() << "\n";
    for (const Track& t : project.tracks) {
        static const char* kinds[] = {"audio", "midi", "instrument", "aux", "bus", "master"};
        out << "  - " << t.name << " [" << kinds[static_cast<int>(t.kind)] << "] regions=" << t.regions.size() << " gain=" << t.strip.gainDb
            << " dB pan=" << t.strip.pan << " sends=" << t.strip.sends.size() << "\n";
    }
    out << "media: " << project.mediaPool.size() << "\n";
    MediaStore media(dir, false);
    for (const MediaItem& m : project.mediaPool) {
        out << "  - " << m.path << "\n";
        media.open(m);
    }
    for (const std::string& w : media.warnings()) err << "warning: " << w << "\n";
    return 0;
}

int cmdRender(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    const Parsed p = parse(args, 1, {"--seconds", "--bits"});
    if (p.positional.size() != 2) throw UsageError{"render needs a project directory and an output file"};

    std::optional<double> seconds;
    if (const auto s = p.get("--seconds")) {
        try {
            std::size_t used = 0;
            const double v = std::stod(*s, &used);
            if (used != s->size() || !std::isfinite(v) || v < 0.0) throw std::invalid_argument("x");
            seconds = v;
        } catch (const std::exception&) {
            throw UsageError{"--seconds needs a non-negative number"};
        }
    }
    WavFormat format = WavFormat::Pcm24;
    if (const auto b = p.get("--bits")) {
        if (*b == "16") format = WavFormat::Pcm16;
        else if (*b == "24") format = WavFormat::Pcm24;
        else if (*b == "32") format = WavFormat::Float32;
        else throw UsageError{"--bits must be 16, 24 or 32"};
    }

    const fs::path dir = toPath(p.positional[0]);
    const Project project = loadProject(dir);
    MediaStore media(dir, /*streaming=*/false);  // memory sources: deterministic output
    RenderOptions options;
    if (seconds) options.frames = static_cast<std::int64_t>(std::llround(*seconds * project.sampleRate));
    const RenderResult result = renderOffline(project, media, options);
    writeWav(toPath(p.positional[1]), result.sampleRate, 2, result.interleaved, format);
    for (const std::string& w : media.warnings()) err << "warning: " << w << "\n";
    out << "rendered " << result.frames << " frames (" << static_cast<double>(result.frames) / result.sampleRate << " s) to "
        << p.positional[1] << "\n";
    return 0;
}

}  // namespace

int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (args.empty()) {
        err << kUsage;
        return 1;
    }
    try {
        const std::string& cmd = args[0];
        if (cmd == "demo") return cmdDemo(args, out);
        if (cmd == "info") return cmdInfo(args, out, err);
        if (cmd == "render") return cmdRender(args, out, err);
        if (cmd == "play") return runPlay(std::vector<std::string>(args.begin() + 1, args.end()), out, err);
        err << "unknown command: " << cmd << "\n" << kUsage;
        return 1;
    } catch (const UsageError& e) {
        err << "error: " << e.message << "\n" << kUsage;
        return 1;
    } catch (const std::exception& e) {
        err << "error: " << e.what() << "\n";
        return 2;
    }
}

}  // namespace lpc::cli
```

Add `#include <algorithm>` to `cli_commands.cpp` (for `std::find`).

- [ ] **Step 6: Build, then generate the golden reference**

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --target lpc_tests
LPC_UPDATE_GOLDEN=1 build/tests/Debug/lpc_tests.exe "[golden]"
build/tests/Debug/lpc_tests.exe "[golden]"
```

Expected: the first run prints `golden file written: ...tests\golden\demo.wav` and passes; the second run compares against it and passes. Sanity-check the reference by ear or by size: `ls -l tests/golden/demo.wav` should be about 384 KB, and `build/tools/lpc-cli/Debug/lpc-cli.exe info <demo dir>` (after `demo`) should list 4 tracks. A reference that is silent fails the `goldenPeak > 0.05` check on the second run.

- [ ] **Step 7: Run all tests**

Run: `build/tests/Debug/lpc_tests.exe "[render],[cli],[golden]"` then `ctest --test-dir build -C Debug --output-on-failure`
Expected: `All tests passed`.

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "feat(core): offline render, demo project, CLI demo/render/info, golden render test" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

### Task 13: Audio device interface, JUCE backend and `lpc-cli play`

**Files:**
- Create: `core/include/lpc/device.h`
- Create: `tools/lpc-cli/juce_device.h`, `tools/lpc-cli/juce_device.cpp`, `tools/lpc-cli/play_command.cpp`
- Modify: `tools/lpc-cli/CMakeLists.txt` (adds the JUCE branch)
- Test: `tests/test_device_interface.cpp` (interface contract, no hardware); manual verification of `play` (Step 5)

**Interfaces:**
- Consumes: `AudioEngine`, `ProjectHost`, `MediaStore`, `projectEndFrame`, `loadProject`, `cli::runPlay` declaration (Tasks 8, 11, 12).
- Produces:
  - `lpc::IAudioCallback { virtual void process(float* outL, float* outR, int frames) noexcept = 0; }`
  - `lpc::IAudioDevice { bool open(double sampleRate, int bufferSize, IAudioCallback&, std::string& error); void close(); double sampleRate() const; int bufferSize() const; }`
  - `std::unique_ptr<lpc::IAudioDevice> lpc::cli::makeJuceAudioDevice()` (in `tools/lpc-cli/juce_device.h`)
  - real `lpc::cli::runPlay` (replaces the stub when `LPC_WITH_JUCE=ON`, which is the default).

- [ ] **Step 1: Write the failing interface test**

`tests/test_device_interface.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <memory>
#include "lpc/audio/engine.h"
#include "lpc/device.h"

using namespace lpc;

namespace {

// A device that never touches hardware: the "device thread" is the test thread calling pump().
class FakeDevice final : public IAudioDevice {
public:
    bool open(double sampleRate, int bufferSize, IAudioCallback& cb, std::string& error) override {
        if (sampleRate != 48000.0) {
            error = "unsupported sample rate";
            return false;
        }
        rate_ = sampleRate;
        block_ = bufferSize;
        cb_ = &cb;
        return true;
    }
    void close() override { cb_ = nullptr; }
    double sampleRate() const override { return rate_; }
    int bufferSize() const override { return block_; }
    void pump(float* l, float* r) { if (cb_) cb_->process(l, r, block_); }

private:
    IAudioCallback* cb_ = nullptr;
    double rate_ = 0.0;
    int block_ = 0;
};

struct EngineCallback final : IAudioCallback {
    explicit EngineCallback(audio::AudioEngine& e) : engine(e) {}
    void process(float* l, float* r, int n) noexcept override { engine.processBlock(l, r, n); }
    audio::AudioEngine& engine;
};

}  // namespace

TEST_CASE("device: an engine can be driven through the device interface", "[device]") {
    audio::AudioEngine engine(48000.0);
    EngineCallback cb(engine);
    FakeDevice dev;
    std::string error;
    REQUIRE(dev.open(48000.0, 128, cb, error));
    audio::AudioMsg play;
    play.kind = audio::MsgKind::Play;
    REQUIRE(engine.postMessage(play));
    std::vector<float> l(128), r(128);
    dev.pump(l.data(), r.data());
    REQUIRE(engine.playing());
    REQUIRE(engine.positionFrames() == 128);
    dev.close();
}

TEST_CASE("device: opening with an unsupported rate reports an error", "[device]") {
    audio::AudioEngine engine(44100.0);
    EngineCallback cb(engine);
    FakeDevice dev;
    std::string error;
    REQUIRE_FALSE(dev.open(44100.0, 128, cb, error));
    REQUIRE_FALSE(error.empty());
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build build --config Debug --target lpc_tests`
Expected: FAIL to compile, `Cannot open include file: 'lpc/device.h'`.

- [ ] **Step 3: Implement the interface**

`core/include/lpc/device.h`:

```cpp
#pragma once
#include <string>

namespace lpc {

// Called on the device's audio thread. Must be real-time safe.
class IAudioCallback {
public:
    virtual ~IAudioCallback() = default;
    virtual void process(float* outL, float* outR, int frames) noexcept = 0;
};

class IAudioDevice {
public:
    virtual ~IAudioDevice() = default;
    // Opens the default output at exactly `sampleRate` (no resampling in Core). On failure returns false and
    // fills `error` with a message a user can act on.
    virtual bool open(double sampleRate, int bufferSize, IAudioCallback& callback, std::string& error) = 0;
    virtual void close() = 0;
    virtual double sampleRate() const = 0;
    virtual int bufferSize() const = 0;
};

}  // namespace lpc
```

Run: `cmake --build build --config Debug --target lpc_tests && build/tests/Debug/lpc_tests.exe "[device]"`
Expected: `All tests passed`.

- [ ] **Step 4: Implement the JUCE backend and the `play` command**

`tools/lpc-cli/juce_device.h`:

```cpp
#pragma once
#include <memory>

#include "lpc/device.h"

namespace lpc::cli {

std::unique_ptr<IAudioDevice> makeJuceAudioDevice();

}  // namespace lpc::cli
```

`tools/lpc-cli/juce_device.cpp`:

```cpp
#include "juce_device.h"

#include <cmath>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>

namespace lpc::cli {

namespace {

class JuceAudioDevice final : public IAudioDevice, private juce::AudioIODeviceCallback {
public:
    ~JuceAudioDevice() override { close(); }

    bool open(double sampleRate, int bufferSize, IAudioCallback& callback, std::string& error) override {
        callback_ = &callback;
        const juce::String initError = manager_.initialiseWithDefaultDevices(0, 2);
        if (initError.isNotEmpty()) {
            error = initError.toStdString();
            return false;
        }
        juce::AudioDeviceManager::AudioDeviceSetup setup = manager_.getAudioDeviceSetup();
        setup.sampleRate = sampleRate;
        setup.bufferSize = bufferSize;
        const juce::String setupError = manager_.setAudioDeviceSetup(setup, true);
        if (setupError.isNotEmpty()) {
            error = setupError.toStdString();
            return false;
        }
        juce::AudioIODevice* device = manager_.getCurrentAudioDevice();
        if (device == nullptr) {
            error = "no audio output device is available";
            return false;
        }
        rate_ = device->getCurrentSampleRate();
        block_ = device->getCurrentBufferSizeSamples();
        if (std::abs(rate_ - sampleRate) > 0.5) {
            error = "the audio device runs at " + std::to_string(static_cast<int>(rate_)) + " Hz but the project needs " +
                    std::to_string(static_cast<int>(sampleRate)) + " Hz; change the device sample rate in the system sound settings";
            manager_.closeAudioDevice();
            return false;
        }
        scratchL_.assign(kScratch, 0.0f);
        scratchR_.assign(kScratch, 0.0f);
        manager_.addAudioCallback(this);
        return true;
    }

    void close() override {
        manager_.removeAudioCallback(this);
        manager_.closeAudioDevice();
    }

    double sampleRate() const override { return rate_; }
    int bufferSize() const override { return block_; }

private:
    static constexpr int kScratch = 8192;

    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputs, int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override {
        if (numOutputs <= 0) return;
        if (numOutputs >= 2) {
            callback_->process(outputs[0], outputs[1], numSamples);
            for (int c = 2; c < numOutputs; ++c) juce::FloatVectorOperations::clear(outputs[c], numSamples);
            return;
        }
        for (int done = 0; done < numSamples;) {  // mono device: mix both channels down
            const int n = std::min(numSamples - done, kScratch);
            callback_->process(scratchL_.data(), scratchR_.data(), n);
            for (int i = 0; i < n; ++i) outputs[0][done + i] = 0.5f * (scratchL_[static_cast<std::size_t>(i)] + scratchR_[static_cast<std::size_t>(i)]);
            done += n;
        }
    }

    juce::AudioDeviceManager manager_;
    IAudioCallback* callback_ = nullptr;
    double rate_ = 0.0;
    int block_ = 0;
    std::vector<float> scratchL_, scratchR_;
};

}  // namespace

std::unique_ptr<IAudioDevice> makeJuceAudioDevice() { return std::make_unique<JuceAudioDevice>(); }

}  // namespace lpc::cli
```

Add `#include <algorithm>` and `#include <string>` to `juce_device.cpp`.

`tools/lpc-cli/play_command.cpp`:

```cpp
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iomanip>
#include <ostream>
#include <thread>

#include <juce_events/juce_events.h>

#include "cli_commands.h"
#include "juce_device.h"
#include "lpc/audio/engine.h"
#include "lpc/media_store.h"
#include "lpc/offline_render.h"
#include "lpc/project_host.h"
#include "lpc/project_io.h"

namespace lpc::cli {

namespace {

std::atomic<bool> gInterrupted{false};
extern "C" void onInterrupt(int) { gInterrupted.store(true); }

struct EngineCallback final : IAudioCallback {
    explicit EngineCallback(audio::AudioEngine& e) : engine(e) {}
    void process(float* l, float* r, int n) noexcept override { engine.processBlock(l, r, n); }
    audio::AudioEngine& engine;
};

}  // namespace

int runPlay(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    if (args.size() != 1) {
        err << "error: play needs exactly one project directory\nusage: lpc-cli play <project-dir>\n";
        return 1;
    }
    try {
        juce::ScopedJuceInitialiser_GUI juceInit;
        const std::filesystem::path dir = std::filesystem::path(std::u8string(args[0].begin(), args[0].end()));
        const Project project = loadProject(dir);
        MediaStore media(dir, /*streaming=*/true);
        audio::AudioEngine engine(static_cast<double>(project.sampleRate));
        EngineCallback callback(engine);

        auto device = makeJuceAudioDevice();
        std::string error;
        if (!device->open(static_cast<double>(project.sampleRate), 256, callback, error)) {
            err << "error: " << error << "\n";
            return 2;
        }
        // the device is running before the host posts the project, so the message queue is always drained
        const std::int64_t end = projectEndFrame(project) + project.sampleRate / 2;
        int underruns = 0;
        {
            ProjectHost host(project, engine, media);
            host.play().get();
            std::signal(SIGINT, onInterrupt);
            out << "playing " << project.name << " (Ctrl+C to stop)\n";
            int tick = 0;
            while (!gInterrupted.load() && engine.positionFrames() < end) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                underruns += media.takeUnderruns();
                if (++tick % 10 == 0) {
                    const double s = static_cast<double>(engine.positionFrames()) / project.sampleRate;
                    out << std::fixed << std::setprecision(1) << "  " << s << " s  peak " << std::setprecision(3) << engine.masterPeak()
                        << "  underruns " << underruns << "\n";
                }
            }
            host.stop().get();
        }
        device->close();
        for (const std::string& w : media.warnings()) err << "warning: " << w << "\n";
        out << "finished, " << underruns << " underrun(s)\n";
        return 0;
    } catch (const std::exception& e) {
        err << "error: " << e.what() << "\n";
        return 2;
    }
}

}  // namespace lpc::cli
```

Replace `tools/lpc-cli/CMakeLists.txt` with the final version:

```cmake
option(LPC_WITH_JUCE "Build the audio device backend and the play command (downloads JUCE, AGPLv3)" ON)

add_library(lpc_cli_lib STATIC cli_commands.cpp)
target_include_directories(lpc_cli_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lpc_cli_lib PUBLIC lpc_core)

if(LPC_WITH_JUCE)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_console_app(lpc-cli PRODUCT_NAME "lpc-cli")
    target_sources(lpc-cli PRIVATE main.cpp juce_device.cpp play_command.cpp)
    target_compile_definitions(lpc-cli PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(lpc-cli
        PRIVATE
            lpc_cli_lib
            juce::juce_audio_devices
            juce::juce_audio_basics
            juce::juce_events
            juce::juce_core
        PUBLIC
            juce::juce_recommended_config_flags)
else()
    add_executable(lpc-cli main.cpp play_unavailable.cpp)
    target_link_libraries(lpc-cli PRIVATE lpc_cli_lib)
endif()
```

- [ ] **Step 5: Build and verify by hand**

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --target lpc-cli
ctest --test-dir build -C Debug --output-on-failure
```

Expected: the build downloads and compiles JUCE (several minutes the first time) and succeeds; the full test suite still passes (the CLI tests run against `lpc_cli_lib`).

Find the executable with `find build -name "lpc-cli.exe"`, then:

```bash
lpc-cli.exe demo demo.lpc
lpc-cli.exe info demo.lpc
lpc-cli.exe render demo.lpc demo.wav
lpc-cli.exe play demo.lpc
```

Expected: `demo` creates the folder; `info` lists 4 tracks (Master, Reverb Bus, Keys, Tone) and one media file; `render` writes `demo.wav` (open it in any player: a C-F-G-C chord progression plus a steady 220 Hz tone); `play` prints `playing Demo`, the same audio comes out of the default output device, a progress line appears every second with `underruns 0`, and it ends by itself after about 4.5 seconds. Pressing Ctrl+C during playback stops it cleanly and prints `finished`.

If the device runs at 44100 Hz, `play` must refuse with the message about the sample rate; set the Windows output format to 48000 Hz and retry. This behaviour is by design (no resampling in Core).

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(cli): audio device interface, JUCE WASAPI backend and play command" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 14: Real-time lint, CI, licence and README

**Files:**
- Create: `tests/check_no_locks.cmake`, `tests/data/bad_core/include/lpc/audio/bad.h`
- Modify: `tests/CMakeLists.txt`
- Create: `.github/workflows/ci.yml`, `LICENSE`, `README.md`

**Interfaces:**
- Consumes: the directory layout of Tasks 7-11 (`core/include/lpc/audio/`, `core/src/audio/`).
- Produces: ctest tests `audio_dirs_have_no_locks` (must pass on the real tree) and `lock_check_detects_violation` (must fail on a fixture that contains a mutex, proving the check works); a CI workflow that builds and tests on Windows; the AGPLv3 licence file.

- [ ] **Step 1: Write the failing check (the fixture first)**

`tests/data/bad_core/include/lpc/audio/bad.h`:

```cpp
#pragma once
#include <mutex>

// Fixture for the lock check: this file must be reported as a violation.
struct Bad {
    std::mutex m;
};
```

Append to `tests/CMakeLists.txt`:

```cmake
add_test(NAME audio_dirs_have_no_locks
    COMMAND ${CMAKE_COMMAND} -DCORE_DIR=${CMAKE_SOURCE_DIR}/core -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_locks.cmake)
add_test(NAME lock_check_detects_violation
    COMMAND ${CMAKE_COMMAND} -DCORE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/data/bad_core -P ${CMAKE_CURRENT_SOURCE_DIR}/check_no_locks.cmake)
set_tests_properties(lock_check_detects_violation PROPERTIES WILL_FAIL TRUE)
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 && ctest --test-dir build -C Debug -R lock --output-on-failure`
Expected: `audio_dirs_have_no_locks` FAILS (the script does not exist yet). `lock_check_detects_violation` passes for the wrong reason (`WILL_FAIL` turns the missing script into a pass), which is why Step 3b also runs the script by hand against the fixture.

- [ ] **Step 3: Implement the check**

`tests/check_no_locks.cmake`:

```cmake
# Fails (exit 1) when code under core/{include,src}/lpc/audio uses blocking or allocating primitives.
# Usage: cmake -DCORE_DIR=<path to core> -P check_no_locks.cmake
if(NOT DEFINED CORE_DIR)
    message(FATAL_ERROR "CORE_DIR is not set")
endif()

file(GLOB_RECURSE AUDIO_FILES
    ${CORE_DIR}/include/lpc/audio/*
    ${CORE_DIR}/src/audio/*)

if(NOT AUDIO_FILES)
    message(FATAL_ERROR "no files found under ${CORE_DIR}/.../audio: wrong CORE_DIR?")
endif()

set(BANNED "std::mutex|std::recursive_mutex|std::shared_mutex|std::timed_mutex|std::lock_guard|std::unique_lock|std::scoped_lock|std::condition_variable|std::future|std::promise|std::function|std::async")

set(VIOLATIONS "")
foreach(file ${AUDIO_FILES})
    file(READ ${file} content)
    # Comments may mention the banned names; strip `// ...` to the end of the line before matching.
    string(REGEX REPLACE "//[^\n]*" "" code "${content}")
    if(code MATCHES "(${BANNED})")
        list(APPEND VIOLATIONS "${file}: uses ${CMAKE_MATCH_1}")
    endif()
endforeach()

if(VIOLATIONS)
    string(REPLACE ";" "\n  " REPORT "${VIOLATIONS}")
    message(FATAL_ERROR "audio-thread code must not use locks or std::function:\n  ${REPORT}")
endif()
message(STATUS "audio directories are free of locks")
```

- [ ] **Step 3b: Run it to verify it passes**

Run: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 && ctest --test-dir build -C Debug -R lock --output-on-failure`
Expected: both tests PASS (`audio_dirs_have_no_locks` succeeds on the real tree; `lock_check_detects_violation` passes because the script fails on the fixture and `WILL_FAIL` inverts it).

Then prove the checker really detects the fixture, not just "fails somehow":

```bash
cmake -DCORE_DIR=tests/data/bad_core -P tests/check_no_locks.cmake
```

Expected: the command exits non-zero and prints `bad.h: uses std::mutex`.

If `audio_dirs_have_no_locks` reports a real violation, it is a defect in the audio code: fix the code, not the check. Comments (from `//` to the end of the line) are ignored by the check.

- [ ] **Step 4: Add the licence, README and CI**

Download the licence text:

```bash
curl -L https://www.gnu.org/licenses/agpl-3.0.txt -o LICENSE
head -n 3 LICENSE
```

Expected: the first lines read `GNU AFFERO GENERAL PUBLIC LICENSE` / `Version 3, 19 November 2007`.

`README.md`:

```markdown
# lpc (working title)

Open source (AGPLv3) digital audio workstation, work in progress. This repository currently contains the
**Core**: project model, undoable JSON commands, real-time audio engine, offline renderer and a CLI.

## Build (Windows, Visual Studio 2022, CMake)

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Debug
    ctest --test-dir build -C Debug --output-on-failure

Use `-DLPC_BUILD_CLI=OFF` to skip the CLI, or `-DLPC_WITH_JUCE=OFF` to build the CLI without audio output
(JUCE is downloaded only when `LPC_WITH_JUCE=ON`).

## CLI

    lpc-cli demo demo.lpc                  # write a small demo project
    lpc-cli info demo.lpc
    lpc-cli render demo.lpc out.wav        # deterministic offline bounce
    lpc-cli play demo.lpc                  # needs a 48 kHz output device

Design: `docs/superpowers/specs/2026-10-07-core-engine-design.md`. Third-party licences: `THIRD_PARTY.md`.
```

`.github/workflows/ci.yml`:

```yaml
name: ci
on: [push, pull_request]
jobs:
  windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake -S . -B build -G "Visual Studio 17 2022" -A x64
      - name: Build
        run: cmake --build build --config Release
      - name: Test
        run: ctest --test-dir build -C Release --output-on-failure
```

(If the repository is hosted on GitLab instead, translate this file to `.gitlab-ci.yml` with a Windows runner; the three commands are the same.)

- [ ] **Step 5: Full verification**

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected: every test passes in both configurations (Debug and Release). A failure only in Release is usually a floating-point difference in the golden test (raise its tolerance only after confirming the difference is below 1e-3 and not a logic error) or an uninitialised value.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "chore: real-time lint, CI workflow, AGPLv3 licence, README" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

