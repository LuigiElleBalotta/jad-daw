#!/usr/bin/env python3
"""Trace raster icons (PNG/TIFF) to SVG with vtracer. usage: trace_icons.py <out dir> <file>..."""
import os
import sys
import tempfile

import vtracer
from PIL import Image

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
for f in sys.argv[2:]:
    base = os.path.splitext(os.path.basename(f))[0]
    im = Image.open(f).convert("RGBA")
    tmp = os.path.join(tempfile.gettempdir(), base + ".png")
    im.save(tmp)
    vtracer.convert_image_to_svg_py(tmp, os.path.join(out, base + ".svg"), colormode="color", hierarchical="stacked",
                                    mode="spline", filter_speckle=2, color_precision=6, layer_difference=12,
                                    corner_threshold=60, length_threshold=3.5, splice_threshold=45, path_precision=2)
    print(base, im.size)
