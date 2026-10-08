# UI-B, the panels: design

Part of the UI line after UI-A (the frame, `2026-10-08-ui-a-frame-design.md`). Status: draft for review. Date: 2026-10-08.
UI-C (editors, Loops, Quick Help content) follows with its own spec.

## 1. Purpose

Turn three stub toggles of the frame (`view.inspector`, `view.library`, `view.smartControls`) into real panels, so a
track can be shaped from the window the way it is in Logic Pro: pick a patch, see and edit the track and its channel
strip, turn a few big knobs. Then restyle the mixer in the same visual language. What has no engine behind it yet is
present, can be toggled and says so (UI-A rule); what runs on the Core today is real, with undo/redo, through JSON
commands only.

Reference: Apple's Logic Pro User Guide for Mac 12.3 (Inspector, Library and Smart Controls interface pages and the
pages linked from them), read on 2026-10-08. Layout and behaviour follow it; every icon and graphic is drawn from
scratch (`assets/icons`), nothing is copied or traced from Apple's material.

Success criteria:

- The three panels open and close from the control bar, the View menu and `I`, `Y`, `B`, and remember their state
  (visibility, width, collapsed sections) with the window settings.
- Inspector: Region and Track sections (collapsible) and two channel strips (selected track, its output) edit the model
  through commands; every real edit is one undo step; stub controls announce themselves with the UI-A toast.
- Library: categories and patches of the track's type, search, apply with click and arrow keys, Revert; applying a
  patch is one undo step.
- Smart Controls: the controls of the track's patch as labelled knobs; one knob can move several parameters; a drag is
  one undo step.
- The mixer uses the same strip component as the Inspector (one strip, two places).
- Core builds and tests pass without Qt; UI tests pass on Windows and macOS CI.
- Every feature that reaches the window ends with a screenshot in `docs/images/` (made with the screenshot options of
  the app, as in UI-A) and the README section updated in the same task: a task is not done until both are in.

Out of scope: Save/Delete of user patches, sound packs and the View filter, `.cst` / `.pst` / `.exs` files, Session
Player, Smart Control layouts, user mapping (Parameter Mapping, Learn) and hardware assignment (External Assignment),
Compare, the EQ tab, MIDI-in/out routing and other MIDI Track inspector fields, Quantize and the other
region playback parameters (no engine), Group and Automation slots (visual only), track stacks, VCA, surround
panners, recording, Quick Help content (UI-C), releasing a version (the owner decides).

## 2. Decisions already taken (conversation, 2026-10-08)

1. UI-B covers Inspector, Library and Smart Controls, plus the Logic-style mixer as the last phase of the plan, which
   can be split into its own cycle if the plan gets too big.
2. Patch data lives in the Core as `patches.json`, loaded at start (approach 1A): `id`, category, name, track kind,
   `instrument`, `inserts`, strip values, `smartControls`. Not stored in the project, not in C++.
3. Applying a patch is a transaction of existing-style commands (one undo step); no new "apply" command type.
4. Smart Controls are declared by each patch: a control has a label and a list of targets, so one knob can move
   several parameters (3A).
5. Icons and graphics: original SVGs in the same visual language, no Apple assets, not even modified ones.
6. The Quick Help area is not built here (UI-C).

## 3. What the Core can do today (limits that shape this spec)

- One effect, `builtin.gain` (parameter `gainDb`), and one instrument, `builtin.sine` (no parameters).
- A strip has gain, pan, mute, solo, inserts, sends (target, level, pre/post) and an output.
- Commands that exist: `set_strip`, `set_inserts`, `add_send`, `remove_send`, `set_track_props`, `add_track`.

So early patches differ only in strip values, an optional gain insert, name and category. That is deliberate: the
panels, data format and commands are real and complete; the catalogue grows with the engine (new processors only
add `patches.json` entries and one id in `processor_ids.h`). The README says so.

## 4. Layout

```
control bar  [Library][Inspector] ...                          (toggles become real)
left column (resizable, 200-320 px)  | tracks area (headers + timeline)
  Library                            |
  Inspector                          |
-------------------------------------+---------------------------------------------
Smart Controls (bottom, resizable height)
Mixer (existing place, Logic-style strips in the last phase)
toast / error bar
```

- Library and Inspector are stacked in the left column; either can be hidden, the other takes the space. Order and
  default heights follow Logic (Library above Inspector).
- Smart Controls is a bottom pane above the mixer; Mixer and Smart Controls can both be open.
- Panels follow the selected track (first selected track when several). No track selected: Library, Inspector (track
  and strips) and Smart Controls show a neutral "no track selected" state; the Region section follows the selected
  region.

### Inspector

Top to bottom: Region section, Track section, two channel strips side by side.

- **Region** (header "Region: <name>", collapsible, hidden when no region): Mute, Loop, Quantize, Transpose,
  Velocity Offset, More. Real now: Gain (`Region.gainDb`) and the name. The rest is visual only (toast on switching on /
  changing), as there is no playback support.
- **Track** (header "Track: <name>", collapsible): name (editable), icon (from the track kind; the picker is visual
  only), colour (real, the theme palette), Default Region, Transpose, Velocity Offset, Key Limit, Velocity Limit,
  Delay, No Transpose: visual only for now. Only the fields that fit the track kind are shown (as Logic does).
- **Channel strips**: left = selected track, right = its output (the master or the bus it sends to). Slots from the
  top, as in Logic: Setting (visual only), Instrument or Input slot (shows the instrument; picking opens the Library),
  Insert slots (add, remove, reorder by drag, gain parameter), Send slots with level knob (add, remove, target,
  level, pre/post), Output slot (choose master or a bus), Group and Automation (visual only), Pan knob, level field in dB,
  fader with meter, M and S, track name. The strip is one QML component (`ChannelStrip`) reused by the mixer.
- Shift-click on a Send or Output slot makes the right strip show that bus (as in the guide).

### Library

Track icon and patch name on top, search field, two lists (categories left, patches right), arrow keys move through
patches and apply immediately, bottom bar with Revert (back to the values the patch was applied with). Only patches of
the selected track's kind are listed. A track has one patch at a time (shown in the Inspector strip header); a track
that never had one shows its current settings with no patch selected. Double click on a track header with no
instrument/inserts opens the Library, as in Logic. Options menu, Save, Delete: shown disabled (out of scope).

### Smart Controls

Top bar: "Track" and the patch name. Left: the Smart Control inspector (layout name, Parameter Mapping and External
Assignment as collapsed sections that only say "not implemented yet"). Right: the screen controls, grouped in
labelled panels, drawn as knobs. Each knob shows its label and value; drag changes it (vertical drag, Shift for
fine, double click resets). Compare and the EQ tab are shown disabled.

## 5. Patch data

`core/data/patches.json` (installed next to the executables; path found like the demo data):

```json
{ "id": "audio.clean", "category": "01 Clean", "name": "Clean Vocal", "kind": "audio",
  "instrument": null,
  "strip": { "gainDb": -3.0, "pan": 0.0 },
  "inserts": [ { "processorId": "builtin.gain", "params": { "gainDb": 0.0 } } ],
  "smartControls": [
    { "id": "level", "label": "Level", "group": "Main", "min": -24, "max": 6, "default": 0,
      "targets": [ { "path": "strip.gainDb", "from": -24, "to": 6 } ] },
    { "id": "pan", "label": "Pan", "group": "Main", "min": -1, "max": 1, "default": 0,
      "targets": [ { "path": "strip.pan", "from": -1, "to": 1 } ] },
    { "id": "boost", "label": "Boost", "group": "Tone", "min": 0, "max": 1, "default": 0,
      "targets": [ { "path": "insert.0.gainDb", "from": 0, "to": 12 },
                   { "path": "strip.gainDb", "from": 0, "to": -3 } ] } ] }
```

- `kind`: `audio`, `instrument`, `aux`, `bus` (a patch applies only to tracks of its kind).
- `path` grammar: `strip.gainDb`, `strip.pan`, `insert.<index>.<param>` (the index refers to the inserts of the patch itself). A target maps the
  control range linearly (`from` to `to`, either direction) onto the parameter. Unknown paths and out-of-range indexes
  are load errors.
- `PatchLibrary` (Core, `lpc/patch_library.h`): loads and validates the file (unique ids, known processors, valid
  strip values, target paths that exist in the patch, `min < max`, default in range); lists by kind and category;
  `applyCommand(track, patchId)` returns the transaction that applies it; `smartControlCommand(track, controlId,
  value)` returns the transaction for a knob position. Problems are reported with the file path and the entry id; an
  invalid entry is skipped, the rest still loads.
- The patch applied to a track is remembered in the project (`Track.patchId`, optional string, JSON field present only
  when set; old files load unchanged). A missing patch id (patch removed from the catalogue) is not an error: the strip
  keeps its values and the Library shows no selection.

## 6. Core: new and changed commands

All JSON, exact inverse, validation from `lpc/validation.h` (a loaded project can never hold something a command
refused), unit tests, and in the random undo/redo property test.

- `set_instrument {trackId, instrument}`: instrument tracks only, id must satisfy `isKnownInstrument`; inverse: same
  command with the old value.
- `set_patch_id {trackId, patchId|null}`: records the patch in the project; inverse: old value. Only used inside the
  transactions below.
- New: `set_output {trackId, output|null}` (bus or aux, no self, no cycle; null is master), `set_send {sendId, levelDb?,
  preFader?}`, `set_region_gain {regionId, gainDb}`, `add_insert {trackId, insert, index}`, `remove_insert {trackId,
  index}`, `set_insert_param {trackId, index, param, value|null}`; each with exact inverse and tests. `set_inserts`,
  `set_strip`, `add_send`, `remove_send`, `set_track_props` already exist.
- Apply patch = transaction `[set_patch_id, set_instrument?, set_inserts, set_strip{gain,pan}]`. It leaves mute, solo,
  sends and output alone (as Logic keeps routing choices of the track unless the patch carries them). One undo step.
- Smart Control move = transaction of `set_strip` / `set_insert_param` commands for the targets, sent when the knob is
  released (as the faders do): one command per gesture.

Error codes reuse the existing ones: `bad_value`, `not_found`, `invalid_kind`, `duplicate_id`.

## 7. UI structure

- `ui/qml/`: `LeftColumn`, `Inspector`, `RegionInspector`, `TrackInspector`, `ChannelStrip` (shared with `Mixer`),
  `StripSlot`, `Library`, `SmartControls`, `ScreenKnob`, `PanelHeader` (collapsible), `Splitter`; `Main`, `Mixer`,
  `MixerStrip` (replaced by `ChannelStrip`), `ControlBar` adapted. No duplicated strip code.
- `ui/bridge/`: `PatchModel` (categories, patches, filtered by kind and search), `SmartControlModel` (controls of the
  current patch with value, label, group), `InspectorModel` (selected track and region, strip view for the left and right
  strips). Models are read-only views of the controller; edits go through controller wrappers.
- `ProjectController` gains: `inspectorVisible`, `libraryVisible`, `smartControlsVisible` (replacing the stub state of the
  three actions, so menu, button and shortcut agree), panel sizes, `applyPatch`, `revertPatch`, `setSmartControl`,
  `addInsert`, `removeInsert`, `setInsertParam`, `addSend`, `removeSend`, `setSendLevel`, `setSendPreFader`, `setOutput`,
  `setRegionGain`. QML never builds JSON for these.
- `actions.json`: `view.library`, `view.inspector`, `view.smartControls` change from `stub` to `ready`; new actions in
  the Track menu for the Library (next/previous patch) with no default shortcut. Shortcuts unchanged
  (`Y`, `I`, `B`).
- Panel visibility and sizes persist with `QSettings` (read and written by `main.cpp`, so tests never touch them).

## 8. Errors and edge cases

- A rejected command shows its message in the error bar and leaves the models unchanged (as today).
- `patches.json` missing or invalid: the Library says so in its own area, lists the valid entries, and the rest of the app
  works; a problem list is available to tests.
- Stale selection (track deleted, undo, project replaced): panels fall back to the neutral state; a knob drag in progress
  when the track disappears is cancelled with no command.
- Applying a patch while playing: allowed (engine already handles strip and insert changes).
- Dragging beyond the range clamps before building the command; NaN is ignored.
- Search with no match: "No patches match".
- Outputs: choosing an output that would create a cycle (a bus sending to itself or through itself) is refused by the
  Core with a clear message and shown in the toast area; the menu lists only valid targets.

## 9. Tests

- Core: `PatchLibrary` (valid file, every load error, listing by kind, apply and Smart Control transactions with exact
  undo), `set_instrument` and `set_patch_id` (success, every rejection, inverse, JSON round trip), property test with
  patch applications and knob moves mixed with the existing commands, a project with `patchId` round trips and old
  files without it load unchanged.
- Bridge (QtTest): models follow the selection, search filters, `ProjectController` panel state survives project
  replacement, a knob release is one undo step, wrappers build valid commands.
- QML (`qmltestrunner`, offscreen): Inspector sections collapse and expand, the strip edits send one command per
  gesture, Library arrow keys apply patches, a knob moves every target, the toast appears for stubs only when switched
  on, mixer and Inspector show the same `ChannelStrip`.
- By hand on Windows (and macOS when the owner tries it): drag feel of knobs and faders, panel resizing, keyboard
  navigation in the Library.

## 10. Plan outline

1. Core: `set_instrument`, `set_patch_id`, `Track.patchId` (JSON, validation, property test).
2. Core: `PatchLibrary`, `patches.json` with a first catalogue, tests.
3. Bridge: models and controller wrappers, panel state and persistence, the three actions become `ready`.
4. UI: `ChannelStrip` and `StripSlot` (new look), used first in the Inspector.
5. UI: left column, Inspector (Region, Track, strips), Shift-click bus view. Screenshot + README.
6. UI: Library. Screenshot + README.
7. UI: Smart Controls. Screenshot + README.
8. UI: Mixer on `ChannelStrip`, Logic-style (can be split into UI-B2). Screenshot + README.
9. Final checks and README pass (limits, patch catalogue, shortcuts table), screenshots refreshed where the frame changed.

Tasks 4 to 8 each include their own screenshot and README edit; task 9 only reviews them.

## 11. Risks

- The engine has one effect and one synth (section 3): a catalogue of near-identical patches can look empty. Mitigation:
  state it in the README and in the Library footer ("built-in patches: N"); the data format is already final, so
  growing the engine does not touch the UI.
- One strip component serving Inspector and Mixer is a coupling point; mitigate by keeping `ChannelStrip` free of layout
  assumptions (the parent decides width and height) and testing both parents.
- Smart Control coalescing must not hide a rejected command (a drag that produced an error leaves the model as it was
  and shows the message once, not per mouse move).
- Details of Logic's Inspector (which fields per track kind, strip slot order) were read from the guide's text and the
  figures; whatever differs from the real app is a data/layout fix, not an architecture change.
