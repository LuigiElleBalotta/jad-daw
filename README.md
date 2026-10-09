# JAD Daw (Just Another Daw)

Open source (AGPLv3) digital audio workstation, work in progress. This repository currently contains the
**Core** (project model, undoable JSON commands, real-time audio engine, offline renderer, a CLI) and the
**UI shell** (Qt Quick: transport, tracks, timeline, mixer).

## Build (Windows, Visual Studio 2022, CMake)

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Debug
    ctest --test-dir build -C Debug --output-on-failure

Use `-DLPC_BUILD_CLI=OFF` to skip the CLI, or `-DLPC_WITH_JUCE=OFF` to build the CLI without audio output
(JUCE is downloaded only when `LPC_WITH_JUCE=ON`).

## CLI

    lpc-cli demo demo.lpc                  # write a small demo project
    lpc-cli info demo.lpc
    lpc-cli render demo.lpc out.wav        # deterministic offline bounce
    lpc-cli play demo.lpc                  # needs a 48 kHz output device

## UI

The UI needs Qt 6.8 (LGPLv3, linked dynamically) and is off by default.

    tools/setup-qt.sh                       # or tools/setup-qt.ps1; installs Qt into ./.qt (aqtinstall)
    cmake -S . -B build-ui -A x64 -DLPC_BUILD_UI=ON "-DCMAKE_PREFIX_PATH=$(pwd)/.qt/6.8.3/msvc2022_64"
    cmake --build build-ui --config Debug
    build-ui/ui/Debug/jad-daw --project demo.lpc

On Windows the Qt DLLs are copied next to the executables after each build, so they start by double click and
`ctest --test-dir build-ui -C Debug` needs nothing on `PATH`.

![The main window](docs/images/main-window.png)

### Window and shortcuts

- Menus (File, Edit, Track, Navigate, Record, Mix, View, Window, Help), control bar with LCD, local toolbar, track
  headers, timeline and mixer. Every action comes from one table, `ui/actions/actions.json`: label, menu, default
  shortcut (Logic Pro's), and whether it is **ready** or a **stub**.
- A stub is present and can be switched on, but nothing is behind it yet (recording, metronome, count-in, punch,
  library, inspector, editors, copy/paste...). Switching a stub on shows a "not implemented yet" notice; switching it off
  shows nothing. Everything else works through the Core's JSON commands, with undo/redo.
- Real today: open/create/save projects (a `.lpc` folder), play, locate, cycle, tempo and time signature (double click
  the LCD), master volume, tracks (new, delete, rename, colour, heights, select with click / Shift / Ctrl), mute, solo,
  fader and pan, regions (select, rectangle select, move, resize from the edges, split, join, delete, draw an empty
  MIDI region with the pencil, import `.wav` by dropping it on a track), snap, zoom, catch playhead.

![Tools and selection](docs/images/tools-and-selection.png)

- **Inspector** (`I`, or the Inspector button): the Region and Track sections of the selected region and track, and two
  channel strips (the track and its output). Real: region gain, track name and colour, and everything on the strips
  (insert gain, sends, output, pan, fader, mute, solo). Quantize, Loop, Transpose, Key and Velocity limits, Delay and the
  like have no engine yet: they are visual only and say so when switched on.

![Inspector](docs/images/inspector.png)

- **Library** (`Y`): the patches of the selected track's kind, by category, with search; click a patch or step with the
  Up and Down keys to apply it (strip values, inserts and instrument in one undo step); Revert applies the track's patch
  again. The catalogue is `core/data/patches.json`; the engine has one effect and one synth for now, so the built-in
  patches differ mostly in level, pan and a gain insert. Save, Delete and the Options menu are not implemented.

![Library](docs/images/library.png)

- **Smart Controls** (`B`): the screen controls of the selected track's patch, grouped in panels; drag a knob (Shift for
  fine, double click resets). A knob can move several parameters at once (the patch decides), and one release is one undo
  step. Parameter Mapping, External Assignment, Compare and the EQ tab are not implemented.

![Smart Controls](docs/images/smart-controls.png)

- **Plug-ins (VST3 effects)**: click the `+` slot of an insert area: Gain or any scanned VST3 plug-in, grouped by vendor.
  Double click a plug-in slot to open its own window. The scan runs in the background at start (in a child process: a plug-in
  that crashes or hangs is listed as failed and skipped) and its result is cached in `plugins.json` in the application config
  folder; Window > Plug-in Manager shows the result and rescans. A plug-in that is not installed shows in red and passes the
  sound through; the project keeps its id and state. Plug-in state is saved with the project, one undo step per editor session
  (committed when the editor closes and before Save). Delay compensation aligns tracks and buses. `lpc-cli render` hosts
  plug-ins too (a plug-in that is not installed is reported and skipped).

- **Mixer** (`X`): one strip per track, built from the same channel-strip component as the Inspector (instrument,
  inserts, sends, output, pan, fader, mute, solo), the master strip last. Double click a strip name to rename the track (Return
  or a click elsewhere confirms, Escape cancels); a click selects it. "New Bus" in a Send or Output menu makes a bus that lives in
  the Mixer only; Track > Show in Tracks Area puts a bus or aux in the Tracks area (and takes it out again).

![Mixer](docs/images/mixer.png)
![Menus](docs/images/menus.png)

- Shortcuts: to change some, create `shortcuts.json` in the application config folder (for example
  `%LOCALAPPDATA%/JAD/JAD Daw/` on Windows, `~/.config/JAD/JAD Daw/` on Linux) with only the actions you want to
  change, e.g. `{"transport.playStop": "P"}`. Unknown actions, invalid sequences and sequences already used by another
  action are ignored and logged. Only some default shortcuts are confirmed against Logic Pro; the checklist is in
  `docs/shortcuts-check.md`.
- Screenshots (used above) are taken by the app itself:
  `jad-daw --project demo.lpc --no-audio --screenshot out.png --size 1280x800 [--tool scissors] [--select-track 2] [--select-region 1] [--open-menu 2] [--panels library,inspector,smart,mixer] [--apply-patch audio.bright-vocal]`.

Known limits: a fader or knob only changes the sound after it is released, the macOS bundle has no icon yet, several
buttons use text labels because there are no icons for them yet, and the panels behind Quick Help, Editors and Loops do
not exist yet. The engine has one effect (gain) and one synth (sine), so the built-in patches and Smart Controls are
small; plug-ins are VST3 effects only (no instruments, MIDI, sidechain or automation of plug-in parameters), stereo in and out, Windows only, and a plug-in that crashes while playing takes the app down; changes made inside a plug-in window become one undo step when it closes. A project with a track name that is empty, longer than 64 characters or has
control characters, or with an unknown track colour is rejected on load.

Design: `docs/superpowers/specs/2026-10-07-core-engine-design.md`, UI: `docs/superpowers/specs/2026-10-07-ui-shell-design.md` `docs/superpowers/specs/2026-10-08-ui-a-frame-design.md` and `docs/superpowers/specs/2026-10-08-ui-b-panels-design.md`. Third-party licences: `THIRD_PARTY.md`.
