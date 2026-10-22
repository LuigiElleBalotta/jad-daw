"""Takes the pictures of the README and of docs/user-guide.md from the running app, with its own --screenshot mode, on docs/showcase/Showcase.lpc.

    python -I -X utf8 tools/screenshots/take.py [path to jad-daw.exe] [name ...]

The project is built by tools/screenshots/build_showcase.qml (see tools/screenshots/README.md). Pictures go to docs/images/app/.
"""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "images" / "app"
PROJECT = ROOT / "docs" / "showcase" / "Showcase.lpc"

# name -> arguments of jad-daw (after --project, --no-audio and --screenshot)
SHOTS = {
    "main-window": ["--panels", "inspector", "--select-track", "3", "--select-region", "3", "--size", "1600x900"],
    "tools": ["--panels", "inspector", "--tool", "pencil", "--select-track", "2", "--size", "1600x900"],
    "library": ["--panels", "library,inspector", "--select-track", "3", "--size", "1600x900"],
    "smart-controls": ["--panels", "smart", "--select-track", "3", "--size", "1600x900"],
    "mixer": ["--panels", "mixer", "--size", "1600x900"],
    "piano-roll": ["--panels", "editors,inspector", "--select-track", "3", "--select-region", "3", "--size", "1600x900"],
    "global-tracks": ["--panels", "global", "--size", "1600x900"],
    "automation": ["--panels", "automation", "--select-track", "3", "--size", "1600x900"],
    "menu-file": ["--open-menu", "1", "--size", "1280x800"],
    "menu-track": ["--open-menu", "3", "--size", "1280x800"],
    "menu-mix": ["--open-menu", "6", "--size", "1280x800"],
    "new-tracks": ["--window", "newTracks", "--size", "1280x800"],
    "preferences": ["--window", "preferences", "--size", "1280x800"],
    "project-settings": ["--window", "projectSettings", "--size", "1280x800"],
    "io-labels": ["--window", "ioLabels", "--size", "1280x800"],
    "bounce": ["--window", "bounce", "--size", "1280x800"],
    "key-commands": ["--window", "keyCommands", "--size", "1280x800"],
    "session-players": ["--window", "sessionPlayers", "--size", "1280x800"],
    "undo-history": ["--window", "undoHistory", "--size", "1280x800"],
    "step-input": ["--window", "stepInput", "--size", "1280x800"],
    "musical-typing": ["--window", "musicalTyping", "--size", "1280x800"],
    "fx-channel-eq": ["--effect", "3:0"],
    "fx-compressor": ["--effect", "2:0"],
    "fx-limiter": ["--effect", "1:0"],
    "fx-adaptive-limiter": ["--effect", "1:1"],
    "fx-match-eq": ["--effect", "1:2"],
    "fx-noise-gate": ["--effect", "5:0"],
    "fx-neural-saturation": ["--effect", "5:1"],
    "fx-delay": ["--effect", "4:0"],
    "fx-reverb": ["--effect", "4:1"],
    "fx-mastering": ["--effect", "m:0"],
    "fx-synth": ["--effect", "3:-2"],
    "fx-drum-kit": ["--effect", "1:-2"],
}


def main():
    args = sys.argv[1:]
    exe = pathlib.Path(args[0]) if args and args[0].lower().endswith(".exe") else ROOT / "build-ui" / "ui" / "Release" / "jad-daw.exe"
    names = [a for a in args if not a.lower().endswith(".exe")] or list(SHOTS)
    OUT.mkdir(parents=True, exist_ok=True)
    for name in names:
        out = OUT / (name + ".png")
        cmd = [str(exe), "--project", str(PROJECT), "--no-audio", "--screenshot", str(out), "--delay", "3000"] + SHOTS[name]
        rc = subprocess.run(cmd, timeout=120).returncode
        print(("ok  " if rc == 0 else "FAIL(%d) " % rc) + name)


if __name__ == "__main__":
    main()
