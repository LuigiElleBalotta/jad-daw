# Converting Logic Pro's Core ML models to ONNX

Logic Pro 11.2 ships 69 `.mlmodelc` bundles (`MAMachineLearning.framework`, see `machine-learning-models.md`). Core ML does not run on Windows, so
they are converted to ONNX (run with onnxruntime). The converted files are not in git (`jad-daw-private/onnx/`, 319 files); the converters are.

## Two formats inside the bundles

| Format | Files | Models | Converter |
|---|---|---|---|
| mlProgram (MIL) | `model*.mil` + `weights/weight.bin` | 67 bundles, 319 `.mil` (7 block sizes 32..2048 plus a `streaming` variant per bundle) | `tools/ml/mil2onnx.py` |
| neuralNetwork (Espresso) | `model.espresso.net/.shape/.weights` | `OnsetDetectionCNN`, `RTPT` | `tools/ml/espresso2onnx.py` |

`tools/ml/convert_all.py <models dir> <out dir>` converts every `.mil`; all 319 convert. The shape of every graph output is checked against the
shape the MIL declares (`--check`, run on files under 3 MB).

### MIL facts
* One statement per line: `tensor<T,[dims]> name = op(args)[name = ...];`, constants are `const()[name=..., val=...]`, weights are `BLOBFILE` offsets.
  Each blob has a 64-byte header: magic `0xDEADBEEF`, dtype, byte size (uint64 at +8), absolute data offset (uint64 at +16).
* Float literals can be hex floats (`0x1.064p-10`).
* Op set used (all map 1:1 to ONNX): conv, add, sub, mul, real_div, sigmoid, abs, relu, elu, softmax, concat, slice_by_size, expand_dims, reshape,
  transpose, pad, max_pool, batch_norm, matmul, cast. Anything else raises, a conversion is never silently wrong.
* The streaming models carry their state as explicit inputs/outputs (`*_in_state` / `*_out_state`): the ONNX graphs are stateless, the host feeds the
  state back (the `audio_in_state` is the last 12 samples of the previous block, each dilated conv has its own state).
* The `func main<...>(...)` header can be followed by a `[BNNSOptions = ...]` attribute before the `{`.
* MIL conv `pad` is `[top, bottom, left, right]` (or `[left, right]` for 1-D), ONNX wants `[begins..., ends...]` i.e. `p[0::2] + p[1::2]`.

### Espresso facts (found against the mlProgram twin)
`RTPT` (Espresso, fp16 blobs) and `RTPTM` (mlProgram) are the same network with the same weights (the first conv bias is identical), which made
the format checkable: the ONNX of both give the same output on the same input (max |diff| 2.5e-3, correlation 0.9999, the difference is fp16 rounding).
* weights file: `uint64 N`, then `N` pairs `(uint64 index, uint64 byte size)`, blobs follow back to back; layers refer to the index (`blob_weights`,
  `blob_biases`, `blob_batchnorm_params`, `_f16` suffix when half precision).
* batchnorm blob: per channel `[gamma, beta, mean, sqrt(var + eps)]` interleaved (C x 4), epsilon already folded into the last value.
* activation `mode` 2 = leaky ReLU (`alpha`), 3 = sigmoid, 0 = ReLU; `fused_relu` on convolutions; the `reshape` layer of RTPT = transpose
  `[0,3,2,1]` then reshape to `[-1, 360]`.
* `OnsetDetectionCNN`: the `inner_product` weights are `[nC, nB]` (confirmed) but the flatten layer flattens in **H, W, C** order, not NCHW. Found on 2026-10-21
  from the Mac's Core ML run (max-abs-err 0.226 with NCHW): on the deterministic input the output must be 0.3988 - 0.226 = 0.1728, and only the (H, W, C) order gives
  it (0.17290). `espresso2onnx.py` now transposes `[0, 2, 3, 1]` before the Flatten.

## Checking against Core ML (needs a Mac)
`tools/ml/coreml_reference.swift` runs every `.mlmodelc` through Core ML (CPU only) on deterministic inputs and writes the outputs;
`tools/ml/compare_with_coreml.py` runs the ONNX files on the same inputs and prints max |diff| per output. This is the only way to prove the
conversions (and the two unverified Espresso facts) numerically; until it has been run on the Mac they are "structurally verified" only.

## Icons
`tools/trace_icons.py` (vtracer, needs Python 3.11: the 3.14 wheel crashes) traces PNG/TIFF to SVG. 91 icons from
`jad-daw-private/car-renditions` and `legacy-mixer-graphics` are in `assets/icons/logic-derived/` (raw traces, to be simplified/recoloured before
use in the UI).

## Analog Model networks (T, TSO, G, G_SO, S, S_SO), measured through onnxruntime

All six converted networks output audio (`var_849`, `[1,1,1,1024]`), not a scalar: the "scalar" reading of the Core ML metadata does not hold for the ONNX
graphs (the `_in_state`/`_out_state` pairs match by name). Gain for a 1 kHz sine against its level (dB, output re input): linear up to about -30 dBFS (TSO -2.1,
T -3.5, G_SO -1.1, G -4.3, S_SO -4.0, S -4.3), then compressing: at 0 dBFS in the output is 15 to 18 dB under the input. So they behave as a level-dependent
compressor/colour with a knee near -27 dBFS, and the chain must feed them a level and make it up afterwards (`MasteringProcessor` feeds -12 dB and makes up 12 dB
plus the linear gain; Apple's real scaling is an open question for the Mac pass).

## CPU cost on Windows (2026-10-21, onnxruntime CPU, blocks of 512 frames at 48 kHz, one instance)

Measured with `lpc_tests "[.bench]"` (`tests/test_ml.cpp`): Mastering Assistant Clean 0.5 % of real time, Valve / Punch / Transparent about 10.6 % each, Neural Saturation 22-30 % depending on the model family. So the Analog Model characters run in real time without Apple silicon; Neural Saturation is the heavy one (a handful of instances would fill a core).
