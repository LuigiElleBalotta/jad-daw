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
- **Edit menu (Piano Roll):** Undo <last action> ⌘Z · Redo ⇧⌘Z · Undo History… ⌥⌘Z · Delete Undo History · (sep) Cut ⌘X · Copy ⌘C · Paste
  ⌘V · Paste Replace ⇧⌘V · Paste at Original Position · Delete · (sep) Select ▸ · Repeat ▸ *(dim)* · Length ▸ *(dim)* · Split ▸ · Join Notes
  ⌘J *(dim)* · Move ▸ · Trim ▸ · Transpose ▸ · (sep) Copy MIDI Events… *(dim)* · Delete MIDI Events ▸ *(dim)*.
- **View menu (Piano Roll):** ✓ Link · (sep) One Track · ✓ Selected Regions · (sep) Hide Local Inspector ⌥⇧I · Drum Names *(dim)* · Note Labels ·
  (sep) ✓ Region Transpose · Set Note Color ▸ · Scroll in Play ⌃< · (sep) Secondary Ruler ⌃⌥⌘R · Show Global Tracks G · Configure Global
  Tracks… ⌥G · Global Track Protect Buttons *(dim)*.
- Not captured: the automation/MIDI area below the grid, the velocity lane, note colouring, Score and Session Player tabs.

### Piano Roll with a MIDI region selected

- The segmented tab strip depends on the selection: with a MIDI region of an instrument track it reads **Piano Roll | Score | Smart Tempo**
  (the earlier capture without a region showed Piano Roll | Score | Step Sequencer | Session Player).
- The local bar has a **position/pitch readout** at the right (e.g. `D#3  3 1 4 161`, `A7  126 2 4 201`): note name under the pointer, then bar, beat,
  division and ticks; it follows the mouse.
- The region appears at the top of the grid as a thin pale-green bar with the region name ("Inst 1") at both ends; the grid ruler shows
  bar numbers with beat subdivisions (e.g. `64`, `64 2`, `64 3`, `64 4`).
- Left pane header: track icon tile (green), region name, "on Track Inst 1", and a button at the right to list the regions. Time Quantize (classic)
  with a `1/16 Note` popup and **Q** button, Strength slider (100), Swing slider (50); Scale Quantize with two popups (Off, Major (lo…)) and **Q**;
  Velocity slider (80).
- Creating a region: with the **Pencil tool** (tool popup in the Tracks area, shown with a pencil icon) a click on an empty part of an instrument track
  makes a very short MIDI region; the Inspector switches to "Region: Inst 1" and the Piano Roll opens on it with the Editors button. A double click with
  the pointer on empty space did nothing.

### Step Sequencer (tab)

Same local bar (Edit ▾ · Functions ▾ · View ▾, icon toggles, a segmented **On/Off | Velocity / Value ▾** that chooses what the cells show,
zoom sliders). A second row: a **+** popup, a step-size popup (`/16`), direction and offset steppers, and at the right a root note popup
(C), a scale/mode popup (Off), a search, and **16 Steps ▾**. The body is a grid: each row is one note (C3, B2, A2, G2, F2, E2, D2, C2…
top to bottom) with, at the left, an expander arrow, a purple round icon, the note name with a stepper, **M** / **S** buttons, the step
size (`/16`), small steppers (direction, offset, gate), and the row's mode label ("On/Off"). Sixteen dark cells per row (groups of four
separated by a slightly wider gap), the first cell outlined as the cursor.

### Score (tab)

The **Inspector changes with the editor**: in Score it shows, top to bottom, **Filter: All Instruments** · **Region: Default** (Style `0`,
Quantize ▾ Default, Interpretation ✓, Syncopation ☐, No Overlap ✓, Max. Dots `1`) · **Event: Insert Defaults** (MIDI Channel ▾ 1, Velocity
`0`, Text ▾ Plain Text, Lyric ☐) · **Part Box: Customized** (a palette of notation symbols: an "All" button and a grid of about 24 icon
buttons, with note-value icons below). The local menu bar is **Layout ▾ · Edit ▾ · Functions ▾ · View ▾**, three layout toggles in purple,
the same icon toggles and link button as the Piano Roll, tool popup, zoom. With no MIDI region selected the page (light grey, paper
colour) says "Create a MIDI region." and shows only the ruler.

## Notes for the clone

- The Editors area is a **tab strip with four editors**, not only the Piano Roll; the first implementation can show Piano Roll and
  grey out the others.
- The Piano Roll left pane doubles as the place for quantize and velocity defaults; the "Q" buttons apply to the selected notes.

## List Editors (control bar button, shortcut D; opens as a column at the right of the window)

Tab strip **Event | Marker | Tempo | Signature**; the Tracks area and the editors narrow to make room.

- **Event list:** local bar Edit ▾ · Functions ▾ · View ▾ and two toggle icons (green); filter buttons (blue when on): **Notes · Progr. Change · Pitch
  Bend · Controller** / **Aftertouch · Poly Aftertouch · Syst. Exclusive · Additional Info**; a row with **+** (create event), a popup of the event kind to
  create (`Notes`), **Quantize:** popup (Off) and **Q**; then the table with columns **L, M** (lock, mute), **Position, Status, Ch, Num, Val, Length/Info**.
  Empty when no region has events.
- **Tempo list:** Edit ▾ · Options ▾ · a toggle icon · **Additional Info** button; **+**, **Tempo Set:** popup (Untitled); table columns **Position**
  (bar, beat, division, ticks: `1 1 1 1`, `34 4 3 1`, `140 4 4 227`) · **Tempo** (4 decimals, `137,9040`) · **SMPTE Position** (`01:00:58:22.72`). The
  project here has 8 tempo changes.
- **Marker** and **Signature** tabs not captured.
- **Marker list** (captured): Edit ▾ · Options ▾ · View ▾ · a toggle icon; **+**; **Marker Set:** popup (Untitled); columns **L, Position, Marker Name,
  Length**.

## Note Pad (control bar, ⌥⌘P; right column)

Tab strip **Project | Track** (notes for the whole project or for the selected track). Under it a tiny toolbar: **Aa** (text format), a second icon, and an
**Edit** button at the right; then a plain text area.

## Loop Browser (control bar, O; right column)

Top row: two view toggles (loops list, loops grid), at the right **Sound Packs: Show All ▾**. Row 2: a clear (x) button and the filter buttons
**Instrument · Genre · Descriptors** and a heart (favourites). Row 3: **Scale:** popup (Any) · **Signature:** popup (Any) · a search field "Search Loops".
List columns: type icon (blue waveform = audio loop, green note = software instrument loop) · **Name** · **Be…** (beats) · heart checkbox · **Tem…** (tempo)
· **Key**. Examples: "12 Bar Blues Bass 16 beats 80 E", "12 String Dream 01 8 140 D", "12-8 Afro Cuban Conga 01 4 107". When open, the Tracks area
shows a hint "Drag Apple Loops here to create tracks" at the bottom of the track list and workspace.
