#!/usr/bin/env python3
"""Row runner and summary table for port/check-all.mk (make -f port/check-all.mk).

    check_all.py --dir DIR run ID TITLE [options] -- COMMAND [ARG...]
    check_all.py --dir DIR table ID...
    check_all.py keyticks KEYFILE
    check_all.py lock LOCKFILE -- COMMAND [ARG...]

run: runs COMMAND with its output in DIR/ID.log and writes the row's result
(PASS, FAIL or SKIP, a short note, the time) to DIR/rows/ID.json. It always
exits 0, so make goes on to the next check; `table` prints every row in the
given order and exits 1 if any row failed or is missing ("PASS (N skipped)"
when rows were skipped and none failed).

  --needs ID        SKIP unless row ID passed in this run (it is a prerequisite)
  --skip-if-pc-stopped ID
                    SKIP when the Gate 0 row ID (a route whose key file is a
                    prefix of this one's) saw the PC stop before its end: the
                    PC replay is deterministic, so it stops there again
  --skip-unless-cmd PROGRAM
                    SKIP when PROGRAM is not on PATH (an optional toolchain)
  --skip-unless-path PATH
                    SKIP when PATH does not exist (an optional library, such
                    as SDL3 for MinGW)
  --skip-unless-windows
                    SKIP unless Windows programs can run from here (WSL with
                    its Windows interop on: a .exe then runs like a Linux one)
  --require REGEX   FAIL unless some line of the output matches
  --reject REGEX    FAIL if some line of the output matches
  --note REGEX[=>FORMAT]
                    the last matching line goes in the note: its first group,
                    or FORMAT with {0} the match and {1}, {2}... its groups
  --note-all REGEX[=>FORMAT]
                    the same for every matching line, joined with " / "
  --count REGEX=LABEL
                    the note gives the number of matching output lines
  --trace-ticks PATH=N
                    FAIL unless the Gate 0 trace PATH (JSON lines) has N ticks
  --gate0 TICKS     a Gate 0 run in build/gate0 (port/test/Makefile): FAIL
                    unless every PC run of the row (the judged trace, and the
                    lag replay and affine replay it made) has all TICKS ticks
                    with no fault in its log, and the compare's verdict is
                    MATCH or LAG-ONLY (DIFFERENT or INCOMPLETE fails, whatever
                    make's status); the note gives the ticks the PC completed,
                    any run's first fault and the verdict; compare.json and
                    the PC logs are kept in DIR/ID/
                    A row that port/test/known_limits.json lists is KNOWN, not
                    FAIL, when its verdict is DIFFERENT only inside that
                    file's windows for it (each limit's fields and ticks, the
                    lag replay's off ticks as "sequence") and every PC run is
                    complete with no fault; the note names each limit, and a
                    passing row whose limit was not seen says so (fixed?)
  --gate0-pc NAME   the PC trace of that run: pc (the plain PC run, default)
                    or pc_lagreplay (the route check: the lag replay compared
                    on its own, make -f port/test/Makefile route)
  --gate0-sram      with --gate0: the route's end SRAM against the GBA's too
                    (port/test/Makefile route-sram: build/gate0/sram_cross.txt,
                    sram_blockdiff.py --cross on gba_end.sav and pc_end.sav):
                    its "SRAM CROSS:" line goes in the note, and a row whose
                    end SRAM differs in a byte that is not a known one, or
                    was not compared, FAILs (a KNOWN row too); the two saves
                    and the report are kept in DIR/ID/
  --warn-dir DIR    compiler warnings gate: every object of the PC build in
                    DIR (DIR/src, DIR/port, romreloc.o, autostubs*.o) keeps its
                    compiler messages in OBJ.warn (port/Makefile); FAIL if any
                    of them has a warning or an object has no log, so a re-run
                    that compiles nothing still sees earlier warnings

COMMAND runs from the current directory with MAKEFLAGS, MFLAGS and MAKELEVEL
removed from its environment, so each sub-make is a plain top-level make with
the -j it is given. GATE0_SRAM_IN is removed too: every row starts from a
blank save, as its baseline does (port/test/README.md, "Chapter saves"); and
so are the save-state variables (GATE0_STATE_*, MGBA_RUN_STATE_IN): every row
starts from power-on (README.md, "Save states"); and GATE0_CHEAT and
ROUTE_CHEAT: a row runs without the test cheats unless its command names them
(README.md, "Test cheats"; check-all.mk passes GATE0_CHEAT for a cheat-assisted
route). An
inherited descriptor of the compare lock
(~/.fe8-compare.lock, or $FE8_COMPARE_LOCK) is passed on to it.

keyticks: the number of ticks in a key file (port/test/README.md, "Input file").

lock: runs COMMAND holding the lock LOCKFILE (flock), and exits with its
status. port/test/Makefile takes ~/.fe8-compare.lock this way around compares
of 60,000 ticks or more, so they take turns across worktrees
(port-plan/DECISIONS.md, test throughput). When the caller already holds that
lock (flock ~/.fe8-compare.lock make ...), its inherited file descriptor is
used as is, so the step does not wait on its own outer lock.
"""

import argparse
import errno
import fcntl
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import textwrap
import time

GATE0_OUT = "build/gate0"
# gate0_compare.py fields that carry game logic rather than video (README.md, "Trace").
LOGIC_FIELDS = ("onmain_calls", "vblank_waits", "gMainCallback", "gRNSeeds", "gLCGRNValue", "gPlaySt",
                "gBmSt", "sKeyStatusBuffer", "gKeyStatusIgnoredSt", "gKeyComboResetEN",
                "gSoftwareResetFlag", "gGameClock")


def fmt_time(s):
    s = int(round(s))
    if s < 60:
        return f"{s}s"
    if s < 3600:
        return f"{s // 60}m{s % 60:02d}s"
    return f"{s // 3600}h{s // 60 % 60:02d}m"


def row_path(d, rid):
    return os.path.join(d, "rows", rid + ".json")


def load_row(d, rid):
    try:
        with open(row_path(d, rid)) as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return None


def save_row(d, row):
    os.makedirs(os.path.join(d, "rows"), exist_ok=True)
    with open(row_path(d, row["id"]), "w") as fh:
        json.dump(row, fh, indent=1)
    note = f"  {row['note']}" if row["note"] else ""
    print(f"[check-all] {row['id']}: {row['status']} ({fmt_time(row['seconds'])}){note}", flush=True)


COMPARE_LOCK = os.environ.get("FE8_COMPARE_LOCK", os.path.expanduser("~/.fe8-compare.lock"))


def inherited_fds(path):
    """Descriptors this process inherited that are open on `path`."""
    real = os.path.realpath(path)
    fds = []
    for name in os.listdir("/proc/self/fd"):
        try:
            if os.readlink(f"/proc/self/fd/{name}") == real:
                fds.append(int(name))
        except OSError:
            continue
    return fds


def take_lock(path):
    """Take the lock `path` (flock). Returns (fd, close_after)."""
    for fd in inherited_fds(path):
        # The same open file description as an outer `flock FILE ...`: this
        # returns at once when that outer flock holds it. Never unlocked here,
        # since that would release the outer lock.
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            continue  # another description of the same file, held elsewhere
        print(f"[check-all] lock {path}: held by the caller (inherited fd {fd})", flush=True)
        return fd, False
    # FE8_COMPARE_SLOTS=N (default 1: one long compare at a time, as before)
    # lets N long compares run at once: slot 0 is `path` itself, slot k > 0 is
    # `path.k`. Each long compare peaks at about 3.5 GB, so set N to what the
    # WSL memory allows (24 GB: 3).
    try:
        slots = max(1, int(os.environ.get("FE8_COMPARE_SLOTS", "1")))
    except ValueError:
        slots = 1
    names = [path] + [f"{path}.{k}" for k in range(1, slots)]
    fds = [os.open(os.path.realpath(n), os.O_RDWR | os.O_CREAT, 0o666) for n in names]
    t = None
    while True:
        for n, fd in zip(names, fds):
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError as e:
                if e.errno not in (errno.EWOULDBLOCK, errno.EAGAIN):
                    raise
                continue
            for other in fds:
                if other != fd:
                    os.close(other)
            if t is not None:
                print(f"[check-all] lock {n} taken after {fmt_time(time.time() - t)}", flush=True)
            return fd, True
        if t is None:
            t = time.time()
            print(f"[check-all] waiting for a compare slot ({slots} in use)", flush=True)
        if slots == 1:
            fcntl.flock(fds[0], fcntl.LOCK_EX)
            print(f"[check-all] lock {path} taken after {fmt_time(time.time() - t)}", flush=True)
            return fds[0], True
        time.sleep(2)


def cmd_lock(a):
    fd, close_after = take_lock(a.lockfile)
    try:
        return subprocess.call(a.command)
    finally:
        if close_after:
            os.close(fd)


def run_logged(cmd, log_path, verbose):
    env = {k: v for k, v in os.environ.items()
           if k not in ("MAKEFLAGS", "MFLAGS", "MAKELEVEL", "GATE0_SRAM_IN", "GATE0_CHEAT", "ROUTE_CHEAT")
           and not k.startswith("GATE0_STATE_") and k != "MGBA_RUN_STATE_IN"}
    with open(log_path, "w") as log:
        log.write("$ " + shlex.join(cmd) + "\n")
        log.flush()
        # An outer `flock ~/.fe8-compare.lock make -f port/check-all.mk` holds the
        # compare lock on an inherited descriptor: pass it on, so the compare
        # step below (check_all.py lock) uses it instead of waiting on it.
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env,
                                text=True, errors="replace", bufsize=1, pass_fds=inherited_fds(COMPARE_LOCK))
        for line in proc.stdout:
            log.write(line)
            if verbose:
                sys.stdout.write(line)
                sys.stdout.flush()
        return proc.wait()


def count_ticks(path):
    n = 0
    try:
        with open(path, errors="replace") as fh:
            for line in fh:
                if line.startswith('{"tick"'):
                    n += 1
    except OSError:
        return None
    return n


def first_line(path, regex):
    try:
        with open(path, errors="replace") as fh:
            for line in fh:
                if re.search(regex, line):
                    return line.strip()
    except OSError:
        pass
    return None


PC_GATE0_EXE = "build/pc-gate0/fe8pc"


def where(eip):
    """FUNCTION (file:line) of a PC code address in the Gate 0 build, or ''."""
    if not shutil.which("addr2line") or not os.path.exists(PC_GATE0_EXE):
        return ""
    try:
        out = subprocess.run(["addr2line", "-f", "-e", PC_GATE0_EXE, eip], capture_output=True, text=True,
                             timeout=30).stdout.split("\n")
    except (OSError, subprocess.SubprocessError):
        return ""
    if len(out) < 2 or out[0] in ("", "??"):
        return ""
    loc = re.sub(r"^.*?/(src|port)/", r"\1/", out[1].split(" ")[0])
    return f"{out[0]} ({loc})"


GATE0_KEEP = ("compare.json", "pc.log", "pc_lagreplay.log", "pc_affine.log", "pc_replay.log",
              "gba_end.sav", "pc_end.sav", "sram_cross.txt")
# Known limits of Gate 0 rows (port/test/known_limits.json; coverage 12's collate): differences that are
# a recorded limit of the test tool (the lag replay's position limits, DIVERGENCES section 5, "Lag check
# v2") or another recorded kind, each with its ID, its kind and the exact fields and ticks it covers.
KNOWN_LIMITS = "port/test/known_limits.json"


def known_for(rid):
    try:
        with open(KNOWN_LIMITS) as fh:
            return json.load(fh).get("rows", {}).get(rid, [])
    except (OSError, ValueError):
        return []


def known_check(cmp, limits):
    """(outside, used): the first differing (field, tick) outside every window of `limits` (or a
    (reason, None) pair when compare.json cannot tell), None when there is none; and the IDs of the
    limits that hold a difference."""
    res = cmp.get("result") or {}
    per = res.get("ticks_per_field")
    if per is None and res.get("first_tick") is not None:
        return ("compare.json has no ticks_per_field", None), set()
    pairs = list((per or {}).items())
    lseq = cmp.get("lag_replay_sequence") or {}
    seq = lseq.get("ticks")
    if seq is None and lseq.get("off_ticks"):
        return ("compare.json has no lag_replay_sequence ticks", None), set()
    if seq:
        pairs.append(("sequence", seq))
    used = set()
    for field, ranges in pairs:
        for a, b in ranges:
            t = a
            while t <= b:
                hit = None
                for lim in limits:
                    for wa, wb in lim.get("windows", {}).get(field, []):
                        if wa <= t <= wb:
                            hit = (lim, wb)
                            break
                    if hit:
                        break
                if not hit:
                    return (field, t), used
                used.add(hit[0]["id"])
                t = min(b, hit[1]) + 1
    return None, used
# The PC runs of a Gate 0 row (port/test/Makefile): trace and log name, the
# name in the note, and the name in make's "fe8pc (...) stopped with status" line.
GATE0_RUNS = (("pc", "PC", ""), ("pc_lagreplay", "PC lag replay", "lag replay"),
              ("pc_affine", "PC affine replay", "affine replay"))


def fault_note(fault, row=None):
    """A [fault] line in short, with its function from addr2line; sets the
    row's pc_fault_where when a row is given."""
    m = re.search(r"signal (\d+) \(([^)]*)\) at EIP=(0x[0-9A-Fa-f]+), fault address (0x[0-9A-Fa-f]+)", fault)
    if not m:
        return fault[:80]
    w = where(m.group(3))
    if row is not None:
        row["pc_fault_where"] = w
    return f"fault: signal {m.group(1)}, address {m.group(4)}" + (f", in {w}" if w else f", EIP {m.group(3)}")


def sram_note(rid=None, row=None):
    """(ok, note) for a route's end SRAM against the GBA's (--gate0-sram; port/test/Makefile route-sram).
    DIFFERENT only in bytes inside the row's known limits' "sram" ranges (KNOWN_LIMITS) is KNOWN."""
    try:
        with open(os.path.join(GATE0_OUT, "sram_cross.txt"), errors="replace") as fh:
            text = fh.read().splitlines()
    except OSError:
        text = []
    line = next((l.strip() for l in text if l.startswith("SRAM CROSS: ")), None)
    if line is None:
        return False, "end SRAM not compared (no sram_cross.txt)"
    verdict = line[len("SRAM CROSS: "):]
    if verdict.startswith(("IDENTICAL", "EQUAL")):
        return True, "end SRAM vs GBA " + verdict
    limits = [lim for lim in known_for(rid) if lim.get("sram")] if rid else []
    nk = []
    for l in text:
        m = re.search(r"(\d+) bytes from (0x[0-9A-Fa-f]+) \[NOT KNOWN\]", l)
        if m:
            a = int(m.group(2), 16)
            nk.extend(range(a, a + int(m.group(1))))
    if not limits or not nk:
        return False, "end SRAM vs GBA " + verdict
    used = set()
    for off in nk:
        lim = next((lim for lim in limits
                    if any(int(a, 16) <= off <= int(b, 16) for a, b in lim["sram"])), None)
        if lim is None:
            return False, f"end SRAM vs GBA {verdict}; 0x{off:04X} outside the known limits ({', '.join(l['id'] for l in limits)})"
        used.add(lim["id"])
    if row is not None:
        row["known"] = sorted(set(row.get("known") or []) | used)
    return True, (f"end SRAM vs GBA KNOWN: DIFFERENT only in {len(nk)} bytes inside "
                  + "; ".join(f"{lim['id']} ({lim['note']})" for lim in limits if lim["id"] in used)
                  + " [" + verdict + "]")


def gate0_note(rid, d, ticks, log_lines, row, pc_name="pc"):
    """(ok, note) for a Gate 0 run of `ticks` ticks; keeps its small outputs in DIR/ID/.
    pc_name: the PC trace judged (pc, or pc_lagreplay for the route check).

    Every PC run the row made (the judged one, and the lag replay and the
    affine replay when their trace or log is there; the stale ones were
    removed before the row) must reach all `ticks` ticks with no [fault] line
    in its log and no "stopped with status" line from make (integration
    review, 2026-10-20: a lag replay that stopped early, or faulted, passed
    when only the judged trace was counted)."""
    keep = os.path.join(d, rid)
    os.makedirs(keep, exist_ok=True)
    for f in GATE0_KEEP:
        src = os.path.join(GATE0_OUT, f)
        if os.path.exists(src):
            shutil.copy(src, os.path.join(keep, f))
    parts = []
    ok = True
    gba = count_ticks(os.path.join(GATE0_OUT, "gba.jsonl"))
    runs = sorted(GATE0_RUNS, key=lambda r: r[0] != pc_name)  # the judged run first
    for name, who, tag in runs:
        trace, log = (os.path.join(GATE0_OUT, name + x) for x in (".jsonl", ".log"))
        main = name == pc_name
        if not main and not os.path.exists(trace) and not os.path.exists(log):
            continue  # this row does not make that run
        n = count_ticks(trace)
        if main:
            row["gate0_ticks"], row["pc_ticks"] = ticks, n
        if n is None:
            parts.append(f"no {who} trace")
            ok = False
        elif main:
            parts.append(f"{who} {n:,}/{ticks:,} ticks")
            ok = ok and n >= ticks
        elif n < ticks:
            parts.append(f"{who} only {n:,}/{ticks:,} ticks")
            ok = False
        fault = first_line(log, r"^\[fault\]")
        if fault:
            ok = False
            if main:
                row["pc_fault"] = fault
            parts.append((f"{who} " if not main else "") + fault_note(fault, row if main else None))
        rx = r"fe8pc " + (rf"\({tag}\) " if tag else "") + "stopped with status"
        stopped = [l for l in log_lines if re.match(rx, l)]
        if stopped:
            ok = False
            if not fault:
                parts.append(stopped[0].split(",")[0].replace("fe8pc ", "PC "))
    if gba is not None and gba < ticks:
        parts.append(f"GBA only {gba:,}")
        ok = False
    try:
        with open(os.path.join(GATE0_OUT, "compare.json")) as fh:
            cmp = json.load(fh)
    except (OSError, ValueError):
        cmp = None
    if cmp is None:
        parts.append("no compare")
        ok = False
    else:
        verdict = cmp.get("verdict", "?").upper()
        # MATCH and LAG-ONLY pass; DIFFERENT, INCOMPLETE (a compared run stopped
        # early or logged a fault) or anything else fails, whatever make said.
        # DIFFERENT only inside the row's known limits (KNOWN_LIMITS) is KNOWN.
        limits = known_for(rid)
        known_note = None
        if verdict == "DIFFERENT" and limits and ok:
            outside, used = known_check(cmp, limits)
            if outside is None:
                row["known"] = sorted(used)
                known_note = "KNOWN: DIFFERENT only inside " + "; ".join(
                    f"{lim['id']} ({lim.get('kind', '')}: " + ", ".join(
                        f"{f} " + "+".join(f"{a:,}-{b:,}" if a != b else f"{a:,}" for a, b in w)
                        for f, w in lim.get("windows", {}).items()) + ")"
                    for lim in limits if lim["id"] in used)
            else:
                known_note = (f"outside the known limits ({', '.join(l['id'] for l in limits)}): "
                              + (outside[0] if outside[1] is None else f"{outside[0]} at {outside[1]:,}"))
                ok = False
        elif verdict not in ("MATCH", "LAG-ONLY"):
            ok = False
        elif limits:
            known_note = "known limit " + ", ".join(l["id"] for l in limits) + " not seen (fixed?)"
        if cmp.get("pc_kind", "pc") != ("lag replay" if pc_name == "pc_lagreplay" else "pc"):
            parts.append(f"compare.json is of a {cmp.get('pc_kind')} trace, not {pc_name}")
            ok = False
        lag = cmp.get("lag_check") or {}
        res = cmp.get("result") or {}
        n = cmp.get("ticks_compared", 0)
        # The verdict on the ticks compared (INCOMPLETE gives it apart).
        compared = (cmp.get("verdict_on_compared_ticks") or "none").upper() if verdict == "INCOMPLETE" else verdict
        head = "GATE 0" if pc_name == "pc" else "lag replay vs GBA"
        if n == 0:
            v = f"{head} {verdict}: no tick compared"
        elif verdict == "INCOMPLETE":
            v = f"{head} INCOMPLETE ({compared} on the {n:,} ticks compared, 0-{n - 1:,})"
        else:
            v = f"{head} {verdict} on 0-{n - 1:,}"
        if lag:
            # Lag check v2 (port/test/README.md, "Lag replay").
            details = [f"{lag.get('lag_only_ticks', 0):,} lag-only"
                       + (f" ({100.0 * lag.get('lag_only_share_of_compared_ticks', 0):.0f}%, checked only "
                          "through the lag replay)" if lag.get("lag_only_ticks") else "")]
            if lag.get("lag_only_raster_ticks"):
                # port/test/README.md, "Raster-timing frames": counted apart from the lag-only ticks.
                details.append(f"{lag['lag_only_raster_ticks']:,} lag-only with a raster-timing frame")
            details += [f"{lag.get('not_lag_only_ticks', 0):,} not lag-only"
                       + (f" (first {lag['first_not_lag_only_tick']})" if lag.get("not_lag_only_ticks") else ""),
                       f"{lag.get('replay_only_ticks', 0):,} replay-only"
                       + (f" (first {lag.get('first_replay_only_tick')})" if lag.get("replay_only_ticks") else "")]
            if lag.get("sequence_left_at_tick") is not None:
                details.append(f"lag replay left the GBA's sequence at {lag['sequence_left_at_tick']}")
            if lag.get("misplaced_lag_ticks"):
                details.append(f"{len(lag['misplaced_lag_ticks'])} ticks with misplaced lag VBlanks")
            v += ": " + ", ".join(details)
        elif compared == "DIFFERENT" and pc_name == "pc":
            v += f": first {res.get('first_tick')} (no lag check)"
        elif compared == "DIFFERENT":
            seq = cmp.get("lag_replay_sequence") or {}
            v += f": first {res.get('first_tick')}" + (
                f"; left the GBA's lag sequence at {seq['left_at_tick']}" if seq.get("left_at_tick") is not None
                else "")
        if cmp.get("raster_timing_frames"):
            # port/test/README.md, "Raster-timing frames": labelled frames, counted apart.
            v += f" ({len(cmp['raster_timing_frames']):,} raster-timing frames labelled)"
        parts.append(v)
        if known_note:
            parts.append(known_note)
        logic = {f: t for f, t in (res.get("first_tick_per_field") or {}).items() if f in LOGIC_FIELDS}
        if n == 0:
            pass
        elif logic:
            f, t = min(logic.items(), key=lambda x: x[1])
            pool = (res.get("details_at_first_tick", []) if res.get("first_tick") == t else []) + \
                   (lag.get("details_at_first_not_lag_only_tick", []) if lag.get("first_not_lag_only_tick") == t
                    else [])
            detail = [re.sub(r"^\S+( \[[^]]*\])?:\s*", "", x) for x in pool if x.startswith(f + ":")
                      or x.startswith(f + " [")]
            parts.append(f"logic first differs at {t}: {f}" + (f" {detail[0][:48]}" if detail else ""))
        else:
            parts.append("logic fields equal")
    return ok, "; ".join(parts)


# A compiler warning line: "FILE:LINE:COL: warning:" (or without the column),
# and the ones with no source position, such as "cc1: warning:" (a bad
# command-line option), "<command-line>: warning:" (a -D redefined) or the
# driver's "gcc: warning:" (integration review, 2026-10-20: only the first
# kind was counted).
WARNING_RX = re.compile(r"^\S+: warning: ")


def warn_logs(build_dir):
    """(ok, note): the compiler warnings gate for one PC build directory.

    port/Makefile writes every object's compiler messages to OBJ.warn (empty
    when clean), next to the object, so the warnings of objects that this run
    did not recompile are still counted. The objects are those port/Makefile
    compiles: BUILD/src/**.o (game code), BUILD/port/**.o (port code),
    romreloc.o and autostubs*.o. An object without its log (built before the
    logs existed) fails too: rebuild it.

    What it covers: the port code (PORT_CFLAGS: -Wall -Wno-unused-function).
    The game objects are compiled with -w (HOST_CFLAGS), and romreloc.o and
    autostubs*.o with -w too, so their logs stay empty whatever GCC would say;
    for them the gate only checks that each log is there. No warning scan of
    the game code (-Wmaybe-uninitialized, -Wreturn-type ...) is part of it."""
    objs = []
    for sub in ("src", "port"):
        for root, _, files in os.walk(os.path.join(build_dir, sub)):
            objs += [os.path.join(root, f) for f in files if f.endswith(".o")]
    objs += [os.path.join(build_dir, f) for f in ("romreloc.o", "autostubs.o", "autostubs-sdl.o")
             if os.path.exists(os.path.join(build_dir, f))]
    if not objs:
        return False, f"{build_dir}: no objects (warnings not checked)"
    warned, missing, count = [], [], 0
    for o in sorted(objs):
        try:
            with open(o + ".warn", errors="replace") as fh:
                n = sum(1 for l in fh if WARNING_RX.match(l))
        except OSError:
            missing.append(o)
            continue
        if n:
            count += n
            warned.append(os.path.relpath(o, build_dir))
    note = f"{count} compiler warnings in {len(objs) - len(missing)} object logs ({build_dir})"
    if warned:
        note += ": " + ", ".join(warned[:4]) + (", ..." if len(warned) > 4 else "")
    if missing:
        note += (f"; {len(missing)} objects have no warning log (built before port/Makefile kept them; "
                 f"rebuild): {os.path.relpath(missing[0], build_dir)}" + (", ..." if len(missing) > 1 else ""))
    return count == 0 and not missing, note


def windows_interop():
    """Whether a Windows .exe runs from here: WSL registers its interop with
    binfmt_misc (WSLInterop, or WSLInterop-late on newer WSL), and it is on
    unless /etc/wsl.conf turns it off."""
    for name in ("WSLInterop", "WSLInterop-late"):
        try:
            with open(os.path.join("/proc/sys/fs/binfmt_misc", name)) as fh:
                if fh.readline().strip() == "enabled":
                    return True
        except OSError:
            pass
    return False


def cmd_run(a):
    d = a.dir
    os.makedirs(d, exist_ok=True)
    cmd = a.command
    row ={"id": a.id, "title": a.title, "status": "SKIP", "note": "", "seconds": 0.0,
           "exit": None, "log": os.path.join(d, a.id + ".log"), "command": " ".join(cmd)}
    for prog in a.skip_unless_cmd:
        if shutil.which(prog) is None:
            row["note"] = f"{prog} not installed"
            save_row(d, row)
            return 0
    for path in a.skip_unless_path:
        if not os.path.exists(path):
            row["note"] = f"not found: {path}"
            save_row(d, row)
            return 0
    if a.skip_unless_windows and not windows_interop():
        row["note"] = ("Windows programs cannot run here (not WSL, or its Windows interop is off): "
                       "the Windows exe is not run")
        save_row(d, row)
        return 0
    for need in a.needs:
        r = load_row(d, need)
        if r is None or r["status"] not in ("PASS", "KNOWN"):
            row["note"] = f"needs {need} ({'not run' if r is None else r['status']})"
            save_row(d, row)
            return 0
    for prefix in a.skip_if_pc_stopped:
        r = load_row(d, prefix)
        if r and r.get("pc_ticks") is not None and r["pc_ticks"] < r.get("gate0_ticks", 0):
            # Same keys up to there and a deterministic PC: this run would stop at the same tick.
            src = r.get("pc_stop_row", prefix)
            row["note"] = (f"not run: the PC stops at tick {r['pc_ticks']:,} on {src}, whose keys are a prefix "
                           f"of this route's" + (f" ({r['pc_fault_where']})" if r.get("pc_fault_where") else "")
                           + "; CHECK_ROUTES_ALL=1 runs it anyway")
            row["pc_ticks"], row["gate0_ticks"] = r["pc_ticks"], a.gate0 or 0
            row["pc_fault_where"], row["pc_stop_row"] = r.get("pc_fault_where", ""), src
            save_row(d, row)
            return 0
    print(f"[check-all] {a.id}: {a.title} ... ({time.strftime('%H:%M:%S')})", flush=True)
    # Old outputs are removed first, so a run that writes nothing is never judged on them.
    stale = [spec.rpartition("=")[0] for spec in a.trace_ticks]
    if a.gate0 is not None:
        stale += [os.path.join(GATE0_OUT, f) for f in ("pc.jsonl", "pc_lagreplay.jsonl", "pc_affine.jsonl")
                  + GATE0_KEEP]
    for f in stale:
        try:
            os.remove(f)
        except OSError:
            pass
    t0 = time.time()
    rc = run_logged(cmd, row["log"], a.verbose)
    row["seconds"] = time.time() - t0
    row["exit"] = rc
    with open(row["log"], errors="replace") as fh:
        lines = [l.rstrip("\n") for l in fh]
    waits = [l for l in lines if l.startswith("[check-all] lock ") and " taken after " in l]
    if waits:
        row["lock_waits"] = waits
    notes, failed = [], []
    for rx in a.require:
        if not any(re.search(rx, l) for l in lines):
            failed.append(f"missing /{rx}/")
    for rx in a.reject:
        hit = next((l for l in lines if re.search(rx, l)), None)
        if hit is not None:
            failed.append(f"found /{rx}/: {hit.strip()[:80]}")
    for spec, every in [(s, False) for s in a.note] + [(s, True) for s in a.note_all]:
        rx, _, fmt = spec.partition("=>")
        hits = [m for m in (re.search(rx, l) for l in lines) if m]
        texts = [(fmt.format(m.group(0), *m.groups()) if fmt else
                  m.group(1) if m.groups() else m.group(0)).strip() for m in hits]
        if texts:
            notes.append(" / ".join(texts) if every else texts[-1])
    for spec in a.count:
        rx, _, label = spec.rpartition("=") if "=" in spec else (spec, "", "")
        notes.append(f"{sum(1 for l in lines if re.search(rx, l))} {label or rx}")
    ok = rc == 0 and not failed
    for spec in a.trace_ticks:
        path, _, want = spec.rpartition("=")
        got = count_ticks(path)
        notes.append(f"PC {got or 0:,}/{int(want):,} ticks")
        if got is None or got < int(want):
            ok = False
    for wdir in a.warn_dir:
        w_ok, w_note = warn_logs(wdir)
        ok = ok and w_ok
        notes.append(w_note)
    if a.gate0 is not None:
        g_ok, g_note = gate0_note(a.id, d, a.gate0, lines, row, a.gate0_pc)
        if a.gate0_sram:
            s_ok, s_note = sram_note(a.id, row)
            g_ok = g_ok and s_ok
            g_note += "; " + s_note
        ok = ok and g_ok
        notes.append(g_note)
        if rc != 0 and row.get("known") and g_ok and not failed:
            # A KNOWN row (KNOWN_LIMITS): make's status is the compare's DIFFERENT, every PC run complete.
            ok = True
            notes.append(f"make exit {rc} (the compare's DIFFERENT)")
        elif rc != 0:  # the verdict above says why; make's status goes last
            notes.append(f"make exit {rc}")
    elif rc != 0:
        notes.insert(0, f"exit {rc}")
    notes = failed + notes
    row["status"] = ("KNOWN" if row.get("known") else "PASS") if ok else "FAIL"
    row["note"] = "; ".join(n for n in notes if n)
    save_row(d, row)
    return 0


def cmd_table(a):
    d = a.dir
    rows = []
    for rid in a.ids:
        r = load_row(d, rid)
        rows.append(r or {"id": rid, "title": "", "status": "MISSING", "note": "row did not run", "seconds": 0})
    counts = {}
    for r in rows:
        counts[r["status"]] = counts.get(r["status"], 0) + 1
    bad = counts.get("FAIL", 0) + counts.get("MISSING", 0)
    idw = max([15] + [len(r["id"]) for r in rows])
    head = f"{'Check':<{idw}} {'Result':<7} {'Time':>6}  Note"
    width = 118
    out = ["", "=" * width, f"check-all ({time.strftime('%Y-%m-%d %H:%M')}): {len(rows)} checks, logs in {d}/",
           "-" * width, head, "-" * width]
    full = []
    for r in rows:
        lead = f"{r['id']:<{idw}} {r['status']:<7} {fmt_time(r['seconds']):>6}  "
        note = r["note"] or "-"
        full.append(lead + note + (f"   [{r.get('title', '')}]" if r.get("title") else ""))
        # A passing row gets one line; any other row up to three, so its reason shows.
        wrapped = textwrap.wrap(note, width - len(lead)) or ["-"]
        keep = wrapped[:1 if r["status"] == "PASS" else 3]   # (a KNOWN row shows why, as a failed one)
        if len(keep) < len(wrapped):
            keep[-1] = keep[-1][:width - len(lead) - 3] + "..."
        out.append(lead + keep[0])
        out += [" " * len(lead) + k for k in keep[1:]]
    out.append("-" * width)
    verdict = "FAIL" if bad else "PASS"
    if not bad and (counts.get("SKIP") or counts.get("KNOWN")):
        # a skipped row is not a pass, and a KNOWN row passes only by a recorded limit: say so up front
        verdict = "PASS (" + ", ".join(x for x in (f"{counts['SKIP']} skipped" if counts.get("SKIP") else "",
                                                    f"{counts['KNOWN']} known" if counts.get("KNOWN") else "")
                                       if x) + ")"
    out.append(f"CHECK-ALL: {verdict}: " + ", ".join(f"{counts[k]} {k.lower()}" for k in
                                                 ("PASS", "KNOWN", "FAIL", "SKIP", "MISSING") if k in counts)
               + f"; full notes in {d}/summary.txt")
    out.append("=" * width)
    print("\n".join(out))
    with open(os.path.join(d, "summary.txt"), "w") as fh:
        fh.write("\n".join(out[1:5] + full + out[-3:]) + "\n")
    return 1 if bad else 0


def cmd_keyticks(a):
    n = 0
    with open(a.keyfile) as fh:
        for line in fh:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            m = re.fullmatch(r"([0-9A-Fa-f]+)(?:\*(\d+))?", line)
            if not m:
                sys.exit(f"{a.keyfile}: bad entry {line!r}")
            n += int(m.group(2) or 1)
    print(n)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--dir", default="build/check-all")
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("id")
    r.add_argument("title")
    r.add_argument("--needs", action="append", default=[])
    r.add_argument("--skip-if-pc-stopped", action="append", default=[])
    r.add_argument("--skip-unless-cmd", action="append", default=[])
    r.add_argument("--skip-unless-path", action="append", default=[])
    r.add_argument("--skip-unless-windows", action="store_true")
    r.add_argument("--require", action="append", default=[])
    r.add_argument("--reject", action="append", default=[])
    r.add_argument("--warn-dir", action="append", default=[])
    r.add_argument("--gate0-pc", default="pc", choices=("pc", "pc_lagreplay"))
    r.add_argument("--gate0-sram", action="store_true")
    r.add_argument("--note", action="append", default=[])
    r.add_argument("--note-all", action="append", default=[])
    r.add_argument("--count", action="append", default=[])
    r.add_argument("--trace-ticks", action="append", default=[])
    r.add_argument("--gate0", type=int)
    r.add_argument("--verbose", action="store_true")
    t = sub.add_parser("table")
    t.add_argument("ids", nargs="*")
    k = sub.add_parser("keyticks")
    k.add_argument("keyfile")
    lk = sub.add_parser("lock")
    lk.add_argument("lockfile")
    argv = sys.argv[1:]
    command = []
    if "--" in argv:
        i = argv.index("--")
        argv, command = argv[:i], argv[i + 1:]
    a = ap.parse_args(argv)
    if a.cmd in ("table", "keyticks"):
        return {"table": cmd_table, "keyticks": cmd_keyticks}[a.cmd](a)
    if not command:
        ap.error(f"{a.cmd}: no command after --")
    a.command = command
    if a.cmd == "lock":
        return cmd_lock(a)
    try:
        return cmd_run(a)
    except Exception as e:  # a broken row is a failed row, never a missing one
        save_row(a.dir, {"id": a.id, "title": a.title, "status": "FAIL", "note": f"check_all.py: {e!r}",
                         "seconds": 0.0, "exit": None, "log": "", "command": " ".join(command)})
        return 0


if __name__ == "__main__":
    sys.exit(main())
