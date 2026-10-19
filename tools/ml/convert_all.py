#!/usr/bin/env python3
"""Convert every mlProgram .mil in a folder of .mlmodelc bundles to ONNX (one file per .mil)."""
import os
import sys
import traceback

sys.path.insert(0, os.path.dirname(__file__))
from mil2onnx import convert  # noqa: E402

src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
ok, bad = 0, []
for d in sorted(os.listdir(src)):
    p = os.path.join(src, d)
    if not d.endswith(".mlmodelc"):
        continue
    for f in sorted(os.listdir(p)):
        if not f.endswith(".mil"):
            continue
        out = os.path.join(dst, d[:-len(".mlmodelc")] + "." + f[:-4] + ".onnx")
        if os.path.exists(out) and "--force" not in sys.argv:
            ok += 1
            continue
        try:
            convert(p, out, os.path.join(p, f), check=os.path.getsize(os.path.join(p, f)) < 3_000_000)
            ok += 1
        except Exception as e:  # noqa: BLE001
            bad.append((d, f, repr(e)[:200]))
print("converted", ok, "failed", len(bad))
for b in bad:
    print(b)
