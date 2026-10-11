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
        with open(os.path.join(norm(project), ".ai-log.jsonl"), "a", encoding="utf-8") as f:
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
    if is_session(project) or os.environ.get("JAD_MCP_WRITE_ORIGINAL") == "1":
        return norm(project)
    raise ToolError("this tool changes the project: call open_project first and use the session it returns (the original stays untouched), "
                    "or set JAD_MCP_WRITE_ORIGINAL=1")


def snapshot(project):
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
    return run_cli(["analyze", norm(a["project"])] + range_args(a) + ([] if a.get("tempo", True) else ["--no-tempo"]))


def t_analyze_track(a):
    track, _ = resolve_track(a["project"], a["track"])
    return run_cli(["analyze", norm(a["project"]), "--track", track["id"]] + range_args(a) + ([] if a.get("tempo", False) else ["--no-tempo"]))


def t_analyze_file(a):
    return run_cli(["analyze", norm(a["file"])] + range_args(a) + ([] if a.get("tempo", True) else ["--no-tempo"]))


def t_compare_tracks(a):
    ta, _ = resolve_track(a["project"], a["track_a"])
    tb, _ = resolve_track(a["project"], a["track_b"])
    return run_cli(["masking", norm(a["project"]), ta["id"], tb["id"]] + range_args(a))


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


def t_run_mastering_assistant(a):
    path = require_writable(a["project"])
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
    args = ["render", norm(a["project"]), out, "--bits", "16"]
    if a.get("to_seconds") is not None:
        args += ["--seconds", str(a["to_seconds"])]
    if a.get("track"):
        track, _ = resolve_track(a["project"], a["track"])
        args += ["--track", track["id"]]
    run_cli(args)
    report = run_cli(["analyze", out, "--no-tempo"] + ([] if a.get("from_seconds") is None else ["--seconds", "%s:%s" % (a["from_seconds"], a.get("to_seconds", 600))]))
    return {"file": out, "analysis": report}


def t_effects(a):
    return run_cli(["effects"])


def t_history(a):
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


P = {"type": "string", "description": "the project folder (a session from open_project for tools that change it)"}
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
    ("list_effects", "The built-in effects and instruments with every parameter: name, unit, minimum, maximum, default.", obj({}), t_effects),
    ("history", "What was changed in this session (the undo steps and the last calls).", obj({"project": P}, ["project"]), t_history),
    ("undo", "Undo the last change made in this session.", obj({"project": P}, ["project"]), t_undo),
    ("commit_session", "Write the session back to the original project (the original is kept as <project>.before-ai), or to another folder with `to`.",
     obj({"session": {"type": "string"}, "to": {"type": "string"}}, ["session"]), t_commit_session),
]
TOOL_MAP = {name: (desc, schema, fn) for name, desc, schema, fn in TOOLS}


# ------------------------------------------------------------------------------------------------------ the protocol

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
    result = fn(arguments)
    project = arguments.get("project") or arguments.get("session")
    if project and os.path.isdir(norm(project)) and name not in ("open_project",):
        log(project, name, arguments, result if len(json.dumps(result)) < 2000 else "(long result)")
    return result


def handle(msg):
    method = msg.get("method")
    mid = msg.get("id")
    params = msg.get("params") or {}
    if method == "initialize":
        send({"jsonrpc": "2.0", "id": mid, "result": {"protocolVersion": params.get("protocolVersion", PROTOCOL), "capabilities": {"tools": {}},
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
    elif method in ("prompts/list",):
        send({"jsonrpc": "2.0", "id": mid, "result": {"prompts": []}})
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
