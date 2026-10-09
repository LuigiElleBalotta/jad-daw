# UI-B: deferred items (from the final review)

## Interactions not implemented (spec section 4)
- Shift-click on a Send/Output slot shows that bus in the right strip
- Reorder inserts by drag
- Send pre/post toggle (Core `setSendPreFader` exists, no QML calls it)
- Instrument slot and double-click on a track header open the Library
- Splitter for `smartControlsHeight`

## Tracks area vs Mixer (reported 2026-10-09)
- Aux and Bus tracks always show in the Tracks area; in Logic a bus made from a send shows only in the Mixer, and appears in
  the Tracks area only when created as a track or shown on purpose (e.g. for automation). The model has no visibility flag
  (`TrackKind::Aux/Bus` only). Likely fix: a per-track `showInTracks` flag, false for buses created from a send, true for Aux
  tracks created with New Track, plus a menu entry to show or hide it. To be decided in its own small spec after VST3.

## Minor findings
- Knob `onCanceled` only partly covered by `cancel()`
- Stale insert drag value after undo mid-drag
- Output menu lists targets the Core refuses (cycle)
- Catalogue problems mostly invisible (no path, parse message dropped, not logged)
- Revert gives a misleading message when the patch left the catalogue
- Patch list scroll jumps to top after applying a patch
- Library Save/Delete announce a stub instead of being disabled
- Send knob spans -60..12 dB, Core allows -96..12

## Not verified
- By-hand Windows pass (menus, drag feel, resizing)
- CI for the UI-B commits (not passing at merge time, ignored on purpose)

## Next
- VST3 hosting as effects: spec `specs/2026-10-09-vst3-hosting-design.md`, then plan
- TODO after VST3: buses made from a send show only in the Mixer, not in the Tracks area (see "Tracks area vs Mixer"); small
  spec for a `showInTracks` flag and a show/hide menu entry
- Then the deferred interactions and minor findings above
