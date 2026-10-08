# UI-B Panels Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Inspector, Library and Smart Controls become real panels (the three stub toggles of UI-A), and the mixer is restyled on the same channel-strip component; every real edit is a JSON command with undo.

**Architecture:** The Core gains eight small commands (patch id, instrument, output, send edit, region gain, three insert edits) and a `PatchLibrary` that loads `patches.json` and builds the transactions that apply a patch or move a Smart Control knob. The UI bridge copies what the panels need into the snapshot, exposes it through an `InspectorModel` and a `LibraryModel`, and forwards edits through controller wrappers. New QML components (`ChannelStrip`, `Inspector`, `Library`, `SmartControls`, `LeftColumn`) read only from the controller and models.

**Tech Stack:** C++20, Catch2 (Core), Qt 6.8 Quick / QtTest / QuickTest (UI), CMake, nlohmann/json. Build dirs: `build` (core + CLI + core tests), `build-ui` (with `-DLPC_BUILD_UI=ON -DCMAKE_PREFIX_PATH=<repo>/.qt/6.8.3/msvc2022_64`).

**Spec:** `docs/superpowers/specs/2026-10-08-ui-b-panels-design.md` (this plan adds `set_output`, `set_send`, `set_region_gain`, `add_insert`, `remove_insert`, `set_insert_param`, drops `send.*` smart-control targets and commits a knob on release like the faders; Task 3 step 1 updates the spec to match).

## Global Constraints

- Core stays free of Qt; `-DLPC_BUILD_UI=OFF` must keep building and passing.
- Every edit reaches the model only through JSON commands (`ProjectController::sendCommand`); QML never builds command JSON by hand.
- Every new command: exact inverse (undo restores a project equal to the original), validation from `lpc/validation.h`, JSON round trip, a case in `tests/random_commands.h` so the undo property test covers it.
- Faders and knobs emit `moved` (visual only) and `released` (one command): keep that, one command per gesture.
- Unimplemented controls: active with visual effect only; the notice "not implemented yet" appears only when a toggle is switched on (a stub command shows it every time).
- UI strings in English with `qsTr`; colours, radii, spacing, fonts only from `Theme` (generated, never edit `ui/qml/Theme.qml`). Icons: original SVGs only (`assets/icons`, `ui/qml/icons`), no Apple assets, not even modified ones, no Apple names.
- `core/src/commands.cpp` and `core/include/lpc/commands.h` are CRLF (also `core/include/lpc/audio/spsc_queue.h`, `tools/lpc-cli/cli_commands.h`, root `CMakeLists.txt`). Edit them only with `tools/crlf_edit.py` (Task 1 step 1). Use the Write tool (not bash heredocs) for new files.
- Windows first; everything must also compile on macOS CI (no Windows-only code outside `#ifdef _WIN32`).
- UI test executables need `<repo>/.qt/6.8.3/msvc2022_64/bin` on `PATH` and `QT_QPA_PLATFORM=offscreen`; QtTest prints nothing unless `-o file.txt,txt` is given.
- Every task that reaches the window ends with a screenshot in `docs/images/` and the README section updated in the same task (spec section 1). Screenshot command (README): `build-ui/ui/Debug/jad-daw.exe --project <demo.lpc> --no-audio --screenshot docs/images/<name>.png --size 1280x800 [options]`; make `demo.lpc` with `lpc-cli demo demo.lpc`.
- Commit messages follow the repo style (`feat(core): ...`, `feat(ui): ...`, `test(...)`, `docs: ...`) and end with the `Co-Authored-By` trailer the harness gives.

## Review Focus

1. A patch applied to a track of another kind, or with an unknown id, is refused and changes nothing; a patch id removed from the catalogue leaves the track playable and the Library with no selection (Task 3, Task 4).
2. Smart Control at min, max, beyond the range, NaN, and a control with several targets: clamped, NaN ignored, one exact undo step (Task 3).
3. `patches.json` missing, empty, not an object, with a duplicate id, an unknown processor or a bad target: valid entries still load, problems are listed with the entry id (Task 3).
4. Selection goes stale while a panel is open (track deleted, undone, project replaced, knob drag in progress): panels fall back to the neutral state, no command is sent for a vanished track (Task 4, Task 8).
5. Routing edits: `set_output` to itself, to a non-bus, forming a cycle; `set_send` on a removed send; insert index out of range; insert parameter out of range (Task 1, Task 2).

## File Structure

Core:
- Create `core/src/command_result.h` (shared `fail` / `success` helpers), `core/src/commands_strip.cpp` (the eight new command classes and their `make*` functions), `core/include/lpc/patch_library.h`, `core/src/patch_library.cpp`, `core/data/patches.json`, `tools/crlf_edit.py`.
- Modify `core/include/lpc/model.h` (`Track.patchId`), `core/src/model_json.cpp`, `core/include/lpc/validation.h`, `core/src/validation.cpp` (`validPatchId`, `checkPatchId`), `core/include/lpc/commands.h` and `core/src/commands.cpp` (CRLF: declarations and JSON dispatch only).
- Tests: `tests/test_commands_strip_edit.cpp` (new), `tests/test_patch_library.cpp` (new), `tests/random_commands.h`, `tests/CMakeLists.txt` (define `LPC_PATCHES_JSON`).

UI:
- Modify `ui/bridge/snapshot.{h,cpp}`, `ui/bridge/project_controller.{h,cpp}`, `ui/CMakeLists.txt`, `ui/main.cpp`, `ui/actions/actions.json`, `ui/qml/Main.qml`, `ui/qml/Mixer.qml`, `ui/qml/ControlBar.qml` (only if it needs the new state), `README.md`.
- Create `ui/bridge/inspector_model.{h,cpp}`, `ui/bridge/library_model.{h,cpp}`.
- Create `ui/qml/`: `ChannelStrip.qml`, `StripSlot.qml`, `PanelHeader.qml`, `LeftColumn.qml`, `Inspector.qml`, `RegionInspector.qml`, `TrackInspector.qml`, `Library.qml`, `SmartControls.qml`, `ScreenKnob.qml`, `Splitter.qml`, icons under `ui/qml/icons/`.
- Create `ui/bridge/row_maps.h` (the strip view of a track as a map for QML).
- Tests: `ui/tests/tst_panel_models.cpp` (new), `ui/tests/tst_models.cpp` and `ui/tests/tst_bridge.cpp` (append), `ui/tests/tst_channelstrip.qml`, `ui/tests/tst_inspector.qml`, `ui/tests/tst_library.qml`, `ui/tests/tst_smartcontrols.qml` (new), `ui/tests/tst_mixer.qml` (adapt).
- Also create `ui/qml/ProjectStrip.qml`, `ui/qml/TrackIcon.qml`, `ui/qml/InspectorRow.qml`, `ui/qml/NumberField.qml`, `ui/qml/StubCheck.qml`, `ui/qml/StubValue.qml`; `ui/qml/MixerStrip.qml` is deleted in Task 9.

---

### Task 1: Core: patch id, instrument and output commands

**Files:**
- Create: `tools/crlf_edit.py`, `core/src/command_result.h`, `core/src/commands_strip.cpp`, `tests/test_commands_strip_edit.cpp`
- Modify: `core/include/lpc/model.h`, `core/src/model_json.cpp`, `core/include/lpc/validation.h`, `core/src/validation.cpp`, `core/include/lpc/commands.h` (CRLF), `core/src/commands.cpp` (CRLF), `tests/random_commands.h`

**Interfaces:**
- Produces (in `lpc/commands.h`):
  `CommandPtr makeSetPatchId(Uuid trackId, std::string patchId);`
  `CommandPtr makeSetInstrument(Uuid trackId, ProcessorRef instrument);`
  `CommandPtr makeSetOutput(Uuid trackId, Uuid output);` (null `output` = master)
- Produces (in `lpc/validation.h`): `bool validPatchId(const std::string&);` (1 to 64 chars of `A-Za-z0-9._-`), `MaybeError checkPatchId(const std::string&);` (empty is valid).
- Produces: `Track::patchId` (`std::string`, empty = none), JSON field `patchId` written only when non-empty.
- JSON: `{"type":"set_patch_id","trackId":..,"patchId":".."}`, `{"type":"set_instrument","trackId":..,"instrument":{processorId,params,state}}`, `{"type":"set_output","trackId":..,"output":"<uuid>"}`.

- [ ] **Step 1: Create the CRLF-safe insert tool**

Write `tools/crlf_edit.py`:

```python
#!/usr/bin/env python3
"""Insert the text of TEXTFILE after the one line of FILE that contains ANCHOR, keeping FILE's line endings.

usage: crlf_edit.py FILE ANCHOR TEXTFILE
"""
import pathlib
import sys

path, anchor, text_path = pathlib.Path(sys.argv[1]), sys.argv[2], pathlib.Path(sys.argv[3])
raw = path.read_bytes().decode("utf-8")
crlf = "\r\n" in raw
lines = raw.replace("\r\n", "\n").split("\n")
hits = [i for i, line in enumerate(lines) if anchor in line]
assert len(hits) == 1, f"{len(hits)} lines contain the anchor {anchor!r}"
new = text_path.read_text(encoding="utf-8").rstrip("\n").split("\n")
lines[hits[0] + 1:hits[0] + 1] = new
out = "\n".join(lines)
path.write_bytes((out.replace("\n", "\r\n") if crlf else out).encode("utf-8"))
```

- [ ] **Step 2: Write the failing tests**

Create `tests/test_commands_strip_edit.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <random>

#include "lpc/commands.h"
#include "lpc/model_json.h"
#include "lpc/processor_ids.h"
#include "lpc/validation.h"

using namespace lpc;

namespace {
std::mt19937_64 gRng(9001);

Track track(TrackKind kind, const char* name) {
    Track t;
    t.id = Uuid::random(gRng);
    t.kind = kind;
    t.name = name;
    if (kind == TrackKind::Instrument) t.instrument = ProcessorRef{kProcSine, {}, ""};
    return t;
}

Project projectWith(std::initializer_list<Track> tracks) {
    Project p{Uuid::random(gRng)};
    for (const Track& t : tracks) REQUIRE(makeAddTrack(t)->apply(p).ok());
    return p;
}

void requireRejected(Project& p, CommandPtr c, const char* code) {
    const Project before = p;
    auto r = c->apply(p);
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.error->code == code);
    REQUIRE(p == before);
}

void requireRoundTrip(const CommandPtr& c) { REQUIRE(commandFromJson(c->toJson())->toJson() == c->toJson()); }

void requireUndo(Project& p, CommandPtr c) {
    const Project before = p;
    auto r = c->apply(p);
    REQUIRE(r.ok());
    REQUIRE_FALSE(p == before);
    REQUIRE(r.inverse->apply(p).ok());
    REQUIRE(p == before);
}
}  // namespace

TEST_CASE("set_patch_id: sets, clears, undo restores", "[commands][patch]") {
    const Track a = track(TrackKind::Audio, "Audio");
    Project p = projectWith({a});
    requireUndo(p, makeSetPatchId(a.id, "audio.clean-vocal"));
    REQUIRE(makeSetPatchId(a.id, "audio.clean-vocal")->apply(p).ok());
    REQUIRE(p.findTrack(a.id)->patchId == "audio.clean-vocal");
    requireUndo(p, makeSetPatchId(a.id, ""));
    requireRoundTrip(makeSetPatchId(a.id, "x.y-1"));
}

TEST_CASE("set_patch_id: bad ids, master and unknown tracks are rejected", "[commands][patch]") {
    const Track a = track(TrackKind::Audio, "Audio");
    Project p = projectWith({a});
    requireRejected(p, makeSetPatchId(a.id, "has space"), "bad_value");
    requireRejected(p, makeSetPatchId(a.id, std::string(65, 'x')), "bad_value");
    requireRejected(p, makeSetPatchId(a.id, "tab\t"), "bad_value");
    requireRejected(p, makeSetPatchId(p.master()->id, "audio.x"), "invalid_kind");
    requireRejected(p, makeSetPatchId(Uuid::random(gRng), "audio.x"), "not_found");
}

TEST_CASE("a project keeps its patch ids through JSON and loads old files without them", "[commands][patch]") {
    const Track a = track(TrackKind::Audio, "Audio");
    Project p = projectWith({a});
    REQUIRE(makeSetPatchId(a.id, "audio.warm")->apply(p).ok());
    nlohmann::json j = toJson(p);
    REQUIRE(projectFromJson(j) == p);
    for (auto& t : j["tracks"]) t.erase("patchId");  // a file written before patch ids existed
    REQUIRE(projectFromJson(j).findTrack(a.id)->patchId.empty());
    j["tracks"][1]["patchId"] = "has space";
    REQUIRE_THROWS(projectFromJson(j));
}

TEST_CASE("set_instrument: swaps the instrument of an instrument track, undo restores", "[commands][instrument]") {
    const Track k = track(TrackKind::Instrument, "Keys");
    Project p = projectWith({k});
    ProcessorRef other{kProcSine, {}, "state"};
    requireUndo(p, makeSetInstrument(k.id, other));
    requireRoundTrip(makeSetInstrument(k.id, other));
}

TEST_CASE("set_instrument: only instrument tracks, only known instruments", "[commands][instrument]") {
    const Track a = track(TrackKind::Audio, "Audio");
    const Track k = track(TrackKind::Instrument, "Keys");
    Project p = projectWith({a, k});
    requireRejected(p, makeSetInstrument(a.id, ProcessorRef{kProcSine, {}, ""}), "invalid_kind");
    requireRejected(p, makeSetInstrument(k.id, ProcessorRef{"vendor.unknown", {}, ""}), "bad_value");
    requireRejected(p, makeSetInstrument(Uuid::random(gRng), ProcessorRef{kProcSine, {}, ""}), "not_found");
}

TEST_CASE("set_output: routes to a bus or back to master, undo restores", "[commands][output]") {
    const Track a = track(TrackKind::Audio, "Audio");
    const Track bus = track(TrackKind::Bus, "Bus");
    Project p = projectWith({a, bus});
    requireUndo(p, makeSetOutput(a.id, bus.id));
    REQUIRE(makeSetOutput(a.id, bus.id)->apply(p).ok());
    requireUndo(p, makeSetOutput(a.id, Uuid{}));  // back to master
    requireRoundTrip(makeSetOutput(a.id, bus.id));
    requireRoundTrip(makeSetOutput(a.id, Uuid{}));
}

TEST_CASE("set_output: self, non-bus, cycles, master, unknown are rejected", "[commands][output]") {
    const Track a = track(TrackKind::Audio, "Audio");
    const Track b1 = track(TrackKind::Bus, "Bus 1");
    const Track b2 = track(TrackKind::Bus, "Bus 2");
    Project p = projectWith({a, b1, b2});
    REQUIRE(makeSetOutput(b1.id, b2.id)->apply(p).ok());
    requireRejected(p, makeSetOutput(b1.id, b1.id), "bad_output");
    requireRejected(p, makeSetOutput(a.id, p.master()->id), "bad_output");  // master is the null output, not a bus
    requireRejected(p, makeSetOutput(b2.id, b1.id), "cycle");               // b1 -> b2 already
    requireRejected(p, makeSetOutput(a.id, Uuid::random(gRng)), "bad_output");
    requireRejected(p, makeSetOutput(p.master()->id, b1.id), "invalid_kind");
    requireRejected(p, makeSetOutput(Uuid::random(gRng), b1.id), "not_found");
}
```

- [ ] **Step 3: Run the build to verify it fails**

Run: `cmake --build build --config Debug --target lpc_tests`
Expected: FAIL to compile (`makeSetPatchId`, `makeSetInstrument`, `makeSetOutput`, `Track::patchId` not declared).

- [ ] **Step 4: Model, JSON and validation**

In `core/include/lpc/model.h`, in `struct Track`, after the `std::optional<ProcessorRef> instrument;` line add:

```cpp
    std::string patchId;  // the built-in patch applied to the track; empty when none
```

In `core/src/model_json.cpp`, in `to_json(Track)` after the `j["instrument"] = ...` line add:

```cpp
    if (!t.patchId.empty()) j["patchId"] = t.patchId;
```

and in `from_json(Track)` after the `t.instrument = ...` line add:

```cpp
    t.patchId = j.value("patchId", std::string());
```

In `core/include/lpc/validation.h` after `bool validTrackName(...)` add:

```cpp
bool validPatchId(const std::string& id);        // 1 to 64 characters of A-Z a-z 0-9 . _ -
MaybeError checkPatchId(const std::string& id);  // "" (no patch) or a valid id
```

In `core/src/validation.cpp` after `checkTrackProps` add:

```cpp
bool validPatchId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (const unsigned char c : id)
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-')) return false;
    return true;
}

MaybeError checkPatchId(const std::string& id) {
    if (!id.empty() && !validPatchId(id)) return CommandError{"bad_value", "a patch id has 1 to 64 characters of letters, digits, '.', '_' or '-'"};
    return std::nullopt;
}
```

add `#include <cctype>` to the includes of `validation.cpp`, and in `checkProject` after `if (auto e = checkTrackProps(t.name, t.color)) return e;` add:

```cpp
        if (auto e = checkPatchId(t.patchId)) return e;
```

- [ ] **Step 5: The shared result helpers and the new commands**

Create `core/src/command_result.h`:

```cpp
#pragma once
#include <string>
#include <utility>

#include "lpc/command.h"

namespace lpc::detail {

inline ApplyResult fail(CommandError e) {
    ApplyResult r;
    r.error = std::move(e);
    return r;
}
inline ApplyResult fail(std::string code, std::string message) { return fail(CommandError{std::move(code), std::move(message)}); }
inline ApplyResult success(CommandPtr inverse) {
    ApplyResult r;
    r.inverse = std::move(inverse);
    return r;
}

}  // namespace lpc::detail
```

Create `core/src/commands_strip.cpp` (LF endings):

```cpp
#include <algorithm>
#include <cmath>
#include <optional>

#include "command_result.h"
#include "lpc/commands.h"
#include "lpc/model_json.h"
#include "lpc/processor_ids.h"
#include "lpc/validation.h"

namespace lpc {

namespace {

using nlohmann::json;
using detail::fail;
using detail::success;

class SetPatchIdCmd final : public Command {
public:
    SetPatchIdCmd(Uuid id, std::string patchId) : id_(id), patchId_(std::move(patchId)) {}
    std::string type() const override { return "set_patch_id"; }
    json toJson() const override { return {{"type", type()}, {"trackId", id_}, {"patchId", patchId_}}; }
    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(id_);
        if (!t) return fail("not_found", "no such track");
        if (t->kind == TrackKind::Master) return fail("invalid_kind", "the master track has no patch");
        if (auto e = checkPatchId(patchId_)) return fail(*e);
        std::string previous = std::move(t->patchId);
        t->patchId = patchId_;
        return success(makeSetPatchId(id_, std::move(previous)));
    }

private:
    Uuid id_;
    std::string patchId_;
};

class SetInstrumentCmd final : public Command {
public:
    SetInstrumentCmd(Uuid id, ProcessorRef instrument) : id_(id), instrument_(std::move(instrument)) {}
    std::string type() const override { return "set_instrument"; }
    json toJson() const override { return {{"type", type()}, {"trackId", id_}, {"instrument", instrument_}}; }
    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(id_);
        if (!t) return fail("not_found", "no such track");
        if (t->kind != TrackKind::Instrument || !t->instrument) return fail("invalid_kind", "only instrument tracks have an instrument");
        if (!isKnownInstrument(instrument_.processorId)) return fail("bad_value", "unknown instrument: " + instrument_.processorId);
        ProcessorRef previous = std::move(*t->instrument);
        t->instrument = instrument_;
        return success(makeSetInstrument(id_, std::move(previous)));
    }

private:
    Uuid id_;
    ProcessorRef instrument_;
};

class SetOutputCmd final : public Command {
public:
    SetOutputCmd(Uuid id, Uuid output) : id_(id), output_(output) {}
    std::string type() const override { return "set_output"; }
    json toJson() const override { return {{"type", type()}, {"trackId", id_}, {"output", output_}}; }
    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(id_);
        if (!t) return fail("not_found", "no such track");
        if (t->kind == TrackKind::Master) return fail("invalid_kind", "the master track has no output");
        if (!output_.isNull()) {
            const Track* out = p.findTrack(output_);
            if (!out || !isBusLike(out->kind) || out->id == id_)
                return fail("bad_output", "output must be another existing bus or aux track (or null for master)");
            if (reaches(p, output_, id_)) return fail("cycle", "this output would create a routing loop");
        }
        const Uuid previous = t->strip.output;
        t->strip.output = output_;
        return success(makeSetOutput(id_, previous));
    }

private:
    Uuid id_;
    Uuid output_;
};

}  // namespace

CommandPtr makeSetPatchId(Uuid trackId, std::string patchId) { return std::make_unique<SetPatchIdCmd>(trackId, std::move(patchId)); }
CommandPtr makeSetInstrument(Uuid trackId, ProcessorRef instrument) { return std::make_unique<SetInstrumentCmd>(trackId, std::move(instrument)); }
CommandPtr makeSetOutput(Uuid trackId, Uuid output) { return std::make_unique<SetOutputCmd>(trackId, output); }

}  // namespace lpc
```

- [ ] **Step 6: Declarations and JSON dispatch (CRLF files)**

Write `scratchpad/decl1.txt` (any temp folder; LF) with:

```cpp
CommandPtr makeSetPatchId(Uuid trackId, std::string patchId);          // "" clears the patch
CommandPtr makeSetInstrument(Uuid trackId, ProcessorRef instrument);   // instrument tracks only
CommandPtr makeSetOutput(Uuid trackId, Uuid output);                   // null output = master
```

and `scratchpad/disp1.txt` with:

```cpp
        if (type == "set_patch_id") return makeSetPatchId(j.at("trackId").get<Uuid>(), j.at("patchId").get<std::string>());
        if (type == "set_instrument") return makeSetInstrument(j.at("trackId").get<Uuid>(), j.at("instrument").get<ProcessorRef>());
        if (type == "set_output") return makeSetOutput(j.at("trackId").get<Uuid>(), j.at("output").get<Uuid>());
```

Run:

```bash
python tools/crlf_edit.py core/include/lpc/commands.h "CommandPtr makeSetInserts(" <scratchpad>/decl1.txt
python tools/crlf_edit.py core/src/commands.cpp 'if (type == "set_inserts")' <scratchpad>/disp1.txt
```

Expected: no output; `git diff --stat` shows both files changed and `file core/src/commands.cpp` still says CRLF.

- [ ] **Step 7: Random command cases**

In `tests/random_commands.h` change `switch (pick(20)) {` to `switch (pick(23)) {` and insert before `default:`:

```cpp
        case 20:  // patch id (sometimes invalid)
            if (nonMaster.empty()) return nullptr;
            return makeSetPatchId(nonMaster[pick(nonMaster.size())]->id,
                                  chance(80) ? "patch." + std::to_string(pick(50)) : (chance(50) ? std::string() : std::string("bad id")));
        case 21:  // instrument on any track (rejected unless it is an instrument track)
            if (nonMaster.empty()) return nullptr;
            return makeSetInstrument(nonMaster[pick(nonMaster.size())]->id,
                                     ProcessorRef{chance(90) ? kProcSine : "vendor.unknown", {}, chance(50) ? "" : "s"});
        case 22: {  // output: a bus, master, or something that cannot be one (cycles and self are rejected naturally)
            if (nonMaster.empty()) return nullptr;
            const Uuid to = chance(30) || busLike.empty() ? Uuid{} : busLike[pick(busLike.size())]->id;
            return makeSetOutput(nonMaster[pick(nonMaster.size())]->id, to);
        }
```

- [ ] **Step 8: Run the tests**

Run: `cmake --build build --config Debug --target lpc_tests` then
`ctest --test-dir build -C Debug -R "set_patch_id|set_instrument|set_output|keeps its patch ids|undo: random" --output-on-failure`
Expected: all PASS (the property test now includes the new cases; if it fails, the failing seed and command are printed).

- [ ] **Step 9: Run the whole Core suite and commit**

Run: `ctest --test-dir build -C Debug --output-on-failure`
Expected: all PASS.

```bash
git add tools/crlf_edit.py core tests
git commit -m "feat(core): set_patch_id, set_instrument and set_output commands"
```

---

### Task 2: Core: send, region gain and insert edit commands

**Files:**
- Modify: `core/src/commands_strip.cpp`, `core/include/lpc/commands.h` (CRLF, via tool), `core/src/commands.cpp` (CRLF, via tool), `tests/random_commands.h`, `tests/test_commands_strip_edit.cpp`

**Interfaces:**
- Consumes: Task 1 helpers (`command_result.h`, `commands_strip.cpp`).
- Produces (in `lpc/commands.h`):
  `struct SendPatch { std::optional<float> levelDb; std::optional<bool> preFader; };`
  `CommandPtr makeSetSend(Uuid sendId, SendPatch patch);`
  `CommandPtr makeSetRegionGain(Uuid regionId, float gainDb);`
  `CommandPtr makeAddInsert(Uuid trackId, ProcessorRef insert, int index = -1);` (index -1 appends)
  `CommandPtr makeRemoveInsert(Uuid trackId, int index);`
  `CommandPtr makeSetInsertParam(Uuid trackId, int index, std::string param, std::optional<double> value);` (nullopt removes the parameter)
- JSON: `{"type":"set_send","sendId":..,"levelDb"?:..,"preFader"?:..}`, `{"type":"set_region_gain","regionId":..,"gainDb":..}`, `{"type":"add_insert","trackId":..,"insert":{..},"index":-1}`, `{"type":"remove_insert","trackId":..,"index":0}`, `{"type":"set_insert_param","trackId":..,"index":0,"param":"gainDb","value":3.0|null}`.
- Error codes: `not_found`, `bad_value`, `bad_index`, `bad_target` (from `checkSendFields`).

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_commands_strip_edit.cpp`:

```cpp
namespace {
Send makeSend(const Uuid& target) { return Send{Uuid::random(gRng), target, -6.0f, false}; }
ProcessorRef gainInsert(double db) { return ProcessorRef{kProcGain, {{"gainDb", db}}, ""}; }
}  // namespace

TEST_CASE("set_send: changes level and pre/post, a patch changes only what it carries, undo restores", "[commands][send]") {
    const Track a = track(TrackKind::Audio, "Audio");
    const Track bus = track(TrackKind::Bus, "Bus");
    Project p = projectWith({a, bus});
    const Send s = makeSend(bus.id);
    REQUIRE(makeAddSend(a.id, s)->apply(p).ok());
    SendPatch level;
    level.levelDb = -12.0f;
    requireUndo(p, makeSetSend(s.id, level));
    REQUIRE(makeSetSend(s.id, level)->apply(p).ok());
    REQUIRE(p.findTrack(a.id)->strip.sends[0].levelDb == -12.0f);
    REQUIRE_FALSE(p.findTrack(a.id)->strip.sends[0].preFader);
    SendPatch pre;
    pre.preFader = true;
    requireUndo(p, makeSetSend(s.id, pre));
    requireRoundTrip(makeSetSend(s.id, level));
    requireRoundTrip(makeSetSend(s.id, pre));
}

TEST_CASE("set_send: out of range, NaN and unknown sends are rejected", "[commands][send]") {
    const Track a = track(TrackKind::Audio, "Audio");
    const Track bus = track(TrackKind::Bus, "Bus");
    Project p = projectWith({a, bus});
    const Send s = makeSend(bus.id);
    REQUIRE(makeAddSend(a.id, s)->apply(p).ok());
    SendPatch tooLoud;
    tooLoud.levelDb = 13.0f;
    requireRejected(p, makeSetSend(s.id, tooLoud), "bad_value");
    SendPatch nan;
    nan.levelDb = std::nanf("");
    requireRejected(p, makeSetSend(s.id, nan), "bad_value");
    requireRejected(p, makeSetSend(Uuid::random(gRng), SendPatch{}), "not_found");
}

TEST_CASE("set_region_gain: sets the gain of one region, undo restores", "[commands][regiongain]") {
    Track k = track(TrackKind::Instrument, "Keys");
    Region r;
    r.id = Uuid::random(gRng);
    r.length = kPPQ * 4;
    k.regions.push_back(r);
    Project p = projectWith({k});
    requireUndo(p, makeSetRegionGain(r.id, -6.0f));
    requireRoundTrip(makeSetRegionGain(r.id, -6.0f));
    requireRejected(p, makeSetRegionGain(r.id, 30.0f), "bad_value");
    requireRejected(p, makeSetRegionGain(r.id, -100.0f), "bad_value");
    requireRejected(p, makeSetRegionGain(r.id, std::nanf("")), "bad_value");
    requireRejected(p, makeSetRegionGain(Uuid::random(gRng), 0.0f), "not_found");
}

TEST_CASE("add_insert and remove_insert: append, insert at an index, remove, exact inverses", "[commands][insert]") {
    const Track a = track(TrackKind::Audio, "Audio");
    Project p = projectWith({a});
    REQUIRE(makeAddInsert(a.id, gainInsert(1.0))->apply(p).ok());
    requireUndo(p, makeAddInsert(a.id, gainInsert(2.0)));              // append
    requireUndo(p, makeAddInsert(a.id, gainInsert(3.0), 0));            // at the front
    requireUndo(p, makeRemoveInsert(a.id, 0));
    REQUIRE(makeAddInsert(a.id, gainInsert(2.0), 0)->apply(p).ok());
    REQUIRE(p.findTrack(a.id)->strip.inserts.size() == 2);
    REQUIRE(p.findTrack(a.id)->strip.inserts[0].params.at("gainDb") == 2.0);
    requireRoundTrip(makeAddInsert(a.id, gainInsert(2.0), 1));
    requireRoundTrip(makeRemoveInsert(a.id, 1));
}

TEST_CASE("add_insert and remove_insert: bad processors, parameters and indexes are rejected", "[commands][insert]") {
    const Track a = track(TrackKind::Audio, "Audio");
    Project p = projectWith({a});
    requireRejected(p, makeAddInsert(a.id, ProcessorRef{"vendor.unknown", {}, ""}), "bad_value");
    requireRejected(p, makeAddInsert(a.id, ProcessorRef{kProcGain, {{"gainDb", 99.0}}, ""}), "bad_value");
    requireRejected(p, makeAddInsert(a.id, gainInsert(0.0), 5), "bad_index");
    requireRejected(p, makeAddInsert(a.id, gainInsert(0.0), -2), "bad_index");
    requireRejected(p, makeRemoveInsert(a.id, 0), "bad_index");
    requireRejected(p, makeAddInsert(Uuid::random(gRng), gainInsert(0.0)), "not_found");
}

TEST_CASE("set_insert_param: sets, adds and removes a parameter, undo is exact", "[commands][insert]") {
    const Track a = track(TrackKind::Audio, "Audio");
    Project p = projectWith({a});
    REQUIRE(makeAddInsert(a.id, ProcessorRef{kProcGain, {}, ""})->apply(p).ok());  // no parameters yet
    requireUndo(p, makeSetInsertParam(a.id, 0, "gainDb", 3.0));                     // inverse removes it again
    REQUIRE(makeSetInsertParam(a.id, 0, "gainDb", 3.0)->apply(p).ok());
    requireUndo(p, makeSetInsertParam(a.id, 0, "gainDb", -2.0));                    // inverse restores 3.0
    requireUndo(p, makeSetInsertParam(a.id, 0, "gainDb", std::nullopt));            // removes, inverse restores
    requireRoundTrip(makeSetInsertParam(a.id, 0, "gainDb", 3.0));
    requireRoundTrip(makeSetInsertParam(a.id, 0, "gainDb", std::nullopt));
}

TEST_CASE("set_insert_param: unknown parameter, out of range, NaN, bad index are rejected", "[commands][insert]") {
    const Track a = track(TrackKind::Audio, "Audio");
    Project p = projectWith({a});
    REQUIRE(makeAddInsert(a.id, gainInsert(0.0))->apply(p).ok());
    requireRejected(p, makeSetInsertParam(a.id, 0, "cutoff", 1.0), "bad_value");
    requireRejected(p, makeSetInsertParam(a.id, 0, "gainDb", 25.0), "bad_value");
    requireRejected(p, makeSetInsertParam(a.id, 0, "gainDb", std::nan("")), "bad_value");
    requireRejected(p, makeSetInsertParam(a.id, 1, "gainDb", 1.0), "bad_index");
    requireRejected(p, makeSetInsertParam(a.id, -1, "gainDb", 1.0), "bad_index");
    requireRejected(p, makeSetInsertParam(Uuid::random(gRng), 0, "gainDb", 1.0), "not_found");
}
```

- [ ] **Step 2: Run the build to verify it fails**

Run: `cmake --build build --config Debug --target lpc_tests`
Expected: FAIL to compile (`SendPatch`, `makeSetSend`, ... not declared).

- [ ] **Step 3: Implement the commands**

In `core/src/commands_strip.cpp`, add inside the anonymous namespace (before its closing `}  // namespace`):

```cpp
class SetSendCmd final : public Command {
public:
    SetSendCmd(Uuid id, SendPatch patch) : id_(id), patch_(patch) {}
    std::string type() const override { return "set_send"; }
    json toJson() const override {
        json j = {{"type", type()}, {"sendId", id_}};
        if (patch_.levelDb) j["levelDb"] = *patch_.levelDb;
        if (patch_.preFader) j["preFader"] = *patch_.preFader;
        return j;
    }
    ApplyResult apply(Project& p) const override {
        for (Track& t : p.tracks) {
            for (Send& s : t.strip.sends) {
                if (s.id != id_) continue;
                Send next = s;
                if (patch_.levelDb) next.levelDb = *patch_.levelDb;
                if (patch_.preFader) next.preFader = *patch_.preFader;
                if (auto e = checkSendFields(p, t.id, next)) return fail(*e);
                SendPatch previous;
                if (patch_.levelDb) previous.levelDb = s.levelDb;
                if (patch_.preFader) previous.preFader = s.preFader;
                s = next;
                return success(makeSetSend(id_, previous));
            }
        }
        return fail("not_found", "no such send");
    }

private:
    Uuid id_;
    SendPatch patch_;
};

class SetRegionGainCmd final : public Command {
public:
    SetRegionGainCmd(Uuid id, float gainDb) : id_(id), gainDb_(gainDb) {}
    std::string type() const override { return "set_region_gain"; }
    json toJson() const override { return {{"type", type()}, {"regionId", id_}, {"gainDb", gainDb_}}; }
    ApplyResult apply(Project& p) const override {
        std::size_t index = 0;
        Track* t = p.findTrackOfRegion(id_, &index);
        if (!t) return fail("not_found", "no such region");
        if (auto e = checkStripValues(gainDb_, 0.0f)) return fail(*e);  // the same -96..24 dB range as a strip
        const float previous = t->regions[index].gainDb;
        t->regions[index].gainDb = gainDb_;
        return success(makeSetRegionGain(id_, previous));
    }

private:
    Uuid id_;
    float gainDb_;
};

class AddInsertCmd final : public Command {
public:
    AddInsertCmd(Uuid trackId, ProcessorRef insert, int index) : trackId_(trackId), insert_(std::move(insert)), index_(index) {}
    std::string type() const override { return "add_insert"; }
    json toJson() const override { return {{"type", type()}, {"trackId", trackId_}, {"insert", insert_}, {"index", index_}}; }
    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(trackId_);
        if (!t) return fail("not_found", "no such track");
        if (auto e = checkInsert(insert_)) return fail(*e);
        auto& chain = t->strip.inserts;
        if (index_ < -1 || index_ > static_cast<int>(chain.size())) return fail("bad_index", "insert index out of range");
        const int at = index_ < 0 ? static_cast<int>(chain.size()) : index_;
        chain.insert(chain.begin() + at, insert_);
        return success(makeRemoveInsert(trackId_, at));
    }

private:
    Uuid trackId_;
    ProcessorRef insert_;
    int index_;
};

class RemoveInsertCmd final : public Command {
public:
    RemoveInsertCmd(Uuid trackId, int index) : trackId_(trackId), index_(index) {}
    std::string type() const override { return "remove_insert"; }
    json toJson() const override { return {{"type", type()}, {"trackId", trackId_}, {"index", index_}}; }
    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(trackId_);
        if (!t) return fail("not_found", "no such track");
        auto& chain = t->strip.inserts;
        if (index_ < 0 || index_ >= static_cast<int>(chain.size())) return fail("bad_index", "insert index out of range");
        ProcessorRef removed = std::move(chain[static_cast<std::size_t>(index_)]);
        chain.erase(chain.begin() + index_);
        return success(makeAddInsert(trackId_, std::move(removed), index_));
    }

private:
    Uuid trackId_;
    int index_;
};

class SetInsertParamCmd final : public Command {
public:
    SetInsertParamCmd(Uuid trackId, int index, std::string param, std::optional<double> value)
        : trackId_(trackId), index_(index), param_(std::move(param)), value_(value) {}
    std::string type() const override { return "set_insert_param"; }
    json toJson() const override {
        return {{"type", type()}, {"trackId", trackId_}, {"index", index_}, {"param", param_},
                {"value", value_ ? json(*value_) : json(nullptr)}};
    }
    ApplyResult apply(Project& p) const override {
        Track* t = p.findTrack(trackId_);
        if (!t) return fail("not_found", "no such track");
        auto& chain = t->strip.inserts;
        if (index_ < 0 || index_ >= static_cast<int>(chain.size())) return fail("bad_index", "insert index out of range");
        ProcessorRef next = chain[static_cast<std::size_t>(index_)];
        std::optional<double> previous;
        if (const auto it = next.params.find(param_); it != next.params.end()) previous = it->second;
        if (value_) next.params[param_] = *value_;
        else next.params.erase(param_);
        if (auto e = checkInsert(next)) return fail(*e);
        chain[static_cast<std::size_t>(index_)] = std::move(next);
        return success(makeSetInsertParam(trackId_, index_, param_, previous));
    }

private:
    Uuid trackId_;
    int index_;
    std::string param_;
    std::optional<double> value_;
};
```

and after the other `make*` definitions at the bottom of the file:

```cpp
CommandPtr makeSetSend(Uuid sendId, SendPatch patch) { return std::make_unique<SetSendCmd>(sendId, patch); }
CommandPtr makeSetRegionGain(Uuid regionId, float gainDb) { return std::make_unique<SetRegionGainCmd>(regionId, gainDb); }
CommandPtr makeAddInsert(Uuid trackId, ProcessorRef insert, int index) { return std::make_unique<AddInsertCmd>(trackId, std::move(insert), index); }
CommandPtr makeRemoveInsert(Uuid trackId, int index) { return std::make_unique<RemoveInsertCmd>(trackId, index); }
CommandPtr makeSetInsertParam(Uuid trackId, int index, std::string param, std::optional<double> value) {
    return std::make_unique<SetInsertParamCmd>(trackId, index, std::move(param), value);
}
```

- [ ] **Step 4: Declarations and dispatch (CRLF files)**

`scratchpad/decl2.txt`:

```cpp
struct SendPatch {
    std::optional<float> levelDb;
    std::optional<bool> preFader;
};
CommandPtr makeSetSend(Uuid sendId, SendPatch patch);  // a patch changes only the fields it carries
CommandPtr makeSetRegionGain(Uuid regionId, float gainDb);
CommandPtr makeAddInsert(Uuid trackId, ProcessorRef insert, int index = -1);  // index -1 appends
CommandPtr makeRemoveInsert(Uuid trackId, int index);
CommandPtr makeSetInsertParam(Uuid trackId, int index, std::string param, std::optional<double> value);  // nullopt removes it
```

`scratchpad/disp2.txt`:

```cpp
        if (type == "set_send") {
            SendPatch patch;
            if (j.contains("levelDb")) patch.levelDb = j["levelDb"].get<float>();
            if (j.contains("preFader")) patch.preFader = j["preFader"].get<bool>();
            return makeSetSend(j.at("sendId").get<Uuid>(), patch);
        }
        if (type == "set_region_gain") return makeSetRegionGain(j.at("regionId").get<Uuid>(), j.at("gainDb").get<float>());
        if (type == "add_insert") return makeAddInsert(j.at("trackId").get<Uuid>(), j.at("insert").get<ProcessorRef>(), j.value("index", -1));
        if (type == "remove_insert") return makeRemoveInsert(j.at("trackId").get<Uuid>(), j.at("index").get<int>());
        if (type == "set_insert_param") {
            const auto& v = j.at("value");
            return makeSetInsertParam(j.at("trackId").get<Uuid>(), j.at("index").get<int>(), j.at("param").get<std::string>(),
                                      v.is_null() ? std::nullopt : std::optional<double>(v.get<double>()));
        }
```

Run:

```bash
python tools/crlf_edit.py core/include/lpc/commands.h "CommandPtr makeSetOutput(" <scratchpad>/decl2.txt
python tools/crlf_edit.py core/src/commands.cpp 'if (type == "set_output")' <scratchpad>/disp2.txt
```

- [ ] **Step 5: Random command cases**

In `tests/random_commands.h` change `switch (pick(23)) {` to `switch (pick(28)) {` and insert before `default:`:

```cpp
        case 23:  // send level / pre-post
            if (withSends.empty()) return nullptr;
            {
                const Track& t = *withSends[pick(withSends.size())];
                SendPatch patch;
                if (chance(70)) patch.levelDb = static_cast<float>(static_cast<int>(pick(30)) - 20);  // sometimes above +12
                if (chance(40)) patch.preFader = chance(50);
                return makeSetSend(t.strip.sends[pick(t.strip.sends.size())].id, patch);
            }
        case 24:  // region gain
            if (withRegions.empty()) return nullptr;
            {
                const Track& t = *withRegions[pick(withRegions.size())];
                return makeSetRegionGain(t.regions[pick(t.regions.size())].id, static_cast<float>(static_cast<int>(pick(40)) - 30));
            }
        case 25: {  // add insert (sometimes with a bad parameter)
            ProcessorRef ins{kProcGain, {}, ""};
            if (chance(80)) ins.params["gainDb"] = static_cast<double>(pick(40)) - 20.0;
            return makeAddInsert(p.tracks[pick(p.tracks.size())].id, std::move(ins), chance(30) ? static_cast<int>(pick(3)) : -1);
        }
        case 26: {  // remove insert
            const Track& t = p.tracks[pick(p.tracks.size())];
            return makeRemoveInsert(t.id, static_cast<int>(pick(3)));
        }
        case 27: {  // insert parameter (sometimes removed, sometimes out of range)
            const Track& t = p.tracks[pick(p.tracks.size())];
            return makeSetInsertParam(t.id, static_cast<int>(pick(3)), chance(90) ? "gainDb" : "cutoff",
                                      chance(20) ? std::optional<double>() : std::optional<double>(static_cast<double>(pick(40)) - 20.0));
        }
```

Add `#include <optional>` to the includes of `random_commands.h`.

- [ ] **Step 6: Run the tests**

Run: `cmake --build build --config Debug --target lpc_tests` then
`ctest --test-dir build -C Debug -R "set_send|set_region_gain|add_insert|set_insert_param|undo: random" --output-on-failure`
Expected: all PASS.

- [ ] **Step 7: Whole suite and commit**

Run: `ctest --test-dir build -C Debug --output-on-failure` — Expected: all PASS.

```bash
git add core tests
git commit -m "feat(core): set_send, set_region_gain and insert edit commands"
```

---

### Task 3: Core: PatchLibrary and the built-in catalogue

**Files:**
- Create: `core/include/lpc/patch_library.h`, `core/src/patch_library.cpp`, `core/data/patches.json`, `tests/test_patch_library.cpp`
- Modify: `tests/CMakeLists.txt`, `docs/superpowers/specs/2026-10-08-ui-b-panels-design.md`

**Interfaces:**
- Consumes: `makeTransaction`, `makeSetPatchId`, `makeSetInstrument`, `makeSetInserts`, `makeSetStrip`, `makeSetInsertParam` (Tasks 1, 2 and existing), `checkInsert`, `checkStripValues`, `validPatchId`.
- Produces (`lpc/patch_library.h`):

```cpp
namespace lpc {
struct SmartTarget {
    enum class Kind { StripGain, StripPan, InsertParam };
    Kind kind = Kind::StripGain;
    std::size_t index = 0;   // insert index (InsertParam)
    std::string param;       // insert parameter name (InsertParam)
    std::string path;        // as written in the file
    double from = 0, to = 1; // the control range maps linearly onto from..to (either direction)
};
struct SmartControl {
    std::string id, label, group;
    double min = 0, max = 1, def = 0;
    std::vector<SmartTarget> targets;
};
struct Patch {
    std::string id, category, name;
    TrackKind kind = TrackKind::Audio;     // Audio, Instrument, Aux or Bus
    std::optional<ProcessorRef> instrument;
    float gainDb = 0.0f, pan = 0.0f;
    std::vector<ProcessorRef> inserts;
    std::vector<SmartControl> smartControls;
};
struct PatchProblem { std::string where; std::string message; };  // where: the entry id, "#<index>" or ""

class PatchLibrary {
public:
    static PatchLibrary fromJson(const nlohmann::json& doc);                 // never throws; bad entries are skipped
    static PatchLibrary fromFile(const std::filesystem::path& file);          // a missing or unreadable file gives a problem
    const std::vector<Patch>& patches() const;
    const std::vector<PatchProblem>& problems() const;
    const Patch* find(const std::string& id) const;
    std::vector<std::string> categories(TrackKind kind) const;                // sorted
    std::vector<const Patch*> inCategory(TrackKind kind, const std::string& category) const;  // file order
    CommandPtr applyCommand(const Uuid& trackId, TrackKind trackKind, const std::string& patchId) const;  // nullptr: unknown or wrong kind
    static CommandPtr smartControlCommand(const Uuid& trackId, const SmartControl& control, double value);  // nullptr: NaN
    static std::optional<double> smartControlValue(const Track& track, const SmartControl& control);       // from the first target
};
}
```

- [ ] **Step 1: Update the spec to what the plan builds**

In `docs/superpowers/specs/2026-10-08-ui-b-panels-design.md` make these edits (Edit tool, LF file):
- Section 5 grammar sentence: replace "`path` grammar: `strip.gainDb`, `strip.pan`, `insert.<index>.<param>`, `send.<index>.levelDb`." with "`path` grammar: `strip.gainDb`, `strip.pan`, `insert.<index>.<param>` (the index refers to the inserts of the patch itself)."
- Section 6: replace the bullet "`set_inserts`, `set_strip`, `add_send`, `remove_send`, `set_track_props` already exist. ..." with: "New: `set_output {trackId, output|null}` (bus or aux, no self, no cycle; null is master), `set_send {sendId, levelDb?, preFader?}`, `set_region_gain {regionId, gainDb}`, `add_insert {trackId, insert, index}`, `remove_insert {trackId, index}`, `set_insert_param {trackId, index, param, value|null}`; each with exact inverse and tests. `set_inserts`, `set_strip`, `add_send`, `remove_send`, `set_track_props` already exist."
- Section 6, the "Smart Control move" bullet: replace "transaction of `set_strip` / `set_inserts` / send level commands for the targets. The controller coalesces a drag into one undo step on release (same mechanism as the faders)." with "transaction of `set_strip` / `set_insert_param` commands for the targets, sent when the knob is released (as the faders do): one command per gesture."
- Section 7: replace "`beginSmartControlDrag` / `endSmartControlDrag`, `setInstrument`, `addInsert`, `removeInsert`, `setInsertParam`, `addSend`, `removeSend`, `setSendLevel`, `setOutput`" with "`addInsert`, `removeInsert`, `setInsertParam`, `addSend`, `removeSend`, `setSendLevel`, `setSendPreFader`, `setOutput`, `setRegionGain`" and "Panel visibility and sizes persist in the existing UI settings file." with "Panel visibility and sizes persist with `QSettings` (read and written by `main.cpp`, so tests never touch them)."
- Section 9: remove the words "a drag is one undo step" from the Bridge bullet and put "a knob release is one undo step".

- [ ] **Step 2: Write the failing tests**

Create `tests/test_patch_library.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>

#include "lpc/commands.h"
#include "lpc/model_json.h"
#include "lpc/patch_library.h"
#include "lpc/processor_ids.h"

using namespace lpc;
using nlohmann::json;

namespace {
std::mt19937_64 gRng(777);

Track track(TrackKind kind, const char* name) {
    Track t;
    t.id = Uuid::random(gRng);
    t.kind = kind;
    t.name = name;
    if (kind == TrackKind::Instrument) t.instrument = ProcessorRef{kProcSine, {}, ""};
    return t;
}

const char* kDoc = R"({"patches":[
 {"id":"audio.a","category":"01 Clean","name":"Clean","kind":"audio","instrument":null,
  "strip":{"gainDb":-3.0,"pan":0.25},
  "inserts":[{"processorId":"builtin.gain","params":{"gainDb":1.0}}],
  "smartControls":[
   {"id":"level","label":"Level","group":"Main","min":-24,"max":6,"default":-3,"targets":[{"path":"strip.gainDb","from":-24,"to":6}]},
   {"id":"boost","label":"Boost","group":"Tone","min":0,"max":1,"default":0.1,
    "targets":[{"path":"insert.0.gainDb","from":0,"to":12},{"path":"strip.gainDb","from":-2,"to":-5}]}]},
 {"id":"inst.a","category":"01 Leads","name":"Lead","kind":"instrument","instrument":{"processorId":"builtin.sine"},
  "strip":{"gainDb":-6.0,"pan":0.0},"inserts":[],"smartControls":[]},
 {"id":"bus.a","category":"01 Returns","name":"Return","kind":"bus","instrument":null,"strip":{"gainDb":-6.0},"inserts":[],"smartControls":[]}
]})";
}  // namespace

TEST_CASE("PatchLibrary: the shipped catalogue loads without problems", "[patches]") {
    const PatchLibrary lib = PatchLibrary::fromFile(LPC_PATCHES_JSON);
    for (const auto& p : lib.problems()) FAIL(p.where + ": " + p.message);
    REQUIRE(lib.patches().size() >= 8);
    for (const TrackKind k : {TrackKind::Audio, TrackKind::Instrument, TrackKind::Bus, TrackKind::Aux}) {
        CAPTURE(static_cast<int>(k));
        REQUIRE_FALSE(lib.categories(k).empty());
    }
}

TEST_CASE("PatchLibrary: lists categories and patches by kind", "[patches]") {
    const PatchLibrary lib = PatchLibrary::fromJson(json::parse(kDoc));
    REQUIRE(lib.problems().empty());
    REQUIRE(lib.patches().size() == 3);
    REQUIRE(lib.categories(TrackKind::Audio) == std::vector<std::string>{"01 Clean"});
    REQUIRE(lib.categories(TrackKind::Midi).empty());
    REQUIRE(lib.inCategory(TrackKind::Audio, "01 Clean").size() == 1);
    REQUIRE(lib.inCategory(TrackKind::Audio, "01 Leads").empty());
    REQUIRE(lib.find("inst.a")->instrument->processorId == "builtin.sine");
    REQUIRE(lib.find("nope") == nullptr);
}

TEST_CASE("PatchLibrary: bad entries are skipped and reported, the rest loads", "[patches]") {
    json doc = json::parse(kDoc);
    auto bad = [&](auto&& edit) {
        json entry = doc["patches"][0];
        entry["id"] = "bad";
        edit(entry);
        return entry;
    };
    json patches = doc["patches"];
    patches.push_back(doc["patches"][0]);                                                               // duplicate id
    patches.push_back(bad([](json& e) { e["kind"] = "midi"; }));                                       // unsupported kind
    patches.push_back(bad([](json& e) { e["id"] = "bad id"; }));                                       // invalid id
    patches.push_back(bad([](json& e) { e["inserts"][0]["processorId"] = "vendor.x"; }));              // unknown processor
    patches.push_back(bad([](json& e) { e["strip"]["gainDb"] = 99; }));                                // out of range
    patches.push_back(bad([](json& e) { e["smartControls"][0]["targets"][0]["path"] = "strip.volume"; }));
    patches.push_back(bad([](json& e) { e["smartControls"][1]["targets"][0]["path"] = "insert.3.gainDb"; }));  // no such insert
    patches.push_back(bad([](json& e) { e["smartControls"][0]["min"] = 6; e["smartControls"][0]["max"] = 6; }));
    patches.push_back(bad([](json& e) { e["smartControls"][0]["default"] = 50; }));
    patches.push_back(bad([](json& e) { e["smartControls"][0]["targets"] = json::array(); }));
    patches.push_back(bad([](json& e) { e["instrument"] = json{{"processorId", "builtin.sine"}}; }));  // audio with an instrument
    patches.push_back(json::array());                                                                   // not an object
    doc["patches"] = patches;
    const PatchLibrary lib = PatchLibrary::fromJson(doc);
    REQUIRE(lib.patches().size() == 3);
    REQUIRE(lib.problems().size() == patches.size() - 3);
}

TEST_CASE("PatchLibrary: a missing file, garbage and a wrong top level give a problem and an empty catalogue", "[patches]") {
    REQUIRE(PatchLibrary::fromFile("does/not/exist.json").problems().size() == 1);
    REQUIRE(PatchLibrary::fromJson(json::parse("[]")).problems().size() == 1);
    REQUIRE(PatchLibrary::fromJson(json::parse(R"({"patches":5})")).problems().size() == 1);
    REQUIRE(PatchLibrary::fromJson(json::parse("{}")).patches().empty());
}

TEST_CASE("PatchLibrary: applying a patch is one exact undo step", "[patches]") {
    const PatchLibrary lib = PatchLibrary::fromJson(json::parse(kDoc));
    const Track a = track(TrackKind::Audio, "Audio");
    const Track k = track(TrackKind::Instrument, "Keys");
    Project p{Uuid::random(gRng)};
    REQUIRE(makeAddTrack(a)->apply(p).ok());
    REQUIRE(makeAddTrack(k)->apply(p).ok());
    const Project before = p;
    auto r = lib.applyCommand(a.id, TrackKind::Audio, "audio.a")->apply(p);
    REQUIRE(r.ok());
    const Track* t = p.findTrack(a.id);
    REQUIRE(t->patchId == "audio.a");
    REQUIRE(t->strip.gainDb == -3.0f);
    REQUIRE(t->strip.pan == 0.25f);
    REQUIRE(t->strip.inserts.size() == 1);
    REQUIRE(t->strip.inserts[0].params.at("gainDb") == 1.0);
    REQUIRE(r.inverse->apply(p).ok());
    REQUIRE(p == before);
    REQUIRE(lib.applyCommand(k.id, TrackKind::Instrument, "inst.a")->apply(p).ok());
    REQUIRE(p.findTrack(k.id)->instrument->processorId == "builtin.sine");
}

TEST_CASE("PatchLibrary: a patch of another kind or an unknown id gives no command", "[patches]") {
    const PatchLibrary lib = PatchLibrary::fromJson(json::parse(kDoc));
    const Uuid id = Uuid::random(gRng);
    REQUIRE(lib.applyCommand(id, TrackKind::Instrument, "audio.a") == nullptr);
    REQUIRE(lib.applyCommand(id, TrackKind::Audio, "inst.a") == nullptr);
    REQUIRE(lib.applyCommand(id, TrackKind::Audio, "gone.patch") == nullptr);
    REQUIRE(lib.applyCommand(id, TrackKind::Master, "audio.a") == nullptr);
}

TEST_CASE("PatchLibrary: a Smart Control moves every target, clamps, and undoes exactly", "[patches][smart]") {
    const PatchLibrary lib = PatchLibrary::fromJson(json::parse(kDoc));
    const Track a = track(TrackKind::Audio, "Audio");
    Project p{Uuid::random(gRng)};
    REQUIRE(makeAddTrack(a)->apply(p).ok());
    REQUIRE(lib.applyCommand(a.id, TrackKind::Audio, "audio.a")->apply(p).ok());
    const SmartControl& level = lib.find("audio.a")->smartControls[0];
    const SmartControl& boost = lib.find("audio.a")->smartControls[1];

    Project before = p;
    auto r = PatchLibrary::smartControlCommand(a.id, level, 6.0)->apply(p);
    REQUIRE(r.ok());
    REQUIRE(p.findTrack(a.id)->strip.gainDb == 6.0f);
    REQUIRE(r.inverse->apply(p).ok());
    REQUIRE(p == before);

    REQUIRE(PatchLibrary::smartControlCommand(a.id, level, 1000.0)->apply(p).ok());  // clamped to the range
    REQUIRE(p.findTrack(a.id)->strip.gainDb == 6.0f);
    REQUIRE(PatchLibrary::smartControlCommand(a.id, level, -1000.0)->apply(p).ok());
    REQUIRE(p.findTrack(a.id)->strip.gainDb == -24.0f);
    REQUIRE(PatchLibrary::smartControlCommand(a.id, level, std::nan("")) == nullptr);

    before = p;
    r = PatchLibrary::smartControlCommand(a.id, boost, 1.0)->apply(p);  // two targets at once
    REQUIRE(r.ok());
    REQUIRE(p.findTrack(a.id)->strip.inserts[0].params.at("gainDb") == 12.0);
    REQUIRE(p.findTrack(a.id)->strip.gainDb == -5.0f);
    REQUIRE(r.inverse->apply(p).ok());
    REQUIRE(p == before);
}

TEST_CASE("PatchLibrary: a Smart Control reads its value back from the track", "[patches][smart]") {
    const PatchLibrary lib = PatchLibrary::fromJson(json::parse(kDoc));
    Track a = track(TrackKind::Audio, "Audio");
    a.strip.gainDb = -9.0f;  // -24..6 is 50% at -9
    a.strip.inserts.push_back(ProcessorRef{kProcGain, {{"gainDb", 6.0}}, ""});
    const SmartControl& level = lib.find("audio.a")->smartControls[0];
    const SmartControl& boost = lib.find("audio.a")->smartControls[1];
    REQUIRE(*PatchLibrary::smartControlValue(a, level) == -9.0);
    REQUIRE(*PatchLibrary::smartControlValue(a, boost) == 0.5);  // insert gain 6 of 0..12
    a.strip.inserts.clear();
    REQUIRE_FALSE(PatchLibrary::smartControlValue(a, boost).has_value());  // the insert it drives is gone
    a.strip.gainDb = 24.0f;                                                // outside the control range: clamped
    REQUIRE(*PatchLibrary::smartControlValue(a, level) == 6.0);
}
```

- [ ] **Step 3: Define `LPC_PATCHES_JSON` for the tests**

In `tests/CMakeLists.txt`, after the `target_compile_definitions(lpc_tests PRIVATE LPC_TEST_DIR=...)` line add:

```cmake
target_compile_definitions(lpc_tests PRIVATE LPC_PATCHES_JSON="${CMAKE_SOURCE_DIR}/core/data/patches.json")
```

- [ ] **Step 4: Run to verify it fails**

Run: `cmake --build build --config Debug --target lpc_tests`
Expected: FAIL to compile (`lpc/patch_library.h` not found).

- [ ] **Step 5: Write the header**

Create `core/include/lpc/patch_library.h` with exactly the declarations of the **Interfaces** block above, preceded by:

```cpp
#pragma once
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "lpc/command.h"
#include "lpc/model.h"
```

with `PatchLibrary` private members `std::vector<Patch> patches_; std::vector<PatchProblem> problems_;`.

- [ ] **Step 6: Write the implementation**

Create `core/src/patch_library.cpp`:

```cpp
#include "lpc/patch_library.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>

#include "lpc/commands.h"
#include "lpc/model_json.h"
#include "lpc/processor_ids.h"
#include "lpc/validation.h"

namespace lpc {

namespace {

using nlohmann::json;

[[noreturn]] void bad(const std::string& message) { throw std::runtime_error(message); }

std::string requireString(const json& j, const char* key, std::size_t maxLength) {
    if (!j.contains(key) || !j[key].is_string()) bad(std::string("\"") + key + "\" must be a string");
    const std::string s = j[key].get<std::string>();
    if (s.empty() || s.size() > maxLength) bad(std::string("\"") + key + "\" must have 1 to " + std::to_string(maxLength) + " characters");
    return s;
}

double requireNumber(const json& j, const char* key, std::optional<double> fallback = std::nullopt) {
    if (!j.contains(key)) {
        if (fallback) return *fallback;
        bad(std::string("\"") + key + "\" is missing");
    }
    if (!j[key].is_number()) bad(std::string("\"") + key + "\" must be a number");
    const double v = j[key].get<double>();
    if (!std::isfinite(v)) bad(std::string("\"") + key + "\" must be finite");
    return v;
}

TrackKind kindFromString(const std::string& s) {
    if (s == "audio") return TrackKind::Audio;
    if (s == "instrument") return TrackKind::Instrument;
    if (s == "aux") return TrackKind::Aux;
    if (s == "bus") return TrackKind::Bus;
    bad("\"kind\" must be audio, instrument, aux or bus");
}

ProcessorRef parseProcessor(const json& j) {
    if (!j.is_object()) bad("a processor must be an object");
    ProcessorRef ref;
    ref.processorId = requireString(j, "processorId", 64);
    if (j.contains("params")) {
        if (!j["params"].is_object()) bad("\"params\" must be an object");
        for (const auto& [name, value] : j["params"].items()) {
            if (!value.is_number()) bad("parameter " + name + " must be a number");
            ref.params[name] = value.get<double>();
        }
    }
    return ref;
}

SmartTarget parseTarget(const json& j, const std::vector<ProcessorRef>& inserts) {
    if (!j.is_object()) bad("a target must be an object");
    SmartTarget t;
    t.path = requireString(j, "path", 64);
    t.from = requireNumber(j, "from");
    t.to = requireNumber(j, "to");
    if (t.from == t.to) bad("target " + t.path + ": from and to must differ");
    if (t.path == "strip.gainDb") {
        t.kind = SmartTarget::Kind::StripGain;
        if (auto e = checkStripValues(static_cast<float>(t.from), 0.0f)) bad("target " + t.path + ": " + e->message);
        if (auto e = checkStripValues(static_cast<float>(t.to), 0.0f)) bad("target " + t.path + ": " + e->message);
    } else if (t.path == "strip.pan") {
        t.kind = SmartTarget::Kind::StripPan;
        if (auto e = checkStripValues(0.0f, static_cast<float>(t.from))) bad("target " + t.path + ": " + e->message);
        if (auto e = checkStripValues(0.0f, static_cast<float>(t.to))) bad("target " + t.path + ": " + e->message);
    } else if (t.path.rfind("insert.", 0) == 0) {
        const std::size_t dot = t.path.find('.', 7);
        if (dot == std::string::npos) bad("target " + t.path + ": expected insert.<index>.<param>");
        const std::string indexText = t.path.substr(7, dot - 7);
        if (indexText.empty() || indexText.size() > 3 || !std::all_of(indexText.begin(), indexText.end(), [](unsigned char c) { return std::isdigit(c); }))
            bad("target " + t.path + ": bad insert index");
        t.kind = SmartTarget::Kind::InsertParam;
        t.index = static_cast<std::size_t>(std::stoul(indexText));
        t.param = t.path.substr(dot + 1);
        if (t.index >= inserts.size()) bad("target " + t.path + ": the patch has no such insert");
        for (const double v : {t.from, t.to}) {
            ProcessorRef probe = inserts[t.index];
            probe.params[t.param] = v;
            if (auto e = checkInsert(probe)) bad("target " + t.path + ": " + e->message);
        }
    } else {
        bad("target path " + t.path + " is not strip.gainDb, strip.pan or insert.<index>.<param>");
    }
    return t;
}

SmartControl parseControl(const json& j, const std::vector<ProcessorRef>& inserts) {
    if (!j.is_object()) bad("a smart control must be an object");
    SmartControl c;
    c.id = requireString(j, "id", 32);
    c.label = requireString(j, "label", 32);
    c.group = j.contains("group") ? requireString(j, "group", 32) : std::string();
    c.min = requireNumber(j, "min");
    c.max = requireNumber(j, "max");
    c.def = requireNumber(j, "default");
    if (!(c.min < c.max)) bad("smart control " + c.id + ": min must be below max");
    if (c.def < c.min || c.def > c.max) bad("smart control " + c.id + ": default is outside min..max");
    if (!j.contains("targets") || !j["targets"].is_array() || j["targets"].empty()) bad("smart control " + c.id + ": needs at least one target");
    for (const json& t : j["targets"]) c.targets.push_back(parseTarget(t, inserts));
    return c;
}

Patch parsePatch(const json& j) {
    if (!j.is_object()) bad("a patch must be an object");
    Patch p;
    p.id = requireString(j, "id", 64);
    if (!validPatchId(p.id)) bad("the id may only contain letters, digits, '.', '_' and '-'");
    p.category = requireString(j, "category", 64);
    p.name = requireString(j, "name", 64);
    p.kind = kindFromString(requireString(j, "kind", 16));
    const bool hasInstrument = j.contains("instrument") && !j["instrument"].is_null();
    if (p.kind == TrackKind::Instrument) {
        if (!hasInstrument) bad("an instrument patch needs an instrument");
        p.instrument = parseProcessor(j["instrument"]);
        if (!isKnownInstrument(p.instrument->processorId)) bad("unknown instrument: " + p.instrument->processorId);
    } else if (hasInstrument) {
        bad("only instrument patches have an instrument");
    }
    if (j.contains("strip")) {
        if (!j["strip"].is_object()) bad("\"strip\" must be an object");
        p.gainDb = static_cast<float>(requireNumber(j["strip"], "gainDb", 0.0));
        p.pan = static_cast<float>(requireNumber(j["strip"], "pan", 0.0));
        if (auto e = checkStripValues(p.gainDb, p.pan)) bad(e->message);
    }
    if (j.contains("inserts")) {
        if (!j["inserts"].is_array()) bad("\"inserts\" must be an array");
        for (const json& i : j["inserts"]) {
            p.inserts.push_back(parseProcessor(i));
            if (auto e = checkInsert(p.inserts.back())) bad(e->message);
        }
    }
    if (j.contains("smartControls")) {
        if (!j["smartControls"].is_array()) bad("\"smartControls\" must be an array");
        std::set<std::string> ids;
        for (const json& c : j["smartControls"]) {
            p.smartControls.push_back(parseControl(c, p.inserts));
            if (!ids.insert(p.smartControls.back().id).second) bad("smart control id " + p.smartControls.back().id + " used twice");
        }
    }
    return p;
}

double mapRange(const SmartControl& c, double value, const SmartTarget& t) {
    const double v = std::clamp(value, c.min, c.max);
    return t.from + (v - c.min) / (c.max - c.min) * (t.to - t.from);
}

}  // namespace

PatchLibrary PatchLibrary::fromJson(const json& doc) {
    PatchLibrary lib;
    if (!doc.is_object() || !doc.contains("patches") || !doc["patches"].is_array()) {
        lib.problems_.push_back({"", "the catalogue must be an object with a \"patches\" array"});
        return lib;
    }
    std::set<std::string> seen;
    std::size_t index = 0;
    for (const json& entry : doc["patches"]) {
        std::string where = "#" + std::to_string(index++);
        if (entry.is_object() && entry.contains("id") && entry["id"].is_string()) where = entry["id"].get<std::string>();
        try {
            Patch p = parsePatch(entry);
            if (!seen.insert(p.id).second) bad("duplicate patch id");
            lib.patches_.push_back(std::move(p));
        } catch (const std::exception& e) {
            lib.problems_.push_back({where, e.what()});
        }
    }
    return lib;
}

PatchLibrary PatchLibrary::fromFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        PatchLibrary lib;
        lib.problems_.push_back({file.string(), "cannot open the patch catalogue"});
        return lib;
    }
    try {
        return fromJson(json::parse(in));
    } catch (const std::exception& e) {
        PatchLibrary lib;
        lib.problems_.push_back({file.string(), std::string("not valid JSON: ") + e.what()});
        return lib;
    }
}

const std::vector<Patch>& PatchLibrary::patches() const { return patches_; }
const std::vector<PatchProblem>& PatchLibrary::problems() const { return problems_; }

const Patch* PatchLibrary::find(const std::string& id) const {
    const auto it = std::find_if(patches_.begin(), patches_.end(), [&](const Patch& p) { return p.id == id; });
    return it == patches_.end() ? nullptr : &*it;
}

std::vector<std::string> PatchLibrary::categories(TrackKind kind) const {
    std::set<std::string> out;
    for (const Patch& p : patches_)
        if (p.kind == kind) out.insert(p.category);
    return {out.begin(), out.end()};
}

std::vector<const Patch*> PatchLibrary::inCategory(TrackKind kind, const std::string& category) const {
    std::vector<const Patch*> out;
    for (const Patch& p : patches_)
        if (p.kind == kind && p.category == category) out.push_back(&p);
    return out;
}

CommandPtr PatchLibrary::applyCommand(const Uuid& trackId, TrackKind trackKind, const std::string& patchId) const {
    const Patch* p = find(patchId);
    if (!p || p->kind != trackKind) return nullptr;
    std::vector<CommandPtr> steps;
    steps.push_back(makeSetPatchId(trackId, p->id));
    if (p->instrument) steps.push_back(makeSetInstrument(trackId, *p->instrument));
    steps.push_back(makeSetInserts(trackId, p->inserts));
    StripPatch strip;
    strip.gainDb = p->gainDb;
    strip.pan = p->pan;
    steps.push_back(makeSetStrip(trackId, strip));
    return makeTransaction(std::move(steps));
}

CommandPtr PatchLibrary::smartControlCommand(const Uuid& trackId, const SmartControl& control, double value) {
    if (!std::isfinite(value)) return nullptr;
    StripPatch strip;
    std::vector<CommandPtr> steps;
    for (const SmartTarget& t : control.targets) {
        const double v = mapRange(control, value, t);
        switch (t.kind) {
            case SmartTarget::Kind::StripGain: strip.gainDb = static_cast<float>(v); break;
            case SmartTarget::Kind::StripPan: strip.pan = static_cast<float>(v); break;
            case SmartTarget::Kind::InsertParam:
                steps.push_back(makeSetInsertParam(trackId, static_cast<int>(t.index), t.param, v));
                break;
        }
    }
    if (strip.gainDb || strip.pan) steps.insert(steps.begin(), makeSetStrip(trackId, strip));
    return makeTransaction(std::move(steps));
}

std::optional<double> PatchLibrary::smartControlValue(const Track& track, const SmartControl& control) {
    if (control.targets.empty()) return std::nullopt;
    const SmartTarget& t = control.targets.front();
    double current = 0.0;
    switch (t.kind) {
        case SmartTarget::Kind::StripGain: current = track.strip.gainDb; break;
        case SmartTarget::Kind::StripPan: current = track.strip.pan; break;
        case SmartTarget::Kind::InsertParam: {
            if (t.index >= track.strip.inserts.size()) return std::nullopt;
            const auto& params = track.strip.inserts[t.index].params;
            const auto it = params.find(t.param);
            current = it == params.end() ? 0.0 : it->second;
            break;
        }
    }
    const double fraction = (current - t.from) / (t.to - t.from);
    return std::clamp(control.min + fraction * (control.max - control.min), control.min, control.max);
}

}  // namespace lpc
```

Add `#include <cctype>` to the includes.

- [ ] **Step 7: Write the catalogue**

Create `core/data/patches.json` (LF). One entry per line group; `strip` values and controls as below; every patch has a `Level` and a `Pan` control (group `Main`), and the ones with an insert also a `Boost` control (group `Tone`, `insert.0.gainDb` 0 to 12):

```json
{
  "patches": [
    { "id": "audio.clean-vocal", "category": "01 Vocals", "name": "Clean Vocal", "kind": "audio", "instrument": null,
      "strip": { "gainDb": -3.0, "pan": 0.0 }, "inserts": [],
      "smartControls": [
        { "id": "level", "label": "Level", "group": "Main", "min": -24, "max": 6, "default": -3, "targets": [ { "path": "strip.gainDb", "from": -24, "to": 6 } ] },
        { "id": "pan", "label": "Pan", "group": "Main", "min": -1, "max": 1, "default": 0, "targets": [ { "path": "strip.pan", "from": -1, "to": 1 } ] } ] },
    { "id": "audio.bright-vocal", "category": "01 Vocals", "name": "Bright Vocal", "kind": "audio", "instrument": null,
      "strip": { "gainDb": -2.0, "pan": 0.0 }, "inserts": [ { "processorId": "builtin.gain", "params": { "gainDb": 3.0 } } ],
      "smartControls": [
        { "id": "level", "label": "Level", "group": "Main", "min": -24, "max": 6, "default": -2, "targets": [ { "path": "strip.gainDb", "from": -24, "to": 6 } ] },
        { "id": "pan", "label": "Pan", "group": "Main", "min": -1, "max": 1, "default": 0, "targets": [ { "path": "strip.pan", "from": -1, "to": 1 } ] },
        { "id": "boost", "label": "Boost", "group": "Tone", "min": 0, "max": 1, "default": 0.25,
          "targets": [ { "path": "insert.0.gainDb", "from": 0, "to": 12 }, { "path": "strip.gainDb", "from": -2, "to": -5 } ] } ] },
    { "id": "audio.warm-guitar", "category": "02 Guitars", "name": "Warm Guitar", "kind": "audio", "instrument": null,
      "strip": { "gainDb": -4.0, "pan": -0.2 }, "inserts": [],
      "smartControls": [
        { "id": "level", "label": "Level", "group": "Main", "min": -24, "max": 6, "default": -4, "targets": [ { "path": "strip.gainDb", "from": -24, "to": 6 } ] },
        { "id": "pan", "label": "Pan", "group": "Main", "min": -1, "max": 1, "default": -0.2, "targets": [ { "path": "strip.pan", "from": -1, "to": 1 } ] } ] },
    { "id": "audio.wide-guitar", "category": "02 Guitars", "name": "Wide Guitar", "kind": "audio", "instrument": null,
      "strip": { "gainDb": -5.0, "pan": 0.5 }, "inserts": [ { "processorId": "builtin.gain", "params": { "gainDb": 2.0 } } ],
      "smartControls": [
        { "id": "level", "label": "Level", "group": "Main", "min": -24, "max": 6, "default": -5, "targets": [ { "path": "strip.gainDb", "from": -24, "to": 6 } ] },
        { "id": "pan", "label": "Pan", "group": "Main", "min": -1, "max": 1, "default": 0.5, "targets": [ { "path": "strip.pan", "from": -1, "to": 1 } ] },
        { "id": "boost", "label": "Boost", "group": "Tone", "min": 0, "max": 1, "default": 0.17, "targets": [ { "path": "insert.0.gainDb", "from": 0, "to": 12 } ] } ] },
    { "id": "instrument.sine-lead", "category": "01 Leads", "name": "Sine Lead", "kind": "instrument", "instrument": { "processorId": "builtin.sine" },
      "strip": { "gainDb": -6.0, "pan": 0.0 }, "inserts": [ { "processorId": "builtin.gain", "params": { "gainDb": 0.0 } } ],
      "smartControls": [
        { "id": "level", "label": "Level", "group": "Main", "min": -24, "max": 6, "default": -6, "targets": [ { "path": "strip.gainDb", "from": -24, "to": 6 } ] },
        { "id": "pan", "label": "Pan", "group": "Main", "min": -1, "max": 1, "default": 0, "targets": [ { "path": "strip.pan", "from": -1, "to": 1 } ] },
        { "id": "boost", "label": "Boost", "group": "Tone", "min": 0, "max": 1, "default": 0, "targets": [ { "path": "insert.0.gainDb", "from": 0, "to": 12 } ] } ] },
    { "id": "instrument.soft-lead", "category": "01 Leads", "name": "Soft Lead", "kind": "instrument", "instrument": { "processorId": "builtin.sine" },
      "strip": { "gainDb": -10.0, "pan": 0.1 }, "inserts": [],
      "smartControls": [
        { "id": "level", "label": "Level", "group": "Main", "min": -24, "max": 6, "default": -10, "targets": [ { "path": "strip.gainDb", "from": -24, "to": 6 } ] },
        { "id": "pan", "label": "Pan", "group": "Main", "min": -1, "max": 1, "default": 0.1, "targets": [ { "path": "strip.pan", "from": -1, "to": 1 } ] } ] },
    { "id": "instrument.sine-pad", "category": "02 Pads", "name": "Sine Pad", "kind": "instrument", "instrument": { "processorId": "builtin.sine" },
      "strip": { "gainDb": -12.0, "pan": 0.0 }, "inserts": [],
      "smartControls": [
        { "id": "level", "label": "Level", "group": "Main", "min": -24, "max": 6, "default": -12, "targets": [ { "path": "strip.gainDb", "from": -24, "to": 6 } ] },
        { "id": "pan", "label": "Pan", "group": "Main", "min": -1, "max": 1, "default": 0, "targets": [ { "path": "strip.pan", "from": -1, "to": 1 } ] } ] },
    { "id": "instrument.deep-sub", "category": "03 Bass", "name": "Deep Sub", "kind": "instrument", "instrument": { "processorId": "builtin.sine" },
      "strip": { "gainDb": -4.0, "pan": 0.0 }, "inserts": [],
      "smartControls": [
        { "id": "level", "label": "Level", "group": "Main", "min": -24, "max": 6, "default": -4, "targets": [ { "path": "strip.gainDb", "from": -24, "to": 6 } ] } ] },
    { "id": "bus.reverb-return", "category": "01 Returns", "name": "Reverb Return", "kind": "bus", "instrument": null,
      "strip": { "gainDb": -6.0, "pan": 0.0 }, "inserts": [],
      "smartControls": [
        { "id": "level", "label": "Level", "group": "Main", "min": -24, "max": 6, "default": -6, "targets": [ { "path": "strip.gainDb", "from": -24, "to": 6 } ] } ] },
    { "id": "bus.group-bus", "category": "02 Groups", "name": "Group Bus", "kind": "bus", "instrument": null,
      "strip": { "gainDb": 0.0, "pan": 0.0 }, "inserts": [],
      "smartControls": [
        { "id": "level", "label": "Level", "group": "Main", "min": -24, "max": 6, "default": 0, "targets": [ { "path": "strip.gainDb", "from": -24, "to": 6 } ] } ] },
    { "id": "aux.fx-return", "category": "01 Returns", "name": "FX Return", "kind": "aux", "instrument": null,
      "strip": { "gainDb": -3.0, "pan": 0.0 }, "inserts": [],
      "smartControls": [
        { "id": "level", "label": "Level", "group": "Main", "min": -24, "max": 6, "default": -3, "targets": [ { "path": "strip.gainDb", "from": -24, "to": 6 } ] } ] }
  ]
}
```

- [ ] **Step 8: Run the tests**

Run: `cmake --build build --config Debug --target lpc_tests` then `ctest --test-dir build -C Debug -R "PatchLibrary" --output-on-failure`
Expected: all PASS (the "bad entries" case reports exactly `patches.size() - 3` problems; if the count differs, the failure lists which entry loaded).

- [ ] **Step 9: Whole suite and commit**

Run: `ctest --test-dir build -C Debug --output-on-failure` — Expected: all PASS.

```bash
git add core tests docs/superpowers/specs/2026-10-08-ui-b-panels-design.md
git commit -m "feat(core): PatchLibrary with the built-in patch catalogue and Smart Control mapping"
```

### Task 4: Bridge: snapshot, models, controller wrappers, panel state

**Files:**
- Create: `ui/bridge/row_maps.h`, `ui/bridge/inspector_model.{h,cpp}`, `ui/bridge/library_model.{h,cpp}`, `ui/tests/tst_panel_models.cpp`
- Modify: `ui/bridge/snapshot.{h,cpp}`, `ui/bridge/project_controller.{h,cpp}`, `ui/CMakeLists.txt`, `ui/tests/CMakeLists.txt`, `ui/tests/tst_bridge.cpp`, `ui/actions/actions.json`, `ui/qml/Main.qml`

**Interfaces:**
- Consumes: Core `PatchLibrary`, the Task 1 and 2 commands (all through `sendCommand`).
- Produces (snapshot): `InsertRow{processorId, gainDb}`, `SendRow{id, targetId, targetName, levelDb, preFader}`, `SmartRow{id, label, group, min, max, value, def}`; `TrackRow` gains `patchId, patchName, instrument, outputId, outputName, inserts, sends, smart`; `RegionRow` gains `gainDb`; `makeSnapshot(project, revision, mediaPresent, const lpc::PatchLibrary* patches = nullptr)`.
- Produces (`row_maps.h`): `QVariantMap trackToMap(const TrackRow&)` with keys `trackId, name, color, kind, master, patchId, patchName, instrument, gainDb, pan, mute, solo, outputId, outputName, inserts` (list of `{processorId, gainDb}`), `sends` (list of `{id, targetId, targetName, levelDb, preFader}`).
- Produces (`InspectorModel`, QML type `jad::InspectorModel*` as `project.inspector`): properties `hasTrack, hasRegion, track, output, region, smartControls, busTargets` (all `NOTIFY changed`); `region` keys `regionId, trackId, trackName, audio, gainDb, startBeats, lengthBeats`; `smartControls` items `{id, label, group, min, max, value, def}`; `busTargets` items `{id, name}`; C++ `update(tracks, regions, selectedTrackIds, selectedRegionIds)`, `trackId()`, `shownKind()`, `shownPatchId()`. The shown track is the first selected track, else the track of the first selected region.
- Produces (`LibraryModel`, `project.library`): properties `categories, category (RW), search (RW), patches` (items `{id, name, category}`), `currentPatchId, kind, problems, patchCount`; `setLibrary(const lpc::PatchLibrary*)`, `setTrack(kind, patchId)`, `Q_INVOKABLE QString neighbour(int step) const`.
- Produces (`ProjectController`): properties `inspector, library, inspectorVisible (default true), libraryVisible (false), smartControlsVisible (false), leftColumnWidth (240, clamped 200..320), smartControlsHeight (180, clamped 120..320)` with `panelsChanged`; invokables `applyPatch(patchId)`, `revertPatch()`, `addInsert(trackId, processorId)`, `removeInsert(trackId, index)`, `setInsertParam(trackId, index, param, value)`, `addSend(trackId, targetId)`, `removeSend(sendId)`, `setSendLevel(sendId, db)`, `setSendPreFader(sendId, on)`, `setOutput(trackId, outputId)` (`""` = master), `setRegionGain(regionId, db)`, `setSmartControl(trackId, controlId, value)`, `announceStub(label)` (emits `notice("<label>: not implemented yet")`).

- [ ] **Step 1: Write the failing model tests**

Create `ui/tests/tst_panel_models.cpp`:

```cpp
#include <QtTest>
#include <nlohmann/json.hpp>

#include "bridge/inspector_model.h"
#include "bridge/library_model.h"
#include "bridge/row_maps.h"
#include "lpc/patch_library.h"

namespace {
jad::TrackRow row(const char* id, const char* kind, const char* name, bool master = false) {
    jad::TrackRow r;
    r.id = id;
    r.kind = kind;
    r.name = name;
    r.master = master;
    r.color = "blue";
    return r;
}
jad::RegionRow region(const char* id, const char* trackId) {
    jad::RegionRow r;
    r.id = id;
    r.trackId = trackId;
    r.lengthBeats = 4;
    return r;
}
const char* kDoc = R"({"patches":[
 {"id":"a.one","category":"01 A","name":"Alpha","kind":"audio","instrument":null,"strip":{"gainDb":-3},"inserts":[],"smartControls":[]},
 {"id":"a.two","category":"01 A","name":"Beta","kind":"audio","instrument":null,"strip":{"gainDb":-4},"inserts":[],"smartControls":[]},
 {"id":"a.three","category":"02 B","name":"Gamma","kind":"audio","instrument":null,"strip":{"gainDb":-5},"inserts":[],"smartControls":[]},
 {"id":"i.one","category":"01 Leads","name":"Lead","kind":"instrument","instrument":{"processorId":"builtin.sine"},"strip":{},"inserts":[],"smartControls":[]}]})";
}  // namespace

class PanelModelsTest : public QObject {
    Q_OBJECT
private slots:
    void inspectorShowsTheFirstSelectedTrackAndItsOutput() {
        jad::TrackRow master = row("m", "master", "Stereo Out", true);
        jad::TrackRow a = row("a", "audio", "Vox");
        a.outputId = "m";
        a.outputName = "Stereo Out";
        a.inserts.push_back({"builtin.gain", 3.0});
        jad::InspectorModel model;
        model.update({master, a}, {}, {"a"}, {});
        QVERIFY(model.hasTrack());
        QVERIFY(!model.hasRegion());
        QCOMPARE(model.track().value("name").toString(), QStringLiteral("Vox"));
        QCOMPARE(model.track().value("inserts").toList().size(), 1);
        QCOMPARE(model.output().value("trackId").toString(), QStringLiteral("m"));
    }
    void inspectorFollowsTheRegionWhenNoTrackIsSelected() {
        jad::InspectorModel model;
        model.update({row("m", "master", "Out", true), row("a", "audio", "Vox")}, {region("r", "a")}, {}, {"r"});
        QVERIFY(model.hasRegion());
        QVERIFY(model.hasTrack());
        QCOMPARE(model.trackId(), QStringLiteral("a"));
        QCOMPARE(model.region().value("trackName").toString(), QStringLiteral("Vox"));
    }
    void inspectorIsNeutralForStaleIds() {
        jad::InspectorModel model;
        model.update({row("m", "master", "Out", true), row("a", "audio", "Vox")}, {}, {"gone"}, {"gone-too"});
        QVERIFY(!model.hasTrack());
        QVERIFY(!model.hasRegion());
        QVERIFY(model.track().isEmpty());
        QVERIFY(model.smartControls().isEmpty());
    }
    void inspectorListsBusTargetsAndSmartControls() {
        jad::TrackRow bus = row("b", "bus", "Reverb");
        jad::TrackRow a = row("a", "audio", "Vox");
        jad::SmartRow s;
        s.id = "level";
        s.label = "Level";
        s.group = "Main";
        s.min = -24;
        s.max = 6;
        s.value = -3;
        a.smart.push_back(s);
        jad::InspectorModel model;
        model.update({row("m", "master", "Out", true), bus, a}, {}, {"a"}, {});
        QCOMPARE(model.busTargets().size(), 1);
        QCOMPARE(model.smartControls().size(), 1);
        QCOMPARE(model.smartControls().first().toMap().value("label").toString(), QStringLiteral("Level"));
    }
    void inspectorEmitsChangedOnlyWhenSomethingChanged() {
        jad::InspectorModel model;
        const std::vector<jad::TrackRow> rows{row("m", "master", "Out", true), row("a", "audio", "Vox")};
        QSignalSpy spy(&model, &jad::InspectorModel::changed);
        model.update(rows, {}, {"a"}, {});
        QCOMPARE(spy.count(), 1);
        model.update(rows, {}, {"a"}, {});
        QCOMPARE(spy.count(), 1);
    }
    void libraryListsCategoriesAndPatchesOfTheTrackKind() {
        const auto lib = lpc::PatchLibrary::fromJson(nlohmann::json::parse(kDoc));
        jad::LibraryModel model;
        model.setLibrary(&lib);
        model.setTrack("audio", "");
        QCOMPARE(model.categories(), (QStringList{"01 A", "02 B"}));
        QCOMPARE(model.category(), QStringLiteral("01 A"));  // the first one when the track has no patch
        QCOMPARE(model.patches().size(), 2);
        model.setCategory("02 B");
        QCOMPARE(model.patches().size(), 1);
        model.setTrack("instrument", "");
        QCOMPARE(model.categories(), (QStringList{"01 Leads"}));
        model.setTrack("midi", "");
        QVERIFY(model.categories().isEmpty());
        QVERIFY(model.patches().isEmpty());
    }
    void libraryOpensOnTheCategoryOfTheCurrentPatch() {
        const auto lib = lpc::PatchLibrary::fromJson(nlohmann::json::parse(kDoc));
        jad::LibraryModel model;
        model.setLibrary(&lib);
        model.setTrack("audio", "a.three");
        QCOMPARE(model.category(), QStringLiteral("02 B"));
        QCOMPARE(model.currentPatchId(), QStringLiteral("a.three"));
        model.setTrack("audio", "removed.from.catalogue");  // an id the catalogue no longer has: no selection, no error
        QCOMPARE(model.currentPatchId(), QStringLiteral("removed.from.catalogue"));
        QVERIFY(!model.neighbour(+1).isEmpty());  // still steps from the start of the shown list
    }
    void librarySearchCrossesCategories() {
        const auto lib = lpc::PatchLibrary::fromJson(nlohmann::json::parse(kDoc));
        jad::LibraryModel model;
        model.setLibrary(&lib);
        model.setTrack("audio", "");
        model.setSearch("a");  // Alpha, Beta, Gamma all contain an a
        QCOMPARE(model.patches().size(), 3);
        model.setSearch("gam");
        QCOMPARE(model.patches().size(), 1);
        model.setSearch("zzz");
        QVERIFY(model.patches().isEmpty());
        model.setSearch("");
        QCOMPARE(model.patches().size(), 2);
    }
    void libraryNeighbourStepsWithoutWrapping() {
        const auto lib = lpc::PatchLibrary::fromJson(nlohmann::json::parse(kDoc));
        jad::LibraryModel model;
        model.setLibrary(&lib);
        model.setTrack("audio", "a.one");
        QCOMPARE(model.neighbour(+1), QStringLiteral("a.two"));
        QCOMPARE(model.neighbour(-1), QStringLiteral("a.one"));  // already the first: stays
        model.setTrack("audio", "a.two");
        QCOMPARE(model.neighbour(+1), QStringLiteral("a.two"));  // already the last of the category: stays
        model.setTrack("audio", "");
        QCOMPARE(model.neighbour(+1), QStringLiteral("a.one"));  // nothing current: the first (or the last going back)
        QCOMPARE(model.neighbour(-1), QStringLiteral("a.two"));
    }
    void libraryReportsCatalogueProblems() {
        const auto lib = lpc::PatchLibrary::fromJson(nlohmann::json::parse(R"({"patches":[{"id":"bad"}]})"));
        jad::LibraryModel model;
        model.setLibrary(&lib);
        QCOMPARE(model.problems().size(), 1);
        QCOMPARE(model.patchCount(), 0);
    }
};

QTEST_MAIN(PanelModelsTest)
#include "tst_panel_models.moc"
```

- [ ] **Step 2: Register the test and run to verify it fails**

In `ui/tests/CMakeLists.txt` add after `jad_add_qt_test(jad_lcd_tests tst_lcd.cpp)`:

```cmake
jad_add_qt_test(jad_panel_models_tests tst_panel_models.cpp)
```

Run: `cmake --build build-ui --config Debug --target jad_panel_models_tests`
Expected: FAIL to compile (`bridge/inspector_model.h` not found).

- [ ] **Step 3: Snapshot data and row maps**

In `ui/bridge/snapshot.h`, before `struct TrackRow`, add:

```cpp
struct InsertRow {
    QString processorId;
    double gainDb = 0.0;  // the "gainDb" parameter (0 when absent)
};
struct SendRow {
    QString id, targetId, targetName;
    double levelDb = 0.0;
    bool preFader = false;
};
struct SmartRow {
    QString id, label, group;
    double min = 0.0, max = 1.0, value = 0.0, def = 0.0;
};
```

In `struct TrackRow` after `int regionCount = 0;` add:

```cpp
    QString patchId, patchName, instrument, outputId, outputName;  // outputId: the master's id when the output is the master ("" on the master)
    std::vector<InsertRow> inserts;
    std::vector<SendRow> sends;
    std::vector<SmartRow> smart;  // the Smart Controls of the track's patch, with their current values
```

In `struct RegionRow` after `QString color;` add `double gainDb = 0.0;`. Add the forward declaration `namespace lpc { class PatchLibrary; }` before `namespace jad {` and change the declaration of `makeSnapshot` to:

```cpp
Snapshot makeSnapshot(const lpc::Project& project, std::uint64_t revision,
                      const std::function<bool(const lpc::MediaItem&)>& mediaPresent, const lpc::PatchLibrary* patches = nullptr);
```

In `ui/bridge/snapshot.cpp` add `#include "lpc/patch_library.h"`, change the definition to take `const lpc::PatchLibrary* patches`, and inside the track loop after `tr.regionCount = ...;` add:

```cpp
        tr.patchId = QString::fromStdString(t.patchId);
        tr.instrument = t.instrument ? QString::fromStdString(t.instrument->processorId) : QString();
        if (!master) {
            const lpc::Track* out = t.strip.output.isNull() ? p.master() : p.findTrack(t.strip.output);
            if (out) {
                tr.outputId = QString::fromStdString(out->id.toString());
                tr.outputName = QString::fromStdString(out->name);
            }
        }
        for (const lpc::ProcessorRef& ins : t.strip.inserts) {
            const auto g = ins.params.find("gainDb");
            tr.inserts.push_back({QString::fromStdString(ins.processorId), g == ins.params.end() ? 0.0 : g->second});
        }
        for (const lpc::Send& s : t.strip.sends) {
            const lpc::Track* target = p.findTrack(s.targetTrackId);
            tr.sends.push_back({QString::fromStdString(s.id.toString()), QString::fromStdString(s.targetTrackId.toString()),
                                target ? QString::fromStdString(target->name) : QString(), s.levelDb, s.preFader});
        }
        if (patches && !t.patchId.empty()) {
            if (const lpc::Patch* patch = patches->find(t.patchId)) {
                tr.patchName = QString::fromStdString(patch->name);
                for (const lpc::SmartControl& c : patch->smartControls) {
                    const auto value = lpc::PatchLibrary::smartControlValue(t, c);
                    if (!value) continue;  // the insert it drives is gone
                    SmartRow sr;
                    sr.id = QString::fromStdString(c.id);
                    sr.label = QString::fromStdString(c.label);
                    sr.group = QString::fromStdString(c.group);
                    sr.min = c.min;
                    sr.max = c.max;
                    sr.value = *value;
                    sr.def = c.def;
                    tr.smart.push_back(sr);
                }
            }
        }
```

and in the region loop after `rr.color = tr.color;` add `rr.gainDb = r.gainDb;`.

Create `ui/bridge/row_maps.h`:

```cpp
#pragma once
#include <QVariantList>
#include <QVariantMap>

#include "bridge/snapshot.h"

namespace jad {

// The strip view of a track as a plain map for QML (the Inspector, the mixer): see the keys below.
inline QVariantMap trackToMap(const TrackRow& t) {
    QVariantList inserts, sends;
    for (const InsertRow& i : t.inserts) inserts.append(QVariantMap{{"processorId", i.processorId}, {"gainDb", i.gainDb}});
    for (const SendRow& s : t.sends)
        sends.append(QVariantMap{{"id", s.id}, {"targetId", s.targetId}, {"targetName", s.targetName}, {"levelDb", s.levelDb}, {"preFader", s.preFader}});
    return {{"trackId", t.id},         {"name", t.name},           {"color", t.color},       {"kind", t.kind},
            {"master", t.master},      {"patchId", t.patchId},     {"patchName", t.patchName}, {"instrument", t.instrument},
            {"gainDb", t.gainDb},      {"pan", t.pan},             {"mute", t.mute},         {"solo", t.solo},
            {"outputId", t.outputId},  {"outputName", t.outputName}, {"inserts", inserts},   {"sends", sends}};
}

}  // namespace jad
```

- [ ] **Step 4: InspectorModel**

Create `ui/bridge/inspector_model.h`:

```cpp
#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <vector>

#include "bridge/snapshot.h"

namespace jad {

// What the Inspector, the Library and the Smart Controls show: the track the selection points at (the first selected
// track, else the track of the first selected region), its output, the region, and the buses a send can go to.
class InspectorModel : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY changed)
    Q_PROPERTY(bool hasRegion READ hasRegion NOTIFY changed)
    Q_PROPERTY(QVariantMap track READ track NOTIFY changed)
    Q_PROPERTY(QVariantMap output READ output NOTIFY changed)
    Q_PROPERTY(QVariantMap region READ region NOTIFY changed)
    Q_PROPERTY(QVariantList smartControls READ smartControls NOTIFY changed)
    Q_PROPERTY(QVariantList busTargets READ busTargets NOTIFY changed)
public:
    explicit InspectorModel(QObject* parent = nullptr) : QObject(parent) {}
    // `tracks` holds every track, the master included. Ids that do not exist are ignored.
    void update(const std::vector<TrackRow>& tracks, const std::vector<RegionRow>& regions, const QStringList& selectedTracks,
                const QStringList& selectedRegions);
    bool hasTrack() const { return !shownTrackId_.isEmpty(); }
    bool hasRegion() const { return !region_.isEmpty(); }
    QVariantMap track() const { return track_; }
    QVariantMap output() const { return output_; }
    QVariantMap region() const { return region_; }
    QVariantList smartControls() const { return smart_; }
    QVariantList busTargets() const { return targets_; }
    QString trackId() const { return shownTrackId_; }
    QString shownKind() const { return shownKind_; }
    QString shownPatchId() const { return shownPatchId_; }

signals:
    void changed();

private:
    QVariantMap track_, output_, region_;
    QVariantList smart_, targets_;
    QString shownTrackId_, shownKind_, shownPatchId_;
};

}  // namespace jad
```

Create `ui/bridge/inspector_model.cpp`:

```cpp
#include "bridge/inspector_model.h"

#include "bridge/row_maps.h"

namespace jad {

void InspectorModel::update(const std::vector<TrackRow>& tracks, const std::vector<RegionRow>& regions, const QStringList& selectedTracks,
                            const QStringList& selectedRegions) {
    const auto findTrack = [&](const QString& id) -> const TrackRow* {
        for (const TrackRow& t : tracks)
            if (t.id == id && !t.master) return &t;
        return nullptr;
    };
    const auto findOutput = [&](const QString& id) -> const TrackRow* {
        for (const TrackRow& t : tracks)
            if (t.id == id) return &t;
        return nullptr;
    };

    const RegionRow* shownRegion = nullptr;
    for (const QString& id : selectedRegions) {
        for (const RegionRow& r : regions)
            if (r.id == id) { shownRegion = &r; break; }
        if (shownRegion) break;
    }
    const TrackRow* shown = nullptr;
    for (const QString& id : selectedTracks)
        if ((shown = findTrack(id))) break;
    if (!shown && shownRegion) shown = findTrack(shownRegion->trackId);

    QVariantMap track, output, region;
    QVariantList smart, targets;
    if (shown) {
        track = trackToMap(*shown);
        if (const TrackRow* out = findOutput(shown->outputId)) output = trackToMap(*out);
        for (const SmartRow& s : shown->smart)
            smart.append(QVariantMap{{"id", s.id}, {"label", s.label}, {"group", s.group}, {"min", s.min}, {"max", s.max}, {"value", s.value}, {"def", s.def}});
    }
    if (shownRegion) {
        region = {{"regionId", shownRegion->id},         {"trackId", shownRegion->trackId},
                  {"trackName", shown ? shown->name : QString()}, {"audio", shownRegion->audio},
                  {"gainDb", shownRegion->gainDb},       {"startBeats", shownRegion->startBeats},
                  {"lengthBeats", shownRegion->lengthBeats}};
    }
    for (const TrackRow& t : tracks)
        if (t.kind == "bus" || t.kind == "aux") targets.append(QVariantMap{{"id", t.id}, {"name", t.name}});

    const QString id = shown ? shown->id : QString();
    const bool same = track == track_ && output == output_ && region == region_ && smart == smart_ && targets == targets_ && id == shownTrackId_;
    track_ = std::move(track);
    output_ = std::move(output);
    region_ = std::move(region);
    smart_ = std::move(smart);
    targets_ = std::move(targets);
    shownTrackId_ = id;
    shownKind_ = shown ? shown->kind : QString();
    shownPatchId_ = shown ? shown->patchId : QString();
    if (!same) emit changed();
}

}  // namespace jad
```

- [ ] **Step 5: LibraryModel**

Create `ui/bridge/library_model.h`:

```cpp
#pragma once
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace lpc {
class PatchLibrary;
}

namespace jad {

// The Library panel: the categories and patches of the selected track's kind, a name search, the current patch.
class LibraryModel : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QStringList categories READ categories NOTIFY changed)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY changed)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY changed)
    Q_PROPERTY(QVariantList patches READ patches NOTIFY changed)
    Q_PROPERTY(QString currentPatchId READ currentPatchId NOTIFY changed)
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    Q_PROPERTY(QStringList problems READ problems NOTIFY changed)
    Q_PROPERTY(int patchCount READ patchCount NOTIFY changed)
public:
    explicit LibraryModel(QObject* parent = nullptr) : QObject(parent) {}
    void setLibrary(const lpc::PatchLibrary* library);  // not owned; must outlive the model
    void setTrack(const QString& kind, const QString& patchId);
    QStringList categories() const { return categories_; }
    QString category() const { return category_; }
    void setCategory(const QString& category);
    QString search() const { return search_; }
    void setSearch(const QString& text);
    QVariantList patches() const { return patches_; }
    QString currentPatchId() const { return patchId_; }
    QString kind() const { return kind_; }
    QStringList problems() const;
    int patchCount() const;
    // The id of the patch `step` places after (or before) the current one in the shown list, without wrapping; with
    // no current patch in the list: the first (step > 0) or the last. Empty when the list is empty.
    Q_INVOKABLE QString neighbour(int step) const;

signals:
    void changed();

private:
    void rebuild();
    const lpc::PatchLibrary* library_ = nullptr;
    QString kind_, patchId_, category_, search_;
    QStringList categories_;
    QVariantList patches_;
};

}  // namespace jad
```

Create `ui/bridge/library_model.cpp`:

```cpp
#include "bridge/library_model.h"

#include <QVariantMap>

#include "lpc/patch_library.h"

namespace jad {

namespace {
bool kindOf(const QString& name, lpc::TrackKind* out) {
    if (name == "audio") *out = lpc::TrackKind::Audio;
    else if (name == "instrument") *out = lpc::TrackKind::Instrument;
    else if (name == "aux") *out = lpc::TrackKind::Aux;
    else if (name == "bus") *out = lpc::TrackKind::Bus;
    else return false;
    return true;
}
QVariantMap entry(const lpc::Patch& p) {
    return {{"id", QString::fromStdString(p.id)}, {"name", QString::fromStdString(p.name)}, {"category", QString::fromStdString(p.category)}};
}
}  // namespace

void LibraryModel::setLibrary(const lpc::PatchLibrary* library) {
    library_ = library;
    rebuild();
}

void LibraryModel::setTrack(const QString& kind, const QString& patchId) {
    if (kind == kind_ && patchId == patchId_) return;
    const bool kindChanged = kind != kind_;
    kind_ = kind;
    patchId_ = patchId;
    if (library_) {
        lpc::TrackKind k;
        if (const lpc::Patch* p = library_->find(patchId.toStdString()); p && kindOf(kind, &k) && p->kind == k)
            category_ = QString::fromStdString(p->category);  // open on the category of the track's patch
        else if (kindChanged)
            category_.clear();
    }
    rebuild();
}

void LibraryModel::setCategory(const QString& category) {
    if (category == category_) return;
    category_ = category;
    rebuild();
}

void LibraryModel::setSearch(const QString& text) {
    if (text == search_) return;
    search_ = text;
    rebuild();
}

void LibraryModel::rebuild() {
    QStringList categories;
    QVariantList patches;
    lpc::TrackKind k;
    if (library_ && kindOf(kind_, &k)) {
        for (const std::string& c : library_->categories(k)) categories.append(QString::fromStdString(c));
        if (!categories.contains(category_)) category_ = categories.isEmpty() ? QString() : categories.first();
        if (search_.isEmpty()) {
            for (const lpc::Patch* p : library_->inCategory(k, category_.toStdString())) patches.append(entry(*p));
        } else {
            for (const lpc::Patch& p : library_->patches()) {
                if (p.kind != k) continue;
                const QString name = QString::fromStdString(p.name), category = QString::fromStdString(p.category);
                if (name.contains(search_, Qt::CaseInsensitive) || category.contains(search_, Qt::CaseInsensitive)) patches.append(entry(p));
            }
        }
    } else {
        category_.clear();
    }
    categories_ = categories;
    patches_ = patches;
    emit changed();
}

QStringList LibraryModel::problems() const {
    QStringList out;
    if (library_)
        for (const lpc::PatchProblem& p : library_->problems()) out.append(QString::fromStdString(p.where + ": " + p.message));
    return out;
}

int LibraryModel::patchCount() const { return library_ ? static_cast<int>(library_->patches().size()) : 0; }

QString LibraryModel::neighbour(int step) const {
    if (patches_.isEmpty()) return {};
    int at = -1;
    for (int i = 0; i < patches_.size(); ++i)
        if (patches_[i].toMap().value("id").toString() == patchId_) at = i;
    int next;
    if (at < 0) next = step > 0 ? 0 : static_cast<int>(patches_.size()) - 1;
    else next = qBound(0, at + step, static_cast<int>(patches_.size()) - 1);
    return patches_[next].toMap().value("id").toString();
}

}  // namespace jad
```

- [ ] **Step 6: Wire the files into the build**

In `ui/CMakeLists.txt`: add to `SOURCES` of `qt_add_qml_module`: `bridge/inspector_model.cpp` and `bridge/library_model.cpp`; add before `qt_add_library(...)`:

```cmake
set_source_files_properties(${CMAKE_SOURCE_DIR}/core/data/patches.json PROPERTIES QT_RESOURCE_ALIAS patches/patches.json)
```

and to `RESOURCES`: `${CMAKE_SOURCE_DIR}/core/data/patches.json`.

- [ ] **Step 7: Run the model tests**

Run: `cmake --build build-ui --config Debug --target jad_panel_models_tests` then `ctest --test-dir build-ui -C Debug -R jad_panel_models_tests --output-on-failure`
Expected: PASS (the `a`-search test counts Alpha, Beta and Gamma, all contain an `a`).

- [ ] **Step 8: Write the failing controller tests**

In `ui/tests/tst_bridge.cpp` add these slots inside `BridgeTest` (before the closing `};`), and the helper in the anonymous section above the class:

```cpp
    static QString trackIdOfKind(jad::ProjectController& c, const char* kind) {
        const auto roles = c.tracks()->roleNames();
        for (int i = 0; i < c.tracks()->rowCount(); ++i)
            if (c.tracks()->data(c.tracks()->index(i), roles.key("kind")).toString() == kind)
                return c.tracks()->data(c.tracks()->index(i), roles.key("trackId")).toString();
        return {};
    }
```

```cpp
    void applyingAPatchIsOneUndoStep() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        c.selectTrack(trackIdOfKind(c, "audio"), "replace");
        c.applyPatch("audio.bright-vocal");
        QTRY_COMPARE(c.inspector()->track().value("patchId").toString(), QStringLiteral("audio.bright-vocal"));
        QCOMPARE(c.inspector()->track().value("gainDb").toDouble(), -2.0);
        QCOMPARE(c.inspector()->track().value("inserts").toList().size(), 1);
        QCOMPARE(c.inspector()->track().value("patchName").toString(), QStringLiteral("Bright Vocal"));
        QCOMPARE(c.library()->currentPatchId(), QStringLiteral("audio.bright-vocal"));
        c.undo();
        QTRY_COMPARE(c.inspector()->track().value("patchId").toString(), QString());
        QCOMPARE(c.inspector()->track().value("inserts").toList().size(), 0);
    }
    void aPatchOfAnotherKindOrNoTrackGivesANoticeAndChangesNothing() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        QSignalSpy notices(&c, &jad::ProjectController::notice);
        c.applyPatch("audio.bright-vocal");  // nothing selected
        QCOMPARE(notices.count(), 1);
        c.selectTrack(trackIdOfKind(c, "instrument"), "replace");
        c.applyPatch("audio.bright-vocal");  // an audio patch on an instrument track
        QCOMPARE(notices.count(), 2);
        c.applyPatch("gone.patch");
        QCOMPARE(notices.count(), 3);
        QVERIFY(c.inspector()->track().value("patchId").toString().isEmpty());
    }
    void revertReappliesThePatchOfTheTrack() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString audio = trackIdOfKind(c, "audio");
        c.selectTrack(audio, "replace");
        c.applyPatch("audio.warm-guitar");
        QTRY_COMPARE(c.inspector()->track().value("gainDb").toDouble(), -4.0);
        c.setGain(audio, 3.0);
        QTRY_COMPARE(c.inspector()->track().value("gainDb").toDouble(), 3.0);
        c.revertPatch();
        QTRY_COMPARE(c.inspector()->track().value("gainDb").toDouble(), -4.0);
    }
    void aSmartControlMovesEveryTargetAndIsOneUndoStep() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString audio = trackIdOfKind(c, "audio");
        c.selectTrack(audio, "replace");
        c.applyPatch("audio.bright-vocal");
        QTRY_COMPARE(c.inspector()->smartControls().size(), 3);
        c.setSmartControl(audio, "boost", 1.0);
        QTRY_COMPARE(c.inspector()->track().value("gainDb").toDouble(), -5.0);
        QCOMPARE(c.inspector()->track().value("inserts").toList().first().toMap().value("gainDb").toDouble(), 12.0);
        double boost = 0;
        for (const QVariant& s : c.inspector()->smartControls())
            if (s.toMap().value("id").toString() == "boost") boost = s.toMap().value("value").toDouble();
        QCOMPARE(boost, 1.0);  // the knob reads its value back from the track
        c.undo();
        QTRY_COMPARE(c.inspector()->track().value("gainDb").toDouble(), -2.0);
        c.setSmartControl(audio, "nope", 1.0);                     // unknown control: ignored
        c.setSmartControl(audio, "boost", std::nan(""));            // NaN: ignored
        c.setSmartControl("not-a-track", "boost", 1.0);             // stale track: ignored
    }
    void insertsSendsOutputAndRegionGainGoThroughCommands() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const QString audio = trackIdOfKind(c, "audio");
        const QString bus = trackIdOfKind(c, "bus");
        c.selectTrack(audio, "replace");
        c.addInsert(audio, "builtin.gain");
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().size(), 1);
        c.setInsertParam(audio, 0, "gainDb", 4.5);
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().first().toMap().value("gainDb").toDouble(), 4.5);
        c.removeInsert(audio, 0);
        QTRY_COMPARE(c.inspector()->track().value("inserts").toList().size(), 0);
        c.addSend(audio, bus);
        QTRY_COMPARE(c.inspector()->track().value("sends").toList().size(), 1);
        const QString sendId = c.inspector()->track().value("sends").toList().first().toMap().value("id").toString();
        c.setSendLevel(sendId, -20.0);
        QTRY_COMPARE(c.inspector()->track().value("sends").toList().first().toMap().value("levelDb").toDouble(), -20.0);
        c.setSendPreFader(sendId, true);
        QTRY_VERIFY(c.inspector()->track().value("sends").toList().first().toMap().value("preFader").toBool());
        c.removeSend(sendId);
        QTRY_COMPARE(c.inspector()->track().value("sends").toList().size(), 0);
        c.setOutput(audio, bus);
        QTRY_COMPARE(c.inspector()->output().value("trackId").toString(), bus);
        c.setOutput(audio, QString());
        QTRY_VERIFY(c.inspector()->output().value("master").toBool());
        c.createRegion(trackIdOfKind(c, "instrument"), 0.0, 4.0);
        QTRY_VERIFY(c.regions()->rowCount() > 0);
        const QString region = c.regions()->regionIdAt(c.regions()->rowCount() - 1);
        c.selectRegion(region, "replace");
        c.setRegionGain(region, -6.0);
        QTRY_COMPARE(c.inspector()->region().value("gainDb").toDouble(), -6.0);
    }
    void theInspectorGoesNeutralWhenItsTrackIsDeleted() {
        TempDir dir;
        jad::ProjectController c(false);
        QVERIFY(c.openProject(url(makeDemo(dir))));
        QTRY_VERIFY(c.tracks()->rowCount() >= 3);
        const int before = c.tracks()->rowCount();
        c.addTrack("audio");
        QTRY_COMPARE(c.tracks()->rowCount(), before + 1);
        const QString last = c.tracks()->trackIdAt(c.tracks()->rowCount() - 1);
        c.selectTrack(last, "replace");
        QTRY_VERIFY(c.inspector()->hasTrack());
        c.deleteSelectedTracks();
        QTRY_VERIFY(!c.inspector()->hasTrack());
        QVERIFY(c.library()->categories().isEmpty());
    }
    void panelStateHasDefaultsAndClampsSizes() {
        jad::ProjectController c(false);
        QVERIFY(c.inspectorVisible());
        QVERIFY(!c.libraryVisible());
        QVERIFY(!c.smartControlsVisible());
        QSignalSpy spy(&c, &jad::ProjectController::panelsChanged);
        c.setLibraryVisible(true);
        c.setLeftColumnWidth(5000);
        QCOMPARE(c.leftColumnWidth(), 320.0);
        c.setLeftColumnWidth(10);
        QCOMPARE(c.leftColumnWidth(), 200.0);
        c.setLeftColumnWidth(std::nan(""));
        QCOMPARE(c.leftColumnWidth(), 200.0);
        c.setSmartControlsHeight(1000);
        QCOMPARE(c.smartControlsHeight(), 320.0);
        c.setSmartControlsHeight(0);
        QCOMPARE(c.smartControlsHeight(), 120.0);
        QVERIFY(spy.count() >= 4);
    }
    void announceStubEmitsANotice() {
        jad::ProjectController c(false);
        QSignalSpy spy(&c, &jad::ProjectController::notice);
        c.announceStub("Quantize");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), QStringLiteral("Quantize: not implemented yet"));
    }
    void theCatalogueLoadsFromTheResources() {
        jad::ProjectController c(false);
        QVERIFY(c.library()->problems().isEmpty());
        QVERIFY(c.library()->patchCount() >= 8);
    }
```

Add `#include <cmath>` at the top of `tst_bridge.cpp` if missing.

In `ui/tests/tst_models.cpp` add to `ModelsTest` (after `missingMediaIsFlagged`):

```cpp
    void snapshotCarriesTheStripDetailsOfEachTrack() {
        TempDir dir;
        const lpc::Project p = lpc::makeDemoProject(dir.path());
        const jad::Snapshot s = jad::makeSnapshot(p, 1, [](const lpc::MediaItem&) { return true; });
        const auto keys = std::find_if(s.tracks.begin(), s.tracks.end(), [](const jad::TrackRow& t) { return t.kind == "instrument"; });
        QVERIFY(keys != s.tracks.end());
        QCOMPARE(keys->instrument, QStringLiteral("builtin.sine"));
        QCOMPARE(int(keys->sends.size()), 1);
        QCOMPARE(keys->sends[0].targetName, QStringLiteral("Reverb Bus"));
        QVERIFY(!keys->outputId.isEmpty());  // the master
        QVERIFY(keys->patchId.isEmpty());
        QVERIFY(keys->smart.empty());
        const auto master = std::find_if(s.tracks.begin(), s.tracks.end(), [](const jad::TrackRow& t) { return t.master; });
        QVERIFY(master != s.tracks.end());
        QVERIFY(master->outputId.isEmpty());
        const auto region = std::find_if(s.regions.begin(), s.regions.end(), [](const jad::RegionRow&) { return true; });
        QVERIFY(region != s.regions.end());
        QCOMPARE(region->gainDb, 0.0);
    }
```

- [ ] **Step 9: Run to verify the controller tests fail**

Run: `cmake --build build-ui --config Debug --target jad_bridge_tests`
Expected: FAIL to compile (`inspector`, `library`, `applyPatch`, ... not members).

- [ ] **Step 10: Controller: header**

In `ui/bridge/project_controller.h` add the includes `#include "bridge/inspector_model.h"` and `#include "bridge/library_model.h"`, a forward declaration `class PatchLibrary;` inside `namespace lpc {`, and `#include <optional>`. Add to the `Q_PROPERTY` block:

```cpp
    Q_PROPERTY(jad::InspectorModel* inspector READ inspector CONSTANT)
    Q_PROPERTY(jad::LibraryModel* library READ library CONSTANT)
    Q_PROPERTY(bool inspectorVisible READ inspectorVisible WRITE setInspectorVisible NOTIFY panelsChanged)
    Q_PROPERTY(bool libraryVisible READ libraryVisible WRITE setLibraryVisible NOTIFY panelsChanged)
    Q_PROPERTY(bool smartControlsVisible READ smartControlsVisible WRITE setSmartControlsVisible NOTIFY panelsChanged)
    Q_PROPERTY(double leftColumnWidth READ leftColumnWidth WRITE setLeftColumnWidth NOTIFY panelsChanged)
    Q_PROPERTY(double smartControlsHeight READ smartControlsHeight WRITE setSmartControlsHeight NOTIFY panelsChanged)
```

In the public section after `MixerModel* mixer() { return &mixer_; }`:

```cpp
    InspectorModel* inspector() { return &inspector_; }
    LibraryModel* library() { return &library_; }
    bool inspectorVisible() const { return inspectorVisible_; }
    bool libraryVisible() const { return libraryVisible_; }
    bool smartControlsVisible() const { return smartControlsVisible_; }
    double leftColumnWidth() const { return leftColumnWidth_; }
    double smartControlsHeight() const { return smartControlsHeight_; }
    void setInspectorVisible(bool on) { if (on != inspectorVisible_) { inspectorVisible_ = on; emit panelsChanged(); } }
    void setLibraryVisible(bool on) { if (on != libraryVisible_) { libraryVisible_ = on; emit panelsChanged(); } }
    void setSmartControlsVisible(bool on) { if (on != smartControlsVisible_) { smartControlsVisible_ = on; emit panelsChanged(); } }
    void setLeftColumnWidth(double width);       // clamped to 200..320, NaN ignored
    void setSmartControlsHeight(double height);  // clamped to 120..320, NaN ignored
```

After the `setSolo` / `toggleSolo` invokables:

```cpp
    // Panels. Every edit is one command (or one transaction); values are clamped, NaN is ignored.
    Q_INVOKABLE void applyPatch(const QString& patchId);  // to the track the panels show; a notice when it does not fit
    Q_INVOKABLE void revertPatch();                       // applies the patch of that track again
    Q_INVOKABLE void addInsert(const QString& trackId, const QString& processorId);
    Q_INVOKABLE void removeInsert(const QString& trackId, int index);
    Q_INVOKABLE void setInsertParam(const QString& trackId, int index, const QString& param, double value);
    Q_INVOKABLE void addSend(const QString& trackId, const QString& targetId);
    Q_INVOKABLE void removeSend(const QString& sendId);
    Q_INVOKABLE void setSendLevel(const QString& sendId, double db);
    Q_INVOKABLE void setSendPreFader(const QString& sendId, bool on);
    Q_INVOKABLE void setOutput(const QString& trackId, const QString& outputId);  // "" = master
    Q_INVOKABLE void setRegionGain(const QString& regionId, double db);
    Q_INVOKABLE void setSmartControl(const QString& trackId, const QString& controlId, double value);
    Q_INVOKABLE void announceStub(const QString& label);  // a visual-only control was used: the usual notice
```

In the signals section add `void panelsChanged();`. In the private section add:

```cpp
    void refreshPanels();
    const TrackRow* rowOf(const QString& trackId) const;
```

and members (after `MixerModel mixer_;`):

```cpp
    InspectorModel inspector_;
    LibraryModel library_;
    std::shared_ptr<const lpc::PatchLibrary> patches_;  // the built-in catalogue; also read on the project thread
    std::vector<TrackRow> allRows_;                     // every track of the last snapshot, the master included
    std::vector<RegionRow> regionRows_;
    bool inspectorVisible_ = true, libraryVisible_ = false, smartControlsVisible_ = false;
    double leftColumnWidth_ = 240.0, smartControlsHeight_ = 180.0;
```

- [ ] **Step 11: Controller: implementation**

In `ui/bridge/project_controller.cpp` add includes `#include "lpc/patch_library.h"`; in the anonymous namespace near the top (after `kMaxBeats`) add:

```cpp
std::shared_ptr<const lpc::PatchLibrary> loadPatchCatalogue() {
    QFile file(QStringLiteral(":/qt/qml/Jad/patches/patches.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        nlohmann::json empty = {{"patches", nlohmann::json::array()}};
        auto lib = std::make_shared<lpc::PatchLibrary>(lpc::PatchLibrary::fromJson(empty));
        return lib;
    }
    try {
        const QByteArray bytes = file.readAll();
        return std::make_shared<lpc::PatchLibrary>(lpc::PatchLibrary::fromJson(nlohmann::json::parse(bytes.constData(), bytes.constData() + bytes.size())));
    } catch (const std::exception&) {
        return std::make_shared<lpc::PatchLibrary>(lpc::PatchLibrary::fromJson(nlohmann::json::object()));  // reports one problem
    }
}

std::optional<lpc::TrackKind> kindFromName(const QString& k) {
    if (k == "audio") return lpc::TrackKind::Audio;
    if (k == "instrument") return lpc::TrackKind::Instrument;
    if (k == "aux") return lpc::TrackKind::Aux;
    if (k == "bus") return lpc::TrackKind::Bus;
    return std::nullopt;
}
std::optional<lpc::Uuid> uuidOf(const QString& id) { return lpc::Uuid::parse(id.toStdString()); }
```

In the second constructor, after the `connect(... trackTogglesChanged)` line add:

```cpp
    patches_ = loadPatchCatalogue();
    library_.setLibrary(patches_.get());
    connect(this, &ProjectController::selectionChanged, this, &ProjectController::refreshPanels);
```

In `refresh()` change the `makeSnapshot` call to pass the catalogue (capture `patches = patches_` by value so the project thread owns a reference):

```cpp
    auto future = std::make_shared<std::future<Snapshot>>(host_->read([host, media, patches = patches_](const lpc::Project& p) {
        return makeSnapshot(p, host->revision(), [media](const lpc::MediaItem& item) { return media->open(item) != nullptr; }, patches.get());
    }));
```

In `applySnapshot`, right after `regions_.reset(s.regions);` add `allRows_ = s.tracks; regionRows_ = s.regions;` and, after `emit projectChanged();` and before `pruneSelection();` leave as is, then at the very end of the function (after `pruneSelection();`) add `refreshPanels();`. Add the new methods at the end of the file (before the closing namespace):

```cpp
void ProjectController::setLeftColumnWidth(double width) {
    if (!std::isfinite(width)) return;
    const double clamped = std::clamp(width, 200.0, 320.0);
    if (clamped == leftColumnWidth_) return;
    leftColumnWidth_ = clamped;
    emit panelsChanged();
}

void ProjectController::setSmartControlsHeight(double height) {
    if (!std::isfinite(height)) return;
    const double clamped = std::clamp(height, 120.0, 320.0);
    if (clamped == smartControlsHeight_) return;
    smartControlsHeight_ = clamped;
    emit panelsChanged();
}

const TrackRow* ProjectController::rowOf(const QString& trackId) const {
    for (const TrackRow& r : allRows_)
        if (r.id == trackId) return &r;
    return nullptr;
}

void ProjectController::refreshPanels() {
    inspector_.update(allRows_, regionRows_, selectedTracks_, selectedRegions_);
    library_.setTrack(inspector_.shownKind(), inspector_.shownPatchId());
}

void ProjectController::announceStub(const QString& label) { emit notice(tr("%1: not implemented yet").arg(label)); }

void ProjectController::applyPatch(const QString& patchId) {
    const QString trackId = inspector_.trackId();
    const TrackRow* row = rowOf(trackId);
    const auto id = uuidOf(trackId);
    const auto kind = row ? kindFromName(row->kind) : std::nullopt;
    if (!row || !id || !kind) {
        emit notice(tr("Select a track to choose a patch"));
        return;
    }
    lpc::CommandPtr cmd = patches_->applyCommand(*id, *kind, patchId.toStdString());
    if (!cmd) {
        emit notice(tr("This patch does not fit the selected track"));
        return;
    }
    sendCommand(cmd->toJson());
}

void ProjectController::revertPatch() {
    const TrackRow* row = rowOf(inspector_.trackId());
    if (!row || row->patchId.isEmpty()) {
        emit notice(tr("This track has no patch to revert"));
        return;
    }
    applyPatch(row->patchId);
}

void ProjectController::setSmartControl(const QString& trackId, const QString& controlId, double value) {
    const TrackRow* row = rowOf(trackId);
    const auto id = uuidOf(trackId);
    if (!row || !id || !std::isfinite(value)) return;
    const lpc::Patch* patch = patches_->find(row->patchId.toStdString());
    if (!patch) return;
    for (const lpc::SmartControl& c : patch->smartControls) {
        if (QString::fromStdString(c.id) != controlId) continue;
        if (lpc::CommandPtr cmd = lpc::PatchLibrary::smartControlCommand(*id, c, value)) sendCommand(cmd->toJson());
        return;
    }
}

void ProjectController::addInsert(const QString& trackId, const QString& processorId) {
    sendCommand({{"type", "add_insert"}, {"trackId", trackId.toStdString()}, {"index", -1},
                 {"insert", {{"processorId", processorId.toStdString()}, {"params", nlohmann::json::object()}, {"state", ""}}}});
}

void ProjectController::removeInsert(const QString& trackId, int index) {
    sendCommand({{"type", "remove_insert"}, {"trackId", trackId.toStdString()}, {"index", index}});
}

void ProjectController::setInsertParam(const QString& trackId, int index, const QString& param, double value) {
    if (!std::isfinite(value)) return;
    const double v = param == "gainDb" ? std::clamp(value, -96.0, 24.0) : value;
    sendCommand({{"type", "set_insert_param"}, {"trackId", trackId.toStdString()}, {"index", index}, {"param", param.toStdString()}, {"value", v}});
}

void ProjectController::addSend(const QString& trackId, const QString& targetId) {
    sendCommand({{"type", "add_send"}, {"trackId", trackId.toStdString()}, {"index", -1},
                 {"send", {{"id", lpc::Uuid::random().toString()}, {"targetTrackId", targetId.toStdString()}, {"levelDb", -12.0}, {"preFader", false}}}});
}

void ProjectController::removeSend(const QString& sendId) { sendCommand({{"type", "remove_send"}, {"sendId", sendId.toStdString()}}); }

void ProjectController::setSendLevel(const QString& sendId, double db) {
    if (!std::isfinite(db)) return;
    sendCommand({{"type", "set_send"}, {"sendId", sendId.toStdString()}, {"levelDb", std::clamp(db, -96.0, 12.0)}});
}

void ProjectController::setSendPreFader(const QString& sendId, bool on) {
    sendCommand({{"type", "set_send"}, {"sendId", sendId.toStdString()}, {"preFader", on}});
}

void ProjectController::setOutput(const QString& trackId, const QString& outputId) {
    sendCommand({{"type", "set_output"}, {"trackId", trackId.toStdString()}, {"output", outputId.isEmpty() ? lpc::Uuid{}.toString() : outputId.toStdString()}});
}

void ProjectController::setRegionGain(const QString& regionId, double db) {
    if (!std::isfinite(db)) return;
    sendCommand({{"type", "set_region_gain"}, {"regionId", regionId.toStdString()}, {"gainDb", std::clamp(db, -96.0, 24.0)}});
}
```

- [ ] **Step 12: The three toggles become real**

In `ui/actions/actions.json` change `"status":"stub"` to `"status":"ready"` on the three lines `view.library`, `view.inspector`, `view.smartControls`. In `ui/qml/Main.qml` add to `handlers` (after `"view.mixer"`):

```qml
        "view.library": () => { controller.libraryVisible = !controller.libraryVisible },
        "view.inspector": () => { controller.inspectorVisible = !controller.inspectorVisible },
        "view.smartControls": () => { controller.smartControlsVisible = !controller.smartControlsVisible },
```

and to `states` (after `"view.mixer"`):

```qml
        "view.library": controller.libraryVisible,
        "view.inspector": controller.inspectorVisible,
        "view.smartControls": controller.smartControlsVisible,
```

- [ ] **Step 13: Run all UI tests**

Run: `cmake --build build-ui --config Debug` then `ctest --test-dir build-ui -C Debug --output-on-failure`
Expected: all PASS, including the new `jad_panel_models_tests` and the ten new `jad_bridge_tests` slots. If a test in `tst_actions` / `tst_shortcuts` listed the three actions as stubs, update that expectation (they are `ready` now).

- [ ] **Step 14: Commit**

```bash
git add ui docs
git commit -m "feat(ui): panel models, patch and strip edit wrappers, panel state in the controller"
```

---

### Task 5: ChannelStrip, StripSlot and the track icons

**Files:**
- Create: `ui/qml/StripSlot.qml`, `ui/qml/ChannelStrip.qml`, `ui/qml/ProjectStrip.qml`, `ui/qml/TrackIcon.qml`, `ui/qml/icons/track-audio.svg`, `track-instrument.svg`, `track-aux.svg`, `track-bus.svg`, `ui/tests/tst_channelstrip.qml`
- Modify: `ui/qml/Knob.qml`, `ui/CMakeLists.txt`

**Interfaces:**
- Produces `ChannelStrip` (QML, root `Panel`): properties `info` (map as in `trackToMap`, default `{}`), `targets` (`[{id, name}]`), `peak`, `showSlots` (default true); aliases `fader, muteButton, soloButton, addInsertSlot, outputSlot, insertList, sendList`; functions `requestOutput(outputId)`, `requestSend(targetId)`; signals `gainReleased(id, db)`, `panReleased(id, pan)`, `muteToggled(id, on)`, `soloToggled(id, on)`, `insertAddRequested(id)`, `insertRemoveRequested(id, index)`, `insertGainReleased(id, index, db)`, `sendAddRequested(id, targetId)`, `sendRemoveRequested(sendId)`, `sendLevelReleased(sendId, db)`, `outputRequested(id, outputId)`, `stubUsed(label)`.
- Produces `ProjectStrip` (root `ChannelStrip`, `required property ProjectController project`): wires every signal to the controller.
- Produces `TrackIcon` (`kind`, `tint`, `size`).
- `Knob` gains `resetValue` (double-click target, default 0) and Shift-for-fine drag.

- [ ] **Step 1: Write the failing QML tests**

Create `ui/tests/tst_channelstrip.qml`:

```qml
import QtQuick
import QtTest
import Jad

TestCase {
    name: "ChannelStrip"
    width: 300; height: 700
    visible: true
    when: windowShown

    readonly property var keys: ({ trackId: "t", name: "Keys", color: "purple", kind: "instrument", master: false,
                                   patchName: "Sine Lead", instrument: "builtin.sine", gainDb: 0, pan: 0, mute: false, solo: false,
                                   outputName: "Stereo Out",
                                   inserts: [{ processorId: "builtin.gain", gainDb: 3 }],
                                   sends: [{ id: "s1", targetId: "b", targetName: "Bus", levelDb: -12, preFader: false }] })
    Component { id: stripC; ChannelStrip { width: 96; height: 640; info: keys; targets: [{ id: "b", name: "Bus" }] } }

    function test_gain_released_once_after_drag() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.gainReleased.connect(function (id, v) { got.push([id, v]) })
        mousePress(s.fader, 10, 100); mouseMove(s.fader, 10, 60, 0, Qt.LeftButton)
        compare(got.length, 0)
        mouseRelease(s.fader, 10, 60)
        compare(got.length, 1)
        compare(got[0][0], "t")
    }
    function test_mute_and_solo_emit_with_the_new_state() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.muteToggled.connect(function (id, on) { got.push(["m", id, on]) })
        s.soloToggled.connect(function (id, on) { got.push(["s", id, on]) })
        mouseClick(s.muteButton); mouseClick(s.soloButton)
        compare(got, [["m", "t", true], ["s", "t", true]])
    }
    function test_the_plus_slot_requests_an_insert() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.insertAddRequested.connect(function (id) { got.push(id) })
        mouseClick(s.addInsertSlot)
        compare(got, ["t"])
    }
    function test_dragging_an_insert_changes_its_gain_once_on_release() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.insertGainReleased.connect(function (id, index, db) { got.push([id, index, db]) })
        var slot = s.insertList.itemAt(0)
        mousePress(slot, 10, 10)
        mouseMove(slot, 40, 10, 0, Qt.LeftButton)
        compare(got.length, 0)
        mouseRelease(slot, 40, 10)
        compare(got.length, 1)
        compare(got[0][1], 0)
        verify(got[0][2] > 3)
    }
    function test_the_cross_of_an_insert_removes_it() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.insertRemoveRequested.connect(function (id, index) { got.push([id, index]) })
        var slot = s.insertList.itemAt(0)
        mouseMove(slot, 10, 10)
        mouseClick(slot, slot.width - 6, 10)
        compare(got, [["t", 0]])
    }
    function test_a_send_knob_release_sets_its_level_and_the_cross_removes_it() {
        var s = createTemporaryObject(stripC, this)
        var levels = [], removed = []
        s.sendLevelReleased.connect(function (id, db) { levels.push([id, db]) })
        s.sendRemoveRequested.connect(function (id) { removed.push(id) })
        var row = s.sendList.itemAt(0)
        row.knob.released(-6)
        compare(levels, [["s1", -6]])
        var slot = row.slot
        mouseMove(slot, 10, 10)
        mouseClick(slot, slot.width - 6, 10)
        compare(removed, ["s1"])
    }
    function test_output_and_send_menus_emit_the_chosen_target() {
        var s = createTemporaryObject(stripC, this)
        var out = [], snd = []
        s.outputRequested.connect(function (id, o) { out.push([id, o]) })
        s.sendAddRequested.connect(function (id, t) { snd.push([id, t]) })
        s.requestOutput("b"); s.requestOutput("")
        s.requestSend("b")
        compare(out, [["t", "b"], ["t", ""]])
        compare(snd, [["t", "b"]])
    }
    function test_slots_hide_when_asked_and_on_the_master() {
        var s = createTemporaryObject(stripC, this, { showSlots: false })
        verify(!s.addInsertSlot.visible)
        verify(!s.outputSlot.visible)
        var m = createTemporaryObject(stripC, this, { info: { trackId: "m", name: "Stereo Out", color: "purple", kind: "master", master: true, gainDb: 0, pan: 0, inserts: [], sends: [] } })
        verify(!m.addInsertSlot.visible)
    }
    function test_group_and_automation_slots_announce_themselves() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.stubUsed.connect(function (label) { got.push(label) })
        mouseClick(s.groupSlot)
        mouseClick(s.automationSlot)
        compare(got, ["Group", "Automation"])
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build-ui --config Debug --target jad_ui_qmltests` then run `jad_ui_qmltests -input ui/tests/tst_channelstrip.qml -import build-ui/ui` (with `QT_QPA_PLATFORM=offscreen` and the Qt `bin` on `PATH`).
Expected: FAIL (`ChannelStrip is not a type`).

- [ ] **Step 3: Icons**

Create the four SVGs (20x20 grid, stroke 1.5, `currentColor`, round caps and joins; drawn for this project):

`ui/qml/icons/track-audio.svg`:
```svg
<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M2 10h2.5l2-5 3 10 3-8 2 3H18"/></svg>
```
`ui/qml/icons/track-instrument.svg`:
```svg
<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><rect x="2.5" y="4.5" width="15" height="7" rx="1.5"/><path d="M6 11.5V8M10 11.5V8M14 11.5V8M6 11.5L4 17M14 11.5L16 17"/></svg>
```
`ui/qml/icons/track-aux.svg`:
```svg
<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="10" cy="10" r="6.5"/><path d="M10 3.5V8M6 14.5h8"/></svg>
```
`ui/qml/icons/track-bus.svg`:
```svg
<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M2.5 5h5l4 5h6M2.5 15h5l4-5"/></svg>
```

- [ ] **Step 4: Knob: reset value and fine drag**

In `ui/qml/Knob.qml` add `property real resetValue: 0` after `property real to: 1`, and replace the `onPositionChanged` and `onDoubleClicked` handlers with:

```qml
        onPositionChanged: (m) => {
            if (!pressed) return
            const span = root.to - root.from
            const travel = (m.modifiers & Qt.ShiftModifier) ? 800 : 200  // Shift: fine
            root.dragValue = Math.max(root.from, Math.min(root.to, startValue + (startY - m.y) / travel * span))
            root.moved(root.dragValue)
        }
        onReleased: { root.dragging = false; root.released(root.dragValue) }
        onDoubleClicked: { root.dragValue = root.resetValue; root.released(root.resetValue) }
```

- [ ] **Step 5: StripSlot, TrackIcon**

Create `ui/qml/StripSlot.qml`:

```qml
import QtQuick
import Jad

// One slot of a channel strip: a bar with a label, an optional value, a cross on hover when removable, and a
// horizontal drag (used for insert gain).
Rectangle {
    id: root
    property string text
    property string value
    property bool filled: false
    property color fillColor: Theme.accentPrimary
    property bool dim: false
    property bool removable: false
    readonly property bool hovered: area.containsMouse
    signal clicked()
    signal removeRequested()
    signal dragged(real dx)
    signal dragReleased()

    implicitHeight: 20
    implicitWidth: 84
    radius: Theme.radiusControl - 2
    color: filled ? fillColor : (area.containsMouse ? Theme.surfaceRaisedHover : Theme.surfaceRaised)
    border.color: Theme.borderSubtle
    opacity: dim ? 0.7 : 1

    Text {
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing[2]
        anchors.right: valueText.visible ? valueText.left : parent.right
        anchors.rightMargin: Theme.spacing[1]
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        elide: Text.ElideRight
        color: root.filled ? Theme.textPrimary : Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
        font.weight: Theme.fontTypeLabelWeight
    }
    Text {
        id: valueText
        visible: root.value !== "" && !(root.removable && area.containsMouse)
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing[2]
        anchors.verticalCenter: parent.verticalCenter
        text: root.value
        color: Theme.textValue
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
    }
    Text {
        visible: root.removable && area.containsMouse
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing[2]
        anchors.verticalCenter: parent.verticalCenter
        text: "×"
        color: Theme.textPrimary
        font.pixelSize: Theme.fontTypeBodySize
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        property real pressX: 0
        property bool moved: false
        onPressed: (m) => { pressX = m.x; moved = false }
        onPositionChanged: (m) => {
            if (!pressed) return
            if (Math.abs(m.x - pressX) > 3) moved = true
            if (moved) root.dragged(m.x - pressX)
        }
        onReleased: { if (moved) root.dragReleased() }
        onClicked: (m) => {
            if (moved) return
            if (root.removable && m.x > root.width - 16) root.removeRequested()
            else root.clicked()
        }
    }
}
```

Create `ui/qml/TrackIcon.qml`:

```qml
import QtQuick
import QtQuick.Effects
import Jad

// The glyph of a track kind (audio, instrument, aux, bus, master).
Item {
    id: root
    property string kind: "audio"
    property color tint: Theme.textPrimary
    property real size: Theme.sizeIcon
    readonly property string file: ({ "audio": "track-audio", "instrument": "track-instrument", "aux": "track-aux",
                                      "bus": "track-bus", "master": "track-bus" })[kind] ?? "track-audio"
    implicitWidth: size
    implicitHeight: size
    width: size
    height: size
    Image {
        id: glyph
        anchors.fill: parent
        source: "icons/" + root.file + ".svg"
        sourceSize: Qt.size(root.size * 2, root.size * 2)
        visible: false
    }
    MultiEffect {
        anchors.fill: glyph
        source: glyph
        brightness: 1.0
        colorization: 1.0
        colorizationColor: root.tint
    }
}
```

- [ ] **Step 6: ChannelStrip and ProjectStrip**

Create `ui/qml/ChannelStrip.qml`:

```qml
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQml.Models
import Jad

// One channel strip: Setting, instrument, inserts, sends, output, group and automation slots, pan, fader with meter,
// M and S, colour bar and name. A pure view: edits leave as signals (ProjectStrip wires them to the controller).
Panel {
    id: root
    property var info: ({})
    property var targets: []     // the buses and auxes a send or the output can go to: [{id, name}]
    property real peak: 0        // shown on the master strip only
    property bool showSlots: true

    readonly property string trackId: info.trackId ?? ""
    readonly property string trackName: info.name ?? ""
    readonly property string trackColor: info.color ?? "purple"
    readonly property string kind: info.kind ?? "audio"
    readonly property bool master: info.master ?? false
    readonly property real gainDb: info.gainDb ?? 0
    readonly property real pan: info.pan ?? 0
    readonly property bool mute: info.mute ?? false
    readonly property bool solo: info.solo ?? false
    readonly property var inserts: info.inserts ?? []
    readonly property var sends: info.sends ?? []
    readonly property bool slotsVisible: showSlots && !master
    readonly property string capitalColor: trackColor.charAt(0).toUpperCase() + trackColor.slice(1)

    readonly property alias fader: fader
    readonly property alias muteButton: muteButton
    readonly property alias soloButton: soloButton
    readonly property alias addInsertSlot: addInsertSlot
    readonly property alias outputSlot: outputSlot
    readonly property alias groupSlot: groupSlot
    readonly property alias automationSlot: automationSlot
    readonly property alias insertList: insertRepeater
    readonly property alias sendList: sendRepeater

    signal gainReleased(string id, real db)
    signal panReleased(string id, real pan)
    signal muteToggled(string id, bool on)
    signal soloToggled(string id, bool on)
    signal insertAddRequested(string id)
    signal insertRemoveRequested(string id, int index)
    signal insertGainReleased(string id, int index, real db)
    signal sendAddRequested(string id, string targetId)
    signal sendRemoveRequested(string sendId)
    signal sendLevelReleased(string sendId, real db)
    signal outputRequested(string id, string outputId)
    signal stubUsed(string label)

    function requestOutput(outputId) { outputRequested(trackId, outputId) }
    function requestSend(targetId) { sendAddRequested(trackId, targetId) }
    function insertLabel(processorId) { return processorId === "builtin.gain" ? qsTr("Gain") : processorId }

    property int dragIndex: -1
    property real dragGain: 0

    implicitWidth: 96
    radius: Theme.radiusRegion

    component TargetMenu: ThemedMenu {
        id: menu
        property bool withMaster: false
        property var targets: []
        signal chosen(string targetId)
        Instantiator {
            model: (menu.withMaster ? [{ id: "", name: qsTr("Stereo Out") }] : []).concat(menu.targets)
            delegate: ThemedMenuItem {
                required property var modelData
                text: modelData.name
                onTriggered: menu.chosen(modelData.id)
            }
            onObjectAdded: (index, object) => menu.insertItem(index, object)
            onObjectRemoved: (index, object) => menu.removeItem(object)
        }
    }
    TargetMenu { id: outputMenu; withMaster: true; targets: root.targets; onChosen: (id) => root.requestOutput(id) }
    TargetMenu { id: sendMenu; targets: root.targets; onChosen: (id) => root.requestSend(id) }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing[2]
        spacing: Theme.spacing[1]

        StripSlot {
            Layout.fillWidth: true
            visible: root.slotsVisible
            text: root.info.patchName && root.info.patchName !== "" ? root.info.patchName : qsTr("Setting")
            dim: true
            onClicked: root.stubUsed(qsTr("Setting"))
        }
        StripSlot {
            Layout.fillWidth: true
            visible: root.slotsVisible && root.kind === "instrument"
            text: root.info.instrument === "builtin.sine" ? qsTr("Sine") : (root.info.instrument ?? "")
            filled: true
            fillColor: Theme.statePlay
        }
        Repeater {
            id: insertRepeater
            model: root.slotsVisible ? root.inserts : []
            delegate: StripSlot {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                text: root.insertLabel(modelData.processorId)
                value: (root.dragIndex === index ? root.dragGain : modelData.gainDb).toFixed(1)
                filled: true
                removable: true
                onRemoveRequested: root.insertRemoveRequested(root.trackId, index)
                onDragged: (dx) => {
                    root.dragIndex = index
                    root.dragGain = Math.max(-96, Math.min(24, modelData.gainDb + dx * 0.1))
                }
                onDragReleased: {
                    const db = root.dragGain
                    root.dragIndex = -1
                    root.insertGainReleased(root.trackId, index, db)
                }
            }
        }
        StripSlot {
            id: addInsertSlot
            Layout.fillWidth: true
            visible: root.slotsVisible
            text: "+"
            onClicked: root.insertAddRequested(root.trackId)
        }
        Repeater {
            id: sendRepeater
            model: root.slotsVisible ? root.sends : []
            delegate: RowLayout {
                id: sendRow
                required property var modelData
                property alias knob: sendKnob
                property alias slot: sendSlot
                Layout.fillWidth: true
                spacing: Theme.spacing[1]
                StripSlot {
                    id: sendSlot
                    Layout.fillWidth: true
                    text: sendRow.modelData.targetName
                    filled: true
                    fillColor: Theme.accentPrimaryHover
                    removable: true
                    onRemoveRequested: root.sendRemoveRequested(sendRow.modelData.id)
                }
                Knob {
                    id: sendKnob
                    width: 20
                    height: 20
                    from: -60
                    to: 12
                    resetValue: 0
                    value: sendRow.modelData.levelDb
                    onReleased: (v) => root.sendLevelReleased(sendRow.modelData.id, v)
                }
            }
        }
        StripSlot {
            id: addSendSlot
            Layout.fillWidth: true
            visible: root.slotsVisible && root.targets.length > 0
            text: qsTr("Send +")
            onClicked: sendMenu.popup(addSendSlot, 0, addSendSlot.height)
        }
        Item { Layout.fillHeight: true; Layout.minimumHeight: 0 }
        StripSlot {
            id: outputSlot
            Layout.fillWidth: true
            visible: root.slotsVisible
            text: root.info.outputName && root.info.outputName !== "" ? root.info.outputName : qsTr("Stereo Out")
            onClicked: outputMenu.popup(outputSlot, 0, outputSlot.height)
        }
        RowLayout {
            Layout.fillWidth: true
            visible: root.slotsVisible
            spacing: Theme.spacing[1]
            StripSlot { id: groupSlot; Layout.fillWidth: true; text: qsTr("Group"); dim: true; onClicked: root.stubUsed("Group") }
            StripSlot { id: automationSlot; Layout.fillWidth: true; text: qsTr("Read"); dim: true; onClicked: root.stubUsed("Automation") }
        }
        Knob {
            Layout.alignment: Qt.AlignHCenter
            value: root.pan
            onReleased: (v) => root.panReleased(root.trackId, v)
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.spacing[2]
            IconButton {
                id: muteButton
                implicitWidth: Theme.sizeControlCompact
                implicitHeight: Theme.sizeControlCompact
                source: "icons/mute.svg"
                active: root.mute
                onClicked: root.muteToggled(root.trackId, !root.mute)
            }
            IconButton {
                id: soloButton
                implicitWidth: Theme.sizeControlCompact
                implicitHeight: Theme.sizeControlCompact
                source: "icons/solo.svg"
                active: root.solo
                onClicked: root.soloToggled(root.trackId, !root.solo)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            spacing: Theme.spacing[3]
            Item { Layout.fillWidth: true }
            Fader {
                id: fader
                Layout.preferredWidth: 28
                Layout.fillHeight: true
                value: root.gainDb
                onReleased: (v) => root.gainReleased(root.trackId, v)
            }
            Meter {
                visible: root.master
                Layout.preferredWidth: 8
                Layout.fillHeight: true
                peak: root.peak
            }
            Item { Layout.fillWidth: true }
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: root.gainDb <= -96 ? "-∞" : root.gainDb.toFixed(1)
            color: Theme.textValue
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 3
            radius: 1
            color: Theme["track" + root.capitalColor + "Solid"]
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: root.trackName
            elide: Text.ElideRight
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeLabelSize
            font.weight: Theme.fontTypeLabelWeight
        }
    }
}
```

Create `ui/qml/ProjectStrip.qml`:

```qml
import QtQuick
import Jad

// A ChannelStrip wired to the controller: every edit is a command.
ChannelStrip {
    id: root
    required property ProjectController project
    targets: project.inspector.busTargets.filter((t) => t.id !== root.trackId)

    onGainReleased: (id, db) => project.setGain(id, db)
    onPanReleased: (id, pan) => project.setPan(id, pan)
    onMuteToggled: (id, on) => project.setMute(id, on)
    onSoloToggled: (id, on) => project.setSolo(id, on)
    onInsertAddRequested: (id) => project.addInsert(id, "builtin.gain")
    onInsertRemoveRequested: (id, index) => project.removeInsert(id, index)
    onInsertGainReleased: (id, index, db) => project.setInsertParam(id, index, "gainDb", db)
    onSendAddRequested: (id, targetId) => project.addSend(id, targetId)
    onSendRemoveRequested: (sendId) => project.removeSend(sendId)
    onSendLevelReleased: (sendId, db) => project.setSendLevel(sendId, db)
    onOutputRequested: (id, outputId) => project.setOutput(id, outputId)
    onStubUsed: (label) => project.announceStub(label)
}
```

- [ ] **Step 7: Register the files**

In `ui/CMakeLists.txt` add to `QML_FILES`: `qml/StripSlot.qml qml/ChannelStrip.qml qml/ProjectStrip.qml qml/TrackIcon.qml`; to `RESOURCES`: `qml/icons/track-audio.svg qml/icons/track-instrument.svg qml/icons/track-aux.svg qml/icons/track-bus.svg`. Add `tst_channelstrip.qml` is picked up automatically by `-input` (the folder).

- [ ] **Step 8: Run the tests**

Run: `cmake --build build-ui --config Debug` then `ctest --test-dir build-ui -C Debug --output-on-failure`
Expected: all PASS including `ChannelStrip` (9 functions). If `test_dragging_an_insert...` fails because no move event reached the `MouseArea`, keep the explicit `Qt.LeftButton` argument of `mouseMove` (it is what marks the button as held).

- [ ] **Step 9: Commit**

```bash
git add ui
git commit -m "feat(ui): channel strip with insert, send and output slots, track icons"
```

---

### Task 6: Left column and the Inspector

**Files:**
- Create: `ui/qml/PanelHeader.qml`, `ui/qml/InspectorRow.qml`, `ui/qml/NumberField.qml`, `ui/qml/StubCheck.qml`, `ui/qml/StubValue.qml`, `ui/qml/Splitter.qml`, `ui/qml/LeftColumn.qml`, `ui/qml/Inspector.qml`, `ui/qml/RegionInspector.qml`, `ui/qml/TrackInspector.qml`, `ui/tests/tst_inspector.qml`, `docs/images/inspector.png`
- Modify: `ui/qml/Main.qml`, `ui/main.cpp`, `ui/CMakeLists.txt`, `README.md`

**Interfaces:**
- Consumes: `ProjectStrip`, `TrackIcon`, `project.inspector`, controller wrappers (Tasks 4 and 5).
- Produces: `LeftColumn` (`required property ProjectController project`; hosts Library and Inspector, width `project.leftColumnWidth`), `Inspector`, `RegionInspector`, `TrackInspector` (alias `nameField`, function `commitName(text)`), `PanelHeader` (`title`, `expanded`, `toggled()`), `InspectorRow` (`label`, default content slot), `NumberField` (`value`, `suffix`, `committed(real)`), `StubCheck` / `StubValue` (`label`, `project`).
- Produces (`main.cpp` options): `--panels <list>` (comma-separated `library`, `inspector`, `smart`; shown for `--screenshot`, others hidden), `--select-region <n>` (1-based).

- [ ] **Step 1: Write the failing QML tests**

Create `ui/tests/tst_inspector.qml`:

```qml
import QtQuick
import QtTest
import Jad

TestCase {
    id: tc
    name: "Inspector"
    width: 600; height: 900
    visible: true
    when: windowShown

    ProjectController { id: p; audioEnabled: false }
    Component { id: inspC; Inspector { width: 240; height: 880; project: p } }

    function init() {
        verify(p.newProjectInTempForTest())
        p.addTrack("audio")
        p.addTrack("instrument")
        tryVerify(function () { return p.tracks.rowCount() >= 2 })
    }

    function test_it_says_so_when_nothing_is_selected() {
        var i = createTemporaryObject(inspC, tc)
        verify(i.emptyLabel.visible)
        verify(!i.trackSection.visible)
        verify(!i.regionSection.visible)
    }
    function test_it_shows_the_selected_track_and_renames_it() {
        var i = createTemporaryObject(inspC, tc)
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return i.trackSection.visible })
        verify(!i.emptyLabel.visible)
        i.trackSection.commitName("Lead")
        tryVerify(function () { return p.inspector.track.name === "Lead" })
    }
    function test_a_colour_swatch_recolours_the_track() {
        var i = createTemporaryObject(inspC, tc)
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return i.trackSection.visible })
        i.trackSection.swatchList.itemAt(6).clicked()   // orange
        tryVerify(function () { return p.inspector.track.color === "orange" })
    }
    function test_the_region_section_edits_the_gain_of_the_region() {
        var i = createTemporaryObject(inspC, tc)
        var id = p.tracks.trackIdAt(1)
        p.createRegion(id, 0, 4)
        tryVerify(function () { return p.regions.rowCount() > 0 })
        p.selectRegion(p.regions.regionIdAt(0), "replace")
        tryVerify(function () { return i.regionSection.visible })
        i.regionSection.gainField.committed(-6)
        tryVerify(function () { return p.inspector.region.gainDb === -6 })
    }
    function test_visual_only_fields_announce_themselves_when_switched_on() {
        var i = createTemporaryObject(inspC, tc)
        var id = p.tracks.trackIdAt(1)
        p.createRegion(id, 0, 4)
        tryVerify(function () { return p.regions.rowCount() > 0 })
        p.selectRegion(p.regions.regionIdAt(0), "replace")
        tryVerify(function () { return i.regionSection.visible })
        var got = []
        p.notice.connect(function (m) { got.push(m) })
        mouseClick(i.regionSection.loopCheck)
        compare(got.length, 1)
        verify(got[0].indexOf("not implemented yet") >= 0)
        mouseClick(i.regionSection.loopCheck)   // off: no notice
        compare(got.length, 1)
    }
    function test_the_sections_collapse() {
        var i = createTemporaryObject(inspC, tc)
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return i.trackSection.visible })
        var h = i.trackSection.header
        verify(h.expanded)
        mouseClick(h)
        verify(!h.expanded)
        verify(!i.trackSection.body.visible)
    }
    function test_the_two_strips_show_the_track_and_its_output() {
        var i = createTemporaryObject(inspC, tc)
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return i.trackStrip.visible && i.outputStrip.visible })
        compare(i.trackStrip.trackId, p.tracks.trackIdAt(0))
        verify(i.outputStrip.master)
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build-ui --config Debug --target jad_ui_qmltests` and the QML runner on `ui/tests/tst_inspector.qml`.
Expected: FAIL (`Inspector is not a type`).

- [ ] **Step 3: Small building blocks**

Create `ui/qml/PanelHeader.qml`:

```qml
import QtQuick
import Jad

// A collapsible section header: chevron and title.
Rectangle {
    id: root
    property string title
    property bool expanded: true
    signal toggled()
    implicitHeight: 24
    color: Theme.surfaceRaised
    Text {
        id: chevron
        x: Theme.spacing[3]
        anchors.verticalCenter: parent.verticalCenter
        text: root.expanded ? "▾" : "▸"
        color: Theme.textSecondary
        font.pixelSize: Theme.fontTypeBodySize
    }
    Text {
        anchors.left: chevron.right
        anchors.leftMargin: Theme.spacing[3]
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        text: root.title
        elide: Text.ElideRight
        color: Theme.textPrimary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
        font.weight: Theme.fontTypeLabelWeight
    }
    MouseArea {
        anchors.fill: parent
        onClicked: { root.expanded = !root.expanded; root.toggled() }
    }
}
```

Create `ui/qml/InspectorRow.qml`:

```qml
import QtQuick
import Jad

// A label on the left, the control on the right.
Item {
    id: root
    property string label
    default property alias content: slot.data
    implicitHeight: 24
    width: parent ? parent.width : 200
    Text {
        x: Theme.spacing[3]
        width: parent.width * 0.42
        anchors.verticalCenter: parent.verticalCenter
        text: root.label
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignRight
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
    }
    Item {
        id: slot
        x: parent.width * 0.42 + Theme.spacing[4]
        width: parent.width - x - Theme.spacing[3]
        height: parent.height
    }
}
```

Create `ui/qml/NumberField.qml`:

```qml
import QtQuick
import Jad

// An editable number: shows `value` with one decimal and `suffix`; Enter or focus loss commits a valid number.
Rectangle {
    id: root
    property real value: 0
    property string suffix: ""
    property real from: -96
    property real to: 24
    signal committed(real value)
    implicitWidth: 72
    implicitHeight: 20
    radius: Theme.radiusControl - 2
    color: input.activeFocus ? Theme.surfaceRaisedHover : Theme.surfaceRaised
    border.color: input.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing[2]
        anchors.rightMargin: Theme.spacing[2]
        verticalAlignment: TextInput.AlignVCenter
        text: root.value.toFixed(1) + root.suffix
        color: Theme.textValue
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
        selectByMouse: true
        onActiveFocusChanged: if (activeFocus) selectAll()
        onEditingFinished: {
            const parsed = parseFloat(text.replace(root.suffix, "").replace(",", "."))
            if (isFinite(parsed)) root.committed(Math.max(root.from, Math.min(root.to, parsed)))
            else text = Qt.binding(function () { return root.value.toFixed(1) + root.suffix })
        }
    }
}
```

Create `ui/qml/StubCheck.qml`:

```qml
import QtQuick
import Jad

// A checkbox with no engine behind it: it flips, and announces itself when switched on.
Rectangle {
    id: root
    required property ProjectController project
    property string label
    property bool on: false
    implicitWidth: 16
    implicitHeight: 16
    radius: 3
    color: on ? Theme.accentPrimary : Theme.surfaceRaised
    border.color: Theme.borderStrong
    MouseArea {
        anchors.fill: parent
        onClicked: {
            root.on = !root.on
            if (root.on) root.project.announceStub(root.label)
        }
    }
}
```

Create `ui/qml/StubValue.qml`:

```qml
import QtQuick
import Jad

// A value field with no engine behind it: a click announces it.
Rectangle {
    id: root
    required property ProjectController project
    property string label
    property string text
    implicitWidth: 72
    implicitHeight: 20
    radius: Theme.radiusControl - 2
    color: area.containsMouse ? Theme.surfaceRaisedHover : Theme.surfaceRaised
    opacity: 0.8
    Text {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing[2]
        verticalAlignment: Text.AlignVCenter
        text: root.text
        elide: Text.ElideRight
        color: Theme.textValue
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
    }
    MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; onClicked: root.project.announceStub(root.label) }
}
```

Create `ui/qml/Splitter.qml`:

```qml
import QtQuick
import Jad

// A thin drag handle on the right edge of the left column; `dragged(dx)` is the horizontal movement since the last event.
Rectangle {
    id: root
    signal dragged(real dx)
    width: 5
    color: area.containsMouse || area.pressed ? Theme.accentPrimary : "transparent"
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.SplitHCursor
        property real lastX: 0
        onPressed: (m) => { lastX = mapToItem(null, m.x, 0).x }
        onPositionChanged: (m) => {
            if (!pressed) return
            const x = mapToItem(null, m.x, 0).x
            root.dragged(x - lastX)
            lastX = x
        }
    }
}
```

- [ ] **Step 4: RegionInspector and TrackInspector**

Create `ui/qml/RegionInspector.qml`:

```qml
import QtQuick
import Jad

// "Region: ..." section of the Inspector. Gain is real; the rest has no playback support yet (visual only).
Column {
    id: root
    required property ProjectController project
    property var region: ({})
    readonly property alias header: head
    readonly property alias body: content
    readonly property alias gainField: gain
    readonly property alias loopCheck: loop

    PanelHeader {
        id: head
        width: parent.width
        title: qsTr("Region: %1").arg(root.region.audio ? qsTr("Audio") : qsTr("MIDI"))
    }
    Column {
        id: content
        width: parent.width
        visible: head.expanded
        topPadding: Theme.spacing[2]
        bottomPadding: Theme.spacing[2]
        InspectorRow { label: qsTr("Mute"); StubCheck { project: root.project; label: qsTr("Region Mute"); anchors.verticalCenter: parent.verticalCenter } }
        InspectorRow { label: qsTr("Loop"); StubCheck { id: loop; project: root.project; label: qsTr("Region Loop"); anchors.verticalCenter: parent.verticalCenter } }
        InspectorRow { label: qsTr("Quantize"); StubValue { project: root.project; label: qsTr("Quantize"); text: qsTr("Off") } }
        InspectorRow { label: qsTr("Transpose"); StubValue { project: root.project; label: qsTr("Region Transpose"); text: "0" } }
        InspectorRow { label: qsTr("Velocity"); StubValue { project: root.project; label: qsTr("Region Velocity"); text: "0" } }
        InspectorRow {
            label: qsTr("Gain")
            NumberField {
                id: gain
                anchors.verticalCenter: parent.verticalCenter
                value: root.region.gainDb ?? 0
                suffix: " dB"
                onCommitted: (v) => root.project.setRegionGain(root.region.regionId, v)
            }
        }
    }
}
```

Create `ui/qml/TrackInspector.qml`:

```qml
import QtQuick
import Jad

// "Track: ..." section of the Inspector. Name and colour are real; the other fields have no engine yet (visual only).
Column {
    id: root
    required property ProjectController project
    property var track: ({})
    readonly property alias header: head
    readonly property alias body: content
    readonly property alias nameField: nameInput
    readonly property alias swatchList: swatches
    readonly property var palette: ["purple", "indigo", "blue", "teal", "green", "yellow", "orange", "red", "pink", "magenta"]
    function commitName(text) {
        const t = text.trim()
        if (t !== "" && t !== (track.name ?? "")) project.renameTrack(track.trackId, t)
    }

    PanelHeader {
        id: head
        width: parent.width
        title: qsTr("Track: %1").arg(root.track.name ?? "")
    }
    Column {
        id: content
        width: parent.width
        visible: head.expanded
        topPadding: Theme.spacing[2]
        bottomPadding: Theme.spacing[2]
        InspectorRow {
            label: qsTr("Name")
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 20
                radius: Theme.radiusControl - 2
                color: Theme.surfaceRaised
                border.color: nameInput.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                TextInput {
                    id: nameInput
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing[2]
                    anchors.rightMargin: Theme.spacing[2]
                    verticalAlignment: TextInput.AlignVCenter
                    text: root.track.name ?? ""
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeLabelSize
                    selectByMouse: true
                    maximumLength: 64
                    onEditingFinished: root.commitName(text)
                }
            }
        }
        InspectorRow {
            label: qsTr("Icon")
            MouseArea {
                anchors.verticalCenter: parent.verticalCenter
                width: 24
                height: 24
                onClicked: root.project.announceStub(qsTr("Track Icon"))
                TrackIcon { anchors.centerIn: parent; kind: root.track.kind ?? "audio"; tint: Theme["track" + (root.track.color ?? "purple").charAt(0).toUpperCase() + (root.track.color ?? "purple").slice(1) + "Solid"] }
            }
        }
        InspectorRow {
            label: qsTr("Color")
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                Repeater {
                    id: swatches
                    model: root.palette
                    delegate: Rectangle {
                        id: swatch
                        required property string modelData
                        signal clicked()
                        width: 11
                        height: 11
                        radius: 3
                        color: Theme["track" + modelData.charAt(0).toUpperCase() + modelData.slice(1) + "Solid"]
                        border.width: (root.track.color ?? "") === modelData ? 2 : 0
                        border.color: Theme.textPrimary
                        onClicked: root.project.setTrackColor(root.track.trackId, modelData)
                        MouseArea { anchors.fill: parent; onClicked: swatch.clicked() }
                    }
                }
            }
        }
        Column {
            width: parent.width
            visible: root.track.kind === "instrument"
            InspectorRow { label: qsTr("Region type"); StubValue { project: root.project; label: qsTr("Default Region Type"); text: qsTr("MIDI") } }
            InspectorRow { label: qsTr("Transpose"); StubValue { project: root.project; label: qsTr("Track Transpose"); text: "0" } }
            InspectorRow { label: qsTr("Velocity"); StubValue { project: root.project; label: qsTr("Track Velocity"); text: "0" } }
            InspectorRow { label: qsTr("Key limit"); StubValue { project: root.project; label: qsTr("Key Limit"); text: "C-2  G8" } }
            InspectorRow { label: qsTr("Velocity limit"); StubValue { project: root.project; label: qsTr("Velocity Limit"); text: "1  127" } }
            InspectorRow { label: qsTr("Delay"); StubValue { project: root.project; label: qsTr("Track Delay"); text: "0" } }
            InspectorRow { label: qsTr("No transpose"); StubCheck { project: root.project; label: qsTr("No Transpose"); anchors.verticalCenter: parent.verticalCenter } }
        }
        Column {
            width: parent.width
            visible: root.track.kind === "audio"
            InspectorRow { label: qsTr("Freeze mode"); StubValue { project: root.project; label: qsTr("Freeze Mode"); text: qsTr("Pre Fader") } }
            InspectorRow { label: qsTr("Q-reference"); StubCheck { project: root.project; label: qsTr("Q-Reference"); anchors.verticalCenter: parent.verticalCenter } }
        }
    }
}
```

- [ ] **Step 5: Inspector, LeftColumn**

Create `ui/qml/Inspector.qml`:

```qml
import QtQuick
import Jad

// The Inspector: Region and Track sections and the channel strips of the selected track and its output.
Rectangle {
    id: root
    required property ProjectController project
    readonly property var insp: project.inspector
    readonly property alias emptyLabel: empty
    readonly property alias regionSection: region
    readonly property alias trackSection: trackSec
    readonly property alias trackStrip: trackStrip
    readonly property alias outputStrip: outputStrip
    color: Theme.surfacePanel
    clip: true

    Flickable {
        anchors.fill: parent
        contentHeight: col.height
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: col
            width: parent.width
            Text {
                id: empty
                visible: !root.insp.hasTrack && !root.insp.hasRegion
                width: parent.width
                height: 80
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: qsTr("No track selected")
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeBodySize
            }
            RegionInspector { id: region; width: parent.width; visible: root.insp.hasRegion; project: root.project; region: root.insp.region }
            TrackInspector { id: trackSec; width: parent.width; visible: root.insp.hasTrack; project: root.project; track: root.insp.track }
            Row {
                visible: root.insp.hasTrack
                width: parent.width
                height: 520
                padding: Theme.spacing[2]
                spacing: Theme.spacing[2]
                ProjectStrip {
                    id: trackStrip
                    width: (col.width - Theme.spacing[2] * 3) / 2
                    height: parent.height - Theme.spacing[2] * 2
                    project: root.project
                    info: root.insp.track
                }
                ProjectStrip {
                    id: outputStrip
                    visible: root.insp.hasTrack && Object.keys(root.insp.output).length > 0
                    width: (col.width - Theme.spacing[2] * 3) / 2
                    height: parent.height - Theme.spacing[2] * 2
                    project: root.project
                    info: root.insp.output
                    showSlots: false
                    peak: info.master ? root.project.masterPeak : 0
                }
            }
        }
    }
}
```

Create `ui/qml/LeftColumn.qml` (the Library arrives in Task 7; until then the file holds only the Inspector; Task 7 step 5 adds the Library above it):

```qml
import QtQuick
import Jad

// The left column of the window: Library above Inspector, either can be hidden. Resizable on its right edge.
Rectangle {
    id: root
    required property ProjectController project
    readonly property alias inspector: inspector
    color: Theme.surfacePanel
    visible: project.inspectorVisible || project.libraryVisible
    width: visible ? project.leftColumnWidth : 0

    Inspector {
        id: inspector
        anchors.fill: parent
        anchors.rightMargin: 5
        visible: root.project.inspectorVisible
        project: root.project
    }
    Splitter {
        anchors.right: parent.right
        height: parent.height
        onDragged: (dx) => root.project.leftColumnWidth = root.project.leftColumnWidth + dx
    }
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.borderSubtle }
}
```

- [ ] **Step 6: Put the column in the window**

In `ui/qml/Main.qml` inside the `RowLayout` that holds `TrackList` and `Timeline`, add before `TrackList`:

```qml
            LeftColumn {
                id: leftColumn
                Layout.fillHeight: true
                Layout.preferredWidth: width
                project: controller
            }
```

- [ ] **Step 7: Register files, screenshot options**

In `ui/CMakeLists.txt` add to `QML_FILES`: `qml/PanelHeader.qml qml/InspectorRow.qml qml/NumberField.qml qml/StubCheck.qml qml/StubValue.qml qml/Splitter.qml qml/LeftColumn.qml qml/Inspector.qml qml/RegionInspector.qml qml/TrackInspector.qml`.

In `ui/main.cpp` add two options next to the others:

```cpp
    QCommandLineOption panelsOption("panels", "Panels shown for --screenshot, comma separated: library, inspector, smart.", "list");
    QCommandLineOption selectRegionOption("select-region", "Select region number <n> (1-based) for --screenshot.", "n");
    parser.addOption(panelsOption);
    parser.addOption(selectRegionOption);
```

capture `&panelsOption, &selectRegionOption` in the timer lambda and add inside it (after the select-track block):

```cpp
            if (controller && parser.isSet(panelsOption)) {
                const QStringList shown = parser.value(panelsOption).split(',', Qt::SkipEmptyParts);
                controller->setLibraryVisible(shown.contains("library"));
                controller->setInspectorVisible(shown.contains("inspector"));
                controller->setSmartControlsVisible(shown.contains("smart"));
            }
            if (controller && parser.isSet(selectRegionOption))
                controller->selectRegion(controller->regions()->regionIdAt(parser.value(selectRegionOption).toInt() - 1), "replace");
```

(extend the lambda capture list accordingly). In `Main.qml` nothing else is needed.

- [ ] **Step 8: Run all UI tests**

Run: `cmake --build build-ui --config Debug` then `ctest --test-dir build-ui -C Debug --output-on-failure`
Expected: all PASS, `Inspector` included (7 functions). The existing tests must still pass: the default Inspector panel is now visible, check `tst_shell` / `tst_timeline*` still fit; fix expectations that assumed the old layout only if they assert widths.

- [ ] **Step 9: Screenshot and README**

```bash
build/tools/lpc-cli/Debug/lpc-cli.exe demo scratch/demo.lpc
build-ui/ui/Debug/jad-daw.exe --project scratch/demo.lpc --no-audio --screenshot docs/images/inspector.png --size 1280x800 --select-track 1 --select-region 1 --panels inspector
```

Open `docs/images/inspector.png` and check: Region and Track sections, two strips, no clipped text. Then in `README.md` under "Window and shortcuts" add after the tools screenshot:

```markdown
- **Inspector** (`I`, or the Inspector button): the Region and Track sections of the selected region and track, and two
  channel strips (the track and its output). Real: region gain, track name and colour, and everything on the strips
  (insert gain, sends, output, pan, fader, mute, solo). Quantize, Loop, Transpose, Key and Velocity limits, Delay and the
  like have no engine yet: they are visual only and say so when switched on.

![Inspector](docs/images/inspector.png)
```

- [ ] **Step 10: Commit**

```bash
git add ui README.md docs/images/inspector.png
git commit -m "feat(ui): left column and Inspector with region, track and channel strips"
```

---

### Task 7: Library

**Files:**
- Create: `ui/qml/Library.qml`, `ui/tests/tst_library.qml`, `docs/images/library.png`
- Modify: `ui/qml/LeftColumn.qml`, `ui/CMakeLists.txt`, `README.md`

**Interfaces:**
- Consumes: `project.library` (`categories, category, search, patches, currentPatchId, problems, patchCount, neighbour`), `project.applyPatch`, `project.revertPatch`, `project.inspector.track`, `TrackIcon`.
- Produces: `Library` (`required property ProjectController project`; aliases `searchField, categoryList, patchList`; function `step(n)` applies the neighbouring patch).

- [ ] **Step 1: Write the failing QML tests**

Create `ui/tests/tst_library.qml`:

```qml
import QtQuick
import QtTest
import Jad

TestCase {
    id: tc
    name: "Library"
    width: 400; height: 700
    visible: true
    when: windowShown

    ProjectController { id: p; audioEnabled: false }
    Component { id: libC; Library { width: 240; height: 680; project: p } }

    function init() {
        verify(p.newProjectInTempForTest())
        p.addTrack("audio")
        tryVerify(function () { return p.tracks.rowCount() >= 1 })
    }

    function test_it_asks_for_a_track_when_none_is_selected() {
        var l = createTemporaryObject(libC, tc)
        verify(l.emptyLabel.visible)
    }
    function test_it_lists_the_categories_and_patches_of_the_track_kind() {
        var l = createTemporaryObject(libC, tc)
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return l.categoryList.count >= 2 })
        verify(l.patchList.count >= 2)
        verify(!l.emptyLabel.visible)
    }
    function test_clicking_a_patch_applies_it_and_it_becomes_current() {
        var l = createTemporaryObject(libC, tc)
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return l.patchList.count >= 2 })
        l.patchList.itemAtIndex(1).clicked()
        tryVerify(function () { return p.library.currentPatchId !== "" })
        tryVerify(function () { return p.inspector.track.patchId === p.library.currentPatchId })
    }
    function test_arrow_keys_step_through_the_patches() {
        var l = createTemporaryObject(libC, tc)
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return l.patchList.count >= 2 })
        l.step(1)
        tryVerify(function () { return p.library.currentPatchId !== "" })
        var first = p.library.currentPatchId
        l.step(1)
        tryVerify(function () { return p.library.currentPatchId !== first })
        l.step(-1)
        tryVerify(function () { return p.library.currentPatchId === first })
    }
    function test_search_filters_and_revert_restores_the_patch() {
        var l = createTemporaryObject(libC, tc)
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
        tryVerify(function () { return l.patchList.count >= 2 })
        l.searchField.text = "guitar"
        tryVerify(function () { return l.patchList.count === 2 })
        l.patchList.itemAtIndex(0).clicked()
        tryVerify(function () { return p.inspector.track.patchId === "audio.warm-guitar" })
        p.setGain(p.tracks.trackIdAt(0), 5)
        tryVerify(function () { return p.inspector.track.gainDb === 5 })
        l.revertButton.clicked()
        tryVerify(function () { return p.inspector.track.gainDb === -4 })
    }
    function test_save_and_delete_are_shown_but_say_they_are_not_there_yet() {
        var l = createTemporaryObject(libC, tc)
        var got = []
        p.notice.connect(function (m) { got.push(m) })
        mouseClick(l.saveButton)
        mouseClick(l.deleteButton)
        compare(got.length, 2)
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build-ui --config Debug --target jad_ui_qmltests` and the runner on `ui/tests/tst_library.qml`.
Expected: FAIL (`Library is not a type`).

- [ ] **Step 3: Write Library.qml**

Create `ui/qml/Library.qml`:

```qml
import QtQuick
import QtQuick.Layouts
import Jad

// The Library: the patches of the selected track's kind. Categories on the left, patches on the right; clicking a
// patch or stepping with Up/Down applies it to the track; Revert applies the track's patch again.
Rectangle {
    id: root
    required property ProjectController project
    readonly property var lib: project.library
    readonly property alias emptyLabel: empty
    readonly property alias searchField: search
    readonly property alias categoryList: categories
    readonly property alias patchList: patches
    readonly property alias revertButton: revert
    readonly property alias saveButton: save
    readonly property alias deleteButton: del
    function step(n) {
        const id = lib.neighbour(n)
        if (id !== "") project.applyPatch(id)
    }
    color: Theme.surfacePanel
    clip: true
    focus: true
    activeFocusOnTab: true
    Keys.onUpPressed: step(-1)
    Keys.onDownPressed: step(1)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            color: Theme.surfaceRaised
            Text {
                anchors.centerIn: parent
                text: qsTr("Library")
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeLabelSize
                font.weight: Theme.fontTypeLabelWeight
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 84
            visible: root.project.inspector.hasTrack
            TrackIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                y: Theme.spacing[3]
                size: 36
                kind: root.project.inspector.track.kind ?? "audio"
                tint: Theme["track" + (root.project.inspector.track.color ?? "purple").charAt(0).toUpperCase() + (root.project.inspector.track.color ?? "purple").slice(1) + "Solid"]
            }
            Text {
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.spacing[2]
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: root.project.inspector.track.patchName && root.project.inspector.track.patchName !== ""
                      ? root.project.inspector.track.patchName : (root.project.inspector.track.name ?? "")
                elide: Text.ElideRight
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeLabelSize
            }
        }
        Text {
            id: empty
            visible: !root.project.inspector.hasTrack
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: qsTr("Select a track to see its patches")
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeBodySize
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.margins: Theme.spacing[2]
            Layout.preferredHeight: 22
            visible: root.project.inspector.hasTrack
            radius: Theme.radiusControl
            color: Theme.surfaceRaised
            border.color: search.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
            TextInput {
                id: search
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing[3]
                anchors.rightMargin: Theme.spacing[3]
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTypeLabelSize
                selectByMouse: true
                onTextChanged: root.lib.search = text
                Text {
                    visible: !search.text && !search.activeFocus
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Search Patches")
                    color: Theme.textDisabled
                    font: search.font
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.project.inspector.hasTrack
            spacing: 0
            ListView {
                id: categories
                Layout.preferredWidth: parent.width * 0.42
                Layout.fillHeight: true
                clip: true
                model: root.lib.categories
                delegate: Rectangle {
                    required property string modelData
                    width: ListView.view.width
                    height: 22
                    color: modelData === root.lib.category && root.lib.search === "" ? Theme.surfaceRaisedHover : "transparent"
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacing[3]
                        verticalAlignment: Text.AlignVCenter
                        text: modelData
                        elide: Text.ElideRight
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeLabelSize
                    }
                    MouseArea { anchors.fill: parent; onClicked: { search.text = ""; root.lib.category = modelData } }
                }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.borderSubtle }
            ListView {
                id: patches
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.lib.patches
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    signal clicked()
                    width: ListView.view.width
                    height: 22
                    color: modelData.id === root.lib.currentPatchId ? Theme.accentPrimary : (area.containsMouse ? Theme.surfaceRaisedHover : "transparent")
                    onClicked: { root.forceActiveFocus(); root.project.applyPatch(modelData.id) }
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacing[3]
                        verticalAlignment: Text.AlignVCenter
                        text: row.modelData.name
                        elide: Text.ElideRight
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeLabelSize
                    }
                    MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; onClicked: row.clicked() }
                }
                Text {
                    anchors.centerIn: parent
                    visible: patches.count === 0
                    text: root.lib.problems.length > 0 ? qsTr("Catalogue problems: %1").arg(root.lib.problems.length) : qsTr("No patches match")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeLabelSize
                }
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacing[3]
            text: qsTr("Built-in patches: %1").arg(root.lib.patchCount)
            color: Theme.textDisabled
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTypeCaptionSize
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacing[2]
            spacing: Theme.spacing[2]
            IconButton { implicitWidth: 28; implicitHeight: 22; label: "⋯"; enabled: false }
            IconButton { id: revert; implicitHeight: 22; label: qsTr("Revert"); onClicked: root.project.revertPatch() }
            Item { Layout.fillWidth: true }
            IconButton { id: del; implicitHeight: 22; label: qsTr("Delete"); opacity: 0.5; onClicked: root.project.announceStub(qsTr("Delete Patch")) }
            IconButton { id: save; implicitHeight: 22; label: qsTr("Save…"); opacity: 0.5; onClicked: root.project.announceStub(qsTr("Save Patch")) }
        }
    }
}
```

(the `⋯` button is disabled: Options are out of scope; `IconButton.opacity` is overridden by `enabled`, set `enabled: false`.)

- [ ] **Step 4: Put the Library above the Inspector**

Replace the body of `ui/qml/LeftColumn.qml` below the `visible/width` lines with the stacked version:

```qml
    Item {
        id: stack
        anchors.fill: parent
        anchors.rightMargin: 5
        Library {
            id: library
            width: parent.width
            height: root.project.inspectorVisible ? parent.height * 0.45 : parent.height
            visible: root.project.libraryVisible
            project: root.project
        }
        Inspector {
            id: inspector
            y: root.project.libraryVisible ? library.height : 0
            width: parent.width
            height: root.project.libraryVisible ? parent.height - library.height : parent.height
            visible: root.project.inspectorVisible
            project: root.project
        }
    }
    Splitter {
        anchors.right: parent.right
        height: parent.height
        onDragged: (dx) => root.project.leftColumnWidth = root.project.leftColumnWidth + dx
    }
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.borderSubtle }
```

and keep `readonly property alias inspector: inspector` and add `readonly property alias library: library` in the root.

- [ ] **Step 5: Register, run tests**

Add `qml/Library.qml` to `QML_FILES`. Run: `cmake --build build-ui --config Debug` then `ctest --test-dir build-ui -C Debug --output-on-failure`
Expected: all PASS (`Library`: 6 functions). In `test_search_filters...`, `"guitar"` matches the two guitar patches of the shipped catalogue (`audio.warm-guitar`, `audio.wide-guitar`, sorted as in the file; the first is `audio.warm-guitar`).

- [ ] **Step 6: Screenshot and README**

```bash
build-ui/ui/Debug/jad-daw.exe --project scratch/demo.lpc --no-audio --screenshot docs/images/library.png --size 1280x800 --select-track 1 --panels library,inspector
```

Check the image (icon, lists, buttons, no clipped text), then add to the README after the Inspector paragraph:

```markdown
- **Library** (`Y`): the patches of the selected track's kind, by category, with search; click a patch or step with the
  Up and Down keys to apply it (strip values, inserts and instrument in one undo step); Revert applies the track's patch
  again. The catalogue is `core/data/patches.json`; the engine has one effect and one synth for now, so the built-in
  patches differ mostly in level, pan and a gain insert. Save, Delete and the Options menu are not implemented.

![Library](docs/images/library.png)
```

- [ ] **Step 7: Commit**

```bash
git add ui README.md docs/images/library.png
git commit -m "feat(ui): Library panel with categories, search and patch apply"
```

---

### Task 8: Smart Controls

**Files:**
- Create: `ui/qml/ScreenKnob.qml`, `ui/qml/SmartControls.qml`, `ui/tests/tst_smartcontrols.qml`, `docs/images/smart-controls.png`
- Modify: `ui/qml/Main.qml`, `ui/CMakeLists.txt`, `README.md`

**Interfaces:**
- Consumes: `project.inspector.smartControls` (`{id, label, group, min, max, value, def}`), `project.inspector.track`, `project.setSmartControl`, `project.smartControlsHeight`, `project.announceStub`.
- Produces: `ScreenKnob` (`label`, `value`, `from`, `to`, `resetValue`; alias `knob`; signal `released(real value)`), `SmartControls` (`required property ProjectController project`; aliases `groupList`, `emptyLabel`).

- [ ] **Step 1: Write the failing QML tests**

Create `ui/tests/tst_smartcontrols.qml`:

```qml
import QtQuick
import QtTest
import Jad

TestCase {
    id: tc
    name: "SmartControls"
    width: 900; height: 300
    visible: true
    when: windowShown

    ProjectController { id: p; audioEnabled: false }
    Component { id: smartC; SmartControls { width: 880; height: 200; project: p } }

    function init() {
        verify(p.newProjectInTempForTest())
        p.addTrack("audio")
        tryVerify(function () { return p.tracks.rowCount() >= 1 })
        p.selectTrack(p.tracks.trackIdAt(0), "replace")
    }

    function test_it_says_so_when_the_track_has_no_patch() {
        var s = createTemporaryObject(smartC, tc)
        verify(s.emptyLabel.visible)
    }
    function test_it_shows_the_controls_of_the_patch_grouped() {
        var s = createTemporaryObject(smartC, tc)
        p.applyPatch("audio.bright-vocal")
        tryVerify(function () { return s.groupList.count === 2 })   // Main and Tone
        verify(!s.emptyLabel.visible)
        compare(s.knobCount, 3)
    }
    function test_releasing_a_knob_moves_the_track_once() {
        var s = createTemporaryObject(smartC, tc)
        p.applyPatch("audio.bright-vocal")
        tryVerify(function () { return s.knobCount === 3 })
        var k = s.knobFor("level")
        verify(k !== null)
        k.released(6)
        tryVerify(function () { return p.inspector.track.gainDb === 6 })
        p.undo()
        tryVerify(function () { return p.inspector.track.gainDb === -2 })
    }
    function test_the_knobs_follow_the_track_when_it_changes_elsewhere() {
        var s = createTemporaryObject(smartC, tc)
        p.applyPatch("audio.bright-vocal")
        tryVerify(function () { return s.knobCount === 3 })
        p.setGain(p.tracks.trackIdAt(0), -10)
        tryVerify(function () { return s.knobFor("level").value === -10 })
    }
    function test_compare_and_the_eq_tab_are_shown_but_say_they_are_not_there_yet() {
        var s = createTemporaryObject(smartC, tc)
        var got = []
        p.notice.connect(function (m) { got.push(m) })
        mouseClick(s.compareButton)
        mouseClick(s.eqTab)
        compare(got.length, 2)
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build-ui --config Debug --target jad_ui_qmltests` and the runner on `ui/tests/tst_smartcontrols.qml`.
Expected: FAIL (`SmartControls is not a type`).

- [ ] **Step 3: ScreenKnob and SmartControls**

Create `ui/qml/ScreenKnob.qml`:

```qml
import QtQuick
import Jad

// A labelled screen control of a Smart Control: a knob with its name above and its value below.
Item {
    id: root
    property string label
    property real value: 0
    property real from: 0
    property real to: 1
    property real resetValue: 0
    readonly property alias knob: knob
    signal released(real value)
    implicitWidth: 64
    implicitHeight: 84

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.label.toUpperCase()
        color: Theme.textSecondary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeCaptionSize
        font.weight: Theme.fontTypeLabelWeight
    }
    Knob {
        id: knob
        anchors.horizontalCenter: parent.horizontalCenter
        y: 16
        width: 44
        height: 44
        from: root.from
        to: root.to
        resetValue: root.resetValue
        value: root.value
        onReleased: (v) => root.released(v)
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 64
        text: (knob.shown).toFixed(Math.abs(root.to - root.from) > 5 ? 1 : 2)
        color: Theme.textValue
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTypeLabelSize
    }
}
```

Create `ui/qml/SmartControls.qml`:

```qml
import QtQuick
import QtQuick.Layouts
import Jad

// The Smart Controls pane: the controls of the selected track's patch as labelled knobs, grouped in panels. One knob
// can move several parameters (the patch decides); a release is one undo step.
Rectangle {
    id: root
    required property ProjectController project
    readonly property var insp: project.inspector
    readonly property var controls: insp.smartControls
    readonly property var groups: {
        const order = [], byName = {}
        for (const c of controls) {
            const g = c.group !== "" ? c.group : qsTr("Controls")
            if (!(g in byName)) { byName[g] = []; order.push(g) }
            byName[g].push(c)
        }
        return order.map((g) => ({ name: g, controls: byName[g] }))
    }
    readonly property int knobCount: controls.length
    readonly property alias groupList: groupRepeater
    readonly property alias emptyLabel: empty
    readonly property alias compareButton: compare
    readonly property alias eqTab: eq
    function knobFor(controlId) {
        for (let g = 0; g < groupRepeater.count; ++g) {
            const grp = groupRepeater.itemAt(g)
            for (let k = 0; k < grp.knobs.count; ++k) {
                const item = grp.knobs.itemAt(k)
                if (item.controlId === controlId) return item
            }
        }
        return null
    }
    color: Theme.surfaceCanvas
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            color: Theme.surfacePanel
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing[3]
                anchors.rightMargin: Theme.spacing[3]
                spacing: Theme.spacing[3]
                Rectangle {
                    implicitWidth: 56
                    implicitHeight: 20
                    radius: Theme.radiusControl
                    color: Theme.accentPrimary
                    Text { anchors.centerIn: parent; text: qsTr("Track"); color: Theme.textPrimary; font.family: Theme.fontFamily; font.pixelSize: Theme.fontTypeLabelSize }
                }
                Text {
                    text: root.insp.track.patchName && root.insp.track.patchName !== "" ? root.insp.track.patchName : qsTr("No patch")
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeLabelSize
                }
                IconButton { id: compare; implicitHeight: 20; label: qsTr("Compare"); opacity: 0.6; onClicked: root.project.announceStub(qsTr("Compare")) }
                Item { Layout.fillWidth: true }
                IconButton { implicitHeight: 20; label: qsTr("Controls"); active: true }
                IconButton { id: eq; implicitHeight: 20; label: qsTr("EQ"); opacity: 0.6; onClicked: root.project.announceStub(qsTr("Smart Controls EQ")) }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Rectangle {
                Layout.preferredWidth: 190
                Layout.fillHeight: true
                color: Theme.surfacePanel
                Column {
                    anchors.fill: parent
                    Text {
                        x: Theme.spacing[3]
                        height: 26
                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("Automatic Smart Controls")
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTypeLabelSize
                    }
                    PanelHeader { width: parent.width; title: qsTr("Parameter Mapping"); expanded: false; onToggled: if (expanded) root.project.announceStub(qsTr("Parameter Mapping")) }
                    PanelHeader { width: parent.width; title: qsTr("External Assignment"); expanded: false; onToggled: if (expanded) root.project.announceStub(qsTr("External Assignment")) }
                }
            }
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Text {
                    id: empty
                    anchors.centerIn: parent
                    visible: root.knobCount === 0
                    text: root.insp.hasTrack ? qsTr("This track has no patch with Smart Controls: choose one in the Library") : qsTr("Select a track")
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTypeBodySize
                }
                Row {
                    anchors.centerIn: parent
                    spacing: Theme.spacing[5]
                    Repeater {
                        id: groupRepeater
                        model: root.groups
                        delegate: Rectangle {
                            id: panel
                            required property var modelData
                            readonly property alias knobs: knobRepeater
                            width: row.width + Theme.spacing[5] * 2
                            height: 108
                            radius: Theme.radiusCard
                            color: Theme.surfacePanel
                            border.color: Theme.borderStrong
                            Row {
                                id: row
                                anchors.centerIn: parent
                                spacing: Theme.spacing[3]
                                Repeater {
                                    id: knobRepeater
                                    model: panel.modelData.controls
                                    delegate: ScreenKnob {
                                        required property var modelData
                                        readonly property string controlId: modelData.id
                                        label: modelData.label
                                        from: modelData.min
                                        to: modelData.max
                                        resetValue: modelData.def
                                        value: modelData.value
                                        onReleased: (v) => root.project.setSmartControl(root.insp.track.trackId, modelData.id, v)
                                    }
                                }
                            }
                            Text {
                                anchors.top: parent.top
                                anchors.topMargin: 2
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: panel.modelData.name
                                color: Theme.textDisabled
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontTypeCaptionSize
                            }
                        }
                    }
                }
            }
        }
    }
}
```

- [ ] **Step 4: Put the pane above the mixer**

In `ui/qml/Main.qml`, in the main `ColumnLayout` before `Mixer { ... }` add:

```qml
        SmartControls {
            Layout.fillWidth: true
            Layout.preferredHeight: controller.smartControlsHeight
            visible: controller.smartControlsVisible
            project: controller
        }
```

- [ ] **Step 5: Register, run tests**

Add `qml/ScreenKnob.qml qml/SmartControls.qml` to `QML_FILES`. Run: `cmake --build build-ui --config Debug` then `ctest --test-dir build-ui -C Debug --output-on-failure`
Expected: all PASS (`SmartControls`: 5 functions).

- [ ] **Step 6: Screenshot and README**

```bash
build-ui/ui/Debug/jad-daw.exe --project scratch/demo.lpc --no-audio --screenshot docs/images/smart-controls.png --size 1280x800 --select-track 2 --panels inspector,smart
```

The demo's `Tone` audio track has no patch: for a useful picture apply one first. Add the option `--apply-patch <id>` to `ui/main.cpp` in the same way as `--select-track` (`QCommandLineOption applyPatchOption("apply-patch", "Apply the patch <id> to the selected track for --screenshot.", "id");` and `if (controller && parser.isSet(applyPatchOption)) controller->applyPatch(parser.value(applyPatchOption));` after the select-track block), rebuild, and run with `--apply-patch audio.bright-vocal`. The command is asynchronous: raise the delay if the knobs are still at their defaults (`--delay 2500`). Check the image, then in the README add after the Library paragraph:

```markdown
- **Smart Controls** (`B`): the screen controls of the selected track's patch, grouped in panels; drag a knob (Shift for
  fine, double click resets). A knob can move several parameters at once (the patch decides), and one release is one undo
  step. Parameter Mapping, External Assignment, Compare and the EQ tab are not implemented.

![Smart Controls](docs/images/smart-controls.png)
```

and add `--panels <list>`, `--select-region <n>` and `--apply-patch <id>` to the screenshot command line in the README bullet "Screenshots (used above)...".

- [ ] **Step 7: Commit**

```bash
git add ui README.md docs/images/smart-controls.png
git commit -m "feat(ui): Smart Controls pane with grouped screen knobs"
```

---

### Task 9: The mixer on the shared channel strip

**Files:**
- Modify: `ui/bridge/mixer_model.{h,cpp}`, `ui/qml/Mixer.qml`, `ui/tests/tst_mixer.qml`, `ui/tests/tst_models.cpp` (only if it asserts the mixer roles), `ui/CMakeLists.txt`, `README.md`
- Delete: `ui/qml/MixerStrip.qml`
- Create: `docs/images/mixer.png`

**Interfaces:**
- Consumes: `trackToMap` (Task 4), `ProjectStrip` (Task 5).
- Produces: `MixerModel` role `info` (the `trackToMap` of the row) in addition to the existing roles.

- [ ] **Step 1: Write the failing model test**

In `ui/tests/tst_models.cpp` add this slot to `ModelsTest` (after `regionModelExposesBeats`):

```cpp
    void mixerRowsCarryTheStripViewForQml() {
        jad::MixerModel model;
        jad::TrackRow a;
        a.id = "a";
        a.kind = "audio";
        a.name = "Vox";
        a.outputName = "Stereo Out";
        a.inserts.push_back({"builtin.gain", 3.0});
        jad::TrackRow m;
        m.id = "m";
        m.kind = "master";
        m.master = true;
        model.reset({m, a});
        const int role = model.roleNames().key("info");
        QVERIFY(role != 0);
        const QVariantMap info = model.data(model.index(0), role).toMap();  // the master goes last: row 0 is the track
        QCOMPARE(info.value("name").toString(), QStringLiteral("Vox"));
        QCOMPARE(info.value("inserts").toList().size(), 1);
        QVERIFY(model.data(model.index(1), role).toMap().value("master").toBool());
    }
```

Run: `cmake --build build-ui --config Debug --target jad_models_tests` — Expected: FAIL (`key("info")` is 0).

- [ ] **Step 2: The model role**

In `ui/bridge/mixer_model.h` extend the role enum to `enum Role { TrackId = Qt::UserRole + 1, Name, Color, IsMaster, Mute, Solo, GainDb, Pan, Info };`. In `ui/bridge/mixer_model.cpp` add `#include "bridge/row_maps.h"`, a case `case Info: return trackToMap(r);` in `data`, and `{Info, "info"}` in `roleNames`.

- [ ] **Step 3: Update the mixer test**

Replace `ui/tests/tst_mixer.qml` by:

```qml
import QtQuick
import QtTest
import Jad

TestCase {
    name: "Mixer"
    width: 200; height: 700
    visible: true
    when: windowShown

    Component { id: stripC; ChannelStrip { width: 96; height: 640; showSlots: false
        info: ({ trackId: "t", name: "Keys", color: "purple", kind: "instrument", master: false, gainDb: 0, pan: 0,
                 mute: false, solo: false, inserts: [], sends: [] }) } }

    function test_gain_released_once_after_drag() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.gainReleased.connect(function (id, v) { got.push([id, v]) })
        mousePress(s.fader, 10, 100); mouseMove(s.fader, 10, 60, 0, Qt.LeftButton)
        compare(got.length, 0)
        mouseRelease(s.fader, 10, 60)
        compare(got.length, 1)
        compare(got[0][0], "t")
    }
    function test_mute_toggle_emits() {
        var s = createTemporaryObject(stripC, this)
        var got = []
        s.muteToggled.connect(function (id, on) { got.push([id, on]) })
        mouseClick(s.muteButton)
        compare(got.length, 1)
        compare(got[0][1], true)
    }
}
```

- [ ] **Step 4: Mixer.qml on ProjectStrip**

In `ui/qml/Mixer.qml` set `readonly property real stripHeight: 420` and replace both `MixerStrip { ... }` delegates by:

```qml
                delegate: ProjectStrip {
                    required property var model
                    visible: !model.isMaster
                    width: visible ? implicitWidth : 0
                    height: strips.height
                    project: root.project
                    info: model.info
                }
```

for the track repeater, and for the master repeater:

```qml
                delegate: ProjectStrip {
                    required property var model
                    visible: model.isMaster
                    width: visible ? implicitWidth : 0
                    height: strips.height
                    project: root.project
                    info: model.info
                    peak: root.project.masterPeak
                }
```

The strips show the instrument, insert, send and output slots now (`showSlots` defaults to true; the master hides them by itself).

- [ ] **Step 5: Remove the old strip**

Delete `ui/qml/MixerStrip.qml` and its line in `ui/CMakeLists.txt`'s `QML_FILES`. Search for other uses: `grep -rn "MixerStrip" ui` must return nothing.

- [ ] **Step 6: Run all tests**

Run: `cmake --build build-ui --config Debug` then `ctest --test-dir build-ui -C Debug --output-on-failure`
Expected: all PASS.

- [ ] **Step 7: Screenshot and README**

```bash
build-ui/ui/Debug/jad-daw.exe --project scratch/demo.lpc --no-audio --screenshot docs/images/mixer.png --size 1280x800 --panels inspector
```

Check the strips (slots, no clipped text, the master strip last). In the README replace the sentence "Menus (File, ..., timeline and mixer)" bullet's mixer mention with a new bullet after Smart Controls:

```markdown
- **Mixer** (`X`): one strip per track, built from the same channel-strip component as the Inspector (instrument,
  inserts, sends, output, pan, fader, mute, solo), the master strip last.

![Mixer](docs/images/mixer.png)
```

- [ ] **Step 8: Commit**

```bash
git add ui README.md docs/images/mixer.png
git commit -m "feat(ui): the mixer uses the shared channel strip"
```

---

### Task 10: Final checks

**Files:**
- Modify: `README.md`, `docs/superpowers/specs/2026-10-08-ui-b-panels-design.md`, `docs/images/main-window.png` and the other UI-A pictures if the frame changed

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: Refresh the old screenshots**

The default window now shows the Inspector. Regenerate the three UI-A pictures with their documented options and check each one (nothing clipped, the frame still matches the README text):

```bash
build-ui/ui/Debug/jad-daw.exe --project scratch/demo.lpc --no-audio --screenshot docs/images/main-window.png --size 1280x800
build-ui/ui/Debug/jad-daw.exe --project scratch/demo.lpc --no-audio --screenshot docs/images/tools-and-selection.png --size 1280x800 --tool scissors --select-track 2
build-ui/ui/Debug/jad-daw.exe --project scratch/demo.lpc --no-audio --screenshot docs/images/menus.png --size 1280x800 --open-menu 2
```

- [ ] **Step 2: README limits and the outdated sentence**

In `README.md` "Known limits" replace "and the panels behind Library, Inspector, Smart Controls, Editors and Loops do not exist yet" with "and the panels behind Quick Help, Editors and Loops do not exist yet; the engine has one effect (gain) and one synth (sine), so the built-in patches and Smart Controls are small; a fader or knob only changes the sound after it is released". Remove the older phrase "a fader only changes the sound after it is released" so it appears once. Add the new spec to the "Design:" line: `docs/superpowers/specs/2026-10-08-ui-b-panels-design.md`.

- [ ] **Step 3: Spec status**

In the spec, change the status line to "Status: implemented (see the plan `docs/superpowers/plans/2026-10-08-ui-b-panels.md`). Date: 2026-10-08." and add under section 3 one sentence: "Hosting real VST3 plug-ins (the owner has some installed) needs its own spec and plan: it is a Core and platform subsystem, not a panel."

- [ ] **Step 4: Full verification**

Run, and paste the summary lines into the final report:

```bash
cmake --build build --config Debug && ctest --test-dir build -C Debug --output-on-failure
cmake --build build-ui --config Debug && ctest --test-dir build-ui -C Debug --output-on-failure
cmake -S . -B build-core -DLPC_BUILD_UI=OFF -DLPC_WITH_JUCE=OFF && cmake --build build-core --config Debug && ctest --test-dir build-core -C Debug --output-on-failure
```

Expected: every suite PASS (Core without Qt included). If anything fails, fix it before continuing; do not edit tests to fit a failure.

- [ ] **Step 5: By-hand pass on Windows**

Open the demo project and check, writing down anything odd: Y, I and B toggle the panels and the menu entries show the check marks; the left column resizes by its edge; selecting tracks and regions updates the Inspector; patch click and Up/Down in the Library; knobs of Smart Controls and faders feel right (one release, one undo step: press Ctrl+Z once); the mixer shows the same strips; stub fields toast once when switched on.

- [ ] **Step 6: Commit and CI**

```bash
git add -A README.md docs
git commit -m "docs: UI-B final pass, screenshots and README"
git push
gh run list --branch main --limit 3
```

(Push only when the owner has said so; otherwise stop after the commit.) Check the macOS and Windows jobs of the new run with `gh run view <id> --json jobs`.

