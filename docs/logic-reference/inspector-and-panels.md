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

## Create New Track (the "+" button above the track list, ⌥⌘N)

A modal sheet titled **Create New Track**, centred, ~750 px wide. Top: four **type cards**, each an icon on top, a title, and one or more option buttons under it:
- **MIDI** (green music-note icon): *Software Instrument* (default, lit green) · *External MIDI*.
- **Pattern** (purple grid icon): *Software Instrument* · *External MIDI* (the Step Sequencer tracks).
- **Session Player** (yellow guitar icon): *Drummer* · *Bass Player* · *Keyboard Player*.
- **Audio** (blue waveform icon): *Mic or Line* · *Guitar or Bass*.
The selected card has a green outline. Below, a collapsible **Details** section (triangle): **Instrument:** popup (here `AmpliTube 5`, the last used) with **☐ Multi-timbral** and a
parts field (`4 parts`, dim) and **☑ Open Library**; **Audio Output:** popup (`Stereo Output`) with **☐ Ascending** and the device line `Device: (Scarlett 4i4 USB) ⓘ`.
For an Audio card the details show input format, input source, monitoring, and a record-enable checkbox. Bottom: a **?** help button at the left, **Number of tracks to
create:** field (`1`), **Cancel** and **Create** (default). A new track is added at the end of the track list and selected.

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

## Toolbar (View > Show Toolbar, ⌃⌥⌘T; the third icon of the left group in the control bar)

A second row of large icon buttons with a caption under each, between the control bar and the Library/Inspector/Tracks area. Seen, left
to right: Articulation · Track Zoom · Note Repeat · Spot Erase · Split by Playhead · Split by Locators · Join *(dim)* · Bounce Regions
*(dim)* · Move to Playhead · Nudge Value (a stepper popup showing "Tick" with arrows) · Repeat Section · Cut Section · Insert Section
*(dim)* · Insert Silence · Set Locators *(dim)* · Zoom · Colors. "Customize Toolbar…" in View lets the user choose the buttons.

## Plug-in window (verified: a click on the centre of an occupied slot opens it)

A floating window titled with the **track name** ("Inst 1") centred in its title bar, a red close dot at the top-left and a small icon at the top-right
(hide/show header). Below the title bar a **header strip common to all plug-ins**:
- Row 1: a round **power button** (bypass, blue when active) at the far left · a popup showing the plug-in setting name ("Manual", dim) ·
  at the right **Side Chain:** popup ("None").
- Row 2: **‹ ›** (previous/next setting) · **Compare** · **Copy** · **Paste** · **Undo** · **Redo** · at the right **View:** popup ("Editor") and a **link**
  button.
Under the header the plug-in's own UI (here AmpliTube's full interface) in its native size; the window resizes to the plug-in's view; a resize
handle sits at the bottom-right. The header is the part the clone must draw itself around a hosted plug-in's native window.

## Not captured yet

Smart Controls and Editors areas, Loop Browser, Note Pad, Browsers, List Editors, Piano Roll, plug-in windows, Settings dialogs.

## Create New Track result and the audio track inspector

- Creating a software instrument track puts it at the **bottom** and selects it; the Library shows the instrument categories, the Inspector the track
  section for a software instrument (see above).
- **Audio track Inspector** (seen on "Audio 1"): header "Track: Audio 1" with the blue waveform icon tile, then **Icon · Channel (Audio 1) · Freeze Mode
  (Source Only) · Q-Reference ☑ · Flex Mode (Automatic) · Complex ☑**. Region section "Region: 3 selected" when three regions are selected.
- **Library for an audio track:** header "Library" (no "All Sounds" popup), blue waveform tile, search field, and a list of categories: User Channel Strip
  Settings, Drums and Percussion, Voice, Performance Patches, Studio Instruments, Acoustic Guitar, Effects, Electric Guitar and Bass, Experimental, Legacy.
- **Audio channel strip** (Mixer bottom of the Inspector): EQ display · input slot (`Input 1` with a small circle icon) · Audio FX slots (blue `Pro-Q 4 /
  Compressor`) · Sends · output (`Stereo Out`) · Group · `Read` · pan knob · dB field (`-2,7`) · fader · **R** and **I** buttons (red/orange when active) · M S · name.
- **Playhead and Move to Playhead:** a click in the ruler area sets the playhead (undo history lists "Move to Playhead"); a region dragged shows a help tag
  **"Move Region: Position 171 1 1 1  +42 0 0 0 · Length 1 0 0 0 · Track: 3 Inst 1"**; with automation shown, moving a region asks **"Do you want to move the Track
  Automation data?"** (Move / Don't Move / "Don't ask again").
- **Undo History** (Edit > Undo History…, ⌥⌘Z): a window listing Number, Action, Date, Time of every step; "Include Parameter Changes From: Mixer, Plug-In" toggles;
  clicking a row undoes back to that step; buttons Undo and Redo. Undone steps show greyed.
