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

Put Qt's `bin` folder on `PATH` to run the executables and tests, and set `QT_QPA_PLATFORM=offscreen` for the tests
(`ctest --test-dir build-ui -C Debug`).

- Open, create and save projects from the File menu (a project is a `.lpc` folder). Undo/redo: Ctrl+Z / Ctrl+Shift+Z.
- Drag regions to move them, Delete removes the selected ones, drop a `.wav` (same sample rate as the project,
  mono or stereo) on an audio track to import it.
- Shortcuts: defaults are in `ui/shortcuts/default-shortcuts.json`. To change some, create `shortcuts.json` in the
  application config folder (for example `%LOCALAPPDATA%/JAD/JAD Daw/` on Windows, `~/.config/JAD/JAD Daw/` on Linux) with
  only the actions you want to change, e.g. `{"transport.playStop": "P"}`. Unknown actions, invalid sequences and
  sequences already used by another action are ignored and logged.

Known limits: regions cannot be resized yet, a fader only changes the sound after it is released, there is no
track selection (M and S act on the tracks of the selected regions), and the macOS bundle has no icon yet.

Design: `docs/superpowers/specs/2026-10-07-core-engine-design.md`, UI: `docs/superpowers/specs/2026-10-07-ui-shell-design.md`. Third-party licences: `THIRD_PARTY.md`.
