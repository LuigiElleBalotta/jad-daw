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
Family B codenames instead. Whatever feature Family A's conditioned audio-to-audio models serve (the "Germanium"
naming still suggests some kind of analog/circuit modeling, just not this particular engine) remains unidentified;
tracing it would mean finding Family A's own caller in a different part of the codebase, not attempted.

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
The `MKBO_88_erdk8f5b7k_0467` string has **zero references anywhere in the binary** — it may be dead/orphaned
data bundled but never loaded by any current code path, unlike every other path string found (all of which do
have at least one reference). Every other codename's resource strings (`FEO`, `FS`, `FSQ`, `FTO`, `MTC`, `MTM`,
`RTRHT`, `RTRST`) were confirmed present as literal embedded strings but, unlike `MKGerm`, are short enough that
the compiler inlined their construction directly into the switch (via SSE `movups` immediate-string copies) rather
than calling a shared helper — which is why their exact per-case sample-rate thresholds weren't individually
decompiled this pass (would need manual disassembly of six separate inlined blocks whose jump-table target
addresses Ghidra didn't resolve automatically; attempted partially, not completed).

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
neural audio processing in this app runs as a **gated dilated-conv stack operating on small blocks (1024–2048
samples)**, shipped as **separate per-sample-rate exports** rather than one resampled model (reusing the nearest
lower rate's model for an unlisted rate, falling back to the best/highest-rate model otherwise) — a sensible
pattern to copy if jad-daw ever adds its own learned (not hand-coded) audio effect. The confirmed Analog Model
engine specifically mixes **audio-generating** and **scalar-control-generating** neural models in the same chain
(two of its three "circuit" slots only extract a per-block control value, not an audio signal) — a reminder that
"neural audio effect" doesn't have to mean "the network outputs the final sound"; a hybrid of neural analysis +
classical processing is a legitimate, apparently-shipped design.

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

The actual compiled model files for all 6 confirmed codenames (24 `.mlmodelc` bundles, 4 sample rates each) were
copied to `../logic-pro-reference/analog-model-ml-models/` per the project's standing "always extract found
models/presets" rule. The user asked for all of them (not knowing yet which feature(s) Family A/C serve shouldn't
block having the actual files in hand for implementation work), so every `.mlmodelc` in
`MAMachineLearning.framework/Resources/` (69 files, 56 MB) was also copied to `../logic-pro-reference/all-ml-models/`
— see that folder's own `README.md` entry for the full breakdown, including confirmation that the one cataloged-but-
unreferenced file (`MKBO_88_erdk8f5b7k_0467`) genuinely doesn't exist on disk anymore.
