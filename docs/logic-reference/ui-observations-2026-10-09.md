# Logic Pro 11.2 UI observations

Seen by hand on 2026-10-09 on a Mac (Logic Pro 11.2, remote desktop), with Apple's Logic Pro User Guide for Mac as the second source.
This is a reference for the clone, not a spec. Anything not listed here has not been looked at yet. The icons are never copied; we draw
our own SVGs.

## Window and control bar

- **Menu bar:** Logic Pro, File, Edit, Track, Navigate, Record, Mix, View, Window, 1, Help.
- **Control bar (top):** on the left, view toggles (Library, Inspector, Quick Help, Toolbar, Smart Controls, Mixer, Editors, plus List
  Editors, Note Pad, Apple Loops, Browsers; in the screenshot seen as small icons grouped on the left); in the middle the transport
  (rewind, fast forward, go to beginning, stop, play, record, capture/record toggle, cycle); then the LCD; then Count-in /
  Metronome icons; on the right the Master meter area and the right-hand view buttons.
- **Right-click on a grey area of the control bar:** a menu with "Customize Control Bar and Display…", "Apply Defaults", "Save As
  Defaults".
- **Customize Control Bar and Display…** is a panel with four columns:
  - *Views:* Library, Inspector, Quick Help, Toolbar, Smart Controls, Mixer, Editors; List Editors, Note Pad, Apple Loops, Browsers.
  - *Transport:* Go to Beginning, Go to Position, Go to Left Locator, Go to Right Locator, Go to Selection Start, Play from Beginning,
    Play from Left Window Edge, Play from Left Locator, Play from Right Locator, Play from Selection, Rewind/Fast Rewind,
    Forward/Fast Forward, Stop, Play, Pause, Record, Free Tempo Recording, Flashback Capture, Skip Cycle, Cycle (each a checkbox;
    visible ones: Rewind, Forward, Stop, Play, Record, Free Tempo Recording, Cycle).
  - *Display:* a popup of LCD modes (Beats & Project, Beats & Project (Large), Beats & Time, Beats & Time (Large), Beats, Time,
    Custom), and checkboxes for the Custom content: Positions (Time/Beats), Locators or Punch Locators (Left/Right or Left/Length),
    Sample Rate / Buffer Size, Varispeed, Tempo, Time Signature / Division, Key Signature / Project End, MIDI Activity (In/Out),
    Performance Meter (CPU/HD).
  - *Modes and Functions:* Sync, Replace, Autopunch, Set Punch In/Out Locator by Playhead, Software Monitoring, Auto Input Monitoring,
    Pre Fader Metering, Low Latency Monitoring Mode, Set Left/Right Locator by Playhead, Set Left/Right Locator Numerically, Move
    Locators by Cycle Length, Tuner, Solo, Count In, Metronome Click, and an "Output Meter" popup.
  - Buttons: Apply Defaults, Save As Default, Revert.
- **LCD** (Beats & Project): large "bar beat" (e.g. `102 1`) with a small BAR / BEAT caption; to its right the tempo (`138`, caption
  KEEP / TEMPO or ADAPT) and the time signature over the key (`4/4`, `Cmaj`), with a drop-down arrow at the right. A right-click on
  it showed no menu of its own.

## Toolbar of the Tracks area

- Left: a "back" arrow, **Edit**, **Functions**, **View** menus, then view toggles (grid, list), and mode buttons (Flex, Catch, etc.).
- Right: **tool popup** (pointer shown), a second tool popup (the "command-click tool"), **Snap:** popup (Smart), **Drag:** popup (No
  Overlap), then zoom controls and horizontal/vertical zoom sliders.
- **Tool popup (17 tools):** Pointer, Pencil, Eraser, Text, Scissors, Join, Solo, Mute, Zoom, Fade, Automation Select, Automation
  Curve, Marquee, Flex, Gain, Slip, Rotate.
- Above the track list: **+** (new track; opens a "Create New Track" sheet) and a second button next to it (duplicate track).

## Global tracks

- A row "Movie" and a row "Tempo" sit above the tracks. Hovering/right-clicking on the Tempo row header: a popup menu **Single Global
  Track**; then the list **Arrangement, Marker, Movie ✓, Tempo ✓, Beat Mapping, Signature, Chord**; then **Configure Global Tracks…**
  (⌥G). Checked entries are the visible ones.
- The Tempo track shows the tempo curve (values 120 to 160, blue line, points at tempo changes with the value, e.g. 137,9).

## Mixer (opened below the Tracks area from the control bar)

- Header: Edit, Options, View menus; **Sends on Faders** (power icon + popup "Off"); **Single / Tracks / All** segmented; then type
  filters **Audio, Inst, Aux, Bus, Input, Output, Master/VCA, MIDI**; two layout buttons at the far right.
- Row labels (left column, top to bottom): Setting, Gain Reduction, EQ, MIDI FX, Input, Audio FX, Sends, Output, Group, Automation,
  Pan, dB. Strips: Setting, EQ, MIDI FX, Input/Instrument, Audio FX, Sends, Output, Group, Automation mode (Read), track icon, pan
  knob, dB field, fader with scale, meter, R/I (record, input monitor), M/S, name bar. The name bar of a strip is coloured by type
  (blue audio, green instrument, magenta stereo out, purple master). The master strip has "D" (dim) next to M.

## Channel strip / plug-in slots

See `docs/superpowers/specs/2026-10-09-vst3-instruments-design.md` section 2: slot menus (instrument and audio effect), hover
controls, "No Plug-in", Recent, formats.

## Not looked at yet

Right-click menus on regions, track headers, the ruler and the piano roll; the Inspector (region and track); Library behaviour;
Smart Controls; the Piano Roll and Event List; the Create New Track sheet in detail; Settings dialogs; key commands.
