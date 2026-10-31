#!/usr/bin/env python3
"""fixptr: literal GBA addresses in ROM data -> symbol references -> host relocations.

Plan risk 12: ROM data holds GBA code pointers (proc scripts, menu tables, ...).
Under D1-a the host reads ROM data in place (the user's ROM is mapped at
0x08000000), so a GBA code pointer read from it is called as-is and crashes.

Two steps, both read-only on the GBA tree:

  convert  Copy data/*.s into OUT/data and rewrite every literal GBA address
           that maps to a known symbol into a symbol reference. "Literal"
           means a numeric .word/.4byte/.long/.int operand, or an aligned word
           inside an `.incbin "baserom.gba", off, size` blob: most ROM data is
           still baserom blobs, so most literal pointers live there. A blob
           with pointers in it is split around them:

               .incbin "baserom.gba", 0xA21658, 0x10
               .word OpAnimInit  @ fixptr: 0x080C6A55 at 0x08A21668
               .incbin "baserom.gba", 0xA2166C, 0x4760

           The Thumb bit is kept: a Thumb function's ELF symbol value has bit
           0 set, so `.word Func` is the same word as the literal. A literal
           that equals a Thumb function's start without bit 0 becomes
           `Func-1`. The copies are then assembled with the GBA assembler and
           the GBA Makefile's flags, which turns every symbol word into an
           R_ARM_ABS32 relocation. Nothing in the GBA tree is written, so the
           GBA ROM stays byte-identical.

  relocs   Collect every R_ARM_ABS32 relocation that lands in ROM data: the
           assembled copies of data/*.s, plus the GBA objects of src/*.c
           (data sections only) and sound/. Each one is checked against the
           ROM (the relocated value must equal the ROM word). Then it writes a
           C table of the words whose host value differs from the GBA value:
             - code pointers -> the host function (a generated logging stub
               when the host does not build that function yet);
             - data pointers to data the host builds itself (C objects, port
               code) -> the host object.
           Data the host does not build is bound to its GBA address by
           gen_host_link.py, so pointers to it need no change.
           At startup the host rewrites those words in its private copy of
           the ROM (port/hw/romreloc.c).

Anything that cannot be resolved is listed with the reason in the report
(default port/tools/fixptr_unresolved.txt).
"""

import argparse
import ast
import bisect
import concurrent.futures
import operator
import os
import re
import struct
import subprocess
import sys
from collections import Counter, defaultdict

ROM_BASE, ROM_END = 0x08000000, 0x0A000000
RAM_RANGES = ((0x02000000, 0x02040000), (0x03000000, 0x03008000))
REGION_BASES = (0x02000000, 0x03000000, 0x08000000)

# The multiboot image for the other GBA: its addresses are in that machine's memory.
EXCLUDED_FILES = {"data/data_fe6sio.s": "multiboot image for the linked GBA; its addresses are "
                                        "in the other machine's memory, not FE8's"}

ASFLAGS = ["-mcpu=arm7tdmi", "-mthumb-interwork", "-I", "include"]  # GBA Makefile ASFLAGS

STT_NOTYPE, STT_OBJECT, STT_FUNC, STT_SECTION, STT_FILE = 0, 1, 2, 3, 4
STB_LOCAL, STB_GLOBAL, STB_WEAK = 0, 1, 2
SHN_UNDEF, SHN_ABS = 0, 0xFFF1
SHT_SYMTAB, SHT_RELA, SHT_REL = 2, 4, 9
SHF_ALLOC = 2
R_ARM_ABS32 = 2

C_KEYWORDS = set("""auto break case char const continue default do double else enum extern float
for goto if inline int long register restrict return short signed sizeof static struct switch
typedef union unsigned void volatile while _Bool asm typeof""".split())
C_IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def in_rom(v):
    return ROM_BASE <= v < ROM_END


def in_ram(v):
    return any(lo <= v < hi for lo, hi in RAM_RANGES)


# ---------------------------------------------------------------------------
# Minimal ELF32 little-endian reader (GBA objects/ELF and i386 host objects).

class Sym:
    __slots__ = ("name", "value", "size", "bind", "type", "shndx")

    def __init__(self, name, value, size, bind, typ, shndx):
        self.name, self.value, self.size = name, value, size
        self.bind, self.type, self.shndx = bind, typ, shndx

    @property
    def is_global(self):
        return self.bind in (STB_GLOBAL, STB_WEAK)


class Elf32:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as fh:
            self.d = d = fh.read()
        if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 1:
            raise ValueError(f"{path}: not a little-endian ELF32 file")
        (_t, _m, _v, _entry, _phoff, shoff, _flags, _ehsize, _phentsize, _phnum,
         shentsize, shnum, shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", d, 16)
        self.sections = []
        for i in range(shnum):
            f = struct.unpack_from("<10I", d, shoff + i * shentsize)
            self.sections.append({"name_off": f[0], "type": f[1], "flags": f[2], "addr": f[3],
                                  "off": f[4], "size": f[5], "link": f[6], "info": f[7]})
        shstr = self.sections[shstrndx]
        for s in self.sections:
            s["name"] = self._str(shstr, s["name_off"])
        self.symbols = []
        for s in self.sections:
            if s["type"] == SHT_SYMTAB:
                strtab = self.sections[s["link"]]
                for j in range(s["size"] // 16):
                    nm, val, sz, info, _other, shndx = struct.unpack_from("<IIIBBH", d, s["off"] + 16 * j)
                    self.symbols.append(Sym(self._str(strtab, nm), val, sz, info >> 4, info & 15, shndx))
                break

    def _str(self, sec, off):
        start = sec["off"] + off
        return self.d[start:self.d.index(b"\0", start)].decode("latin-1")

    def section_bytes(self, idx):
        s = self.sections[idx]
        return self.d[s["off"]:s["off"] + s["size"]]

    def rels(self):
        """Yield (target section index, offset, symbol index, type) for every REL entry."""
        for s in self.sections:
            if s["type"] == SHT_RELA:
                raise ValueError(f"{self.path}: RELA relocations are not expected for ARM")
            if s["type"] != SHT_REL:
                continue
            for j in range(s["size"] // 8):
                off, info = struct.unpack_from("<II", self.d, s["off"] + 8 * j)
                yield s["info"], off, info >> 8, info & 0xFF


# ---------------------------------------------------------------------------
# GBA symbol table.

class GbaSymbols:
    """Symbol lookups over the GBA ELF (exact values keep the Thumb bit)."""

    def __init__(self, elf_path):
        elf = Elf32(elf_path)
        self.funcs = defaultdict(list)      # st_value (Thumb bit kept) -> [Sym]
        self.data = defaultdict(list)       # value -> [Sym]
        self.globals = {}                   # name -> Sym
        sized = []
        for s in elf.symbols:
            if not s.name or s.name.startswith(("$", ".L")) or s.type in (STT_SECTION, STT_FILE):
                continue
            if s.shndx == SHN_ABS and s.is_global:
                # Linker-script symbols, e.g. the banim blobs placed by the banim linker.
                self.globals.setdefault(s.name, s)
                continue
            if s.shndx in (SHN_UNDEF, SHN_ABS) or s.shndx >= len(elf.sections):
                continue
            if not elf.sections[s.shndx]["flags"] & SHF_ALLOC:
                continue
            if s.is_global:
                self.globals.setdefault(s.name, s)
            if s.type == STT_FUNC:
                self.funcs[s.value].append(s)
                if s.size:
                    sized.append((s.value & ~1, s))
            else:
                self.data[s.value].append(s)
                if s.size and s.type == STT_OBJECT:
                    sized.append((s.value, s))
        sized.sort(key=lambda t: (t[0], -t[1].size))
        self.sized_starts = [t[0] for t in sized]
        self.sized = [t[1] for t in sized]

    @staticmethod
    def pick(cands, prefer_sized=False):
        """Best name among symbols at one address: global first, then sized, then by name."""
        def key(s):
            return (not s.is_global, prefer_sized and not s.size, s.type != STT_OBJECT, s.name)
        return sorted(cands, key=key)[0] if cands else None

    def containing(self, v):
        """Smallest sized object or function that contains v (not at its start)."""
        i = bisect.bisect_right(self.sized_starts, v) - 1
        best = None
        # Walk back over a few candidates: sized objects can nest (aliases).
        for j in range(i, max(-1, i - 8), -1):
            s = self.sized[j]
            start = s.value & ~1 if s.type == STT_FUNC else s.value
            if start < v < start + s.size and (best is None or s.size < best.size):
                best = s
        return best

    def nearest_label(self, v):
        """Nearest preceding data label (for the report only)."""
        keys = self._label_keys()
        i = bisect.bisect_right(keys, v) - 1
        if i < 0:
            return None
        s = self.pick(self.data[keys[i]])
        return s, v - keys[i]

    def _label_keys(self):
        if not hasattr(self, "_lk"):
            self._lk = sorted(self.data)
        return self._lk


def classify(syms, v, blob=False):
    """Decide what a literal word v is.

    blob=True is for words inside baserom blobs. Those blobs are mostly
    graphics and compressed data, so only exact symbol starts count there
    (no `Func-1`, no `Object+offset`); the caller also checks the neighbours.

    Returns (kind, text, sym) where kind is one of:
      'code'   - convert to `text` (a global function, Thumb bit kept)
      'data'   - convert to `text` (a global data symbol, maybe +offset)
      None     - not an address (no report line)
      other    - an unresolved reason code; `text` explains it
    """
    if not (in_rom(v) or in_ram(v)):
        return None, None, None
    if v in REGION_BASES:
        return "region-base", f"0x{v:08X} is a memory region base, treated as a constant", None
    cands = syms.funcs.get(v)
    if cands:
        s = syms.pick(cands)
        if s.is_global:
            return "code", s.name, s
        return "static-function", (f"{s.name} is a file-local (static) function; the host cannot "
                                   f"name it from a table"), s
    if not v & 1 and syms.funcs.get(v | 1):
        s = syms.pick(syms.funcs[v | 1])
        if blob:
            return "inside-function", f"{s.name} start without the Thumb bit (not a code pointer)", s
        if s.is_global:
            # Equals a Thumb function's start without the Thumb bit.
            return "code", f"{s.name}-1", s
        return "static-function", f"{s.name}-1 (file-local function, no Thumb bit)", s
    cands = syms.data.get(v)
    if cands:
        s = syms.pick(cands, prefer_sized=True)
        if s.is_global:
            return "data", s.name, s
        return "static-data", f"{s.name} is file-local data", s
    s = syms.containing(v)
    if s is not None:
        start = s.value & ~1 if s.type == STT_FUNC else s.value
        off = v - start
        if s.type == STT_FUNC:
            return "inside-function", f"inside function {s.name}+0x{off:X}, not a function start", s
        if blob:
            return "inside-data", f"inside {s.name}+0x{off:X}, not at a symbol start", s
        if s.is_global:
            return "data", f"{s.name}+0x{off:X}", s
        return "static-data", f"inside file-local data {s.name}+0x{off:X}", s
    near = syms.nearest_label(v)
    where = f"; nearest label {near[0].name}+0x{near[1]:X}" if near else ""
    if in_rom(v):
        return "no-symbol-rom", f"ROM address with no symbol{where}", None
    return "no-symbol-ram", f"RAM address with no symbol{where}", None


# ---------------------------------------------------------------------------
# convert

_ALLOWED_BINOPS = {ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul,
                   ast.LShift: operator.lshift, ast.RShift: operator.rshift,
                   ast.BitOr: operator.or_, ast.BitAnd: operator.and_}


def eval_int(expr):
    """Evaluate a GAS integer expression made of literals and + - * << >> | &."""
    def ev(n):
        if isinstance(n, ast.Expression):
            return ev(n.body)
        if isinstance(n, ast.Constant) and isinstance(n.value, int):
            return n.value
        if isinstance(n, ast.BinOp) and type(n.op) in _ALLOWED_BINOPS:
            return _ALLOWED_BINOPS[type(n.op)](ev(n.left), ev(n.right))
        if isinstance(n, ast.UnaryOp) and isinstance(n.op, ast.USub):
            return -ev(n.operand)
        raise ValueError(expr)
    return ev(ast.parse(expr.strip(), mode="eval"))


LABELS = r"(?P<pre>\s*(?:[A-Za-z_.$][\w.$]*:\s*)*)"
INCBIN_RE = re.compile(LABELS + r'\.incbin\s+"baserom\.gba"\s*,\s*(?P<args>[^@]*?)\s*(?P<cmt>@.*)?$')
WORD_RE = re.compile(LABELS + r"\.(?P<dir>word|4byte|long|int)\s+(?P<ops>[^@]*?)\s*(?P<cmt>@.*)?$",
                     re.IGNORECASE)
NUM_RE = re.compile(r"^(0[xX][0-9a-fA-F]+|\d+)$")


def structured(w, syms):
    """True for words that look like struct fields rather than packed pixels."""
    if w < 0x10000 or w == 0xFFFFFFFF:
        return True
    if (w >> 16) < 0x400 and (w & 0xFFFF) < 0x400:
        return True                     # two small halfwords
    if 0x04000000 <= w < 0x04000400:
        return True                     # I/O register address
    if in_ram(w) and not w & 3:
        return True
    return w in syms.funcs or w in syms.data


NEIGHBOURS = 4          # words checked on each side of a candidate
MIN_STRUCTURED = 5      # of 2 * NEIGHBOURS


def context_score(rom, off, syms):
    n = 0
    for k in list(range(-NEIGHBOURS, 0)) + list(range(1, NEIGHBOURS + 1)):
        o = off + 4 * k
        if 0 <= o <= len(rom) - 4 and structured(struct.unpack_from("<I", rom, o)[0], syms):
            n += 1
    return n


def convert_file(rel, text, rom, syms, conv, unres):
    """Return the converted text of one .s file."""
    out = []
    indent_default = "\t"
    for lineno, line in enumerate(text.splitlines(), 1):
        where = f"{rel}:{lineno}"
        m = INCBIN_RE.match(line)
        if m:
            args = [a for a in m.group("args").split(",")]
            if len(args) != 2:
                unres.append(("incbin-form", where, None, None, "baserom incbin without an explicit size"))
                out.append(line)
                continue
            try:
                off, size = eval_int(args[0]), eval_int(args[1])
            except ValueError:
                unres.append(("incbin-form", where, None, None, f"cannot evaluate incbin arguments {m.group('args')!r}"))
                out.append(line)
                continue
            hits = []
            start = (off + 3) & ~3
            blob = rom[start:off + size]
            for k in range(0, len(blob) - 3, 4):
                v = struct.unpack_from("<I", blob, k)[0]
                kind, txt, _s = classify(syms, v, blob=True)
                site = ROM_BASE + start + k
                if kind in ("code", "data"):
                    score = context_score(rom, start + k, syms)
                    if score >= MIN_STRUCTURED:
                        hits.append((start + k, v, kind, txt))
                    else:
                        unres.append((f"blob-unlikely-{kind}", where, site, v,
                                      f"equals {txt}, but only {score}/{2 * NEIGHBOURS} neighbouring words "
                                      f"look like struct fields (graphics or compressed data?)"))
                elif kind is not None:
                    unres.append(("blob-" + kind, where, site, v, txt))
            if not hits:
                out.append(line)
                continue
            pre = m.group("pre")
            indent = re.match(r"\s*", pre).group(0) or indent_default
            cmt = (" " + m.group("cmt")) if m.group("cmt") else ""
            pos = off
            first = True
            for hoff, v, kind, txt in hits:
                if hoff > pos:
                    lead = pre if first else indent
                    out.append(f'{lead}.incbin "baserom.gba", 0x{pos:X}, 0x{hoff - pos:X}'
                               + (cmt if first else ""))
                    first = False
                lead = pre if first else indent
                first = False
                out.append(f"{lead}.word {txt}  @ fixptr: 0x{v:08X} at 0x{ROM_BASE + hoff:08X}")
                conv.append((kind, where, ROM_BASE + hoff, v, txt))
                pos = hoff + 4
            if pos < off + size:
                out.append(f'{indent}.incbin "baserom.gba", 0x{pos:X}, 0x{off + size - pos:X}')
            continue
        m = WORD_RE.match(line)
        if m:
            ops = [o.strip() for o in m.group("ops").split(",")]
            changed = False
            for i, op in enumerate(ops):
                if not NUM_RE.match(op):
                    continue
                v = int(op, 0) & 0xFFFFFFFF
                kind, txt, _s = classify(syms, v)
                if kind in ("code", "data"):
                    ops[i] = txt
                    changed = True
                    conv.append((kind, where, None, v, txt))
                elif kind is not None:
                    unres.append((kind, where, None, v, txt))
            if changed:
                cmt = (" " + m.group("cmt")) if m.group("cmt") else ""
                line = f"{m.group('pre')}.{m.group('dir')} {', '.join(ops)}  @ fixptr: was {m.group('ops')}{cmt}"
        out.append(line)
    return "\n".join(out) + "\n"


def assemble(gba_dir, src, obj):
    os.makedirs(os.path.dirname(obj), exist_ok=True)
    r = subprocess.run(["arm-none-eabi-as"] + ASFLAGS + ["-I", ".", src, "-o", obj],
                       cwd=gba_dir, capture_output=True, text=True)
    return src, r.returncode, r.stderr


def cmd_convert(a):
    syms = GbaSymbols(a.elf)
    with open(a.rom, "rb") as fh:
        rom = fh.read()
    files = sorted(f for f in os.listdir(os.path.join(a.gba_dir, "data")) if f.endswith(".s"))
    conv, unres = [], []
    srcs = []
    for f in files:
        rel = f"data/{f}"
        with open(os.path.join(a.gba_dir, rel), encoding="utf-8", errors="surrogateescape") as fh:
            text = fh.read()
        if rel in EXCLUDED_FILES:
            unres.append(("excluded-file", rel, None, None, EXCLUDED_FILES[rel]))
            new = text
        else:
            new = convert_file(rel, text, rom, syms, conv, unres)
        dst = os.path.join(a.out, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "w", encoding="utf-8", errors="surrogateescape") as fh:
            fh.write(new)
        srcs.append(rel)

    # Assemble every copy with the GBA assembler (cwd = GBA tree, so .include
    # and .incbin resolve exactly as in the GBA build).
    out_abs = os.path.abspath(a.out)
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        jobs = [ex.submit(assemble, a.gba_dir, os.path.join(out_abs, rel),
                          os.path.join(out_abs, "obj", rel[:-2] + ".o")) for rel in srcs]
        for j in jobs:
            src, rc, err = j.result()
            if rc != 0:
                failed += 1
                sys.stderr.write(f"fixptr: assembling {src} failed:\n{err}\n")
    if failed:
        return 1

    with open(os.path.join(a.out, "converted.txt"), "w") as fh:
        fh.write("# kind\tsource\tROM site\tvalue\tsymbol\n")
        for kind, where, site, v, txt in conv:
            fh.write(f"{kind}\t{where}\t{'0x%08X' % site if site else '-'}\t0x{v:08X}\t{txt}\n")
    write_unres(os.path.join(a.out, "convert_unresolved.tsv"), unres)
    c = Counter(k for k, *_ in conv)
    u = Counter(k for k, *_ in unres)
    print(f"fixptr convert: {len(files)} files, {sum(c.values())} literal addresses converted "
          f"({c['code']} code, {c['data']} data), {sum(u.values())} not converted "
          f"({', '.join(f'{k} {n}' for k, n in sorted(u.items()))}); {len(srcs)} copies assembled")
    return 0


def write_unres(path, rows):
    with open(path, "w") as fh:
        for kind, where, site, v, txt in rows:
            fh.write(f"{kind}\t{where}\t{'0x%08X' % site if site else '-'}\t"
                     f"{'0x%08X' % v if v is not None else '-'}\t{txt}\n")


def read_unres(path):
    rows = []
    with open(path) as fh:
        for line in fh:
            kind, where, site, v, txt = line.rstrip("\n").split("\t", 4)
            rows.append((kind, where, None if site == "-" else int(site, 16),
                         None if v == "-" else int(v, 16), txt))
    return rows


# ---------------------------------------------------------------------------
# relocs

def parse_map_placements(map_path):
    """{(obj, section): (addr, size)} for allocated input sections in the memory map."""
    out = {}
    started = False
    pending = None
    one = re.compile(r"^ (\S+)\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S+)\s*$")
    cont = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S+)\s*$")
    name_only = re.compile(r"^ (\.\S+)\s*$")
    with open(map_path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            if not started:
                started = line.startswith("Linker script and memory map")
                continue
            m = one.match(line)
            if m:
                sec, addr, size, obj = m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4)
                pending = None
            else:
                m = cont.match(line) if pending else None
                if m:
                    sec, (addr, size, obj) = pending, (int(m.group(1), 16), int(m.group(2), 16), m.group(3))
                    pending = None
                else:
                    m = name_only.match(line)
                    pending = m.group(1) if m else None
                    continue
            if size and (in_rom(addr) or in_ram(addr)) and obj.endswith(".o"):
                out.setdefault((obj, sec), (addr, size))
    return out


def host_defined(objs):
    names = set()
    for o in objs:
        for s in Elf32(o).symbols:
            if s.is_global and s.shndx != SHN_UNDEF and s.name:
                names.add(s.name)
    return names


def cmd_relocs(a):
    syms = GbaSymbols(a.elf)
    with open(a.rom, "rb") as fh:
        rom = fh.read()
    placements = parse_map_placements(a.map)
    hostdef = host_defined(a.host_objs)
    hostbuilt = {o[o.rindex("/src/") + 1:] for o in a.host_objs if "/src/" in o}
    by_obj = defaultdict(dict)
    for (obj, sec), pl in placements.items():
        by_obj[obj][sec] = pl

    unres = read_unres(os.path.join(a.fixptr_dir, "convert_unresolved.tsv"))
    conv_sites = set()
    with open(os.path.join(a.fixptr_dir, "converted.txt")) as fh:
        for line in fh:
            if not line.startswith("#"):
                site = line.split("\t")[2]
                if site != "-":
                    conv_sites.add(int(site, 16))

    entries = {}            # site -> (gba value, host symbol, addend, kind)
    stats = Counter()
    seen_conv = set()

    def host_name_for(cands):
        """First candidate the host defines, else None."""
        for s in cands:
            if s.is_global and s.name in hostdef:
                return s.name
        return None

    def objpath(obj):
        if obj.startswith("data/") and obj.count("/") == 1:
            return os.path.join(a.fixptr_dir, "obj", obj)
        return os.path.join(a.gba_dir, obj)

    for obj in sorted(by_obj):
        if obj.startswith("asm/") or obj.startswith("/") or "(" in obj:
            continue        # hand-written code, or toolchain library members
        is_c = obj.startswith("src/")
        path = objpath(obj)
        if not os.path.exists(path):
            stats["object-missing"] += 1
            continue
        elf = Elf32(path)
        for sidx, off, symidx, rtype in elf.rels():
            tsec = elf.sections[sidx]
            secname = tsec["name"]
            if not tsec["flags"] & SHF_ALLOC or secname.startswith((".debug", ".ARM")):
                continue
            if is_c and secname.startswith(".text"):
                continue    # literal pools: only GBA code reads them
            pl = by_obj[obj].get(secname)
            if pl is None or not in_rom(pl[0]):
                continue
            site = pl[0] + off
            where = f"{obj}({secname})+0x{off:X}"
            if rtype in (0, 40):
                continue    # R_ARM_NONE, R_ARM_V4BX: markers, no data word
            if rtype != R_ARM_ABS32:
                unres.append(("reloc-type", where, site, None, f"relocation type {rtype} in ROM data"))
                continue
            addend = struct.unpack_from("<I", elf.d, tsec["off"] + off)[0]
            sym = elf.symbols[symidx]
            if sym.shndx == SHN_UNDEF or sym.is_global:
                g = syms.globals.get(sym.name)
                if g is None:
                    unres.append(("unknown-symbol", where, site, None, f"{sym.name} is not in the GBA ELF"))
                    continue
                svalue = g.value
                tsym = g
            else:
                spl = by_obj[obj].get(elf.sections[sym.shndx]["name"]) if sym.shndx < len(elf.sections) else None
                if spl is None:
                    unres.append(("local-symbol", where, site, None,
                                  f"cannot place local symbol {sym.name or '(section)'}"))
                    continue
                svalue = spl[0] + (sym.value if sym.type != STT_SECTION else 0)
                tsym = None
            value = (svalue + addend) & 0xFFFFFFFF
            romword = struct.unpack_from("<I", rom, site - ROM_BASE)[0]
            if romword != value:
                unres.append(("rom-mismatch", where, site, value,
                              f"relocated value differs from the ROM word 0x{romword:08X}"))
                continue
            stats["relocs"] += 1
            if site in conv_sites:
                seen_conv.add(site)

            # Code target? (exact function start, Thumb bit kept, or without it)
            fc = syms.funcs.get(value) or (syms.funcs.get(value | 1) if not value & 1 else None)
            if fc:
                name = host_name_for(fc)
                if name is None:
                    g = [s for s in fc if s.is_global]
                    if not g:
                        if obj in hostbuilt:
                            # ROM copy of data the host compiles itself; the host
                            # uses its own copy, which points at its own function.
                            unres.append(("host-copy-static", where, site, value,
                                          f"{syms.pick(fc).name} (static) in the ROM copy of {obj}, "
                                          f"which the host builds itself"))
                        else:
                            unres.append(("static-function", where, site, value,
                                          f"{syms.pick(fc).name} is a file-local (static) function in "
                                          f"{obj}, which the host does not build"))
                        continue
                    name = syms.pick(g).name
                if not C_IDENT.match(name) or name in C_KEYWORDS:
                    unres.append(("bad-name", where, site, value, f"{name} is not a C identifier"))
                    continue
                entries[site] = (value, name, 0, "code")
                stats["code"] += 1
                continue
            if tsym is not None and tsym.type == STT_FUNC:
                unres.append(("inside-function", where, site, value,
                              f"points inside function {tsym.name} (+0x{addend:X})"))
                continue
            # Data target: patch only when the host builds that data itself.
            cands = []
            if tsym is not None:
                cands = [tsym] + [s for s in syms.data.get(tsym.value, []) if s is not tsym]
                name = host_name_for(cands)
                add = addend if addend < 0x80000000 else addend - (1 << 32)
                if name is None:
                    name, add = host_name_for(syms.data.get(value, [])), 0
            else:
                name, add = host_name_for(syms.data.get(value, [])), 0
            if name is None:
                stats["identity"] += 1      # host binds it to its GBA address: no change
                continue
            if not C_IDENT.match(name) or name in C_KEYWORDS:
                unres.append(("bad-name", where, site, value, f"{name} is not a C identifier"))
                continue
            entries[site] = (value, name, add, "data")
            stats["data"] += 1

    missing_conv = conv_sites - seen_conv
    for site in sorted(missing_conv):
        unres.append(("lost-conversion", "-", site, None, "converted word produced no relocation in ROM"))

    # C table for the host.
    names = sorted({e[1] for e in entries.values()})
    c = ["/* Generated by port/tools/fixptr.py relocs. Do not edit. */",
         "/* ROM words the host rewrites at startup (port/hw/romreloc.c). */",
         "struct PortRomReloc { unsigned site; unsigned gbaValue; const void *target; int addend; };", ""]
    c += [f"extern char {n}[];" for n in names]
    c += ["", "const struct PortRomReloc gPortRomRelocs[] = {"]
    for site in sorted(entries):
        value, name, add, kind = entries[site]
        c.append(f"    {{ 0x{site:08X}u, 0x{value:08X}u, {name}, {add} }}, /* {kind} */")
    if not entries:
        c.append("    { 0u, 0u, 0, 0 },")
    c += ["};", f"const unsigned gPortRomRelocCount = {len(entries)}u;", ""]
    with open(a.out_c, "w") as fh:
        fh.write("\n".join(c))

    write_report(a, unres, stats, entries)
    print(f"fixptr relocs: {stats['relocs']} ROM-data relocations checked against the ROM; "
          f"{len(entries)} in the host table ({stats['code']} code, {stats['data']} host-built data), "
          f"{stats['identity']} need no change; {len([u for u in unres if u[0] in HARMFUL])} unresolved "
          f"code-like, {len(unres)} report lines")
    return 0


# Reasons that can leave a GBA code pointer (or a wrong pointer) in host-visible ROM data.
HARMFUL = {"static-function", "inside-function", "reloc-type", "unknown-symbol", "local-symbol",
           "rom-mismatch", "lost-conversion", "bad-name", "incbin-form"}
# Benign categories that are short enough, or worth reviewing, to list in full.
FULL_LIST = {"excluded-file", "blob-region-base", "static-data", "blob-static-data",
             "blob-static-function", "blob-unlikely-code", "blob-unlikely-data", "host-copy-static"}

REASON_TEXT = {
    "static-function": "Target is a file-local (static) function in an object the host does not build. "
                       "Left as the GBA value: calling it from the host crashes.",
    "host-copy-static": "Target is a static function, but the word sits in the ROM copy of data that "
                        "the host compiles itself. The host uses its own copy, so this is harmless "
                        "unless host code reaches the ROM copy through a literal address.",
    "inside-function": "A relocation points inside a function body, not at its start. Left as is.",
    "blob-inside-function": "Word in a baserom blob that falls inside a function body. Almost all are "
                            "graphics or compressed data that look like a code address. Left as is.",
    "blob-inside-data": "Word in a baserom blob that falls inside a sized object, not at its start. "
                        "Blobs only accept exact symbol starts. Left as is.",
    "blob-unlikely-code": "Word in a baserom blob equal to a function start, rejected because its "
                          "neighbours look like pixels or compressed data, not a struct. Review these "
                          "by hand: a real one would crash when called.",
    "blob-unlikely-data": "Word in a baserom blob equal to a data symbol, rejected for the same reason.",
    "blob-static-data": "Word in a baserom blob equal to file-local data. Left as is.",
    "blob-static-function": "Word in a baserom blob equal to a static function. Left as is.",
    "static-data": "Value matches file-local data. Left as is: that ROM data stays at its GBA address.",
    "blob-no-symbol-rom": "Word in a baserom blob that is a ROM address with no symbol at it. Left as "
                          "is: under D1-a ROM data stays at its GBA address, so a real data pointer "
                          "is already right; most are not pointers at all.",
    "blob-no-symbol-ram": "Word in a baserom blob that is a RAM address with no symbol. Left as is "
                          "(RAM is at its GBA address).",
    "blob-region-base": "Exactly a memory region base (0x02000000, 0x03000000, 0x08000000). Treated "
                        "as a constant.",
    "excluded-file": "File deliberately not converted.",
    "incbin-form": "baserom incbin the converter could not parse.",
    "reloc-type": "Relocation type other than R_ARM_ABS32 in ROM data.",
    "unknown-symbol": "Relocation symbol not in the GBA ELF.",
    "local-symbol": "Local relocation symbol whose section is not in the map.",
    "rom-mismatch": "Relocated value differs from the ROM word.",
    "lost-conversion": "A converted word produced no relocation.",
    "bad-name": "Symbol name is not a valid C identifier.",
}


def write_report(a, unres, stats, entries):
    by = defaultdict(list)
    for row in unres:
        by[row[0]].append(row)
    order = sorted(by, key=lambda k: (k not in HARMFUL, k))
    lines = ["# fixptr unresolved addresses",
             "# Generated by port/tools/fixptr.py (convert + relocs). Columns: source, ROM site, value, detail.",
             f"# Host relocation table: {len(entries)} entries ({stats['code']} code, {stats['data']} data); "
             f"{stats['identity']} relocations need no change on the host.",
             "# Summary:"]
    for k in order:
        note = ("(can leave a bad pointer)" if k in HARMFUL else
                "(review by hand)" if k == "blob-unlikely-code" else "(benign under D1-a)")
        lines.append(f"#   {k:22s} {len(by[k]):6d}  {note}")
    for k in order:
        lines += ["", f"## {k}: {len(by[k])}", f"# {REASON_TEXT.get(k, '')}"]
        rows = by[k]
        limit = None if k in HARMFUL or k in FULL_LIST else a.benign_limit
        for kind, where, site, v, txt in rows[:limit]:
            lines.append(f"{where}\t{'0x%08X' % site if site else '-'}\t"
                         f"{'0x%08X' % v if v is not None else '-'}\t{txt}")
        if limit is not None and len(rows) > limit:
            lines.append(f"# ... {len(rows) - limit} more (full list: {a.fixptr_dir}/convert_unresolved.tsv)")
    with open(a.report, "w") as fh:
        fh.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("convert")
    c.add_argument("--elf", required=True, help="GBA fireemblem8.elf (address oracle)")
    c.add_argument("--rom", required=True, help="baserom.gba")
    c.add_argument("--gba-dir", required=True, help="GBA tree the ROM is built from (read only)")
    c.add_argument("--out", required=True, help="PC build directory for the converted copies")
    r = sub.add_parser("relocs")
    r.add_argument("--elf", required=True)
    r.add_argument("--map", required=True)
    r.add_argument("--rom", required=True)
    r.add_argument("--gba-dir", required=True)
    r.add_argument("--fixptr-dir", required=True, help="the --out directory of convert")
    r.add_argument("--host-objs", nargs="+", required=True)
    r.add_argument("--out-c", required=True)
    r.add_argument("--report", default="port/tools/fixptr_unresolved.txt")
    r.add_argument("--benign-limit", type=int, default=200,
                   help="report lines per benign category (full list stays in the build dir)")
    a = ap.parse_args()
    if os.path.abspath(getattr(a, "out", "/nonexistent")).startswith(os.path.abspath(a.gba_dir) + "/data"):
        sys.exit("fixptr: refusing to write into the GBA data tree")
    return cmd_convert(a) if a.cmd == "convert" else cmd_relocs(a)


if __name__ == "__main__":
    sys.exit(main())
