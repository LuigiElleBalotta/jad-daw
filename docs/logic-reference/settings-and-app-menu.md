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
Other General sub-tabs (Cycle, Catch, Notifications, Accessibility): not captured.

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
Behavior ▾ (Off) · **Apply** button (dim until a change). Other sub-tabs not captured.

### Recording

*Audio Recording:* File Type ▾ (AIFF) · Bit Depth ▾ (24-bit). *MIDI Recording:* Auto Record Enable ▾ (The Focused Track). *Overlapping Track
Recordings* (two columns, MIDI and Audio): Cycle Off ▾ (Merge / Create Take Folder) · Cycle On ▾ (Merge / Create Take Folder) · Replace ▾
(Region Erase, MIDI column only). Button **Recording Project Settings…**.

### MIDI (sub-tabs: General, Reset Messages, Sync, Inputs)

**General:** ☑ MIDI 2.0 · ☑ External stop message ends recording · button Reset All MIDI Drivers. *Articulation Switches* (a small table with a
"Set" column header): MIDI Remote ▾ (Off | Global) · MIDI Channel ▾ (All | Global) · Octave Offset (stepper, 0, dim | Per Channel Strip ▾).
Other sub-tabs not captured.

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

Not captured yet: Audio, Recording, MIDI, Score, Movie, Automation, Control Surfaces, My Info, and the Cycle, Catch, Notifications, Accessibility and View-Tracks/Editors sub-tabs.
