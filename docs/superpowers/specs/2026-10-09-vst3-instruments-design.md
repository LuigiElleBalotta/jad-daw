# VST3 instruments and the Logic plug-in slot: design

Date: 2026-10-09. Sub-project 1 of 4 on the way to Logic Pro's software-instrument workflow. Scope: an instrument interface in
the Core, MIDI note delivery from regions to a hosted plug-in, the JUCE adapter, scanning of instruments, and the Logic-style
plug-in slot and menu (instrument slot and insert slots). Follows `2026-10-09-vst3-hosting-design.md` (effects), which listed
instruments and MIDI as out of scope.

The four sub-projects, each its own spec, plan and implementation, in this order:

1. **This one:** VST3 instruments, notes from regions, the Logic plug-in slot and menu.
2. MIDI model with CC, pitch bend and aftertouch in regions, and live input (MIDI device and Musical Typing) on the selected track.
3. MIDI recording on the record-enabled track (count-in, cycle, overdub).
4. Editors: Piano Roll velocity and controller lanes, Event List.

## 1. Goal and scope

An instrument track can hold a VST3 instrument instead of the built-in sine. The notes of its MIDI regions reach the plug-in with
sample-accurate offsets inside the audio block. Its audio goes through the insert effects, fader, pan and sends as today. The
instrument's state is saved with the project and restored when it opens. A missing plug-in leaves the track silent without data
loss. The slot and its menu behave like Logic Pro's, and the insert slots get the same gestures because they share the component.

In scope: Windows; VST3 instruments with a stereo output as the first output bus (no audio input needed); MIDI notes from regions
(note on and note off, plus controller resets on stop as Logic sends them); instrument latency in plug-in delay compensation; the instrument slot with the
Logic menu (section 8: search field, current plug-in, "No Plug-in", Recent, the built-in Sine, VST3 instruments by manufacturer);
bypass of the instrument; the same menu shape for insert slots (effects only, including the built-in effect); slot gestures
(below); the plug-in window opening on insertion.

Out of scope (later specs or never): multi-output instruments, sidechain and audio input of instruments, instruments that are not
VST3, CC / pitch bend / aftertouch and live MIDI input (sub-projects 2 and 3), the MIDI Effect slot, the EQ display, the "Legacy"
folder, the plug-in window header (Undo/Redo, View Editor/Controls, Link, Side Chain), a Settings dialog (so the "open plug-in
window on insertion" behaviour is always on), automation and Smart Controls of plug-in parameters.

## 2. Logic Pro reference

Two sources. **(A)** Apple's Logic Pro User Guide for Mac (pages "Add, remove, move, and copy plug-ins", "Channel strip controls",
"Overview of plug-ins", "Work in the plug-in window", "Search for plug-ins in the Mixer"). **(B)** Logic Pro 11.2 on a Mac, driven
by hand on 2026-10-09 in a throw-away project (items marked *seen*). Menu and slot facts below come from both.

- Channel strip order, top to bottom: Setting, Gain Reduction meter, EQ display, MIDI Effect slot, Input/Instrument slot, Audio
  Effect slots, Send slots, Output, Group, Automation Mode. The Instrument slot is green and shows the plug-in name, effect slots
  are blue, empty slots are a dark grey placeholder. The last empty effect slot is shown at half height.
- Click an **empty** slot: a pop-up menu of plug-ins of the slot's type (instrument slot: instruments only; audio slots: effects
  only). The menu has a search field at the top; typing filters the list to that type.
- Click the **centre** of an occupied slot: opens the plug-in window. Hover shows **arrows on the right**: they open the menu to
  replace the plug-in. **No Plug-in** is the first entry and removes it. A **Bypass button** shows on the left on hover.
- Command-click on a slot: removes the plug-in (eraser pointer). Option-drag copies a plug-in to an unused slot. Plain drag moves it
  within the strip or to another strip.
- The plug-in window opens automatically when a plug-in is inserted (preference "Open plug-in window on insertion", on by default).
- Logic hosts Audio Units only. VST3 is our adaptation: wherever Logic says "AU" we say "VST3".

Seen on Logic Pro 11.2 (instrument slot):

- **Occupied slot, hover:** the slot shows three small controls: a **power (bypass) icon on the left**, a second icon in the middle
  (purpose not identified), **up/down arrows on the right**, and a tooltip with the plug-in's full name below the slot. So the
  instrument slot has a bypass button (an earlier version of this spec said it had none).
- **Empty slot:** a dark grey placeholder with the word "Instrument". Hover shows the right-hand arrows only. Click opens the menu.
  An empty effect slot shows the tooltip "Click to insert Audio effect plug-in".
- **Menu of an occupied slot (top to bottom):** a Search field; the current plug-in with a submenu; a separator; **No Plug-in**; a
  separator; a grey "Recent" header and the last 5 plug-ins used (each with a submenu arrow); a separator; Logic's own instrument
  categories (Drums, Sampler, Studio Instruments, Synthesizer, Utility, Vintage Keys) each with a submenu; a separator; the leaf
  entry "Drum Machine Designer"; a separator; **AU Generators**, **AU Instruments**, **AU MIDI-controlled Effects**, each with a
  submenu.
- **Menu of an empty slot:** the same without the current plug-in and without No Plug-in (Search, Recent, categories, leaf, AU groups).
- **AU Instruments path:** AU Instruments > manufacturer (Apple, Mixed In Key, Steven Slate, Waves seen; every manufacturer has a
  submenu even with several plug-ins) > plug-in (each has a submenu) > channel format ("Stereo", "Multi-Output (16xStereo)", ...).
- **Search:** typing filters to a flat list of matching plug-ins (no manufacturer grouping, no headers), each still with the format
  submenu; the field shows a clear (x) button. The menu opens just under the slot, left edge about at the slot's left edge.
- **No Plug-in:** the track stays an instrument track; the slot becomes the empty placeholder; the Library switches to its category
  browser. So an instrument track without an instrument is a valid state.
- The menu closes without a choice when the pointer clicks outside it.
- **Format submenu:** it is shown for every plug-in, also for single-format ones (Recent and the manufacturer lists both show an arrow
  on every row). Seen contents: instruments "Stereo" / "Multi-Output (16xStereo)"; an effect "Mono" / "Mono->Stereo" (and Stereo for
  stereo effects). Our hosted plug-ins are stereo only, so the submenu has one entry, "Stereo".

Seen on Logic Pro 11.2 (Audio Effect slot, empty, clicked):

- **Menu (top to bottom):** Search field; a grey "Recent" header with 5 plug-ins (AmpliTube 5, Tuner, Gain, Pitch Shifter, AmpliTube
  4), each with a format submenu; a separator; Logic's effect categories (Amps and Pedals, Delay, Distortion, Dynamics, EQ, Filter,
  Imaging, Metering, Modulation, Multi Effects, Pitch, Reverb, Specialized, Utility), each with a submenu; a separator; **Audio Units**
  with a submenu. The group is called "Audio Units" here (instrument menu: "AU Instruments").
- **Audio Units path:** Audio Units > manufacturer (Apple, Dear Reality, FabFilter, IK Multimedia, iZotope, Mixed In Key, PositiveGrid,
  Sonosaurus, Soundtoys, Waves seen) > plug-in (AUBandpass, AUDelay, ... each with the format submenu) > format.
- **Empty effect slot:** a dark grey placeholder labelled "Audio FX"; on the output strip it is labelled "Audio FX" and a second
  slot offers "Click to add Mastering Assistant" (not for us).
- Adding a track: the "+" button above the track list opens a "Create New Track" sheet (MIDI > Software Instrument preselected, with
  an Instrument popup, Audio Output popup, "Open Library", and "Number of tracks to create"). Not part of this feature.

Not verified (to check against the real Logic Pro before the UI is built): the middle hover icon, the exact pixel size of the hover
controls, the delay before they appear, and what Option-click shows beyond "Legacy".

Verified in Logic's Settings > MIDI > Reset Messages (see `docs/logic-reference/settings-and-app-menu.md`): on stop Logic sends software
instruments Control 64 (sustain) off, Control 4, 2 and 1 to zero, aftertouch to zero and pitch bend to centre, and does **not** send Control 123
(All Notes Off) by default. Settings > Audio > General also has Plug-in Latency Compensation (All) and "Playback pre-roll".

On Windows, Command maps to Ctrl and Option to Alt. Icons are our own SVGs.

## 3. Architecture

```
Core (no JUCE, no VST types)            platform/juce                      ui (Qt Quick)
  IInstrument (+MidiEvent)         <--  PluginProcessor (+MidiBuffer)
  IPluginHost (+acquireInstrument) -->  JucePluginHost                     StripSlot (arrows, click, Ctrl-click)
  SineInstrument, SilentInstrument      PluginScanner (instruments listed)  PluginMenu (search, vendor groups)
  RenderGraph::renderInstrument         PluginEditorWindow                  ProjectController (instrument slot)
```

## 4. Core changes

### 4.1 `IInstrument` and `MidiEvent`

```
struct MidiEvent { int offset; std::uint8_t status, data1, data2; };   // offset in frames from the block start
class IInstrument {
public:
    virtual ~IInstrument() = default;
    virtual void prepare(double sampleRate, int maxBlock) {}            // project thread
    virtual int latencySamples() const { return 0; }                    // project thread
    // Audio thread. Overwrites l and r with `frames` frames. Events are sorted by offset, all within the block.
    virtual void render(float* l, float* r, int frames, const MidiEvent* events, int count) noexcept = 0;
    virtual nlohmann::json describe() const = 0;                        // tests
};
```

The raw MIDI form (`status`, `data1`, `data2`) is what sub-project 2 needs for CC and pitch bend, so the interface does not change
again. `kMaxBlockEvents` (256) stays the per-block cap.

- `SineSynth` becomes `SineInstrument : IInstrument`: it applies the events at their offsets and renders the stretches in between.
  `TrackNode::synth` disappears.
- `SilentInstrument` fills silence and ignores events (no plug-in, missing, failed, still loading). Its describe is `{"missing": true}`
  when it stands in for a plug-in and `{"none": true}` when the slot is empty.
- `SharedInstrument` wraps a `std::shared_ptr<IInstrument>` that outlives the config, like `SharedProcessor`.
- `TrackConfig` gains `std::unique_ptr<IInstrument> instrument` (null for non-instrument tracks).

### 4.2 Renderer

`renderInstrument` collects note on/off from the regions into `MidiEvent`s (status 0x90 / 0x80) exactly as it does now, sorts them
and makes **one** call `instrument->render(l, r, n, events, count)`. The per-segment rendering moves into `SineInstrument`.

Stop, seek and loop jump: `RenderGraph::allNotesOff` (existing name kept) flags each instrument node, and `renderInstrument` emits on the next
block, at offset 0, what Logic sends (section 2): for notes still held a note-off each, then CC 64 = 0, CC 4 = 0, CC 2 = 0, CC 1 = 0, channel
aftertouch 0 and pitch bend centre (`0xE0 0x00 0x40`). It does not send CC 123 and does not reset the plug-in: tails and releases keep
sounding. When the instrument is replaced or bypassed the old instance is simply dropped (nothing to send); for a bypassed instrument that
stays loaded the same stop messages are sent once when bypass is turned on.

### 4.3 `IPluginHost`

```
inline constexpr int kInstrumentSlot = -1;                 // InsertSlot{track, kInstrumentSlot}
virtual std::shared_ptr<audio::IInstrument> acquireInstrument(const InsertSlot& slot, const ProcessorRef& ref,
                                                              double sampleRate, int maxBlock) = 0;
```

Same contract as `acquire`: never blocks, null while loading, missing or failed, ready listener on load. The instrument uses the slot
index `-1`, so the live-instance registry, `setWanted`, `prune`, `captureState`, `openEditor` and the ready listener work unchanged.
`catalogue()` entries carry `instrument` (bool). `makeInsert` is untouched; a new `makeInstrument(ref, host, slot, sr, maxBlock)`
returns `SineInstrument` for `builtin.sine`, the host's instance (as `SharedInstrument`) for a `vst3:` id, `SilentInstrument` while the
host has none, and nullptr for any other id.

### 4.4 Graph builder and PDC

`buildPlan` builds `TrackConfig::instrument` through `makeInstrument` for instrument tracks and counts the instrument's latency
(clamped like the insert latencies) in `trackLatency`. The instrument comes first in the track's chain: instrument latency plus the
insert latencies. The same-track rebuild test (`a.instrument != b.instrument`) stays.

## 5. Model and validation

- `Track.instrument` is already `std::optional<ProcessorRef>` with state, label and bypass; JSON already writes `null` for none. No
  format change. Old projects load unchanged.
- **An instrument track may have no instrument** ("No Plug-in" in Logic). The rule "exactly instrument tracks have one" becomes
  "only instrument tracks have one" in `validation.cpp` and `commands.cpp` (add track). `SetInstrumentCmd` takes
  `std::optional<ProcessorRef>`; `makeSetInstrument(trackId, std::optional<ProcessorRef>)` replaces the current signature (an implicit
  conversion keeps existing call sites and the patch library). Undo restores the previous value, empty or not.
- `checkInstrument(ref)`: `builtin.sine` with no state, or a `vst3:` id with the same rules as `checkInsert` for a plug-in (no params,
  base64 state of at most 16 MiB, label length). `isKnownInstrument` stays for built-ins; `isVst3Id` is accepted additionally.
- New command `set_instrument_state(trackId, state)`, mirroring `set_insert_state`: only instrument tracks with a `vst3:` instrument,
  state validated like an insert's, undo restores the previous state. Its JSON round-trips like the others.
- The instrument slot has a bypass (power icon on hover, seen in Logic): `ProcessorRef::bypass` is used for the instrument too.
  A bypassed instrument renders silence and ignores events (the instance stays loaded and keeps its latency, as a bypassed insert
  does); turning bypass on sends the stop messages first. A command `set_instrument_bypass(trackId, on)` mirrors the insert's bypass
  command, with undo.

## 6. Platform: `platform/juce`

- `PluginProcessor::process` gains a `juce::MidiBuffer` form: the audio buffer has `max(inputs, outputs)` channels, is zeroed, the
  events are added with their offsets (`MidiMessage` built from status/data) and `processBlock` is called once per block (blocks
  larger than the prepared size are split, events distributed by offset). The first output bus's two channels are copied out. A plug-in
  that outputs one channel is copied to both.
- `JucePluginHost::acquireInstrument` follows `acquire` (background load, ready listener, reuse while id and state are unchanged,
  take-over of a moved slot). Instrument slots are keyed `track:-1`. `captureState` and the editor window work as for inserts.
- **Scanner:** instruments are no longer rejected. The accepted layout for an instrument is a stereo (or mono, upmixed) first output
  bus and any input layout with no audio input required; effects keep the stereo/stereo rule. The descriptor carries `instrument`.
  Catalogue cache version increases so existing caches are rescanned. The blocklist reason "instruments are not supported yet"
  disappears.
- **Plug-in Manager** lists instruments with a "Type" of Instrument or Effect.

## 7. State, saving and undo

Same mechanism as inserts: `captureState(slot{track, -1})` when the editor closes, a `set_instrument_state` command only when the
state differs from the model, and the host remembers the captured state so the following `acquireInstrument` does not reload.
Undo of the command puts the old state in the model and the instance is reloaded with it. Opening a project loads the instance in
the background; until it is ready the track is silent. A plug-in id missing from the catalogue shows the slot as missing (red) and
the state is kept in the project.

## 8. UI

- **Instrument slot** (green, plug-in name; "Sine" for the built-in; the grey "Instrument" placeholder for No Plug-in). Empty:
  click opens the menu. Occupied: click on the centre opens the plug-in window (VST3 only; the Sine has none); the hover controls are
  the bypass on the left, a spacer for the unidentified middle icon (not drawn), and the arrows on the right, which open the menu. A
  tooltip with the full plug-in name follows the pointer. The Library keeps opening from the header double click.
- **Menu** (`PluginMenu`, one component for the instrument slot and the insert slots), top to bottom: Search field; the current
  plug-in (occupied slots only) with its format submenu; **No Plug-in** (occupied slots only); **Recent** (grey header, the last 5
  plug-ins used for this slot type, kept in the application settings, each with the format submenu); the built-in leaf entry (Sine for
  instruments, Gain for effects); a group **VST3 Instruments** (effects: **VST3 Effects**, replacing Logic's "Audio Units") >
  manufacturer > plug-in > format. Our only format is "Stereo". The effect menu has no category groups of ours either (Logic draws 14
  of its own between Recent and the plug-in group); we draw none. Logic's own category groups and its "AU Generators" / "AU MIDI-controlled Effects" groups have no counterpart and
  are not drawn. Search filters to a flat list of plug-ins of the slot's type with the format submenu and a clear button. Plug-ins the
  scanner rejected are not listed (they stay visible in the Plug-in Manager). Choosing an entry runs `set_instrument` or the insert
  replacement/addition, adds it to Recent, and opens the plug-in window when the new plug-in has one and is loaded (on load, via the
  ready listener, if it is not yet).
- **Slot gestures** in `StripSlot`, for the instrument slot and the insert slots: hover arrows on the right, Ctrl-click removes
  (instrument slot: sets No Plug-in), Alt-drag copies an insert to an unused slot (not offered on the instrument slot), the bypass
  button on the left (instrument and inserts). The insert slots open their editor with a **single click on the centre**; the double
  click handler goes. The "+" add-insert slot is replaced by Logic's empty slot behaviour: an empty slot at the end of the inserts that
  opens the menu on click (shown at half height).
- The menu popup and the arrows use our own SVG icons and the existing theme tokens; their size and placement follow section 2.

## 9. Errors

| Situation | Result |
|---|---|
| `vst3:` instrument id not in the catalogue | slot red, track silent, state kept |
| plug-in fails to instantiate | silent track, logged, slot not red (as for insert effects) |
| plug-in has no editor | message "This plug-in has no editor" when its window is requested |
| more than 256 events in one block | extra events dropped, as today |
| effect plug-in chosen from the instrument menu | impossible: the menu lists instruments only; `set_instrument` rejects it with `bad_value` |
| `set_instrument` on a non-instrument track | `invalid_kind` |
| `captureState` returns nullopt on close | no command, model unchanged |

## 10. Testing

- Core: `SineInstrument` and `SilentInstrument` unit tests (events at offsets, silence); renderer test that notes at known frames
  reach a recording fake instrument with the right offsets, across block boundaries; stop messages on stop (note-offs for held notes, CC resets, pitch bend centre); PDC with an instrument
  latency; offline render equals live render for the sine (regression of the old path).
- Commands: `set_instrument` with none, with a vst3 id, undo and JSON round trip, rejection of effect ids; `set_instrument_state`;
  `set_instrument_bypass` (silence, stop messages on bypass, undo);
  `add_track` of an instrument track with no instrument; random command generator extended; old projects load.
- Fake host (`tests/fake_plugin_host.h`) gains `acquireInstrument` and an event-recording instrument; integration test in
  `test_plugin_host_integration.cpp` (instrument state commit does not reload, undo reloads).
- JUCE: a VST3 instrument in `tools/test-plugin` (one sine voice per note, amplitude from velocity, pitch from the note) to prove
  offsets and note-off through a real plug-in; scanner tests (instrument accepted, layout rules, cache version).
- UI: QML tests for the slot (click on empty opens the menu, click on the centre opens the window, arrows, Ctrl-click, bypass, search
  filters, No Plug-in, the "Instrument" placeholder) and for the shared menu (order of entries, Recent keeps 5 and moves a re-used
  plug-in to the top, format submenu); bridge tests for `set_instrument` and the window opening on insertion.
- By hand (not automated): a real third-party VST3 instrument, audio continuity when the instrument is moved or replaced during
  playback, the feel of the hover arrows.

## 11. Risks

- A plug-in that crashes while playing takes the app down (same as effects).
- `renderInstrument` changes the sine path: the regression test against the old output is the guard.
- Some instruments need a non-stereo bus layout to load; only the first stereo-capable output is used, others are listed as failed
  with the reason.
- The slot gesture changes touch insert behaviour users already have (double click to open the editor): covered by QML tests.

## 12. Order of work (for the plan)

1. Core: `MidiEvent`, `IInstrument`, `SineInstrument`, `SilentInstrument`, renderer, stop messages.
2. Model: optional instrument, validation, `set_instrument` with none, `set_instrument_state`, `set_instrument_bypass`, tests.
3. Host interface, fake host, graph builder, PDC.
4. JUCE: `PluginProcessor` MIDI, `acquireInstrument`, scanner and cache, test instrument plug-in, tests.
5. Controller: instrument slot commands, editor on insertion, state commit for slot `-1`.
6. UI: `StripSlot` gestures, `PluginMenu`, instrument slot, then the insert slots on the same component.
7. README, `THIRD_PARTY.md` unchanged (no code taken from magda-core in this sub-project), deferred list, by-hand pass.
