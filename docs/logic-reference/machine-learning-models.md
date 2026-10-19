# Logic Pro's shipped CoreML/BNNS models — a full catalog

Logic Pro 11.2 ships its machine-learning models as compiled `.mlmodelc` bundles inside
`MAMachineLearning.framework/Versions/A/Resources/`. Two of them were already identified earlier in this
reverse-engineering effort (Smart Tempo's beat/onset detectors — see `flex-algorithms.md`). This pass read every
other model's `metadata.json` (CoreML's own machine-readable schema: input/output tensor shapes, the op histogram,
the framework/tool versions used to export it) to catalog the rest, after `mixer.md`'s twentieth pass found that
`CAnalogModelingMasteringChainFX` (behind the Mastering Assistant's Clean/Valve/Punch/Transparent menu) loads
5 BNNS neural models via `std::vector<std::unique_ptr<IMil2BNNSModel>>`.

No official, human-readable name for any model below was found anywhere in the app bundle (checked: no matching
strings outside the compiled binaries/models themselves) — only the bundle's own internal codenames, which read as
arbitrary short tokens, not self-explanatory. Everything below is read directly from Apple's own CoreML metadata
and the shared conv/op patterns across models; nothing here is guessed from documentation, since none exists
locally.

## What's actually there

Beyond `OnsetDetectionCNN.mlmodelc` and `DownbeatTrackerCondMPS.mlmodelc` (Smart Tempo, already documented),
`Resources/` has **~20 more model names**, almost all shipped as **4 separate files per native sample rate**
(44.1 / 48 / 88.2 / 96 kHz — each a fully separate `.mlmodelc`, not one model with a rate parameter) to avoid
runtime resampling, named `<codename>_<rate>_<hash>_<number>.mlmodelc`. They fall into three clearly distinct
architecture families by inspecting each one's `metadata.json`:

### Family A — "neural audio effect", conditioned (`modelForStreaming_2048`)

**`FEO`, `FS`, `FSQ`, `FTO`, `MTC`, `MTM`, `RTRHT`, `RTRST`** (8 codenames × 4 rates = 32 files) plus
**`MKBO`, `MKGerm`, `MK2Germ`** (3 codenames, 8 files — `MKGerm` only ships 3 of the 4 rates, `MK2Germ` only ships
a single 96 kHz file, suggesting `MK2Germ` is a newer/improved retrain of `MKGerm` rolled out for one rate so far).

- **Inputs**: `audio` `[1,1,2048]` + `cond` `[1,1,1]` (a single conditioning scalar).
- **Output**: `[1,1,2048]` — *the same shape as the input*. This is a genuine **audio-to-audio** model, not a
  classifier.
- **Architecture**: a WaveNet-style gated dilated-convolution stack — repeated blocks of `Conv → Sigmoid → Mul →
  Abs → RealDiv` (a gated-activation + normalization pattern), ~14–15 repeated blocks, totaling either **78**
  conv ops (`FEO`/`FS`/`FSQ`/`FTO`/`MTC`/`MTM`/`RTRHT`/`RTRST`) or a shallower **73**-conv variant (`MKBO`/
  `MKGerm`/`MK2Germ`).
- **Provenance**: trained in **PyTorch** (`torch==2.0.1`–`2.1.0`, `TorchScript` source dialect), converted with
  `coremltools` 7.0b1 through 7.1.2 — i.e. genuinely recent (2023-era) ML work, not a long-standing legacy asset.

This is exactly the shape `CAnalogModelingMasteringChainFX`'s BNNS models should have (audio in, audio out,
a conditioning input matching a device-type selector) — a real-time neural audio effect, most plausibly circuit
modeling (amp/pedal/preamp emulation). **`MKGerm`/`MK2Germ`** ("Germanium") is the strongest naming hint: Germanium
transistors are a specific, well-known vintage fuzz/distortion-circuit material, which fits a neural *analog
circuit* model far better than any other interpretation. With 11 distinct codenames here (8 "big" + 3 "small") but
`CAnalogModelingMasteringChainFX` only loading 5 at a time, **most of these almost certainly serve more than one
feature** — which ones back the Mastering Assistant's Analog Model menu specifically, and which (if any) back
something else (e.g. the stock amp/pedal plugins), wasn't established this pass; that needs either live-instance
inspection or tracing references from a different binary, not attempted.

### Family B — "neural audio analyzer", unconditioned (`modelForStreaming`, 1024-sample blocks)

**`G`, `G_SO`, `S`, `S_SO`** (scalar output) and **`T`, `TSO`** (audio-shaped output) — 6 codenames × 4 rates =
24 files.

- **Input**: `audio` `[1,1,1,1024]` only — **no conditioning input**, unlike Family A.
- **Output**: either a single scalar `[]` (`G`, `G_SO`, `S`, `S_SO`) or the same `[1,1,1,1024]` audio shape back
  (`T`, `TSO`).
- **Architecture**: the same gated-conv building block as Family A (`Conv/Sigmoid/Mul/Abs/RealDiv`), but shallower
  (46 conv ops) and on a smaller 1024-sample block.
- **Provenance**: also PyTorch → coremltools, slightly older tool versions (7.0b1/7.0b2) than most of Family A.

Family B is architecturally related to Family A (same gated-conv block) but structurally distinct: no
conditioning input, smaller blocks, and two different output shapes split across names (scalar vs. audio). The
`_SO`-suffixed names (`G_SO`, `S_SO`) share their un-suffixed counterpart's exact shape and op count, suggesting
paired models (e.g. an analysis/detector variant alongside a base one) — but since both members of each pair
output a scalar with no distinguishing input, this pairing's actual purpose (what differs between `G` and `G_SO`
if not shape) wasn't established from metadata alone. What feature(s) consume Family B at all is unidentified —
nothing ties it specifically to the Analog Model menu or to anything else traced so far in this session.

### Family C — small conventional CNN, likely pitch-related (`PT`, `RTPT`, `RTPTM`)

Unlike every other model here, these ship as **one file each, no per-sample-rate variants**.

- **`PT`**: input `[1,1,16224,1]` (a ~368 ms window at 44.1 kHz) → output a single scalar `[]`. 6 conv + 5 batchnorm
  + 5 relu + 3 maxpool + 1 sigmoid — a conventional CNN classifier, not the gated-conv family above.
- **`RTPTM`**: identical architecture to `PT`, but on a much shorter `[1,1,1,1024]` window (~23 ms) — a
  lower-latency/real-time variant of the same scalar classifier ("RT" = real-time, "M" possibly "mono" or
  "monitor").
- **`RTPT`**: input `[1,1,1024]`, output **`[360]`** — a 1-layer (`Permute`) model, far too small to be a full
  network on its own; it's most plausibly a *reshape/preprocessing stub* feeding a bigger graph, or a thin wrapper
  Core ML exported separately. The **360-element output is notable**: 360 is the exact bin count used by the
  well-known CREPE pitch estimator (20-cent bins spanning roughly six octaves) — a strong (but not proven, since
  no label confirms it) hint that this family does **pitch detection/tracking** in real time, independent of
  project sample rate (consistent with having no per-rate variants).

## For jad-daw

Nothing here is a reusable algorithm the way the DSP decompiles elsewhere in `mixer.md`/`flex-algorithms.md` are —
these are opaque trained-weight files, not formulas. The transferable facts are architectural: Apple's real-time
neural audio effects in this app run as a **WaveNet-style gated dilated-conv stack operating on small blocks
(1024–2048 samples) with an optional single conditioning scalar**, shipped as **separate per-sample-rate exports**
rather than one resampled model — a sensible pattern to copy if jad-daw ever adds its own learned (not hand-coded)
audio effect.

Source: `metadata.json` for every `.mlmodelc` in
`/Applications/Logic Pro.app/Contents/Frameworks/MAMachineLearning.framework/Versions/A/Resources/`, read directly
(no decompilation needed — this is Apple's own exported model schema, machine-readable, not binary analysis).
Cross-referenced against `CAnalogModelingMasteringChainFX`'s constructor (`mixer.md`, twentieth pass) for the
`IMil2BNNSModel`/BNNS connection.
