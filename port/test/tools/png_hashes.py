#!/usr/bin/env python3
"""Precompute the frame15 values of a GBA capture's PNG frames (port/test/README.md, "PNG hash
cache"), so that gate0_compare.py --png-hashes FILE does not decode every PNG again under the
compare lock.

    python3 port/test/tools/png_hashes.py GBA_TRACE.jsonl[.gz] OUT.json [--check N]

The value is the one gate0_compare.png_15bit_hash gives: FNV-1a 32 over the 240x160 pixels as
little-endian r5 | g5 << 5 | b5 << 10. With PIL and numpy the PNGs are decoded in C and the FNV
loop runs over a batch of frames at once (numpy uint32 arithmetic wraps as FNV wants); without
them every frame goes through png_15bit_hash itself. --check N also hashes N frames, spread over
the trace, the slow way and exits 1 if any differs. OUT.json maps each "png:PATH" value of the
trace to its hash; it can be made outside the lock, while the PC runs go on, and then reused by
every compare of that capture."""
import gzip
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gate0_compare import png_15bit_hash  # noqa: E402

try:
    import numpy as np
    from PIL import Image
except ImportError:  # the slow way only
    np = None

BATCH = 256


def frame_paths(trace):
    op = gzip.open if trace.endswith(".gz") else open
    paths = []
    with op(trace, "rt") as fh:
        for line in fh:
            if '"frame15":"png:' in line:
                v = json.loads(line).get("frame15")
                if isinstance(v, str) and v.startswith("png:"):
                    paths.append(v)
    return paths


def fast_hashes(values):
    """FNV-1a 32 of each frame's 76,800 bytes, BATCH frames at a time (one numpy step per byte)."""
    out = {}
    for i in range(0, len(values), BATCH):
        chunk = values[i:i + BATCH]
        rows = []
        for v in chunk:
            a = np.asarray(Image.open(v[4:]).convert("RGB"), dtype=np.uint16)
            if a.shape != (160, 240, 3):
                raise ValueError(f"{v[4:]}: {a.shape[1]}x{a.shape[0]}, not 240x160")
            px = (a[:, :, 0] >> 3) | ((a[:, :, 1] >> 3) << 5) | ((a[:, :, 2] >> 3) << 10)
            rows.append(np.frombuffer(px.astype("<u2").tobytes(), dtype=np.uint8))
        data = np.ascontiguousarray(np.stack(rows).T, dtype=np.uint32)  # byte position x frame
        h = np.full(len(chunk), 0x811C9DC5, dtype=np.uint32)
        prime = np.uint32(0x01000193)
        with np.errstate(over="ignore"):
            for col in data:
                h ^= col
                h *= prime
        for v, x in zip(chunk, h):
            out[v] = f"{int(x):08x}"
    return out


def main():
    args = sys.argv[1:]
    check = 0
    if "--check" in args:
        i = args.index("--check")
        check = int(args[i + 1])
        del args[i:i + 2]
    if len(args) != 2:
        print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
        return 2
    trace, out = args
    values = sorted(set(frame_paths(trace)))
    t0 = time.time()
    if np is not None:
        res = fast_hashes(values)
        how = "PIL + numpy"
    else:
        res = {v: png_15bit_hash(v[4:]) for v in values}
        how = "png_15bit_hash (no PIL/numpy)"
    t1 = time.time()
    bad = 0
    if check and values:
        step = max(1, len(values) // check)
        sample = values[::step][:check]
        for v in sample:
            slow = png_15bit_hash(v[4:])
            if slow != res[v]:
                bad += 1
                print(f"png_hashes: MISMATCH {v}: png_15bit_hash {slow}, cache {res[v]}", file=sys.stderr)
        print(f"png_hashes: check: {len(sample)} frames hashed the slow way too, {bad} differ")
    if bad:
        return 1
    with open(out + ".tmp", "w") as fh:
        json.dump(res, fh)
    os.replace(out + ".tmp", out)
    per = (t1 - t0) / max(1, len(res)) * 1000
    print(f"png_hashes: {len(res)} frames in {t1 - t0:.1f} s ({per:.2f} ms each, {how}) -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
