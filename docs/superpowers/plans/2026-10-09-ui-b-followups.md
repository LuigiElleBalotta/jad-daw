# UI-B follow-ups, insert bypass and cross-track insert drag: implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (the user chose native, inline execution). Steps use checkbox (`- [ ]`) syntax. No subagent is used; if one ever is, it runs on Haiku 5.5.

**Goal:** insert bypass; drag inserts to reorder and onto another track; the five UI-B interactions (Shift-click bus pin, send pre/post menu, Library from instrument slot and header double click, vertical splitter); the eight minors; and the fixes found by hand in the VST3 UI.

**Architecture:** two Core commands (`move_insert`, `set_insert_bypass`) and one processor wrapper (`BypassedProcessor`); the JUCE host adopts a live instance across tracks; the bridge gains `targetsFor`, `pinnedBusId`, `moveInsert`, `setInsertBypass`; QML slots emit gesture signals and `ProjectStrip` / the Mixer map them to controller calls.

**Tech Stack:** C++20 Core (Catch2), JUCE 8.0.4 platform (Catch2, real VST3 test plug-in), Qt 6.8 bridge (QtTest), QML (Qt Quick Test).

**Spec:** `docs/superpowers/specs/2026-10-09-ui-b-followups-design.md`

## Global Constraints

- Core has no JUCE or Qt. JSON `bypass` on a `ProcessorRef` is written **only when true**.
- `move_insert {trackId, from, to, toTrackId?}`: same track `to` in `0..n-1`; another track `to` in `0..m`; errors `not_found`, `bad_index`, `bad_target` (master or a track that cannot hold inserts: every non-master track can, so only the master); `from == to` on the same track is a no-op that is accepted. Inverse swaps tracks and indexes.
- `set_insert_bypass {trackId, index, bypass}`: errors `not_found`, `bad_index`.
- A bypassed insert keeps its latency in `computePdc` (it delays the signal by the same number of frames instead of processing it).
- Send knob range `-96..12`; smart-controls height clamp stays 120..320.
- Builds: `cmake --build <dir> --config Debug --parallel 2`. Test dirs: `build-core`, `build-plugin`, `build-ui`.
- Commit messages end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.

## Review Focus

- Moving a plug-in insert to another track keeps its live instance, state and editor-less audio continuity (no reload, no state loss); undo moves it back.
- A bypassed plug-in insert does not shift the timing of any track (offline render with and without the bypassed insert is sample-identical except for the plug-in's own processing, and PDC delays are unchanged).
- A drag that ends outside every strip sends nothing; Escape or an inserts-list change mid-drag drops the drag.
- The Gain insert still changes gain by a horizontal drag and moves by a vertical drag; a plug-in slot moves with any drag.
- The scanner never shows a modal CRT dialog, whichever build.

---

### Task 1: Core `move_insert`, `set_insert_bypass`, `BypassedProcessor`

**Files:**
- Modify: `core/include/lpc/model.h` (`ProcessorRef::bypass`), `core/src/model_json.cpp`, `core/include/lpc/commands.h`, `core/src/commands_strip.cpp`, `core/src/commands.cpp` (`commandFromJson`), `core/include/lpc/audio/processors.h` + `core/src/audio/processors.cpp` (`BypassedProcessor`), `core/src/plugin_host.cpp` (`makeInsert`), `core/src/graph_builder.cpp` (nothing if PDC reads latency through `acquire`: check), `tests/random_commands.h`
- Test: new `tests/test_move_insert.cpp`, new `tests/test_insert_bypass.cpp`, `tests/test_pdc.cpp`, `tests/test_undo_property.cpp`

**Interfaces:**
- Produces: `bool ProcessorRef::bypass = false` (included in `operator==`); `CommandPtr makeMoveInsert(Uuid trackId, int from, int to, Uuid toTrackId = {})` (null `toTrackId` = same track); `CommandPtr makeSetInsertBypass(Uuid trackId, int index, bool bypass)`; `audio::BypassedProcessor(std::unique_ptr<IProcessor> inner)`.

- [ ] **Step 1: failing tests** (add the two files to `tests/CMakeLists.txt` and re-run `cmake -S . -B build-core`: the sources are a GLOB with CONFIGURE_DEPENDS).
  - `test_move_insert.cpp`: moves within a track (3 inserts, 0 -> 2, order check); same index accepted and unchanged; bad index (-1, n) -> `bad_index`, project unchanged; unknown track -> `not_found`; to another track at 0, middle and end, carrying params, state, label and bypass; target the master -> `bad_target`; target index `m+1` -> `bad_index`; inverse restores both tracks exactly (compare projects with `==`); JSON round trip of the command with and without `toTrackId`.
  - `test_insert_bypass.cpp`: sets and clears; `bad_index`; `not_found`; inverse; JSON of `ProcessorRef` has no `bypass` key when false and `"bypass": true` when true; old documents load with false; a non-boolean value is a load error; audio: a Gain insert with `bypass` renders identically to the track without the insert (`offline_render` or `RenderGraph` as in `test_render_graph.cpp`); PDC: with the fake plug-in host from `tests/fake_plugin_host.h` reporting latency 32, `computePdc` gives the same edge delays with the insert bypassed and not.
  - `test_undo_property.cpp` + `random_commands.h`: the random generator emits `move_insert` (same and cross track) and `set_insert_bypass`.
- [ ] **Step 2: run, expect FAIL** (compile errors). `cmake --build build-core --config Debug --parallel 2`.
- [ ] **Step 3: implement**
  - `model.h`: `bool bypass = false;` in `ProcessorRef`.
  - `model_json.cpp`: write `bypass` only when true; read `if (j.contains("bypass")) { if (!j["bypass"].is_boolean()) throw ...; r.bypass = ...; }`.
  - `commands_strip.cpp`: `MoveInsertCmd` and `SetInsertBypassCmd` following `RemoveInsertCmd`/`SetInsertStateCmd`. MoveInsert: resolve source track and chain; same track: range-check both indexes `0..n-1`, `std::rotate`-style move (erase then insert at `to`), inverse `makeMoveInsert(track, to, from)`. Cross track: `toTrack` must exist and not be the master; check `from < n_src`, `to <= n_dst`; move the element; inverse `makeMoveInsert(toTrack, to, from, trackId)`. Validation of the moved insert is not repeated (it came from the model).
  - `commands.h`/`commands.cpp`: declarations and `commandFromJson` branches `move_insert` (`toTrackId` optional, default null Uuid) and `set_insert_bypass`.
  - `processors.h/.cpp`: `BypassedProcessor` holds `inner_` and a `DelayLine delay_` built in the constructor from `inner_->latencySamples()`; `prepare` forwards to `inner_`; `latencySamples()` forwards; `process` runs `delay_.process(l, r, l, r, n)` in place through two small scratch copies is not allowed on the audio thread (no allocation): `DelayLine::process` requires `in` and `out` not to overlap, so keep two preallocated `std::vector<float>` of `kMaxBlock` in the object and process in chunks of that size (copy in to scratch, delay into the caller's buffers). When latency is 0 the loop is a no-op. `describe()` returns `{"bypassed": true, "inner": inner_->describe()}`.
  - `plugin_host.cpp` `makeInsert`: build the processor as today (`makeEffect`, `SharedProcessor` or `MissingPluginProcessor`), then `if (ref.bypass) return std::make_unique<audio::BypassedProcessor>(std::move(processor));`.
  - `graph_builder.cpp`: `trackLatency` keeps counting `live->latencySamples()` for vst3 inserts whatever the flag; verify with the PDC test.
- [ ] **Step 4: run Core suite**: `build-core/tests/Debug/lpc_tests.exe`. Expected: all pass.
- [ ] **Step 5: commit** `feat(core): move_insert across tracks, set_insert_bypass, bypassed inserts keep their latency`.

### Task 2: JUCE host adoption across tracks; scanner and hosting fixes

**Files:**
- Modify: `platform/juce/juce_plugin_host.cpp` (adoption looks at every track), `tools/plugin-scanner/main.cpp` (no CRT dialogs), `ui/bridge/project_controller.cpp` (`setUpPlugins` at construction, not at project load), `ui/qml/PluginManager.qml`, `ui/qml/ChannelStrip.qml` and `ui/qml/ThemedMenu.qml`/`ThemedMenuItem.qml` (vendor submenu text colours), `ui/qml/Main.qml` or the dialog file for the footer
- Test: `platform/juce/tests/test_plugin_host.cpp`, a new scanner test in `platform/juce/tests/test_scanner.cpp`, `ui/tests/tst_plugins.cpp`

- [ ] **Step 1: failing tests**
  - `test_plugin_host.cpp`: `juce host: an insert moved to another track keeps its instance`: acquire `{trackA, 0}` with state S; acquire `{trackB, 0}` with the same plug-in and state S; the returned pointer equals the first (a live instance of the same id and state on another track is adopted when the old slot no longer asks for it: the caller calls `acquire(trackB,0)` first then `prune`/`acquire` for the old slot with a different plug-in or none).
    Assert: `a == b`, `created count` does not grow (use the test plug-in's instance counter if one exists; otherwise compare pointers).
  - `test_scanner.cpp`: `scanner child that aborts is reported as failed and no dialog blocks`: build a tiny helper exe path via `Options::scannerArgs` that makes the child call `std::abort()` (the existing scanner tests already pass `scannerArgs`; add a `--crash` mode to `tools/plugin-scanner/main.cpp` guarded by an env var `LPC_SCANNER_TEST_CRASH=1`); expect entry status Failed with reason `crashed (exit code ...)` within 5 s.
  - `tst_plugins.cpp`: a controller built with `openAudioDevice=false` reports hosting inactive with the reason "audio output disabled"; with audio enabled and no project, `plugins.hostingActive` is already true once the controller exists (use a property on `PluginsModel`).
- [ ] **Step 2: run, expect FAIL.** `cmake --build build-plugin --config Debug --parallel 2 && build-plugin/platform/juce/tests/Debug/lpc_juce_tests.exe`.
- [ ] **Step 3: implement**
  - `juce_plugin_host.cpp` `acquire`: the adoption loop drops `!(o.slot.track == slot.track)`; keep the `o.id == ref.processorId && o.state == ref.state && o.proc` match, but skip entries whose **old slot is still wanted**: the host cannot know, so adoption stays a swap as today (the entry at `key` goes to `k`), which is what makes a cross-track move and its undo work. Add a comment saying so.
  - `tools/plugin-scanner/main.cpp`: at the top of `main`: `#ifdef _WIN32 SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX); _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT); #ifdef _DEBUG _CrtSetReportMode(_CRT_WARN, 0); _CrtSetReportMode(_CRT_ERROR, 0); _CrtSetReportMode(_CRT_ASSERT, 0); #endif #endif` (`<crtdbg.h>`, `<stdlib.h>`, `<windows.h>`); and the `LPC_SCANNER_TEST_CRASH` abort hook.
  - Controller: move the hosting set-up (`setUpPlugins()`, `JuceInit`) out of the project-open path into the constructor when `openAudioDevice_` is true and JUCE is built in; project open only calls `refreshPluginHost()` as today. Keep the rule "hosting only when audio is enabled" and keep the teardown order.
  - `PluginManager.qml`: the inactive message reads "Plug-in hosting is off: audio output is disabled (--no-audio)." when `audioEnabled` is false and "Plug-in hosting is not available in this build." otherwise.
  - Theme: the vendor submenu entries and the Plug-in Manager footer use the dark tokens (`Theme.surface*`, `Theme.textPrimary`); give `ThemedMenu` a themed `delegate`/`background` for submenus (they are `ThemedMenu` already: check why the title text is dark when not highlighted: the `Menu` `title` row is drawn by `MenuItem` with `palette`; set `palette.text`) and style the dialog's `footer` and `Close` button like the other dialogs (`AboutDialog.qml`).
- [ ] **Step 4: run the three suites.** Expected: all pass (`build-plugin` JUCE tests, `build-ui` ctest).
- [ ] **Step 5: by-hand with windows-mcp** (the app must be closed to rebuild `build-ui`): start the app with no project; open Window > Plug-in Manager: the list fills and the buttons work; open a menu: vendor submenu text is readable; no CRT dialog appears during a scan.
- [ ] **Step 6: commit** `fix: scanner never shows a CRT dialog, hosting starts with the app, themed plug-in menus`.

### Task 3: Bridge

**Files:**
- Modify: `ui/bridge/snapshot.h/.cpp` (`InsertRow.bypass`), `ui/bridge/inspector_model.h/.cpp` (`pinnedBusId`, `pinned`), `ui/bridge/project_controller.h/.cpp` (`moveInsert`, `setInsertBypass`, `showBus`, `targetsFor`, `routingRevision`, `libraryVisible` path for `libraryRequested`, revert message), `ui/bridge/library_model.h/.cpp` (`patchesChanged`/`currentChanged`), `core/include/lpc/patch_library.h` + `core/src/patch_library.cpp` (`unreadable(path, message)`, problems carry path and message), the place that calls `loadPatchCatalogue` (log each problem once with `qWarning`)
- Test: `ui/tests/tst_bridge.cpp`, `tests/test_patch_library.cpp`

**Interfaces:**
- Consumes: Task 1 commands.
- Produces: `Q_INVOKABLE void moveInsert(const QString& trackId, int from, int to, const QString& toTrackId = QString())`; `Q_INVOKABLE void setInsertBypass(const QString& trackId, int index, bool on)`; `Q_INVOKABLE void showBus(const QString& busId)`; `Q_INVOKABLE QVariantList targetsFor(const QString& trackId)` (`[{id,name}]`); `Q_PROPERTY(int routingRevision READ routingRevision NOTIFY snapshotApplied)`; `InsertRow::bypass`; `InspectorModel::pinned`.

- [ ] **Step 1: failing tests** in `tst_bridge.cpp`: `moveInsert is one undo step` (same track and cross track); `setInsertBypass round trip and snapshot row`; `showBus pins, selection change clears it, deleting the pinned bus clears it, project replace clears it`; `targetsFor excludes self, a bus that outputs to the track and a bus that reaches it through a send`; `targetsFor with no snapshot is empty`; `revert notice for a patch id that left the catalogue` ("Patch '<id>' is no longer in the catalogue", nothing changes) and the existing "no patch to revert" for an empty id; `LibraryModel does not emit patchesChanged when only the current patch changes`. In `test_patch_library.cpp`: `unreadable(path, message)` yields one problem with both; a broken resource entry's problem carries the file and the parse message.
- [ ] **Step 2: run, expect FAIL.**
- [ ] **Step 3: implement** as in the spec sections 3.1, 3.2 (controller side), 3.4 (`libraryRequested` is handled in QML by `ProjectStrip`/`Main`: only `selectTrack(id, "replace")` + `libraryVisible = true` needed here), 4.3, 4.4, 4.5, 4.6. `targetsFor` is computed from `Snapshot` rows with the Core's `reaches` rule (copy the algorithm from `core/src/validation.cpp` over rows, since the snapshot has no `Project`). `moveInsert`/`setInsertBypass` submit one command through the controller's existing helper. `inspectorBusId_` is cleared in the same places the shown track changes (see where `InspectorModel::update` is called).
- [ ] **Step 4: run Core and bridge suites.** Expected: all pass.
- [ ] **Step 5: commit** `feat(ui): bridge for insert move and bypass, pinned bus, valid routing targets, catalogue problems, library signals`.

### Task 4: QML interactions

**Files:**
- Modify: `ui/qml/StripSlot.qml` (modifiers, move gesture, bypass toggle area), `ui/qml/ChannelStrip.qml`, `ui/qml/ProjectStrip.qml`, the Mixer view that hosts the strips (find with `Grep "ProjectStrip" ui/qml`), `ui/qml/Knob.qml` (`cancel`), `ui/qml/Splitter.qml`, `ui/qml/Library.qml` (disabled Save/Delete, scroll keep), `ui/qml/TrackHeader.qml` (double click), `ui/qml/Main.qml` (vertical splitter)
- Test: `ui/tests/tst_channelstrip.qml`, `tst_knob.qml` (or where Knob is tested), `tst_library.qml`, `tst_trackheader.qml`, `tst_splitter.qml`, `tst_smartcontrols.qml`

**Interfaces:**
- Consumes: Task 3 bridge.
- Produces: `StripSlot.clicked(int modifiers)`, `moveStarted()`, `moved(real x, real y)`, `moveReleased()`, `bypassToggled(bool on)`; `ChannelStrip` signals `busViewRequested(string busId)`, `insertMoveRequested(string trackId, int from, int to, string toTrackId)`, `insertBypassToggled(string trackId, int index, bool on)`, `sendPreFaderToggled(string sendId, bool on)`, `libraryRequested()`; `Splitter.orientation`.

- [ ] **Step 1: failing QML tests**, one per spec bullet:
  - Shift-click on a send slot emits `busViewRequested(targetId)`; Shift-click on the Output slot emits it with the output id; a plain click still opens the output menu.
  - Insert drag: a vertical drag on a Gain slot emits `insertMoveRequested(trackId, from, to, "")` with the right indexes (`clamp(from + round(dy / slotHeight), 0, n-1)`); a horizontal drag still emits `insertGainReleased`; a plug-in slot moves with a horizontal drag too; a release on the start position emits nothing; an `inserts` change mid-drag drops the drag (no signal on release); `dragIndex`/`dragGain` reset when `inserts` changes (minor 2).
  - Cross-track drop: the Mixer test drives two strips, drags a slot from strip A to strip B's area and expects `insertMoveRequested(A, from, to, B)` (the Mixer holds the strips and tells the source slot where the pointer is: use `mapToItem` on each strip to find the one under the pointer and the index from the slot geometry of that strip's `insertList`).
  - Bypass: the toggle on an insert emits `insertBypassToggled(trackId, index, on)`; bypassed slots are dimmed; Alt-click on the name toggles.
  - Send: right-click opens a menu with a checkable "Pre Fader" that emits `sendPreFaderToggled`; a pre send shows "pre".
  - Instrument slot click emits `libraryRequested()`; the header double click emits the controller path that selects the track and shows the Library; the name still renames on double click.
  - `Splitter` vertical: dragging up by 40 grows `smartControlsHeight` by 40 within 120..320.
  - Minors: `Knob.cancel()` restores `shown`; send knob `from == -96`; Save and Delete buttons are disabled; the patch list keeps `contentY` after a patch is applied; the Output and Send menus list `controller.targetsFor(trackId)` (bind `targets` to it plus `routingRevision`).
- [ ] **Step 2: run, expect FAIL.** `cmake --build build-ui --config Debug --parallel 2 && ctest --test-dir build-ui -C Debug -R qml`.
- [ ] **Step 3: implement.** `StripSlot`: keep the existing `MouseArea`; on first movement past 4 px decide the axis once; `property bool movable: true` (plug-ins: any direction; Gain: vertical only, horizontal keeps `dragged`). While moving, `Drag` is not used: the slot reports pointer positions in the Mixer's coordinate space via `mapToItem(mixerRoot, m.x, m.y)` and the Mixer draws the 2 px accent line in the strip under the pointer and applies `insertMoveRequested`. `ChannelStrip` exposes `function dropIndexAt(yInStrip)` (counts the insert delegates whose centre is above `y`) so the Mixer can compute `to`. The dragged slot follows the pointer with `opacity 0.6` and `z: 100`. `Knob.cancel()` also does `dragValue = value`. `Splitter` gets `property int orientation: Qt.Horizontal`, `cursorShape` by orientation, `dragged(delta)` along the axis; `Main.qml` puts a 5 px vertical `Splitter` on the top edge of the Smart Controls pane calling `controller.smartControlsHeight -= dy`.
- [ ] **Step 4: run the whole UI suite.** Expected: all pass.
- [ ] **Step 5: by-hand with windows-mcp**: drag a Gain slot up and down; drag a plug-in slot to another track (the audio keeps running); toggle bypass on a plug-in; Shift-click the Output slot; right-click a send; double click a header; drag the Smart Controls splitter.
- [ ] **Step 6: commit** `feat(ui): move and bypass inserts, bus pin, send pre/post, Library shortcuts, Smart Controls splitter, minors`.

### Task 5: Docs and cleanup

**Files:** `README.md`, `docs/superpowers/ui-b-deferred.md`

- [ ] **Step 1:** README: Mixer inserts paragraph (bypass, drag to reorder and to another track), the Shift-click bus view, send pre/post, Library shortcuts. `ui-b-deferred.md`: remove the done items; keep "Not verified" for what was not checked by hand; note the deferred `captureState()` empty-state ambiguity.
- [ ] **Step 2:** run all three suites one last time (`build-core`, `build-plugin`, `build-ui`); expected all pass.
- [ ] **Step 3: commit** `docs: README and deferred list after the UI-B follow-ups`.
