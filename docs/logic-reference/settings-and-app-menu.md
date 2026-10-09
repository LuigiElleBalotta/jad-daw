# Logic Pro 11.2: the "Logic Pro" app menu and the Settings window

Seen on 2026-10-09.

## Logic Pro menu

About Logic Pro · (sep) **Settings ▸** · **Control Surfaces ▸** · **Key Commands ▸** · (sep) Sound Library ▸ · Provide Logic Pro Feedback · Learn About
Logic Remote… · Learn About MainStage… · (sep) Services ▸ · (sep) Hide Logic Pro ⌘H · Hide Others ⌥⌘H · Show All *(dim)* · (sep) Quit Logic Pro ⌘Q.

**Settings ▸** (each opens the same window on a tab): General… · Audio… · Recording… · MIDI… · Score… · Movie… · Automation… · Control Surfaces… ·
View… · My Info… · Advanced… · (sep) Reset All Settings Except Key Command Assignments… · (sep) Plug-in Manager… · Chord Grid Library….

## Settings window

A small non-resizable window titled "Settings". A toolbar of icon+caption buttons, left to right: **General, Audio, Recording, MIDI, Score,
Movie, Automation, Control Surfaces, View, My Info, Advanced**. Under it a segmented tab row for the sub-pages of the selected category, then
a rounded panel with the controls. Labels are right-aligned in a column, controls left-aligned next to them.

### General (sub-tabs: Project Handling, Editing, Cycle, Catch, Notifications, Accessibility)

**Project Handling:**
- Startup Action ▾ (Open Most Recent Project).
- Default Template: (button "Select Template", dim).
- ☑ When opening a project, ask whether current project should be closed.
- ☑ Export MIDI File command saves single MIDI region as format 0.
- ☐ Save undo history with project.
- Auto Backup ▾ (Last 10 Alternative Versions).
- Recent Items ▾ (System Default).

**Editing:** Number of Undo Steps (stepper, 100) · ☑ Groove template edits immediately update all associated regions · ☑ Create new regions
after splitting loops · ☑ Select regions on track selection · ☐ Select tracks on region/marquee selection · Right Mouse Button ▾ (Opens
Shortcut Menu) · Trackpad: ☐ Enable Force Touch trackpad *(dim)* · Pointer Tool in Tracks Provides: ☐ Fade tool click zones, ☐ Marquee tool
click zones, ☐ Quick Swipe and Take Editing click zones · Limit Dragging to One Direction In: ☐ Piano Roll Editor and Score Editor, ☐ Tracks
area · Double-Clicking a MIDI Region Opens ▾ (Piano Roll Editor) · Piano Roll Editor: ☑ Region border trimming.
**Cycle:** Cycle Pre-Processing ▾ (Off) · ☐ Smooth Cycle Algorithm.
**Catch:** "Catch mode default settings": ☑ Catch when starting playback · ☑ Catch when moving playhead · ☐ Catch content by position if Catch and Link
are enabled (explanatory text: editors with the Catch button follow the playhead; Control-click on a Catch button changes it for one editor).
**Notifications:** list of warnings previously set to "Do not show again" (columns Text and Triggered Button, a popup per row), buttons Reset
Selected Warnings *(dim)* and Reset All Warnings.
**Accessibility:** "Enable Playhead position announcements when": ☑ Playing ☐ Recording ☑ Scrubbing (VoiceOver) · ☐ Open Plug-in windows in
Controls view by default.

### View (sub-tabs: General, Tracks, Mixer, Editors)

**General:** Appearance ▾ (System Setting). *Windows:* ☐ Large local window menus · ☐ Large inspectors · ☐ Wide playhead · ☑ Show help tags ·
☐ Show beats and time in help tags · ☐ Show default values · ☑ Show animations. *Displays:* Display Middle C As ▾ (C3 (Yamaha)) · Display Time
As ▾ (SMPTE/EBU with Subframes) with ☐ Zeros as spaces · Display Tempo As ▾ (Beats per Minute (BPM, Maelzel)) · Clock Format ▾ (1 1 1 1) ·
Display MIDI Data As ▾ (MIDI 1.0).

**Mixer:** ☑ Show "Mastering Assistant" Button in Stereo Output. *Plug-in Window:* **☑ Open plug-in window on insertion** · **☑ Show recent
plug-in list in plug-in menu** (so the "Recent" block of the plug-in menus is a setting, on by default). *Level Meters:* Peak Hold Time ▾
(800 ms) · Return Time ▾ (IEC Type I (11.8 dB/s)–Recommended) · Channel Order ▾ (Clockwise (Ls L C R Rs LFE)).

**View > Tracks:** ☐ Show track or bar number while scrolling. *Appearance:* Track Color ▾ (Static) · Region Color ▾ (Individual) · Marker Color ▾
(Static) · Background ▾ (Dark) · Grid Lines ☑ Automatic. *Regions:* ☐ Shaded loops · ☑ Show "+" button next to Session Player regions.
**View > Editors:** Piano Roll: ☐ Bright background.

### Audio (sub-tabs: Devices, General, Sampler, Editing, I/O Assignments, File Editor, MP3)

**Devices:** Core Audio ☑ Enabled · Output Device ▾ · Input Device ▾ (the interface in use) · I/O Buffer Size ▾ (128) Samples · Resulting
Latency (read-only text: "12,5 ms Roundtrip (6,1 ms Output)") · Recording Delay (slider + stepper, 0 Samples) · Processing Threads ▾
(Automatic) · Process Buffer Range ▾ (Small) · Multithreading ▾ (Playback & Live Tracks) · Summing ▾ (High Precision (64-bit)) · ReWire
Behavior ▾ (Off) · **Apply** button (dim until a change). **General:** ☑ Display audio engine overload message · Sample Accurate Automation ▾ (Off) · Automatic Bus Assignment Uses ▾ (All Busses) · ☑ Software
monitoring · ☑ Input monitoring only for the focused track, and only when input monitoring is enabled (as in GarageBand) · ☐ Independent monitoring
level for record-enabled channel strips · Dim Level (slider + stepper, -20 dB). *Plug-in Latency:* Compensation ▾ (All) · ☑ Playback pre-roll ·
☐ Low Latency Monitoring Mode · Limit (slider + stepper, 5 ms, dim until the mode is on).
**Sampler** (inner tabs Misc / Virtual Memory): Sample Storage ▾ (Original) · Search Samples On ▾ (Local Volumes) · Read Root Key From ▾
(File/Analysis) · Root Key at File Name Position ▾ (Auto) · ☑ Keep common samples in memory when switching projects.
**Editing:** *Crossfades for Merge and Take Comping:* Crossfade Time (slider + stepper, 20 ms) · Crossfade Curve (slider + stepper, 0). *Scrubbing:*
☐ Scrubbing with audio in Tracks area · Maximum Scrub Speed ▾ (Normal) · Scrub Response ▾ (Normal).
I/O Assignments, File Editor and MP3 sub-tabs not captured.

### Recording

*Audio Recording:* File Type ▾ (AIFF) · Bit Depth ▾ (24-bit). *MIDI Recording:* Auto Record Enable ▾ (The Focused Track). *Overlapping Track
Recordings* (two columns, MIDI and Audio): Cycle Off ▾ (Merge / Create Take Folder) · Cycle On ▾ (Merge / Create Take Folder) · Replace ▾
(Region Erase, MIDI column only). Button **Recording Project Settings…**.

### MIDI (sub-tabs: General, Reset Messages, Sync, Inputs)

**General:** ☑ MIDI 2.0 · ☑ External stop message ends recording · button Reset All MIDI Drivers. *Articulation Switches* (a small table with a
"Set" column header): MIDI Remote ▾ (Off | Global) · MIDI Channel ▾ (All | Global) · Octave Offset (stepper, 0, dim | Per Channel Strip ▾).
**Reset Messages** (what Logic sends when playback stops or the project is reset). *Software Instruments:* ☑ Control 64 (Sustain) off · ☑ Control 4
(Foot Control) to zero · ☑ Control 2 (Breath) to zero · ☑ Control 1 (Modulation) to zero · ☑ Aftertouch to zero · ☑ Pitch Bend to center position.
*External MIDI:* ☐ Control 123 (All Notes Off) · ☐ Control 121 (Reset Controls) · ☑ the same six as above · ☐ Send used instrument settings on reset.
So by default Logic does **not** send All Notes Off to software instruments: it resets controllers; notes still held are ended by their own note-offs.
**Sync:** *All MIDI Output:* Delay (stepper, 0 ms). *MIDI Time Code (MTC):* MTC Pickup Delay (0 Frames) · Delay MTC Transmission By (0 ms). *MIDI Machine
Control (MMC):* MMC Uses ▾ (MMC Standard Messages) · Output ID (Transport) (All ☑ + 127, dim) · Input ID (Transport) (same) · Transmit Locate Commands
When: ☑ Pressing Stop twice ☑ Dragging regions or events · ☐ Transmit record-enable commands for audio tracks. Button MIDI Sync Project Settings….
**Inputs:** "Enable MIDI ports to use as inputs in Logic": a table with columns On (checkbox) and Device or Port, rows "Logic Pro Virtual In" ☐ and the
connected interface ☑.

### Automation

Move Track Automation with Regions ▾ (Ask) with ☑ Include trails, if possible · Region Automation: ☑ Create Node when cutting at constant
values · Pencil Tool ▾ (Hold Option for Stepped Editing) · Snap Offset (stepper, -5 Ticks) · Ramp Time (stepper, 200 ms) · Write Mode Changes To ▾
(Touch) · Write Automation For: ☑ Volume ☑ Send ☑ Pan ☑ Plug-in ☑ Mute ☐ Solo · Automation Quick Access: ◉ Off ○ On · button **Learn Message**
("Click the Learn Message button to assign a new control") · **Edit…** (dim).

### Score

*Display:* ☑ Show region selection in color · ☐ Display distance values in inches · Double-Click to Open ▾ (Note Attributes) · Selection Color
(swatch) + Reset. *Camera Tool:* Write To ◉ Clipboard ○ PDF file. *Split:* ☐ Auto split notes in polyphonic staff styles · Split Notes At
(slider + stepper, C3, dim). Button **Score Project Settings…**.

### Control Surfaces (sub-tabs: General, Help Tags, MIDI Controllers)

**General:** ☐ Bypass all while in background · Resolution of Relative Controls (slider + stepper, 128) · Maximum MIDI Bandwidth (slider + stepper,
50 %) · ☐ Touching fader selects track · ☑ Control surface follows track selection · ☑ Open plug-in window on track selection · ☐ Jog
resolution depends on horizontal zoom · ☑ Pickup mode · ☑ Flash Mute and Solo buttons · Multiple Controls per Parameter ▾ (2) · "For Longer
Labels and Value Displays": ☑ Only when all parameters fit on one page · Show Value Units For: ☑ Instrument/plug-in parameters ☑ Volume and
other parameters. Buttons **Controller Assignments…** and **Setup…**.

### My Info

"Logic Pro will use this information to identify your songs when sharing them": text fields Composer Name, Artist Name, Album Name, Playlist.

### Advanced

A single option, **☑ Enable Complete Features** ("Expands simplified features to include all available features"), with a read-only list of
what it turns on, and a "Learn More" button:
- *Customization and Control:* key commands, screensets, region colors, control bar, track headers, zoom levels, controller assignments.
- *Editing:* Undo History, list editors, Quick Swipe Comping, drum replacement, in-place bouncing, additional tools.
- *Audio:* Project Audio Browser, Audio File Editor, Surround, other advanced audio features.
- *Mixing:* advanced automation, Mixer views, automation groups, advanced plug-in window controls.
- *Score Editor:* multiple tracks, score sets, Page view.
So Logic has a "simplified" mode that hides much of the UI; the clone can ship with complete features always on.

### Other categories

Not captured yet: the Movie category, the Audio sub-tabs after Devices, the MIDI sub-tabs after General, and the Control Surfaces sub-tabs after General.
