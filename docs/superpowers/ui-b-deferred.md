# UI-B: deferred items (from the final review)

## Interactions not implemented (spec section 4)
- Shift-click on a Send/Output slot shows that bus in the right strip
- Reorder inserts by drag
- Send pre/post toggle (Core `setSendPreFader` exists, no QML calls it)
- Instrument slot and double-click on a track header open the Library
- Splitter for `smartControlsHeight`

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
- VST3 hosting as effects: separate Core/platform spec
