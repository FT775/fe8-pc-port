#!/usr/bin/env python3
"""PPU test ROM check (port/test/README.md, "PPU test ROM").

   pputest_check.py DIR

Reads the oracle's shots DIR/pputest0.png, pputest1.png and pputest2.png of
port/test/oracle/pputest (mGBA, HLE BIOS) and checks the listed pixels
against what mGBA 0.10.5 drew on 2026-10-21. The same tables are in
port/render/test_obj.c RT19 (variants 0 and 1) and test_bg.c's mGBA BG clip
case (variant 2), where the PC renderer must draw the same scenes the same
way. Exit 0 when every listed pixel is as listed, 1 otherwise.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import frame15  # noqa: E402

A, B, C, BG0, BG1 = 0x7C00, 0x03FF, 0x7FFF, 0x001F, 0x03E0
OBJ_NAMES = {A: "A", B: "B", C: "C", BG0: "BG0", BG1: "BG1", 0: "backdrop"}
# Variants 0 and 1, line 10: (test, x, colour, rule). Test k's sprites are at x = 16k + 4 (pputest.c).
OBJ_EXPECT = [
    (0, 6, A, "A (prio 2) under a later sprite's clear pixels (prio 0) takes priority 0: C (prio 1) cannot "
              "replace it, and it is in front of BG0"),
    (1, 22, C, "no clear sprite: C (prio 1) replaces A (prio 2)"),
    (2, 38, A, "A lifted to priority 0 is in front of BG0 (priority 1)"),
    (3, 54, A, "the lift also takes the clear sprite's mode: a semi-transparent A becomes normal (no blend)"),
    (4, 70, 0x3DE0, "and a normal A becomes semi-transparent: A and BG1 blended 8/8"),
    (5, 86, BG0, "an OBJ-window sprite's clear pixels lift nothing"),
    (6, 102, BG0, "nor do its opaque pixels"),
    (7, 118, A, "equal priority: the earlier entry stays"),
    (8, 134, A, "A (prio 3) lifted to 1: C (prio 2) cannot replace it; A ties with BG0 and is in front"),
    (9, 150, BG0, "clear pixels over nothing do nothing: B (prio 2) is behind BG0"),
    (10, 166, A, "an affine sprite's clear pixels lift too"),
    (11, 182, A, "a later clear sprite with a higher priority value changes nothing"),
    (12, 198, A, "lifted to 0, then C at priority 0: the tie keeps A"),
    (13, 213, BG0, "A's left half, where the clear sprite is not: not lifted"),
    (13, 217, A, "A's right half, under the clear sprite: lifted"),
]
# Variant 2: colour index c + 1 of tile column c (pputest.c BgClipScene); (line, x0, indices, rule).
CLIP_COLORS = {(i * 3 | (i * 2) << 5 | (31 - i * 3) << 10): i for i in range(1, 9)}
CLIP_COLORS[0] = 0
CLIP_EXPECT = [
    (10, 128, [0, 0, 5, 4, 3, 2, 1, 0, 0],
     "BG0 (HOFS 0) through WIN0 [130, 135): the segment starts at column 2 of a flipped tile and ends before its "
     "last pixel; mGBA puts the tile's pixel 0 at x 134, one to the left of the hardware's 6 5 4 3 2"),
    (90, 128, [0, 0, 3, 2, 1, 8, 7, 0, 0],
     "BG1 (HOFS 3) through WIN1 [130, 135): the first tile ends inside the segment, so it is drawn right"),
    (130, 128, [0] * 9, "no window on line 130: outside shows nothing"),
]


def check_obj(path):
    bad = 0
    f = frame15.png_to_555(path)
    for k, x, want, rule in OBJ_EXPECT:
        got = f[10 * 240 + x]
        ok = got == want
        bad += not ok
        print(f"{os.path.basename(path)} test {k:2d} x {x:3d}: {OBJ_NAMES.get(got, format(got, '04x')):>8s} "
              f"(want {OBJ_NAMES.get(want, format(want, '04x'))}) {'ok' if ok else 'DIFFERENT'}  {rule}")
    return bad


def check_clip(path):
    bad = 0
    f = frame15.png_to_555(path)
    for y, x0, want, rule in CLIP_EXPECT:
        got = [CLIP_COLORS.get(f[y * 240 + x], -1) for x in range(x0, x0 + len(want))]
        ok = got == want
        bad += not ok
        print(f"{os.path.basename(path)} line {y:3d} x {x0}-{x0 + len(want) - 1}: {got} (want {want}) "
              f"{'ok' if ok else 'DIFFERENT'}  {rule}")
    return bad


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    d = sys.argv[1]
    bad = 0
    for name, fn in (("pputest0.png", check_obj), ("pputest1.png", check_obj), ("pputest2.png", check_clip)):
        try:
            bad += fn(os.path.join(d, name))
        except (OSError, ValueError) as e:
            print(f"pputest_check: {name}: {e}")
            bad += 1
    print(f"PPU TEST ROM: {'PASS' if bad == 0 else 'FAIL'}")
    return 1 if bad else 0


sys.exit(main())
