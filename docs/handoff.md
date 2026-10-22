# Handoff: where the work stands

Updated at every commit so that another session (or another AI) can resume. The detailed list is `docs/roadmap-autonomous.md` (sections E and F); this file is the short version.
Last update: 2026-10-21, after the UI build and tests.

## Rules from the owner (Luigi, Italian-speaking)
- Use ALL the reverse-engineering knowledge (Logic Pro 11.2) and `jad-daw-private` (nested git repo, ignored): do not skip anything.
- Leave the CI alone until told. Commit, never push. Commits end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01P21WR25Z8dp2xuJ2oVVTx3`.
- After every commit print in the chat a table (Lavoro / Stato / Cosa succederà) in Italian, and say where the work stands and what is missing. The table is not in commits or repo files.
- UI first, build and test at the end. UI must match Logic in behaviour and look; icons are our own SVGs (traced ones may be modified).
- Never print or commit `.secrets/`. The owner dropped macOS work. Source files are CRLF: edit with Python (normalise, edit, convert back) or the Write tool; long heredocs fail in this shell.

## Done (committed)
F0, F0b converters and icon tracer; F1 presets (324) and Match EQ curves (75); F2 pan law, Channel EQ, compressor circuits, gate/limiter, Match EQ; F3 Mastering macro and limiter fit like `AutoAdjustLimiter`; F4 Klopfgeist click; F5a-e onnxruntime, Smart Tempo and Flex Pitch networks, Neural Saturation insert; F5f models and DLLs shipped with the app (`lpc_copy_models`); E1, E2, E3, E4, E5, E6, E8, E10, E13, E14.

## Build state (2026-10-21)
Everything is built and tested: core (`build-core`) 425 test cases, 1 skipped; UI QML 223, bridge 172 (1 skipped), all green. The UI had never been compiled before this day: only missing includes showed up.

## What is missing, in order
1. G (the owner, 2026-10-21): graphical windows for the stock effects. Done: Channel EQ, Compressor, Limiter, Adaptive Limiter, Noise Gate, Mastering Assistant, Match EQ, Reverb, Delay, Neural Saturation, Synth (files `ui/qml/Fx*View.qml`, frame `FxShell.qml`, chosen in the Loader of `EffectEditorWindow.qml`). Also done since: EQ Analyzer (SpectrumTap), Q-Couple, Processing (Stereo/Left/Right/Mid/Side, param `processing`), Gate Monitor + Characteristics (`keyMonitor`, `keyMode`), compressor side chain filter. Left: Drum Kit/External MIDI (still the knob grid), and G4 in the roadmap (what the windows draw but the engine does not do).
2. E15b Pattern tracks (Step Sequencer): `ui/qml/StepSequencer.qml` exists, the Pattern card of Create New Track is disabled until a pattern region exists.
3. E1 sub-item: Record Automation with Audio Regions (Trim and Relative are done).
4. Engine behaviour still only saved for: region automation priority, preset value for regions, balancer pan law, colorize takes, loadOnlyNeeded, resetRecordEnable, midi send/thru, assets copy flags, trim start of new regions; metronome Options and MIDI click output; CAF bounce.
5. F7 simplify and recolour the traced SVGs and wire them into QML. F8 macOS CI ctest 364.
6. The replacement of the -12/+12 dB guess in `MasteringProcessor`: the Mac could not say what level the Analog Model networks expect (see mac-pass-3.md 2b/2e).

## How to build and test
- Core: `cmake build-core` (needed when files are added), then `cmake --build build-core --config Release --target lpc_tests -- -m` (about 5 minutes), run `./build-core/tests/Release/lpc_tests.exe`. Models for the tests: `LPC_ONNX_MODELS_DIR=jad-daw-private/onnx`.
- UI: `build-ui` (Qt 6.8.3 in `.qt/`), kill running jad exes before building.
