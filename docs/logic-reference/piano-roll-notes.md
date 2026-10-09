# Logic Pro 11.2: writing and editing MIDI notes (Piano Roll)

Seen on 2026-10-09 with a software instrument track and a short MIDI region. Complements `smart-controls-and-editors.md`.

## Making regions and notes

- With the Tracks area **Pencil** tool and the automation view **off**, a click-drag on an instrument track creates a **MIDI region** of that length (a click makes a
  very short one; each drag made a separate region, here one beat wide at the default snap, shown as a thin green block). The new region is selected and named
  after the track (`Inst 1`); the Inspector shows "Region: Inst 1" and the Piano Roll header "Inst 1 / on Track Inst 1".
- In the Piano Roll with the **Pencil**, a click-drag horizontally on the grid creates **one note** whose length is the dragged length (snapped by Snap: Smart);
  the note is a pale-green rectangle with a darker bar inside showing the velocity; its key on the piano at the left is highlighted blue. The left pane header
  changes to **"One Note selected / in Inst 1"** (with several: "N Notes selected"). The readout at the right of the bar shows the pointer position as note, bar,
  beat, division, tick (e.g. `D#3  86 2 3 1`).
- The region bar at the top of the grid is pale green with the region name at both ends and a loop icon.

## Piano Roll tool menus (12 tools; left-click tool popup and command-click tool popup)

Pointer Tool ✓ · Pencil Tool · Eraser Tool · **Finger Tool** · Scissors Tool · Join Tool · Mute Tool · Quantize Tool · **Velocity Tool** · Zoom Tool · Automation Select
Tool · Automation Curve Tool · **Brush Tool**. (The Tracks area has 17: see `ui-observations-2026-10-09.md`.)

## Right-click menu on a note

Top: **Define as Default Note**. Then **Cut ⌘X · Copy ⌘C · Paste Replace ⇧⌘V · Delete**. (sep) **Invert Selection ⇧I · Select All Following ⇧F · All Following of Same
Pitch ⌃⇧F · Select Same Subpositions ⇧P · Select Same Articulation · Same Note Pitch ⇧E · Same Note Name ⇧S · Same-Colored Notes ⇧C**. (sep) **Articulation ▸** ·
**Trim Note End to Remove Overlaps to Selected Notes (8 Ticks Gap)** · **… to Following Notes (8 Ticks Gap)** · **Trim Note End to Selected Notes (8 Ticks Gap)** ·
**(4 Ticks Overlap)** · **to Following Notes (8 Ticks Gap)** · **(4 Ticks Overlap)**. (sep) **Nudge Region/Event/Marquee Position Left/Right by Nudge Value** ⌥◀ ⌥▶ ·
**Nudge … Length Left/Right by Nudge Value** ⌥⇧◀ ⌥⇧▶ · **Set Nudge Value to ▸**. (sep) **Transpose Region/Event +12 / ±1 / −12 Semitone(s)** (⌥⇧▲ ⌥▲ ⌥▼ ⌥⇧▼; also
Nudge Automation Up/Down and Move Marquee) . (sep) **Region Colors · ✓ Velocity Colors · MIDI Channel Colors** (how notes are coloured).

## Notes for the clone

The context menu is built from key commands: each item is one global command with a key; the same command nudges notes, regions, markers, automation, or the marquee
depending on what is selected. A reasonable port is a key-command registry (`ui/actions/actions.json` already holds commands) and menus built from it.

## Automation/MIDI area (bottom of the Piano Roll)

The icon in the Piano Roll bar (first of the three blue toggles) shows an **Automation/MIDI** area under the note grid. Its left pane has: a **power** button, a
**Region** button (blue; toggles region automation vs the track's), a **share/move** icon button, and a parameter popup (here `Any Ch.: Note Velocity`, green).
The popup: **Display off · Cycle Through** · (sep) "Automation": Smart Controls ▸, Volume, Main ▸, `1 AmpliTube 5` ▸ · (sep) "MIDI": **MIDI Channel ▸**, MIDI Volume, MIDI Pan,
Modulation, Expression, Sustain, MIDI Control 0-63 ▸, MIDI Control 64-127 ▸, **Note Velocity** (✓ default), **Pitch Bend**, **Aftertouch**, **Program Change**, and more below
(scrolls). Each choice changes the lane: **Note Velocity** shows one vertical bar per note aligned under the note (and a dot); **Pitch Bend / controllers** show a
polyline of points.

- **Drawing a controller curve** with the Pencil: a drag on the lane created a smooth line of points from the start to the end of the drag and a flat continuation
  to the region end; the final value is labelled in the lane (`-2165` for Pitch Bend, whose range is −8192…+8191).
- The notes in the grid turn a **darker green** while the controller lane is the active one.
- Everything is stored per **region**, so a region carries notes plus controller/pitch-bend/aftertouch events; this is what sub-project 2 of the roadmap models.
