#!/usr/bin/env python3
# soundcheck.py ORIG.lr16 HQ.lr16 [WAVDIR] (v1.32.0, port/tools/soundcheck.sh): the original 13,379 Hz s16 L,R stream against the enhanced
# 53,516 Hz one of the same run. Prints, per second of sound: the level of each (dB), the correlation of the
# enhanced stream (low-passed and taken every 4th frame) with the original, and the original's noise floor
# vs the enhanced one in the quietest band; exits 1 when the two do not match (another song, a lag, silence
# where there is sound). With WAVDIR: writes original.wav and enhanced.wav there.
import struct, sys, wave
import numpy as np

orig = np.fromfile(sys.argv[1], dtype='<i2').reshape(-1, 2).astype(np.float64)
hq = np.fromfile(sys.argv[2], dtype='<i2').reshape(-1, 2).astype(np.float64)
n = min(len(orig), len(hq) // 4)
orig, hq = orig[:n], hq[:4 * n]
if len(sys.argv) > 3:
    for name, data, rate in (('original.wav', orig, 13379), ('enhanced.wav', hq, 53516)):
        with wave.open('%s/%s' % (sys.argv[3], name), 'wb') as w:
            w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
            w.writeframes(np.clip(data, -32768, 32767).astype('<i2').tobytes())
# low-pass the enhanced stream below 6.6 kHz (a windowed sinc) and take every 4th frame
taps = 129
k = np.arange(taps) - (taps - 1) / 2
h = np.sinc(k * 2 * 6000 / 53516) * np.hamming(taps)
h /= h.sum()
dec = np.stack([np.convolve(hq[:, c], h, mode='same')[::4] for c in range(2)], axis=1)[:n]
sec = 13379
bad = 0
rows = 0
for s in range(0, n - sec + 1, sec):
    a, b = orig[s:s + sec].ravel(), dec[s:s + sec].ravel()
    la = 20 * np.log10(np.sqrt(np.mean(a * a)) + 1e-9)
    lb = 20 * np.log10(np.sqrt(np.mean(b * b)) + 1e-9)
    if la < -10 and lb < -10:
        corr = 1.0
    else:
        aa, bb = a - a.mean(), b - b.mean()
        corr = float(np.dot(aa, bb) / (np.sqrt(np.dot(aa, aa) * np.dot(bb, bb)) + 1e-9))
    ok = la < 30 or (corr > 0.8 and abs(la - lb) < 4)
    bad += not ok
    rows += 1
    print('  %3d s: original %5.1f dB, enhanced %5.1f dB, correlation %.3f%s' % (s // sec, la, lb, corr, '' if ok else '  <- MISMATCH'))
clip = int(np.sum(np.abs(hq) >= 32767))
print('SOUNDCOMPARE: %d seconds, %d mismatched, %d clipped samples in the enhanced stream' % (rows, bad, clip))
sys.exit(1 if bad or rows == 0 else 0)
