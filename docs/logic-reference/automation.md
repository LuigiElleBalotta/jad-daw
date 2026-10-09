# Logic Pro 11.2: automation in the Tracks area

Seen on 2026-10-09. Toggle: the **automation** icon in the Tracks area bar (first of the three blue toggles after the view icons; shortcut A, Mix > Show
Automation).

## What changes when it is on

- Every track header grows a **second row** (the Flex row of audio tracks is replaced by it): an expand arrow `›` · automation **mode** popup (`Read`, green text) ·
  **parameter** popup (`Volume`, yellow text) · **value** field (`+0,0 dB`, `-2,7 dB`).
- The first row gets a **Region | Track** toggle button (blue) next to M/S/R: choose whether the shown automation is stored on the region or on the track
  (audio track showed `Track`, the audio region row `Region`; instrument `Track`).
- In the workspace the waveform dims, and the automation lane appears: the track's current value shown as a faint label (`-2,7 dB`, `+0,0 dB`) and the curve
  drawn on top; the track gets a taller lane (the rows grow from 64 to about 75 px).
- The expand arrow `›` opens extra lanes for more parameters.

## Parameter popup (instrument track, top to bottom)

✓ **Volume** · (sep) **Display off** ⇧⌘Y · **Cycle Through** ⌘Y · (sep) header "Automation": **Smart Controls ▸** · **Volume** · **Main ▸** · **1 AmpliTube 5 ▸** (the
instrument plug-in's parameters, one submenu per plug-in) · (sep) header "MIDI": **MIDI Channel ▸** · **MIDI Volume** · **MIDI Pan** · **Modulation** · **Expression** ·
**Sustain** · **MIDI Control 0-63 ▸** · **MIDI Control 64-127 ▸** · **Pitch Bend** · **Aftertouch** · **Program Change**.
(An audio track shows the "Automation" block without the MIDI block.) So plug-in parameters and every MIDI controller are automatable lanes.
