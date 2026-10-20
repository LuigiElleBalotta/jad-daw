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
| 4. Klopfgeist/metronome | **partial** | Live Project Settings > Metronome tab fully captured (full note/velocity table for both Audio Click/Klopfgeist and MIDI Click, all options). `CKnockVoice`'s own frequency table / synthesis internals not decompiled this pass — only the UI-level defaults are confirmed. |
| 5. Project Settings (all tabs) | **done** (live UI capture) | All 11 tabs captured live via screenshots/AX snapshots (General, Audio incl. Pan Law's full 7-item list, Recording incl. Count-in's full list, Smart Tempo, MIDI's 4 sub-tabs, Score's 8 sub-tabs, Movie, Synchronization's 4 sub-tabs, Metronome, Tuning, Assets) — see below. Not every single dropdown's full option list was opened (only the ones explicitly called out); defaults and visible control layout are captured for all of them. |
| 6. Missing dialogs (colors, I/O Labels, New Tracks, Global Tracks, overlapping recordings, Bounce, automation modes) | **partial** | Track/region color picker's grid layout captured live (6a). I/O Labels window's full column structure and channel-name scope captured live, including confirming busses run past 232 (6b). Create New Track dialog's 4 top-level categories identified from the Track menu (6c, not opened/screenshotted this pass). Global Tracks, overlapping-recordings behavior, Bounce dialogs, and the channel-strip automation-mode popup are still not done (the automation-mode popup specifically kept opening the adjacent Group popup instead of Read's own popup via simulated clicks — same general Logic-popup click unreliability noted elsewhere in this doc, not resolved this pass) — needs further live UI passes plus, for exact color hex values, an `Assets.car` search (the main app catalog searched in an earlier pass didn't have them under an obvious name). |
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

---

## Block 4 — Klopfgeist / metronome

Captured live from Project Settings > Metronome on a disposable test project (`Untitled 13.logicx`, never the
user's real `ProvaPianoforte.logicx`, which stayed open untouched in the background the whole session).

**Options:**

| Checkbox | State |
|---|---|
| Simple mode | on |
| Click while recording | on (parent, forces "Only during count-in" greyed off) |
| Only during count-in | off, disabled |
| Click while playing | on |
| Polyphonic clicks | on |

**Source: Audio Click (Klopfgeist) — note/velocity table**, Audio Click itself enabled:

| Row | Enabled | Note | Velocity |
|---|---|---|---|
| Bar | on | G5 | 127 |
| Group | on | D#5 | 119 |
| Beat | on | C5 | 112 |
| Division | off | C2 | 60 |

**Source: MIDI Click — channel/note/velocity table**, same 4 rows, independent of the Audio Click enable state:

| Row | Channel | Note | Velocity |
|---|---|---|---|
| Bar | 10 | C#1 | 112 |
| Group | 10 | C#1 | 88 |
| Beat | 10 | C#1 | 68 |
| Division | 10 | C#1 | 52 |

Tone and Volume sliders both at their default center position. MIDI Click's `Port:` popup shows `Off` (no MIDI
destination routed by default). Audio Click's `Output:` popup shows `1-2`.

**Not determined this pass**: `CKnockVoice`'s own synthesis internals (how `Tone` maps to the actual oscillator/
envelope, whether the audio click is a synthesized click or a sampled one) — this would need decompiling
`CKnockVoice` itself, not just reading the Project Settings UI defaults above.

### Source

Live UI capture via `mcp__macos-mcp__*` (Click + Snapshot with vision) on Project Settings > Metronome, disposable
test project.

---

## Block 5 — Project Settings, all 11 tabs

All captured live on the same disposable test project, via Click on each tab/sub-tab icon followed by a vision
Snapshot. Values shown are the **defaults of a freshly created project** — not verified against any other project.

### General

- Project Type: "Use musical grid" — on (the only Project Type option exposed in this build; its description:
  "The primary ruler displays bars and beats. Tempo information is recorded in audio files.")
- Opening Project: "Only load plug-ins needed for project playback" — on; "Reset record enable state of Software
  Instrument tracks" — on
- Automation: "Region automation takes priority over track automation" — on; "Use preset parameter value for
  regions without region automation (if no track automation is present)" — off

### Audio

**General sub-tab:**

| Control | Default |
|---|---|
| Sample Rate | 48 kHz |
| Spatial Audio | Off |
| Surround Format | 7.1.2 |
| Pan Law | **-3 dB Compensated** |
| Apply Pan Law compensation to stereo balancers | off |
| Apple Loops: High quality | on |

**Pan Law popup — full option list** (7 items, confirmed by opening the dropdown): `0 dB`, `-3 dB`,
`-3 dB Compensated` (default, checked), `-4.5 dB`, `-4.5 dB Compensated`, `-6 dB`, `-6 dB Compensated`.

**Channel Strips sub-tab:** "Automatic management of channel strip objects" — on (parent); "Automatic naming of
channel strip objects" — on (child, nested under the first).

### Recording

**General:** "When Beginning" radio group — `Count-in` selected (vs. `Pre-roll`), count-in length popup set to
`1 Bar`. Pre-roll's own seconds field greyed out at `0` while Count-in is selected. "Allow tempo change recording"
— off. "Automatically colorize takes" — off.

**Count-in popup — full option list** (confirmed by opening the dropdown): `None`, `1 Bar` (default, checked),
`2 Bars`, `3 Bars`, `4 Bars`, `5 Bars`, `6 Bars`, then a second group of note-value-style options: `1/4`, `2/4`,
`3/4`, `4/4`, `5/4`, `6/4`, `7/4`, `8/4`, `9/4` (list likely continues scrolled off-screen past `9/4` — not
confirmed where it ends).

**MIDI:** "MIDI data reduction" — on; "Automatically erase duplicates" — on.

**Audio Recording Path:** Folder set to `Project` (the project's own folder, via the `Project` quick-button next
to `Set…`).

### Smart Tempo

| Control | Default |
|---|---|
| Project Tempo Mode | **Keep Project Tempo** (3-option popup: `Keep Project Tempo` ✓ / `Adapt Project Tempo` / `Automatic`) |
| Set New Recordings To: | Off, "Trim start of new regions" unchecked |
| Set Imported Files To: | Off, "Trim start of new regions" unchecked |
| Create matching Editing group when creating Smart Tempo multitrack set | off |
| Export Tempo Resolution | Smoothed |
| Default Tempo Behavior (Free Tempo Recording) | Ask |

The Flex & Follow Region Setting popups ("Set New Recordings To:"/"Set Imported Files To:") did not reliably open
via simulated click in this pass (same general unreliability with Logic's native popups noted elsewhere in this
doc) — their full option lists are **not determined**; their current default value (`Off`) is confirmed from the
field itself. "Export Tempo Resolution" and "Default Tempo Behavior" popups' own full option lists were also not
opened — **not determined**, only their current defaults are confirmed.

### MIDI (4 sub-tabs)

**General:** "Send After Loading Project": Used instrument MIDI settings (off), All fader values (off), Control
change message 7/10 sets Volume fader/Pan knob for channel strips (off). MIDI Thru: "SysEx with MIDI Thru
function" (off); "Instrument Without MIDI Thru Function" popup = `Not Assigned`.

**Input Filter:** 7 independent checkboxes, all **off** by default: Notes, Program changes, Pitch Bend, Control
Changes, Aftertouch, Polyphonic Aftertouch, System Exclusive. (This is a filter list — checked = filtered out —
consistent with everything passing through by default.)

**Chase:** Notes (off, with 2 disabled sub-options "Sustained"/"In No Transpose instrument channel strips"),
Program Change (on), Pitch Bend (on), Control Changes (on, with 3 checked sub-items: `0-15`, `64-71`,
`All Other`), Aftertouch (on), Polyphonic Aftertouch (on), System Exclusive (on), Text Meta Events (off), "Chase
separate channels in All Channels instruments" (on), "Chase on cycle jump" (on, with a nested, unchecked "Notes"
sub-option), "Send full MIDI reset before chasing" (off).

**Clip Length:** 5 independent "...to zero/max" checkboxes, all **off** by default: Aftertouch to zero, Pitch Bend
to center position, Control 1 (Modulation) to zero, Control 2 (Breath Control) to zero, Control 4 (Foot Control)
to zero, Control 11 (Expression) to max.

### Score (8 sub-tabs — typesetting defaults, not audio DSP)

**Global:** Margins all 0.50 cm (Score and Part columns identical), Header Space 1.00 cm, Line Distance 0.00 cm,
Maximum Bars/Line 0 (no limit), Constant/Proportional/Slash Spacing all 16, Beaming Slant Factor 4 (Min 1 / Max
4), Default Pedal Position 144. Checkboxes: "Add bracket space" on, "Alternating margins" off, "Open single
staffs" off, "Justify last staff" on, "Alternate repeat symbols" off, "Hide muted notes" off, "Hide muted
regions" on, "Hide muted tracks" off, "Allow alias/loop editing" off, "Fill gaps" on. Breve symbol popup: `-`.
Symbol Font: Internal, "Font-specific spacing and positioning" on.

**Numbers & Names:** Page Numbers (on) — Horizontal Position `Alternating`, Vertical Position `Top`, Page Offset
0, font Times 12, "Prefix" off. Bar Numbers (on) — Horizontal Position `At Bar Line`, Vertical Position 32, Step
4, Start With 1, font Times 10; "Follow staff size"/"Count multiple rests"/"Show at double bars"/"Top/bottom
staff only"/"Hide bar numbers in linear view" all off. Instrument Names (on) — Position `Beside Staffs`, Align
`Left`, First Staff `Full Names`, Other Staffs `Short Names`, font Times 10, "Follow staff size" off.

**Tablature:** a built-in instrument-tuning table (Guitar standard E1-A1-D2-G2-B2-E3 plus Guitar D/G/lowG/D7/CG/CD
variants, Bass 4/5-C/5-B/6-C, more rows scrolled off-screen) each with its own Strings count, Assign mode
(`Pitch`), and per-string open-note names. Head Style: `Numbers only` (of 4 radio options: Numbers only / Numbers
in circle / Circle 1/1,1/2 notes only / Black and white (1/1,1/2 notes) circles), font Times 8. TAB Markings:
Palm Mute = "P.M.", Let Ring = "let ring", Slap = "slap", Line style `Solid`, font Times 8.

**Chords & Grids:** Chords — Root Font Times 14, Extension Font Times 10, "Follow staff size" off, Slash Note
Position `Normal`, Accidental Scale 0, Language `International (B, Bb, F#...)`, Alignment `Right-Aligned`. Grids
— font Times 8, Grid Scaling 75/100/125% (Reduced/Normal/Enlarged), Chord Scaling same 75/100/125%, "Show
Fingering" on for Normal and Enlarged (off for Reduced), Minimum Number of Frets 5, Barré style = curved-line icon
(of 2 options), Thumb style = circled-6 icon (of 2 options), "Left-Handed" off.

**Clefs & Signatures:** Clefs popup `Every Staff`, "Display warnings at line breaks" on, Smaller Clef Changes `-2`.
Signatures: Key Signatures popup `Every Staff` (warnings on, "Automatic key transposition" on, "Minimize
transposed accidentals" off, "Show naturals" on, "Show grace accidentals" off), Time Signatures popup `First
Staff on Page 1` (warnings on), Size `Regular`, "Hide bar lines" off. Octave Symbols: +8 = "8va", -8 = "8va
bassa", 0 = "Loco", +15 = "15ma", -15 = "15ma bassa", font Times 9, Line `Solid`.

**Layout:** Line Thickness — Stavelines/Stems/Leger Lines/Barlines/Tuplet Brackets/Text Boxes/Crescendi all `2`
except Endlines `12`. Slurs — Line Thickness 2, Horizontal/Vertical Distance/Curve Height/Curvature all 0. Ties —
Line Thickness 2, Horizontal Position 5, Vertical Position 6. Spacing — Bar Start 12, Bar End 16, Dot to Note 5,
Dot to Dot 3, Accidental to Note 0, Accidental to Accidental 0. Others — Stem Length 7. A `Defaults` reset button
is present.

**MIDI Meaning:** a 15-row table mapping score articulation symbols (staccato dot, accent marks, fermata, etc. —
glyphs, not transcribable as text) to Velocity (all `-`, i.e. unmodified) and Length (all `100 %`, i.e.
unmodified) — **every symbol is a no-op on playback by default**, confirming these are purely notational markings
unless explicitly configured.

**Colors:** Pitch Colors — Type radio `Diatonic` (vs `Chromatic`), 7 note-name swatches shown (one per diatonic
degree) plus a parallel row of 7 solid colors. Velocity Colors — 7 swatches labeled with velocity breakpoints `1,
17, 33, 49, 65, 81, 97, 113` (a velocity-to-color gradient in 16-unit steps). Note Color Options — "Accidentals",
"Dots", "Ties", "Rests" all off (so none of these sub-elements inherit the pitch/velocity coloring by default).
**User Palette** — a named 16-color list (`Color 0` through `Color 15`), each with its own solid swatch; this is
the same kind of user-color list likely shared with/parallel to the main app's track-color picker referenced in
Block 6a, though this one is specifically Score-context (note coloring), not confirmed to be the identical
48-color track palette.

### Movie

Position: Movie / Region timecode fields both `00:00:00:00.00` (no movie loaded). Region Borders: Start/End/
Length all `00:00:00:00.00`. Movie Volume slider at a default position, "Mute" button present. A "Movie
Settings…" button opens further (unexplored) options. No movie file was loaded in the test project, so these are
all empty/placeholder states, not meaningful defaults.

### Synchronization (4 sub-tabs)

**General:** Sync Mode popup `Internal` (self-clocked — disables "Transmit and Receive Start/Stop"). "Auto-enable
external sync and Tap Tempo" off. Frame Rate `24 fps`, "Auto detect format of MTC" on, Validate MTC popup
`Always`. Bar Position `1 1 1 1` plays at SMPTE `01:00:00:00.00` (i.e. the project's bar 1 is offset to start at
the 1-hour SMPTE mark, Logic's standard convention to avoid negative timecode). "Enable separate SMPTE view
offset" off.

**Audio:** Current Sync Status — MTC/Sample Rate/Deviation shown as live meters (not numeric defaults, just UI
widgets: MTC centered at 24.0 fps, Sample Rate centered at 48.0 kHz, Deviation centered at 0 ms, each with a
±10%/±40 ms range). Audio Sync Mode: Core Audio popup = `MTC Continuous`.

**MIDI:** A 10-row MIDI Clock destination table (Destination/Clock/Delay[ms]/PDC/MTC/MMC columns), every row
`Off` and unchecked by default (no MIDI sync destinations configured). Clock Mode popup = `Song — SPP at Play
Start and Stop/SPP/Continue at Cycle Jump`. Clock Start: "at position 1 1 1 1 with a pattern length of 1 bar(s)".
"Transmit MIDI Machine Control (MMC)" off, "Listen to MIDI Machine Control (MMC) input" off.

**Unitor:** General — Device Firmware "(not recognized)" (no Unitor hardware attached), SMPTE Mode radio `Read`
(vs `Generate`), "Refresh" unchecked, SMPTE Type radio `LTC` (vs `VITC`/`Off`), Freewheel slider+field = 1 Frame,
TV Format popup `PAL` (disabled, no hardware), VITC Line 1/2 popups both `Scan` (disabled). Visible Timecode
Display: Size `Small` (disabled), Style `White 1` (disabled), Horizontal/Vertical Position sliders both at a
default/centered value (disabled) — this entire sub-tab is effectively inert without Unitor hardware connected,
consistent with its name.

### Metronome

See Block 4 above — this is the same tab, documented there since it was the explicit ask.

### Tuning

Software Instrument Pitch: Tune slider at `0.0 cent (440.0 Hz)`. Software Instrument Tuning Table: 3-way radio —
`Equal Tempered` (selected default), `Fixed` (Type popup shows `Ethnic - Arabic` as an example entry even while
unselected, with its own Root Key popup and a "Copy to User" button), `User` (a 2-row × 6-column custom
cents-offset grid, all `0.000`, plus `Reset`/`Export` buttons). Stretch Upper/Lower sliders both `0%`. Root Key
popup `C`. Hermode Tuning (HMT) — unselected radio, Type popup `Classic (3/5-All)` (disabled while unselected),
Depth slider `100%` (disabled).

### Assets

A single "Project Package" checklist, all **off** by default except one: "Copy audio files into project" off,
"Convert audio file sample rate when importing" off (disabled, depends on the first), "Copy Sampler audio files
into project" off, **"Copy Alchemy audio files into project" — on** (the one default-enabled item), "Copy
Ultrabeat audio files into project" off, "Copy Space Designer impulse responses into project" off, "Copy movie
file into project" off, "Copy Apple Sound Library content into project" off.

### Source

All of Block 5 and Block 4's tables are live UI captures via `mcp__macos-mcp__*` (`Click` on each tab icon/
sub-tab, `Wait` where needed, `Snapshot` with vision) against a disposable test project (`Untitled 13.logicx`,
created via File > New, "Don't Close" chosen to keep the user's real `ProvaPianoforte.logicx` open untouched in
the background per the standing safety rule). No settings were changed or saved; every opened popup was closed
via Escape or by clicking its own already-selected item, never a different one.

---

## Block 6 — missing dialogs (partial)

### 6a. Track/region color picker

Opened via Track > Assign Track Color… A floating palette window titled "Color", with a single reset/undo arrow
button at top-left and a grid of solid color swatches: **4 rows × roughly 24 columns** (counted from swatch
spacing across the window — not a round 16 or 48 as earlier passes guessed). Columns sweep a continuous hue
wheel left to right (red → orange → yellow → green → cyan → blue → purple → magenta, wrapping back toward red at
the far right). Rows vary brightness/saturation top to bottom: row 1 is the brightest/most saturated "pure" set,
row 2 is a close match to row 1 but very slightly darker, rows 3-4 get progressively darker/more desaturated.
The currently assigned color for the selected track is shown with a white selection border around its swatch
(for the default "Audio 1" track, a blue swatch in row 2). No numeric/hex readout is shown anywhere in this UI —
**exact hex values for each swatch are not determined this pass**; would need either pixel-sampling the live
window (unreliable/approximate, not authoritative) or finding the real values in `Assets.car` or the app's own
resource files (an earlier pass's `Assets.car` search for colors came up empty under an obvious name — not
re-attempted this pass).

Separately, `Track > Assign Track Icon…` exists as a sibling command (not opened this pass) and `Track > Sort
Tracks by` offers: MIDI Channel, Audio Channel, Output Channel, Instrument Name, Track Name, Used/Unused,
Creation Date.

### 6b. I/O Labels window

Opened via Mix > I/O Labels… Columns: **Channel**, **Provided by Driver**, **User** (a toggle/radio per row,
selecting whether the User name or the driver-provided name is shown), **Long**, **Short**. A **Reset** button
(with its own dropdown arrow, suggesting a scoped-reset submenu not explored) sits above the table.

Row groups seen, in order: `Input 1`-`Input 6` (individual mono inputs; `Input 5`/`Input 6` show driver names
`Loop 1`/`Loop 2` — this test Mac's audio device exposes loopback inputs), `Output 1`-`Output 4`, stereo-paired
rows `Input 1-2`/`Input 3-4`/`Input 5-6`, `Output 1-2` (greyed/disabled — this is the actual Stereo Out pair, with
**User already enabled and both Long/Short pre-filled as "Stereo Output"**, the one row with real default
content), `Output 3-4`, then a very long run of **`Bus 1` through at least `Bus 232`** (confirmed by scrolling;
the exact upper bound — Logic Pro's documented internal bus limit is commonly cited as 256, consistent with what
was seen, but the exact final row was **not scrolled to and is not confirmed**). Every Bus row defaults to driver
name `-` (no name) and both Long/Short `-`, with User disabled until a custom name is typed. This confirms the
I/O Labels window is a single flat per-channel naming table (driver inputs/outputs plus all internal busses), not
split into separate per-category windows.

### 6c. Create New Track — only partially covered this pass

Not re-opened/screenshotted this pass (an earlier screenshot pass was done but lost to context compaction before
being written up). Confirmed again from `Track` menu that the relevant command is `Track > New Tracks…`
(⌥⌘N) — distinct from the single-track shortcuts `New Audio Track` (⌥⌘A), `New Software Instrument Track`
(⌥⌘S), `New Session Player SI Track…` (⌥⌘U), `New External MIDI Track` (⌥⌘X), and an `Other` submenu. The
dialog's own 4 top-level categories (MIDI, Pattern, Session Player, Audio) and their sub-options, as described in
an earlier pass, were **not re-verified or re-screenshotted this pass** — still open for a future pass.

### Not reached this pass

Global Tracks (Chord, Arrangement) content/editors, overlapping-recordings dialogs (Create Take Folder etc. with
Cycle On/Off), Bounce dialogs (Project/Section and Regions/Tracks in Place), and the channel-strip
automation-mode popup (clicking the "Read" button at the bottom of a channel strip repeatedly opened the
*adjacent* Group popup instead, both this pass and on retry — likely a coordinate-precision issue against the
narrower track-header layout once the Project Settings window was closed, not a fundamental click-reliability
problem like the SwiftUI sheets noted elsewhere in this doc; worth retrying with a fresh coordinate read in a
future pass).

### Source

Live UI capture via `mcp__macos-mcp__*` against the same disposable `Untitled 13.logicx` test project. Track >
Assign Track Color… and Mix > I/O Labels… opened via the application menu bar; I/O Labels scrolled with the
`Scroll` tool to confirm its row count extends past Bus 232.
