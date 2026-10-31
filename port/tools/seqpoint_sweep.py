#!/usr/bin/env python3
"""GCC -Wsequence-point over every game C file of the FULL PC build, judged against an accept list.

An expression that changes a variable and reads it again with no sequence point between
(`*trap++ = *(trap + 1)`, `x = ++x & 0x3F`) is undefined C, and agbcc and the host GCC can
evaluate it in different orders: round 10's C10-10 (a), `RemoveTrap`'s `*trap++ = *(trap + 1)`
(src/bmtrick.c), where agbcc reads the next entry before it moves the pointer and the host GCC
moved it first, so each entry took the one two places on (port-plan DIVERGENCES D-72, its working
ID D-R10-FRAMES-1). The argument-order sweep (argorder_sweep.py, D-52) does not model this kind.

The game files are compiled with -w (port/Makefile HOST_CFLAGS), so the build's warnings gate
never sees it there; the port's own files are compiled with -Wall, which has -Wsequence-point, so
that gate already covers them. This tool runs the host GCC with the FULL build's flags (the PC
view: PC_PORT defined, so a site fixed under PC_PORT is gone), -w removed, -fsyntax-only and
-Wsequence-point, over port/Makefile's FULL GAME_SRCS (the lvcast copies of LVCAST_SRCS, as the
build compiles them; the per-object flags of hardware.o, m4a.o and bmdifficulty.o included).
Each warning is keyed FILE:FUNCTION:OPERAND (the function from GCC's "In function" line, the
operand from "operation on 'X' may be undefined") and judged in the accept list:

  benign  the two compilers give the same result there (the reason says why); it must still be
          reported (a benign entry that matches no warning is stale)
  fixed   the site is rewritten under PC_PORT with the retail line in #else; it must not be
          reported in the PC view ("FIX MISSING" otherwise)

--check exits 1 on a warning not in the list, a fixed site reported, a stale benign entry, or a
file the compiler refused (a syntax error means that file was not checked). Last line:
"SEQPOINT SWEEP: PASS ..." or "SEQPOINT SWEEP: FAIL ...". check-all's seqpoint row.

usage (from a worktree root, after the GBA make and a FULL PC build, which make the generated
and lvcast sources):  python3 port/tools/seqpoint_sweep.py [--accept port/tools/seqpoint_accept.txt]
                      [--check] [-j N] [--cc gcc]
"""
import argparse
import os
import re
import shlex
import subprocess
import sys
from multiprocessing import Pool

WARN_RE = re.compile(r"^(?P<file>[^:\s]+):(?P<line>\d+):\d+: warning: operation on '(?P<op>[^']+)' may be undefined "
                     r"\[-Wsequence-point\]")
FUNC_RE = re.compile(r"^(?P<file>[^:\s]+): In function '(?P<func>[^']+)':")


def make_vars():
    """FULL build variables from port/Makefile's database (make -pn, nothing run)."""
    env = dict(os.environ, MAKEFLAGS="")
    out = subprocess.run(["make", "-f", "port/Makefile", "-pn", "FULL=1", ".seqpoint-none"], env=env,
                         capture_output=True, text=True).stdout
    want = ("HOST_CFLAGS", "HOST_CPPFLAGS", "GAME_SRCS", "LVCAST_SRCS", "BUILD", "NO_ASM_CPPFLAGS",
            "BMDIFFICULTY_CPPFLAGS")
    vals = {}
    for ln in out.splitlines():
        m = re.match(r"^(\w+) :?= (.*)$", ln)
        if m and m.group(1) in want and m.group(1) not in vals:
            vals[m.group(1)] = m.group(2)
    missing = [k for k in want if k not in vals]
    if missing:
        sys.exit(f"seqpoint_sweep: port/Makefile gives no {', '.join(missing)} (run from a worktree root)")
    return vals


def per_object_flags(vals):
    """port/Makefile's single-object HOST_CPPFLAGS additions ($(BUILD)/src/X.o: HOST_CPPFLAGS += $(VAR))."""
    extra = {}
    with open("port/Makefile") as fh:
        for ln in fh:
            m = re.match(r"^\$\(BUILD\)/(src/\S+)\.o: HOST_CPPFLAGS \+= \$\((\w+)\)\s*$", ln)
            if m:
                extra.setdefault(m.group(1) + ".c", []).extend(shlex.split(vals.get(m.group(2), "")))
    return extra


def one(job):
    path, src, cmd = job
    # LC_ALL=C: GCC quotes names with ' in the C locale (with typographic quotes in a UTF-8 one).
    r = subprocess.run(cmd, capture_output=True, text=True, env=dict(os.environ, LC_ALL="C"))
    return path, src, r.returncode, r.stderr


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--accept", default="port/tools/seqpoint_accept.txt")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("-j", type=int, default=4)
    ap.add_argument("--cc", default="gcc")
    a = ap.parse_args()

    vals = make_vars()
    cflags = [f for f in shlex.split(vals["HOST_CFLAGS"]) if f != "-w"]
    cppflags = shlex.split(vals["HOST_CPPFLAGS"])
    lvcast = set(vals["LVCAST_SRCS"].split())
    extra = per_object_flags(vals)
    build = vals["BUILD"]
    jobs = []
    for path in vals["GAME_SRCS"].split():
        if not path.endswith(".c"):
            continue
        src, more = path, []
        if path in lvcast:
            src = os.path.join(build, "lvcast", os.path.relpath(path, "src"))
            more = ["-iquote", "src"]
        if not os.path.exists(src):
            sys.exit(f"seqpoint_sweep: {src} is not there (run the GBA make and make -f port/Makefile FULL=1 first)")
        jobs.append((path, src, [a.cc] + cflags + cppflags + extra.get(path, []) + more
                     + ["-fsyntax-only", "-Wsequence-point", src]))

    found = {}            # key -> [path:line]
    refused = []
    with Pool(max(1, a.j)) as pool:
        for path, src, rc, err in pool.imap_unordered(one, jobs):
            if rc != 0:
                refused.append((path, err.strip().splitlines()[-1:] or [f"exit {rc}"]))
            func = "-"
            for ln in err.splitlines():
                m = FUNC_RE.match(ln)
                if m:
                    func = m.group("func")
                    continue
                m = WARN_RE.match(ln)
                if m:
                    key = f"{m.group('file')}:{func}:{m.group('op')}"
                    found.setdefault(key, []).append(f"{m.group('file')}:{m.group('line')}")

    accept = {}
    if os.path.exists(a.accept):
        with open(a.accept) as fh:
            for n, ln in enumerate(fh, 1):
                ln = ln.strip()
                if not ln or ln.startswith("#"):
                    continue
                parts = ln.split(None, 2)
                if len(parts) < 3 or parts[1] not in ("benign", "fixed"):
                    sys.exit(f"seqpoint_sweep: {a.accept}:{n}: want KEY benign|fixed reason")
                accept[parts[0]] = parts[1]
    elif a.check:
        sys.exit(f"seqpoint_sweep: no accept list {a.accept}")

    new = sorted(k for k in found if k not in accept)
    fix_missing = sorted(k for k in found if accept.get(k) == "fixed")
    stale = sorted(k for k, v in accept.items() if v == "benign" and k not in found)
    n_warn = sum(len(v) for v in found.values())
    print(f"seqpoint_sweep: {len(jobs)} game files (PC view), {n_warn} -Wsequence-point warnings at "
          f"{len(found)} sites; accept list {len(accept)} entries "
          f"({sum(1 for v in accept.values() if v == 'benign')} benign, "
          f"{sum(1 for v in accept.values() if v == 'fixed')} fixed)")
    for k in sorted(found):
        print(f"  {k} ({', '.join(found[k])}): {accept.get(k, 'NOT IN THE ACCEPT LIST')}")
    for k in sorted(k for k, v in accept.items() if v == "fixed" and k not in found):
        print(f"  {k}: fixed, gone from the PC view")
    for k in new:
        print(f"  NEW: {k}")
    for k in fix_missing:
        print(f"  FIX MISSING: {k} (a fixed site is reported again)")
    for k in stale:
        print(f"  STALE: {k} (a benign entry that matches no warning)")
    for path, last in refused:
        print(f"  REFUSED: {path}: {last[0][:200]}")
    ok = not new and not fix_missing and not stale and not refused
    print(f"SEQPOINT SWEEP: {'PASS' if ok else 'FAIL'} ({len(found)} sites, {len(new)} new, "
          f"{len(fix_missing)} fix missing, {len(stale)} stale, {len(refused)} files refused)")
    return 0 if ok or not a.check else 1


if __name__ == "__main__":
    sys.exit(main())
