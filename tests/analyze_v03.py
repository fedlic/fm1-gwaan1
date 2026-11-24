#!/usr/bin/env python3
"""Offline analysis for GWAAN-1 v0.3 (live edition).

mixwav.txt captures: base.wav (all layers), noguw.wav (GUWAAN muted),
muteall.wav (all four layers muted). Checks:
  - the 150-600 Hz body band (the GUWAAN layer's home) drops >= 6 dB
    when GUWAAN is muted, and
  - the all-muted capture is near silence (RMS < -50 dBFS).

freeze.txt captures: frz.wav (frozen, all layers muted - the captured
wash alone) and rel.wav (freeze released, still all muted). Checks:
  - the frozen wash sustains: RMS > -45 dBFS and the last 2 s are
    within 6 dB of the first 2 s (a loop, not a decay), and
  - after release the output falls silent (RMS < -50 dBFS).

crush check on proto3.wav is qualitative (unique sample values in the
crushed section collapse); shimmer is checked by ear on proto3.wav.
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


d = sys.argv[1] if len(sys.argv) > 1 else "."
fails = 0

base, sr = load(f"{d}/base.wav")
noguw, _ = load(f"{d}/noguw.wav")
muteall, _ = load(f"{d}/muteall.wav")
b0 = band_rms(base, sr, 150, 600, 2.0, 6.0)
b1 = band_rms(noguw, sr, 150, 600, 2.0, 6.0)
print(f"body band 150-600 Hz: base {b0:.1f} dB, GUWAAN muted {b1:.1f} dB (drop {b0-b1:.1f} dB)")
if b0 - b1 < 6.0:
    print("FAIL: GUWAAN mute did not drop the body band by >= 6 dB")
    fails += 1
ma = seg_rms(muteall, sr, 1.0, 4.0)
print(f"all-muted RMS: {ma:.1f} dBFS")
if ma > -50.0:
    print("FAIL: all-muted capture is not near silence")
    fails += 1

frz, sr = load(f"{d}/frz.wav")
rel, _ = load(f"{d}/rel.wav")
f0 = seg_rms(frz, sr, 0.2, 2.2)
f1 = seg_rms(frz, sr, 3.0, 5.0)
print(f"frozen wash RMS: first {f0:.1f} dB, last {f1:.1f} dB")
if f0 < -45.0 or abs(f1 - f0) > 6.0:
    print("FAIL: the frozen wash does not sustain")
    fails += 1
r0 = seg_rms(rel, sr, 0.5, 2.5)
print(f"after release RMS: {r0:.1f} dBFS")
if r0 > -50.0:
    print("FAIL: output did not fall silent after freeze release")
    fails += 1

print("analyze_v03:", "FAIL" if fails else "PASS")
sys.exit(1 if fails else 0)
