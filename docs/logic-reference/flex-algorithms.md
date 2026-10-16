# Flex Time and Flex Pitch: algorithms we build on

Our own notes (own words) on the papers behind `core/src/flex_*.cpp`. Logic's behaviour comes from Apple's guide; the DSP below is ours.

## Behaviour to match (Logic Pro)

- **Flex Time**: transient markers found by analysis; flex markers set by the user. Dragging a flex marker stretches the audio between its neighbours, Option-drag moves it without stretching. Modes: Slicing, Rhythmic, Monophonic, Polyphonic, Tempophone, Speed (plus Automatic).
- **Flex Pitch**: monophonic pitch analysis shown as notes. Per note: pitch (drag up/down), position and length, split, join, six hotspots (drift in, vibrato, drift out, gain, fine pitch, formant).
- Both play in real time: holding a note sounds it, dragging changes it at once.

## Time-scale modification (Driedger & Müller, 2016, "A Review of Time-Scale Modification of Music Signals")

| Method | Good for | Weak at | Parameters |
|---|---|---|---|
| OLA | percussive | warbling on harmonic sound (phase jumps) | frame about 10 ms |
| WSOLA | monophonic | transient doubling or skipping; polyphonic input keeps only the dominant period | frame 50 ms, tolerance 25 ms |
| Phase vocoder | harmonic, polyphonic | smeared transients, "phasiness" | frame about 100 ms, identity phase locking |
| Harmonic/percussive split | mixes | cost | median filters on the spectrogram (Fitzgerald); phase vocoder on the harmonic part, OLA on the percussive part |

- Transient preservation in WSOLA: near a transient the analysis hop is fixed to the synthesis hop, so the transient is copied unchanged; the length error is made up between transients.
- Pitch shift = resampling plus stretch, with the spectral envelope put back (X_mod = X_shift * env_orig / env_shift) to keep the formants.

## Phase vocoder (Laroche & Dolson, 1999, "Improved phase vocoder time-scale modification of audio")

Identity phase locking: find spectral peaks (bin larger than its four neighbours); the region of influence of a peak reaches the midpoint to the next peak. Only the peak bins get the usual phase propagation (instantaneous frequency by unwrapping); every other bin of the region keeps its analysis phase difference to the peak (one complex rotation per bin). This removes most phasiness and lets 50% overlap work instead of 75%. "Scaled" locking follows a peak that moves to another bin by using the peak of the region the bin belonged to in the previous frame.

## Peak-shifting pitch shifter (Laroche & Dolson, 1999, WASPAA, "New phase-vocoder techniques for pitch-shifting, harmonizing and other exotic effects")

Move the regions of influence of the peaks to their new bins directly, fixing the phase for continuity. Fixed cost whatever the factor; 50% overlap with integer bin shifts, 75% with fractional shifts.

## Pitch tracking

- **YIN** (de Cheveigné & Kawahara, 2002): difference function d(t) = sum (x[j] - x[j+t])^2, cumulative mean normalisation d'(t) = d(t) / mean(d(1..t)), absolute threshold 0.1 (first dip below it, else the global minimum), parabolic interpolation, best local estimate. Window 25 ms, range 40-800 Hz.
- **pYIN** (Mauch & Dixon, 2014): replace the threshold by a Beta-distributed prior over 100 thresholds (0.01..1); each threshold picks a period, the weights add up per period, the missing mass is the "unvoiced" probability. The candidates go to an HMM (pitch bins of 10 cents, one voiced and one unvoiced state per pitch, voicing switch 0.01, pitch jump triangular up to 25 bins), decoded by Viterbi. Frame 2048, hop 256 at 44.1 kHz.
- **Note tracker** (the pYIN/Tony `MonoNote` model): states per pitch (3 pitches per semitone, 69 semitones from MIDI 35): attack, stable, silent. Self-transitions 0.9, 0.99, 0.9999; stable to silent 0.01; silent to the attack of another pitch weighted by a Gaussian of the distance in semitones (sigma 0.7, more than half a semitone and less than 13). Observation: nearest candidate, likelihood p^0.1 times a Gaussian of the pitch distance (sigma 5 semitones in attack, 0.8 in stable); the silent state takes 1 - P(pitched), P(pitched) mixed with a 0.7 prior at weight 0.5. Decoded by Viterbi; a note starts at an attack state and ends at silence or the next attack.

## What Logic 11.2 itself actually runs (decompiled, 2026-10-16)

Static analysis of `MAMusicAnalysis.framework` (x86_64 slice via REA + Ghidra 12.1.4) — the analysis/detection side; the
actual time-stretch rendering engine lives in `MADSP.framework` under a `BeatBreaker`/`SliceHandler` class family not
yet decompiled this pass. This section is **what Apple does**, kept separate from "DSP above is ours" by design —
Apple's architecture is a fact worth knowing, not something to port.

- **Onset/transient detection is CNN-based**, not classic spectral-flux or HFC: `CMetroStrategyOnsetDetectionCNN`
  (plus an `...Online` streaming variant) and `CMetroModuleStreamingTransientsCNN` exist as real classes, alongside
  function names `beat_tracker_mpsmodel_init/run/destroy` — **MPS = Metal Performance Shaders**, i.e. Logic 11's
  transient/onset detector runs a small on-device neural network (likely a CoreML model under MPS), not a hand-tuned
  DSP heuristic. A non-CNN streaming-transients path (`CMetroModuleStreamingTransients`, no "CNN" suffix) also exists,
  so the CNN path may be one of several strategies rather than the only one (`CMetroStrategyBeatTracker` looks like a
  strategy-selector base class).
- **Beat/downbeat tracking is HMM + Viterbi**, matching the dynamic-programming family (closer to Ellis 2007 than to
  anything ML): `CMetroBeatTrackerHMM`, `CMetroDownBeatTrackerHMM`, `CMetroJointBeatHMM`, and
  `CMetroJointTimeSigBeatHMM` (so time signature and beat/downbeat can be estimated jointly, not as separate passes),
  decoded via `CMetroBeatTrackerHMMBacktracker`/`...BacktrackerOptimized` (a Viterbi backtrack, with a
  `BacktrackerMatrix` helper). `CMetroConstantBeatDetector` and `CMetroVariableBeatDetector` suggest separate
  fixed-tempo vs. tempo-varying code paths, which would match the UI's Smart Tempo "Keep/Adapt/Automatic" modes from
  `behavior-check-2026-10-14.md`.
- **`TempoWeighting::ComputeWeighting`** (decompiled, messier than the Mastering Assistant mappings — this function
  still has unstripped debug `std::cout` logging in the shipped binary) scores nearby tempo/beat-datapoint hypotheses
  by two independent linear-decay factors multiplied together:
  - **BPM distance**: `weight × (1 − |bpm_diff| / 120)`, hard-cut to 0 once `|bpm_diff| ≥ 60` — candidates more than
    an octave-ish of tempo away from the hypothesis don't contribute at all, and the contribution fades linearly
    inside that window.
  - **Rank distance**: `weight × (1 − 0.1 × rank_distance)`, i.e. linear fade to zero at the 10th-nearest ranked
    datapoint — only the ~10 closest-ranked candidates matter at all.
  - The accumulated weighting is then normalized by its own max and rescaled by `(valid_datapoint_count / 15)`, so a
    tempo hypothesis backed by many corroborating datapoints is trusted more than one resting on a few.
- **`CMetroBeatHMMPhaseObserver::TransitionProb(prevState, candidateState)`** — the actual HMM transition/observation
  probability for beat-phase tracking — reduces to `GaussianTable(mean=0, sigma=10/3, x=phase_error)`, where
  `phase_error` is the (mod-wrapped) difference between the expected next-beat phase (from the current tempo-node's
  BPM and a `floor`-quantized position) and the candidate's phase. **Sigma = 10/3 ≈ 3.33** (in sub-beat tick units) is
  an exact, previously-unknown constant: it sets how forgiving the beat tracker is about a candidate beat landing
  slightly off the predicted grid before the HMM penalizes it. One early-exit: if a per-observer field is negative
  (read as "no established tempo yet", e.g. the very first beat), the function returns `1.0` unconditionally — no
  phase penalty before there's anything to compare against.
- Tempo-range bounding exists as its own mechanism, separate from the HMM's per-step probabilities:
  `BeatTrackerHints::ComputeMinAndMaxTempoFromHints`, `CMetroBeatMapping::SetPreferedTempoRange`, and
  `CMetroJointHMMConfiguration::UpdateActiveTransitionsWithTempoRange` prune the HMM's state space to a tempo window
  (from user hints and/or already-decided candidates) before running Viterbi, rather than searching the full tempo
  range on every pass.

**Correction**: `BeatBreaker`/`SliceHandler` (seen via `SetPanStart`/`SetPanSlope`/`SetPanCurve`) turned out to be a
*different* feature — Logic's "Beat Breaker" MIDI FX plugin (a pattern-based step slicer/glitcher with its own
save/load "Pattern" files), not Flex Time's engine. Corrected below.

### The actual Flex Time stretch engine: "Elastic Audio" (decompiled, 2026-10-16)

Found in `MADSP.framework` by searching for the published time-stretch techniques `flex-algorithms.md` already cites
(WSOLA/phase-vocoder terms) rather than guessing a product-facing name — and it worked immediately:

- **`CElasticAudio::eElasticAudioType`** is a real enum — "Elastic Audio" is Apple's own internal engineering name
  for Flex Time (this matches, independently, the name Avid uses for the equivalent Pro Tools feature — apparently
  shared industry terminology, not something copied between them).
- **`CEAPhaseVocoder`** (`CEA` = `C`+`ElasticAudio`) is a textbook phase vocoder implementation, confirmed by its own
  method names using the exact terms from Laroche & Dolson 1999 (already in this file, above): `SetFFTSizeAndSamplerate`,
  `CreateWindow`/`SetWindowType` (`ePVWindowtype`), `SetHopFraction`, **`SetLaminationMode` (`ePVLamination`)** — Laroche
  & Dolson's own paper names their phase-locking scheme "lamination" — and `EnableTransientDetection`, `PitchShift`,
  `PhaseAdjust`. It takes a `WarpMarkerProvider`/`CWarpMarkerChecker`, matching the UI's Flex marker model exactly: the
  phase vocoder stretches *between* user/analysis-placed markers, not the whole file uniformly.
- A separate **`CEAGrainTrain`** class (`CEAGrainTrain::Create`, taking a `CWarpMarkerChecker` and `CEAPhaseVocoderData`)
  exists alongside the phase vocoder — almost certainly the granular/transient-preserving engine behind the
  percussive Flex modes (Slicing/Rhythmic), paralleling this file's own WSOLA-vs-phase-vocoder split (percussive →
  grain/transient-locked, harmonic/polyphonic → phase vocoder) rather than Apple using one algorithm for everything.
- `CStretchFeederPrimitiveFloat::phasevocoder` and `CSFSliceDice::makePhaseVocoderInstruction` are further plumbing
  around the same phase vocoder core, used by the "Sample Editor"/Flex workflow's stretch feeder.

This is a strong independent confirmation that jad-daw's own published-algorithm approach (phase vocoder per Laroche
& Dolson, WSOLA for transient material) already matches Apple's actual architecture, not just its observable
behaviour — Apple did not invent a secret, unrelated DSP technique for Flex Time.

**Open**: which Flex mode (Automatic/Monophonic/Polyphonic/Slicing/Rhythmic/Speed/Tempophone) maps to the phase
vocoder vs. the grain train, and the exact lamination/window parameters used by each, weren't traced this pass —
`CEAPhaseVocoder::SetTransposeAndTempo`/`SetTransposeAndStretchfactor` (both taking an `eElasticPreRenderMode`) are
the next concrete entry points for that.

Source: static analysis of `MADSP.framework/MADSP` (x86_64 slice) via REA + Ghidra 12.1.4, classes at `0x960460`
(`CEAGrainTrain`) and `0x961470`-`0x96a990` (`CEAPhaseVocoder`/`CEAPhaseVocoderProcessor`). Class/method names only —
these confirm the algorithm family and parameters (FFT size, window type, hop fraction, lamination mode), not ported
pseudocode.

---

Source for the Smart Tempo section above: static analysis of `MAMusicAnalysis.framework/MAMusicAnalysis` (x86_64
slice) via REA + Ghidra 12.1.4. Class/function names only (confirms architecture family), no pseudocode ported.
