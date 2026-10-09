# UI-B follow-ups: deferred interactions and minor findings

Date: 2026-10-09. Scope: the five interactions that `2026-10-08-ui-b-panels-design.md` section 4 promised and UI-B left out, and
the eight minor findings of the UI-B final review, as recorded in `docs/superpowers/ui-b-deferred.md`. One Core command is added
(`move_insert`); everything else is bridge and QML. It is independent of `2026-10-09-tracks-area-visibility-design.md`; both
touch `ChannelStrip` menus, and the order of work in section 6 avoids conflicts.

## 1. Goal and scope

Make the Inspector and the Mixer strips behave the way the UI-B spec describes, and remove the rough edges found in review.

In scope: the five interactions (section 3), the eight fixes (section 4), `move_insert` (section 2).
Out of scope: anything not in the deferred list (Parameter Mapping, Compare, the EQ tab, Save and Delete of patches, a real
insert menu for non-plug-in effects).

Decisions taken here, not in the UI-B spec:
- Double click on a track header opens the Library wherever it lands **outside the name and the R, I, M, S buttons**, whether or
  not the track has an instrument or inserts (the UI-B text limited it to tracks with neither; the simpler rule is predictable).
- The send knob goes down to -96 dB, the range the Core accepts, instead of the Core being narrowed.

## 2. Core: `move_insert`

`move_insert {trackId, from, to}` moves one insert inside the track's insert chain. `to` is the index the insert has after the
move (`0 .. n-1`). Errors: `not_found` (no such track), `bad_index` (either index outside `0 .. n-1`). `from == to` is accepted
and changes nothing. The inverse is `move_insert {trackId, from: to, to: from}`. JSON round trip, exact undo, and an entry in the
random undo/redo property test. The audio side needs nothing new: the config rebuild already rebuilds the insert chain, and the
plug-in host keeps a live instance when its insert changes index (JUCE host adoption, VST3 work).

## 3. Interactions

### 3.1 Shift-click on a Send or Output slot shows that bus in the right strip

- `StripSlot.clicked` becomes `clicked(int modifiers)`; existing `onClicked` handlers that ignore the argument keep working.
- `ChannelStrip` emits `busViewRequested(string busId)` for a Shift-click on a send slot (its target) or on the Output slot
  (the output; the master is a valid bus here). A plain click keeps today's behaviour (the Output slot opens the output menu; a
  send slot does nothing).
- `InspectorModel::update` takes a `pinnedBusId`. When it names an existing track the right strip shows that track instead of the
  output of the shown track; `InspectorModel` gets `bool pinned`. `ProjectController` keeps `inspectorBusId_`: set by
  `showBus(busId)` (invokable), cleared when the shown track changes, when the pinned track is deleted, and when the project is
  replaced. It is view state: not in the project, not undoable, not persisted.
- A pinned right strip is drawn with the track colour bar and a small "pinned" accent on its name; Shift-click on the Output slot
  of the left strip while pinned shows the output again.

### 3.2 Reorder inserts by drag

- `StripSlot` decides the drag axis after 4 px: horizontal keeps today's meaning (insert gain; plug-in slots ignore it), vertical
  starts a reorder. New signals `verticalDragged(real dy)` and `verticalReleased()`.
- During a vertical drag the slot follows the pointer and a 2 px accent line shows the drop position. The target index is
  `clamp(index + round(dy / slotHeight), 0, count - 1)`.
- Release with a target different from the start emits `insertMoveRequested(trackId, from, to)`; `ProjectStrip` calls
  `ProjectController::moveInsert`, one `move_insert` command (one undo step). Release on the same index sends nothing.
- If the inserts list changes during the drag (undo, plug-in rebuild) the drag is dropped, no command (fix 4.2 covers the
  gain drag too).

### 3.3 Send pre/post toggle

- Right-click on a send slot opens a small menu with one checkable entry "Pre Fader" (checked when the send is pre-fader).
  Choosing it emits `sendPreFaderToggled(sendId, on)`; `ProjectStrip` calls `setSendPreFader` (exists in the controller and the
  Core, `set_send`).
- A pre-fader send shows the text "pre" in the right of its slot (the `value` of `StripSlot`).

### 3.4 Instrument slot and track header open the Library

- Click on the Instrument slot of a strip emits `libraryRequested()`; `ProjectStrip` sets `libraryVisible = true`. The slot looks
  clickable (hover state like other slots).
- Double click on a track header outside the name (which renames) and the buttons emits `libraryRequested(trackId)`; the
  controller selects that track (`replace`) and sets `libraryVisible = true`.

### 3.5 Splitter for the Smart Controls height

- `Splitter` gets `orientation` (`Qt.Horizontal` handle for the left column as today, `Qt.Vertical` for a handle that moves up
  and down) with the matching cursor; `dragged(delta)` reports the movement along the axis.
- A vertical `Splitter` (5 px) sits on the top edge of the Smart Controls pane in `Main.qml`; dragging up grows the pane:
  `smartControlsHeight -= dy`. The existing setter clamps to 120..320 and the value is already saved with the panel layout.

## 4. Minor findings

1. **Knob cancel.** `Knob.cancel()` also puts `dragValue` back to `value`, so `shown` returns to the model value after a cancelled
   gesture; `onCanceled` and a track change both use it. No `released` is emitted.
2. **Stale insert drag after undo.** `ChannelStrip` clears `dragIndex` and `dragGain` when its `inserts` change.
3. **Output and send menus list targets the Core refuses.** `ProjectController::targetsFor(trackId)` (invokable) returns the
   buses and auxes `B` with `B != track` and where `B` does not reach `track` through outputs and sends (the Core's `reaches`
   rule, computed from the snapshot rows). `routingRevision` (a counter that changes with every snapshot) lets bindings refresh.
   Both menus use it; `ProjectStrip` stops filtering `busTargets` itself.
4. **Catalogue problems are invisible.** `loadPatchCatalogue` keeps the resource path and the JSON parse message in a problem
   (`PatchLibrary::unreadable(path, message)` returns a library with that one problem), every problem is logged once at startup
   with `qWarning`, and the Library footer reads "Built-in patches: N" plus " (M problems)" when there are any, with a tooltip
   listing them (each with the file and the entry id).
5. **Revert after the patch left the catalogue.** When the track's `patchId` is not in the catalogue the notice says
   "Patch '<id>' is no longer in the catalogue" and nothing changes; "This track has no patch to revert" stays for an empty id.
6. **Patch list jumps to the top after applying a patch.** `LibraryModel` splits `changed` in `patchesChanged` (the list changed)
   and `currentChanged` (the selected patch changed); `patches` only notifies when its content differs. The list keeps its scroll
   position when a patch is applied or reverted.
7. **Save and Delete of patches announce a stub.** The two buttons are disabled (like the Options button), no notice.
8. **Send knob range.** The send `Knob` spans -96..12 dB (`from: -96`), the range the Core accepts, so a send set to -80 dB by a
   command is shown where it is and keeps its value until touched.

## 5. Errors and edge cases

| Situation | Behaviour |
|---|---|
| `move_insert` with a bad index | rejected, message in the error bar, models unchanged |
| Reorder drag released outside the strip | target clamps to the first or last slot |
| Pinned bus deleted (undo of its creation) | the right strip returns to the output of the shown track |
| `targetsFor` with no snapshot yet | empty list |
| Shift-click on a send whose target no longer exists | ignored |

## 6. Testing

1. Core: `move_insert` (moves, same index, bad index, unknown track, inverse, JSON, property test).
2. Bridge (QtTest): `inspectorBusId_` set, cleared on selection change and on deletion, `pinned` flag; `targetsFor` excludes
   self, a bus that outputs to the track, and a bus that reaches it through a send; `moveInsert` is one undo step;
   `loadPatchCatalogue` problems carry path and parse message (a broken resource), logging called once; revert notice for an
   unknown patch id; `LibraryModel` does not emit `patchesChanged` when only the current patch changes.
3. QML: Shift-click on send and output slots emits `busViewRequested`; a vertical drag emits `insertMoveRequested` with the
   right indexes, a horizontal drag still changes gain, a plug-in slot does not change gain; right-click on a send toggles
   `sendPreFaderToggled` and a pre send shows "pre"; the Instrument slot and a header double click emit `libraryRequested`, the
   name double click still renames; the splitter changes `smartControlsHeight` within the clamp; `Knob.cancel()` restores `shown`;
   inserts changing during a drag drops it; the patch list keeps `contentY` after applying a patch; Save and Delete are disabled;
   the send knob has `from == -96`.
4. By hand on Windows: the drag feel of the reorder, the splitter, the right-click menu, the Library opening.

## 7. Risks

- Vertical and horizontal drags on one slot can fight (a gain drag that drifts vertically). The 4 px axis lock decides once per
  gesture and the other axis is ignored for the rest of it.
- `routingRevision` binding churn: it changes with every snapshot, so menus re-evaluate often; the lists are tiny, so this is
  accepted.
- Splitting `LibraryModel.changed` touches every binding on it; the QML tests for the Library and Smart Controls cover the
  affected paths.

## 8. Order of work (for the plan)

1. Core: `move_insert` with tests.
2. Bridge: `targetsFor` and `routingRevision`, pinned bus, `moveInsert`, `LibraryModel` signals, catalogue problems, revert
   message, `libraryRequested` handling, with tests.
3. QML: `StripSlot` (modifiers, vertical drag, doubleClicked already exists), `ChannelStrip` (Shift-click, reorder, pre/post
   menu, instrument slot, stale drag, knob range), `Knob`, `Splitter`, `Library` (disabled buttons, scroll), `TrackHeader`, `Main`.
4. README and `ui-b-deferred.md` cleanup.

The visibility spec (`showInTracks`, "New Bus") is planned and built separately; if both are built, its "New Bus" entries go
above the list that `targetsFor` produces.
