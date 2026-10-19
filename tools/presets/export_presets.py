#!/usr/bin/env python3
"""Export Logic Pro's factory plug-in presets (.pst) to core/data/effect_presets.json.

    python export_presets.py <stock-plugin-presets dir> <match-eq-presets dir> <out json>

.pst container (both byte orders exist; the first 32-bit word is the file size, which tells them apart):
    u32 size | u32 flag | u32 count | 8 bytes magic ("GAMETSPP", byte-swapped in big-endian files) | u32 type tag | u32 0 | float32 x N
The floats are the parameters, float k = the parameter with ID k of the plug-in's CSParameterOrder.plist (checked on the factory
presets: every column of the 115 Compressor presets and 149 Channel EQ presets falls in the range of the control it is named after).
Older big-endian presets have fewer floats (no Mix on the Compressor): the missing ones keep their default.
"""
import glob
import json
import os
import struct
import sys

# float index (= parameter ID) -> parameter name of our effect, per plug-in
COMPRESSOR = {0: "threshold", 1: "ratio", 2: "attack", 3: "release", 4: "gain", 5: "knee", 6: "peakRms", 7: "autoGain",
              8: "outputDistortion", 9: "circuit", 11: "limiterThreshold", 12: "limiter", 13: "autoRelease", 24: "mix"}
EQ = {}
for i, band in enumerate(["lc", "ls", "p1", "p2", "p3", "p4", "hs", "hc"]):
    base = 4 * i
    EQ[base] = band + "On"
    EQ[base + 1] = band + "Freq"
    if band in ("lc", "hc"):
        EQ[base + 2] = band + "Slope"
    else:
        EQ[base + 2] = band + "Gain"
    EQ[base + 3] = band + "Q"
EQ[32] = "masterGain"
GATE = {0: "threshold", 1: "hysteresis", 2: "range", 3: "attack", 4: "hold", 5: "release", 6: "keyHighCut", 7: "keyLowCut", 8: "lookahead",
        12: "keyFilter"}
LIMITER = {0: "gain", 1: "lookahead", 2: "softKnee", 3: "release", 4: "ceiling", 6: "truePeak"}
ADAPTIVE = {0: "inputScale", 1: "gain", 2: "ceiling", 3: "mode", 4: "lookahead", 5: "removeDc"}

PLUGINS = {"Compressor": ("builtin.compressor", COMPRESSOR), "Channel EQ": ("builtin.eq", EQ), "Noise Gate": ("builtin.gate", GATE),
           "Limiter": ("builtin.limiter", LIMITER), "Adaptive Limiter": ("builtin.adaptivelimiter", ADAPTIVE)}


def read_pst(path):
    b = open(path, "rb").read()
    endian = "<" if struct.unpack("<I", b[:4])[0] == len(b) else ">"
    if struct.unpack(endian + "I", b[:4])[0] != len(b):
        raise ValueError("size word does not match: " + path)
    n = (len(b) - 28) // 4
    return [round(x, 6) for x in struct.unpack(endian + "%df" % n, b[28:28 + 4 * n])]


def round_value(v):
    return int(v) if abs(v - round(v)) < 1e-6 else round(v, 4)


def main():
    presets_dir, match_dir, out = sys.argv[1:4]
    doc = {"version": 1, "plugins": {}, "matchEq": {}}
    for plugin, (pid, names) in PLUGINS.items():
        items = []
        default = None
        for f in sorted(glob.glob(os.path.join(presets_dir, plugin, "raw-pst", "**", "*.pst"), recursive=True)):
            rel = os.path.relpath(f, os.path.join(presets_dir, plugin, "raw-pst")).replace("\\", "/")
            parts = rel[:-4].split("/")
            floats = read_pst(f)
            params = {names[k]: round_value(floats[k]) for k in names if k < len(floats)}
            entry = {"category": parts[0] if len(parts) > 1 else "", "name": parts[-1], "params": params}
            if parts[-1] == "#default":
                default = params
            else:
                items.append(entry)
        doc["plugins"][pid] = {"default": default, "presets": items}
        print(plugin, len(items), "presets")
    # Match EQ: one target curve per preset (the mean of its channels), on the preset files' own frequency axis
    models = {}
    freq = None
    for kind in ("stereo", "surround"):
        for f in sorted(glob.glob(os.path.join(match_dir, kind, "*.json"))):
            d = json.load(open(f))
            freq = freq or d["freq_hz"]
            ch = d["target_magnitude_db"]
            curve = [round(sum(c[i] for c in ch) / len(ch), 2) for i in range(len(ch[0]))]
            models[kind + "/" + os.path.splitext(os.path.basename(f))[0]] = curve if len(curve) == len(freq) else {"freqHz": [round(x, 3) for x in d["freq_hz"]], "db": curve}
    for k in range(1, 5):
        d = json.load(open(os.path.join(match_dir, "mastering-assistant-models", "AM_Curve_%d.json" % k)))
        ch = d["chunks"]["SMMT"]["channels_db"]
        axis = d["chunks"]["SMMT"]["freq_hz"]
        curve = [round(sum(c[i] for c in ch) / len(ch), 2) for i in range(len(ch[0]))]
        models["mastering/AM_Curve_%d" % k] = curve if len(curve) == len(freq) else {"freqHz": [round(x, 3) for x in axis], "db": curve}
    doc["matchEq"] = {"freqHz": [round(x, 3) for x in freq], "curves": models}
    print("Match EQ", len(models), "curves of", len(freq), "points")
    json.dump(doc, open(out, "w", encoding="utf8"), separators=(",", ":"))
    print("wrote", out, os.path.getsize(out), "bytes")


if __name__ == "__main__":
    main()
