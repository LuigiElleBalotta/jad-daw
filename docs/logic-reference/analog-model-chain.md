# Analog Model (Mastering Assistant) — the processing chain, end to end

This is a focused follow-up pass on `CAnalogModelingMasteringChainFX` (the engine behind the Mastering Assistant's
Clean/Valve/Punch/Transparent menu, `MADSP.framework/MADSP`), tracing the actual per-block processing, how it's
wired into `CAutoMaster`'s chain, and how the six consumed models (`TSO`/`T`/`G_SO`/`G`/`S_SO`/`S`, see
`machine-learning-models.md`) are selected and used. Static analysis only (REA + Ghidra 12.1.4), no Logic project
touched. Everything below is paraphrased from decompiled pseudocode in our own words — no Apple source text is
reproduced, and no long literal code dumps are included; numeric constants and addresses are cited because they're
facts, not expression.

## 1. Where it sits, and the call order through it

The host-facing entry point is `CAutoMasterFX::OnProcess`. It runs `CAutoMaster::Process()` (buffering/record-mode
bookkeeping), then unconditionally chains **four more** generic `CBasicFX::Process` calls on object slots at
`this+0xb08/+0xb10/+0xb18/+0xb20`. Those four weren't identified this pass (plausibly a final output-safety/trim
stage or two) — **not determined**.

`CAutoMaster::Process()` itself, once past its loudness/record-mode state machine, calls
`CAutoMaster::ProcessMainMasteringChain(sampleCount)`, whose confirmed internal order is:

1. **Biquad EQ** (`ProcessBiquadChain`, filter slot "2")
2. **Stereo spread** (`AutoAdjustSpread` in Analyze mode, `ProcessSpread` otherwise)
3. **Biquad EQ again** (`ProcessBiquadChain`, filter slot "0") — a second, distinct filter chain from step 1
4. **Match EQ** (`CMatchEQFX`, mutex-guarded, processed in internal chunks)
5. **A separate generic `CGen` effect** (`ProcessGenInplace`, object at `this+0x600`) — **correction
   (`mac-pass-3.md`): this is the Dynamic EQ** (`CAutoMaster::ProcessDynEQ` calls the exact same object), not
   saturation/waveshaper as first guessed. `AutoAdjustDynEQ`, its Analyze-mode counterpart, decompiles to an empty
   stub — this feature looks unused/vestigial in build 6306. **Not** Analog Model either way — see below for why
   these are different slots
6. **Loudness-matching trim gain**: compares measured loudness (`AnalyzeLoudness`) against a target, converts the
   dB difference to a linear multiplier via `10^(dB/20)`, applies it with `vDSP_vsmul`
7. **`ProcessAllDynamicCBs(gain, sampleCount)`** — the "dynamics" stage

**Analog Model is invoked from inside step 7, not as its own top-level stage.** `CAutoMaster::ProcessAnalogModel`
(confirmed via its only two callers) is called exclusively from `ProcessAllDynamicCBs`, i.e. it's grouped with
whatever else that function does (almost certainly the multiband compressor and limiter, going by
`mixer.md`'s already-documented chain). `ProcessAllDynamicCBs` is a large function (61 basic blocks); its exact
internal ordering — compressor before or after Analog Model, etc. — **was not traced this pass**. This corrects an
implicit assumption in earlier passes that Analog Model was a standalone chain link; it's actually interleaved with
dynamics processing.

`CAutoMaster::ProcessAnalogModel(sampleCount)` itself is a thin wrapper: it chunks the buffer, calls
`CAnalogModelingMasteringChainFX`'s process method through the generic FX vtable (slot `+0x30`, object at
`this+0x610` — a **different** slot from the `+0x600` `CGen` in step 5 above, confirming they're sibling effects,
not the same object), then applies **its own** separate dB-to-linear trim gain via `vDSP_vsmul` immediately
afterward, using the same per-channel field pair (`+0x930`/`+0x938`) the rest of the chain uses for makeup gain.
No wet/dry blend specific to Analog Model was found — this looks like a plain trim/makeup gain around the engine,
not a mix control.

## 2. Inside `CAnalogModelingMasteringChainFX::OnProcess` — what actually happens per block

The object decouples the host's buffer size from the model's fixed internal block: incoming samples are
accumulated into a per-channel queue, and the model is only invoked once 1024 samples are available (the FX's own
`+0x178` field, confirmed as 1024 by construction). Synthesized flow, in order:

1. **Per channel/path** (the object supports up to 6 slots; a plain stereo project uses 2): copy the next
   1024-sample block out of the queue.
2. **Wait for the model instance to be free.** A lock-free spin (64 attempts decrementing an atomic counter), then
   a timed wait using `std::chrono::steady_clock`: `sched_yield()` once ~4 µs have passed, then a real
   condition-variable wait (`__libcpp_atomic_wait`) past ~64 µs. This means model inference can run on a separate
   thread from the audio thread — the audio thread blocks on this wait rather than running inference inline.
3. **Submit the block**: `memcpy` the 1024 samples into the model's input buffer, then two vtable calls (slots
   `+0x10`, `+0x18` — set input/output pointers) and one at `+0x90` (run/predict).
4. **Wait again**, same pattern, then **retrieve the result** via vtable slot `+0x68` and `memcpy` it back into the
   channel's own sample buffer — **correction (second pass): all six models output full `[1,1,1,1024]` audio (see
   §3), not a mix of audio and scalar, so this is a uniform audio-in/audio-out copy for every one of them, no
   special case needed.**
5. **Apply a per-block linear gain**: `vDSP_vsmul` multiplies the channel's buffer by a value stored at `+0x164`.
6. **Call `CBasicFX::Process`** (the generic parent-class step that moves the final buffer out to the host).
7. **Run the crossfade/silence state machine** (see §4).

A **silence-timeout** was also found: if every channel/path reports no new model result for roughly **41000
samples** (≈0.85 s at 48 kHz — read directly as a literal constant, `41000 % blockSize` / `41000 / blockSize`), the
per-channel output buffers are zeroed. This looks like a DC/stuck-state safety net, separate from the crossfade
mechanism below.

## 3. Correction (second pass): there are no scalar outputs — `+0x164` is a separate gain parameter

The original pass, going only off `metadata.json`, read `G`/`G_SO`/`S`/`S_SO`'s declared output shape as a scalar
(`[]`) and spent this section trying to work out how a single float could drive a per-block gain. **That premise
was wrong.** `mac-pass-3.md`'s follow-up verification confirms, three independent ways, that all six models —
including `G`/`G_SO`/`S`/`S_SO` — output full `[1,1,1,1024]` audio: the raw `.mil` program's own `return` statement
types the output as `[1,1,1,1024]` for every one of the six (a real `conv` op, not a reduction); the project's own
ONNX conversion of all six produces audio for all six; and a live Core ML run of a same-symptom model (`PT`)
returned a genuine `[96,360]` array where its own `metadata.json` also claimed `[]` — `[]` turns out to be Core
ML's "shape not statically resolved" placeholder, not a true scalar declaration, for every model that shows it.

So §2 step 4's `memcpy` is a plain, uniform audio-to-audio copy for all six models — no scalar case, nothing
special to explain there.

`+0x164` and its `Coefficients(value, 3)` setter (confirmed: `10^(dB/20)` stored there, applied via `vDSP_vsmul`
every block) are real and still exist, but are now understood as **a separate, ordinary automatable gain
parameter on the FX itself**, unrelated to any model output — not a conduit for a (nonexistent) scalar model
result. Where the host sets this value from, and what it represents musically, remains open — see
`mac-pass-3.md` §2b for the current state of that question (not determined: the external caller lives outside
`CAutoMaster`'s own process chain, in the UI/automation parameter system).

## 4. Model-switching crossfade (confirmed, exact constants)

`SetModel` sets a "pending switch" flag (`+0x170 = 1`). The next `OnProcess` call, on detecting either that flag or
a buffer-continuity break, sets a separate state field at `+0x174` to **1** and "touches" every active model
instance (vtable slot `+0x40` on each — a reset/reload call, consistent with these models being internally
stateful, see §6). `+0x174` then drives a 4-state machine, read at the top of every subsequent `OnProcess` call:

| State | Meaning | Behaviour |
|---|---|---|
| 0 | steady | no ramp, pass-through |
| 1 | switching out | linear fade from the current gain down toward 0, step **exactly 1/1024 = 0.0009765625** per sample (clamped ≥ 0); once ≤ 1e-6, moves to state 2 |
| 2 | silent hold | output buffers zeroed; a signed countdown field (`+0x160`) increments each call until it stops being negative, then moves to state 3 |
| 3 | switching in | linear fade from 0 up toward 1.0, same **0.0009765625**/sample step (clamped ≤ 1.0); once ≥ 1.0, returns to state 0 |

Both ramps take exactly 1024 samples (1 internal block) start to finish — a clean, click-free crossfade whenever
the user changes Clean/Valve/Punch/Transparent live.

## 5. `AMMCDeviceType` → model enum — fully confirmed, not inferred

`SetModel(AMMCDeviceType)` switches on values 0–5; each case copies a 24-byte vector handle (a
`std::vector<unique_ptr<IMil2BNNSModel>>`'s begin/end/cap triple) from a fixed object offset into the "active
model" slot (`+0x1e0`):

| device type | source offset | = constructor argument (by position) |
|---|---|---|
| 0 | `+0x228` | `param_4` |
| 1 | `+0x240` | `param_5` |
| 2 | `+0x258` | `param_6` |
| 3 | `+0x270` | `param_7` |
| 4 | `+0x288` | `param_8` |
| 5 | `+0x2a0` | `param_9` |

The six offsets are each exactly `0x18` (24 bytes) apart, matching six consecutive vector objects stored inline —
and dividing each offset by 8 lines up exactly with the six `vector<unique_ptr<IMil2BNNSModel>>` constructor
parameters as built in `CAnalogModelingMasteringChainFX`'s own constructor. Cross-referencing against the
already-confirmed fact (`machine-learning-models.md`, twenty-third pass) that those six constructor arguments are
populated by `ModelFactory::createModel` calls for enum **10, 11, 12, 13, 14, 15 = `TSO`, `T`, `G_SO`, `G`, `S_SO`,
`S`**, in that exact order, gives:

| device type | enum | model |
|---|---|---|
| 0 | 10 | `TSO` |
| 1 | 11 | `T` |
| 2 | 12 | `G_SO` |
| 3 | 13 | `G` |
| 4 | 14 | `S_SO` |
| 5 | 15 | `S` |

**Which menu entry picks which device-type pair — confirmed directly from `CAutoMaster::Process`'s own decompiled
switch** (not inferred from naming or creation order, as the earlier pass had to leave it): the stored "desired
mastering mode" index (0=Clean, 1=Valve, 2=Punch, 3=Transparent) combines with a per-object `nativeAvailable` flag
(read from `+0xe1`, the Apple-Silicon/Neural-Engine-availability bit from the earlier-documented
`ECHardwareUtilities` check) exactly like this:

- index 0 (**Clean**): no `SetModel` call at all — confirmed bypass, as already known.
- index 1 (**Valve**): device = `nativeAvailable ? 0 : 1` → **`{TSO, T}`**
- index 2 (**Punch**): device = `2 + (nativeAvailable ? 0 : 1)` → **`{G_SO, G}`**
- index 3 (**Transparent**): device = `4 + (nativeAvailable ? 0 : 1)` → **`{S_SO, S}`**

This resolves the open question from `mixer.md`'s twenty-third pass with certainty: Valve, Punch, and Transparent
each use their own pair of models. **Correction (second pass, §3): all three pairs are audio-to-audio neural
models** (`G`/`G_SO`/`S`/`S_SO` were wrongly read as scalar from stale metadata) — so "Punch" and "Transparent"
aren't control-signal extractors either, they're genuinely distinct audio circuits, same as Valve.

## 6. `G` vs `G_SO`, `S` vs `S_SO` — what decides which one loads

Purely the `nativeAvailable` flag at selection time (§5) — a static, load-time choice re-evaluated only when the
model changes (user picks a different menu entry, or Apple Silicon/ML availability changes between sessions), never
per-block or per-sample-rate. Sample rate is a fully separate, orthogonal axis: whichever single device gets
selected here then separately picks one of its own 4 per-rate `.mlmodelc` files (44.1/48/88.2/96 kHz, documented in
`machine-learning-models.md`) purely based on the project's sample rate.

## 7. Normalization, latency, state between blocks

- **No explicit soft-clip or pre-scale** was found around the model's input/output specifically — the only
  explicit audio-range safety net in the whole `CAutoMaster::Process` path is a single `vDSP_vclip` to `[-1, 1]`
  applied once, at the very end of the function, to the whole chain's output (not specific to Analog Model).
- **Reported latency** (`CAnalogModelingMasteringChainFX::GetDelay`, exact thresholds and values):

  | Sample rate | Reported delay |
  |---|---|
  | ≤ 48000 Hz | **114 samples** (`0x72`) |
  | 48000–96000 Hz | **138 samples** (`0x8a`) |
  | > 96000 Hz | **372 samples** (`0x174`) |

  This is the value the plugin declares for host PDC (plugin delay compensation); it wasn't independently
  cross-checked against the model's own true processing latency.
- **Internal block size is fixed at 1024 samples**, independent of host buffer size and project sample rate; the
  constructor builds a dedicated FFT-based up/down-sampler pair (`CDownSampler`/`CUpSampler`, `CFFTFilter`-based)
  per channel/path to resample between the model's fixed internal rate and the project's actual rate (confirmed in
  the twentieth pass, re-confirmed here from the same constructor).
- **The models are genuinely stateful, confirmed by directly inspecting the shipped `.mil` file** (not just
  inferred from architecture): `G_48`'s raw MIL program signature declares dozens of explicit `_in_state`/
  `_out_state` tensor pairs threaded through the conv stack (e.g. `audio_in_state [1,1,1,12]`,
  `blockInput_elementwise_in_state [1,6,1,6]`, and many more), with receptive-field sizes that grow by **both
  powers of 2** (12→24→48→96→192→384→768) **and powers of 3** (324→972→2916→8748→26244) in different branches of
  the same network — two distinct dilation families in one model. This is a genuinely new structural fact about
  the architecture, not previously documented.
- **These `.mlmodelc` bundles cannot be loaded through the public CoreML API** — confirmed empirically this pass:
  `MLModel(contentsOf:)` on the real, unmodified files (both the extracted copy and the original in-framework
  copy) fails with `Error Domain=com.apple.CoreML Code=3 "Error in reading the MIL network."`. This is consistent
  with, and reinforces, the already-established fact that Logic loads these through its own private
  `Mil2BNNSModel`/BNNS path rather than the public CoreML runtime — the public loader genuinely can't read them
  (not merely "wasn't used"), likely because of the nonstandard `.mil` filename (`modelstreaming_transformed.mil`
  instead of the usual `model.mil`) and/or the stateful-model feature predating its public CoreML API surface.
- **Model inference is asynchronous** relative to the audio thread (§2 step 2) — a spin/yield/condition-wait
  pattern bridges it, meaning a slow inference can still stall the audio thread (it blocks until done or until the
  wait primitives give up, no fallback/skip path was seen).
- **Warm-up at construction**: each of the 6 model instances is run 32 times on a zeroed buffer as part of building
  the object, before any real audio reaches it — almost certainly to let internal state/lazy allocations settle
  ahead of the first real block, avoiding a cold-start glitch.
- **Model-switch behaviour**: see §4's crossfade — switching models resets every active instance's internal state
  (vtable `+0x40`) and crossfades the output over two 1024-sample linear ramps either side of a silent hold.

## 8. Other exposed parameters

No continuous "Analog Model amount/intensity" control was found. The UI (confirmed in an earlier pass,
`CAutoMasterShell::BuildIntelModelMenu`) is a plain 4-item discrete menu (Clean/Valve/Punch/Transparent); no
mix/wet-dry slider string or parameter was found alongside it. `Coefficients(value, index)` does expose at least
one generic automatable numeric parameter (`index == 3`, the dB-sourced gain from §3) beyond the discrete model
choice — but its real meaning (an overall trim? something else?), its UI name, its range, and any other `index`
values **were not decompiled this pass — not determined.**

## 9. Empirical verification — resolved in the second pass (`mac-pass-3.md`)

Per the request, first tried to run the shipped `G`/`G_SO`/`S`/`S_SO` models (48 kHz) with a Swift CoreML harness
feeding 1 kHz sine/noise blocks, reading what this pass still believed was a scalar output per block. That failed
at the loading step (`MLModel(contentsOf:)` can't read the `.mlmodelc` as shipped — see §7), and the premise
(scalar output) turned out to be wrong anyway (§3). **The second pass fixed the loading step** (copy +
`model.mil` rename) and ran real deterministic-input inference where possible: Core ML's *public prediction API*
still refuses to run any of these six models even once loadable (a second, distinct "invalid Tensor port" error,
independent of the scalar question) — so no block-by-block I/O table from Core ML exists for `G`/`T`/`S` etc.
specifically. What the second pass *could* run directly (`DownbeatTrackerCondMPS`, `PT`, `RTPT`) matched the
project's own ONNX conversions to within ~1e-6–1e-4 max-abs-err, and separately caught one real conversion bug
(`OnsetDetectionCNN`, 0.226 max-abs-err) — strong validation of the overall ONNX pipeline even though it can't
directly touch the Analog Model networks. Full numbers in `docs/logic-reference/mac-pass-3.md`, Block 1.

## Source

Static analysis of `MADSP.framework/MADSP` (x86_64 slice) via REA + Ghidra 12.1.4 (no headless scripting needed
this pass — every address below decompiled directly through the live Ghidra session, no unresolved jump tables
encountered this time):

- `CAnalogModelingMasteringChainFX`: constructor (6-model overload) `0x2d45d0`, `OnProcess` `0x2d0d10`, `SetModel`
  `0x2d6060`, `GetDelay` `0x2d6540`, `Coefficients` `0x2d65d0`, plus the full method list (`Reset` `0x2d0c70`,
  `FilterSetup` `0x2d1f40`, `SamplerateSupported` `0x2d6580`, `IsContinuousProcessing` `0x2d6600`, destructors) from
  `search_procedures`.
- `CAutoMasterFX::OnProcess` `0x7409e0`; `CAutoMaster::Process` (`__ZN11CAutoMaster7ProcessEv`);
  `CAutoMaster::ProcessMainMasteringChain` (`__ZN11CAutoMaster25ProcessMainMasteringChainEx`);
  `CAutoMaster::ProcessAnalogModel` `0x5fd320`; `CAutoMaster::ProcessAllDynamicCBs` `0x5fc140` (callees enumerated,
  not fully decompiled); caller/callee graph confirmed via `procedure_callers`/`procedure_callees`.
- CoreML load-failure reproduction: `MLModel(contentsOf:)` against
  `../logic-pro-reference/analog-model-ml-models/{G,G_SO,S,S_SO}_48_*.mlmodelc` and the original files under
  `/Applications/Logic Pro.app/Contents/Frameworks/MAMachineLearning.framework/Versions/A/Resources/`, via a
  throwaway Swift script (`swift <script>.swift`, no Xcode project). Raw `.mil` IR read directly
  (`modelstreaming_transformed.mil`'s own `func main<ios15>(...)` signature) to extract the state-tensor list in
  §7.

For jad-daw: the concrete, reusable facts here are architectural, not formulas to port verbatim (these are opaque
trained weights) — the **1024-sample fixed internal block with FFT resampling around it**, the **32-pass
zero-buffer warm-up at construction**, the **exact 1024-sample linear crossfade either side of a silent hold on
model switch**, and the **spin/yield/condition-wait bridge to asynchronous inference** are all directly applicable
patterns if jad-daw ever hosts its own learned, stateful audio effect behind a live-switchable "model" menu.
