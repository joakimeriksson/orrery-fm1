#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A spectrogram of a 16-bit stereo raw or WAV, 30 Hz .. 8 kHz on a log axis, as a PNG: how this project listens.
  uv run --with numpy --with pillow tools/spectrogram.py IN.raw|IN.wav OUT.png "a title" [HOP_MS]"""
import sys, struct, numpy as np
from PIL import Image, ImageDraw
SR = 44100
src, dst, title = sys.argv[1], sys.argv[2], sys.argv[3]
raw = open(src, "rb").read()
if src.endswith(".wav"):
    import wave
    w = wave.open(src)
    raw = w.readframes(w.getnframes())
d = np.frombuffer(raw, dtype="<i2").astype(np.float64)
x = d[0::2]                                      # the left channel
N, hop = 4096, int(44.1 * float(sys.argv[4])) if len(sys.argv) > 4 else 441   # 93 ms windows, 10 ms hops (or HOP_MS)
win = np.hanning(N)
frames = (len(x) - N) // hop
S = np.empty((frames, N // 2 + 1))
for i in range(frames):
    S[i] = np.abs(np.fft.rfft(x[i * hop:i * hop + N] * win))
S = 20 * np.log10(S / (32768 * N / 4) + 1e-9)
# a log-frequency axis 30 Hz .. 8 kHz, 360 rows; -84 .. -6 dB to grey, then a palette
rows = 360
fr = np.geomspace(30, 8000, rows)
bins = (fr / (SR / N)).astype(int)
img = S[:, bins].T[::-1]                         # rows top = high
img = np.clip((img + 84) / 78, 0, 1)
pal = (np.array([[0, 0, 20], [40, 0, 90], [160, 20, 100], [240, 110, 40], [255, 230, 120], [255, 255, 255]]) / 255.0)
idx = img * (len(pal) - 1)
lo = np.floor(idx).astype(int); hi = np.minimum(lo + 1, len(pal) - 1); f = (idx - lo)[..., None]
rgb = (pal[lo] * (1 - f) + pal[hi] * f) * 255
im = Image.fromarray(rgb.astype(np.uint8)).resize((min(1600, frames), rows), Image.BILINEAR)
W, H = im.size
out = Image.new("RGB", (W + 60, H + 40), (20, 20, 24)); out.paste(im, (60, 20))
dr = ImageDraw.Draw(out)
for hz in (50, 100, 200, 500, 1000, 2000, 4000, 8000):
    y = 20 + int((1 - np.log(hz / 30) / np.log(8000 / 30)) * (H - 1))
    dr.text((2, y - 5), f"{hz}", fill=(200, 200, 200)); dr.line([(56, y), (60, y)], fill=(200, 200, 200))
secs = len(x) / SR
for s in range(int(secs) + 1):
    xx = 60 + int(s / secs * (W - 1)); dr.line([(xx, H + 20), (xx, H + 24)], fill=(200, 200, 200)); dr.text((xx - 3, H + 26), f"{s}", fill=(200, 200, 200))
dr.text((64, 4), title, fill=(255, 255, 255))
out.save(dst)
print(dst, "dB range", round(S.max(), 1))
