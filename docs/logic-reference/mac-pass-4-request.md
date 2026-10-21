# Mac pass 4: request (graphical windows of the stock effects)

For the Claude session that runs on the Mac with Logic Pro 11.2. Same safety rules as before: never edit or save the user's project, never touch a real `.logicx`
(use a disposable new project), wait 3-4 s after each click, undo every change, commit only notes and screenshots under `docs/logic-reference/` or into
`../logic-pro-reference/` (screenshots stay out of git: Apple's artwork).

Goal: jad-daw's stock effect windows are only parameter lists. We need to rebuild Logic's real windows. For each plug-in below, open its window from a channel strip
insert (Retina, 2x, whole window, no other windows in the shot) and save a PNG named `plugin-<name>-default.png`; then move 2 or 3 controls and save
`plugin-<name>-moved.png`. Save into `../logic-pro-reference/plugin-windows/`.

Plug-ins: Channel EQ, Compressor (and switch the Circuit Type through all 7: one shot each), Limiter, Adaptive Limiter, Noise Gate, Match EQ, Delay Designer or Stereo Delay
(the simple one), ChromaVerb, Space Designer (only the default window), Neural Saturation (`Saturation`), Mastering Assistant window (on Stereo Out), Alchemy and
Retro Synth (the default windows), the Drum Kit Designer / Drummer editor if it opens from a track.

For each window also write, in `docs/logic-reference/plugin-windows.md`, in our own words:
1. the layout (areas, sizes in points relative to the window, which controls sit where);
2. every control: type (knob, slider, switch, menu, display), label, range, default, unit, how it responds to drag/scroll/double-click/Option-click;
3. the displays: what a curve or meter shows (axes, ranges, grid lines, colours as hex when readable from a zoomed screenshot), what is live (input level, gain reduction, spectrum);
4. fonts, corner radii, the header (preset menu, Compare, Copy/Paste, Undo/Redo, A/B) and the footer (view toggles, "Expert/Reduced view");
5. anything animated (meters ballistics, knob highlight on hover).
Zoomed crops of knobs, switches and meters (unscaled) help most: save them next to the full shots.
