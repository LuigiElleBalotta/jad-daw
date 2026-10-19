#!/usr/bin/env python3
"""Convert a compiled Core ML program (.mlmodelc: *.mil text + weights/weight.bin) to ONNX.

Only the operator set the Logic Pro 11.2 models use is supported (conv, add, mul, real_div, sigmoid,
abs, relu, elu, softmax, concat, slice_by_size, expand_dims, reshape, transpose, pad, max_pool,
batch_norm, matmul, cast, const). Anything else raises, so a conversion is never silently wrong.

    python mil2onnx.py <model.mlmodelc> <out.onnx> [--mil <file.mil>] [--check]

--check runs onnxruntime on zeros and compares every output shape with the shape the MIL declares.
"""
import argparse
import os
import re
import struct
import sys

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper

DT = {"fp32": (np.float32, TensorProto.FLOAT), "fp16": (np.float16, TensorProto.FLOAT16),
      "int32": (np.int32, TensorProto.INT32), "bool": (np.bool_, TensorProto.BOOL)}


def split_top(s, sep=","):
    """Split s at top-level separators (ignoring those inside (), [], <>, quotes)."""
    out, depth, cur, quote = [], 0, [], False
    for i, ch in enumerate(s):
        if ch == '"':
            quote = not quote
        if not quote:
            if ch in "([<":
                depth += 1
            elif ch in ")]>":
                depth -= 1
        if ch == sep and depth == 0 and not quote:
            out.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    if "".join(cur).strip():
        out.append("".join(cur).strip())
    return out


def parse_type(t):
    """'tensor<fp32, [1, 2]>' -> ('fp32', [1, 2]); 'tensor<string, []>' -> ('string', [])."""
    m = re.match(r"tensor<\s*(\w+)\s*,\s*\[([^\]]*)\]\s*>$", t.strip())
    if not m:
        raise ValueError("type " + t)
    dims = [int(x) for x in m.group(2).split(",") if x.strip()]
    return m.group(1), dims


def read_blob(weights, offset, dtype, shape):
    magic, = struct.unpack_from("<I", weights, offset)
    if magic != 0xDEADBEEF:
        raise ValueError("bad blob magic at %d" % offset)
    size, = struct.unpack_from("<Q", weights, offset + 8)
    data_off, = struct.unpack_from("<Q", weights, offset + 16)
    npdt = DT[dtype][0]
    arr = np.frombuffer(weights, dtype=npdt, count=int(np.prod(shape)) if shape else 1, offset=data_off)
    if arr.nbytes != size:
        raise ValueError("blob size %d != %d" % (arr.nbytes, size))
    return arr.reshape(shape).copy()


def num(v, is_float):
    v = v.strip()
    if not is_float:
        return int(v)
    return float.fromhex(v) if "0x" in v.lower() else float(v)


def parse_literal(txt, dtype, shape):
    txt = txt.strip()
    if dtype == "string":
        return txt.strip('"')
    flat = txt.replace("[", "").replace("]", "")
    toks = [v for v in flat.split(",") if v.strip()]
    if dtype == "bool":
        vals = [v.strip() == "true" for v in toks]
    else:
        vals = [num(v, dtype.startswith("fp")) for v in toks]
    arr = np.array(vals, dtype=DT[dtype][0])
    return arr.reshape(shape) if shape else arr.reshape(())


class Converter:
    def __init__(self, mil_text, weights):
        self.weights = weights
        self.consts = {}      # name -> numpy value or string
        self.shapes = {}      # tensor name -> (dtype, shape)
        self.nodes = []
        self.inits = {}       # name -> numpy array (consts promoted to initializers on demand)
        self.inputs = []
        self.outputs = []
        self.cnt = 0
        self.parse(mil_text)

    # ---- parsing -----------------------------------------------------------------------------
    def parse(self, text):
        m = re.search(r"func\s+main<[^>]*>\((.*?)\)(?:\[[^\n]*\])?\s*\{[ \t]*\n", text, re.S)
        for part in split_top(m.group(1)):
            ty, name = part.rsplit(" ", 1)
            dt, dims = parse_type(ty)
            self.inputs.append(helper.make_tensor_value_info(name, DT[dt][1], dims))
            self.shapes[name] = (dt, dims)
        tail = re.search(r"\}\s*->\s*\((.*?)\)\s*;", text, re.S)
        self.out_names = [x.strip() for x in tail.group(1).split(",")]
        body = text[m.end():tail.start()]
        for line in body.split("\n"):
            line = line.strip().rstrip(";")
            if line:
                self.statement(line)

    def statement(self, line):
        m = re.match(r"(tensor<[^>]*>)\s+(\w+)\s*=\s*(\w+)\(", line)
        if not m:
            raise ValueError("cannot parse: " + line[:200])
        ty, name, op = m.group(1), m.group(2), m.group(3)
        depth, i = 1, m.end()
        quote = False
        while depth:
            ch = line[i]
            if ch == '"':
                quote = not quote
            elif not quote:
                depth += ch == "("
                depth -= ch == ")"
            i += 1
        args = line[m.end():i - 1]
        attrs = line[i:].strip()
        dt, dims = parse_type(ty)
        self.shapes[name] = (dt, dims)
        kw = {}
        for a in split_top(args):
            k, v = a.split("=", 1)
            kw[k.strip()] = v.strip()
        if op == "const":
            for a in split_top(attrs.strip("[]")):
                k, v = a.split("=", 1)
                kw[k.strip()] = v.strip()
            self.consts[name] = self.const_value(kw["val"], dt, dims)
        else:
            self.op(name, op, kw, dt, dims)

    def const_value(self, val, dt, dims):
        m = re.match(r"tensor<[^>]*>\((.*)\)$", val, re.S)
        inner = m.group(1).strip()
        b = re.search(r"BLOBFILE\(.*offset\s*=\s*tensor<uint64, \[\]>\((\d+)\)\)", inner)
        if b:
            return read_blob(self.weights, int(b.group(1)), dt, dims)
        return parse_literal(inner, dt, dims)

    # ---- helpers -----------------------------------------------------------------------------
    def val(self, ref):
        """Resolve an argument to a python/numpy constant (for attributes)."""
        ref = ref.strip()
        if ref in self.consts:
            return self.consts[ref]
        m = re.match(r"tensor<([^,]*),\s*\[([^\]]*)\]>\((.*)\)$", ref, re.S)
        if m:
            return parse_literal(m.group(3), m.group(1), [int(x) for x in m.group(2).split(",") if x.strip()])
        if ref in ("true", "false"):
            return ref == "true"
        raise KeyError(ref)

    def tensor(self, ref):
        """Resolve an argument to an ONNX value name, promoting consts to initializers."""
        ref = ref.strip()
        if ref in self.consts:
            v = self.consts[ref]
            if ref not in self.inits:
                self.inits[ref] = np.asarray(v)
            return ref
        if ref in self.shapes:
            return ref
        raise KeyError(ref)

    def emit(self, op, ins, outs, **attrs):
        self.cnt += 1
        self.nodes.append(helper.make_node(op, ins, outs, name="n%d" % self.cnt, **attrs))

    def const_in(self, arr, hint):
        name = "%s_c%d" % (hint, self.cnt)
        self.cnt += 1
        self.inits[name] = np.asarray(arr)
        return name

    # ---- operators ---------------------------------------------------------------------------
    def op(self, name, op, kw, dt, dims):
        t = self.tensor
        if op == "conv":
            x = t(kw["x"])
            w = self.val(kw["weight"])
            ins = [x, t(kw["weight"])]
            if "bias" in kw:
                ins.append(t(kw["bias"]))
            attrs = dict(strides=[int(i) for i in self.val(kw["strides"])],
                         dilations=[int(i) for i in self.val(kw["dilations"])],
                         group=int(self.val(kw["groups"])),
                         kernel_shape=list(w.shape[2:]))
            pt = self.val(kw["pad_type"])
            if pt == "custom":
                p = [int(i) for i in self.val(kw["pad"])]       # MIL: top, bottom, left, right
                attrs["pads"] = p[0::2] + p[1::2]
            elif pt == "same":
                attrs["auto_pad"] = "SAME_UPPER"
            self.emit("Conv", ins, [name], **attrs)
        elif op in ("add", "mul", "real_div", "sub"):
            self.emit({"add": "Add", "mul": "Mul", "real_div": "Div", "sub": "Sub"}[op], [t(kw["x"]), t(kw["y"])], [name])
        elif op in ("sigmoid", "abs", "relu"):
            self.emit({"sigmoid": "Sigmoid", "abs": "Abs", "relu": "Relu"}[op], [t(kw["x"])], [name])
        elif op == "elu":
            self.emit("Elu", [t(kw["x"])], [name], alpha=float(self.val(kw["alpha"])))
        elif op == "softmax":
            self.emit("Softmax", [t(kw["x"])], [name], axis=int(self.val(kw["axis"])))
        elif op == "concat":
            vals = [v.strip() for v in kw["values"].strip("()").split(",")]
            self.emit("Concat", [t(v) for v in vals], [name], axis=int(self.val(kw["axis"])))
        elif op == "slice_by_size":
            x = kw["x"]
            xshape = self.shapes[x][1]
            begin = [int(i) for i in self.val(kw["begin"])]
            size = [int(i) for i in self.val(kw["size"])]
            starts, ends = [], []
            for d, (b, s) in enumerate(zip(begin, size)):
                b = b + xshape[d] if b < 0 else b
                starts.append(b)
                ends.append(xshape[d] if s < 0 else b + s)
            self.emit("Slice", [t(x), self.const_in(np.array(starts, np.int64), "st"),
                                self.const_in(np.array(ends, np.int64), "en"),
                                self.const_in(np.arange(len(starts), dtype=np.int64), "ax")], [name])
        elif op == "expand_dims":
            axes = np.array([int(i) for i in np.atleast_1d(self.val(kw["axes"]))], np.int64)
            self.emit("Unsqueeze", [t(kw["x"]), self.const_in(axes, "ax")], [name])
        elif op == "reshape":
            shp = np.array([int(i) for i in self.val(kw["shape"])], np.int64)
            self.emit("Reshape", [t(kw["x"]), self.const_in(shp, "sh")], [name])
        elif op == "transpose":
            self.emit("Transpose", [t(kw["x"])], [name], perm=[int(i) for i in self.val(kw["perm"])])
        elif op == "pad":
            p = [int(i) for i in self.val(kw["pad"])]
            rank = len(dims)
            full = [0] * (2 * rank)
            first = rank - len(p) // 2
            for i in range(len(p) // 2):
                full[first + i] = p[2 * i]
                full[rank + first + i] = p[2 * i + 1]
            mode = self.val(kw["mode"]) if "mode" in kw else "constant"
            ins = [t(kw["x"]), self.const_in(np.array(full, np.int64), "pd")]
            if mode == "constant":
                cv = float(self.val(kw["constant_val"])) if "constant_val" in kw else 0.0
                ins.append(self.const_in(np.array(cv, DT[self.shapes[kw["x"]][0]][0]), "cv"))
            self.emit("Pad", ins, [name], mode={"constant": "constant", "reflect": "reflect", "replicate": "edge"}[mode])
        elif op == "max_pool":
            attrs = dict(kernel_shape=[int(i) for i in self.val(kw["kernel_sizes"])],
                         strides=[int(i) for i in self.val(kw["strides"])],
                         ceil_mode=int(bool(self.val(kw["ceil_mode"]))) if "ceil_mode" in kw else 0)
            pt = self.val(kw["pad_type"])
            if pt == "custom":
                p = [int(i) for i in self.val(kw["pad"])]
                attrs["pads"] = p[0::2] + p[1::2]
            elif pt == "same":
                attrs["auto_pad"] = "SAME_UPPER"
            self.emit("MaxPool", [t(kw["x"])], [name], **attrs)
        elif op == "batch_norm":
            self.emit("BatchNormalization", [t(kw["x"]), t(kw["gamma"]), t(kw["beta"]), t(kw["mean"]), t(kw["variance"])],
                      [name], epsilon=float(self.val(kw["epsilon"])))
        elif op == "matmul":
            x, y = t(kw["x"]), t(kw["y"])
            if self.val(kw["transpose_x"]):
                tx = name + "_tx"
                self.emit("Transpose", [x], [tx], perm=self.swap_last(kw["x"]))
                x = tx
            if self.val(kw["transpose_y"]):
                ty = name + "_ty"
                self.emit("Transpose", [y], [ty], perm=self.swap_last(kw["y"]))
                y = ty
            self.emit("MatMul", [x, y], [name])
        elif op == "cast":
            to = self.val(kw["dtype"])
            self.emit("Cast", [t(kw["x"])], [name], to={"fp32": TensorProto.FLOAT, "fp16": TensorProto.FLOAT16,
                                                         "int32": TensorProto.INT32, "bool": TensorProto.BOOL}[to])
        else:
            raise NotImplementedError("MIL op '%s' (%s)" % (op, name))

    def swap_last(self, ref):
        rank = len(self.shapes[ref.strip()][1]) if ref.strip() in self.shapes else self.consts[ref.strip()].ndim
        perm = list(range(rank))
        perm[-1], perm[-2] = perm[-2], perm[-1]
        return perm

    # ---- model -------------------------------------------------------------------------------
    def build(self, opset=17):
        outs = []
        for n in self.out_names:
            dt, dims = self.shapes[n]
            outs.append(helper.make_tensor_value_info(n, DT[dt][1], dims))
        inits = [numpy_helper.from_array(v, k) for k, v in self.inits.items()]
        g = helper.make_graph(self.nodes, "mil", self.inputs, outs, inits)
        m = helper.make_model(g, opset_imports=[helper.make_opsetid("", opset)])
        m.ir_version = 8
        onnx.checker.check_model(m)
        return m


def pick_mil(path, explicit):
    if explicit:
        return explicit
    for f in sorted(os.listdir(path)):
        if f.endswith(".mil"):
            return os.path.join(path, f)
    raise FileNotFoundError("no .mil in " + path)


def convert(mlmodelc, out, mil=None, check=False):
    mil = pick_mil(mlmodelc, mil)
    text = open(mil, encoding="utf8").read()
    weights = open(os.path.join(mlmodelc, "weights", "weight.bin"), "rb").read()
    c = Converter(text, weights)
    model = c.build()
    onnx.save(model, out)
    if check:
        import onnxruntime as ort
        sess = ort.InferenceSession(out, providers=["CPUExecutionProvider"])
        feeds = {i.name: np.zeros([d for d in i.shape], np.float16 if "float16" in i.type else np.float32)
                 for i in sess.get_inputs()}
        res = sess.run(None, feeds)
        for n, r in zip(c.out_names, res):
            want = c.shapes[n][1]
            if list(r.shape) != want:
                raise AssertionError("output %s shape %s != declared %s" % (n, list(r.shape), want))
            if not np.all(np.isfinite(r)):
                raise AssertionError("output %s not finite on zero input" % n)
    return model


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("mlmodelc")
    ap.add_argument("out")
    ap.add_argument("--mil")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    convert(a.mlmodelc, a.out, a.mil, a.check)
    print("ok", a.out)
