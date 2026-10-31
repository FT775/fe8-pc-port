#!/usr/bin/env python3
"""Edited start for the Extras menu's support viewer (test coverage only, not story play).

The viewer (Extras > Support Conversation, src/uisupport.c) lists the characters the save's
global block knows (charKnownFlags) and lets A play every support conversation whose rank the
block records (SuppordRecord, 2 bits a conversation in GetSupportTalkList()'s order; in play,
StartSupportTalk -> UpdateBestGlobalSupportValue writes it when a support conversation is
seen). No bot route plays a support conversation into a cleared game, so this tool writes the
record into a copy of a cleared save: every conversation at rank RANK (default 3: C, B and A),
then the block's checksum (Checksum16 over its first 0x50 bytes, src/bmsave-lib.c) as
WriteGlobalSaveInfo does. Nothing else changes. The FNV-1a 32 of the result's first 32 KiB
(the key files' "# Start:" line) is printed.

usage: sram_support_edit.py IN.sav OUT.sav [--rank 1|2|3]
"""
import argparse
import struct
import sys

GLOBAL_CHECKSUM_SIZE = 0x50   # GLOBALSIZEINFO_SIZE_FOR_CHECKSUM (include/bmsave.h)
SUPPORT_RECORD = (0x20, 0x40)  # struct GlobalSaveInfo SuppordRecord[0x20]
CHECKSUM_AT = 0x60


def checksum16(b):
    add = xor = 0
    for (w,) in struct.iter_unpack("<H", b):
        add += w
        xor ^= w
    return (add + xor) & 0xFFFF


def fnv1a(b):
    h = 0x811C9DC5
    for c in b:
        h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--rank", type=int, default=3, choices=(1, 2, 3))
    a = ap.parse_args()
    s = bytearray(open(a.src, "rb").read())
    head = bytes(s[:GLOBAL_CHECKSUM_SIZE])
    if struct.unpack_from("<H", s, CHECKSUM_AT)[0] != checksum16(head):
        sys.exit(f"sram_support_edit: {a.src}: the global block's checksum does not match (not an FE8 save?)")
    byte = 0
    for k in range(4):
        byte |= a.rank << (2 * k)
    lo, hi = SUPPORT_RECORD
    s[lo:hi] = bytes([byte]) * (hi - lo)
    struct.pack_into("<H", s, CHECKSUM_AT, checksum16(bytes(s[:GLOBAL_CHECKSUM_SIZE])))
    open(a.dst, "wb").write(s)
    print(f"{a.dst}: every support conversation recorded at rank {a.rank}; fnv1a {fnv1a(bytes(s[:0x8000])):08x}")


if __name__ == "__main__":
    main()
