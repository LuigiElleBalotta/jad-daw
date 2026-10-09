# UI-B follow-ups: deferred interactions and minor findings

Date: 2026-10-09. Scope: the five interactions that `2026-10-08-ui-b-panels-design.md` section 4 promised and UI-B left out, and
the eight minor findings of the UI-B final review, as recorded in `docs/superpowers/ui-b-deferred.md`. Two Core commands are added
(`move_insert`, `set_insert_bypass`); everything else is bridge and QML. It is independent of `2026-10-09-tracks-area-visibility-design.md`; both
touch `ChannelStrip` menus, and the order of work in section 6 avoids conflicts.

## 1. Goal and scope

Make the Inspector and the Mixer strips behave the way the UI-B spec describes, and remove the rough edges found in review.

In scope: the five interactions (section 3), the eight fixes (section 4), `move_insert` (section 2).
Added by the user after the first review: bypass of an insert (section 2.2, 3.6) and dragging an insert onto another track
(section 2.1, 3.2). Also fixes found in a by-hand run of the VST3 UI: the scanner child must not show a CRT dialog on `abort()`
(`_set_abort_behavior`, `SetErrorMode`, `_CrtSetReportMode` in `tools/plugin-scanner`); plug-in hosting and the first scan start
with the application, not with the first open project; the Plug-in Manager message says what is really wrong; vendor submenu
entries and the Plug-in Manager footer follow the dark theme.

Out of scope: anything not in the deferred list (Parameter Mapping, Compare, the EQ tab, Save and Delete of patches, a real
insert menu for non-plug-in effects).

Decisions taken here, not in the UI-B spec:
- Double click on a track header opens the Library wherever it lands **outside the name and the R, I, M, S buttons**, whether or
  not the track has an instrument or inserts (the UI-B text limited it to tracks with neither; the simpler rule is predictable).
- The send knob goes down to -96 dB, the range the Core accepts, instead of the Core being narrowed.

## 2. Core: `move_insert` and insert bypass

### 2.1 `move_insert`

`move_insert {trackId, from, to, toTrackId?}` moves one insert. Without `toTrackId` it stays inside the track's chain and `to`
is the index the insert has after the move (`0 .. n-1`). With `toTrackId` (another track) the insert leaves `trackId` and is
inserted into `toTrackId` at `to` (`0 .. m`, `m` = the target's insert count). The whole insert travels with its processor id,
parameters, state, label and bypass flag. Errors: `not_found` (either track), `bad_index` (an index outside its range),
`bad_target` (`toTrackId` is the master or a track that cannot hold inserts). `from == to` on the same track is accepted and
changes nothing. The inverse is `move_insert` with the tracks and indexes swapped. JSON round trip, exact undo, and an entry in
the random undo/redo property test. The audio side needs nothing new: the config rebuild already rebuilds the insert chains.
A plug-in moved to another track is a new `InsertSlot` for the JUCE host; it keeps its instance through the same adoption
rule as an index change, extended to look at every track (the old track's entry is looked up by processor id and state).

### 2.2 Bypass

`ProcessorRef.bypass` (bool, default false), written to JSON **only when true** so existing projects stay byte-identical.
New command `set_insert_bypass {trackId, index, bypass}` (errors `not_found`, `bad_index`; inverse restores the old value).
A bypassed insert passes the signal unchanged. Its latency still counts in the delay compensation, so toggling bypass never
shifts the timing of other tracks (a bypassed insert is a delay line of the plug-in's latency, or the plug-in itself kept
loaded and its output ignored: the engine keeps the plug-in running so that switching back has no click and no reload).
Applies to built-in Gain and to plug-ins. Core tests: JSON (flag absent when false), validation of an unknown track and index,
audio (a bypassed Gain is identical to no insert), PDC (a bypassed plug-in insert keeps its `latencySamples`), exact undo, and
the property test.

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

### 3.2 Move inserts by drag (within a strip and to another track)

Decision (user): inserts can be dragged vertically to reorder and in any direction onto another track's strip.

- A plug-in slot has no horizontal meaning, so **any** drag past 4 px starts a move. A built-in Gain slot keeps its gain
  drag: the axis is decided once after 4 px, horizontal = gain (as today), vertical = move; once a move has started the pointer
  may go anywhere (including sideways onto other strips) and the gain drag is not available again until the next gesture.
- Slots grab the drag in `StripSlot` (signals `moveStarted()`, `moved(real x, real y)` in the Mixer's coordinates,
  `moveReleased()`); `ChannelStrip` stays a view, so the drop target is found by `ProjectStrip` / the Mixer, which know every
  strip. A strip under the pointer shows a 2 px accent line at the drop position (between two slots, or at the end); the
  dragged slot follows the pointer at reduced opacity.
- Drop on the same strip: `insertMoveRequested(trackId, from, to)` with the target index from the line position, clamped to the
  list. Drop on another strip that holds inserts (audio, instrument, bus, aux; not the master): `insertMoveRequested` with
  `toTrackId`. Drop anywhere else, or on the start position, sends nothing. One `move_insert` per gesture = one undo step.
- If the insert lists change during the drag (undo, plug-in rebuild) the drag is dropped, no command (fix 4.2 covers the
  gain drag too).

### 3.6 Bypass button on an insert

- Each insert slot has a small power toggle on its left (accent when on, dim when bypassed); a bypassed insert's name is dimmed
  and struck out. Click emits `insertBypassToggled(trackId, index, on)`; `ProjectStrip` calls `setInsertBypass`, one
  `set_insert_bypass` (one undo step). A missing plug-in (pass-through already) still shows the toggle.
- Shortcut: Alt-click on the name toggles it too (no new menu action in this round).

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

1. Core: `move_insert` (also across tracks) and `set_insert_bypass`, with tests.
2. Bridge: `targetsFor` and `routingRevision`, pinned bus, `moveInsert`, `LibraryModel` signals, catalogue problems, revert
   message, `libraryRequested` handling, with tests.
3. QML: `StripSlot` (modifiers, vertical drag, doubleClicked already exists), `ChannelStrip` (Shift-click, reorder, pre/post
   menu, instrument slot, stale drag, knob range), `Knob`, `Splitter`, `Library` (disabled buttons, scroll), `TrackHeader`, `Main`.
4. README and `ui-b-deferred.md` cleanup.

The visibility spec (`showInTracks`, "New Bus") is planned and built separately; if both are built, its "New Bus" entries go
above the list that `targetsFor` produces.
