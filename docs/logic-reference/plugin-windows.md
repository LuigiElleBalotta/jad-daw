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
