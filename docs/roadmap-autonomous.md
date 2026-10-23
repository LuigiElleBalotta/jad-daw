# Autonomous roadmap (what is missing for a recording + mixing session)

Requested by the project owner on 2026-10-10: do all of it, in this order, without waiting for answers. Each item: write the code, add
tests (core Catch2, bridge QTest, QML TestCase), build, run the whole suite (`/tmp/buildui.sh` in the author's setup: build-ui + ctest;
grep the build log for `error`, a stale test binary can report a pass), update the README, commit, push to main. Mark an item `[x]`
when it is done and pushed; leave a one-line note under it about what is deliberately not covered.

Working rules: the UI should look and behave like Logic Pro (guide pages in the corpus, see CLAUDE.md); icons are our own SVGs; never
commit secrets (`.secrets/` is ignored); ask before outward actions; the CI fix waits until the owner asks.

## 1. Audio settings (device, rate, buffer)  — first
- [x] Preferences window (File > Preferences, Ctrl+,) with an Audio tab: output device, input device, buffer size, latency, open-device
      status; Apply reopens the device; remembered in QSettings. (The sample rate follows the project; other tabs say not implemented.)
- [x] A project whose rate differs from the device is told so with the fix: the device error is shown in the Audio tab.
- [x] Device list from JUCE (`listJuceAudioDevices`); a fake lister for tests.

## 2. Inputs, monitoring, recording quality
- [x] Input channel per track (the strip's Input slot: 1+2 stereo, or one input as mono), stored in the project (`set_strip` input).
- [x] Input monitoring (the I button): the chosen input channels play through the track's strip and inserts. (No Auto Input Monitoring.)
- [x] Latency compensation: takes move earlier by the device's reported round trip plus the recording delay of Preferences > Audio.
- [x] Several armed tracks record at once (one take per track).
- [x] Punch: Record while playing is a quick punch-in (Record > Allow Quick Punch-In), Record again stops recording and keeps playing; the autopunch
      range (red strip under the cycle area, Pnc button) crops takes and stops recording at its end.
- [x] Cycle recording makes one region per pass (overlapping). Not covered: take folders, comping, muting the earlier takes.
- [x] Input level meter on the armed track's strip (its meter shows the input).

## 3. Built-in effects for mixing
- [x] Channel EQ (low/high shelf + 3 parametric bands, output gain), with its response curve in the strip's EQ box and in its editor.
- [x] Compressor (threshold, ratio, attack, release, knee, make-up, mix) with the gain-reduction bar of the strip.
- [x] Reverb (Freeverb layout) and Delay (time, feedback, mix, ping-pong), Limiter (1 ms look-ahead, reported as latency), Noise Gate.
- [x] They appear in the Audio FX slot menu (grouped), a double click opens an editor window with a slider per parameter, they are saved in the
      project and the offline bounce renders them. (A parameter change rebuilds the effect: its tail restarts.)

## 4. MIDI
- [x] MIDI input devices (File > Preferences > MIDI), live play of the selected or armed instrument track (also with the transport stopped), MIDI
      recording into a region with the count-in (cycle passes merge), Musical Typing window (Window > Show Musical Typing, Ctrl+K).
      Not covered: step input, sustain pedal and pitch bend/CC recording (only notes are stored).
- [x] A polyphonic Synth (waveform, ADSR, low-pass) chosen from the instrument slot; the sine stays as "Sine" (the default).
- [x] VST3 instruments (hosting), MIDI to plug-ins, controller events (CC, pitch bend, aftertouch) in regions: played, recorded, imported, edited in the Piano Roll lane.
      Not covered: step input, MIDI effects, multi-output instruments.

## 5. Audio editing
- [x] Fades (in, out) on audio regions: quarter-sine ramps, handles at the top corners of a selected region, `set_region_fades` with undo. Not covered:
      crossfade curves other than that, automatic crossfades of overlapping regions, region gain handles (the Inspector has the gain).
- [x] Waveform Zoom (View > Zoom: 1x, 2x, 4x, 8x), Audio Track Editor (the region's own waveform, selection, Trim to Selection, the Edit > Audio functions) and
      Audio File Editor (the whole file with the region marked) in the Editors area. Region waveforms now show only the region's part of the file.
- [x] Edit > Audio: Normalize, Reverse, Change Gain, Time Stretch (WSOLA, pitch kept), Pitch Shift (length kept) make a new file in the project and replace the
      region media in one undo step; Strip Silence cuts a region at its silences. (Region looping is not done.)

## 6. Mixer and automation
- [x] Groups (set_groups; volume, pan, mute, solo and selection shared; Group slot menu, Group Settings window, Mix > Groups Active), I/O Labels (names of
      the interface inputs), Pre-Fader Metering, summing stacks (Track > Create Track Stack: a new aux that sums the selected tracks). Not covered: folder stacks,
      I/O Assignments.
- [x] Automation modes per track (Off, Read, Touch, Latch, Write; the strip's Automation slot) that write fader and pan moves into the lanes while the project
      plays (one undo step per take). Not covered: sends and plug-in parameters as targets, curve shapes between points.

## 7. Export
- [x] Bounce dialog: WAV 16/24/32-bit float, AIFF 16/24, normalize to -0.3 dBFS, range (whole project / cycle area), tail, TPDF dither on 16-bit; the built-in effects
      are rendered. FLAC 16/24 (own encoder) and VST3 effects and instruments in the render. Not covered: MP3 (no encoder library).

## 8. Windows and dialogs of the menus (all of them)
- [x] Project Settings (name, rate, tempo, count-in, recording), Preferences (Audio, MIDI; the other tabs say what is not done).
- [x] Undo History, Customize Control Bar and Display, Customize Toolbar, Key Commands editor, Colors. Not done: Quick Help.
- [x] Loop Browser and Browsers (a file browser, no audition), List Editors (Event, Marker, Tempo, Signature), Note Pad, Project Audio.
- [x] Score (view only), Step Sequencer, MIDI Transform, Group Settings, I/O Labels/Assignments are done. Not done: Session Players, Automation Settings.
- [x] The remaining `stub`s (about 74: Flex, Smart Tempo, Session Players, folder stacks, movie, printing, Mac window sets, ...) say "not implemented yet" when chosen; each is listed in `ui/actions/actions.json` with status `stub`.

## 9. Platform
- [x] (dropped by the owner, 2026-10-11: "dimenticati di macOS"; the app icon exists) was: macOS run and the CI Configure fix; the app icon exists) macOS build run for real (never done), the CI Configure failure (only when the owner says), signed bundle, app icon.

## 10. The rest of the `stub` actions (the owner: "port them to Windows")
Most stubs were never Mac-only: they are Logic features not written yet. Left out for good: Print, Page Setup, Movie, Dolby Atmos, Mastering Assistant.
- [x] Batch 1: Record Enable / Input Monitoring (already wired), Select Tracks, Auto Set Locators, Snap Smart, Drag Mode Shuffle L/R.
- [x] Batch 2: Overlapping MIDI/Audio Recordings (Create Take Folder / Create Tracks / Create Tracks and Mute, MIDI also Merge; the passes of a cycle are told apart by the engine), Use Musical Grid, Record Button Options (Record/Record Toggle, Record/Record Repeat), Discard Recording. Not done: Low Latency Monitoring Mode (needs an engine switch that bypasses latent inserts on monitored tracks) and Play/Stop Button Options (the guide does not describe them).
- [x] Batch 3: Edit: Trim > Fill within Locators; Cut/Insert Time (Insert Silence / Cut Section Between Locators: regions only, markers, tempo and automation stay); Delete MIDI Events (duplicates, inside, outside locators); Separate MIDI Events by Note Pitch (by event channel needs a channel in the notes: no); Copy MIDI Events dialog (copy/move x merge/replace/insert, no Rotate); Move > To Recorded Position (media remember `recordedAt`), To Beat / First Transient to Nearest Beat (an onset finder), To Focused Track; Join per Tracks; Open in External Sample Editor. Left: Copy/Insert Section, SMPTE-locked regions, Convert and Tempo submenus.
- [x] Batch 4: region names (model, Region inspector) and the Text, Slip and Rotate tools (Slip right now moves audio content right); floats: Transport, Event, Region Inspector, Step Input Keyboard (put notes at the playhead, chord/rest/back); Window > Cycle Through Windows, Move & Resize. Then also: Marquee tool (cuts at its edges for Cut/Copy/Delete), Automation Curve tool (a `curve` on automation points, played by the engine) and Automation Select tool; the Low Latency Monitoring Mode. Flex tool: done later (item 11).
- [x] Batch 5: Track > Assign Track Icon (12 own pictures, `icon` on the track), Configure Track Header (which buttons and sliders show), Show Output Track. Left: External MIDI track (no MIDI output), Session Player, stacks, Track Alternatives, Drum Replacement, Sampler conversion.
- [x] Batch 6: Mix > Automation Settings window, Automation Quick Access (one MIDI CC writes volume or pan of the selected track), Autoselect Automation Parameter in Read Mode; LCD Key (project key, `set_project_key`); File > Project Alternatives (named copies), Project Management (show folder, clean up unused audio), Share (bounce). Left: Convert Automation (there is no region automation), Open Marker Text, Play/Stop Button Options.

## 11. Flex Time and Flex Pitch (the owner: "Inizia dal flex")
- [x] Papers read and written up in `docs/logic-reference/flex-algorithms.md` (WSOLA, phase vocoder with identity phase locking, peak-shift pitching, YIN, pYIN, the MonoNote note model).
- [x] Core DSP with tests: pitch track and notes, transients, time map, WSOLA with kept attacks, phase vocoder, slicing, rhythmic, tempophone, speed, TD-PSOLA note editing.
- [x] Model: flex data on audio regions (markers, notes, the source media), flex mode on the track; split, trim and join keep it; the source media counts as used.
- [x] Engine: an audition voice (a short clip over everything, repeating, replaced while it plays) so a held or dragged note is heard.
- [x] UI: Flex button and mode pop-up in the header, Show Flex Pitch/Time, Flex tool, transients and markers over the regions, drag/slide/delete, quantize, slice, reset, Flex Pitch editor with note strip, MIDI from the notes.
- [x] Left out for now (listed, not blocking): Smart Tempo and Flex & Follow; hotspots on the notes themselves (the sliders do their job); a pitch/time live preview of a marker drag (the audio changes when the marker is let go); Q-Reference groups; Option-drag moves the marker along the sound (a different thing than Logic's).

## Log
- 2026-10-10: roadmap written. Done before it: metronome with counting modes and user samples, audio recording with count-in,
  automation (volume/pan), global tracks, markers, cycle area, Save As/Import/Bounce, MP3/FLAC/AIFF import and drop-to-new-track.
- 2026-10-10: Track > Hide Selected Track / Hide Unselected Tracks / Unhide All Tracks / Toggle Hide View (any track but the master can be hidden; hidden tracks keep playing and stay in the Mixer).
- 2026-10-10: Track > Sort Tracks by (name/type/color, set_track_order command) and Assign Track Color (Colors window).
- 2026-10-10: Marker List window (Navigate > Open Marker List, Go To > Marker, Rename Marker).
- 2026-10-10: Mix > Delete Automation and Create Track Automation.
- 2026-10-10: MIDI file import/export (core midi_file, File > Import > MIDI File, File > Export > Selected Regions as MIDI File).
- 2026-10-10: Record > Auto Input Monitoring (armed tracks monitor while stopped or recording) and Recording Settings (Project Settings).
- 2026-10-10: View > Control Bar / Toolbar toggles, Note Pads (notes.txt in the project), Window > Open Project Audio.
- 2026-10-10: VST3 instruments (IInstrument, notes to the plug-in at their offsets, stop messages, live notes, PDC, scanner lists instruments, state commit, test synth plug-in and host tests).
- 2026-10-10: MIDI controller events in regions (MidiControl: control change, aftertouch, pitch bend): model/JSON, validation, split/resize/join, playback to plug-in instruments, live forwarding, recording, MIDI file; controller API regionControls/setRegionControls..
- 2026-10-10: Piano Roll controller lane (sustain, modulation, volume, pan, expression, pitch bend, aftertouch).
- 2026-10-10: the bounce renders VST3 effects and instruments (live instances, transport locked while rendering).
- 2026-10-10: FLAC export (own encoder: fixed predictors + Rice, verified through the decoder); Bounce dialog offers FLAC 16/24. MP3 export still open (needs an encoder library).
- 2026-10-10: region Loop/Unloop (audio and MIDI) and Mute Regions now real (model, playback, Inspector, L key).
- 2026-10-10: non-destructive MIDI shaping: region Quantize/Transpose/Velocity and instrument track Transpose/Velocity/Key limit/Velocity limit (Inspector).
- 2026-10-10: Track Delay (ms) for audio and instrument tracks.
- 2026-10-10: takes: cycle recording passes share a take group (last plays, others muted), region right-click menu to pick the take, delete others, unpack. Quick-swipe comping not done.
- 2026-10-10: Edit > Repeat > Multiple, Length > Change, Track > Search and Select Track, View > Colors, Mix > I/O Assignments, Show All Plug-in Windows, Window > Show Keyboard.
- 2026-10-10: Bounce in Place (Track menu). Freeze still open.
- 2026-10-10: List Editors window (Event, Marker, Tempo, Signature).
- 2026-10-10: Customize Control Bar and Display / Customize Toolbar.
- 2026-10-10: Browsers / Loop Browser (file browser, import on double click; no audition).
- 2026-10-10: Drag modes No Overlap and X-Fade.
- 2026-10-10: File > Save as Template / New from Template.
- 2026-10-10: Edit > Paste Replace and Move > Shuffle Left/Right.
- 2026-10-10: Step Sequencer (drum grid) and a read-only Score in the Editors area.
- 2026-10-10: MIDI Transform window.
- 2026-10-10: Quick Help bar (action name, menu path, key of the hovered button).
- 2026-10-10: Mix > Move Track Automation with Regions.
- 2026-10-10: Window > Zoom All / Fill / Center / Full Screen Tile / Bring All to Front, Open Audio Track Editor / Audio File Editor / Step Editor.
- 2026-10-10: Track > Freeze (rendered audio before the fader replaces instrument, regions and inserts; set_track_freeze, undo).
- 2026-10-10: Mix > Search and Add Plug-in.
- 2026-10-10: send level automation (target send:<id>, lanes Send 1/Send 2 in Show Automation).
- 2026-10-10: comping: Split Takes at Playhead (each side a passage of its own, pick a take per part).
- 2026-10-10: plug-in parameter automation (lane target param/<vst3 id>/<n>/<index>, per-track lane parameter chooser).
- 2026-10-10: unsaved-changes prompt (save, don't save, cancel) and autosave with recovery.
- 2026-10-10: Gain, Fade and Solo tools.
- 2026-10-10: Edit > Move > Slip/Rotate Left/Right and Set Nudge Value.
- 2026-10-10: stub actions batch 1 (Select Tracks, Auto Set Locators, Snap Smart, Shuffle L/R drag modes).
- 2026-10-10: stub actions batch 2 (overlapping recordings, musical grid, record button options, discard recording).
- 2026-10-10: stub actions batch 3 (edit menu: locators, cut/insert time, MIDI events, move to recorded position and beat).
- 2026-10-10: stub actions batches 4-6 (tools, floats, track icons and header, automation settings, project key, alternatives, project management).
- 2026-10-10: Low Latency Monitoring Mode, Automation Curve and Select tools, Marquee tool, Add Audio File… in the track shortcut menu; 74 stub actions became 29 (what is left is Apple-only, needs hardware or is a large feature of its own).
- 2026-10-11: Flex Time and Flex Pitch (item 11): core DSP (pYIN, MonoNote notes, WSOLA with kept attacks, phase vocoder with identity phase locking, slicing, rhythmic, tempophone, speed, TD-PSOLA note editing), flex data on regions and tracks (with split, trim and join), an audition voice in the engine, markers, notes, Flex button, Flex tool, Flex Pitch editor.

## 12. What item 11 left out (the owner: "Finisci le cose lasciate fuori")

- [x] A marker drag is heard while it moves; six hotspots on the selected Flex Pitch note; Q-Reference (the first selected region leads the quantize of the others).
- [x] Smart Tempo analysis (`core/src/tempo_analysis.cpp`: onset curve, tempo from autocorrelation, dynamic-programming beats, downbeat guess) and Flex & Follow: beats kept in the flex data, markers put every beat on the project's beats (bars on bars), conformed again when the tempo or place changes; Apply Region Tempo to Project Tempo; move the downbeat; beats drawn in the Flex view.
- [x] Flex Pitch tried on a voice: Fish Audio answered 402 (no API credit), so a Windows speech synthesis recording was used (`LPC_VOICE_WAV=<wav> lpc_tests [voice]` prints the notes found: plausible pitches, 12 notes in a spoken sentence). A real singer is still untested.
- [x] Beat markers can be dragged and deleted in the Flex view (`smartMoveBeat`, `smartDeleteBeat`).
- [x] Smart Tempo hints: Downbeat Here / Beat Here at the playhead (`smartHint`).
- [x] Hints can be tapped while playing: Alt+D / Alt+T (plain D is List Editors), collected and applied as one edit.
- [x] A round slide handle on each Flex Time marker (the marker moves along the sound, like Alt-drag).

## 13. Mastering Assistant and Drum Replacement (the owner: "Farei drum replacement e mastering assistant sicuramente")

- [x] Mastering Assistant: loudness measurement (BS.1770), mix analysis, `builtin.mastering` insert (correction EQ, characters, excite, loudness, limiter at -1 dB, bypass, compensation), Mix menu entry, window with Reanalyze, tests.
- [x] Drum Replacement: hits found with the transient detector, built-in synthesized sounds or a chosen file, Replace (ducking the original) or Double, sensitivity, level, velocity response; audio-to-audio in one undo step (there is no sampler instrument to play MIDI hits).
- [x] Session Players: chord parser, rule-based Drummer / Bass / Keys generators with styles, intensity, swing and seeds, a Drum Kit instrument (General MIDI drums made in the program), dialog and track creation in one undo step. Not done: a Session Player Editor, a Chord Track.
- [x] Track Stacks: `Track.parent/stack/stackOpen` with commands, validation and JSON; folder and summing stacks, fold/unfold, Flatten Stack, Convert Folder Stack to Summing Stack; the Tracks area indents and folds them.
- [x] Checked on a real recording (`LPC_REAL_AUDIO=<file> lpc_tests [real]`, Boulevard_78bpm.mp3): Smart Tempo finds 77.9 bpm (78 expected), the mastering analysis reads -23.2 LUFS and asked for +-6 dB on most bands, so the Auto EQ was made gentler (0.4 of the difference, at most 4 dB); the drum detector finds 78 hits in the first minute. Not heard: no audio device in these runs.



## 14. The night plan (the owner: "niente funzionalita' a meta'", then "le orecchie della daw")

Rule: a feature is done only when it works end to end, has a test, is checked on a real mp3 from `C:/Users/l.balotta/Music/band/sequenze` where audio is involved, and its README line says what it does and does not do. Order: finish what is half done, then the DAW's ears.

### A. Finish the half-done features
- [x] A1. Session Players (the region already takes the colour of its track: the palette is by row, as for every track; Regenerate and the chord source from markers are done): the part can be made again from the menu with the same settings kept in the region (new seed); a chord progression can be read from the project's markers (marker names such as "Am", "F") so there is a chord source.
- [x] A2. Drum Replacement (checked on Boulevard_78bpm.mp3: 78 hits in the first minute, one per beat at 78 bpm): also "make a MIDI track with the hits" playing the Drum Kit (velocity from the level), so the hits can be edited; tested on Boulevard_78bpm.mp3.
- [x] A3. Mastering Assistant: the window's Output line (LUFS-I, LU range, dBTP, momentary max, from an offline render with the insert on; no live meter while playing), a true-peak limiter, Loudness Compensation measured (`compTrim`), compressor before the gain, soft clip; checked on the real mp3 (peak -0.98 dBTP, +4.7 LU louder; -14 is out of reach for its 22 dB peak-to-loudness ratio and the window says so).
- [x] A4. Flex Pitch (checked on a spoken sentence, `LPC_VOICE_WAV=<wav> lpc_tests [voice]`: the longest note moved +3 semitones reads +3.0, the length is kept, everything outside the note is bit-identical, gain and formant leave the pitch alone): a quality pass on a spoken or sung recording, hotspot values checked against the numbers, notes edited and rendered, audio compared before/after in a test.
- [x] A5. Smart Tempo (3 or 4 beats a bar detected; a 90 to 130 bpm recording is followed within 5 bpm and its tempo events are written; 6/8 and other meters are not told apart; also fixed the flaky `jad_bridge_tests`: a bounce test read a half-written file): time signatures other than 4/4 (3/4, 6/8) in the beat/downbeat model and in Flex & Follow; tempo changes inside a recording (a tempo map from the beats, "Apply Region Tempo" writing tempo events along the region, not one tempo).
- [x] A6. External MIDI track (core instrument, JUCE port, menu, port list, channel, test note; the real port could not be opened on this PC, see README): a MIDI output in the platform layer (JUCE), a track field with device and channel, the engine sends its MIDI there; New External MIDI Track in the menu; tests with a fake output.
- [x] A7 (Track Alternatives, Marker Text, SMPTE-locked selection, Deselect Global Tracks done; Play/Stop Button Options stay stubs, not documented anywhere): the small stubs that can be real: Play/Stop button options, Open Marker Text, Deselect Global Tracks, Show Track Alternatives (takes), SMPTE-locked regions (absolute-time regions). Movie, printing, Dolby Atmos stay out for good.
- [x] A8 (done on a project made from Boulevard_78bpm.mp3: Flex & Follow, the Flex editor, Mastering Assistant (-14.4 LUFS, -1.1 dBTP), Drum Replacement MIDI only, Marker Text, Track Alternatives, Track Stacks, Session Players, External MIDI; defects found and fixed: the mastering gain, region length after Follow, marker text commit, steady dialog, toggle check): a full pass: every new window opened once in the app (Flex editor, Smart Tempo beats, Mastering, Drum Replacement, Session Players, Stacks), screenshots checked, defects fixed.

### B. The DAW's ears (an MCP server so an AI can assist with mixing, recording and mastering)
- [x] B1 (`core/src/audio_analysis.cpp`: `analyseAudio`, `toJson`, `describeReport`, `compareMasking`; checked on Boulevard_78bpm.mp3): core audio analysis library: per-track and master loudness (M/S/I, LRA), true peak, RMS, crest factor, third-octave spectrum, stereo width and correlation, clipping and DC, silence, tempo, spectral centroid, masking between two tracks (band overlap). JSON output. Tests on synthetic and real audio.
- [x] B2 (`tools/lpc-cli/cli_ears.cpp`: snapshot, analyze, masking, apply, render --track; tested in `tests/test_cli.cpp`, tried on the Boulevard project): `lpc-cli` commands: `analyze <project|wav> [--track id] [--range a:b]`, `snapshot` (project state as JSON: tracks, inserts with parameters, sends, automation, regions, tempo), `apply` (JSON commands, undoable), `render --solo track`, so a headless session works on a project file.
- [x] B3. MCP server (stdio, Python) over lpc-cli: tools list_tracks, get_state, analyze_mix, analyze_track, compare_tracks (masking), set_fader/pan/mute/solo, add_insert/set_insert_param (EQ, compressor, limiter, reverb, mastering), run_mastering_analysis, render_excerpt, undo/redo, save. Safe by default: works on a copy unless told otherwise; every change is one undo step; a log of what the AI did.
- [x] B4. Live link: the running app exposes the same commands and meters over a loopback socket with a token, so the MCP server can drive and listen to the open project (playhead, live meters, record arm), not only files.
- [x] B5. Recording assistant tools: input level check (peak, noise floor, clipping risk), gain staging suggestion, latency check, take comparison.
- [x] B6. A guide for the AI (docs/ai-assistant.md): what each tool measures, target values (LUFS per platform, headroom, balance curves), a mixing and mastering checklist, example sessions; verified end to end on a real mp3 project.

- [x] B7. Pictures for the ears: `render_image` (spectrogram, waveform with clipping in red, third-octave spectrum) as PNG of the mix, a track or a file (`lpc-cli analyze --image out.png --kind ...`, own PNG encoder in core); audio clips for models are the existing `render_excerpt` WAV.
- [x] B8. `compare_audio`: two sounds (before/after render, mix vs reference) side by side: loudness, peak, crest, stereo and tonal difference per band with the overall level taken out.

### C. Output strips (the owner, 2026-10-12: Logic's mixer has "Stereo Out" beside "Master", the Mastering Assistant lives on Stereo Out, and with an XR18 all its outputs should be visible)
Today: one Master track, stereo engine (device channels above 2 are cleared), "St Out" is only the label of the routing menu.
- [x] C1 (the Master volume strip was added on 2026-10-12 at the owner's request). Multi-output engine and Output strips. Model: `TrackKind::Output` tracks with `outputPair` (0 = Out 1-2, 1 = Out 3-4, ...); the existing master is Out 1-2 and is shown in the Mixer as "Stereo Out" (its strip keeps inserts, so Mastering Assistant is there, plus a separate "Master" volume strip that scales every output when there is more than one); the Output menu of a track lists "Stereo Out" and the other pairs the device has; Output strips appear in the Mixer when the device has more outputs or something is routed to them (Output filter in the Mixer header); the engine renders each output pair into its device channels (JUCE device opens all outputs); offline render and bounce keep mixing Stereo Out only unless a pair is chosen; tests (graph with two output pairs, JSON round trip, routing menu, bridge), screenshot of the Mixer.

### D. Reverse-engineering pass on the real Logic Pro 11.2 (the owner, 2026-10-14: set up REA, then "aggiorna la documentazione... ed estrai gli asset")
- [x] D1. REA (`github.com/morluto/rea`) set up for Claude Code, with Hopper and a self-built Ghidra 12.1.4 (native decompiler compiled from source,
      macOS releases don't ship one) as its two providers.
- [x] D2. `docs/logic-reference/color-tokens.md`: ~836 named semantic colour tokens read out of `Assets.car` (Display P3, converted to sRGB), curated to the
      ones relevant to the Mixer/Tracks/LCD, with hex values; confirms Logic's workspace is one fixed dark theme (not light/dark adaptive).
- [x] D3. `smart-controls-and-editors.md` extended with the exact Smart Control knob set and order for Logic's Compressor, Limiter, Noise Gate, Echo/Delay
      Designer/Tape Delay and PlatinumVerb/SilverVerb (from `ControlsViewParameters.plist`) — a direct reference for giving jad-daw's own built-ins the same
      Smart Controls knobs.
- [x] D4. `shortcuts-check.md` / `actions.json`: 16 more shortcuts marked verified (3 → 19 of 107) by reading them live off the running Key Command
      Assignments window (search field + screenshot, via a one-off accessibility pass — REA's own native-UI tool needed its hardcoded compile timeout raised
      locally, its `swiftc` step takes ~90 s on this machine against a 60 s default). Two open questions recorded in `asset-reference.md`: Logic splits Zoom
      into Horizontal/Vertical (no single +/- "Zoom In"), and `transport.goToPosition`'s key rendered ambiguously on this machine's keyboard layout.
- [x] D5. Extracted material (colour tokens, plug-in parameter JSON, legacy mixer bitmaps, live screenshots) kept in `../logic-pro-reference/`, a sibling
      folder outside git (Apple's assets, not ours to commit); pointer and method in `docs/logic-reference/asset-reference.md`.

### D (Phase 2). Binary decompilation for algorithm-level detail (the owner, 2026-10-16: UI phase done, "prima scopri la UI e poi decompila per dettagli algoritmici"; private, non-distributed study project — see owner's explicit scope note below)
Static analysis of Logic's own frameworks (`MADSP.framework` = DSP engine, `MAMixer.framework` = mixer UI/view-models only) via REA + Ghidra 12.1.4,
universal binaries thinned to x86_64 with `lipo` first (Ghidra v1 can't import a fat Mach-O directly). `REA_GHIDRA_STARTUP_TIMEOUT_MS` raised from the
330 s default to 1,800,000 (30 min) in `~/.claude.json`'s `rea` server env — `MADSP` is 26 MB and auto-analysis alone exceeded the default on this
machine; each target switch re-analyzes from scratch (ephemeral Ghidra project, ~10-20 min for MADSP).
- [x] D6. Metronome ("Klopfgeist") confirmed at the engine level, not just in the Settings UI: a real `CKnock`/`CKnockBackplane`/`CKnockShell`/`CKnockVoice`
      class family synthesising the click from a resonant filter excited per tick (128-entry cutoff lookup table, `Q = clamp(-1.39×reso²+1.4, 0.01, 1.4)`),
      with `TonalityToReso(t) = 1-(1-t)³` and a Level/Velocity crossfade (`LevelViaVel`) — not sample playback. Written up in
      `docs/logic-reference/behavior-check-2026-10-14.md` under the existing Metronome mismatch note.
- [x] D7. Pan Law exact formula: `CStereoMixer::SetPanForChannel` is a **quadratic** law (`gainL=1-t²`, `gainR=1-(1-t)²`, t=pan/200+0.5), center dip
      **-2.498 dB**, not the textbook equal-power/-3.01 dB or linear/-6.02 dB law. `CMixer::SetCompensated` applies a `4/3` (+2.498 dB) multiplier for the
      UI's "-3 dB Compensated" default — the exact reciprocal of the dip, so "compensated" means unity gain at center, not a flat -3 dB shift. Full writeup
      in `docs/logic-reference/mixer.md`. Open: the other named Pan Law presets' (0 dB / -4.5 dB / -6 dB, compensated or not) exact multipliers weren't traced.
- [x] D8. Mastering Assistant: the `CAutoMaster` chain (loudness analysis, Match EQ, multiband compressor, limiter, saturation/waveshaper, spread,
      correlation) lives in `MADSP.framework`, not `MAMixer.framework` (that one's just the UI/view-models). The single "Loudness Macro" slider's exact
      piecewise-linear mappings to compressor gain, limiter gain/release, and waveshaper dry/wet are documented in `docs/logic-reference/mixer.md` — staged
      engagement (limiter/saturation stay off below a threshold, then ramp linearly), not a uniform scale.
      **Match EQ traced further (second pass):** it's a separate `CMatchEQFX` class with a 4-stage pipeline — capture a target spectrum (reference audio
      file or built-in model), convert both source and target to a log-frequency dB curve (the same curve the UI graph draws), fade that curve to flat at
      the spectral extremes (found/decompiled: averages per-channel spectra, finds where content drops within a threshold of the peak — i.e. separates
      real content from noise floor — and fades over a configurable width there, which is *why* the UI has a Fade Extremes control), then turns the result
      into smoothed, cached filter coefficients. **`Coefficients()` fully decompiled (third pass, at the owner's request):** it's a 126-block parameter
      dispatcher whose gain/Q cases call straight into a member `CBiquadChain` (`SetGaindB`/`SetQ`/`CalcCoeffs` per band) — confirms the correction is
      applied as a **parametric biquad filter bank**, not FFT-domain convolution; the FFT/spectrum work above is analysis-only.
      **Fourth pass (at the owner's request):** loudness measurement is confirmed as **ITU-R BS.1770/EBU R128** — `CLoudnessMeter`'s own method names
      (`AnalyseMomentaryLoudness`/`...Short...`/`...Integrated...`/`...LoudnessRange`, `SetupPreFilter` for K-weighting, histogram-based relative gating)
      match the standard exactly, independently confirming jad-daw's own BS.1770 choice matches Apple's real implementation. And `AutoAdjustLimiter` is not
      a closed-form curve at all — it's a small iterative controller (render a trial chunk, measure loudness, add the error onto a running gain-in-dB,
      re-render, up to 5 attempts or until within 0.2 dB/LU); `AutoAdjustMBCompressor` is the same shape with fixed +2 dB steps instead.
      **Fifth pass:** `ProcessLimiter`/`ProcessMBCompressor` themselves fully decompiled — both are orchestration (gain staging, buffer chunking, margin
      bookkeeping), not the DSP kernel: the actual limiting/compression curve is behind a runtime-polymorphic `CGen`-family object this static pass can't
      name concretely. `ProcessLimiter` does yield one clean, reusable fact: its dB meter readout is computed via a fast IEEE-754 bit-hack `log2`→dB
      approximation (`dB ≈ (e + m − 0.33985×(m−1.5)² − 0.9150375) × 6.0206003`).
      **Sixth pass (at the owner's request — stock Compressor/EQ specifically, for their special mixer-strip UI):** found the concrete class family
      directly (searching for the standalone plugins beat tracing `CAutoMaster`'s vtable pointer) — `CCompressorBase` + six circuit-model subclasses
      (`SSL`, `Opto`, `DBX`, `LP`, `ClassA`, `FET`) matching Logic's "Circuit Type" selector, each with its own distinct curve (SSL: dual time-constant +
      soft-knee cubic at exact bounds 0.6151–1.1925 + a secondary limiter stage; Opto: single time constant + the same knee shape + a power-law/
      Michaelis-Menten saturation curve, `powf(x,-1.2)` then `37.15y/(37.15y+100)`) — real, structurally distinct circuit emulations, not reskinned
      numbers. Also found `CNoisegateFX` (fully named setters: Threshold/Attack/Release/Hold/Reduction/Lookahead/Keylisten/KeyFilter, plus a
      hysteresis/reopen threshold derived from the main one) and `CChannelEQShell` (confirms its curve-preview data — `GetResponseLUT`/`GetFreqTable`/
      `GetRelXTable` — is the same data driving the actual filter, computed via a sine-table shortcut rather than per-pixel biquad evaluation). Full
      writeup in `docs/logic-reference/mixer.md`.
      **Seventh pass:** `CCompressorDBX::GainRedCurve` decompiled — a *third* distinct implementation strategy (not a closed-form curve like SSL/Opto at
      all): a cubic-Hermite-interpolated lookup table (`CHermiteTableLookUp`), fed by `log(clamp(level,1e-6,1e6)) × 0.05211534 + offset`. Three circuits,
      three genuinely different engineering approaches. `CLimiterFX::Coefficients` turned out to be a thin forwarder into `CLimiter::Coefficients` (not
      yet decompiled) plus one per-channel toggle at parameter index 6.
      **Eighth pass:** `CLimiter::Coefficients` and `CCompressorClassA::Coefficients` decompiled — both clean enum-indexed dispatchers. `CLimiter`
      confirms sample-rate-adaptive lookahead (48 samples <50kHz, 24 <100kHz, 0 ≥100kHz) and reuses the same fast bit-hack `log2`→dB polynomial
      internally, not just for metering; every compressor/limiter decompiled so far shares one `exp(-2300/(time*sampleRate))` time-constant formula.
      `CCompressorClassA` appears to offer discrete ratio presets (a 4-entry constant table, each with its own function pointer) rather than a
      continuous ratio knob — 1176-style fixed-ratio buttons, not a VCA-style continuous control. Full writeup in `docs/logic-reference/mixer.md`. Open:
      the ratio table's actual 4 values/functions, `CCompressorLP`/`FET`'s own curves, and `CChannelEQShell`'s LUT contents; the Match EQ's
      reference-model factory/selection also remains open.
      **Ninth pass:** read the ClassA ratio table directly — `{0.0, 3.0, 7.0, 0.0}`, giving slopes 0/1.5/1.167/0 (doesn't map cleanly onto classic
      labelled ratios; the matching function-pointer table, which is what actually differs per preset, wasn't read). `CChannelEQShell::
      AllocateAndFillStaticTables` decompiled and gives the exact curve-preview frequency axis formula (`freq[i] = 20×10^(3×i/(N-1))`, a plain 20Hz-20kHz
      log axis) — also **corrects** the earlier guess that the curve uses a sine-table shortcut: the real response comes from
      `CBiquadChainDualRateFX::FillResponseLUT`, an actual biquad evaluation per point (`MapChanEQSineTable` is unrelated).
      **Tenth pass:** `CCompressorFET::GainRedCurve` decompiled — a fourth circuit, back to closed-form: same shared knee (0.6151-1.1925) as SSL/Opto,
      single time constant, then `(x+1)^-4` (a much steeper power than Opto's -1.2, matching a FET/1176-style "grabby" onset) rescaled linearly. Four
      compressor circuits now decompiled, four genuinely different nonlinearities on one shared knee shape. Full writeup in `docs/logic-reference/mixer.md`.
      **Eleventh pass:** `CCompressorLP::GainRedCurve` decompiled — a fifth strategy, a 1024-entry linearly-interpolated lookup table (no knee/power-law
      math at all). "LP" = Platinum, Logic's clean/modern digital model, fitting its plain table-driven precision vs. the analog-modeled circuits'
      curve math. **All six stock Compressor circuits now at least partially characterized** (five distinct engineering strategies: SSL/Opto/FET
      closed-form algebra on a shared knee, DBX Hermite-table, LP linear-table, ClassA discrete-ratio architecture with its curve itself still untraced).
      **Twelfth pass:** `CAutoMaster::MatchEQFactory` decompiled — the Mastering Assistant's own dedicated Match EQ instance defaults its fade-extremes
      boundaries to 30Hz/50Hz (low) and 7.5kHz/15kHz (high), leaving rumble and air alone by default. `SelectSpectralReferenceDataForModel` confirms a
      static table of built-in reference spectra selected by index (count/contents/labels not read). Full writeup in `docs/logic-reference/mixer.md`.
      **Thirteenth pass:** `CCompressorClassA::GainRedCurve` decompiled — it's not a standalone fifth closed-form curve after all, it's a *sixth*
      strategy: the same 1024-entry linear-interpolation table mechanism as LP/Platinum, but indexed by `input × 1023 × threshold-offset[ratioIndex]`
      (the 4-entry ratio-preset constant from `Coefficients`) rather than a continuous formula. So ClassA's "4 ratio buttons" UI behavior is implemented
      by re-scaling one shared table, not by routing to 4 different curve functions. **All six stock Compressor circuits are now fully characterized.**
      Also decompiled `CBiquadChain`/`CBiquadChainDualRateFX::FillResponseLUT`: not a full per-point biquad evaluation, a cheaper shared precomputation
      of `cos(ω)/sin(ω)/cos(2ω)/sin(2ω)` at each of the N display frequencies (ω = 2π·f/sampleRate) that every band in the chain then reuses to compute
      its own response cheaply; the DualRate variant calls this twice (for two chain pointers at different sample rates) and concatenates the results,
      confirming what "DualRate" means architecturally. Also **corrected** an eighth-pass guess: `CCompressorClassA::Coefficients`'s second 4-entry
      table (next to the `{0.0,3.0,7.0,0.0}` ratio table) was assumed to hold per-preset function pointers; reading its raw bytes shows it's actually
      a parallel small-integer table `{0,3,7,1}` — far too small to be code addresses, and never dereferenced as code — not a dispatch table at all.
      Full writeup in `docs/logic-reference/mixer.md`. **Fourteenth pass:** `CAutoMaster::SpectralReferenceDataFactory` decompiled — it's not a static
      in-binary table, it loads each model as a regular `.pst` preset file from the app bundle via the same generic mechanism every Logic plugin preset
      uses. Searching the actual app bundle found the real library: `Plug-In Settings/Match EQ/Stereo Match EQ/` ships 50 genre-named target-curve
      presets (Alternative/Blues/Breakbeat/Dance/Disco/Funk/House/Jazz/Orchestral/Pop/RnB/Rock, 2-5 variants each) and `Surround Match EQ/` a separate
      19-preset library — almost certainly what the Mastering Assistant's "model" picker actually selects from, not a small hardcoded set as guessed in
      the twelfth pass. Also found: `CAutoMasterShell::BuildIntelModelMenu` builds a 4-option "Clean/Valve/Punch/Transparent" analog-model menu, three
      options gated behind Apple Silicon (exact UI copy decompiled); `CAutoMaster::SetAnalogModel` confirms the engine is its own class,
      `CAnalogModelingMasteringChainFX`, with "Clean" a true bypass (no engine call at all) and each other model having a native/fallback device-type
      pair selected by Apple-Silicon-ML availability. The circuit simulation itself is a sizeable separate engine, out of scope for this pass. Full
      writeup in `docs/logic-reference/mixer.md`. **Fifteenth pass:** the user asked whether the actual Match EQ preset data had been pulled out, not
      just its existence confirmed - it hadn't, so it was. Fully decoded the `.pst` binary format by cross-checking the decompiled factory function
      against the real files byte-for-byte: fixed 96-byte `GAMETSPP` header, then three length-prefixed chunks (`SMMT`/`SMUC`/`SMOM`), each holding a
      shared log-frequency axis (0, then 20 Hz-22,050 Hz, N=306, every preset) plus one magnitude-in-dB array per channel (2 for stereo, 6 for
      surround). Only `SMMT` ("target") is non-zero in factory presets. Parsed all 70 presets (50 stereo + 20 surround - corrected the stereo-pass
      count, it's 20 not 19) into plain JSON and saved them, plus the original `.pst` files, in `../logic-pro-reference/match-eq-presets/` (outside
      git, same convention as the rest of that folder) - real, reusable target-curve data for jad-daw's own Match EQ, not just an architecture note.
      Also checked `CCompressorClassA::OnReset` for the table-fill step: it only resets small envelope-state fields, not the 1024-entry table itself -
      that's still built elsewhere (constructor or lazily), not found this pass. Remaining opens (`CCompressorClassA`'s actual table-fill step, the
      per-band response-combination math, the exact `.pst` filename table proving which presets `SpectralReferenceDataFactory` reads, and
      `CAnalogModelingMasteringChainFX`'s own engine) all still need either a live instance or a genuinely new, separate reverse-engineering pass.
      **Sixteenth pass:** user said to always extract found presets going forward. The same generic preset container format (confirmed via the Match
      EQ factory) backs every stock plugin's `.pst` files, not just Match EQ's - extracted and parsed all 324 factory presets for Compressor (159),
      Channel EQ (149), Noise Gate (8), Adaptive Limiter (5), and Limiter (3), plus each plugin's `CSParameterOrder.plist` (the real UI parameter
      ID-to-name list). Also pulled each plugin's own `#default.pst`: Match EQ's is empty, but Mastering Assistant's has a real non-zero curve in its
      `SMUC` chunk, refining (not confirming) the earlier "SMUC is always an empty placeholder" guess. Byte-position-to-parameter-ID correspondence
      for the generic presets is a plausible match (spot-checked values look sensible) but not proven the way the Match EQ format was. Full writeup
      in `docs/logic-reference/mixer.md`; raw + parsed data in `../logic-pro-reference/{match-eq-presets,stock-plugin-presets}/`.
      **Seventeenth pass:** chased `CCompressorClassA`'s table-fill step - ruled out both the constructor (only `bzero`s the 1024-entry region to a
      zeroed start state) and `OnReset` (only touches small envelope fields). `Coefficients`'s ratio-preset case does call back into the vtable
      afterward, resolved by reading the real vtable bytes - it's `CCompressorBase::CalcAutoGain` (auto-gain recalculation), not a table builder. The
      real fill logic isn't in any of the three places checked; may live outside `MADSP` entirely (speculative). Not pursued further - this was the
      last readily-reachable lead on that particular open item.
      **Eighteenth pass:** decompiled `CAutoMaster`'s constructor to find the Mastering Assistant's real reference-model filename table - and it
      corrects the fourteenth pass's guess. The constructor literally writes 4 strings - `"AM_Curve_1"` through `"AM_Curve_4"` - into the exact
      11-byte-stride region `SpectralReferenceDataFactory` indexes, proving there are exactly 4 models (not the 50/20-item Match EQ genre library
      guessed earlier). Found the actual files in `MADSP.framework/Versions/A/Resources/AM_Curve_{1,2,3,4}.pst`; all 4 have real, non-zero target
      curves, extracted into `../logic-pro-reference/match-eq-presets/mastering-assistant-models/`. No genre/style label was found for any of them.
      Full correction in `docs/logic-reference/mixer.md`.
      **Nineteenth pass:** found and decompiled the per-band response-combination step that was left open after `FillResponseLUT` - `CBiquadChain::
      FrequencyResponse` (two overloads: one orchestrates multiplying each active band's magnitude response into a running chain total, the other
      computes one band's own `|H(e^-jω)|` from `FillResponseLUT`'s cos/sin table plus 6 coefficients pre-folded once per band). Chain response =
      product of active bands' magnitudes, not a sum; disabled bands are skipped, not multiplied by 1. Each band also caches its last computed
      per-point magnitude array and skips the trig/sqrt math entirely when nothing changed. This was the last fully-tractable open item from the
      Channel EQ curve-preview thread. Full writeup in `docs/logic-reference/mixer.md`.
      **Twentieth pass:** resolved the last big open question - whether `CAnalogModelingMasteringChainFX` (behind the Clean/Valve/Punch/Transparent
      menu) is CoreML inference or classical DSP. Its own constructor signature settles it: it takes 5 `vector<unique_ptr<IMil2BNNSModel>>` arguments
      - BNNS (Apple's Basic Neural Network Subroutines) models compiled from Core ML's MIL, run directly through BNNS rather than the full CoreML
      runtime. 5 separate models are held at once, each with its own FFT-based up/down-sampler pair (CDownSampler/CUpSampler), confirming the models
      run at one fixed internal rate with the engine resampling around them for other project rates. The specific `.mlmodelc` files weren't matched to
      Valve/Punch/Transparent by name (none in `MAMachineLearning.framework` are obviously labeled that way) - network topology/weights are out of
      scope for static analysis regardless. **This closes the main open-item list for this session's Mastering-Assistant/stock-plugin sweep** -
      remaining items (the 4 `AM_Curve_N` models' musical meaning, `CCompressorClassA`'s table-fill step) are minor and already ruled-out-as-quick-wins
      or unlabeled. Full writeup in `docs/logic-reference/mixer.md`.
      **Twenty-first pass:** user asked to go deeper into `MAMachineLearning.framework`'s other models. Catalogued all ~20 remaining model names by
      reading each one's CoreML `metadata.json` directly (no decompilation needed). Found a clean 3-family split: an 11-model "neural audio effect"
      family (`FEO`/`FS`/`FSQ`/`FTO`/`MTC`/`MTM`/`RTRHT`/`RTRST`/`MKBO`/`MKGerm`/`MK2Germ`) with exactly the audio-in/audio-out/conditioning-scalar
      shape `CAnalogModelingMasteringChainFX` should want (WaveNet-style gated-conv, PyTorch-trained) - `MKGerm`/`MK2Germ` ("Germanium") strongly hints
      at vintage analog-circuit modeling; a structurally-related but unconditioned 6-model "analyzer" family (`G`/`G_SO`/`S`/`S_SO`/`T`/`TSO`) whose
      consuming feature wasn't identified; and a small conventional-CNN family (`PT`/`RTPT`/`RTPTM`, no per-rate variants) where `RTPT`'s 360-element
      output matches the well-known CREPE pitch-estimator bin count, hinting at real-time pitch detection (not confirmed by any label). Full catalog
      in the new `docs/logic-reference/machine-learning-models.md`.
      **Twenty-second pass:** user chose to trace a second binary rather than use the live UI to pin down which of the 11 candidate models back
      Valve/Punch/Transparent. Found the missing link in `MADSP.framework`: `CAnalogModelingMasteringChainFX`'s real constructor makes 6 (not 5) calls
      to `MAMachineLearning::ModelFactory::createModel(RealtimeFXModel enum 10-15, sampleRate, Compile)` - exactly matching the 6 `AMMCDeviceType`
      native/fallback values found earlier. Opened `MAMachineLearning.framework` itself: `createModel`'s switch has a jump table Ghidra couldn't
      recover automatically, so the exact enum-to-codename mapping for all 6 cases wasn't pinned down, but the `MKGerm`/`MK2Germ` case's own helper
      function was fully decompiled - exact sample-rate thresholds (44100/48000/88200/96000/176400 Hz) and fallback behavior (unrecognized rates use
      `MK2Germ_96`, the best model, not the nearest rate). Also found two previously-unknown bundled files (`MKBO_88_erdk8f5b7k_0467`, which has zero
      references anywhere in the binary found by string/xref search - later corrected in the twenty-fourth pass: it's a tooling blind spot, not dead
      code - and `MKGerm88_uy2kv6feen_0560`, the actual 176.4kHz fallback). Full writeup in
      `docs/logic-reference/machine-learning-models.md`.
      **Twenty-third pass:** user authorized using macOS automation to open Logic and run a Ghidra GUI session to finish the enum-to-codename mapping.
      GUI automation of Ghidra itself proved unreliable for this specific job (Java/Swing dialog buttons and menu items don't respond to synthetic
      clicks or keystrokes through the available automation layer - only AppleScript's native accessibility `click` action worked, and only on some
      elements). Switched to the right tool instead: `analyzeHeadless -postScript` with a small custom Ghidra script that force-disassembles and
      decompiles arbitrary addresses programmatically, no UI involved. This resolved all 6 switch cases cleanly. Result: `CAnalogModelingMasteringChainFX`
      does **not** use the 11-codename conditioned-audio family guessed in the twenty-first pass at all - it uses all **six** codenames from the other
      ("Family B") model family instead: enum 10/11 = `TSO`/`T` (audio output), 12/13 = `G_SO`/`G` (scalar output), 14/15 = `S_SO`/`S` (scalar output).
      Only one of the three native/fallback pairs produces audio directly; the other two only output a per-block scalar - two of the engine's three
      "circuit" slots are neural control-signal extractors, not audio generators. Which pair is Valve/Punch/Transparent is still an unconfirmed
      inference. Full writeup in `docs/logic-reference/machine-learning-models.md` and `docs/logic-reference/mixer.md`.
      **Twenty-fourth pass:** user asked who actually consumes the 43 not-yet-attributed model codenames from the twenty-first pass. Searched for every
      caller of `ModelFactory::createModel` across `MADSP.x86_64` and found three more consumers beyond `CAnalogModelingMasteringChainFX`. **`CMLSaturation`**
      (Logic's stock neural Saturation/distortion plugin), enum 0-9, resolved via the same headless-scripting technique (`DumpSwitchCases3.java` then
      `DumpHelpers2.java` for its 7 helper functions): `0`=`RTRST`, `1`=`RTRHT`, `2`=`MTC`, `3`=`MTM`, `4`=`FEO`, `5`=`FTO`, `6`=`FS`, `7`=`FSQ`, `8`=`MKBO`,
      `9`=`MKGerm`/`MK2Germ` - this exactly and completely accounts for all 11 "Family A" codenames, overturning the earlier Analog Model guess for good.
      Decompiling `CMLSaturation`'s own `MKBO` case also resolved the `MKBO_88_erdk8f5b7k_0467` "dead code" question from the twenty-second pass: it IS
      referenced, just via an inline SSE-immediate string construction that string/xref search tools don't track as a reference - the code path is
      reachable, the file is just missing from the shipped bundle. **`CPickupModelingFX`/`CPickupModelingShell`** (a standalone plugin, same pattern as
      `CAutoMasterShell`), enum 16-23, 8 models, resolved via `DumpSwitchCases2.java`: `01_02N`/`02M`/`03B`/`01N`/`02N_M`/`03M`/`04M_B`/`05B` - guitar
      pickup-position codes - but none of these 8 files exist anywhere in the current app bundle (confirmed via `find`), so this looks like unreleased
      functionality compiled in ahead of its model assets. **`CAnalogModelingProfiling`**, enum 24-49, 27 models (likely an amp/pedal "profiling"
      capture-your-own-device system) - confirmed to exist and its call count/enum range pinned down, but its codenames weren't decoded this pass, left
      open. Also confirmed **Family C**'s consumer: `WrappedNeuralPitchTrackerRT` (found via `MADSP`'s import list, not `createModel` - a dedicated
      wrapper class bypassing the shared factory entirely), confirming the CREPE-style pitch-bin guess for `PT`/`RTPT`/`RTPTM`. Together this accounts
      for all 43 of the twenty-first pass's not-yet-attributed codenames. Full writeup in `docs/logic-reference/machine-learning-models.md`.
      **Twenty-fifth pass:** decoded `CAnalogModelingProfiling`'s remaining 26 codenames (enum 24-49), the one open item left from the twenty-fourth
      pass, with a generalized version of the same headless-scripting technique (`DumpSwitchCases4.java`, looping the runtime jump-table read over the
      wider range). Ghidra's own string analysis found all 26 names directly this time (no inline-SSE trick to work around) - and they're real,
      human-readable gear names: 18 classic/boutique guitar pedals (Klon Clone, RAT, TS808, Muff, Fuzz Face HF, Fuzz Factory, Boss DS-1, Zendrive, Xotic
      AC/RC, Wampler Taunus, Duke Of Tone, Dover Drive, Belle Epoch, Argonaut, Multi Fuzz, Octavix, Tchula, Wong Compressor) and 8 real amps each captured
      through an SM57 mic (3 Diezel VH4 channels, 2 Fender combos, 2 Two Rock amps, 1 Vox AC4) - unmistakably a "tone capture"/profiling feature, the
      same category as Neural DSP's Quad Cortex or Positive Grid's ToneMatch. Confirmed via `find` (same as Pickup Modeling) that none of these 26 files
      exist in this build - compiled-in but unreleased. This closes out every `RealtimeFXModel` enum value 0-49; nothing about Logic's neural-model
      loading remains unidentified. Full writeup in `docs/logic-reference/machine-learning-models.md`.
- [x] D9. Flex Time DSP internals: the actual engine is `MADSP.framework`'s **`CElasticAudio`**/`CEAPhaseVocoder` family — Apple's own internal name for
      Flex Time is "Elastic Audio". It's a textbook phase vocoder (FFT size, window type, **lamination mode** — Laroche & Dolson's own term for their
      identity-phase-locking scheme, already cited in `flex-algorithms.md`), driven by the same Flex-marker/warp-marker model as the UI, plus a separate
      `CEAGrainTrain` class (an internal building block, not a top-level mode — see below). Confirms jad-daw's own published-algorithm choice (phase
      vocoder + WSOLA split) already matches Apple's real architecture.
      **Mode→engine mapping resolved (third pass, at the owner's request):** `CElasticAudio::CreateElasticAudioObject`'s factory switch is the ground-truth
      list of every engine `eElasticAudioType` can instantiate: `CEATempophon`=Tempophone, `CEAPhaseVocoder`=Polyphonic, `CEAMonophonic`=Monophonic (all
      exact name matches), plus `CSFStretch`/`CEAResampler`/`CSFSlice`/`CSFSliceDice`/`CEAFlexPitch(_PV)` (inferred/unclear mapping to Speed/Slicing/
      Rhythmic/Flex Pitch — see the doc for per-value confidence). Full writeup in `docs/logic-reference/flex-algorithms.md`.
      (`MAMusicAnalysis.framework`'s onset/beat-tracking side was covered separately in the same file: CNN-based onset detection via Metal Performance
      Shaders, HMM+Viterbi beat/downbeat tracking with a joint time-signature-beat model.)

**Scope note (the owner, 2026-10-16, after an initial caution about shipping decompiled/derived code in a public repo):** this repository is private and
not distributed; the owner explicitly wants exact algorithm facts extracted by decompilation and reused (adapted/improved) in jad-daw, not just treated as
reference facts for an independent reimplementation. D6/D7 above are plain numeric formulas/constants either way (no literal code copied). If this repo's
distribution status ever changes (made public, shared, published), this decision should be revisited before any more literally-derived algorithm code ships.

## E. Behaviour gaps found against the real Logic Pro 11.2, not yet fixed in code (the owner, 2026-10-14: "vince Logic, non jad-daw")
Found with a disposable test project and the AppleScript-menu-enumeration + Quartz-coordinate-click method in `docs/logic-reference/behavior-check-2026-10-14.md`
(read that file for the full evidence before touching any of these). Each is a real behaviour/structure mismatch, not a shortcut.
- [x] E1. Automation mode slot: Logic's popup is **Read / Touch / Latch / Write** only, no "Off" entry — check whether jad-daw's Off state should be
      removed/renamed, or whether Logic really does have an Off state reachable some other way before changing anything. Logic's popup also has
      "Record Automation with Audio Regions", "Trim" and "Relative" that jad-daw doesn't have.
      DONE: kept the Off state: Apple's guide lists the key commands "Toggle Current Track Automation Off/Read" and "Set All Tracks to Automation Off", so Off is a real mode (the live popup read simply showed the other four). Still open as a sub-item: Trim (T-Touch / T-Latch, fader centred and acting as an offset), Relative and "Record Automation with Audio Regions".
- [x] E2. Track Stacks: Logic has **one** Track-menu command, "Create Track Stack…" (a dialog, presumably Folder vs. Summing inside it), not jad-daw's
      two separate actions `track.createTrackStack` and a standalone "Create Folder Stack". Merge them into one command + dialog, or confirm via the
      dialog's actual contents (not seen yet) before changing `actions.json`.
      DONE: one Track > Create Track Stack… that opens `CreateTrackStackDialog` (Summing / Folder); the separate Create Folder Stack action is gone.
- [x] E3. Aux tracks: Logic creates an auxiliary channel strip from the **Mixer's Options menu** ("Create New Auxiliary Channel Strip", ⌃N — already in
      `mixer.md`), not from Track > New. Move jad-daw's `track.newAux` (if it exists under the Track menu today) to match, or confirm it's already in
      the Mixer and this is a non-issue.
      DONE: `track.newBus` became `mix.newAux`, "Create New Auxiliary Channel Strip" in the Mix menu (no shortcut: Ctrl+N is New from Template here).
- [x] E4. Menu *locations* (shortcuts are already right, only the menu path is wrong): Preferences is **Logic Pro > Settings** (no ellipsis, it's a
      submenu), not File > Preferences; Key Commands is directly in the **Logic Pro** app menu, not under Help. Fix `actions.json`'s `menu` field for
      `file.preferences` and `help.keyCommands` (and whatever UI chrome renders those menu paths).
      DONE: Preferences is now JAD Daw > Settings > Preferences… and Key Commands sits in the JAD Daw app menu (new first menu in `actions.json`).
- [x] E5. Record > Count-in goes to **1/4 through 9/4** (nine beat counts), jad-daw only offers up to 3/4 — extend the Count-in UI/engine to match.
      DONE: Count-in menu has 1/4 to 9/4 (`countInChoice` -1..-9) beside 1..6 bars.
- [x] E6. Record > Overlapping Audio Recordings is **two separate groups** depending on whether Cycle is on or off (Cycle Off: Create Take Folder /
      Create New Track / Create Track Alternative; Cycle On: Create Take Folder / Create Tracks And Mute / Create Track Alternatives), not jad-daw's
      one flat Take-Folder/Tracks/Tracks-and-Mute/Merge list for both states. "Create Track Alternative(s)" from a cycle recording isn't in jad-daw at
      all today.
      DONE: Cycle On has Take Folder / Create Tracks and Mute / Create Track Alternatives, Cycle Off has Take Folder / Create New Track / Create Track Alternative (`overlapAudio`, `overlapAudioOff`). Cycle Off acts only when the take lies over regions of the track: a take folder joins them (the new one plays), a new track copies the strip, an alternative gets the take on a new empty alternative. Cycle On alternatives: each pass after the first is a new alternative.
- [x] E8. Project Settings is **one window with 11 tabs** (General, Audio, Recording, Smart Tempo, MIDI, Score, Movie, Synchronization, Metronome,
      Tuning, Assets), not the single "name, rate, tempo, count-in, recording" panel the roadmap describes. The General tab alone has three groups
      (Project Type/Opening Project/Automation) that don't map to that description. Needs a per-tab pass before assuming today's single panel covers it.
      DONE: `ProjectSettingsDialog` is the one window with the 11 tabs. Core got `Project::settings` (key -> value, command `set_project_setting`, saved as "settings") for the plain choices; Pan Law has its UI (`ProjectController::panLaw`). Pre-roll works in the engine (StartRecord frame2 = pre-roll frames, capture starts at its end). Choices that are only saved for now (the engine does not use them yet): loadOnlyNeeded, resetRecordEnable, regionAutomationPriority, presetValueForRegions, balancerPanLaw, allowTempoRecording, colorizeTakes, midi*, Smart Tempo mode and Flex & Follow defaults, tuning, Assets copy flags. Score/Movie/Sync tabs say the feature is not offered.
- [x] E9. Flex Mode menu read live (2026-10-16): **Off, Flex Pitch, (sep), Flex Time - Automatic/Monophonic/Slicing/Rhythmic/Polyphonic/Speed (FX)/
      Tempophone (FX)**. jad-daw's README has the same 8 algorithms but as one flat list in a different order (Automatic last instead of first) and
      without the "Flex Time -" grouping or the "(FX)" marking Apple itself puts on Speed and Tempophone. Update the Flex popup's grouping/order/labels
      in `smart-controls-and-editors.md` and the UI to match; the underlying DSP quality question is separate (see `flex-algorithms.md`).
- [x] E10. Bounce dialog is far richer than `README`/roadmap describe: Destination is a **multi-select checklist** (Uncompressed/MP3/M4A: AAC/Burn to
      CD-DVD, can bounce to several at once), File Type is **AIFF/WAVE/CAF only — no FLAC** (jad-daw's own FLAC export is an addition beyond Logic, keep
      it), a **Sample Rate** dropdown (bounce at a different rate than the project), explicit **Start/End bar fields** instead of a binary whole-project/
      cycle choice, a **Mode: Offline/(Realtime?)** dropdown, **Bounce 2nd Cycle Pass**, **Add to Project**, **Add to Music**, and **Normalize as a
      dropdown** not a single flag. See `behavior-check-2026-10-14.md` for the full breakdown before touching the Bounce dialog code.
      DONE: `BounceDialog` now has Destination (Uncompressed on; MP3, M4A and Burn listed but off), File Type WAVE/AIFF (+FLAC), bit depth, Sample Rate (project, 44.1 to 192 kHz, cubic conversion of the finished mix), Mode (Offline), Start/End in bars (`barToBeats`, `beatsToBar` follow signature changes), Normalize Off/Overall, Tail, Bounce 2nd Cycle Pass (the tail folded onto the start), Add to Project (imported on a new track). CAF is not written.
- [x] E10b. Compressed bounce destinations: MP3 (256 kbit/s) and M4A (AAC, 192 kbit/s) through the Windows Media Foundation sink writer (`core/src/compressed_audio.cpp`), several destinations together; 44.1/48 kHz only (other rates are converted first). Burn to CD/DVD stays out. CAF file type is still not written.
- [x] E11. DONE 2026-10-21: the Mac's pass 3 corrected the structure: the palette is **24 hues x 4 shades = 96 swatches** (not 16 x 3). The colours were sampled from the live screenshot (`jad-daw-private/color-tokens/track-color-palette-screenshot.json`), `ui/qml/TrackColors.qml` holds them, a track colour is a name or `#rrggbb`, the arrow resets. Old note: `View > Colors` is a palette (16×3 was a wrong count) for `Track > Assign Track Color…`; jad-daw has no
      colour-picker component for that command yet (searched `ui/qml`, found none) — this is new work, not a correction. Exact hex values need a
      cleaner re-extraction (screenshot pixel-sampling wasn't reliable); the 16×3 structure itself is confirmed.
- [x] E12. DONE 2026-10-21: the table has Channel / Provided by Driver / User / Long / Short for inputs, outputs, pairs and 256 buses, saved with the project (`io.<key>` settings), Reset, used by the Input menu and the Output menu. Old note: `Mix > I/O Labels…` names **inputs, outputs and buses** (table: Channel/Provided by Driver/User-toggle/Long/Short), not just inputs as the
      README says. Output 1-2 is already labelled "Stereo Output" in Logic itself, confirming jad-daw's "Stereo Out" naming direction. Extend jad-daw's
      I/O Labels feature to cover outputs/buses and the driver-name-vs-custom-name toggle.
- [x] E8 (continued, 2026-10-16). Read 6 of Project Settings' 11 tabs: Audio (Pan Law `-3 dB Compensated` — check jad-daw's pan law against this),
      Recording (Count-in **vs. Pre-roll radio choice**, jad-daw only has Count-in), Smart Tempo (rich: Flex & Follow defaults per
      new-recording/imported-file, Export Tempo Resolution, Free Tempo Recording behaviour), MIDI (Send-After-Loading / MIDI Thru, low priority),
      Tuning (microtonal tables, Hermode Tuning, low priority), Metronome (see E13). Score/Movie/Synchronization/Assets still unread.
- [x] E13. **Metronome Settings is fundamentally different from what the README describes — needs the owner's attention before further metronome
      work.** Confirmed `Record > Metronome Settings…` opens the *same* Project Settings > Metronome tab (not a separate dialog). Real structure: Audio
      Click ("**Klopfgeist**", Logic's click-synth's actual name) and MIDI Click sections, each with one Note+Velocity (or Channel+Note+Velocity) row
      per accent level — **Bar, Group, Beat, Division** — plus Tone/Volume/Output. **There is no counting-pattern UI** (no "beats 1 2 3 4 / eighths
      1 & 2 & / sixteenths / grouped" choice, no per-count custom WAV file) anywhere in it. jad-daw's README describes exactly that counting-pattern
      model as already implemented. Either the README is describing an older Logic version, a different app, or a feature that was never checked
      against 11.2 — this is a foundational mismatch in a "done" feature, not a wording nitpick.
      DONE: Metronome Settings is now the Metronome tab of Project Settings (Record > Metronome Settings… opens it) with Logic's structure: Options, Audio Click (Klopfgeist) rows Bar/Group/Beat/Division with note, velocity and on/off (Logic's defaults G5/127, D#5/119, C5/112, C2/60 off; notes named with C3 = 60), Tone, Volume, Output, and the MIDI Click rows. Values live in the project settings (`click.<level>.note|velocity|on`, `click.tone`, `click.volume`) and reach the engine (`ClickKit.levels`, `ClickTrack.level`). The spoken-count WAV slots are gone from the UI (the bridge and engine still accept sample files). Still saved only: the Options flags and the MIDI Click (no MIDI port output yet).
- [x] E14. LCD doc correction: Smart Tempo's project tempo states are **Keep / Adapt / Automatic** (confirmed in Project Settings), not "Keep and Off"
      as `lcd.md` currently says — "Automatic" is the real third state.
      DONE: `lcd.md` now says Keep / Adapt / Automatic.
- [x] E15. DONE 2026-10-21 (Pattern is shown but disabled until a Step Sequencer exists): Create New Track window with the 4 cards, Details and number of tracks (`createTracks`). Old note: Create New Track dialog's full structure confirmed (4 categories x 2-3 sub-options each, a Details panel that changes per category, Number
      of tracks to create) — see `behavior-check-2026-10-14.md` for the complete breakdown and reference screenshots.
- [x] E8 (closed, 2026-10-16). All 11 Project Settings tabs now read at least once. Score (7 sub-tabs of notation engraving, confirms jad-daw's
      view-only Score is the right scope unless real engraving is ever committed to), Movie (confirms "left out for good" is right), Synchronization
      (external-hardware-sync, low priority), Assets (Project Package copy-on-import checkboxes — distinct from jad-daw's unused-audio cleanup, a real
      but minor gap). Full breakdown and screenshots in `behavior-check-2026-10-14.md`.
- [x] E16. DONE 2026-10-21: Arrangement and Chord lanes in the global tracks (Arrangement, Marker, Tempo, Signature, Chord), `Project::arrangement` and `Project::chords`, commands `set_arrangement` / `set_chords`; Session Players read the Chord track first. Old note: **Logic Pro 11.2 has a real Chord global track** (confirmed via Global Tracks add-list: Arrangement, Marker, Tempo, Signature, **Chord**),
      contradicting jad-daw's own README ("no following of a Chord Track (there is none)"). jad-daw's Session Players currently fake a chord source by
      reading marker names; a real Chord global track would be the faithful way to do it. Its own editing UI wasn't opened (didn't want to touch
      Apple's demo content) — needs a follow-up to see how it's populated/edited before building against it. Also: Logic has an **Arrangement** global
      track jad-daw doesn't have at all.
- [x] E7 (partly closed, 2026-10-16 second follow-up). Flex Time on real audio tested at last, using Apple's own bundled Apple Loop
      (`/Library/Audio/Apple Loops/Apple/01 Hip Hop/Safe And Sound Beat.caf` — not the owner's files) imported into a disposable area of a demo project.
      Findings: turning on Flex for a track with content pops a confirmation dialog jad-daw doesn't have ("Do you want to turn on Flex for track X?");
      Logic's own default algorithm is **Flex Time - Slicing** (not Automatic, despite Automatic being listed first in E9's menu); **Flex Mode**
      (Track Inspector, the algorithm picker) and **Flex & Follow** (Region Inspector, a plain On/Off checkbox) are two separate controls that jad-daw's
      README wording conflates into one; transient tick marks are confirmed real and rendering correctly once Flex is on, matching jad-daw's description.
      Not confirmed: the yellow flex-markers-with-handles distinct from the transient ticks (E17), and why an Apple Loop region shows "Follow Tempo &
      Pitch" where a plain recording shows "Flex & Follow" in the same Inspector slot (E18). Smart Tempo analysis quality itself (not just the Flex
      picker) remains untested — still open below.
- [x] E17 (closed, 2026-10-16 third follow-up). Flex markers confirmed by actually selecting the **Flex Tool** (its own tool-popup entry) and
      clicking the region's upper half: one flag-shaped marker appeared with a vertical guide line, exactly matching jad-daw's "one marker in
      the upper half" description. **Correction: the marker rendered blue (the track's own colour), not yellow** — jad-daw's README should
      probably say "the track's colour" rather than a fixed yellow, unless confirmed otherwise on a differently-coloured track.
- [x] E18 (closed). Confirmed structurally with a plain (non-Apple-Loop) import: **Flex & Follow** is a real 4-state popup — **Off / On /
      On + Align Bars / On + Align Bars and Beats** (menu header "Flex Time and Smart Tempo") — for ordinary audio, while an Apple-Loop-tagged
      file shows the simpler **Follow Tempo & Pitch** checkbox in the same Inspector slot. The control's type (popup vs. checkbox) and label
      both depend on the file's Apple Loop metadata.
- [x] Correction to the earlier Flex-default note: **Flex Mode's real default is "Automatic"**, not "Slicing" — the "Slicing" observation was
      specific to that session's track state. Automatic immediately resolves to and displays a specific algorithm, e.g. "Flex Time - Automatic
      (Polyphonic)"; a new **Complex:** checkbox appears in the Track Inspector only while Flex Mode is Automatic.
- [ ] E7. Still open: Smart Tempo *analysis quality* (does jad-daw's beat/tempo detection match Logic's on the same real audio — not yet compared), a
      second Edit-menu read with an actual region/note selected (two attempts to right-click-create a MIDI region to select failed in the first
      follow-up session — likely a coordinate miss; the macOS automation tools available now, see note in `behavior-check-2026-10-14.md`, make this
      much more tractable next time), and the Chord global track's own UI (see E16).

Notes: commit and push after each item; run the full ctest before each push; do not stop at "analysis only" for a feature that has a UI.

## F. Using everything the reverse-engineering and `jad-daw-private` hold (the owner, 2026-10-19: "sfrutta tutta la conoscenza ... non devi saltare niente"; icons from an official repo may be used with small changes; ML models are converted)
Order: things that are exact and testable first, then the ML runtime, then the UI gaps (section E). The CI is left alone.
- [x] F0. Core ML -> ONNX: `tools/ml/mil2onnx.py` (all 319 `.mil` convert, output shapes checked), `espresso2onnx.py` (`RTPT`, `OnsetDetectionCNN`; layout checked against the mlProgram twin RTPTM),
      `coreml_reference.swift` + `compare_with_coreml.py` to prove the numbers on the Mac; `docs/logic-reference/ml-conversion.md`. Converted files in `jad-daw-private/onnx/` (not in git).
- [x] F0b. 91 icons traced to SVG (`tools/trace_icons.py`, vtracer) into `assets/icons/logic-derived/`: raw traces, to be simplified and recoloured before they reach the UI.
- [x] F1. Pan law: the quadratic law (`gainL = 1 - t^2`, `gainR = 1 - (1-t)^2`, `t = pan/200 + 0.5`) and "-3 dB Compensated" (x 4/3) as the Project Settings > Audio default.
- [x] F2. (data, library, menu in the effect window) Factory presets: Match EQ (70 target curves) and Mastering Assistant (4 `AM_Curve` models) as data, Compressor/Channel EQ/Noise Gate/Limiter (324) in the patch library and the plug-in Setting menu.
- [~] F3. (Match EQ insert, Learn, curves and the Mastering macro mappings done; iterative limiter adjustment done: 4 adjustments after the first measurement, tolerance 0.2; the Analog Model remains)
- [x] F4. (Channel EQ 8 bands, Compressor circuits, Gate, Limiter, Adaptive Limiter, limiter meter with the bit-pattern log, the Klopfgeist metronome click; per-sample-rate lookahead does not apply, ours is in ms) Compressor circuit types (Platinum, Classic VCA, Studio VCA, Studio FET, Classic FET, Vintage VCA, Vintage FET, Vintage Opto) as real different curves from `mixer.md`; Limiter look-ahead per sample rate (48/24/0 samples); Noise Gate hysteresis/hold; Channel EQ response curve exactness.
- [~] F5. ONNX runtime in the app (`lpc::ml`, onnxruntime 1.20.1 in `third_party/onnxruntime`, optional `LPC_WITH_ONNX`, models from `$LPC_MODELS_DIR`/`models`): done: OnsetDetectionCNN (Smart Tempo onsets), DownbeatTrackerCondMPS (beat activation, downbeats, meter), RTPTM pitch tracker (Flex Pitch candidates, origin 32.7032 Hz found with sine tones). Neural Saturation insert done (`ml::NeuralStream`: worker thread plus lock-free rings, 256-sample blocks, latency 512, 10 families, `cond` = drive). Shipping done (`lpc_copy_models`: 44 files, 19 MB, copied to `<exe>/models` from `LPC_MODELS_SOURCE`; onnxruntime DLLs via `lpc_copy_onnx_dlls` on `jad-daw` too). Mastering Analog Model done as far as it is known (see `docs/logic-reference/analog-model-chain.md`): Valve/Punch/Transparent run TSO/G_SO/S_SO in the Mastering insert after the compressor, 1024-sample blocks, latency +2048; the input is fed 12 dB lower and made up after (the networks are linear below -30 dBFS and compress above); the real input level, the order inside `ProcessAllDynamicCBs` and a CoreML reference are asked of the Mac (second prompt).
- [ ] F6. Section E gaps E1-E16 (one commit each).
- [x] F7. DONE 2026-10-22 (owner allowed using them with small changes): the ten flat one-colour glyphs (Preferences, Waveform, MIDI, Analysis, Settings, Pattern, I/O Labels, Key Commands, keyboard, Sampler) are cleaned by `tools/clean_traced_icons.py` into `ui/qml/icons/icon-*.svg` (white, rounded numbers) and used by `TrackIcon { glyphFile: ... }` in the Preferences sections, the Create New Track cards, the I/O Labels window and the Key Commands window. The skeuomorphic bitmap-like traces (knobs, faders, meters, switches) stay unused. Old note: Icons: pick the traced SVGs the UI needs (Preferences, MIDI, Waveform, IO labels, Key Commands, mixer knobs/meters), simplify, recolour with the colour tokens, wire into QML.
- [x] F8. DONE 2026-10-22. Causes found from the Actions logs: (1) the macOS core test failure (seed 2, step 31 of the undo property test) was `move_insert` of an insert of the master to another track, whose undo is refused; reproduced exactly on Windows once the random command generator drew its arguments in a fixed order (MSVC and clang evaluate call arguments in different orders, so the two platforms ran different sequences), and fixed in the command. (2) The macOS builds took 2 h 20 min (core) and over 6 h (UI, never finished): `cmake --build --parallel` without a number let make start every compile at once; the workflow now sets `CMAKE_BUILD_PARALLEL_LEVEL: 4` and a 150 minute timeout. Not yet confirmed on a new run: that needs a push.

## G. Graphical windows for the stock effects (the owner, 2026-10-21: "la UI dei plugin stock e' pessima, sono solo parametri da impostare")
Today `EffectEditorWindow.qml` is a list of parameters (plus the Channel EQ curve). Logic has a real graphical window per stock effect.
- [x] G1. Ask the Mac for screenshots of the stock plug-in windows (Channel EQ, Compressor, Limiter, Noise Gate, Match EQ, Delay, Reverb, Neural Saturation, Mastering Assistant, Drum/Synth instruments) at 2x, with the controls at their defaults and one moved.
- [x] G2. Reusable controls from the screenshots and `jad-daw-private/legacy-mixer-graphics`: large knob with value readout, draggable-node EQ display, compressor transfer curve with live input level, gain-reduction meter, level meters, switches and menus in Logic's style, colours from the colour tokens.
- [ ] G3. One window per effect built from those controls (the parameter table stays as the fallback view), then the instruments. Done: Channel EQ, Compressor (7 skins), Limiter, Adaptive Limiter, Noise Gate, Mastering Assistant (Character always selectable, Auto EQ up to 150 %), Match EQ, Reverb, Delay, Neural Saturation. Left: the instruments.
- [ ] G4. What the windows draw but the engine does not do yet: HQ of the Channel EQ is done too (the `hq` parameter: the filters at twice the rate, 15 samples of latency); (Q-Couple and Processing L/R/M/S are done, the latter as the `processing` parameter of the EQ; the Analyzer is done: `SpectrumTap` in the render graph, `spectrumBands`, `editorSpectrum`; the compressor side chain filter is done too); Limiter Mode menu; Gate Monitor and Characteristics (done: `keyMonitor`, `keyMode`); Mastering Custom EQ, Spread and the stereo width meter (the live Momentary/Short-term/Integrated LUFS, True Peak and correlation meters are done: `LiveMeter` fed from the spectrum tap, `editorLoudness()`); per-channel L/R meters (done for the insert meters: `takeInsertChannelMeters`, `editorMeters` has inLDb/inRDb/outLDb/outRDb, the Compressor and Limiter input/output bars use them). (CPU of the neural models is measured: see ml-conversion.md.)

## H. Region automation and what was left (the owner, 2026-10-23: "parti dall'automazione di regione, poi continua con tutto il resto")

- [x] H1. Region automation. `Region::automation` (lanes, ticks counted from the region start whatever its time base), command `set_region_automation`; split, trim (resize) and glue cut and join the lanes
      (`cutAutomation`); a move or a copy takes them along. `playingLanes` / `overlayRegionLanes` (`core/src/automation.cpp`) lay the region lanes over the track's: inside a region with a lane the
      region drives the parameter and the track lane is suspended, with the two Project Settings choices (priority, preset value; exact meaning from Apple's guide). UI: Mix > Region Automation draws the
      lanes inside the regions (`AutomationLane.regionId`), Mix > Convert Automation (four commands, one `convert_automation` undo step per track).
- [x] H2. Record Automation with Audio/MIDI Regions (`Track::recordInRegions`): touch/latch/write takes go into the lanes of the regions under them.
- [x] H3. Track On/Off (`Track::on`, header button, Track > Turn Track On/Off); Only load plug-ins needed: an Off track loads none.
- [x] H4. Settings that were only saved: Reset record enable state (armed instrument tracks kept per project when off), MIDI settings sent after loading (bank, program, volume, pan of External MIDI
      tracks), Allow tempo change recording (a tempo move while recording is a tempo change at the playhead), Trim start of new regions (off by default; needs the tempo analysis). The copy-assets
      flags are used since H6.
- [x] H5. Windows: Limiter Mode (Legacy has the soft knee, Precision the true peak), Mastering Custom EQ (three bands) and Spread (width), Match EQ Learn (Current, Reference), External MIDI window.
- [x] H6. Side chain between tracks (Compressor, Gate), Sampler + Convert Regions to New Sampler Track, Print/Page Setup to PDF, assets as in Logic (referenced files, Consolidate, Missing Audio Files, Audio Recording Path).
- [x] H7. Movie window (`MovieWindow.qml`, Qt Multimedia; file and start in the project settings `moviePath`/`movieStart`; File > Movie > Open, Close, Set Movie Start to Playhead; View > Movie Window).
      The CI installs `qtmultimedia` with the Qt action. The video plays muted, in step with the transport (60 ms timer, a seek when it is more than 180 ms off).
- [x] H8. Live Loops as Logic's grid: `Track::cells` (`LiveCell` with the Cell inspector settings: loop, start, loop start/length, trigger mode, Play From, Quantize Start, Q Loop Start, recording
      mode, Rec-Length, At Rec-End, takes, pattern/Session Player type), `Track::liveRuns` and `liveCells` (transient, `ProjectHost::setLiveRuns`, outside the undo history), command `set_live_cells`.
      `core/src/live_loops.cpp`: `cellFromRegions`, `cellPositionAt`, `ticksToCellEnd`, `playedRegions` (intro, then the loop; the graph builder lays them over the track), `importLiveLoopsGrid`, `suggestLoop`
      (Extract Loops). Bridge `project_controller_liveloops.cpp`: quantized launch with Smart Pickup, Cell End, queue, pause/resume, Track Activation, recording into cells (takes, merge, replace), takes menu,
      Session Player/pattern cells, Add Audio File, copy cell/scene to Tracks, performance recording to regions. UI `LiveLoopsWindow.qml` (tracks as rows, Divider column, Cell inspector). Not done:
      the graphical Cell Editor, control from other devices.
- [x] H9. Dolby Atmos: `builtin.objectpanner` (3D Object Panner: x y z size, Monitoring Format 0-8, downmix and trim parameters; binaural, stereo, 2.0 downmixes with the Hilbert shift for Lt/Rt phase 90,
      or speaker gains published to the graph, which feeds the objects to the output pairs), `core/src/atmos_render.cpp` (distance-based panning over a 7.1.4 room, folds to 7.1/5.1/5.1.2/5.1.4 with the
      guide's downmix algorithms, trims and balances; `renderSpeakers` for bounces), `core/src/atmos.cpp` (objects, `renderAdm`, `projectFromAdm`), `core/src/adm_bwf.cpp` (ADM BWF writer and reader: fmt/data/chna/axml),
      `DolbyAtmosDialog.qml` (Mix > Dolby Atmos…: formats, Downmix & Trim, export, bounce, File > Import > Create Project from ADM BWF…), `FxObjectPannerView.qml`. Not done (not ours to do): Apple renderer,
      head tracking, the Dolby `dbmd` chunk, MP4 exports.
- [ ] H10. Still open: the Mac window sets (macOS only); the -12/+12 dB Analog Model level and the Smart Tempo quality check (need the Mac).
