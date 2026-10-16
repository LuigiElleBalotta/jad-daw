# Logic Pro 11.2 behaviour check against jad-daw (2026-10-14)

Follow-up to the shortcut/colour/asset pass the same day. The owner asked for an actual behavioural comparison, not
just shortcuts: "vince Logic, non jad-daw" (where they differ, Logic's real behaviour is the target). Done with a
disposable test project (`Untitled 13.logicx`, never saved, closed at the end) so the owner's real project
(`Luce Nel Profondo - Singolo.logicx`) was never touched — only opened, read and closed, no clicks on its tracks,
transport or Save.

## Method (new and reusable)

Two techniques, both much more reliable than reading screenshots pixel by pixel:

1. **AppleScript / System Events menu enumeration.** Logic's actual menu bar can be read directly and exactly:
   ```
   osascript -e 'tell application "System Events" to tell process "Logic Pro" to get name of every menu item of menu "Track" of menu bar 1'
   ```
   and, for a submenu, `menu 1 of menu item "X" of menu "Y" of menu bar 1`. This gives the *exact* item text and order
   with zero risk of misreading small UI text, and works for any menu without opening it visually first. The same
   `click menu item "X" of menu "Y" of menu bar 1` form can trigger one, standard-AppleKit-menu-bar items only — it
   cannot click inside Logic's custom-drawn canvases (Mixer, Tracks area, Piano Roll), which expose almost no
   accessibility tree (see below).
2. **Raw coordinate clicks via Quartz, for the custom-drawn canvases.** Logic's Mixer/Tracks/Piano Roll are drawn by a
   single custom view (confirmed: querying their accessibility tree returns only ~230 generic nodes with no readable
   titles/values for the fader, knobs, automation popup, etc. — standard AX-element clicking as used for dialogs
   doesn't reach them). A window screenshot's pixels map 1:1 to window-local points (confirmed: screenshot size equals
   `kCGWindowBounds` size), so `windowOrigin + pixelOffsetFromScreenshot` gives an exact absolute screen point, and a
   synthetic `CGEventCreateMouseEvent` click there works like a real click. This is how the Automation-mode popup below
   was opened and read.

Both were layered on top of the same REA-derived, locally-recompiled accessibility/screenshot helper from the
2026-10-14 asset pass (`asset-reference.md`).

## Findings

### Mixer — mostly already accurate, one real gap

- Strip layout, legend column order (Input/Audio FX/Sends/Output/Group/Automation/icon/Pan/dB), and the
  Audio(blue)/Stereo Out(pink)/Master(purple) strip colouring in `mixer.md` are all **confirmed correct** against the
  live Mixer.
- The meter scale read off a live strip — **0, 3, 6, 9, 12, 15, 18, 21, 24, 30, 35, 40, 45, 50, 60** — matches
  `mixer.md`'s existing table exactly.
- **Automation mode popup (new finding):** clicking a strip's Automation slot shows **Read / Touch / Latch / Write**
  only — there is **no "Off" entry** in this menu. jad-daw's README and Mixer code describe the slot as
  "Off / Read / Touch / Latch / Write". Either Logic doesn't have a distinct Off state here (Read with no automation
  data already behaves like "off"), or Off lives somewhere else; worth checking before assuming jad-daw's 5-state
  model is right.
- The same popup also has, below a separator, **✓ Record Automation with Audio Regions**, then **Trim** and
  **Relative** — none of which jad-daw's Automation slot has today. (Trim/Relative are likely write-mode
  curve-blending options; worth a closer look if/when jad-daw's automation write modes grow.)

### Track menu / Track Stacks — one real correction

- Logic's **Track** menu has exactly **one** stack-related creation command, **"Create Track Stack…"** (the ellipsis
  means it opens a dialog, presumably to choose Folder vs. Summing there). jad-daw's `actions.json` has **two**
  separate top-level actions, `track.createTrackStack` ("Create Track Stack…") and a separate `"Create Folder Stack"`
  action. **This is a real mismatch** — Logic does not expose "Create Folder Stack" as its own menu command.
- Logic's Track menu has no "New Aux Track" item. An auxiliary channel strip is created from the **Mixer's Options
  menu** ("Create New Auxiliary Channel Strip", ⌃N — already correctly recorded in `mixer.md`), not from Track > New.
  jad-daw's `actions.json` lists `track.newAux` under the Track menu; its menu placement looks wrong even if the
  command itself is fine to keep.
- Full confirmed Track menu (top level, nothing selected): New Tracks…, New Audio Track, New Software Instrument
  Track, New Session Player SI Track…, New External MIDI Track, Other, Rename Track, (sep) Delete Track, Delete
  Unused Tracks, (sep) **Create Track Stack…**, Flatten Stack, Convert Folder Stack to Summing Stack, (sep) Global
  Tracks, Track Alternatives, Master Track, (sep) Replace or Double Drum Track…, Convert Regions to New Sampler
  Track…, (sep) Search and Select Track…, Hide/Show Selected Tracks, Hide/Show Unselected Tracks, Unhide All Tracks,
  Toggle Hide View, (sep) Sort Tracks by, Assign Track Color…, Assign Track Icon…, Configure Track Header…

### Two menu **locations** jad-daw has wrong

- **Preferences is not under File, and it's not called "Preferences" any more.** It's **Logic Pro > Settings** (no
  ellipsis — it's a submenu, not a single dialog), under the app menu, matching Apple's 2023+ "Settings" rename across
  their apps. jad-daw's `file.preferences` (File menu, `Ctrl+,`) has the wrong menu path; the shortcut itself
  (⌘,) is still the macOS-standard one and is presumably still right.
- **Key Commands is not under Help.** It's directly in the **Logic Pro** app menu ("Key Commands", no ellipsis — used
  successfully throughout this session to open the Key Command Assignments panel). jad-daw's `help.keyCommands`
  (Help menu, `Alt+K`) has the wrong menu path; the shortcut (⌥K) is confirmed correct (see `shortcuts-check.md`).

### Record menu — two structural simplifications in jad-daw

- **Count-in submenu is bigger than documented:** None, 1–6 Bars, then **1/4 through 9/4** (nine beat counts, not just
  "1/4, 2/4, 3/4" as the README says and `actions.json` implements). jad-daw currently stops at 3/4.
- **Overlapping Audio Recordings is two parallel groups, not one flat list.** Real Logic: **Cycle Off** group (Create
  Take Folder / Create New Track / Create Track Alternative) and a separate **Cycle On** group (Create Take Folder /
  Create Tracks And Mute / Create Track Alternatives) — the available choices differ depending on whether Cycle is on,
  and "Create Track Alternative(s)" is an option in both, which jad-daw doesn't have at all today (it has
  Take-Folder/Tracks/Tracks-and-Mute/Merge as one flat set regardless of Cycle state).

### Edit menu is contextual, not static

With nothing selected (just a track header), Logic's Edit menu is short: Undo, Redo, Undo History…, Delete Undo
History, (sep) Cut, Copy, Paste, Delete, (sep) Select All — roughly 10 items. jad-daw's `actions.json` lists **102**
Edit-menu actions (every Select/Tool/Move/Flex/Convert submenu item flattened into one menu id namespace). This isn't
necessarily wrong — jad-daw may just always show the full menu with items disabled rather than removing them — but it
means a literal "what does Logic's Edit menu show" comparison depends entirely on what's selected and which area has
focus, and a single static snapshot (this one, nothing selected) isn't enough to validate or invalidate jad-daw's
model either way. Worth a follow-up pass with an actual region/note selected in each editor.

### Project Settings — one window, many more tabs than documented

`File > Project Settings` is **one window** with an icon toolbar of **11 tabs**: General, Audio, Recording, Smart
Tempo, MIDI, Score, Movie, Synchronization, Metronome, Tuning, Assets (confirmed: switching tabs stays in the same
window, `Untitled 13.logicx - Project Settings`). jad-daw's roadmap describes Project Settings as one undifferentiated
dialog ("name, rate, tempo, count-in, recording"); real Logic's **General** tab alone has three unrelated groups —
**Project Type** (Use musical grid), **Opening Project** (Only load plug-ins needed for playback, Reset record enable
state of Software Instrument tracks), **Automation** (Region automation takes priority over track automation, Use
preset parameter value for regions without region automation) — none of which obviously map to "name, rate, tempo,
count-in, recording." The other 10 tabs weren't read this session. This is a bigger dialog than jad-daw models today;
worth a dedicated pass per tab before assuming today's single-panel Project Settings covers the same ground.

### Flex Mode — exact menu read live (2026-10-16 follow-up)

The Inspector's Track section **Flex Mode** popup (an `AXPopUpButton`, unlike the Mixer/Tracks canvas which exposes
almost nothing to accessibility — the Inspector sidebar *is* a normal AppKit list) opened on a retry and was read
directly from the screenshot:

```
✓ Off
Flex Pitch
——————————
Flex Time - Automatic
Flex Time - Monophonic
Flex Time - Slicing
Flex Time - Rhythmic
Flex Time - Polyphonic
Flex Time - Speed (FX)
Flex Time - Tempophone (FX)
```

jad-daw's README lists the same eight algorithms (Slicing, Rhythmic, Monophonic, Polyphonic, Tempophone, Speed,
Automatic, Flex Pitch) but as one flat list — real Logic's menu has real structure jad-daw's doesn't model:
- **"Off" is the default, separate, top entry** (not just "no algorithm chosen" within one list).
- **"Flex Pitch" sits with Off, above a separator**, not grouped with the Flex *Time* algorithms.
- The seven Flex Time algorithms share a **"Flex Time - " label prefix**, and crucially the **order is different**:
  Logic goes Automatic, Monophonic, Slicing, Rhythmic, Polyphonic, Speed, Tempophone — jad-daw's README order is
  Slicing, Rhythmic, Monophonic, Polyphonic, Tempophone, Speed, Automatic (Automatic last instead of first).
- **Speed and Tempophone are suffixed "(FX)"** in Logic's own menu — i.e. Apple itself flags those two as effects/
  special-case algorithms, distinct from the other five "proper" time-stretch algorithms. jad-daw's flat list doesn't
  carry that distinction anywhere (worth deciding whether the Flex popup should visually separate them the same way).

This is a UI/menu-structure finding only — it doesn't speak to whether jad-daw's actual Monophonic/Polyphonic/WSOLA/
phase-vocoder DSP (`flex-algorithms.md`) sounds right, only to how the picker should be grouped, ordered and labelled.

### One mishap, for the record

While repeating the "new disposable project" setup a second time, the owner's real project window (`Luce Nel
Profondo - Singolo.logicx`) closed unexpectedly — most likely a queued Return keystroke landed on the close-current-
project prompt's default (highlighted) **Close** button instead of **Don't Close**. No edits had been made to that
project in this or the first session (read-only observation only), so nothing was lost, but the window closing without
explicit confirmation is exactly the failure mode the "never touch the user's project" rule exists to prevent. Flagged
to the owner immediately; they confirmed it didn't matter this time. For any future session automating Logic Pro's
GUI: don't assume a modal's default button is the safe one, and re-check the window list after every keystroke that
could reach a confirmation dialog, not just after clicks.

### Bounce dialog — much richer than documented

`File > Bounce` is a **submenu** (Regions in Place…, Tracks in Place…, And Replace All Tracks…, Project or Section…) —
"Project or Section…" opens the dialog jad-daw's README describes, titled **`Bounce "Output 1-2"`** (confirms the
Output-strip naming: the project's main stereo output is internally "Output 1-2", labelled "Stereo Output" in I/O
Labels — see below). The dialog itself:

- **Destination is a multi-select checklist**, not a single format choice: Uncompressed / MP3 / M4A: AAC / Burn to
  CD/DVD — you can bounce to several destinations from one pass, not pick one.
- Only when **Uncompressed** is checked do File Type / Bit Depth / Format / Dithering apply. **File Type is AIFF /
  WAVE / CAF** (confirmed by opening the menu) — **no FLAC**. jad-daw's own FLAC export (own encoder, `third_party`-free)
  is a genuine addition beyond Logic, not a gap — worth keeping, not worth hiding.
- **Sample Rate is a dropdown in the dialog itself** (48 kHz shown) — Logic can bounce at a different rate than the
  project's, with conversion; jad-daw's bounce presumably always matches the project rate.
- **Start/End are explicit bar-position fields** (`1 1 1 1` to `129 1 1 1`), not jad-daw's "whole project / cycle area"
  binary choice — Logic's dialog always shows a concrete range (defaulting to project start/end, or the cycle range)
  that can be typed into directly.
- **Mode: Offline** is a dropdown (implying a Realtime option exists too), plus **Bounce 2nd Cycle Pass**, **Add to
  Project** and **Add to Music** checkboxes jad-daw doesn't have.
- **Normalize is a dropdown ("Off"/presumably On/Peak modes)**, not jad-daw's simple "normalize to -0.3 dBFS" flag.

### Colors window — a real 16×3 palette, not yet in jad-daw

`View > Colors` opens a small borderless panel: **16 columns × 3 rows = 48 swatches**. Row 1 is the full-saturation
hue sweep (orange → yellow → green → cyan → blue → purple → magenta), row 2 the same 16 hues at a darker/more muted
variant, row 3 darker still. No numeric/hex labels are shown in the UI itself. jad-daw's codebase doesn't appear to
have a dedicated colour-picker component yet for `Track > Assign Track Color…` (searched `ui/qml` for a
ColorPicker/ColorPalette component and found none) — this is a real gap to fill, not a correction to an existing one.
A screenshot is saved at `logic-pro-reference/live-ui-screenshots/colors-window.png`; pixel-sampling it for exact hex
wasn't reliable enough this session (swatch-grid alignment kept landing on the gaps between cells) — worth a cleaner
re-extraction (e.g. a higher-resolution capture, or hunting for the palette in a resource file instead of a
screenshot) before jad-daw commits to exact values, but the **16×3 structure itself is solid**.

### I/O Labels — covers outputs and buses too, not just inputs

`Mix > I/O Labels…` opens a **table**, columns **Channel | Provided by Driver | User | Long | Short**, one row per
physical channel *and* per stereo pair *and* per bus: Input 1–6, Output 1–4, Input 1-2/3-4/5-6, Output 1-2/3-4, then
Bus 1–7+ (list continues past what was captured). Each row has a radio toggle between "use the driver-provided name"
and "User" (custom Long/Short names). **Output 1-2 is already labelled "Stereo Output" (Long) / "Stereo Output"
(Short)** — confirms jad-daw's "Stereo Out" strip-naming convention is on the right track, if not an exact string
match. jad-daw's README says I/O Labels "names the inputs" only; real Logic names inputs, outputs *and* buses, with
separate long/short strings and a driver-name-vs-custom-name toggle — a noticeably bigger feature than documented.

### Create New Track dialog — full structure (2026-10-16)

`File > New Tracks…` or the "+" above the track list opens **Create New Track**: four category tiles, each with its
own sub-options —

- **MIDI**: Software Instrument, External MIDI
- **Pattern**: Software Instrument, External MIDI
- **Session Player**: Drummer, Bass Player, Keyboard Player *(three, not two)*
- **Audio**: Mic or Line, Guitar or Bass

— and a **Details** panel that changes with the selected category (for Audio: Audio Input/Output popups, Ascending/
Load Default Patch/Open Library on the input side, Ascending/Input Monitoring/Record Enable on the output side, each
side showing its device name). A **Number of tracks to create** field and Cancel/Create close it. The category last
used persists across the session (this one defaulted to Audio because that's what was created earlier in this
session) — an earlier design note in `2026-10-09-vst3-instruments-design.md` recorded it defaulting to MIDI > Software
Instrument on a fresh app state, which is consistent, not contradictory. Reference screenshots:
`logic-pro-reference/live-ui-screenshots/{new-project-chooser,create-new-track-dialog}.png`.

### Project Settings, more tabs — and a major Metronome correction

Continuing from the General tab (above):

- **Audio** tab has its own sub-tabs (General/Channel Strips). General: Sample Rate, Spatial Audio, Surround Format,
  and **Pan Law** (`-3 dB Compensated` by default, with an "Apply Pan Law compensation to stereo balancers" checkbox)
  — a real DSP-relevant setting (how a centred pan knob splits power vs. amplitude between L/R) that jad-daw's pan
  implementation should be checked against, not just a cosmetic setting.
- **Recording** tab: "When Beginning" is a **Count-in vs. Pre-roll radio choice** (not just Count-in) — Pre-roll plays
  audio before the playhead instead of silent-counting bars; jad-daw only has Count-in. Plus "Allow tempo change
  recording", "Automatically colorize takes", MIDI data reduction / erase duplicates, and a separate Audio Recording
  Path folder picker.
- **Smart Tempo** tab: **Project Tempo Mode is Keep / Adapt / Automatic** — the LCD doc (`lcd.md`) currently says the
  states are "KEEP and OFF" besides ADAPT; **"Automatic" is the real third state, not "Off."** Also here: separate
  Flex & Follow defaults for new recordings vs. imported files (each Off/other + a "Trim start of new regions"
  checkbox), an Export Tempo Resolution setting, and Free Tempo Recording's "Default Tempo Behavior" (Ask / apply
  recorded tempo / apply average tempo / keep project tempo) — none of which jad-daw's Smart Tempo models today.
- **MIDI** tab has its own sub-tabs (General/Input Filter/Chase/Clip Length): "Send After Loading Project" options,
  MIDI Thru settings. Low priority — compatibility-with-old-MIDI-gear territory, not core DAW behaviour.
- **Tuning** tab: software instrument pitch (cents/Hz), a tuning-table system (Equal Tempered / Fixed microtonal
  presets like "Ethnic - Arabic" / a custom User stretch table / Hermode Tuning). Low priority for jad-daw today.
- **Metronome** tab — **this is the important one.** `Record > Metronome Settings…` was confirmed (by opening it) to
  be a shortcut into this *exact same* Project Settings tab, not a separate dialog. Its actual structure: Options
  (Simple mode, Click while recording [+ "Only during count-in"], Click while playing, Polyphonic clicks), then
  **Source**: an Audio Click section (named "**Klopfgeist**" — that's Logic's internal click-synth's real name) with
  one row each for **Bar / Group / Beat / Division**, each row a **Note + Velocity** pair (defaults G5/127, D#5/119,
  C5/112, C2/60-unchecked), plus Tone and Volume sliders and an Output routing popup; and a parallel **MIDI Click**
  section with Channel/Note/Velocity per the same four rows plus a Port popup.

  **This does not match jad-daw's README at all.** jad-daw describes Metronome Settings as choosing how a bar is
  *counted* — "beats `1 2 3 4`, eighths `1 & 2 &`, sixteenths `1 e & a`, or grouped `1 la li 2 la li`" — plus a custom
  WAV file per spoken count. Real Logic 11.2 has no such counting-pattern UI anywhere in this dialog: it's entirely
  about the *pitch and velocity* of up to four accent levels (bar/group/beat/subdivision), for both its own synthesised
  click and an outgoing MIDI click, not about spoken/sampled count words. Either jad-daw's description is from a much
  older Logic version, is conflating this with something else (GarageBand's metronome, or a different app), or was
  never verified against the real app. **This needs the owner's attention before any more metronome work happens** —
  it's a foundational mismatch in a described, "done" feature, not a minor detail.

### Project Settings, the last 4 tabs — low priority, briefly

- **Score**: its own 7 sub-tabs (Global, Numbers & Names, Tablature, Chords & Grids, Clefs & Signatures, Layout, MIDI
  Meaning, Colors) of notation-engraving settings (margins, spacing, pedal position, beaming slant, symbol fonts...).
  jad-daw's Score is intentionally view-only; this confirms real engraving control is a whole separate feature domain,
  not a few missing checkboxes — not worth chasing unless jad-daw commits to real score editing.
- **Movie**: position/region-border timecode fields, Movie Volume, Mute, "Movie Settings…" — matches the roadmap's
  "left out for good" call on Movie.
- **Synchronization**: its own 4 sub-tabs (General/Audio/MIDI/Unitor); General has Sync Mode (Internal/external),
  Frame Rate, MTC validation, Bar-Position-to-SMPTE mapping. External-hardware-sync territory, low priority.
- **Assets**: "Project Package" checkboxes for what gets copied into the project folder on import (audio files,
  Sampler/Alchemy/Ultrabeat audio, Space Designer impulse responses, movie file, Apple Sound Library content). Related
  to, but distinct from, jad-daw's "Project Management" (which cleans up *unused* audio) — this is about copy-on-import
  behaviour instead. Minor, but a real gap if jad-daw ever imports Sampler-style content.

All 11 Project Settings tabs have now been read at least once; screenshots for all of them are in
`logic-pro-reference/live-ui-screenshots/projset-*.png`.

### Edit menu with a selection — still not reached

The context-sensitivity question from the first pass (`Edit menu is contextual, not static`, above) wasn't resolved
this follow-up either. A right-click on an empty part of the new MIDI track (to create a region the way jad-daw's own
README says Logic supports) didn't produce a visible context menu in two attempts this session — likely a coordinate
miss rather than a real behaviour difference, since jad-daw's own note on this ("right-click on an empty part of an
instrument track offers Create MIDI Region") was presumably itself verified against real Logic at some point. Left
on the backlog (E7) rather than drawing a conclusion from a failed click.

### Flex Time DSP — blocked, plus two bonus findings from Apple's demo projects

Tried to get real recorded audio to test Flex Time transient detection/marker rendering against jad-daw's own DSP
(`flex-algorithms.md`). Both of Apple's bundled demo projects (`Contents/Resources/Demo Projects/Artist Songs/Artist
Song {1,2}.logicx` — legitimate to open, shipped with the app for exactly this kind of exploration, no personal/
copyright concern) turned out to be **entirely software-instrument and Session Player based** (Drum Machine Designer-
style kits: "Trap Door", "Big Room", synth/bass/string instrument tracks) — **no recorded audio regions in either
one**, so Flex Time (audio-only) couldn't be exercised. MacOs's own short system sounds (`/System/Library/Sounds/*.aiff`)
would work technically but are ~1 second long, too short to say anything about transient/tempo detection quality.
**This needs either a real recording or a music file from the owner (with explicit permission — not assumed) to test
properly; left open on E7.**

Two things worth keeping from this detour:
- **Session Player regions carry a version-migration dialog.** Opening either demo project's Drummer region shows:
  *"This Drummer region was created in a previous version of Logic Pro. Convert to MIDI to preserve the original
  state, or update to new Session Player performance."* with **Convert** / **Update** buttons (not clicked — don't
  want to modify Apple's shipped demo content). Confirms Session Player regions are versioned and Logic offers an
  explicit migration path rather than silently reinterpreting old data. jad-daw's own Session Player
  (`Regenerate Session Player Part`) doesn't have an equivalent "this was made by an older engine version" concept
  yet — probably fine to skip (jad-daw's generator doesn't have version history the way Apple's multi-release engine
  does), but worth knowing the real behaviour exists.
- **Logic Pro 11.2 has a real Chord global track — correcting an explicit jad-daw claim.** The Global Tracks add-list
  in both demos reads, unambiguously (confirmed with a pixel-level crop, not a guess): **Arrangement, Marker, Tempo,
  Signature, Chord**. jad-daw's README states, about Session Players, "no following of a Chord Track (**there is
  none**)" — that's wrong for 11.2; Logic has a genuine Chord global track as a fifth, available lane alongside
  Marker/Tempo/Signature (which jad-daw already has) and Arrangement (which jad-daw doesn't). This one wasn't opened/
  populated in the demos (not clicked, to avoid touching Apple's shipped content), so its exact editing UI and how it
  feeds Session Players isn't confirmed yet — but its *existence* is solid evidence, not speculation. Given jad-daw's
  Session Players already read chord progressions from marker names as a substitute, a real Chord global track would
  be the more faithful (if bigger) way to do it — worth a dedicated follow-up.
- **The Session Player chord editor is confirmed** in both demos: a lower "Session Player" pane with **Chords** mode,
  a Preset popup, and a chord-progression timeline using triangular beat markers — visually consistent with jad-daw's
  own chord-progression-driven Session Player editor.

### Flex Time on real audio — tested at last (2026-10-16, second follow-up)

Unblocked E7 using a legitimate source: Apple's own `/Library/Audio/Apple Loops/Apple/01 Hip Hop/Safe And Sound Beat.caf`
(a real drum loop shipped with Logic, 29.5 s, 44.1 kHz stereo — not the owner's personal files, and not a demo project
this time). Imported onto a fresh Audio track with File > Import Audio File, in a disposable area of `Artist Song
2.logicx` (Apple's own demo project; not saved afterwards). New findings, all from direct observation:

- **Turning on Flex for a track with real content asks for confirmation first**: *"Do you want to turn on Flex for
  track 'Audio 1'?"* with **Cancel** / **Turn on Flex** — not documented anywhere in jad-daw today. (On an empty track
  with no regions, by contrast, the Flex Mode popup just switches instantly with no prompt — the prompt is specifically
  about there being real audio to analyse.)
- **The default algorithm Logic picks when Flex is first turned on is "Flex Time - Slicing"** — confirmed in the Track
  editor's own local toolbar, which shows the active Flex Time algorithm by name. If jad-daw's Flex Mode picker needs
  a sensible default instead of "Off", Slicing is the real one Logic uses, not Automatic (even though Automatic is
  first in the picker's own list — see the earlier Flex Mode finding above).
- **"Flex & Follow" and "Flex Mode" are two different controls, not one** — correcting an assumption in this session's
  earlier Flex Mode note. **Flex Mode** (Off / Flex Pitch / Flex Time - *seven algorithms*) lives in the **Track**
  Inspector section and picks the algorithm. **Flex & Follow** lives in the **Region** Inspector section and is a
  plain **On/Off checkbox** — it's the per-region Smart Tempo "should this region's tempo follow/be followed"
  toggle, not an algorithm choice. jad-daw's single "Flex & Follow" README wording conflates what are really two
  separate, differently-scoped controls.
- **Transient marks are real and visible once Flex is on**: thin vertical tick marks appear through the waveform at
  the detected transients (confirmed with a pixel crop — `logic-pro-reference/live-ui-screenshots/
  flex-transient-marks-zoom.png`), matching jad-daw's README ("the transients the analysis found (thin marks)").
  The yellow flex markers-with-handles jad-daw also describes weren't confirmed distinctly from the transient ticks
  in the time available (they may only render in the upper Tracks-area Flex view rather than the lower Track/File
  editor used here, or need a click on a transient to appear) — worth a quick follow-up, but the core transient-
  detection behaviour itself is confirmed real and working as documented.
- Imported **Apple Loop audio** shows **"Follow Tempo & Pitch"** (checked) in the Region inspector, while a plain
  audio region on an empty track showed **"Flex & Follow"** (Off) in the same inspector slot — the field's label (and
  possibly its exact behaviour) depends on whether the file carries Apple Loop tempo metadata. Not fully chased down;
  flagged for whoever looks at this next.

### Method note: use macOS's own Snapshot/Click/Type automation, not pixel-coordinate guessing

Partway through this session the owner made `mcp__macos-mcp__*` tools available (Snapshot, Click, Type, Shortcut,
App, Wait, etc.) — a real accessibility-driven automation layer, a significant upgrade over the raw-Quartz-click
technique from the first pass. `Snapshot(use_vision: true)` returns every interactive element's exact role, label and
click coordinates *and* an annotated screenshot in one call, which made every interaction in this section (opening
files, confirming dialogs, reading field values) far more reliable than estimating pixel positions from a plain
screenshot. One real gotcha hit repeatedly: **running a Bash/osascript command switches focus to Terminal**, so a
`mcp__macos-mcp__App(mode: "switch", name: "Logic Pro")` is needed before every interactive macos-mcp call that
follows a Bash call, or the click/keystroke silently goes to the wrong app. Also hit: the Go-to-Folder sheet's text
field mangled `/` characters typed via `Type` into `-`; using the Open-dialog's own Search field (which matches
Spotlight-style filename search) instead of typing a path sidestepped this reliably.

## What this pass did not get to

Smart Tempo behaviour and most of the minor dialogs (Bounce, Colors, I/O Labels, and 10 of Project Settings' 11 tabs)
were not reached this session. The techniques above make a follow-up pass on those areas straightforward; see
`docs/roadmap-autonomous.md` section E for the itemised backlog.
