# lpc (working title)

Open source (AGPLv3) digital audio workstation, work in progress. This repository currently contains the
**Core**: project model, undoable JSON commands, real-time audio engine, offline renderer and a CLI.

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

Design: `docs/superpowers/specs/2026-10-07-core-engine-design.md`. Third-party licences: `THIRD_PARTY.md`.
