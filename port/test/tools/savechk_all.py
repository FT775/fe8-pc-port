#!/usr/bin/env python3
"""The save checks as one standing check: P5.3, P5.4 and the wider ones, PASS or FAIL.

    python3 port/test/tools/savechk_all.py [--builds full,sdl,win] [--parts p53,p53b,p54,rfo]
                                           [--out DIR] [--start SAV]

port/test/README.md, "Save checks"; check-all's opt-in row (make -f port/check-all.mk
CHECK_SAVES=1, row "saves"); the user's SAVE-2 B, SAVE-3 B and SAVE-4 B of 2026-10-30
(port-plan/CHOICES-2026-10-30.md). Run from the repository root, after the GBA build, the
played builds asked for (build/pc-full/fe8pc, build/pc-sdl/fe8pc-sdl,
build/win32-sdl/fe8pc-sdl.exe) and, for p54, the Gate 0 test build and the oracle
(port/test/Makefile chapsave builds those itself). Every session is played by
tools/savechk.sh; the parts:

  p53   P5.3, all three slots: the nine sessions of inputs/savechk/ (s1_frelia_slot2, r_resume,
        s2_resume2_suspend, s3_newgame3_suspend, r_resume, s4_restart1_suspend, r_resume,
        s5_restart2, s6_restart3) from ch1_seize.sav, on mGBA and on each build.
  p53b  The save-menu actions (SAVE-2): c1_copy13 (Copy Data, slot 1 to slot 3), c2_copy
        (slot 1 to slot 2 while slot 3 is full), c3_erase (Erase Data, slot 2), then
        s1_frelia_slot2 (a chapter-end save to slot 2 while slot 3 is full), from ch1_seize.sav.
  p54   P5.4, a suspend between the PC and mGBA: direction 1 from the FULL build's p53 session 3
        save (a suspend the PC wrote) with p54_k1_end_turn, direction 2 from mGBA's 32 KiB end
        save of direction 1 with p54_k2_wait_end; each as a Gate 0 run (make chapsave: the
        test build against mGBA tick by tick, and the two end SRAMs) and on each played build.
  rfo   Reforged Options on (SAVE-4; full and sdl only, as the settings file and the test
        switch are given through the Linux builds' per-user folder and environment):
        casual_mode = 1, unbreakable_weapons = 2 (everyone), cheat_infinite_gold = 1, the
        chain s1_frelia_slot2 + FE8PC_RFO_TEST_FALL=4 (Franz falls on the resume and retreats;
        he must be back in slot 2's Chapter 2 save), s2_resume2_suspend + FE8PC_RFO_TEST_FALL=3
        (Gilliam falls; the suspend's fallen list), p54_k1_end_turn (the fallen list loaded back;
        an enemy phase with battles: weapons kept at full uses), c2_copy (slot 2 to slot 3: the
        mark copied) and c3_erase (slot 3 erased: its mark gone); the same chain with the
        options off (the control: the test falls are retail deaths there); then the chain's
        last .sav, with no sidecar, played on mGBA and on the FULL build with the options off
        (p54_k2_wait_end): mGBA loads it and both play on the same.

What is checked (each a "PASS"/"FAIL" line):
  - every session completes on every side (savechk.sh exit 0);
  - "other slots untouched": in each session, on mGBA and on each build, the game slots whose
    block or SaveBlockInfo entry changed are exactly the ones the session writes (the table
    below; the suspend blocks are not slots);
  - each build's .sav after each session against mGBA's (sram_blockdiff.py --cross): equal
    apart from the known bytes (the play clock, its checksums, the P-12/P-18 bytes);
  - p54's Gate 0 runs: verdict MATCH or LAG-ONLY on every tick, and the two end SRAMs --cross;
  - rfo: the sidecar's fingerprints match their .sav after every session (sidecar_check.py),
    and the marks, the fallen lists and the options' effects named above.

Prints one line per check and at the end "SAVE CHECKS: PASS (N checks)" or "SAVE CHECKS:
FAIL (n of N checks failed)"; exit 0 or 1, 2 on a setup error. Outputs (saves, logs) in
--out (default build/savechk): game data, keep them out of git.
"""
import argparse
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
K = "port/test/inputs/savechk/"
START_SHA1 = "5737d58b2cc6241072c6ae2680c2eaf66bc7dfa6"  # ch1_seize.sav as sram-capture makes it
RFO_SETTINGS = r"format = 2\ncasual_mode = 1\nunbreakable_weapons = 2\ncheat_infinite_gold = 1"

# (key file, the game slots (1-3) the session writes)
P53 = [("s1_frelia_slot2", {2}), ("r_resume", set()), ("s2_resume2_suspend", set()),
       ("s3_newgame3_suspend", {3}), ("r_resume", set()), ("s4_restart1_suspend", set()),
       ("r_resume", set()), ("s5_restart2", set()), ("s6_restart3", set())]
P53B = [("c1_copy13", {3}), ("c2_copy", {2}), ("c3_erase", {2}), ("s1_frelia_slot2", {2})]
RFO = [("s1_frelia_slot2+FE8PC_RFO_TEST_FALL=4", {2}), ("s2_resume2_suspend+FE8PC_RFO_TEST_FALL=3", set()),
       ("p54_k1_end_turn", set()), ("c2_copy", {3}), ("c3_erase", {3})]


def load_blockdiff():
    spec = importlib.util.spec_from_file_location("sram_blockdiff", os.path.join(HERE, "sram_blockdiff.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


BD = load_blockdiff()


class Report:
    def __init__(self):
        self.n = 0
        self.bad = []

    def check(self, ok, name, detail=""):
        self.n += 1
        if not ok:
            self.bad.append(name)
        print(f"{'PASS' if ok else 'FAIL'} {name}{': ' + detail if detail else ''}", flush=True)
        return ok


def sram(path):
    return BD.load(path)[0]


def slots_written(before, after):
    a, b = sram(before), sram(after)
    out = set()
    for start, size, name in BD.REGIONS:
        if a[start:start + size] != b[start:start + size]:
            if name.startswith("game."):
                out.add(int(name[5:]) + 1)
            elif name.startswith("info.game"):
                out.add(int(name[9:]))
    return out


def cross(gba, pc):
    line, other = BD.cross_line(sram(gba), sram(pc))
    return other == 0, line[len("SRAM CROSS: "):]


def run(cmd, log, env=None):
    with open(log, "w") as fh:
        p = subprocess.run(cmd, stdout=fh, stderr=subprocess.STDOUT, env=env)
    return p.returncode


def chain(side, out, start, sessions, rep, label, rfo=None):
    """Plays one chain with savechk.sh; returns the list of .sav paths (start first) or None."""
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    args = [K + s.split("+", 1)[0] + ".keys" + ("+" + s.split("+", 1)[1] if "+" in s else "") for s, _ in sessions]
    cmd = ["bash", "port/test/tools/savechk.sh"] + (["gba"] if side == "gba" else ["pc", side]) + [out, start] + args
    env = dict(os.environ)
    env.pop("SAVECHK_RFO", None)
    if rfo:
        env["SAVECHK_RFO"] = rfo
    rc = run(cmd, out + ".log", env)
    names = ["00_start.sav"] + ["%02d_%s.sav" % (i + 1, s.split("+", 1)[0]) for i, (s, _) in enumerate(sessions)]
    files = [os.path.join(out, n) for n in names]
    ok = rc == 0 and all(os.path.exists(f) for f in files)
    rep.check(ok, f"{label} {side}: {len(sessions)} sessions played",
              "" if ok else f"savechk.sh exit {rc}, see {out}.log")
    return files if ok else None


def check_slots(files, sessions, rep, label, side):
    bad = []
    for i, (s, want) in enumerate(sessions):
        got = slots_written(files[i], files[i + 1])
        if got != want:
            bad.append(f"session {i + 1} {s.split('+')[0]}: wrote slots {sorted(got) or '-'}, "
                       f"expected {sorted(want) or '-'}")
    rep.check(not bad, f"{label} {side}: other slots untouched in every session", "; ".join(bad))


def check_cross(gba_files, pc_files, rep, label, side):
    bad, known = [], 0
    for g, p in zip(gba_files[1:], pc_files[1:]):
        ok, what = cross(g, p)
        if not ok:
            bad.append(f"{os.path.basename(p)}: {what}")
        else:
            m = re.search(r"\((\d+) bytes", what)
            known = max(known, int(m.group(1)) if m else 0)
    rep.check(not bad, f"{label} {side} against mGBA, every session (--cross)",
              "; ".join(bad) if bad else f"{len(pc_files) - 1} saves equal apart from the known bytes "
              f"(at most {known} known bytes in one)")


def part_chain(name, sessions, builds, out, start, rep):
    gba = chain("gba", f"{out}/{name}/gba", start, sessions, rep, name)
    if gba:
        check_slots(gba, sessions, rep, name, "gba")
    res = {"gba": gba}
    for b in builds:
        pcf = chain(b, f"{out}/{name}/{b}", start, sessions, rep, name)
        res[b] = pcf
        if pcf:
            check_slots(pcf, sessions, rep, name, b)
            if gba:
                check_cross(gba, pcf, rep, name, b)
    return res


def gate0_chapsave(sav, keys, ticks, out, rep, label):
    log = f"{out}.chapsave.log"
    rc = run(["make", "-f", "port/test/Makefile", "chapsave", f"CHAPSAVE_SAV={sav}", f"CHAPSAVE_KEYS={K}{keys}.keys",
              f"CHAPSAVE_TICKS={ticks}"], log)
    try:
        with open("build/gate0/compare.json") as fh:
            cmp = json.load(fh)
    except (OSError, ValueError):
        cmp = {}
    verdict = (cmp.get("verdict") or "none").upper()
    lag = cmp.get("lag_check") or {}
    n = cmp.get("ticks_compared", 0)
    rep.check(verdict in ("MATCH", "LAG-ONLY") and n == ticks and rc in (0, 3),
              f"{label} Gate 0 (test build against mGBA, {ticks} ticks)",
              f"{verdict} on {n} ticks compared, {lag.get('lag_only_ticks', 0)} lag-only, "
              f"{lag.get('not_lag_only_ticks', 0)} not; make exit {rc}; log {log}")
    for f in ("gba_end.sav", "pc_end.sav", "compare.json"):
        if os.path.exists("build/gate0/" + f):
            shutil.copy("build/gate0/" + f, f"{out}.{f}")
    if os.path.exists(f"{out}.gba_end.sav") and os.path.exists(f"{out}.pc_end.sav"):
        ok, what = cross(f"{out}.gba_end.sav", f"{out}.pc_end.sav")
    else:
        ok, what = False, "no end SRAM"
    rep.check(ok, f"{label} Gate 0 end SRAM (--cross)", what)


def part_p54(builds, out, p53, rep):
    d1 = (p53 or {}).get("full")
    if not d1:
        rep.check(False, "p54: direction 1 needs the FULL build's p53 session 3 save", "p53 on full did not run")
        return
    os.makedirs(f"{out}/p54", exist_ok=True)
    start = f"{out}/p54/d1_pc_written.sav"
    shutil.copy(d1[3], start)
    for d, keys, ticks in (("d1", "p54_k1_end_turn", 4000), ("d2", "p54_k2_wait_end", 5500)):
        sess = [(keys, set())]
        gate0_chapsave(os.path.abspath(start), keys, ticks, f"{out}/p54/{d}", rep, f"p54 {d}")
        gba = chain("gba", f"{out}/p54/{d}_gba", start, sess, rep, f"p54 {d}")
        for b in builds:
            pcf = chain(b, f"{out}/p54/{d}_{b}", start, sess, rep, f"p54 {d}")
            if pcf and gba:
                check_cross(gba, pcf, rep, f"p54 {d}", b)
        if d == "d1":
            if not gba:
                return
            nxt = f"{out}/p54/d2_mgba_written.sav"
            shutil.copy(gba[1], nxt)
            rep.check(os.path.getsize(nxt) == 0x8000, "p54 d2 starts from mGBA's own 32 KiB save",
                      f"{os.path.getsize(nxt)} bytes")
            start = nxt


def sidecar(path):
    kv = {}
    try:
        with open(path + ".reforged") as fh:
            for line in fh:
                line = line.split("#", 1)[0].strip()
                if "=" in line:
                    k, v = line.split("=", 1)
                    kv[k.strip()] = v.strip()
    except OSError:
        return None
    return kv


def latest_suspend(sav):
    s = sram(sav)
    alt = s[0x63] == 1  # GlobalSaveInfo.last_suspend_slot: the suspend block written last
    name = "suspend_alt" if alt else "suspend"
    off = BD.info_entry(s, 4 if alt else 3)["offset"]
    return name, s, off


def game_unit(s, slot, pid):
    """(dead, supports) of character pid in game block slot (0-2), or None: GameSavePackedUnit's
    13-bit flag starts at bit 31 (PACKED_US_DEAD its bit 0); the supports at +0x1D."""
    e = BD.info_entry(s, slot)
    if not e["valid"]:
        return None
    for k in range(51):
        u = e["offset"] + 0x4C + k * BD.GAME_UNIT
        if s[u + 0x14] == pid:
            return bool(s[u + 3] & 0x80), list(s[u + 0x1D:u + 0x24])
    return None


def suspend_units(s, off):
    """{(faction, pid, n): (curHP, [item words 1-3])} of a suspend block's units (n: the n-th unit of
    that faction with that character id, as generic enemies share one)."""
    out = {}
    for fac, base, count in (("blue", 0x84, 51), ("red", 0xB14, 50), ("green", 0x153C, 10)):
        seen = {}
        for k in range(count):
            u = off + base + k * BD.SUS_UNIT
            pid = s[u]
            if pid:
                n = seen[pid] = seen.get(pid, -1) + 1
                items = [int.from_bytes(s[u + o:u + o + 2], "little") for o in (8, 10, 12)]
                out[(fac, pid, n)] = (s[u + 0xF], items)
    return out


def part_rfo(builds, out, start, rep):
    linux = [b for b in builds if b != "win"]
    if not linux:
        rep.check(False, "rfo: needs the full or the sdl build")
        return
    for b in linux:
        on = chain(b, f"{out}/rfo/{b}_on", start, RFO, rep, "rfo options on", rfo=RFO_SETTINGS)
        off = chain(b, f"{out}/rfo/{b}_off", start, RFO, rep, "rfo options off (control)")
        if not on or not off:
            continue
        check_slots(on, RFO, rep, "rfo options on", b)
        logs = [f[:-4] + ".log" for f in on]
        # the sidecar's fingerprints, after every session
        bad = []
        for f in on[1:]:
            rc = subprocess.run([sys.executable, "-I", os.path.join(HERE, "sidecar_check.py"), f],
                                capture_output=True, text=True)
            if rc.returncode != 0 or sidecar(f) is None:
                bad.append(f"{os.path.basename(f)}: {'no sidecar' if sidecar(f) is None else rc.stdout.strip()}")
        rep.check(not bad, f"rfo {b}: the sidecar's fingerprints match the .sav after every session", "; ".join(bad))
        # session 1: Franz (pid 4) falls on the resume and retreats; back in slot 2's Chapter 2 save
        log1 = open(logs[1], errors="replace").read()
        franz_on, franz_off = game_unit(sram(on[1]), 1, 4), game_unit(sram(off[1]), 1, 4)
        sc1 = sidecar(on[1]) or {}
        rep.check("FE8PC_RFO_TEST_FALL: pid 0x04 falls" in log1 and "pid 0x04 retreats" in log1
                  and franz_on and not franz_on[0] and franz_off and franz_off[0] and "game2_fallen" not in sc1,
                  f"rfo {b}: casual mode, a unit that fell in Chapter 1 is back in Chapter 2",
                  f"fall and retreat logged {'pid 0x04 retreats' in log1}; slot 2's Chapter 2 save: pid 0x04 "
                  + ("alive, supports %s" % franz_on[1] if franz_on and not franz_on[0] else "dead or missing")
                  + " (options off: " + ("dead, supports %s" % franz_off[1] if franz_off and franz_off[0]
                                         else "alive or missing") + f"); game2_fallen {sc1.get('game2_fallen', 'none')}")
        opts = sc1.get("game2", "")
        rep.check(all(o in opts for o in ("casual_mode=1", "unbreakable_weapons=2", "cheat_infinite_gold=1")),
                  f"rfo {b}: slot 2's mark names the gameplay options", f"game2 = {opts}")
        # session 2: Gilliam (pid 3) falls; the suspend written last keeps him on its fallen list
        log2 = open(logs[2], errors="replace").read()
        name2, s2, o2 = latest_suspend(on[2])
        sc2 = sidecar(on[2]) or {}
        rep.check("FE8PC_RFO_TEST_FALL: pid 0x03 falls" in log2 and "3" in sc2.get(name2 + "_fallen", "").split(","),
                  f"rfo {b}: a fall mid-chapter is on the suspend's fallen list",
                  f"{name2}_fallen = {sc2.get(name2 + '_fallen', 'none')}")
        # session 3: the list comes back on the resume; the battles' weapons keep their uses; the gold
        log3 = open(logs[3], errors="replace").read()
        name3, s3, o3 = latest_suspend(on[3])
        sc3 = sidecar(on[3]) or {}
        rep.check("pid 0x03 is on the fallen list" in log3 and "3" in sc3.get(name3 + "_fallen", "").split(","),
                  f"rfo {b}: the fallen list comes back with the suspend, and is saved again",
                  f"loaded {'pid 0x03 is on the fallen list' in log3}; {name3}_fallen = "
                  f"{sc3.get(name3 + '_fallen', 'none')}")
        cname, cs, co = latest_suspend(off[3])
        u_on, u_off = suspend_units(s3, o3), suspend_units(cs, co)
        more, fewer = [], []
        for key in sorted(set(u_on) & set(u_off)):
            for i, (a, c) in enumerate(zip(u_on[key][1], u_off[key][1])):
                if a != c and (a & 0xFF) == (c & 0xFF):
                    (more if a >> 8 > c >> 8 else fewer).append(f"{key[0]} pid 0x{key[1]:02X} item {i + 1} "
                                                                f"0x{c & 0xFF:02X} {c >> 8}->{a >> 8}")
        rep.check(bool(more) and not fewer, f"rfo {b}: unbreakable weapons, the battles' weapons keep their uses",
                  f"{len(more)} items with more uses than with the options off ({'; '.join(more[:4])})"
                  + (f"; FEWER: {'; '.join(fewer)}" if fewer else ""))
        gold_on = int.from_bytes(s3[o3 + 8:o3 + 12], "little")
        gold_off = int.from_bytes(cs[co + 8:co + 12], "little")
        rep.check(gold_on == 999999 and gold_off != 999999, f"rfo {b}: infinite gold in the saved game",
                  f"suspend gold {gold_on} (options off: {gold_off})")
        # sessions 4 and 5: Copy Data copies the mark, Erase Data drops it
        sc4, sc5 = sidecar(on[4]) or {}, sidecar(on[5]) or {}
        rep.check(sc4.get("game3") == sc4.get("game2") and bool(sc4.get("game3")) and "game3" not in sc5
                  and "game2" in sc5, f"rfo {b}: Copy Data copies slot 2's mark to slot 3, Erase Data drops it",
                  f"after the copy game3 = {sc4.get('game3')}; after the erase game3 "
                  f"{'gone' if 'game3' not in sc5 else 'still there'}, game2 {'kept' if 'game2' in sc5 else 'gone'}")
        # the options-on .sav, without its sidecar, on mGBA and on the PC with the options off
        if b != linux[0]:
            continue
        last = f"{out}/rfo/mgba_start.sav"
        shutil.copy(on[-1], last)
        sess = [("p54_k2_wait_end", set())]
        gba = chain("gba", f"{out}/rfo/mgba", last, sess, rep, "rfo: the options-on .sav on mGBA")
        pcf = chain(b, f"{out}/rfo/mgba_pc_off", last, sess, rep, "rfo: the options-on .sav, options off")
        if gba and pcf:
            sm = BD.block_summary(sram(gba[1]), 3 if sram(gba[1])[0x63] == 0 else 4)
            start_turn = BD.block_summary(sram(last), 3 if sram(last)[0x63] == 0 else 4).get("turn")
            rep.check(sm.get("valid") and sm.get("turn", 0) > (start_turn or 0),
                      "rfo: mGBA loads the options-on .sav and plays on (its own suspend saves)",
                      f"mGBA's last suspend: chapter {sm.get('chapterIndex')} turn {sm.get('turn')} "
                      f"(the file's: turn {start_turn})")
            check_cross(gba, pcf, rep, "rfo: the options-on .sav", b + " (options off)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--builds", default="full,sdl,win")
    ap.add_argument("--parts", default="p53,p53b,p54,rfo")
    ap.add_argument("--out", default="build/savechk")
    ap.add_argument("--start", default="port/test/saves/ch1_seize.sav")
    a = ap.parse_args()
    if not os.path.isfile("port/test/tools/savechk.sh"):
        print("savechk_all.py: run from the repository root", file=sys.stderr)
        return 2
    builds = []
    exe = {"full": "build/pc-full/fe8pc", "sdl": "build/pc-sdl/fe8pc-sdl", "win": "build/win32-sdl/fe8pc-sdl.exe"}
    for b in [x for x in a.builds.split(",") if x]:
        if b not in exe:
            print(f"savechk_all.py: unknown build {b}", file=sys.stderr)
            return 2
        if not os.path.exists(exe[b]):
            print(f"NOTE {b}: not run ({exe[b]} is not built)")
            continue
        if b == "win" and not any(open(f"/proc/sys/fs/binfmt_misc/{n}").readline().strip() == "enabled"
                                  for n in ("WSLInterop", "WSLInterop-late")
                                  if os.path.exists(f"/proc/sys/fs/binfmt_misc/{n}")):
            print("NOTE win: not run (Windows programs cannot run from here: not WSL, or its interop is off)")
            continue
        builds.append(b)
    parts = [x for x in a.parts.split(",") if x]
    out = a.out
    if not os.path.exists(a.start):
        os.makedirs(os.path.dirname(a.start) or ".", exist_ok=True)
        name = os.path.splitext(os.path.basename(a.start))[0]
        print(f"making {a.start}: sram-capture of route_ch1.keys at suspend:1 (README.md, 'Chapter saves')", flush=True)
        if run(["make", "-f", "port/test/Makefile", "sram-capture", "SAVE_KEYS=port/test/inputs/route_ch1.keys",
                "SAVE_AT=suspend:1", f"SAVE_NAME={name}"], f"{a.start}.capture.log") != 0 or \
                os.path.dirname(os.path.abspath(a.start)) != os.path.abspath("port/test/saves"):
            print(f"savechk_all.py: could not make {a.start} (see {a.start}.capture.log)", file=sys.stderr)
            return 2
    import hashlib
    sha = hashlib.sha1(open(a.start, "rb").read()[:0x8000]).hexdigest()
    if sha != START_SHA1:
        print(f"savechk_all.py: {a.start} is not the ch1_seize save the key files were made from "
              f"(SHA-1 {sha}, want {START_SHA1})", file=sys.stderr)
        return 2
    os.makedirs(out, exist_ok=True)
    start = os.path.abspath(f"{out}/ch1_seize.sav")
    shutil.copy(a.start, start)
    print(f"save checks: parts {' '.join(parts)}; builds {' '.join(builds) or '(none)'} and mGBA; "
          f"start {a.start} (SHA-1 {sha[:8]}); outputs in {out}/", flush=True)
    rep = Report()
    p53 = None
    if "p53" in parts or "p54" in parts:
        p53 = part_chain("p53", P53, builds if "p53" in parts else [b for b in builds if b == "full"], out, start, rep)
    if "p53b" in parts:
        part_chain("p53b", P53B, builds, out, start, rep)
    if "p54" in parts:
        part_p54(builds, out, p53, rep)
    if "rfo" in parts:
        part_rfo(builds, out, start, rep)
    if rep.bad:
        print(f"SAVE CHECKS: FAIL ({len(rep.bad)} of {rep.n} checks failed: {'; '.join(rep.bad[:3])}"
              f"{' ...' if len(rep.bad) > 3 else ''})")
        return 1
    print(f"SAVE CHECKS: PASS ({rep.n} checks; builds {' '.join(builds)} and mGBA)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
