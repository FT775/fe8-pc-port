#!/usr/bin/env python3
"""Turn a Gate 0 key file into FE8PC_KEYSCRIPT for a played build.

    python3 port/test/tools/keys_to_keyscript.py FILE.keys        -> prints "frame:mask,..."
    python3 port/test/tools/keys_to_keyscript.py --ticks FILE.keys -> prints the file's tick count

port/test/README.md, "Save checks". A played build has no Gate 0 key file;
FE8PC_KEYSCRIPT (port/hw/frame.c) holds each "frame:mask" press for 2 frames,
at most 64 presses. On the lag-free PC frame N is harness tick N: a Gate 0
tick is one main-loop VBlankIntrWait, and the PC waits once per frame. OnMain
calls can be fewer than frames (in battles and enemy phases, e.g. "frames=4000
OnMain calls=3202"; Gate 0's onmain_calls counts the same on mGBA and the
PC), so the OnMain count is not the measure. A key file whose presses are each
held exactly 2 ticks plays the same on both. Refused (exit
2): a press held for another number of ticks, two presses that touch, or more
than 64 presses.
"""
import sys


def read_keys(path):
    out = []
    for line in open(path):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        m, _, n = line.partition("*")
        out += [int(m, 16)] * (int(n) if n else 1)
    return out


def main():
    args = sys.argv[1:]
    ticks = args and args[0] == "--ticks"
    if ticks:
        args = args[1:]
    if len(args) != 1:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    keys = read_keys(args[0])
    if ticks:
        print(len(keys))
        return 0
    presses = []
    t = 0
    while t < len(keys):
        if keys[t] == 0:
            t += 1
            continue
        m, s = keys[t], t
        while t < len(keys) and keys[t] == m:
            t += 1
        if t - s != 2:
            print(f"{args[0]}: tick {s}: mask {m:03X} held {t - s} ticks, the key script holds 2", file=sys.stderr)
            return 2
        if t < len(keys) and keys[t] != 0:
            print(f"{args[0]}: tick {t}: a press right after another", file=sys.stderr)
            return 2
        presses.append(f"{s}:0x{m:X}")
    if len(presses) > 64:
        print(f"{args[0]}: {len(presses)} presses, FE8PC_KEYSCRIPT takes 64", file=sys.stderr)
        return 2
    print(",".join(presses))
    return 0


if __name__ == "__main__":
    sys.exit(main())
