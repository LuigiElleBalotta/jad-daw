#!/usr/bin/env python3
"""Does OnsetDetectionCNN (converted to ONNX) find the onsets of a real recording with Schluter-Bock style features?
usage: try_onset.py <audio> <OnsetDetectionCNN.espresso.onnx> [seconds]"""
import sys

import librosa
import numpy as np
import onnxruntime as ort

path, model = sys.argv[1], sys.argv[2]
seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 30.0
sr = 44100
y, _ = librosa.load(path, sr=sr, mono=True, duration=seconds)
hop = 441  # 100 frames per second
specs = []
for n in (1024, 2048, 4096):
    S = np.abs(librosa.stft(y, n_fft=n, hop_length=hop, window="hann", center=True)).astype(np.float32)
    mel = librosa.filters.mel(sr=sr, n_fft=n, n_mels=80, fmin=27.5, fmax=16000, norm=None)
    # unit-area triangles (madmom norm_filters)
    mel = mel / np.maximum(mel.sum(axis=1, keepdims=True), 1e-9)
    specs.append(np.log10(mel @ S + 1.0))
X = np.stack(specs)  # 3 x 80 x T
T = X.shape[2]
pad = np.pad(X, ((0, 0), (0, 0), (7, 7)), mode="edge")
sess = ort.InferenceSession(model, providers=["CPUExecutionProvider"])
name = sess.get_inputs()[0].name
act = np.zeros(T, np.float32)
for t in range(T):
    act[t] = sess.run(None, {name: pad[None, :, :, t:t + 15].astype(np.float32)})[0].ravel()[0]
# compare with a classical onset strength
env = librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop)[:T]
n = min(len(env), T)
corr = np.corrcoef(act[:n], env[:n])[0, 1]
peaks = librosa.util.peak_pick(act, pre_max=3, post_max=3, pre_avg=10, post_avg=10, delta=0.2, wait=10)
print("frames", T, "activation min %.3f max %.3f mean %.3f" % (act.min(), act.max(), act.mean()))
print("correlation with spectral flux %.3f" % corr)
print("peaks", len(peaks), "first", (peaks[:12] / 100.0).round(2))
iois = np.diff(peaks) / 100.0
if len(iois):
    print("median inter-onset %.3f s -> %.1f bpm if a beat" % (np.median(iois), 60.0 / np.median(iois)))
