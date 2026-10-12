#!/usr/bin/env python3
"""The DAW's ears: an MCP server (stdio) that lets an AI assistant look at a JAD Daw project and listen to it.

It drives `lpc-cli` (snapshot, analyze, masking, apply, mastering, render, effects), so it works on project folders, headless; the running app is
not needed. Nothing here needs a package: the Model Context Protocol is spoken directly (JSON-RPC 2.0, one message per line).

Safe by default
  * Reading tools work on any project.
  * Tools that change a project work on a *session*, a copy of the project made by `open_project` (`<project>.ai-session`). The original is only
    written by `commit_session`, which first keeps the original as `<project>.before-ai`. Set JAD_MCP_WRITE_ORIGINAL=1 to let the write tools work on
    the original directly.
  * Every change first saves `project.json`, so `undo` can go back step by step; every call is logged in `<project>/.ai-log.jsonl`.

Configuration: JAD_CLI (path of lpc-cli), JAD_MCP_WRITE_ORIGINAL.
"""
import datetime
import json
import os
import shutil
import subprocess
import sys
import tempfile

SERVER_NAME = "jad-ears"
SERVER_VERSION = "1.0.0"
PROTOCOL = "2024-11-05"

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))


class ToolError(Exception):
    pass


def find_cli():
    env = os.environ.get("JAD_CLI")
    if env and os.path.exists(env):
        return env
    names = ["lpc-cli.exe", "lpc-cli"]
    roots = [os.path.join(REPO, "build", "tools", "lpc-cli", "lpc-cli_artefacts", cfg) for cfg in ("Release", "Debug", "RelWithDebInfo")]
    roots += [os.path.join(REPO, "build", "tools", "lpc-cli"), os.path.join(REPO, "build-plugin", "tools", "lpc-cli", "lpc-cli_artefacts", "Debug")]
    candidates = [os.path.join(r, n) for r in roots for n in names if os.path.exists(os.path.join(r, n))]
    if candidates:
        return max(candidates, key=os.path.getmtime)  # the newest build
    found = shutil.which("lpc-cli")
    if found:
        return found
    raise ToolError("lpc-cli was not found: build it or set JAD_CLI to its path")


def run_cli(args, timeout=1800):
    cli = find_cli()
    proc = subprocess.run([cli] + [str(a) for a in args], capture_output=True, text=True, encoding="utf-8", timeout=timeout)
    if proc.returncode != 0:
        # `apply` prints its result on stdout even when refused: keep it for the caller
        message = (proc.stderr or proc.stdout or "").strip()
        raise ToolError(message or "lpc-cli failed with code %d" % proc.returncode)
    out = proc.stdout.strip()
    try:
        return json.loads(out) if out else {}
    except json.JSONDecodeError:
        return {"text": out}


def run_cli_allow_refusal(args):
    cli = find_cli()
    proc = subprocess.run([cli] + [str(a) for a in args], capture_output=True, text=True, encoding="utf-8", timeout=600)
    try:
        return json.loads(proc.stdout), proc.returncode
    except json.JSONDecodeError:
        raise ToolError((proc.stderr or proc.stdout or "lpc-cli failed").strip())


# ----------------------------------------------------------------------------------------------- projects and sessions

def norm(path):
    return os.path.abspath(os.path.expanduser(path))


def is_session(path):
    return norm(path).endswith(".ai-session")


def log(project, tool, args, result):
    try:
        logfile = os.path.join(tempfile.gettempdir(), "jad-live-ai-log.jsonl") if is_live(project) else os.path.join(norm(project), ".ai-log.jsonl")
        with open(logfile, "a", encoding="utf-8") as f:
            f.write(json.dumps({"time": datetime.datetime.now().isoformat(timespec="seconds"), "tool": tool, "arguments": args, "result": result}, ensure_ascii=False) + "\n")
    except OSError:
        pass


def history_dir(project):
    d = os.path.join(norm(project), ".ai-history")
    os.makedirs(d, exist_ok=True)
    return d


def save_history(project):
    d = history_dir(project)
    n = len([f for f in os.listdir(d) if f.endswith(".json")]) + 1
    shutil.copyfile(os.path.join(norm(project), "project.json"), os.path.join(d, "%04d.json" % n))
    return n


def require_writable(project):
    if is_live(project) or is_session(project) or os.environ.get("JAD_MCP_WRITE_ORIGINAL") == "1":
        return norm(project) if not is_live(project) else LIVE
    raise ToolError("this tool changes the project: call open_project first and use the session it returns (the original stays untouched), "
                    "or set JAD_MCP_WRITE_ORIGINAL=1")


# ------------------------------------------------------------------------------------------------- the running app ("live")
# With the live link on (Help > AI Assistant Link) the project "live" is the one open in the app: reads come from it, changes are commands in the app
# (one undo step each, which the user can undo there), and listening is done on a copy of the project as it is now.

LIVE = "live"
LIVE_COPY = os.path.join(tempfile.gettempdir(), "jad-live-copy")


def is_live(project):
    return str(project).strip().lower() == LIVE


def live_info_path():
    env = os.environ.get("JAD_LIVE_LINK_FILE")
    if env:
        return env
    base = os.environ.get("LOCALAPPDATA") or os.path.join(os.path.expanduser("~"), ".local", "share")
    return os.path.join(base, "JAD", "JAD Daw", "live-link.json")


def live_request(op, timeout=60, **fields):
    import socket
    try:
        with open(live_info_path(), encoding="utf-8") as f:
            info = json.load(f)
    except (OSError, ValueError):
        raise ToolError("the app is not linked: open JAD Daw and switch on Help > AI Assistant Link")
    try:
        with socket.create_connection(("127.0.0.1", int(info["port"])), timeout=5) as s:
            s.settimeout(timeout)
            s.sendall((json.dumps(dict(fields, op=op, token=info["token"], id=1)) + "\n").encode("utf-8"))
            buf = b""
            while not buf.endswith(b"\n"):
                chunk = s.recv(65536)
                if not chunk:
                    break
                buf += chunk
    except OSError:
        raise ToolError("the app does not answer: is JAD Daw still open with the AI Assistant Link on?")
    reply = json.loads(buf.decode("utf-8"))
    if not reply.get("ok"):
        raise ToolError(reply.get("error", "the app refused the request"))
    return reply


def project_dir(project):
    """The folder `lpc-cli` can read: the project itself, or for "live" a copy of the open project as it is now."""
    if is_live(project):
        live_request("export", dir=LIVE_COPY)
        return LIVE_COPY
    return norm(project)


def snapshot(project):
    if is_live(project):
        return live_request("state")["project"]
    return run_cli(["snapshot", norm(project)])


def resolve_track(project, selector):
    state = snapshot(project)
    exact = [t for t in state["tracks"] if t["id"] == selector]
    if exact:
        return exact[0], state
    named = [t for t in state["tracks"] if t["name"].lower() == str(selector).lower()]
    if len(named) == 1:
        return named[0], state
    if len(named) > 1:
        raise ToolError('more than one track is called "%s": use its id (get_state lists them)' % selector)
    raise ToolError('no track "%s": the tracks are %s' % (selector, ", ".join(t["name"] for t in state["tracks"])))


def apply_commands(project, commands, what):
    path = require_writable(project)
    if path == LIVE:
        live_request("apply", commands=commands)
        return {"done": what, "project": LIVE, "note": "one undo step in the app (Edit > Undo, or the undo tool)"}
    n = save_history(path)
    path_json = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8")
    try:
        json.dump(commands, path_json)
        path_json.close()
        result, code = run_cli_allow_refusal(["apply", path, path_json.name])
    finally:
        os.unlink(path_json.name)
    if code != 0 or not result.get("applied"):
        os.remove(os.path.join(history_dir(path), "%04d.json" % n))  # nothing changed
        errors = [r.get("error") for r in result.get("results", []) if not r.get("ok")]
        raise ToolError("refused: " + "; ".join(e for e in errors if e))
    return {"done": what, "undoSteps": n, "project": path}


def compact(report):
    """The reports are read by a model: the bands become two short lists and the empty overlaps are dropped."""
    if isinstance(report, dict):
        spec = report.get("spectrum")
        if isinstance(spec, dict) and isinstance(spec.get("thirdOctaveBands"), list):
            bands = spec.pop("thirdOctaveBands")
            spec["bandsHz"] = [b["hz"] for b in bands]
            spec["bandsDb"] = [b["db"] for b in bands]
        if isinstance(report.get("overlapByBand"), list):
            report["overlapByBand"] = [b for b in report["overlapByBand"] if b["overlap"] > 0.05]
    return report


# ----------------------------------------------------------------------------------------------------------- the tools

def t_open_project(a):
    src = norm(a["project_path"])
    if not os.path.exists(os.path.join(src, "project.json")):
        raise ToolError("%s is not a project folder (no project.json)" % src)
    if a.get("edit_original") and os.environ.get("JAD_MCP_WRITE_ORIGINAL") == "1":
        return {"project": src, "session": False, "state": snapshot(src)}
    session = src.rstrip("/\\") + ".ai-session"
    if os.path.exists(session) and not a.get("fresh"):
        pass  # carry on with the session that is there
    else:
        if os.path.exists(session):
            shutil.rmtree(session)
        shutil.copytree(src, session, ignore=shutil.ignore_patterns(".ai-*", "cache"))
    return {"project": session, "session": True, "note": "Changes go to this copy. commit_session writes them back (the original is kept as .before-ai).", "state": snapshot(session)}


def t_get_state(a):
    return snapshot(a["project"])


def t_list_tracks(a):
    state = snapshot(a["project"])
    rows = []
    for t in state["tracks"]:
        rows.append({"id": t["id"], "name": t["name"], "kind": t["kind"], "gainDb": t["gainDb"], "pan": t["pan"], "mute": t["mute"], "solo": t["solo"],
                     "output": t.get("output"), "inserts": [i["processorId"] + (" (bypassed)" if i["bypass"] else "") for i in t["inserts"]],
                     "regions": len(t["regions"])})
    return {"tempo": state["tempo"], "lengthSeconds": state["lengthSeconds"], "tracks": rows}


def range_args(a):
    if a.get("from_seconds") is not None and a.get("to_seconds") is not None:
        return ["--seconds", "%s:%s" % (a["from_seconds"], a["to_seconds"])]
    return []


def t_analyze_mix(a):
    return run_cli(["analyze", project_dir(a["project"])] + range_args(a) + ([] if a.get("tempo", True) else ["--no-tempo"]))


def t_analyze_track(a):
    track, _ = resolve_track(a["project"], a["track"])
    return run_cli(["analyze", project_dir(a["project"]), "--track", track["id"]] + range_args(a) + ([] if a.get("tempo", False) else ["--no-tempo"]))


def t_analyze_file(a):
    return run_cli(["analyze", norm(a["file"])] + range_args(a) + ([] if a.get("tempo", True) else ["--no-tempo"]))


def t_compare_tracks(a):
    ta, _ = resolve_track(a["project"], a["track_a"])
    tb, _ = resolve_track(a["project"], a["track_b"])
    return run_cli(["masking", project_dir(a["project"]), ta["id"], tb["id"]] + range_args(a))


def t_set_fader(a):
    track, _ = resolve_track(a["project"], a["track"])
    return apply_commands(a["project"], [{"type": "set_strip", "trackId": track["id"], "gainDb": float(a["gain_db"])}], "%s fader to %s dB" % (track["name"], a["gain_db"]))


def t_set_pan(a):
    track, _ = resolve_track(a["project"], a["track"])
    return apply_commands(a["project"], [{"type": "set_strip", "trackId": track["id"], "pan": float(a["pan"])}], "%s pan to %s" % (track["name"], a["pan"]))


def t_set_mute(a):
    track, _ = resolve_track(a["project"], a["track"])
    return apply_commands(a["project"], [{"type": "set_strip", "trackId": track["id"], "mute": bool(a["mute"])}], "%s mute %s" % (track["name"], a["mute"]))


def t_set_solo(a):
    track, _ = resolve_track(a["project"], a["track"])
    return apply_commands(a["project"], [{"type": "set_strip", "trackId": track["id"], "solo": bool(a["solo"])}], "%s solo %s" % (track["name"], a["solo"]))


def t_add_insert(a):
    track, _ = resolve_track(a["project"], a["track"])
    insert = {"processorId": a["processor"], "params": a.get("params") or {}, "state": ""}
    cmd = {"type": "add_insert", "trackId": track["id"], "index": a.get("position", -1), "insert": insert}
    return apply_commands(a["project"], [cmd], "added %s to %s" % (a["processor"], track["name"]))


def t_set_insert_param(a):
    track, _ = resolve_track(a["project"], a["track"])
    cmd = {"type": "set_insert_param", "trackId": track["id"], "index": int(a["insert_index"]), "param": a["param"], "value": float(a["value"])}
    return apply_commands(a["project"], [cmd], "%s insert %s: %s = %s" % (track["name"], a["insert_index"], a["param"], a["value"]))


def t_set_insert_bypass(a):
    track, _ = resolve_track(a["project"], a["track"])
    cmd = {"type": "set_insert_bypass", "trackId": track["id"], "index": int(a["insert_index"]), "bypass": bool(a["bypass"])}
    return apply_commands(a["project"], [cmd], "%s insert %s bypass %s" % (track["name"], a["insert_index"], a["bypass"]))


def t_remove_insert(a):
    track, _ = resolve_track(a["project"], a["track"])
    return apply_commands(a["project"], [{"type": "remove_insert", "trackId": track["id"], "index": int(a["insert_index"])}], "removed insert %s of %s" % (a["insert_index"], track["name"]))


def t_set_send_level(a):
    track, _ = resolve_track(a["project"], a["track"])
    sends = [s for s in track["sends"] if s["id"] == a["send"] or s["target"].lower() == str(a["send"]).lower()]
    if len(sends) != 1:
        raise ToolError("the send must be its id or the name of its target: %s" % ", ".join(s["target"] for s in track["sends"]))
    return apply_commands(a["project"], [{"type": "set_send", "sendId": sends[0]["id"], "levelDb": float(a["level_db"])}], "%s send to %s: %s dB" % (track["name"], sends[0]["target"], a["level_db"]))


def t_set_tempo(a):
    return apply_commands(a["project"], [{"type": "set_tempo", "tick": int(round(float(a.get("beat", 0)) * 960)), "bpm": float(a["bpm"])}], "tempo %s bpm" % a["bpm"])


def t_apply_commands(a):
    return apply_commands(a["project"], a["commands"], "%d command(s)" % len(a["commands"]))


def run_mastering_live(a):
    """Mastering on the open project: the assistant runs on a copy, and what it found is put in the app as one undo step."""
    copy = project_dir(LIVE)
    result = run_cli(["mastering", copy] + range_args(a))
    master = [t for t in snapshot(copy) ["tracks"] if t["kind"] == "master"][0]
    found = [i for i in master["inserts"] if i["processorId"] == "builtin.mastering"][0]
    live_master = [t for t in snapshot(LIVE)["tracks"] if t["kind"] == "master"][0]
    existing = [i for i in live_master["inserts"] if i["processorId"] == "builtin.mastering"]
    if existing:
        cmds = [{"type": "set_insert_param", "trackId": live_master["id"], "index": existing[0]["index"], "param": k, "value": v} for k, v in found["params"].items()]
    else:
        cmds = [{"type": "add_insert", "trackId": live_master["id"], "index": -1, "insert": {"processorId": "builtin.mastering", "params": found["params"], "state": ""}}]
    live_request("apply", commands=cmds)
    result["appliedTo"] = "the master of the open project (one undo step in the app)"
    return result


def t_run_mastering_assistant(a):
    path = require_writable(a["project"])
    if path == LIVE:
        return run_mastering_live(a)
    n = save_history(path)
    try:
        result = run_cli(["mastering", path] + range_args(a))
    except ToolError:
        os.remove(os.path.join(history_dir(path), "%04d.json" % n))
        raise
    result["undoSteps"] = n
    return result


def t_render_excerpt(a):
    out = a.get("out") or os.path.join(tempfile.gettempdir(), "jad-excerpt-%d.wav" % int(datetime.datetime.now().timestamp()))
    args = ["render", project_dir(a["project"]), out, "--bits", "16"]
    if a.get("to_seconds") is not None:
        args += ["--seconds", str(a["to_seconds"])]
    if a.get("track"):
        track, _ = resolve_track(a["project"], a["track"])
        args += ["--track", track["id"]]
    run_cli(args)
    report = run_cli(["analyze", out, "--no-tempo"] + ([] if a.get("from_seconds") is None else ["--seconds", "%s:%s" % (a["from_seconds"], a.get("to_seconds", 600))]))
    return {"file": out, "analysis": report}


def t_import_audio(a):
    path = require_writable(a["project"])
    if path == LIVE:
        raise ToolError("importing into the open app is not supported: use the session of open_project, or Import in the app")
    n = save_history(path)
    args = ["import", path, norm(a["file"])]
    if a.get("track"):
        track, _ = resolve_track(path, a["track"])
        args += ["--track", track["id"]]
    if a.get("name"):
        args += ["--name", a["name"]]
    if a.get("start_seconds") is not None:
        args += ["--start-seconds", str(a["start_seconds"])]
    try:
        result = run_cli(args)
    except ToolError:
        os.remove(os.path.join(history_dir(path), "%04d.json" % n))
        raise
    result["undoSteps"] = n
    return result


def t_render_image(a):
    """A PNG of the mix, a track or a file (spectrogram, waveform or spectrum) for a model that can look at pictures; the path is returned (open or read the file)."""
    kind = a.get("kind", "spectrogram")
    out = a.get("out") or os.path.join(tempfile.gettempdir(), "jad-%s-%d.png" % (kind, int(datetime.datetime.now().timestamp() * 1000)))
    if a.get("file"):
        target = [norm(a["file"])]
    else:
        target = [project_dir(a["project"])]
        if a.get("track"):
            track, _ = resolve_track(a["project"], a["track"])
            target += ["--track", track["id"]]
    report = run_cli(["analyze"] + target + range_args(a) + ["--no-tempo", "--image", out, "--kind", kind])
    return {"image": out, "kind": kind, "how_to_read": {
        "spectrogram": "time left to right, frequency bottom 20 Hz to top 20 kHz (logarithmic; white ticks at 100 Hz, 1 kHz, 10 kHz), brighter is louder",
        "waveform": "peak envelope over time; blue normal, red where the signal reaches full scale (clipping); guides at half and full height",
        "spectrum": "31 third-octave bars from 20 Hz to 20 kHz, height is level (-90 to 0 dBFS)"}[kind], "reading": report.get("reading", [])}


def t_effects(a):
    return run_cli(["effects"])


def t_live_status(a):
    return live_request("hello")


def t_live_meters(a):
    """The level meters of the running app. With `seconds`, they are read a few times a second and the loudest values are kept (a peak that came and went is not lost)."""
    seconds = float(a.get("seconds", 0))
    reply = live_request("meters")
    if seconds > 0:
        import time
        end = time.time() + min(seconds, 120)
        while time.time() < end:
            time.sleep(0.25)
            now = live_request("meters")
            reply["masterPeakDb"] = max(reply["masterPeakDb"], now["masterPeakDb"])
            peaks = {t["id"]: t for t in reply["tracks"]}
            for t in now["tracks"]:
                if t["id"] in peaks:
                    peaks[t["id"]]["peakDb"] = max(peaks[t["id"]]["peakDb"], t["peakDb"])
                    peaks[t["id"]]["holdDb"] = t["holdDb"]
            reply["positionSeconds"] = now["positionSeconds"]
            reply["playing"] = now["playing"]
    reply.pop("ok", None)
    reply.pop("id", None)
    return reply


def t_check_recording_setup(a):
    """Before recording: listen to the inputs for a few seconds (the performer plays or sings), then say if the level is right, if there is noise or clipping,
    how many dB to change at the preamp, and how big the latency is."""
    seconds = min(max(float(a.get("seconds", 5)), 1.0), 60.0)
    live_request("reset_inputs")
    import time
    time.sleep(seconds)
    reply = live_request("inputs")
    reply.pop("ok", None)
    reply.pop("id", None)
    notes = []
    dev = reply.get("device", {})
    if not dev.get("open"):
        notes.append("The audio device is not open (%s): nothing can be recorded; check Preferences > Audio." % (dev.get("error") or "no error given"))
    else:
        ms = dev.get("roundTripLatencyMs", 0.0)
        if ms and ms > 20:
            notes.append("Round-trip latency is %.1f ms: too much to play along comfortably; lower the buffer size in Preferences > Audio, or monitor through the interface (direct monitoring)." % ms)
        elif ms and ms > 10:
            notes.append("Round-trip latency is %.1f ms: workable, but a smaller buffer would feel better for fast playing." % ms)
        elif ms:
            notes.append("Round-trip latency is %.1f ms: fine for recording." % ms)
    if not reply.get("armedTracks"):
        notes.append("No audio track is armed for recording: arm one (the R button) and choose its input.")
    for ch in reply.get("channels", []):
        if ch.get("armed") or ch.get("peakDb", -200) > -70:
            notes.append("Input %d: %s" % (ch["input"], " ".join(ch["advice"])))
    reply["notes"] = notes
    return reply


def t_compare_takes(a):
    """The takes (regions) of a track, each analysed alone: level, clipping, noise floor. The recommendation is about the sound only (a take that clips is out, a quiet floor and good level
    count), not about timing or pitch, which only a listener can judge."""
    state = snapshot(a["project"])
    track, _ = resolve_track(a["project"], a["track"])
    regions = [r for r in track["regions"] if not r.get("muted")] if not a.get("region_ids") else [r for r in track["regions"] if r["id"] in a["region_ids"]]
    if a.get("take_group"):
        regions = [r for r in regions if r.get("takeGroup") == a["take_group"]]
    if len(regions) < 1:
        raise ToolError("the track has no regions to compare")
    directory = project_dir(a["project"])
    rows = []
    for r in regions[:24]:
        rep = run_cli(["analyze", directory, "--region", r["id"], "--no-tempo"])
        problems = rep.get("problems", {})
        row = {"region": r["id"], "startSeconds": r.get("startSeconds"), "lengthSeconds": r.get("lengthSeconds"), "takeGroup": r.get("takeGroup", ""),
               "peakDb": rep["level"]["peakDb"], "rmsDb": rep["level"]["rmsDb"], "crestDb": rep["level"]["crestDb"], "integratedLufs": rep["loudness"]["integratedLufs"],
               "clippedPercent": problems.get("clippedPercent", 0), "noiseFloorDb": problems.get("noiseFloorDb"), "reading": rep.get("reading", [])}
        score = 100.0
        why = []
        if row["clippedPercent"] > 0.01:
            score -= 60
            why.append("clips")
        if row["peakDb"] > -1:
            score -= 10
            why.append("no headroom")
        if row["peakDb"] < -24:
            score -= 15
            why.append("very low level")
        nf = row["noiseFloorDb"]
        if nf is not None and nf > -200 and row["rmsDb"] - nf > 12:
            if nf > -50:
                score -= 20
                why.append("noisy floor (%.0f dBFS)" % nf)
            elif nf < -70:
                score += 5
                why.append("quiet floor")
        row["score"] = round(score, 1)
        row["why"] = why
        rows.append(row)
    ranked = sorted(rows, key=lambda x: -x["score"])
    return {"track": track["name"], "takes": rows, "bestBySound": ranked[0]["region"], "ranking": [x["region"] for x in ranked],
            "note": "Ranked by clipping, headroom, level and noise only; timing, pitch and feel need a listener."}


def t_live_transport(a):
    action = a["action"]
    if action == "locate":
        live_request("locate", seconds=float(a.get("seconds", 0)))
    elif action in ("play", "stop", "reset_peaks"):
        live_request(action)
    else:
        raise ToolError("action must be play, stop, locate or reset_peaks")
    return live_request("hello")


def t_history(a):
    if is_live(a["project"]):
        raise ToolError("the open project keeps its own history: the changes are undo steps in the app (undo takes them back)")
    path = norm(a["project"])
    d = os.path.join(path, ".ai-history")
    steps = sorted(f for f in os.listdir(d)) if os.path.isdir(d) else []
    entries = []
    logfile = os.path.join(path, ".ai-log.jsonl")
    if os.path.exists(logfile):
        with open(logfile, encoding="utf-8") as f:
            entries = [json.loads(line) for line in f if line.strip()][-30:]
    return {"undoSteps": len(steps), "recentCalls": entries}


def t_undo(a):
    path = require_writable(a["project"])
    if path == LIVE:
        for _ in range(int(a.get("steps", 1))):
            live_request("undo")
        return {"undone": int(a.get("steps", 1)), "project": LIVE}
    d = history_dir(path)
    steps = sorted(f for f in os.listdir(d) if f.endswith(".json"))
    if not steps:
        raise ToolError("nothing to undo")
    last = steps[-1]
    shutil.copyfile(os.path.join(d, last), os.path.join(path, "project.json"))
    os.remove(os.path.join(d, last))
    return {"undone": last, "remainingUndoSteps": len(steps) - 1}


def t_commit_session(a):
    session = norm(a["session"])
    if not is_session(session):
        raise ToolError("that is not a session made by open_project")
    target = norm(a["to"]) if a.get("to") else session[: -len(".ai-session")]
    backup = None
    if os.path.exists(target) and not a.get("to"):
        backup = target + ".before-ai"
        if os.path.exists(backup):
            shutil.rmtree(backup)
        shutil.copytree(target, backup)
    shutil.copyfile(os.path.join(session, "project.json"), os.path.join(target, "project.json")) if os.path.exists(target) else shutil.copytree(session, target, ignore=shutil.ignore_patterns(".ai-*"))
    return {"written": target, "originalKeptAs": backup}


def obj(props, required=None):
    return {"type": "object", "properties": props, "required": required or [], "additionalProperties": False}


P = {"type": "string", "description": "the project folder (a session from open_project for tools that change it), or \"live\" for the project open in the running app (needs Help > AI Assistant Link)"}
TRACK = {"type": "string", "description": "track name or id"}
FROM = {"type": "number", "description": "start of the excerpt, seconds"}
TO = {"type": "number", "description": "end of the excerpt, seconds"}

TOOLS = [
    ("open_project", "Open a JAD Daw project for editing: makes a working copy (session) so the original is untouched, and returns its state. Always do this before changing anything.",
     obj({"project_path": {"type": "string"}, "fresh": {"type": "boolean", "description": "start the session again from the original"}, "edit_original": {"type": "boolean"}}, ["project_path"]), t_open_project),
    ("get_state", "The whole project as a mixer sees it: tempo, markers, every track with fader, pan, mute, solo, output, inserts with their parameters, sends and regions.", obj({"project": P}, ["project"]), t_get_state),
    ("list_tracks", "A short list of the tracks (name, kind, fader, pan, inserts) and the tempo.", obj({"project": P}, ["project"]), t_list_tracks),
    ("analyze_mix", "Listen to the whole mix (or a part of it): peak, RMS, LUFS (momentary, short-term, integrated), loudness range, true peak, clipping, DC, silence, "
     "third-octave spectrum, centroid, stereo correlation and width, balance, tempo, a loudness timeline, and a plain-language reading of what stands out.",
     obj({"project": P, "from_seconds": FROM, "to_seconds": TO, "tempo": {"type": "boolean"}}, ["project"]), t_analyze_mix),
    ("analyze_track", "Listen to one track alone (after its fader, inserts and buses): the same measurements as analyze_mix.", obj({"project": P, "track": TRACK, "from_seconds": FROM, "to_seconds": TO}, ["project", "track"]), t_analyze_track),
    ("analyze_file", "The same measurements for any audio file (wav, mp3, flac, aiff).", obj({"file": {"type": "string"}, "from_seconds": FROM, "to_seconds": TO}, ["file"]), t_analyze_file),
    ("compare_tracks", "Where two tracks pile up in the spectrum (masking): a score, the worst bands in Hz, and advice (cut one there, or pan them apart).",
     obj({"project": P, "track_a": TRACK, "track_b": TRACK, "from_seconds": FROM, "to_seconds": TO}, ["project", "track_a", "track_b"]), t_compare_tracks),
    ("set_fader", "Set a track's fader in dB (-96 to 24).", obj({"project": P, "track": TRACK, "gain_db": {"type": "number"}}, ["project", "track", "gain_db"]), t_set_fader),
    ("set_pan", "Set a track's pan (-1 left to 1 right).", obj({"project": P, "track": TRACK, "pan": {"type": "number"}}, ["project", "track", "pan"]), t_set_pan),
    ("set_mute", "Mute or unmute a track.", obj({"project": P, "track": TRACK, "mute": {"type": "boolean"}}, ["project", "track", "mute"]), t_set_mute),
    ("set_solo", "Solo or unsolo a track.", obj({"project": P, "track": TRACK, "solo": {"type": "boolean"}}, ["project", "track", "solo"]), t_set_solo),
    ("add_insert", "Add a built-in effect to a track (see list_effects for the ids and parameters: builtin.eq, builtin.compressor, builtin.limiter, builtin.gate, builtin.delay, "
     "builtin.reverb, builtin.gain, builtin.mastering). params are the parameter names with values.",
     obj({"project": P, "track": TRACK, "processor": {"type": "string"}, "params": {"type": "object"}, "position": {"type": "integer"}}, ["project", "track", "processor"]), t_add_insert),
    ("set_insert_param", "Change one parameter of an insert (insert_index counts from 0 in the track's insert list).",
     obj({"project": P, "track": TRACK, "insert_index": {"type": "integer"}, "param": {"type": "string"}, "value": {"type": "number"}}, ["project", "track", "insert_index", "param", "value"]), t_set_insert_param),
    ("set_insert_bypass", "Bypass or enable an insert.", obj({"project": P, "track": TRACK, "insert_index": {"type": "integer"}, "bypass": {"type": "boolean"}}, ["project", "track", "insert_index", "bypass"]), t_set_insert_bypass),
    ("remove_insert", "Remove an insert from a track.", obj({"project": P, "track": TRACK, "insert_index": {"type": "integer"}}, ["project", "track", "insert_index"]), t_remove_insert),
    ("set_send_level", "Set the level of a send in dB (send: the id or the name of its target bus).", obj({"project": P, "track": TRACK, "send": {"type": "string"}, "level_db": {"type": "number"}}, ["project", "track", "send", "level_db"]), t_set_send_level),
    ("set_tempo", "Set the tempo at a beat (default the start).", obj({"project": P, "bpm": {"type": "number"}, "beat": {"type": "number"}}, ["project", "bpm"]), t_set_tempo),
    ("apply_commands", "Apply JSON commands of the project model directly (advanced); all or none are applied.", obj({"project": P, "commands": {"type": "array", "items": {"type": "object"}}}, ["project", "commands"]), t_apply_commands),
    ("run_mastering_assistant", "Add the Mastering Assistant to the master (if missing), analyse the mix and set it up: correction EQ, gain to -14 LUFS, loudness compensation. "
     "Returns the loudness before and after.", obj({"project": P, "from_seconds": FROM, "to_seconds": TO}, ["project"]), t_run_mastering_assistant),
    ("render_excerpt", "Render a part of the project (or one track alone) to a 16-bit WAV file and return its path with its analysis.",
     obj({"project": P, "from_seconds": FROM, "to_seconds": TO, "track": TRACK, "out": {"type": "string"}}, ["project"]), t_render_excerpt),
    ("import_audio", "Bring an audio file (wav, mp3, flac, aiff) into the session as a new track (or on an existing track), starting at start_seconds. Use it to add a reference track to compare with.",
     obj({"project": P, "file": {"type": "string"}, "track": TRACK, "name": {"type": "string"}, "start_seconds": {"type": "number"}}, ["project", "file"]), t_import_audio),
    ("render_image", "A picture (PNG) of the mix, one track or a file: kind spectrogram, waveform or spectrum; look at it for what numbers hide (hum lines, clipped sections, "
     "a missing band, where things happen in time). Returns the file path and how to read it.", obj({"project": P, "file": {"type": "string"}, "track": TRACK, "kind": {"type": "string", "enum": ["spectrogram", "waveform", "spectrum"]},
     "from_seconds": FROM, "to_seconds": TO, "out": {"type": "string"}}), t_render_image),
    ("list_effects", "The built-in effects and instruments with every parameter: name, unit, minimum, maximum, default.", obj({}), t_effects),
    ("history", "What was changed in this session (the undo steps and the last calls).", obj({"project": P}, ["project"]), t_history),
    ("undo", "Undo the last change made in this session (or in the open app when the project is \"live\").", obj({"project": P, "steps": {"type": "integer"}}, ["project"]), t_undo),
    ("live_status", "Is the app linked? The project that is open, whether it plays, where the playhead is, whether it has unsaved changes.", obj({}), t_live_status),
    ("live_meters", "The level meters of the running app: master peak, each track's peak and hold in dB, gain reduction of compressors, playhead. With seconds, the loudest values over that time "
     "are kept (play a section first).", obj({"seconds": {"type": "number"}}), t_live_meters),
    ("check_recording_setup", "Recording assistant: with the app linked and a track armed, listens to the inputs for `seconds` (the performer plays or sings the loudest part) and says whether the level is right, "
     "whether it clips or hisses, how many dB to change at the preamp, and how big the latency is.", obj({"seconds": {"type": "number"}}), t_check_recording_setup),
    ("compare_takes", "Compare the takes (regions) of a track, each analysed alone: peak, loudness, clipping, noise floor, with a ranking by sound quality (not timing or pitch). "
     "Optional region_ids or take_group choose which.", obj({"project": P, "track": TRACK, "region_ids": {"type": "array", "items": {"type": "string"}}, "take_group": {"type": "string"}}, ["project", "track"]), t_compare_takes),
    ("live_transport", "Drive the transport of the running app: play, stop, locate (seconds), reset_peaks.", obj({"action": {"type": "string", "enum": ["play", "stop", "locate", "reset_peaks"]}, "seconds": {"type": "number"}}, ["action"]), t_live_transport),
    ("commit_session", "Write the session back to the original project (the original is kept as <project>.before-ai), or to another folder with `to`.",
     obj({"session": {"type": "string"}, "to": {"type": "string"}}, ["session"]), t_commit_session),
]
TOOL_MAP = {name: (desc, schema, fn) for name, desc, schema, fn in TOOLS}


# ------------------------------------------------------------------------------------------------------ the protocol

PROMPTS = {
    "mix_check": ("Listen to a mix and say what to fix.",
                  "Listen to the mix of the project {project}: analyze_mix, then analyze_track for each track and compare_tracks for pairs that share the same range. Say in plain words what is wrong "
                  "(balance, masking, stereo, dynamics, clipping, headroom) with the numbers behind it, in order of importance. Do not change anything yet."),
    "mix_it": ("Balance and fix the mix, checking each change.",
               "Mix the project {project}. If it is a folder, open_project first and work on the session. Analyze the mix and each track, then change one thing at a time (faders, pans, EQ, compression, "
               "sends), analyze again after each change to check it did what you meant, and undo what did not help. Stop with a short list of what you changed and why; commit_session only if the user agrees."),
    "master_it": ("Master the mix to streaming loudness.",
                  "Master the project {project}: analyze_mix first, run_mastering_assistant, analyze_mix again and check the loudness is about -14 LUFS with a true peak under -1 dBTP. Report before and after, "
                  "and what the correction EQ did. Compare the result with a reference file if the user gave one (import_audio, analyze_track)."),
    "recording_check": ("Check the input level, noise and latency before recording.",
                        "The app is linked (live). Use check_recording_setup while the performer plays the loudest part, report what to change at the preamp, and check again after the change. "
                        "After the takes are recorded, use compare_takes on the track to rank them by sound."),
}


def send(message):
    sys.stdout.write(json.dumps(message, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def call_tool(name, arguments):
    if name not in TOOL_MAP:
        raise ToolError("unknown tool " + name)
    desc, schema, fn = TOOL_MAP[name]
    for key in schema.get("required", []):
        if key not in arguments:
            raise ToolError("missing argument: " + key)
    result = compact(fn(arguments))
    project = arguments.get("project") or arguments.get("session")
    if project and (is_live(project) or os.path.isdir(norm(project))) and name not in ("open_project",):
        log(project, name, arguments, result if len(json.dumps(result)) < 2000 else "(long result)")
    return result


def handle(msg):
    method = msg.get("method")
    mid = msg.get("id")
    params = msg.get("params") or {}
    if method == "initialize":
        send({"jsonrpc": "2.0", "id": mid, "result": {"protocolVersion": params.get("protocolVersion", PROTOCOL), "capabilities": {"tools": {}, "prompts": {}},
                                                       "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
                                                       "instructions": "Tools to look at and listen to a JAD Daw project. Start with open_project, then analyze_mix / analyze_track, change things with the set_* and add_insert tools, "
                                                                       "check again, and commit_session when the user agrees. Levels are dB; LUFS targets: about -14 for streaming."}})
    elif method == "ping":
        send({"jsonrpc": "2.0", "id": mid, "result": {}})
    elif method == "tools/list":
        send({"jsonrpc": "2.0", "id": mid, "result": {"tools": [{"name": n, "description": d, "inputSchema": s} for n, (d, s, _f) in TOOL_MAP.items()]}})
    elif method == "tools/call":
        try:
            result = call_tool(params.get("name"), params.get("arguments") or {})
            send({"jsonrpc": "2.0", "id": mid, "result": {"content": [{"type": "text", "text": json.dumps(result, indent=1, ensure_ascii=False)}], "isError": False}})
        except ToolError as e:
            send({"jsonrpc": "2.0", "id": mid, "result": {"content": [{"type": "text", "text": str(e)}], "isError": True}})
        except subprocess.TimeoutExpired:
            send({"jsonrpc": "2.0", "id": mid, "result": {"content": [{"type": "text", "text": "the analysis took too long"}], "isError": True}})
        except Exception as e:  # a bug here must not kill the server
            send({"jsonrpc": "2.0", "id": mid, "result": {"content": [{"type": "text", "text": "internal error: %s" % e}], "isError": True}})
    elif method in ("resources/list",):
        send({"jsonrpc": "2.0", "id": mid, "result": {"resources": []}})
    elif method == "prompts/list":
        send({"jsonrpc": "2.0", "id": mid, "result": {"prompts": [{"name": n, "description": d, "arguments": [{"name": "project", "description": "project folder, or live", "required": True}]} for n, (d, _t) in PROMPTS.items()]}})
    elif method == "prompts/get":
        name = params.get("name")
        if name not in PROMPTS:
            send({"jsonrpc": "2.0", "id": mid, "error": {"code": -32602, "message": "unknown prompt " + str(name)}})
        else:
            project = (params.get("arguments") or {}).get("project", "live")
            send({"jsonrpc": "2.0", "id": mid, "result": {"description": PROMPTS[name][0], "messages": [{"role": "user", "content": {"type": "text", "text": PROMPTS[name][1].replace("{project}", project)}}]}})
    elif mid is not None and not str(method).startswith("notifications/"):
        send({"jsonrpc": "2.0", "id": mid, "error": {"code": -32601, "message": "method not found: %s" % method}})


def main():
    sys.stdin.reconfigure(encoding="utf-8")
    sys.stdout.reconfigure(encoding="utf-8")
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            handle(json.loads(line))
        except json.JSONDecodeError:
            send({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": "parse error"}})


if __name__ == "__main__":
    main()
