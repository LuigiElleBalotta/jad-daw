# Tracks area visibility of buses: design

Date: 2026-10-09. Scope: a per-track flag that hides a bus or aux track from the Tracks area while it stays in the Mixer,
"New Bus" from a send or an output slot, and the controls to show or hide a track. Follows the note in
`docs/superpowers/ui-b-deferred.md` ("Tracks area vs Mixer").

## 1. Goal and scope

In Logic, a bus made from a send slot ("Send > New Aux") appears in the Mixer only; the Tracks area shows an Aux track only
when it was created as a track (New Track) or when the user shows it on purpose (for example to automate it). Here every
Bus or Aux track is always in the Tracks area, and there is no way to make a bus from a send.

In scope:
- `Track.showInTracks` in the model, JSON, validation and `set_track_props`.
- Tracks area and timeline rows skip hidden tracks; the Mixer always lists every track.
- "New Bus" entries in the Send and Output slot menus, creating the bus hidden and routing to it in one undo step.
- A way to select a track from the Mixer (a hidden bus has no header to click), and a Track menu action to show or hide it.

Out of scope: automation lanes of a hidden track, a "show all tracks" view mode, hiding audio or instrument tracks, groups
and folders, Track Stacks.

## 2. Model, JSON and commands (Core)

- `Track` gains `bool showInTracks = true`. It takes part in `operator==`.
- JSON: the field `showInTracks` is written **only when false**, so existing projects and golden files stay byte-identical;
  a missing field means true; a value that is not a boolean is a load error.
- Validation (`checkProject` and every command that creates or edits a track): `showInTracks == false` is allowed only on
  tracks of kind Bus or Aux (error `bad_value`, message "only buses and auxes can be hidden from the Tracks area"). No rule about
  regions is needed: `checkRegion` already refuses regions on any track that is not audio, MIDI or instrument
  (`invalid_kind`), so a bus or aux never holds one and a hidden track never has a region to lose. The master is never hidden
  (it is not a Tracks area row anyway).
- `TrackPatch` (used by `set_track_props`) gains `std::optional<bool> showInTracks`; the JSON command accepts the field
  `showInTracks`; the inverse restores the previous value; `add_track` carries the flag inside the `Track` JSON.
- Core tests: JSON round trip (flag false, flag absent in old files, golden project unchanged), validation of audio and
  instrument tracks, `set_track_props` with exact inverse, the random undo/redo property test with the new patch field.

## 3. Bridge

- `TrackRow` gains `showInTracks`; `snapshot.cpp` fills it.
- `TrackListModel` (the Tracks area headers) lists only the rows with `showInTracks`; its numbering ("1", "2", ...) counts
  the shown tracks only. `RegionRow::trackIndex` (the timeline row) is the index among the **shown** non-master tracks, so
  the timeline and the headers agree. A region always belongs to a shown track (only audio, MIDI and instrument tracks hold regions, and they are never hidden),
  so no region is ever dropped.
- `MixerModel` lists every track, shown or not.
- `ProjectController`:
  - `newBusFor(trackId, role)` with `role` `"send"` or `"output"`: one transaction `[add_track{kind bus, name "Bus N",
    showInTracks false}, add_send{trackId -> new bus} | set_output{trackId -> new bus}]`, so one undo removes both. `N` is the
    smallest positive number that makes the name unused among the tracks. The new bus is selected in the Inspector.
  - `setShowInTracks(trackId, on)`: one `set_track_props`.
  - `addTrack("bus")` (Track menu, "New Bus") keeps creating a **shown** bus, like New Track in Logic.
- The Inspector and the Library follow the selection as before, whether or not the selected track is shown.

## 4. UI

- `ChannelStrip`: the "Send +" menu and the Output menu get a first entry "New Bus" (before the existing targets,
  separated by a line). Choosing it calls `newBusFor`. The component stays a pure view: it emits `newBusRequested(id, role)`,
  and `ProjectStrip` wires it to the controller.
- `ChannelStrip` track name (bottom label) becomes a selection target: click selects the track, Shift extends and Ctrl toggles,
  with the same modes as the track headers (`controller.selectTrack(id, mode)`). The Master strip is not selectable.
- Track menu: new action `track.showInTracks` ("Show in Tracks Area", toggle, no default shortcut, status `ready`). Its checked
  state is the flag of the first selected track; it is enabled only when the selected track is a bus or aux. Triggering it
  calls `setShowInTracks`.
- When the last selected track becomes hidden it stays selected (the Inspector keeps showing it); the Tracks area simply has
  no row for it.
- README: the Mixer paragraph says that buses made from a send are Mixer-only and how to show them.

## 5. Errors and edge cases

| Situation | Behaviour |
|---|---|
| Project made before this change | every track shown, no field in the file |
| Hidden bus deleted from the Mixer selection (Track > Delete Track) | works; one undo brings it back hidden |
| Two sends to the same new bus | the second send picks the existing bus from the menu as today; "New Bus" always makes another |
| Output menu on a bus: "New Bus" | the new bus is hidden and the output is set to it; cycles are impossible (the new bus has no outputs) |
| Toggling on an audio track | the action is disabled, no command |
| Undo of "New Bus" while the Inspector shows it | the Inspector falls back to the neutral state (existing behaviour for stale selection) |

## 6. Testing

1. Core: as in section 2.
2. Bridge (QtTest): the Tracks area model skips hidden rows and renumbers; `trackIndex` of regions follows the shown tracks;
   `newBusFor` makes one undo step (bus and route disappear together) for both roles; `setShowInTracks` round trip; the Mixer
   model still has every track.
3. QML: the Send and Output menus have "New Bus" and emit `newBusRequested`; clicking the strip name emits a selection request
   with the modifier modes; the Track menu action is disabled for an audio track (action table test).
4. By hand on Windows: create a bus from a send, see it only in the Mixer, select it from its strip name, show it in the
   Tracks area, undo.

## 7. Risks

- Row indexes now depend on the flag: the timeline and the headers must use the same filtered list. Mitigation: one function
  in the snapshot builder produces the shown-track order for both, with a test that compares them.
- A hidden track that is selected has no visual anchor in the Tracks area. Accepted for v1; the strip name highlight in the
  Mixer shows the selection.

## 8. Order of work (for the plan)

1. Core: field, JSON, validation, `set_track_props`, tests.
2. Bridge: snapshot, models, controller wrappers and tests.
3. UI: menus, strip name selection, Track menu action, README.
