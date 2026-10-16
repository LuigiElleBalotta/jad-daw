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

## Related settings

Settings > View > Mixer: show Mastering Assistant button, open plug-in window on insertion, show recent plug-in list, peak hold 800 ms, IEC Type I return
time. See `settings-and-app-menu.md`.
