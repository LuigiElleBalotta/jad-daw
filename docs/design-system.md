# Design System (v0.1, draft)

Dark, flat, iOS-inspired look for a DAW with a Logic-style workflow.
Colour values are **estimates sampled from public reference screenshots** and are to be tuned in-app.
Original assets only: icons, textures and names are our own. No Apple assets, logos or product names.

## 1. Principles

1. Flat and dark; depth comes from tone steps, not gradients or skeuomorphism.
2. Content (waveforms, notes, regions) is the loudest thing on screen; chrome stays quiet.
3. Colour = meaning (track identity, state), never decoration.
4. Every value is a token. No literals in QML.
5. HiDPI first (1x/2x/3x), vector assets only.

## 2. Colour tokens

### Surfaces (dark theme, default)

| Token | Value | Use |
|---|---|---|
| `surface.app` | `#0e0e0e` | window background, toolbar |
| `surface.canvas` | `#141416` | tracks area, track headers |
| `surface.panel` | `#1c1c1e` | inspector, dialogs, editor panes |
| `surface.raised` | `#2c2c2e` | cards, rows, buttons (rest) |
| `surface.raisedHover` | `#3a3a3c` | button hover, selected row |
| `surface.lcd` | `#050608` | transport display well |
| `surface.lcdBezel` | `#232730` | display border |
| `border.subtle` | `#ffffff14` | dividers (8% white) |
| `border.strong` | `#ffffff29` | focused/selected outline (16% white) |

### Text

| Token | Value |
|---|---|
| `text.primary` | `#f2f2f7` |
| `text.secondary` | `#a1a1a6` |
| `text.disabled` | `#636366` |
| `text.value` | `#ffb340` (parameter values in plugin UIs) |

### State / accent

| Token | Value | Use |
|---|---|---|
| `accent.primary` | `#2c7dd9` | selection, checkmarks, power-on |
| `accent.primaryHover` | `#4a93e8` | |
| `state.play` | `#30d158` | play button, MIDI regions |
| `state.record` | `#ff453a` | record, armed |
| `state.solo` | `#ffd60a` | solo |
| `state.mute` | `#8e8e93` | mute (dimmed) |
| `state.clip` | `#ff453a` | meter clip |
| `meter.low` / `mid` / `high` | `#30d158` / `#ffd60a` / `#ff453a` | |

### Track / region palette (identity colours)

Each colour has a **fill** (region body, dimmed) and a **solid** (selected, headers, icons).

| Name | Solid | Fill (region) |
|---|---|---|
| Purple | `#8b5cf6` | `#2b205a` |
| Indigo | `#5b6bd6` | `#1f2a5c` |
| Blue | `#3b82f6` | `#134053` |
| Teal | `#14b8a6` | `#0d5353` |
| Green | `#30d158` | `#145a24` |
| Yellow | `#e6c229` | `#6b5a10` |
| Orange | `#ff9f0a` | `#6b3d08` |
| Red | `#ff453a` | `#6b1a16` |
| Pink | `#ff5fa2` | `#6b1f3d` |
| Magenta | `#d946ef` | `#5a1a63` |

Waveform stroke on a region = region solid at 70% opacity.

### Plugin panels (optional gradient)

`plugin.bgTop #0d0423` to `plugin.bgBottom #1d1635` (vertical), title bar `#262626`, header `#383838`.

### Light theme (later)

Same token names, different values. Not part of v0.1.

## 3. Typography

- **Family:** Inter (variable). Fallback: system-ui, sans-serif.
- **Numerals:** tabular (`font-feature-settings: "tnum"`) for LCD, meters, time, BPM, parameter values.
- Monospace for sample/tick readouts is not needed; tabular Inter is enough.

| Token | Size / weight | Use |
|---|---|---|
| `type.lcd` | 22 / 500 | transport display |
| `type.title` | 15 / 600 | dialog and panel titles |
| `type.body` | 13 / 400 | rows, menus, inspector |
| `type.label` | 11 / 500 | parameter labels, track names |
| `type.caption` | 10 / 400 | rulers, region names, meter scale |

## 4. Geometry

- **Spacing scale (px):** 2, 4, 6, 8, 12, 16, 24, 32.
- **Radius:** `r.control` 6, `r.card` 10, `r.dialog` 14, `r.region` 3, `r.pill` 999.
- **Control heights:** compact 22, default 28, large 36.
- **Track height:** 40 / 56 / 80 / 120 (zoom presets).
- **Hit target minimum:** 20 px desktop.
- **Elevation:** only popovers and dialogs cast a shadow (`0 8 24 #00000066`); everything else is flat.
- **Motion:** 120 ms ease-out for hover/press, 200 ms for panel show/hide. No motion in the timeline during playback.

## 5. Window layout

```
+--------------------------------------------------------------+
| Control bar: [areas] [transport] [LCD: bar/beat, BPM, key]   |
+---------+-----------------------------------------+----------+
|Inspector| Toolbar (tools, snap, zoom)             | Browsers |
| Library +-----------------------------------------+ (loops,  |
|         | Track list | Ruler + Regions            |  files)  |
|         |            |                            |          |
+---------+-----------------------------------------+----------+
| Bottom pane tabs: Mixer | Smart Controls | Editor | Flex ... |
+--------------------------------------------------------------+
```

Areas toggle independently from the control bar. Panes are resizable and remember their size.

## 6. Component inventory

Build order is the priority for the MVP.

**P0 (core)**
1. `ControlBar`, `TransportButton`, `LcdDisplay`
2. `Toolbar`, `ToolButton`, `SegmentedControl`, `Popover/Menu`
3. `TrackList`, `TrackHeader` (icon, name, M/S/R, volume, pan)
4. `Ruler` (bars/beats, time, markers, playhead, loop range)
5. `RegionView` (audio waveform, MIDI mini-notes, selected/muted/looped states)
6. `ChannelStrip`, `Fader`, `Knob` (ring style), `PanKnob`, `Meter` (peak hold)
7. `InspectorPanel`, `LibraryBrowser`

**P1**
8. `PianoRoll`, `StepSequencer`, `AudioEditor`
9. `SmartControlsPanel`
10. Plugin chrome: `PluginWindow`, `PresetBar` (power, preset menu, compare/copy/paste/undo/redo)
11. Flex Time/Pitch editor overlays (markers, note blobs)
12. Space Designer-style IR view (envelope, filter curve)

**P2**
13. Score editor, automation lanes, Live Loops grid

## 7. Control specs

- **Knob:** thin arc ring (2.5 px) in `accent.primary` or `text.value`, 270 degree sweep, dark centre (`surface.raised`), pointer line. Drag vertical, shift for fine, double-click to reset.
- **Fader:** thin track (3 px), rounded-rect thumb (`surface.raisedHover`, 1 px `border.strong`), dB scale in `type.caption`. Value box under it.
- **Meter:** segmented 2 px gaps, green to yellow to red, peak hold line 1.5 s, clip indicator latches.
- **Button:** `surface.raised`, `r.control`, no border at rest, `border.strong` on focus. Toggle on = tinted with state colour.
- **M/S/R:** 20x20, letter centred, on = `state.mute` / `state.solo` / `state.record` fill with dark text.
- **Region:** `r.region`, solid-colour header strip with name (`type.caption`), fill body with waveform. Selected = solid body + `border.strong`.
- **Playhead:** 1 px `#f2f2f7`, triangle cap on ruler.
- **LCD:** `surface.lcd` well, `type.lcd` tabular, value groups separated by thin dividers.

## 8. Icons

- Own SVG set, 20x20 grid, 1.5 px stroke, rounded caps, single colour (`currentColor`).
- Track-type glyphs sit on a rounded square filled with the track solid colour.
- Minimum v0.1 set: play, stop, record, loop, rewind, forward, metronome, count-in, pointer, pencil, scissors, glue, eraser, zoom in/out, snap, mute, solo, arm, inspector, library, mixer, smart controls, editor, browser, plus, chevron, power.
- Sources allowed: drawn in-house, or openly licensed sets (e.g. Lucide, ISC; Phosphor, MIT; Tabler, MIT) with attribution recorded in `THIRD_PARTY.md`. Never trace Apple icons.

## 9. QML implementation notes

- Tokens live in one `Theme.qml` singleton (`pragma Singleton`) generated from `tokens.json`, so a future light theme or user theme is a data swap.
- Custom items in C++ (`QQuickItem` / `QSGGeometryNode`) for: waveform, meters, timeline grid, piano-roll notes. QML Canvas is too slow here.
- Waveform data: multi-resolution peak cache (mipmaps) on disk; the view picks the level by zoom.
- Fonts: bundle Inter (OFL) in resources.

## 10. Open questions

- Control bar light variant (Mac-style grey) as a secondary theme: later.
- Final names for our features (do not reuse Apple's: "Smart Tempo", "Flex", "Space Designer", "Drummer", "Alchemy").
- Colour tuning pass once there is a running UI to compare side by side.
