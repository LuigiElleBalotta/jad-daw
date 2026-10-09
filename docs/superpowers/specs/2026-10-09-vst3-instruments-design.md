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
(note on, note off, and CC 123 all-notes-off); instrument latency in plug-in delay compensation; the instrument slot with a menu
that has a search field, "No Plug-in", the built-in Sine and the VST3 instruments grouped by manufacturer; the same menu for insert
slots (effects only, including the built-in effect); slot gestures (below); the plug-in window opening on insertion.

Out of scope (later specs or never): multi-output instruments, sidechain and audio input of instruments, instruments that are not
VST3, CC / pitch bend / aftertouch and live MIDI input (sub-projects 2 and 3), the MIDI Effect slot, the EQ display, the "Legacy"
folder, the plug-in window header (Undo/Redo, View Editor/Controls, Link, Side Chain), a Settings dialog (so the "open plug-in
window on insertion" behaviour is always on), automation and Smart Controls of plug-in parameters.

## 2. Logic Pro reference

Verified against Apple's Logic Pro User Guide for Mac (pages "Add, remove, move, and copy plug-ins", "Channel strip controls",
"Overview of plug-ins", "Work in the plug-in window", "Search for plug-ins in the Mixer"):

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
- Logic hosts Audio Units only. For an AU instrument the menu path is Instrument slot > AU Instruments > manufacturer > plug-in.
  VST3 is our adaptation: the same shape with VST3 instruments grouped by manufacturer.

To verify against the real Logic Pro before the implementation (the guide has no figure of the menu): exact size and position of the
hover arrows, order and separators of the menu entries, how search results are grouped, the delay before the arrows appear, whether
the manufacturer level is skipped when there is a single manufacturer, and whether All Notes Off on stop also resets tails.

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

All Notes Off: `RenderGraph::allNotesOff` (stop, seek, loop jump, instrument replaced) delivers CC 123 to each instrument track on
the next block: it sets a flag per node that `renderInstrument` turns into one `MidiEvent{0, 0xB0, 123, 0}` at offset 0. No hard
reset: tails and releases keep sounding (to verify, section 2).

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
- Bypass of the instrument slot is not offered (Logic's instrument slot has no bypass button); `ProcessorRef::bypass` stays false.

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

- **Instrument slot** (green, plug-in name; "Sine" for the built-in; empty placeholder for No Plug-in). Empty: click opens the menu.
  Occupied: click on the centre opens the plug-in window (VST3 only; the Sine has none), the hover arrows on the right open the
  menu. The Library keeps opening from the header double click.
- **Menu** (`PluginMenu`, one component for the instrument slot and the insert slots): a search field at the top that filters by name
  within the slot's type, **No Plug-in** (occupied slots only), then the built-in entries (Sine for instruments, Gain for effects),
  then a submenu per manufacturer with its plug-ins of that type. Plug-ins the scanner rejected are not listed (they stay visible in
  the Plug-in Manager). Selecting an entry runs `set_instrument` or an insert replacement/addition and opens the plug-in window
  when the new plug-in has one and is loaded (on load, via the ready listener, if it is not yet).
- **Slot gestures** in `StripSlot`, for the instrument slot and the insert slots: hover arrows on the right, Ctrl-click removes
  (instrument slot: sets No Plug-in), Alt-drag copies an insert to an unused slot (not offered on the instrument slot), the existing
  bypass button on the left (inserts only). The insert slots open their editor with a **single click on the centre**; the double
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
  reach a recording fake instrument with the right offsets, across block boundaries; All Notes Off on stop; PDC with an instrument
  latency; offline render equals live render for the sine (regression of the old path).
- Commands: `set_instrument` with none, with a vst3 id, undo and JSON round trip, rejection of effect ids; `set_instrument_state`;
  `add_track` of an instrument track with no instrument; random command generator extended; old projects load.
- Fake host (`tests/fake_plugin_host.h`) gains `acquireInstrument` and an event-recording instrument; integration test in
  `test_plugin_host_integration.cpp` (instrument state commit does not reload, undo reloads).
- JUCE: a VST3 instrument in `tools/test-plugin` (one sine voice per note, amplitude from velocity, pitch from the note) to prove
  offsets and note-off through a real plug-in; scanner tests (instrument accepted, layout rules, cache version).
- UI: QML tests for the slot (click on empty opens the menu, click on the centre opens the window, arrows, Ctrl-click, search filters,
  No Plug-in) and for the shared menu; bridge tests for `set_instrument` and the window opening on insertion.
- By hand (not automated): a real third-party VST3 instrument, audio continuity when the instrument is moved or replaced during
  playback, the feel of the hover arrows.

## 11. Risks

- A plug-in that crashes while playing takes the app down (same as effects).
- `renderInstrument` changes the sine path: the regression test against the old output is the guard.
- Some instruments need a non-stereo bus layout to load; only the first stereo-capable output is used, others are listed as failed
  with the reason.
- The slot gesture changes touch insert behaviour users already have (double click to open the editor): covered by QML tests.

## 12. Order of work (for the plan)

1. Core: `MidiEvent`, `IInstrument`, `SineInstrument`, `SilentInstrument`, renderer, All Notes Off.
2. Model: optional instrument, validation, `set_instrument` with none, `set_instrument_state`, tests.
3. Host interface, fake host, graph builder, PDC.
4. JUCE: `PluginProcessor` MIDI, `acquireInstrument`, scanner and cache, test instrument plug-in, tests.
5. Controller: instrument slot commands, editor on insertion, state commit for slot `-1`.
6. UI: `StripSlot` gestures, `PluginMenu`, instrument slot, then the insert slots on the same component.
7. README, `THIRD_PARTY.md` unchanged (no code taken from magda-core in this sub-project), deferred list, by-hand pass.
