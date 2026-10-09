# Notes for Claude sessions

## Where earlier work and references live (outside the repo)

- The other AI's config dir is `C:\Users\l.balotta\.claude-work`. Its memories are in
  `C:\Users\l.balotta\.claude-work\projects\C--altro-Personale-git-LogicProClone\memory\` (read `MEMORY.md` first); its session logs are the `*.jsonl` next to that folder.
- Apple's Logic Pro guide (1975 pages, crawled, private, not committed because of copyright):
  `C:\Users\l.balotta\AppData\Local\Temp\claude\C--altro-Personale-git-LogicProClone\e6055b72-2240-422c-8d8f-53509a74d976\scratchpad\corpus.txt`
  with `toc.json` beside it (pages separated by `=== slug ===`; read with `python -I -X utf8`). The repo holds only notes in our own words, in `docs/logic-reference/`.
- Logic Pro 11.2 runs on a Mac reached through AnyDesk with windows-mcp; the quirks and safety rules are in the other AI's memory file `reference_logic_mac_anydesk.md`
  (never edit or save the user's project, wait 3-4 s after each click, undo every change).

## Working rules

- The UI should match Logic Pro in behaviour and look; icons are our own SVGs.
- Write the UI first and build/test at the end (the user asked to avoid many build-and-test cycles).
- The CI is handled when the user says so.
