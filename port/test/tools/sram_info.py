#!/usr/bin/env python3
"""Show what an FE8 save (SRAM image) holds, as the game reads it.

    python3 port/test/tools/sram_info.py port/test/saves/ch2.sav [--json]

port/test/README.md, "Chapter saves". The layout and the checks are the
game's own (include/bmsave.h, include/sram-layout.h, src/bmsave-lib.c,
src/bmsave.c):

- 0x0000 GlobalSaveInfo: marker "AGB-FE9", magic32 0x40624, magic16 0x200A,
  Checksum16 of its first 0x50 bytes at +0x60, last game save slot at +0x62,
  last suspend slot at +0x63.
- 0x0064 SaveBlockInfo[7] (0x10 each): magic32, magic16, kind, offset, size,
  checksum32 (Checksum32 of the block's bytes). Ids 0-2 game saves, 3-4 the
  two suspend slots, 5 arena, 6 extra map.
- A game or suspend block starts with the saved gPlaySt (0x4C bytes):
  +0x0E chapterIndex, +0x0F phase, +0x10 turn. A suspend block has
  gActionData after it, with suspendPointType at +0x16.

The title menu offers "Resume Chapter" when a suspend slot is valid
(IsValidSuspendSave: the last-written slot first, then the other) and
"Continue" when a game save is valid. The first entry is selected when the
menu opens (SaveMenu_LoadExtraMenuGraphics: main_select = 0).

Only reads the file. Exit status: 0, or 2 on a bad file.
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
BLOCK_NAMES = ["game slot 0", "game slot 1", "game slot 2", "suspend", "suspend (alt)", "arena", "extra map"]
KINDS = {0: "game", 1: "suspend", 2: "arena", 3: "xmap", 0xFF: "invalid"}
SUSPEND_POINTS = {0: "PLAYERIDLE", 1: "DURINGACTION", 2: "CPPHASE", 3: "BSKPHASE", 4: "DURINGARENA",
                  9: "PHASECHANGE"}
PHASES = {0x00: "player", 0x40: "other (green)", 0x80: "enemy"}


def checksum16(data):
    """Checksum16 (src/bmsave-lib.c): sum plus xor of the u16 words, as u16."""
    add = xor = 0
    for (w,) in struct.iter_unpack("<H", data):
        add += w
        xor ^= w
    return (add + xor) & 0xFFFF


def checksum32(data):
    """Checksum32 (asm/arm.s, port/asm/arm_pc.c): low 16 bits of the u16 sum,
    the u16 xor in the high 16 bits."""
    add = xor = 0
    for (w,) in struct.iter_unpack("<H", data[: len(data) & ~1]):
        add += w
        xor ^= w
    return ((add & 0xFFFF) + (xor << 16)) & 0xFFFFFFFF


def play_st(sram, off):
    chapter = struct.unpack_from("<b", sram, off + 0x0E)[0]
    return {
        "time_saved": struct.unpack_from("<I", sram, off + 0x00)[0],
        "slot": sram[off + 0x0C],
        "chapterIndex": chapter,
        "phase": sram[off + 0x0F],
        "turn": struct.unpack_from("<H", sram, off + 0x10)[0],
        "chapterStateBits": sram[off + 0x14],
        "chapterModeIndex": sram[off + 0x1B],
    }


def inspect(sram):
    out = {}
    marker = sram[0:8].split(b"\0")[0].decode("latin-1")
    magic32, magic16 = struct.unpack_from("<IH", sram, 8)
    cks = struct.unpack_from("<H", sram, 0x60)[0]
    out["header"] = {
        "marker": marker,
        "valid": marker == "AGB-FE9" and magic32 == SAVEMAGIC32 and magic16 == SAVEMAGIC16
                 and cks == checksum16(sram[:0x50]),
        "last_game_save_id": sram[0x62],
        "last_suspend_slot": sram[0x63],
    }
    blocks = []
    for i in range(7):
        b = 0x64 + i * 0x10
        m32, m16, kind, _pad, off, size, c32 = struct.unpack_from("<IHBbHHI", sram, b)
        ent = {"id": i, "name": BLOCK_NAMES[i], "kind": KINDS.get(kind, kind), "offset": off, "size": size}
        ok = m16 == SAVEMAGIC16 and m32 == MAGIC32[i] and off + size <= SRAM_SIZE
        ent["valid"] = bool(ok and checksum32(sram[off:off + size]) == c32)
        if ent["valid"] and i <= 4:
            ent["playSt"] = play_st(sram, off)
            if i >= 3:
                ent["suspendPoint"] = SUSPEND_POINTS.get(sram[off + 0x4C + 0x16], sram[off + 0x4C + 0x16])
        blocks.append(ent)
    out["blocks"] = blocks
    # IsValidSuspendSave: the last-written suspend slot first, then the other.
    last = 1 if sram[0x63] == 1 else 0
    resume = next((blocks[3 + s] for s in (last, 1 - last) if blocks[3 + s]["valid"]), None)
    games = [b for b in blocks[:3] if b["valid"]]
    menu = []
    if resume:
        menu.append("Resume Chapter")
    if games:
        menu.append("Continue")
    menu.append("New Game")
    out["menu_first"] = menu[0] if out["header"]["valid"] else "New Game (no valid save header)"
    out["resume"] = resume
    out["games"] = games
    return out


def describe(ps):
    return (f"chapterIndex {ps['chapterIndex']}, turn {ps['turn']}, phase "
            f"{PHASES.get(ps['phase'], hex(ps['phase']))}, mode {ps['chapterModeIndex']}, "
            f"time_saved {ps['time_saved']}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("sav")
    ap.add_argument("--json", action="store_true", help="print the result as JSON")
    args = ap.parse_args()
    with open(args.sav, "rb") as fh:
        data = fh.read()
    if len(data) not in (SRAM_SIZE, 2 * SRAM_SIZE):
        print(f"sram_info: {args.sav}: {len(data)} bytes, want 32 KiB or 64 KiB")
        return 2
    sram = data[:SRAM_SIZE]
    info = inspect(sram)
    fnv = 0x811C9DC5
    for byte in sram:
        fnv = ((fnv ^ byte) * 0x01000193) & 0xFFFFFFFF
    info["fnv1a"] = f"{fnv:08x}"
    if args.json:
        print(json.dumps(info, indent=1))
        return 0
    h = info["header"]
    print(f"{args.sav}: 32 KiB, fnv1a {info['fnv1a']} (the trace header's sram_fnv)")
    print(f"header: {'valid' if h['valid'] else 'NOT valid'} ({h['marker']!r}), last game save slot "
          f"{h['last_game_save_id']}, last suspend slot {h['last_suspend_slot']}")
    for b in info["blocks"]:
        if b["valid"] and "playSt" in b:
            extra = f", suspend point {b['suspendPoint']}" if "suspendPoint" in b else ""
            print(f"  {b['name']}: {describe(b['playSt'])}{extra}")
        elif b["valid"]:
            print(f"  {b['name']}: valid ({b['kind']}, {b['size']} bytes)")
        else:
            print(f"  {b['name']}: empty")
    print(f"title menu opens on: {info['menu_first']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
