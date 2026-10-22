"""Writes docs/showcase/loop.wav: four bars at 100 bpm of a synthetic drum loop (kick, snare, hats), mono, 24 kHz, 16 bit. Python standard library only."""
import math, random, struct, wave, pathlib

RATE, BPM, BARS = 24000, 100, 4
beat = 60.0 / BPM
n = int(RATE * beat * 4 * BARS)
buf = [0.0] * n
rng = random.Random(7)

def add(at_beats, kind):
    start = int(at_beats * beat * RATE)
    length = int(RATE * (0.25 if kind != "hat" else 0.05))
    for i in range(length):
        if start + i >= n: break
        t = i / RATE
        if kind == "kick":
            v = math.sin(2 * math.pi * (50 + 90 * math.exp(-t * 30)) * t) * math.exp(-t * 11)
        elif kind == "snare":
            v = (rng.uniform(-1, 1) * 0.7 + math.sin(2 * math.pi * 190 * t) * 0.4) * math.exp(-t * 18)
        else:
            v = rng.uniform(-1, 1) * 0.35 * math.exp(-t * 90)
        buf[start + i] += v * 0.8

for bar in range(BARS):
    b = bar * 4
    for k in (0, 1.5, 2.5): add(b + k, "kick")
    for s in (1, 3): add(b + s, "snare")
    for h in range(8): add(b + h * 0.5, "hat")

peak = max(abs(x) for x in buf)
path = pathlib.Path("docs/showcase/loop.wav")
with wave.open(str(path), "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
    w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, x / peak * 0.9)) * 32767)) for x in buf))
print(path, path.stat().st_size)
