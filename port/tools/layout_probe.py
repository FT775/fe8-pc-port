#!/usr/bin/env python3
"""Compare struct and union layouts between agbcc (GBA) and the host compiler (PC).

Run from the repository root, after the GBA `make` and a FULL=1 host build:
    python3 port/tools/layout_probe.py [--jobs 8] [--out build/layout] [src/x.c ...]

For every shared C file the host build compiles (default: every src/*.c with an
object in build/pc-full/src) the tool
 1. compiles the file with the host flags and -fno-eliminate-unused-debug-types
    and lists every struct and union the file can see, with its members, from
    the DWARF;
 2. writes a probe file that #includes the source file and adds, per type:
    a constant array of sizeof, __alignof__ and offsetof of every non-bitfield
    member; for every bitfield, a constant object of the type with only that
    field set to all ones (its bit position is read from the bytes); and one
    object of the type (so both compilers emit its DWARF);
 3. compiles the probe with agbcc (the GBA pipeline: cpp, iconv, agbcc -g, as)
    and with the host compiler (the port flags); a type agbcc cannot see in that
    file (host-only headers such as port.h and libc) fails there and is dropped;
 4. compares, type by type: size, alignment, every member offset and every
    bitfield position. Anonymous struct and union members are part of their
    parent. Member sizes come from each compiler's DWARF.
The layout numbers come from the compilers' constant output, not from DWARF:
agbcc's DWARF gives wrong member offsets for packed structs (BlendCnt._unused
is at 2, its DWARF says 4). DWARF is used for the list of types and members,
member sizes, and the few types the probe cannot name (block-scope types).

Output: <out>/report.txt (every mismatch, grouped by type) and <out>/layouts.json.
Exit status 1 if anything differs. The GBA tree is only read.
"""
import argparse
import collections
import concurrent.futures
import json
import os
import re
import subprocess
import sys

# port/Makefile's HOST_CFLAGS, without -fdata-sections (section names only),
# plus the DWARF options the probe reads. Keep the two in step: B-15's
# -fno-aggressive-loop-optimizations changes code, not layouts, but the probe
# compiles the files as the port does.
HOST_CFLAGS = ("-m32 -std=gnu89 -O1 -g -gdwarf-4 -fno-eliminate-unused-debug-types -fno-pie "
               "-funsigned-char -fno-toplevel-reorder -fno-common -fno-strict-aliasing -fwrapv "
               "-fpermissive -malign-data=abi -fno-delete-null-pointer-checks "
               "-fno-aggressive-loop-optimizations -w").split()
HOST_CPPFLAGS = ("-DPC_PORT=1 -DNONMATCHING=1 -Iinclude -Iport/include -include port/include/port.h "
                 "-iquote .").split()
GBA_CPPFLAGS = "-I tools/agbcc/include -iquote include -iquote . -nostdinc -undef".split()
GBA_CC1FLAGS = "-mthumb-interwork -Wimplicit -Wparentheses -O2 -fhex-asm -ffix-debug-line -g".split()
# The per-object flags of port/Makefile (m4a.c: the swi 0x2A in
# MusicPlayerJumpTableCopy, built since phase 3).
PER_FILE_HOST = {
    "src/hardware.c": ["-Dasm(x)=((void)0)"],
    "src/m4a.c": ["-Dasm(x)=((void)0)"],
    "src/bmdifficulty.c": ["-DPORT_CONST_DATA_DEFAULT_SECTION"],
}
# B-6: the host compiles a sed-made copy of these (port/Makefile LVCAST_SRCS).
LVCAST = {f"src/{n}.c": f"build/pc-full/lvcast/{n}.c"
          for n in ("eventcall", "eventfx-earthquake", "banim-efxmagic-shine",
                    "banim-efxmagic-crimsoneye")}


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, errors="replace", **kw)


# ---------------------------------------------------------------- DWARF reading

DIE_RE = re.compile(r"^\s*<(\d+)><([0-9a-f]+)>: Abbrev Number: (\d+)(?: \((\w+)\))?")
ATTR_RE = re.compile(r"^\s*<[0-9a-f]+>\s+(DW_AT_\w+)\s*:\s?(.*)$")
STR_RE = re.compile(r"^\((?:indirect|indexed|alt indirect)[^)]*\):\s*(.*)$")


class Die:
    __slots__ = ("off", "level", "tag", "attrs", "children", "parent")

    def __init__(self, off, level, tag, parent):
        self.off, self.level, self.tag, self.parent = off, level, tag, parent
        self.attrs, self.children = {}, []

    def name(self):
        v = self.attrs.get("DW_AT_name")
        if v is None:
            return None
        m = STR_RE.match(v)
        return (m.group(1) if m else v).strip()

    def num(self, attr):
        v = self.attrs.get(attr)
        if v is None:
            return None
        v = v.strip()
        m = re.search(r"DW_OP_plus_uconst: (\d+)", v)
        if m:
            return int(m.group(1))
        m = re.match(r"^(-?(?:0x[0-9a-f]+|\d+))", v)
        return int(m.group(1), 0) if m else None

    def ref(self):
        v = self.attrs.get("DW_AT_type")
        if v is None:
            return None
        m = re.search(r"<0x([0-9a-f]+)>", v)
        return int(m.group(1), 16) if m else None


def read_dwarf(obj):
    p = run(["readelf", "--debug-dump=info", obj])
    dies, stack, cur = {}, [], None
    for line in p.stdout.splitlines():
        m = DIE_RE.match(line)
        if m:
            level, off, abbrev, tag = int(m.group(1)), int(m.group(2), 16), int(m.group(3)), m.group(4)
            del stack[level:]
            if abbrev == 0:
                cur = None
                continue
            parent = stack[-1] if stack else None
            cur = Die(off, level, tag, parent)
            dies[off] = cur
            if parent is not None:
                parent.children.append(cur)
            stack.append(cur)
            continue
        if cur is not None:
            m = ATTR_RE.match(line)
            if m:
                cur.attrs[m.group(1)] = m.group(2)
    return dies


CV_TAGS = ("DW_TAG_const_type", "DW_TAG_volatile_type")
AGG_TAGS = {"DW_TAG_structure_type", "DW_TAG_union_type"}


def strip(dies, d, typedefs=True):
    tags = CV_TAGS + (("DW_TAG_typedef",) if typedefs else ())
    for _ in range(50):
        if d is None or d.tag not in tags:
            break
        r = d.ref()
        d = dies.get(r) if r is not None else None
    return d


def type_size(dies, d):
    d = strip(dies, d)
    if d is None:
        return None
    if d.tag == "DW_TAG_array_type":
        n = 1
        for c in d.children:
            if c.tag == "DW_TAG_subrange_type":
                ub, cnt = c.num("DW_AT_upper_bound"), c.num("DW_AT_count")
                if cnt is None:
                    cnt = ub + 1 if ub is not None and ub >= 0 else 0
                n *= cnt
        es = type_size(dies, dies.get(d.ref()))
        return None if es is None else es * n
    bs = d.num("DW_AT_byte_size")
    if bs is not None:
        return bs
    return 4 if d.tag == "DW_TAG_pointer_type" else None


def is_anon_agg(d):
    return d is not None and d.tag in AGG_TAGS and d.name() is None


def flatten(dies, agg, prefix="", base=0, out=None):
    """Members of agg as [path, byte offset, size, bit position or None, bit size or None]."""
    if out is None:
        out = []
    for m in agg.children:
        if m.tag != "DW_TAG_member":
            continue
        name = m.name()
        path = prefix + (name if name else "<anon>")
        loc = m.num("DW_AT_data_member_location") or 0
        mt = dies.get(m.ref())
        bsz = m.num("DW_AT_bit_size")
        if bsz is not None:
            dbo = m.num("DW_AT_data_bit_offset")
            if dbo is not None:
                pos = dbo
            else:
                unit = m.num("DW_AT_byte_size") or type_size(dies, mt) or 4
                pos = loc * 8 + unit * 8 - m.num("DW_AT_bit_offset") - bsz
            out.append([path, None, None, base * 8 + pos, bsz])
            continue
        out.append([path, base + loc, type_size(dies, mt), None, None])
        inner = strip(dies, mt, typedefs=False)
        sub = prefix + (name + "." if name else "")
        if inner is not None and inner.tag == "DW_TAG_array_type":
            el = strip(dies, dies.get(inner.ref()), typedefs=False)
            if is_anon_agg(el):
                flatten(dies, el, (prefix + name + "[0].") if name else sub, base + loc, out)
        elif is_anon_agg(inner):
            flatten(dies, inner, sub, base + loc, out)
    return out


def collect_types(dies):
    """Return {key: (die, C type expression or None)} for the complete aggregates in a CU.

    key = (scope, kind, name): name is the tag, 'typedef:T' for an anonymous
    aggregate named by a typedef, or 'var:v' for one used only by a variable.
    Anonymous aggregates used as members are compared inside their parent.
    """
    typedef_of, var_of = collections.defaultdict(list), collections.defaultdict(list)
    for d in dies.values():
        r = d.ref()
        if r is None:
            continue
        if d.tag == "DW_TAG_typedef" and d.name():
            typedef_of[r].append(d.name())
        elif d.tag == "DW_TAG_variable" and d.name():
            var_of[r].append(d.name())
            # An array of an anonymous struct: name the element type after the array.
            t = strip(dies, dies.get(r), typedefs=False)
            while t is not None and t.tag == "DW_TAG_array_type":
                el = strip(dies, dies.get(t.ref()), typedefs=False)
                if el is not None and el.tag != "DW_TAG_array_type":
                    var_of[el.off].append(d.name() + "[]")
                t = el
    out = {}
    for d in dies.values():
        if d.tag not in AGG_TAGS or "DW_AT_declaration" in d.attrs or d.num("DW_AT_byte_size") is None:
            continue
        kind = "struct" if d.tag == "DW_TAG_structure_type" else "union"
        scope, p = [], d.parent
        while p is not None and p.tag != "DW_TAG_compile_unit":
            if p.tag != "DW_TAG_lexical_block":  # agbcc and GCC nest blocks differently
                scope.append(p.name() or p.tag)
            p = p.parent
        scope = "/".join(reversed(scope))
        name, expr = d.name(), None
        if name:
            key_name = name
            expr = f"{kind} {name}" if not scope else None
        elif typedef_of.get(d.off):
            key_name = "typedef:" + sorted(typedef_of[d.off])[0]
            expr = key_name[8:] if not scope else None
        elif var_of.get(d.off):
            key_name = "var:" + sorted(var_of[d.off])[0]
        else:
            continue
        key = (scope, kind, key_name)
        k2 = 2
        while key in out:
            key = (scope, kind, f"{key_name}#{k2}")
            k2 += 1
        out[key] = (d, expr)
    return out


def dwarf_layouts(dies):
    res = {}
    for key, (d, expr) in collect_types(dies).items():
        res["|".join(key)] = {"size": d.num("DW_AT_byte_size"), "align": None,
                              "members": flatten(dies, d), "line": d.num("DW_AT_decl_line")}
    return res


# ---------------------------------------------------------------- probe files

def c_path(path):
    """Member path for offsetof: anonymous members are reached by their fields' names."""
    return path.replace("<anon>.", "")


def designator(path):
    parts = [p for p in path.split(".") if p != "<anon>"]
    s = "-1"
    for p in reversed(parts):
        m = re.match(r"^(\w+)\[0\]$", p)
        s = f"{{ .{m.group(1)} = {{ [0] = {s} }} }}" if m else f"{{ .{p} = {s} }}"
    return s


def make_probe(include, types, dies, probe_path, drop=()):
    """Write the probe C file; return ([(i, key, offset labels, bitfields)], {line: i})."""
    lines = [f'#include "{include}"', "",
             "#define PROBE_OFF(T, m) ((unsigned int)&((T *)0)->m)", ""]
    index, line_of = [], {}
    for i, (key, (d, expr)) in enumerate(sorted(types.items())):
        if expr is None or i in drop:
            continue
        members = flatten(dies, d)
        labels = ["sizeof", "alignof"]
        items = [f"sizeof({expr})", f"__alignof__({expr})"]
        bitfields = []
        for m in members:
            if m[3] is None:
                labels.append(m[0])
                items.append(f"PROBE_OFF({expr}, {c_path(m[0])})")
            else:
                bitfields.append(m[0])
        index.append((i, "|".join(key), labels, bitfields))
        lines.append(f"const unsigned int probe_layout_{i}[] = {{ {', '.join(items)} }};")
        line_of[len(lines)] = i
        for j, bf in enumerate(bitfields):
            lines.append(f"const {expr} probe_bf_{i}_{j} = {designator(bf)};")
            line_of[len(lines)] = i
        lines.append(f"{expr} probe_obj_{i};")
        line_of[len(lines)] = i
    with open(probe_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    return index, line_of


SIZES = {
    "gba": {"byte": 1, "short": 2, "hword": 2, "2byte": 2, "word": 4, "long": 4, "int": 4, "4byte": 4},
    "host": {"byte": 1, "value": 2, "short": 2, "2byte": 2, "word": 2, "long": 4, "int": 4, "4byte": 4},
}
LABEL_RE = re.compile(r"^_?(probe_(?:layout|bf)_[\d_]+):")
DATA_RE = re.compile(r"^\.(\w+)\s+(.*)$")


def read_probe_data(asm_path, side):
    """Return {label: bytes} for every probe_layout_* and probe_bf_* object in an assembly file."""
    out, cur = {}, None
    with open(asm_path, encoding="latin-1") as f:
        for line in f:
            s = line.strip()
            m = LABEL_RE.match(s)
            if m:
                cur = out.setdefault(m.group(1), bytearray())
                continue
            if cur is None:
                continue
            m = DATA_RE.match(s)
            if not m:
                cur = None
                continue
            d, arg = m.group(1), m.group(2).split("@")[0].split("#")[0].strip()
            if d in ("zero", "space"):
                cur.extend(b"\0" * int(arg.split(",")[0], 0))
            elif d in SIZES[side]:
                n = SIZES[side][d]
                for a in arg.split(","):
                    cur.extend((int(a.strip(), 0) & ((1 << (8 * n)) - 1)).to_bytes(n, "little"))
            elif d in ("align", "p2align", "balign"):
                continue
            else:
                cur = None
    return out


ERRLINE_RE = re.compile(r"^:(\d+): ")


def agbcc_compile(probe, gs):
    # The GBA Makefile's $(CPP) is make's default, "cc -E" (CPP ?= does not override it).
    cpp = subprocess.Popen(["cc", "-E"] + GBA_CPPFLAGS + [probe], stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE)
    ic = subprocess.Popen(["iconv", "-f", "UTF-8", "-t", "CP932"], stdin=cpp.stdout, stdout=subprocess.PIPE)
    cpp.stdout.close()
    cc = subprocess.run(["tools/agbcc/bin/agbcc"] + GBA_CC1FLAGS + ["-o", gs], stdin=ic.stdout,
                        capture_output=True, text=True, errors="replace")
    ic.wait()
    cpp.wait()
    err = cc.stderr + cpp.stderr.read().decode(errors="replace")
    return (cc.returncode or cpp.returncode), err


def probe_layouts(data, index, dwarf):
    """Layouts from the constant output: {key: {size, align, members}}."""
    res, errs = {}, []
    for i, key, labels, bitfields in index:
        raw = data.get(f"probe_layout_{i}")
        if raw is None or len(raw) != 4 * len(labels):
            errs.append(f"probe array {i} ({key}) missing or short")
            continue
        vals = [int.from_bytes(raw[k:k + 4], "little") for k in range(0, len(raw), 4)]
        dw = dwarf.get(key, {"members": []})
        dsize = {m[0]: m[2] for m in dw["members"]}
        members = [[lab, v, dsize.get(lab), None, None] for lab, v in zip(labels[2:], vals[2:])]
        for j, bf in enumerate(bitfields):
            img = data.get(f"probe_bf_{i}_{j}")
            if img is None:
                errs.append(f"bitfield image {key}.{bf} missing")
                continue
            bits = [b for b in range(len(img) * 8) if img[b // 8] >> (b % 8) & 1]
            if not bits or bits != list(range(bits[0], bits[-1] + 1)):
                errs.append(f"bitfield image {key}.{bf} not one run: {bits[:4]}..")
                continue
            members.append([bf, None, None, bits[0], len(bits)])
        res[key] = {"size": vals[0], "align": vals[1], "members": members, "line": dw.get("line")}
    return res, errs


def process(src, out_dir):
    name = os.path.basename(src)[:-2]
    tdir, pdir = os.path.join(out_dir, "tu"), os.path.join(out_dir, "probe")
    os.makedirs(tdir, exist_ok=True)
    os.makedirs(pdir, exist_ok=True)
    host_src = LVCAST.get(src, src)
    extra = PER_FILE_HOST.get(src, []) + (["-iquote", "src"] if src in LVCAST else [])
    res = {"src": src, "errors": [], "notes": []}

    # 1. Host DWARF of the unchanged file: every type it can see.
    enum_obj = os.path.join(tdir, name + ".host.o")
    p = run(["gcc"] + HOST_CFLAGS + HOST_CPPFLAGS + extra + ["-c", host_src, "-o", enum_obj])
    if p.returncode:
        res["errors"].append("host enumerate: " + p.stderr[-2000:])
        return res
    hdies = read_dwarf(enum_obj)
    htypes = collect_types(hdies)

    # 2-3a. Probe file and agbcc (drop the types agbcc cannot see here).
    gba_probe, host_probe = os.path.join(pdir, name + ".gba.c"), os.path.join(pdir, name + ".host.c")
    gs, go = os.path.join(pdir, name + ".gba.s"), os.path.join(pdir, name + ".gba.o")
    drop = set()
    for _ in range(10):
        index, line_of = make_probe(src, htypes, hdies, gba_probe, drop)
        rc, err = agbcc_compile(gba_probe, gs)
        if not rc:
            break
        bad = set()
        for el in err.splitlines():
            if el.startswith(gba_probe + ":") and "warning" not in el:
                m = ERRLINE_RE.match(el[len(gba_probe):])
                if m and int(m.group(1)) in line_of:
                    bad.add(line_of[int(m.group(1))])
        if not bad:
            res["errors"].append("agbcc: " + err[-3000:])
            return res
        drop |= bad
    else:
        res["errors"].append("agbcc: too many retries")
        return res
    make_probe(host_src, htypes, hdies, host_probe, drop)
    res["dropped"] = sorted("|".join(k) for i, k in enumerate(sorted(htypes)) if i in drop)
    with open(gs, "a") as f:
        f.write("\n.section .debug_abbrev\n.byte 0\n")  # agbcc leaves the table unterminated
    p = run(["arm-none-eabi-as", "-mcpu=arm7tdmi", "-mthumb-interwork", "-I", "include", gs, "-o", go])
    if p.returncode:
        res["errors"].append("gba as: " + p.stderr[-2000:])
        return res

    # 3b. Host.
    hs, ho = os.path.join(pdir, name + ".host.s"), os.path.join(pdir, name + ".host.o")
    p = run(["gcc"] + HOST_CFLAGS + HOST_CPPFLAGS + extra + ["-S", host_probe, "-o", hs])
    if p.returncode:
        res["errors"].append("host probe: " + p.stderr[-2000:])
        return res
    p = run(["as", "--32", hs, "-o", ho])
    if p.returncode:
        res["errors"].append("host as: " + p.stderr[-2000:])
        return res

    # 4. Layouts: probe constants first, DWARF for the types the probe cannot name.
    for side, s_path, o_path in (("gba", gs, go), ("host", hs, ho)):
        dwarf = dwarf_layouts(read_dwarf(o_path))
        lay, errs = probe_layouts(read_probe_data(s_path, side), index, dwarf)
        res["errors"] += [f"{side}: {e}" for e in errs]
        for key, t in dwarf.items():
            scope, _, tname = key.split("|")
            # Types the probe cannot name: block scope, or only a variable's type.
            if key not in lay and "#" not in key and (scope or tname.startswith("var:")):
                lay[key] = dict(t, source="dwarf")
        # Cross-check (information): probe offsets against the same compiler's DWARF.
        for key, t in lay.items():
            dw = dwarf.get(key)
            if dw is None or t.get("source") == "dwarf":
                continue
            dmem = {m[0]: m for m in dw["members"]}
            for m in t["members"]:
                d = dmem.get(m[0])
                if d is not None and (d[1], d[3]) != (m[1], m[3]):
                    res["notes"].append(f"{side} DWARF {key}.{m[0]}: DWARF {d[1] if d[3] is None else d[3]}, "
                                        f"probe {m[1] if m[3] is None else m[3]}")
        res[side] = lay
    return res


# ---------------------------------------------------------------- comparison

def fmt_member(m):
    if m is None:
        return "-"
    if m[3] is not None:
        return f"bits {m[3]}..{m[3] + m[4] - 1}"
    return f"+0x{m[1]:X} size {m[2]}"


def compare(results):
    by_type, stats = collections.OrderedDict(), collections.Counter()
    for r in results:
        if "gba" not in r:
            continue
        gba, host = r["gba"], r["host"]
        for key in sorted(set(gba) | set(host)):
            g, h = gba.get(key), host.get(key)
            if g is None or h is None:
                stats["one_side_" + ("host" if g is None else "gba")] += 1
                continue
            stats["compared"] += 1
            sig = json.dumps([g["size"], g["align"], g["members"], h["size"], h["align"], h["members"]])
            ent = by_type.setdefault(key, {})
            ent.setdefault(sig, {"files": [], "g": g, "h": h})["files"].append(r["src"])
    mismatches = []
    for key, sigs in by_type.items():
        for e in sigs.values():
            g, h = e["g"], e["h"]
            hmap = {m[0]: m for m in h["members"]}
            gnames = {m[0] for m in g["members"]}
            detail = []
            if g["size"] != h["size"]:
                detail.append(f"size GBA {g['size']} PC {h['size']}")
            if g["align"] != h["align"]:
                detail.append(f"align GBA {g['align']} PC {h['align']}")
            for m in g["members"]:
                hm = hmap.get(m[0])
                if hm is None or (hm[1], hm[3], hm[4]) != (m[1], m[3], m[4]) or \
                        (m[2] is not None and hm[2] is not None and hm[2] != m[2]):
                    detail.append(f"{m[0]}: GBA {fmt_member(m)} PC {fmt_member(hm)}")
            for m in h["members"]:
                if m[0] not in gnames:
                    detail.append(f"{m[0]}: GBA - PC {fmt_member(m)}")
            if detail:
                mismatches.append({"key": key, "files": e["files"], "line": g.get("line"),
                                   "gba_size": g["size"], "host_size": h["size"],
                                   "gba_align": g["align"], "host_align": h["align"],
                                   "source": g.get("source", "probe"), "detail": detail})
    stats["types"] = len(by_type)
    return mismatches, stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--out", default="build/layout")
    ap.add_argument("srcs", nargs="*")
    args = ap.parse_args()
    srcs = args.srcs or ["src/" + f[:-2] + ".c" for f in sorted(os.listdir("build/pc-full/src"))
                         if f.endswith(".o")]
    os.makedirs(args.out, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        results = list(ex.map(lambda s: process(s, args.out), srcs))
    errors = [(r["src"], e) for r in results for e in r.get("errors", [])]
    notes = collections.Counter(n for r in results for n in r.get("notes", []))
    mismatches, stats = compare(results)
    with open(os.path.join(args.out, "layouts.json"), "w") as f:
        json.dump({"results": results, "mismatches": mismatches}, f)
    nm_types = len({m["key"] for m in mismatches})
    lines = [f"layout_probe: {len(srcs)} files, {len(errors)} errors, {stats['types']} distinct types "
             f"(scope|kind|name) in {stats['compared']} file-type pairs; {stats['one_side_gba']} only on the "
             f"GBA side, {stats['one_side_host']} only on the host side; {nm_types} types differ "
             f"({len(mismatches)} variants)"]
    lines += [f"ERROR {s}: {e.strip()}" for s, e in errors]
    lines += [f"NOTE ({c} files) {n}" for n, c in sorted(notes.items())]
    for m in sorted(mismatches, key=lambda m: m["key"]):
        files = m["files"]
        fl = ", ".join(files[:4]) + (f" (+{len(files) - 4} more)" if len(files) > 4 else "")
        lines.append(f"MISMATCH {m['key']} [{m['source']}] (line {m['line']}; {len(files)} files: {fl})")
        lines += ["    " + d for d in m["detail"][:40]]
        if len(m["detail"]) > 40:
            lines.append(f"    ... {len(m['detail']) - 40} more")
    with open(os.path.join(args.out, "report.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print(lines[0])
    return 1 if errors or mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
