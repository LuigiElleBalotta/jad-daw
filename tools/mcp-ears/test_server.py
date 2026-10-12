#!/usr/bin/env python3
"""Smoke test of the MCP server: speaks the protocol to it over stdio on a demo project (needs a built lpc-cli).

    python tools/mcp-ears/test_server.py
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jad_ears_server as srv  # noqa: E402


class Client:
    def __init__(self):
        self.proc = subprocess.Popen([sys.executable, os.path.join(HERE, "jad_ears_server.py")], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8")
        self.n = 0

    def call(self, method, params=None):
        self.n += 1
        self.proc.stdin.write(json.dumps({"jsonrpc": "2.0", "id": self.n, "method": method, "params": params or {}}) + "\n")
        self.proc.stdin.flush()
        reply = json.loads(self.proc.stdout.readline())
        assert reply["id"] == self.n, reply
        return reply

    def tool(self, tool_name, **args):
        reply = self.call("tools/call", {"name": tool_name, "arguments": args})["result"]
        text = reply["content"][0]["text"]
        return reply["isError"], (json.loads(text) if not reply["isError"] else text)

    def close(self):
        self.proc.stdin.close()
        self.proc.wait(timeout=10)


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        check.failed += 1


check.failed = 0


def main():
    cli = srv.find_cli()
    work = tempfile.mkdtemp(prefix="jad-mcp-test-")
    project = os.path.join(work, "demo")
    subprocess.run([cli, "demo", project], check=True, capture_output=True)
    c = Client()
    try:
        init = c.call("initialize", {"protocolVersion": "2024-11-05"})["result"]
        check(init["serverInfo"]["name"] == "jad-ears", "initialize")
        tools = c.call("tools/list")["result"]["tools"]
        names = {t["name"] for t in tools}
        check({"open_project", "analyze_mix", "set_fader", "run_mastering_assistant", "undo", "commit_session"} <= names, "tools/list has %d tools" % len(tools))

        prompts = c.call("prompts/list")["result"]["prompts"]
        check({p["name"] for p in prompts} >= {"mix_check", "master_it", "recording_check"}, "prompts/list")
        got = c.call("prompts/get", {"name": "master_it", "arguments": {"project": "X"}})["result"]
        check("project X" in got["messages"][0]["content"]["text"], "prompts/get fills the project")
        err, state = c.tool("list_tracks", project=project)
        check(not err and len(state["tracks"]) > 0, "list_tracks")
        first = state["tracks"][0]

        err, msg = c.tool("set_fader", project=project, track=first["name"], gain_db=-6)
        check(err and "open_project" in msg, "writing the original is refused")

        err, opened = c.tool("open_project", project_path=project)
        check(not err and opened["session"], "open_project makes a session")
        session = opened["project"]

        err, done = c.tool("set_fader", project=session, track=first["name"], gain_db=-6)
        check(not err and done["undoSteps"] == 1, "set_fader in the session: %s" % (done if err else ""))
        _, after = c.tool("list_tracks", project=session)
        check(abs(after["tracks"][0]["gainDb"] + 6) < 1e-6, "fader is -6")
        _, orig = c.tool("list_tracks", project=project)
        check(abs(orig["tracks"][0]["gainDb"] - first["gainDb"]) < 1e-6, "the original is untouched")

        err, _ = c.tool("add_insert", project=session, track=first["name"], processor="builtin.gain", params={"gainDb": 3})
        check(not err, "add_insert: %s" % _)
        err, _ = c.tool("set_insert_param", project=session, track=first["name"], insert_index=0, param="gainDb", value=-3)
        check(not err, "set_insert_param: %s" % _)
        err, _ = c.tool("set_pan", project=session, track=first["name"], pan=0.5)
        check(not err, "set_pan")

        err, mix = c.tool("analyze_mix", project=session, to_seconds=4, tempo=False)
        check(not err and "reading" in mix, "analyze_mix has a reading")
        err, tr = c.tool("analyze_track", project=session, track=first["name"], to_seconds=4)
        check(not err, "analyze_track: %s" % (tr if err else ""))
        if len(state["tracks"]) > 1:
            err, m = c.tool("compare_tracks", project=session, track_a=state["tracks"][0]["name"], track_b=state["tracks"][1]["name"], to_seconds=4)
            check(not err and "score" in m, "compare_tracks")
        err, takes = c.tool("compare_takes", project=session, track="Tone")
        check(not err and len(takes["takes"]) == 1 and takes["bestBySound"], "compare_takes on one region: %s" % (takes if err else ""))
        err, img = c.tool("render_image", project=session, track=first["name"], kind="spectrogram")
        check(not err and os.path.getsize(img["image"]) > 500 and open(img["image"], "rb").read(4)[1:] == b"PNG", "render_image writes a PNG: %s" % (img if err else ""))
        err, cmp = c.tool("compare_audio", a={"project": session, "track": first["name"], "to_seconds": 2}, b={"file": os.path.join(session, "audio", "tone.wav")})
        check(not err and "tonalDiffByBand" in cmp and cmp["reading"], "compare_audio: %s" % (cmp if err else ""))
        err, eff = c.tool("list_effects")
        check(not err and any(e["id"] == "builtin.compressor" for e in eff["effects"]), "list_effects")

        err, imp = c.tool("import_audio", project=session, file=os.path.join(session, "audio", "tone.wav"), name="Reference")
        check(not err and imp["ok"], "import_audio adds a track: %s" % (imp if err else ""))
        _, listed = c.tool("list_tracks", project=session)
        check(any(t["name"] == "Reference" and t["regions"] == 1 for t in listed["tracks"]), "the imported track has its region")
        err, live = c.tool("live_status")
        check(err and "AI Assistant Link" in live or not err, "live_status says how to link when the app is off")
        err, bad = c.tool("set_fader", project=session, track="no such track", gain_db=0)
        check(err, "unknown track is an error")
        err, hist = c.tool("history", project=session)
        check(not err and hist["undoSteps"] >= 5, "history counts %s steps" % hist.get("undoSteps"))

        for _i in range(hist["undoSteps"]):
            c.tool("undo", project=session)
        _, back = c.tool("list_tracks", project=session)
        check(abs(back["tracks"][0]["gainDb"] - first["gainDb"]) < 1e-6 and abs(back["tracks"][0]["pan"] - first["pan"]) < 1e-6, "undo goes back to the start")
        err, _ = c.tool("undo", project=session)
        check(err, "nothing left to undo")

        c.tool("set_fader", project=session, track=first["name"], gain_db=-9)
        err, commit = c.tool("commit_session", session=session)
        check(not err and commit["originalKeptAs"], "commit_session keeps the original")
        _, final = c.tool("list_tracks", project=project)
        check(abs(final["tracks"][0]["gainDb"] + 9) < 1e-6, "the original has the change")
        check(os.path.exists(os.path.join(project + ".before-ai", "project.json")), ".before-ai exists")
        check(os.path.exists(os.path.join(session, ".ai-log.jsonl")), "audit log written")
    finally:
        c.close()
        shutil.rmtree(work, ignore_errors=True)
    print("%d failed" % check.failed)
    return 1 if check.failed else 0


if __name__ == "__main__":
    sys.exit(main())
