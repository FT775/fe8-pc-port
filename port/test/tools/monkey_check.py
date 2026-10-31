#!/usr/bin/env python3
"""Menu monkey tools (port/test/README.md, "Menu monkey").

  monkey_check.py start START
      Shell assignments for `make monkey`: the prefix key file and tick count,
      the save to boot (GATE0_SRAM_IN) and a name, for START = a .sav, a save
      state directory DIR/tick<T> (its key file's first T ticks and its save;
      the state itself is not loaded), or power-on.

  monkey_check.py info COVERAGE.json
      Shell assignments for `make monkey-check`: ticks, sav.

  monkey_check.py replay --digest X.trace.jsonl --gba GBA.jsonl [--keys X.keys]
      The monkey (oracle/menu_monkey.lua, MONKEY_TRACE) wrote the Gate 0 trace
      fields of every tick it played while it made the key file. This compares
      them with the normal Gate 0 capture (gate0_capture.lua, debugger
      breakpoints) of that key file, every field of every tick, frame and lag
      counts included (gBmSt.prevVCount, the raster's write-only line counter,
      left out as in the Gate 0 compare): same buttons in, same GBA game out.
      The capture samples a tick at VBlankIntrWait, the monkey at the frame
      callback just after it; on an early flush tick (the capture's
      "early_flush") the VBlank came between the two, so such ticks are listed,
      not compared.

  monkey_check.py frames --gba-dir DIR --pc-dir DIR --gba GBA.jsonl --digest X.trace.jsonl --out F.jsonl
                         [--pc PC.jsonl] [--chunk N] [--jobs J]
      Every frame of a run: GBA tickNNNNNN.png against the PC's
      tickNNNNNN.555 in 15-bit space (as tools/frame15.py), in chunks of N
      ticks (default 1,000) on J processes (default 2). Each differing frame
      gets its pixel and line counts, whether the PC frame equals the GBA's
      frame one or two ticks before or after, the GBA lag around it and the
      screen the monkey was on, and a label. First the exact rule of
      gate0_compare.py (README.md, "Raster-timing frames"; decision 9), when
      --pc is the PC trace of a run with GATE0_RASTER_FROM: "raster-timing
      frame (P-17)" when the PC redrew the frame with the GBA's timing
      ("raster": "gba", from the GBA trace's own raster log block) and that
      redraw, tickNNNNNN.raster.555, is exactly the GBA's frame. Otherwise the
      heuristics: "phase banner (P-17)", "minimap (P-17 family)", "after GBA
      lag", "busy menu tick" (one frame in a menu or help-box tick), or
      "other" (to look at); each such frame also gets the PC's raster mark
      and why it is not a raster-timing frame. Exit 0 only when no frame
      differs or every differing frame is a raster-timing frame. Needs numpy.

  monkey_check.py same --a A.jsonl --b B.jsonl
      Two PC traces of the same keys (the route pass and the every-frame
      pass) must be the same run: every field but the frames (frame15 and the
      raster-timing redraw's keys, which only the frames decide).

  monkey_check.py sites SITES.jsonl
      The NULL probe's table (FE8PC_NULLPROBE_OUT): its sites, and whether the
      run ended normally.

Exit status: 0 equal, 1 different, 2 bad input.
"""
import argparse
import glob
import json
import os
import shlex
import sys
import struct
import zlib

COMPARED = ["frame", "vblanks", "keys", "gGameClock", "gKeyStatusIgnoredSt", "gKeyComboResetEN",
            "gSoftwareResetFlag", "gMainCallback", "gLCGRNValue", "gBmSt", "gPlaySt", "gRNSeeds",
            "sKeyStatusBuffer", "gLCDControlBuffer", "gPaletteBuffer", "gBG0TilemapBuffer", "gBG1TilemapBuffer",
            "gBG2TilemapBuffer", "gBG3TilemapBuffer", "gOam"]
W, H = 240, 160
# Keys the raster-timing redraw adds to a PC tick line (GATE0_RASTER_FROM; as RASTER_KEYS in gate0_compare.py).
RASTER_KEYS = ("raster", "raster_rec", "raster_note", "frame15_raster")
RASTER_LABEL = "raster-timing frame (P-17)"


def sh(**kv):
    return "\n".join(f"{k}={shlex.quote(str(v))}" for k, v in kv.items())


def cmd_start(a):
    s = a.start
    if s == "power-on":
        print(sh(prefix="port/test/inputs/route_ch0_prologue.keys", ptick=1984, sav="", label="power-on",
                 name="poweron"))
        return 0
    if os.path.isfile(os.path.join(s, "gba", "meta.json")):
        meta = json.load(open(os.path.join(s, "gba", "meta.json")))
        hdr = meta.get("sram_header") or ""
        sav = ""
        if hdr:
            sav = json.loads("{" + hdr.lstrip(",") + "}").get("sram_in", "")
        snap = os.path.basename(os.path.dirname(os.path.abspath(s).rstrip("/"))) + "_" + os.path.basename(s.rstrip("/"))
        print(sh(prefix=meta["keys"], ptick=meta["tick"], sav=sav,
                 label=f"snapshot {snap} (its keys to tick {meta['tick']}, from power-on)", name=snap))
        return 0
    if os.path.isfile(s):
        name = os.path.splitext(os.path.basename(s))[0]
        print(sh(prefix="port/test/inputs/resume.keys", ptick=2000, sav=os.path.abspath(s),
                 label=os.path.basename(s), name=name))
        return 0
    print(f"monkey: START={s} is not a .sav, a save state directory DIR/tick<T> or power-on", file=sys.stderr)
    return 2


def cmd_info(a):
    c = json.load(open(a.coverage))
    print(sh(ticks=c["ticks"], sav=c.get("sram_in", "")))
    return 0


def load(path):
    head, ticks = None, {}
    with open(path) as f:
        for line in f:
            if not line.strip():
                continue
            o = json.loads(line)
            if "tick" in o and "source" not in o:
                ticks[o["tick"]] = o
            elif head is None:
                head = o
    return head, ticks


def bmst_cmp(v):
    return v[:12] + v[16:]          # gBmSt without prevVCount (bytes 6-7)


def read_keys(path):
    masks = []
    for line in open(path):
        line = line.split("#")[0].strip()
        if not line:
            continue
        m, _, n = line.partition("*")
        masks += [int(m, 16)] * (int(n) if n else 1)
    return masks


def cmd_replay(a):
    dh, dt = load(a.digest)
    gh, gt = load(a.gba)
    if not dt or not gt:
        print("monkey_check: a trace is empty", file=sys.stderr)
        return 2
    print(f"digest: {len(dt)} ticks (monkey header: {dh.get('ticks')}); GBA capture: {len(gt)} ticks"
          f"{', sram_fnv ' + gh['sram_fnv'] if gh and 'sram_fnv' in gh else ''}")
    if gh and "sram_fnv" in gh and gh["sram_fnv"] != dh.get("sram_fnv"):
        print(f"MONKEY REPLAY: DIFFERENT: the capture booted from other SRAM ({gh['sram_fnv']}) than the monkey "
              f"({dh.get('sram_fnv')})")
        return 1
    if a.keys:
        masks = read_keys(a.keys)
        bad = [t for t in sorted(dt) if t < len(masks) and int(dt[t]["keys"], 16) != masks[t]]
        print(f"key file: {len(masks)} ticks; the digest's keys {'are its keys' if not bad else 'DIFFER at ' + str(bad[:5])}")
        if bad:
            return 1
    early = sorted(t for t, o in gt.items() if o.get("early_flush"))
    n = min(len(dt), len(gt))
    for t in range(n):
        d, g = dt.get(t), gt.get(t)
        if d is None or g is None:
            print(f"MONKEY REPLAY: DIFFERENT: tick {t} missing on {'the monkey' if d is None else 'the capture'}")
            return 1
        if t in early:
            continue
        diffs = []
        for f in COMPARED:
            dv, gv = d.get(f), g.get(f)
            if f == "gBmSt" and dv and gv:
                dv, gv = bmst_cmp(dv), bmst_cmp(gv)
            if dv != gv:
                diffs.append((f, dv, gv))
        if diffs:
            print(f"MONKEY REPLAY: DIFFERENT at tick {t}:")
            for f, dv, gv in diffs:
                print(f"  {f}: monkey {dv} capture {gv}")
            return 1
    if early:
        print(f"early flush ticks (a VBlank between the two sample points; not compared): {early}")
    if len(dt) != len(gt):
        print(f"MONKEY REPLAY: DIFFERENT: the traces have {len(dt)} and {len(gt)} ticks")
        return 1
    print(f"MONKEY REPLAY: MATCH on ticks 0-{n - 1} ({len(COMPARED)} fields a tick, {n - len(early)} ticks compared)")
    return 0


# --- every frame ------------------------------------------------------------

def png_555(path):
    import numpy as np
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, ctype = 8, b"", 2
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, inter = struct.unpack(">IIBBBBB", body)
            if (w, h, depth, inter) != (W, H, 8, 0) or ctype not in (2, 6):
                raise ValueError(f"{path}: unsupported PNG")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    bpp = 3 if ctype == 2 else 4
    stride = W * bpp
    raw = np.frombuffer(zlib.decompress(idat), dtype=np.uint8).reshape(H, 1 + stride)
    out = np.zeros((H, stride), dtype=np.int32)
    prev = np.zeros(stride, dtype=np.int32)
    for y in range(H):
        f = raw[y, 0]
        line = raw[y, 1:].astype(np.int32)
        if f == 0:
            cur = line
        elif f == 2:
            cur = (line + prev) & 255
        elif f == 1:
            cur = np.cumsum(line.reshape(W, bpp), axis=0).reshape(-1) & 255
        else:
            cur = line.tolist()
            pv = prev.tolist()
            for i in range(stride):
                a_ = cur[i - bpp] if i >= bpp else 0
                b_ = pv[i]
                c_ = pv[i - bpp] if i >= bpp else 0
                if f == 3:
                    cur[i] = (cur[i] + ((a_ + b_) >> 1)) & 255
                else:
                    pa, pb, pc = abs(b_ - c_), abs(a_ - c_), abs(a_ + b_ - 2 * c_)
                    cur[i] = (cur[i] + (a_ if pa <= pb and pa <= pc else (b_ if pb <= pc else c_))) & 255
            cur = np.array(cur, dtype=np.int32)
        out[y] = cur
        prev = cur
    px = out.reshape(H, W, bpp)
    return ((px[:, :, 0] >> 3) | ((px[:, :, 1] >> 3) << 5) | ((px[:, :, 2] >> 3) << 10)).astype(np.uint16).reshape(-1)


def raw_555(path):
    import numpy as np
    return (np.fromfile(path, dtype="<u2") & 0x7FFF).astype(np.uint16)


def _chunk(job):
    import numpy as np
    gdir, pdir, ticks, margin = job
    cache = {}

    def gba(t):
        if t not in cache:
            p = os.path.join(gdir, f"tick{t:06d}.png")
            cache[t] = png_555(p) if os.path.exists(p) else None
        return cache[t]

    out = []
    for t in ticks:
        g = gba(t)
        pp = os.path.join(pdir, f"tick{t:06d}.555")
        if g is None:
            continue
        if not os.path.exists(pp):
            out.append({"tick": t, "missing_pc": True})
            continue
        p = raw_555(pp)
        d = np.nonzero(g != p)[0]
        if len(d) == 0:
            continue
        lines = np.unique(d // W)
        lo, hi = int(lines[0]), int(lines[-1])
        near, near_lines = [], []
        for k in (-1, 1, -2, 2):
            n = gba(t + k) if margin else None
            if n is None:
                continue
            if np.array_equal(n, p):
                near.append(k)
            elif np.array_equal(n[lo * W:(hi + 1) * W], p[lo * W:(hi + 1) * W]):
                near_lines.append(k)
        row = {"tick": t, "differing_pixels": int(len(d)), "first_line": lo,
               "last_line": hi, "differing_lines": int(len(lines)),
               "pc_equals_gba": [f"t{k:+d}" for k in near],
               "pc_lines_equal_gba": [f"t{k:+d}" for k in near_lines]}
        # The raster-timing redraw (GATE0_RASTER_FROM), written only when its mark is "gba".
        rp = os.path.join(pdir, f"tick{t:06d}.raster.555")
        if os.path.exists(rp):
            row["raster_redraw_equals_gba"] = bool(np.array_equal(g, raw_555(rp)))
        out.append(row)
    return out


MENU_STATES = {"menu", "helpbox", "statscreen", "unitlist", "trade", "supply", "shop", "guide", "options",
               "chapstatus", "savemenu", "supportscreen", "target", "menu-frozen", "prep", "wmbase", "range", "free",
               "pp-busy"}
WORLD_MAP_STATES = {"worldmap", "generic", "wmbase"}


def cmd_frames(a):
    from multiprocessing import Pool
    gh, gt = load(a.gba)
    _, dt = load(a.digest) if a.digest else (None, {})
    _, pt = load(a.pc) if a.pc else (None, {})
    ticks = sorted(int(os.path.basename(g)[4:10]) for g in glob.glob(os.path.join(a.gba_dir, "tick*.png")))
    if not ticks:
        print("monkey_check frames: no GBA frames", file=sys.stderr)
        return 2
    jobs = [(a.gba_dir, a.pc_dir, ticks[i:i + a.chunk], True) for i in range(0, len(ticks), a.chunk)]
    diffs = []
    with Pool(a.jobs) as pool:
        for res in pool.imap(_chunk, jobs):
            diffs += res
    # Runs of differing frames in a row with about the same lines: a lasting difference, not one frame.
    diffs.sort(key=lambda r: r["tick"])
    run = 0
    for i, r in enumerate(diffs):
        prev = diffs[i - 1] if i else None
        if (prev and prev["tick"] == r["tick"] - 1 and not r.get("missing_pc") and not prev.get("missing_pc")
                and abs(prev["first_line"] - r["first_line"]) <= 4 and abs(prev["last_line"] - r["last_line"]) <= 4):
            run += 1
        else:
            run = 1
        r["run"] = run
    for i in range(len(diffs) - 2, -1, -1):          # every frame of a run gets the run's length
        if diffs[i + 1]["run"] == diffs[i]["run"] + 1:
            diffs[i]["run_length"] = diffs[i + 1].get("run_length", diffs[i + 1]["run"])
    for r in diffs:
        r["run_length"] = r.get("run_length", r["run"])
        del r["run"]
    labels = {}
    for r in diffs:
        t = r["tick"]
        if r.get("missing_pc"):
            r["label"] = "no PC frame"
        else:
            d = dt.get(t, {})
            lag = [k for k in (t - 1, t) if gt.get(k, {}).get("vblanks", 1) > 1]
            r["gba_lag_ticks"] = lag
            st = r["monkey_state"] = d.get("monkey")
            p, g = pt.get(t, {}), gt.get(t, {})
            mark = p.get("raster")
            if mark is not None:
                r["raster"] = mark
                why = None
                if mark != "gba":
                    why = p.get("raster_note") or mark
                elif p.get("raster_rec") != g.get("raster"):
                    why = f"redrawn from raster log block {p.get('raster_rec')}, not the GBA trace's {g.get('raster')}"
                elif not r.get("raster_redraw_equals_gba"):
                    why = "with the GBA's timing the PC's frame is still not the GBA's"
                if why:
                    r["raster_why"] = why
            if mark == "gba" and "raster_why" not in r:
                r["label"] = RASTER_LABEL
            elif d.get("phase_intro"):
                r["label"] = "phase banner (P-17)"
            elif d.get("minimap"):
                r["label"] = "minimap (P-17 family)"
            elif lag:
                r["label"] = "after GBA lag"
            elif r["differing_lines"] >= 100 and not r["pc_equals_gba"]:
                r["label"] = "other"                 # a whole frame that matches no GBA frame near it
            elif st in WORLD_MAP_STATES and r["last_line"] <= 31:
                r["label"] = "world-map name box (P-17)"
            elif st in MENU_STATES and r["run_length"] <= 3:
                r["label"] = "busy menu tick"
            else:
                r["label"] = "other"
        labels[r["label"]] = labels.get(r["label"], 0) + 1
    with open(a.out, "w") as f:
        for r in diffs:
            f.write(json.dumps(r) + "\n")
    print(f"frames: {len(ticks)} GBA frames compared in {len(jobs)} chunks of {a.chunk}; {len(diffs)} differ"
          f"{': ' + ', '.join(f'{k} {v}' for k, v in sorted(labels.items())) if labels else ''} -> {a.out}")
    others = [r for r in diffs if r.get("label") == "other"]
    for r in others[:30]:
        print(f"  other: tick {r['tick']}: {r['differing_pixels']} px, lines {r['first_line']}-{r['last_line']},"
              f" PC = GBA {r['pc_equals_gba'] or 'none of t-2..t+2'}"
              f"{' (those lines: ' + ','.join(r['pc_lines_equal_gba']) + ')' if r.get('pc_lines_equal_gba') else ''},"
              f" state {r.get('monkey_state')}{', a run of %d frames' % r['run_length'] if r['run_length'] > 1 else ''}"
              f"{', raster ' + r['raster'] + ': ' + r.get('raster_why', '') if r.get('raster') else ''}")
    if not a.pc:
        print("frames: no --pc trace, so no frame can be a raster-timing frame")
    return 0 if all(r.get("label") == RASTER_LABEL for r in diffs) else 1


def cmd_same(a):
    """Two traces of the same keys (the route pass and the every-frame pass): every field but the frames."""
    _, at = load(a.a)
    _, bt = load(a.b)
    skip = {"frame15", "frame15_pc", "affine", *RASTER_KEYS}
    n = min(len(at), len(bt))
    for t in range(n):
        x, y = at.get(t, {}), bt.get(t, {})
        keys = (set(x) | set(y)) - skip
        bad = sorted(k for k in keys if x.get(k) != y.get(k))
        if bad:
            print(f"SAME RUN: DIFFERENT at tick {t}: {bad[:6]}")
            return 1
    if len(at) != len(bt):
        print(f"SAME RUN: DIFFERENT: {len(at)} and {len(bt)} ticks")
        return 1
    print(f"SAME RUN: the every-frame pass equals the route pass on every field but the frames, ticks 0-{n - 1}")
    return 0


def cmd_sites(a):
    head = None
    rows = []
    if os.path.exists(a.sites):
        for line in open(a.sites):
            if line.strip():
                o = json.loads(line)
                if head is None and "nullprobe" in o:
                    head = o
                else:
                    rows.append(o)
    if head is None:
        print(f"NULL probe: no table ({a.sites})")
        return 1
    # Writes to the NULL page that H-1 already drops, as the GBA ignores them (DIVERGENCES.md H-1), are
    # listed but pass; any other site (a read, a division, an undecided latch) fails the check.
    h1 = [r for r in rows if r.get("how") == "h1-drop"]
    other = [r for r in rows if r.get("how") != "h1-drop"]
    print(f"NULL probe: {head.get('sites', len(rows))} sites ({len(h1)} H-1 write drops, {len(other)} other), "
          f"{head.get('hits', 0)} hits, run end {head.get('end')} at tick {head.get('tick')} ({a.sites}; "
          f"tools: port/tools/nullprobe.py report)")
    for r in other[:20]:
        print("  " + json.dumps(r)[:300])
    return 0 if not other and head.get("end") == "exit" else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("start")
    s.add_argument("start")
    i = sub.add_parser("info")
    i.add_argument("coverage")
    r = sub.add_parser("replay")
    r.add_argument("--digest", required=True)
    r.add_argument("--gba", required=True)
    r.add_argument("--keys")
    fr = sub.add_parser("frames")
    fr.add_argument("--gba-dir", required=True)
    fr.add_argument("--pc-dir", required=True)
    fr.add_argument("--gba", required=True)
    fr.add_argument("--digest")
    fr.add_argument("--pc")
    fr.add_argument("--out", required=True)
    fr.add_argument("--chunk", type=int, default=1000)
    fr.add_argument("--jobs", type=int, default=2)
    sm = sub.add_parser("same")
    sm.add_argument("--a", required=True)
    sm.add_argument("--b", required=True)
    st = sub.add_parser("sites")
    st.add_argument("sites")
    a = ap.parse_args()
    return {"start": cmd_start, "info": cmd_info, "replay": cmd_replay, "frames": cmd_frames, "same": cmd_same,
            "sites": cmd_sites}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
