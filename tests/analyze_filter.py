#!/usr/bin/env python3
"""Offline analysis for GWAAN-1 v0.2.1 SELECT (tests/filter.txt).

Ratio spectra against the centre capture cancel the (slowly drifting)
source, so what remains is the filter's own shape:
  LP mid (sel -24, fc ~1.45 kHz): a resonance peak near fc - the max of
    the ratio inside 0.9-2.2 kHz, and its prominence over the ratio's
    flanking bands (0.35-0.7 kHz and 2.6-4 kHz).
  LP end (sel -48, fc ~120 Hz): mean ratio in 3-8 kHz (rejection).
  HP end (sel +48, fc ~5 kHz): mean ratio in 60-250 Hz (rejection),
    and the ratio's max in 3.5-9 kHz (the highpass resonance region).
Every capture's peak must stay below full scale (no clipping).
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
    return x[int(0.5 * sr):], sr   # skip the sweep's settling head

def db(v):
    return 20.0 * np.log10(max(v, 1e-9))

def psd(x, sr):
    f, p = signal.welch(x, sr, nperseg=8192)
    return f, p

def band(f, v, lo, hi, fn=np.mean):
    m = (f >= lo) & (f <= hi)
    return float(fn(v[m]))

d = sys.argv[1] if len(sys.argv) > 1 else "."
names = ("fcent", "flpmid", "flpend", "fhpend")
xs = {}
for nm in names:
    xs[nm], sr = load(f"{d}/{nm}.wav")
    raw = wave.open(f"{d}/{nm}.wav")
    n = raw.getnframes()
    peak = db(np.abs(np.frombuffer(raw.readframes(n), dtype=np.int16)).max() / 32768.0)
    print(f"== {nm}: peak {peak:.1f} dBFS")
fc_, pc = psd(xs["fcent"], sr)

def ratio(nm):
    f, p = psd(xs[nm], sr)
    return f, 10.0 * np.log10(np.maximum(p, 1e-12) / np.maximum(pc, 1e-12))

f, r = ratio("flpmid")
pk_i = np.argmax(np.where((f >= 900) & (f <= 2200), r, -1e9))
flank = np.median(np.concatenate([r[(f >= 350) & (f <= 700)], r[(f >= 2600) & (f <= 4000)]]))
print(f"LP mid : resonance peak {r[pk_i]:+.1f} dB at {f[pk_i]:.0f} Hz (expect near ~1450 Hz), "
      f"prominence over flanks {r[pk_i] - flank:+.1f} dB")
print(f"         ratio at 6 kHz: {band(f, r, 5500, 6500):+.1f} dB (slope above cutoff)")
f, r = ratio("flpend")
print(f"LP end : mean ratio 3-8 kHz {band(f, r, 3000, 8000):+.1f} dB (rejection), "
      f"100-300 Hz {band(f, r, 100, 300):+.1f} dB (passband kept)")
f, r = ratio("fhpend")
pk_i = np.argmax(np.where((f >= 3500) & (f <= 9000), r, -1e9))
print(f"HP end : mean ratio 60-250 Hz {band(f, r, 60, 250):+.1f} dB (rejection), "
      f"max ratio 3.5-9 kHz {r[pk_i]:+.1f} dB at {f[pk_i]:.0f} Hz (resonance region)")
