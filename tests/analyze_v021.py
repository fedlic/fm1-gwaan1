#!/usr/bin/env python3
"""Offline analysis for GWAAN-1 v0.2.1 knob response (tests/knob.txt).

Short windows only - 3 s before vs 3 s after the jump:
  k2a/k2b.wav: KNOB2 0 -> 127. The body band (150-600 Hz, the built-in
               speaker's fatness) must rise immediately, not one swell
               cycle later.
  k1a/k1b.wav: KNOB1 0 -> 127. The mihyon band (700-2600 Hz) energy's
               frame-to-frame movement (flux) must jump: at K1 = 0 the
               glide is an 18 s / 0.15-octave crawl; after the jump the
               answer sweep plus a 2 s / 1.65-octave cycle move it now.
"""
import sys
import numpy as np
import wave
from scipy import signal

def load(path):
    w = wave.open(path)
    n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
    x = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float32) / 32768.0
    if ch == 2:
        x = x.reshape(-1, 2).mean(axis=1)
    return x, sr

def db(v):
    return 20.0 * np.log10(max(v, 1e-9))

def band_rms(x, sr, lo, hi):
    f, p = signal.welch(x, sr, nperseg=8192)
    m = (f >= lo) & (f <= hi)
    return db(np.sqrt(p[m].sum() * (f[1] - f[0])))

def band_flux(x, sr, lo, hi, frame=0.1):
    """Mean |dB step| of the band energy between 0.1 s frames, and the
    band's level range across the window - how much the harmonics move."""
    n = int(frame * sr)
    vals = []
    for i in range(0, len(x) - n + 1, n):
        seg = x[i:i + n] * np.hanning(n)
        sp = np.abs(np.fft.rfft(seg)) ** 2
        fr = np.fft.rfftfreq(n, 1.0 / sr)
        m = (fr >= lo) & (fr <= hi)
        vals.append(db(np.sqrt(sp[m].sum())))
    vals = np.array(vals)
    return float(np.abs(np.diff(vals)).mean()), float(vals.max() - vals.min()), float(vals.mean())

d = sys.argv[1] if len(sys.argv) > 1 else "."

xa, sr = load(f"{d}/k2a.wav")
xb, _ = load(f"{d}/k2b.wav")
print(f"== KNOB2 jump (3 s windows): peak A {db(np.abs(xa).max()):.1f} / B {db(np.abs(xb).max()):.1f} dBFS")
for name, lo, hi in (("body 150-600 Hz", 150, 600), ("sub 40-120 Hz", 40, 120), ("overall", 20, 16000)):
    a, b = band_rms(xa, sr, lo, hi), band_rms(xb, sr, lo, hi)
    print(f"   {name}: {a:.1f} -> {b:.1f} dBFS  ({b - a:+.1f} dB)")

xa, sr = load(f"{d}/k1a.wav")
xb, _ = load(f"{d}/k1b.wav")
fa = band_flux(xa, sr, 700, 2600)
fb = band_flux(xb, sr, 700, 2600)
print(f"== KNOB1 jump (3 s windows, 700-2600 Hz): peak A {db(np.abs(xa).max()):.1f} / B {db(np.abs(xb).max()):.1f} dBFS")
print(f"   before: flux {fa[0]:.2f} dB/0.1s, range {fa[1]:.1f} dB, mean level {fa[2]:.1f}")
print(f"   after : flux {fb[0]:.2f} dB/0.1s, range {fb[1]:.1f} dB, mean level {fb[2]:.1f}")
print(f"   flux growth: x{fb[0] / max(fa[0], 1e-9):.1f}")
