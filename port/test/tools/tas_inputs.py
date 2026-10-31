#!/usr/bin/env python3
"""Decode TAS input files into per-frame key files (port/test/README.md, "TAS route").

The inputs are third-party data: this tool only parses them (never executes
anything from them), and what it writes reproduces them, so its output goes
to the git-ignored port/test/tas/ (or anywhere outside git). Never commit a
movie, a list or anything made from them.

  tas_inputs.py vbm MOVIE.vbm OUT.keys
      VBA-rr movie, VBM version 1: header, flags and the 16-bit input word of
      each frame (bit 0 A, 1 B, 2 Select, 3 Start, 4 Right, 5 Left, 6 Up,
      7 Down, 8 R, 9 L, 10-11 reset, 12-15 motion sensor). Writes entry k =
      the word of frame k (bits 0-9, the GBA's KEYINPUT order). Refused: a
      movie that starts from a snapshot or from SRAM, uses controllers other
      than 1, is not for the GBA, or has reset or motion bits (the oracle has
      no movie reset; none of the movies used so far has one).

  tas_inputs.py console LIST.txt OUT.keys
      A console verification list, one "HHHHHHHH KKKK" line per key change
      (hex time, hex key mask). The time unit is 4096 CPU cycles (checked here:
      every change lies within 4096 cycles of a VBlank start, line 160 of a
      228-line frame of 280,896 cycles, counted from time 0 = console power-on
      with the BIOS boot). The mask has bits 0-7 in KEYINPUT order and L and R
      swapped (0x100 L, 0x200 R; checked against the VBM of the same run).
      Writes entry k = the keys in effect for console VBlank interval k
      (from the VBlank start of console frame k to the next): a change is
      assigned to the VBlank start within 4096 cycles before it.

  tas_inputs.py compare MOVIE.vbm LIST.txt
      Lines up the key changes of the two (difflib on the change sequences)
      and prints the console-minus-VBM frame offset over the run.
"""
import argparse
import collections
import difflib
import struct
import sys

CYC_LINE, LINES, CYC_FRAME = 1232, 228, 280896
VBLANK_LINE = 160
UNIT = 4096

HEADER = ("# TAS-derived key data: local only, never commit (port/test/README.md, \"TAS route\").\n"
          "# Per-FRAME entries for oracle/movie_check.lua and gate0_capture.lua's frame mode.\n")


def write_keys(path, seq, header):
    with open(path, "w") as f:
        f.write(header)
        i = 0
        while i < len(seq):
            j = i
            while j + 1 < len(seq) and seq[j + 1] == seq[i]:
                j += 1
            n = j - i + 1
            f.write("%03x\n" % seq[i] if n == 1 else "%03x*%d\n" % (seq[i], n))
            i = j + 1


def read_vbm(path):
    d = open(path, "rb").read()
    sig, ver, uid, frames, rerecords = struct.unpack_from("<4sIIII", d, 0)
    if sig != b"VBM\x1a" or ver != 1:
        sys.exit("%s: not a VBM version 1 movie" % path)
    start, ctl, system, emu = d[0x14], d[0x15], d[0x16], d[0x17]
    save_off, ctl_off = struct.unpack_from("<II", d, 0x38)
    info = collections.OrderedDict(
        frames=frames, rerecords=rerecords, uid=uid, start_flags=start, controller_flags=ctl,
        system_flags=system, emulator_flags=emu,
        use_bios_file=emu & 1, skip_bios_file=(emu >> 1) & 1, rtc=(emu >> 2) & 1, lag_reduction=(emu >> 4) & 1,
        save_type=struct.unpack_from("<I", d, 0x18)[0], flash_size=struct.unpack_from("<I", d, 0x1C)[0],
        title=d[0x24:0x30].decode("ascii", "replace"), game_code=d[0x34:0x38].decode("ascii", "replace"),
        rom_crc_byte=d[0x31], author=d[0x40:0x80].split(b"\0")[0].decode("latin-1"),
        description=d[0x80:0x100].split(b"\0")[0].decode("latin-1"), savestate_offset=save_off,
        input_offset=ctl_off)
    n = (len(d) - ctl_off) // 2
    words = list(struct.unpack_from("<%dH" % n, d, ctl_off))
    return info, words


def cmd_vbm(a):
    info, words = read_vbm(a.movie)
    for k, v in info.items():
        print("%s: %s" % (k, v))
    print("input words in the file: %d (header frames %d)" % (len(words), info["frames"]))
    problems = []
    if info["start_flags"] & 3:
        problems.append("starts from a snapshot or SRAM (start flags 0x%02x)" % info["start_flags"])
    if info["controller_flags"] != 1:
        problems.append("controller flags 0x%02x (want controller 1 only)" % info["controller_flags"])
    if not info["system_flags"] & 1:
        problems.append("not a GBA movie")
    resets = [i for i, w in enumerate(words) if w & 0x0C00]
    motion = [i for i, w in enumerate(words) if w & 0xF000]
    if resets:
        problems.append("reset bits on %d frames (first %d)" % (len(resets), resets[0]))
    if motion:
        problems.append("motion-sensor bits on %d frames" % len(motion))
    if problems:
        sys.exit("refused: " + "; ".join(problems))
    if info["use_bios_file"]:
        print("note: recorded with a BIOS file%s" % ("" if info["skip_bios_file"] else ", BIOS intro in the movie"))
    else:
        print("note: no BIOS file (the emulator's HLE BIOS, start at the cartridge entry), as the oracle")
    keys = [w & 0x3FF for w in words]
    write_keys(a.out, keys, HEADER + "# VBM %s: entry k = frame k from power-on (%d entries).\n"
               % (info["description"][:60], len(keys)))
    print("wrote %s: %d frame entries, %d key changes" % (a.out, len(keys),
          sum(1 for i in range(1, len(keys)) if keys[i] != keys[i - 1])))


def read_console(path):
    ent = []
    for ln, line in enumerate(open(path, "r", newline=None), 1):
        line = line.strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 2 or len(parts[0]) != 8 or len(parts[1]) != 4:
            sys.exit("%s:%d: not an 'HHHHHHHH KKKK' line" % (path, ln))
        ent.append((int(parts[0], 16), int(parts[1], 16)))
    for i in range(1, len(ent)):
        if ent[i][0] <= ent[i - 1][0]:
            sys.exit("%s: times are not increasing at entry %d" % (path, i))
    return ent


def remap_console(k):
    if k & ~0x3FF:
        sys.exit("console mask %04x has bits above 9" % k)
    m = k & 0xFF
    if k & 0x100:
        m |= 0x200   # L
    if k & 0x200:
        m |= 0x100   # R
    return m


def console_frames(ent):
    """Per console VBlank interval: the keys from the VBlank start of frame k on."""
    far = 0
    for t, _ in ent[1:]:
        c = t * UNIT
        f = c // CYC_FRAME
        # distance from the nearest VBlank start at or before c + UNIT
        v = f * CYC_FRAME + VBLANK_LINE * CYC_LINE
        if c < v - UNIT:
            v -= CYC_FRAME
        far = max(far, abs(c - v))
        if not (v - UNIT < c <= v + UNIT):
            sys.exit("change at time %08x is not within one unit of a VBlank start" % t)
    last = ent[-1][0] * UNIT // CYC_FRAME + 1
    out, cur, j = [], 0, 0
    for f in range(last + 1):
        v = f * CYC_FRAME + VBLANK_LINE * CYC_LINE
        while j < len(ent) and ent[j][0] * UNIT <= v + UNIT:
            cur = remap_console(ent[j][1])
            j += 1
        out.append(cur)
    return out, far


def cmd_console(a):
    ent = read_console(a.list)
    keys, far = console_frames(ent)
    deltas = collections.Counter(ent[i + 1][0] - ent[i][0] for i in range(len(ent) - 1))
    print("entries %d, first time %08x, last %08x" % (len(ent), ent[0][0], ent[-1][0]))
    print("most common gaps (units): %s (a frame is %.3f units)" % (deltas.most_common(4), CYC_FRAME / UNIT))
    print("every change within %d cycles (%.2f lines) of a VBlank start" % (far, far / CYC_LINE))
    write_keys(a.out, keys, HEADER + "# Console list: entry k = console VBlank interval k from power-on, BIOS boot "
               "included (%d entries).\n" % len(keys))
    print("wrote %s: %d console frame entries, %d key changes" % (a.out, len(keys),
          sum(1 for i in range(1, len(keys)) if keys[i] != keys[i - 1])))


def changes(seq):
    out, prev = [], 0
    for i, x in enumerate(seq):
        if x != prev:
            out.append((i, x))
            prev = x
    return out


def cmd_compare(a):
    _, words = read_vbm(a.movie)
    v = changes([w & 0x3FF for w in words])
    c = changes(console_frames(read_console(a.list))[0])
    sm = difflib.SequenceMatcher(None, [x for _, x in v], [x for _, x in c], autojunk=False)
    pairs = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            pairs += [(v[i1 + k][0], c[j1 + k][0]) for k in range(i2 - i1)]
    print("VBM changes %d, console changes %d, matched in order %d" % (len(v), len(c), len(pairs)))
    print("first matched: VBM frame %d = console frame %d (offset %d)" % (pairs[0][0], pairs[0][1],
          pairs[0][1] - pairs[0][0]))
    for fv, fc in pairs[:a.show]:
        print("  VBM %7d  console %7d  offset %d" % (fv, fc, fc - fv))
    print("last matched: VBM frame %d = console frame %d (offset %d)" % (pairs[-1][0], pairs[-1][1],
          pairs[-1][1] - pairs[-1][0]))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("vbm")
    s.add_argument("movie")
    s.add_argument("out")
    s.set_defaults(fn=cmd_vbm)
    s = sub.add_parser("console")
    s.add_argument("list")
    s.add_argument("out")
    s.set_defaults(fn=cmd_console)
    s = sub.add_parser("compare")
    s.add_argument("movie")
    s.add_argument("list")
    s.add_argument("--show", type=int, default=20)
    s.set_defaults(fn=cmd_compare)
    a = p.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
