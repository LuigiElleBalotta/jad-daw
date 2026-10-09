# Logic Pro 11.2: the LCD

Seen on 2026-10-09 (Beats & Project mode). See also `ui-observations-2026-10-09.md` for the display modes and the customize panel.

## Parts (left to right)

1. **Position** (large, `102 1`): bar and beat, with the captions `BAR` and `BEAT` under it. (The mode decides what shows here: SMPTE time, beats, etc.)
2. **Tempo** (`138`) with the captions `ADAPT` / `TEMPO` under it. `ADAPT` appears when the project tempo is following the detected tempo
   (Smart Tempo "Adapt"); the other states are KEEP and OFF.
3. **Time signature** (`4/4`) above the **key** (`Cmaj`), with a small popup chevron at the right edge of the LCD.

## Interactions (verified unless marked)

- **Click the key** (`Cmaj`): a popup lists keys around the current one in circle-of-fifths order. Major keys first (A, D, G, ✓C, F, B♭, E♭, A♭,
  D♭, G♭, C♭), a separator, then the minors (A♯m, D♯m, G♯m, C♯m, F♯m, Bm, Em, Am, Dm, Gm, Cm, Fm, B♭m, E♭m, A♭m). The current key has a ✓. The popup
  is a long list with scroll arrows at the top (and bottom): it is centred on the current key.
- **Click the time signature** (`4/4`): popup **3/4, ✓4/4, 5/4, 6/8, 7/8, 12/8** · separator · **2/4, 6/4, 1/4, 22/1, 20/1, 8/1, 9/1** · separator · **Custom…**.
  The current signature has a ✓; the list proposes common signatures, not a free text field.
- **Press and drag on the tempo number** (user-reported, to be verified): dragging up or down raises or lowers the BPM.
- **Right-click** the LCD: no menu of its own (the control bar menu opens only on the grey areas).
- The modes (Beats & Project, Large, Beats & Time, Beats, Time, Custom) are chosen in "Customize Control Bar and Display…".

## Not yet captured

Drag on the tempo, double-click to type a value, position field editing, the chevron popup, hover help tags, the Custom mode contents, the Large mode
sizes.

## Verified negatives

- Dragging the tempo number up by 12 px did **not** change `138` in this project (the project follows Smart Tempo "ADAPT"); the user reports drag changes the BPM in
  Logic, so it probably needs Smart Tempo set to Keep/Off. To verify.
- Changing the time signature from the popup raises a modal: **"Do you want to change the project time signature?"** with buttons **Change project
  signature** (default), **Insert new signature**, **Cancel**, and a "Don't ask again" checkbox (explanation: change the project signature or insert a new
  signature change at the current position).
