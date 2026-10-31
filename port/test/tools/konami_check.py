#!/usr/bin/env python3
"""Checks for the test-build shortcut (port/test/konami.mk;
port-plan/DIVERGENCES.md TB-1 to TB-6).

  konami_check.py run --trace PC.jsonl --log PC.log --menu-tick T
                      [--jump-tick J --chapter C] [--frames DIR]
      A run of the Gate 0 test build (FE8PC_TESTBUILD=1) and its log. Passes
      when the log says the debug menu opened once, in tick T (the sequence's
      A; the log gives OnMain T+1), and, with --chapter, when
      gPlaySt.chapterIndex is C from tick J (the Map item's A) on and chapter
      C's map load has begun (chapterTurnNumber back to 0 with chapterIndex C).
      Prints the chapterIndex, phase and turn changes from T on. With --frames,
      every raw frame DIR/tickNNNNNN.555 (GATE0_SHOTDIR) is also written as
      DIR/tickNNNNNN.png with frame15.py.

  konami_check.py frames --jsonl FRAMES.jsonl --ticks A-B --lines C-D
      The output of `frame15.py batch GBA_DIR PC_DIR DIFF_DIR`. Passes when
      every frame that differs is from a tick in A-B and differs only on
      lines C-D (the debug menu's Weather row, whose text the GBA decodes from
      RAM that holds host pointers on the PC; src/bmdebug.c).

Exit 0 on pass, 1 on fail, 2 on bad input.
"""

import argparse
import glob
import json
import os
import re
import subprocess
import sys

PLAYST_CHAPTER, PLAYST_FACTION, PLAYST_TURN = 0x0E, 0x0F, 0x10   # struct PlaySt (include/types.h)


def playst_fields(line):
    raw = bytes.fromhex(line["gPlaySt"])
    chapter = raw[PLAYST_CHAPTER]
    if chapter >= 0x80:
        chapter -= 0x100                                            # s8
    return chapter, raw[PLAYST_FACTION], raw[PLAYST_TURN] | (raw[PLAYST_TURN + 1] << 8)


def span(text):
    a, _, b = text.partition("-")
    return int(a), int(b or a)


def check_run(args):
    ok = True
    try:
        with open(args.log, encoding="utf-8", errors="replace") as fh:
            log = [l.rstrip() for l in fh]
        with open(args.trace, encoding="utf-8") as fh:
            header = json.loads(fh.readline())
            lines = [json.loads(l) for l in fh if l.strip()]
    except (OSError, ValueError) as exc:
        print(f"konami_check: {exc}")
        return 2
    if header.get("source") != "pc" or not lines:
        print(f"konami_check: {args.trace} is not a PC trace with ticks")
        return 2

    opened = [l for l in log if l.startswith("[test] debug menu opened")]
    typed = [int(m.group(1)) - 1 for l in log
             for m in [re.search(r"^\[test\] debug-menu key sequence typed \(OnMain (\d+)", l)] if m]
    print(f"key sequence typed in ticks {typed}; menu opened {len(opened)} time(s) {opened}")
    if len(opened) != 1 or typed != [args.menu_tick]:
        print(f"FAIL: expected the debug menu to open once, in tick {args.menu_tick}")
        ok = False
    faults = [l for l in log if l.startswith("[fault]")]
    if faults:
        print(f"FAIL: {faults[0]}")
        ok = False

    last = None
    loaded = None
    left = None
    for line in lines:
        t = line["tick"]
        fields = playst_fields(line)
        if t >= args.menu_tick and (last is None or fields != last):
            print(f"tick {t}: chapterIndex {fields[0]}, phase {fields[1]:02x}, turn {fields[2]}")
        last = fields
        if args.chapter is not None and t >= args.jump_tick:
            if fields[0] != args.chapter and left is None:
                left = t
            if fields[0] == args.chapter and fields[2] == 0 and loaded is None:
                loaded = t
    print(f"trace: {len(lines)} ticks (0-{lines[-1]['tick']})")
    if args.chapter is not None:
        if left is not None:
            print(f"FAIL: chapterIndex is not {args.chapter} at tick {left}")
            ok = False
        if loaded is None:
            print(f"FAIL: chapter {args.chapter}'s map load (turn 0) not seen from tick {args.jump_tick}")
            ok = False
        else:
            print(f"chapter {args.chapter}: chapterIndex from tick {args.jump_tick}, map load (turn 0) "
                  f"from tick {loaded}")

    if args.frames:
        tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "frame15.py")
        shots = sorted(glob.glob(os.path.join(args.frames, "tick*.555")))
        for path in shots:
            subprocess.run([sys.executable, tool, "png", path, path[:-4] + ".png"], check=True)
        print(f"{len(shots)} frames written as PNG in {args.frames}")
    return 0 if ok else 1


def check_frames(args):
    ticks, rows = span(args.ticks), span(args.lines)
    compared, bad = 0, []
    try:
        fh = open(args.jsonl, encoding="utf-8")
    except OSError as exc:
        print(f"konami_check: {exc}")
        return 2
    with fh:
        for line in fh:
            try:
                o = json.loads(line)
            except ValueError:
                continue
            compared += 1
            if not o.get("differing_pixels"):
                continue
            t = o.get("tick", -1)
            inside = (ticks[0] <= t <= ticks[1] and rows[0] <= o["first_line"]
                      and o["last_line"] <= rows[1])
            print(f"tick {t}: {o['differing_pixels']} px on lines {o['first_line']}-{o['last_line']}"
                  + ("" if inside else "  <- outside the allowed ticks or lines"))
            if not inside:
                bad.append(t)
    print(f"{compared} frames compared; differing outside ticks {args.ticks}, lines {args.lines}: "
          f"{len(bad)}")
    return 0 if compared and not bad else 1


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--trace", required=True)
    r.add_argument("--log", required=True)
    r.add_argument("--menu-tick", type=int, required=True)
    r.add_argument("--jump-tick", type=int)
    r.add_argument("--chapter", type=int)
    r.add_argument("--frames")
    f = sub.add_parser("frames")
    f.add_argument("--jsonl", required=True)
    f.add_argument("--ticks", required=True)
    f.add_argument("--lines", required=True)
    args = ap.parse_args()
    if args.cmd == "run":
        if (args.chapter is None) != (args.jump_tick is None):
            ap.error("--chapter and --jump-tick go together")
        status = check_run(args)
    else:
        status = check_frames(args)
    print("KONAMI CHECK: " + ("PASS" if status == 0 else "FAIL"))
    return status


if __name__ == "__main__":
    sys.exit(main())
