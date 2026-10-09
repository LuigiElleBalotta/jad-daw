# Logic Pro 11.2: Inspector and side panels

Seen on 2026-10-09 with a Software Instrument track selected (the Inspector follows the selection). The window is split left to right:
**Library** · **Inspector** (two channel strips at the bottom) · **Tracks area** (track headers + workspace), and the Mixer / Editors /
Smart Controls open below the Tracks area.

## Inspector

A stack of disclosure sections, each with a triangle (▸ closed, ▾ open). Seen: **Region: <name>**, **Track: <name>**, then the two
channel strips (the selected track on the left, its output on the right). Section titles read "Region: MIDI Defaults" when no region is
selected (the settings apply to new regions) and "Track: Inst 1".

**Region section** (MIDI Defaults), top to bottom, label on the left and value on the right, labels right-aligned in a grey column:
Mute (checkbox) · Loop (checkbox) · Quantize ▾ (Off, with a disclosure arrow next to the label) · Q-Swing · Transpose (stepper) · a thin
row with two dashes · Pitch Source ▾ (Off) · another dashes row · Velocity Offset (slider) · **More** (disclosure arrow).

**Track section** (software instrument track): Icon (a small coloured square with the track icon) · Default Region Type ▾ (MIDI) · Channel
▾ (Inst 1) · MIDI Input ▾ (All) · Internal MIDI In ▾ (Off) · MIDI In Channel ▾ (All) · MIDI Out Channel ▾ (All) · Freeze Mode ▾ (Pre
Fader) · Transpose (stepper) · Velocity Offset (slider) · Key Limit (C-2 to G8, two fields) · Velocity Limit (1 to 127, two fields) ·
Delay ▾ (a popup used as the label, with a value field) · No Transpose (checkbox) · No Reset (checkbox) · Staff Style ▾ (Auto) ·
Articulation Set ▾ (None). For an audio track the list is shorter (no MIDI fields).

Appearance: dark grey (~#3a3a3a) rows, 16 px high, 10 px labels in a lighter grey, values in white; popups show up/down stepper arrows
at the right edge; the disclosure triangle sits at the left of the section title.

## Channel strips (bottom of the Inspector and in the Mixer)

Top to bottom: Setting · Gain Reduction · EQ · MIDI FX · Input/Instrument · Audio FX · Sends · Output · Group · Automation mode (Read)
· track icon button · pan knob · dB field + peak field · fader with scale and level meter · R and I buttons (record, input monitor) ·
M and S buttons · name bar (Bnc button on the output strip). The Instrument slot is green with the plug-in name, effect slots are blue,
empty ones are dark grey. The output strip ("Stereo Out") has a slot offering "Mastering Assistant".

## Library

Header "Library" with a popup "All Sounds ▾" at the right of it; a large coloured icon of the track (green music-note tile for an
instrument track); a search field "Search Sounds" with a magnifier; two columns of categories and patches below; at the bottom a gear
popup, **Revert**, **Delete**, **Save…**. With "No Plug-in" on an instrument slot the Library shows the category browser instead of a
patch.

## Create New Track (the "+" button above the track list)

A sheet: type buttons (Audio, Software Instrument, External MIDI, Drummer/Session Players, Guitar or Bass), a popup for the
Instrument (software instrument) and the Audio Output, Input, "Open Library" checkbox, and "Number of tracks to create". Buttons:
Cancel / Create. "Software Instrument" is preselected after the first one.

## Tracks area toolbar (local menu bar)

Left to right: back arrow · Edit ▾ · Functions ▾ · View ▾ · (two icon toggles: grid and list) · (tools: automation, flex, catch, ...) ·
[spacer] · tool popup (pointer) · command-click tool popup · Snap: ▾ (Smart) · Drag: ▾ (No Overlap) · waveform zoom · vertical /
horizontal zoom buttons and sliders.

## Track header (software instrument track)

Number column · colour bar · icon tile (green music note) · name ("Inst 1") · M · S · R · I buttons · a volume slider and a pan knob in the
same row. Audio tracks add a "Polyphonic (Auto)" Flex row below and an input monitor.

## Sizes (screen pixels at the remote view, divide by ~1.0 for logical)

Control bar height ~ 56 px; local toolbar ~ 30 px; track header 64 px high at the default height; ruler 24 px; Inspector column ~ 190 px;
Library column ~ 260 px.

## Not captured yet

Smart Controls and Editors areas, Loop Browser, Note Pad, Browsers, List Editors, Piano Roll, plug-in windows, Settings dialogs.
