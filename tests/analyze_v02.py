#!/usr/bin/env python3
"""Offline analysis for GWAAN-1 v0.2.

proto2.wav (the listening prototype): peak / RMS and 5 s window RMS.
Ping timestamps come from tests/probe.txt instead of audio forensics:
the host prints the app's own ping counter once per simulated second,
so increments in that log are the exact pattern/ping times.

fat.wav (tests/fat.txt): one 80 s DEEP capture, SIGNAL off - lean
(KNOB2 = 0) for the first ~40 s, fat (KNOB2 = 127) for the last ~40 s.
Each half spans at least one full swell period, so comparing the
150-600 Hz body band between the halves is not swell-phase biased.
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

def band_rms(x, sr, lo, hi, t0, t1):
    seg = x[int(t0 * sr):int(t1 * sr)]
    f, p = signal.welch(seg, sr, nperseg=8192)
    m = (f >= lo) & (f <= hi)
    return db(np.sqrt(p[m].sum() * (f[1] - f[0])))

def seg_rms(x, sr, t0, t1):
    seg = x[int(t0 * sr):int(t1 * sr)]
    return db(np.sqrt((seg ** 2).mean()))

proto = sys.argv[1] if len(sys.argv) > 1 else "proto2.wav"
fat = sys.argv[2] if len(sys.argv) > 2 else "fat.wav"

x, sr = load(proto)
print(f"== {proto}: {len(x)/sr:.1f} s, peak {db(np.abs(x).max()):.1f} dBFS, "
      f"RMS {db(np.sqrt((x**2).mean())):.1f} dBFS")
win = 5 * sr
rms = [db(np.sqrt((x[i:i+win]**2).mean())) for i in range(0, len(x) - win + 1, win)]
print("   RMS/5s:", " ".join(f"{v:.0f}" for v in rms))

x, sr = load(fat)
print(f"== {fat}: {len(x)/sr:.1f} s (lean 2-38 s vs fat 47-83 s)")
for name, t0, t1 in (("lean K2=0  ", 2, 38), ("fat  K2=127", 47, 83)):
    print(f"   {name}: overall {seg_rms(x, sr, t0, t1):.1f} dBFS, "
          f"body 150-600 Hz {band_rms(x, sr, 150, 600, t0, t1):.1f} dBFS, "
          f"sub 40-120 Hz {band_rms(x, sr, 40, 120, t0, t1):.1f} dBFS")
lean = band_rms(x, sr, 150, 600, 2, 38)
fatb = band_rms(x, sr, 150, 600, 47, 83)
print(f"   fatness growth in the body band: {fatb - lean:+.1f} dB")
