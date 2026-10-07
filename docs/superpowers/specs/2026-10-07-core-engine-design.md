# Core Engine: Design Spec

Date: 2026-10-07
Status: draft, pending review
Scope: sub-project 1 of 6 (Core). Working name of the product is undecided; `.lpc` below is a placeholder project extension.

## 1. Context

Open source (AGPLv3), cross-platform DAW with a Logic-style workflow, an MCP server so external AIs can control it (plugins included), and a built-in assistant using OpenAI-compatible and Anthropic endpoints. No AI models are bundled. See `docs/design-system.md` for the visual language.

The full product is decomposed into six sub-projects, each with its own spec, plan and implementation cycle:

1. **Core** (this spec): project model, command queue with undo, audio engine, transport, persistence.
2. UI shell (Qt Quick).
3. Plugin host (VST3/CLAP, AU on macOS).
4. MCP server.
5. DSP features: Flex Time, then Smart Tempo, Space Designer, Flex Pitch.
6. Built-in AI assistant.

Fixed decisions carried in: C++ engine with JUCE used headless (`juce_audio_*`, no `juce_gui`), Qt Quick for the UI later, Windows is the first platform, classical DSP only.

## 2. Goal and success criteria

Deliver a headless C++ library plus a CLI that can load, edit, play, render and save a multitrack project, proven by automated tests. No UI and no MCP in this sub-project.

Done when:
- A project with audio and MIDI tracks, a tempo map, gain/pan/mute/solo, sends and buses can be built through commands only.
- `lpc-cli render project.lpc out.wav` produces a deterministic bounce, and `lpc-cli play project.lpc` plays it on a Windows audio device.
- Every command has a working undo and redo.
- The test suite in section 8 passes on CI (Windows, MSVC).
- No allocation or lock occurs on the audio thread during test runs (verified by the detector).

## 3. Non-goals

- Any UI, any MCP or AI code.
- Real plugin hosting (only the `IProcessor` interface and two test processors).
- Recording, time stretching, pitch editing, tempo detection.
- Linux and macOS support (kept possible by design, not built or tested now).
- A persistent command log, collaborative editing.

## 4. Architecture: threads and data flow

Three actors, each datum has exactly one owner.

1. **Project model**, owned by the *project thread*. It is the authoritative state. UI, MCP and the assistant never touch it directly; they send commands and issue read requests to this thread.
2. **Render graph**, owned by the *audio thread*. An optimized copy used only for playback.
3. **Command queue**, feeding the project thread from any client.

```
Client ──Command──▶ [project thread] ──AudioMsg (SPSC, lock-free)──▶ [audio thread]
                        authoritative model         │  render graph
                        ◀────── Feedback (SPSC) ◀────┘  (meters, playhead, garbage)
```

Rules:
- The project thread applies a command to the model, then translates the change into small typed audio messages (for example `SetGain`, `AddClip`, `SwapNode`).
- **No allocation on the audio thread.** New objects (nodes, buffers, clips) are built and prepared on the project thread and arrive as ready pointers. Replaced objects are never freed on the audio thread: they return through the feedback queue and the project thread destroys them.
- Messages carry a sequence number. The audio thread applies them in order at the start of each block. The project thread tracks the last applied sequence, so reads can report "applied" or "pending".
- Frequently changing parameters (faders, knobs) use a dedicated atomic value channel with smoothing on the audio side, so they do not flood the message queue.
- Meters, playhead position and clip flags travel back through the feedback queue or atomics, never through the model.
- A full queue applies backpressure on the project thread. Messages are never dropped and the audio thread never waits.

Main risk: two copies of state can diverge. Mitigation: each kind of change has a single model-to-message translator, covered by the equivalence test (section 8, item 2).

## 5. Project model

```
Project
├─ tempoMap        tempo and time signature over time
├─ markers
├─ tracks[]        audio | midi | instrument | aux | bus | master
│   ├─ strip       gain, pan, mute, solo, inserts[], sends[], output
│   ├─ regions[]   audio or MIDI
│   └─ automation[]
└─ mediaPool       source files (content hash, path, metadata)
```

- Every entity has a stable UUID. Clients, undo and persistence refer to IDs, never indices.
- **Time:** musical positions are stored in ticks (960 PPQ). The tempo map converts ticks to samples. Each audio region declares whether it follows tempo (anchored in ticks, as needed later by Smart Tempo and Flex Time) or stays anchored in absolute time. This is decided now because later features depend on it.
- Plugins are references (format ID plus opaque saved state), so the model does not depend on VST3, CLAP or AU types.

## 6. Commands and undo

- Each modification is a `Command` whose `apply(model)` returns a result and its inverse. The undo stack lives on the project thread.
- **Transactions** group several commands into one undo step (one UI action or one MCP request is one entry).
- Commands are **JSON-serializable with a schema**. This is the same surface the MCP server and the assistant will expose, and it enables replay tests.
- A command is validated before it runs. An invalid command returns a structured error (usable by an AI to self-correct) and leaves the model untouched.
- Outside undo: transport state, zoom, selection.

## 7. Audio engine and persistence

**Engine**
- `IAudioDevice` abstracts the device (WASAPI and ASIO via JUCE on Windows). The rest of the engine does not depend on JUCE, so tests never need hardware.
- Graph: track, channel strip (inserts, gain, pan), sends and buses, master. Processing order and plugin delay compensation (PDC) are computed on the project thread whenever the graph changes and delivered ready to the audio thread.
- `float32` blocks, 64-bit sample positions, smoothed parameters.
- Transport: play, stop, locate, loop; position is available in samples and in ticks. MIDI events are scheduled with sample accuracy inside a block.
- Disk streaming: a reader thread fills ring buffers ahead of time. The audio thread only reads from them; on a miss it outputs silence and reports an underrun, it never waits.
- Plugins: Core defines `IProcessor` and ships two test processors (gain, sine). Real hosting is sub-project 3.
- **Offline render** uses the same graph without a device, faster than real time and deterministic. It is also the main testing tool.

**Persistence**
- A project is a folder `Name.lpc/`: `project.json` (readable, diffable), `audio/` (media), `cache/` (peaks and analysis, regenerable, ignorable by VCS).
- `project.json` has `schemaVersion` and a chain of migrations. Writes are atomic (temp file then rename) and the previous version is kept as a backup.
- Saving works from a model snapshot taken on the project thread, so it never blocks audio.

## 8. Testing strategy

1. **Commands:** property tests with random command, undo and redo sequences. `apply` followed by the inverse must return an identical model.
2. **Model/graph equivalence:** the graph built from scratch from a model must equal the graph obtained by applying audio messages one at a time.
3. **Golden render:** example projects rendered offline to WAV and compared with stored references within a tolerance.
4. **Real-time safety:** in test builds a detector intercepts any allocation or lock on the audio thread and fails the test.
5. **Persistence:** save, reload, compare. Migrations are tested with files from earlier schema versions.
6. **CI:** Windows (CMake, vcpkg, MSVC). Linux and macOS jobs are added later.

## 9. Deliverables

- `core` static library (C++20).
- `lpc-cli` with `render` and `play`.
- Test suite and example projects.
- CMake and vcpkg build, CI pipeline.
- `THIRD_PARTY.md` listing dependencies and licenses.

## 10. Risks and open questions

- **Model/graph divergence** (section 4): handled by the equivalence test; revisit if it proves costly.
- **Audio-thread allocation leaks** from third-party code (JUCE, plugin SDKs): the detector catches them in tests only for exercised paths.
- **Tick resolution** (960 PPQ) is a guess; confirm it against later needs (tuplets, Smart Tempo).
- **Product name** and project extension are undecided.
- **JUCE licensing:** AGPLv3 means the whole product must be AGPLv3 (or a commercial JUCE license). Confirm before the first public release.
- **ASIO SDK** license terms must be checked before shipping ASIO support.
