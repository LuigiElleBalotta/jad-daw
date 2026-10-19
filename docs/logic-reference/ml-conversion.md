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
* Not verifiable without Core ML: `OnsetDetectionCNN`'s `inner_product` weight layout (taken as `[nC, nB]`) and the flatten order (NCHW).

## Checking against Core ML (needs a Mac)
`tools/ml/coreml_reference.swift` runs every `.mlmodelc` through Core ML (CPU only) on deterministic inputs and writes the outputs;
`tools/ml/compare_with_coreml.py` runs the ONNX files on the same inputs and prints max |diff| per output. This is the only way to prove the
conversions (and the two unverified Espresso facts) numerically; until it has been run on the Mac they are "structurally verified" only.

## Icons
`tools/trace_icons.py` (vtracer, needs Python 3.11: the 3.14 wheel crashes) traces PNG/TIFF to SVG. 91 icons from
`jad-daw-private/car-renditions` and `legacy-mixer-graphics` are in `assets/icons/logic-derived/` (raw traces, to be simplified/recoloured before
use in the UI).
