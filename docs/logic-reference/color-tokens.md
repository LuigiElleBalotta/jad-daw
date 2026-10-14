# Logic Pro 11.2 (build 6306): colour tokens

Logic's current UI (the vector/Metal-drawn chrome, not the legacy TIFF bitmaps still shipped for backward compatibility —
see `asset-reference.md`) is built from about 836 named, semantic colour tokens compiled into `Assets.car` (Apple's
compiled asset catalog format), extracted on this machine with `assetutil -I` plus a Display P3 → sRGB conversion (Apple
stores these in the P3 colour space; naively reading the raw components as sRGB gives visibly wrong hues). The full set
(name, raw P3 components, converted sRGB hex) is in the external reference folder, not in this repo:
`logic-pro-reference/color-tokens/logic-pro-11.2-color-tokens.json` (see `asset-reference.md` for the folder's location
and why it stays out of git).

**One dark theme, not light/dark adaptive.** The catalog's own appearance breakdown is 8 colours under Aqua (light) vs.
1 under Dark Aqua vs. 0 under "System" — i.e. Logic's main workspace does not follow the macOS appearance setting, it is
always the one dark theme. `docs/design-system.md` already commits jad-daw to a single dark workspace theme; this
confirms that is the right call, not just a stylistic choice of ours.

## Curated values relevant to jad-daw (sRGB hex, alpha where it matters)

### Mixer / channel strip
- Channel strip background `#151517`, selected `#747481` at 18% alpha (a translucent overlay, not a flat colour)
- Divider between strips `#222224`
- Fader thumb `#76767A` at 80%, fader track `#747481` at 7% (very low-contrast groove, the thumb carries the contrast)
- Pan knob fill `#00D543`; Send knob fill `#007AFF` (pre-fader) / `#0086FF` at 30% (post-fader send, dimmed `off` state
  lives at low alpha rather than a different hue — same idea for post-pan sends: `#28CD41` on, `#00D543` at 20% off)
- Mute button lit `#009BC1` (cyan-blue — matches what the Mixer doc already recorded as "blue"); Solo lit `#B99400`
  (amber/gold, matches the doc's "yellow"); Record-enable lit `#D51F21`; Input-monitoring lit `#DB6500`
- Level numbers `#EBEBF6` at 40%; level-meter background `#747481` at 7%

### Automation modes (strip's Automation slot)
Exact colour per mode — useful since jad-daw's Mixer already implements Off/Read/Touch/Latch/Write:
- **Read** `#00AE0A` (green) · off `#747481` at 12%
- **Touch** `#B99400` (amber)
- **Latch** `#DB6500` (orange)
- **Write** `#983CB9` (purple)

### Transport / LCD
- Background: `#0E1012` normal, `#1C0A09` while recording (both near-black, record tints warm instead of literally
  turning red)
- Digit fill: `#C7E8FF` normal (ice blue), `#FFBDBB` while recording (pink/red) — matches what every Logic screenshot
  shows for the LCD; `docs/logic-reference/lcd.md` can cite these directly instead of "looks blue/red"
- Overload indicator `#FF3B30` (the standard system red)

### Tracks area
- Workspace/track background `#0E0E0F`, divider `#1C1E20`, selection overlay `#747481` at 7%
- Automation line `#FFFFFF` at 10% (primary) / 5% (secondary) — automation curves are drawn as a *faint white* line over
  the track colour, not a saturated colour of their own
- Solo tint on a region `#FFD200` (matches the Mixer doc's "yellow playhead/solo" observation)

### General surfaces
- Workspace `#0E0E0F`, toolbar/control-bar `#222225`, widget background `#18181A`
- Inspector label text `#FFFFFF` at 60%, value text `#FFFFFF` at 85% (two different white alphas, not two different
  greys — worth matching exactly since it's a cheap, high-fidelity detail)

## Known-odd entries (do not use without re-checking against a screenshot)

`VCSLevelmeterGreen/Yellow/Orange/Red` extract (after the same P3→sRGB correction) to `#904951`, `#1E6165`, `#006265`,
`#004F66` — none of which are plausibly green/yellow/orange/red. Either these four tokens are not literal fill colours
(used with a blend mode, or paired with a gradient/mask we didn't extract), or "VCS" is a legacy component no longer
used for the main level meter. **Do not adopt these for jad-daw's meter colours.** The meter screenshots taken live
(see `asset-reference.md`) are the better reference for actual meter segment colours until this is resolved.

## Method (so this can be redone after a Logic update)

```
assetutil -I "/Applications/Logic Pro.app/Contents/Resources/Assets.car" > assets-metadata.json
```
then, per `AssetType == "Color"` entry, convert `Color components` (P3, already alpha-including) to sRGB with the
standard Display-P3 → XYZ (D65) → sRGB matrices and the shared sRGB transfer function (Display P3 uses the sRGB OETF/EOTF,
not a pure gamma curve) — the script used is not checked into this repo (one-off), but the maths is standard and cheap
to redo if someone wants a refreshed extraction for a newer Logic Pro build.
