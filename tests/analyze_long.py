#!/usr/bin/env python3
"""Offline analysis of GWAAN-1 host captures (proto.wav / long.wav).
Checks: peak/RMS, windowed RMS (the GUWAAN swells, long-term stability),
and slow spectral movement in the 700-2600 Hz band (the MIHYON glide)."""
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

def db(v):
    return 20.0 * np.log10(max(v, 1e-9))

for path in sys.argv[1:]:
    x, sr = load(path)
    dur = len(x) / sr
    print(f"== {path}: {dur:.1f} s, peak {db(np.abs(x).max()):.1f} dBFS, RMS {db(np.sqrt((x**2).mean())):.1f} dBFS")
    win = 5 * sr
    rms = [db(np.sqrt((x[i:i+win]**2).mean())) for i in range(0, len(x) - win + 1, win)]
    print("   RMS/5s:", " ".join(f"{v:.0f}" for v in rms))
    # slow spectral movement: band energy 700-2600 Hz in 1 s hops, report its swing
    hop = sr
    spec = []
    for i in range(0, len(x) - hop + 1, hop):
        seg = x[i:i+hop] * np.hanning(hop)
        mag = np.abs(np.fft.rfft(seg))
        freqs = np.fft.rfftfreq(hop, 1.0 / sr)
        band = mag[(freqs >= 700) & (freqs <= 2600)].sum()
        low = mag[(freqs >= 40) & (freqs <= 160)].sum()
        spec.append(band / max(low, 1e-9))
    spec = np.array(spec)
    print(f"   band(700-2600)/low ratio per 1s: min {spec.min():.2f} max {spec.max():.2f} "
          f"(swing x{spec.max()/max(spec.min(),1e-9):.1f})")
