# UI-B: deferred items (from the final review)

## Interactions not implemented (spec section 4)
- Shift-click on a Send/Output slot shows that bus in the right strip
- Reorder inserts by drag
- Send pre/post toggle (Core `setSendPreFader` exists, no QML calls it)
- Instrument slot and double-click on a track header open the Library
- Splitter for `smartControlsHeight`

## Tracks area vs Mixer
Done: `Track.showInTracks`, "New Bus" in the Send and Output menus, Track > Show in Tracks Area (spec `specs/2026-10-09-tracks-area-visibility-design.md`).

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
- By-hand pass of the plug-in window with third-party plug-ins (drag the slider, resize, DPI, close while playing) and of the Qt and JUCE shared message loop beyond the spike

## Next
- VST3 hosting as effects: done (spec `specs/2026-10-09-vst3-hosting-design.md`, plan `plans/2026-10-09-vst3-hosting.md`)
- Tracks area visibility of buses: done
- Then the deferred interactions and minor findings above
