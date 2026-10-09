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
- [ ] Input channel per track (the track's Input slot: Input 1, Input 2, 1+2 stereo), mono or stereo take.
- [ ] Input monitoring (the I button): the input is heard through the track's strip while armed; Auto Input Monitoring.
- [ ] Latency compensation: recorded takes are shifted by the device's input latency so they line up.
- [ ] Several armed tracks record at once (one take per track).
- [ ] Punch in/out (Quick Punch-In, punch locators), recording only inside the range.
- [ ] Takes: recording over a region while cycling makes takes in a folder; comping is a later step.
- [ ] Input level meter on the armed track.

## 3. Built-in effects for mixing
- [ ] Channel EQ (low/high shelf + 3 parametric bands), with a curve display in the strip's EQ box.
- [ ] Compressor (threshold, ratio, attack, release, make-up, gain-reduction meter).
- [ ] Reverb (algorithmic, a few parameters) and Delay (time, feedback, mix), Limiter, Gate.
- [ ] They appear in the Audio FX slot menu, have their own small editor windows, are saved in the project, bounce correctly.

## 4. MIDI
- [ ] MIDI input device selection, MIDI recording into a region (notes, with the count-in), step input keyboard.
- [ ] A basic polyphonic synth with envelope as the default instrument (the sine stays as "Sine").
- [ ] VST3 instruments (hosting) and MIDI to plug-ins.

## 5. Audio editing
- [ ] Fades (in, out) and crossfades on regions, region gain handles.
- [ ] Waveform zoom, Audio Track Editor (region waveform in the Editors area), Audio File Editor basics (trim, normalize, reverse).
- [ ] Time stretch / pitch shift for regions (offline), Strip Silence, region mute/loop.

## 6. Mixer and automation
- [ ] Groups (Group Settings window, Groups Active), I/O labels, pre-fader metering, track stacks (summing).
- [ ] Automation modes (Read, Touch, Latch, Write), more targets (sends, plug-in parameters), curves.

## 7. Export
- [ ] Bounce dialog: format (WAV 16/24/32, AIFF, FLAC, MP3), normalize, range (cycle / whole), include plug-ins and the effects above.

## 8. Windows and dialogs of the menus (all of them)
- [ ] Project Settings (Audio, Metronome, Recording, Sync, General), Preferences (General, Audio, Display, MIDI, Advanced).
- [ ] Undo History, Customize Control Bar and Display, Customize Toolbar, Key Commands editor, Colors, Quick Help.
- [ ] Loop Browser, Browsers (All Files, Project, Media), List Editors (Event, Marker, Tempo, Signature), Note Pad, Project Audio.
- [ ] Score editor, Step Sequencer, Session Players, MIDI Transform, Group Settings, I/O Labels/Assignments, Automation Settings.
- [ ] Every remaining `stub` in `ui/actions/actions.json` becomes real or gets a window that is honest about what it does.

## 9. Platform
- [ ] macOS build run for real (never done), the CI Configure failure (only when the owner says), signed bundle, app icon.

## Log
- 2026-10-10: roadmap written. Done before it: metronome with counting modes and user samples, audio recording with count-in,
  automation (volume/pan), global tracks, markers, cycle area, Save As/Import/Bounce, MP3/FLAC/AIFF import and drop-to-new-track.
