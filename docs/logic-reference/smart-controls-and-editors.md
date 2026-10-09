# Logic Pro 11.2: Smart Controls and the Editors area

Seen on 2026-10-09. Control bar buttons (left group): Library, Inspector, Quick Help, Toolbar. Second group: **Smart Controls**, **Mixer**,
**Editors**. Each toggles an area; Mixer / Editors / Smart Controls open in the lower half of the window below the Tracks area and the
Tracks area keeps the upper half. A button is lit (lighter background) when its area is shown.

## Smart Controls (lower area)

Header row, left to right: a small icon button (screen-control / plug-in) · segmented **Track | Master ▾** (Track lit) · **Compare** ·
centred segmented **Controls | EQ** (Controls lit) · at the far right an icon button (Parameter Mapping).
Body: a black panel with the screen controls of the patch. For a track whose instrument has no patch layout, eight generic rotary knobs
labelled `PARAM 1` to `PARAM 8` in a row, each with two small dots under it (the range ends). Knob caption above, in small caps grey.

## Editors area (lower area)

Top: a centred segmented control **Piano Roll | Score | Step Sequencer | Session Player** (Piano Roll lit). Below it the editor's own
local menu bar, then the editor.

### Piano Roll

- **Local menus:** Edit ▾ · Functions ▾ · View ▾, then icon toggles (a quantize-ish icon, an automation curve icon, two link/colour
  toggles in green, a note-input toggle, and a "link" button in yellow = Link mode), a tool popup (pointer + pencil as the two tools: the
  left-click tool and the command-click tool), a name field, **Snap: Smart ▾**, a "ruler follows" toggle, zoom buttons and two sliders.
- **Left pane (the editor's inspector):** the track icon and name ("Inst 1") with "No Regions selected" below it; a button at the right of
  it that opens the region list. Then:
  - **Time Quantize:** note value popup (1/16 Note), a **Q** button, **Strength** slider (100), **Swing** slider (50).
  - **Scale Quantize:** two popups (Off, Major (lo…) ) and a **Q** button.
  - **Velocity:** slider (80) shown at the bottom.
- **Keyboard and grid:** a vertical piano keyboard with labelled C notes (C3, C2…), a time ruler on top (bars with 1/2/3/4 ticks and beat
  subdivisions, e.g. "1", "1 3", "2", "2 3", "3", …), dark horizontal lanes alternating for black/white keys. No notes are shown when no region
  is selected.
- **Functions menu:** Time Handles ⌃T · (sep) Quantize Notes Q · Dequantize ⌥⌘Q · Include Non-Note MIDI Events · (sep) ✓ Mute Notes On/Off
  ⌃M *(dim)* · Convert Sustain Pedal to Note Length · Set MIDI Channel to Voice Number · Insert Instrument MIDI settings as Events *(dim)* ·
  (sep) MIDI Transform ▸ *(dim)* · (sep) Lock SMPTE Position ⌘↓ *(dim)* · Unlock SMPTE Position ⌘↑ *(dim)*.
- Not captured: Edit and View menus of the Piano Roll, the automation/MIDI area below the grid, the velocity lane, note colouring,
  Score / Step Sequencer / Session Player tabs.

## Notes for the clone

- The Editors area is a **tab strip with four editors**, not only the Piano Roll; the first implementation can show Piano Roll and
  grey out the others.
- The Piano Roll left pane doubles as the place for quantize and velocity defaults; the "Q" buttons apply to the selected notes.
