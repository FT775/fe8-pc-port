#!/usr/bin/env python3
"""Sound on any route: the snd rows' checks, in chunks, plus the PC's own sound
against the GBA's, record by record (port/test/README.md, "Sound on any route").

    python3 port/test/audio/snd_route.py --gba DIR/X.gba --pc DIR/X.pc --ticks N [--chunk 4096] [--jobs 4]

Inputs (port/test/Makefile snd-route makes them; ROM-derived, under build/):
  X.gba.snd, .calls, .idx   gate0_capture.lua with SND_OUT, SND_CALLS, SND_INDEX: one record
                            per GBA VBlank (frame callback), the m4a calls between them, and
                            for each tick the record of the VBlank it waits for
  X.pc.snd, .fifo8, .idx    the PC lag replay (GATE0_LAG_FROM) with FE8PC_SND_RECORD,
                            FE8PC_AUDIO_DUMP and GATE0_SND_INDEX: one record per PC VBlank,
                            the lag VBlanks too (port/hw/frame.c PortLagFrameLines)
  X.pc.jsonl, X.pc.log      that run's trace and log (complete run: every tick, no fault)

Checks, each over the whole capture, run in chunks of --chunk records (any
number of worker processes, each with its own C driver; a chunk reads only its
records, so memory does not grow with the route):
  1. Replay (the snd rows' pass/fail, audio/m4a_replay.py): every GBA record
     pair (v, v+1) loaded into the C driver with the GBA's own m4a calls of
     window v made again. The call record is classified once for the whole
     capture (m4a_replay.CallPlan) and every pair is judged by
     m4a_replay.replay_range, the code the snd rows run; the chunks' counts
     add up to what one run over the same records gives (checked on route_ch1:
     README). A capture resumed from a save state has no boot m4aSoundInit; its
     first record must show the driver set up and idle.
  2. PC self-check (as the snd rows): the PC .fifo8 equals the slot rule on
     the PC's own records.
  3. PC against GBA (new, the frame clock is master, D4): the lag replay runs
     every GBA VBlank, lag VBlanks too (each with its sound frame), at the
     GBA's place among the main loop's events and m4a calls (the trace's
     "lag_at" and "lag_snd"), so PC record v and GBA record v + D must be the
     same VBlank for one constant D. D is read from the two index files and
     must be the same on every tick both have (each tick spans the same
     VBlanks on both sides); the two runs must end at the same VBlank, and
     every PC record must have its GBA record. Then each PC record must equal
     its GBA record in every byte the replay compares (tracks, the sound block
     with code pointers translated by name, the readable sound register bits,
     DMA1CNT_H/DMA2CNT_H without the destination bits), in SOUND_INFO_PTR and
     in the header's gGameClock. GBA records taken before the boot
     m4aSoundInit returned are skipped, as in the replay. The only excuse is
     the replay's: on a record that a GBA m4a call that was not clean is open
     across (the GBA ran part of it on each side of the record), the bytes
     that call can touch (m4a_replay.Scope). Every PCM lane must be equal on
     every record compared, excused or not, but on a record a not-clean
     m4aSoundMain is open across (m4a_replay.MIXER: the GBA's mixer caught
     half-way, integration 11), counted apart. The header's OnMain count is
     reported, not compared: it is no sound state, and a lag VBlank that came
     on the GBA before the main callback was entered (OnVBlank ran into the
     next frame) runs on the PC at the tick's first event, inside OnMain.
  4. The PC run complete: its trace has every tick to --ticks and its log no
     [fault] line (the snd rows' --trace-ticks rule).
The last line is SND ROUTE: PASS only when all four pass (INCOMPLETE when the
PC run stopped early, FAIL otherwise).
"""

import argparse
import json
import multiprocessing
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import m4a_difftest as dt  # noqa: E402
import m4a_replay as mr  # noqa: E402
import snd_calls  # noqa: E402

REC = mr.REC
VB = 224


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def read_records(path, lo, count):
    """count records from record lo (fewer at the end of the file)."""
    with open(path, "rb") as f:
        f.seek(lo * REC)
        data = f.read(count * REC)
    return data[:len(data) - len(data) % REC]


def n_records(path):
    size = os.path.getsize(path)
    return size // REC, size % REC


def read_index(path):
    out = {}
    with open(path) as f:
        first = f.readline()
        if not first.startswith("# fe8-sndindex-1"):
            sys.exit(f"{path}: not a fe8-sndindex-1 file")
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            t, v = line.split()
            out[int(t)] = int(v)
    return out


def pc_code_table(pc_exe, names, syms):
    """{PC host address: GBA address} of the functions the driver keeps as code
    pointers (the harness's SYM list), by name (nm of the PC binary)."""
    r = subprocess.run(["nm", pc_exe], capture_output=True, text=True, check=True)
    host = {}
    for line in r.stdout.splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in "tTwW" and p[2] in names:
            host.setdefault(p[2], int(p[0], 16))
    missing = sorted(n for n in names if n not in host)
    if missing:
        # Not linked into the PC binary: such an address cannot be in its records.
        print(f"{pc_exe}: no symbol for {missing} (left out of the code-pointer table)")
    return {host[n]: syms[n][0] | 1 for n in names if n in host}


# Worker state (one C driver per process).
W = {}


def worker_init(exe, rom_path, elf, plan, args):
    rom, syms = dt.read_elf(elf)
    c = dt.CSide(exe, rom_path)
    ctx = mr.Ctx(rom, syms)
    W.update(c=c, ctx=ctx, run=mr.Runner(c, syms, ctx.players), plan=plan, a=args, syms=syms)


class Direct:
    """PC against GBA for a range of GBA records (check 3)."""
    COUNTS = ("compared", "equal", "excused", "differ", "lanes", "lanes_excused", "skipped", "onmain")

    def __init__(self):
        for k in self.COUNTS:
            setattr(self, k, 0)
        self.excused_by, self.lines, self.differing = {}, [], []
        self.onmain_first = None

    def merge(self, o):
        for k in self.COUNTS:
            setattr(self, k, getattr(self, k) + getattr(o, k))
        for k, n in o.excused_by.items():
            self.excused_by[k] = self.excused_by.get(k, 0) + n
        self.lines.extend(o.lines)
        self.differing.extend(o.differing)
        if o.onmain_first and (self.onmain_first is None or o.onmain_first < self.onmain_first):
            self.onmain_first = o.onmain_first
        return self


def open_across(u, r):
    """The call (not clean) was open across record r: entered before it, returned after it."""
    return u.w_in < r and (u.w_out < 0 or u.w_out >= r)


def direct_range(gdata, gbase, pdata, pbase, lo, hi, d, code, show):
    """GBA records lo..hi-1 (file indices) against PC records v - d."""
    ctx, plan = W["ctx"], W["plan"]
    out = Direct()
    for i in range(lo, hi):
        g = gdata[(i - gbase) * REC:(i - gbase + 1) * REC]
        v = u32(g, 8)
        j = v - d - pbase
        if j < 0 or (j + 1) * REC > len(pdata):
            continue
        p = pdata[j * REC:(j + 1) * REC]
        if u32(p, 8) != v - d:
            sys.exit(f"PC record for v {v - d} holds v {u32(p, 8)}: the PC records are not one per VBlank")
        if v <= plan.init_end:
            # Taken before the GBA's boot m4aSoundInit returned: skipped, not
            # excused, as in the replay (the GBA record is half-initialised; the
            # PC's init does not wait for VBlanks, so its record is after it).
            out.skipped += 1
            continue
        out.compared += 1
        want = mr.State.of_record(g)
        got = mr.State(p[mr.O_TRK:mr.O_SND], translate(p[mr.O_SND:mr.O_PTR], code), p[mr.O_IO:mr.O_DMA],
                       p[mr.O_DMA:mr.O_DMA + 8])
        lanes = got.snd[mr.LANES[0]:mr.LANES[1]] == want.snd[mr.LANES[0]:mr.LANES[1]]
        out.lanes += lanes
        if not lanes and any(u.name in mr.MIXER and open_across(u, v) for u in plan.unclean):
            out.lanes_excused += 1   # the GBA's mixer caught half-way (m4a_replay.MIXER)
        if g[6:8] != p[6:8]:
            # The OnMain count (not sound state, and not in the replay's rule): a
            # lag VBlank that came on the GBA before the main callback was entered
            # (OnVBlank ran past the next VBlank) runs on the PC before the tick's
            # first event, inside OnMain. Reported, not compared.
            out.onmain += 1
            if out.onmain_first is None:
                out.onmain_first = (v, struct.unpack_from("<H", g, 6)[0], struct.unpack_from("<H", p, 6)[0])
        head_ok = g[12:16] == p[12:16]
        ptr_ok = g[mr.O_PTR:mr.O_IO] == p[mr.O_PTR:mr.O_IO]
        if head_ok and ptr_ok and got.equal(want):
            out.equal += 1
            continue
        touching = [u for u in plan.unclean if open_across(u, v)]
        why = []
        if not head_ok:
            why.append(f"header gGameClock GBA {u32(g, 12)} PC {u32(p, 12)}")
        if not ptr_ok:
            why.append("SOUND_INFO_PTR")
        if touching and head_ok and ptr_ok:
            scope = mr.Scope()
            for u in touching:
                ctx.add_scope(scope, u, [want, got])
            if not mr.where_differs(got, want, ctx.players, scope):
                out.excused += 1
                key = "; ".join(scope.what)
                out.excused_by[key] = out.excused_by.get(key, 0) + 1
                continue
            why += mr.where_differs(got, want, ctx.players, scope)
        else:
            why += mr.where_differs(got, want, ctx.players)
        out.differ += 1
        out.differing.append(v)
        if len(out.lines) < show:
            out.lines.append((v, f"  GBA record v {v} / PC record v {v - d}: differs in " + "; ".join(why)))
    return out


def translate(block, code):
    b = bytearray(block)
    for off in W["codeoffs"]:
        if 0 <= off <= len(b) - 4:
            x = struct.unpack_from("<I", b, off)[0]
            if x in code:
                struct.pack_into("<I", b, off, code[x])
    return bytes(b)


def selfcheck_range(pdata, lo, hi, f8path):
    """PC fifo8 blocks lo..hi-1 against the slot rule on PC records lo+1..hi (file indices)."""
    with open(f8path, "rb") as f:
        f.seek(lo * 2 * VB)
        f8 = f.read((hi - lo) * 2 * VB)
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
    import sndtool  # noqa: E402
    n, diff = 0, []
    for k in range(lo, hi):
        blk = f8[(k - lo) * 2 * VB:(k - lo + 1) * 2 * VB]
        r = pdata[(k + 1 - lo) * REC:(k + 2 - lo) * REC]
        if len(blk) < 2 * VB or len(r) < REC:
            break
        s = sndtool.slot_bytes(r)
        n += 1
        if (s if s is not None else bytes(2 * VB)) != blk:
            diff.append(k)
    return n, diff


def chunk_job(job):
    """One chunk. ("gba", lo, hi, last): the replay of GBA pairs [lo, hi) and
    the PC records of GBA records [lo, hi) (and the last record, on the last
    chunk) against them. ("pc", lo, hi): the PC self-check of fifo8 blocks
    [lo, hi) (block k pairs with PC record k + 1, file indices)."""
    a = W["a"]
    if job[0] == "pc":
        _, lo, hi = job
        pdata = read_records(a.pc + ".snd", lo, hi - lo + 1)
        return {"self": selfcheck_range(pdata, lo, hi, a.pc + ".fifo8")}
    _, lo, hi, last = job
    gdata = read_records(a.gba + ".snd", lo, hi - lo + 1)
    st = mr.replay_range(W["run"], W["ctx"], W["plan"], gdata, lo, lo, hi, a.show, False, a.gba + ".snd")
    res = {"replay": st, "direct": None}
    if W["d"] is not None:
        g_lo_v, g_hi_v = u32(gdata, 8), u32(gdata, 8) + (hi - lo) + (1 if last else 0)
        p_lo = max(g_lo_v - W["d"] - W["pc_first"], 0)
        p_hi = min(g_hi_v - W["d"] - W["pc_first"], W["pc_n"])
        if p_hi > p_lo:
            pdata = read_records(a.pc + ".snd", p_lo, p_hi - p_lo)
            res["direct"] = direct_range(gdata, lo, pdata, p_lo + W["pc_first"], lo, hi + (1 if last else 0),
                                         W["d"], W["code"], a.show)
    return res


def trace_ticks(path):
    """(first tick, last tick, lines) of a Gate 0 trace."""
    first = last = None
    n = 0
    with open(path) as f:
        f.readline()
        for line in f:
            if not line.startswith("{"):
                continue
            t = json.loads(line)["tick"]
            first = t if first is None else first
            last = t
            n += 1
    return first, last, n


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--gba", required=True, help="prefix of the GBA files (X.gba)")
    ap.add_argument("--pc", required=True, help="prefix of the PC files (X.pc)")
    ap.add_argument("--pc-exe", default="build/pc-gate0/fe8pc")
    ap.add_argument("--ticks", type=int, required=True, help="the run's GATE0_TICKS (last tick + 1)")
    ap.add_argument("--chunk", type=int, default=4096, help="records per chunk")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--show", type=int, default=8)
    ap.add_argument("--root", default=".")
    ap.add_argument("--out", default="build/difftest/snd-route")
    a = ap.parse_args()

    root, out = os.path.abspath(a.root), os.path.abspath(a.out)
    elf = os.path.join(root, "fireemblem8.elf")
    rom, syms = dt.read_elf(elf)
    os.makedirs(out, exist_ok=True)
    rom_path = os.path.join(out, "rom.bin")
    with open(rom_path, "wb") as f:
        f.write(rom)
    exe, _ = dt.build_harness(root, out, syms, "gcc", "port/audio/m4a_1_pc.c")

    # The GBA capture and its call record.
    n, rest = n_records(a.gba + ".snd")
    if rest or n < 2:
        sys.exit(f"{a.gba}.snd: {n} records and {rest} bytes over: not a finished capture")
    first = read_records(a.gba + ".snd", 0, 1)
    v_first = u32(first, 8)
    info = {}
    calls, _, (cap_records, cap_from) = snd_calls.load(a.gba + ".calls", syms, info)
    if cap_records != n or cap_from != v_first:
        sys.exit(f"{a.gba}.calls: written for {cap_records} records from {cap_from}, {a.gba}.snd has {n} from"
                 f" {v_first}: not the same capture")
    plan = mr.CallPlan(calls, v_first, n, info.get("resumed"))
    print(f"GBA capture: {n} records (v {v_first}-{v_first + n - 1}), {len(calls)} top-level m4a calls"
          + (f"; resumed at tick {plan.resumed[0]}" if plan.resumed else "; from power-on"))
    if plan.error:
        print(f"{a.gba}.calls: {plan.error}: nothing can be replayed")
        print("RESULT: FAIL")
        print("SND ROUTE: FAIL")
        return 1

    # Frame clock (check 3): D from the two indexes.
    gidx, pidx = read_index(a.gba + ".idx"), read_index(a.pc + ".idx")
    pc_n, pc_rest = n_records(a.pc + ".snd")
    pc_first = u32(read_records(a.pc + ".snd", 0, 1), 8) if pc_n else None
    common = sorted(set(gidx) & set(pidx))
    # d_ok: one D on every tick both indexes have (the records can be paired);
    # end_ok: the two runs end at the same VBlank. The records are compared
    # whenever d_ok, also for a PC run that stopped early (INCOMPLETE).
    d, d_ok, end_ok, clock_msgs = None, True, True, []
    if not common or not pc_n:
        d_ok = False
        clock_msgs.append("no tick in both indexes" if not common else "no PC record")
    else:
        d = gidx[common[0]] - pidx[common[0]]
        bad = [t for t in common if gidx[t] - pidx[t] != d]
        if bad:
            d_ok = False
            t = bad[0]
            clock_msgs.append(f"D is {d} at tick {common[0]} but {gidx[t] - pidx[t]} at tick {t}: that tick spans"
                              " other VBlanks on the two sides")
        gap = [t for t in range(common[0], common[-1] + 1) if t not in pidx or t not in gidx]
        if gap:
            d_ok = False
            clock_msgs.append(f"ticks missing from an index: {gap[:5]}")
    pc_last = pc_first + pc_n - 1 if pc_n else None
    g_last = v_first + n - 1
    if d is not None and pc_n:
        if pc_first + d < v_first:
            d_ok = False
            clock_msgs.append(f"the PC records start before the GBA's (PC v {pc_first} = GBA v {pc_first + d})")
        if pc_last + d != g_last:
            end_ok = False
            clock_msgs.append(f"the runs end at other VBlanks: PC record v {pc_last} is GBA v {pc_last + d}, the GBA's"
                              f" last record is v {g_last}")
    clock_ok = d_ok and end_ok
    print(f"frame clock: D = GBA record - PC record = {d} on {len(common)} ticks"
          + (f" ({common[0]}-{common[-1]})" if common else "") + ("" if clock_ok else ": " + "; ".join(clock_msgs)))

    # Chunks.
    jobs = []
    lo = 0
    while lo < n - 1:
        hi = min(lo + a.chunk, n - 1)
        jobs.append(("gba", lo, hi, hi == n - 1))
        lo = hi
    lo = 0
    while lo < pc_n - 1:
        hi = min(lo + a.chunk, pc_n - 1)
        jobs.append(("pc", lo, hi))
        lo = hi
    c0 = dt.CSide(exe, rom_path)
    names = set(c0.sym)
    c0.close()
    code = pc_code_table(a.pc_exe, names, syms)
    codeoffs = [o for o in mr.SOUNDINFO_CODE_FIELDS]
    codeoffs += [syms["gMPlayJumpTable"][0] + 4 * i - mr.SND for i in range(36)]
    ctx0 = mr.Ctx(rom, syms)
    codeoffs += [p + 0x38 - mr.SND for p in ctx0.players]

    def init(*args):
        worker_init(*args)
        W.update(d=d if d_ok else None, pc_first=pc_first, pc_n=pc_n, code=code, codeoffs=codeoffs)

    print(f"chunks: {len(jobs)} of up to {a.chunk} record pairs, {a.jobs} worker(s)")
    replay, direct = mr.Stats(), Direct()
    self_n, self_diff = 0, []
    with multiprocessing.get_context("fork").Pool(a.jobs, initializer=init,
                                                    initargs=(exe, rom_path, elf, plan, a)) as pool:
        for res in pool.imap_unordered(chunk_job, jobs):
            if "self" in res:
                self_n += res["self"][0]
                self_diff += res["self"][1]
                continue
            replay.merge(res["replay"])
            if res["direct"] is not None:
                direct.merge(res["direct"])

    # 4. first: a PC run that stopped early makes the verdict INCOMPLETE, and
    # checks 2 and 3 then judge the records it wrote (each side's files end
    # where its buffers were last flushed).
    first_t, last_t, lines = trace_ticks(a.pc + ".jsonl")
    faults = [ln.rstrip() for ln in open(a.pc + ".log", errors="replace") if "[fault]" in ln]
    complete = last_t == a.ticks - 1 and not faults and bool(pidx) and max(pidx) == a.ticks - 1

    # 1. Replay, as the snd rows print it.
    print("-- 1. replay (the snd rows' rule) --")
    ok_replay = mr.report(replay, plan, 0, n - 1, a.show)

    # 2. PC self-check.
    print("-- 2. PC self-check --")
    print(f"PC .fifo8 against the slot rule on the PC records: {self_n} VBlanks, {len(self_diff)} differ"
          + (f" (first {sorted(self_diff)[:5]})" if self_diff else "")
          + ("" if self_n == pc_n - 1 else f" (the .fifo8 has {self_n} of the {pc_n - 1} VBlanks to check)"))
    ok_self = self_n > 0 and not self_diff and ((self_n == pc_n - 1 and pc_rest == 0) or not complete)

    # 3. PC against GBA.
    print("-- 3. PC against GBA, record by record --")
    want_n = pc_n
    for v, text in sorted(direct.lines)[:a.show]:
        print(text)
    print(f"PC records {pc_n}" + (f" (v {pc_first}-{pc_last})" if pc_n else "") + f": {direct.compared} compared with"
          f" GBA records ({direct.skipped} taken before the GBA's m4aSoundInit returned skipped), {direct.equal} fully"
          f" equal, {direct.excused} equal outside the scope of a call that was not clean, {direct.differ} differ"
          + (f" (GBA v {sorted(direct.differing)[:10]})" if direct.differing else ""))
    print(f"  PCM lanes equal on {direct.lanes} of {direct.compared}"
          + (f", excused on {direct.lanes_excused} (a not-clean m4aSoundMain open across the GBA record: its mixer"
             " caught half-way)" if direct.lanes_excused else ""))
    for k, c in direct.excused_by.items():
        print(f"  excused on {c} records, only for: {k}")
    if direct.onmain:
        v, gm, pm = direct.onmain_first
        print(f"  header OnMain count (reported, not compared) differs on {direct.onmain} records, first GBA v {v}:"
              f" GBA {gm}, PC {pm} (a lag VBlank before the main callback's entry on the GBA)")
    if pc_rest:
        print(f"  {a.pc}.snd ends with a partial record ({pc_rest} bytes): the PC run did not finish")
    ok_direct = (d_ok and (end_ok or not complete) and direct.compared + direct.skipped == want_n
                 and direct.compared > 0 and direct.differ == 0
                 and direct.lanes + direct.lanes_excused == direct.compared)
    if direct.compared + direct.skipped != want_n:
        print(f"  {want_n - direct.compared - direct.skipped} PC records have no GBA record: FAIL")
    if not complete and d_ok:
        print("  (the PC run stopped early: these are the records it wrote)")

    # 4. The PC run complete.
    print("-- 4. PC run --")
    print(f"PC trace ticks {first_t}-{last_t} ({lines} lines), want to {a.ticks - 1}; faults in the log: {len(faults)}"
          + (f" ({faults[0][:120]})" if faults else ""))

    ok = ok_replay and ok_self and ok_direct and complete
    verdict = "PASS" if ok else ("INCOMPLETE" if not complete else "FAIL")
    print(f"SND ROUTE: {verdict} (replay {'PASS' if ok_replay else 'FAIL'}, self-check"
          f" {'PASS' if ok_self else 'FAIL'}, PC against GBA {'PASS' if ok_direct else 'FAIL'}, PC run"
          f" {'complete' if complete else 'INCOMPLETE'})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
