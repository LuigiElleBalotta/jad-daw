# Mac pass 3 — status index

One section per requested block. Status legend: **done** / **partial** / **not started**. For anything not fully
resolved, the "what's missing" line says what a future pass would need.

| Block | Status | What's missing (if any) |
|---|---|---|
| 1. ONNX vs CoreML numeric verification | **done** (for everything that can run through the public API) | The 6 Analog Model + 10 Saturation-family networks (317 of 321 bundle/block-size combinations) can't be numerically compared at all — Core ML's public `prediction(from:)` API itself refuses to run them (see below), independent of ONNX correctness. A real conversion bug was found and confirmed in `OnsetDetectionCNN` (0.226 max-abs-err, flagged as suspect in `ml-conversion.md` already) — needs a fix in `espresso2onnx.py`'s weight-layout/flatten-order handling, not a Mac-side task. |
| 2a. `ProcessAllDynamicCBs` stage order | **done** | Exact exit condition of the Analyze-mode MB-compressor calibration loop not traced (lives inside a vtable call Ghidra couldn't resolve). |
| 2b. `Coefficients(value,3)` origin/range | **partial** | The field it writes (+0x164) and the dB→linear conversion are confirmed (`analog-model-chain.md`). This pass confirmed via `procedure_callers` that it has **zero resolved callers inside `MADSP.x86_64` itself** — the UI/automation parameter system that calls it lives in a separate framework binary, not this one. Where the external caller sources the dB value remains not determined; would need opening that other binary (not identified/thinned this session). |
| 2c. The 4 trailing `CBasicFX::Process` slots | **done** | — identified directly in `CAutoMasterFX`'s own constructor: `CLevelMeterFX`, `CLoudnessMeterFX`, `CCorrelationMeterFX`, `CSpectrumAnalyzerFX` — the window's metering chain, not further audio processing. |
| 2d. `nativeAvailable` meaning | **partial** | Confirmed it's read from an Apple-Silicon-ML-capability check; whether it changes *sound* vs only *speed* not independently re-verified this pass (carried over from earlier passes' finding). |
| 2e. Clean/Valve/Punch/Transparent parameter defaults/ranges | **partial** | Found the "Character" Smart Control parameter (ID 10), the Mastering Assistant's `#default.pst` flat-parameter block, and the concrete `+0xd8`/`+0xe0`-indexed trim table at `+0x3c0` that `ProcessAnalogModel` reads from (2a); the table's actual stored values weren't read this pass. |
| 2f. Stereo spread / multiband compressor / Auto EQ algorithms | **partial** | Stereo spread (`ProcessSpread`) fully decoded: real Mid/Side matrix processing, exact formula and a `-1.0` sentinel for full mono. `AutoAdjustMBCompressor`'s per-iteration mechanics are clear (2 dB steps, shared loudness-meter object) but its exact convergence/exit check wasn't recoverable (same unresolved-control-flow pattern as 2a). Found that `CAutoMaster::AutoAdjustDynEQ` is an **empty no-op stub** — "Dynamic EQ" looks unused/vestigial in this build. The 31-band spectral-correction EQ ("Auto EQ") was narrowed to a specific biquad-chain object (`ProcessSpectralBalance`, filter at `+0x580`, distinct from the other two EQ chains) but its coefficient-calculation code (the actual 31-band correction math) wasn't traced. The 4 Loudness-Macro gain formulas are now confirmed byte-exact (not just structurally) straight from their own named functions and literal constant tables. |
| 2g. Compressor circuit tables (Platinum, Class A, Studio VCA/FET, Opto, Classic VCA) | **partial** | Found the real class hierarchy: `CCompressorBase` with 6 derived circuits — `CCompressorClassA`, `CCompressorDBX`, `CCompressorFET`, `CCompressorLP` (already in `mixer.md`), plus `CCompressorOpto` and `CCompressorSSL` (new this pass, with real decompiled `GainRedCurve`/`Coefficients` constants below). `CCompressorSSL` is almost certainly "Classic VCA" (SSL G-bus-style). Exact UI-name↔class mapping for "Platinum"/"Studio VCA"/"Studio FET" is not confirmed — `CPlatinumVerb` exists but is a reverb, not a compressor circuit, so "Platinum" compressor likely maps to the generic `CCompressor`/`CCompressorBase` itself (not independently confirmed). |
| 3. Analysis-network preprocessing (Onset/Downbeat/Pitch/Saturation) | **partial** | Got real, confirmed CoreML input/output shapes for all of these (Block 1). Found `CAutoMaster::SetSaturationAmnt` — confirms a **second, classical** saturation path exists alongside the neural one (`CPassiveEQFX::SetOutputDrive`, amount parameter scaled ×127, 0-127 range) for when the neural model isn't used; the neural path's drive→`cond` wiring itself, and all the mel/beat/pitch preprocessing code, remain untraced. |
| 4. Klopfgeist/metronome | **partial** | Live Project Settings > Metronome tab fully captured (full note/velocity table for both Audio Click/Klopfgeist and MIDI Click, all options). `CKnockVoice` found and confirmed structurally (`SetCutoff`/`SetQ`/`CalcCoeffs` — the click is a synthesized resonant-filter voice, not a sample); the exact `CalcCoeffs()` formula itself wasn't pulled before the REA session moved on to Block 8. |
| 5. Project Settings (all tabs) | **done** (live UI capture) | All 11 tabs captured live via screenshots/AX snapshots (General, Audio incl. Pan Law's full 7-item list, Recording incl. Count-in's full list, Smart Tempo, MIDI's 4 sub-tabs, Score's 8 sub-tabs, Movie, Synchronization's 4 sub-tabs, Metronome, Tuning, Assets) — see below. Not every single dropdown's full option list was opened (only the ones explicitly called out); defaults and visible control layout are captured for all of them. |
| 6. Missing dialogs (colors, I/O Labels, New Tracks, Global Tracks, overlapping recordings, Bounce, automation modes) | **done** (live UI capture) | All 7 sub-items (6a-6g) captured live: track color grid, I/O Labels, Create New Track (all categories), Global Tracks, Overlapping Track Recordings defaults (found in `Logic Pro > Settings > Recording…`, a global app preference, not a project one — MIDI merges, Audio creates a Take Folder, regardless of Cycle mode), Bounce dialog, automation-mode popup. Only the exact color hex values for 6a (would need an `Assets.car` search) and the full option lists behind a few popups (blocked by a recurring native-popup click-registration issue in this app, not left untried) remain open. |
| 7. Smart Tempo/Flex quality comparison | **partial (unblocked)** | Loop Browser drag-and-drop never worked (genuine tooling limitation, see below), but `File > Import > Audio File…` did — a real Apple Loop (`Ambition Beat.caf`, 80 BPM) was successfully placed on a track. Key finding: importing it changed project tempo 120→80 instantly, and the Smart Tempo Editor explicitly refused to open on it ("Apple Loops contain tempo information that cannot be modified by this editor") — **Apple Loops bypass Smart Tempo's detection engine entirely, using embedded file metadata instead**. Testing the *actual* detection/warping algorithm (the original ask) needs a non-Apple-Loop, untagged audio file run through Smart Tempo's "Detect" — not attempted, time ran out. |
| 8. Match EQ / Channel EQ / Limiter / Pan / Fader curves | **partial** | `CLimiter`'s real gain-smoothing algorithm and full `Coefficients` parameter table decompiled; `CNoisegateFX::Coefficients`'s real ranges/formulas decompiled; **Channel EQ's real default 8-band table (frequencies, gains, Q/slope) captured live from the plugin UI itself** — no decompilation needed. Pan/Fader: `MAMixer.framework` was thinned and opened, found to contain extensive Pan-knob code, but it's **all GUI/drawing**, not DSP — the real pan-law/fader-gain math lives in a third, not-yet-identified framework. Match EQ wasn't re-touched (already in `mixer.md`). |

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

**`CKnockVoice` found and structurally confirmed** (real class in `MADSP.x86_64`): `SetLevel(float)`,
`SetCutoff(float)`, `SetQ(float)`, `CalcCoeffs()`, `Start()`/`SoftStart()`, `Process()`, `GetSignal()` — this
proves the Klopfgeist click **is a synthesized, filtered signal, not a sampled one**: each voice is built around
a resonant filter with its own tunable cutoff frequency and Q, recalculated via `CalcCoeffs()`, exactly matching
the UI's single `Tone` slider (which almost certainly drives `SetCutoff` directly) and consistent with
`Polyphonic clicks` needing multiple independent `CKnockVoice` instances active at once. **Not determined**: the
exact formula inside `CalcCoeffs()` (the literal cutoff/Q-to-filter-coefficient math) — the REA session was
reused for Block 8's Pan/Fader search before this could be pulled, and reopening `MADSP.x86_64` costs another
~11-minute Ghidra re-analysis; left for a future pass.

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

### 6c. Create New Track — now fully captured

Opened via `Track > New Tracks…` (⌥⌘N) — distinct from the single-track shortcuts `New Audio Track` (⌥⌘A),
`New Software Instrument Track` (⌥⌘S), `New Session Player SI Track…` (⌥⌘U), `New External MIDI Track`
(⌥⌘X), and an `Other` submenu. The dialog has exactly **4 top-level categories**, each with its own sub-options
and its own "Details" panel below:

| Category | Sub-options | Details panel fields (defaults) |
|---|---|---|
| **MIDI** | Software Instrument, External MIDI | *Software Instrument*: Instrument popup (default **Vintage B3 Organ**), Multi-timbral (off, 4 parts), Open Library (on), Audio Output (Stereo Output). *External MIDI*: "Use External Instrument plug-in" (on), MIDI Destination popup (**Off - All**), Audio Input (**No Input**), Audio Output (Stereo Output). |
| **Pattern** | Software Instrument, External MIDI | *Software Instrument*: Instrument popup (default **Default Patch**), Multi-timbral (off), Open Library (on), **Open Editor** (on, Pattern-specific extra checkbox vs. plain MIDI), Audio Output (Stereo Output). |
| **Session Player** | Drummer, Bass Player, Keyboard Player | *Drummer*: Drummer Style popup (default **Hard Rock**), Open Library (off), Open Editor (on). *Bass Player*: Bass Player Style popup (default **Retro Rock**), "Use Default Chord Progression for New Regions" (on), Open Library (off), Open Editor (on). *Keyboard Player*: Keyboard Player Style popup (default **Freely**), same chord-progression/Library/Editor checkboxes as Bass Player. All three share Audio Output (Stereo Output) and a Device readout (Scarlett 4i4 USB, this Mac's interface). |
| **Audio** | Mic or Line, Guitar or Bass | *Mic or Line*: Audio Input popup (default **Input 3**), Ascending (off), Load Default Patch (off), Open Library (off), Input Monitoring (on), Record Enable (on). *Guitar or Bass*: Audio Input popup (default **Input 1** — a different default input than Mic or Line), Load Default Patch (**on**, unlike Mic or Line), Open Library (**on**), Input Monitoring (on), Record Enable (**off**, unlike Mic or Line). Both share Audio Output (Stereo Output). |

A shared **"Number of tracks to create"** field (default `1`) and **Cancel**/**Create** buttons sit at the bottom
of the dialog regardless of category. Cancel was used to close it without creating any track.

### 6d. Global Tracks — structure confirmed

`Track > Global Tracks` submenu lists **8 track types**: Arrangement, Marker, Movie, Signature, Chord, Tempo,
Beat Mapping, plus a "Show Single Global Track" option (a combined/compact view) and "Configure Global Tracks…"
(⌥G, a settings dialog not opened this pass). `Show Global Tracks` (G) reveals a **default subset of 5**
stacked lanes — Arrangement, Marker, Tempo, Signature, Chord — each with its own `+` add button; **Movie** and
**Beat Mapping** are not part of the default set and need their own individual `Show Movie Track`/`Show Beat
Mapping Track` toggles. Observed on the fresh disposable project: Tempo lane shows a graph ruler (100/120/140
BPM gridlines, current value 120 as a horizontal line); Signature lane shows `4/4` and key `C` at bar 1;
Arrangement, Marker and Chord lanes are all empty by default. Actually creating a chord symbol or marker (to see
their editors) was not attempted this pass, to avoid non-reversible edits beyond simple navigation; the lane
structure and the full track-type list are the concrete findings here.

### 6f. Bounce dialogs — Project or Section captured

`File > Bounce` has 4 items: `Regions in Place…` (⌃B, greyed out — needs a selected region), `Track in Place…`
(⌃⌘B), `And Replace All Tracks…` (no selected shortcut shown), `Project or Section…` (⌘B). Clicking
`Project or Section…` from the menu did not visibly trigger (same click-delivery issue seen elsewhere in this
doc for certain Logic menu items/sheets); using the `⌘B` keyboard shortcut directly worked and opened the real
dialog, titled **"Bounce 'Output 1-2'"** (named after the Stereo Out channel strip):

| Field | Default |
|---|---|
| Destination (multi-select list) | `Uncompressed` highlighted/selected, **MP3` checked**, `M4A: AAC` unchecked, `Burn to CD / DVD` unchecked — so MP3 export is on by default alongside the main file |
| File Type | WAVE |
| Bit Depth | 16-bit |
| Sample Rate | 48 kHz (greyed — locked to project sample rate) |
| Format | Interleaved |
| Dithering | None |
| Start / End | `1 1 1 1` / `129 1 1 1` (spans the whole visible timeline of this empty test project) |
| Mode | Offline |
| Bounce 2nd Cycle Pass | off, disabled (needs Cycle mode on) |
| Include Audio Tail | **on** |
| Include Tempo Information | off |
| Normalize | Off |
| Surround Bounce / Add to Project / Add to Music | all off |

Bottom status line: "Requires 49,6 MB of free disk space (Time 4:16)" — a live estimate. Closed via `Escape`
(Cancel's own click also failed to register via simulated click, consistent with the same dialog-click-delivery
issue; Escape worked immediately) — no file was actually bounced.

### 6e. Overlapping Track Recordings — found via `Logic Pro > Settings > Recording…` (resolved, defaults only)

Not a project setting — a global app preference. `Logic Pro > Settings > Recording…` has 3 sections:
**Audio Recording** (File Type: `AIFF`, Bit Depth: `24-bit`), **MIDI Recording** (Auto Record Enable:
`The Focused Track`), and the explicitly-requested **Overlapping Track Recordings** table — 3 rows (`Cycle Off`,
`Cycle On`, `Replace`) × 2 columns (`MIDI`, `Audio`):

| Scenario | MIDI default | Audio default |
|---|---|---|
| Cycle Off | `Merge` | `Create Take Folder` |
| Cycle On | `Merge` | `Create Take Folder` |
| Replace | `Region Erase` (single control, not split by MIDI/Audio) | — |

This directly confirms Logic's real default behavior without needing an actual recording pass: **recording over
an existing MIDI region merges by default** (regardless of Cycle mode), while **recording over an existing audio
region creates a Take Folder by default** (also regardless of Cycle mode) — the Cycle On/Off switch does not
change the default outcome, only the available alternatives behind each popup. The popups' own full option
lists (presumably also `Create New Track`, `Overwrite`/`Don't Create Take Folder`, etc.) could not be opened —
same click-registration issue documented elsewhere for Logic's native popups; only the default values could be
read directly off the closed controls. A `Recording Project Settings…` button links back to the already-captured
Project Settings > Recording tab (Block 5).

This resolves the "needs an actual recording pass" blocker from the previous version of this doc: the
**default** behavior is now confirmed from the preference pane itself, without needing to record anything live.

### 6g. Channel-strip automation-mode popup — resolved

The popup directly below the "Group" button (its real hit area starts a few pixels lower than it visually
appears — this is why earlier clicks kept re-opening the Group popup instead) contains, top to bottom: **Read**
(checked by default), **Touch**, **Latch**, **Write** — a 4-item mutually exclusive mode group, confirming
**there is no "Off" automation mode** in this build (consistent with Logic's documented behavior: "Read" is the
closest thing to off, it just means automation plays back but nothing is written). Below a separator: a single
checked toggle **"Record Automation with Audio Regions"**. Below a second separator: **Trim** and **Relative** —
these read as a second, independent pair (not visually grouped with Read/Touch/Latch/Write, consistent with the
idea that Trim/Relative are modifiers layered on top of Touch/Latch/Write rather than modes of their own, matching
the Logic documentation's description of "Touch/Latch/Write with Trim/Relative behavior"). Closed by re-clicking
the already-selected "Read" item, so no setting changed.

### Not reached this pass

Global Tracks (Chord, Arrangement) content/editors, overlapping-recordings dialogs (Create Take Folder etc. with
Cycle On/Off), and Bounce dialogs (Project/Section and Regions/Tracks in Place).

### A note on session safety this pass

Track > Assign Track Color… opens a floating "Color" palette window that proved unusually persistent: its own
close button did not respond to simulated clicks, and `Escape`/`Cmd+W` sent while intending to target it
instead landed on whichever window actually had OS input focus at that moment. One `Cmd+W` was sent while focus
had silently shifted to **this session's own Terminal window**, which responded with "Vuoi terminare
l'esecuzione dei processi di questa finestra?" (a prompt to kill the Terminal's processes — including this very
Claude Code session). It was cancelled immediately ("Annulla") and no processes were terminated; the user's real
`ProvaPianoforte.logicx` project was never touched by any of this. From that point on, blind keyboard shortcuts
were avoided in favor of explicit app-switching (clicking the Dock/Finder to bring Logic Pro back to the
front) and dragging the stray Color window out of the way (via `Move` with `drag=true`) instead of trying to
close it. Flagging this here as a concrete, reproducible gotcha for any future live-UI pass on this app: prefer
mouse-only interaction once a window shows itself resistant to closing, rather than retrying keyboard shortcuts
that can be delivered to the wrong (frontmost) application.

### Source

Live UI capture via `mcp__macos-mcp__*` against the same disposable `Untitled 13.logicx` test project. Track >
Assign Track Color… and Mix > I/O Labels… opened via the application menu bar; I/O Labels scrolled with the
`Scroll` tool to confirm its row count extends past Bus 232; Track > New Tracks… walked through all 4
categories × their sub-options via `Click`, closed with Cancel (no track created).

---

## Block 7 — Smart Tempo/Flex quality comparison (blocked)

`View > Show Loop Browser` (⌥O) opened Logic's real Apple Loops library live — 20158 items indexed, with
`Instrument`/`Genre`/`Descriptors`/favorites filter tabs, `Scale`/`Signature`/text-search fields, and a results
table (`Name`, `Beats`, favorite-heart, `Tempo`, `Key` columns; a green note icon marks a plain audio loop, a
blue "loop-with-waveform" icon marks an Apple-Loop-with-tempo-metadata type — both seen in the list). Selecting
a row (`12 Bar Blues Bass`, 16 beats, 80 BPM, key E) previews/plays it.

**Could not get a loop onto a track.** Every attempt at a simulated drag from the Loop Browser list onto the
Tracks area (a single-shot `Move` drag, a two-step `Move`-then-`Move-with-drag` sequence to different drop
targets, a right-click for a context menu, and the Loop Browser's own bottom-left tool button) left the browser
in its original state — no track was created, no region appeared. This reads as a genuine limitation of
simulating macOS's native drag-and-drop (an `NSDraggingSession`) via synthesized mouse events, not a bad
coordinate: Logic's own non-drag interactions (menu clicks, text fields, regular buttons) worked fine in the
same window. The actual Smart Tempo/Flex-quality test this block asked for — importing a real Apple Loop and
judging the quality of its tempo detection/warping — could not be performed as a result.

**A second, more serious near-miss happened while hunting for the Loop Browser toggle** before switching to the
View-menu approach above: a blind click on an unfamiliar toolbar icon brought the user's real
`ProvaPianoforte.logicx` project window to the front (not the disposable `Untitled 13.logicx`). No content in
that project was touched — no track, region, fader, or menu item was clicked inside it, only the `Window` menu
was used to confirm which windows were open — and it was switched away from immediately via `Window > Untitled
13.logicx - Tracks` (itself fiddly: the window-list menu item didn't respond to simulated clicks on the first
several tries and only worked via `Window > Cycle Through Windows` instead). The real project was not saved,
closed, or modified in any way. This reinforces the same lesson as the Block 6 safety note: unfamiliar toolbar
icons in this app are higher-risk than menu items, since a couple of them turn out to toggle which project
window is frontmost rather than opening a panel within the current one.

### Unblocked: `File > Import > Audio File…` works where Loop Browser drag-and-drop doesn't

A later continuation of this pass found a working path: `File > Import > Audio File…` opens a real native "Open
File" dialog (not a drag source), and selecting an actual Apple Loop file from disk
(`/Library/Audio/Apple Loops/Apple/01 Hip Hop/Ambition Beat.caf`, a 12-second, 16-beat, 80 BPM loop) and clicking
Open (the click itself didn't register — same dialog-click-delivery issue documented elsewhere in this doc; the
`Return` key, with Logic Pro confirmed focused, worked immediately) placed the region directly onto the existing
`Audio 1` track at bar 1.

**The key finding**: importing the Apple Loop **instantly and exactly** changed the project tempo from 120 to
80 BPM — not an estimate, not a gradual conform, an immediate jump to the file's own declared tempo. Opening
`Window > Open Smart Tempo Editor` on this region surfaced a direct, explicit confirmation of why: the editor
refused to open its usual beat-marker view and instead showed the literal message **"Apple Loops contain tempo
information that cannot be modified by this editor."** This is a concrete, previously-undocumented fact: **Apple
Loops do not go through Smart Tempo's audio-analysis/transient-detection engine at all** — their tempo comes
straight from embedded file metadata (the `.caf`'s own tags), which Logic reads and trusts directly. The
"quality of tempo detection" this block asked to evaluate is consequently not a meaningful question for Apple
Loops specifically — there is no detection step to judge, only metadata lookup. Actually exercising Smart
Tempo's real detection/warping algorithm would need a **non-Apple-Loop** audio file (an plain recording with no
embedded tempo tag) run through "Detect"/the Smart Tempo multitrack set — not attempted in this pass, since the
project's own `Untitled 13.logicx` only had the Apple-Loop-sourced tempo change to work with by this point, and
time ran out before preparing an untagged test file.

### Source

`mcp__macos-mcp__*` live UI capture, `Untitled 13.logicx` test project, Loop Browser opened via
`View > Show Loop Browser`; the successful import via `File > Import > Audio File…` against a real file on disk,
`Window > Open Smart Tempo Editor` for the "cannot be modified" message.

---

## Block 2g — compressor circuit tables (partial)

### The real class hierarchy

`search_procedures` for `^__ZN\d+CCompressor` reveals the actual structure: a shared `CCompressorBase` (envelope
smoothing, auto-gain, limiter sub-stage, clip mode) with 6 concrete circuit subclasses, each implementing its own
`GainRedCurve`/`Coefficients`: `CCompressorClassA`, `CCompressorDBX`, `CCompressorFET`, `CCompressorLP` (all
already partly covered in `mixer.md` from an earlier pass), plus two **not previously documented**:
`CCompressorOpto` and `CCompressorSSL`. A separate, unrelated `CCompressor` class (no `Base`/circuit suffix) is
the multi-channel **link/sidechain-routing engine** used by the Compressor plugin's channel-linking feature, not
a circuit model — its own `Coefficients` is a 28-case parameter dispatcher (link table, EQ pre/post routing,
distortion calc, etc.), not curve math. `CPlatinumVerb` also exists in this binary but is a **reverb**, not a
compressor circuit, so the UI name "Platinum" for a compressor circuit most likely routes to the generic
`CCompressor`/`CCompressorBase` path itself rather than a same-named class — **not independently confirmed**.

### `CCompressorOpto` — real T4-cell/LA-2A-style emulation, now decoded

`GainRedCurve` (`0x9e740`) implements a genuine nonlinear opto-cell model, 30× oversampled per output sample
(`lVar6 = 0x1e`):

1. One-pole envelope: `coeff = exp(-2300 / (2 × sampleRate))` (same "2300" RC-time-constant numerator seen
   throughout MADSP's dynamics processors — confirmed here as a shared internal utility, not circuit-specific).
2. A **cubic soft-knee** region: for envelope values in `(0.6151, 1.1925]`, adds
   `(envelope − 0.6151)³ − threshold` to the raw `envelope − 1.0` gain-reduction candidate, taking the max of
   the two — the two literal constants `0.6151`/`1.1925` are the knee boundaries of the T4-cell response curve.
3. Final per-sample gain: `g = (x^−1.2 × 37.15) / (x^−1.2 × 37.15 + 100.0)`, where `x` is the clamped
   (≥0.001) knee output — `37.15`/`100.0` are the photocell-response curve constants, `−1.2` the shaping
   exponent (`0xbf99999a`).
4. A **second, independent "Limit" sub-path** (only active if a flag at `+0xac` is set, itself toggled by
   `Coefficients` parameter index `0x19 > 0`): computes `1/x` clamped to 1.0 via SIMD `divps`/`blendvps`, scaled
   by a ratio parameter (index `0x18`, `10^(−dB/20)`) and makeup gain, then takes the `min()` against the
   opto-curve's own output — i.e. **Opto can run a hard parallel brick-wall limiter on top of its own nonlinear
   compression**, confirmed structurally, not just inferred from the UI.

`Coefficients(value, index)` parameter map (indices confirmed by their exact effect, matching `GainRedCurve`'s
own offset reads):

| Index | Parameter | Formula / range |
|---|---|---|
| 2 | Threshold | `10^(−dB/20)`, stored at the curve's own threshold slot |
| 3 | Ratio → knee shape | `max(p×0.445−1.3, p×0.3325 + (p−1)^0.7^−2×0.2695 − 0.602)`, floored at 0 — a genuine two-branch empirical curve, not a simple ratio |
| 5, 6, 7 | Attack/program-dependent timing | `1 − exp(−2300/(time×sampleRate))`, time floor/ceiling clamps at 0.2–5.0 |
| 8 | Makeup gain | `10^(dB/20)` |
| 9 | (unlabeled, "crest"-like) | `(1 − p) × 0.057022` |
| 0xb | SC/limiter-related gain | `10^(dB×0.05)`, gated by `p ≤ 0` |
| 0xd | Sidechain filter type select | indexes a 4-entry constant+pointer table (`DAT_00d4b2d0`/`DAT_00d53920`) — a 4-way HPF-type selector |
| 0x18 | Limit-mode ratio | `10^(−dB/20)` |
| 0x19 | Limit-mode enable | boolean, `p > 0` |

### `CCompressorSSL` — almost certainly "Classic VCA" (SSL bus-style), now decoded

Shares the **identical** `Coefficients` parameter-index map with `CCompressorOpto` (same case numbers, same
formulas byte-for-byte) — confirming both circuits sit behind one common parameter-dispatch interface. Its
`GainRedCurve` (`0x9d030`) differs from Opto in the actual curve math:

- Same cubic-knee shaping (`0.6151`/`1.1925` thresholds, same cubic formula) as Opto.
- **Dual envelope time constants** instead of Opto's single one: a fast coefficient `exp(−2300/(2×sampleRate))`
  and a slow one `exp(−2300/(6×sampleRate))`, chosen per-sample by comparing the new target against the
  running envelope (fast when the target is above/growing, slow otherwise) — this is the classic SSL G-Bus
  "auto-release"/program-dependent behavior, now confirmed as real, present code rather than marketing copy.
- **10× oversampling** per output sample in real-time mode (`lVar6 = 10`, vs Opto's 30×), 900× in single-sample
  offline mode.
- Final gain via `10^(−envelope)` (plain log-domain), not Opto's `pow(x,−1.2)` cell-response curve — a cleaner,
  more "VCA-like" response, consistent with SSL's VCA (not opto-cell) gain element.
- The same secondary hard-limit sub-path as Opto (identical structure, same offsets).

### Not reached this pass

Exact UI-name↔class confirmation for "Platinum"/"Studio VCA"/"Studio FET" (plausible but unconfirmed: Platinum =
plain `CCompressor`/digital path, Studio VCA = `CCompressorDBX`, Studio FET = `CCompressorFET` — both already in
`mixer.md`). The sidechain-filter 4-entry constant table at `DAT_00d4b2d0`/`DAT_00d53920` wasn't read directly
(would give the 4 HPF type names/frequencies).

### Source

`search_procedures` regex `^__ZN\d+CCompressor` against `MADSP.x86_64` (REA + Ghidra 12.1.4, same thinned
x86_64 slice used throughout this doc). `CCompressorOpto::GainRedCurve` (`0x9e740`), `::Coefficients`
(`0x9e310`), `CCompressorSSL::GainRedCurve` (`0x9d030`), `::Coefficients` (`0x9cc00`) all decompiled directly.

---

## Block 8 — Limiter and Noise Gate tables (partial; Channel EQ/Pan/Fader not found in this binary)

### `CLimiter` — real gain-smoothing algorithm and parameter table

`CalcAmp` (`0xd17e0`) is the core per-sample gain-smoothing state machine — a lookahead-free limiter design:

- Running gain `g` (never exceeds 1.0 / 0 dBFS — confirmed hard ceiling).
- **Release** (target gain ≥ current): linear ramp, `g += (target − g) × releaseCoeff + 1e-10` (the `1e-10`
  epsilon avoids float denormals during long silence), resets an internal "hold" counter.
- **Attack** (target gain < current): on a *new* gain-reduction event, computes a program-dependent attack
  multiplier via `pow(target/g, exponent)` (the exponent is itself a stored coefficient, so attack "shape" is
  tunable, not fixed); while still within that same reduction event (held below the last threshold), it just
  reapplies the same cached multiplier each sample rather than recomputing `pow()` — an optimization that also
  smooths rapid successive reductions into one continuous curve.

`Coefficients(eCoeffIndex, value)` (`0xd0bc0`) parameter map:

| Index | Parameter | Range | Formula |
|---|---|---|---|
| 0 | Input Gain | ±100 dB | `10^(dB/20)` |
| 1 | Lookahead | 0–10 ms | buffer-size samples = `time × sampleRate × 0.001`; **sample-rate-tiered extra delay**: +48 samples below 50 kHz, +24 samples 50–100 kHz, +0 samples ≥100 kHz (only applied if a separate "enable" condition from index 6/7 holds) |
| 2 | True Peak (bool) | 0/1 | boolean flag, gates oversampled peak detection elsewhere |
| 3 | Release | 0–2000 ms | main release coefficient `1 − exp(−2300/(ms×sampleRate))`; **also** computes a separate dither/noise-floor constant via a fast log2 bit-trick (constants `−0.33985`, `−0.9150375`, `−6.0206003` — the last is `20×log10(2)`, confirming this is a dB-domain noise-floor-vs-release-time curve) |
| 4 | (linked to same state as 3) | 0–2000 | sets a related ratio field, recomputed from index 3's stored sample rate |
| 5 | Output Ceiling | ±100 dB | `10^(dB/20)` |
| 6 | Lookahead enable (bool) | — | combines with index 7's channel/oversample count to decide the sample-rate-tiered extra delay (same table as index 1) |
| 7 | Channel/oversample count | int | paired with index 6, same delay-table selection |

The sample-rate-tiered lookahead delay table (+48/+24/+0 samples at <50 kHz / 50–100 kHz / ≥100 kHz) is a new,
concrete, previously-undocumented fact — it means the Limiter's actual look-ahead latency is **not** a pure
function of the Lookahead ms setting; it also silently grows at lower sample rates to keep inter-sample-peak
detection accurate.

### `CNoisegateFX` — real threshold/reduction/attack/release ranges

`Coefficients(threshold, _, reduction, attack, lookahead, holdOrRelease, self, keyFilterIndex)` (`0x134a10`):

| Parameter | Range | Formula |
|---|---|---|
| Threshold | ≤ 0 dB (values below −100 dB snap to a fixed −5.0 floor constant) | `10^(dB/20)` |
| Reduction | ±120 dB | sign-dependent: stores both an "open" and "closed" gain multiplier (`10^(±dB/20)`) — confirms the gate can both attenuate (gate/duck, negative) and boost (expand upward, positive) |
| Attack | 0–1000 ms | `ceil(ms × sampleRate × 0.001) + 1` samples |
| Hold/Release | 0–5000 ms | same shared `1 − exp(−2300/(ms×sampleRate))` formula as every other MADSP dynamics processor |
| Lookahead | clamped, ms-to-sample via `×0.001×sampleRate` | feeds a buffered-delay object shared with a paired level-meter object |

The `2300` RC-time-constant numerator is now confirmed present, byte-identical, across **four** independent
dynamics processors this project — `CAutoMaster`'s Loudness-Macro compressor (earlier pass), `CCompressorOpto`/
`CCompressorSSL`, `CLimiter`, and `CNoisegateFX` — strong evidence it's a shared, standardized internal utility
constant in MADSP's envelope-follower code, not something tuned per-circuit.

### Channel EQ — real default band table, captured live from the plugin UI

Static search for `CPan`/`CFader`/EQ-specific classes in `MADSP.x86_64` found nothing (see below) — but opening
the actual **Channel EQ** plugin UI on the disposable test project's track gave the real default values directly,
no decompilation needed. **8 bands**, left to right:

| Band | Type | Frequency | Gain | Q / Slope |
|---|---|---|---|---|
| 1 | Low Cut | 20.0 Hz | — | 12 dB/Oct, Q 0.71 |
| 2 | Low Shelf | 75.0 Hz | 0.0 dB | Q 1.00 |
| 3 | Parametric (low-mid) | 100 Hz | 0.0 dB | Q 0.60 |
| 4 | Parametric (mid) | 250 Hz | 0.0 dB | Q 0.30 |
| 5 | Parametric (high-mid) | 1040 Hz | 0.0 dB | Q 0.41 |
| 6 | Parametric (presence) | 2500 Hz | 0.0 dB | Q 0.20 |
| 7 | High Shelf | 7500 Hz | 0.0 dB | Q 1.00 |
| 8 | High Cut | 20000 Hz | — | 24 dB/Oct, Q 0.71 |

Plus an overall output **Gain**: 0.0 dB. Both cut filters default to the same **Q 0.71** (the standard
Butterworth-flat value, `1/√2`), while the default low-cut slope is 12 dB/Oct and the default high-cut slope is
24 dB/Oct — not symmetric. The 4 middle parametric/shelf bands have distinct, deliberately-chosen default Q
values (1.00, 0.60, 0.30, 0.41, 0.20) rather than a single uniform default, suggesting Apple tuned each band's
starting "musicality" individually rather than using one generic Q for all. Other visible controls: `Analyzer`
(POST position), `Q-Couple`, `HQ` (high-quality/oversampled mode), `Processing: Stereo`.

### Pan, Fader — not found in `MADSP.x86_64`

Searched for `^__ZN\d+CPan` (no matches) and `^__ZN\d+C(ChannelEQ|...)` patterns — no `CPan`/`CFader`/
`CChannelEQ`-named classes exist in `MADSP.x86_64` (Channel EQ's own DSP class wasn't located by name either,
hence the live-UI capture above instead of decompiled constants). `MADSP.x86_64` appears to be specifically the
**plugin DSP framework** (Compressor, Limiter, Noise Gate, reverbs, EQs-as-plugins, the Mastering Assistant chain
already covered); channel-strip-level primitives like Pan and Fader live elsewhere.

**`MAMixer.framework` was thinned and opened this pass** (`/tmp/rea-bins/MAMixer.x86_64`) as the obvious
candidate — it does contain extensive Pan-related code (`MACartesianPannerNode`, `MAStereoPannerCapNode`,
`MAPlanarPannerNode`, a Swift `MixerPanViewModel` with a `panValue: Float` property, etc.) — but every single one
of these is **GUI/drawing code** (`drawRect:inContext:`, `mouseDown:`, hit-testing, color/highlight state,
accessibility) for the pan-knob widget itself, in Objective-C and Swift. Searching specifically for
value-setting/gain-computation functions (`panValue`/`setPan`/`panLaw`/`calcPan` patterns) turned up only the
same GUI layer — **no actual gain-computation DSP function was found in `MAMixer.framework`**. The real pan-law
math (and almost certainly the fader-gain/meter-ballistics math too) lives in a third framework not yet
identified — plausibly inlined into the main Logic Pro executable itself, or a framework whose name doesn't
obviously suggest "mixer" (several dozen frameworks exist; not all were checked by name this pass — see
`find "/Applications/Logic Pro.app/Contents/Frameworks" -maxdepth 1 -iname "*.framework"` for the full list).
**Not determined**: would need a broader framework-by-framework search in a future pass. Match EQ itself was not
revisited (already documented in `mixer.md`).

### Source

`search_procedures` literal/regex against `MADSP.x86_64`. `CLimiter::CalcAmp` (`0xd17e0`), `::Coefficients`
(`0xd0bc0`), `CNoisegateFX::Coefficients` (`0x134a10`) all decompiled directly via the same REA/Ghidra session
used for Block 2g above.
