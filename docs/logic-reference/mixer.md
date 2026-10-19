# Logic Pro 11.2: Mixer window

Seen on 2026-10-09 with the Mixer area open under the Tracks area (control bar button 3 of the second group, shortcut X). Five strips: two
audio tracks, "Inst 1" (instrument), "Stereo Out", "Master".

## Header (local menu bar)

Left to right: back arrow · **Edit ▾** · **Options ▾** · **View ▾** · **Sends on Faders:** (a power icon + popup, "Off") · centred segmented **Single | Tracks
| All** (Tracks lit) · type filters **Audio | Inst | Aux | Bus | Input | Output | Master/VCA | MIDI** (all lit blue when shown) · two layout icons at the far right.

## Layout of a strip (top to bottom)

A label column at the left names the rows: Setting · Gain Reduction · EQ · MIDI FX · Input · Audio FX · Sends · Output · Group · Automation · (icon) ·
Pan · dB. Each strip then has: Setting button · Gain Reduction meter bar · EQ display · MIDI FX slot · Input (audio: input source with a small
eye/circle icon; instrument: the green plug-in name) · Audio FX slots (blue, the empty tail slot at half height; "Mastering" slot on the output strip) ·
Sends · Output button ("St Out") · Group · automation mode ("Read", green text) · track icon tile (colour by type: blue audio, green instrument, pink output,
purple master) · Pan knob · dB field with peak field · fader with dB scale, level meter · R / I buttons (record, input monitor; audio) · M S buttons
(Master has M D) · name bar coloured by type ("Madr…ori_1", "Audio 1", "Inst 1", "Stereo Out", "Master"). The output strip also shows **Bnc**.
The selected strip has a lighter background.

## Edit menu

Undo <last action> ⌘Z · Redo ⇧⌘Z · Undo History… ⌥⌘Z · Delete Undo History · (sep) Mixer Undo · Mixer Redo · Undo selected Channel Strips · Redo selected
Channel Strips · Delete Mixer Undo History · ✓ Include Mixer Undo Steps in Project Undo History · (sep) Cut ⌘X · Copy ⌘C · Paste ⌘V · Delete *(dim when
nothing selected)* · (sep) Select All ⌘A · Deselect All ⇧D · Invert Selection ⇧I · Select Audio Channel Strips ⇧A · Select Instrument Channel Strips ⇧S ·
Select Summing Stack Channel Strips · Select Auxiliary Channel Strips ⇧F · Select Output Channel Strips ⇧O · Select MIDI Channel Strips ⇧E · (sep) Select
Same-Colored Channel Strips ⇧C · Select Muted Channel Strips ⇧M · Select Channel Strips with Same Panner Type.

## Options menu

Create New Auxiliary Channel Strip ⌃N · Create New VCA for Selected Channel Strips · Create Tracks for Selected Channel Strips ⌃T *(dim)* · Create Track Stack
for Selected Channel Strips · Flatten Stack *(dim)* · Send All MIDI Mixer Data · (sep) ✓ Enable Groups ⇧G · I/O Labels….

## View menu

Hide Legend ⌥⌃I · ✓ Link Control Surfaces · ✓ Autoscroll to Selection · Scroll To ▸ · (sep) ✓ Signal Flow Channel Strips · Channels with Sends only · ✓ Folder
Tracks · Other Tracks · All Tracks with Same Channel Strip/Instrument · ✓ Follow Track Stacks · ✓ Follow Hide · (sep) Long Faders · Channel Strip
Components ▸ · MIDI Channel Strip Components ▸ · Configure Channel Strip Components… ⌥X.

## Behaviour (tried by hand on 2026-10-09)

The remote view shows no cursor shape and no frame in the middle of a drag, so only results are recorded. Every change below was undone afterwards.

- **dB field:** one click puts the field in edit mode with the whole value selected (the Inspector strip follows live). Committing an edit is **one undo step**
  named after the strip and the parameter, "Audio 1: Volume". The step shows twice in the Edit menu: as "Undo Audio 1: Volume" and as "Mixer Undo Audio 1: Volume"
  (the menu entry "Include Mixer Undo Steps in Project Undo History" is on, so both stacks hold it). The Mixer keeps its own redo stack: after Undo, the Edit menu
  offers "Mixer Redo Audio 1: Volume" next to the project's own "Redo …".
  (The tool typed `-6` + Return and the field ended at -2,8 instead; the cause is the remote key injection, not a Logic rule, so nothing is concluded from it.)
- **Mute (M):** a click lights the button **blue**, the same button in the track header lights blue too, and the region of that track is drawn in grey in the Tracks
  area. Mute is **not** an undo step (the Edit menu does not mention it after it was clicked twice).
- **Solo (S):** a click lights the button **yellow**; a yellow **S** indicator appears at the left of the Tracks area toolbar (next to the "+" and the track-folder
  buttons) and the **playhead and its handle turn yellow** while a solo is active. Solo **is** an undo step: "Stereo Out: Solo" (both switching on and off are separate
  steps). The other strips do not change their look.
- **Strip types** (from the zoomed picture): audio strip: Setting button, thin Gain Reduction bar, EQ display, Input (eye or circle icon plus "In 1"), Audio FX slots,
  Sends (a rectangular field plus a small round send knob, both dark while empty), Output "St Out", Group, automation mode "Read" (green text), track icon tile, Pan
  knob, dB field plus a darker **peak field** to its right, fader with the dB scale on the left and the level meter with its own scale on the right, **R** and **I**
  buttons (R in red text, I fills orange when input monitoring is on), M and S, name bar (blue for audio).
  Output strip (pink name bar): Input shows only a link icon, the Audio FX area carries a "Mastering" placeholder at its bottom, no Sends and no Output rows, a **Bnc**
  button instead of R/I. Master strip (purple): only Group, "Read" in white text, the track icon tile, a single wide dB field (no peak field), the fader and **M**
  plus **D** (dim); no pan knob, no Setting, EQ, Input, FX, Sends or Output.
- **Selected strip:** the whole strip has a lighter background than the others.
- **Pan knob:** a round grey knob with a dark ring and a small green tick at the top for the centre; the ring is dark.

### Fader and meter scales

Fader scale (labels left of the fader, top to bottom; the fader runs from +6 dB at the top to −∞ at the bottom, the 0 mark is bold and has a short line that meets the
cap's centre line). Fraction of the travel measured from the top (0 = +6 dB, 1 = −∞), read from the picture and good to about ±1 %:

| dB | 6 | 3 | 0 | −3 | −6 | −10 | −15 | −20 | −30 | −40 | −∞ |
|---|---|---|---|---|---|---|---|---|---|---|---|
| from top | 0.00 | 0.13 | 0.26 | 0.38 | 0.52 | 0.60 | 0.71 | 0.79 | 0.87 | 0.91 | 1.00 |

So 0 dB sits at about **74 %** of the travel from the bottom, which is what our `Fader` already assumes; below −6 dB the spacing per dB shrinks steadily (about
2 % per dB between −6 and −20, then less). Minor ticks sit between the labels, denser near 0.

Meter scale (a separate narrow bar to the right of the fader, labels in dB below full scale, top to bottom): 0, 3, 6, 9, 12, 15, 18, 21, 24, 30, 35, 40, 45, 50, 60.
The bar is dark grey while silent; the cap is a wide metallic grey handle with horizontal ridges.

## Pan Law — decompiled (2026-10-16)

Phase 1 (UI only) found the Project Settings > Audio setting **Pan Law: "-3 dB Compensated"** (default), with an
"Apply Pan Law compensation to stereo balancers" checkbox (see `behavior-check-2026-10-14.md`). This pass opened
`MADSP.framework` (the DSP engine, not the UI) with Ghidra via REA to find the actual numbers behind that label.

**Base per-channel law**, `CStereoMixer::SetPanForChannel(int channel, float pan)` — `pan` in [-100, 100]:

```
t      = pan / 200 + 0.5        // normalized 0..1
gainL  = 1 - t²
gainR  = 1 - (1 - t)²
```

This is a **quadratic law**, not the textbook equal-power (sin/cos, center = -3.01 dB exactly) or linear (center =
-6.02 dB) law. At center (`t = 0.5`): `gainL = gainR = 0.75` = **-2.498 dB**, not literally -3 dB. At a hard
left/right pan, the far channel's gain is exactly 0 and the near channel's gain is exactly 1 (0 dB).

**Compensation**, `CMixer::SetCompensated(bool)` — when enabled and the mixer's pan-law mode field equals `2`, every
channel's gain is scaled by the float constant `0x3faaaaab` = **4/3** (= **+2.498 dB**), i.e. the exact reciprocal of
the 0.75 center dip above: `0.75 × 4/3 = 1.0`. So "compensated" means *center pan is unity gain (0 dB) and the hard
L/R ends end up +2.5 dB over unity*, not that the whole curve shifts by a flat -3 dB. When compensation is off, the
constant used is `0x3f800000` = 1.0 (no scaling) and the hard L/R ends are at 0 dB while center dips -2.5 dB.

**Open**: the pan-law mode field (seen taking at least the value `2`) almost certainly selects among the other named
presets visible in the UI (0 dB / -3 dB / -4.5 dB Compensated / -6 dB / -6 dB Compensated); only mode `2`'s
compensation constant (4/3) was traced this pass. The other modes' exact multipliers are unresolved — a follow-up
would trace `CMixer`'s pan-law-mode setter and whatever reads offset `+0x90` to apply the scale during `Process`.

Source: static analysis of `MADSP.framework/MADSP` (x86_64 slice) via REA + Ghidra 12.1.4, functions at `0x905b40`
(`CStereoMixer::SetPanForChannel`) and `0x1cc450` (`CMixer::SetCompensated`). Pure numeric facts (formulas and
constants), not ported code — see `asset-reference.md` for the method and the reasoning on what gets kept as "facts"
versus reimplemented from scratch.

## Mastering Assistant — decompiled (2026-10-16)

The Mastering Assistant that sits on the Output strip (`CAutoMasterShell`/`CAutoMaster` in `MADSP.framework`, ~140
methods) is a full chain: input/output gain staging, loudness analysis (`AnalyzeLoudness`, `EstimateLoudnessMacrosBasedOnTargetLUFS`),
a Match EQ (`ProcessMatchEQ`, with a factory/spectral-reference-data system — i.e. it fits the signal's spectrum toward
a reference curve chosen by content type/model), a multiband compressor, a limiter, a saturation/waveshaper stage, a
stereo-spread control, a correlation meter, and separate "Analyze" vs "Record"/"Bypass" run modes
(`AnalyzeMode`/`RecordingMode`/`BypassMode`). It also supports an offline analysis pass on its own thread
(`AnalyzeOfflineInNewThread`, with a progress callback) — matching the UI's "Analyzing…" progress state.

The whole UI boils down to one user-facing control, the **Loudness Macro** slider (likely -1..+1 or similar), which
several small, exact mapping functions turn into the underlying DSP parameters — these are plain piecewise-linear
formulas, not ML or anything proprietary-looking:

| Function | Formula (macro position `p`) |
|---|---|
| `CompressorInputGainFromLoudnessMacroPos` | `p ≤ 0: gain_dB = p × 13.1`  ·  `p > 0: gain_dB = p × 12.0` (asymmetric slope either side of center) |
| `LimiterInputGainFromLoudnessMacroPos` | `p < 0.6: 2.8 dB` (flat)  ·  `p ≥ 0.6: 2.8 + 3.25×(p−0.6) dB` (reaches 4.1 dB at p=1) |
| `LimiterReleaseFromLoudnessMacroPos` | `p < 0.4: 2.0 ms` (flat)  ·  `p ≥ 0.4: 2.0 + 44×(p−0.4)/0.6 ms` (reaches 46 ms at p=1) |
| `WaveShaperDryWetFactorFromLoudnessMacroPos` | `p < 0.4: wet=0`  ·  `p ≥ 0.4: wet=(p−0.4)/0.6`, `dry = 1−wet` |

So pushing the macro up doesn't scale everything uniformly: the compressor responds across the whole range (and
responds *more* to pulling it down than pushing it up, 13.1 vs 12.0 dB/unit), while the limiter, its release time, and
the saturation/waveshaper blend all stay off until the macro crosses a threshold (0.4 or 0.6) and then ramp in
linearly. This staged-engagement design is a concrete, reusable fact for jad-daw's own Mastering/Loudness macro, not
just "turn a knob and multiply everything."

### Match EQ, traced further (2026-10-16, second pass)

The actual Match EQ DSP is its own class, **`CMatchEQFX`** (not part of `CAutoMaster` itself — `CAutoMaster` just
owns and drives one), found via `CAutoMaster::SetSpectralCorrectionAmountMacro`'s calls into
`CMatchEQFX::UpdateCoeffAndCaches`. Its full pipeline, confirmed by method names and two decompiled functions:

1. **Capture a target spectrum** — either from a reference audio file (`GenerateSpectrumFromAudioFile`/`...Finalize`/
   `...Block`, run incrementally so it doesn't block audio) or from a built-in model (`SpectralReferenceDataFactory`,
   `SelectSpectralReferenceDataForModel` — not traced further this pass), stored per-channel as `CSpecData` (raw FFT
   magnitude spectra).
2. **Convert to a log-frequency dB curve** for both the source and target (`FFTMagSpecToLogResponse`,
   `GraphRelXToFreq`/`GraphFreqToRelX` — these are also what draws the Match EQ's on-screen curve, so the graph the
   user sees *is* the actual correction curve, not a separate visualization).
3. **Fade the curve to flat at the spectral extremes** — `FadeExtremesFromCurrent(thresholdOffset, fadeWidth1,
   fadeWidth2, ...)`, decompiled: it averages the per-channel magnitude spectra, finds the lowest and highest
   frequency bin whose level is within `thresholdOffset` dB of the overall peak (i.e. "where the reference's content
   actually starts and stops" vs. near-silence/noise floor), then produces four bin indices marking fade-in/fade-out
   windows of width `fadeWidth1`/`fadeWidth2` around those edges. This is exactly why the UI's Match EQ has a
   "Fade Extremes" control: without it, the correction curve would try to boost frequencies where the reference had
   no real energy (just noise floor), creating wild, meaningless gain at the spectrum's edges.
4. **`Coefficients(float, int)`** turns the (possibly macro-scaled, possibly extreme-faded) target curve into actual
   filter coefficients, smoothed over time (`Smooth`) and cached (`UpdateCoeffAndCaches`,
   `UpdateIRInFilterCores` — "IR" here reads as the filter's impulse response/coefficient set, not a convolution
   reverb). **Fully decompiled this pass** (126 basic blocks — it's one big parameter-ID dispatcher, one case per
   settable value, each ending in a call to `UpdateCoeffAndCaches`). This settles the open filter-topology question:
   the correction is **not** applied in the FFT/frequency domain at all — the gain- and Q-setting cases call directly
   into a member **`CBiquadChain`** (`CBiquadChain::SetGaindB`, `::SetQ`, `::CalcCoeffs` per band index). So the FFT
   machinery above is purely *analysis* (deriving the target curve); the real-time correction itself is a classic
   **parametric biquad filter bank**, one biquad per band, each with its own gain/Q recalculated when its coefficient
   changes — the efficient, low-latency choice for a mastering insert, not an FFT-domain convolution. Four of the
   dispatcher's cases are an identical linear search ("find the table index of the first frequency bin ≥ a target
   frequency") against the same shared frequency table, storing into four different fields — almost certainly the
   Match EQ's user-facing band-boundary markers (low/high fade edges, etc.), consistent with the Fade Extremes
   mechanism above. One case sets a fixed `-3.0` literal into `SetQ`, purpose not pinned down further.

`CAutoMaster::SetSpectralCorrectionAmountMacro`'s own blend (decompiled): for a macro value `p` in the plugin's own
0-100 UI range, it computes `p×0.01 − 1.0` (always ≤ 0 in that range) and, when ≤ 0, just applies the stored target
curve directly with no extra blending — the interpolation-toward-a-different-target branch only activates if `p`
somehow exceeds 100, so in normal use the macro doesn't blend two different target curves, it scales how hard the
curve is applied (the third coefficient it sets is literally `p × 0.005`, i.e. half of `p` as a 0..0.5 fraction).

### Loudness measurement is ITU-R BS.1770/EBU R128 (2026-10-16, fourth pass — at the owner's request)

`AnalyzeLoudness` (the function `AutoAdjustLimiter` below calls to measure where it stands) is a thin chunking loop
that feeds audio into a separate **`CLoudnessMeter`** object. That object's own methods settle the question
unambiguously: `AnalyseMomentaryLoudness`, `AnalyseShortLoudness`, `AnalyseIntegratedLoudness`,
`AnalyseLoudnessRange` — the exact **Momentary / Short-term / Integrated / Loudness Range** terminology of the
standard, not generic names — plus `SetupPreFilter` (the standard's two-stage K-weighting pre-filter),
`IsLFEChannel`/`IsSideChannel` (channel-weighting exclusions BS.1770 specifies), and, most tellingly,
`AddLoudnessToHistogram`/`GetRelativeGateIndex`/`GetPercentileOfHistogram`/`HistogramIndexForLoudnessDb` — the
histogram-based **relative gating** step that is BS.1770's specific, distinctive integrated-loudness algorithm (not
a simple running average). **This independently confirms jad-daw's own stated choice of BS.1770/EBU R128 for
loudness matches Apple's actual implementation**, architecturally — another case (like the phase vocoder) of Apple
using the same published standard jad-daw already builds on, not a secret in-house metric.

### Limiter/compressor auto-adjustment is iterative, not a closed-form curve

`CAutoMaster::AutoAdjustLimiter(startGain, targetLoudness, ...)`, fully decompiled: it is **not** a formula — it's a
small closed-loop controller. Each iteration: render a trial chunk through the limiter at the current gain,
re-measure loudness via `AnalyzeLoudness`, compute `error = target − measured`, add that error directly onto the
running gain-in-dB (`gain_dB += error`), convert to linear via the standard `10^(gain_dB / 20)` (clamped to ±100 dB),
and re-render. It stops as soon as `|error| ≤ 0.2` (dB/LU) or after **5 attempts total** (1 initial measurement + 4
adjustment loops), whichever comes first — a simple integral (accumulate-the-error) controller with a tight
convergence budget, not an elaborate model. The function returns the total gain delta it ended up applying. This is
a genuinely useful fact for jad-daw's own Mastering Assistant: don't look for a formula mapping "target loudness" to
"limiter gain" — measure, adjust, remeasure, a handful of times, same as Apple does.

`AutoAdjustMBCompressor` follows the same family of trick as the limiter, decompiled but not fully traced through
(it's a large, multi-buffer-juggling loop, over usage-limit budget to fully unpack this pass): it steps a trial gain
in **+2 dB increments** (`gain_dB`, converted via the same `10^(gain_dB/20)` each step), applies it to one or two
band buffers via `vDSP_vsmul` (so at least two compressor bands get scaled, matching "multiband"), runs
`ProcessMBCompressor` on a trial chunk, reads back a measured value at a fixed offset (almost certainly gain
reduction or achieved compression amount) and accumulates a margin term (`min(measured, 0.99 − measured)`) — the
same shape as the limiter's measure/adjust loop, but working in fixed 2 dB steps rather than the limiter's
error-proportional steps, and against a compression-amount target rather than a loudness target. This loop's exact
exit condition wasn't resolved (relies on an internal abort flag not traced to its setter).

### `ProcessLimiter`/`ProcessMBCompressor` themselves: orchestration, not the DSP kernel (fifth pass)

Both fully decompiled. Neither contains the actual limiting/compression math:

- **`ProcessLimiter(macroPos, this, buffer, startFlag)`** — sets the limiter's input gain from `macroPos` (same
  `10^(clamp(x,-100,100)/20)` conversion as everywhere else in this class), calls a **virtual** `ProcessGenInplace`
  to do the real per-sample work (a `CGen`-family object resolved only at runtime, not a concrete class this static
  pass can follow), then computes a gain-reduction **meter readout in dB** from the object's resulting state using a
  neat **fast bit-hack `log2`→dB approximation**: unpack the IEEE-754 float's mantissa as `m ∈ [1,2)` and exponent
  `e` via bit masking, then `dB ≈ (e + m − 0.33985×(m−1.5)² − 0.9150375) × 6.0206003`, where `6.0206003 = 20/log2(10)`
  is the exact log2-to-dB-of-amplitude conversion constant and the quadratic term is a minimax polynomial
  approximation to `log2(m)` over `[1,2)`. A genuinely reusable fact for any dB meter that wants to avoid a real
  `log10f` call per sample — pure, exact numerics.
- **`ProcessMBCompressor(this, sampleCount, startFlag)`** — purely a chunked buffer-feeding loop (splits the signal
  into pieces no larger than a fixed internal block size, rewires per-band input/output pointers for up to N bands,
  checks an abort flag every chunk) that ends each chunk with a call through the **same kind of virtual dispatch**
  (vtable slot) as the limiter — plus the identical `0.99 − measured` margin-accumulation line seen in
  `AutoAdjustMBCompressor`, confirming that margin bookkeeping happens on *every* call, not just during auto-adjust.

**Conclusion for this thread**: `CAutoMaster`'s "Process*" methods are consistently an **orchestration layer** —
buffer chunking, gain staging, dB/linear conversions, margin/meter bookkeeping — while the actual limiting/
compression curve (ratio, knee, attack/release) lives in a separate, runtime-polymorphic object (`CGen`-derived)
that this static entry point never names concretely. `SpectralReferenceDataFactory`/model selection also remains
open. **Update, sixth pass:** the concrete class family behind that vtable pointer is now identified — see the
stock Compressor/Noise Gate/Limiter/Channel EQ section below, found by searching for the standalone plugins
directly rather than tracing the pointer.

## Stock plugins: Compressor, Noise Gate, Limiter, Channel EQ (2026-10-16, sixth pass — at the owner's request, "almeno eq e compressore")

These are the plugins with special mixer-channel-strip UI (no normal plugin slot icon; the EQ gets a curve-preview
thumbnail and the Compressor gets its own purple Gain Reduction meter bar directly in the strip, per
`behavior-check-2026-10-14.md`/`mixer.md`'s channel-strip layout notes). Found in `MADSP.framework` directly (not
reached via Mastering Assistant's vtable — faster and gives the real concrete classes).

### Compressor — a real `CCompressorBase` + six distinct circuit-model subclasses

`CCompressorBase` holds the shared plumbing every circuit type uses: `SetSCInput`/`SetLimitInput` (separate
sidechain vs. built-in limiter input), `SetGRBuffer`/`GetMeter`/**`GetGainReduction_dB`** (the purple bar's data
source — `log2f(linear_gr) × 6.0206003`, floored at **−400 dB** when the linear value is below `1e-20`),
`CalcAutoGain`, `DoSmoothGR`, `CalcRelease`, and its own built-in **output limiter stage**
(`LimiterGainRedCurve`/`ProcessLimiter` — confirming the stock Compressor has an internal safety limiter, not just
a compressor). Six concrete subclasses, matching Logic's "Circuit Type" selector one-to-one by behavior (not just
by count):

- **`CCompressorSSL`** (`GainRedCurve`, decompiled) — models a VCA bus-compressor envelope: **two time constants**
  from the same release setting (`exp(−2300/(2×release))` fast, `exp(−2300/(6×release))` slow — a real dual-stage
  release, matching how actual SSL-style bus compressors behave), a **soft-knee cubic correction** active only in a
  narrow input range **0.6151 to 1.1925** (`(x−0.6151)³ − kneeOffset`), and a secondary SIMD-vectorized peak-limiting
  pass after the main envelope (a genuine two-detector design: compressor + limiter, not one curve doing both).
- **`CCompressorOpto`** (`GainRedCurve`, decompiled) — models a photocell/opto compressor: only **one** (slower)
  time constant (no fast stage — matches real opto compressors' single program-dependent release), the *same*
  0.6151–1.1925 knee-correction shape as SSL (a shared base curve family, re-parameterized per circuit — not
  independently reinvented each time), then a genuinely different nonlinear stage: **`powf(x, −1.2)`** followed by
  a **saturating rational curve `37.15y / (37.15y + 100)`** — the classic Michaelis-Menten-shaped response used to
  model a light-dependent resistor's nonlinearity, not a generic compressor curve.
- **`CCompressorDBX`** (`GainRedCurve`, decompiled, seventh pass) — a *third*, structurally different implementation
  strategy again: not a closed-form curve at all, a **lookup table read with cubic Hermite spline interpolation**
  (a dedicated `CHermiteTableLookUp` helper class does the interpolation; the table itself is filled elsewhere, not
  traced this pass). The input path: clamp the level to `[1e-6, 1e6]`, take its natural log, scale by the constant
  `0.05211534` and add a threshold-dependent offset, then use that as a fractional index into the table (4
  consecutive table points combined via the classic Catmull-Rom-style cubic blend). So where SSL and Opto compute
  their gain reduction algebraically every sample, DBX looks it up from a precomputed curve — a third, genuinely
  different engineering approach for a third circuit character, not just different constants in the same shape.
- **`CCompressorFET`** (`GainRedCurve`, decompiled, tenth pass) — a *fourth* circuit, back to a closed-form curve: the
  *same* shared soft-knee cubic (0.6151–1.1925) as SSL/Opto, a single time constant (like Opto, no fast/slow split),
  then a distinctly steeper nonlinearity: **`(x+1)^−4`** (exponent exactly `−4.0`) rescaled linearly by
  `y×0.665397 + 0.08`, then `×1.341567`. A much steeper power than Opto's `−1.2` — consistent with real FET
  compressors (1176-style) having a far more sudden, "grabby" gain-reduction onset than an opto cell's slow,
  photoresistive response. Four circuits decompiled now, four distinct nonlinearities on top of one shared knee
  shape: SSL (linear-ish envelope + secondary limiter), Opto (`x^−1.2` → Michaelis-Menten saturation), DBX (table
  lookup), FET (`(x+1)^−4` → linear rescale).
- **`CCompressorLP`** (`GainRedCurve`, decompiled, eleventh pass) — a *fifth* strategy: a **1024-entry lookup table**
  (indices 0–1023, clamped for interpolation at 1022) read with plain **linear interpolation** between adjacent
  points — no cubic knee, no power law, no spline, just the finest-resolution table of any circuit here. "LP" almost
  certainly stands for **Platinum**, Logic's clean/modern digital compressor model — a precise, table-driven curve
  fits that "transparent, no character" design brief far better than the analog-modeled power-law/knee curves used
  by SSL/Opto/FET.
- **`CCompressorClassA`** (`GainRedCurve`, decompiled, thirteenth pass) — turns out to be a *sixth* strategy, but
  not the one its `Coefficients` function implied: not a pure discrete-ratio dispatch, a **table lookup like
  LP/DBX**. `GainRedCurve` reads through a **1024-entry table** at `this+0xb0` (indices 0–1023, linear
  interpolation between adjacent entries exactly like LP's table — same mechanism, different table contents),
  indexed by `input_level × 1023 × m_fThresholdOffset[ratioIndex]` clamped to `[0, 1023]` — i.e. the single-sample
  path (`n==1`) and the block path both scale the input by the *selected ratio preset's* threshold-offset constant
  (the same 4-entry `{0.0, 3.0, 7.0, 0.0}` table from `Coefficients`) before the table lookup, rather than computing
  the ratio curve algebraically every sample. After interpolation, the result is further clamped against
  `min(1.0, 1/(input × envelope × makeup))` — a safety ceiling tied to the smoothed envelope/makeup-gain state —
  before the final `× makeup` multiply. So the real architecture is: `Coefficients()` picks one of 4 ratio presets
  and converts it to a threshold-scaling constant; `GainRedCurve()` always walks the *same* 1024-entry table, just
  re-scaled by whichever preset is active — a hybrid of ClassA's "discrete ratio buttons" UI behavior and LP's
  table-driven curve mechanism, not a fifth closed-form power-law curve like SSL/Opto/FET.

**All six stock Compressor circuits are now fully characterized** — five genuinely different engineering
strategies for the "same" effect, two of which (LP and ClassA) share the same 1024-entry linear-interpolation
lookup mechanism while differing in what fills the table and how it's indexed: SSL (dual envelope + shared cubic
knee + secondary limiter stage), Opto (shared knee + `x^−1.2` power + Michaelis-Menten saturation), FET (shared
knee + `(x+1)^−4` power + linear rescale), DBX (Hermite-spline table), LP/Platinum (1024-point linearly-interpolated
table, continuous curve), ClassA (same 1024-point linear-interpolation mechanism, but indexed via a discrete
ratio-preset threshold-scaling constant — 4 ratio buttons feeding one shared table).
- `CLimiterFX::Coefficients` turned out to be a thin forwarder into `CLimiter::Coefficients` (the real implementation,
  not decompiled this pass) plus one special case: parameter index `6` toggles something per-channel across every
  linked channel object (`0.0 < value` as a bool) — likely a stereo-link/channel-count-dependent switch, not pinned
  down further.

**For jad-daw**: if/when its own Compressor grows circuit-type variants, this confirms each one should be a genuinely
different curve shape (not a shared curve with different numbers) — Apple's own "Vintage"-style models differ in
knee shape, number of envelope time constants, and the nonlinearity itself, not just ratio/threshold/release knob
values. The exact SSL knee bounds (0.6151/1.1925) and Opto exponent/saturation constants (−1.2, 37.15, 100) are
reusable as-is if a matching circuit character is wanted.

### `CLimiter::Coefficients` and `CCompressorClassA::Coefficients` (eighth pass)

Both are clean, enum-indexed parameter dispatchers (unlike `CMatchEQFX`'s 126-block one) — easy, reliable decompiles.

`CLimiter::Coefficients` (the real implementation `CLimiterFX` forwards to): an 8-case switch, each case validating
its input range and returning a **numeric error code `0x3ed` (1005)** when out of range (gain params clamped to
±100, a release-like param to 0–10, others to 0–2000 — i.e. Logic's own internal parameter validation uses sentinel
error codes, not exceptions, at this layer). Two notable architecture facts:
- **Sample-rate-adaptive lookahead**: a lookahead/delay buffer size is chosen as **48 samples below 50 kHz, 24
  samples below 100 kHz, 0 at ≥100 kHz** (only when a channel/stereo-link flag is also set) — the limiter's
  lookahead is a fixed sample count, not a fixed time, and shrinks at higher sample rates rather than staying
  constant in milliseconds.
- The exact same **fast bit-hack `log2`→dB polynomial** found earlier in `CAutoMaster::ProcessLimiter`
  (`(e + m − 0.33985×(m−1.5)² − 0.9150375) × ±6.0206003`) is reused **internally** here too (sign flipped, so it's
  computing an attenuation rather than a readout) — confirming it's a shared, general-purpose fast-log utility used
  wherever Logic needs dB from a linear value in a hot path, not something special-cased for one meter.
- Release/attack-style envelope coefficients throughout reuse the same `exp(−2300/(time×sampleRate))`-shaped
  constant seen across every compressor/limiter decompiled so far — `−2300` is evidently Logic's one shared
  "time-constant-to-coefficient" magic number for this whole plugin family, not circuit-specific.

`CCompressorClassA::Coefficients`: mostly ordinary gain (`10^(±0.05x)`) and envelope-coefficient cases (same `-2300`
family, with cases 6/7 combining two time-derived coefficients multiplicatively — a program-dependent release
scaled by another parameter, a real analog-modeling detail). One case stands out: **case `0xd` reads a ratio value
from a small (4-entry) float constant table *and* a second small-integer value from a second 4-entry table at the
same index**, then converts the selected ratio `r` into a slope pair `r/(r−1)` and its complement. This confirms
`CCompressorClassA` offers a small set of **discrete ratio presets** — consistent with real FET/Class-A hardware
compressors (e.g. a 1176-style "4 ratio buttons") rather than the continuously-variable ratio of a VCA design. The
4 ratio values (read directly, ninth pass): **`{0.0, 3.0, 7.0, 0.0}`** — giving slopes `0/(0−1)=0`, `3/2=1.5`,
`7/6≈1.167`, `0` for indices 0–3. **Correction (thirteenth pass)**: the second table was earlier assumed to hold
function pointers (one processing routine per preset); reading its raw bytes shows it's actually a parallel
**small-integer table, `{0, 3, 7, 1}`** (64-bit values, far too small to be code addresses in this binary) — nearly
identical to the ratio table but with index 3 reading `1` instead of `0`. Neither `Coefficients` nor `GainRedCurve`
dereferences the stored value as code, so it's some other per-preset integer (mode ID, display value, or similar),
not a dispatch table — `GainRedCurve` (see above) reads straight through one shared 1024-entry table for every
preset, re-scaled by the ratio's threshold-offset constant, so there's no per-preset routing to begin with.

Source (eighth pass, corrected thirteenth pass): `CLimiter::Coefficients` at `0xd0bc0`, `CCompressorClassA::Coefficients`
at `0x9bd90`, both in `MADSP.framework/MADSP`; second constant table confirmed at `0xd53920` (`{0,3,7,1}` as `int64`),
first ratio table at `0xd4b2d0` (`{0.0,3.0,7.0,0.0}` as `float`).

### Noise Gate — clean, fully named parameter set

`CNoisegateFX` has one plainly-named setter per UI control: `SetThreshold`, `SetAttack`, `SetReleaseTime`,
`SetHoldTime`, `SetReduction`, `SetLookahead`, `SetKeylisten` (key-input listen/solo), `DoDirectMix`, plus
`SetKeyFilter` (a filter on the key/trigger signal, separate from the main signal) and `RegionGate`/`PeakTracking`/
`CalcEnvelope` for its detection side. `SetThreshold` (decompiled): standard `10^(dB/20)`, but **clamped so a
positive dB threshold is impossible** (silently forced to 0 dB = unity, i.e. the UI's threshold range effectively
tops out at 0 dB), floored at −100 dB. It also derives a *second* linear value from a second, not-fully-resolved
`exp10f` call scaled by the same base — almost certainly the gate's **hysteresis/reopen threshold** (a classic
noise-gate design: the gate opens at the set threshold but doesn't close again until the signal drops further
below it, to avoid chattering) rather than one single on/off point.

### Channel EQ — the curve-preview data, confirmed and corrected (ninth pass)

`CChannelEQShell` (the strip's built-in EQ) exposes exactly the data a curve-preview thumbnail needs: `GetResponseLUT`,
`GetFreqTable`, `GetUnityResponse`, `GetResponsePoints`, `GetRelXTable`. **`AllocateAndFillStaticTables` decompiled**
gives the exact, clean formula for the graph's frequency axis — correcting an earlier guess that it used a
precomputed sine table (that's a different, unrelated method; the real curve math is a genuine biquad evaluation):

```
for i in 0..N-1:
    relX[i]  = i / (N-1)                         // linear 0..1 position along the displayed x-axis
    freq[i]  = 20 × 10^(3 × i/(N-1))              // Hz, log-spaced: 20 Hz at i=0, 20,000 Hz at i=N-1
```

A plain **20 Hz–20 kHz logarithmic frequency axis**, N points evenly spaced in log-frequency (and so evenly spaced
in on-screen x position too, via the parallel linear `relX` table) — the standard EQ-display layout, now an exact,
reusable formula rather than an assumption. `MapChanEQSineTable` is a separate, unrelated method, not the curve path.

**`CBiquadChain::FillResponseLUT` and `CBiquadChainDualRateFX::FillResponseLUT` decompiled (thirteenth pass)** —
and it's *not* a full complex biquad transfer-function evaluation at each point after all: it's a cheaper, more
elegant precomputation step. For each of the N frequency points, given `ω = 2π × freq[i] / sampleRate`, it computes
and stores **four values per point** — `cos(ω)`, `sin(ω)`, `cos(2ω)`, `sin(2ω)` — laid out as four parallel arrays
in the output buffer (returns `N×4` as the total double count). This is the standard trick for evaluating a biquad's
frequency response cheaply: `H(e^(-jω)) = (b0 + b1·e^(-jω) + b2·e^(-2jω)) / (1 + a1·e^(-jω) + a2·e^(-2jω))` only needs
`e^(-jω)` and `e^(-2jω)` (i.e. exactly this cos/sin pair at ω and 2ω) — once this table is built, *every biquad
stage in the chain* reuses it to evaluate its own magnitude/phase response at the same N frequencies with plain
multiply-adds, no further trig calls. The frequency-domain trig basis is computed once per redraw, not once per
band — the actual per-band response combination (reading these tables plus each biquad's `b0/b1/b2/a1/a2`) happens
elsewhere, not traced this pass. The 2-wide SIMD unrolled loop (`___sincos_stret`, processing points in pairs) is
purely a throughput optimization of this same precomputation, not a different algorithm.

`CBiquadChainDualRateFX::FillResponseLUT` confirms the "DualRate" naming precisely: it calls this exact trig-basis
routine **twice**, once for a chain pointer at `this+0x148` (always present) and, only if a second chain pointer at
`this+0x150` is non-null, again for a second chain — each using its *own* current sample rate — and concatenates
the two results. So "dual rate" means the curve preview can represent two biquad chains running at genuinely
different internal sample rates (e.g. a main-rate stage plus an oversampled stage) in one combined response, not
that it merely re-derives the curve after a sample-rate change.

### Match EQ's model factory (twelfth pass)

`CAutoMaster::MatchEQFactory` builds the Mastering Assistant's own dedicated `CMatchEQFX` instance (separate from the
standalone Match EQ plugin) with mastering-sensible hardcoded defaults, decompiled:
- Three default float fields: **0.8, 6.0, 0.5** (exact values; which UI-facing control each backs wasn't traced).
- Default **fade-extremes boundaries** computed at **30 Hz, 50 Hz, 7,500 Hz and 15,000 Hz** (with the same
  near-Nyquist safety fallback seen in `FadeExtremesFromCurve` earlier) — so out of the box, the Assistant's Match EQ
  leaves sub-30Hz rumble and anything above 15kHz alone by default, only correcting the "useful" 50Hz–7.5kHz range
  unless told otherwise. A sensible, mastering-appropriate default range, not an arbitrary one.

`CAutoMaster::SelectSpectralReferenceDataForModel(modelIndex)` is a one-line table lookup: it indexes a static array
of model structs (each holding a data length, a float spectrum pointer, and a secondary pointer) at `this+0x4a8` and
feeds the selected one straight into `CMatchEQFX::SetSpecData`. This confirms the architecture — a small set of
**built-in reference spectra** (presumably per content type, e.g. "Pop", "EDM", etc., matching the UI's model
picker) stored as precomputed data, selected by plain index — but not the model count, labels, or actual spectral
data, which weren't read out of the binary this pass.

**Open**: what actually fills `CCompressorClassA`'s shared 1024-entry table in the first place (built once,
presumably at construction/reset — not traced), the actual per-band response-combination step that consumes
`FillResponseLUT`'s cos/sin basis (which biquad coefficients multiply which table entries — not traced), and the
actual reference-model table contents.

Source (thirteenth pass): `CCompressorClassA::GainRedCurve` at `0x9c100`, `CBiquadChain::FillResponseLUT` at
`0x1b2740`, `CBiquadChainDualRateFX::FillResponseLUT` at `0x90b730`, all in `MADSP.framework/MADSP`.

Source: static analysis of `MADSP.framework/MADSP` (x86_64 slice) via REA + Ghidra 12.1.4. `CCompressorBase` family
at `0x70ae0`-`0xa0f90`, `CNoisegateFX` at `0x133f00`-`0x135fe0`, `CChannelEQShell` at `0x231580`-`0x232cd0`,
`CLimiterFX`/`CLimiter` at `0xce360`-`0xd3a00`, `CAutoMaster::MatchEQFactory` at `0x5ff730`,
`SelectSpectralReferenceDataForModel` at `0x600270`, `CAutoMaster` class at `0x5f8db0`-`0x60bd50`,
`CAutoMasterShell`/`CAutoMasterFX` wrappers, `CMatchEQFX` at `0x254cc0`-`0x25f290`, and
`CLoudnessMeter`/`CLoudnessMeterFX`/`CLoudnessMeterShell` at `0x90bd70`-`0x90dd90`. Pure numeric facts and
architecture, not ported code.

## Related settings

Settings > View > Mixer: show Mastering Assistant button, open plug-in window on insertion, show recent plug-in list, peak hold 800 ms, IEC Type I return
time. See `settings-and-app-menu.md`.
