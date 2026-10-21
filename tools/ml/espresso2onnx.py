#!/usr/bin/env python3
"""Convert an old-style (neuralNetwork) .mlmodelc, i.e. model.espresso.{net,shape,weights}, to ONNX.

Layout facts (found by comparing RTPT against its mlProgram twin RTPTM, see docs/logic-reference/ml-conversion.md):
  * weights file: uint64 blob count N, then N pairs (uint64 index, uint64 byte size), then the blobs back to back.
  * batchnorm blob: per channel [gamma, beta, mean, sqrt(var + eps)] interleaved (C x 4); epsilon is already folded in.
  * fp16 blobs are named *_f16 in the layer, everything else is fp32.
  * activation mode: 2 = leaky ReLU (alpha), 3 = sigmoid. Flatten mode 1 = H, W, C order (verified against Core ML on a Mac:
    NCHW gave 0.399, HWC gives 0.173 = Core ML's 0.1728).
  * convolution weights are [C_out, K_in, Ny, Nx]; inner product weights are [nC, nB] (out, in).

    python espresso2onnx.py <model.mlmodelc> <out.onnx> [--check]
"""
import argparse
import json
import os
import struct

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper


def read_blobs(path):
    w = open(path, "rb").read()
    n, = struct.unpack_from("<Q", w, 0)
    sizes = [struct.unpack_from("<Q", w, 8 + 16 * k + 8)[0] for k in range(n)]
    off, offs = 8 + 16 * n, []
    for s in sizes:
        offs.append(off)
        off += s
    if off != len(w):
        raise ValueError("weights file size mismatch: %d != %d" % (off, len(w)))
    return w, offs, sizes


class Conv:
    def __init__(self, d):
        self.dir = d
        self.net = json.load(open(os.path.join(d, "model.espresso.net")))
        self.shape = json.load(open(os.path.join(d, "model.espresso.shape")))["layer_shapes"]
        self.w, self.offs, self.sizes = read_blobs(os.path.join(d, "model.espresso.weights"))
        self.nodes, self.inits = [], {}
        self.cnt = 0

    def blob(self, layer, key, shape=None):
        for suffix, dt in (("_f16", np.float16), ("", np.float32)):
            if key + suffix in layer:
                i = layer[key + suffix]
                a = np.frombuffer(self.w, dt, count=self.sizes[i] // np.dtype(dt).itemsize, offset=self.offs[i]).astype(np.float32)
                return a.reshape(shape) if shape else a
        return None

    def init(self, arr, hint):
        self.cnt += 1
        name = "%s_%d" % (hint, self.cnt)
        self.inits[name] = np.asarray(arr)
        return name

    def emit(self, op, ins, outs, **kw):
        self.cnt += 1
        self.nodes.append(helper.make_node(op, ins, outs, name="n%d" % self.cnt, **kw))

    def shp(self, name):
        s = self.shape[name]
        return [s.get("n", 1), s["k"], s["h"], s["w"]]

    def build(self):
        layers = self.net["layers"]
        tops = {l["top"] for l in layers}
        bottoms = {b for l in layers for b in l["bottom"].split(",")}
        in_names = sorted(bottoms - tops)
        out_names = [l["top"] for l in layers if l["top"] not in bottoms]
        alias = {}
        for l in layers:
            typ, bot, top = l["type"], l["bottom"], l["top"]
            bot = alias.get(bot, bot)
            if typ == "convolution":
                K, C, Nx, Ny = l["K"], l["C"], l["Nx"], l["Ny"]
                wgt = self.blob(l, "blob_weights", (C, K // l.get("n_groups", 1), Ny, Nx))
                ins = [bot, self.init(wgt, "w")]
                if l.get("has_biases"):
                    ins.append(self.init(self.blob(l, "blob_biases"), "b"))
                out = top if not l.get("fused_relu") else top + "_pre"
                self.emit("Conv", ins, [out], kernel_shape=[Ny, Nx], strides=[l.get("stride_y", 1), l.get("stride_x", 1)],
                          pads=[l.get("pad_t", 0), l.get("pad_l", 0), l.get("pad_b", 0), l.get("pad_r", 0)],
                          group=l.get("n_groups", 1))
                if l.get("fused_relu"):
                    self.emit("Relu", [out], [top])
            elif typ == "batchnorm":
                C = l["C"]
                p = self.blob(l, "blob_batchnorm_params", (C, 4))
                gamma, beta, mean, std = p[:, 0], p[:, 1], p[:, 2], p[:, 3]
                self.emit("BatchNormalization", [bot, self.init(gamma, "g"), self.init(beta, "be"), self.init(mean, "m"),
                                                 self.init(std * std, "v")], [top], epsilon=0.0)
            elif typ == "activation":
                mode = l["mode"]
                if mode == 2:
                    self.emit("LeakyRelu", [bot], [top], alpha=float(l["alpha"]))
                elif mode == 3:
                    self.emit("Sigmoid", [bot], [top])
                elif mode == 0:
                    self.emit("Relu", [bot], [top])
                else:
                    raise NotImplementedError("activation mode %d" % mode)
            elif typ == "pool":
                if l["avg_or_max"] != 1:
                    raise NotImplementedError("average pooling")
                self.emit("MaxPool", [bot], [top], kernel_shape=[l["size_y"], l["size_x"]], strides=[l["stride_y"], l["stride_x"]],
                          pads=[l.get("pad_t", 0), l.get("pad_l", 0), l.get("pad_b", 0), l.get("pad_r", 0)])
            elif typ == "flatten":
                self.emit("Transpose", [bot], [top + "_t"], perm=[0, 2, 3, 1])
                self.emit("Flatten", [top + "_t"], [top], axis=1)
            elif typ == "inner_product":
                nB, nC = l["nB"], l["nC"]
                wgt = self.blob(l, "blob_weights", (nC, nB))
                ins = [bot, self.init(wgt, "w")]
                if l.get("has_biases"):
                    ins.append(self.init(self.blob(l, "blob_biases"), "b"))
                self.emit("Gemm", ins, [top], transB=1)
            elif typ == "cast":
                alias[top] = bot            # all arithmetic is fp32 here
            elif typ == "reshape":
                # [1, C, 1, N] -> [N, C] : move the channel axis last, as the mlProgram twin does
                self.emit("Transpose", [bot], [top + "_t"], perm=[0, 3, 2, 1])
                self.emit("Reshape", [top + "_t", self.init(np.array([l["dst_h"], l["dst_w"]], np.int64), "sh")], [top])
            else:
                raise NotImplementedError("layer type " + typ)
        final = [alias.get(o, o) for o in out_names]
        inputs = [helper.make_tensor_value_info(n, TensorProto.FLOAT, self.shp(n)) for n in in_names]
        outputs = [helper.make_tensor_value_info(n, TensorProto.FLOAT, self.shp(n) if n in self.shape else [None, None]) for n in final]
        inits = [numpy_helper.from_array(v, k) for k, v in self.inits.items()]
        g = helper.make_graph(self.nodes, "espresso", inputs, outputs, inits)
        m = helper.make_model(g, opset_imports=[helper.make_opsetid("", 17)])
        m.ir_version = 8
        onnx.checker.check_model(m)
        return m, in_names, final


def convert(d, out, check=False):
    m, ins, outs = Conv(d).build()
    onnx.save(m, out)
    if check:
        import onnxruntime as ort
        s = ort.InferenceSession(out, providers=["CPUExecutionProvider"])
        feeds = {i.name: np.random.RandomState(0).randn(*i.shape).astype(np.float32) for i in s.get_inputs()}
        r = s.run(None, feeds)
        for o, a in zip(outs, r):
            if not np.all(np.isfinite(a)):
                raise AssertionError(o + " not finite")
    return m


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("mlmodelc")
    ap.add_argument("out")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    convert(a.mlmodelc, a.out, a.check)
    print("ok", a.out)
