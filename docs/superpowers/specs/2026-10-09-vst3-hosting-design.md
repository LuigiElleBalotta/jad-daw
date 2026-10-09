# VST3 hosting as insert effects: design

Date: 2026-10-09. Scope: Core interface changes, a JUCE platform adapter, plug-in scanning, the project format, and the UI hooks
(insert slot menu, editor window, Plug-in Manager). Follows UI-B (`2026-10-08-ui-b-panels-design.md`, section 3 note) and the
Core spec's "Plugin host" sub-project (`2026-10-07-core-engine-design.md`).

## 1. Goal and scope

Load real VST3 plug-ins as **insert effects** on tracks and buses, play them live and in offline render, edit them in their own
native window, and save and restore their state with the project.

In scope: Windows; stereo in / stereo out effects; scan with cache and blocklist; rescan from the UI; native editor window;
undoable state; plug-in delay compensation (PDC); missing plug-ins handled without data loss.

Out of scope (each is its own later spec): VST3 instruments and MIDI input to effects, sidechain, automation and Smart Controls
mapping of plug-in parameters, a generic parameter panel, out-of-process hosting (crash isolation during playback), CLAP and AU,
Linux and macOS, bypass per insert, plug-in presets in the Library.

Decisions taken in brainstorming: native editor window (not a generic panel); scan in a child process with cache and blocklist,
plug-ins loaded in-process; JUCE `juce_audio_processors` does the hosting. This mirrors what Logic does with AU (scan and validate at
launch, manual rescan from a manager, plug-ins in-process), adapted to VST3.

## 2. Architecture

```
Core (no JUCE, no VST types)          platform/juce                      ui (Qt Quick)
  IProcessor (+prepare, latency)  <--  PluginProcessor (adapter)
  IPluginHost  ------------------->    JucePluginHost                     PluginBridge (QML-facing)
  PdcPlan (graph_builder)              PluginScanner (parent side)        insert slot menu, Plug-in Manager
  ProcessorRef (+label)                PluginEditorWindow
                                     tools/lpc-plugin-scanner (child exe)
```

Core never includes JUCE. The one new Core abstraction is `IPluginHost`, injected into `ProjectHost`; tests use a fake.

## 3. Core changes

### 3.1 `IProcessor`

Adds two non-real-time virtuals with defaults, so `GainProcessor` and test processors are unchanged:

- `virtual void prepare(double sampleRate, int maxBlock)`: called on the project thread before the processor goes to the audio
  thread. `maxBlock` is `kMaxBlock` (512).
- `virtual int latencySamples() const`: reported latency, read on the project thread when the graph is built. Fixed for the life of
  the instance (a plug-in that changes its latency at run time is rebuilt on the next graph change; no live update in v1).

`process(float* l, float* r, int frames)` stays the real-time entry point.

### 3.2 `IPluginHost`

```cpp
struct PluginDescriptor { std::string id, name, vendor, version, path, native; };  // id = "vst3:<32 hex>", native = opaque host data
struct InsertSlot { Uuid track; int index = 0; };                                   // which insert of which track
class IPluginHost {
public:
    virtual ~IPluginHost() = default;
    virtual std::vector<PluginDescriptor> catalogue() const = 0;
    // Project thread. Never blocks. The live, prepared processor behind `slot`, reused while the insert's id and state are
    // unchanged; nullptr while it is not available (loading, missing, failed). A first call starts loading in the background
    // and `readyListener` fires when the instance exists.
    virtual std::shared_ptr<audio::IProcessor> acquire(const InsertSlot&, const ProcessorRef&, double sampleRate, int maxBlock) = 0;
    // Project thread. Drops every instance not listed (slot and the ref it must match).
    virtual void prune(const std::vector<std::pair<InsertSlot, ProcessorRef>>& live) = 0;
    // UI thread. Serialises the live state (base64) and remembers it as "already in the model", so the command that stores it
    // does not make the next acquire reload the instance. Empty string when there is no live instance.
    virtual std::string captureState(const InsertSlot&) = 0;
    virtual void setReadyListener(std::function<void(const InsertSlot&)>) = 0;  // may be called from any thread
};
```

`makeInsert(ref, host, slot, sampleRate, maxBlock)` (new, `plugin_host.h`) builds an insert: built-ins as before; for ids starting
`vst3:` it asks the host and wraps the result in a `SharedProcessor`. When the host returns `nullptr` (not loaded yet, plug-in
missing, load failure, no host in the build) it returns a `MissingPluginProcessor`: pure pass-through, latency 0, `describe()`
reports `{"missing": true}`. The audible result is the dry signal, the model keeps the reference and its state untouched, and
saving never destroys data. When a background load finishes, `ProjectHost` rebuilds every track config so the real processor
replaces the pass-through.

### 3.3 Model and validation

- `ProcessorRef.processorId` for plug-ins is `vst3:<32 lowercase hex digits>`: the MD5 of name, vendor and JUCE's unique ids, so it
  does not change when the file moves. `state` holds the base64 of the plug-in's saved state, `params` stays empty.
- `ProcessorRef` gains `label` (display name, informational, default empty) so a missing plug-in can still be named in the UI.
  JSON uses the `_WITH_DEFAULT` macro, so existing `.lpc` files load unchanged.
- `checkInsert`: ids starting `vst3:` are valid when the rest is 32 lowercase hex digits, `params` is empty and `state` is valid base64 of at
  most 16 MiB. It does **not** check the catalogue: a project must load on a machine without the plug-in.
- New command `set_insert_state {trackId, index, state}`: undoable, replaces the state of one insert. It is the only way plug-in
  state enters the model (section 5.3).

### 3.4 Plug-in delay compensation

Without PDC a plug-in with look-ahead puts its track out of time. PDC is part of this spec because hosting is the first real source
of latency.

- Latency of a track = sum of its inserts' `latencySamples()` (pass-through while a plug-in is not loaded: 0).
- Compensation is **per edge** (the track's output and each send), because a bus receives signals from several sources. In
  processing order, `in[node]` = the largest `in[source] + latency[source]` over the edges that feed it (0 for a node nothing
  feeds). The delay of an edge is `in[target] - (in[source] + latency[source])`, so every signal reaches each bus, and the master,
  aligned. `PdcPlan::totalLatency` = `in[master] + latency[master]`.
- A delay is a fixed `DelayLine` owned by the track config (preallocated on the project thread, zero frames when the edge is
  already the longest). A delay or latency change rebuilds the configs involved; the in-flight audio of the delay lines is lost
  once, on a graph change.
- Transport position and the playhead are not shifted; audible output is `totalLatency` samples behind, as in any DAW. The offline
  render renders `totalLatency` extra frames and drops the first `totalLatency`, so the file is aligned with the timeline.

## 4. Platform: `platform/juce`

### 4.1 Module changes

`juce_audio_processors` depends on `juce_gui_basics`, so the Core spec's "JUCE headless, no `juce_gui`" decision is relaxed for
`platform/juce` only. Core and the tests that do not use plug-ins are unaffected. JUCE and the VST3 SDK it bundles are used under
GPLv3, which AGPLv3 permits; `THIRD_PARTY.md` gets the entry.

### 4.2 `PluginProcessor`

Adapter from `juce::AudioPluginInstance` to `IProcessor`.

- `prepare`: `setPlayConfigDetails(2, 2, sr, maxBlock)`, `prepareToPlay`, bus layout stereo/stereo (refused layouts fail
  instantiation).
- `process`: wraps `l` and `r` in a `juce::AudioBuffer<float>` over the caller's pointers (no copy, no allocation), an empty
  `MidiBuffer` that is a member, and calls `processBlock`. Blocks larger than the prepared size are split.
- `latencySamples` forwards `getLatencySamples()` read once after `prepareToPlay`.
- Destruction (and therefore `releaseResources`) always happens on the project thread via the existing deferred-release path, never
  on the audio thread.

### 4.3 `JucePluginHost`

- Holds the catalogue (from the scan cache) and one `AudioPluginFormatManager` with the VST3 format.
- `instantiate` loads by id, calls `setStateInformation` from `ref.state` if not empty, then `prepare`.
- **Threading.** VST3 creation and editors must happen on the JUCE message thread, and the project thread must never wait for
  it (the UI thread, which is the message thread, already blocks on the project thread in places such as `openProject`).
  `acquire` therefore never blocks: on a miss it posts the creation to the message thread with `MessageManager::callAsync` and
  returns `nullptr`; when the instance is ready it calls the ready listener, and `ProjectHost` rebuilds the configs. A plug-in
  is pass-through for a moment after a project opens. Where the caller already is the message thread (`lpc-cli render`), creation
  is synchronous, so offline renders have every plug-in loaded. Instances are destroyed on the message thread (the shared
  pointer's deleter hops there). Qt's Windows event loop and the JUCE message window share the thread's Win32 message queue;
  JUCE is initialised with `ScopedJuceInitialiser_GUI` on the Qt main thread. Verified early in the plan (task 2), because it is
  the main technical risk.
- A live-instance registry maps (track id, insert index) to the instance so the editor window and `captureState` act on the
  instance that is actually playing, not on a copy.

### 4.4 Scanning

- **Child process.** `lpc-plugin-scanner <file>` loads one VST3 file with JUCE, checks it can run as 2-in/2-out, prints a JSON
  list of descriptors and exits. The parent starts one child per file with a 30 s timeout. A crash, a timeout or an unsupported
  layout ends in a blocklist entry with the reason.
- **Cache** `plugins.json` in the application config folder (next to `shortcuts.json`): per file `{path, mtime, size, status,
  reason, descriptors[]}`. On startup only files that are new, changed (mtime or size) or previously `ok` but now missing are
  handled. Failed files are not retried automatically.
- **Folders:** `%COMMONPROGRAMFILES%\VST3` and `%LOCALAPPDATA%\Programs\Common\VST3`.
- **Startup:** the scan runs in the background after the window is up; the app never waits for it. The catalogue is usable from
  the cache immediately and grows as files finish; the bridge emits `catalogueChanged`.
- **Rescan:** "Rescan new and changed" (same as startup), "Rescan failed" (retries blocklisted files), and "Rescan all" (clears the
  cache). A rescan never unloads a plug-in already in use by the project.

### 4.5 Editor window

`PluginEditorWindow` is a JUCE `DocumentWindow` hosting `AudioProcessorEditor` for the registry instance. One window per insert,
reused if already open (raised, not duplicated). It closes when its track or insert is removed, when the project closes, and on
undo/redo that rebuilds the insert. If the plug-in has no editor, the window is not offered. Window position is not persisted in v1.

## 5. State and undo

### 5.1 Format

`ProcessorRef.state` is the base64 of the plug-in's opaque state. The model never interprets it.

### 5.2 Loading

Project load builds each insert with `makeEffect`; a missing or failing plug-in becomes `MissingPluginProcessor` and the UI marks
the slot (section 6). Nothing is deleted, nothing is rewritten.

### 5.3 Saving and undo

Plug-in state changes happen inside the plug-in's editor, outside the command system. They enter the model through
`set_insert_state`:

- when an editor window **closes**, if the captured state differs from the model's, one `set_insert_state` command is issued
  (one editing session is one undo step);
- **before saving** the project, any open instance whose state differs is committed the same way, so Save always writes what is
  playing;
- undo and redo of that command change the model's state; `acquire` sees that the ref's state differs from the state the live
  instance was loaded with or last captured, and loads a new instance with the model's state (pass-through until it is ready).
  `captureState` records the captured string as the instance's current state before the command is issued, so the command issued
  from the editor itself does not reload the live instance.

Changes made inside an editor while it stays open are not undoable until it closes. This is stated in the README limits.

## 6. UI

All changes are in the existing insert slot and menus; no new panel layout.

- **Insert slot menu** (Inspector and Mixer strips share one component): after the built-ins, a "Plug-ins" submenu grouped by
  vendor, then "Plug-in Manager...". Choosing a plug-in is `add_insert` / `set_inserts` with `vst3:<id>` and `label = name`.
- **Slot appearance:** shows `label`. A missing plug-in shows the label in the error colour with a "missing" tag; hovering says
  where it was and that audio passes through unchanged.
- **Open editor:** double-click on a plug-in slot (this is also the "open" gesture that UI-B left open for the slot). A slot menu
  entry "Open editor" does the same.
- **Plug-in Manager** (Window or the slot menu): a dialog with a table (name, vendor, format, status ok/failed/missing, path, reason
  for failures) and the buttons Rescan, Rescan failed, Rescan all. A status line shows the scan progress.
- A `PluginBridge` QObject in `ui/bridge` exposes the catalogue (model for the menu and the table), `scanState`, the three rescan
  actions and `openEditor(trackId, index)`. It is the only UI code that knows about the platform host.

`ui/actions/actions.json` gains `view.pluginManager` marked ready.

## 7. Errors

| Situation | Behaviour |
|---|---|
| Plug-in file missing at load | `MissingPluginProcessor`, slot marked, project unchanged |
| Plug-in fails to instantiate | pass-through; not retried for the same id and state; in v1 the slot is marked only when the id is missing from the catalogue |
| Scan child crashes or times out | blocklist entry with reason; visible in the Manager; not retried until "Rescan failed" |
| Plug-in does not accept stereo/stereo, or is an instrument | blocklist entry with the reason ("unsupported layout", "instruments are not supported yet") |
| Corrupt or incompatible state blob | plug-in loads with defaults; log entry; the stored blob is kept until the next commit |
| Plug-in crashes while playing | the app crashes (in-process hosting, out of scope; README says so) |
| `captureState` empty on close | no command, model unchanged |

## 8. Testing

1. **Core, no JUCE:** a `FakePluginHost` (fixed latency, state echo, can fail to instantiate). Tests: missing plug-in is
   pass-through and keeps its ref through a save and load round trip; `vst3:` validation; `set_insert_state` undo and redo;
   `label` default for old JSON; PDC (three tracks with latencies 0, 64, 256 plus a bus: compensation values and null test after
   alignment; offline render alignment); the real-time guard still passes with a fake latency processor on the audio thread.
2. **Golden render tests are unchanged** and plug-in free.
3. **Platform, real VST3:** `tests/plugin/` builds a tiny JUCE VST3 (gain with one parameter, a latency of 32 samples, a state
   blob) behind `LPC_BUILD_PLUGIN_TESTS`. Tests: scan finds it in a temp folder; a broken file in the same folder is blocklisted
   without stopping the scan; instantiate, process and check gain and latency; state round trip; the scanner child timeout.
4. **UI:** QML tests for slot rendering (normal, missing), menu contents from a fake bridge, and the Manager table. The
   screenshot tool gets no plug-in option (it would need a real plug-in); the README screenshot of the Manager uses the fake
   bridge.
5. **By hand on Windows (recorded as not automated):** a real third-party plug-in editor opens, resizes, closes, and the project
   reloads with its state.

## 9. Risks

- Qt and JUCE sharing one Win32 message loop (section 4.3). Spike result (2026-10-09): the JUCE editor window and a Qt window ran together and the Qt timer kept ticking at 10 Hz alongside the JUCE window; dragging the plug-in slider and resizing were not checked in the spike and stay on the by-hand check of the UI task. Prototyped first; fall back is running JUCE on its own message thread
  and marshalling the editor window to it.
- Third-party plug-ins allocate or lock on the audio thread; the real-time guard cannot catch it outside tests (already noted in
  the Core spec).
- PDC across sends and buses is the largest Core change; it is isolated in the graph builder with its own tests.
- `set_insert_state` and instance reuse (section 5.3) are subtle; if the "already applied" flag proves fragile, the fallback is to
  reload the instance on every state command and accept a glitch.
- Build time: the test plug-in pulls JUCE's plug-in client modules; it is behind an option and off in the default build.

## 10. Order of work (for the plan)

1. Prototype: JUCE message loop inside Qt, open one real plug-in editor. Gate for the rest.
2. Core: `IProcessor` additions, `IPluginHost`, `MissingPluginProcessor`, `ProcessorRef.label`, validation, `set_insert_state`,
   PDC, all with the fake host.
3. Platform: `PluginProcessor`, `JucePluginHost`, registry, state capture.
4. Scanner: child exe, cache, blocklist, rescan.
5. UI: bridge, slot menu, editor open, Manager dialog, README and `THIRD_PARTY.md`.
6. Then the UI-B deferred items (`docs/superpowers/ui-b-deferred.md`), as a separate spec and plan.
