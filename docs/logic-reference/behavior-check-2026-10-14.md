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

## What this pass did not get to

Flex Time/Pitch behaviour, Smart Tempo, and most of the minor dialogs (Project Settings, Bounce, Colors, I/O Labels)
were not reached this session — time went into Mixer + menu structure, which surfaced more concrete, checkable
mismatches per minute spent than screenshotting individual dialogs would have. The two techniques above make a
follow-up pass on those areas straightforward; see `docs/roadmap-autonomous.md` section E for the itemised backlog.
