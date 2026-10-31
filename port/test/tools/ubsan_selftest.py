#!/usr/bin/env python3
"""Self-test of the UBSan sweep's runtime (port/test/pc/ubsan_log.c) against
GCC's own libubsan (make -f port/test/sweep.mk ubsan-selftest; README.md,
"Bug sweeps"). Run from the repository root.

port/test/pc/ubsan_selftest.c fails one check of each kind twice. It is
compiled once with the sweep's flags and linked twice: with ubsan_log.c, and
with libubsan (32-bit, lib32ubsan1). Passes when
  - every report of libubsan (each site once) is a site of ubsan_log.c's table
    at the same file:line:column with the same message (up to libubsan's ";"
    and ubsan_log.c's added " (...)"), named in the same function;
  - every site of the table has 2 hits, first tick 7 (the test's
    PortGate0Tick), and a first-hit return address named Function+offset;
  - any further site of the table is at a line:column libubsan reported (one
    check's data serves several kinds, and libubsan reports only the first).
Exit 0 on PASS, 1 on FAIL, 2 when a step cannot run.
"""

import argparse
import json
import os
import re
import subprocess
import sys

CFLAGS = ["-m32", "-O1", "-g", "-std=gnu11", "-fno-pie"]


def sh(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0 and not kw.get("check_ok"):
        print(f"ubsan_selftest: {' '.join(cmd)}: exit {r.returncode}\n{r.stderr[-2000:]}")
        sys.exit(2)
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", default="build/gate0-sweep/ubsan-selftest")
    ap.add_argument("--checks", default="undefined,bounds", help="UBSAN_CHECKS")
    ap.add_argument("--cc", default="gcc")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    o = a.out
    san = [f"-fsanitize={a.checks}", "-fsanitize-recover=all"]
    sh([a.cc] + CFLAGS + san + ["-c", "port/test/pc/ubsan_selftest.c", "-o", f"{o}/t.o"])
    sh([a.cc] + CFLAGS + ["-fno-sanitize=all", "-Iport/include", "-c", "port/test/pc/ubsan_log.c", "-o", f"{o}/u.o"])
    sh([a.cc, "-m32", "-no-pie", f"{o}/t.o", f"{o}/u.o", "-o", f"{o}/mine"])
    sh([a.cc, "-m32", "-no-pie"] + san + [f"{o}/t.o", "-o", f"{o}/lib"])

    env = dict(os.environ, UBSAN_OPTIONS="print_summary=0:halt_on_error=0:print_stacktrace=1")
    lib = subprocess.run([f"{o}/lib"], capture_output=True, text=True, env=env)
    for p in (f"{o}/u.jsonl", f"{o}/u.txt"):
        if os.path.exists(p):
            os.remove(p)
    mine = subprocess.run([f"{o}/mine"], capture_output=True, text=True,
                          env=dict(os.environ, FE8PC_UBSAN_OUT=f"{o}/u.jsonl"))
    if lib.returncode != 0 or mine.returncode != 0:
        print(f"ubsan_selftest: the test program stopped (libubsan exit {lib.returncode}, ubsan_log.c exit "
              f"{mine.returncode})")
        return 2

    # libubsan: "FILE:L:C: runtime error: MSG", then with print_stacktrace "#0 0x... in FUNC FILE:L".
    lib_sites = []
    lines = lib.stderr.splitlines()
    for i, line in enumerate(lines):
        m = re.match(r"^(.*?):(\d+):(\d+): runtime error: (.*)$", line)
        if m:
            func = None
            for nxt in lines[i + 1:i + 4]:
                f = re.match(r"^\s*#0 0x[0-9a-f]+ in (\S+)", nxt)
                if f:
                    func = f.group(1)
                    break
            lib_sites.append((int(m.group(2)), int(m.group(3)), m.group(4), func))
    with open(f"{o}/u.jsonl") as fh:
        rows = [json.loads(line) for line in fh if line.strip()]
    head, table = rows[0], rows[1:]

    # Addresses differ between the two programs (data and stack): compared as 0x?.
    def norm_lib(msg):
        return re.sub(r"0x[0-9a-f]+", "0x?", msg.split(";")[0].strip())

    def norm_mine(msg):
        return re.sub(r"0x[0-9a-f]+", "0x?", re.sub(r" \([^()]*\)$", "", msg).strip())

    problems, matched = [], set()
    for line, col, msg, func in lib_sites:
        cands = [r for r in table if r["line"] == line and r["col"] == col and norm_mine(r["msg"]) == norm_lib(msg)]
        if not cands:
            problems.append(f"libubsan reported :{line}:{col} {msg!r}; ubsan_log.c has no such site")
            continue
        r = cands[0]
        matched.add(id(r))
        where = (r.get("where") or ["?"])[0]
        if func and not where.startswith(func + "+"):
            problems.append(f":{line}:{col}: libubsan names {func}, ubsan_log.c {where}")
    lib_places = {(line, col) for line, col, _m, _f in lib_sites}
    extra = []
    for r in table:
        if r["count"] != 2 or r["first_tick"] != 7:
            problems.append(f":{r['line']}:{r['col']} {r['check']}: {r['count']} hits, first tick {r['first_tick']} "
                            f"(want 2 hits, tick 7)")
        if not re.match(r"^[A-Za-z_]\w*\+0x[0-9a-f]+$", (r.get("where") or ["?"])[0]):
            problems.append(f":{r['line']}:{r['col']}: first hit not named (where {r.get('where')})")
        if id(r) not in matched:
            if (r["line"], r["col"]) in lib_places:
                extra.append(f":{r['line']}:{r['col']} {r['check']}: {r['msg']}")
            else:
                problems.append(f"ubsan_log.c site :{r['line']}:{r['col']} {r['check']} {r['msg']!r} was not "
                                f"reported by libubsan")
    print(f"ubsan_selftest: libubsan {len(lib_sites)} reports, ubsan_log.c {len(table)} sites "
          f"({head.get('hits')} hits)")
    for e in extra:
        print(f"  also counted (libubsan reports one kind per check): {e}")
    for p in problems:
        print(f"  FAIL {p}")
    if len(lib_sites) < 15:
        problems.append("fewer reports than the test makes")
        print(f"  FAIL libubsan made only {len(lib_sites)} reports")
    print("UBSAN SELFTEST: " + ("PASS" if not problems else "FAIL"))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
