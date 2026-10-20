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

### The per-band response combination, found and decompiled (nineteenth pass)

`CBiquadChain::FrequencyResponse` exists as two overloads that together are exactly the piece `FillResponseLUT`
left open. The simpler one (5 args) is pure **orchestration**: it walks every enabled band in the chain (checking
each band's own "is this stage active" flag) and, for each one, calls the 7-arg overload — ping-ponging an
input/output buffer pair so each band's output magnitude becomes the next band's input. **The chain's displayed
response is the product of each active band's own magnitude response** (plain multiplication in linear-amplitude
space, not a dB sum, though the two are mathematically equivalent for a cascade) — disabled bands are skipped
entirely, not multiplied by a flat 1.0.

The 7-arg overload is the actual **per-band magnitude evaluation**, and it's exactly the consumer of
`FillResponseLUT`'s `cos(ω)/sin(ω)/cos(2ω)/sin(2ω)` table this pass was looking for. Each band keeps 6 precomputed
`double`s (set up once, whenever `Coefficients()` runs on that band — not recomputed per response query): calling
them `c0..c5`, the function first folds them with each other into three running values (`c0`, `c0·c1+c4`,
`c0·c2+c5`, plus `c3+c0`) — i.e. it pre-combines the raw biquad `a1/a2/b0/b1/b2`-derived coefficients once per
band, not once per frequency point. Then, **per frequency point `i`** (pulling `cosω[i]`, `sinω[i]`, `cos2ω[i]`,
`sin2ω[i]` straight from `FillResponseLUT`'s table): it builds the real/imaginary parts of both the numerator and
denominator of `H(e^-jω)` as linear combinations of those four trig values and the band's folded coefficients, then
computes `|H|² = (Re_num² + Im_num²) / (Re_den² + Im_den²)`, takes `sqrt`, and **multiplies the running input
magnitude by this band's `|H|`** — confirming the product-of-magnitudes chain combination described above at the
single-band level too. The whole thing is 2-point SIMD-unrolled (`divpd`/`sqrtpd` pairs).

One more detail: each band keeps **a small result cache** — a per-point magnitude buffer plus a "last `N` computed"
counter. If the same point count is requested again and a size/coefficient-staleness check passes, the function
skips the entire trig/sqrt computation and just multiplies the input by the cached per-point gains (a plain
SIMD elementwise multiply) — the expensive math only reruns when the curve-preview resolution or the band's own
coefficients actually change, not on every redraw. This explains why `AllocateResponseCache`/`GetResponseLUTSize`
exist as their own methods, found alongside `FrequencyResponse` this pass but not decompiled.

**For jad-daw**: if its own EQ curve preview recomputes per-band response from scratch every frame, this is the
reusable optimization worth copying — build the `cos/sin(ω)`/`cos/sin(2ω)` basis once per redraw (shared across all
bands), fold each band's own 5 biquad coefficients down to a handful of per-band constants once when they change
(not per point), and cache the resulting per-point magnitude array so an unchanged band costs one multiply per
point instead of a `sqrt`+`div`.

Source (nineteenth pass): `CBiquadChain::FrequencyResponse` at `0x1aef20` (orchestration) and `0x1ae7e0` (per-band
evaluation), `MADSP.framework/MADSP`.

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

### The reference-spectrum models: NOT the Match EQ genre library — 4 dedicated `AM_Curve_N` files (fourteenth pass, corrected eighteenth pass)

`CAutoMaster::SpectralReferenceDataFactory(modelIndex)` — the function that actually *builds* each model's data
(distinct from `SelectSpectralReferenceDataForModel`, which only *selects* among already-loaded ones) — decompiled.
It is **not** a static in-binary table at all: it computes a filename from an 11-byte-per-entry name table embedded
in the object (`this + modelIndex×0xb + 0x18`), resolves it to a bundle file via the same generic
`SetFileRefToDataFileInBundleR8CFileRefPKcS2_S2_` helper used for ordinary plugin presets, with extension **`.pst`**
— i.e. Logic's completely standard preset-file mechanism — reads the file, and (when a header tag `0xf6` matches)
parses out a float spectrum array starting right after the file's own header. So each "model" is a regular `.pst`
preset file loaded by name/index, not hardcoded data compiled into `MADSP`.

**Correction (eighteenth pass)**: the fourteenth pass guessed this reuses Match EQ's 50-genre preset library, since
no obvious dedicated file set was found at the time — that guess was wrong, and is fully superseded by what
follows. Decompiling `CAutoMaster`'s own constructor shows exactly what fills that 11-byte-per-entry name table at
`this+0x18`: four literal strings, written directly by the constructor — **`"AM_Curve_1"`, `"AM_Curve_2"`,
`"AM_Curve_3"`, `"AM_Curve_4"`** (10 characters + null = 11 bytes each, spanning `this+0x18` to `this+0x44` exactly,
matching `SpectralReferenceDataFactory`'s `modelIndex×0xb` stride for `modelIndex` 0–3 precisely). So there are
**exactly 4 models**, not dozens, and they're named generically, not by genre. Searching the app bundle for these
literal names found them immediately: `MADSP.framework/Versions/A/Resources/AM_Curve_{1,2,3,4}.pst` (also mirrored
in `MADSPPlugInPublic.framework/.../Resources/` for `2`–`4`). All four parse with the same chunked `.pst` format as
the Match EQ presets (`SMMT`/`SMUC`/`SMOM`, confirmed below) and **all four have real, non-zero `SMMT` target
curves** — genuine reference spectra, e.g. `AM_Curve_1`: 306 points, −82 to −27 dB; `AM_Curve_2`/`3`/`4`: 340 points
(a different point count than their own `SMUC`/`SMOM` chunks, which stay at 306 — the first time a file's three
chunks weren't all the same length), roughly −117 to −26 dB. Extracted and saved (raw `.pst` + parsed JSON) to
`../logic-pro-reference/match-eq-presets/mastering-assistant-models/`. What musical/loudness concept each of the 4
numbered slots represents (genre-agnostic "intensity" levels? a generic/EDM/acoustic/orchestral-style split? plain
placeholders?) wasn't identified — no UI string or label was found for them this pass, only the internal filenames
and their raw curve data.

**The `.pst` binary format itself is now fully decoded and the data extracted** (same pass, triggered by checking
whether the actual preset data was worth pulling out — it is). Every file: a fixed 96-byte header tagged
`GAMETSPP` (bytes 0–3 = total file size, byte 8 = `18`, used by the factory's own offset arithmetic, bytes 0xc–0x13
literally spell `GAMETSPP`, byte 0x14 = the `0xf6` tag the decompiled factory checks for), followed by **three
length-prefixed chunks** tagged `SMMT` / `SMUC` / `SMOM` (each chunk's length field spans tag+length+payload exactly
— verified against the next chunk's start byte-for-byte). Each chunk's payload is `[channel count: int32][N: int32]`
followed by `1 + channelCount` arrays of `N` floats each: **one shared log-frequency axis** (`0.0`, then `20 Hz` up
to `22,050 Hz` — a 44.1 kHz-Nyquist axis, `N=306` points for every preset, stereo and surround alike) **plus one
magnitude-in-dB array per channel** (2 channels for Stereo Match EQ, 6 for Surround — exactly L/R vs. a 5.1 layout).
Only the **`SMMT`** ("target") chunk carries real data in every factory preset checked; `SMUC` and `SMOM` are
all-zero in all of them — almost certainly "current/measured" and a third capture slot that only get filled in at
runtime after a live analysis, not meaningful in a shipped preset. So `SMMT`'s magnitude array *is* the genre's
actual target curve (e.g. Pop 01's L channel: −54 dB at 0 Hz-bin, rising to a −24 dB peak, settling into a
−90-to-−96 dB noise floor above ~15 kHz — a real, usable mastering target shape, not placeholder data).

All 70 presets (50 stereo + 20 surround) were parsed with a one-off script into plain JSON (`freq_hz` + one
`target_magnitude_db` array per channel) and saved alongside the original `.pst` files in `../logic-pro-reference/
match-eq-presets/` (outside jad-daw's git repo, same convention as the rest of that folder) — real, directly reusable
reference curves for jad-daw's own Match EQ, not just an architecture description. Each plugin's own `#default.pst`
was pulled too: Match EQ's is genuinely all-zero (an empty default), but **Mastering Assistant's own `#default.pst`
is not** — its single `SMUC` chunk holds a real curve (−110 to −27 dB), suggesting `SMUC` isn't purely an
empty-until-captured slot as guessed above; possibly a generic fallback reference used before any content-type
model is picked. Not confirmed further this pass.

### Stock Compressor/Limiter/Noise Gate/Channel EQ: all factory presets extracted too (sixteenth pass)

Following the same request for the Match EQ presets, the same generic container format (`GAMETSPP` header, a
declared parameter count, then a flat float array — the same header shape `SpectralReferenceDataFactory` uses,
just without the Match EQ-specific chunking) turned out to back **every** stock plugin's `.pst` preset, not just
Match EQ's. All factory presets for Compressor (159, under category folders like `01 Drums`), Channel EQ (149),
Noise Gate (8), Adaptive Limiter (5), and Limiter (3) — 324 total — were copied and parsed into JSON, alongside
each plugin's own `CSParameterOrder.plist` (Apple's own UI-parameter-ID-to-name list — e.g. Compressor's
`0 Compressor Threshold`, `1 Ratio`, `2 Attack`, `3 Release`, `4 Gain`, `5 Knee`, `9 Circuit Type`...). A spot check
on `Compressor/01 Drums/Classic Drums.pst` reads as a plausible, sensible real-world setting (threshold ≈ −23 dB,
ratio ≈ 5.2:1, attack ≈ 10 ms, release ≈ 48 ms), which is reassuring, but **the exact byte-position ↔ parameter-ID
correspondence was not independently confirmed** — unlike the Match EQ spectral format (verified byte-for-byte
against the decompiled factory function), this is pattern-matched from the plausible-looking values, not proven.
Treat it as a strong lead for future work (e.g. decompiling `CCompressorBase::GetPropertyInfo` to get the real
ID table), not an established fact yet. Channel EQ's presets consistently carry 2 extra floats beyond their
declared count, unexplained (likely per-band metadata, since Channel EQ's parameter list is inherently variable
with band count, unlike the other four plugins' presets which matched their declared count exactly).

Source (sixteenth pass): on-disk preset libraries at `/Applications/Logic Pro.app/Contents/Resources/Plug-In
Settings/{Compressor,Limiter,Noise Gate,Channel EQ,Adaptive Limiter}/`; extracted into
`../logic-pro-reference/stock-plugin-presets/`.

**Unrelated but notable find from the same search**: `CAutoMasterShell::BuildIntelModelMenu` (in the UI shell, not
`CAutoMaster` itself) builds a *different* 4-item menu — **"Clean" / "Valve" / "Punch" / "Transparent"** — for what
looks like an analog-circuit-modeling stage (matches the `SetAnalogModel`/`ProcessAnalogModel`/`UpdateModel`
methods seen in the `CAutoMaster` method list). Three of the four options are gated behind Apple Silicon: the exact
UI strings are `"<Name> (Requires Apple silicon)"` normally, or `"<Name> (Not supported while Logic Pro is running
Rosetta)"` when the app itself is detected running under Rosetta (via `ECHardwareUtilities processIsTranslated`),
with "Clean" as the universal fallback. Strongly suggests "Valve"/"Punch"/"Transparent" are ML-driven circuit
models that need the Neural Engine, not just DSP-cost gating — a separate feature from Match EQ, not pursued
further this pass, but exact, reusable UI copy if jad-daw ever wants a hardware-gated "quality model" selector.

`CAutoMaster::SetAnalogModel(index, nativeAvailable)` (decompiled) confirms the engine behind this menu is a
dedicated object — **`CAnalogModelingMasteringChainFX`** (its own class, not a repurposed biquad/EQ stage), reached
via `SetModel(AMMCDeviceType)`. Index `0` ("Clean") makes **no call into the engine at all** — it's a true bypass
of this whole stage, not "model 0" of the analog engine. Indices 1–3 (Valve/Punch/Transparent) each XOR the
`nativeAvailable` bool into a different small device-type pair (`1↔0`, `3↔2`, `5↔4`) — i.e. every model has two
concrete device-type values, a "native" one and a "fallback" one, selected by whether Apple Silicon ML support is
actually available at that moment, matching the menu text's own "(using Clean instead)" substitution exactly. The
analog circuit simulation itself (what `CAnalogModelingMasteringChainFX` actually computes — whether it's CoreML
inference like Smart Tempo, or classical nonlinear DSP) is a large, separate engine, not traced this pass.

**Resolved (twentieth pass): it's neural-network inference, confirmed architecturally.** The class's own
constructor signature settles the CoreML-vs-classical-DSP question directly — it takes several
`std::vector<std::unique_ptr<IMil2BNNSModel>>` arguments (the exact count, **six**, confirmed in the twenty-second
pass below). "BNNS" is Apple's **Basic Neural Network Subroutines**
API (a low-level Accelerate-framework NN-inference backend); "Mil2BNNS" names an interface for a model compiled
down from **MIL (Core ML's Model Intermediate Language)** to run through BNNS directly, bypassing the full CoreML
runtime — the same general lineage as Smart Tempo's CNN, but a different, lighter-weight execution path, and
**several separate models** are held at once. Decompiling the constructor (dense, mostly buffer/ownership
bookkeeping, not attempted in full) shows it also builds a **per-path FFT-based up-sampler/down-sampler pair**
(`CDownSampler`/`CUpSampler`, both `CFFTFilter`-based) for up to 6 internal paths — strongly suggesting the neural
models run at one fixed internal rate and the engine resamples around them for any other project sample rate
(consistent with the separately-found `SamplerateSupported` method). **Follow-up (twenty-first pass)**: catalogued
every other model in `MAMachineLearning.framework/Resources/` by reading its CoreML `metadata.json` — full writeup
in the new `docs/logic-reference/machine-learning-models.md`. Initially guessed the engine used an 11-model
conditioned-audio family (`FEO`/`FS`/`FSQ`/`FTO`/`MTC`/`MTM`/`RTRHT`/`RTRST`/`MKBO`/`MKGerm`/`MK2Germ`). **Corrected
in the twenty-third pass**: that guess was wrong — see below.

**Resolved (twenty-third pass): the exact 6 models, by name.** The user authorized tracing a second binary and,
when GUI automation of Ghidra proved unreliable (Java/Swing dialogs don't respond to synthetic clicks/keystrokes
here), a headless `analyzeHeadless -postScript` run force-disassembled and decompiled `ModelFactory::createModel`'s
6 switch cases directly — no GUI needed. **All six consumed models turn out to be the *other* model family**
(`machine-learning-models.md`'s "Family B", unconditioned models, not the conditioned one guessed above): enum
10=`TSO`, 11=`T`, 12=`G_SO`, 13=`G`, 14=`S_SO`, 15=`S` — three clean native/fallback pairs. **Correction (Mac
reverse-engineering pass, `analog-model-chain.md`/`mac-pass-3.md`): all six output full `[1,1,1,1024]` audio, not
a scalar** — the "`G`/`G_SO`/`S`/`S_SO` output a scalar" read here came from a stale/placeholder shape annotation
in `metadata.json` (confirmed wrong three independent ways: the raw `.mil` program's own return-value type, the
real ONNX conversion of all six, and a live Core ML run of a same-symptom model returning a real multi-element
array where the static metadata claimed `[]`). There are no "control-signal extractor" slots after all — and the
"which pair is Valve/Punch/Transparent" question below is now **fully confirmed, not open**: Valve={10,11}=
`{TSO,T}`, Punch={12,13}=`{G_SO,G}`, Transparent={14,15}=`{S_SO,S}`, traced directly in `CAutoMaster::Process`'s
own decompiled switch, not inferred from creation order. Full writeup in `docs/logic-reference/analog-model-
chain.md`. (**Twenty-fourth pass**: the 11-model conditioned-audio family guessed above and ruled out for this
engine does have a real owner after all, just not this one — `CMLSaturation`, Logic's stock neural Saturation
plugin; see `docs/logic-reference/machine-learning-models.md`.)

**`CCompressorClassA`'s table-fill step, ruled out two candidates (seventeenth pass)**: neither `OnReset` nor the
constructor (`CCompressorClassAC2ER9CProcDatal`) build the curve — the constructor only `bzero`s the whole
1024-entry region (`this+0xb0`, confirmed: `bzero(this+0x16 [in 8-byte units] = this+0xb0, ...)`) to an all-zero
starting state, and `OnReset` only touches small envelope-state fields well below that offset. `Coefficients`'s
ratio-preset case (`0xd`) does call back into the object's vtable afterward (`(*vtable+0x90)`, resolved by reading
the real vtable bytes at `0xee3ab0+0x90`) — but that slot is **`CCompressorBase::CalcAutoGain`**, an auto-gain
recalculation, not a table builder. So the actual fill logic either lives in a function not reached from any of
these three places, or — given the "models"/presets discovery this session — is plausibly supplied **from outside
`MADSP` entirely**, e.g. baked into a preset/resource rather than computed by this binary at all (speculative,
not confirmed). Not pursued further.

**Open**: what the 4 `AM_Curve_N` models actually represent musically (no label/UI string found), and (ruled out as
a quick win, see above) `CCompressorClassA`'s table-fill step. **Resolved** (was open here): which specific
`{TSO,T}`/`{G_SO,G}`/`{S_SO,S}` pair is Valve/Punch/Transparent — confirmed, not just plausible, in
`docs/logic-reference/analog-model-chain.md` (Valve={TSO,T}, Punch={G_SO,G}, Transparent={S_SO,S}, traced directly
in `CAutoMaster::Process`'s own switch body). The per-band response-combination step, `CAnalogModelingMasteringChainFX`'s
fundamental nature (BNNS neural inference), and the exact 6 models it loads are now all resolved — see the
nineteenth/twentieth/twenty-third-pass sections above and the full catalog in
`docs/logic-reference/machine-learning-models.md`. The actual network weights remain genuinely out of scope for
static analysis at this depth.

Source (thirteenth pass): `CCompressorClassA::GainRedCurve` at `0x9c100`, `CBiquadChain::FillResponseLUT` at
`0x1b2740`, `CBiquadChainDualRateFX::FillResponseLUT` at `0x90b730`, all in `MADSP.framework/MADSP`.

Source (fourteenth pass): `CAutoMaster::SpectralReferenceDataFactory` at `0x5ffa10` in `MADSP.framework/MADSP`;
`CAutoMasterShell::BuildIntelModelMenu` at `0xb81200`, same binary; on-disk preset libraries at
`/Applications/Logic Pro.app/Contents/Resources/Plug-In Settings/Match EQ/{Stereo,Surround} Match EQ/*.pst`.

Source (eighteenth pass, correction): `CAutoMaster`'s constructor (`CAutoMasterC2ER9CProcDataii`) at `0x605290` in
`MADSP.framework/MADSP`; on-disk files at `/Applications/Logic Pro.app/Contents/Frameworks/MADSP.framework/Versions
/A/Resources/AM_Curve_{1,2,3,4}.pst`.

Source (twentieth pass): `CAnalogModelingMasteringChainFX`'s constructor overloads at `0x2d45d0` (the
`vector<unique_ptr<IMil2BNNSModel>>` one, 5 arguments) and `0x2d3cc0`, `MADSP.framework/MADSP`; `.mlmodelc` files
surveyed at `/Applications/Logic Pro.app/Contents/Frameworks/MAMachineLearning.framework/Versions/A/Resources/`.

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
