# UI-A, the frame: design

Part of the UI line after the UI shell (sub-project 2). Status: draft for review. Date: 2026-10-08.
UI-B (mixer and inspector) and UI-C (editors and panels) follow with their own specs.

## 1. Purpose

Make the window look and behave like a complete Logic-style workstation frame, so the whole interaction can be tried
now: control bar with LCD, local toolbar with tools, track headers, menus with shortcuts, region editing. What has no
engine behind it yet is present, can be toggled, and says so (section 5). What can run on the Core today is real.

Success criteria:

- The window matches the agreed layout (section 3): menu bar, control bar, local toolbar, track headers, timeline,
  mixer (existing), error/toast area.
- Every menu of Logic Pro (File, Edit, Track, Navigate, Record, Mix, View, Window, Help) exists with its items and
  default shortcuts; items that are not implemented are marked in the action table, not hidden.
- The real functions in section 4 work, each with undo/redo, through JSON commands only (no new path into the model).
- Switching an unimplemented toggle on shows a toast once ("not implemented yet"); switching it off shows nothing.
- Core builds and tests pass without Qt; UI tests pass on Windows and macOS CI.

Out of scope: real panels (Inspector, Library, editors, Smart Controls, Loops: UI-B and UI-C), Logic-style mixer
(UI-B), recording, metronome and count-in engine, copy/paste/duplicate, region looping, the shortcut-remapping screen,
preferences, releasing 0.0.2 (the owner decides).

## 2. Decisions already taken (conversation, 2026-10-08)

1. Scope split in three cycles: UI-A frame, UI-B mixer and inspector, UI-C editors and panels. This is UI-A.
2. Top of the window: layout "A" of the mockup: global control bar (panel toggles, transport, LCD, metronome / count-in
   / cycle / punch, master volume) and a local toolbar inside the tracks area (Edit / Functions / View menus, tools,
   snap, drag mode, zoom, catch playhead).
3. Track header: the complete version: type icon, number, name, R, I, M, S, volume and pan; what shows depends on the
   track height (40 px: name and M/S only).
4. Unimplemented controls are active with visual effect only, with a toast only when switched on.
5. Real functions: region resize, split and join, track management (new, delete, rename, colour), tempo and time
   signature from the LCD.
6. Data-driven action registry (approach A): one table drives menus, buttons, shortcuts and the toast.

## 3. Layout

```
menu bar:  JAD  File  Edit  Track  Navigate  Record  Mix  View  Window  Help
control bar: [Library][Inspector][Help][Smart Ctl][Mixer][Editors][Loops] | |<< << >> [] > (o) | LCD | Metro CountIn Cycle Punch  MasterVol
tracks area: [Edit][Functions][View] | pointer pencil eraser scissors glue | Snap  Drag  | Zoom  Catch
             track headers (230 px)  |  ruler + timeline
mixer:       (existing strips, toggled by the Mixer button)
toast / error bar
```

LCD: position in bars (bar beat division tick) or time (click toggles the view; double click edits and locates),
BPM and time signature (double click edits), key (visual only).

Track header (height dependent): colour chip, type icon, number, name (double click renames), R, I, M, S;
from 56 px also volume and pan sliders. Heights: the four steps of the theme (40, 56, 80, 120), changed from the
toolbar or the View menu.

## 4. Behaviour: what is real

| Area | Real now | Visual only (toast on switching on) |
|---|---|---|
| Panel toggles | Mixer | Library, Inspector, Quick Help, Smart Controls, Editors, Loops |
| Transport | to start, bar back, bar forward, stop, play, cycle | record, metronome, count-in, punch |
| LCD | position view and entry, BPM, time signature, master volume | key |
| Tools | pointer, pencil (empty region on instrument tracks), eraser, scissors, glue | other tools |
| Snap | off, bar, 1/2, 1/4, 1/8, 1/16 | smart |
| Drag mode | overlap | no overlap, cross-fade |
| View | horizontal zoom, track heights, catch playhead | waveform zoom, fit |
| Tracks | select (click, Shift, Ctrl), rename, colour, new, delete, type icon, M, S, volume, pan | R, I, freeze |
| Regions | select (also with a rectangle), move, resize, delete, split, join, create | copy, paste, duplicate, loop |
| Menus | every entry for the real items above, plus Undo, Redo, Select All, About JAD Daw | everything else |

M and S act on the selected tracks (with no track selected, on none). Selecting a region selects nothing else.

## 5. The action registry

`ui/actions/actions.json` is the single table of actions. One entry:

```json
{ "id": "transport.cycle", "label": "Cycle", "menu": "Navigate/Cycle", "shortcut": "C",
  "kind": "toggle", "status": "ready" }
```

Fields: `id` (stable, dotted), `label` (English, translatable with `qsTr`), `menu` (path, or empty for actions that
live only on a button), `shortcut` (portable text, `Ctrl` is Cmd on macOS; empty for none), `kind` (`command`,
`toggle`), `status` (`ready` or `stub`), optional `group` (radio sets such as snap values).

- `ActionRegistry` (C++, exposed to QML) loads the table and the user override file (`shortcuts.json`, same format as
  the UI shell: id to sequence), reports unknown ids and clashing sequences as problems and ignores those overrides.
- QML component `JadAction` wraps `Action` for one id: text, shortcut, checkable and enabled come from the registry.
  When triggered: `ready` calls the handler bound in `Main.qml` / the controller; `stub` flips the visual state if it is a
  toggle and shows the toast when it turns on (a stub command shows the toast every time).
- The menu bar is generated from the table with a `Repeater` per menu, in table order.
- Shortcut defaults: Logic Pro's. Not all could be confirmed from Apple's pages (they render with JavaScript and the
  tables are cut off when fetched, and the sources disagree on some keys). Confirmed by at least one source: Space
  play/stop, C cycle, P pencil, E eraser, T tool menu. The rest of `actions.json` is a **proposal to check against
  Logic Pro's own Key Commands window (Option-K)**, and the implementation plan has a task for that check. A wrong
  default is a data fix, not a code change.

## 6. Core: new commands

All JSON, with an exact inverse, using the shared validation of `lpc/validation.h` (so a loaded project can never hold
something a command refused); each has unit tests and joins the random undo/redo property test.

- `replace_region {region}`: replaces the region with the same id by the given one (validated). Building block for the
  inverses below; also usable by clients.
- `resize_region {regionId, start, length}`: `length > 0`, both within `kMaxPosition`. Moving the left edge keeps the
  content where it is: audio regions adjust `sourceOffsetFrames` by the moved distance (rejected when it would become
  negative); MIDI regions keep note positions and drop notes that fall entirely outside. Extending the right edge past
  the end of the media is allowed (silence). Inverse: `replace_region` with the old region.
- `split_region {regionId, at, newRegionId}`: `at` strictly inside the region; the caller gives the id of the right
  part (deterministic, serialisable). Audio: the right part's offset advances by the distance. MIDI: a note starting
  before `at` stays in the left part, cut at `at`; notes starting at or after go to the right part. Inverse:
  `replace_region` with the original plus `remove_region` of the new one.
- `join_regions {regionIds}`: two or more regions of one track, adjacent without gap or overlap, all audio of the same
  media with a continuous source offset, or all MIDI. The first id survives. Inverse: `replace_region` of the first
  plus `add_region` of the others.
- `set_track_props {trackId, name?, color?}`: name 1 to 64 characters, colour one of the theme palette names.
  Inverse: the same command with the previous values.
- `set_signature {tick, numerator, denominator}`: denominator 1, 2, 4, 8, 16 or 32, numerator 1 to 32. Needs
  `TempoMap::signatureEventAt` and `TempoMap::removeSignature` (new, tested) for the inverse.
- Existing commands cover the rest: `add_track`, `remove_track`, `set_strip`, `set_tempo`, `add_region`,
  `remove_region`, `move_region`.

Error codes reuse the existing ones: `bad_region`, `bad_value`, `not_found`, `duplicate_id`.

## 7. UI structure

- `ui/actions/`: `actions.json`, `action_registry.{h,cpp}`.
- `ui/qml/`: `ControlBar`, `Lcd`, `ToolBar`, `TrackHeader` (replaces the current track list rows), `Toast`,
  `ActionMenuBar`, `JadAction`; `TrackList`, `Timeline`, `RegionItem`, `Main` adapted.
- `ProjectController` gains: active tool, snap value, catch playhead, track heights, selection of tracks and regions,
  and wrappers `resizeRegion`, `splitRegion`, `joinRegions`, `createRegion`, `renameTrack`, `setTrackColor`,
  `addTrack`, `setTempo`, `setSignature`, `setMasterGain`. QML never builds JSON by hand for these.
- Selection lives in the controller (not in QML) so menus, shortcuts and the timeline agree.

## 8. Errors and edge cases

- A rejected command shows its message in the error bar and leaves the models unchanged (as today).
- LCD edits are parsed and range-checked before a command is built (BPM 20 to 999, valid signature); an invalid value
  restores the old one and shows a toast.
- Dragging or typing far outside the valid range clamps before building the command (negative, beyond 2^40, NaN).
- Split at a position not strictly inside, join of non-adjacent regions, resize to zero length: rejected by the Core with
  a clear message; the tools show it in the toast area and change nothing.
- Pencil on a track that cannot hold MIDI regions: toast "this track cannot hold MIDI regions".

## 9. Tests

- Core: unit tests per new command (success, every rejection, exact inverse, JSON round trip), property test of random
  command sequences with undo to the start, `TempoMap` tests for the new calls.
- Registry (QtTest): the JSON is valid, every action has a label and a unique id, no two actions share a default
  shortcut, every `menu` path belongs to a known top menu, an override with an unknown id or a clash is reported and
  ignored, a stub toggle shows the toast once on switching on and not on switching off.
- QML (`qmltestrunner`, offscreen): dragging a region edge sends one `resize_region` on release; each tool sends the
  right command; M/S act on selected tracks; LCD rejects out-of-range input; header content follows track height.
- By hand on Windows (and macOS when the owner tries it): menus, shortcuts, toasts, drag feel.

## 10. Plan outline

1. Core commands: `replace_region`, `resize_region`, `split_region`, `join_regions` (+ tests, property test).
2. Core commands: `set_track_props`, `set_signature`, `TempoMap` additions.
3. Action registry: table, `ActionRegistry`, `JadAction`, toast, generated menu bar.
4. Shortcut check against Logic's Key Commands and fixes to the table.
5. Control bar and LCD.
6. Track headers and track selection, heights.
7. Local toolbar: tools, snap, drag mode, zoom, catch playhead.
8. Region editing in the timeline: resize, split, join, create, rectangle selection.
9. Menus complete, About dialog, README, final checks.

## 11. Risks

- Logic's shortcut list is long and partly unverified here (section 5); wrong defaults are cheap to fix but easy to
  get wrong silently. Mitigation: plan task 4 and a note in the README until checked.
- Many visual-only toggles can mislead; the toast on switching on is the safeguard, and the action table is the one
  place to see what is real.
- Region split and join on MIDI regions touch note data; the property test must include MIDI regions with notes.
