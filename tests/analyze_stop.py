#!/usr/bin/env python3
"""PLAY stop tail (tests/stoptail.txt): stop.wav starts at the stop.
The wet tail must be drained: RMS at 1.0-1.5 s < -42 dBFS and at
3.0-4.0 s < -52 dBFS. v0.4 measured -26.4 dBFS at 1.0-1.5 s (the echo
and reverb feedback ignored the PLAY gate and rang on)."""
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


def rms_db(seg):
    return 20.0 * np.log10(max(float(np.sqrt((seg ** 2).mean())), 1e-9))


d = sys.argv[1] if len(sys.argv) > 1 else "."
x, sr = load(f"{d}/stop.wav")
a = rms_db(x[int(1.0 * sr):int(1.5 * sr)])
b = rms_db(x[int(3.0 * sr):int(4.0 * sr)])
print(f"stop tail: 1.0-1.5 s {a:.1f} dBFS (want < -42), 3.0-4.0 s {b:.1f} dBFS (want < -52)")
ok = a < -42.0 and b < -52.0
print("analyze_stop:", "PASS" if ok else "FAIL")
sys.exit(0 if ok else 1)
