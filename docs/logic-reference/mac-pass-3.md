# Mac pass 3 — status index

One section per requested block. Status legend: **done** / **partial** / **not started**. For anything not fully
resolved, the "what's missing" line says what a future pass would need.

| Block | Status | What's missing (if any) |
|---|---|---|
| 1. ONNX vs CoreML numeric verification | **partial** | `tools/ml/coreml_reference.swift`, `tools/ml/compare_with_coreml.py`, and `jad-daw-private/onnx/` don't exist on this machine (checked the whole repo and filesystem) — the actual ONNX-vs-CoreML diff table could not be produced. What *could* be done without those files: fixed the CoreML load failure (same rename trick, now confirmed to generalize beyond Analog Model), found all 6 Analog Model models' real MIL output is full audio (not scalar), found `prediction(from:)` itself still fails for every state-threaded model tried. Needs: the missing tools/onnx files copied onto this machine, or this pass run on the machine that has them.
| 2a. `ProcessAllDynamicCBs` stage order | **done** | — |
| 2b. `Coefficients(value,3)` origin/range | **done** | Other `index` values beyond 3: not determined (would need the rest of the function decompiled). |
| 2c. The 4 trailing `CBasicFX::Process` slots | **not started** | Needs `CAutoMasterFX`'s constructor decompiled to identify the 4 object types at `+0xb08/0xb10/0xb18/0xb20`. |
| 2d. `nativeAvailable` meaning | **partial** | Confirmed it's read from an Apple-Silicon-ML-capability check; whether it changes *sound* vs only *speed* not independently re-verified this pass (carried over from earlier passes' finding). |
| 2e. Clean/Valve/Punch/Transparent parameter defaults/ranges | **partial** | Found the "Character" Smart Control parameter (ID 10) and the Mastering Assistant's `#default.pst` flat-parameter block; could not reliably align the flat array's byte offset to parameter ID without guessing (this project's own earlier docs already flag this general correspondence as unverified). |
| 2f. Stereo spread / multiband compressor / Auto EQ algorithms | **not started** | Needs dedicated decompilation of `AutoAdjustSpread`, `ProcessSpread`, `AutoAdjustMBCompressor`, and the 31-band Auto EQ path. |
| 2g. Compressor circuit tables (Platinum, Class A, Studio VCA/FET, Opto, Classic VCA) | **not started** | Builds on existing `mixer.md` findings (ClassA, DBX, FET, LP curves already partly decompiled there); the other circuits (Platinum, Studio VCA/FET, Opto, Classic VCA) not yet touched. |
| 3. Analysis-network preprocessing (Onset/Downbeat/Pitch/Saturation) | **partial** | Got real, confirmed CoreML input/output shapes for all of these (see Block 1) — a solid anchor — but the actual *preprocessing code* (mel filterbank construction, hop size, log compression, beat-decoding logic, pitch-bin-to-Hz mapping, drive-to-cond mapping) was not traced this pass. |
| 4. Klopfgeist/metronome | **not started** | Needs both decompilation (`CKnockVoice`'s frequency table) and live Project Settings > Metronome UI inspection. |
| 5. Project Settings (remaining tabs) | **not started** | Needs live UI inspection across ~10 tabs. |
| 6. Missing dialogs (colors, I/O Labels, New Tracks, Global Tracks, overlapping recordings, Bounce, automation modes) | **not started** | Needs live UI inspection/screenshots and, for colors, an `Assets.car` search specifically for track/UI color swatches (the main app catalog searched in an earlier pass didn't have them under an obvious name). |
| 7. Smart Tempo/Flex quality comparison | **not started** | Needs a throwaway test project with real Apple Loops, built and played in the live app. |
| 8. Match EQ / Channel EQ / Limiter / Pan / Fader curves | **not started** | Match EQ's pipeline architecture is already documented in `mixer.md`; the exact per-band Q/slope/curve constants for Channel EQ, Limiter/Adaptive Limiter/Noise Gate tables, and pan/fader taper formulas are not yet decompiled. |

This pass covered Block 1 (partially, given the missing files) and Block 2a/2b (fully), 2d/2e (partially) as the
highest-priority, most tractable items in the time available. Everything below "not started" needs its own
dedicated pass — most of blocks 4-7 specifically need live interaction with the running app (screenshots, UI
inspection, test-project playback), not just static analysis, which is a different and slower kind of work than
what's in this file so far.

---

## Block 1 — ONNX vs Core ML numeric verification

### Blocker

`tools/ml/coreml_reference.swift`, `tools/ml/compare_with_coreml.py`, and `jad-daw-private/onnx/` are not present
anywhere on this machine (checked the full repo tree and a filesystem-wide search for `jad-daw-private`). The
actual max-abs-err / max-rel-err comparison table the block asks for **could not be produced** — it needs either
those files copied here, or this specific sub-task run on whichever machine already has them.

### What was done instead: fixing and generalizing the Core ML load failure

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

Root cause of the `[]` vs `[1,1,1,1024]` metadata inconsistency: **not determined** — plausibly a shape-inference
bug in the early-beta `coremltools 7.0b1` used for `G`/`G_SO`/`S`/`S_SO` specifically (their
`userDefinedMetadata` shows `7.0b1`; `T`/`TSO`, which got the shape right, use the same `7.0b1` too per earlier
notes, so tool-version alone doesn't fully explain it — genuinely not determined).

### Input/output shapes confirmed via Core ML for the other requested models (load-only, no ONNX diff possible)

These loaded directly, no rename needed — real, Core-ML-reported shapes, useful as a preprocessing anchor for
Block 3:

| Model | Inputs | Outputs |
|---|---|---|
| `OnsetDetectionCNN` | `input1` `[3, 80, 15]` | `output1` `[1]` |
| `DownbeatTrackerCondMPS` | `input_melspec` `[1, 4096, 131]` | `output_bpb` `[1,4096,13]`, `output_beats` `[1,4096,1]`, `output_tempo` `[1,4096,251]`, `output_beats_no_downbeats` `[1,4096,1]`, `output_downbeats` `[1,4096,1]` |
| `PT` | `input_1` `[1, 1, 16224, 1]` | `Identity` `[]` (scalar) |
| `RTPT` | `input_1` `[1, 1, 1024]` | `Identity` `[360]` |
| `RTPTM` | `input_1` `[1, 1, 1024, 1]` | `Identity` `[]` (scalar — not cross-checked against its own raw `.mil` return statement this pass, so unlike `G`/`S` this one **might** be a genuine scalar; not determined) |

`DownbeatTrackerCondMPS`'s **`input_melspec [1, 4096, 131]`** is a direct, concrete confirmation of two numbers
from your hypothesis: a **4096-frame window** and **131 mel bands** — both now Core-ML-confirmed facts, not
guesses. The 5-headed output (`bpb`/`tempo`/`beats`/`beats_no_downbeats`/`downbeats`) is new information: this is
a multi-task network, not a single beat-activation signal — `output_tempo`'s 251-wide last axis is almost
certainly a tempo histogram/classification over ~251 BPM bins, and `output_bpb`'s 13-wide axis most likely spans
a small set of beats-per-bar hypotheses (e.g. 2 through 14). Exact bin-to-BPM/meter mapping: **not determined**
this pass (would need the preprocessing/postprocessing code, Block 3's actual remaining ask).

### Source

CoreML load/predict tests via throwaway Swift scripts (`swift <script>.swift`, no Xcode project) against copies
under `/tmp` (never against `/Applications` originals) of files from
`../logic-pro-reference/all-ml-models/` and `../logic-pro-reference/analog-model-ml-models/`. Raw `.mil` text read
directly from the same files (`modelstreaming_transformed.mil` for Analog Model, `model_2048_transformed.mil` for
`RTRST`).
