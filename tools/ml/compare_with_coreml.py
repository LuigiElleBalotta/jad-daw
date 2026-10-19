#!/usr/bin/env python3
"""Compare ONNX conversions with the outputs Core ML produced (see coreml_reference.swift).

    python compare_with_coreml.py <coreml out folder with manifest.json> <onnx folder> [--tol 1e-3]

For each model the ONNX files whose input names and shapes match are run on the same deterministic inputs.
"""
import argparse
import json
import os

import numpy as np
import onnxruntime as ort


def make_input(shape, k):
    n = int(np.prod(shape))
    i = np.arange(n, dtype=np.uint64)
    h = ((i * np.uint64(2654435761)) & np.uint64(0xFFFFFFFF)) ^ np.uint64((k + 1) * 97 & 0xFFFFFFFF)
    return ((h.astype(np.float64) / 4294967296.0 - 0.5) * 0.6).astype(np.float32).reshape(shape)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("coreml")
    ap.add_argument("onnx")
    ap.add_argument("--tol", type=float, default=1e-3)
    a = ap.parse_args()
    manifest = json.load(open(os.path.join(a.coreml, "manifest.json")))
    onnx_files = sorted(f for f in os.listdir(a.onnx) if f.endswith(".onnx"))
    bad = 0
    for m in manifest:
        stem = m["model"][:-len(".mlmodelc")]
        if "error" in m:
            print("COREML-ERR", stem, m["error"][:100])
            continue
        want = {i["name"]: i["shape"] for i in m["inputs"]}
        ks = {i["name"]: i["k"] for i in m["inputs"]}
        matched = 0
        for f in (x for x in onnx_files if x.startswith(stem + ".")):
            s = ort.InferenceSession(os.path.join(a.onnx, f), providers=["CPUExecutionProvider"])
            have = {i.name: [d if isinstance(d, int) else -1 for d in i.shape] for i in s.get_inputs()}
            if have != want:
                continue
            matched += 1
            res = s.run(None, {n: make_input(want[n], ks[n]) for n in want})
            for o, r in zip([x.name for x in s.get_outputs()], res):
                ref = next((x for x in m["outputs"] if x["name"] == o), None)
                if ref is None:
                    continue
                y = np.fromfile(os.path.join(a.coreml, ref["file"]), np.float32).reshape(ref["shape"])
                if y.shape != r.shape:
                    r = r.reshape(y.shape) if y.size == r.size else r
                err = float(np.max(np.abs(y - r))) if y.shape == r.shape else float("inf")
                flag = "OK " if err <= a.tol else "BAD"
                bad += err > a.tol
                print(flag, f, o, "maxabs %.3g" % err)
        if not matched:
            print("NO-ONNX-MATCH", stem)
    print("mismatches:", bad)


if __name__ == "__main__":
    main()
