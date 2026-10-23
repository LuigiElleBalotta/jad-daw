# Handoff: where the work stands

Updated at every commit so that another session (or another AI) can resume. The detailed list is `docs/roadmap-autonomous.md` (sections E and F); this file is the short version.
Last update: 2026-10-23, after region automation and the rest of the list.

## Rules from the owner (Luigi, Italian-speaking)
- Use ALL the reverse-engineering knowledge (Logic Pro 11.2) and `jad-daw-private` (nested git repo, ignored): do not skip anything.
- Leave the CI alone until told. Commit, never push. Commits end with `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01P21WR25Z8dp2xuJ2oVVTx3`.
- After every commit print in the chat a table (Lavoro / Stato / Cosa succederà) in Italian, and say where the work stands and what is missing. The table is not in commits or repo files.
- UI first, build and test at the end. UI must match Logic in behaviour and look; icons are our own SVGs (traced ones may be modified).
- Never print or commit `.secrets/`. The owner dropped macOS work. Source files are CRLF: edit with Python (normalise, edit, convert back) or the Write tool; long heredocs fail in this shell.

## Done (committed)
F0, F0b converters and icon tracer; F1 presets (324) and Match EQ curves (75); F2 pan law, Channel EQ, compressor circuits, gate/limiter, Match EQ; F3 Mastering macro and limiter fit like `AutoAdjustLimiter`; F4 Klopfgeist click; F5a-e onnxruntime, Smart Tempo and Flex Pitch networks, Neural Saturation insert; F5f models and DLLs shipped with the app (`lpc_copy_models`); E1, E2, E3, E4, E5, E6, E8, E10, E13, E14.

## Build state (2026-10-21)
Everything is built and tested: core (`build-core`) 434 test cases, 1 skipped; UI QML 228, bridge 172 (1 skipped), all green. The UI had never been compiled before this day: only missing includes showed up.

## What is missing, in order
1. G (the owner, 2026-10-21): graphical windows for the stock effects: all done now, including the Limiter Mode (Legacy/Precision), the Mastering Custom EQ and Spread, the Match EQ Learn buttons (Current and Reference, from what reaches the insert) and the External MIDI window (`FxExternalMidiView`: channel pads, port, bank, program, volume, pan). Side chain between tracks is done (`ProcessorRef::sideChain`, Compressor and Gate; the Side Chain menu of the window). Also done: Sampler instrument + Convert Regions to New Sampler Track, File > Print and Page Setup (PDF).
2. (done) Pattern tracks: the Pattern card of Create New Track makes a drum instrument track and opens the Step Sequencer.
3. (done 2026-10-23) E1: Record Automation with Audio/MIDI Regions (`Track::recordInRegions`, the Automation slot's menu).
4. (done 2026-10-23, code first, tests written but only partly run) Region automation: `Region::automation` (ticks from the region start), `set_region_automation`, split/trim/join keep the lanes (`core/src/automation.cpp`), `playingLanes` lays them over the track's lanes with the two project choices, Mix > Region Automation and Convert Automation (`convert_automation`), the lanes are drawn in the regions (`AutomationLane.regionId`). Also done: track On/Off (`Track::on`, Only load plug-ins needed), Reset record enable state, MIDI settings sent after loading, Allow tempo change recording, Trim start of new regions (off by default, needs the analysis). Assets as in Logic (2026-10-23): a media path is relative to the project or the absolute path of a file kept elsewhere (`validMediaPath`, `set_media_path`); Add/Import follow "Copy audio files into project", drops always copy, Save and File > Project Management > Consolidate copy the referenced files in, Missing Audio Files window on open (Locate / Search a Folder), Audio Recording Path.
5. F7 done (ten glyphs wired, see roadmap). F8 macOS CI: confirmed green on run 39817637433 (all four jobs; core mac 6 min, UI mac 27 min, thanks to `--parallel 4`). 0.0.2 alpha released 2026-10-23.
6. The replacement of the -12/+12 dB guess in `MasteringProcessor`: the Mac could not say what level the Analog Model networks expect (see mac-pass-3.md 2b/2e).

## How to build and test
- Core: `cmake build-core` (needed when files are added), then `cmake --build build-core --config Release --target lpc_tests -- -m` (about 5 minutes), run `./build-core/tests/Release/lpc_tests.exe`. Models for the tests: `LPC_ONNX_MODELS_DIR=jad-daw-private/onnx`.
- UI: `build-ui` (Qt 6.8.3 in `.qt/`), kill running jad exes before building.
