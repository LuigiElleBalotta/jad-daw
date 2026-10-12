# Using an AI assistant with JAD Daw: mixing, recording, mastering

JAD Daw gives an AI assistant two things a chat model does not have: **hands** (it can change the project) and **ears** (it can measure what the project
sounds like). Both are in the MCP server `tools/mcp-ears/jad_ears_server.py`. This guide says how to connect it, what every tool measures, which numbers to
aim for, and gives checklists and a real session. It was written from a session on real recordings (the two mp3 songs `Boulevard_78bpm` and `HoldTheLine`),
and the numbers in the example are what the tools printed.

## Connecting

Python 3 is all that is needed (no packages). Build `lpc-cli` once (`cmake -S . -B build && cmake --build build --target lpc-cli`). In the MCP client, for
example a `.mcp.json` next to the repository:

```json
{ "mcpServers": { "jad-ears": { "command": "python", "args": ["tools/mcp-ears/jad_ears_server.py"] } } }
```

Two ways to work:

* **On a project folder** (works without the app). `open_project` makes a copy (`<project>.ai-session`); every change is an undo step of that copy, every call is
  written to `.ai-log.jsonl`; `commit_session` writes the result back and keeps the original as `<project>.before-ai`. The original is never touched before that.
* **On the running app** (`project: "live"`). In the app switch on **Help > AI Assistant Link**. The assistant then reads the open project, changes it with
  commands that are undo steps of the app (Edit > Undo takes them back), drives the transport, reads the meters and the inputs, and listens to a copy of the
  project as it is now. The link is on the loopback interface only and needs a token that the app writes in `%LOCALAPPDATA%\JAD\JAD Daw\live-link.json`.

Ready-made prompts: `mix_check`, `mix_it`, `master_it`, `recording_check`.

## What the tools measure

| tool | what you get |
|---|---|
| `analyze_mix`, `analyze_track`, `analyze_file` | peak, RMS, crest factor; loudness (momentary, short-term, integrated LUFS, loudness range, true peak, BS.1770-4); clipping, DC offset, silence, **noise floor**; spectrum in third-octave bands, centroid, five energy shares; stereo correlation, width, balance; tempo; a loudness curve; a plain-language `reading` |
| `compare_tracks` | masking between two tracks: where they pile up (score 0..1, worst bands in Hz) and advice |
| `compare_takes` | each take (region) of a track analysed alone and ranked by clipping, headroom, level and noise |
| `check_recording_setup` | listens to the armed inputs for some seconds: peak, usual level, noise floor, clipping, the dB to change at the preamp, round-trip latency |
| `live_meters`, `live_status`, `live_transport` | the running app: peak and hold of every track, gain reduction, playhead, play/stop/locate |
| `get_state`, `list_tracks`, `list_effects` | the project as a mixer sees it; the built-in effects with every parameter, unit and range |
| `set_fader`, `set_pan`, `set_mute`, `set_solo`, `set_send_level`, `set_tempo`, `add_insert`, `set_insert_param`, `set_insert_bypass`, `remove_insert`, `import_audio`, `apply_commands` | the hands: each is one undo step |
| `run_mastering_assistant` | adds the Mastering Assistant to the master and fits it (correction EQ, gain, loudness compensation) to about -14 LUFS with a true peak at -1 dBTP |
| `render_image` | a PNG of the mix, a track or a file: spectrogram (time x log frequency, brighter is louder), waveform (clipping in red), third-octave spectrum; look at it for hum lines, clipped sections, a missing band |
| `render_excerpt` | a 16-bit WAV of a range, or of one track alone, with its analysis |
| `history`, `undo`, `commit_session` | step back; write the session to the original |

Every level is in dB; LUFS are relative to full scale. Ranges are in seconds (`from_seconds`, `to_seconds`) and should be a representative part, such as the
loudest chorus: loudness of a whole song with a quiet intro reads lower than the part that matters. The analysis of a track is the track **after** its own
fader, inserts and buses, so a change on a track shows in its analysis.

## Numbers to aim for

| what | target | notes |
|---|---|---|
| Streaming loudness | -14 LUFS integrated (Spotify, YouTube about -14; Apple Music -16; broadcast EBU R128 -23) | Streaming turns louder music down, so over-loud only loses dynamics |
| True peak | at or under -1 dBTP (-2 for lossy codecs at low rates) | `truePeakDbtp` |
| Mix before mastering | peaks around -6 dBFS, integrated -20 to -16 LUFS | leaves the mastering stage room to work |
| Loudness range | 5-10 LU for pop and rock; under 3 LU is squashed, over 14 LU needs care | `rangeLu` |
| Crest factor of a full mix | 8-14 dB | under 8 dB heavily limited, over 20 dB sharp peaks over a low level |
| Tracking level (24-bit) | peaks -12 to -6 dBFS, never clipping | `check_recording_setup` says the dB to change |
| Noise floor of a take | under -60 dBFS between the notes is clean; over -50 dBFS is audible hiss or hum | `noiseFloorDb` |
| Latency for playing along | under 10 ms round trip good, over 20 ms too much | lower the buffer size or monitor through the interface |
| Stereo correlation | above 0.3 for a mix; near 1 is mono; negative values mean phase trouble | `correlation`, `widthPercent` |
| Balance L/R | within 1 dB | `balanceDb` |
| Spectral balance | most of the energy under 500 Hz for a full-range mix; `above6k` of a few percent is normal | `energyPercent` |

## Checklists

**Mix** (one change at a time, measure after each):
1. `analyze_mix` on the loudest part; read `reading`. Fix clipping and headroom first (lower faders, not the master, until the peak is around -6 dBFS).
2. `analyze_track` on each track. Anything with rumble below 40 Hz that does not belong: high-pass EQ. DC offset: high-pass at 20 Hz.
3. `compare_tracks` for the pairs in the same range (kick and bass, two guitars, vocal and keys). A score above 0.5 means they fight: cut one where the worst band
   is, or pan them apart.
4. Balance with faders by loudness (`integratedLufs` per track), then pan. Check `balanceDb` and `correlation` of the mix.
5. Dynamics: a compressor where `crestDb` of a track is over 20 dB and it needs to sit in the mix; check the result with `analyze_track` and with gain reduction in
   `live_meters`.
6. After the changes, `analyze_mix` again and compare with the first numbers. Undo what did not help.

**Recording** (app linked, track armed):
1. `check_recording_setup` while the performer plays the loudest part; follow the advice, then run it again until it says the level is good.
2. If the noise floor is high, look at the source and the cables before the gain; recording hotter only helps while nothing clips.
3. After the takes, `compare_takes` on the track. It ranks by sound only; timing, pitch and feel are the producer's call, so present the ranking as a hint.

**Mastering**:
1. `analyze_mix` of the whole mix. If the true peak is over 0 dBTP the mix itself clips: lower the faders first.
2. `run_mastering_assistant`; check the loudness is about -14 LUFS and the true peak under -1 dBTP.
3. Compare with a reference: `import_audio` the reference song, `analyze_track` it, and look at loudness, balance and stereo width against the mix. Do not chase the
   loudness of the reference: match the target of the platform.
4. Ask the user before `commit_session`.

## A real session

The project: the demo project with two real songs imported (`lpc-cli import`), the demo tracks muted. Everything below was run through the server on the part from
20 s to 80 s.

```
analyze_mix                  -23.3 LUFS-I, true peak +0.1 dBTP, range 3.0 LU, correlation 0.91, width 4.8 %
  reading: "The peak touches 0 dBFS: there is no headroom." "True peak 0.1 dBTP is above -1 dBTP ..."
analyze_track Boulevard      -22.8 LUFS-I, peak -0.1 dBFS, crest 28 dB, 83 % of the energy between 500 Hz and 2 kHz
analyze_track HoldTheLine    -29.7 LUFS-I (after its fader at -6 dB and pan 0.3), peak -6.1 dBFS
compare_tracks               masking score 0.64, worst band 1000 Hz: "cut one of them there with an EQ, or pan them apart"
run_mastering_assistant      before -23.3 LUFS-I / +0.1 dBTP  ->  after -14.2 LUFS-I / -1.0 dBTP, range 6.0 LU
                             (gain +13.4 dB, correction EQ lifting both ends by 4 dB, loudness compensation +2.1 dB)
undo                         back to -23.3 LUFS-I; the original project was never written
```

Reading it: the mix has no headroom (true peak over 0), the two songs sit in the same 500 Hz - 2 kHz band (masking 0.64), and the loudness is far under
streaming level. A sensible order: lower the faders until the peak is at -6 dBFS, separate the two with EQ or pan, then run the mastering assistant.

## Limits to keep in mind

* The tools measure; they do not judge taste. Say what the numbers show and let the user decide.
* `compare_takes` and the masking score cannot hear timing, pitch or feel.
* The mastering assistant targets -14 LUFS and -1 dBTP; another target needs `set_insert_param` on its `gain`.
* The live meters and the input check need an open audio device; without one the app says so (`live_status`: `deviceError`).
* A tool that changes the original project is refused unless the project is a session or `live`.
