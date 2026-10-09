# Tracks area visibility of buses: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (the user chose native, inline execution). Steps use checkbox (`- [ ]`) syntax. No subagent is used; if one ever is, it runs on Haiku 5.5.

**Goal:** a Bus or Aux track can be hidden from the Tracks area (it stays in the Mixer); "New Bus" in the Send and Output menus makes a hidden bus and routes to it in one undo step; the Mixer strip name selects a track; a Track menu action shows or hides a bus.

**Architecture:** one new `Track.showInTracks` field (JSON only when false) edited through `set_track_props`; the bridge filters Tracks area rows and timeline row indexes with one shared "shown tracks" order; QML stays a view and emits signals that `ProjectStrip` wires to new controller methods.

**Tech Stack:** C++20 Core (Catch2), Qt 6.8 bridge (QtTest), QML (Qt Quick Test).

**Spec:** `docs/superpowers/specs/2026-10-09-tracks-area-visibility-design.md`

## Global Constraints

- Core has no JUCE or Qt. JSON field `showInTracks` is written **only when false**; a missing field means true; a non-boolean value is a load error.
- `showInTracks == false` only on kind Bus or Aux: error code `bad_value`, message `only buses and auxes can be hidden from the Tracks area`.
- No region rule is added (`checkRegion` already refuses regions on buses and auxes with `invalid_kind`).
- `addTrack("bus")` from the Track menu keeps creating a **shown** bus. "New Bus" from a send or output creates a **hidden** one, named "Bus N" with the smallest N that is unused.
- Builds: `cmake --build <dir> --config Debug --parallel 2` (16 GB machine). Test dirs: `build-core` (Core), `build-plugin` (with JUCE), `build-ui` (UI).
- Commit messages end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.

## Review Focus

- A project saved by the old version loads with every track shown and re-saves byte-identical (golden test).
- Hiding then undoing restores the flag exactly; `add_track` of a hidden bus then undo removes it.
- Timeline rows and Tracks area headers agree when a hidden track sits between shown ones.
- Selecting a hidden track keeps the Inspector on it; deleting it works and undo restores it hidden.
- "New Bus" on a track whose output is already a bus replaces the output in the same transaction; one undo restores both.

---

### Task 1: Core field, JSON, validation, `set_track_props`

**Files:**
- Modify: `core/include/lpc/model.h` (Track), `core/include/lpc/commands.h` (TrackPatch), `core/src/model_json.cpp`, `core/src/validation.cpp` (checkProject), `core/src/commands.cpp` (AddTrackCmd::validate, SetTrackPropsCmd, commandFromJson), `core/include/lpc/validation.h` (declare `checkShowInTracks`)
- Test: `tests/test_commands_track_props.cpp`, `tests/test_model_json.cpp`, `tests/test_undo_property.cpp` + `tests/random_commands.h`

**Interfaces:**
- Produces: `bool Track::showInTracks = true`; `std::optional<bool> TrackPatch::showInTracks`; `MaybeError checkShowInTracks(TrackKind kind, bool show)`.

- [ ] **Step 1: failing tests** in `tests/test_commands_track_props.cpp`:
  - `set_track_props showInTracks hides a bus and undo restores it`: project with a bus; apply `makeSetTrackProps(bus, TrackPatch{.showInTracks=false})`; `REQUIRE(p.findTrack(bus)->showInTracks == false)`; apply the returned inverse; `REQUIRE(showInTracks == true)`.
  - `showInTracks=false on an audio track is refused`: result error code `bad_value`, project unchanged.
  - `showInTracks=false on the master is refused`.
  - `add_track with a hidden bus is accepted; with a hidden audio track is refused`.
  - `JSON command set_track_props carries showInTracks` (round trip through `commandFromJson` and `toJson`).
  In `tests/test_model_json.cpp`:
  - `showInTracks is written only when false` (`toJson(project)["tracks"][i]` has no key when true, has `false` when hidden).
  - `old documents without the field load as shown`.
  - `a non-boolean showInTracks is a load error` (`showInTracks: "no"` throws `std::runtime_error`).
  - `a hidden audio track in a document is rejected` (checkProject).
- [ ] **Step 2: run, expect FAIL** (does not compile: no `showInTracks`). Run: `cmake --build build-core --config Debug --parallel 2 && build-core/tests/Debug/lpc_tests.exe "[track_props],[model_json]"` (use the tags the files already use; check with `--list-tags`).
- [ ] **Step 3: implement**
  - `model.h`: in `Track` add `bool showInTracks = true;` after `patchId`.
  - `validation.h/.cpp`: `MaybeError checkShowInTracks(TrackKind kind, bool show) { if (!show && !isBusLike(kind)) return CommandError{"bad_value", "only buses and auxes can be hidden from the Tracks area"}; return std::nullopt; }`. Call it in `checkProject` per track and in `AddTrackCmd::validate`.
  - `model_json.cpp`: `to_json(Track)`: `if (!t.showInTracks) j["showInTracks"] = false;`. `from_json`: `if (j.contains("showInTracks")) { if (!j["showInTracks"].is_boolean()) throw std::runtime_error("showInTracks must be a boolean"); t.showInTracks = j["showInTracks"].get<bool>(); }`.
  - `commands.h`: `std::optional<bool> showInTracks;` in `TrackPatch`.
  - `commands.cpp` `SetTrackPropsCmd`: serialise `if (patch_.showInTracks) j["showInTracks"] = *patch_.showInTracks;`; in `apply` after the master check: `if (patch_.showInTracks) if (auto e = checkShowInTracks(t->kind, *patch_.showInTracks)) return fail(*e);` then record previous and assign like name/color. In `commandFromJson` read `if (j.contains("showInTracks")) patch.showInTracks = j["showInTracks"].get<bool>();`.
  - `tests/random_commands.h`: let the random generator emit `set_track_props` with `showInTracks` only for bus/aux tracks.
- [ ] **Step 4: run Core suite**: `build-core/tests/Debug/lpc_tests.exe`. Expected: all pass, golden tests unchanged.
- [ ] **Step 5: commit** `feat(core): Track.showInTracks, hidden buses and auxes`.

### Task 2: Bridge, snapshot order, models, controller

**Files:**
- Modify: `ui/bridge/snapshot.h/.cpp` (`TrackRow.showInTracks`, `trackIndex` over shown tracks), `ui/bridge/track_list_model.cpp` (skip hidden rows, renumber), `ui/bridge/project_controller.h/.cpp` (`newBusFor`, `setShowInTracks`), `ui/bridge/row_maps.h` if it maps TrackRow fields
- Test: `ui/tests/tst_bridge.cpp`

**Interfaces:**
- Consumes: Task 1 `TrackPatch::showInTracks`.
- Produces: `Q_INVOKABLE void ProjectController::newBusFor(const QString& trackId, const QString& role)` (`"send"`/`"output"`), `Q_INVOKABLE void setShowInTracks(const QString& trackId, bool on)`, `TrackRow::showInTracks`.

- [ ] **Step 1: failing tests** in `ui/tests/tst_bridge.cpp` (follow its existing `ProjectController c(false)` pattern):
  - `snapshot marks hidden buses`: project with a hidden bus, `makeSnapshot` -> `tracks[i].showInTracks == false`.
  - `regions' trackIndex counts shown tracks only`: tracks audio A, hidden bus H, audio B with regions on A and B; B's region `trackIndex == 1`.
  - `track list model skips hidden tracks and renumbers`.
  - `mixer model lists every track`.
  - `newBusFor send makes one undo step`: audio track; `newBusFor(id, "send")`; rows: +1 bus hidden named "Bus 1", the audio track has one send to it; `undo()` once -> both gone.
  - `newBusFor output sets the output and one undo restores the previous output`.
  - `newBusFor picks the smallest unused number` (existing "Bus 1" -> "Bus 2").
  - `setShowInTracks is one undoable change`.
- [ ] **Step 2: run, expect FAIL.** `cmake --build build-ui --config Debug --parallel 2 && build-ui/ui/tests/Debug/<bridge test exe> ` (find the exe name in `ui/tests/CMakeLists.txt`).
- [ ] **Step 3: implement**
  - `snapshot.cpp`: `tr.showInTracks = t.showInTracks;` and keep a second counter `shownRow` that increments only for non-master tracks with `showInTracks`; use it for `rr.trackIndex`. Keep `row` for colour assignment (colours must not change when something is hidden).
  - `TrackListModel`: filter on `showInTracks` where it builds its row list; its displayed number counts shown tracks. Keep a single helper `shownTracks(const Snapshot&)` in `snapshot.h` returning the indexes, used by both the model and the test that compares them.
  - Controller: `newBusFor` builds a transaction with `makeTransaction({makeAddTrack(bus, -1), role == "send" ? makeAddSend(trackId, Send{newUuid, busId, 0.0f, false}) : makeSetOutput(trackId, busId)})` where the bus is `Track{id=newUuid, kind=Bus, name="Bus N", color=<existing default for buses>, showInTracks=false}` (copy how `addTrack("bus")` fills a bus). Reuse the controller's existing helper that submits one command and updates the selection; select the new bus afterwards via the existing select call. `setShowInTracks` submits `makeSetTrackProps(id, TrackPatch{.showInTracks = on})`.
- [ ] **Step 4: run bridge suite.** Expected: all pass.
- [ ] **Step 5: commit** `feat(ui): hidden buses leave the Tracks area; newBusFor and setShowInTracks`.

### Task 3: UI menus, strip name selection, Track menu action, README

**Files:**
- Modify: `ui/qml/ChannelStrip.qml` (New Bus entries, name click), `ui/qml/ProjectStrip.qml` (wire), `ui/actions/actions.json` (`track.showInTracks`), `ui/qml/ActionHub.qml` or wherever actions are bound (see how `track.delete` is bound), `README.md`, `docs/superpowers/ui-b-deferred.md` (tick the todo)
- Test: `ui/tests/tst_channelstrip.qml`, `ui/tests/` action table test (find with `Grep "actions.json"` in `ui/tests`)

**Interfaces:**
- Consumes: Task 2 `newBusFor`, `setShowInTracks`, existing `selectTrack(id, mode)`.
- Produces: `ChannelStrip` signals `newBusRequested(string id, string role)` and `selectRequested(string id, int modifiers)`.

- [ ] **Step 1: failing QML tests** in `tst_channelstrip.qml`: the Send and Output menus have a first item "New Bus" and triggering it emits `newBusRequested(trackId, "send"/"output")` (use `SignalSpy`); clicking the strip name emits `selectRequested(trackId, Qt.NoModifier)`, Shift-click carries `Qt.ShiftModifier`, and the master strip emits nothing. Action table test: `track.showInTracks` exists, menu `Track`, status `ready`.
- [ ] **Step 2: run, expect FAIL** (`build-ui` QML test exe).
- [ ] **Step 3: implement**
  - `TargetMenu` gets `property bool withNewBus: false` and `signal newBusChosen()`; first item `ThemedMenuItem { text: qsTr("New Bus"); onTriggered: menu.newBusChosen() }` followed by `MenuSeparator`. `outputMenu` and `sendMenu` set it true and connect to `root.newBusRequested(root.trackId, "output"/"send")`. The send slot must stay visible when `targets.length == 0` now that "New Bus" is always available: change `visible: root.slotsVisible && root.targets.length > 0` to `root.slotsVisible`.
  - Name `Text` becomes a `MouseArea` target: `onClicked: (m) => { if (!root.master) root.selectRequested(root.trackId, m.modifiers) }`; highlight the name when the strip's track is selected (property `selected` from `info`).
  - `ProjectStrip.qml`: `onNewBusRequested: (id, role) => controller.newBusFor(id, role)`; `onSelectRequested: (id, mods) => controller.selectTrack(id, mods & Qt.ShiftModifier ? "extend" : mods & Qt.ControlModifier ? "toggle" : "replace")` (use the exact mode strings the track headers use: look at `TrackHeader` usage).
  - `actions.json`: `{"id":"track.showInTracks","label":"Show in Tracks Area","menu":"Track","shortcut":"","kind":"toggle","status":"ready"}` (copy the field names of an existing toggle such as a View toggle). Bind checked/enabled/trigger where the other Track actions are bound.
  - README Mixer paragraph: buses made from a send are Mixer-only; show them with Track > Show in Tracks Area.
- [ ] **Step 4: run the whole UI suite** (`ctest --test-dir build-ui -C Debug`). Expected: all pass.
- [ ] **Step 5: by-hand check with windows-mcp** (screenshot each): create a bus from Send+ "New Bus": it appears only in the Mixer; click its name to select; Track > Show in Tracks Area shows it; Undo.
- [ ] **Step 6: commit** `feat(ui): New Bus from send and output, Mixer name selects, Show in Tracks Area`.
