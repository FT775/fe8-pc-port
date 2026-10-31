#!/usr/bin/env python3
"""List every function declaration whose type differs from the function's definition, across
translation units, and say which of them the host ABI can make behave differently from the GBA.

The game's C files declare many functions themselves: block-scope `extern`s, file-scope
prototypes in .c files and headers, K&R `f()` declarations and implicit declarations (a call
with no declaration in sight, `int f()` in gnu89). C makes a call through a declaration whose
type differs from the definition undefined; agbcc and the host GCC each compile it their own
way, and the two ways can differ (port-plan DIVERGENCES, "Host-ABI values"; round 10's C10-6,
`SelectSummonPos`'s `extern int CanUnitCrossTerrain` for an `s8` function):

  agbcc (the GBA build, Thumb; checked on a test file and at every site listed): a function
    returning a char or short extends the value in r0 before it returns (lsl/asr or lsl/lsr),
    and a caller extends the value again as its own declaration says (an `int` declaration:
    r0 is taken whole); a char or short parameter is narrowed again by the callee on entry.
  host GCC (i386 -O1, every PC build): a function returning a char or short sets only AL or
    AX (`setg %al`, `sete %al` over a pointer ...), so the upper bytes of EAX hold whatever
    was there; the caller extends the value as its declaration says. A char or short
    parameter is reloaded from its stack slot by the callee (movzbl, movswl).

So the one kind that changes values is a declaration whose return type is wider than the
definition's (an `int` read of an `s8` function: the GBA sees the clean value, the host the
other bytes of EAX), and a value read from a `void` function (each side's leftover
register). A narrower or other-signed return, and any char/short parameter mismatch, give the
same value on both (both callers extend per the declaration; both callees narrow per the
definition); a parameter count, a struct or float, and an unprototyped call need a look.
Each listed site is judged by hand from both compilers' output (port/tools/decl_accept.txt)
and fixed under PC_PORT where it matters; each kind printed says "differs", "same" or
"look".

How it works: the host build's DWARF (every game and port object of a FULL build, compiled
with -g) holds, for each object, the definition of every function it defines and the
declaration it used for every external function it calls or takes the address of, with the
declared return and parameter types, whether it is prototyped, and the calls (DW_TAG_call_site)
that go through it.  The tool resolves every type through typedefs, const and volatile to
its ABI class (s8, u8, s16, u16, 32-bit word, pointer, struct, void ...), compares each
declaration with the one definition, and reports every difference:

  return   void / value, width, signedness, struct
  params   count, each parameter's class (prototyped declarations only), and an
           unprototyped declaration (K&R or implicit) of a function that has parameters:
           its calls pass default-promoted arguments, which the tool cannot see; listed so
           the calls are checked by hand
  data     external variables declared with another type (informational, not checked: the
           same bytes at the same address on both sides, B-12, read as each declaration says)

--lto also builds every FULL game and port object again with -flto (BUILD_DIR, default
build/declsweep, about half a minute), links them with LTO and checks that every name
-Wlto-type-mismatch reports is in the tool's lists (LTO does not report a difference of
signedness only, an unprototyped declaration of a void function or an argument count: a
two-file test, round 10, so the tool's list is the larger one).  It also lists the implicit
declarations (-Wimplicit-function-declaration; GCC's gnu89 build accepts them silently).

usage (from a worktree root, after a FULL build: make -f port/Makefile FULL=1, or any build
that compiles the same files with -g, such as port/test's build/pc-gate0):
  python3 port/tools/decl_sweep.py [--objs-dir build/pc-full] [--json out.json] [--md out.md]
        [--accept port/tools/decl_accept.txt] [--check] [--lto] [--build-dir build/declsweep]
        [--show all|open] [-j N]

--accept names the sites judged by hand (KEY VERDICT reason; the key is printed for each
site); --check exits 1 when a site is not in the list, when a `fixed` site still differs (its
PC_PORT declaration is missing), when a `benign` entry matches no site or a `fixed` entry no
declaration in use (stale), and with --lto when LTO reports a name the lists do not have or
the -flto build or link fails.  check-all's `declsweep` row runs --check --lto on the pc-full
build.  The DWARF scan sees the declarations each object uses (a declaration nothing calls or
takes the address of makes no code); the PC_PORT view only, so a fixed site is checked by its
absence, and the 8 FULL_EXCLUDE files the host does not build are not scanned.
"""
import argparse
import collections
import json
import os
import re
import subprocess
import sys
from multiprocessing import Pool

DIE_RE = re.compile(r'^\s*<(\d+)><([0-9a-f]+)>: Abbrev Number: (\d+)(?: \((DW_TAG_\w+)\))?')
ATTR_RE = re.compile(r'^\s*<[0-9a-f]+>\s+(DW_AT_\w+)\s*:\s*(.*)$')
REF_RE = re.compile(r'<0x([0-9a-f]+)>')
FILE_RE = re.compile(r'^\s*(\d+)\s+(\d+)\s+(?:\(indirect line string, offset: 0x[0-9a-f]+\): |\(line_strp\) \(offset: 0x[0-9a-f]+\): )?(.*)$')
DIR_RE = re.compile(r'^\s*(\d+)\s+(?:\(indirect line string, offset: 0x[0-9a-f]+\): |\(line_strp\) \(offset: 0x[0-9a-f]+\): )?(.*)$')

SKIP_OBJS = ('autostubs', 'romreloc')     # generated stubs and the relocation table

ATE = {1: 'addr', 2: 'b', 4: 'f', 5: 's', 6: 's', 7: 'u', 8: 'u', 0x10: 'u'}


class Die:
    __slots__ = ('off', 'level', 'tag', 'attrs', 'children', 'parent')

    def __init__(self, off, level, tag, parent):
        self.off, self.level, self.tag, self.parent = off, level, tag, parent
        self.attrs, self.children = {}, []

    def ref(self, a='DW_AT_type'):
        v = self.attrs.get(a)
        if v is None:
            return None
        m = REF_RE.search(v)
        return int(m.group(1), 16) if m else None

    def num(self, a):
        v = self.attrs.get(a)
        if v is None:
            return None
        v = re.sub(r'^\([a-z_0-9]+\)\s*', '', v).split()[0]
        try:
            return int(v, 0)
        except ValueError:
            return None

    def flag(self, a):
        return a in self.attrs

    def name(self):
        v = self.attrs.get('DW_AT_name')
        if v is None:
            return None
        if '): ' in v:
            return v.rsplit('): ', 1)[1].strip()
        return re.sub(r'^\([a-z_0-9]+\)\s*', '', v).strip()


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, errors='replace').stdout


def read_files(obj):
    """DW_AT_decl_file index -> path relative to the worktree (DWARF 5 line table)."""
    out = run(['readelf', '--debug-dump=rawline', obj])
    dirs, files, mode = {}, {}, None
    comp = None
    for line in out.splitlines():
        if 'The Directory Table' in line:
            mode = 'dir'
            continue
        if 'The File Name Table' in line:
            mode = 'file'
            continue
        if 'Line Number Statements' in line or line.startswith(' Offset:') and files:
            if files:
                break
        if mode == 'dir':
            m = DIR_RE.match(line)
            if m and not line.strip().startswith('Entry'):
                dirs[int(m.group(1))] = m.group(2).strip()
        elif mode == 'file':
            m = FILE_RE.match(line)
            if m and not line.strip().startswith('Entry'):
                files[int(m.group(1))] = (int(m.group(2)), m.group(3).strip())
    comp = dirs.get(0, '')
    res = {}
    for i, (d, n) in files.items():
        dd = dirs.get(d, '')
        p = n if n.startswith('/') else (os.path.join(dd, n) if dd else n)
        if comp and p.startswith(comp + '/'):
            p = p[len(comp) + 1:]
        res[i] = os.path.normpath(p)
    return res


def read_dwarf(obj):
    out = run(['readelf', '--debug-dump=info', '--wide', obj])
    dies, stack, cur = {}, [], None
    roots = []
    for line in out.splitlines():
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
            else:
                roots.append(cur)
            stack.append(cur)
            continue
        if cur is not None:
            m = ATTR_RE.match(line)
            if m:
                cur.attrs[m.group(1)] = m.group(2)
    return dies, roots


STRIP_TAGS = ('DW_TAG_typedef', 'DW_TAG_const_type', 'DW_TAG_volatile_type', 'DW_TAG_restrict_type',
              'DW_TAG_atomic_type')


def canon(dies, ref):
    """(abi class, readable name) of the type at ref; None ref = void."""
    shown = None
    d = dies.get(ref) if ref is not None else None
    for _ in range(64):
        if d is None or d.tag not in STRIP_TAGS:
            break
        if shown is None and d.tag == 'DW_TAG_typedef':
            shown = d.name()
        r = d.ref()
        d = dies.get(r) if r is not None else None
    if d is None:
        return ('void', shown or 'void')
    t = d.tag
    if t == 'DW_TAG_base_type':
        enc, size = d.num('DW_AT_encoding'), d.num('DW_AT_byte_size')
        k = ATE.get(enc, '?')
        abi = '%s%d' % (k, size * 8)
        return (abi, shown or d.name() or abi)
    if t in ('DW_TAG_pointer_type', 'DW_TAG_array_type', 'DW_TAG_subroutine_type'):
        tgt = d.ref()
        inner = canon(dies, tgt)[1] if t == 'DW_TAG_pointer_type' else '?'
        return ('p32', shown or (inner + ' *'))
    if t == 'DW_TAG_enumeration_type':
        size = d.num('DW_AT_byte_size') or 4
        under = canon(dies, d.ref())[0] if d.ref() is not None else 'u%d' % (size * 8)
        return (under, shown or ('enum ' + (d.name() or '?')))
    if t in ('DW_TAG_structure_type', 'DW_TAG_union_type'):
        size = d.num('DW_AT_byte_size')
        kw = 'struct' if t == 'DW_TAG_structure_type' else 'union'
        return ('%s:%s' % (kw, size), shown or ('%s %s' % (kw, d.name() or '?')))
    return ('?' + t, shown or t)


def wide(abi):
    """The register class: every 4-byte scalar or pointer is one class."""
    if abi in ('s32', 'u32', 'p32', 'b32', 'addr32'):
        return 'w32'
    return abi


def enclosing_func(d, dies):
    p = d.parent
    while p is not None:
        if p.tag == 'DW_TAG_subprogram':
            n = p.name()
            if n is None:
                o = p.ref('DW_AT_abstract_origin') or p.ref('DW_AT_specification')
                if o is not None and o in dies:
                    n = dies[o].name()
            return n
        p = p.parent
    return None


def sig(dies, d):
    """Return type, params, prototyped and varargs/unspecified of a subprogram DIE."""
    base = d
    for a in ('DW_AT_abstract_origin', 'DW_AT_specification'):
        o = d.ref(a)
        if o is not None and o in dies:
            base = dies[o]
    ret = canon(dies, base.ref())
    params, unspec = [], False
    for c in base.children:
        if c.tag == 'DW_TAG_formal_parameter':
            params.append(canon(dies, c.ref()))
        elif c.tag == 'DW_TAG_unspecified_parameters':
            unspec = True
    return ret, params, base.flag('DW_AT_prototyped'), unspec, base


def vcanon(dies, ref):
    """ABI class of a data object's type: arrays by element class and count."""
    d = dies.get(ref) if ref is not None else None
    shown = None
    for _ in range(64):
        if d is None or d.tag not in STRIP_TAGS:
            break
        if shown is None and d.tag == 'DW_TAG_typedef':
            shown = d.name()
        r = d.ref()
        d = dies.get(r) if r is not None else None
    if d is not None and d.tag == 'DW_TAG_array_type':
        n = None
        for c in d.children:
            if c.tag == 'DW_TAG_subrange_type':
                cnt, ub = c.num('DW_AT_count'), c.num('DW_AT_upper_bound')
                n = cnt if cnt is not None else (ub + 1 if ub is not None else None)
        e = canon(dies, d.ref())
        return ('arr:%s' % wide(e[0]), '%s[%s]' % (shown or e[1], '' if n is None else n))
    c = canon(dies, ref)
    return (c[0], c[1])


def scan_object(obj):
    files = read_files(obj)
    dies, roots = read_dwarf(obj)
    defs, decls = [], {}
    vdefs, vdecls = [], {}
    specified = {x.ref('DW_AT_specification') for x in dies.values()
                 if x.tag == 'DW_TAG_variable' and 'DW_AT_specification' in x.attrs}
    for d in dies.values():
        if d.tag != 'DW_TAG_variable' or d.parent is None:
            continue
        if d.parent.tag != 'DW_TAG_compile_unit' and not d.flag('DW_AT_declaration'):
            continue                      # a local variable
        base = d
        o = d.ref('DW_AT_specification')
        if o is not None and o in dies:
            base = dies[o]
        name = base.name()
        if name is None or not base.flag('DW_AT_external'):
            continue
        fidx = base.num('DW_AT_decl_file')
        where = '%s:%s' % (files.get(fidx, '?%s' % fidx), base.num('DW_AT_decl_line'))
        rec = {'name': name, 'where': where, 'type': vcanon(dies, base.ref()), 'obj': obj}
        if d.flag('DW_AT_declaration') and 'DW_AT_location' not in d.attrs:
            if d.off in specified:
                continue                  # this file's own definition completes it
            vdecls[(name, where)] = rec
        elif 'DW_AT_location' in d.attrs or not d.flag('DW_AT_declaration'):
            if not any(x['name'] == name for x in vdefs):
                vdefs.append(rec)
    calls = collections.defaultdict(list)     # decl DIE offset -> [(caller, return pc)]
    for d in dies.values():
        if d.tag == 'DW_TAG_call_site' or d.tag == 'DW_TAG_GNU_call_site':
            o = d.ref('DW_AT_call_origin') or d.ref('DW_AT_abstract_origin')
            if o is not None:
                pc = d.attrs.get('DW_AT_call_return_pc') or d.attrs.get('DW_AT_low_pc') or ''
                calls[o].append((enclosing_func(d, dies), pc.split()[-1] if pc else ''))
    for d in dies.values():
        if d.tag != 'DW_TAG_subprogram':
            continue
        ret, params, proto, unspec, base = sig(dies, d)
        name = base.name()
        if name is None or not base.flag('DW_AT_external'):
            continue
        fidx = base.num('DW_AT_decl_file')
        where = '%s:%s' % (files.get(fidx, '?%s' % fidx), base.num('DW_AT_decl_line'))
        rec = {'name': name, 'where': where, 'ret': ret, 'params': params, 'proto': proto,
               'unspec': unspec, 'obj': obj}
        if d.flag('DW_AT_declaration'):
            scope = enclosing_func(d, dies)
            k = (name, where)
            r = decls.get(k)
            if r is None:
                rec['scope'] = scope
                rec['callers'] = []
                decls[k] = r = rec
            elif scope and not r.get('scope'):
                r['scope'] = scope
            for c in calls.get(d.off, ()):
                if c not in r['callers']:
                    r['callers'].append(c)
        elif d.ref('DW_AT_abstract_origin') is None:
            # a definition (an abstract instance of an inline one counts once, with its own name)
            if not any(x['name'] == name for x in defs):
                defs.append(rec)
    return obj, defs, [r for r in decls.values() if not r['where'].startswith('<built-in>')], \
        vdefs, list(vdecls.values())


def compare_data(vdefs, vdecls):
    """Data objects declared with another type than their definition (informational)."""
    out = []
    bydef = {d['name']: d for d in vdefs}
    for c in vdecls:
        d = bydef.get(c['name'])
        if d is None or (c['obj'] == d['obj'] and c['where'] == d['where']):
            continue
        a, b = c['type'][0], d['type'][0]
        if a == b or (a.startswith('arr:') and b.startswith('arr:') and a == b):
            continue
        if wide(a) == wide(b) and not a.startswith('arr:') and not b.startswith('arr:'):
            continue
        out.append({'key': '%s:%s' % (c['where'].rsplit(':', 1)[0], c['name']), 'name': c['name'],
                    'decl': c['where'], 'def': d['where'], 'decl_type': c['type'][1] + ' (%s)' % a,
                    'def_type': d['type'][1] + ' (%s)' % b})
    out.sort(key=lambda s: (s['name'], s['decl']))
    return out


def objects(objs_dir):
    out = []
    for root, _, fs in os.walk(objs_dir):
        for f in fs:
            if f.endswith('.o') and not any(f.startswith(s) for s in SKIP_OBJS):
                p = os.path.join(root, f)
                rel = os.path.relpath(p, objs_dir)
                if rel.startswith('src/') or rel.startswith('port/'):
                    out.append(p)
    return sorted(out)


def tname(t):
    return t[1] if t[1] == t[0] or t[0].startswith('struct') or t[0].startswith('union') else '%s (%s)' % (t[1], t[0])


def int_width(abi):
    """Width in bits of an integer-like class (integers, bool, pointers, enums), else None."""
    if abi[:1] in ('s', 'u', 'b', 'p') and abi[1:].isdigit():
        return int(abi[1:])
    return None


# What each kind means for the two compilers (the module comment): "differs" kinds can give the
# host another value than the GBA and need a fix or a reason; "same" kinds give the same value on
# both by the two compilers' conventions (still listed, and judged, so the list is the census).
def classify_ret(dr, cr):
    """Kind of a return-type difference: definition class dr, declared class cr."""
    if dr == cr or wide(dr) == wide(cr):
        return None
    if dr == 'void':
        return 'return void-def (differs: each caller reads a leftover register)'
    if cr == 'void':
        return 'return value-dropped (same: the caller ignores it)'
    wd, wc = int_width(dr), int_width(cr)
    if wd is not None and wc is not None:
        if wc > wd:
            return 'return wider-than-def (differs: the host callee leaves the bytes above %d bits undefined)' % wd
        if wc < wd:
            return 'return narrower-than-def (same: both callers extend the low %d bits)' % wc
        return 'return signedness (same: both callers extend per the declaration)'
    return 'return other (look)'


def decl_key(c):
    scope = c.get('scope')
    return '%s:%s%s' % (c['where'].rsplit(':', 1)[0], (scope + ':') if scope else '', c['name'])


def compare(defs, decls):
    sites = []
    bydef = {}
    for d in defs:
        bydef.setdefault(d['name'], []).append(d)
    for c in decls:
        ds = bydef.get(c['name'])
        if not ds:
            continue
        d = ds[0]
        if c['obj'] == d['obj'] and c['where'] == d['where']:
            continue
        kinds = []
        rk = classify_ret(d['ret'][0], c['ret'][0])
        if rk:
            kinds.append(rk)
        if c['proto']:
            if len(c['params']) != len(d['params']) or c['unspec'] != d['unspec']:
                kinds.append('param count %d%s vs %d%s (look)' % (len(c['params']), '+...' if c['unspec'] else '',
                                                                  len(d['params']), '+...' if d['unspec'] else ''))
            for i, (pc, pd) in enumerate(zip(c['params'], d['params'])):
                if pc[0] == pd[0]:
                    continue
                if wide(pc[0]) == wide(pd[0]):
                    kinds.append('param %d type (same: one 32-bit word)' % (i + 1))
                elif int_width(pc[0]) is not None and int_width(pd[0]) is not None:
                    kinds.append('param %d %s vs %s (same: both callees narrow to the definition\'s type)'
                                 % (i + 1, pc[0], pd[0]))
                else:
                    kinds.append('param %d class %s vs %s (look)' % (i + 1, pc[0], pd[0]))
        else:
            if d['params'] and d['proto']:
                narrow = [i + 1 for i, p in enumerate(d['params']) if wide(p[0]) != 'w32']
                kinds.append('unprototyped, the definition has %d params%s (look at the calls\' argument counts)' % (
                    len(d['params']), ', narrow: %s' % ','.join(map(str, narrow)) if narrow else ''))
        # benign-only kinds are still listed, so the list is the whole census
        if not kinds:
            continue
        scope = c.get('scope')
        key = decl_key(c)
        sites.append({
            'key': key, 'name': c['name'], 'decl': c['where'], 'scope': scope, 'decl_obj': c['obj'],
            'def': d['where'], 'def_obj': d['obj'],
            'decl_ret': tname(c['ret']), 'def_ret': tname(d['ret']),
            'decl_params': [tname(p) for p in c['params']] + (['...'] if c['unspec'] else []),
            'def_params': [tname(p) for p in d['params']] + (['...'] if d['unspec'] else []),
            'decl_proto': c['proto'], 'kinds': kinds,
            'callers': sorted(set(x[0] or '?' for x in c['callers'])),
            'calls': len(c['callers']),
        })
    sites.sort(key=lambda s: (s['name'], s['decl']))
    return sites


def read_accept(path):
    acc = {}
    if not path:
        return acc
    for line in open(path):
        line = line.split('#', 1)[0].rstrip() if line.lstrip().startswith('#') else line.rstrip()
        if not line.strip():
            continue
        parts = line.split(None, 2)
        if len(parts) < 2:
            continue
        acc[parts[0]] = (parts[1], parts[2] if len(parts) > 2 else '')
    return acc


def lto_check(args, sites, data):
    """Build every FULL object with -flto, link with LTO, parse -Wlto-type-mismatch."""
    B = args.build_dir
    env = dict(os.environ, MAKEFLAGS='')
    pn = subprocess.run(['make', '-f', 'port/Makefile', '-pn', 'FULL=1', 'BUILD=' + B, '.none'], capture_output=True,
                        text=True, errors='replace', env=env).stdout
    def var(v):
        m = re.search(r'^%s := (.*)$' % v, pn, re.M)
        return m.group(1) if m else ''
    hc = (' %s ' % var('HOST_CFLAGS')).replace(' -w ', ' ')
    pcf = var('PORT_CFLAGS')
    go, po = var('GAME_OBJS').split(), var('PORT_OBJS').split()
    # CONST_DATA's named ".data" section would make LTRANS refuse const and non-const objects of
    # different files in one section (B-7's conflict); the declarations do not depend on it.
    # (In HOST_CFLAGS, not HOST_CPPFLAGS: a command-line HOST_CPPFLAGS would drop port/Makefile's
    # per-object additions, such as hardware.o's and m4a.o's asm() removal.)
    cmd = ['make', '-f', 'port/Makefile', 'FULL=1', 'BUILD=' + B, '-j%d' % args.jobs, '-k',
           'HOST_CFLAGS=%s -flto -Wimplicit-function-declaration -DPORT_CONST_DATA_DEFAULT_SECTION' % hc,
           'PORT_CFLAGS=%s -flto' % pcf] + go + po
    p = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env)
    build_log = p.stdout + p.stderr
    if p.returncode != 0:
        print('decl_sweep: the -flto build failed (exit %d); see %s/build.log' % (p.returncode, B))
    os.makedirs(B, exist_ok=True)
    open(os.path.join(B, 'build.log'), 'w').write(build_log)
    warn = ''
    for o in go + po:
        w = o + '.warn'
        if os.path.exists(w):
            warn += open(w, errors='replace').read()
    implicit = sorted(set(re.findall(r"^(\S+?):(\d+):\d+: warning: implicit declaration of function '?‘?([A-Za-z_0-9]+)", warn, re.M)))
    q = subprocess.run(['gcc', '-m32', '-flto=%d' % args.jobs, '-flinker-output=nolto-rel', '-r', '-nostdlib',
                        '-o', os.path.join(B, 'lto-all.o')] + go + po, capture_output=True, text=True, errors='replace')
    link_log = q.stdout + q.stderr
    open(os.path.join(B, 'lto.log'), 'w').write(link_log)
    lto = []
    for m in re.finditer(r"^(\S+?):(\d+):\d+: warning: type of [‘']([^’']+)[’'] does not match original declaration",
                         link_log, re.M):
        lto.append((m.group(3), '%s:%s' % (m.group(1), m.group(2))))
    names = {s['name'] for s in sites} | {s['name'] for s in data}
    missing = sorted({n for n, _ in lto if n not in names})
    return {'build_exit': p.returncode, 'link_exit': q.returncode, 'lto': lto, 'implicit': implicit,
            'not_in_list': missing}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--objs-dir', default='build/pc-full')
    ap.add_argument('--json')
    ap.add_argument('--md')
    ap.add_argument('--accept')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--lto', action='store_true')
    ap.add_argument('--build-dir', default='build/declsweep')
    ap.add_argument('--show', default='open', choices=('all', 'open'))
    ap.add_argument('-j', '--jobs', type=int, default=4)
    args = ap.parse_args()

    objs = objects(args.objs_dir)
    if not objs:
        sys.exit('decl_sweep: no objects under %s (build it first: make -f port/Makefile FULL=1)' % args.objs_dir)
    with Pool(args.jobs) as pool:
        res = pool.map(scan_object, objs)
    defs, decls, vdefs, vdecls = [], [], [], []
    for obj, ds, cs, vds, vcs in res:
        defs += ds
        decls += cs
        vdefs += vds
        vdecls += vcs
    sites = compare(defs, decls)
    data = compare_data(vdefs, vdecls)
    acc = read_accept(args.accept)
    bad = []
    seen = set()
    for s in sites:
        v = acc.get(s['key'])
        s['verdict'] = v[0] if v else 'open'
        s['reason'] = v[1] if v else ''
        seen.add(s['key'])
        if v is None:
            bad.append('OPEN %s' % s['key'])
        elif v[0] == 'fixed':
            bad.append('FIX MISSING %s (listed as fixed, still differs)' % s['key'])
    all_keys = {decl_key(c) for c in decls}
    for k, (v, r) in acc.items():
        if v not in ('fixed', 'benign'):
            bad.append('BAD VERDICT %s (%s: want fixed or benign)' % (k, v))
        elif k not in seen and v == 'benign':
            bad.append('STALE %s (benign: matches no site)' % k)
        elif v == 'fixed' and k not in all_keys:
            bad.append('STALE %s (fixed: no such declaration is used any more)' % k)
    nd = len({d['name'] for d in defs})
    print('decl_sweep: %d objects, %d external definitions, %d declarations used, %d sites differ'
          % (len(objs), nd, len(decls), len(sites)))
    for s in sites:
        if args.show == 'open' and s['verdict'] != 'open':
            continue
        print('%-6s %s\n       decl %s: %s (%s)%s\n       def  %s: %s (%s)\n       %s; %d call(s) from %s'
              % (s['verdict'], s['key'], s['decl'], s['decl_ret'], ', '.join(s['decl_params']) or
                 ('void' if s['decl_proto'] else ''), '' if s['decl_proto'] else ' [unprototyped]',
                 s['def'], s['def_ret'], ', '.join(s['def_params']) or 'void',
                 '; '.join(s['kinds']), s['calls'], ', '.join(s['callers']) or '-'))
    print('decl_sweep: data objects declared with another type than their definition (the same bytes at the'
          ' same address on both sides; the reader\'s declared type decides on both): %d' % len(data))
    for s in data:
        print('data   %s\n       decl %s: %s\n       def  %s: %s' % (s['key'], s['decl'], s['decl_type'],
                                                                  s['def'], s['def_type']))
    out = {'objects': len(objs), 'definitions': nd, 'declarations': len(decls), 'sites': sites, 'data': data}
    if args.lto:
        lr = lto_check(args, sites, data)
        out['lto'] = lr
        print('decl_sweep --lto: link exit %d; -Wlto-type-mismatch: %d reports (%d names), %d not in the'
              ' lists above: %s; implicit declarations (-Wimplicit-function-declaration): %d, of %s'
              % (lr['link_exit'], len(lr['lto']), len({n for n, _ in lr['lto']}), len(lr['not_in_list']),
                 ', '.join(lr['not_in_list']) or 'none', len(lr['implicit']),
                 ', '.join(sorted({x[2] for x in lr['implicit']})) or 'none'))
        if lr['not_in_list']:
            bad.append('LTO reports a mismatch the DWARF scan did not list: %s' % ', '.join(lr['not_in_list']))
        if lr['link_exit'] != 0 or lr['build_exit'] != 0:
            bad.append('the -flto build or link failed (exit %d, %d; %s/build.log, lto.log), so its list may be short'
                       % (lr['build_exit'], lr['link_exit'], args.build_dir))
    if args.json:
        json.dump(out, open(args.json, 'w'), indent=1)
    if args.md:
        with open(args.md, 'w') as f:
            f.write('| Key | Declared | Defined | Kinds | Callers | Verdict |\n|---|---|---|---|---|---|\n')
            for s in sites:
                f.write('| `%s` | `%s` (%s) %s | `%s` (%s) %s | %s | %s | %s %s |\n' % (
                    s['key'], s['decl_ret'], ', '.join(s['decl_params']), s['decl'], s['def_ret'],
                    ', '.join(s['def_params']), s['def'], '; '.join(s['kinds']),
                    ', '.join(s['callers']), s['verdict'], s['reason']))
    if args.check:
        for b in bad:
            print('decl_sweep:', b)
        print('DECL SWEEP: %s (%d sites, %d judged, %d problems)' % ('PASS' if not bad else 'FAIL', len(sites),
                                                                      len(sites) - sum(1 for s in sites if s['verdict'] == 'open'), len(bad)))
        sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
