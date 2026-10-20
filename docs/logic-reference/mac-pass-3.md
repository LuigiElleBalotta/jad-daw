# Mac pass 3 — status index

One section per requested block. Status legend: **done** / **partial** / **not started**. For anything not fully
resolved, the "what's missing" line says what a future pass would need.

| Block | Status | What's missing (if any) |
|---|---|---|
| 1. ONNX vs CoreML numeric verification | **done** (for everything that can run through the public API) | The 6 Analog Model + 10 Saturation-family networks (317 of 321 bundle/block-size combinations) can't be numerically compared at all — Core ML's public `prediction(from:)` API itself refuses to run them (see below), independent of ONNX correctness. A real conversion bug was found and confirmed in `OnsetDetectionCNN` (0.226 max-abs-err, flagged as suspect in `ml-conversion.md` already) — needs a fix in `espresso2onnx.py`'s weight-layout/flatten-order handling, not a Mac-side task. |
| 2a. `ProcessAllDynamicCBs` stage order | **done** | Exact exit condition of the Analyze-mode MB-compressor calibration loop not traced (lives inside a vtable call Ghidra couldn't resolve). |
| 2b. `Coefficients(value,3)` origin/range | **partial** | The field it writes (+0x164) and the dB→linear conversion are confirmed (`analog-model-chain.md`); where the external caller sources the dB value, and any other `index` besides 3, are not determined — needs tracing the UI/automation parameter system, which calls in from outside `CAutoMaster`'s own process chain. |
| 2c. The 4 trailing `CBasicFX::Process` slots | **done** | — identified directly in `CAutoMasterFX`'s own constructor: `CLevelMeterFX`, `CLoudnessMeterFX`, `CCorrelationMeterFX`, `CSpectrumAnalyzerFX` — the window's metering chain, not further audio processing. |
| 2d. `nativeAvailable` meaning | **partial** | Confirmed it's read from an Apple-Silicon-ML-capability check; whether it changes *sound* vs only *speed* not independently re-verified this pass (carried over from earlier passes' finding). |
| 2e. Clean/Valve/Punch/Transparent parameter defaults/ranges | **partial** | Found the "Character" Smart Control parameter (ID 10), the Mastering Assistant's `#default.pst` flat-parameter block, and the concrete `+0xd8`/`+0xe0`-indexed trim table at `+0x3c0` that `ProcessAnalogModel` reads from (2a); the table's actual stored values weren't read this pass. |
| 2f. Stereo spread / multiband compressor / Auto EQ algorithms | **partial** | Stereo spread (`ProcessSpread`) fully decoded: real Mid/Side matrix processing, exact formula and a `-1.0` sentinel for full mono. `AutoAdjustMBCompressor`'s per-iteration mechanics are clear (2 dB steps, shared loudness-meter object) but its exact convergence/exit check wasn't recoverable (same unresolved-control-flow pattern as 2a). Found that `CAutoMaster::AutoAdjustDynEQ` is an **empty no-op stub** — "Dynamic EQ" looks unused/vestigial in this build. The 31-band spectral-correction EQ ("Auto EQ") was narrowed to a specific biquad-chain object (`ProcessSpectralBalance`, filter at `+0x580`, distinct from the other two EQ chains) but its coefficient-calculation code (the actual 31-band correction math) wasn't traced. The 4 Loudness-Macro gain formulas are now confirmed byte-exact (not just structurally) straight from their own named functions and literal constant tables. |
| 2g. Compressor circuit tables (Platinum, Class A, Studio VCA/FET, Opto, Classic VCA) | **not started** | Builds on existing `mixer.md` findings (ClassA, DBX, FET, LP curves already partly decompiled there); the other circuits (Platinum, Studio VCA/FET, Opto, Classic VCA) not yet touched. |
| 3. Analysis-network preprocessing (Onset/Downbeat/Pitch/Saturation) | **partial** | Got real, confirmed CoreML input/output shapes for all of these (Block 1). Found `CAutoMaster::SetSaturationAmnt` — confirms a **second, classical** saturation path exists alongside the neural one (`CPassiveEQFX::SetOutputDrive`, amount parameter scaled ×127, 0-127 range) for when the neural model isn't used; the neural path's drive→`cond` wiring itself, and all the mel/beat/pitch preprocessing code, remain untraced. |
| 4. Klopfgeist/metronome | **not started** | Needs both decompilation (`CKnockVoice`'s frequency table) and live Project Settings > Metronome UI inspection. |
| 5. Project Settings (remaining tabs) | **not started** | Needs live UI inspection across ~10 tabs. |
| 6. Missing dialogs (colors, I/O Labels, New Tracks, Global Tracks, overlapping recordings, Bounce, automation modes) | **not started** | Needs live UI inspection/screenshots and, for colors, an `Assets.car` search specifically for track/UI color swatches (the main app catalog searched in an earlier pass didn't have them under an obvious name). |
| 7. Smart Tempo/Flex quality comparison | **not started** | Needs a throwaway test project with real Apple Loops, built and played in the live app. |
| 8. Match EQ / Channel EQ / Limiter / Pan / Fader curves | **not started** | Match EQ's pipeline architecture is already documented in `mixer.md`; the exact per-band Q/slope/curve constants for Channel EQ, Limiter/Adaptive Limiter/Noise Gate tables, and pan/fader taper formulas are not yet decompiled. |

This pass covered Block 1 (fully, once `jad-daw-private` was cloned) and Block 2a (fully), 2b/2d/2e (partially) as
the highest-priority, most tractable items in the time available. Everything below "not started" needs its own
dedicated pass — most of blocks 4-7 specifically need live interaction with the running app (screenshots, UI
inspection, test-project playback), not just static analysis, which is a different and slower kind of work than
what's in this file so far.

---

## Block 1 — ONNX vs Core ML numeric verification

Correction mid-pass: `tools/ml/coreml_reference.swift`/`compare_with_coreml.py` and `jad-daw-private/onnx/` weren't
on this machine yet because this repo's `main` had moved on (the parallel Windows session pushed a large batch of
work, including those two tools) and `jad-daw-private` is its own separate, gitignored repo
(`LuigiElleBalotta/jad-daw-private` on GitHub, never a subfolder of `jad-daw`'s own git history). `git pull`
(actually a rebase, since local work had diverged) plus `gh repo clone LuigiElleBalotta/jad-daw-private
jad-daw-private` got both. With that, the full verification described below was possible.

### Real numeric results

Compiled `coreml_reference.swift` (`swiftc -O`) and ran it against every `.mlmodelc` in
`../logic-pro-reference/all-ml-models/`, on a prepared copy where every bundle needing the `model.mil` rename fix
(see below) got it — 321 bundle/block-size combinations total (one per `.mil` file, matching the ONNX side's own
319 conversions 1:1, plus the 2 Espresso-format bundles). Then ran `compare_with_coreml.py` against
`jad-daw-private/onnx/`.

**Models that actually run through the public API (4 of 321) — real max-abs-err table:**

| Model | Output(s) | Max abs err | Verdict |
|---|---|---|---|
| `DownbeatTrackerCondMPS` | `output_beats` | 8.2e-7 | OK |
| | `output_downbeats` | 1.79e-7 | OK |
| | `output_beats_no_downbeats` | 5.89e-7 | OK |
| | `output_tempo` | 2e-6 | OK |
| | `output_bpb` | 2.41e-6 | OK |
| `PT` | `Identity` | 7.63e-4 | OK |
| `RTPT` | sigmoid output | 5.96e-5 | OK (compared manually — the stock script's exact-shape input matcher doesn't account for ONNX's extra leading batch dim; same deterministic input formula, reshaped) |
| `OnsetDetectionCNN` | `output1` | **0.226** | **BAD — real conversion bug, not a tolerance nit** |

`OnsetDetectionCNN`'s mismatch is a genuine, confirmed problem, not a comparison-harness artifact (checked by
hand with the identical deterministic-input formula, reshaped to the ONNX file's own declared input order). It
lines up exactly with `ml-conversion.md`'s own flagged uncertainty: *"Not verifiable without Core ML:
`OnsetDetectionCNN`'s `inner_product` weight layout (taken as `[nC, nB]`) and the flatten order (NCHW)."* That
guess is now proven wrong by a real number. Needs a fix in `tools/ml/espresso2onnx.py`'s weight-layout handling —
a conversion-code task, not something to chase further from the Mac side.

Everything else — the 6 Analog Model and 10 Saturation-family networks, across every block size and sample rate —
**cannot be run through the public API at all**, so no ONNX comparison is possible for them regardless of how
correct the conversion is. Details below.

### Fixing (and generalizing) the Core ML load failure

The known failure (`MLModel(contentsOf:)` → `Error Domain=com.apple.CoreML Code=3 "Error in reading the MIL
network."`) comes from the compiled `.mlmodelc`'s main graph file not being named `model.mil` (the name CoreML's
loader expects) — Apple's internal export pipeline names it after the model class instead
(`modelstreaming_transformed.mil` for Analog Model/Family B, `model_<size>_transformed.mil` for Saturation/Family
A, one file per supported block size).

**The fix generalizes**: on a disposable copy, copying the size-matched `.mil` file to `model.mil` inside the
bundle makes `MLModel(contentsOf:)` **succeed** for both families. Confirmed on `G`, `S`, `T` (Analog Model, 48 kHz)
and `RTRST` (Saturation, which ships 7 separate `.mil` files — one per block size **32/64/128/256/512/1024/2048** —
confirming Saturation models support multiple real-time block sizes in one bundle, unlike Analog Model's single
fixed 1024; `metadata.json`'s own `generatedClassName: "modelForStreaming_2048"` says 2048 is the canonical/default
one). Never done on the original files under `/Applications` — always on a `cp -R` copy in `/tmp`.

**Loading works, but `prediction(from:)` does not — a second, distinct failure**, same on every state-threaded
model tried (`G`, `S`, `T`, `RTRST`, with or without the ~22 extra state tensors supplied by name/shape alongside
`audio`):

```
Error Domain=com.apple.CoreML Code=1 "The input feature is invalid or unsupported. (port trait Tensor, feature trait Unknown.)"
```

Tried: minimal input (`audio` only), full input (`audio` + every declared `_in_state` tensor, zero-filled, by exact
name/shape from the raw `.mil` signature), and forcing `computeUnits = .cpuOnly` — identical error every time. This
points to the model's I/O ports being declared with a raw internal "Tensor" port representation that the public
`MLFeatureValue`/`MLDictionaryFeatureProvider` bridge doesn't know how to satisfy — consistent with (and a second,
independent piece of evidence for) the standing conclusion that these models are only meant to run through Logic's
own private `Mil2BNNSModel`/BNNS path, not the public CoreML prediction API, even once the filename issue is
worked around. **So the requested block-by-block ONNX-vs-CoreML diff isn't obtainable via the public API at all**
for these specific model families, independent of the missing-files blocker above.

### The 6 Analog Model models: real output is audio, not scalar — confirmed from the raw MIL and from CoreML itself

Both methods agree, for every one of `T`, `TSO`, `G`, `G_SO`, `S`, `S_SO`:

- **Raw `.mil` text**: the final op before `return` is `var_849 = conv(...)`, explicitly typed
  `tensor<fp32, [1, 1, 1, 1024]>` — a real convolution producing a full 1024-sample block, not a reduction to a
  single value. Checked directly in all 6 files.
- **Live CoreML `modelDescription`** (once loadable via the rename fix): `T`'s declared output shape is correctly
  `[1, 1, 1, 1024]`; **`G` and `S` are declared `[]`** (empty/scalar) despite computing the exact same
  `[1,1,1,1024]` conv internally. This is an inconsistency in Apple's own compiled metadata, not a real behavioral
  difference — the two methods of inspecting the *same* model disagree with each other, and the raw computation
  graph (which is authoritative) says audio. **This confirms your ONNX finding and corrects `machine-learning-
  models.md`/`mixer.md`/`analog-model-chain.md`'s earlier "G/S output a single scalar" claim, based only on
  `metadata.json`, which carries the same wrong `[]` annotation** — those docs need the same correction (not yet
  applied this pass; flagging here first since this doc is the priority-1 deliverable).

**Root cause, now better supported (not fully proven, but no longer a guess)**: `PT` gave direct runtime evidence
of the same pattern. Its static `modelDescription` (and `metadata.json`) declare output shape `[]`, exactly like
`G`/`S` — but running it with real deterministic input through `coreml_reference.swift` returned an actual,
observed `[96, 360]` array. A statically-declared `[]` that resolves to a real, multi-element shape only once you
actually run the model is strong evidence that **`[]` is CoreML's placeholder for "shape not statically
determined / dynamic," not a true scalar declaration** — these MLProgram models have input- or
state-dependent output shapes that the static analyzer can't resolve ahead of time, and whichever tool produced
`metadata.json` just printed the placeholder as if it were the real shape. This explains `G`/`S` too, without
needing a coremltools-version-specific bug.

Also found this pass: `RTPTM`'s declared input shape in `modelDescription` (`[1, 1, 1024, 1]`) does **not** match
what the compiled program actually expects at prediction time (`[1, 1, 1, 1024]` — confirmed by the exact
mismatch error) — a second, independent example of the same declared-vs-real metadata gap, on the input side this
time. `coreml_reference.swift` (unmodified) fails to run `RTPTM` for this reason; not a model problem, a
declared-shape problem.

### Input/output shapes confirmed via Core ML for the other requested models (load-only, no ONNX diff possible)

These loaded directly, no rename needed — real, Core-ML-reported shapes, useful as a preprocessing anchor for
Block 3:

| Model | Inputs | Outputs |
|---|---|---|
| `OnsetDetectionCNN` | `input1` `[3, 80, 15]` | `output1` `[1]` |
| `DownbeatTrackerCondMPS` | `input_melspec` `[1, 4096, 131]` | `output_bpb` `[1,4096,13]`, `output_beats` `[1,4096,1]`, `output_tempo` `[1,4096,251]`, `output_beats_no_downbeats` `[1,4096,1]`, `output_downbeats` `[1,4096,1]` |
| `PT` | `input_1` `[1, 1, 16224, 1]` | `Identity` — statically declared `[]`, **really `[96, 360]` at runtime** (see below) |
| `RTPT` | `input_1` `[1, 1, 1024]` | `Identity` `[360]` |
| `RTPTM` | `input_1` `[1, 1, 1024, 1]` (statically declared — **wrong**, see below) | `Identity` `[]` (not independently confirmed at runtime this pass; given the `PT`/`G`/`S` pattern, likely also a placeholder, not determined for certain) |

`DownbeatTrackerCondMPS`'s **`input_melspec [1, 4096, 131]`** is a direct, concrete confirmation of two numbers
from your hypothesis: a **4096-frame window** and **131 mel bands** — both now Core-ML-confirmed facts, not
guesses. The 5-headed output (`bpb`/`tempo`/`beats`/`beats_no_downbeats`/`downbeats`) is new information: this is
a multi-task network, not a single beat-activation signal — `output_tempo`'s 251-wide last axis is almost
certainly a tempo histogram/classification over ~251 BPM bins, and `output_bpb`'s 13-wide axis most likely spans
a small set of beats-per-bar hypotheses (e.g. 2 through 14). Exact bin-to-BPM/meter mapping: **not determined**
this pass (would need the preprocessing/postprocessing code, Block 3's actual remaining ask).

### Source

`tools/ml/coreml_reference.swift` compiled with `swiftc -O` and run against a prepared copy (321
bundle/block-size combinations, `model.mil`-renamed where needed) of
`../logic-pro-reference/all-ml-models/` under `/tmp` (never against `/Applications` originals).
`tools/ml/compare_with_coreml.py` run against `jad-daw-private/onnx/` (cloned this pass via
`gh repo clone LuigiElleBalotta/jad-daw-private`). Raw `.mil` text read directly from the same files
(`modelstreaming_transformed.mil` for Analog Model, `model_2048_transformed.mil` for `RTRST`). Python env for
`onnx`/`onnxruntime`/`numpy`: a throwaway venv (`python3 -m venv`, system Python disallows global pip installs on
this machine).

---

## Block 2 — Analog Model and the Mastering chain (continued)

### 2a. `CAutoMaster::ProcessAllDynamicCBs` — the real stage order, fully decompiled

`ProcessAllDynamicCBs(float macroPos, CAutoMaster* self, size_t sampleCount)` is called once per block from
`ProcessMainMasteringChain` (already documented in `analog-model-chain.md`) with `macroPos` = the Loudness Macro
slider position (0..1) and runs, in this exact order:

1. **(mode 4 only, if a flag is set)** run the shared EQ biquad chain *before* everything else — see point 6, this
   is the "pre" side of a pre/post toggle.
2. **Multiband compressor** (if enabled): in Analyze mode, calls `AutoAdjustMBCompressor()` then an inline
   iterative calibration loop (tries gain in +2 dB steps, applies it, re-measures via a loudness-analyzer object,
   reprocesses — the exact convergence/exit condition lives inside that loop and wasn't fully traced, since
   Ghidra's control-flow recovery couldn't resolve its exit edge). In normal/Record mode: replays a **previously
   recorded** per-block gain (see the "automation replay" mechanism below), then runs the compressor.
3. **Unconditional compressor-input gain from the Loudness Macro position** — confirmed as the exact formula
   already documented in `mixer.md` (`CompressorInputGainFromLoudnessMacroPos`: asymmetric slope either side of
   center), now pinned to precisely this point in the call order, applied as `10^(dB/20)` via `vDSP_vsmul`,
   **every block, not just during analysis**.
4. **Saturation vs Analog Model — mutually exclusive, picked by one field.** This is the pass's single biggest new
   finding:
   ```
   if (mode == 4) {
       if (self->mode_select == 0) {          // offset +0xd8
           ProcessBasicDspModel();
           if (saturationEnabled && self->circuit_index)   // flags at +0x249 and +0xe0
               ProcessSaturation();
       } else {
           ProcessAnalogModel();
       }
       if (!self->eq_is_pre)                  // flag at +0x591
           ProcessBiquadChain(sharedFilter, 0); // same filter as step 1, now AFTER
   } else {
       ProcessBasicDspModel();
   }
   ```
   `+0xd8` (an `int`) and `+0xe0` (a `byte`) are **the same two fields `CAutoMaster::ProcessAnalogModel` itself
   uses as a 2-D index into a per-mode trim-gain table at `+0x3c0`** (`gain = table[+0xd8 * 8 + 0xe0 * 4]`,
   confirmed in the twenty-fourth-pass/`analog-model-chain.md` decompile of that function) — so `+0xd8` isn't a
   plain on/off switch, it's a mode index that both (a) picks Saturation vs Analog Model and (b) indexes a trim
   table shared with whichever circuit is active. This is a strong, concrete lead for §2e below, even though the
   table's actual contents weren't read this pass.
5. **Same shared EQ biquad filter can run either before (step 1) or after (end of step 4) the neural stage** —
   controlled by the `+0x591` flag. Not previously known; a genuine pre/post-EQ routing toggle around
   Saturation/Analog Model.
6. **Peak normalize** (if enabled): Analyze mode measures and records; Record mode replays the recorded dB.
7. **Waveshaper**: `wet = macroPos < 0.4 ? 0 : (macroPos - 0.4) / 0.6` — the exact, already-documented
   `WaveShaperDryWetFactorFromLoudnessMacroPos` formula, now pinned to this exact call site (`ProcessGenInplace`
   on the `CGen` object at `self+0x608`).
8. **Limiter**: input gain `= macroPos < 0.6 ? 2.8 : 2.8 + (macroPos - 0.6) * 1.3 / 0.4` — exactly the documented
   `LimiterInputGainFromLoudnessMacroPos` formula (the `1.3/0.4` fraction *is* the already-known 3.25 dB/unit
   slope, now seen as literal decompiled constants rather than a derived figure). Analyze mode calls
   `AutoAdjustLimiter(...)` and records the result; Record mode replays it (clamped to ±100 dB, falls back to
   silence below −100), then runs two separate `ProcessGenInplace` calls — one on a trim object (`self+0x5d8`),
   one on the actual limiter (`self+0x5e0`).

**The "automation replay" mechanism, confirmed structurally**: a shared float array at `self+0x358`, walked by a
running index at `self+0x34c` (reset to 0 once per call to `ProcessMainMasteringChain`). In Analyze mode, each
stage above that has an "Auto-" counterpart (compressor, peak normalize, limiter) **writes** its freshly-computed
gain into the next slot; in Record/normal mode, the same stages **read back** the exact value recorded during the
last analysis pass, in the same call order, instead of recomputing it live. This is the concrete mechanism behind
Logic's "Analyze once, then just replay" Mastering Assistant design — not previously documented at this level of
detail.

### 2b. `Coefficients(value, 3)` — confirmed again, origin still open

No new information on *where* the external caller gets the dB value it passes into
`CAnalogModelingMasteringChainFX::Coefficients(dB, 3)` (which, per `analog-model-chain.md`, stores
`10^(dB/20)` at `+0x164`) — that call happens from outside `CAutoMaster`'s own internal process chain (the
UI/automation parameter system), which this pass didn't trace. **Not determined.** Other `index` values besides 3:
not decompiled this pass either.

### 2c, 2d, 2e, 2f, 2g — not reached this pass

Time ran out before decompiling `CAutoMasterFX`'s constructor (2c), independently re-verifying `nativeAvailable`'s
sound-vs-speed effect (2d), reading the `+0x3c0` trim table's actual contents against the Smart Control "Character"
parameter (2e — though §2a above gives a concrete next step: that table is exactly 2 × 4 or similar small grid
indexed by `[+0xd8][+0xe0]`, worth reading directly in a follow-up), `AutoAdjustSpread`/`ProcessSpread`/
`AutoAdjustMBCompressor`/the 31-band Auto EQ (2f), or the other five compressor circuit tables (2g, Platinum/Class
A/Studio VCA/Studio FET/Opto/Classic VCA — `mixer.md` already has ClassA/DBX/FET/LP).

### Source

`CAutoMaster::ProcessAllDynamicCBs` at `0x5fc140` in `MADSP.x86_64` (REA + Ghidra 12.1.4, same MCP session as the
`analog-model-chain.md` pass — the ephemeral Ghidra session has to fully re-run its ~11-minute auto-analysis each
time `open_binary` is called fresh, since `close_binary`'s cleanup can't fully confirm itself on this sandbox
— `sysctl_errno_22` reading the spawned process's ownership token — so the binary was reopened mid-pass without
incident; REA's own analysis cache was not reused across the reopen). Callees cross-checked via
`procedure_callers`/`procedure_callees` to independently confirm `ProcessAnalogModel`'s only two call sites are
inside this function and `ProcessMainMasteringChain`.

### 2c. The 4 trailing metering stages — confirmed from `CAutoMasterFX`'s constructor

The constructor builds, in order, and stores at exactly the four offsets `analog-model-chain.md` flagged as
unidentified:

| Offset | Class | Constructed |
|---|---|---|
| `+0xb08` | `CLevelMeterFX` | always |
| `+0xb10` | `CLoudnessMeterFX` | always |
| `+0xb18` | `CCorrelationMeterFX` | always |
| `+0xb20` | `CSpectrumAnalyzerFX` | only if the channel-count parameter is > 0 |

So the four trailing `CBasicFX::Process` calls in `CAutoMasterFX::OnProcess` are **not further audio processing** —
they're the window's own metering chain (peak/RMS level, integrated/short-term loudness, stereo correlation,
spectrum display), run once per block purely to keep the UI meters current. `CSpectrumAnalyzerFX`'s own
constructor builds a Hann-style raised-cosine analysis window (`0.5 - 0.5·cos(2π·i/N)` pattern, read directly from
the decompiled loop) and a dB-reference scale via `10^x` / `pow` — standard spectrum-analyzer windowing, not
mastering DSP.

### 2f. Stereo spread, multiband compressor, and "Auto EQ" — partial results

**`CAutoMaster::ProcessSpread(floorLimit, ceilLimit, self, &spreadParam, sampleCount)` — fully decoded, genuine
Mid/Side processing**, confirmed via `AmplifyBy`'s own body (called with channel args `1,2`, which triggers an
explicit encode-to-M/S → scale the side → decode-back-to-L/R sequence, not a simple per-channel multiply):

- `spreadParam == -1.0` is a **distinct sentinel, not just an extreme of the continuous range**: full mono
  downmix — every channel is summed, divided by the channel count, and the resulting mono signal is copied back
  into every channel.
- `spreadParam >= 0` (widen): side-channel gain `= min(spreadParam × 8, ceilLimit)` (linear, via `AmplifyBy`, which
  internally converts through `10^(dB/20)` — so the ×8 multiplier is applied to a dB figure, not a raw linear
  gain); the parameter is clamped back down to `ceilLimit / 8` if it would exceed the ceiling. The mid channel
  gets a `spreadParam × -2.5` dB compensating trim.
- `spreadParam < 0` (narrow): side-channel gain `= spreadParam × 24` dB, floor-clamped against `floorLimit`
  (via the `× -6` relation seen in the clamp check); an extra `spreadParam × -6` dB pre-compensation is applied to
  one channel buffer before the `AmplifyBy` call, and the mid channel gets `spreadParam × 4` dB.

`AutoAdjustSpread` (Analyze-mode counterpart, presumably auto-picking `floorLimit`/`ceilLimit`) was not
decompiled this pass.

**`AutoAdjustMBCompressor`**: per-iteration mechanics are clear — copy buffers aside, run the multiband
compressor once, then loop testing gain in **+2 dB steps** (`0, 2, 4, 6, ...`), applying each via
`10^(dB/20)`+`vDSP_vsmul`, re-measuring through a shared loudness-analysis object (vtable `+0x28` then `+0x30`),
and accumulating `min(measured, 0.99 - measured)` into an error field. **The exact convergence/exit check could
not be recovered** — Ghidra's control-flow analysis produces an unresolvable `while(true)` for this function (the
same failure mode seen in `ProcessAllDynamicCBs`'s own inline calibration loop), meaning the real exit edge is
hidden inside a call Ghidra can't trace into. Not determined: the actual tolerance/iteration-count values (the
user's own earlier finding for the sibling `AutoAdjustLimiter` — "0.2 tolerance, 4 adjustments after the first
measurement" — is plausibly the same pattern here, but wasn't independently confirmed for the MB compressor).

**"Auto EQ" / dynamic EQ — a real surprise**: `CAutoMaster::AutoAdjustDynEQ` decompiles to a **literal empty
function body** (`{ return; }`) — Logic's "Dynamic EQ" machinery in this class appears to be unused/vestigial in
build 6306, not a working feature (consistent with other half-built things found in earlier passes, e.g.
`CPickupModelingFX`'s missing model files). `ProcessDynEQ` itself is live (it processes a `CGen` object at
`+0x600` via `ProcessGenInplace`) but nothing calls `AutoAdjustDynEQ` to actually tune it, going by what's visible
here — **this also corrects `analog-model-chain.md`'s guess that the `+0x600` object was "most likely
saturation/waveshaper"; it's the Dynamic EQ object instead.** The actual spectral-correction "Auto EQ" (31
third-octave bands, smoothing, ±dB limits, "amount") is a **separate** mechanism: `ProcessSpectralBalance` drives
its own, third biquad-chain object (`+0x580`, distinct from the `+0x588` chain used before/after the neural stage
and the `+0x568`/`+0x570` pair from `mixer.md`'s Match EQ work) — narrowed to the right object, but the actual
31-band coefficient-calculation code (where the correction curve itself gets computed from the measured spectrum)
wasn't traced this pass.

**The four Loudness-Macro gain formulas, now confirmed byte-exact** (previously documented from indirect evidence;
this pass read them straight from their own named functions and literal constant bytes):

| Function | Formula | Constants (confirmed from raw bytes) |
|---|---|---|
| `CompressorInputGainFromLoudnessMacroPos` | `p ≤ 0: p × 13.1`, `p > 0: p × 12.0` | `0x4151999a`=13.1, `0x41400000`=12.0 |
| `LimiterInputGainFromLoudnessMacroPos` | `p < 0.6: 2.8`, `p ≥ 0.6: 2.8 + (p−0.6)×1.3/0.4` | `0x40333333`=2.8 |
| `LimiterReleaseFromLoudnessMacroPos` | `p < 0.4: 2.0 ms`, `p ≥ 0.4: 2.0 + (p−0.4)/0.6 × 44.0 ms` | `0x40000000`=2.0 |
| `WaveShaperDryWetFactorFromLoudnessMacroPos` | `p < 0.4: wet=0`, `p ≥ 0.4: wet=(p−0.4)/0.6`, `dry=1−wet` | — |

### 3. Saturation's "drive" parameter — a second, classical path found

`CAutoMaster::SetSaturationAmnt(float amount)` stores `amount` directly on the Saturation object, **and**, when a
flag at a fixed offset is zero, separately computes `amount × 127.0` and calls `CPassiveEQFX::SetOutputDrive()` —
i.e. there appear to be **two independent saturation mechanisms**: the neural one (`CMLSaturation`, extensively
documented in `machine-learning-models.md`) and a classical one built on `CPassiveEQFX` with a 0–127-scaled drive
parameter, used when the flag selects the non-neural path. This is a new, concrete fact — but the neural path's
own `amount`→`cond`-tensor wiring (what the user actually asked about) wasn't traced this pass, and neither was
what that flag represents (plausibly the same `+0xd8`/`+0xe0` mode selection from 2a, not confirmed).

### Source (2c/2f/3 additions)

`CAutoMasterFX`'s real constructor (`__ZN13CAutoMasterFXC2Eflli`) at `0x740d90`; `ProcessSpread` at `0x5fba90`;
`AmplifyBy` at `0x5fdbd0`; `AutoAdjustMBCompressor` at `0x5fc840`; `AutoAdjustDynEQ` at `0x6097f0`; `ProcessDynEQ`
at `0x60a5d0`; `ProcessSpectralBalance` at `0x60a4f0`; the four Loudness-Macro formula functions at `0x607400`,
`0x607420`, `0x607460`, `0x6074a0` (constants cross-checked with `read_bytes` at their literal data addresses,
e.g. `0xd449c0`); `SetSaturationAmnt` at `0x606840`. Full `CAutoMaster` method list (130+ names) enumerated via
`search_procedures`, used to locate all of the above by name rather than guessing addresses.
