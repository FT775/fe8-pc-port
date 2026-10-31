#!/usr/bin/env python3
"""Checks the route sound check itself (audio/snd_route.py) with altered copies
of a finished snd-route run (port/test/README.md, "Sound on any route").

    python3 port/test/audio/mutate_snd_route.py build/gate0/snd-route/NAME [--ticks N]

Needs a run kept with SND_KEEP=1 that passes. Each mutant is a copy of its
files in build/difftest/snd-route-mutants/ (ignored by git, deleted after; the
run itself is never changed) with one change, and snd_route.py must give the
verdict listed:
  baseline        no change                                              PASS
  pc-pcm          one PCM lane byte of a PC record                       FAIL (PC against GBA, PCM lanes)
  pc-track        one track byte of a PC record                          FAIL (PC against GBA; PCM lanes equal)
  pc-skip-vblank  one PC VBlank left out (its record, FIFO and samples;
                  the later records and index renumbered)                FAIL (frame clock)
  gba-track       one track byte of a GBA record                         FAIL (replay)
  gba-drop-call   one clean game m4aSongNumStart left out of the calls   FAIL (replay)
  unclean-edge    one clean game m4aSongNumStart marked not clean, open
                  across the record that starts a chunk                  PASS, excused, the same output in
                                                                         one chunk as with that chunk edge
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys

REC = 8236
VB = 224
O_TRK, O_LANES = 16, 16 + 3120 + 0x350
HERE = os.path.dirname(os.path.abspath(__file__))


def run(args, d, ticks, chunk=4096, jobs=4):
    cmd = [sys.executable, os.path.join(HERE, "snd_route.py"), "--gba", f"{d}/snd.gba", "--pc", f"{d}/snd.pc",
           "--pc-exe", args.pc_exe, "--ticks", str(ticks), "--chunk", str(chunk), "--jobs", str(jobs),
           "--out", os.path.join(args.work, "harness")]
    r = subprocess.run(cmd, capture_output=True, text=True)
    out = r.stdout + r.stderr
    m = re.search(r"^SND ROUTE: (\w+) \(replay (\w+), self-check (\w+), PC against GBA (\w+)", out, re.M)
    return (m.groups() if m else None), out


def copy_run(src, dst, mutable):
    """dst gets src's files: those in mutable copied, the others linked."""
    if os.path.exists(dst):
        shutil.rmtree(dst)
    os.makedirs(dst)
    for name in ("snd.gba.snd", "snd.gba.calls", "snd.gba.idx", "snd.pc.snd", "snd.pc.fifo8", "snd.pc.lr16",
                 "snd.pc.idx", "snd.pc.jsonl", "snd.pc.log"):
        s, t = os.path.join(src, name), os.path.join(dst, name)
        if not os.path.exists(s):
            continue
        if name in mutable:
            shutil.copyfile(s, t)
        else:
            os.symlink(os.path.abspath(s), t)


def flip(path, offset):
    with open(path, "r+b") as f:
        f.seek(offset)
        b = f.read(1)
        f.seek(offset)
        f.write(bytes([b[0] ^ 0x5A]))


def calls_lines(path):
    with open(path) as f:
        return f.readlines()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dir")
    ap.add_argument("--ticks", type=int, help="the run's ticks (default: the PC index's last tick + 1)")
    ap.add_argument("--pc-exe", default="build/pc-gate0/fe8pc")
    ap.add_argument("--work", default="build/difftest/snd-route-mutants")
    a = ap.parse_args()
    src = a.dir
    os.makedirs(a.work, exist_ok=True)
    if a.ticks is None:
        last = [ln.split() for ln in open(f"{src}/snd.pc.idx") if not ln.startswith("#")][-1]
        a.ticks = int(last[0]) + 1
    n_g = os.path.getsize(f"{src}/snd.gba.snd") // REC
    n_p = os.path.getsize(f"{src}/snd.pc.snd") // REC
    pc_first = struct.unpack_from("<I", open(f"{src}/snd.pc.snd", "rb").read(16), 8)[0]
    lines = calls_lines(f"{src}/snd.gba.calls")
    starts = [i for i, ln in enumerate(lines) if not ln.startswith("#") and ln.split()[4] == "m4aSongNumStart"
              and ln.split()[3] == "1" and int(ln.split()[1]) > n_g // 3]
    if not starts:
        sys.exit(f"{src}: no clean m4aSongNumStart in the last two thirds of the capture")
    call_i = starts[0]
    call_w = int(lines[call_i].split()[1])
    mid = n_p // 2
    ok = True
    expect = {}

    def check(name, want, got, out, extra=""):
        nonlocal ok
        good = got is not None and got[0] == want[0] and all(w is None or w == g for w, g in zip(want, got))
        ok &= good
        print(f"{'OK ' if good else 'BAD'} {name:15s} {('SND ROUTE ' + got[0]) if got else 'no verdict'}"
              + (f" (replay {got[1]}, self-check {got[2]}, PC against GBA {got[3]})" if got else "") + extra)
        if not good:
            print(out[-3000:])

    d = os.path.join(a.work, "m")
    # baseline
    copy_run(src, d, ())
    got, out = run(a, d, a.ticks)
    check("baseline", ("PASS", "PASS", "PASS", "PASS"), got, out)
    # pc-pcm: a lane byte of the PC record in the middle
    copy_run(src, d, ("snd.pc.snd",))
    flip(f"{d}/snd.pc.snd", mid * REC + O_LANES + 100)
    got, out = run(a, d, a.ticks)
    m = re.search(r"PCM lanes equal on (\d+) of (\d+)", out)
    check("pc-pcm", ("FAIL", "PASS", None, "FAIL"), got, out,
          f"; PCM lanes equal on {m.group(1)} of {m.group(2)}" if m else "")
    # pc-track
    copy_run(src, d, ("snd.pc.snd",))
    flip(f"{d}/snd.pc.snd", mid * REC + O_TRK + 0x10)
    got, out = run(a, d, a.ticks)
    m = re.search(r"PCM lanes equal on (\d+) of (\d+)", out)
    lanes_ok = m is not None and m.group(1) == m.group(2)
    check("pc-track", ("FAIL", "PASS", "PASS", "FAIL"), got if lanes_ok else None, out,
          f"; PCM lanes equal on {m.group(1)} of {m.group(2)}" if m else "")
    # pc-skip-vblank: PC record `mid` left out, the later ones renumbered
    copy_run(src, d, ())
    for name, size in (("snd.pc.fifo8", 2 * VB), ("snd.pc.lr16", 4 * VB)):
        if os.path.lexists(f"{d}/{name}"):
            os.remove(f"{d}/{name}")
            with open(f"{src}/{name}", "rb") as fi, open(f"{d}/{name}", "wb") as fo:
                data = fi.read()
                fo.write(data[:mid * size] + data[(mid + 1) * size:])
    os.remove(f"{d}/snd.pc.snd")
    with open(f"{src}/snd.pc.snd", "rb") as fi, open(f"{d}/snd.pc.snd", "wb") as fo:
        for i in range(n_p):
            r = bytearray(fi.read(REC))
            if i == mid:
                continue
            if i > mid:
                struct.pack_into("<I", r, 8, struct.unpack_from("<I", r, 8)[0] - 1)
            fo.write(r)
    os.remove(f"{d}/snd.pc.idx")
    with open(f"{src}/snd.pc.idx") as fi, open(f"{d}/snd.pc.idx", "w") as fo:
        for ln in fi:
            if not ln.startswith("#"):
                t, v = map(int, ln.split())
                if v - pc_first > mid:
                    ln = f"{t} {v - 1}\n"
            fo.write(ln)
    got, out = run(a, d, a.ticks)
    m = re.search(r"^frame clock: .*", out, re.M)
    check("pc-skip-vblank", ("FAIL", "PASS", None, "FAIL"), got if m and "but" in m.group(0) else None, out,
          f"; {m.group(0)[:150]}" if m else "")
    # gba-track
    copy_run(src, d, ("snd.gba.snd",))
    flip(f"{d}/snd.gba.snd", (n_g // 2) * REC + O_TRK + 0x10)
    got, out = run(a, d, a.ticks)
    check("gba-track", ("FAIL", "FAIL", None, None), got, out)
    # gba-drop-call
    copy_run(src, d, ())
    os.remove(f"{d}/snd.gba.calls")
    with open(f"{d}/snd.gba.calls", "w") as fo:
        fo.writelines(lines[:call_i] + lines[call_i + 1:])
    got, out = run(a, d, a.ticks)
    check("gba-drop-call", ("FAIL", "FAIL", None, None), got, out, f"; the call of window {call_w}")
    # unclean-edge: the call not clean, open across record call_w + 1, which starts a chunk
    copy_run(src, d, ())
    os.remove(f"{d}/snd.gba.calls")
    p = lines[call_i].split()
    p[2], p[3] = str(call_w + 1), "0"
    with open(f"{d}/snd.gba.calls", "w") as fo:
        fo.writelines(lines[:call_i] + [" ".join(p) + "\n"] + lines[call_i + 1:])
    gba_first = struct.unpack_from("<I", open(f"{src}/snd.gba.snd", "rb").read(16), 8)[0]
    edge = call_w + 1 - gba_first            # file index of the record that starts the second chunk
    got1, out1 = run(a, d, a.ticks, chunk=10 ** 9, jobs=1)
    got2, out2 = run(a, d, a.ticks, chunk=edge, jobs=4)
    same = [ln for ln in out1.splitlines() if not ln.startswith("chunks:")] == \
           [ln for ln in out2.splitlines() if not ln.startswith("chunks:")]
    m = re.search(r"(\d+) equal outside the scope of a call that was not clean", out1)
    check("unclean-edge", ("PASS", "PASS", "PASS", "PASS"), got1 if same and m and m.group(1) != "0" else None,
          out1 + "\n----\n" + out2, f"; excused {m.group(1) if m else '?'} VBlanks in the replay; one chunk and a chunk"
          f" edge at record {edge} give {'the same' if same else 'DIFFERENT'} output")
    shutil.rmtree(d)
    print("RESULT:", "PASS" if ok else "FAIL", "(the baseline passes and every mutant gets its verdict)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
