# Stock plug-in windows — reference for rebuilding jad-daw's own UI

Screenshots live outside this repo, in `../logic-pro-reference/plugin-windows/` (Apple's own UI pixels, not ours
to redistribute — see that folder's own README). Every shot is a clean, isolated capture of just the plugin
window (no desktop, no other Logic windows): `screencapture -x -R<x>,<y>,<w>,<h>` against the window's own
point-space bounds (read via `osascript`'s System Events `position`/`size` of the window), which yields a true
Retina 2x PNG (`width == 2× the window's point width`) with nothing else in frame — more reliable than cropping a
full-screen vision snapshot. Taken on a disposable `Untitled 13.logicx` test project; the user's real
`ProvaPianoforte.logicx` was never touched. Each plug-in gets a `-default.png` (freshly inserted, factory
defaults) and a `-moved.png` (2-3 controls changed, to show live/active visual states — colors, highlights, curve
redraws). Written in our own words; no Apple text reproduced beyond short control labels, which are just names.

## Channel EQ

`plugin-channel-eq-default.png` / `plugin-channel-eq-moved.png`. Inserted on a plain stereo Audio track's first
insert slot.

**1. Layout.** A single floating window, no sidebar/tabs. Top to bottom: a **header bar** (title "Audio 1" =
the channel strip's own name, not the plugin's; preset menu + transport-style prev/next arrows; Compare/Copy/
Paste/Undo/Redo; a `View: 99%` zoom stepper; a link icon, top right), a tall **graph area** (the bulk of the
window — EQ curve plus, per earlier live captures with Analyzer on, a spectrum overlay), a row of **8 per-band
numeric readouts** directly under the graph (one column per band, each 3 values stacked: Frequency / Gain-or-
Slope / Q), a **footer toggle row** (`Analyzer POST`, `Q-Couple`, `HQ`, `Processing: Stereo` menu), and a final
**plugin-identity bar** at the very bottom (large centered text, `Channel EQ` — this is Logic's standard way of
naming which plug-in is loaded, present on every stock plug-in window, not Channel-EQ-specific).

**2. Controls.**
- **Preset menu** (`Default Preset`): dropdown, click opens Apple's factory/user preset list (not opened this
  pass).
- **Prev/Next arrows** either side of the preset name: step through presets; greyed out when there's nothing to
  step to.
- **Compare**: toggle button (blue fill when active — seen in the moved shot after an unrelated click landed on
  it instead of its intended target, so its *existence and active-state color* are confirmed, its exact behavior
  wasn't deliberately tested).
- **Copy / Paste**: plain buttons, Paste greyed out until something's been copied.
- **Undo / Redo**: plain buttons, greyed out with nothing to undo/redo.
- **View: 99%**: a numeric stepper (up/down chevrons) controlling the graph's zoom level; not range-tested.
- **Link icon** (top right of header): toggle, likely stereo channel linking — not tested.
- **8 band filter-type icons**, directly above the graph, one per band: small line-drawings of each filter's
  shape (low-cut curve, low shelf, 2 peak/bell diamonds repeated ×4, a high-shelf, a high-cut curve). Each is
  colored to match its band (band 1/8 neutral gray-white, 2 orange, 3 yellow, 4 green, 5 teal, 6 blue, 7 purple)
  — consistent, not random per-session colors. Clicking one almost certainly opens a filter-type menu (Logic's
  Channel EQ supports switching e.g. band 1 between high-pass/low-shelf); not opened this pass.
- **8 draggable band nodes** on the graph itself: dragging moves frequency (X, log-spaced 20 Hz-20 kHz) and gain
  (Y, ±15 dB) simultaneously; confirmed live — dragging the 250 Hz node up produced `+7.6 dB`, dragging 1040 Hz
  down produced `-8.4 dB`, both read back exactly in the numeric row below and the curve redrew in real time with
  a soft gradient fill under/over the line in the dragged band's own color. Scroll-to-adjust-Q and
  Option-click-to-reset weren't tested this pass.
- **Per-band numeric readouts** (Frequency/Gain/Q, stacked): the active/hovered band's cell gets a filled
  rounded-rect highlight in its band color (seen on the 1040 Hz column, solid teal background) — these are very
  likely also directly editable text fields (double-click to type a value), not confirmed this pass.
- **Right-side vertical Gain scale** (`+15` to `-15`) with its own small circular handle at `0`: a 9th,
  independent **output gain** control separate from the 8 bands — not dragged this pass.
- **Analyzer POST / Q-Couple / HQ**: footer toggle switches (blue-filled when on — `Analyzer`/`Q-Couple` are on
  by default, `HQ` off by default). `POST` superscript on the Analyzer button suggests a pre/post toggle lives
  behind it (not opened).
- **Processing: Stereo**: a dropdown menu (left/right/mid-side processing modes, typical for Logic EQs — not
  opened to confirm the full option list this pass).

**3. Displays.** The graph is a **dual-axis** design: the right-hand `+15`/`-15` dB scale belongs to the EQ curve
and band nodes; the left-hand `0`-`60` scale (increasing downward) is almost certainly the real-time spectrum
**analyzer**'s own amplitude axis (0 dB at top down to a -60 dB-ish noise floor) — a separate, lower-resolution
scale sharing the same plot area as the curve, standard for an EQ-with-built-in-analyzer design. Not
independently confirmed against Apple documentation, inferred from the layout and the `Analyzer` toggle sitting
directly below it. X axis: logarithmic Hz, gridlines at `20/30/40/50/60/80/100/200/300/400/500/800/1k/2k/3k/4k/
6k/8k/10k/20k` — denser below 1 kHz, matching how a log frequency axis bunches up visually. Colors (not
independently confirmed as hex values — read by eye, not pixel-sampled): background a dark navy-blue-gray
(`#1a2230`-ish), gridlines a faint lighter gray, the curve fill a translucent light blue/teal that darkens toward
each band's own accent color near its node.

**4. Fonts / chrome.** All text in Logic's standard system sans-serif (SF Pro or very close to it). Header
control labels and the band-readout numbers use the same weight; the bottom `Channel EQ` identity bar is
noticeably larger/bolder than everything else in the window. No rounded "pill" buttons — Compare/Copy/Paste/
Undo/Redo are plain rectangular buttons with subtle corner rounding (a few px). No Expert/Reduced-view footer
toggle on this plug-in (some others have one — see below).

**5. Animation.** Not tested this pass — would need real audio playing through the channel to see the Analyzer
overlay or any meter animate; the static defaults/moved shots don't exercise this.

### Source
Inserted via the channel-strip insert slot's own selector (`Dynamics/EQ > Channel EQ` menu path from earlier
passes), captured via `screencapture -x -R` against window bounds read with `osascript`'s System Events
(`position`/`size` of the `"Audio 1"` window — Logic names floating plug-in windows after the channel strip, not
the plug-in). Moved state: two band nodes dragged via `mcp__macos-mcp__Move` (position-then-drag pattern), one
footer button clicked.

## Compressor

`plugin-compressor-default.png` (Platinum Digital, factory default) / `plugin-compressor-moved.png` (a few
controls changed) / `plugin-compressor-circuit-{studio-vca,studio-fet,classic-vca,vintage-vca,vintage-fet,
vintage-opto}.png` — one clean shot per remaining Circuit Type, all 7 now covered (default.png *is* the 7th,
Platinum Digital).

**A tooling note, since it shaped how every later plug-in in this doc was captured**: the Logic insert-slot
"choose a plug-in" popup and the circuit-type submenu genuinely do open on click, but
`mcp__macos-mcp__Snapshot`'s own accessibility-tree query appears to dismiss them before it can report back —
every Snapshot taken right after such a click showed the popup already gone, even though the click itself had
worked. Confirmed by instead taking a screenshot via a direct `screencapture -x` (no accessibility query) in the
same instant: the popup was there. From this plug-in on, verifying *menu/popup* state uses `screencapture`
directly instead of `Snapshot`; `Snapshot` is still used freely for checking ordinary window state where nothing
transient is open.

**1. Layout.** Same floating-window shell as Channel EQ (header: preset menu, prev/next, Compare/Copy/Paste/
Undo/Redo, `Side Chain: Internal` menu + `View: 100%` + link icon). Below that, two full-width rows: the **7
Circuit Type buttons** (`Platinum Digital`/`Studio VCA`/`Studio FET`/`Classic VCA`/`Vintage VCA`/`Vintage FET`/
`Vintage Opto`) on the left, a 2-button **Side Chain / Output** view switcher on the right. The main body is a
**3-column control layout**: left column (Input Gain meter + knob), center column (a large VU-style Gain
Reduction meter with Meter/Graph tabs, then Threshold/Ratio/Make Up/Auto Gain + Knee/Attack/Release under it),
right column (a secondary Output meter, Limiter section with its own Threshold knob, Distortion knob, Mix and
Output Gain knobs). Plugin-identity bar `Compressor` at the bottom, same style as Channel EQ's.

**2. The single most important finding: each Circuit Type is a full re-skin, not a parameter swap.** Clicking a
different circuit changes the entire window's color scheme and meter/knob art, live: `Platinum Digital` = cool
blue brushed metal with a digital-looking VU meter; `Studio VCA` = bright red brushed metal, cream VU meter;
`Studio FET` = near-black brushed metal, amber/gold VU meter, amber-highlighted active-circuit label text;
`Classic VCA` = dark gunmetal, pale meter with a **blue** needle/scale (the only circuit whose meter scale isn't
cream/gold) and blue-outlined Meter/Graph tabs; `Vintage VCA` = light gray-silver chassis with genuinely different
**blue-capped knob graphics** (every other circuit uses plain brushed-silver/black knobs) and a dark meter;
`Vintage FET`: not independently re-described here but confirmed captured; `Vintage Opto` = light warm-gray
chassis, cream meter, orange/amber accent color on active toggle labels (`ON`, `-12 dB`, `AUTO`). This is a
**direct, load-bearing fact for jad-daw's own rebuild** (`G2` in the roadmap: "reusable controls... colours from
the colour tokens") — a single generic Compressor skin can't represent this plug-in; it needs (at minimum) a
per-circuit color/asset theme, not just per-circuit DSP.

**3. Controls** (common to every circuit, positions identical, only art changes):
- **Threshold** (center, `-50`…`0 dB`), **Ratio** (`1`…`30 :1`, labeled `:1` beneath), **Make Up** (`-20`…`50 dB`),
  **Knee** (`0`…`1.0`), **Attack** (`0`…`200 ms`), **Release** (`5 ms`…`5k ms`, i.e. 5 s): all large rotary knobs,
  drag vertically to change (confirmed live — dragging the Ratio knob up showed a **live numeric overlay**,
  `4.8:1`, centered where the static `RATIO` label normally sits, replacing it only while the value is away from
  a clean label position — a real interactive detail to replicate). Scroll-to-adjust and Option-click-reset not
  tested.
- **Auto Gain**: a 3-way vertical button stack (`Off`/`0 dB`/`-12 dB`, mutually exclusive, `-12 dB` is the
  default) — each has its own small round LED-style indicator lit when active (confirmed via the `Vintage VCA`
  shot, which renders these as hollow squares + separate red LED dots rather than filled buttons — the exact
  widget style is itself circuit-dependent, another re-skin detail).
- **Input Gain** / **Output Gain** (bottom-left / right column, `-30`…`30 dB` knobs) and their own **peak meters**
  above them (`-∞ dB` readout, `+3` to `-60` scale, twin vertical bars).
- **Meter / Graph** tab switcher inside the big central display: `Meter` (the VU needle, default) vs `Graph`
  (not opened this pass — likely the gain-reduction-over-time curve used elsewhere in this doc's Block 2g
  research).
- **Side Chain / Output** button pair, top right: switches the right-hand column between the Limiter/Distortion/
  Mix/Output-Gain controls shown here (`Output` view, the default) and presumably sidechain-filter controls
  (`Side Chain` view — the real State/Mode/Frequency/Q/Gain set already fully documented from static analysis in
  `mac-pass-3.md` Block 2g; not re-opened live this pass since it's already known).
- **Limiter**: `ON`/off button with its own status LED, `Threshold` knob (`-10`…`0 dB`) — a hard limiter stage
  after the compressor, confirmed toggleable live (LED lit after clicking `ON` in the moved shot).
- **Distortion**: a single knob sweeping `Off`…`Soft`…`Hard`…`Clip` (one knob covering what reads as 4 named
  positions, not 4 separate controls).
- **Mix**: `Input`…`Output` knob (dry/wet, i.e. parallel compression blend).

**4. Displays.** The central VU meter is the plug-in's visual centerpiece: a skeuomorphic analog-style arc gauge,
`-50` to `0` dB, non-linear tick spacing (denser near 0) — a deliberately "vintage hardware" look that is
**consistently styled per-circuit** (color changes, the needle/scale art stays the same arc geometry across all
7). The twin peak meters either side (Input/Output) are plain linear vertical bar meters, `+3` to `-60` dB.

**5. Fonts / chrome.** Circuit-button labels: 2-line stacked text (e.g. "Studio\nVCA"), the active circuit's
button gets a filled/pressed look plus, on the darker-themed circuits, a colored (not just white) label — amber
on Studio FET and Vintage Opto specifically, suggesting the active-label accent color is itself part of each
circuit's theme, not a single global "selected" color. No Expert/Reduced-view toggle seen on this plug-in either.

**6. Animation.** The live numeric-overlay-while-dragging behavior (point 2 above) is the one confirmed animated/
reactive detail this pass; meter needle movement needs real audio, not tested.

### Source
Inserted on the same disposable `Audio 1` track's insert slot, chosen from the plug-in browser's `Dynamics >
Compressor` entry (the browser opened fine on click — see the tooling note above; it was *checking* its state
with Snapshot that failed, not opening it). All 7 circuit buttons and the Limiter toggle clicked via
`mcp__macos-mcp__Click`; Threshold/Ratio knobs adjusted via `mcp__macos-mcp__Move`'s position-then-drag pattern.
Every capture via `screencapture -x -R` against the window's `osascript`-read bounds.

## Limiter

`plugin-limiter-default.png` / `plugin-limiter-moved.png`. Window 729x378 pt, shown at `View: 75%` (the default zoom for
the small dynamics plug-ins, unlike Channel EQ/Compressor at 99-100%).

**1. Layout.** Same standard header as every stock plug-in (power button top-left, preset menu with prev/next, Compare/
Copy/Paste, Undo/Redo, `View` stepper, link icon). Body split in two: a left **meter block** (~290 pt wide: three
vertical meters `INPUT` / `REDUCTION` / `OUTPUT` with their dB scales) on a slightly darker navy panel, and a right
**control block** with 4 knobs in one row (`Gain`, `Release`, `Output Level`, `Lookahead`) and, under them, a `Mode`
menu and a `True Peak Detection` toggle. Identity bar `Limiter` at the bottom.

**2. Controls.** Gain `-20…+20 dB` (default 0.0), Release `2…2000 ms` (default 250 ms), Output Level `-10…+1 dB`
(default 0.0), Lookahead `0…10 ms` (default 5.0 ms): all small rotary knobs drawn as a thin arc that fills from the
minimum (Release/Lookahead) or from the center (Gain) up to the value; a big numeric readout with unit sits *above* each
knob, the label above that, and the min/max numbers *below* the knob ends. `Mode: Precision` (menu with up/down
chevrons, default `Precision`), `True Peak Detection` (text toggle, `ON` by default, turns `OFF` on click — the toggle
is a plain word, not a button shape). Confirmed live: dragging Gain up showed `+5.3 dB` and the arc re-filled,
Release dragged down to `110.0 ms`, True Peak clicked to `OFF`.

**3. Displays.** Three vertical bar meters, scales `20…-60 dB` (Input and Output) and `0…-60 dB` (Reduction, hanging
downward from 0); no curve display. Meters were empty (no audio playing), so colour of the lit segments wasn't
observed.

**4. Chrome.** Dark navy panel (~`#14263d` by eye, not pixel-sampled) with lighter blue knob arcs and white value text;
labels in a muted gray-blue, small. After any change the header `Compare` button turns blue (it is the "differs from the
saved preset" indicator, not a toggle you pressed).

**5. Animation.** Not observed (needs audio).

## Adaptive Limiter

`plugin-adaptive-limiter-default.png` / `-moved.png`. 654x378 pt, `View: 75%`. Same skeleton as Limiter (meters on the
left, 3 knobs on the right) but the lower half has a different set of switches.

**Controls.** `Gain` `0…12 dB` (default 3.0 dB), `Out Ceiling` `-2…0 dB` (default 0.0), `Lookahead` `20…200 ms`
(default 50 ms); below them `Remove DC Offset` (toggle, default `ON`), `True Peak Detection` (toggle, default `OFF`),
`Optimal Lookahead` with a live readout `20 ms` and an `Apply` button (copies the suggested value into Lookahead — the
"optimal" number comes from the plug-in's own analysis). Moved: Gain `4.6 dB`, Lookahead `71 ms`, DC Offset `OFF`.
The header has no Side Chain menu. Everything else (meters `INPUT`/`REDUCTION`/`OUTPUT`, colors, fonts) as Limiter.

## Noise Gate

`plugin-noise-gate-default.png` / `-moved.png`. 772x378 pt, `View: 75%`. The header adds a `Side Chain: None` menu.

**1. Layout.** Three vertical groups separated by thin lines: left **Threshold group** (a `Gate | Ducker` 2-way tab at the
top, then two large knobs `Threshold` and `Reduction`, then a `Hysteresis` slider under them); middle **Timing group**
(an `Open ●  Close ●` pair of status LEDs on top, three knobs `Attack`, `Hold`, `Release`, and a `Lookahead` slider
below); right **Filter group** (`Characteristics` menu, `Monitor` toggle, `High Cutoff` and `Low Cutoff` horizontal
sliders with a small power icon at their left edge that enables the sidechain filter).

**2. Controls.** Threshold `-100…0 dB` (default -50), Reduction `-100…0 dB` (default -100), Attack `0…100 ms` (3), Hold
`0…1 k ms` (40), Release `0…10 k ms` (10.0), Hysteresis slider (default -3.0 dB), Lookahead slider (0.0 ms),
Characteristics `Bandpass`, High Cutoff `20000 Hz`, Low Cutoff `20 Hz`, Monitor `OFF`. **Gate | Ducker tab**: clicking
`Ducker` relabels the second knob from `Reduction` to `Ducking` (same parameter slot, different meaning) — confirmed
live. Moved: Threshold `-63 dB`, Ducking `-20 dB`, Attack `14 ms`, Ducker tab selected. The Open LED lights green when
the gate opens; in the Ducker shot it was lit (no signal → ducking state), Close LED dim.

**3-5.** No curve; the two LEDs are the only live displays. Knobs here are the same thin-arc style as Limiter but
larger (~40 pt) and the Threshold arc is bright green (the gate-open color), unlike the blue arcs elsewhere.
Sliders are thin tracks with a small round thumb and a centered readout (`-3.0 dB`) to the right of the label.

## Match EQ

`plugin-match-eq-default.png` / `-moved.png`. 1028x535 pt, `View: 100%`, header with `Side Chain: None`.

**1. Layout.** Header, then a **graph area** (~420 pt tall) occupying most of the window, then a **3-stage pipeline
bar** (`Current` → `Reference` → `EQ Curve`, each a chevron-shaped segment with its own small red button `Learn`,
`Learn`, `Match`), and a bottom control row. A thin vertical **Apply** slider with `+100 %` readout sits at the far
right of the graph. A `Fade Extremes` checkbox at the top-left of the graph area.

**2. Controls.** Bottom row: `Analyzer` toggle with `PRE` tag, `Smoothing` slider (`1…?`, default 4.0, moved to 24.0),
`Channel: L&R` menu + `Hide Others` checkbox (disabled when L&R), `Channel Link` slider (`100 %`), `Phase: Linear` menu.
The three pipeline segments each have a gear icon at the left edge (the Action menu mentioned in the on-screen hint:
load/save/clear the spectrum) and a red `Learn`/`Match` button that arms capture. The text overlay in the graph area
explains how to start (click Learn, or drag an audio file onto Current/Reference).

**3. Displays.** Left axis `0…-60 dB` for the spectra, right axis (very dim until a curve exists) for the EQ curve,
X axis log `20…20k` with grid at 50/100/200/500/1k/2k/5k/10k/20k. With nothing learned the graph is empty except a
flat `0` line. The `Current` segment is teal (active) while `Reference` and `EQ Curve` are the dim blue of inactive
steps. Live behaviour (spectra filling in, the matched curve) needs audio and was not captured.

**4-5.** Standard header; the large dark-navy graph is darker than the control panel below it. Moved shot differs only
in the Smoothing thumb (4.0 → 24.0).

### Source (these four)
Opened on the same `Audio 1` insert chain by the owner (the plug-in browser is awkward to drive from automation); all
captures via `screencapture -x -R` on the windows' `osascript`-read bounds. The four windows do not overlap each other,
so no crop contains another plug-in. Knobs dragged vertically and toggles clicked via `mcp__macos-mcp__Move`/`Click`.

### Match EQ, live behaviour (addendum, captured with audio playing)

Files: `plugin-match-eq-learning-current.png`, `plugin-match-eq-reference-action-menu.png`, `plugin-match-eq-reference-armed.png`,
`plugin-match-eq-learning-reference.png`, `plugin-match-eq-eq-curve-stage.png`. Playing the `Ambition Beat` loop on the
channel while the stage's `Learn` button is armed.

- **Each pipeline stage has its own colour**, and the active stage's chevron is filled with it: `Current` teal, `Reference` purple,
  `EQ Curve` olive-gold. The spectrum drawn in the graph takes the same colour (teal fill for Current, purple fill for Reference),
  with a thin light line on top of the filled area that is the faster, un-averaged live level (two nearly overlapping outlines
  were visible: the filled average and the live trace).
- **Learn is a hold-to-record style toggle**: the button turns bright red while armed and the filled spectrum grows/settles over
  ~5 s of audio; pressing the button again (or stopping transport and clicking it) ends the capture. Re-arming `Learn` on a stage
  that already has a spectrum clears and restarts it (the first Current capture disappeared when I clicked Learn a second time).
- **Spectrum axes**: while learning, the left scale `0…-60 dB` is used. Once the `EQ Curve` stage is active, the **right-hand scale
  changes to `+30…-30 dB`** (the correction curve's gain axis, 0 dB at the same height as the `-30` on the left), the correction
  line is a thin yellow line (flat here because Current and Reference were made from the same material), and the analyzer stays
  as a thin white trace behind it.
- **Gear (Action) menu of each stage**: for `Reference` it lists (greyed text until hovered): `Clear Reference Spectrum`,
  `Copy Reference Spectrum`, `Paste Reference Spectrum`, `Load Reference Spectrum from setting file...`,
  `Generate Reference Spectrum from audio file...`. Clicking the last two with synthesized clicks did not open the file chooser
  (menu items did not react to simulated mouse clicks; arrow-key + Return closed the menu without a dialog), so those two flows
  were not captured.
- Transport: `Return` goes to the start and `Space` toggles play (Logic key commands); a leftover spectrum stays on screen after
  stopping.

## Space Designer

`plugin-space-designer-default.png` / `-moved.png`. 1028x797 pt, `View: 100%`. A **hybrid dark/light window**: dark
top half (graph), light-gray bottom half (controls) — the only stock reverb here with that split.

**1. Layout.** Header (power, preset menu, Compare/Copy/Paste, Undo/Redo, View, link; no Side Chain menu), then a
full-width **mode tab bar** `SAMPLED IR | SYNTHESIZED IR` (the active tab is the light one with orange text, the inactive tab is
mid-gray), a **toolbar row** (`Volume Env` and `Filter Env` each with a power icon, an IR file menu `1.8s_Large & Brite`, a `Stereo`
label, `Output EQ` with a power icon), the big **envelope/waveform display** (about 470 pt tall), a readout line under it
(`Init Level`, `Attack`, `Decay`, `LIN | EXP`, `End Level`), a thin **options strip** (`Quality: High`, `IR Offset: 0.0 s`, a
`Reverse` button, a gear menu at the right) and the **control strip** (light gray, ~210 pt): `Input` (a vertical slider with 3
small icons = stereo/mono-ish input routing), `Predelay` (+ a note icon = sync to tempo toggle), `Length`, `Size` (knobs),
`X-Over` (vertical slider, `200 Hz`…`16k`), `Lo Spread`, `Hi Spread` (knobs), `Dry`, `Wet` (vertical sliders). Bar `Space Designer`.

**2. Controls and defaults.** Predelay `8 ms`, Length `1.30 s`, Size `100 %` (`10…1000`), X-Over `230 Hz`, Lo Spread `38 %`,
Hi Spread `100 %`, Dry `0.0 dB`, Wet `-10.0 dB`, Quality `High`, IR Offset `0.0 s`. Active values and unit labels are orange
(`#f4623a`-ish by eye), inactive/grey text for non-value labels. The knobs are large white-faced dials with a thick orange arc
from the minimum to the value and a thin line pointer; sliders are thin vertical tracks with a small rectangular white thumb
with a red-orange line and an orange fill below the thumb. Moved: Predelay `127 ms`, Length `823 ms` (changing Length
**rescaled the horizontal time axis of the graph** and redrew the volume envelope: the axis went from `0.0…1.2 (s)` to
`0.0…800 (ms)` and the envelope's `Decay` readout changed from `1.22 s` to `774 ms`), and `Filter Env` was turned on (its power
icon becomes yellow, unlike the orange of the others).

**3. Displays.** The center graph shows the impulse-response waveform (dark blue-gray decaying noise burst, left to right) under the
blue **volume envelope**: a filled blue area under a bright-cyan curve with three round cyan handles (start top-left, an attack
point at the bottom near `76.6 ms`, an end point at the right at `30 %`); dragging handles edits Init Level / Attack / Decay /
End Level, whose current numbers are the readout line. X axis in seconds (ms after Length is shortened), light vertical
gridlines every `0.2 s`.

**4-5.** Fonts as elsewhere; the tab labels `SAMPLED IR` / `SYNTHESIZED IR` are large (~22 pt), letter-spaced uppercase.
Not tested: Synthesized IR tab, Filter Env/Output EQ editors, IR file menu, the gear menu, reverse.

## ChromaVerb

`plugin-chromaverb-default.png` / `-moved.png`. 1028x796 pt, `View: 100%`. Same dark-over-light structure as Space Designer
but a different layout and two **views**: `MAIN` (default) and `DETAILS`.

**1. Layout (MAIN).** Header (power, preset, Compare/Copy/Paste, Undo/Redo, `Side Chain` absent, View, link). Top strip: `DAMPING EQ`
(left, label), a centred **Room-type selector** showing the current room name in orange (`Room`), the `MAIN | DETAILS` switch
(right). Graph (dark, ~430 pt tall): the **damping curve** (orange line on a dark grid, decay time as a percentage of the global
decay `20 %…200 %` on the left axis, absolute seconds on the right axis `0.2 s…2.2 s`) with 3 grey control points; bottom-right of
the graph a small orange **reverb-visualiser icon** (a dotted orb). Light-gray control strip: `Attack` (`0 %`), `Size`
(`60 %`), `Density` (`60 %`), `Decay` (`1.10 s`, bigger knob, `0.3…100`, with a `Freeze` button under it), `Distance`
(`50 %`), `Dry` (`100 %`) and `Wet` (`50 %`) sliders, plus `Predelay 8 ms` (knob-less readout with a note/sync icon at the left).
**DETAILS view** replaces the graph with the **Output EQ** (6 bands: low cut, low shelf, 2 bells, high shelf, high cut, same
band-icon row and colours as Channel EQ's, `+24…-24 dB` on both sides, log `20…20k`) and the lower strip with `Quality: High`,
`Mod Speed` (`0.50 Hz`), `Mod Depth` (`25 %`), `Mod Source` (3 icon buttons: sine, square/step, random/noise — the square one
selected, in orange), `Smoothing` (`12 %`), `Early/Late` (`50 | 50`, a vertical slider whose scale shows `0 100` at top and
`100 0` at the bottom, i.e. the two ends mean opposite things), `Width` (`100 %`), `Mono Maker` (power icon + `20 Hz` greyed out).

**2. Controls.** Knobs (Attack, Size, Density, Decay, Distance) are white-faced with a thick orange arc; sliders are vertical.
Orange = active value / selected state, same palette as Space Designer. Moved: Size `60 → ~65 %`, Decay `1.10 → ~1.7 s`
(values in the shot were read before DETAILS was opened; the DETAILS view is what the moved shot shows).

**3. Displays.** MAIN: the damping curve is editable by its three grey points (curve in orange, previous/alternate curves thin
gray). DETAILS: the Output EQ curve fills with a translucent violet area under the curve when a band is active (here the
high-cut band, with its violet node at 10-12 kHz).

**4-5.** Tab/segment labels `MAIN` / `DETAILS`: the active one orange on dark. The Room-type selector (`Room`) is the plug-in's
main identity element and is the big visual accent of the top strip (orange text on a slightly lighter dark rounded box).
Not tested: the room-type popup (Room/Chamber/Hall/…), Freeze, Mono Maker, Mod Source other than the selected one.

### Source (Space Designer, ChromaVerb)
Both opened by the owner. ChromaVerb extended 12 pt beyond the right screen edge so its window was moved left with
`osascript` (`set position of window`) before capturing; the Space Designer window was raised by clicking its title bar so
ChromaVerb's overlapping 11 pt did not cover its right edge, and `Compare` was toggled to recapture the factory-state
(default) shot after the moved one (Compare switches between the edited and the stored settings).
