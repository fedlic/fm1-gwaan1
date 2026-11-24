#!/usr/bin/env python3
"""Keyboard-voice regression analysis (tests/keyrel.txt).

pitch.wav: keys 0..5, 1.4 s apart (0.9 s held + 0.5 s gap). Each held
window's spectral peak must sit on the DEEP scale pitch for that key.
midipitch.wav: MIDI note 50 held; its peak must match key 0's pitch
(the v0.4 snap_midi rounding bug put it a semitone high, at C#3).
keyrel.wav: key 0 held, released 0.5 s into the capture. The melt is
measured against the held level: the -20 dB crossing must land
2.3-3.4 s after the release (the ~6 s melt: tau ~1.19 s), and the
voice must be off (< -55 dBFS) in the window ending 6.5 s after the
release. v0.4's 0.000038 coefficient crossed at ~1.5 s, off ~2.3 s.
"""
import sys
import numpy as np
import wave

EXPECT = [146.83, 174.61, 196.00, 220.00, 261.63, 293.66]  # D3 F3 G3 A3 C4 D4


def load(path):
    w = wave.open(path)
    n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
    x = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float32) / 32768.0
    if ch == 2:
        x = x.reshape(-1, 2).mean(axis=1)
    return x, sr


def peak_freq(seg, sr):
    X = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    fr = np.fft.rfftfreq(len(seg), 1.0 / sr)
    m = (fr > 80) & (fr < 500)
    return float(fr[m][np.argmax(X[m])])


def rms_db(seg):
    return 20.0 * np.log10(max(float(np.sqrt((seg ** 2).mean())), 1e-9))


d = sys.argv[1] if len(sys.argv) > 1 else "."
fails = 0

x, sr = load(f"{d}/pitch.wav")
peaks = []
for i in range(6):
    t0 = i * 1.4 + 0.25
    f = peak_freq(x[int(t0 * sr):int((t0 + 0.6) * sr)], sr)
    peaks.append(f)
    ok = abs(f - EXPECT[i]) / EXPECT[i] < 0.02
    print(f"key {i}: peak {f:.1f} Hz, expected {EXPECT[i]:.2f} ({'ok' if ok else 'FAIL'})")
    fails += 0 if ok else 1

y, _ = load(f"{d}/midipitch.wav")
fm = peak_freq(y[int(0.15 * sr):int(0.85 * sr)], sr)
ok = abs(fm - peaks[0]) / peaks[0] < 0.015
print(f"MIDI note 50: peak {fm:.1f} Hz vs panel key 0 {peaks[0]:.1f} Hz ({'ok' if ok else 'FAIL'})")
fails += 0 if ok else 1

r, _ = load(f"{d}/keyrel.wav")
held = rms_db(r[int(0.05 * sr):int(0.45 * sr)])
target = held - 20.0
cross = None
t = 0.5
while t + 0.1 <= len(r) / sr:
    if rms_db(r[int(t * sr):int((t + 0.1) * sr)]) < target:
        cross = t - 0.5
        break
    t += 0.05
print(f"release: held {held:.1f} dBFS, -20 dB crossing at "
      f"{cross if cross is None else round(cross, 2)} s after release (want 2.3-3.4)")
if cross is None or not (2.3 <= cross <= 3.4):
    print("FAIL: the release melt is not the ~6 s one")
    fails += 1
off = rms_db(r[int(6.9 * sr):int(7.4 * sr)])
print(f"release: level at 6.4-6.9 s after release {off:.1f} dBFS (want < -55)")
if off >= -55.0:
    print("FAIL: the voice is still audible at ~6.5 s")
    fails += 1

print("analyze_key:", "FAIL" if fails else "PASS")
sys.exit(1 if fails else 0)
