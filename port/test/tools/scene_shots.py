#!/usr/bin/env python3
"""GATE0_SHOT_TICKS for every frame of a route's scenes (coverage 12, inputs/watch/README.md).

usage: scene_shots.py SCENES.txt [--kinds opening,intro,quote,event,talk,ending,battle,menu] [--pad 3] [--gap 30]

SCENES.txt is route_check.lua's ROUTE_SCENES_OUT: one line per run of ticks of one kind,
"first last kind chapterIndex". The runs of the chosen kinds (default: every scene kind of
route_lib.lua's L.sceneKind, the battle screen and the menus: every kind but "-", the map play,
which keeps its frame every 60 ticks) are widened by PAD ticks on each side, runs less than GAP
ticks apart are joined, and the result is printed as "a-b,c-d,..." (the ranges GATE0_SHOT_TICKS
takes: port/test/README.md, "Trace"). A summary goes to stderr.

--max-windows (default 64): the PC hook keeps only the first 64 ranges of GATE0_SHOT_TICKS
(port/test/pc/gate0_hook.c, sShotRanges[64]; ReadShotTicks stops there without a word, and the
compare then compares frames only "where both exist"), so the two windows nearest each other are
joined, again and again, until at most that many are left (the ticks between them get frames too)."""
import argparse
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenes")
    ap.add_argument("--kinds", default="opening,intro,quote,event,talk,ending,battle,menu")
    ap.add_argument("--pad", type=int, default=3)
    ap.add_argument("--gap", type=int, default=30)
    ap.add_argument("--max-windows", type=int, default=64)
    a = ap.parse_args()
    kinds = set(a.kinds.split(","))
    runs, last, bykind = [], 0, {}
    with open(a.scenes) as fh:
        for line in fh:
            first, end, kind, _ch = line.split()
            first, end = int(first), int(end)
            last = max(last, end)
            bykind[kind] = bykind.get(kind, 0) + end - first + 1
            if kind in kinds:
                runs.append([max(0, first - a.pad), end + a.pad])
    runs.sort()
    merged = []
    for first, end in runs:
        if merged and first <= merged[-1][1] + a.gap:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([first, end])
    merged = [[f, min(e, last)] for f, e in merged if f <= last]
    w0 = len(merged)
    while len(merged) > a.max_windows:
        i = min(range(len(merged) - 1), key=lambda j: merged[j + 1][0] - merged[j][1])
        merged[i][1] = max(merged[i][1], merged[i + 1][1])
        del merged[i + 1]
    n = sum(e - f + 1 for f, e in merged)
    print(",".join(f"{f}-{e}" for f, e in merged))
    print(f"scene_shots: {len(merged)} windows ({w0} before joining to at most {a.max_windows}), {n} ticks of "
          f"{last + 1} ({100.0 * n / max(1, last + 1):.1f}%); "
          "ticks by kind: " + ", ".join(f"{k} {v}" for k, v in sorted(bykind.items())), file=sys.stderr)


if __name__ == "__main__":
    main()
