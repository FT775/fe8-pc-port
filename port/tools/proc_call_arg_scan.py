#!/usr/bin/env python3
"""List the PROC_CALL_ARG commands in the built GBA ROM and the return type of each target.

PROC_CALL_ARG (proc opcode 0x18, include/proc.h) calls func(arg, proc) and
RunProcessScript goes on to the next command only when the s8 it returns is
not 0. Some targets are void: the GBA then returns whatever r0 holds, the host
whatever eax holds. ProcCmd_CALL_ROUTINE_ARG (src/proc.c) returns the GBA's r0
for each void target it knows (port-plan/DIVERGENCES.md D-29, host-ABI values).
This tool finds every target, so that list can be checked:

  python3 port/tools/proc_call_arg_scan.py [--rom fireemblem8.gba] [--elf fireemblem8.elf]
                                           [--host build/pc-full/fe8pc] [--check]

The ROM is scanned at 4-byte steps for {u16 0x18, s16 arg, u32 Thumb function}
whose function is a GBA ELF symbol. A hit counts as a command only when the
words from it to the next PROC_END ({0, 0, 0}) are all proc commands (opcodes
0x00-0x19); other hits (data that happens to look like one) are listed apart.
The return type comes from the host build's DWARF (the C definition; any
host build with -g, for example the FULL one). --check exits 1 unless the
void targets are exactly the functions ProcCmd_CALL_ROUTINE_ARG handles in
src/proc.c and every other target is in the VETTED list of
port/test/pc/proc_call_arg_check.c (the test builds' report of an unvetted
target).
"""
import argparse
import bisect
import collections
import re
import struct
import subprocess
import sys

OP_CALL_ARG = 0x18
OP_COUNT = 0x1A          # sProcessCmdTable in src/proc.c: opcodes 0x00-0x19
ROM_BASE = 0x08000000
MAX_SCRIPT = 4096        # commands to walk forward looking for PROC_END


def gba_symbols(elf):
    out = subprocess.run(["arm-none-eabi-nm", "-n", elf], capture_output=True, text=True, check=True).stdout
    funcs, syms = {}, []
    for line in out.splitlines():
        p = line.split()
        if len(p) != 3:
            continue
        a = int(p[0], 16)
        if not ROM_BASE <= a < 0x0A000000 or p[1] in "aAN":
            continue
        syms.append((a, p[2]))
        if p[1] in "Tt":
            funcs.setdefault(a, p[2])
    syms.sort()
    return funcs, syms


def is_script_tail(rom, off):
    """True if the 8-byte commands from off reach a PROC_END with every opcode valid."""
    for i in range(MAX_SCRIPT):
        o = off + 8 * i
        if o + 8 > len(rom):
            return False
        op, arg, ptr = struct.unpack_from("<HhI", rom, o)
        if op >= OP_COUNT:
            return False
        if op == 0 and arg == 0 and ptr == 0:
            return True
    return False


def scan(rom, funcs, syms):
    addrs = [a for a, _ in syms]
    cmds, rejected = [], []
    for off in range(0, len(rom) - 8, 4):
        op, arg, ptr = struct.unpack_from("<HhI", rom, off)
        if op != OP_CALL_ARG or not ptr & 1:
            continue
        name = funcs.get(ptr - 1)
        if name is None:
            continue
        a = ROM_BASE + off
        i = bisect.bisect_right(addrs, a) - 1
        where = "%s+0x%x" % (syms[i][1], a - syms[i][0]) if i >= 0 else "?"
        (cmds if is_script_tail(rom, off) else rejected).append((a, arg, name, where))
    return cmds, rejected


def host_return_types(host, wanted):
    """{name: set of 'void' / 'returns'} from the host binary's DWARF definitions."""
    types = collections.defaultdict(set)
    decl = collections.defaultdict(set)
    proc = subprocess.Popen(["objdump", "--dwarf=info", host], stdout=subprocess.PIPE, text=True,
                            errors="replace", bufsize=1 << 20)
    die_rx = re.compile(r"^\s*<(\d+)><[0-9a-f]+>: Abbrev Number: \d+ \((DW_TAG_\w+)\)")
    cur = None   # [name, has_type, is_decl] of the current subprogram DIE

    def done(d):
        if d and d[0] in wanted:
            (decl if d[2] else types)[d[0]].add("returns" if d[1] else "void")

    for line in proc.stdout:
        m = die_rx.match(line)
        if m:
            done(cur)
            cur = [None, False, False] if m.group(2) == "DW_TAG_subprogram" else None
            continue
        if cur is None:
            continue
        s = line.strip()
        if s.startswith("<") and "DW_AT_" in s:
            attr = s.split()[1]
            if attr == "DW_AT_name":
                cur[0] = s.rsplit(": ", 1)[-1].strip()
            elif attr == "DW_AT_type":
                cur[1] = True
            elif attr == "DW_AT_declaration":
                cur[2] = True
    done(cur)
    proc.wait()
    for n, v in decl.items():
        if n not in types:
            types[n] = {x + " (declaration only)" for x in v}
    return types


def names_in(path, rx):
    with open(path) as fh:
        return set(re.findall(rx, fh.read(), re.M))


def proc_c_void_targets(path):
    with open(path) as fh:
        text = fh.read()
    m = re.search(r"static s8 ProcCmd_CALL_ROUTINE_ARG\(.*?\n}\n", text, re.S)
    return set(re.findall(r"== \(ProcFunc\)(\w+)", m.group(0))) if m else set()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--rom", default="fireemblem8.gba")
    ap.add_argument("--elf", default="fireemblem8.elf")
    ap.add_argument("--host", default="build/pc-full/fe8pc", help="host binary with DWARF (-g)")
    ap.add_argument("--check", action="store_true",
                    help="compare with src/proc.c and port/test/pc/proc_call_arg_check.c")
    ap.add_argument("--proc-c", default="src/proc.c")
    ap.add_argument("--vetted-c", default="port/test/pc/proc_call_arg_check.c")
    a = ap.parse_args()

    rom = open(a.rom, "rb").read()
    funcs, syms = gba_symbols(a.elf)
    cmds, rejected = scan(rom, funcs, syms)
    per = collections.defaultdict(list)
    for addr, arg, name, where in cmds:
        per[name].append((addr, arg, where))
    rtypes = host_return_types(a.host, set(per))

    print("PROC_CALL_ARG: %d commands, %d targets (%s)" % (len(cmds), len(per), a.rom))
    void, other, unknown = [], [], []
    for name in sorted(per, key=lambda n: (-len(per[n]), n)):
        t = rtypes.get(name, set())
        kind = "void" if t == {"void"} else "returns" if t == {"returns"} else \
               ("/".join(sorted(t)) if t else "no DWARF")
        (void if kind == "void" else other if kind == "returns" else unknown).append(name)
        args = sorted(set(x[1] for x in per[name]))
        print("  %3d  %-8s %-40s args %s" % (len(per[name]), kind, name,
                                               ",".join(map(str, args[:10])) + (",..." if len(args) > 10 else "")))
        if kind != "returns":
            for addr, arg, where in per[name]:
                print("         0x%08X  %-6d %s" % (addr, arg, where))
    print("void targets: %d (%d commands): %s" % (len(void), sum(len(per[n]) for n in void), ", ".join(void)))
    print("targets with a return value: %d; unknown: %d%s" % (len(other), len(unknown),
                                                              (" (" + ", ".join(unknown) + ")") if unknown else ""))
    for addr, arg, name, where in rejected:
        print("not a command (no PROC_END after it): 0x%08X arg %d -> %s, in %s" % (addr, arg, name, where))

    if not a.check:
        return 0
    bad = 0
    handled = proc_c_void_targets(a.proc_c)
    vetted = names_in(a.vetted_c, r"^\s*VETTED\((\w+)\)")
    for n in sorted(set(void) - handled):
        print("CHECK: void target %s is not handled in %s" % (n, a.proc_c)); bad += 1
    for n in sorted(handled - set(void)):
        print("CHECK: %s handles %s, which is not a void PROC_CALL_ARG target" % (a.proc_c, n)); bad += 1
    for n in sorted(set(other) - vetted):
        print("CHECK: target %s is not in %s's VETTED list" % (n, a.vetted_c)); bad += 1
    for n in sorted(vetted - set(other)):
        print("CHECK: %s vets %s, which is not a PROC_CALL_ARG target with a return value" % (a.vetted_c, n)); bad += 1
    for n in unknown:
        print("CHECK: no return type for %s" % n); bad += 1
    print("CHECK: %s" % ("PASS" if not bad else "FAIL (%d)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
