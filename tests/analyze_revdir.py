#!/usr/bin/env python3
"""Direction check for REV ECHO (tests/revdir.txt).

revA.wav: two notes dry (REV ECHO 0). revB.wav: the same script with
REV ECHO full. E = B - A isolates the echo (the limiter is near-linear
at these levels). In the gap between the notes (0.50-0.92 s) the echo
replays the first note's tail. Real (reversed) playback is continuous
audio: its 5 ms windows vary and its spectrum carries the note's
fundamental. The v0.4 implementation's read heads stand still relative
to the ring, so its "echo" there is a pair of held sample values -
flat windows, energy near DC. That is what this test gates on.
"""
import sys
import numpy as np
import wave


def load(path):
    w = wave.open(path)
    n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
    x = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float32) / 32768.0
    if ch == 2:
        x = x.reshape(-1, 2).mean(axis=1)
    return x, sr


d = sys.argv[1] if len(sys.argv) > 1 else "."
A, sr = load(f"{d}/revA.wav")
B, _ = load(f"{d}/revB.wav")
n = min(len(A), len(B))
E = B[:n] - A[:n]
seg = E[int(0.50 * sr):int(0.92 * sr)]
rms = float(np.sqrt(np.mean(seg * seg)))
w = 220  # 5 ms
stds = np.array([seg[i:i + w].std() for i in range(0, len(seg) - w, w)])
flat = float(np.mean(stds < 0.02 * rms))
X = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
fr = np.fft.rfftfreq(len(seg), 1.0 / sr)
fund = float(X[(fr > 130) & (fr < 165)].max() / max(X.max(), 1e-9))
print(f"revdir: gap echo RMS {20 * np.log10(max(rms, 1e-9)):.1f} dBFS, "
      f"flat-window fraction {flat:.2f}, fundamental share {fund:.2f}")
ok = flat < 0.2 and fund > 0.3
print("revdir:", "PASS (real reversed audio)" if ok else "FAIL (held-sample grains, not reverse)")
sys.exit(0 if ok else 1)
