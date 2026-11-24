#!/usr/bin/env python3
"""Offline analysis for GWAAN-1 v0.4.

flfowav.txt: SELECT at -24, LFO depth full, 2 s period. The 3-8 kHz
band's 0.5 s window RMS must swing (max/min > 6 dB) with the LFO on,
and swing less with it off.

revwav.txt: lone key notes, layers muted, SPACE off. The tail in the
0.5-3.5 s window after the last note's release (the window the code
below measures) must be louder with REVERSE ECHO at full than without
it. Playback direction itself is checked by analyze_revdir.py.

widewav.txt: the drone at width 0 / 127. The side RMS ((L-R)/2) must
rise >= 6 dB with width, and the mono sum ((L+R)/2) RMS must stay
within 3 dB (mono compatibility).
"""
import sys
import numpy as np
import wave


def load_stereo(path):
    w = wave.open(path)
    n, ch, sr = w.getnframes(), w.getnchannels(), w.getframerate()
    x = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float32) / 32768.0
    if ch == 2:
        x = x.reshape(-1, 2)
    else:
        x = np.stack([x, x], axis=1)
    return x, sr


def db(v):
    return 20.0 * np.log10(max(v, 1e-9))


def rms_db(seg):
    return db(np.sqrt((seg ** 2).mean()))


d = sys.argv[1] if len(sys.argv) > 1 else "."
fails = 0

# --- the filter LFO ---
for name, want in (("flfoon.wav", True), ("flfooff.wav", False)):
    x, sr = load_stereo(f"{d}/{name}")
    mono = x.mean(axis=1)
    wins = []
    t = 1.0
    while t + 0.5 <= len(mono) / sr:
        seg = mono[int(t * sr):int((t + 0.5) * sr)]
        # a crude 3-8 kHz emphasis: difference of neighbours kills lows
        hf = np.diff(seg)
        wins.append(rms_db(hf))
        t += 0.5
    swing = max(wins) - min(wins)
    print(f"{name}: HF window swing {swing:.1f} dB (max {max(wins):.1f}, min {min(wins):.1f})")
    if want and swing < 6.0:
        print("FAIL: the LFO does not swing the HF band by >= 6 dB")
        fails += 1
    if not want and swing > 9.0:
        print("FAIL: the HF band swings even with the LFO off")
        fails += 1

# --- reverse echo ---
tails = {}
for name in ("revoff.wav", "revon.wav"):
    x, sr = load_stereo(f"{d}/{name}")
    mono = x.mean(axis=1)
    tails[name] = rms_db(mono[int(0.5 * sr):int(3.5 * sr)])
    print(f"{name}: 0.5-3.5 s tail {tails[name]:.1f} dBFS")
if tails["revon.wav"] < tails["revoff.wav"] + 2.0:
    print("FAIL: the reverse echo does not bloom the tail by >= 2 dB")
    fails += 1

# --- stereo wide ---
x, sr = load_stereo(f"{d}/wall2.wav")
res = {}
for name, (t0, t1) in (("off", (3.0, 9.0)), ("on", (13.0, 19.0))):
    seg = x[int(t0 * sr):int(t1 * sr)]
    side = (seg[:, 0] - seg[:, 1]) / 2.0
    mid = (seg[:, 0] + seg[:, 1]) / 2.0
    res[name] = (rms_db(side), rms_db(mid) - rms_db(seg[:, 0]))
    print(f"width {name}: side {res[name][0]:.1f} dB, mid-L {res[name][1]:.1f} dB")
if res["on"][0] < res["off"][0] + 6.0:
    print("FAIL: the side signal does not rise by >= 6 dB at full width")
    fails += 1
if res["on"][1] < res["off"][1] - 4.5:
    print("FAIL: the mono sum collapses at full width (mono compatibility)")
    fails += 1

print("analyze_v04:", "FAIL" if fails else "PASS")
sys.exit(1 if fails else 0)
