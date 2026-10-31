#!/usr/bin/env python3
"""Sort and merge the UBSan sweep's site tables (port/test/README.md, "Bug
sweeps"; port/test/pc/ubsan_log.c writes them, port/test/sweep.mk runs this).

    ubsan_collate.py TABLE.jsonl [TABLE.jsonl ...] [--out FILE] [--json FILE] [--no-addr2line]

Each table is one run of build/pc-gate0-ubsan/fe8pc: a header line, then one
line per check site (check, file:line:col, hits, first and last tick, the
first hit's message and return addresses). Sites of several tables are merged
by check and file:line:col (hits added, each run's ticks kept). Every site is
kept, the ones the GBA means too (a shift or a signed overflow that the GBA's
arithmetic wraps the same way): this only sorts them, by where the code is
(game: src/ and include/; port: port/ outside port/test/; harness: port/test/),
then by check, then by file and line. With addr2line (binutils) each return
address of the first hit gets its function and file:line (inlined calls
included, innermost first); --no-addr2line keeps the run's own Function+offset
names. Prints the sorted table (also to --out); --json writes the merged sites.
Exit 0, or 2 when a table cannot be read.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
from collections import defaultdict

AREAS = ("game", "port", "harness", "other")


def area_of(path):
    p = (path or "").replace("\\", "/")
    for marker in ("/port/test/", "/port/", "/src/", "/include/"):
        if marker in "/" + p:
            p = p[("/" + p).index(marker):].lstrip("/")
            break
    if p.startswith("port/test/"):
        return "harness"
    if p.startswith("port/"):
        return "port"
    if p.startswith(("src/", "include/")):
        return "game"
    return "other"


def read_table(path):
    try:
        with open(path) as fh:
            lines = [json.loads(line) for line in fh if line.strip()]
    except (OSError, ValueError) as e:
        print(f"ubsan_collate: {path}: {e}")
        sys.exit(2)
    if not lines or lines[0].get("ubsan") != 1:
        print(f"ubsan_collate: {path}: not a UBSan sweep table (port/test/pc/ubsan_log.c)")
        sys.exit(2)
    return lines[0], lines[1:]


def symbolize(exe, addrs):
    """{addr: [(function, file:line), ...]} for return addresses (looked up at
    addr - 1, the call instruction), inlined frames innermost first."""
    out = {}
    if not addrs or not exe or not os.path.exists(exe) or not shutil.which("addr2line"):
        return out
    todo = sorted(addrs)
    try:
        res = subprocess.run(["addr2line", "-a", "-f", "-i", "-e", exe] + [f"0x{a - 1:x}" for a in todo],
                             capture_output=True, text=True, check=True).stdout.splitlines()
    except (OSError, subprocess.CalledProcessError):
        return out
    cur, i = None, 0
    while i < len(res):
        line = res[i]
        if line.startswith("0x"):
            cur = int(line, 16) + 1
            out[cur] = []
            i += 1
            continue
        if cur is not None and i + 1 < len(res):
            fn, loc = line, res[i + 1]
            loc = loc.split(" (discriminator")[0]
            for marker in ("/port/", "/src/", "/include/"):
                if marker in loc:
                    loc = loc[loc.index(marker) + 1:]
                    break
            out[cur].append((fn, loc))
            i += 2
        else:
            i += 1
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("tables", nargs="+")
    ap.add_argument("--out", help="also write the sorted table here")
    ap.add_argument("--json", help="write the merged sites as JSON")
    ap.add_argument("--no-addr2line", action="store_true")
    ap.add_argument("--frames", type=int, default=4, help="return addresses shown per site (default 4)")
    a = ap.parse_args()

    merged = {}
    heads = []
    for path in a.tables:
        head, sites = read_table(path)
        heads.append((path, head))
        for s in sites:
            key = (s["check"], s["file"], s["line"], s["col"])
            m = merged.get(key)
            if m is None:
                m = merged[key] = {"check": s["check"], "file": s["file"], "line": s["line"], "col": s["col"],
                                   "area": area_of(s["file"]), "count": 0, "msg": s["msg"], "runs": [],
                                   "exe": head.get("exe"), "bt": [int(x, 16) for x in s.get("bt", [])],
                                   "where": s.get("where", [])}
            m["count"] += s["count"]
            m["runs"].append({"table": path, "count": s["count"], "first_tick": s["first_tick"],
                              "last_tick": s["last_tick"]})

    if not a.no_addr2line:
        by_exe = defaultdict(set)
        for m in merged.values():
            by_exe[m["exe"]].update(m["bt"][:a.frames])
        names = {exe: symbolize(exe, addrs) for exe, addrs in by_exe.items()}
        for m in merged.values():
            table = names.get(m["exe"], {})
            m["frames"] = [table.get(x) or [(m["where"][i] if i < len(m["where"]) else f"0x{x:08x}", "?")]
                           for i, x in enumerate(m["bt"][:a.frames])]
    else:
        for m in merged.values():
            m["frames"] = [[(w, "?")] for w in m["where"][:a.frames]]

    order = sorted(merged.values(), key=lambda m: (AREAS.index(m["area"]), m["check"], m["file"], m["line"],
                                                   m["col"]))
    lines = []
    total_hits = sum(m["count"] for m in order)
    lines.append(f"UBSan sweep: {len(order)} sites, {total_hits} hits, from {len(heads)} table(s):")
    for path, head in heads:
        lines.append(f"  {path}: {head.get('sites')} sites, {head.get('hits')} hits, written at {head.get('end')} "
                     f"(tick {head.get('tick')}), hits at sites not kept (table full) {head.get('hits_lost', 0)}; "
                     f"{head.get('exe')}")
    counts = defaultdict(lambda: [0, 0])
    for m in order:
        counts[(m["area"], m["check"])][0] += 1
        counts[(m["area"], m["check"])][1] += m["count"]
    lines.append("By area and check (sites, hits):")
    for (area, check), (n, h) in sorted(counts.items(), key=lambda kv: (AREAS.index(kv[0][0]), kv[0][1])):
        lines.append(f"  {area:8} {check:26} {n:5} sites {h:12} hits")
    cur = None
    for m in order:
        if (m["area"], m["check"]) != cur:
            cur = (m["area"], m["check"])
            lines.append("")
            lines.append(f"== {m['area']}: {m['check']}")
        # One first-last range per table (a place can hold several sites of one check: inlined copies).
        per_table = {}
        for r in m["runs"]:
            lo, hi = per_table.get(r["table"], (r["first_tick"], r["last_tick"]))
            per_table[r["table"]] = (min(lo, r["first_tick"]), max(hi, r["last_tick"]))
        ticks = ", ".join(f"{lo}-{hi}" for lo, hi in per_table.values())
        lines.append(f"{m['count']:10}  {m['file']}:{m['line']}:{m['col']}  ticks {ticks}  {m['msg']}")
        chain = []
        for frame in m["frames"]:
            fn, loc = frame[0]
            inl = "".join(f" [inlined in {f2}]" for f2, _l in frame[1:])
            chain.append(f"{fn} ({loc}){inl}")
        if chain:
            lines.append("            at " + "\n               < ".join(chain))
    text = "\n".join(lines) + "\n"
    sys.stdout.write(text)
    if a.out:
        with open(a.out, "w") as fh:
            fh.write(text)
    if a.json:
        with open(a.json, "w") as fh:
            json.dump([{k: v for k, v in m.items() if k != "bt"} | {"bt": [f"0x{x:08x}" for x in m["bt"]]}
                       for m in order], fh, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
