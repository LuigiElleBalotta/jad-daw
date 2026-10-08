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
![Menus](docs/images/menus.png)

- Shortcuts: to change some, create `shortcuts.json` in the application config folder (for example
  `%LOCALAPPDATA%/JAD/JAD Daw/` on Windows, `~/.config/JAD/JAD Daw/` on Linux) with only the actions you want to
  change, e.g. `{"transport.playStop": "P"}`. Unknown actions, invalid sequences and sequences already used by another
  action are ignored and logged. Only some default shortcuts are confirmed against Logic Pro; the checklist is in
  `docs/shortcuts-check.md`.
- Screenshots (used above) are taken by the app itself:
  `jad-daw --project demo.lpc --no-audio --screenshot out.png --size 1280x800 [--tool scissors] [--select-track 2] [--open-menu 2]`.

Known limits: a fader only changes the sound after it is released, the macOS bundle has no icon yet, several buttons
use text labels because there are no icons for them yet, and the panels behind Library, Inspector, Smart Controls,
Editors and Loops do not exist yet.

Design: `docs/superpowers/specs/2026-10-07-core-engine-design.md`, UI: `docs/superpowers/specs/2026-10-07-ui-shell-design.md` and `docs/superpowers/specs/2026-10-08-ui-a-frame-design.md`. Third-party licences: `THIRD_PARTY.md`.
