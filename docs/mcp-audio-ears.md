# MCP server: giving the assistants "ears"

Requirement (from the project owner, 2026-10-09): the MCP server of the DAW is how an LLM (Claude Code, Codex, others) drives the
DAW, and it must also let the model *hear* the project, because the goal is a recording / mixing / mastering assistant.
An LLM cannot listen to a waveform. It can read numbers, text and images, and some models accept short audio clips. So the server
must turn sound into those things.

State today: the MCP server does not exist yet. What exists and is ready to be exposed: every edit is a JSON command (`ProjectHost`),
`lpc-cli render` makes a deterministic offline bounce of the whole project, and the engine keeps a master peak. The tools below are
the "ears" half of the server and should be designed in the same step as the "hands" half (commands).

## Tools that make the DAW audible to a model

All of them work on an **offline render** (no audio device, deterministic, repeatable) of a chosen range and a chosen source: the
master, one track, one bus, solo of a set of tracks, or the project before and after a pending change.

1. `render_analysis(source, range)` -> one JSON report:
   - loudness: integrated, short-term max, momentary max (LUFS, ITU-R BS.1770 / EBU R128), loudness range (LRA);
   - levels: sample peak and true peak (dBTP), RMS, crest factor, DC offset, number of clipped samples and where;
   - stereo: correlation, mid/side balance, width, mono compatibility (level drop when summed);
   - spectrum: energy per third octave or per 24 Bark bands, spectral centroid, rolloff, low/mid/high balance;
   - dynamics over time: the loudness curve as a list (one value per second) so the model can say "the chorus is 4 LU louder".
2. `render_image(source, range, kind)` -> a PNG the model can look at (models with vision read it): `waveform`, `spectrogram`,
   `spectrum`, `loudness`, `stereo` (vectorscope). The image is the closest thing to listening for a model that has no audio input.
3. `render_audio(source, range)` -> a short clip (WAV or MP3, a few seconds, size capped) for models that accept audio input, and
   a file path for tools that run next to the DAW.
4. `compare(sourceA, sourceB)` -> the difference of two reports (a render before and after an EQ change, or the mix against a
   reference track): per-band dB difference, loudness difference, stereo difference. This is what lets an assistant check that its
   own change did what it meant.
5. `detect(source)` -> tempo and beat grid, key, onsets, silence regions, clipping regions, noise floor, and per-track problems
   (hum at 50/60 Hz, sibilance, resonances, phase issues between two tracks).
6. `meters()` -> the live meters (per track and master) for a project that is playing; a subscription for a stream.

## Rules that keep it useful and safe

- Analysis never changes the project and never needs the audio device; it runs on the project thread's read path or on a copy of
  the project, so it cannot disturb playback.
- Every tool returns numbers with units and the exact range analysed, and says when the source is silent.
- A model's changes go through the same undoable commands as the UI (one undo step per instruction), so the user can reject them.
- Big results (images, clips) are returned as files plus a short summary, not inlined.
- The in-app assistant needs an HTTP client for the providers; `Conceptual-Machines/juce-llm` (MIT, a JUCE module for OpenAI,
  Anthropic, Gemini, OpenRouter and a local llama-server) can be a starting point for that part, or Qt Network can be used. It is
  not needed by the MCP server, where the model is outside the DAW.
