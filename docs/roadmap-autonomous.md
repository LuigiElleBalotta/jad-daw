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
- [ ] A4. Flex Pitch: a quality pass on a spoken or sung recording, hotspot values checked against the numbers, notes edited and rendered, audio compared before/after in a test.
- [ ] A5. Smart Tempo: time signatures other than 4/4 (3/4, 6/8) in the beat/downbeat model and in Flex & Follow; tempo changes inside a recording (a tempo map from the beats, "Apply Region Tempo" writing tempo events along the region, not one tempo).
- [ ] A6. External MIDI track: a MIDI output in the platform layer (JUCE), a track field with device and channel, the engine sends its MIDI there; New External MIDI Track in the menu; tests with a fake output.
- [ ] A7. The small stubs that can be real: Play/Stop button options, Open Marker Text, Deselect Global Tracks, Show Track Alternatives (takes), SMPTE-locked regions (absolute-time regions). Movie, printing, Dolby Atmos stay out for good.
- [ ] A8. A full pass: every new window opened once in the app (Flex editor, Smart Tempo beats, Mastering, Drum Replacement, Session Players, Stacks), screenshots checked, defects fixed.

### B. The DAW's ears (an MCP server so an AI can assist with mixing, recording and mastering)
- [ ] B1. Core audio analysis library: per-track and master loudness (M/S/I, LRA), true peak, RMS, crest factor, third-octave spectrum, stereo width and correlation, clipping and DC, silence, tempo, spectral centroid, masking between two tracks (band overlap). JSON output. Tests on synthetic and real audio.
- [ ] B2. `lpc-cli` commands: `analyze <project|wav> [--track id] [--range a:b]`, `snapshot` (project state as JSON: tracks, inserts with parameters, sends, automation, regions, tempo), `apply` (JSON commands, undoable), `render --solo track`, so a headless session works on a project file.
- [ ] B3. MCP server (stdio, Python) over lpc-cli: tools list_tracks, get_state, analyze_mix, analyze_track, compare_tracks (masking), set_fader/pan/mute/solo, add_insert/set_insert_param (EQ, compressor, limiter, reverb, mastering), run_mastering_analysis, render_excerpt, undo/redo, save. Safe by default: works on a copy unless told otherwise; every change is one undo step; a log of what the AI did.
- [ ] B4. Live link: the running app exposes the same commands and meters over a loopback socket with a token, so the MCP server can drive and listen to the open project (playhead, live meters, record arm), not only files.
- [ ] B5. Recording assistant tools: input level check (peak, noise floor, clipping risk), gain staging suggestion, latency check, take comparison.
- [ ] B6. A guide for the AI (docs/ai-assistant.md): what each tool measures, target values (LUFS per platform, headroom, balance curves), a mixing and mastering checklist, example sessions; verified end to end on a real mp3 project.

Notes: commit and push after each item; run the full ctest before each push; do not stop at "analysis only" for a feature that has a UI.
