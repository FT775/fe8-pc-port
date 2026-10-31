#!/usr/bin/env python3
"""Compare struct and union layouts between the Linux and Windows host builds.

    python3 port/win32/abi_check.py build/pc-full/fe8pc build/win32/fe8pc.exe

Both builds keep the game's structs at GBA addresses and read ROM data through
them in place, so every struct must have the same layout on both hosts. MinGW's
i686 defaults differ from Linux i386 (-mms-bitfields, -malign-double), which
port/Makefile.mingw turns off. This tool reads the DWARF of both executables
(objdump --dwarf=info) and compares, per compile unit, the byte size of every
struct and union and the offset and bit position of every member. It prints
the differences and exits 1 if there are any. Types that exist in only one
build (each host's C library and Windows headers) are counted, not compared,
and the host signal types in HOST_TYPES differ on purpose.
"""

import re
import subprocess
import sys
from collections import defaultdict

TAG_RE = re.compile(r"^\s*<(\d+)><([0-9a-f]+)>: Abbrev Number: \d+ \((DW_TAG_\w+)\)")
ATTR_RE = re.compile(r"^\s*<[0-9a-f]+>\s+(DW_AT_\w+)\s*:\s*(.*)$")
# The string offset prints as "0x1c2" or, for offset 0, as plain "0" (objdump), so a
# compile unit whose name sits at .debug_line_str offset 0 (it depends on the worktree path)
# was read as unnamed and its layouts never compared (the round 10 builds and paths reviews).
NAME_RE = re.compile(r"(?:\(indirect (?:line )?string, offset: (?:0x)?[0-9a-f]+\): |\(strx\d*\) \(offset: (?:0x)?[0-9a-f]+\): )?(.*)$")
WANT = {"DW_TAG_structure_type": "struct", "DW_TAG_union_type": "union"}
# Host C library types that differ on purpose: glibc's against port/win32/compat's.
HOST_TYPES = {"sigaction", "ucontext_t", "mcontext_t", "siginfo_t"}


def objdump_for(path):
    with open(path, "rb") as fh:
        magic = fh.read(2)
    return "i686-w64-mingw32-objdump" if magic == b"MZ" else "objdump"


def value(attr_text):
    m = NAME_RE.match(attr_text.strip())
    return m.group(1).strip() if m else attr_text.strip()


def layouts(path):
    """{(cu, kind, name-or-member-signature): (size, ((member, offset, bitpos, bitsize), ...))}"""
    proc = subprocess.Popen([objdump_for(path), "--dwarf=info", path], stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, text=True, errors="replace", bufsize=1 << 20)
    out = {}
    cu = None
    stack = []          # (depth, record) of open struct/union entries
    cur = None          # the DIE whose attributes are being read: (kind, record)
    for line in proc.stdout:
        m = TAG_RE.match(line)
        if m:
            depth, tag = int(m.group(1)), m.group(3)
            while stack and stack[-1][0] >= depth:
                finish(out, cu, stack.pop()[1])
            if tag == "DW_TAG_compile_unit":
                cur = ("cu", None)
            elif tag in WANT:
                rec = {"kind": WANT[tag], "name": None, "size": None, "members": [], "decl": False}
                stack.append((depth, rec))
                cur = ("agg", rec)
            elif tag == "DW_TAG_member" and stack and stack[-1][0] == depth - 1:
                mem = {"name": None, "off": None, "bitoff": None, "bitsize": None}
                stack[-1][1]["members"].append(mem)
                cur = ("mem", mem)
            else:
                cur = (None, None)
            continue
        a = ATTR_RE.match(line)
        if not a or cur[0] is None:
            continue
        attr, text = a.group(1), a.group(2)
        kind, rec = cur
        if kind == "cu" and attr == "DW_AT_name":
            cu = value(text)
        elif kind == "agg":
            if attr == "DW_AT_name":
                rec["name"] = value(text)
            elif attr == "DW_AT_byte_size":
                rec["size"] = int(value(text), 0)
            elif attr == "DW_AT_declaration":
                rec["decl"] = True
        elif kind == "mem":
            if attr == "DW_AT_name":
                rec["name"] = value(text)
            elif attr == "DW_AT_data_member_location":
                rec["off"] = value(text)
            elif attr in ("DW_AT_data_bit_offset", "DW_AT_bit_offset"):
                rec["bitoff"] = value(text)
            elif attr == "DW_AT_bit_size":
                rec["bitsize"] = value(text)
    while stack:
        finish(out, cu, stack.pop()[1])
    proc.wait()
    return out


def finish(out, cu, rec):
    if rec["decl"] or rec["size"] is None:
        return
    members = tuple((m["name"], m["off"], m["bitoff"], m["bitsize"]) for m in rec["members"])
    name = rec["name"] or "<anon:" + ",".join(str(m[0]) for m in members) + ">"
    out[(cu, rec["kind"], name)] = (rec["size"], members)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    a, b = layouts(sys.argv[1]), layouts(sys.argv[2])
    common = sorted(k for k in set(a) & set(b) if k[2] not in HOST_TYPES)
    diffs = [k for k in common if a[k] != b[k]]
    only_a = len(set(a) - set(b))
    only_b = len(set(b) - set(a))
    names = {k[2] for k in common}
    print(f"abi_check: {len(common)} struct/union layouts compared ({len(names)} distinct names) across "
          f"{len({k[0] for k in common})} compile units; {len(diffs)} differ "
          f"({only_a} only in {sys.argv[1]}, {only_b} only in {sys.argv[2]})")
    shown = set()
    for k in diffs:
        if k[1:] in shown:
            continue
        shown.add(k[1:])
        sa, ma = a[k]
        sb, mb = b[k]
        print(f"  {k[1]} {k[2]} ({k[0]}): size {sa} vs {sb}")
        for x, y in zip(ma, mb):
            if x != y:
                print(f"    member {x} vs {y}")
    return 1 if diffs else 0


if __name__ == "__main__":
    sys.exit(main())
