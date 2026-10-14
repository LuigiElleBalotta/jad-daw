# Where the extracted Logic Pro reference material lives

Reverse-engineering pass of 2026-10-14, against a licensed, locally-installed Logic Pro **11.2 (build 6306)** on the
Mac used for this work, with REA (`github.com/morluto/rea`, reverse-engineering MCP tools; Hopper and a self-built
Ghidra 12.1.4 as its two providers) plus direct inspection of the app bundle's resources and a short accessibility-driven
read of the running app's Key Command Assignments window.

Everything extracted — colour tokens, plug-in parameter lists, the legacy mixer bitmaps, live UI screenshots — is kept
**outside this git repository**, in a sibling folder:

```
jadaw/
├── jad-daw/                     (this repo)
└── logic-pro-reference/         (NOT in git — Apple's assets/colours/strings, reference only)
```

See `logic-pro-reference/README.md` (in that folder, not this repo) for exactly what's in there and how it was made.
This file just records *that* it exists and *why* it's not here, so a future session (including on Windows, where
`logic-pro-reference/` obviously won't be present) knows where the source of `color-tokens.md` and the Smart Controls
section of `smart-controls-and-editors.md` came from, and that the underlying files need to be re-extracted on a Mac if
ever needed again.

## What actually made it into this repo (in our own words, as usual)

- `color-tokens.md` — curated, named colour values relevant to the Mixer, Tracks area and LCD, as sRGB hex.
- `smart-controls-and-editors.md` (bottom section) — exact Smart Control knob sets for the built-in plug-ins that
  overlap with jad-daw's own (Compressor, Limiter, Noise Gate, Delay, Reverb).
- `shortcuts-check.md` / `ui/actions/actions.json` — 16 additional shortcuts marked `"verified": true` after reading
  them directly off the running Key Command Assignments window (not just a published source), bringing verified
  defaults from 3 to 19. Two findings worth remembering for whoever does the next pass:
  - Logic has no single "Zoom In" / "Zoom Out" command with a plain `+`/`-` default — it splits into **Zoom Horizontal**
    (⌘→/⌘←) and **Zoom Vertical** (⌘↓/⌘↑), plus separate Individual Track Zoom commands. jad-daw's `view.zoomIn`/`view.zoomOut`
    (`+`/`-`) don't correspond to one single Logic command; worth deciding whether jad-daw's simpler model is intentional
    or should be split the same way.
  - `transport.goToPosition`'s Logic default rendered as the glyph "Ò" in the Key Commands window on this machine's
    keyboard layout — almost certainly an Option+letter combo displayed through a non-US layout, not literally `/`.
    Needs re-checking on a plain US keyboard layout before trusting either value.

## Reproducing or redoing this

1. Logic Pro must be installed locally (it does not run on Windows or Linux) — this is necessarily a Mac-only step.
2. REA: `npx rea-agents setup --client claude_code --yes`, with `GHIDRA_INSTALL_DIR` and `HOPPER_LAUNCHER_PATH` set if
   those providers are wanted (not required for resource/plist/colour extraction, only for actual binary/disassembly work,
   which this pass did not end up needing — Logic's behaviour here was recoverable from its shipped resources and its own
   running UI, not from disassembling its Swift/Objective-C frameworks).
3. Colour tokens: `assetutil -I Contents/Resources/Assets.car`, then a Display P3 → sRGB conversion (see `color-tokens.md`
   for the method).
4. Plug-in Smart Control parameters: `plutil -convert json Contents/Resources/ControlsViewParameters.plist`.
5. Live shortcut confirmation: open Logic Pro, Option-K for Key Command Assignments, type a command name into its search
   field, read the Key column. (On this machine REA's own `observe_native_ui`/`capture_native_ui_scenario` needed a
   source-level timeout bump — its `swiftc` compile step genuinely takes ~90 s on this hardware against a 60 s default —
   before it worked; the fix was applied locally to the installed `rea-agents` package.)
