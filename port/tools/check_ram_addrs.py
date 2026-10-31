#!/usr/bin/env python3
"""Check that every RAM symbol of the host binary is at its GBA address.

Build check (DIVERGENCES.md B-12; port/Makefile runs it after every link and
fails the build, removing the binary, on any difference):

  check_ram_addrs.py --binary build/pc-full/fe8pc --map build/pc-full/fe8pc.map \
      --gba-map fireemblem8.map --gba-elf fireemblem8.elf --gba-dir . --game-objs build/pc-full/src/*.o

It checks three things:
  1. every data variable that a host-built game object defines, statics and
     function-statics included: where the GBA build has it in EWRAM or IWRAM,
     the host must have it at that same address; where the GBA build does not
     have it in RAM, the host must not put it in GBA RAM either. Addresses are
     found per object (host: the link map and the object's symbol table; GBA:
     the GBA ELF for globals, the GBA object and the GBA map for statics), so a
     static is checked against the static of the same file;
  2. every absolute symbol in GBA RAM (the data symbols gen_host_link.py binds
     to GBA addresses) has the GBA ELF's value;
  3. no other symbol of the binary sits in GBA RAM.
port/win32/pe_gba.py runs it for the Windows build with COFF readers.

Quick report by name (no objects needed; the old form of this tool):

  check_ram_addrs.py build/pc-full/fe8pc fireemblem8.elf
"""

import argparse
import importlib.util
import os
import re
import subprocess
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))


def load_gen_host_link():
    spec = importlib.util.spec_from_file_location("gen_host_link", os.path.join(HERE, "gen_host_link.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def symbols(path, nm="nm"):
    out = {}
    for line in subprocess.run([nm, "-n", path], check=True, capture_output=True,
                               text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            addr, kind, name = parts
            out.setdefault(name, []).append((int(addr, 16), kind))
    return out


def binary_symbols(path):
    """[(name, addr, kind)] of a linked ELF binary (nm). pe_gba.py replaces it for PE."""
    out = []
    for line in subprocess.run(["nm", "-n", path], check=True, capture_output=True,
                               text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            out.append((parts[2], int(parts[0], 16), parts[1]))
    return out


def in_ram(addr):
    return 0x02000000 <= addr < 0x02040000 or 0x03000000 <= addr < 0x03008000


def parse_link_map(map_path):
    """{(object, input section): address} from a GNU ld map (ELF or PE)."""
    out = {}
    started = False
    pending = None
    one = re.compile(r"^ (\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S+)\s*$")
    cont = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S+)\s*$")
    name_only = re.compile(r"^ (\S+)\s*$")
    with open(map_path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if not started:
                started = line.startswith("Linker script and memory map")
                continue
            m = one.match(line)
            if m:
                sec, addr, obj = m.group(1), int(m.group(2), 16), m.group(4)
                pending = None
            else:
                m = cont.match(line) if pending else None
                if m:
                    sec, addr, obj = pending, int(m.group(1), 16), m.group(3)
                    pending = None
                else:
                    m = name_only.match(line)
                    pending = m.group(1) if m and not m.group(1).startswith("*") else None
                    continue
            if sec.startswith("*") or not obj.endswith(".o"):
                continue
            out.setdefault((obj, sec), addr)
    return out


def strict(args, ghl, bin_syms):
    gba_name = {o: ghl.gba_object_name(o) for o in args.game_objs}
    ram = ghl.GbaRam(args.gba_map, args.gba_elf, args.gba_dir, set(gba_name.values()))
    linkmap = parse_link_map(args.map)

    placed, misplaced, stray, problems = [], [], [], []
    verified = set()           # (name, address) pairs accounted for
    globals_final = {}
    for host_obj, gobj in gba_name.items():
        syms, _secs = ghl.obj_data_symbols(host_obj)
        statics = Counter(ghl.base_name(s.name) for s in syms if not s.is_global)
        for s in syms:
            base = linkmap.get((host_obj, s.section))
            if base is None:
                problems.append(f"{gobj}: {s.name}: section {s.section} is not in the link map {args.map}")
                continue
            final = base + s.value
            verified.add((s.name, final))
            if s.is_global:
                globals_final[s.name] = final
            e = ram.resolve(gobj, s, statics)
            if e.status == "ram":
                if final == e.addr:
                    placed.append((s.name, final))
                else:
                    misplaced.append(f"{gobj}: {s.name}: host 0x{final:08X}, GBA 0x{e.addr:08X}")
            elif in_ram(final):
                why = {"other": "the GBA has it outside RAM", "absent": "not in the GBA build",
                       "ambiguous": "cannot be matched to one GBA variable"}[e.status]
                stray.append(f"{gobj}: {s.name} at host 0x{final:08X} in GBA RAM, but {why} ({e.note})")

    absolute_ok = 0
    for name, addr, kind in bin_syms:
        if not in_ram(addr):
            continue
        if kind in "Aa":
            g = ram.globals.get(name)
            if g is None:
                stray.append(f"absolute symbol {name} = 0x{addr:08X} in GBA RAM is not in the GBA ELF")
            elif g[0] != addr:
                misplaced.append(f"absolute symbol {name}: host 0x{addr:08X}, GBA 0x{g[0]:08X}")
            else:
                absolute_ok += 1
        elif (name, addr) not in verified:
            stray.append(f"{name} at host 0x{addr:08X} ({kind}) is in GBA RAM but is no checked game variable")
    in_bin = {}
    for name, addr, kind in bin_syms:
        in_bin.setdefault(name, set()).add(addr)
    for name, final in globals_final.items():
        if final not in in_bin.get(name, ()):
            problems.append(f"{name}: the link map gives 0x{final:08X}, the binary " +
                            (", ".join(f"0x{a:08X}" for a in sorted(in_bin[name])) if name in in_bin else "lacks it"))

    total = len(placed) + absolute_ok + len(misplaced)
    print(f"check_ram_addrs: {args.binary}: {total} RAM symbols checked: {len(placed)} host-built variables and "
          f"{absolute_ok} absolute symbols at their GBA addresses; {len(misplaced)} at another address, "
          f"{len(stray)} in GBA RAM without a GBA RAM address, {len(problems)} other problems")
    for line in misplaced + stray + problems:
        print("  " + line)
    if misplaced or stray or problems:
        print("check_ram_addrs: FAILED: RAM symbols must sit at their GBA addresses (DIVERGENCES.md B-12)")
        return 1
    return 0


def quick(host_path, gba_path):
    host, gba = symbols(host_path), symbols(gba_path)
    same, diff, missing = 0, [], 0
    for name, entries in host.items():
        for addr, kind in entries:
            if not in_ram(addr) or kind in "Aa":
                continue  # absolute symbols are GBA addresses by construction
            gba_addrs = [a for a, _ in gba.get(name, [])]
            if not gba_addrs:
                missing += 1
            elif addr in gba_addrs:
                same += 1
            else:
                diff.append((name, addr, gba_addrs))
    outside = [(name, addr, [a for a, _ in gba[name] if in_ram(a)])
               for name, entries in host.items() for addr, kind in entries
               if not in_ram(addr) and kind in "bBdD" and any(in_ram(a) for a, _ in gba.get(name, []))]
    print(f"RAM variables defined by host objects: {same} at the GBA address, "
          f"{len(diff)} at a different address, {missing} not in the GBA ELF; "
          f"{len(outside)} outside GBA RAM although the GBA has them in RAM")
    for name, addr, gba_addrs in sorted(diff + outside, key=lambda d: d[1]):
        print(f"  {name}: host 0x{addr:08X}, GBA " + ", ".join(f"0x{a:08X}" for a in gba_addrs))
    return 0


def main(argv=None, ghl=None, bin_reader=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) == 2 and not any(a.startswith("-") for a in argv):
        return quick(argv[0], argv[1])
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", required=True)
    ap.add_argument("--map", required=True, help="the host link map (-Wl,-Map)")
    ap.add_argument("--gba-map", required=True)
    ap.add_argument("--gba-elf", required=True)
    ap.add_argument("--gba-dir", default=".")
    ap.add_argument("--game-objs", nargs="+", required=True)
    args = ap.parse_args(argv)
    ghl = ghl or load_gen_host_link()
    return strict(args, ghl, (bin_reader or binary_symbols)(args.binary))


if __name__ == "__main__":
    sys.exit(main())
