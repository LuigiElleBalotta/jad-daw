# Logic Pro's shipped CoreML/BNNS models — a full catalog

Logic Pro 11.2 ships its machine-learning models as compiled `.mlmodelc` bundles inside
`MAMachineLearning.framework/Versions/A/Resources/`. Two of them were already identified earlier in this
reverse-engineering effort (Smart Tempo's beat/onset detectors — see `flex-algorithms.md`). This pass read every
other model's `metadata.json` (CoreML's own machine-readable schema: input/output tensor shapes, the op histogram,
the framework/tool versions used to export it) to catalog the rest, after `mixer.md`'s twentieth pass found that
`CAnalogModelingMasteringChainFX` (behind the Mastering Assistant's Clean/Valve/Punch/Transparent menu) loads
BNNS neural models via `std::vector<std::unique_ptr<IMil2BNNSModel>>` (the twenty-second pass below pins the exact
count at **6**, correcting the "5" read off the constructor's argument count alone).

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

This shape (audio in, audio out, a conditioning input) originally looked exactly like what
`CAnalogModelingMasteringChainFX`'s BNNS models should have, and **`MKGerm`/`MK2Germ`** ("Germanium" — a specific,
well-known vintage fuzz/distortion-circuit material) still reads as a strong hint toward analog circuit modeling of
some kind. **Correction (twenty-third pass): this guess was wrong.** Tracing `CAnalogModelingMasteringChainFX`'s
actual model-loading code (below) proves it consumes none of Family A's 11 codenames at all — it uses all six
Family B codenames instead.

**Resolved (twenty-fourth pass): Family A's real consumer is `CMLSaturation`** — Logic's stock neural-network-based
Saturation/distortion effect. All 11 Family A codenames map to `RealtimeFXModel` enum values **0 through 9** (the
lowest range, directly below Analog Model's 10-15), confirmed via the same headless-Ghidra force-disassembly
technique used for Analog Model: a first pass (`DumpSwitchCases3.java`) force-disassembled/decompiled all 10
switch-case bodies, revealing that most call their own shared helper functions; a second pass (`DumpHelpers2.java`)
decompiled those 7 helpers directly. Full, confirmed mapping:

| `RealtimeFXModel` enum | Codename | Resolved via |
|---|---|---|
| 0 | `RTRST` | helper at `0x95a0` |
| 1 | `RTRHT` | helper at `0x9710` |
| 2 | `MTC` | helper at `0x9880` |
| 3 | `MTM` | helper at `0x99f0` |
| 4 | `FEO` | inlined in the switch case itself |
| 5 | `FTO` | helper at `0x9c00` |
| 6 | `FS` | helper at `0x9d70` |
| 7 | `FSQ` | helper at `0x9ee0` |
| 8 | `MKBO` | inlined in the switch case itself |
| 9 | `MKGerm`/`MK2Germ` | helper at `0xa050` (the same helper already decompiled in the twenty-second pass) |

This exactly and completely accounts for all 11 Family A codenames — no leftover, no ambiguity. So the "Germanium"
naming hint was right about the *material* but not the *feature*: Logic's Saturation plugin, not the Analog Model
mastering chain, is what applies a trained neural Germanium-fuzz (and seven sibling) model to the signal.

## Tracing the loader: `MAMachineLearning.framework` and the exact model count (twenty-second pass)

Following up on exactly which models back the Analog Model menu — the user chose to trace a second binary rather
than use the live UI for this. `MADSP.framework`'s `CAnalogModelingMasteringChainFX` doesn't hardcode model names
itself: its real constructor (`CAnalogModelingMasteringChainFXC2Efml`, the one `CAutoMaster::SetupTeardownAMMC`
actually calls) makes **six** calls to `MAMachineLearning::ModelFactory::createModel(RealtimeFXModel, float, Compile)`
with enum values **10, 11, 12, 13, 14, 15** (not five, as a literal argument count on the other constructor overload
suggested) — then passes the six resulting models into the 6-vector constructor. **This is the missing link**: six
models requested, exactly matching the six `AMMCDeviceType` values found earlier (`SetAnalogModel`'s
native/fallback pair for each of Valve/Punch/Transparent) — so each of the three menu entries really does draw on
two separately-loaded models (one "native" Apple Silicon / ML path, one "fallback"), not a shared model reused
across the pair.

Opened `MAMachineLearning.x86_64` to find `ModelFactory::createModel` itself: it's a big `switch` on the
`RealtimeFXModel` enum via a computed jump table that Ghidra's static analysis couldn't fully recover ("too many
branches"), so the exact enum→codename assignment for all six cases wasn't pinned down byte-for-byte. What *was*
recovered: the concrete `Mil2BNNSModel` class (the real implementation behind `IMil2BNNSModel`) takes a model-name
string in its constructor, appends it to `MAMachineLearning::getResourcePath()` (the framework's own
`Resources/` folder — confirming models load by name from exactly where the `.mlmodelc` files sit), and — for at
least the `MKGerm`/`MK2Germ` case — a dedicated helper function (not inlined, unlike the shorter-named cases)
picks the exact resource path by comparing the live sample rate against **44100.0 / 48000.0 / 88200.0 / 96000.0 /
176400.0** (exact float equality checks, decompiled in full):

| Sample rate | Resource loaded |
|---|---|
| 44100 Hz | `MKGerm_44_2km99ux63a_0568.mlmodelc` |
| 48000 Hz | `MKGerm_48_vi7hgbg8ef_0833.mlmodelc` |
| 88200 Hz | `MKGerm_88_dh6c3ygvgd_0130.mlmodelc` |
| 96000 Hz | `MK2Germ_96_9ker9d6t67_0185.mlmodelc` (not a plain `MKGerm_96` — confirms `MK2Germ` is the 96 kHz member of this family, not a separate feature) |
| 176400 Hz | `MKGerm88_uy2kv6feen_0560.mlmodelc` (a distinct, no-underscore-named file — the 88 kHz-era model reused at 4×44.1 kHz rather than a dedicated 176.4 kHz retrain) |
| anything else | falls back to `MK2Germ_96` — the newest/highest-quality member, not the nearest rate |

Searching the binary for every catalogued codename's exact resource-path string also turned up **two previously
unknown files** neither `find` nor the earlier metadata sweep had reason to surface as distinct: a fifth `MKBO`
variant (`MKBO_88_erdk8f5b7k_0467.mlmodelc`, alongside the four already catalogued) and the `MKGerm88` file above.
The `MKBO_88_erdk8f5b7k_0467` string has **zero references anywhere in the binary** found by string/xref search
tools — this pass's first-pass conclusion was that it's dead/orphaned data. **Correction (twenty-fourth pass): that
was a tooling artifact, not a code-reachability fact.** Fully decompiling `CMLSaturation`'s enum-8 (`MKBO`) case
(above) shows it does reference `MKBO_88_erdk8f5b7k_0467` in its 176.4 kHz-fallback slot, via the exact same
inline-SSE-immediate-string-construction pattern used for every other short codename — which is precisely why
`search_strings`/xref tools don't see it as a "reference": the filename bytes are built with `movups` immediates
inside the function body, not stored as an addressable string constant. The code path **is** reachable; the file
genuinely does **not exist on disk** (confirmed via `find`), so the accurate description is "a reachable code path
whose target file is missing from the shipped bundle," not "dead code." Every other codename's resource strings
(`FEO`, `FS`, `FSQ`, `FTO`, `MTC`, `MTM`, `RTRHT`, `RTRST`) were confirmed present the same inline way, and their
exact per-case sample-rate thresholds are now individually decompiled too (see the twenty-fourth-pass table above).

**Resolved (twenty-third pass): the exact enum→codename mapping, fully confirmed — and it overturns the "Family A"
assumption above.** REA's headless Ghidra session couldn't disassemble the other 5 switch-case bodies (Ghidra's own
jump-table recovery had failed, and no available tool could force a function into existence at an arbitrary,
statically-unreached address). The GUI was tried next, per the user's suggestion, but turned out to be too
unreliable for this specific job — Java/Swing dialog buttons don't respond to synthetic mouse clicks or keystrokes
through the automation layer available here at all (confirmed repeatedly; only AppleScript's native `click`
accessibility action worked, and only on buttons, not menus or text input reliably). The approach that actually
worked: **Ghidra's own headless analyzer with a custom post-analysis script** (`analyzeHeadless ... -postScript`),
which forces disassembly + function creation + decompilation at arbitrary addresses entirely programmatically, no
UI involved. This is the right tool for "make Ghidra analyze code it didn't find on its own" and sidesteps the GUI
automation problem completely.

The script force-disassembled and decompiled all 6 switch targets directly. Two of them (codenames short enough to
inline-construct) decompiled cleanly in one pass; the other two turned out to call their own shared helper
functions (the same pattern already seen for `MKGerm`/`MK2Germ`), which a second script pass decompiled by address.
**Full, confirmed result:**

| `RealtimeFXModel` enum | Codename | Output shape |
|---|---|---|
| 10 | `TSO` | audio (same shape as input) |
| 11 | `T` | audio (same shape as input) |
| 12 | `G_SO` | scalar |
| 13 | `G` | scalar |
| 14 | `S_SO` | scalar |
| 15 | `S` | scalar |

**This is not Family A at all — it's all six Family B codenames**, the "neural audio analyzer" family from the
section below, not the 11 "neural audio effect" (`modelForStreaming_2048`, conditioned) codenames this section
originally guessed at. That guess is now superseded: whatever Family A's 11 codenames serve, it isn't
`CAnalogModelingMasteringChainFX`. The sample-rate-selection logic (now confirmed identical across every family
checked — `TSO`, `T`, `G`/`G_SO`, `S`/`S_SO`, and `MKGerm`/`MK2Germ`) is exact-float comparison against
44100.0/48000.0/88200.0/96000.0/176400.0, where 176400 Hz reuses the 88.2 kHz-trained model (not a dedicated
retrain) and any other rate falls back to the 96 kHz model.

The pairing is clean and telling: three enum pairs (10/11, 12/13, 14/15), each a `{_SO suffix, plain}` pair except
`TSO`/`T` (no underscore, but still the same two-model pairing pattern) — one pair per `AMMCDeviceType` slot
(Valve/Punch/Transparent), matching the native/fallback structure found in `SetAnalogModel`. Only the `T`/`TSO`
pair outputs **audio** — `G`/`G_SO` and `S`/`S_SO` both output a **single scalar** per block. That means two of
the three "circuit" slots in this engine aren't neural audio generators at all; they're neural **control-signal
extractors** (most plausibly a per-block gain/envelope/dynamics value) feeding some other stage of the chain —
only one of the three device types does direct neural audio-to-audio modeling. Which specific enum pair is
Valve/Punch/Transparent (the `AMMCDeviceType`+10 offset hypothesis would give Valve={10,11}=`{TSO,T}`,
Punch={12,13}=`{G_SO,G}`, Transparent={14,15}=`{S_SO,S}`, based on the constructor's own creation order) is
plausible but still not independently verified — the codename↔enum mapping above is proven; the
enum↔`AMMCDeviceType` offset is still an inference, not confirmed against the `SetAnalogModel` switch bodies
directly.

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
if not shape) wasn't established from metadata alone. **Consumer confirmed (twenty-third pass)**: all six Family B
codenames are loaded by `CAnalogModelingMasteringChainFX`, behind the Mastering Assistant's Analog Model menu — see
above.

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

**Consumer confirmed (twenty-fourth pass)**: `MADSP.x86_64`'s external-symbol import list names
`WrappedNeuralPitchTrackerRT` — exposing `Predict(const float*, vector<float>&)` plus constructor/destructor — as a
dedicated wrapper class that bypasses `MAMachineLearning::ModelFactory::createModel` entirely (unlike every other
family above, it doesn't go through the shared enum-indexed factory at all, which is presumably why Family C ships
with no per-sample-rate variants: a dedicated class rather than the generic per-rate-file factory pattern). This
confirms the CREPE-style pitch-bin guess: `PT`/`RTPT`/`RTPTM` back a real-time neural pitch tracker.

## `CMLSaturation`, `CPickupModelingFX`, and `CAnalogModelingProfiling` — the other three `ModelFactory` callers (twenty-fourth pass)

Besides `CAnalogModelingMasteringChainFX` (enum 10-15, Family B), three more classes in `MADSP.x86_64` call
`MAMachineLearning::ModelFactory::createModel`, found by searching for callers of `0x81c0` across the binary:

- **`CMLSaturation`** (enum 0-9) — Logic's stock neural-network Saturation/distortion plugin. Fully resolved; see
  the Family A table above. This directly answers which feature consumes all 11 Family A codenames.
- **`CPickupModelingFX`** / **`CPickupModelingShell`** (enum 16-23, 8 models) — a standalone plugin "Shell" (the
  same architectural pattern `CAutoMasterShell`/`CChannelEQShell` use), confirmed by `CPickupModelingShell::OnSetup`
  calling into it. `CPickupModelingFX`'s constructor loops over an 8-entry `int32` table at `0xdd5acc` —
  `{16,17,18,19,20,21,22,23}` — calling `createModel` once per entry. Force-disassembling/decompiling all 8 switch
  targets (`DumpSwitchCases2.java`, same jump-table-read-at-runtime technique as the other passes) resolved every
  codename:

  | `RealtimeFXModel` enum | Codename |
  |---|---|
  | 16 | `01_02N` |
  | 17 | `02M` |
  | 18 | `03B` |
  | 19 | `01N` |
  | 20 | `02N_M` |
  | 21 | `03M` |
  | 22 | `04M_B` |
  | 23 | `05B` |

  These read as guitar-pickup-position codes (N/M/B = Neck/Middle/Bridge, with combinations — consistent with a
  5-way pickup selector feeding a "pickup modeling" feature: physically re-amping a guitar's captured tone as if
  through a different pickup/position). **None of these 8 `.mlmodelc` files exist anywhere in the current Logic
  Pro 11.2 (build 6306) app bundle** — confirmed via exhaustive `find` (including the mostly-empty
  `MAMachineLearningResources.bundle`, which holds only one unrelated model). The class, the enum range, and the
  codenames are all real and reachable; the trained weights simply aren't shipped in this build. Most likely
  explanation: unreleased/future functionality compiled into the binary ahead of its model assets, rather than
  removed/dead code (unlike `MKBO_88_erdk8f5b7k_0467` above, there's no reachable string evidence these files ever
  existed on disk at all — this is a different situation: the *class* is unreleased, not a single missing file from
  an otherwise-complete family).
- **`CAnalogModelingProfiling`** (enum 24-49, 26 distinct models behind 27 `createModel` calls — one enum value,
  44/`0x2c`, is called twice: once out of sequence right after the first call, once again in normal order) — **fully
  resolved (twenty-fifth pass)**. Same technique as before: a generalized version of the jump-table-reading script
  (`DumpSwitchCases4.java`) force-disassembled/decompiled all 26 switch-case bodies at once. Unlike every family
  above, Ghidra's own string/xref analysis found these names directly (no inline-SSE-immediate trick to work
  around) — the names are real, human-readable gear names, not cryptic codenames:

  | enum | Name | enum | Name |
  |---|---|---|---|
  | 24 | `Fuzz_Face_HF` | 37 | `Muff` |
  | 25 | `Argonaut` | 38 | `Multi_Fuzz` |
  | 26 | `Belle_Epoch` | 39 | `Octavix` |
  | 27 | `Boss_DS1` | 40 | `RAT` |
  | 28 | `Diezel_VH4_CH2_SM57` | 41 | `Tchula` |
  | 29 | `Diezel_VH4_CH3_SM57` | 42 | `TS808` |
  | 30 | `Diezel_VH4_CH4_SM57` | 43 | `Two_Rock_TC_SM57` |
  | 31 | `Dover_Drive` | 44 | `Two_Rock_Crystal_SM57` |
  | 32 | `Duke_Of_Tone` | 45 | `Vox_AC4_SM57` |
  | 33 | `Fender_Princeton_SM57` | 46 | `Wampler_Taunus` |
  | 34 | `Fender_Vibro_Champ_SM57` | 47 | `Wong_Compressor` |
  | 35 | `Fuzz_Factory` | 48 | `Xotic_ACRC` |
  | 36 | `Klon_Clone` | 49 | `Zendrive` |

  This is unambiguously a **"tone capture" / amp-and-pedal profiling feature** (the same category as Neural DSP's
  Quad Cortex captures or Positive Grid's ToneMatch): 18 of the 26 names are real, well-known boutique/classic
  guitar **pedals** (Klon Centaur clone, ProCo RAT, Ibanez TS808 Tube Screamer, Big Muff ("`Muff`"), Fuzz Face,
  Fuzz Factory, Boss DS-1, Zendrive, Xotic AC/RC, Wampler Taunus, etc.), and the other 8 are real **amps**, each
  captured through an **SM57** microphone (the industry-standard guitar-cab mic) — including three separate
  channels of a Diezel VH4 (`CH2`/`CH3`/`CH4`) and two different Fender combos (Princeton, Vibro Champ) plus two
  Two Rock boutique amps and a Vox AC4. **None of these 26 `.mlmodelc` files exist anywhere in the current Logic
  Pro 11.2 (build 6306) app bundle** (confirmed via `find`, same as `CPickupModelingFX`) — so this, too, is
  compiled-in but unreleased functionality, not a currently-usable feature. Whether it ships per-sample-rate
  variants like every other family (these are single enum slots, no `_44`/`_48`/etc. suffix pattern visible in the
  decompiled names, unlike Family A/B) isn't established — there may simply be one file per profile rather than
  four.

With this pass, **every `RealtimeFXModel` enum value from 0 to 49 now has a confirmed owning feature and name** —
`CMLSaturation` (0-9), `CAnalogModelingMasteringChainFX` (10-15), `CPickupModelingFX` (16-23), and
`CAnalogModelingProfiling` (24-49) — plus the separate, `ModelFactory`-independent `WrappedNeuralPitchTrackerRT`.
Nothing about Logic's neural-model loading in `MADSP`/`MAMachineLearning` remains unidentified.

## For jad-daw

Nothing here is a reusable algorithm the way the DSP decompiles elsewhere in `mixer.md`/`flex-algorithms.md` are —
these are opaque trained-weight files, not formulas. The transferable facts are architectural: Apple's real-time
neural audio processing in this app runs as a **gated dilated-conv stack operating on small blocks (1024–2048
samples)**, shipped as **separate per-sample-rate exports** rather than one resampled model (reusing the nearest
lower rate's model for an unlisted rate, falling back to the best/highest-rate model otherwise) — a sensible
pattern to copy if jad-daw ever adds its own learned (not hand-coded) audio effect. The confirmed Analog Model
engine specifically mixes **audio-generating** and **scalar-control-generating** neural models in the same chain
(two of its three "circuit" slots only extract a per-block control value, not an audio signal) — a reminder that
"neural audio effect" doesn't have to mean "the network outputs the final sound"; a hybrid of neural analysis +
classical processing is a legitimate, apparently-shipped design. With `CMLSaturation`/`CPickupModelingFX`/
`CAnalogModelingProfiling`/`WrappedNeuralPitchTrackerRT` all confirmed, every one of the 69 shipped `.mlmodelc`
bundles now has a named owning feature — so the full, real, directly reusable weight files for Saturation, Analog
Model, and the real-time pitch tracker are all in hand in
`../logic-pro-reference/all-ml-models/` for this private study project, per the user's standing "we need all of
them for implementation" request.

Source: `metadata.json` for every `.mlmodelc` in
`/Applications/Logic Pro.app/Contents/Frameworks/MAMachineLearning.framework/Versions/A/Resources/`, read directly
(no decompilation needed — this is Apple's own exported model schema, machine-readable, not binary analysis).
Cross-referenced against `CAnalogModelingMasteringChainFX`'s constructor (`mixer.md`, twentieth pass) for the
`IMil2BNNSModel`/BNNS connection.

Source (twenty-third pass): `MAMachineLearning::ModelFactory::createModel` at `0x81c0` in `MAMachineLearning.x86_64`
(the enum-indexed switch itself, body not statically reachable by Ghidra's own analysis); the 6 switch-case bodies
force-disassembled/decompiled at `0x8304` (enum 10, `TSO`), `0x8362`→helper `0xa1c0` (enum 11, `T`), `0x8370` (enum
12, `G_SO`), `0x83ce` (enum 13, `G`), `0x842c`→helper `0xa350` (enum 14, `S_SO`), `0x844a` (enum 15, `S`) — all via
a headless `analyzeHeadless -postScript` run (`DumpSwitchCases.java`/`DumpHelpers.java`), not GUI interaction.

Source (twenty-fourth pass): same `ModelFactory::createModel` at `0x81c0`, same headless-scripting technique.
`CMLSaturation`'s 10 switch-case bodies (enum 0-9) force-disassembled/decompiled via `DumpSwitchCases3.java`
(looping the jump table read at runtime instead of a fixed address list); its 7 helper functions
(`0x95a0`/`0x9710`/`0x9880`/`0x99f0`/`0x9c00`/`0x9d70`/`0x9ee0`) decompiled via `DumpHelpers2.java`.
`CPickupModelingFX`'s 8 switch-case bodies (enum 16-23) resolved the same way via `DumpSwitchCases2.java`; its
8-entry enum-value table found at `0xdd5acc`. `WrappedNeuralPitchTrackerRT` (found via `MADSP.x86_64`'s
external-symbol import list, not `createModel`) identified by import search, not force-disassembled case-by-case
(it bypasses the enum/jump-table mechanism entirely).

Source (twenty-fifth pass): `CAnalogModelingProfiling`'s 26 switch-case bodies (enum 24-49) force-disassembled/
decompiled via `DumpSwitchCases4.java` (the same runtime-jump-table-read technique, generalized to a 24-49 loop) —
all 26 names recovered directly from Ghidra's own string analysis (Apple's compiler didn't inline these via the
SSE-immediate trick used elsewhere, so no helper-function second pass was needed this time). File-existence check
via `find` across the whole `MAMachineLearning.framework` and the separate `MAMachineLearningResources.bundle`
confirmed none of the 26 profiled-gear files ship in build 6306, same situation as `CPickupModelingFX`.

The actual compiled model files for all 6 confirmed codenames (24 `.mlmodelc` bundles, 4 sample rates each) were
copied to `../logic-pro-reference/analog-model-ml-models/` per the project's standing "always extract found
models/presets" rule. The user asked for all of them (not knowing yet which feature(s) Family A/C serve shouldn't
block having the actual files in hand for implementation work), so every `.mlmodelc` in
`MAMachineLearning.framework/Resources/` (69 files, 56 MB) was also copied to `../logic-pro-reference/all-ml-models/`
— see that folder's own `README.md` entry for the full breakdown, including confirmation that the one cataloged-but-
unreferenced file (`MKBO_88_erdk8f5b7k_0467`) genuinely doesn't exist on disk anymore.
