#!/usr/bin/env python3
"""Find expressions that agbcc (the GBA build) and the host GCC (every PC build) evaluate
in a different order, and say whether the order can change what the game does.

C leaves the order of a call's argument evaluations unspecified, and the two compilers
take opposite orders (port-plan DIVERGENCES, D-52, "Argument evaluation order";
round 9's analysis: port-plan/pending/r9-argorder.md):

  agbcc (tools/agbcc, "gcc 2.9-arm-000512" for Thumb; old_agbcc for m4a.c is the same):
    calls.c leaves PUSH_ARGS_REVERSED undefined, so arguments are evaluated left to right,
    one at a time; a plain memory read is loaded in its turn (precompute_register_parameters
    copies it to a register).  thumb.h defines ACCUMULATE_OUTGOING_ARGS, so when the call
    passes arguments on the stack (more than 4 words) precompute_arguments first evaluates
    every argument that contains a call, left to right, then the others left to right.
  host GCC (i386 -O1, HOST_CFLAGS; MinGW i686 the same): gimplify_call_expr evaluates the
    arguments right to left, every one of them, plain reads included (no flag changes it
    for C: -maccumulate-outgoing-args and -mno-push-args leave the order as it is).

So two arguments i < j are taken in opposite orders unless the call has stack arguments,
j contains a call and i does not.  This tool lists every call whose arguments make that
matter (class A: two or more arguments with a call or a side effect; class B: one such
argument and another that reads memory the first one may write) and, as class C, the
operators, assignments and function-pointer calls where the two compilers also differ:
agbcc pre-expands the calls of an operator expression, and of an assignment whose left side
is not a plain variable (expr.c preexpand_calls), before the plain reads and the left-hand
address, and loads a function pointer after the arguments; the host goes left to right
(fold puts a plain variable last in a commutative operator or comparison), takes
`lhs = call()`'s left-hand address first but any other right-hand side first, and `op=`'s
right side first.  port/tools/argorder_cases.c holds the compilers' real orders for each
form (--selftest checks the model against them).

For each site it works out what each argument may write and read (a per-function summary
of the whole game, through calls, with writes through pointer parameters mapped to the
caller's argument) and gives a verdict:

  conflict   the orders differ and one argument writes what the other reads or writes
  unknown    the orders differ and one argument makes a call this tool cannot follow
             (a function pointer, a proc start, an asm routine) while the other one has
             an effect or a read: needs a look by hand
  no-conflict / same-order   nothing to fix

With --gba-objs and --pc-objs it also reads the call order at each site from the two
builds' objects (objdump -d -l -r: agbcc's src/*.o and the host's build/pc-*/src/*.o) and
says whether it agrees with the rule.

usage (from a worktree root, after the GBA `make`, which generates headers some files need):
  python3 port/tools/argorder_sweep.py [--view gba|pc] [--files src/x.c ...] [-j 8]
        [--gba-objs .] [--pc-objs build/pc-gate0] [--json out.json] [--md out.md]
        [--show all|flagged] [--accept port/tools/argorder_accept.txt] [--check]
        [--func NAME] [--site FILE:LINE] [--selftest]

--view gba (default) preprocesses as the GBA build does (retail lines); --view pc as the
PC build does (PC_PORT=1 and port/include/port.h), so sites inside PC_PORT blocks show
and fixed sites go away.  --accept names the list of sites judged by hand (KEY VERDICT
reason; the key, file:function:hash of the expression, is printed for each flagged site);
--check exits 1 when a flagged site is not in the list, when a site listed as `fixed` is
still evaluated in the host's order (--view pc), or when an entry matches no flagged site
(--view gba).  --func prints a function's summary, --site every site at FILE:LINE.
check-all's `argorder` row runs --selftest and --check in both views.
"""
import argparse
import collections
import json
import os
import re
import subprocess
import sys
from multiprocessing import Pool

# ----------------------------------------------------------------------------- tokens

TOKEN_RE = re.compile(r'''
 (?P<ws>[ \t\r\f\v]+)
|(?P<id>[A-Za-z_$][A-Za-z0-9_$]*)
|(?P<num>(?:0[xX][0-9A-Fa-f]+|[0-9]+\.?[0-9]*(?:[eE][+-]?[0-9]+)?|\.[0-9]+(?:[eE][+-]?[0-9]+)?)[uUlLfF]*)
|(?P<str>L?"(?:\\.|[^"\\])*")
|(?P<chr>L?'(?:\\.|[^'\\])*')
|(?P<punct>\.\.\.|<<=|>>=|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||\*=|/=|%=|\+=|-=|&=|\^=|\|=|\#\#|[\[\](){}.,;:?~!%^&*+\-=<>|/#@\\`])
''', re.X)
LINEMARK_RE = re.compile(r'^#\s*(?:line\s+)?(\d+)\s+"([^"]*)"')


class Tok:
    __slots__ = ('k', 't', 'f', 'l', 'c')

    def __init__(self, k, t, f, l, c):
        self.k, self.t, self.f, self.l, self.c = k, t, f, l, c

    def __repr__(self):
        return '%s@%s:%d' % (self.t, self.f, self.l)


def tokenize(text):
    toks = []
    fname, line = '?', 1
    for raw in text.split('\n'):
        m = LINEMARK_RE.match(raw)
        if m:
            line, fname = int(m.group(1)), m.group(2)
            continue
        if raw.startswith('#'):          # pragma, ident
            line += 1
            continue
        pos, n = 0, len(raw)
        while pos < n:
            m = TOKEN_RE.match(raw, pos)
            if not m:
                pos += 1                 # stray byte (non-ASCII outside a string)
                continue
            kind = m.lastgroup
            if kind != 'ws':
                toks.append(Tok(kind, m.group(kind), fname, line, pos + 1))
            pos = m.end()
        line += 1
    toks.append(Tok('eof', '<eof>', fname, line, 1))
    return toks

# ----------------------------------------------------------------------------- types


class Ty:
    """A C type, as much as this tool needs: a base name ('struct Unit', 'int', 'void',
    'fn'), the pointer depth (arrays count), whether the outermost level is an array, and
    for functions and function pointers the return type."""
    __slots__ = ('base', 'ptr', 'arr', 'fn', 'ret', 'params', 'nwords')

    def __init__(self, base, ptr=0, arr=False, fn=False, ret=None, params=None):
        self.base, self.ptr, self.arr, self.fn, self.ret, self.params = base, ptr, arr, fn, ret, params
        self.nwords = None

    def deref(self):
        if self.fn:
            return self
        if self.ptr <= 0:
            return Ty('?')
        return Ty(self.base, self.ptr - 1, False, False, self.ret)

    def addr(self):
        return Ty(self.base, self.ptr + 1, False, False, self.ret)

    def is_ptr(self):
        return self.ptr > 0 or self.fn

    def tag(self):
        """The pointee's base: what a load through this pointer reads."""
        if self.fn:
            return 'fn'
        if self.ptr == 1:
            return self.base
        if self.ptr > 1:
            return 'ptr'
        return '?'

    def __repr__(self):
        return '%s%s%s' % (self.base, '*' * self.ptr, '[]' if self.arr else '')


T_INT = Ty('int')
T_UNKNOWN = Ty('?')
WILD = {'?', 'void', 'char', 'unsigned char', 'signed char', 'ptr'}
BASIC = {'void', 'char', 'short', 'int', 'long', 'float', 'double', 'signed', 'unsigned',
         '_Bool', '__int128', '__builtin_va_list'}
QUALS = {'const', 'volatile', '__volatile__', '__volatile', 'restrict', '__restrict',
         '__restrict__', '__const', '__const__', '__extension__'}
STORAGE = {'typedef', 'extern', 'static', 'auto', 'register', 'inline', '__inline',
           '__inline__', '_Noreturn', '_Thread_local'}
KEYWORDS = BASIC | QUALS | STORAGE | {
    'struct', 'union', 'enum', 'sizeof', 'if', 'else', 'while', 'do', 'for', 'switch',
    'case', 'default', 'return', 'break', 'continue', 'goto', '__attribute__',
    '__attribute', 'asm', '__asm__', '__asm', 'typeof', '__typeof__', '__typeof',
    '__alignof__', '__alignof', '_Alignof', '__builtin_offsetof', '_Static_assert',
    '__label__', '__builtin_va_arg', '__builtin_types_compatible_p'}

# ----------------------------------------------------------------------------- AST


class N:
    __slots__ = ('k', 'tok', 'op', 'name', 'kids', 'ty', 'ref', 'end')

    def __init__(self, k, tok, op=None, name=None, kids=(), ty=None, ref=None):
        self.k, self.tok, self.op, self.name, self.kids = k, tok, op, name, kids
        self.ty, self.ref, self.end = ty or T_UNKNOWN, ref, tok


class ParseError(Exception):
    pass


class Local:
    __slots__ = ('name', 'ty', 'pidx', 'static', 'addr')

    def __init__(self, name, ty, pidx=None, static=False):
        self.name, self.ty, self.pidx, self.static, self.addr = name, ty, pidx, static, False

# ----------------------------------------------------------------------------- parser


class TU:
    """Parses one preprocessed translation unit into function bodies (ASTs) and the
    symbol tables of that unit."""

    def __init__(self, toks, mainfile):
        self.toks, self.i, self.main = toks, 0, mainfile
        self.typedefs = {'__builtin_va_list': Ty('__builtin_va_list')}
        self.structs = {}        # tag -> {field: Ty}
        self.enums = set()
        self.globals = {}        # name -> (qname, Ty)
        self.funcs = {}          # name -> (qname, Ty)
        self.defs = []           # (qname, name, Local params, body stmts, tok)
        self.scopes = []
        self.anon = 0
        self.errors = []
        self.cur_func = None

    # -- token helpers
    def peek(self, o=0):
        return self.toks[min(self.i + o, len(self.toks) - 1)]

    def next(self):
        t = self.toks[self.i]
        self.i += 1
        return t

    def at(self, s, o=0):
        return self.peek(o).t == s

    def expect(self, s):
        t = self.next()
        if t.t != s:
            raise ParseError('expected %r, got %r at %s:%d' % (s, t.t, t.f, t.l))
        return t

    def skip_balanced(self):
        """At an opening bracket: skip to just past its partner."""
        pairs = {'(': ')', '[': ']', '{': '}'}
        open_ = self.next().t
        close, depth = pairs[open_], 1
        while depth:
            t = self.next()
            if t.k == 'eof':
                raise ParseError('unbalanced %s' % open_)
            if t.t == open_:
                depth += 1
            elif t.t == close:
                depth -= 1

    def skip_attrs(self):
        while self.peek().t in ('__attribute__', '__attribute', 'asm', '__asm__', '__asm', '__extension__'):
            t = self.next().t
            if t == '__extension__':
                continue
            while self.peek().t in QUALS:
                self.next()
            if self.at('('):
                self.skip_balanced()

    def lookup(self, name):
        for sc in reversed(self.scopes):
            if name in sc:
                return sc[name]
        return None

    def is_typename(self, t):
        if t.k != 'id':
            return False
        if t.t in BASIC or t.t in QUALS or t.t in ('struct', 'union', 'enum', 'typeof', '__typeof__', '__typeof'):
            return True
        if t.t in self.typedefs and self.lookup(t.t) is None:
            return True
        return False

    def is_decl_start(self):
        t = self.peek()
        if t.k != 'id':
            return False
        if t.t in STORAGE or t.t in ('__attribute__', '__attribute', '_Static_assert'):
            return True
        if t.t == '__extension__':
            return True
        if self.is_typename(t):
            if t.t in self.typedefs and self.peek(1).t in (':', '=', '(', '->', '.', '[', '++', '--', ')', ',', ';'):
                # a typedef name used as a label or an expression: not a declaration
                return self.peek(1).t == '(' and self.peek(2).t == '*'
            return True
        return False

    # -- types
    def resolve(self, name):
        ty = self.typedefs.get(name)
        if ty is None:
            return Ty(name)
        return ty

    def parse_specs(self):
        """Declaration specifiers. Returns (Ty, storage set) or None when there are none."""
        storage, words, ty, seen = set(), [], None, False
        while True:
            t = self.peek()
            if t.k != 'id':
                break
            s = t.t
            if s in STORAGE:
                storage.add(s)
                self.next()
            elif s in QUALS:
                self.next()
            elif s in ('__attribute__', '__attribute'):
                self.skip_attrs()
            elif s in BASIC:
                words.append(s)
                self.next()
            elif s in ('struct', 'union'):
                ty = self.parse_struct()
            elif s == 'enum':
                ty = self.parse_enum()
            elif s in ('typeof', '__typeof__', '__typeof'):
                self.next()
                self.skip_balanced()
                ty = T_UNKNOWN
            elif s in self.typedefs and ty is None and not words and self.lookup(s) is None:
                ty = self.resolve(s)
                self.next()
            else:
                break
            seen = True
        if not seen:
            return None
        if ty is None:
            ty = Ty(canon_basic(words))
        return ty, storage

    def parse_struct(self):
        kw = self.next().t
        self.skip_attrs()
        tag = None
        if self.peek().k == 'id' and self.peek().t not in KEYWORDS:
            tag = '%s %s' % (kw, self.next().t)
        if self.at('{'):
            if tag is None:
                self.anon += 1
                tag = '%s <anon%d@%s:%d>' % (kw, self.anon, self.peek().f, self.peek().l)
            self.next()
            fields = {}
            while not self.at('}'):
                if self.at(';'):
                    self.next()
                    continue
                sp = self.parse_specs()
                if sp is None:
                    raise ParseError('struct member at %r' % self.peek())
                base, _ = sp
                if self.at(';'):                    # anonymous struct/union member
                    sub = self.structs.get(base.base, {})
                    fields.update(sub)
                    self.next()
                    continue
                while True:
                    if self.at(':'):
                        self.next()
                        self.parse_cond()
                    else:
                        name, ty = self.parse_declarator(base)
                        if self.at(':'):
                            self.next()
                            self.parse_cond()
                        if name:
                            fields[name] = ty
                    self.skip_attrs()
                    if self.at(','):
                        self.next()
                        continue
                    break
                self.expect(';')
            self.next()
            self.skip_attrs()
            self.structs[tag] = fields
        if tag is None:
            tag = kw + ' ?'
        return Ty(tag)

    def parse_enum(self):
        self.next()
        self.skip_attrs()
        if self.peek().k == 'id' and self.peek().t not in KEYWORDS:
            self.next()
        if self.at('{'):
            self.next()
            while not self.at('}'):
                t = self.next()
                if t.k == 'id':
                    self.enums.add(t.t)
                if self.at('='):
                    self.next()
                    self.parse_cond()
                if self.at(','):
                    self.next()
            self.next()
        self.skip_attrs()
        return Ty('int')

    def nested_declarator_follows(self):
        """At '(' inside a declarator: True when it opens a nested declarator such as
        (*fp), False when it is a parameter list."""
        t = self.peek(1)
        if t.t in ('*', '(', '['):
            return True
        if t.t in ('__attribute__', '__attribute'):
            return True
        if t.k == 'id' and not self.is_typename(t) and t.t not in STORAGE:
            return True
        return False

    def parse_declarator(self, base, want_params=None):
        """Returns (name or None, Ty). want_params: a list that receives the outermost
        function declarator's parameters (name, Ty)."""
        ptr = 0
        while True:
            t = self.peek().t
            if t == '*':
                ptr += 1
                self.next()
            elif t in QUALS:
                self.next()
            elif t in ('__attribute__', '__attribute'):
                self.skip_attrs()
            else:
                break
        name, inner = None, None
        if self.at('(') and self.nested_declarator_follows():
            self.next()
            inner = self.parse_declarator_inner()
            self.expect(')')
        elif self.peek().k == 'id' and self.peek().t not in KEYWORDS:
            name = self.next().t
        ty = Ty(base.base, base.ptr + ptr, False, base.fn, base.ret, base.params)
        if base.fn and ptr:
            ty = Ty('fn', ptr, fn=False, ret=base.ret)
        # suffixes
        dims, params, isfunc = 0, None, False
        while True:
            t = self.peek().t
            if t == '[':
                self.skip_balanced()
                dims += 1
            elif t == '(':
                params = self.parse_params()
                isfunc = True
                # a function's own suffixes stop here (no arrays of functions)
            elif t in ('__attribute__', '__attribute', 'asm', '__asm__', '__asm'):
                self.skip_attrs()
            else:
                break
        if isfunc:
            fty = Ty('fn', 0, fn=True, ret=ty, params=params)
            ty = fty
        elif dims:
            ty = Ty(ty.base, ty.ptr + dims, True, False, ty.ret)
        if inner is not None:
            iname, iptr, idims, iparams = inner
            name = iname
            if iptr:
                if ty.fn:                       # pointer to function
                    ty = Ty('fn', iptr, ret=ty.ret, params=ty.params)
                else:
                    ty = Ty(ty.base, ty.ptr + iptr, False, False, ty.ret)
            if idims:
                ty = Ty(ty.base, ty.ptr + idims, True, ty.fn, ty.ret)
            if iparams is not None:             # function returning (inner suffix)
                ty = Ty('fn', 0, fn=True, ret=ty, params=iparams)
                params = iparams
        if want_params is not None and params is not None:
            want_params.extend(params)
        return name, ty

    def parse_declarator_inner(self):
        """Inside the parentheses of a nested declarator: returns (name, ptr, dims, params)."""
        ptr = 0
        while True:
            t = self.peek().t
            if t == '*':
                ptr += 1
                self.next()
            elif t in QUALS:
                self.next()
            elif t in ('__attribute__', '__attribute'):
                self.skip_attrs()
            else:
                break
        name = None
        if self.at('(') and self.nested_declarator_follows():
            self.next()
            n2, p2, d2, pa2 = self.parse_declarator_inner()
            self.expect(')')
            name, ptr = n2, ptr + p2
        elif self.peek().k == 'id' and self.peek().t not in KEYWORDS:
            name = self.next().t
        dims, params = 0, None
        while True:
            if self.at('['):
                self.skip_balanced()
                dims += 1
            elif self.at('('):
                params = self.parse_params()
            else:
                break
        return name, ptr, dims, params

    def parse_params(self):
        self.expect('(')
        params = []
        if self.at(')'):
            self.next()
            return params
        if self.at('void') and self.at(')', 1):
            self.next()
            self.next()
            return params
        while True:
            if self.at('...'):
                self.next()
                params.append(('...', T_UNKNOWN))
            else:
                sp = self.parse_specs()
                if sp is None:
                    # K&R identifier list
                    t = self.next()
                    params.append((t.t, T_INT))
                else:
                    base, _ = sp
                    name, ty = self.parse_declarator(base)
                    if ty.fn:
                        ty = Ty('fn', 1, ret=ty.ret, params=ty.params)
                    elif ty.arr:
                        ty = Ty(ty.base, ty.ptr, False, False, ty.ret)
                    params.append((name, ty))
            self.skip_attrs()
            if self.at(','):
                self.next()
                continue
            break
        self.expect(')')
        return params

    def parse_typename(self):
        sp = self.parse_specs()
        if sp is None:
            raise ParseError('type name expected at %r' % self.peek())
        base, _ = sp
        _, ty = self.parse_declarator(base)
        return ty

    # -- top level
    def parse(self):
        while self.peek().k != 'eof':
            start = self.i
            try:
                self.parse_external()
            except ParseError as e:
                self.errors.append(str(e))
                self.i = start
                self.recover_top()

    def recover_top(self):
        depth = 0
        while self.peek().k != 'eof':
            t = self.next().t
            if t in ('(', '[', '{'):
                depth += 1
            elif t in (')', ']', '}'):
                depth -= 1
                if depth <= 0 and t == '}':
                    if self.at(';'):
                        self.next()
                    return
            elif t == ';' and depth <= 0:
                return

    def parse_external(self):
        if self.at(';'):
            self.next()
            return
        if self.peek().t in ('asm', '__asm__', '__asm'):
            self.next()
            while self.peek().t in QUALS:
                self.next()
            self.skip_balanced()
            if self.at(';'):
                self.next()
            return
        if self.at('_Static_assert'):
            self.next()
            self.skip_balanced()
            self.expect(';')
            return
        sp = self.parse_specs()
        if sp is None:
            sp = (T_INT, set())                 # implicit int (old-style)
        base, storage = sp
        if self.at(';'):
            self.next()
            return
        while True:
            params = []
            tok = self.peek()
            name, ty = self.parse_declarator(base, params)
            self.skip_attrs()
            if 'typedef' in storage:
                if name:
                    self.typedefs[name] = ty
            elif name and ty.fn:
                q = (self.main + '::' + name) if 'static' in storage else name
                if name in self.funcs and self.funcs[name][0] != name:
                    q = self.funcs[name][0]
                self.funcs[name] = (q, ty)
                if self.at('{') or (self.peek().k == 'id' and self.is_typename(self.peek())):
                    while not self.at('{'):         # K&R parameter declarations
                        self.next()
                    self.parse_function(q, name, params, ty, tok)
                    return
            elif name:
                q = (self.main + '::' + name) if 'static' in storage else name
                if name in self.globals and 'static' not in storage:
                    q = self.globals[name][0]
                self.globals[name] = (q, ty)
            if self.at('='):
                self.next()
                if self.at('{'):
                    self.skip_balanced()
                else:
                    self.parse_assign()
            if self.at(','):
                self.next()
                continue
            break
        self.expect(';')

    def parse_function(self, q, name, params, fty, tok):
        locs = {}
        plist = []
        for k, (pn, pt) in enumerate(params):
            if pn == '...':
                continue
            loc = Local(pn, pt, pidx=k)
            plist.append(loc)
            if pn:
                locs[pn] = loc
        self.scopes = [locs]
        self.cur_func = q
        start = self.i
        try:
            body = self.parse_compound(new_scope=False)
        except ParseError as e:
            self.errors.append('%s: %s' % (q, e))
            self.i = start
            self.skip_balanced()
            self.scopes = []
            return
        self.scopes = []
        self.defs.append((q, name, plist, body, tok, fty))

    # -- statements: a statement is a list of ('expr', N, first_tok, last_tok) entries and
    # nested lists; only the full expressions matter here.
    def parse_compound(self, new_scope=True):
        self.expect('{')
        if new_scope:
            self.scopes.append({})
        out = []
        while not self.at('}'):
            if self.peek().k == 'eof':
                raise ParseError('eof in block')
            if self.at('__label__'):
                while not self.at(';'):
                    self.next()
                self.next()
            elif self.is_decl_start():
                self.parse_local_decl(out)
            else:
                self.parse_statement(out)
        self.next()
        if new_scope:
            self.scopes.pop()
        return out

    def full(self, out, node, t0):
        out.append(('expr', node, t0, self.toks[self.i - 1]))

    def parse_local_decl(self, out):
        if self.at('_Static_assert'):
            self.next()
            self.skip_balanced()
            self.expect(';')
            return
        sp = self.parse_specs()
        base, storage = sp
        if self.at(';'):
            self.next()
            return
        while True:
            name, ty = self.parse_declarator(base)
            self.skip_attrs()
            if 'typedef' in storage:
                if name:
                    self.typedefs[name] = ty
            elif name:
                if 'extern' in storage or ty.fn:
                    if ty.fn:
                        self.funcs.setdefault(name, (name, ty))
                    else:
                        self.globals.setdefault(name, (name, ty))
                    loc = None
                else:
                    loc = Local(name, ty, static='static' in storage)
                    if loc.static:
                        loc.name = '%s::%s' % (self.cur_func, name)
                    self.scopes[-1][name] = loc
                if self.at('='):
                    t0 = self.next()
                    if self.at('{'):
                        node = self.parse_init_list()
                    else:
                        node = self.parse_assign()
                    if loc is not None and not loc.static:
                        lhs = N('id', t0, name=name, ty=ty, ref=('local', loc))
                        node = N('asg', t0, op='=', kids=(lhs, node), ty=ty)
                        node.end = self.toks[self.i - 1]
                    if loc is None or not loc.static:
                        self.full(out, node, t0)
            if self.at(','):
                self.next()
                continue
            break
        self.expect(';')

    def parse_init_list(self):
        t0 = self.expect('{')
        kids = []
        while not self.at('}'):
            if self.at('.'):
                self.next()
                self.next()
                while self.at('.') or self.at('['):
                    if self.at('['):
                        self.skip_balanced()
                    else:
                        self.next()
                        self.next()
                if self.at('='):
                    self.next()
            elif self.at('['):
                self.skip_balanced()
                if self.at('...'):
                    self.next()
                    self.parse_cond()
                if self.at('='):
                    self.next()
            if self.at('{'):
                kids.append(self.parse_init_list())
            else:
                kids.append(self.parse_assign())
            if self.at(','):
                self.next()
        self.next()
        return N('init', t0, kids=tuple(kids))

    def parse_statement(self, out):
        t = self.peek()
        s = t.t
        if s == '{':
            out.append(self.parse_compound())
        elif s == ';':
            self.next()
        elif s == 'if':
            self.next()
            self.expect('(')
            t0 = self.peek()
            self.full(out, self.parse_expr(), t0)
            self.expect(')')
            self.parse_statement(out)
            if self.at('else'):
                self.next()
                self.parse_statement(out)
        elif s in ('while', 'switch'):
            self.next()
            self.expect('(')
            t0 = self.peek()
            self.full(out, self.parse_expr(), t0)
            self.expect(')')
            self.parse_statement(out)
        elif s == 'do':
            self.next()
            self.parse_statement(out)
            self.expect('while')
            self.expect('(')
            t0 = self.peek()
            self.full(out, self.parse_expr(), t0)
            self.expect(')')
            self.expect(';')
        elif s == 'for':
            self.next()
            self.expect('(')
            self.scopes.append({})
            if self.is_decl_start():
                self.parse_local_decl(out)
            else:
                if not self.at(';'):
                    t0 = self.peek()
                    self.full(out, self.parse_expr(), t0)
                self.expect(';')
            if not self.at(';'):
                t0 = self.peek()
                self.full(out, self.parse_expr(), t0)
            self.expect(';')
            if not self.at(')'):
                t0 = self.peek()
                self.full(out, self.parse_expr(), t0)
            self.expect(')')
            self.parse_statement(out)
            self.scopes.pop()
        elif s in ('case',):
            self.next()
            self.parse_cond()
            if self.at('...'):
                self.next()
                self.parse_cond()
            self.expect(':')
            if not self.at('}'):
                self.parse_statement(out)
        elif s == 'default' and self.at(':', 1):
            self.next()
            self.next()
            if not self.at('}'):
                self.parse_statement(out)
        elif s == 'return':
            self.next()
            if not self.at(';'):
                t0 = self.peek()
                self.full(out, self.parse_expr(), t0)
            self.expect(';')
        elif s in ('break', 'continue'):
            self.next()
            self.expect(';')
        elif s == 'goto':
            self.next()
            if self.at('*'):
                self.next()
                self.parse_expr()
            else:
                self.next()
            self.expect(';')
        elif s in ('asm', '__asm__', '__asm'):
            self.next()
            while self.peek().t in QUALS or self.peek().t == 'goto':
                self.next()
            self.skip_balanced()
            self.expect(';')
        elif t.k == 'id' and self.at(':', 1) and s not in KEYWORDS:
            self.next()
            self.next()
            self.skip_attrs()
            if not self.at('}'):
                self.parse_statement(out)
        else:
            t0 = self.peek()
            node = self.parse_expr()
            self.full(out, node, t0)
            self.expect(';')

    # -- expressions
    def parse_expr(self):
        n = self.parse_assign()
        while self.at(','):
            t = self.next()
            r = self.parse_assign()
            n = self.mk('comma', t, kids=(n, r), ty=r.ty)
        return n

    def mk(self, k, tok, **kw):
        n = N(k, tok, **kw)
        n.end = self.toks[self.i - 1]
        return n

    ASSIGN_OPS = {'=', '+=', '-=', '*=', '/=', '%=', '<<=', '>>=', '&=', '^=', '|='}

    def parse_assign(self):
        lhs = self.parse_cond()
        if self.peek().t in self.ASSIGN_OPS:
            t = self.next()
            rhs = self.parse_assign()
            return self.mk('asg', t, op=t.t, kids=(lhs, rhs), ty=lhs.ty)
        return lhs

    def parse_cond(self):
        c = self.parse_binary(0)
        if self.at('?'):
            t = self.next()
            if self.at(':'):                    # GNU a ?: b
                a = c
            else:
                a = self.parse_expr()
            self.expect(':')
            b = self.parse_cond()
            return self.mk('cond', t, kids=(c, a, b), ty=a.ty if a.ty.base != '?' else b.ty)
        return c

    PREC = [('||',), ('&&',), ('|',), ('^',), ('&',), ('==', '!='), ('<', '>', '<=', '>='),
            ('<<', '>>'), ('+', '-'), ('*', '/', '%')]

    def parse_binary(self, level):
        if level == len(self.PREC):
            return self.parse_cast()
        n = self.parse_binary(level + 1)
        ops = self.PREC[level]
        while self.peek().t in ops:
            t = self.next()
            r = self.parse_binary(level + 1)
            ty = T_INT
            if t.t in ('+', '-'):
                if n.ty.is_ptr() and not (t.t == '-' and r.ty.is_ptr()):
                    ty = Ty(n.ty.base, n.ty.ptr, False, n.ty.fn, n.ty.ret)
                elif r.ty.is_ptr() and t.t == '+':
                    ty = Ty(r.ty.base, r.ty.ptr, False, r.ty.fn, r.ty.ret)
                else:
                    ty = n.ty if n.ty.base != '?' else r.ty
            elif t.t in ('*', '/', '%', '&', '|', '^', '<<', '>>'):
                ty = n.ty
            n = self.mk('bin', t, op=t.t, kids=(n, r), ty=ty)
        return n

    def paren_is_type(self):
        """At '(': True when a type name follows (cast, compound literal, sizeof type)."""
        t = self.peek(1)
        return self.is_typename(t)

    def parse_cast(self):
        if self.at('(') and self.paren_is_type():
            t = self.next()
            ty = self.parse_typename()
            self.expect(')')
            if self.at('{'):
                init = self.parse_init_list()
                n = self.mk('clit', t, kids=(init,), ty=ty)
                return self.parse_postfix_ops(n)
            k = self.parse_cast()
            return self.mk('cast', t, kids=(k,), ty=ty)
        return self.parse_unary()

    def parse_unary(self):
        t = self.peek()
        s = t.t
        if s in ('++', '--'):
            self.next()
            k = self.parse_unary()
            return self.mk('pre', t, op=s, kids=(k,), ty=k.ty)
        if s in ('&', '*', '+', '-', '~', '!'):
            self.next()
            k = self.parse_cast()
            if s == '&':
                ty = k.ty.addr() if not k.ty.fn else k.ty
            elif s == '*':
                ty = k.ty.deref()
            elif s == '!':
                ty = T_INT
            else:
                ty = k.ty
            return self.mk('un', t, op=s, kids=(k,), ty=ty)
        if s == '&&':                           # GNU label address
            self.next()
            self.next()
            return self.mk('num', t)
        if s in ('sizeof', '__alignof__', '__alignof', '_Alignof'):
            self.next()
            if self.at('(') and self.paren_is_type():
                self.next()
                self.parse_typename()
                self.expect(')')
            else:
                self.parse_unary()
            return self.mk('num', t, ty=T_INT)
        if s == '__extension__':
            self.next()
            return self.parse_cast()
        return self.parse_postfix()

    def parse_primary(self):
        t = self.next()
        if t.k == 'id':
            s = t.t
            if s == '__builtin_offsetof':
                self.expect('(')
                self.parse_typename()
                self.expect(',')
                depth = 0
                while not (self.at(')') and depth == 0):
                    x = self.next().t
                    if x in ('(', '['):
                        depth += 1
                    elif x in (')', ']'):
                        depth -= 1
                self.next()
                return self.mk('num', t, ty=T_INT)
            if s == '__builtin_va_arg':
                self.expect('(')
                k = self.parse_assign()
                self.expect(',')
                ty = self.parse_typename()
                self.expect(')')
                return self.mk('cast', t, kids=(k,), ty=ty)
            if s == '__builtin_types_compatible_p':
                self.skip_balanced()
                return self.mk('num', t, ty=T_INT)
            loc = self.lookup(s)
            if loc is not None:
                ref, ty = ('local', loc), loc.ty
            elif s in self.enums:
                ref, ty = ('enum',), T_INT
            elif s in self.globals:
                q, ty = self.globals[s]
                ref = ('global', q)
            elif s in self.funcs:
                q, ty = self.funcs[s]
                ref = ('func', q)
            else:
                ref, ty = ('undeclared', s), T_UNKNOWN
            return self.mk('id', t, name=s, ty=ty, ref=ref)
        if t.k == 'num' or t.k == 'chr':
            return self.mk('num', t, ty=T_INT)
        if t.k == 'str':
            while self.peek().k == 'str':
                self.next()
            return self.mk('str', t, ty=Ty('char', 1))
        if t.t == '(':
            if self.at('{'):                    # GNU statement expression
                self.scopes.append({})
                body = self.parse_compound(new_scope=False)
                self.scopes.pop()
                self.expect(')')
                ty = T_UNKNOWN
                last = body[-1] if body else None
                if last and last[0] == 'expr':
                    ty = last[1].ty
                return self.mk('stmtexpr', t, kids=tuple(body), ty=ty)
            n = self.parse_expr()
            self.expect(')')
            return n
        raise ParseError('unexpected %r at %s:%d' % (t.t, t.f, t.l))

    def parse_postfix(self):
        return self.parse_postfix_ops(self.parse_primary())

    def parse_postfix_ops(self, n):
        while True:
            t = self.peek()
            s = t.t
            if s == '[':
                self.next()
                ix = self.parse_expr()
                self.expect(']')
                base = n
                if not n.ty.is_ptr() and ix.ty.is_ptr():
                    base = ix
                n = self.mk('idx', t, kids=(n, ix), ty=base.ty.deref())
            elif s == '(':
                self.next()
                args = []
                while not self.at(')'):
                    args.append(self.parse_assign())
                    if self.at(','):
                        self.next()
                self.next()
                fty = n.ty
                if fty.fn and fty.ptr == 0:
                    ret = fty.ret or T_UNKNOWN
                elif fty.base == 'fn' and fty.ptr >= 1:
                    ret = fty.ret or T_UNKNOWN
                else:
                    ret = T_UNKNOWN
                c = self.mk('call', t, kids=(n,) + tuple(args), ty=ret)
                n = c
            elif s in ('.', '->'):
                self.next()
                f = self.next().t
                bt = n.ty if s == '.' else n.ty.deref()
                fields = self.structs.get(bt.base) if bt.ptr == 0 else None
                fty = (fields or {}).get(f, T_UNKNOWN)
                n = self.mk('mem', t, op=s, name=f, kids=(n,), ty=fty)
            elif s in ('++', '--'):
                self.next()
                n = self.mk('post', t, op=s, kids=(n,), ty=n.ty)
            else:
                return n


def canon_basic(words):
    if not words:
        return 'int'
    w = [x for x in words if x not in ('signed',)] or ['int']
    s = ' '.join(sorted(set(w), key=w.index))
    if 'char' in words:
        if 'unsigned' in words:
            return 'unsigned char'
        if 'signed' in words:
            return 'signed char'
        return 'char'
    if 'short' in words:
        return 'unsigned short' if 'unsigned' in words else 'short'
    if 'long' in words:
        n = words.count('long')
        return ('unsigned ' if 'unsigned' in words else '') + ('long long' if n > 1 else 'long')
    if 'unsigned' in words:
        return 'unsigned int'
    return s

# ----------------------------------------------------------------------------- facts
# A location is a tuple:
#   ('G', qname, field|None)  a global object (field: first member below it)
#   ('F', tag, field)         a struct member reached through a pointer of pointee type tag
#   ('M', tag)                memory reached through a pointer, no member (tag: pointee)
#   ('D', qname)              memory reached through the global pointer variable qname
#   ('L', name)               an address-taken local (or local array) of this function
#   ('P', k, field|'*')       (summaries only) memory reached through parameter k


class Fact:
    """What evaluating an expression may write and read, and the calls it makes.
    pre and late split it the way agbcc's expr.c expands an operator expression:
    preexpand_calls() first runs every call it reaches without entering a call's
    arguments or a ?:, &&, || or comma operand (pre: one Fact per call), then the rest is
    expanded in tree order (late: the plain reads and writes, and everything under those
    operators)."""
    __slots__ = ('W', 'R', 'calls', 'hascall', 'effop', 'pre', 'late')

    def __init__(self):
        self.W, self.R, self.calls, self.hascall, self.effop = set(), set(), [], False, False
        self.pre = []
        self.late = None

    def merge(self, o):
        self.W |= o.W
        self.R |= o.R
        self.calls.extend(o.calls)
        self.hascall |= o.hascall
        self.effop |= o.effop
        return self

    def copy(self):
        f = Fact()
        f.merge(self)
        return f

    def lt(self):
        return self.late if self.late is not None else Fact()


def merge_split(*facts):
    """pre and late of an expression whose operands are all in one preexpand region."""
    out = Fact()
    late = Fact()
    for f in facts:
        out.merge(f)
        out.pre.extend(f.pre)
        late.merge(f.lt())
    out.late = late
    return out


def opaque(f):
    """A ?:, &&, ||, comma or statement-expression: preexpand_calls stops there."""
    f.pre = []
    f.late = f.copy()
    return f


def leaf(f):
    """No calls reached by preexpand: everything is late."""
    f.late = f.copy()
    return f


def strip(n):
    while n.k == 'cast':
        n = n.kids[0]
    return n


class FuncAnalyzer:
    """Walks one function's AST: its own direct effects (with parameter-relative
    locations), its calls (callee, argument bases) and the candidate sites."""

    def __init__(self, tu, qname, params, body, file):
        self.tu, self.q, self.params, self.body, self.file = tu, qname, params, body, file
        self.sites = []
        self.fact = Fact()

    def run(self):
        self.mark_addr(self.body)
        self.walk_stmts(self.body)
        return self

    def mark_addr(self, stmts):
        for st in stmts:
            if isinstance(st, list):
                self.mark_addr(st)
            else:
                self._mark(st[1])

    def _mark(self, n):
        if n.k == 'un' and n.op == '&':
            k = n.kids[0]
            while k.k in ('mem', 'idx') or (k.k == 'cast'):
                if k.k == 'mem' and k.op == '->':
                    break
                if k.k == 'idx' and not k.kids[0].ty.arr:
                    break
                k = k.kids[0]
            if k.k == 'id' and k.ref and k.ref[0] == 'local':
                k.ref[1].addr = True
        if n.k == 'id' and n.ref and n.ref[0] == 'local' and n.ty.arr:
            n.ref[1].addr = True
        if n.k == 'stmtexpr':
            self.mark_addr(list(n.kids))
            return
        for c in n.kids:
            if isinstance(c, N):
                self._mark(c)

    def walk_stmts(self, stmts):
        for st in stmts:
            if isinstance(st, list):
                self.walk_stmts(st)
            else:
                _, node, t0, t1 = st
                f = self.rv(node, (t0, t1))
                self.fact.merge(f)

    # -- locations
    def loc_id(self, n):
        r = n.ref
        if not r:
            return None
        if r[0] == 'local':
            loc = r[1]
            if loc.static:
                return ('G', loc.name, None)
            if loc.addr:
                return ('L', loc.name)
            return None
        if r[0] == 'global':
            return ('G', r[1], None)
        if r[0] == 'undeclared':
            return ('G', r[1], None)
        return None

    def ptr_root(self, e):
        """What the pointer expression e points into."""
        e = strip(e)
        while e.k == 'bin' and e.op in ('+', '-'):
            a, b = e.kids
            if strip(a).ty.is_ptr() or not strip(b).ty.is_ptr():
                e = strip(a)
            else:
                e = strip(b)
        if e.k == 'id':
            r = e.ref
            if r and r[0] == 'local':
                loc = r[1]
                if loc.static:
                    return ('G', loc.name, None) if loc.ty.arr else ('D', loc.name)
                if loc.ty.arr:
                    return ('L', loc.name)
                if loc.pidx is not None:
                    return ('P', loc.pidx, '*')
                return ('M', e.ty.tag())
            if r and r[0] in ('global', 'undeclared'):
                q = r[1]
                if e.ty.arr:
                    return ('G', q, None)
                return ('D', q)
            return ('M', e.ty.tag())
        if e.k == 'un' and e.op == '&':
            l = self.loc(e.kids[0])
            return l if l else ('L', '?')
        if e.k == 'num' and e.tok.k == 'num':
            # a fixed address: I/O registers, VRAM, palette, OAM (REG_*, VRAM macros)
            try:
                v = int(e.tok.t.rstrip('uUlL'), 0)
            except ValueError:
                v = -1
            if 0x04000000 <= v < 0x04000400:
                return ('G', '<io>', None)
            if 0x05000000 <= v < 0x08000000:
                return ('G', '<vram/pal/oam 0x%X>' % (v & 0xFF000000), None)
            return ('G', '<addr %s>' % e.tok.t, None)
        if e.k in ('idx', 'mem') and e.ty.arr:
            l = self.loc(e)
            if l:
                return l
        if e.k == 'idx' or (e.k == 'un' and e.op == '*'):
            inner = self.ptr_root(e.kids[0])
            if inner[0] in ('G', 'D'):
                return ('D', inner[1])
        return ('M', e.ty.tag())

    def loc(self, n):
        n = strip(n)
        if n.k == 'id':
            return self.loc_id(n)
        if n.k == 'mem':
            if n.op == '.':
                b = self.loc(n.kids[0])
                if b is None:
                    return None
                if b[0] == 'G':
                    return b if b[2] is not None else ('G', b[1], n.name)
                if b[0] == 'P':
                    return b if b[2] != '*' else ('P', b[1], n.name)
                if b[0] in ('M', 'D'):
                    return ('F', strip(n.kids[0]).ty.base, n.name)
                return b
            base = strip(n.kids[0])
            if base.k == 'id' and base.ref and base.ref[0] == 'local' and base.ref[1].pidx is not None \
                    and not base.ref[1].addr:
                return ('P', base.ref[1].pidx, n.name)
            return ('F', base.ty.tag(), n.name)
        if n.k == 'idx':
            b = n.kids[0]
            if not strip(b).ty.is_ptr() and strip(n.kids[1]).ty.is_ptr():
                b = n.kids[1]
            if strip(b).ty.arr:
                l = self.loc(b)
                if l is not None:
                    return l
                return None                     # a plain local array element
            return self.ptr_root(b)
        if n.k == 'un' and n.op == '*':
            k = strip(n.kids[0])
            if k.k == 'un' and k.op == '&':
                return self.loc(k.kids[0])
            return self.ptr_root(k)
        if n.k == 'stmtexpr' or n.k == 'call' or n.k == 'cond' or n.k == 'comma':
            return ('M', n.ty.base)
        return None

    def argbase(self, a):
        """How an actual argument maps a callee's parameter-relative locations."""
        a = strip(a)
        if not a.ty.is_ptr():
            if a.ty.base == '?' and a.k == 'id' and a.ref and a.ref[0] in ('global', 'undeclared'):
                return ('G', a.ref[1])
            return None
        r = self.ptr_root(a)
        if r[0] == 'G':
            return ('G', r[1])
        if r[0] == 'L':
            return ('L', r[1])
        if r[0] == 'P':
            return ('P', r[1])
        if r[0] == 'D':
            tag = a.ty.tag()
            if tag.startswith('struct ') or tag.startswith('union '):
                return ('T', tag)
            return ('D', r[1])
        if r[0] == 'F':
            return ('T', r[1])
        return ('T', r[1] if len(r) > 1 else '?')

    # -- evaluation
    def addr(self, n, span):
        """Effects of computing the address of lvalue n (n itself is not read)."""
        n = strip(n)
        if n.k == 'id':
            return leaf(Fact())
        if n.k == 'mem':
            if n.op == '.':
                return self.addr(n.kids[0], span)
            return self.rv(n.kids[0], span)
        if n.k == 'idx':
            parts = []
            for k in n.kids:
                parts.append(self.addr(k, span) if strip(k).ty.arr else self.rv(k, span))
            return merge_split(*parts)
        if n.k == 'un' and n.op == '*':
            return self.rv(n.kids[0], span)
        return self.rv(n, span)

    def rv(self, n, span):
        k = n.k
        if k in ('num', 'str'):
            return leaf(Fact())
        if k == 'id':
            f = Fact()
            if not (n.ty.arr or n.ty.fn):
                l = self.loc_id(n)
                if l:
                    f.R.add(l)
            return leaf(f)
        if k in ('mem', 'idx') or (k == 'un' and n.op == '*'):
            a = self.addr(n, span)
            own = Fact()
            if not n.ty.arr and not n.ty.fn:
                l = self.loc(n)
                if l:
                    own.R.add(l)
            return merge_split(a, leaf(own))
        if k == 'un':
            if n.op == '&':
                return self.addr(n.kids[0], span)
            return self.rv(n.kids[0], span)
        if k in ('cast',):
            return self.rv(n.kids[0], span)
        if k in ('pre', 'post'):
            t = n.kids[0]
            a = self.addr(t, span)
            own = Fact()
            l = self.loc(t)
            if l:
                own.R.add(l)
                own.W.add(l)
            own.effop = True
            return merge_split(a, leaf(own))
        if k == 'asg':
            lhs, rhs = n.kids
            fa = self.addr(lhs, span)
            fr = self.rv(rhs, span)
            self.site_asg(n, lhs, rhs, fa, fr, span)
            own = Fact()
            l = self.loc(lhs)
            if l:
                own.W.add(l)
                if n.op != '=':
                    own.R.add(l)
            own.effop = True
            return merge_split(fa, fr, leaf(own))
        if k == 'bin':
            fa = self.rv(n.kids[0], span)
            fb = self.rv(n.kids[1], span)
            if n.op in ('&&', '||'):
                return opaque(merge_split(fa, fb))
            self.site_bin(n, fa, fb, span)
            return merge_split(fa, fb)
        if k in ('init', 'clit'):
            parts = [self.rv(c, span) for c in n.kids]
            self.site_init(n, parts, span)
            return opaque(merge_split(*parts))
        if k in ('cond', 'comma'):
            return opaque(merge_split(*[self.rv(c, span) for c in n.kids]))
        if k == 'stmtexpr':
            f = Fact()
            self._stmts_into(list(n.kids), f)
            return opaque(f)
        if k == 'call':
            fn = strip(n.kids[0])
            args = n.kids[1:]
            f = Fact()
            callee = None
            if fn.k == 'id' and fn.ref and fn.ref[0] == 'func':
                callee = fn.ref[1]
            elif fn.k == 'id' and fn.ref and fn.ref[0] == 'undeclared':
                callee = fn.ref[1]
            elif fn.k == 'un' and fn.op == '&' and strip(fn.kids[0]).k == 'id' and strip(fn.kids[0]).ref \
                    and strip(fn.kids[0]).ref[0] == 'func':
                callee = strip(fn.kids[0]).ref[1]
            elif fn.k == 'id' and fn.ref and fn.ref[0] == 'global' and ('@' + fn.name) in EXTERN:
                callee = '@' + fn.name          # a known pointer to an ARM routine in IWRAM
                f.merge(self.rv(n.kids[0], span))
            else:
                ffact = self.rv(n.kids[0], span)
                f.merge(ffact)
            afacts = [self.rv(a, span) for a in args]
            for af in afacts:
                f.merge(af)
            if callee is None:
                self.site_fnptr(n, afacts, span)
            f.calls.append((callee, tuple(self.argbase(a) for a in args), n.tok.l))
            f.hascall = True
            if len(args) >= 2:
                self.site_call(n, callee, args, afacts, span)
            f.pre = [f.copy()]
            f.late = Fact()
            return f
        return leaf(Fact())

    def _stmts_into(self, stmts, f):
        for st in stmts:
            if isinstance(st, list):
                self._stmts_into(st, f)
            else:
                f.merge(self.rv(st[1], (st[2], st[3])))

    # -- sites
    def fmt(self, n):
        return src_text(n)

    def site_call(self, n, callee, args, afacts, span):
        nwords = 0
        params = None
        fn = strip(n.kids[0])
        if fn.ty.fn:
            params = fn.ty.params
        for i, a in enumerate(args):
            w = 1
            pt = None
            if params and i < len(params) and params[i][0] != '...':
                pt = params[i][1]
            t = pt or a.ty
            if t.ptr == 0 and not t.fn:
                if t.base in ('long long', 'unsigned long long', 'double'):
                    w = 2
                elif t.base.startswith('struct ') or t.base.startswith('union '):
                    fields = self.tu.structs.get(t.base)
                    w = 'struct'
            nwords = nwords + w if isinstance(w, int) and isinstance(nwords, int) else 'struct'
        self.sites.append({
            'kind': 'call', 'file': n.tok.f, 'line': n.tok.l, 'col': n.tok.c,
            'span': (span[0].f, span[0].l, span[1].l), 'func': self.q,
            'callee': callee or ('(*%s)' % self.fmt(n.kids[0])),
            'nwords': nwords,
            'args': [self.pack(a, af) for a, af in zip(args, afacts)],
            'text': self.fmt(n),
        })

    # Operators and assignments (class C).  agbcc: preexpand_calls() runs the calls first
    # (pre), then the rest in tree order (late).  The host: operands left to right, but
    # fold() swaps a commutative operator or comparison whose left operand is a plain
    # variable (tree_swap_operands_p, "put variables last"); an assignment whose
    # right-hand side is a call (no conversion) takes the left-hand address first,
    # any other right-hand side first (gimplify_modify_expr), and op= takes the right side
    # first.
    SWAP_OPS = {'+', '*', '&', '|', '^', '==', '!=', '<', '>', '<=', '>='}

    def _has(self, f):
        return bool(f.R or f.W or f.calls)

    def _site(self, kind, n, span, first, second, roles, gba, pc, note):
        self.sites.append({
            'kind': kind, 'op': n.op, 'file': n.tok.f, 'line': n.tok.l, 'col': n.tok.c,
            'span': (span[0].f, span[0].l, span[1].l), 'func': self.q, 'callee': n.op,
            'nwords': 0, 'note': note,
            'args': [self.pack(first[0], first[1], roles[0]), self.pack(second[0], second[1], roles[1])],
            'order_gba': gba, 'order_pc': pc,
            'text': self.fmt(n),
        })

    def site_bin(self, n, fa, fb, span):
        if not fb.pre:
            return
        la = fa.lt()
        if not self._has(la):
            return
        a0 = strip(n.kids[0])
        swap = n.op in self.SWAP_OPS and a0.k == 'id' and not (a0.ty.arr or a0.ty.fn)
        if swap:
            return                              # the host takes the variable last too
        pre_b = Fact()
        for p in fb.pre:
            pre_b.merge(p)
        self._site('bin', n, span, (n.kids[0], la), (n.kids[1], pre_b),
                   ('left operand, after the calls on agbcc', 'calls of the right operand'),
                   [1, 0], [0, 1], 'agbcc runs the right operand\'s calls before the left operand\'s reads')

    def site_asg(self, n, lhs, rhs, fa, fr, span):
        l0 = strip(lhs)
        if l0.k == 'id':
            return                              # a plain variable: no address to compute
        pre_l = Fact()
        for p in fa.pre:
            pre_l.merge(p)
        pre_r = Fact()
        for p in fr.pre:
            pre_r.merge(p)
        r0 = rhs
        direct = r0.k == 'call' and (r0.ty.base == '?' or lhs.ty.base == '?' or
                                     (r0.ty.base == lhs.ty.base and r0.ty.ptr == lhs.ty.ptr))
        if n.op == '=' and direct:
            la = fa.lt()
            if pre_r.calls and self._has(la):
                self._site('asg', n, span, (lhs, la), (rhs, pre_r),
                           ('left-hand address', 'right-hand call'), [1, 0], [0, 1],
                           'agbcc runs the right-hand call before the left-hand address; the host after')
        else:
            if pre_l.calls and self._has(fr):
                self._site('asg', n, span, (lhs, pre_l), (rhs, fr),
                           ('calls in the left-hand address', 'right-hand side'), [0, 1], [1, 0],
                           'agbcc runs the left-hand calls first; the host the right-hand side first')

    def site_fnptr(self, n, afacts, span):
        """A call through a function pointer loaded from memory: agbcc loads the pointer after
        the arguments (expand_call expands the address first and loads it as the call is
        made; calls.c), the host before them (gimplify_call_expr does the function first)."""
        fn = strip(n.kids[0])
        if fn.k not in ('mem', 'idx') and not (fn.k == 'un' and fn.op == '*') and not \
                (fn.k == 'id' and fn.ref and fn.ref[0] in ('global', 'undeclared')):
            return
        l = self.loc(fn)
        if not l:
            return
        args = Fact()
        for af in afacts:
            args.merge(af)
        if not args.calls and not args.effop:
            return
        load = Fact()
        load.R.add(l)
        self._site('fnptr', n, span, (n.kids[0], load), (n, args),
                   ('function pointer load', 'arguments'), [1, 0], [0, 1],
                   'agbcc loads the function pointer after the arguments, the host before')

    def site_init(self, n, parts, span):
        """An initializer list or compound literal with two or more elements that have calls
        or side effects. Both compilers store the elements in order as far as tested; listed
        so a conflict gets a look in the binaries."""
        eff = [i for i, p in enumerate(parts) if p.calls or p.effop]
        if len(eff) < 2:
            return
        first = Fact()
        for i in eff[:-1]:
            first.merge(parts[i])
        self._site('init', n, span, (n, first), (n, parts[eff[-1]]),
                   ('earlier elements', 'last element with a call'), [0, 1], [0, 1],
                   'initializer elements: same order on both compilers (unverified for this site)')

    def pack(self, a, af, role=None):
        return {
            'W': sorted(af.W, key=repr), 'R': sorted(af.R, key=repr),
            'calls': [(c, list(b), l) for c, b, l in af.calls],
            'hascall': af.hascall, 'effop': af.effop, 'role': role,
            'text': self.fmt(a), 'line': a.tok.l, 'endline': a.end.l,
            'ptypes': None,
        }


def src_text(n):
    """A short source rendering of an expression (for reports)."""
    try:
        return _src(n)[:200]
    except RecursionError:
        return '?'


def _src(n):
    k = n.k
    if k == 'id':
        return n.name
    if k == 'num':
        return n.tok.t if n.tok.k in ('num', 'chr') else n.tok.t + '(..)'
    if k == 'str':
        return n.tok.t
    if k == 'call':
        return '%s(%s)' % (_src(n.kids[0]), ', '.join(_src(a) for a in n.kids[1:]))
    if k == 'mem':
        return '%s%s%s' % (_src(n.kids[0]), n.op, n.name)
    if k == 'idx':
        return '%s[%s]' % (_src(n.kids[0]), _src(n.kids[1]))
    if k == 'un':
        return '%s%s' % (n.op, _src(n.kids[0]))
    if k == 'pre':
        return '%s%s' % (n.op, _src(n.kids[0]))
    if k == 'post':
        return '%s%s' % (_src(n.kids[0]), n.op)
    if k == 'cast':
        return '(%r)%s' % (n.ty, _src(n.kids[0]))
    if k in ('bin', 'asg'):
        return '%s %s %s' % (_src(n.kids[0]), n.op, _src(n.kids[1]))
    if k == 'cond':
        return '%s ? %s : %s' % tuple(_src(c) for c in n.kids)
    if k == 'comma':
        return '%s, %s' % (_src(n.kids[0]), _src(n.kids[1]))
    if k == 'stmtexpr':
        return '({...})'
    if k in ('init', 'clit'):
        return '{...}'
    return k

# ----------------------------------------------------------------------------- per file


def preprocess(root, path, view):
    if view == 'gba':
        # the GBA Makefile's $(CPP) $(CPPFLAGS): CPP ?= does not replace make's built-in
        # default, so the build preprocesses with `cc -E`
        cmd = ['cc', '-E', '-I', 'tools/agbcc/include', '-iquote', 'include', '-iquote', '.',
               '-nostdinc', '-undef', path]
    else:
        # port/Makefile's HOST_CPPFLAGS for a FULL build (with the generated headers)
        cmd = ['gcc', '-E', '-m32', '-std=gnu89', '-DPC_PORT=1', '-DNONMATCHING=1', '-Iinclude',
               '-Iport/include', '-include', 'port/include/port.h', '-iquote', '.', path]
        if os.path.basename(path) == 'bmdifficulty.c':
            cmd.insert(-1, '-DPORT_CONST_DATA_DEFAULT_SECTION')
    r = subprocess.run(cmd, cwd=root, capture_output=True)
    if r.returncode != 0:
        return None, r.stderr.decode('latin-1')[-2000:]
    return r.stdout.decode('latin-1'), None


def analyse_file(job):
    root, path, view = job
    text, err = preprocess(root, path, view)
    if text is None:
        return {'file': path, 'error': 'preprocess: ' + err, 'funcs': [], 'sites': [], 'globals': {}}
    toks = tokenize(text)
    tu = TU(toks, path)
    sys.setrecursionlimit(20000)
    tu.parse()
    funcs, sites = [], []
    for q, name, plist, body, tok, fty in tu.defs:
        try:
            fa = FuncAnalyzer(tu, q, plist, body, path).run()
        except RecursionError:
            tu.errors.append('%s: too deep' % q)
            continue
        f = fa.fact
        funcs.append({
            'q': q, 'name': name, 'file': tok.f, 'line': tok.l, 'tu': path,
            'params': [(p.name, p.ty.tag(), p.ty.is_ptr()) for p in plist],
            'W': sorted(f.W, key=repr), 'R': sorted(f.R, key=repr),
            'calls': [(c, list(b), l) for c, b, l in f.calls],
        })
        for s in fa.sites:
            s['ptags'] = [(p.ty.tag()) for p in plist]
        sites.extend(fa.sites)
    gl = {}
    for name, (q, ty) in tu.globals.items():
        gl[q] = (ty.base, ty.ptr, ty.arr)
    return {'file': path, 'errors': tu.errors[:50], 'nerrors': len(tu.errors), 'funcs': funcs,
            'sites': sites, 'globals': gl, 'ntoks': len(toks)}

# ----------------------------------------------------------------------------- summaries

# Functions with no C definition in the game (BIOS calls, libc, libgcc, the sound asm):
# (writes, reads) with parameter-relative locations; None = cannot follow.
EXTERN = {
    'CpuSet': ([('P', 1, '*')], [('P', 0, '*')]),
    'CpuFastSet': ([('P', 1, '*')], [('P', 0, '*')]),
    'LZ77UnCompWram': ([('P', 1, '*')], [('P', 0, '*')]),
    'LZ77UnCompVram': ([('P', 1, '*')], [('P', 0, '*')]),
    'RLUnCompWram': ([('P', 1, '*')], [('P', 0, '*')]),
    'RLUnCompVram': ([('P', 1, '*')], [('P', 0, '*')]),
    'HuffUnComp': ([('P', 1, '*')], [('P', 0, '*')]),
    'BitUnPack': ([('P', 1, '*')], [('P', 0, '*'), ('P', 2, '*')]),
    'BgAffineSet': ([('P', 1, '*')], [('P', 0, '*')]),
    'ObjAffineSet': ([('P', 1, '*')], [('P', 0, '*')]),
    'Div': ([], []), 'DivRem': ([], []), 'Mod': ([], []), 'DivArm': ([], []), 'DivArmRem': ([], []),
    'Sqrt': ([], []), 'ArcTan': ([], []), 'ArcTan2': ([], []),
    '__divsi3': ([], []), '__modsi3': ([], []), '__udivsi3': ([], []), '__umodsi3': ([], []),
    'memcpy': ([('P', 0, '*')], [('P', 1, '*')]),
    'memset': ([('P', 0, '*')], []),
    'strcpy': ([('P', 0, '*')], [('P', 1, '*')]),
    'strcat': ([('P', 0, '*')], [('P', 0, '*'), ('P', 1, '*')]),
    'strlen': ([], [('P', 0, '*')]),
    'strcmp': ([], [('P', 0, '*'), ('P', 1, '*')]),
    'strstr': ([], [('P', 0, '*'), ('P', 1, '*')]),
    'abs': ([], []), 'umul3232H32': ([], []), '__builtin_next_arg': ([], []),
    '__builtin_bswap16': ([], []), '__builtin_bswap32': ([], []), '__builtin_bswap64': ([], []),
    # port/include/port_gbabus.h (PC view only): a load at a GBA address
    'PortGbaLoad8': ([], [('M', '?')]), 'PortGbaLoad16': ([], [('M', '?')]), 'PortGbaLoad32': ([], [('M', '?')]),
    'sprintf': ([('P', 0, '*')], [('P', 1, '*'), ('M', '?')]),
    'vsprintf': ([('P', 0, '*')], [('P', 1, '*'), ('M', '?')]),
    # asm/arm_call.s: Thumb stubs into the ARM routines copied to IWRAM (src/ramfunc.c)
    'ClearOAMBuffer': ([('P', 0, '*')], []),
    'TileMap_FillRect': ([('P', 0, '*')], []),
    'CallARM_FillTileRect': ([('P', 0, '*')], [('P', 1, '*')]),
    'TileMap_CopyRect': ([('P', 1, '*')], [('P', 0, '*')]),
    'ComputeChecksum32': ([], [('P', 0, '*')]),
    'CALLARM_ColorFadeTick': ([('G', 'gPaletteBuffer', None)], [('G', 'gPaletteBuffer', None), ('M', '?')]),
    # the sound engine's asm (src/m4a_1.s): its own state only
    'm4aSoundVSync': ([('G', '<m4a>', None)], [('G', '<m4a>', None)]),
    'SoundMain': ([('G', '<m4a>', None)], [('G', '<m4a>', None)]),
    'TrackStop': ([('G', '<m4a>', None), ('P', 1, '*')], [('G', '<m4a>', None), ('P', 1, '*')]),
    'MPlayJumpTableCopy': ([('P', 0, '*')], []),
    'SoundBiasReset': ([('G', '<m4a>', None)], []), 'SoundBiasSet': ([('G', '<m4a>', None)], []),
    # src/ramfunc.c: the pointers StoreRoutinesToIRAM sets to the ARM routines of asm/arm.s
    '@DecodeStringRAMFunc': ([('P', 1, '*')], [('P', 0, '*')]),          # DecodeString(src, dst)
    '@DrawGlyphRAMFunc': ([('P', 1, '*')], [('P', 0, '*'), ('P', 2, '*')]),  # DrawGlyph(lut, dst, src, x)
    '@PutOamHiRAMFunc': ([('G', '<oam>', None)], [('G', '<oam>', None), ('P', 2, '*')]),  # PutOamHi
    '@PutOamLoRAMFunc': ([('G', '<oam>', None)], [('G', '<oam>', None), ('P', 2, '*')]),  # PutOamLo
    # include/agb_sram.h: pointers to the SRAM routines copied to RAM (src/agb_sram.c)
    '@ReadSramFast': ([('P', 1, '*')], [('P', 0, '*')]),
    '@VerifySramFast': ([], [('P', 0, '*'), ('P', 1, '*')]),
    # --- Reforged Options, the Cheats (port/reforged/reforged_cheats.c; PC view, played builds):
    # "Infinite gold" in GetPartyGoldAmount reads and tops up the party's gold only (and reads the
    # option values, which no game code writes) ---
    'Reforged_CheatGoldRefill': ([('G', 'gPlaySt', 'partyGoldAmount')], [('G', 'gPlaySt', 'partyGoldAmount')]),
    # --- end of the Cheats block ---
}


def subst(loc, bases):
    """A callee's location seen from the caller, through the call's argument bases."""
    if loc[0] == 'L':
        return None
    if loc[0] != 'P':
        return loc
    k, fld = loc[1], loc[2]
    if k >= len(bases):
        return None
    b = bases[k]
    if b is None:
        return ('F', '?', fld) if fld != '*' else ('M', '?')
    if b[0] == 'G':
        return ('G', b[1], None)
    if b[0] == 'D':
        return ('D', b[1])
    if b[0] == 'P':
        return ('P', b[1], fld)
    if b[0] == 'L':
        return ('L', b[1])
    if b[0] == 'T':
        return ('F', b[1], fld) if fld != '*' else ('M', b[1])
    return None


def tup(x):
    return tuple(tup(y) if isinstance(y, list) else y for y in x) if isinstance(x, (list, tuple)) else x


class Program:
    def __init__(self, results):
        self.funcs = {}
        self.globals = {}
        self.by_name = collections.defaultdict(list)
        for r in results:
            for q, g in r['globals'].items():
                self.globals.setdefault(q, g)
            for f in r['funcs']:
                f['W'] = set(tup(x) for x in f['W'])
                f['R'] = set(tup(x) for x in f['R'])
                f['calls'] = [(c, tup(b), l) for c, b, l in f['calls']]
                if f['q'] in self.funcs and '::' in f['q']:
                    continue                    # a header inline, once per unit: keep one
                self.funcs[f['q']] = f
                self.by_name[f['name']].append(f['q'])
        self.undefined = collections.Counter()
        self.summ = {}

    def resolve(self, callee):
        if callee is None:
            return None
        if callee in self.funcs:
            return callee
        if '::' in callee:
            base = callee.split('::')[-1]
            if base in self.funcs:
                return base
        return callee

    def summarize(self):
        S = {}
        for q, f in self.funcs.items():
            S[q] = [set(f['W']), set(f['R']), False]
        changed, rounds = True, 0
        while changed and rounds < 60:
            changed, rounds = False, rounds + 1
            for q, f in self.funcs.items():
                W, R, unk = S[q]
                n0 = (len(W), len(R), unk)
                for callee, bases, _ in f['calls']:
                    cw, cr, cu = self.callee_effects(callee, bases, S)
                    W |= cw
                    R |= cr
                    unk = unk or cu
                S[q][2] = unk
                if (len(W), len(R), unk) != n0:
                    changed = True
        self.summ = S
        self.rounds = rounds

    def callee_effects(self, callee, bases, S=None):
        S = S if S is not None else self.summ
        W, R = set(), set()
        if callee is None:
            return W, R, True
        q = self.resolve(callee)
        if q in S:
            cw, cr, cu = S[q]
        else:
            name = q.split('::')[-1]
            if name in EXTERN:
                cw, cr = EXTERN[name]
                cu = False
            else:
                self.undefined[name] += 1
                return W, R, True
        for l in cw:
            s = subst(l, bases)
            if s:
                W.add(s)
        for l in cr:
            s = subst(l, bases)
            if s:
                R.add(s)
        return W, R, cu

    def arg_effects(self, a, ptags):
        """Whole effects of one argument of a site: its own plus its calls'."""
        W = set(tup(x) for x in a['W'])
        R = set(tup(x) for x in a['R'])
        unk, why = False, []
        for callee, bases, _ in a['calls']:
            cw, cr, cu = self.callee_effects(callee, tup(bases))
            W |= cw
            R |= cr
            if cu:
                unk = True
                why.append(callee or 'a function pointer')
        conv = lambda s: set(filter(None, (self.unparam(l, ptags) for l in s)))
        return conv(W), conv(R), unk, why

    def unparam(self, l, ptags):
        if l[0] != 'P':
            return l
        tag = ptags[l[1]] if l[1] < len(ptags) else '?'
        return ('F', tag, l[2]) if l[2] != '*' else ('M', tag)

    def gtag(self, q):
        g = self.globals.get(q)
        if not g:
            return '?'
        base, ptr, arr = g
        if ptr == 0 or (arr and ptr == 1):
            return base
        return 'ptr'

    def gptag(self, q):
        """What a load through the global pointer (or pointer array) q reads."""
        g = self.globals.get(q)
        if not g:
            return '?'
        base, ptr, arr = g
        depth = ptr - (1 if arr else 0)
        if depth == 1:
            return base
        if depth > 1:
            return 'ptr'
        return '?'


def overlap(a, b, prog):
    ka, kb = a[0], b[0]
    if ka > kb:
        a, b, ka, kb = b, a, kb, ka
    if ka == kb:
        if ka == 'G':
            return a[1] == b[1] and (a[2] is None or b[2] is None or a[2] == b[2])
        if ka in ('L', 'D'):
            return a[1] == b[1]
        if ka == 'F':
            return a[2] == b[2] and (a[1] == b[1] or a[1] in WILD or b[1] in WILD)
        if ka == 'M':
            return a[1] == b[1] or a[1] in WILD or b[1] in WILD
    pair = (ka, kb)
    if pair == ('F', 'G'):
        return prog.gtag(b[1]) == a[1] and (b[2] is None or b[2] == a[2])
    if pair == ('F', 'M'):
        return b[1] == a[1] or b[1] in WILD
    if pair == ('G', 'M'):
        return b[1] in WILD or prog.gtag(a[1]) == b[1]
    if pair == ('D', 'M'):
        # memory reached through the global pointer a[1], and memory reached through some
        # other pointer of pointee type b[1] (a local copy of that pointer, say)
        t = prog.gptag(a[1])
        return b[1] in WILD or t in WILD or t == b[1]
    if pair == ('D', 'F'):
        t = prog.gptag(a[1])
        return t == b[1] or t in WILD
    return False


def conflicts(e1, e2, prog):
    """Locations where order matters between two evaluations e = (W, R, unk)."""
    W1, R1, u1 = e1[:3]
    W2, R2, u2 = e2[:3]
    hits = []
    for a in W1:
        for b in W2 | R2:
            if overlap(a, b, prog):
                hits.append((a, b))
    for a in R1:
        for b in W2:
            if overlap(a, b, prog):
                hits.append((a, b))
    return hits

# ----------------------------------------------------------------------------- orders


def agbcc_order(site):
    n = len(site['args'])
    if site['kind'] != 'call':
        return list(range(n))
    nw = site['nwords']
    if nw == 'struct' or (isinstance(nw, int) and nw > 4):
        withcall = [i for i in range(n) if site['args'][i]['hascall']]
        return withcall + [i for i in range(n) if i not in withcall]
    return list(range(n))


def host_order(site):
    n = len(site['args'])
    if site['kind'] == 'call':
        return list(reversed(range(n)))
    return list(range(n))


def judge(site, prog):
    """Verdict for one site."""
    args = site['args']
    effs = [prog.arg_effects(a, site['ptags']) for a in args]
    site['eff'] = [{'W': sorted(map(list, e[0]), key=repr), 'R': sorted(map(list, e[1]), key=repr), 'unknown': e[2],
                    'why': sorted(set(e[3]))} for e in effs]
    pairs = []
    if site['kind'] == 'call':
        ga, ho = agbcc_order(site), host_order(site)
        pos_g = {a: i for i, a in enumerate(ga)}
        pos_h = {a: i for i, a in enumerate(ho)}
        for i in range(len(args)):
            for j in range(i + 1, len(args)):
                if (pos_g[i] < pos_g[j]) != (pos_h[i] < pos_h[j]):
                    pairs.append((i, j))
        site['order_gba'] = ga
        site['order_pc'] = ho
    elif site['kind'] == 'init':
        # listed for review; the orders are the same as far as tested (no pair differs)
        pairs = []
        site['order_gba'], site['order_pc'] = [0, 1], [0, 1]
        e0, e1 = effs
        if conflicts(e0, e1, prog) or (e0[2] and (e1[0] or e1[1])) or (e1[2] and (e0[0] or e0[1])):
            site['verdict'] = 'unknown'
            site['detail'] = [{'pair': [0, 1], 'unknown': ['initializer order not modelled']}]
            site['pairs'] = [(0, 1)]
            return site
    else:
        # operator, assignment and function-pointer sites are recorded only where the orders
        # differ (FuncAnalyzer.site_bin, site_asg, site_fnptr): parts 0 and 1 in opposite orders
        pairs = [(0, 1)]
    site['pairs'] = pairs
    if not pairs:
        site['verdict'] = 'same-order'
        return site
    worst, detail = 'no-conflict', []
    for i, j in pairs:
        ei, ej = effs[i], effs[j]
        hits = conflicts(ei, ej, prog)
        if hits:
            worst = 'conflict'
            detail.append({'pair': [i, j], 'hits': [[list(x), list(y)] for x, y in hits[:6]]})
        elif (ei[2] and (ej[0] or ej[1] or ej[2])) or (ej[2] and (ei[0] or ei[1])):
            if worst != 'conflict':
                worst = 'unknown'
            detail.append({'pair': [i, j], 'unknown': sorted(set(ei[3] + ej[3]))})
    site['verdict'] = worst
    site['detail'] = detail
    return site


def site_class(site):
    if site['kind'] != 'call':
        return 'C'
    eff = [a for a in site['args'] if a['hascall'] or a['effop']]
    if len(eff) >= 2:
        return 'A'
    if len(eff) == 1 and any(a['R'] or a['calls'] for a in site['args'] if not (a['hascall'] or a['effop'])):
        return 'B'
    return '-'

# ----------------------------------------------------------------------------- objects


class ObjCalls:
    """Call instructions of one object file, with source lines (objdump -d -l -r)."""
    LINE_RE = re.compile(r'^(/[^:]*|[^:\s]+\.[ch]):(\d+)')
    FUNC_RE = re.compile(r'^[0-9a-f]+ <([^>]+)>:')
    CALL_RE = re.compile(r'^\s*([0-9a-f]+):\s.*\b(bl|call|blx)\s+([0-9a-f]+)?\s*<?([^>\s]*)>?')
    RELOC_RE = re.compile(r'^\s*([0-9a-f]+):\s+R_\S+\s+(\S+)')

    def __init__(self, objdump, path):
        self.calls = []          # (func, file, line, addr, target)
        r = subprocess.run([objdump, '-d', '-l', '-r', '--no-show-raw-insn', path], capture_output=True,
                           text=True, errors='replace')
        func, fline, last = None, (None, 0), None
        for ln in r.stdout.split('\n'):
            m = self.FUNC_RE.match(ln)
            if m:
                func = m.group(1)
                continue
            m = self.LINE_RE.match(ln)
            if m:
                fline = (os.path.basename(m.group(1)), int(m.group(2)))
                continue
            m = self.CALL_RE.match(ln)
            if m:
                tgt = m.group(4) or ''
                tgt = re.sub(r'[+-]0x[0-9a-f]+$', '', tgt)
                last = [func, fline[0], fline[1], int(m.group(1), 16), tgt]
                self.calls.append(last)
                continue
            m = self.RELOC_RE.match(ln)
            if m and last is not None and int(m.group(1), 16) - last[3] <= 4:
                sym = m.group(2)
                sym = re.sub(r'[+-]0x[0-9a-f]+$', '', sym)
                if sym not in ('.text',):
                    last[4] = sym
                last = None

    def at(self, func, file, l0, l1):
        base = os.path.basename(file)
        return [c[4] for c in self.calls if c[1] == base and l0 <= c[2] <= l1
                and (func is None or c[0] == func)]


def binary_order(site, objs_g, objs_p, prog):
    """The order of the conflicting pair's calls in the two builds, by callee name."""
    if not site.get('pairs'):
        return
    f, l0, l1 = site['span']
    tu = site['tu']
    fn = site['func'].split('::')[-1]
    out = {}
    for side, cache, root, objdump in (('gba', objs_g[0], objs_g[1], 'arm-none-eabi-objdump'),
                                       ('pc', objs_p[0], objs_p[1], 'objdump')):
        if not root:
            continue
        obj = os.path.join(root, tu[:-2] + '.o')
        if not os.path.exists(obj):
            out[side] = 'no object'
            continue
        if obj not in cache:
            cache[obj] = ObjCalls(objdump, obj)
        seq = cache[obj].at(fn, f, l0, l1)
        res = []
        for i, j in site['pairs']:
            ni = [c for c, _, _ in site['args'][i]['calls'] if c]
            nj = [c for c, _, _ in site['args'][j]['calls'] if c]
            ni = [x.split('::')[-1] for x in ni]
            nj = [x.split('::')[-1] for x in nj]
            only_i = [x for x in ni if x not in nj]
            only_j = [x for x in nj if x not in ni]
            if not only_i or not only_j:
                res.append('?')
                continue
            pi = [k for k, c in enumerate(seq) if c in only_i]
            pj = [k for k, c in enumerate(seq) if c in only_j]
            if not pi or not pj:
                res.append('?')
                continue
            res.append('%d<%d' % (i, j) if min(pi) < min(pj) else '%d<%d' % (j, i))
        out[side] = res
    site['binary'] = out

# ----------------------------------------------------------------------------- main


def site_key(s):
    """A key that survives line moves: file, function and a hash of the expression."""
    import hashlib
    func = s['func'].split('::')[-1]
    t = re.sub(r'\s+', '', s['text'])
    h = hashlib.sha1(('%s|%s|%s' % (s['kind'], func, t)).encode()).hexdigest()[:8]
    return '%s:%s:%s' % (s['file'], func, h)


def load_accept(path):
    """The accept list: one judged site per line, `KEY VERDICT reason`, where VERDICT is
    `benign` (the order cannot change behaviour: why) or `fixed` (sequenced in agbcc's
    order under PC_PORT: the DIVERGENCES row). '#' starts a comment."""
    acc = {}
    if not path or not os.path.exists(path):
        return acc
    for ln in open(path):
        ln = ln.split('#', 1)[0].strip()
        if not ln:
            continue
        parts = ln.split(None, 2)
        if len(parts) >= 2:
            acc[parts[0]] = (parts[1], parts[2] if len(parts) > 2 else '')
    return acc


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', default='.')
    ap.add_argument('--view', choices=('gba', 'pc'), default='gba')
    ap.add_argument('--files', nargs='*')
    ap.add_argument('-j', type=int, default=8)
    ap.add_argument('--json')
    ap.add_argument('--md')
    ap.add_argument('--gba-objs')
    ap.add_argument('--pc-objs')
    ap.add_argument('--show', choices=('all', 'flagged'), default='flagged')
    ap.add_argument('--accept')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--func', action='append', default=[], help='print the summary of this function')
    ap.add_argument('--site', action='append', default=[], help='print every site at FILE:LINE')
    ap.add_argument('--selftest', action='store_true',
                    help='check the order model against port/tools/argorder_cases.c (the compilers\' real orders)')
    a = ap.parse_args()
    root = os.path.abspath(a.root)
    if a.selftest:
        sys.exit(selftest(root))
    files = a.files
    if not files:
        files = sorted('src/' + x for x in os.listdir(os.path.join(root, 'src')) if x.endswith('.c'))
        files = [x for x in files if x != 'src/msg_data.c']
    jobs = [(root, f, a.view) for f in files]
    with Pool(a.j) as pool:
        results = pool.map(analyse_file, jobs, chunksize=1)
    nerr = 0
    for r in results:
        if r.get('error'):
            print('ERROR %s: %s' % (r['file'], r['error'][:300]), file=sys.stderr)
            nerr += 1
        elif r.get('nerrors'):
            nerr += r['nerrors']
            for e in r['errors'][:5]:
                print('parse %s: %s' % (r['file'], e), file=sys.stderr)
    prog = Program(results)
    prog.summarize()
    sites = []
    ncalls = nops = 0
    seen_sites = set()
    for r in results:
        for s in r['sites']:
            # a header's inline function is in every unit that includes it: judge it once
            ident = (s['file'], s['line'], s['col'], s['kind'], s['text'])
            if ident in seen_sites:
                continue
            seen_sites.add(ident)
            if s['kind'] == 'call':
                ncalls += 1
            else:
                nops += 1
            s['tu'] = r['file']
            s['cls'] = site_class(s)
            if s['cls'] == '-':
                continue
            judge(s, prog)
            sites.append(s)
    objs_g = ({}, os.path.join(root, a.gba_objs) if a.gba_objs else None)
    objs_p = ({}, os.path.join(root, a.pc_objs) if a.pc_objs else None)
    if a.gba_objs or a.pc_objs:
        for s in sites:
            if s['verdict'] in ('conflict', 'unknown') or (s['cls'] == 'A' and s['kind'] == 'call'):
                binary_order(s, objs_g, objs_p, prog)
    acc = load_accept(a.accept)
    counts = collections.Counter((s['cls'], s['verdict']) for s in sites)
    nfun = len(prog.funcs)
    unk_funcs = sum(1 for v in prog.summ.values() if v[2])
    print('argorder_sweep: view %s, %d files, %d functions (%d whose effects pass through a call it '
          'cannot follow), %d parse errors, summary rounds %d' % (a.view, len(files), nfun, unk_funcs, nerr,
                                                                 prog.rounds))
    ncand = sum(1 for s in sites if s['kind'] == 'call')
    print('  %d calls with two or more arguments, %d of them candidates (class A or B); %d operator and '
          'assignment sites where the orders differ (class C)' % (ncalls, ncand, nops))
    for cls in ('A', 'B', 'C'):
        row = ', '.join('%s %d' % (v, counts[(cls, v)]) for v in ('conflict', 'unknown', 'no-conflict', 'same-order')
                        if counts[(cls, v)])
        print('  class %s: %s' % (cls, row or 'none'))
    open_, missing_fix = [], []
    seen = set()
    for s in sorted(sites, key=lambda s: (s['file'], s['line'], s['col'])):
        s['key'] = site_key(s)
        s['accepted'] = acc.get(s['key'])
        flagged = s['verdict'] in ('conflict', 'unknown')
        if not flagged:
            continue
        seen.add(s['key'])
        if not s['accepted']:
            open_.append(s)
        elif a.view == 'pc' and s['accepted'][0] == 'fixed':
            missing_fix.append(s)
    stale = sorted(k for k in acc if k not in seen and a.view == 'gba')
    for name in a.func:
        for q in prog.by_name.get(name, [name]):
            sm = prog.summ.get(q)
            if not sm:
                print('func %s: no summary' % q)
                continue
            print('func %s: unknown=%s\n  W: %s\n  R: %s' % (q, sm[2], ' '.join(sorted(fmtloc(x) for x in sm[0])),
                                                          ' '.join(sorted(fmtloc(x) for x in sm[1]))))
    for want in a.site:
        for s in sites:
            if '%s:%d' % (s['file'], s['line']) == want:
                print('site %s %s %s verdict %s orders %s / %s binary %s' % (
                    want, s['cls'], s['text'], s['verdict'], s.get('order_gba'), s.get('order_pc'),
                    s.get('binary')))
                for i, (arg, e) in enumerate(zip(s['args'], s['eff'])):
                    print('  arg %d %s: calls %s unknown %s (%s)\n    W: %s\n    R: %s' % (
                        i, arg['text'][:80], [c for c, _, _ in arg['calls']], e['unknown'], ','.join(e['why'][:5]),
                        ' '.join(fmtloc(x) for x in e['W'][:40]), ' '.join(fmtloc(x) for x in e['R'][:40])))
                for d in s.get('detail', []):
                    print('  detail', d)
    if a.json:
        with open(a.json, 'w') as fh:
            json.dump({'view': a.view, 'files': len(files), 'functions': nfun, 'parse_errors': nerr,
                       'undefined_callees': prog.undefined.most_common(), 'sites': sites}, fh, indent=1,
                      default=list)
    if a.md:
        write_md(a.md, sites, a.show)
    nacc = sum(1 for s in sites if s.get('accepted') and s['verdict'] in ('conflict', 'unknown'))
    print('  flagged: %d (%d in the accept list), not accepted: %d' % (len(seen), nacc, len(open_)))
    for s in open_[:400]:
        print('  %-9s %s %s:%d %s: %s\n            key %s' % (
            s['verdict'], s['cls'], s['file'], s['line'], s['func'].split('::')[-1], s['text'][:150], s['key']))
    for s in missing_fix:
        print('  FIX MISSING: %s:%d %s is accepted as fixed but this build still evaluates it in the host\'s '
              'order (key %s)' % (s['file'], s['line'], s['text'][:120], s['key']))
    for k in stale:
        print('  stale accept entry (no flagged site has this key): %s' % k)
    if a.check and (open_ or missing_fix or stale):
        sys.exit(1)


def selftest(root):
    """Each case in port/tools/argorder_cases.c holds one expression and the order agbcc and
    the host GCC really took (read from their output); the model must predict both."""
    path = 'port/tools/argorder_cases.c'
    exp_re = re.compile(r'^\w+ (\w+)\(.*/\* expect (none|(\w+) ([\d,]+) / ([\d,]+)) \*/')
    expect = {}
    for ln in open(os.path.join(root, path)):
        m = exp_re.match(ln)
        if m:
            expect[m.group(1)] = None if m.group(2) == 'none' else (
                m.group(3), [int(x) for x in m.group(4).split(',')], [int(x) for x in m.group(5).split(',')])
    r = analyse_file((root, path, 'gba'))
    if r.get('error') or r.get('nerrors'):
        print('selftest: %s does not parse: %s' % (path, r.get('error') or r.get('errors')))
        return 1
    fails = 0
    for fn, want in sorted(expect.items()):
        sites = [s for s in r['sites'] if s['func'].split('::')[-1] == fn]
        if want is None:
            got = [s for s in sites if s['kind'] != 'call']
            ok = not got
            desc = 'none' if ok else ', '.join('%s %s / %s' % (s['kind'], s['order_gba'], s['order_pc']) for s in got)
        else:
            kind, g, p = want
            cand = [s for s in sites if s['kind'] == kind]
            if kind == 'call':
                cand = [s for s in cand if s['callee'].split('::')[-1].startswith('f')]
                for s in cand:
                    s['order_gba'], s['order_pc'] = agbcc_order(s), host_order(s)
            ok = any(s['order_gba'] == g and s['order_pc'] == p for s in cand)
            desc = ', '.join('%s %s / %s' % (s['kind'], s['order_gba'], s['order_pc']) for s in cand) or 'no site'
        print('  %-4s %-5s expected %-28s got %s' % ('ok' if ok else 'FAIL', fn,
                                                    'none' if want is None else '%s %s / %s' % want, desc))
        fails += 0 if ok else 1
    print('selftest: %d cases, %d failed' % (len(expect), fails))
    return 1 if fails or not expect else 0


def write_md(path, sites, show):
    with open(path, 'w') as fh:
        fh.write('| class | verdict | site | function | expression | orders (GBA / PC) | binary | why |\n')
        fh.write('|---|---|---|---|---|---|---|---|\n')
        for s in sorted(sites, key=lambda s: (s['file'], s['line'], s['col'])):
            if show != 'all' and s['verdict'] not in ('conflict', 'unknown'):
                continue
            why = []
            for d in s.get('detail', []):
                if 'hits' in d:
                    why.append('args %d/%d: %s' % (d['pair'][0], d['pair'][1],
                                                   '; '.join('%s~%s' % (fmtloc(x), fmtloc(y)) for x, y in d['hits'][:3])))
                else:
                    why.append('args %d/%d: cannot follow %s' % (d['pair'][0], d['pair'][1], ', '.join(d['unknown'][:4])))
            fh.write('| %s | %s | `%s:%d` | `%s` | `%s` | %s / %s | %s | %s |\n' % (
                s['cls'], s['verdict'], s['file'], s['line'], s['func'].split('::')[-1],
                s['text'].replace('|', '\\|')[:160], s.get('order_gba'), s.get('order_pc'),
                s.get('binary', ''), '<br>'.join(why).replace('|', '\\|')))


def fmtloc(l):
    l = list(l)
    if l[0] == 'G':
        return l[1].split('::')[-1] + ('.' + l[2] if l[2] else '')
    if l[0] == 'F':
        return '(%s)->%s' % (l[1], l[2])
    if l[0] == 'M':
        return '*(%s)' % l[1]
    if l[0] == 'D':
        return '*%s' % l[1].split('::')[-1]
    if l[0] == 'L':
        return '&' + l[1]
    return str(l)


if __name__ == '__main__':
    main()
