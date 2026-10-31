#!/usr/bin/env python3
"""PE (Windows, i686 MinGW) helpers for the D1-a host build (port/Makefile.mingw).

The Linux build gets EWRAM and IWRAM from the ELF loader: port/tools/gen_host_link.py
places each RAM variable of the game objects at its GBA address in NOLOAD output
sections, and port/hw/memory.c reserves the other regions with mmap at startup.

A PE image is one contiguous block above its image base, so the same linker
script cannot make the Windows loader map 0x02000000 and 0x03000000 while the
code sits at 0x10000000 (as on Linux; main_pc.c treats 0x10000000-0x1FFFFFFF as
host code and data). The Windows build therefore links the RAM sections at
their GBA addresses exactly as on Linux (the linker resolves every reference to
a GBA address; the image has no relocations), then `strip` removes those
sections from the PE section table. At startup memory.c reserves EWRAM and IWRAM
with VirtualAlloc like every other region (port/win32/posix_win32.c), and the
RAM variables are at their GBA addresses as on Linux.

Subcommands:
  gen-host-link ...   port/tools/gen_host_link.py for COFF objects: MinGW nm and
                      objdump, the leading underscore of i686 C symbols, MinGW's
                      libraries instead of glibc, and a linker script that keeps
                      the location counter of the PE sections that follow.
  relocs ...          port/tools/fixptr.py relocs for COFF host objects.
  check-ram ...       port/tools/check_ram_addrs.py for COFF objects and the PE
                      image (run on the linked image, before strip).
  strip IN OUT        drop the GBA RAM sections from the PE image and check the
                      layout the Windows loader needs (see check_layout).
  check EXE           check that layout again, for an image a later step rewrote
                      (port/Makefile.mingw: objcopy takes the debug information
                      out of the window build's release exe).
  stamp FILE          print FILE's PE TimeDateStamp (seconds since 1970), the
                      build stamp a fault report names.
  stamp --want N FILE...  check that each FILE has stamp N (port/Makefile.mingw:
                      the release exe and its .debug keep the link's stamp).
  run TOOL.py ARGS    run one of port/tools/*.py, routing the three above through
                      their COFF variants (how port/Makefile.mingw reuses port/Makefile).
"""

import importlib.util
import os
import re
import struct
import subprocess
import sys
from collections import namedtuple

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, "..", "tools")
CROSS = os.environ.get("MINGW_PREFIX", "i686-w64-mingw32-")
C_IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

IMAGE_BASE = 0x10000000          # Makefile.mingw: --image-base (Linux: -Ttext-segment=0x10000000)
GBA_RAM = ((0x02000000, 0x02040000), (0x03000000, 0x03008000))
GBA_WINDOW = (0x02000000, 0x0E010000)


def load_tool(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(TOOLS, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run(cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def c_name(coff_name):
    """i686 COFF prefixes C names with '_'; None for names C code cannot spell."""
    if not coff_name.startswith("_"):
        return None
    name = coff_name[1:]
    return name if C_IDENT.match(name) else None


# ---------------------------------------------------------------------------
# COFF replacements for the ELF helpers in gen_host_link.py and fixptr.py

def coff_sections(obj_path):
    """{section: (size, align, writable data)} for one COFF object (objdump -h)."""
    secs = {}
    lines = run([CROSS + "objdump", "-h", obj_path]).splitlines()
    for i, line in enumerate(lines):
        m = re.match(r"^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+2\*\*(\d+)",
                     line)
        if m:
            flags = lines[i + 1] if i + 1 < len(lines) else ""
            writable = "ALLOC" in flags and "READONLY" not in flags and "CODE" not in flags
            old = secs.get(m.group(1), (0, 1, writable))
            secs[m.group(1)] = (old[0] + int(m.group(2), 16), max(old[1], 1 << int(m.group(3))), writable)
    return secs


# The fields of gen_host_link.Sym.
Sym = namedtuple("Sym", "name is_global section value size")


def coff_obj_data_symbols(obj_path):
    """COFF version of gen_host_link.obj_data_symbols: the data symbols one object
    defines in writable sections (nm -f sysv gives each one's section), with the
    '_' prefix removed. Statics keep GCC's ".N" suffix (_sFoo.1 -> sFoo.1). A
    COFF symbol has no size; for a variable in its own section (-fdata-sections:
    .data$NAME, .bss$NAME) the section size is used."""
    secs = coff_sections(obj_path)
    syms = []
    for line in run([CROSS + "nm", "-f", "sysv", obj_path]).splitlines():
        parts = [p.strip() for p in line.split("|")]
        if len(parts) < 7:
            continue
        name, value, cls, section = parts[0], parts[1], parts[2], parts[6]
        if len(cls) != 1 or cls not in "bBdD" or not name.startswith("_") or not value:
            continue
        sec = secs.get(section)
        if sec is None or not sec[2]:
            continue
        cname = name[1:]
        size = sec[0] if section.endswith("$" + cname) else 0
        syms.append(Sym(cname, cls.isupper(), section, int(value, 16), size))
    return syms, {n: (s[0], s[1]) for n, s in secs.items()}


def coff_binary_symbols(path):
    """[(C name, addr, kind)] of a linked PE image (MinGW nm), for check_ram_addrs.py."""
    out = []
    for line in run([CROSS + "nm", "-n", path]).splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2].startswith("_"):
            out.append((parts[2][1:], int(parts[0], 16), parts[1]))
    return out


def coff_nm(objs):
    """(defined, undefined) C names in COFF objects. Decorated host names
    (__imp__X@4, stdcall @N) are not GBA symbols and are left out."""
    defined, undefined = set(), set()
    for line in run([CROSS + "nm", "-P"] + objs).splitlines():
        parts = line.split()
        if len(parts) < 2 or parts[0].endswith(":") or parts[0].endswith("]:"):
            continue
        name, kind = c_name(parts[0]), parts[1]
        if name is None:
            continue
        if kind == "U":
            undefined.add(name)
        elif kind.isupper() or kind in "vw":
            defined.add(name)
    return defined, undefined


def mingw_lib_symbols():
    """C names the MinGW runtime, Win32 import libraries and libgcc define
    (the libraries the gcc driver links by default, plus libm), and those of
    the libraries in PE_GBA_EXTRA_LIBS (space-separated paths: the window
    build's SDL3 import library, port/Makefile.mingw SDL=1), which its link
    provides, so none of their names becomes a stub."""
    cmd = run_driver_link_line()
    libdirs = [a[2:] for a in cmd if a.startswith("-L")]
    libs = [a[2:] for a in cmd if a.startswith("-l")] + ["m"]
    names = set()
    for path in os.environ.get("PE_GBA_EXTRA_LIBS", "").split():
        if not os.path.exists(path):
            raise SystemExit("pe_gba: PE_GBA_EXTRA_LIBS names a library that is not there: " + path)
        for line in run([CROSS + "nm", "-P", "--defined-only", path]).splitlines():
            parts = line.split()
            if len(parts) >= 2 and not parts[0].endswith(":") and not parts[0].endswith("]:"):
                n = c_name(parts[0])
                if n:
                    names.add(n)
    for lib in dict.fromkeys(libs):
        for d in libdirs:
            for fn in ("lib%s.a" % lib, "lib%s.dll.a" % lib):
                path = os.path.join(d, fn)
                if os.path.exists(path):
                    try:
                        out = run([CROSS + "nm", "-P", "--defined-only", path])
                    except subprocess.CalledProcessError:
                        continue
                    for line in out.splitlines():
                        parts = line.split()
                        if len(parts) >= 2 and not parts[0].endswith(":") and not parts[0].endswith("]:"):
                            n = c_name(parts[0])
                            if n:
                                names.add(n)
                    break
            else:
                continue
            break
    return names


def run_driver_link_line():
    r = subprocess.run([CROSS + "gcc", "-###", "-x", "c", os.devnull, "-o", os.devnull],
                       capture_output=True, text=True)
    for line in r.stderr.splitlines():
        if "collect2" in line or line.lstrip().startswith('"') and "ld" in line.split()[0]:
            return [t.strip('"') for t in line.split()]
    raise SystemExit("pe_gba: cannot read the MinGW driver's link line")


# ---------------------------------------------------------------------------
# gen-host-link

def cmd_gen_host_link(argv):
    ghl = load_tool("gen_host_link")
    ghl.obj_data_symbols = coff_obj_data_symbols
    ghl.SECTION_SIZE_PADDED = True
    ghl.nm_symbols = coff_nm
    ghl.libc_symbols = mingw_lib_symbols
    sys.argv = ["gen_host_link.py"] + argv
    rc = ghl.main()
    out_ld = argv[argv.index("--out-ld") + 1]
    with open(out_ld) as fh:
        text = fh.read()
    head, sep, tail = text.partition("/* Data symbols not built on the host: bound to their GBA addresses. */")
    if not sep:
        raise SystemExit("pe_gba: unexpected gen_host_link.py output")
    # 1. The absolute symbols carry the C prefix '_'.
    tail = re.sub(r"(?m)^([A-Za-z_][A-Za-z0-9_]*) = ", r"_\1 = ", tail)
    # 2. INSERT AFTER .bss puts the GBA sections where the PE sections after .bss
    #    (.idata, .CRT, .tls, debug) would take their address from; restore the
    #    location counter after them so those stay above the image base.
    head = head.replace("SECTIONS\n{\n", "SECTIONS\n{\n    __port_pe_dot = .;\n", 1)
    head = head.replace("}\nINSERT AFTER .bss;", "    . = __port_pe_dot;\n}\nINSERT AFTER .bss;", 1)
    if "__port_pe_dot = .;" not in head or ". = __port_pe_dot;" not in head:
        raise SystemExit("pe_gba: could not add the location-counter restore to " + out_ld)
    head = head.replace("Do not edit. */", "Do not edit. */\n/* PE variant (port/win32/pe_gba.py gen-host-link). */", 1)
    with open(out_ld, "w") as fh:
        fh.write(head + sep + tail)
    return rc


# ---------------------------------------------------------------------------
# relocs

def cmd_relocs(argv):
    fixptr = load_tool("fixptr")

    def host_defined(objs):
        return coff_nm(list(objs))[0]

    fixptr.host_defined = host_defined
    sys.argv = ["fixptr.py", "relocs"] + argv
    return fixptr.main()


# ---------------------------------------------------------------------------
# check-ram

def cmd_check_ram(argv):
    """port/tools/check_ram_addrs.py with the COFF readers. Run it on the linked
    image before strip: strip turns the GBA RAM symbols into absolute ones."""
    ghl = load_tool("gen_host_link")
    ghl.obj_data_symbols = coff_obj_data_symbols
    ghl.SECTION_SIZE_PADDED = True
    chk = load_tool("check_ram_addrs")
    return chk.main(argv, ghl=ghl, bin_reader=coff_binary_symbols)


# ---------------------------------------------------------------------------
# strip

def align(v, a):
    return (v + a - 1) & ~(a - 1)


class Pe:
    def __init__(self, data):
        self.d = d = data
        if d[:2] != b"MZ":
            raise SystemExit("pe_gba: not a PE file")
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        if d[pe:pe + 4] != b"PE\0\0":
            raise SystemExit("pe_gba: not a PE file")
        self.coff = pe + 4
        self.opt = self.coff + 20
        (self.machine, self.nsec, _ts, self.symptr, self.nsyms, self.optsz,
         self.fchars) = struct.unpack_from("<HHIIIHH", d, self.coff)
        magic = struct.unpack_from("<H", d, self.opt)[0]
        if magic != 0x10B:
            raise SystemExit("pe_gba: not a PE32 (32-bit) image")
        self.image_base, self.sec_align, self.file_align = struct.unpack_from("<III", d, self.opt + 28)
        self.size_of_image, self.size_of_headers = struct.unpack_from("<II", d, self.opt + 56)
        self.dll_chars = struct.unpack_from("<H", d, self.opt + 70)[0]
        self.ndirs = struct.unpack_from("<I", d, self.opt + 92)[0]
        self.sect = self.opt + self.optsz
        self.sections = []
        for i in range(self.nsec):
            off = self.sect + 40 * i
            vsize, va = struct.unpack_from("<II", d, off + 8)
            self.sections.append({
                "num": i + 1, "raw": bytes(d[off:off + 40]), "vsize": vsize, "va": va,
                "vma": (self.image_base + va) & 0xFFFFFFFF, "name": self.section_name(d[off:off + 8]),
                "chars": struct.unpack_from("<I", d, off + 36)[0]})

    def section_name(self, raw):
        name = bytes(raw).rstrip(b"\0").decode("ascii", "replace")
        if name.startswith("/") and self.symptr:
            strtab = self.symptr + 18 * self.nsyms
            off = int(name[1:])
            end = self.d.index(b"\0", strtab + off)
            name = self.d[strtab + off:end].decode("ascii", "replace")
        return name

    def data_dir(self, i):
        if i >= self.ndirs:
            return 0, 0
        return struct.unpack_from("<II", self.d, self.opt + 96 + 8 * i)


def check_layout(pe):
    """The rules the Windows loader applies to an image, plus the D1-a ones."""
    errs = []
    if pe.image_base != IMAGE_BASE:
        errs.append(f"image base 0x{pe.image_base:08X}, expected 0x{IMAGE_BASE:08X}")
    if pe.dll_chars & 0x0040:
        errs.append("DYNAMIC_BASE is set: ASLR could move the image (link with --disable-dynamicbase)")
    if not pe.fchars & 0x0001:
        errs.append("RELOCS_STRIPPED is not set (link with --disable-reloc-section)")
    if pe.data_dir(5) != (0, 0):
        errs.append("the image has a base-relocation directory")
    nxt = align(pe.size_of_headers, pe.sec_align)
    for s in pe.sections:
        if s["va"] != nxt:
            errs.append(f"section {s['name']} at RVA 0x{s['va']:X}, the loader needs 0x{nxt:X} (sections must be "
                        f"adjacent and in order)")
        nxt = align(s["va"] + max(s["vsize"], 1), pe.sec_align)
        lo, hi = s["vma"], s["vma"] + s["vsize"]
        if lo < GBA_WINDOW[1] and hi > GBA_WINDOW[0]:
            errs.append(f"section {s['name']} (0x{lo:08X}-0x{hi:08X}) is inside the GBA window")
    if pe.size_of_image != nxt:
        errs.append(f"SizeOfImage 0x{pe.size_of_image:X}, expected 0x{nxt:X}")
    if pe.image_base + pe.size_of_image > 0x20000000:
        errs.append("the image reaches past 0x20000000 (main_pc.c's host range is 0x10000000-0x1FFFFFFF)")
    return errs


def cmd_strip(argv):
    if len(argv) != 2:
        raise SystemExit("usage: pe_gba.py strip IN.exe OUT.exe")
    src, dst = argv
    with open(src, "rb") as fh:
        data = bytearray(fh.read())
    pe = Pe(data)

    gone, keep = [], []
    for s in pe.sections:
        below = s["vma"] < pe.image_base
        in_ram = any(lo <= s["vma"] and s["vma"] + s["vsize"] <= hi for lo, hi in GBA_RAM)
        if below and in_ram:
            gone.append(s)
        elif below:
            raise SystemExit(f"pe_gba: section {s['name']} at 0x{s['vma']:08X} is below the image base and "
                             f"not GBA RAM")
        else:
            keep.append(s)
    if not gone:
        raise SystemExit("pe_gba: no GBA RAM sections found (was the PE gba_mem.ld used?)")

    # Section table: the kept headers in order, then zeros.
    renumber = {}
    for i, s in enumerate(keep):
        data[pe.sect + 40 * i:pe.sect + 40 * (i + 1)] = s["raw"]
        renumber[s["num"]] = i + 1
    for i in range(len(keep), pe.nsec):
        data[pe.sect + 40 * i:pe.sect + 40 * (i + 1)] = bytes(40)
    struct.pack_into("<H", data, pe.coff + 2, len(keep))

    # COFF symbol table (kept by the MinGW linker for debuggers and nm):
    # symbols of a removed section become absolute at their GBA address.
    gone_vma = {s["num"]: s["vma"] for s in gone}
    i = 0
    while i < pe.nsyms:
        off = pe.symptr + 18 * i
        value, secnum = struct.unpack_from("<Ih", data, off + 8)
        naux = data[off + 17]
        if secnum in gone_vma:
            struct.pack_into("<Ih", data, off + 8, (gone_vma[secnum] + value) & 0xFFFFFFFF, -1)
        elif secnum > 0:
            struct.pack_into("<h", data, off + 12, renumber.get(secnum, 0))
        i += 1 + naux

    out = Pe(data)
    errs = check_layout(out)
    if errs:
        raise SystemExit("pe_gba: the stripped image is not loadable as D1-a needs:\n  " + "\n  ".join(errs))
    tmp = dst + ".tmp"
    with open(tmp, "wb") as fh:
        fh.write(data)
    os.replace(tmp, dst)
    os.chmod(dst, 0o755)
    print(f"pe_gba strip: {dst}: image 0x{out.image_base:08X}-0x{out.image_base + out.size_of_image:08X}, "
          f"{len(keep)} sections, layout OK; removed "
          + ", ".join(f"{s['name']} 0x{s['vma']:08X}+0x{s['vsize']:X}" for s in gone)
          + " (memory.c reserves them at startup)")
    return 0


def cmd_check(argv):
    if len(argv) != 1:
        raise SystemExit("usage: pe_gba.py check EXE")
    with open(argv[0], "rb") as fh:
        pe = Pe(bytearray(fh.read()))
    errs = check_layout(pe)
    if errs:
        raise SystemExit(f"pe_gba: {argv[0]} is not loadable as D1-a needs:\n  " + "\n  ".join(errs))
    print(f"pe_gba check: {argv[0]}: image 0x{pe.image_base:08X}-0x{pe.image_base + pe.size_of_image:08X}, "
          f"{len(pe.sections)} sections, {pe.nsyms} COFF symbols, layout OK")
    return 0


def pe_stamp(path):
    """The COFF header's TimeDateStamp (an exe and objcopy's --only-keep-debug file both have the header)."""
    with open(path, "rb") as fh:
        d = fh.read(4096)
    if d[:2] != b"MZ" or len(d) < 0x40:
        raise SystemExit(f"pe_gba: {path}: not a PE file")
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    if d[pe:pe + 4] != b"PE\0\0":
        raise SystemExit(f"pe_gba: {path}: not a PE file")
    return struct.unpack_from("<I", d, pe + 8)[0]


def cmd_stamp(argv):
    if argv[:1] == ["--want"] and len(argv) >= 3:
        want, bad = int(argv[1]), 0
        for path in argv[2:]:
            got = pe_stamp(path)
            bad += got != want
            print(f"pe_gba stamp: {path}: {got}" + ("" if got == want else f", not {want}"))
        if bad:
            raise SystemExit("pe_gba: the build stamps differ: a fault report would not name its own .debug")
        return 0
    if len(argv) != 1:
        raise SystemExit("usage: pe_gba.py stamp FILE | pe_gba.py stamp --want N FILE...")
    print(pe_stamp(argv[0]))
    return 0


def cmd_run(argv):
    """port/Makefile.mingw sets PYTHON to "python3 port/win32/pe_gba.py run", so
    port/Makefile's recipes call its tools through here: the three that read host
    objects get their COFF variants, everything else runs unchanged."""
    if not argv:
        raise SystemExit("usage: pe_gba.py run TOOL.py [ARGS...]")
    tool, args = argv[0], argv[1:]
    base = os.path.basename(tool)
    if base == "gen_host_link.py":
        return cmd_gen_host_link(args)
    if base == "fixptr.py" and args[:1] == ["relocs"]:
        return cmd_relocs(args[1:])
    if base == "check_ram_addrs.py":
        return cmd_check_ram(args)
    import runpy
    sys.argv = [tool] + args
    runpy.run_path(tool, run_name="__main__")
    return 0


def main():
    cmds = {"gen-host-link": cmd_gen_host_link, "relocs": cmd_relocs, "check-ram": cmd_check_ram,
            "strip": cmd_strip, "check": cmd_check, "stamp": cmd_stamp, "run": cmd_run}
    if len(sys.argv) < 2 or sys.argv[1] not in cmds:
        raise SystemExit(__doc__)
    return cmds[sys.argv[1]](sys.argv[2:])


if __name__ == "__main__":
    sys.exit(main())
