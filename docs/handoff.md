# Handoff: where the work stands

Updated at every commit so that another session (or another AI) can resume. The detailed list is `docs/roadmap-autonomous.md` (sections E and F); this file is the short version.
Last update: 2026-10-19, after commit "bounce: Logic's dialog fields".

## Rules from the owner (Luigi, Italian-speaking)
- Use ALL the reverse-engineering knowledge (Logic Pro 11.2) and `jad-daw-private` (nested git repo, ignored): do not skip anything.
- Leave the CI alone until told. Commit, never push. Commits end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01P21WR25Z8dp2xuJ2oVVTx3`.
- After every commit print in the chat a table (Lavoro / Stato / Cosa succederà) in Italian, and say where the work stands and what is missing. The table is not in commits or repo files.
- UI first, build and test at the end. UI must match Logic in behaviour and look; icons are our own SVGs (traced ones may be modified).
- Never print or commit `.secrets/`. The owner dropped macOS work. Source files are CRLF: edit with Python (normalise, edit, convert back) or the Write tool; long heredocs fail in this shell.

## Done (committed)
F0, F0b converters and icon tracer; F1 presets (324) and Match EQ curves (75); F2 pan law, Channel EQ, compressor circuits, gate/limiter, Match EQ; F3 Mastering macro and limiter fit like `AutoAdjustLimiter`; F4 Klopfgeist click; F5a-d onnxruntime, Smart Tempo and Flex Pitch networks, Neural Saturation insert; F5f models and DLLs shipped with the app (`lpc_copy_models`); E1, E2, E3, E4, E5, E6, E10, E14.

## Not built yet (important)
The UI changes of F2, F3, E2, E4, E5, E6, E10 and F5f are written but `build-ui` has not been compiled and the QML/bridge tests (`jad_bridge_tests`, `tst_actions`, QML tests) have not been run. Core (`build-core`, `lpc_tests`) is built and green: 406 passed, 1 skipped.

## What is missing, in order
1. E8 Project Settings window with 11 tabs (General, Audio with Pan Law, Recording with Count-in vs Pre-roll, Smart Tempo, MIDI, Score, Movie, Synchronization, Metronome, Tuning, Assets).
2. E13 Metronome settings: Klopfgeist, Audio Click and MIDI Click with Bar/Group/Beat/Division rows (no counting-pattern UI). The owner must look at this mismatch.
3. E11 48-colour palette (16 x 3) for Assign Track Color. E12 I/O Labels for inputs, outputs and buses. E15 Create New Track dialog (4 categories). E16 Chord global track and Arrangement global track.
4. E10b MP3/M4A bounce destinations and CAF. E1 sub-items: Trim, Relative, Record Automation with Audio Regions.
5. F5e Analog Model of the Mastering: `T`/`TSO` (audio) can be done now as a stage like `ml::NeuralStream`; `G`/`S` (+`_SO`, one scalar per block) wait for the Ghidra pass on the Mac (prompt given to the owner; result expected in `docs/logic-reference/analog-model-chain.md`).
6. F7 simplify and recolour the traced SVGs (`assets/icons/logic-derived`, 91 files) and wire them into QML.
7. Build the UI (`build-ui`), run all UI tests, fix. Run the Mac harness `tools/ml/coreml_reference.swift` vs ONNX when a Mac is available.
8. F8 (later): GitHub macOS CI fails ctest 364 "undo: random command sequences undo and redo exactly".

## How to build and test
- Core: `cmake build-core` (needed when files are added), then `cmake --build build-core --config Release --target lpc_tests -- -m` (about 5 minutes), run `./build-core/tests/Release/lpc_tests.exe`. Models for the tests: `LPC_ONNX_MODELS_DIR=jad-daw-private/onnx`.
- UI: `build-ui` (Qt 6.8.3 in `.qt/`), kill running jad exes before building.
