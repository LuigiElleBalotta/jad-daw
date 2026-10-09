# UI-B: deferred items (from the final review)

## Interactions and minor findings of UI-B
Done (spec `specs/2026-10-09-ui-b-followups-design.md`, plan `plans/2026-10-09-ui-b-followups.md`): Shift-click bus view, insert reorder and
move between tracks, insert bypass, send pre/post, Library from the instrument slot and the header, Smart Controls splitter, and the
eight minors (knob cancel, stale insert drag, routing targets without cycles, catalogue problems, revert message, patch list scroll,
Save and Delete disabled, send knob range).

## Tracks area vs Mixer
Done: `Track.showInTracks`, "New Bus" in the Send and Output menus, Track > Show in Tracks Area (spec `specs/2026-10-09-tracks-area-visibility-design.md`).

## Not verified
- Drag feel with a real mouse beyond the short pass done with windows-mcp (reorder, move to another track, bypass); resizing; DPI
- CI for the UI-B commits (not passing at merge time, ignored on purpose)
- By-hand pass of the plug-in window with third-party plug-ins (drag the slider, resize, DPI, close while playing) and of the Qt and JUCE shared message loop beyond the spike

## Next
- VST3 hosting as effects: done (spec `specs/2026-10-09-vst3-hosting-design.md`, plan `plans/2026-10-09-vst3-hosting.md`)
- Tracks area visibility of buses: done
- Deferred interactions and minor findings: done
- Open: `captureState()` returns "" both when there is no live instance and when the plug-in state is legitimately empty
- Not done: an insert menu with the other effects, plug-in instruments, automation of plug-in parameters
