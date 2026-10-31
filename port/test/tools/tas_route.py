#!/usr/bin/env python3
"""Turn a frame-mode GBA trace into a tick key file, and check the result
(port/test/README.md, "TAS route").

  tas_route.py make FRAME_TRACE.jsonl OUT.keys --last-tick N [--milestones M.jsonl] [--note TEXT]
      FRAME_TRACE is gate0_capture.lua's trace in frame mode (GATE0_FRAME_KEYS):
      each tick line's "keys" is the mask the game read in that tick. Writes
      ticks 0..N as a key file (README.md, "Input file"): the mask for tick t
      is the one the game read in tick t, which is what a tick key file
      replays. Refused: a trace that is not in frame mode, one that ends
      before tick N, or a tick up to N whose reads saw different masks
      ("key_reads_differ"), which one mask per tick cannot replay. With
      --milestones (movie_check.lua's MOVIE_OUT of the same run), the
      milestones up to tick N go into the file's header.

  tas_route.py verify FRAME_TRACE.jsonl TICK_TRACE.jsonl --last-tick N
      TICK_TRACE is the ordinary Gate 0 capture of the tick key file. Every
      tick 0..N must be equal in every field of the frame trace's line except
      "key_reads"/"key_reads_differ" and the frame15 PNG path (the PNGs are
      compared byte for byte when both exist). Exit 0 on MATCH, 1 otherwise.

  tas_route.py diff-milestones A.jsonl B.jsonl --last-tick N
      Two movie_check.lua milestone lists (the frame run and the tick route
      run): equal kind, text, frame and tick up to tick N. Exit 0 when equal.

The key file is TAS-derived data: write it under port/test/tas/ (git-ignored)
and never commit it.
"""
import argparse
import json
import sys


def read_trace(path):
    with open(path) as f:
        header = json.loads(f.readline())
        lines = [json.loads(l) for l in f if l.strip()]
    return header, lines


def write_keys(path, seq, header):
    with open(path, "w") as f:
        f.write(header)
        i = 0
        while i < len(seq):
            j = i
            while j + 1 < len(seq) and seq[j + 1] == seq[i]:
                j += 1
            n = j - i + 1
            f.write("%03x\n" % seq[i] if n == 1 else "%03x*%d\n" % (seq[i], n))
            i = j + 1


def cmd_make(a):
    header, lines = read_trace(a.trace)
    if not header.get("frame_keys"):
        sys.exit("%s: not a frame-mode trace (no \"frame_keys\" in its header)" % a.trace)
    by_tick = {l["tick"]: l for l in lines}
    keys, differ = [], []
    for t in range(a.last_tick + 1):
        l = by_tick.get(t)
        if l is None:
            sys.exit("%s ends before tick %d" % (a.trace, t))
        if l.get("key_reads_differ"):
            differ.append(t)
        keys.append(int(l["keys"], 16))
    if differ:
        sys.exit("ticks whose reads saw different masks (one mask per tick cannot replay them): %s" % differ[:20])
    reads = [by_tick[t].get("key_reads", 0) for t in range(a.last_tick + 1)]
    h = ["# TAS-derived tick route: local only, never commit (port/test/README.md, \"TAS route\").",
         "# Made by port/test/tools/tas_route.py from the frame-mode trace %s" % a.trace,
         "# Frame source: %s (crc32 %s), interval 0 = entry %d" % (header.get("keys"), header.get("frame_keys_crc32"),
                                                                    header.get("frame0", 0)),
         "# Ticks 0-%d (%d entries): entry t = the mask the game read in tick t." % (a.last_tick, len(keys)),
         "# Ticks with no key read: %d; ticks with more than one: %d." % (sum(1 for r in reads if r == 0),
                                                                         sum(1 for r in reads if r > 1)),
         "# GBA frames: tick 0 at %d, tick %d at %d." % (by_tick[0]["frame"], a.last_tick,
                                                         by_tick[a.last_tick]["frame"])]
    if a.note:
        h += ["# " + line for line in a.note.split("\n")]
    if a.milestones:
        h.append("# Milestones (movie_check.lua on the frame run; frame = GBA VBlanks since power-on):")
        for line in open(a.milestones):
            m = json.loads(line)
            if m["kind"] == "end" or m["tick"] > a.last_tick:
                continue
            h.append("#   tick %d, frame %d: %s (chapterIndex %d, turn %d, phase %02x, gRNSeeds %s)" % (
                m["tick"], m["frame"], m["text"], m["chapter"], m["turn"], m["phase"], m["rn"]))
    write_keys(a.out, keys, "\n".join(h) + "\n")
    print("wrote %s: %d ticks (0-%d), %d key changes" % (a.out, len(keys), a.last_tick,
          sum(1 for i in range(1, len(keys)) if keys[i] != keys[i - 1])))


IGNORED = {"key_reads", "key_reads_differ", "frame15"}


def cmd_verify(a):
    hf, lf = read_trace(a.frame_trace)
    ht, lt = read_trace(a.tick_trace)
    if not hf.get("frame_keys") or ht.get("frame_keys"):
        sys.exit("want a frame-mode trace first and a tick-mode trace second")
    bf = {l["tick"]: l for l in lf}
    bt = {l["tick"]: l for l in lt}
    pngs = 0
    for t in range(a.last_tick + 1):
        x, y = bf.get(t), bt.get(t)
        if x is None or y is None:
            print("DIFFERENT: tick %d missing in the %s trace" % (t, "frame" if x is None else "tick"))
            return 1
        keys = (set(x) | set(y)) - IGNORED
        diff = sorted(k for k in keys if x.get(k) != y.get(k))
        if diff:
            print("DIFFERENT at tick %d: %s" % (t, ", ".join("%s %s vs %s" % (k, x.get(k), y.get(k)) for k in diff[:6])))
            return 1
        fx, fy = x.get("frame15"), y.get("frame15")
        if (fx is None) != (fy is None):
            print("DIFFERENT at tick %d: frame15 on one side only" % t)
            return 1
        if fx and fx.startswith("png:") and fy.startswith("png:"):
            if open(fx[4:], "rb").read() != open(fy[4:], "rb").read():
                print("DIFFERENT at tick %d: frame15 PNGs differ (%s, %s)" % (t, fx[4:], fy[4:]))
                return 1
            pngs += 1
    print("MATCH: ticks 0-%d equal in every traced field (%d frame15 PNGs byte-identical)" % (a.last_tick, pngs))
    return 0


def cmd_diff_milestones(a):
    def load(p):
        out = []
        for line in open(p):
            m = json.loads(line)
            if m["kind"] != "end" and m["tick"] <= a.last_tick:
                out.append((m["tick"], m["frame"], m["kind"], m["text"], m["rn"]))
        return out
    x, y = load(a.a), load(a.b)
    for i, (p, q) in enumerate(zip(x, y)):
        if p != q:
            print("DIFFERENT at milestone %d: %s vs %s" % (i, p, q))
            return 1
    if len(x) != len(y):
        print("DIFFERENT: %d milestones vs %d" % (len(x), len(y)))
        return 1
    print("MATCH: %d milestones up to tick %d equal (tick, frame, kind, text, gRNSeeds)" % (len(x), a.last_tick))
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("make")
    s.add_argument("trace")
    s.add_argument("out")
    s.add_argument("--last-tick", type=int, required=True)
    s.add_argument("--milestones")
    s.add_argument("--note")
    s.set_defaults(fn=cmd_make)
    s = sub.add_parser("verify")
    s.add_argument("frame_trace")
    s.add_argument("tick_trace")
    s.add_argument("--last-tick", type=int, required=True)
    s.set_defaults(fn=cmd_verify)
    s = sub.add_parser("diff-milestones")
    s.add_argument("a")
    s.add_argument("b")
    s.add_argument("--last-tick", type=int, required=True)
    s.set_defaults(fn=cmd_diff_milestones)
    a = p.parse_args()
    sys.exit(a.fn(a) or 0)


if __name__ == "__main__":
    main()
