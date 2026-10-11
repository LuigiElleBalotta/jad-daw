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
