#!/usr/bin/env python3
"""Run the Gate 0 routes with the BIOS-area/NULL-page probe and list every site.

    python3 port/tools/nullprobe.py run [--routes ch0 ch1 ...] [--out DIR]
                                        [--gba-trace GBA.jsonl [--lag-replay]] [--env NAME=VALUE ...]
    python3 port/tools/nullprobe.py check [--out DIR] [--gba-trace GBA.jsonl]
    python3 port/tools/nullprobe.py report [--out DIR] [--routes NAME ...]

`make -f port/test/Makefile nullprobe` builds the Gate 0 PC binary and runs both
(README.md, "NULL probe").

run: for each route (port/test/inputs/README-routes.md), runs the Gate 0 PC
binary (build/pc-gate0/fe8pc) over the route's key file to its last tick with
FE8PC_NULLPROBE=1 (port/hw/nullprobe.c): every read of the GBA BIOS area or the
NULL page returns the open-bus latch and the run goes on, and every site is
recorded. Writes DIR/<route>/pc.log, pc.jsonl (the PC Gate 0 trace), sites.jsonl
(the probe's table) and check.json:
  - the key file's own milestones (chapter, turn, phase, gRNSeeds and
    gLCGRNValue at the end of each milestone tick, from the GBA) against the PC
    trace;
  - with --gba-trace (a GBA Gate 0 trace of the same keys or of a longer route,
    whose keys start with these), the first tick where the game state differs:
    gRNSeeds, gLCGRNValue, gPlaySt without its two clock copies (+0x00-0x07)
    or sKeyStatusBuffer without its frame counter (+0x12).
Both show how long the PC stayed on the GBA's route: the routes are timed to
the tick on the GBA, and the PC has no lag frames (PLAN.md 3.4), so a GBA lag
effect can put the PC off the route (README-routes.md, "Limits"). A site first
seen after that tick may not be on the GBA's path.

--lag-replay (with --gba-trace) also sets GATE0_LAG_FROM for the PC run, so the
Gate 0 binary runs the GBA's lag VBlanks where they came (the lag replay of
port/test/README.md, "Lag replay": a replay in the test harness, never in the
game). It needs a GBA trace in format fe8-gate0-trace-4 (with "events" and
"lag_at"); the run log says whether the lag replay ran. --env passes any other
variable to the PC run.

check: redoes those checks for the runs already in DIR (no PC run).

A route of 60,000 ticks or more runs under the compare lock (~/.fe8-compare.lock
or $FE8_COMPARE_LOCK, port-plan/DECISIONS.md test throughput; an inherited lock
is reused, as in check_all.py lock).

report: merges the routes' tables, deduplicated by call site (the faulting
instruction and its caller), symbolises them with addr2line and objdump, marks
the sites that an existing port-plan/DIVERGENCES.md entry covers or relates to,
and writes DIR/report.md and DIR/report.json. --routes names the run
directories under DIR (default the bot routes above; any other name works, for
a table written by FE8PC_NULLPROBE_OUT=DIR/NAME/sites.jsonl in another run).

The probe also lets the run go past integer division traps (x86 div/idiv by
zero or INT_MIN / -1, where the GBA's libgcc routines return 0, or INT_MIN with
remainder 0): such sites are listed with how "div0" or "div-overflow".

The probe changes the run only where the PC would otherwise fault; it is a
diagnostic, not a fix.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_all  # noqa: E402  (take_lock, COMPARE_LOCK)

EXE = "build/pc-gate0/fe8pc"
OUT = "build/nullprobe"
ROUTES = ["ch0", "ch1", "ch2", "ch3", "ch4", "ch5"]
HEAVY_TICKS = 60000  # runs of this many ticks or more take the compare lock
LATCH = {"swi": 0xE3A02004, "hblank": 0xE55EC002}  # PORT_OPEN_BUS_SWI/IRQ, port/include/port_gbabus.h

# Functions that an existing port-plan/DIVERGENCES.md entry guards or names.
# A read site in one of them is a path the entry's guard does not cover.
KNOWN = {
    "IsGameNotFirstChapter": "D-2",
    "SaveMenuInitSaveSlotData": "D-2",
    "EndFace": "D-3",
    "SetFaceBlinkControl": "D-4",
    "FaceBlinkProc_GenBlinkInterval": "D-4",
    "IsGmapRmBorder1ActiveAtIndex": "D-5",
    "GetAnimPosition": "D-7",
    "CpuSet": "D-8",
    "CpuFastSet": "D-8",
    "TerrainDisplay_Loop_OnSideChange": "D-9",
    "UnitMapUiUpdate": "D-10",
    "GetUnitCurrentHp": "D-10",
    "GetUnitMaxHp": "D-10",
    "SetTalkFaceDisp": "D-13",
    "GetFaceDisplayBits": "D-13",
    "WriteGameSave": "D-14",
    "WriteGameSavePackedUnit": "D-14",
    "SGM_SetCharacterKnown": "D-14",
    "AnimDelete": "H-1",
    "SyncHiOam": "H-2",
    "ClearOam": "H-2",
    "EndGmapRmBorder1": "section 4 latent loops (B-5)",
    "SetGmapRmBorder1FlagForIndex": "section 4 latent loops (B-5)",
    "EndWmPlaceDotByIndex": "section 4 latent loops (B-5)",
    "IsWmPlaceDotActiveAtIndex": "section 4 latent loops (B-5)",
    "SetWmPlaceDotFlagForIndex": "section 4 latent loops (B-5)",
    "chk_adr_r2": "D-20",
}


def route_keys(route):
    return f"port/test/inputs/{'route_ch0_prologue' if route == 'ch0' else 'route_' + route}.keys"


def key_ticks(path):
    n = 0
    with open(path) as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if line:
                m = re.fullmatch(r"([0-9A-Fa-f]+)(?:\*(\d+))?", line)
                if not m:
                    sys.exit(f"{path}: bad entry {line!r}")
                n += int(m.group(2) or 1)
    return n


MILESTONE_RE = re.compile(r"^#\s+tick (\d+): (.*) \(chapterIndex (-?\d+), turn (\d+), phase ([0-9a-f]{2}), "
                          r"gRNSeeds ([0-9a-f]{4}) ([0-9a-f]{4}) ([0-9a-f]{4}), gLCGRNValue ([0-9a-f]{8})\)")


def key_milestones(path):
    out = []
    with open(path) as fh:
        for line in fh:
            m = MILESTONE_RE.match(line)
            if m:
                out.append({"tick": int(m.group(1)), "what": m.group(2), "chapter": int(m.group(3)),
                            "turn": int(m.group(4)), "phase": m.group(5),
                            "rng": f"{m.group(6)} {m.group(7)} {m.group(8)}", "lcg": m.group(9)})
    return out


def state(d):
    """chapter, turn, phase, gRNSeeds and gLCGRNValue of one trace line, as the milestones give them."""
    ps = bytes.fromhex(d["gPlaySt"])
    rn = bytes.fromhex(d["gRNSeeds"])
    ch = ps[0x0E] - 0x100 if ps[0x0E] >= 0x80 else ps[0x0E]
    return {"chapter": ch, "turn": ps[0x10] | ps[0x11] << 8, "phase": f"{ps[0x0F]:02x}",
            "rng": " ".join(f"{rn[i] | rn[i + 1] << 8:04x}" for i in (0, 2, 4)), "lcg": d["gLCGRNValue"]}


def logic_key(d):
    """The game-state fields compared with the GBA trace (see the module doc). sKeyStatusBuffer
    leaves out +0x12 (TimeSinceStartSelect), a frame counter that, like the clock, counts lag."""
    return (d["gRNSeeds"], d["gLCGRNValue"], d["gPlaySt"][16:], d["sKeyStatusBuffer"][:36])


def read_trace(path, ticks=None, keep=None):
    """{tick: line dict} for the ticks in `ticks` (all when None); keep(d) selects what is stored."""
    out = {}
    with open(path, errors="replace") as fh:
        for line in fh:
            if not line.startswith('{"tick"'):
                continue
            t = int(line[8:line.index(",")])
            if ticks is not None and t not in ticks:
                continue
            try:
                d = json.loads(line)
            except ValueError:
                break  # a line cut by a fault
            out[t] = keep(d) if keep else d
    return out


def check_route(keys, trace, gba):
    want = key_milestones(keys)
    pc_all = read_trace(trace, keep=lambda d: {"state": state(d), "logic": logic_key(d)})
    rows, first_bad = [], None
    for m in want:
        pc = pc_all.get(m["tick"], {}).get("state")
        ok = pc is not None and all(pc[k] == m[k] for k in ("chapter", "turn", "phase", "rng", "lcg"))
        rows.append({"tick": m["tick"], "what": m["what"], "gba": {k: m[k] for k in
                     ("chapter", "turn", "phase", "rng", "lcg")}, "pc": pc, "equal": ok})
        if not ok and first_bad is None:
            first_bad = m["tick"]
    res = {"pc_ticks": len(pc_all), "milestones": len(rows), "milestones_equal": sum(r["equal"] for r in rows),
           "first_milestone_not_equal": first_bad, "milestone_rows": rows}
    if gba is not None:
        compared, diff = 0, []
        for t in sorted(pc_all):
            if t not in gba:
                break
            compared += 1
            if pc_all[t]["logic"] != gba[t]:
                diff.append(t)
        res["gba_ticks_compared"] = compared
        res["first_logic_diff"] = diff[0] if diff else None
        res["logic_diff_count"] = len(diff)
        res["logic_diff_ticks"] = diff[:5000]
    return res


def run_line(route, res, gba_compared):
    line = (f"[nullprobe] {route}: exit {res.get('exit')} in {res.get('seconds', 0):.0f} s; PC "
            f"{res['pc_ticks']:,}/{res['ticks']:,} ticks; {res.get('sites', '?')} sites, {res.get('hits', '?')} hits "
            f"({res.get('end') or 'no table'}); milestones {res['milestones_equal']}/{res['milestones']} equal"
            + (f" (first not equal: tick {res['first_milestone_not_equal']:,})"
               if res["first_milestone_not_equal"] is not None else ""))
    if gba_compared:
        n = res["logic_diff_count"]
        line += (f"; game state equal to the GBA on all {res['gba_ticks_compared']:,} ticks" if n == 0 else
                 f"; game state differs from the GBA on {n:,} of {res['gba_ticks_compared']:,} ticks "
                 f"(first {res['first_logic_diff']:,})")
    return line


def cmd_run(a):
    if not os.path.exists(a.exe):
        sys.exit(f"nullprobe: {a.exe} is missing; build it with make -f port/test/Makefile pc")
    if a.lag_replay and not a.gba_trace:
        sys.exit("nullprobe: --lag-replay needs --gba-trace")
    extra = {}
    for spec in a.env:
        name, eq, value = spec.partition("=")
        if not eq:
            sys.exit(f"nullprobe: --env {spec!r}: expected NAME=VALUE")
        extra[name] = value
    if a.lag_replay:
        extra.update(GATE0_LAG_FROM=os.path.abspath(a.gba_trace))
    gba = None
    if a.gba_trace:
        print(f"[nullprobe] reading the GBA trace {a.gba_trace} ...", flush=True)
        gba = read_trace(a.gba_trace, keep=logic_key)
    os.makedirs(a.out, exist_ok=True)
    summary = []
    for route in a.routes:
        keys = route_keys(route)
        ticks = key_ticks(keys)
        d = os.path.join(a.out, route)
        os.makedirs(d, exist_ok=True)
        for f in ("pc.log", "pc.jsonl", "sites.jsonl", "check.json"):
            try:
                os.remove(os.path.join(d, f))
            except OSError:
                pass
        env = {k: v for k, v in os.environ.items() if not k.startswith(("GATE0_", "FE8PC_"))}
        env.update(GATE0_KEYS=keys, GATE0_TICKS=str(ticks), GATE0_OUT=os.path.join(d, "pc.jsonl"),
                   FE8PC_BLEND="mgba8", FE8PC_FRAMES="0", FE8PC_NULLPROBE="1",
                   FE8PC_NULLPROBE_OUT=os.path.join(d, "sites.jsonl"), FE8PC_NULLPROBE_ROUTE=route)
        env.update(extra)
        cmd = ["timeout", str(a.timeout), a.exe, os.path.join(a.gba_dir, "baserom.gba")]
        lock_fd, close_lock = None, False
        heavy = ticks >= HEAVY_TICKS and not a.no_lock
        print(f"[nullprobe] {route}: {ticks:,} ticks{' (long: taking the compare lock)' if heavy else ''} ...",
              flush=True)
        if heavy:
            lock_fd, close_lock = check_all.take_lock(check_all.COMPARE_LOCK)
        t0 = time.time()
        try:
            with open(os.path.join(d, "pc.log"), "w") as log:
                rc = subprocess.call(cmd, stdout=log, stderr=subprocess.STDOUT, env=env)
        finally:
            if close_lock:
                os.close(lock_fd)
        secs = time.time() - t0
        with open(os.path.join(d, "pc.log"), errors="replace") as fh:
            log = fh.read()
        replays = [l for l in log.split("\n") if l.startswith("[gate0] ") and "replay" in l]
        res = check_route(keys, os.path.join(d, "pc.jsonl"), gba)
        head = {}
        try:
            with open(os.path.join(d, "sites.jsonl")) as fh:
                head = json.loads(fh.readline())
        except (OSError, ValueError):
            pass
        res.update(route=route, ticks=ticks, exit=rc, seconds=round(secs, 1), sites=head.get("sites"),
                   hits=head.get("hits"), end=head.get("end"), env=extra, replays=replays,
                   lag_replay_ran=any("lag replay" in l for l in replays))
        with open(os.path.join(d, "check.json"), "w") as fh:
            json.dump(res, fh, indent=1)
        line = run_line(route, res, gba is not None)
        if a.lag_replay and not res["lag_replay_ran"]:
            line += "; WARNING: this binary ran no lag replay"
        print(line, flush=True)
        summary.append({k: v for k, v in res.items() if k not in ("milestone_rows", "logic_diff_ticks")})
    with open(os.path.join(a.out, "runs.json"), "w") as fh:
        json.dump(summary, fh, indent=1)
    return 0


def cmd_check(a):
    """Redo the milestone and GBA checks of existing runs (no PC run)."""
    gba = read_trace(a.gba_trace, keep=logic_key) if a.gba_trace else None
    summary = []
    for route in ROUTES:
        d = os.path.join(a.out, route)
        try:
            with open(os.path.join(d, "check.json")) as fh:
                old = json.load(fh)
        except (OSError, ValueError):
            continue
        res = check_route(route_keys(route), os.path.join(d, "pc.jsonl"), gba)
        for k in ("route", "ticks", "exit", "seconds", "sites", "hits", "end", "env", "replays", "lag_replay_ran"):
            res[k] = old.get(k)
        with open(os.path.join(d, "check.json"), "w") as fh:
            json.dump(res, fh, indent=1)
        print(run_line(route, res, gba is not None), flush=True)
        summary.append({k: v for k, v in res.items() if k not in ("milestone_rows", "logic_diff_ticks")})
    with open(os.path.join(a.out, "runs.json"), "w") as fh:
        json.dump(summary, fh, indent=1)
    return 0


def symbolise(exe, addrs):
    """{addr: [(function, file:line), ...innermost first]} via addr2line -i."""
    # One address per call: with -i the number of output pairs per address varies.
    res = {}
    for x in sorted(set(addrs)):
        o = subprocess.run(["addr2line", "-f", "-i", "-e", exe, f"0x{x:x}"], capture_output=True,
                           text=True).stdout.strip().split("\n")
        chain = []
        for i in range(0, len(o) - 1, 2):
            loc = re.sub(r"^.*?/(src|port|include)/", r"\1/", o[i + 1].split(" ")[0])
            chain.append((o[i], loc))
        res[x] = chain
    return res


def disasm(exe, eip):
    o = subprocess.run(["objdump", "-d", "--no-show-raw-insn", f"--start-address=0x{eip:x}",
                        f"--stop-address=0x{eip + 15:x}", exe], capture_output=True, text=True).stdout
    for line in o.split("\n"):
        m = re.match(rf"\s*{eip:x}:\s+(.*)$", line)
        if m:
            return re.sub(r"\s+", " ", m.group(1)).strip()
    return ""


def access_width(insn):
    """Bytes the instruction reads from memory, from the AT&T mnemonic (None if unknown)."""
    if not insn:
        return None
    mn = insn.split(" ")[0]
    ops = insn[len(mn):].strip()
    if mn.startswith(("movzb", "movsb")):
        return 1
    if mn.startswith(("movzw", "movsw")):
        return 2
    if mn.startswith(("rep", "movs", "lods", "cmps", "scas", "stos")) or mn.startswith(("f", "call", "jmp")):
        return None
    m = re.match(r"^([a-z]+?)([bwl])$", mn)
    if m and m.group(1) in ("mov", "cmp", "test", "add", "sub", "and", "or", "xor", "adc", "sbb", "inc", "dec",
                            "push", "imul", "mul", "neg", "not", "shl", "shr", "sar", "bt"):
        return {"b": 1, "w": 2, "l": 4}[m.group(2)]
    for r in re.findall(r"%([a-z]+)", ops)[::-1]:
        if r in ("eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp"):
            return 4
        if r in ("ax", "bx", "cx", "dx", "si", "di", "bp", "sp"):
            return 2
        if r in ("al", "bl", "cl", "dl", "ah", "bh", "ch", "dh"):
            return 1
    return None


def lanes(latch, addr, width):
    return sum(((latch >> (8 * ((addr + i) & 3))) & 0xFF) << (8 * i) for i in range(width))


DIV_HOWS = ("div0", "div-overflow")  # integer division traps (nullprobe.c PortNullProbe_OnDivFault)


def cmd_report(a):
    tables = []
    for route in a.routes:
        p = os.path.join(a.out, route, "sites.jsonl")
        if not os.path.exists(p):
            continue
        with open(p) as fh:
            lines = [json.loads(l) for l in fh if l.strip()]
        if lines:
            tables.append((route, lines[0], lines[1:]))
    if not tables:
        sys.exit(f"nullprobe report: no sites.jsonl under {a.out}")
    runs, diffs = {}, {}
    try:
        with open(os.path.join(a.out, "runs.json")) as fh:
            runs = {r["route"]: r for r in json.load(fh)}
    except (OSError, ValueError):
        pass
    for route, _, _ in tables:
        try:
            with open(os.path.join(a.out, route, "check.json")) as fh:
                c = json.load(fh)
            if "logic_diff_ticks" in c:
                diffs[route] = set(c["logic_diff_ticks"])
        except (OSError, ValueError):
            pass
    merged = {}
    for route, head, sites in tables:
        for s in sites:
            key = (s["eip"], s["callers"][0] if s["callers"] else "", s["how"])
            m = merged.get(key)
            if m is None:
                m = merged[key] = dict(s, routes={}, first_route=route)
                m.pop("route", None)
            else:
                m["addr_min"] = min(m["addr_min"], s["addr_min"], key=lambda v: int(v, 16))
                m["addr_max"] = max(m["addr_max"], s["addr_max"], key=lambda v: int(v, 16))
                m["reads"] = max(m["reads"], s["reads"])
                m["writes"] = max(m["writes"], s["writes"])
                m["aliased_hits"] = max(m["aliased_hits"], s["aliased_hits"])
            m["routes"][route] = {"hits": s["hits"], "first_tick": s["first_tick"], "last_tick": s["last_tick"],
                                  "rule_hits": s["rule_hits"]}
    addrs = set()
    for (eip, caller, _), m in merged.items():
        addrs.add(int(eip, 16))
        for c in m["callers"]:
            addrs.add(int(c, 16) - 1)  # return address -> the call
    sym = symbolise(a.exe, addrs)
    rows = []
    for key, m in merged.items():
        eip = int(m["eip"], 16)
        chain = sym.get(eip, [])
        inner = chain[0] if chain else ("??", "")
        outer = chain[-1] if chain else ("??", "")
        callers = []
        for c in m["callers"]:
            cc = sym.get(int(c, 16) - 1, [])
            callers.append({"ret": c, "func": cc[-1][0] if cc else "??", "loc": cc[-1][1] if cc else "",
                            "inline": [f"{f} ({l})" for f, l in cc[:-1]]})
        insn = disasm(a.exe, eip)
        width = access_width(insn) if m["how"] not in ("h1-drop",) + DIV_HOWS else None
        first = int(m["addr_first"], 16)
        rule = m["rule_first"]
        latch = LATCH.get(rule, LATCH["swi"])
        if m["how"] in DIV_HOWS:
            value = ""  # the note has the dividend, divisor and the GBA's results
        elif rule == "mirror":
            value = f"mirror of 0x{m['latch_first']}"
        else:
            value = f"0x{lanes(latch, first, width):0{2 * width}X}" if width and m["reads"] else ""
        funcs = [f for f, _ in chain] + [c["func"] for c in callers[:3]]
        known = sorted({KNOWN[f] for f in funcs if f in KNOWN})
        if m["how"] == "h1-drop":
            status = "handled: H-1 (write dropped)"
        elif m["how"] == "unsupported":
            status = "probe could not handle it (the run stopped here)"
        elif m["how"] in DIV_HOWS:
            status = ("new: integer division trap (" + ("by zero" if m["how"] == "div0" else "INT_MIN / -1") +
                      "; the GBA's libgcc result was used)")
        elif known:
            status = "related: " + ", ".join(known) + " (outside its guard)"
        elif m["how"] == "mirror":
            status = "new (GBA memory mirror, not open bus)"
        else:
            status = "new"
        all_rules = {}
        for r in m["routes"].values():
            for k, v in r["rule_hits"].items():
                all_rules[k] = all_rules.get(k, 0) + v
        first_route = m["first_route"]
        first_tick = m["routes"][first_route]["first_tick"]
        # On the GBA's path: the game state equals the GBA trace at the end of the tick before
        # and of the tick itself (None without a GBA compare).
        dset = diffs.get(first_route)
        on_route = None if dset is None else (first_tick - 1 not in dset and first_tick not in dset)
        rows.append({
            "eip": m["eip"], "function": outer[0], "location": outer[1],
            "inlined": [f"{f} ({l})" for f, l in chain[:-1]], "innermost": f"{inner[0]} ({inner[1]})",
            "insn": insn, "how": m["how"], "rw": "W" if m["writes"] and not m["reads"] else
            ("R" if m["reads"] and not m["writes"] else "RW"),
            "addr_first": m["addr_first"], "addr_min": m["addr_min"], "addr_max": m["addr_max"],
            "width": width, "value_first": value, "latch_first": m["latch_first"], "rule_first": rule,
            "rule_hits": {k: v for k, v in all_rules.items() if v},
            "first_route": first_route, "first_tick": first_tick, "on_gba_route": on_route,
            "routes": m["routes"], "callers": callers, "aliased_hits": m["aliased_hits"], "note": m["note"],
            "status": status, "known": known,
        })
    order = {r: i for i, r in enumerate(a.routes)}
    rows.sort(key=lambda r: (order.get(r["first_route"], 99), r["first_tick"], r["eip"]))
    for i, r in enumerate(rows, 1):
        r["id"] = f"N-{i}"
    with open(os.path.join(a.out, "report.json"), "w") as fh:
        json.dump({"runs": [runs.get(r) for r, _, _ in tables], "heads": {r: h for r, h, _ in tables},
                   "sites": rows}, fh, indent=1)
    md = ["# BIOS-area / NULL-page probe: sites", "",
          f"Binary `{a.exe}`; tables from {', '.join(r for r, _, _ in tables)}. One row per call site (faulting "
          "instruction + caller), listed under the shortest route that reaches it. Value: the bytes the read "
          "got, from the first hit's latch and address. Integer division traps (how div0 or div-overflow) have "
          "the dividend as the address and the operands and the GBA's results in the note.", "",
          "## Runs", "", "| Route | Ticks | PC ticks | End | Sites | Hits | Milestones equal | Game state vs the "
          "GBA trace | Replays |", "|---|---|---|---|---|---|---|---|---|"]
    for route, head, _ in tables:
        r = runs.get(route, {})
        nd = r.get("logic_diff_count")
        logic = ("not compared" if "gba_ticks_compared" not in r else
                 f"equal on all {r['gba_ticks_compared']:,}" if not nd else
                 f"differs on {nd:,} of {r['gba_ticks_compared']:,} (first {r['first_logic_diff']:,})")
        ms = (f"{r.get('milestones_equal')}/{r.get('milestones')}" +
              (f" (first not equal {r['first_milestone_not_equal']:,})" if r.get("first_milestone_not_equal")
               is not None else "")) if r else "?"
        md.append(f"| {route} | {r.get('ticks', 0):,} | {r.get('pc_ticks', 0):,} | {head.get('end')} | "
                  f"{head.get('sites')} | {head.get('hits')} | {ms} | {logic} | "
                  f"{'; '.join(x.replace('[gate0] ', '') for x in r.get('replays', [])) or 'none'} |")
    md += ["", "## Sites", "", "| ID | Function (file:line) | Instruction | R/W | Address | Value (first) | "
           "First tick (route) | On the GBA route | Hits per route | Latch rule (hits) | Caller | Status |",
           "|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        addr = r["addr_first"] if r["addr_min"] == r["addr_max"] else f"{r['addr_min']}-{r['addr_max']}"
        hits = ", ".join(f"{k} {v['hits']}" for k, v in r["routes"].items())
        rules = ", ".join(f"{k} {v}" for k, v in r["rule_hits"].items())
        caller = r["callers"][0] if r["callers"] else None
        cal = f"{caller['func']} ({caller['loc']})" if caller else "?"
        inl = (" [inlined: " + "; ".join(r["inlined"]) + "]") if r["inlined"] else ""
        onr = {True: "yes", False: "no (state differs there)", None: "not compared"}[r["on_gba_route"]]
        md.append(f"| {r['id']} | {r['function']} ({r['location']}){inl} | `{r['insn']}` | {r['rw']} | {addr} | "
                  f"{r['value_first'] or '-'} | {r['first_tick']:,} ({r['first_route']}) | {onr} | {hits} | {rules} | "
                  f"{cal} | {r['status']}{'; ' + r['note'] if r['note'] else ''} |")
    md += ["", "Callers (return addresses, innermost first):", ""]
    for r in rows:
        md.append(f"- {r['id']} `{r['eip']}`: " + " <- ".join(f"{c['func']} ({c['loc']})" for c in r["callers"]))
    with open(os.path.join(a.out, "report.md"), "w") as fh:
        fh.write("\n".join(md) + "\n")
    print("\n".join(md))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("--routes", nargs="+", default=ROUTES, choices=ROUTES)
    r.add_argument("--exe", default=EXE)
    r.add_argument("--out", default=OUT)
    r.add_argument("--gba-dir", default=".")
    r.add_argument("--timeout", type=int, default=3600)
    r.add_argument("--gba-trace", help="GBA Gate 0 trace of these keys (or a longer route) to compare with")
    r.add_argument("--lag-replay", action="store_true",
                   help="also pass the GBA trace (format 3) as GATE0_LAG_FROM, the lag replay")
    r.add_argument("--env", action="append", default=[], help="NAME=VALUE for the PC run")
    r.add_argument("--no-lock", action="store_true", help="do not take the compare lock (the caller holds it)")
    c = sub.add_parser("check")
    c.add_argument("--out", default=OUT)
    c.add_argument("--gba-trace")
    p = sub.add_parser("report")
    p.add_argument("--exe", default=EXE)
    p.add_argument("--out", default=OUT)
    p.add_argument("--routes", nargs="+", default=ROUTES,
                   help="the run directories under --out to merge, shortest route first (default: the bot routes; "
                        "any name, such as a TAS route's run made with make route and FE8PC_NULLPROBE_OUT)")
    a = ap.parse_args()
    return {"run": cmd_run, "check": cmd_check, "report": cmd_report}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
