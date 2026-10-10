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
- [ ] Score editor, Step Sequencer, Session Players, MIDI Transform, Group Settings, I/O Labels/Assignments, Automation Settings.
- [ ] Every remaining `stub` in `ui/actions/actions.json` becomes real or gets a window that is honest about what it does.

## 9. Platform
- [ ] macOS build run for real (never done), the CI Configure failure (only when the owner says), signed bundle, app icon.

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
