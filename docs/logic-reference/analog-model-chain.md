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
5. **A separate generic `CGen` effect** (`ProcessGenInplace`, object at `this+0x600`) — most likely the
   saturation/waveshaper stage from the Loudness Macro mapping (`mixer.md`), **not** Analog Model — see below for
   why these are different slots
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
   channel's own sample buffer — the same code path is used whether the model's CoreML output is audio-shaped
   (`T`/`TSO`) or a single scalar (`G`/`G_SO`, `S`/`S_SO`); see §3 for what's and isn't confirmed about the scalar
   case.
5. **Apply a per-block linear gain**: `vDSP_vsmul` multiplies the channel's buffer by a value stored at `+0x164`.
6. **Call `CBasicFX::Process`** (the generic parent-class step that moves the final buffer out to the host).
7. **Run the crossfade/silence state machine** (see §4).

A **silence-timeout** was also found: if every channel/path reports no new model result for roughly **41000
samples** (≈0.85 s at 48 kHz — read directly as a literal constant, `41000 % blockSize` / `41000 / blockSize`), the
per-channel output buffers are zeroed. This looks like a DC/stuck-state safety net, separate from the crossfade
mechanism below.

## 3. What the scalar outputs (`G`/`G_SO`/`S`/`S_SO`) actually do

**Confirmed**: `CAnalogModelingMasteringChainFX::Coefficients(value, index)` — the class's generic,
externally-driven automatable-parameter setter (same pattern as `CCompressorClassA::Coefficients` /
`CLimiter::Coefficients` documented in `mixer.md`) — has at least one concrete case: `index == 3` converts `value`
(a dB figure) to linear via `10^(dB/20)` and stores it at `+0x164`, the exact field `OnProcess` multiplies into the
audio every block via `vDSP_vsmul` (§2 step 5).

**Not confirmed**: whether a scalar model's own inference result is what ends up at `+0x164` each block, or
whether `+0x164` is a separately automation-driven trim parameter unrelated to the live model output. The
`OnProcess` pseudocode decompiled this pass shows the scalar retrieved via vtable `+0x68` being `memcpy`'d into the
*audio* sample buffer (consistent for the audio-output models, but would only overwrite one sample's worth of a
1024-sample buffer for a true scalar output) — the data-flow edge connecting a scalar model's result to the gain
field actually applied would have to happen inside the opaque `Mil2BNNSModel` wrapper's own `+0x68` implementation,
which wasn't reached by static analysis this pass. So: **the mechanism is plausible and architecturally consistent
(gain field exists, is applied per block, `Coefficients` proves it's dB→linear) but the literal wiring from "scalar
model output" to "+0x164" was not independently verified — flagged as not determined rather than guessed.**

No smoothing/interpolation was seen on the `+0x164` multiply itself (unlike the crossfade ramps in §4, which are
explicitly stepped); if the scalar does drive it directly, updates would be stepped at the 1024-sample block rate,
not per-sample.

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

This resolves the open question from `mixer.md`'s twenty-third pass with certainty: **Valve is the only one of the
three that's a true audio-to-audio neural model** (`T`/`TSO`); **Punch and Transparent are both scalar/control
models** (`G`/`G_SO` and `S`/`S_SO` respectively) whose exact application mechanism is the open point in §3.

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

## 9. Empirical verification — attempted, partial result

Per the request, tried to run the actual shipped `G`/`G_SO`/`S`/`S_SO` models (48 kHz) with a Swift CoreML harness
feeding 1 kHz sine blocks at −30/−20/−10/−3 dBFS and white noise, reading the scalar output per block. **This
failed**: `MLModel(contentsOf:)` cannot load any of these `.mlmodelc` bundles at all (see §7) — not a bug in the
test harness, confirmed by trying both the extracted copy and the original in-framework file directly. Reproducing
the real inference would require reimplementing (or finding) Apple's private `Mil2BNNSModel`/BNNS loading path,
which is out of scope for this pass. The attempt wasn't wasted, though: inspecting the raw `.mil` IR directly (to
understand *why* loading failed) is what surfaced the stateful-model architecture fact in §7 — a genuine, if
indirect, empirical finding from the real shipped artifact, just not the block-by-block scalar table originally
asked for.

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
