#!/usr/bin/env python3
"""Which log-mel features make DownbeatTrackerCondMPS (ONNX) output sensible beats? Tries a few hypotheses on a real recording.
usage: try_downbeat.py <audio> <DownbeatTrackerCondMPS.model.onnx> [start seconds]"""
import sys

import librosa
import numpy as np
import onnxruntime as ort

path, model = sys.argv[1], sys.argv[2]
start = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
sess = ort.InferenceSession(model, providers=["CPUExecutionProvider"])


def run(mel, fps, label):
    T = 4096
    if mel.shape[1] < T:
        mel = np.pad(mel, ((0, 0), (0, T - mel.shape[1])))
    x = mel[:, :T].T[None].astype(np.float32)
    out = dict(zip([o.name for o in sess.get_outputs()], sess.run(None, {"input_melspec": x})))
    beats = out["output_beats"][0, :, 0]
    tempo = out["output_tempo"][0]
    bpb = out["output_bpb"][0]
    peaks = librosa.util.peak_pick(beats, pre_max=3, post_max=3, pre_avg=5, post_avg=5, delta=0.1, wait=int(fps * 0.25))
    iois = np.diff(peaks) / fps
    bpm = 60.0 / np.median(iois) if len(iois) else 0.0
    print("%-34s beats max %.2f mean %.2f peaks %3d median ioi -> %6.1f bpm | tempo bin %3d | bpb argmax %d" % (
        label, beats.max(), beats.mean(), len(peaks), bpm, tempo.mean(axis=0).argmax(), bpb.mean(axis=0).argmax()))


for sr, n_fft, fps, fmin, fmax, mode in [
    (44100, 2048, 100, 30, 17000, "log1p"), (44100, 2048, 100, 30, 17000, "db"), (22050, 2048, 50, 30, 11000, "log1p"),
    (44100, 4096, 50, 30, 17000, "log1p"), (44100, 2048, 50, 30, 17000, "db"), (16000, 1024, 100, 30, 8000, "log1p"),
    (44100, 1024, 100, 30, 17000, "log1p"),
]:
    y, _ = librosa.load(path, sr=sr, mono=True, offset=start, duration=4096 / fps + 1)
    S = np.abs(librosa.stft(y, n_fft=n_fft, hop_length=int(sr / fps)))
    mel = librosa.filters.mel(sr=sr, n_fft=n_fft, n_mels=131, fmin=fmin, fmax=fmax) @ S
    mel = np.log1p(mel) if mode == "log1p" else librosa.power_to_db(mel ** 2, top_db=80.0) / 80.0 + 1.0
    run(mel, fps, "sr%d fft%d fps%d %s" % (sr, n_fft, fps, mode))
