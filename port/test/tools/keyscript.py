#!/usr/bin/env python3
"""Build a hand-made Gate 0 tick key file (port/test/README.md, "Input file") from a small script
(round 10 "paths", port/test/inputs/paths/README.md). Run from the repository root:

    python3 port/test/tools/keyscript.py port/test/inputs/paths/NAME.ks port/test/inputs/paths/NAME.keys

Script lines (# comments):
  header TEXT              a '# TEXT' line in the output header
  prefix FILE TICKS        the first TICKS entries of key file FILE (a bot route's way to its first free cursor)
  wait N                   N ticks with no key
  tap KEY[+KEY] [GAP]      the keys for 1 tick, then GAP ticks with none (default 4)
  hold KEY[+KEY] N [GAP]   the keys for N ticks, then GAP ticks with none (default 4)
  rep N KEY [GAP]          tap KEY N times
  mark NAME                print the tick here (to stdout)
Keys: A B SEL START R L RIGHT LEFT UP DOWN (or hex).
"""
import sys

KEYS = {"A": 0x001, "B": 0x002, "SEL": 0x004, "START": 0x008, "RIGHT": 0x010, "LEFT": 0x020, "UP": 0x040,
        "DOWN": 0x080, "R": 0x100, "L": 0x200}


def mask(spec):
    m = 0
    for k in spec.split("+"):
        m |= KEYS[k] if k in KEYS else int(k, 16)
    return m


def read_keys(path):
    out = []
    for line in open(path):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        if "*" in line:
            m, n = line.split("*")
            out += [int(m, 16)] * int(n)
        else:
            out.append(int(line, 16))
    return out


def main():
    script, outp = sys.argv[1], sys.argv[2]
    seq, header = [], []
    for raw in open(script):
        line = raw.split("#", 1)[0].strip() if not raw.lstrip().startswith("header") else raw.strip()
        if not line:
            continue
        p = line.split()
        op = p[0]
        if op == "header":
            header.append(line[len("header"):].strip())
        elif op == "prefix":
            pre = read_keys(p[1])
            n = int(p[2])
            assert len(pre) >= n, "prefix shorter than %d" % n
            seq += pre[:n]
        elif op == "wait":
            seq += [0] * int(p[1])
        elif op == "tap":
            gap = int(p[2]) if len(p) > 2 else 4
            seq += [mask(p[1])] + [0] * gap
        elif op == "hold":
            gap = int(p[3]) if len(p) > 3 else 4
            seq += [mask(p[1])] * int(p[2]) + [0] * gap
        elif op == "rep":
            gap = int(p[3]) if len(p) > 3 else 4
            for _ in range(int(p[1])):
                seq += [mask(p[2])] + [0] * gap
        elif op == "mark":
            print("mark %s %d" % (p[1], len(seq)))
        else:
            raise SystemExit("bad line: " + raw)
    # run-length encode
    lines = ["# FE8 Gate 0 key file (format: port/test/README.md, \"Input file\")."]
    lines += ["# " + h for h in header]
    lines.append("# %d ticks." % len(seq))
    i = 0
    while i < len(seq):
        j = i
        while j < len(seq) and seq[j] == seq[i]:
            j += 1
        lines.append("%03x*%d" % (seq[i], j - i) if j - i > 1 else "%03x" % seq[i])
        i = j
    open(outp, "w").write("\n".join(lines) + "\n")
    print("ticks %d -> %s" % (len(seq), outp))


main()
