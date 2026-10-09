# Logic Pro 11.2 context menus (right-click), Tracks area

Seen on 2026-10-09. `▸` = submenu. *(dim)* = greyed in the capture (depends on the selection).

## Region (audio region, right-click)

When the pointer is over a Flex marker the menu starts with **Delete Flex Marker** and **Set To Neutral Position**. Then, in groups:

1. Edit ▸ · Select ▸ · Playback ▸ · Folder ▸ · Name and Color ▸
2. Move ▸ · Trim ▸ · Split ▸ · Bounce and Join ▸ *(dim: needs several regions)* · Convert ▸ *(dim)*
3. Chords ▸ *(dim)* · Processing ▸ *(dim)* · Automation ▸ · MIDI Transform ▸ *(dim, MIDI only)* · Tempo ▸ *(dim)* · Export ▸

The submenus repeat the Edit menu groups (see `menu-bar.md`). A MIDI region adds the MIDI-only groups (MIDI Transform, Convert to MIDI
related, quantize).

## Track header (right-click on a track header)

- **Reassign Track ▸** (first entry, with a leading dash icon).
- (sep) New Audio Track ⌥⌘A · New Software Instrument Track ⌥⌘S · New Session Player SI Track… ⌥⌘U · New External MIDI Track ⌥⌘X · New Track
  with Duplicate Settings ⌘D.
- (sep) Rename Track · Delete Track · Delete unused Tracks.
- (sep) Create Track Stack… · Flatten Stack *(dim)* · Convert Folder Stack to Summing Stack *(dim)*.
- (sep) Hide Selected Track · Hide Unselected Tracks · Unhide All Tracks *(dim)* · Toggle Hide View.
- (sep) Assign Track Color… · Assign Track Icon….
- (sep) **Track Header Components ▸** · Configure Track Header… ⌥T · Store as User Defaults · Apply User Defaults · Revert to Defaults.

**Track Header Components ▸** is a list of toggles (✓ = on in the capture): On/Off · ✓ Mute · ✓ Solo · Track Protect · Freeze · ✓ Record
Enable · ✓ Input Monitoring · (sep) ✓ Volume · ✓ Pan/Send · Pan/Send Knob Function ▸ · (sep) Additional Name Column · Additional Name Column
Mode ▸ · (sep) ✓ Control Surface Bars · ✓ Track Numbers · Track Color Bars · Groove Track · ✓ Track Icons · Track Alternatives.

## Ruler (right-click)

Move Locators Forward by Cycle Length ⇧⌘. · Move Locators Backwards by Cycle Length ⇧⌘, *(dim)* · Skip Cycle · (sep) Set Locators by Selection
and Enable Cycle ⌘U *(dim)* · Set Rounded Locators by Selection and Enable Cycle *(dim)* · Auto Set Locators · (sep) Split Regions at
Playhead ⌘T · Split Regions at Locators ⌃⌘T · Trim Regions to Fill within Locators · (sep) Cut Section Between Locators (Global) ⌃⌘X ·
Insert Silence Between Locators (Global) ⌃⌘Z · Copy Section Between Locators (Global) · Insert Section at Playhead (Global) ⌃⌘V *(dim)* ·
Repeat Section Between Locators (Global) ⌃⌘R · Delete Section Between Locators (Global) · (sep) Copy Cycle to Live Loops Scene.

## Global tracks header (right-click on the "Tempo" row)

Single Global Track · (sep) Arrangement · Marker · ✓ Movie · ✓ Tempo · Beat Mapping · Signature · Chord · (sep) Configure Global Tracks… ⌥G.

## Control bar (right-click on a grey area)

Customize Control Bar and Display… · Apply Defaults · Save As Defaults.

## Not captured yet

Piano Roll and other editors, Mixer strips and slots (other than plug-in slots), Library, Inspector, MIDI region, empty workspace area,
marker, automation point, tempo-track point, the LCD (no menu of its own).

## Tracks area background (right-click on empty grid)

Paste ⌘V · Paste Replace ⇧⌘V · Paste at Original Position · (sep) Select All ⌘A · Select All Inside Locators ⇧L · (sep) Select Empty Regions · Select
Overlapped Regions ⇧O · Select Muted Regions ⇧M · (sep) Scroll in Play ⌃<.

## MIDI region (right-click on a region of an instrument track)

Top block (no submenu): **Mute Regions/Cells On/Off** ⌃M · Unlock SMPTE Position ⌘↑ *(dim)* · **Bounce Regions/Cells in Place…** ⌃B · Join Regions ⌘J *(dim)* ·
Color Regions by Tracks ⌥⇧C. Then submenus: **Edit · Select · Playback · Quantize · Folder · Name and Color** · (sep) **Move · Trim · Split · Bounce and Join ·
Convert** · (sep) Chords *(dim)* · Processing *(dim)* · **Automation · MIDI Transform · Tempo · Export**.

Differences from an audio region: a MIDI region has **Quantize ▸** (✓ Off, 1/1 Note, 1/2, 1/4, 1/8, 1/16, 1/32, 1/64, **Triplets ▸**, **Swing Presets ▸**,
**n-Tuplets and Mixed ▸**, **Apply Quantization Permanently** ⌃Q) and **MIDI Transform ▸**; the audio region's menu starts with the Flex marker entries and
has Processing enabled. Chords and Processing stay dim for MIDI.
