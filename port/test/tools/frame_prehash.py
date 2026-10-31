#!/usr/bin/env python3
"""Hash a GBA trace's frame15 PNGs before the compare, in several processes (coverage 12).

usage: frame_prehash.py OUTDIR [--jobs 4] [--check 100] [--slow]

gate0_compare.py hashes each "png:PATH" frame15 value of OUTDIR/gba.jsonl itself, one process,
about 25-37 ms a frame: 40,000 frames (a route whose scenes are all compared, inputs/watch/README.md)
take some 25 minutes, under the compare lock. This hashes them first, in JOBS processes, and writes
each hash into gba.jsonl in place of its "png:PATH" (frame15_value passes a hash through as it is),
so the compare's result is the same (checked: compare.json equal on a 52,928-tick route with 11,215
frames).

The hash is gate0_compare.png_15bit_hash's: FNV-1a 32 over the 240x160 pixels as little-endian
r5 | g5<<5 | b5<<10 (the top 5 bits of each 8-bit channel). With Pillow and numpy installed the
PNG is decoded and packed by them (several times faster; the FNV-1a stays a Python loop); --slow,
or either missing, uses png_15bit_hash itself. Either way CHECK random frames, plus the first and
the last, are hashed again with png_15bit_hash in this process and must be equal before the file
is rewritten (else it is left as it was, exit 1)."""
import argparse
import json
import os
import random
import sys
from multiprocessing import Pool

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gate0_compare  # noqa: E402

try:
    import numpy as np
    from PIL import Image
except ImportError:  # the slow path: png_15bit_hash itself
    np = Image = None


def fast_hash(path):
    """png_15bit_hash with the decoding and packing done by Pillow and numpy."""
    with Image.open(path) as im:
        if im.size != (240, 160) or im.mode not in ("RGB", "RGBA"):
            raise ValueError(f"{path}: unsupported PNG ({im.size}, {im.mode})")
        a = np.asarray(im)
    v = ((a[:, :, 0] >> 3).astype(np.uint16) | ((a[:, :, 1] >> 3).astype(np.uint16) << 5)
         | ((a[:, :, 2] >> 3).astype(np.uint16) << 10))
    h = 0x811C9DC5
    for byte in v.astype("<u2").tobytes():
        h = ((h ^ byte) * 0x01000193) & 0xFFFFFFFF
    return f"{h:08x}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--check", type=int, default=100)
    ap.add_argument("--slow", action="store_true", help="hash with gate0_compare.png_15bit_hash only")
    a = ap.parse_args()
    src = os.path.join(a.out, "gba.jsonl")
    with open(src) as fh:
        lines = fh.read().splitlines()
    paths = []
    for ln in lines:
        v = json.loads(ln).get("frame15")
        if isinstance(v, str) and v.startswith("png:"):
            paths.append(v[4:])
    if not paths:
        print("frame_prehash: no PNG frames (already hashed?)")
        return 0
    fn = gate0_compare.png_15bit_hash if a.slow or np is None else fast_hash
    with Pool(a.jobs) as pool:
        table = dict(zip(paths, pool.map(fn, paths, chunksize=64)))
    sample = random.Random(12).sample(paths, min(a.check, len(paths))) + [paths[0], paths[-1]]
    bad = [p for p in sample if gate0_compare.png_15bit_hash(p) != table[p]]
    if bad:
        print(f"frame_prehash: self-check FAILED on {len(bad)} of {len(sample)} frames; {src} left as it was")
        return 1
    tmp = src + ".tmp"
    with open(tmp, "w") as fh:
        for ln in lines:
            v = json.loads(ln).get("frame15")
            if isinstance(v, str) and v.startswith("png:"):
                ln = ln.replace(json.dumps(v), json.dumps(table[v[4:]]), 1)
            fh.write(ln + "\n")
    os.replace(tmp, src)
    print(f"frame_prehash: {len(paths)} frames hashed in {a.jobs} processes "
          f"({'png_15bit_hash' if fn is not fast_hash else 'Pillow and numpy'}); self-check {len(sample)} equal")
    return 0


if __name__ == "__main__":
    sys.exit(main())
