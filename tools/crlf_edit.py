#!/usr/bin/env python3
"""Insert the text of TEXTFILE after the one line of FILE that contains ANCHOR, keeping FILE's line endings.

usage: crlf_edit.py FILE ANCHOR TEXTFILE
"""
import pathlib
import sys

path, anchor, text_path = pathlib.Path(sys.argv[1]), sys.argv[2], pathlib.Path(sys.argv[3])
raw = path.read_bytes().decode("utf-8")
crlf = "\r\n" in raw
lines = raw.replace("\r\n", "\n").split("\n")
hits = [i for i, line in enumerate(lines) if anchor in line]
assert len(hits) == 1, f"{len(hits)} lines contain the anchor {anchor!r}"
new = text_path.read_text(encoding="utf-8").rstrip("\n").split("\n")
lines[hits[0] + 1:hits[0] + 1] = new
out = "\n".join(lines)
path.write_bytes((out.replace("\n", "\r\n") if crlf else out).encode("utf-8"))
