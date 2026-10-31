#!/usr/bin/env python3
"""Generate the host link inputs for the D1-a spike.

Outputs:
  gba_mem.ld   A GNU ld script (used with -T, it augments the default script via
               INSERT). It places every RAM variable of the host-built game
               objects at the address the GBA build gave that variable, and
               defines every remaining undefined data symbol as an absolute
               symbol at its GBA address (from the GBA .elf symbol table). RAM
               symbols therefore land in the reserved RAM images and ROM data
               symbols read the user's ROM mapped at 0x08000000.
  autostubs.c  A logging stub for every undefined function that the host build
               does not provide yet.

RAM placement (DIVERGENCES.md B-12): the host compiles the game files with
-fdata-sections and with EWRAM_DATA, IWRAM_DATA and EWRAM_OVERLAY(id) left
empty (port/include/port.h), so every variable sits in a section of its own
(.bss.NAME or .data.NAME; .data$NAME in a MinGW object). Each such section is
placed at its variable's GBA address:
  - a global variable: its value in the GBA ELF symbol table;
  - a file-static or function-static variable: its section and offset in the
    GBA object (src/x.o) plus that section's base in the GBA map. A
    function-static is matched by its name without GCC's ".N" suffix.
So neither GCC's emit order nor its alignment of a variable can move it.
port/tools/check_ram_addrs.py checks the linked result.

The GBA build is the address oracle; nothing here is hand-maintained.
"""

import argparse
import os
import re
import subprocess
import sys
from collections import Counter, defaultdict, namedtuple

REGIONS = {
    # name: (start, size)
    "EWRAM": (0x02000000, 0x40000),
    "IWRAM": (0x03000000, 0x8000),
}
ROM_START, ROM_END = 0x08000000, 0x0A000000

# Never stub these: the host C library and libgcc provide them.
HOST_LIB_PREFIXES = ("__", "_GLOBAL_OFFSET_TABLE_")

# One data symbol of an object: is_global, the name of its section, its offset
# in that section, and its size (0 when the object does not say).
Sym = namedtuple("Sym", "name is_global section value size")
# Where the GBA build put one variable. status: "ram" (addr is in EWRAM or
# IWRAM), "other" (the GBA has it outside RAM, for example in ROM .data),
# "absent" (no GBA symbol of that name), "ambiguous" (several candidates).
Expect = namedtuple("Expect", "status addr size gba_name note")

STATIC_SUFFIX = re.compile(r"^(.*)\.\d+$")

# A COFF object rounds each section's size up to its alignment (a u16 in
# .data$NAME has a 4-byte section), so a host size that is only that padding
# is not a mismatch there. port/win32/pe_gba.py sets this; ELF sizes are exact.
SECTION_SIZE_PADDED = False

# GBA functions whose bytes the host reads as data, so they are bound to their
# ROM address (Thumb bit cleared) instead of becoming logging stubs.
# SoundMainRAM is the mixer's ARM code; m4aSoundInit copies its 0x400 bytes
# into SoundMainRAM_Buffer (m4a.c:69), so IWRAM holds what it holds on the
# GBA. Nothing executes the copy: the mixer is C (port/audio/m4a_1_pc.c).
# DIVERGENCES.md B-14 (port-plan/audio-design.md 1.3 and 1.6, its B-7).
CODE_READ_AS_DATA = {"SoundMainRAM"}

# GBA data that the host leaves to the ROM (not built on the host) and reaches
# only through a pointer in other ROM data, never by name, so no host object
# references it and the binding below would not name it. It is bound anyway, so
# the PC binary has the GBA's name at that address for debuggers (naming canon);
# like every absolute symbol, the address comes from the GBA ELF. Nothing reads
# these symbols. AnimSprite_EkrPopup: the sprite of the ROM anim script
# AnimScr_EkrPopup (src/banim-ekrpopup.c, PC_PORT; DIVERGENCES.md D-26).
ROM_DATA_NAMED = {"AnimSprite_EkrPopup"}


def run(cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def region_of(addr):
    for name, (start, size) in REGIONS.items():
        if start <= addr < start + size:
            return name
    return None


def base_name(name):
    """A function-static's name without the compiler's ".N" suffix."""
    m = STATIC_SUFFIX.match(name)
    return m.group(1) if m else name


def parse_map_sections(map_path, wanted_objs):
    """Return {obj: [(section, addr, size)]} for RAM input sections of wanted objects."""
    out = defaultdict(list)
    pending = None
    line_re = re.compile(r"^ (\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S+\.o)\s*$")
    cont_re = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S+\.o)\s*$")
    name_only_re = re.compile(r"^ (\S+)\s*$")
    with open(map_path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            m = line_re.match(line)
            if m:
                sec, addr, size, obj = m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4)
                pending = None
            else:
                m = cont_re.match(line)
                if m and pending:
                    sec = pending
                    addr, size, obj = int(m.group(1), 16), int(m.group(2), 16), m.group(3)
                    pending = None
                else:
                    m = name_only_re.match(line)
                    pending = m.group(1) if m else None
                    continue
            if obj in wanted_objs and size > 0 and region_of(addr):
                out[obj].append((sec, addr, size))
    return out


def elf_sections(obj_path):
    """{index: (name, size, flags, align)} for one ELF file (readelf -SW)."""
    secs = {}
    head = re.compile(r"^\s*\[\s*(\d+)\]\s+(\S+)\s+\S+\s+[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)\s+[0-9a-f]+\s+(.*)$")
    for line in run(["readelf", "-SW", obj_path]).splitlines():
        m = head.match(line)
        if not m:
            continue
        rest = m.group(4).split()
        flags = rest[0] if len(rest) == 4 else ""
        secs[m.group(1)] = (m.group(2), int(m.group(3), 16), flags, int(rest[-1]))
    return secs


def elf_symbols(obj_path):
    """[(name, bind, ndx, value, size, type)] for one ELF file (readelf -sW)."""
    out = []
    for line in run(["readelf", "-sW", obj_path]).splitlines():
        parts = line.split()
        if len(parts) != 8 or not parts[0].endswith(":") or parts[0] == "Num:":
            continue
        _, value, size, typ, bind, _vis, ndx, name = parts
        try:
            size = int(size, 0)
        except ValueError:
            size = 0
        out.append((name, bind, ndx, int(value, 16), size, typ))
    return out


def obj_data_symbols(obj_path):
    """Data symbols that one host object defines in writable sections, and its
    sections: ([Sym], {section: (size, align)}). ELF version; port/win32/pe_gba.py
    replaces it for COFF objects."""
    secs = elf_sections(obj_path)
    syms = []
    for name, bind, ndx, value, size, typ in elf_symbols(obj_path):
        if typ not in ("OBJECT", "NOTYPE") or not name or name.startswith(".") or ndx not in secs:
            continue
        sname, _ssize, flags, _al = secs[ndx]
        if "A" not in flags or "W" not in flags or "X" in flags:
            continue
        syms.append(Sym(name, bind in ("GLOBAL", "WEAK"), sname, value, size))
    return syms, {n: (sz, al) for n, sz, _f, al in secs.values()}


def gba_elf_symbols(elf_path):
    """({name: (value, size, type)} for GLOBAL GBA symbols, {name: set(values)} for all)."""
    glob, every = {}, defaultdict(set)
    for name, bind, ndx, value, size, typ in elf_symbols(elf_path):
        if ndx == "UND" or typ in ("SECTION", "FILE"):
            continue
        every[name].add(value)
        if bind in ("GLOBAL", "WEAK"):
            glob.setdefault(name, (value, size, typ))
    return glob, every


def gba_symbols(elf_path):
    """Return {name: (value, size, type)} for GLOBAL GBA symbols."""
    return gba_elf_symbols(elf_path)[0]


class GbaRam:
    """Where the GBA build put the variables of the GBA objects the host builds.

    gba_objs are GBA object names as the GBA map spells them (src/x.o)."""

    def __init__(self, map_path, elf_path, gba_dir, gba_objs):
        self.globals, self.every = gba_elf_symbols(elf_path)
        self.bases = {}
        for obj, lst in parse_map_sections(map_path, set(gba_objs)).items():
            for sec, addr, _size in lst:
                self.bases.setdefault((obj, sec), addr)
        self.gba_dir = gba_dir
        self._objs = {}

    def object(self, obj):
        """{"locals": {name: [(sec, value, size)]}, "by_base": {...}, "sizes": {global name: size},
        "ram": [(addr, size, name, is_global)]} for one GBA object (cached)."""
        if obj in self._objs:
            return self._objs[obj]
        info = {"locals": defaultdict(list), "by_base": defaultdict(list), "sizes": {}, "ram": [],
                "missing": False}
        path = os.path.join(self.gba_dir, obj)
        if not os.path.exists(path):
            info["missing"] = True
        else:
            secs = elf_sections(path)
            for name, bind, ndx, value, size, typ in elf_symbols(path):
                if typ not in ("OBJECT", "NOTYPE") or not name or name.startswith(("$", ".")):
                    continue
                if ndx == "COM":
                    info["sizes"].setdefault(name, size)
                    continue
                if ndx not in secs:
                    continue
                sec = secs[ndx][0]
                if bind in ("GLOBAL", "WEAK"):
                    info["sizes"].setdefault(name, size)
                    g = self.globals.get(name)
                    if g and region_of(g[0]):
                        info["ram"].append((g[0], size, name, True))
                else:
                    info["locals"][name].append((sec, value, size))
                    info["by_base"][base_name(name)].append((name, sec, value, size))
                    base = self.bases.get((obj, sec))
                    if base is not None:
                        info["ram"].append((base + value, size, name, False))
        self._objs[obj] = info
        return info

    def resolve(self, obj, sym, host_base_count):
        """Expect for one host symbol of the host object built from GBA object obj.
        host_base_count: Counter of base_name() over that host object's statics."""
        info = self.object(obj)
        if sym.is_global:
            g = self.globals.get(sym.name)
            if g is None:
                return Expect("absent", None, 0, None, "no GBA symbol of this name")
            size = info["sizes"].get(sym.name) or g[1]
            return Expect("ram" if region_of(g[0]) else "other", g[0], size, sym.name, "")
        cands = [(sym.name,) + c for c in info["locals"].get(sym.name, [])]
        what = f"GBA statics named {sym.name}"
        if not cands and base_name(sym.name) != sym.name:
            b = base_name(sym.name)
            cands = list(info["by_base"].get(b, []))
            what = f"GBA function-statics named {b} ({host_base_count[b]} on the host)"
        if not cands:
            return Expect("absent", None, 0, None,
                          "GBA object not found" if info["missing"] else "no GBA static of this name")
        if len(cands) > 1 or (base_name(sym.name) != sym.name and host_base_count[base_name(sym.name)] > 1):
            # Two functions of one file with a static of the same name (GCC's
            # ".N" numbers do not follow agbcc's). Fine when none is in RAM;
            # otherwise only a unique size decides.
            if all(self.bases.get((obj, c[1])) is None for c in cands):
                return Expect("other", None, 0, cands[0][0], f"all {len(cands)} {what} are outside GBA RAM")
            same = [c for c in cands if sym.size and c[3] == sym.size]
            if len(same) != 1:
                return Expect("ambiguous", None, 0, None, f"{len(cands)} {what}, some in GBA RAM")
            cands = same
        gname, sec, value, size = cands[0]
        base = self.bases.get((obj, sec))
        if base is None:
            return Expect("other", None, size, gname, f"GBA {obj}({sec}) is not in RAM")
        addr = base + value
        if addr not in self.every.get(gname, ()):
            raise SystemExit(f"gen_host_link: {obj}({sec})+0x{value:X} = 0x{addr:08X} for {gname}, but the GBA ELF "
                             f"has {gname} at " + ", ".join(f"0x{v:08X}" for v in sorted(self.every.get(gname, ())))
                             + " (map and objects disagree)")
        return Expect("ram", addr, size, gname, "")


def plan_ram(ram, gba_name):
    """Place each RAM variable of the host game objects.

    Returns (items, warnings, notes); an item is
    (addr, host section, host object, GBA size, host size, label)."""
    items, warnings, notes = [], [], []
    matched = defaultdict(set)
    for host_obj, gobj in gba_name.items():
        syms, secs = obj_data_symbols(host_obj)
        statics = Counter(base_name(s.name) for s in syms if not s.is_global)
        by_sec = defaultdict(list)
        for s in syms:
            e = ram.resolve(gobj, s, statics)
            if e.gba_name:
                matched[gobj].add(e.gba_name)
            if e.status == "ram":
                by_sec[s.section].append((e.addr - s.value, s, e))
            elif e.status == "ambiguous":
                warnings.append(f"WARNING {gobj}: cannot match {s.name}: {e.note}")
            elif e.status == "absent":
                notes.append(f"{gobj}: {s.name} ({s.section}) is not in the GBA build: {e.note}; "
                             f"it stays in host memory")
        for sec, lst in sorted(by_sec.items()):
            bases = sorted({b for b, _s, _e in lst})
            labels = ", ".join(s.name for _b, s, _e in lst)
            if len(bases) > 1:
                warnings.append(f"WARNING {gobj}({sec}) holds {labels}, which the GBA places apart; "
                                f"placed by the first")
            hsize, align = secs.get(sec, (0, 1))
            base = bases[0]
            gsize = lst[0][2].size if len(lst) == 1 and lst[0][1].value == 0 else 0
            pad = max(align, 4)
            padding_only = SECTION_SIZE_PADDED and gsize < hsize <= (gsize + pad - 1) // pad * pad
            if gsize and gsize != hsize and not padding_only:
                warnings.append(f"WARNING size mismatch {gobj}({labels}): GBA 0x{gsize:x}, host 0x{hsize:x}")
            if region_of(base) is None or region_of(base + max(hsize, 1) - 1) != region_of(base):
                warnings.append(f"WARNING {gobj}({labels}) at 0x{base:08X}+0x{hsize:x} leaves GBA RAM")
                continue
            if align > 1 and base % align:
                notes.append(f"{gobj}: {labels} at 0x{base:08X} is less aligned than the host's {align}; "
                             f"SUBALIGN(1) keeps the GBA address")
            items.append((base, sec, host_obj, gsize, hsize, labels))
            unplaced = [s.name for s in syms if s.section == sec and s not in {x for _b, x, _e in lst}]
            if unplaced:
                warnings.append(f"WARNING {gobj}({sec}) also holds {', '.join(unplaced)}, which have no GBA "
                                f"RAM address")
    dropped = []
    for gobj in sorted(set(gba_name.values())):
        for addr, size, name, is_global in ram.object(gobj)["ram"]:
            if name not in matched[gobj] and not is_global:
                dropped.append(f"{gobj}: {name} (0x{addr:08X}, {size} bytes)")
    if dropped:
        notes.append(f"{len(dropped)} GBA RAM statics of host-built objects that the host does not define "
                     f"(unused, so GCC leaves them out): " + "; ".join(dropped))
    return items, warnings, notes


def nm_symbols(objs):
    defined, undefined = set(), set()
    for line in run(["nm", "-P"] + objs).splitlines():
        parts = line.split()
        if len(parts) < 2 or parts[0].endswith(":"):
            continue
        name, kind = parts[0], parts[1]
        if kind == "U":
            undefined.add(name)
        elif kind.isupper() or kind in "vw":
            defined.add(name)
    return defined, undefined


def libc_symbols():
    """Symbols the 32-bit host C and math libraries export (never stubbed)."""
    names = set()
    for lib in ("/usr/lib32/libc.so.6", "/usr/lib32/libm.so.6"):
        try:
            for line in run(["nm", "-D", "--defined-only", lib]).splitlines():
                parts = line.split()
                if parts:
                    names.add(parts[-1].split("@")[0])
        except (subprocess.CalledProcessError, FileNotFoundError):
            continue
    return names


def gba_object_name(host_obj):
    """Host object path build/pc/src/x.o corresponds to GBA object src/x.o."""
    return host_obj[host_obj.index("src/"):]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", required=True)
    ap.add_argument("--elf", required=True)
    ap.add_argument("--gba-dir", default=None, help="GBA build tree with src/*.o (default: the map's directory)")
    ap.add_argument("--out-ld", required=True)
    ap.add_argument("--out-stubs", required=True)
    ap.add_argument("--game-objs", nargs="+", required=True)
    ap.add_argument("--port-objs", nargs="*", default=[])
    ap.add_argument("--verbose", action="store_true", help="also print the notes")
    args = ap.parse_args()
    gba_dir = args.gba_dir if args.gba_dir is not None else (os.path.dirname(args.map) or ".")

    gba_name = {o: gba_object_name(o) for o in args.game_objs}
    ram = GbaRam(args.map, args.elf, gba_dir, set(gba_name.values()))
    items, warnings, notes = plan_ram(ram, gba_name)

    ld = ["/* Generated by port/tools/gen_host_link.py. Do not edit. */",
          "/* One input section per RAM variable, each at the variable's GBA address. SUBALIGN(1):",
          "   the GBA address wins over the host's alignment of a section. */", "", "SECTIONS", "{"]
    n_out = 0
    for region, (start, size) in REGIONS.items():
        reg_items = sorted(it for it in items if region_of(it[0]) == region)
        # Split into lanes so overlapping overlay variables get their own output section.
        lanes = []
        for it in reg_items:
            for lane in lanes:
                last = lane[-1]
                if last[0] + max(last[3], last[4]) <= it[0]:
                    lane.append(it)
                    break
            else:
                lanes.append([it])
        if not lanes:
            lanes = [[]]
        for i, lane in enumerate(lanes):
            sec_name = f".gba_{region.lower()}" + (f"_lane{i}" if i else "")
            base = start if i == 0 else lane[0][0]
            ld.append(f"    {sec_name} 0x{base:08X} (NOLOAD) : SUBALIGN(1)")
            ld.append("    {")
            for addr, sec, host_obj, gsize, hsize, label in lane:
                ld.append(f"        . = 0x{addr - base:X};")
                ld.append(f"        KEEP(*{host_obj}({sec}))  /* {label} */")
            if i == 0:
                ld.append(f"        . = 0x{size:X};  /* span the whole {region} image */")
            ld.append("    }")
            n_out += 1
    ld.append("}")
    ld.append("INSERT AFTER .bss;")
    ld.append("")

    defined, undefined = nm_symbols(args.game_objs + args.port_objs)
    missing = sorted(undefined - defined)
    gsyms = ram.globals
    libc = libc_symbols()

    stubs, absolutes, unknown = [], [], []
    for name in sorted(ROM_DATA_NAMED - defined - undefined):
        info = gsyms.get(name)
        if info is None or info[2] == "FUNC" or not ROM_START <= info[0] < ROM_END or info[0] & 1:
            warnings.append(f"ROM_DATA_NAMED: {name} is not a ROM data symbol of the GBA ELF; not bound")
            continue
        absolutes.append((name, info[0], info[1], info[2] + ", named only (reached through ROM data)"))
    for name in missing:
        info = gsyms.get(name)
        if name in libc:
            continue
        # libgcc helpers come from the host; GBA linker-script symbols such as
        # __ewram_start (RAM addresses) are bound like any other data symbol.
        if name.startswith(HOST_LIB_PREFIXES) and not (info and region_of(info[0])):
            continue
        if info is None:
            unknown.append(name)
            continue
        value, size, typ = info
        is_code = typ == "FUNC" or (ROM_START <= value < ROM_END and value & 1)
        if name in CODE_READ_AS_DATA:
            absolutes.append((name, value & ~1, size, typ + " bytes, read as data"))
        elif is_code:
            stubs.append((name, value))
        else:
            absolutes.append((name, value, size, typ))

    ld.append("/* Data symbols not built on the host: bound to their GBA addresses. */")
    for name, value, size, typ in absolutes:
        ld.append(f"{name} = 0x{value:08X};  /* {typ} size {size} */")
    with open(args.out_ld, "w") as fh:
        fh.write("\n".join(ld) + "\n")

    c = [
        "/* Generated by port/tools/gen_host_link.py. Do not edit. */",
        "/* Functions the host build does not provide yet. Each logs its first call. */",
        "void PortStubHit(const char *name, unsigned gbaAddr, unsigned *count);",
        "",
    ]
    for name, value in stubs:
        c.append(f"static unsigned sCount_{name};")
        c.append(f"int {name}(void) {{ PortStubHit(\"{name}\", 0x{value:08X}u, &sCount_{name}); return 0; }}")
    for name in unknown:
        c.append(f"static unsigned sCount_{name};")
        c.append(f"int {name}(void) {{ PortStubHit(\"{name} (not in GBA ELF)\", 0u, &sCount_{name}); return 0; }}")
    with open(args.out_stubs, "w") as fh:
        fh.write("\n".join(c) + "\n")

    print(f"gen_host_link: {len(items)} RAM variables placed at their GBA addresses ({n_out} output sections), "
          f"{len(warnings)} warnings, {len(absolutes)} absolute data symbols, "
          f"{len(stubs)} function stubs, {len(unknown)} unknown symbols stubbed")
    for line in warnings:
        print("  " + line)
    for line in notes if args.verbose else []:
        print("  note: " + line)
    for name in unknown:
        print(f"  unknown symbol (stubbed as function): {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
