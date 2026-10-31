#!/usr/bin/env python3
"""Compare two FE8 saves (SRAM images) block by block, as the game lays them out.

    python3 port/test/tools/sram_blockdiff.py A.sav B.sav [--cross] [--json]
    python3 port/test/tools/sram_blockdiff.py --summary A.sav

port/test/README.md, "Save checks (P5.3, P5.4)". The layout is the game's
(include/bmsave.h struct SaveBlocks, include/sram-layout.h; sizes and offsets
as agbcc lays them out, port/save/layout_asserts.c):

    0x0000 GlobalSaveInfo (0x64)         0x0064 SaveBlockInfo[7] (0x10 each)
    0x00D4 SuspendSaveBlock[2] (0x1F78)  0x3FC4 GameSaveBlock[3] (0xDC8)
    0x691C MultiArenaSaveBlock           0x7190 GameRankSaveDataPacks
    0x7224 SoundRoomSaveData             0x7248 bmsave_unkstruct2
    0x725C BonusClaimSaveData            0x73A0 reserved[4] (the SRAM test)
    0x73A4 padding                       0x7400 ExtraMapSaveHead (XMAP) to 0x7FFF

Each region is reported equal or different, with the differing bytes named
by field (PlaySt, ActionData, unit slot n, ...). Each SaveBlockInfo entry is
also checked as the game checks it (magic16, the kind's magic32, offset and
size inside SRAM, Checksum32 of the block's bytes).

Two uses:
- the same build before and after one session (default): which regions a
  write changed, so a write to one slot is seen to leave every other slot's
  bytes alone. Exit 0 always (the report is the result).
- --cross: a PC save against a GBA (mGBA) save made with the same keys. The
  bytes known to differ between the two machines are named and not counted
  (DIVERGENCES.md P-12 cause 3 and P-18): the clock fields of each saved
  PlaySt (time_saved, time_chapter_started: GetGameClock counts the GBA's lag
  VBlanks, the lag-free PC has none), the checksums over them, the
  SaveBlockInfo pad byte (+0x07) and the magic32 that InvalidateSuspendSave
  leaves unset (an entry of kind 0xFF), and the bytes retail writes from
  uninitialised stack memory (P-18): the last supplyItems byte, the spare bits
  4-7 of GMapSaveInfo byte +0x1C and its padding +0x21..+0x23, gmUnit bytes 6-7
  (WriteGMMonsterRnState saves a u32[2] of which 3 halfwords are set, and
  SetGmMonsterRnState reads back only those 3), and the GameSaveBlock padding. Exit 1 when any other byte differs, else 0.
- --cross --line: the same verdict as one line, "SRAM CROSS: IDENTICAL ...",
  "SRAM CROSS: EQUAL apart from the known bytes (...)" or "SRAM CROSS:
  DIFFERENT (...)", with the known bytes counted by kind and the first bytes
  not known named; the long-test route rows print it (port/test/Makefile
  route-sram, port/check-all.mk). Same exit status.

--block A:REGION B:REGION compares one block with another of the same kind (game.N or
suspend.N), for example the suspend block a Resume Chapter loaded with the one the game
wrote right after it from the resumed state: every byte that differs is listed by field.

--summary FILE prints what each game and suspend block holds (chapter, turn,
phase, gold, slot, the units by character id and HP) and exits.
Only reads the files. Exit 2 on a bad file.
"""
import argparse
import json
import struct
import sys

SRAM_SIZE = 0x8000
SAVEMAGIC16 = 0x200A
SAVEMAGIC32 = 0x40624
MAGIC32 = {0: SAVEMAGIC32, 1: SAVEMAGIC32, 2: SAVEMAGIC32, 3: SAVEMAGIC32, 4: SAVEMAGIC32,
           5: 0x20112, 6: 0x20223}
INFO_NAMES = ["game1", "game2", "game3", "suspend", "suspend_alt", "arena", "xmap"]

SUSPEND_SIZE = 0x1F78
GAME_SIZE = 0xDC8
SUS_UNIT = 0x34
GAME_UNIT = 0x24

# (start, size, name) of each top-level region, in SRAM order.
REGIONS = [(0x0000, 0x64, "header")]
REGIONS += [(0x64 + i * 0x10, 0x10, "info." + INFO_NAMES[i]) for i in range(7)]
REGIONS += [(0x00D4, SUSPEND_SIZE, "suspend.0"), (0x00D4 + SUSPEND_SIZE, SUSPEND_SIZE, "suspend.1")]
REGIONS += [(0x3FC4 + i * GAME_SIZE, GAME_SIZE, "game.%d" % i) for i in range(3)]
REGIONS += [(0x691C, 0x7190 - 0x691C, "arena"), (0x7190, 0x7224 - 0x7190, "ranks"),
            (0x7224, 0x7248 - 0x7224, "soundroom"), (0x7248, 0x725C - 0x7248, "unkstruct2"),
            (0x725C, 0x73A0 - 0x725C, "bonusclaim"), (0x73A0, 4, "reserved"),
            (0x73A4, 0x7400 - 0x73A4, "pad"), (0x7400, 0x8000 - 0x7400, "xmap")]

PLAYST_FIELDS = [(0x00, 4, "time_saved"), (0x04, 4, "time_chapter_started"), (0x08, 4, "partyGoldAmount"),
                 (0x0C, 1, "gameSaveSlot"), (0x0D, 1, "chapterVisionRange"), (0x0E, 1, "chapterIndex"),
                 (0x0F, 1, "faction"), (0x10, 2, "chapterTurnNumber"), (0x12, 2, "cursor"),
                 (0x14, 1, "chapterStateBits"), (0x15, 1, "chapterWeatherId"),
                 (0x16, 2, "chapterTotalSupportGain"), (0x18, 4, "ids"), (0x1C, 4, "unk1C"),
                 (0x20, 0x0B, "playerName"), (0x2B, 0x21, "flags/config")]

# Suspend and game block members (offset, size, name, element size or 0).
SUSPEND_MEMBERS = [(0x000, 0x4C, "playSt", 0), (0x04C, 0x38, "action", 0),
                   (0x084, 51 * SUS_UNIT, "blueUnits", SUS_UNIT), (0xAE0, SUS_UNIT, "wmMonsterUnit", 0),
                   (0xB14, 50 * SUS_UNIT, "redUnits", SUS_UNIT), (0x153C, 10 * SUS_UNIT, "greenUnits", SUS_UNIT),
                   (0x1744, 0x200, "traps", 8), (0x1944, 0xB0, "supplyItems", 0),
                   (0x19F4, 0x460, "pidStats", 0x10), (0x1E54, 0xC0, "chapterStats", 4),
                   (0x1F14, 0x10, "menuOverride", 0), (0x1F24, 0x19, "permanentFlags", 0),
                   (0x1F3D, 7, "chapterFlags", 0), (0x1F44, 0x24, "wmStuff", 0), (0x1F68, 0xC, "dungeon", 0),
                   (0x1F74, 4, "eventSlotCnt", 0)]
GAME_MEMBERS = [(0x000, 0x4C, "playSt", 0), (0x04C, 51 * GAME_UNIT, "units", GAME_UNIT),
                (0x778, GAME_UNIT, "gmUnit", 0), (0x79C, 0xB0, "supplyItems", 0),
                (0x84C, 0x460, "pidStats", 0x10), (0xCAC, 0xC0, "chapterStats", 4),
                (0xD6C, 0x19, "permanentFlags", 0), (0xD85, 3, "pad", 0), (0xD88, 4, "bonusClaimFlags", 0),
                (0xD8C, 0x24, "wmStuff", 0), (0xDB0, 0x18, "dungeons", 0xC)]

# Bytes known to differ between a PC save and a GBA save made with the same keys
# (--cross), by region kind: offsets inside the block.
KNOWN_SUSPEND = {**{o: "clock" for o in range(0, 8)}, 0x19F3: "uninit (supplyItems last byte, P-18)",
                 0x1F60: "uninit (GMapSaveInfo +0x1C bits 4-7, P-18)",
                 **{o: "uninit (GMapSaveInfo pad, P-18)" for o in (0x1F65, 0x1F66, 0x1F67)}}
KNOWN_GAME = {**{o: "clock" for o in range(0, 8)}, 0x84B: "uninit (supplyItems last byte, P-18)",
              0x77E: "uninit (gmUnit bytes 6-7, P-18)", 0x77F: "uninit (gmUnit bytes 6-7, P-18)",
              0xDA8: "uninit (GMapSaveInfo +0x1C bits 4-7, P-18)",
              0xD85: "uninit (pad, P-18)", 0xD86: "uninit (pad, P-18)", 0xD87: "uninit (pad, P-18)",
              **{o: "uninit (GMapSaveInfo pad, P-18)" for o in (0xDAD, 0xDAE, 0xDAF)}}


def checksum16(data):
    add = xor = 0
    for (w,) in struct.iter_unpack("<H", data):
        add += w
        xor ^= w
    return (add + xor) & 0xFFFF


def checksum32(data):
    add = xor = 0
    for (w,) in struct.iter_unpack("<H", data[: len(data) & ~1]):
        add += w
        xor ^= w
    return ((add & 0xFFFF) + (xor << 16)) & 0xFFFFFFFF


def load(path):
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) not in (SRAM_SIZE, 2 * SRAM_SIZE):
        raise SystemExit(f"sram_blockdiff: {path}: {len(data)} bytes, want 32 KiB or 64 KiB")
    return data[:SRAM_SIZE], len(data)


def info_entry(sram, i):
    m32, m16, kind, pad, off, size, c32 = struct.unpack_from("<IHBbHHI", sram, 0x64 + i * 0x10)
    ok = m16 == SAVEMAGIC16 and m32 == MAGIC32[i] and off + size <= SRAM_SIZE and kind != 0xFF
    valid = bool(ok and checksum32(sram[off:off + size]) == c32)
    return {"magic32": m32, "kind": kind, "pad": pad & 0xFF, "offset": off, "size": size, "checksum32": c32,
            "valid": valid}


def header_ok(sram):
    marker = sram[0:8].split(b"\0")[0]
    magic32, magic16 = struct.unpack_from("<IH", sram, 8)
    return (marker == b"AGB-FE9" and magic32 == SAVEMAGIC32 and magic16 == SAVEMAGIC16
            and struct.unpack_from("<H", sram, 0x60)[0] == checksum16(sram[:0x50]))


def field_name(region, rel):
    """Name of the byte at rel inside region."""
    if region == "header":
        if rel < 8:
            return "name"
        if rel < 0x0E:
            return "magic"
        if rel < 0x14:
            return "flags 0x0E-0x13"
        if rel < 0x20:
            return "cleared_playthroughs"
        if rel < 0x40:
            return "SuppordRecord"
        if rel < 0x60:
            return "charKnownFlags"
        return {0x60: "checksum16", 0x61: "checksum16", 0x62: "last_game_save_id", 0x63: "last_suspend_slot"}[rel]
    if region.startswith("info."):
        return ("magic32" if rel < 4 else "magic16" if rel < 6 else "kind" if rel == 6 else "_pad_07"
                if rel == 7 else "offset" if rel < 0xA else "size" if rel < 0xC else "checksum32")
    members = SUSPEND_MEMBERS if region.startswith("suspend.") else GAME_MEMBERS if region.startswith("game.") else None
    if members is None:
        return region
    for off, size, name, elem in members:
        if off <= rel < off + size:
            if name == "playSt":
                for fo, fs, fn in PLAYST_FIELDS:
                    if fo <= rel < fo + fs:
                        return "playSt." + fn
            if elem:
                return "%s[%d]+0x%X" % (name, (rel - off) // elem, (rel - off) % elem)
            return "%s+0x%X" % (name, rel - off)
    return "?"


def known_class(region, rel, a, b, start=0):
    """--cross: why a byte may differ between a PC and a GBA save, or None."""
    if region.startswith("suspend.") or region.startswith("game."):
        cls = (KNOWN_SUSPEND if region.startswith("suspend.") else KNOWN_GAME).get(rel)
        # only the spare high bits of the GMapSaveInfo bitfield byte
        if cls and "bits 4-7" in cls and (a[start + rel] ^ b[start + rel]) & 0x0F:
            return None
        return cls
    if region == "header" and rel in (0x60, 0x61):
        return "checksum"
    if region.startswith("info."):
        i = INFO_NAMES.index(region[5:])
        if rel >= 0x0C:
            return "checksum"
        if rel == 7:
            return "uninit (SaveBlockInfo pad, P-12)"
        if rel < 4 and (info_entry(a, i)["kind"] == 0xFF or info_entry(b, i)["kind"] == 0xFF):
            return "uninit (InvalidateSuspendSave magic32, P-12)"
    return None


def diff(a, b, cross):
    out = []
    for start, size, name in REGIONS:
        ra, rb = a[start:start + size], b[start:start + size]
        if ra == rb:
            out.append({"region": name, "equal": True})
            continue
        fields = {}
        other = 0
        for rel in range(size):
            if ra[rel] != rb[rel]:
                fn = field_name(name, rel)
                cls = known_class(name, rel, a, b, start) if cross else None
                ent = fields.setdefault(fn, {"bytes": 0, "first": "0x%04X" % (start + rel), "known": cls})
                ent["bytes"] += 1
                if cls is None:
                    ent["known"] = None
                if cls is None:
                    other += 1
        out.append({"region": name, "equal": False, "fields": fields, "other_bytes": other})
    return out


def known_kind(cls):
    """The kind a --line report counts a known byte under."""
    if cls in ("clock", "checksum"):
        return cls
    return "P-12" if "P-12" in cls else "P-18" if "P-18" in cls else cls


def cross_line(a, b):
    """(line, other): the --cross verdict as one line, and the bytes not known."""
    valid = ("".join("1" if info_entry(a, i)["valid"] else "0" for i in range(7)),
             "".join("1" if info_entry(b, i)["valid"] else "0" for i in range(7)))
    blocks = ("valid blocks (game1-3, suspend, suspend_alt, arena, xmap) "
              + (valid[0] if valid[0] == valid[1] else f"A {valid[0]}, B {valid[1]}"))
    if a == b:
        return f"SRAM CROSS: IDENTICAL (all {SRAM_SIZE} bytes; {blocks})", 0
    kinds, other = {}, []
    for start, size, name in REGIONS:
        for rel in range(size):
            if a[start + rel] != b[start + rel]:
                cls = known_class(name, rel, a, b, start)
                if cls is None:
                    other.append(f"0x{start + rel:04X} {name} {field_name(name, rel)}")
                else:
                    k = known_kind(cls)
                    kinds[k] = kinds.get(k, 0) + 1
    known = sum(kinds.values())
    known_txt = ", ".join(f"{k} {n}" for k, n in sorted(kinds.items()))
    if not other:
        return f"SRAM CROSS: EQUAL apart from the known bytes ({known} bytes: {known_txt}; {blocks})", 0
    named = []
    for o in other:  # one name per field (a unit slot as a whole), in SRAM order
        f = o.split(" ", 1)[1].split("+", 1)[0]
        if f not in named:
            named.append(f)
    return (f"SRAM CROSS: DIFFERENT ({len(other)} bytes not known, first {other[0].split(' ')[0]}: "
            + ", ".join(named[:4]) + (f" and {len(named) - 4} more fields" if len(named) > 4 else "")
            + (f"; {known} known: {known_txt}" if known else "") + f"; {blocks})"), len(other)


def block_summary(sram, i):
    e = info_entry(sram, i)
    if not e["valid"]:
        return {"block": INFO_NAMES[i], "valid": False}
    off = e["offset"]
    ps = sram[off:off + 0x4C]
    s = {"block": INFO_NAMES[i], "valid": True, "offset": "0x%04X" % off,
         "checksum32": "0x%08X" % e["checksum32"],
         "chapterIndex": struct.unpack_from("<b", ps, 0x0E)[0], "turn": struct.unpack_from("<H", ps, 0x10)[0],
         "phase": ps[0x0F], "gold": struct.unpack_from("<I", ps, 0x08)[0], "slot": ps[0x0C],
         "stateBits": ps[0x14], "mode": ps[0x1B], "time_saved": struct.unpack_from("<I", ps, 0)[0]}
    units = []
    if i <= 2:
        for k in range(51):
            u = off + 0x4C + k * GAME_UNIT
            if sram[u + 0x14]:
                units.append("%02x" % sram[u + 0x14])
    else:
        for k in range(51):
            u = off + 0x84 + k * SUS_UNIT
            if sram[u]:
                units.append("%02x:%d/%d" % (sram[u], sram[u + 0xF], sram[u + 0xE]))
        s["suspendPoint"] = sram[off + 0x4C + 0x16]
        s["red"] = sum(1 for k in range(50) if sram[off + 0xB14 + k * SUS_UNIT])
        s["green"] = sum(1 for k in range(10) if sram[off + 0x153C + k * SUS_UNIT])
    s["units"] = units
    return s


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("a")
    ap.add_argument("b", nargs="?")
    ap.add_argument("--cross", action="store_true", help="PC against GBA: name and leave out the known bytes")
    ap.add_argument("--summary", action="store_true", help="print what A's blocks hold and exit")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--line", action="store_true", help="with --cross: the verdict as one 'SRAM CROSS:' line")
    ap.add_argument("--block", action="store_true", help="A and B are FILE:REGION (game.N or suspend.N)")
    args = ap.parse_args()
    if args.block:
        (fa, ra), (fb, rb) = args.a.rsplit(":", 1), args.b.rsplit(":", 1)
        reg = {name: (start, size) for start, size, name in REGIONS}
        if ra.split(".")[0] != rb.split(".")[0] or ra not in reg or rb not in reg or ra.split(".")[0] not in ("game", "suspend"):
            raise SystemExit("--block: two game.N or two suspend.N regions")
        da, db = load(fa)[0], load(fb)[0]
        (sa, n), (sb, _) = reg[ra], reg[rb]
        fields = {}
        empty = 0
        units = SUSPEND_MEMBERS if ra.startswith("suspend") else GAME_MEMBERS
        for rel in range(n):
            if da[sa + rel] != db[sb + rel]:
                # a unit slot with no unit (pid 0 in both): what the slot held before, not game state
                hit = [(off, elem) for off, size, nm, elem in units
                       if elem in (SUS_UNIT, GAME_UNIT) and off <= rel < off + size]
                if hit:
                    off, elem = hit[0]
                    base = off + (rel - off) // elem * elem
                    pidoff = 0 if elem == SUS_UNIT else 0x14
                    if da[sa + base + pidoff] == 0 and db[sb + base + pidoff] == 0:
                        empty += 1
                        continue
                fn = field_name(ra, rel)
                fields.setdefault(fn, []).append(rel)
        print(f"{fa}:{ra} against {fb}:{rb}: {sum(len(v) for v in fields.values())} bytes differ"
              f" (and {empty} bytes in unit slots with no unit on either side, not counted)")
        for fn, rels in fields.items():
            print(f"    {fn}: {len(rels)} bytes (+0x{rels[0]:X}): " +
                  " ".join(f"{da[sa + r]:02X}>{db[sb + r]:02X}" for r in rels[:8]))
        return 0
    a, alen = load(args.a)
    if args.summary or args.b is None:
        res = {"file": args.a, "size": alen, "header_valid": header_ok(a),
               "last_game_save_id": a[0x62], "last_suspend_slot": a[0x63],
               "blocks": [block_summary(a, i) for i in range(5)],
               "info_valid": [info_entry(a, i)["valid"] for i in range(7)]}
        if args.json:
            print(json.dumps(res, indent=1))
            return 0
        print(f"{args.a}: {alen} bytes; header {'valid' if res['header_valid'] else 'NOT valid'}; "
              f"last game save id {a[0x62]}, last suspend slot {a[0x63]}")
        for s in res["blocks"]:
            if not s["valid"]:
                print(f"  {s['block']}: empty or not valid")
                continue
            extra = (f", suspend point {s['suspendPoint']}, red {s['red']}, green {s['green']}"
                     if "suspendPoint" in s else "")
            print(f"  {s['block']}: chapter {s['chapterIndex']} turn {s['turn']} phase 0x{s['phase']:02X} "
                  f"gold {s['gold']} slot {s['slot']} bits 0x{s['stateBits']:02X} mode {s['mode']} "
                  f"time {s['time_saved']} cks {s['checksum32']}{extra}")
            print(f"    units ({len(s['units'])}): {' '.join(s['units'])}")
        return 0
    b, blen = load(args.b)
    if args.line:
        if not args.cross:
            raise SystemExit("--line goes with --cross")
        line, other = cross_line(a, b)
        print(line)
        return 1 if other else 0
    res = diff(a, b, args.cross)
    checks = {"a_header_valid": header_ok(a), "b_header_valid": header_ok(b),
              "a_info_valid": [info_entry(a, i)["valid"] for i in range(7)],
              "b_info_valid": [info_entry(b, i)["valid"] for i in range(7)],
              "sizes": [alen, blen]}
    other = sum(r.get("other_bytes", 0) for r in res)
    if args.json:
        print(json.dumps({"regions": res, "checks": checks, "other_bytes": other if args.cross else None}, indent=1))
    else:
        print(f"A {args.a} ({alen} bytes), B {args.b} ({blen} bytes)")
        print(f"header valid: A {checks['a_header_valid']}, B {checks['b_header_valid']}; blocks valid "
              f"(game1-3, suspend, suspend_alt, arena, xmap): A {''.join('1' if v else '0' for v in checks['a_info_valid'])}"
              f", B {''.join('1' if v else '0' for v in checks['b_info_valid'])}")
        eq = [r["region"] for r in res if r["equal"]]
        print(f"equal: {' '.join(eq) if eq else '(none)'}")
        for r in res:
            if r["equal"]:
                continue
            nb = sum(f["bytes"] for f in r["fields"].values())
            print(f"DIFFERENT {r['region']}: {nb} bytes" + (f", {r['other_bytes']} not known" if args.cross else ""))
            for fn, f in r["fields"].items():
                tag = f" [{f['known']}]" if args.cross and f["known"] else (" [NOT KNOWN]" if args.cross else "")
                print(f"    {fn}: {f['bytes']} bytes from {f['first']}{tag}")
        if args.cross:
            print(f"CROSS RESULT: {'EQUAL apart from the known bytes' if other == 0 else 'DIFFERENT'}"
                  f" ({other} bytes not known)")
    return 1 if args.cross and other else 0


if __name__ == "__main__":
    sys.exit(main())
