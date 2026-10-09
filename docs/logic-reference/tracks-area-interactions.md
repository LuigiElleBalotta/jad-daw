# Logic Pro 11.2: Tracks area interactions (verified by hand)

Seen on 2026-10-09 with the Pointer tool. The remote view does not show the mouse cursor shape, so cursor changes are not recorded.

- **Playhead:** the playhead is the thin vertical line from the ruler to the bottom with a small grey **handle ("Playhead thumb", help tag) in the ruler**. Dragging the
  handle left or right moves it; the LCD position (`102 1` to `60 1`) follows live. A click in the ruler also sets it ("Move to Playhead" in Undo History).
- **Resizing a region:** dragging the **right edge** of a region with the Pointer shortens or lengthens it (here 40 px to the left shortened it). When several regions
  are selected the same delta applies to every selected region (three audio regions ended 40 px earlier together). The undo entry is "Length Change"; the Tempo track
  below shows tempo points moving in sync because these regions are Smart-Tempo analysed. The left edge trims the start the same way (Trim).
- **Moving a region:** drag shows the help tag **"Move Region: Position 171 1 1 1 +42 0 0 0 / Length 1 0 0 0 / Track: 3 Inst 1"**, snapped by Snap: Smart; the undo entry is "Drag".
- **Tools:** the left-click tool and the command-click tool are two popups; the Tracks area list has 17: Pointer, Pencil, Eraser, Text, Scissors, Join, Solo, Mute, Zoom,
  Fade, Automation Select, Automation Curve, Marquee, Flex, Gain, Slip, Rotate (the Piano Roll's has 13). With the Pencil on an empty instrument lane a drag creates a MIDI
  region of that length; with the automation view on it draws automation (see `automation.md`).
- **Track selection / Inspector:** selecting a region switches the Inspector to "Region: <name>" and the Library to the track's kind; selecting several shows "Region: 3 selected".
- **Undo hygiene:** all steps appear in Edit > Undo History; every item is a command ("Create New Tracks", "Drag", "Insert events", "Change Note Length", "Automation Edit",
  "Length Change", "Flex Mode", "Move to Playhead", "Create Initial Automation Node").

## Snap and Drag popups (seen 2026-10-09, nothing was changed)

The first click after the focus was in another area (here the Mixer) only moves the focus: it shows a help tag ("Drag Mode") or the little **power switch** that appears at the
left of "Snap:" on hover. The popup opens on the next click.

- **Snap:** popup (current value "Smart"), top to bottom:
  - ✓ **Snap to Grid** ⌘G (the same switch as the power icon beside the label)
  - values (one is checked): **Smart**, Bar, Beat, Division, Ticks, Frames, Quarter Frames, Samples
  - ✓ **Snap Regions to Relative Value** (default) / Snap Regions to Absolute Value (a radio pair)
  - **Snap Flexed Audio Regions by First Downbeat** (dim, checked) · Snap Quick Swipe Comping · **Snap Edits to Zero Crossings** ⌃0
  - **Snap Automation ▸** (dim) · Automation Snap Offset…
  - **Alignment Guides** ⌥⌘G
- **Drag:** popup (current value "No Overlap"): Overlap · ✓ **No Overlap** · X-Fade · Shuffle R · Shuffle L.
- Both popups also exist in the Piano Roll and Audio editor toolbars (the lower editor in the screenshot shows "Snap: Smart" too).
