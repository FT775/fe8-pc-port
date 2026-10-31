#!/usr/bin/env python3
"""Check a Reforged Options sidecar (<sav>.reforged) against its .sav.

    python3 port/test/tools/sidecar_check.py FILE.sav [--json]

port/test/README.md, "Save checks". The sidecar (port/reforged/reforged_savemark.c,
played builds only) names, per save block (game1-3, suspend, suspend_alt), the
gameplay options that block was written with, Casual mode's fallen list, and
<block>_check: the block's SaveBlockInfo checksum32 in the .sav it was written
for. This prints each marked block and whether its fingerprint is the .sav's
(a valid game or suspend block with that checksum32), and the valid blocks of
the .sav that have no entry. No sidecar: says so.
Exit 0 when every fingerprint matches, 1 when one does not, 2 on a bad file.
"""
import json
import os
import struct
import sys

NAMES = ["game1", "game2", "game3", "suspend", "suspend_alt"]


def blocks(sram):
    out = {}
    for i, n in enumerate(NAMES):
        m32, m16, kind, _pad, off, size, c32 = struct.unpack_from("<IHBbHHI", sram, 0x64 + i * 0x10)
        ok = m16 == 0x200A and m32 == 0x40624 and kind == (1 if i >= 3 else 0)
        out[n] = c32 if ok else None
    return out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    sav = args[0]
    sram = open(sav, "rb").read()[:0x8000]
    if len(sram) != 0x8000:
        print(f"{sav}: not a 32 KiB save", file=sys.stderr)
        return 2
    have = blocks(sram)
    side = sav + ".reforged"
    res = {"sav": sav, "sidecar": os.path.exists(side), "entries": {}, "unmarked_valid": []}
    bad = 0
    if res["sidecar"]:
        kv = {}
        for line in open(side):
            line = line.split("#", 1)[0].strip()
            if "=" in line:
                k, v = line.split("=", 1)
                kv[k.strip()] = v.strip()
        res["format"] = kv.get("format")
        for n in NAMES:
            if n in kv or n + "_check" in kv or n + "_fallen" in kv:
                chk = int(kv[n + "_check"], 0) if n + "_check" in kv else None
                ok = chk is not None and have[n] is not None and chk == have[n]
                bad += not ok
                res["entries"][n] = {"options": kv.get(n, ""), "fallen": kv.get(n + "_fallen", ""),
                                     "check": None if chk is None else f"0x{chk:08X}",
                                     "sav_checksum": None if have[n] is None else f"0x{have[n]:08X}", "match": ok}
    res["unmarked_valid"] = [n for n in NAMES if have[n] is not None and n not in res["entries"]]
    if "--json" in sys.argv:
        print(json.dumps(res, indent=1))
    else:
        if not res["sidecar"]:
            print(f"{sav}: no sidecar; valid blocks: {', '.join(n for n in NAMES if have[n] is not None)}")
        else:
            print(f"{sav}: sidecar format {res['format']}")
            for n, e in res["entries"].items():
                print(f"  {n}: [{e['options']}]{' fallen ' + e['fallen'] if e['fallen'] else ''} check {e['check']}"
                      f" .sav {e['sav_checksum']}: {'MATCH' if e['match'] else 'NO MATCH'}")
            print(f"  valid blocks with no entry: {', '.join(res['unmarked_valid']) or '(none)'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
