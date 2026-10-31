#!/usr/bin/env python3
"""Compare two PC Gate 0 traces of the same buttons made by two different
binaries: the uninitialised-locals sweep (port/test/sweep.mk uninit-compare;
port/test/README.md, "Bug sweeps"), where one binary fills every automatic
variable without an initialiser with zeros and the other with a pattern
(GCC's -ftrivial-auto-var-init=zero and =pattern). Same buttons, same start:
any traced field or frame that differs means some code read an uninitialised
local, and the value it found changed the game state.

    sweep_pair.py --a A.jsonl --b B.jsonl --a-exe A --b-exe B [--a-raw A.raw --b-raw B.raw]
                  [--a-log A.log --b-log B.log] [--names zero,pattern] [--ticks N] [--json OUT]

Every field of every tick is compared, the frames (frame15) too.
gMainCallback holds a host address, which moves between binaries, so it is
compared by function name (nm on each binary); nothing else in the trace is a
host address. Both traces must be of the same key file, start (blank SRAM, a
save, or one save state's set, tick and part) and GBA lag source (the lag
replay's gba.jsonl) or neither: otherwise exit 2. Printed: the first differing
tick with every field that differs there (the first differing byte and its
member, palette entry, tilemap cell or OAM entry, from the raw records when
given), then the first differing tick of each field and how many ticks differ.
Exit 0: the same on every tick, and both runs reached --ticks (when given)
with no [fault] in their logs; 1: a tick differs, or a run stopped early
(INCOMPLETE); 2: bad input.
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gate0_compare as g0  # noqa: E402  (trace and raw readers, member names)

HEADER_SAME = ("keys", "sram_fnv", "lag_from", "raster_from", "clock_from", "affine_from")


def name_of(table, value):
    try:
        addr = int(value, 16)
    except (TypeError, ValueError):
        return value
    return table.get(addr, value)


def first_byte_diff(x, y):
    for i in range(min(len(x), len(y))):
        if x[i] != y[i]:
            return i
    return None if len(x) == len(y) else min(len(x), len(y))


def field_detail(field, va, vb, raw_a, raw_b, tick):
    """One differing field, in words."""
    if field in g0.RAWS and isinstance(va, str) and isinstance(vb, str):
        ba, bb = bytes.fromhex(va), bytes.fromhex(vb)
        off = first_byte_diff(ba, bb)
        if off is not None:
            return (f"{field} {g0.describe_offset(field, off)}: {ba[off:off + 4].hex()} vs {bb[off:off + 4].hex()} "
                    f"({sum(1 for i in range(min(len(ba), len(bb))) if ba[i] != bb[i])} bytes differ)")
    hashed = dict(g0.HASHED)
    if field in hashed:
        ra, rb = g0.raw_region(raw_a, tick, field), g0.raw_region(raw_b, tick, field)
        if ra is not None and rb is not None:
            ra, rb = bytes(ra), bytes(rb)
            off = first_byte_diff(ra, rb)
            if off is not None:
                n = sum(1 for i in range(len(ra)) if ra[i] != rb[i])
                return (f"{field} {g0.describe_offset(field, off)}: {ra[off:off + 2].hex()} vs {rb[off:off + 2].hex()} "
                        f"({n} bytes differ; hash {va} vs {vb})")
        return f"{field}: hash {va} vs {vb} (no raw records)"
    return f"{field}: {json.dumps(va)} vs {json.dumps(vb)}"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--a", required=True)
    ap.add_argument("--b", required=True)
    ap.add_argument("--a-exe", required=True)
    ap.add_argument("--b-exe", required=True)
    ap.add_argument("--a-raw")
    ap.add_argument("--b-raw")
    ap.add_argument("--a-log")
    ap.add_argument("--b-log")
    ap.add_argument("--names", default="A,B")
    ap.add_argument("--ticks", type=int, help="the end tick both runs should reach (TICKS)")
    ap.add_argument("--json")
    a = ap.parse_args()
    na, nb = (a.names.split(",") + ["A", "B"])[:2]

    ha, ta = g0.load_trace(a.a)
    hb, tb = g0.load_trace(a.b)
    problems = [f"{k}: {na} {ha.get(k)!r}, {nb} {hb.get(k)!r}" for k in HEADER_SAME if ha.get(k) != hb.get(k)]
    ra_, rb_ = ha.get("resume"), hb.get("resume")
    if bool(ra_) != bool(rb_):
        problems.append(f"resume: {na} {'resumed' if ra_ else 'from power-on'}, {nb} {'resumed' if rb_ else 'from power-on'}")
    elif ra_:
        for k in ("tick", "set", "part", "rom_crc32", "keys_crc32"):
            if ra_.get(k) != rb_.get(k):
                problems.append(f"resume.{k}: {na} {ra_.get(k)!r}, {nb} {rb_.get(k)!r}")
    if problems:
        print("sweep_pair: the two runs did not get the same buttons from the same start:\n  " + "\n  ".join(problems))
        return 2
    first = ra_["tick"] - 1 if ra_ else 0
    g0.FIRST = first
    same_exe = ha.get("exe_crc32") == hb.get("exe_crc32")

    fa = g0.function_names(g0.nm_table("nm", a.a_exe), thumb=False)
    fb = g0.function_names(g0.nm_table("nm", a.b_exe), thumb=False)
    if not fa or not fb:
        print(f"sweep_pair: cannot read the symbols of {a.a_exe if not fa else a.b_exe} (nm)")
        return 2
    end_a, end_b = len(ta), len(tb)
    n = min(end_a, end_b)
    raw_a = g0.load_raw(a.a_raw, n) if a.a_raw else None
    raw_b = g0.load_raw(a.b_raw, n) if a.b_raw else None

    first_diff = None
    per_field = {}
    diff_ticks = 0
    for t in range(first, n):
        x, y = ta[t], tb[t]
        if x is None or y is None:
            continue
        bad = []
        for k in sorted(set(x) | set(y)):
            vx, vy = x.get(k), y.get(k)
            if k == "gMainCallback":
                vx, vy = name_of(fa, vx), name_of(fb, vy)
            if vx != vy:
                bad.append((k, vx, vy))
        if bad:
            diff_ticks += 1
            for k, _vx, _vy in bad:
                if k not in per_field:
                    per_field[k] = [t, 0]
                per_field[k][1] += 1
            if first_diff is None:
                first_diff = (t, [field_detail(k, vx, vy, raw_a, raw_b, t) for k, vx, vy in bad],
                              [k for k, _vx, _vy in bad])

    faults = {}
    for name, log in ((na, a.a_log), (nb, a.b_log)):
        if log:
            faults[name] = g0.first_fault(log) if os.path.exists(log) else "log not there"
    last_a, last_b = end_a - 1, end_b - 1
    want = a.ticks
    short = []
    for name, last, fault in ((na, last_a, faults.get(na)), (nb, last_b, faults.get(nb))):
        if want is not None and last + 1 < want:
            short.append(f"{name} stopped after tick {last} of {want - 1}" + (f" ({fault})" if fault else ""))
        elif fault:
            short.append(f"{name} logged {fault}")

    print(f"sweep_pair: {na} {a.a} ({ha.get('exe_crc32')}), {nb} {a.b} ({hb.get('exe_crc32')})"
          + (" -- the SAME binary" if same_exe else ""))
    print(f"start: {'save state tick ' + str(ra_['tick']) + ' (' + ra_.get('part', '?') + ')' if ra_ else ha.get('sram_in', 'power-on, blank SRAM')}"
          f"; {'lag replay of ' + ha['lag_from'] if ha.get('lag_from') else 'plain run'}; keys {ha.get('keys')}")
    print(f"compared ticks {first}-{n - 1} ({n - first} ticks)")
    if end_a != end_b:
        print(f"  {na} has ticks to {last_a}, {nb} to {last_b}: the shorter run stopped first")
    result = {"a": na, "b": nb, "a_trace": a.a, "b_trace": a.b, "first_tick": first, "last_tick": n - 1,
              "a_last": last_a, "b_last": last_b, "faults": faults, "incomplete": short,
              "differing_ticks": diff_ticks, "first_differing_tick": None, "fields_at_first": [],
              "per_field": {k: {"first_tick": v[0], "ticks": v[1]} for k, v in per_field.items()}}
    if first_diff is not None:
        t, details, keys = first_diff
        result["first_differing_tick"] = t
        result["fields_at_first"] = keys
        result["details_at_first"] = details
        print(f"first differing tick: {t}")
        for d in details:
            print(f"  {d}")
        print("first differing tick of each field (ticks that differ):")
        for k, (ft, cnt) in sorted(per_field.items(), key=lambda kv: (kv[1][0], kv[0])):
            print(f"  {k:22} {ft:8} ({cnt})")
    elif end_a != end_b:
        t = min(end_a, end_b)
        result["first_differing_tick"] = t
        result["fields_at_first"] = ["(run length)"]
        print(f"first differing tick: {t} (one run has it, the other stopped)")
    if result["first_differing_tick"] is not None:
        verdict, status = f"DIFFERENT at tick {result['first_differing_tick']}: {', '.join(result['fields_at_first'][:8])}", 1
    elif short:
        verdict, status = "INCOMPLETE: " + "; ".join(short), 1
    else:
        verdict, status = f"SAME on every field of ticks {first}-{n - 1}", 0
    if short and result["first_differing_tick"] is not None:
        verdict += " (also: " + "; ".join(short) + ")"
    result["verdict"] = verdict
    print(f"UNINIT PAIR ({na} vs {nb}): {verdict}")
    if a.json:
        with open(a.json, "w") as fh:
            json.dump(result, fh, indent=1)
    return status


if __name__ == "__main__":
    sys.exit(main())
