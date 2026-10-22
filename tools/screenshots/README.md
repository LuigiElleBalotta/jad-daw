# Pictures of the app

The pictures of the README and of `docs/user-guide.md` (`docs/images/app/*.png`) are taken from the running app, with its own `--screenshot` mode, on a small
project, `docs/showcase/Showcase.lpc` (drums, bass, keys, a lead and an audio loop, with a few effects on them).

1. **The project** (only when it has to change): `python -I -X utf8 tools/screenshots/make_loop.py` writes `docs/showcase/loop.wav`; then run the QML test
   `build_showcase.qml` with the UI test runner, from the build folder of the UI:

       cd build-ui/ui/tests
       QT_QPA_PLATFORM=offscreen ../Release/jad_ui_qmltests.exe -input ../../../tools/screenshots/build_showcase.qml -import ../

   It builds the project through the same controller the app uses and saves it in `docs/showcase/Showcase.lpc` (delete the folder first to start again).
2. **The pictures:** `python -I -X utf8 tools/screenshots/take.py [path to jad-daw.exe] [name ...]`. Without names it takes all of them; each one is a line of
   `SHOTS` in `take.py` (the panels shown, the track and region selected, the window or the insert opened).

The app's options for this: `--screenshot <file>`, `--size WxH`, `--delay ms`, `--panels list`, `--select-track n`, `--select-region n`, `--tool name`,
`--open-menu index`, `--window name` (a dialog or a window: see `showWindow` in `ui/qml/Main.qml`) and `--effect track:insert` (`m` for the master, insert `-2` for the instrument).
Pictures use the app's own font, so take them on a machine where the build runs with a display (the offscreen platform has no fonts).
