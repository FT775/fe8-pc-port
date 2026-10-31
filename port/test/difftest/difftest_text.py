#!/usr/bin/env python3
"""Difftest: port/asm/arm_pc.c DrawGlyph and DecodeString against the ARM code.

Runs the decomp's own ARM routines (asm/arm.s, read from the built
fireemblem8.elf and copied to IWRAM the way StoreRoutinesToIRAM does) in
Unicorn, and the port's C versions (port/asm/arm_pc.c, compiled with
port/Makefile's PORT_CFLAGS) in text_harness.c. Both worlds see the same
memory at the same addresses: the ELF's ROM bytes at 0x08000000 and an EWRAM
block at 0x02000000. Both run the same list of operations (memory writes,
calls, dumps, CRCs), and every dump and CRC must match byte for byte.

Run from the repository root, after the GBA `make` (fireemblem8.elf):

    python3 port/test/difftest/difftest_text.py

Output goes to build/difftest (ignored by git). The ELF is only read.
"""

import argparse
import os
import random
import struct
import subprocess
import sys
import zlib

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_MEM_WRITE
from unicorn import arm_const as A

EWRAM_BASE = 0x02000000
EWRAM_SIZE = 0x00040000
IWRAM_BASE = 0x03000000
IWRAM_SIZE = 0x00008000
ROM_BASE = 0x08000000
ROM_SIZE = 0x01000000

ARENA = EWRAM_BASE          # every buffer the cases use is in here
ARENA_SIZE = 0x10000        # CRC'd after every call
STACK_TOP = 0x03007F00      # GBA user stack top
STACK_LO = 0x03007000
STOP_ADDR = IWRAM_BASE + 0x100

OP_END, OP_WRITE, OP_DRAWGLYPH, OP_DECODE, OP_DUMP, OP_CRC = range(6)


# --------------------------------------------------------------------------
# ELF (32-bit little-endian), just enough for segments and symbols.

def read_elf(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        sys.exit(f'{path}: not a 32-bit little-endian ELF')
    e_phoff, e_shoff = struct.unpack_from('<II', data, 0x1C)
    e_phentsize, e_phnum, e_shentsize, e_shnum = struct.unpack_from('<HHHH', data, 0x2A)
    rom = None
    for i in range(e_phnum):
        p_type, p_offset, _vaddr, p_paddr, p_filesz = struct.unpack_from('<5I', data, e_phoff + i * e_phentsize)
        if p_type == 1 and p_filesz and p_paddr == ROM_BASE:
            rom = data[p_offset:p_offset + p_filesz]
    if rom is None:
        sys.exit(f'{path}: no ROM segment at 0x{ROM_BASE:08X}')
    secs = [struct.unpack_from('<10I', data, e_shoff + i * e_shentsize) for i in range(e_shnum)]
    syms = {}
    for sec in secs:
        if sec[1] != 2:  # SHT_SYMTAB
            continue
        stroff = secs[sec[6]][4]
        for off in range(sec[4], sec[4] + sec[5], 16):
            st_name, st_value, st_size = struct.unpack_from('<III', data, off)
            end = data.index(b'\0', stroff + st_name)
            name = data[stroff + st_name:end].decode('latin-1')
            if name and name not in syms:
                syms[name] = (st_value, st_size)
    return rom, syms


# --------------------------------------------------------------------------
# Operation list, run by both worlds.

class Ops:
    def __init__(self):
        self.items = []

    def write(self, addr, data):
        self.items.append((OP_WRITE, addr, bytes(data)))

    def drawglyph(self, pal, dst, src, subx, label):
        self.items.append((OP_DRAWGLYPH, pal, dst, src, subx, label))

    def decode(self, src, dst, label):
        self.items.append((OP_DECODE, src, dst, label))

    def dump(self, addr, n):
        self.items.append((OP_DUMP, addr, n))

    def crc(self, addr, n):
        self.items.append((OP_CRC, addr, n))

    def serialize(self):
        out = bytearray()
        for it in self.items:
            op = it[0]
            if op == OP_WRITE:
                out += struct.pack('<III', op, it[1], len(it[2])) + it[2]
                out += b'\0' * (-len(it[2]) & 3)
            elif op == OP_DRAWGLYPH:
                out += struct.pack('<IIIII', op, it[1], it[2], it[3], it[4] & 0xFFFFFFFF)
            elif op == OP_DECODE:
                out += struct.pack('<III', op, it[1], it[2])
            else:
                out += struct.pack('<III', op, it[1], it[2])
        out += struct.pack('<I', OP_END)
        return bytes(out)


# --------------------------------------------------------------------------
# The ARM side.

class ArmWorld:
    def __init__(self, rom, syms):
        mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        mu.ctl_set_cpu_model(A.UC_CPU_ARM_926)   # ARMv5; arm.s uses only ARMv4 instructions
        mu.mem_map(EWRAM_BASE, EWRAM_SIZE)
        mu.mem_map(IWRAM_BASE, IWRAM_SIZE)
        mu.mem_map(ROM_BASE, ROM_SIZE)
        mu.mem_write(ROM_BASE, rom)
        # StoreRoutinesToIRAM (src/ramfunc.c): copy ARMCodeToCopy_Start..End to
        # RAMFuncArea and call the routines there.
        start = syms['ARMCodeToCopy_Start'][0]
        end = syms['ARMCodeToCopy_End'][0]
        dest = syms['RAMFuncArea'][0]
        mu.mem_write(dest, bytes(mu.mem_read(start, end - start)))
        self.fn = {n: dest + (syms[n][0] - start) for n in ('DrawGlyph', 'DecodeString')}
        self.mu = mu
        self.writes = []
        self.rng = random.Random(1234)
        mu.hook_add(UC_HOOK_MEM_WRITE, self._on_write)

    def _on_write(self, uc, access, addr, size, value, user):
        if not (STACK_LO <= addr < STACK_TOP):
            self.writes.append((addr, size))

    def call(self, fn, args):
        mu = self.mu
        regs = [A.UC_ARM_REG_R0, A.UC_ARM_REG_R1, A.UC_ARM_REG_R2, A.UC_ARM_REG_R3]
        # Unused argument registers and r4-r12 get garbage: the routines must not depend on them.
        for r in regs + [A.UC_ARM_REG_R4, A.UC_ARM_REG_R5, A.UC_ARM_REG_R6, A.UC_ARM_REG_R7,
                         A.UC_ARM_REG_R8, A.UC_ARM_REG_R9, A.UC_ARM_REG_R10, A.UC_ARM_REG_R11,
                         A.UC_ARM_REG_R12]:
            mu.reg_write(r, self.rng.getrandbits(32))
        for r, v in zip(regs, args):
            mu.reg_write(r, v & 0xFFFFFFFF)
        mu.reg_write(A.UC_ARM_REG_SP, STACK_TOP)
        mu.reg_write(A.UC_ARM_REG_LR, STOP_ADDR)
        self.writes = []
        mu.emu_start(self.fn[fn], STOP_ADDR, count=5_000_000)
        pc = mu.reg_read(A.UC_ARM_REG_PC)
        if pc != STOP_ADDR or mu.reg_read(A.UC_ARM_REG_SP) != STACK_TOP:
            raise RuntimeError(f'{fn} did not return (pc 0x{pc:08X})')
        return self.writes

    def run(self, ops):
        outs = []
        stray = []
        for it in ops.items:
            op = it[0]
            if op == OP_WRITE:
                self.mu.mem_write(it[1], it[2])
            elif op == OP_DRAWGLYPH:
                w = self.call('DrawGlyph', it[1:5])
                stray += [(it[5], a) for a, s in w if not (ARENA <= a and a + s <= ARENA + ARENA_SIZE)]
            elif op == OP_DECODE:
                w = self.call('DecodeString', it[1:3])
                stray += [(it[3], a) for a, s in w if not (ARENA <= a and a + s <= ARENA + ARENA_SIZE)]
            elif op == OP_DUMP:
                outs.append(bytes(self.mu.mem_read(it[1], it[2])))
            elif op == OP_CRC:
                outs.append(struct.pack('<I', zlib.crc32(bytes(self.mu.mem_read(it[1], it[2])))))
        return outs, stray


# --------------------------------------------------------------------------
# The C side.

def make_var(root, var):
    env = dict(os.environ, MAKEFLAGS='')
    r = subprocess.run(['make', '-f', 'port/Makefile', '-pn', '.difftest-none'], cwd=root,
                       capture_output=True, text=True, env=env)
    for line in r.stdout.splitlines():
        if line.startswith(var + ' := '):
            return line[len(var) + 4:].split()
    sys.exit(f'cannot read {var} from port/Makefile')


def build_harness(root, out, syms, cc, c_source):
    port_cflags = make_var(root, 'PORT_CFLAGS')
    obj = os.path.join(out, 'arm_pc.o')
    # The same command as port/Makefile's rule for port/%.o.
    run([cc] + port_cflags + ['-DPC_PORT=1', '-Iinclude', '-Iport/include',
                              '-c', c_source, '-o', obj], root)
    harness_o = os.path.join(out, 'text_harness.o')
    run([cc, '-m32', '-O2', '-Wall', '-fno-pie', '-c', 'port/test/difftest/text_harness.c',
         '-o', harness_o], root)
    # Game variables arm_pc.c refers to for its other routines (not called here).
    undef = subprocess.run(['nm', '-u', obj], cwd=root, capture_output=True, text=True).stdout.split()
    bound = {'gMsgHuffmanTable', 'gMsgHuffmanTableRoot'}
    dummies = sorted(s for s in undef if s.startswith('g') and s[1:2].isupper() and s not in bound)
    dummy_c = os.path.join(out, 'dummies.c')
    with open(dummy_c, 'w') as f:
        f.write('/* generated by difftest_text.py */\n')
        for s in dummies:
            f.write(f'char {s}[0x1000] __attribute__((aligned(16)));\n')
    dummy_o = os.path.join(out, 'dummies.o')
    run([cc, '-m32', '-fno-pie', '-c', dummy_c, '-o', dummy_o], root)
    exe = os.path.join(out, 'text_harness')
    defsyms = [f'-Wl,--defsym,{s}=0x{syms[s][0]:08X}' for s in sorted(bound)]
    run([cc, '-m32', '-no-pie', '-Wl,-Ttext-segment=0x10000000', harness_o, obj, dummy_o]
        + defsyms + ['-o', exe], root)
    return exe, port_cflags


def run(cmd, cwd):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(' '.join(cmd) + '\n' + r.stdout + r.stderr)
        sys.exit('build failed')


# --------------------------------------------------------------------------
# A Python model of DecodeString, used only to choose fuzz cases that end
# inside their buffers (so neither side runs off). It is not compared.

class Shadow:
    def __init__(self, rom):
        self.ew = bytearray(EWRAM_SIZE)
        self.rom = bytearray(ROM_SIZE)
        self.rom[:len(rom)] = rom

    def _loc(self, a):
        if EWRAM_BASE <= a < EWRAM_BASE + EWRAM_SIZE:
            return self.ew, a - EWRAM_BASE
        if ROM_BASE <= a < ROM_BASE + ROM_SIZE:
            return self.rom, a - ROM_BASE
        return None, None

    def write(self, a, data):
        buf, off = self._loc(a)
        buf[off:off + len(data)] = data

    def rd(self, a, n):
        buf, off = self._loc(a)
        if buf is None or off + n > len(buf):
            return None
        return int.from_bytes(buf[off:off + n], 'little')


def model_decode(mem, table, root, src, dst, in_rng, out_rng, stats):
    over = {}

    def rd8(a):
        if a in over:
            stats['read_own_output'] += 1
            return over[a]
        return mem.rd(a, 1)

    bits = left = 0
    s, d = src, dst
    while True:
        node = root
        while True:
            left -= 1
            if left < 0:
                if not (in_rng[0] <= s < in_rng[1]):
                    return None
                bits = rd8(s)
                s += 1
                left = 7
            idx = mem.rd(node + 2 if bits & 1 else node, 2)
            if idx is None:
                return None
            if idx >= 0x4000:
                stats['index_ge_0x4000'] += 1
                if idx >= 0x8000:
                    stats['index_ge_0x8000'] += 1
            node = (table + idx * 4) & 0xFFFFFFFF
            bits >>= 1
            word = mem.rd(node, 4)
            if word is None:
                return None
            if word & 0x80000000:
                break
        if word & 0x7FFF0000:
            stats['leaf_bits16_30'] += 1
            if not word & 0xFFFF:
                stats['term_bits16_30'] += 1
        if word & 0xFF00:
            if not (out_rng[0] <= d and d + 2 <= out_rng[1]):
                return None
            if not word & 0xFF:
                stats['two_byte_low_zero'] += 1
            stats['two_byte'] += 1
            over[d] = word & 0xFF
            over[d + 1] = (word >> 8) & 0xFF
            d += 2
            continue
        if not (out_rng[0] <= d < out_rng[1]):
            return None
        over[d] = word & 0xFF
        if not word & 0xFF:
            break
        d += 1
    for a, v in over.items():
        mem.write(a, bytes([v]))
    return bytes(over[a] for a in range(dst, d + 1) if a in over), s - src


# --------------------------------------------------------------------------
# Cases.

def u32(rom, addr):
    return struct.unpack_from('<I', rom, addr - ROM_BASE)[0]


class Cases:
    def __init__(self, rom, syms, seed):
        self.rom = rom
        self.syms = syms
        self.rng = random.Random(seed)
        self.ops = Ops()
        self.mem = Shadow(rom)
        self.count = {}
        self.stats = {k: 0 for k in ('read_own_output', 'leaf_bits16_30', 'term_bits16_30', 'two_byte',
                                     'two_byte_low_zero', 'index_ge_0x4000', 'index_ge_0x8000', 'inplace',
                                     'dg_hi_word', 'dg_pal_2mod4', 'dg_overlap', 'dg_subx_rom')}
        self.decoded = {}
        self.far = {}
        self.far_touched = set()
        self.table = syms['gMsgHuffmanTable'][0]
        self.table_size = syms['gMsgHuffmanTable'][1]
        self.root_sym = syms['gMsgHuffmanTableRoot'][0]

    def write(self, addr, data):
        self.ops.write(addr, data)
        self.mem.write(addr, data)

    def rand_bytes(self, n):
        return self.rng.randbytes(n)

    def after_call(self, dump_addr, dump_len):
        self.ops.dump(dump_addr, dump_len)
        self.ops.crc(ARENA, ARENA_SIZE)

    def tally(self, cat):
        self.count[cat] = self.count.get(cat, 0) + 1

    # -- DecodeString --------------------------------------------------------

    def decode_real_messages(self):
        tab = self.syms['gMsgTable']
        n = tab[1] // 4
        root = u32(self.rom, self.root_sym)
        out = ARENA + 0xC000                    # the longest message decodes to 4,000 bytes
        for i in range(n):
            src = u32(self.rom, tab[0] + 4 * i)
            if not ROM_BASE <= src < ROM_BASE + len(self.rom):
                sys.exit(f'gMsgTable[0x{i:04X}] = 0x{src:08X} is not in the ROM')
            dst = out + (i & 3)
            self.write(out, self.rand_bytes(0x1000))
            res = model_decode(self.mem, self.table, root, src, dst, (ROM_BASE, ROM_BASE + len(self.rom)),
                               (out, out + 0x1000), self.stats)
            if res is None:
                sys.exit(f'model: message 0x{i:04X} does not fit the 0x1000 buffer')
            self.decoded[i] = res[0]
            self.ops.decode(src, dst, f'decode-real msg 0x{i:04X}')
            self.tally('decode-real')
            self.after_call(out, 0x1000)

    def decode_input_case(self, cat, label, table, root, data, inplace):
        """data: encoded bytes. Returns False if the model says it runs off."""
        if inplace:
            region = ARENA + 0xA000
            size = 0x800
            src = region + 0x200 + self.rng.randrange(4)
            dst = src + self.rng.randint(-24, 24)
            fill = bytearray(self.rand_bytes(size))
            fill[src - region:src - region + len(data)] = data
            fill = bytes(fill[:size])
            in_rng = out_rng = (region, region + size)
        else:
            src = ARENA + 0x4000 + self.rng.randrange(4)
            dst = ARENA + 0x8000 + self.rng.randrange(4)
            in_rng = (src, src + len(data))
            out_rng = (ARENA + 0x8000, ARENA + 0x9000)
        # Try the case on a copy of the shadow first; keep it only if it ends inside its buffers.
        snap_ew = bytes(self.mem.ew)
        self.mem.write(region, fill) if inplace else self.mem.write(src, data)
        trial = {k: 0 for k in self.stats}
        res = model_decode(self.mem, table, root, src, dst, in_rng, out_rng, trial)
        self.mem.ew[:] = snap_ew
        if res is None:
            return False
        for k, v in trial.items():
            self.stats[k] += v
        # Accepted: emit the writes for both worlds, then the call.
        if inplace:
            self.write(region, fill)
            self.stats['inplace'] += 1
        else:
            self.write(src, data)
            self.write(ARENA + 0x8000, self.rand_bytes(0x1000))
        model_decode(self.mem, table, root, src, dst, in_rng, out_rng, dict(trial))
        self.ops.decode(src, dst, f'{cat} {label}')
        self.tally(cat)
        if inplace:
            self.after_call(region, size)
        else:
            self.after_call(ARENA + 0x8000, 0x1000)
        return True

    def decode_real_table_random(self, n):
        root = u32(self.rom, self.root_sym)
        done = tries = 0
        while done < n:
            tries += 1
            data = self.rand_bytes(self.rng.choice([16, 64, 256, 1024]))
            inplace = self.rng.random() < 0.2
            if self.decode_input_case('decode-realtable-random', f'case {done}', self.table, root, data, inplace):
                done += 1
            if tries > n * 50:
                sys.exit('too few random inputs end inside their buffers')

    def fuzz_leaf(self):
        r = self.rng.random()
        if r < 0.45:
            v = self.rng.randrange(1, 0x100)
        elif r < 0.65:
            v = self.rng.randrange(1, 0x100) << 8              # two bytes, low byte 0
        elif r < 0.9:
            v = self.rng.randrange(0x100, 0x10000)
        else:
            v = self.rng.choice([0x00FF, 0xFF00, 0xFFFF, 0x0100, 0x0001, 0x8080])
        if self.rng.random() < 0.2:
            v |= self.rng.getrandbits(15) << 16                 # bits 16-30 are ignored
        return 0x80000000 | v

    def fuzz_term(self):
        v = 0x80000000
        if self.rng.random() < 0.3:
            v |= (self.rng.getrandbits(15) or 1) << 16
        return v

    def resync(self):
        """Rewrite all of EWRAM in both worlds, so a difference in one section
        does not show up again in the CRCs of the next."""
        self.write(EWRAM_BASE, self.rand_bytes(EWRAM_SIZE))

    def build_tree_table(self):
        nleaves = self.rng.choice([2, 3, 5, 17, 64, 200, 600])
        leaves = [('L', self.fuzz_term())] + [('L', self.fuzz_leaf()) for _ in range(nleaves - 1)]
        if self.rng.random() < 0.3:
            leaves.append(('L', self.fuzz_term()))
        pool = list(range(len(leaves)))
        nodes = list(leaves)
        while len(pool) > 1:
            if self.rng.random() < 0.3:
                a = pool.pop()                                  # deepen: reuse the newest node
            else:
                a = pool.pop(self.rng.randrange(len(pool)))
            b = pool.pop(self.rng.randrange(len(pool)))
            if self.rng.random() < 0.5:
                a, b = b, a
            nodes.append(('I', a, b))
            pool.append(len(nodes) - 1)
        root_node = pool[0]
        max_words = self.table_size // 4
        size = min(max_words, len(nodes) + self.rng.randrange(0, 40))
        pos = list(range(size))
        self.rng.shuffle(pos)
        # Some tables put a few nodes at far indices (past the real table, in
        # the ROM after it): 0x4000-0x7FFF for a right child (that index is
        # bits 16-31 of its parent, so bit 31 must stay clear, while bit 30 is
        # set), 0x4000-0xFFFF for a left child (a full u16).
        far = {}
        if self.rng.random() < 0.4:
            right = {nd[2] for nd in nodes if nd[0] == 'I'}
            for k in range(len(nodes)):
                if k == root_node or self.rng.random() > 0.08:
                    continue
                hi = 0x8000 if k in right else 0x10000
                p = self.rng.randrange(0x4000, hi)
                while p in far:
                    p = self.rng.randrange(0x4000, hi)
                far[p] = None
                pos[k] = p
        words = [self.rng.getrandbits(32) for _ in range(size)]
        for k, nd in enumerate(nodes):
            w = nd[1] if nd[0] == 'L' else pos[nd[1]] | (pos[nd[2]] << 16)
            if pos[k] < size:
                words[pos[k]] = w
            else:
                far[pos[k]] = w
        self.far = far
        # Codes (bit lists from the root, 0 = halfword +0, 1 = halfword +2).
        codes = {}

        def walk(k, path):
            nd = nodes[k]
            if nd[0] == 'L':
                codes[k] = path
            else:
                walk(nd[1], path + [0])
                walk(nd[2], path + [1])
        walk(root_node, [])
        return words, pos[root_node], codes, nodes

    def build_graph_table(self):
        size = self.rng.choice([4, 16, 64, 300])
        words = []
        for _ in range(size):
            if self.rng.random() < 0.4:
                words.append(self.fuzz_term() if self.rng.random() < 0.3 else self.fuzz_leaf())
            else:
                words.append(self.rng.randrange(size) | (self.rng.randrange(size) << 16))
        root = self.rng.randrange(size)
        words[root] = self.rng.randrange(size) | (self.rng.randrange(size) << 16)
        self.far = {}
        return words, root

    def install_table(self, words, root_idx):
        self.write(self.table, b''.join(struct.pack('<I', w) for w in words))
        for p, w in self.far.items():
            self.write(self.table + 4 * p, struct.pack('<I', w))
            self.far_touched.add(self.table + 4 * p)
        self.write(self.root_sym, struct.pack('<I', self.table + 4 * root_idx))
        return self.table + 4 * root_idx

    def decode_fuzz_tables(self, ntables, per_table):
        for t in range(ntables):
            kind = 'tree' if t % 3 else 'graph'
            if kind == 'tree':
                words, root_idx, codes, nodes = self.build_tree_table()
            else:
                words, root_idx = self.build_graph_table()
            root = self.install_table(words, root_idx)
            done = tries = 0
            while done < per_table and tries < per_table * 40:
                tries += 1
                inplace = self.rng.random() < 0.2
                if kind == 'tree' and self.rng.random() < 0.7:
                    plain = [k for k in codes if (nodes[k][1] & 0xFFFF) != 0]
                    terms = [k for k in codes if (nodes[k][1] & 0xFFFF) == 0]
                    seq = [self.rng.choice(plain) for _ in range(self.rng.choice([0, 1, 5, 40, 200]))] if plain else []
                    seq.append(self.rng.choice(terms))
                    bitlist = [b for k in seq for b in codes[k]]
                    bitlist += [self.rng.getrandbits(1) for _ in range(self.rng.randrange(0, 24))]
                    data = bytearray((len(bitlist) + 7) // 8)
                    for i, b in enumerate(bitlist):
                        data[i >> 3] |= b << (i & 7)
                    if len(bitlist) & 7 and self.rng.random() < 0.5:
                        data[-1] |= self.rng.getrandbits(8) & (0xFF << (len(bitlist) & 7)) & 0xFF
                    data = bytes(data)
                    if len(data) > 0x600:
                        continue
                else:
                    data = self.rand_bytes(self.rng.choice([8, 64, 512]))
                if self.decode_input_case(f'decode-fuzz-{kind}', f'table {t} case {done}', self.table, root,
                                          data, inplace):
                    done += 1
        # Put the real table (and the ROM words the far nodes used) back.
        for a in sorted(self.far_touched) + [self.root_sym]:
            self.write(a, self.rom[a - ROM_BASE:a - ROM_BASE + 4])
        lo = self.table - ROM_BASE
        self.write(self.table, self.rom[lo:lo + self.table_size])

    # -- DrawGlyph -----------------------------------------------------------

    def luts(self):
        tab = self.syms['s2bppTo4bppLutTable']
        return [u32(self.rom, tab[0] + 4 * i) for i in range(tab[1] // 4)]

    def font(self, name):
        base, size = self.syms[name]
        return [u32(self.rom, base + 4 * i) for i in range(size // 4)]

    def note_hi(self, src, subx):
        if subx and 0 <= subx < 8:
            for i in range(16):
                w = self.mem.rd(src + 4 * i, 4)
                if w is not None and (w >> (32 - 2 * subx)):
                    self.stats['dg_hi_word'] += 1
                    return

    def drawglyph_real(self):
        luts = self.luts()
        dst = ARENA + 0x2000
        n = 0
        for fname in ('TextGlyphs_System', 'TextGlyphs_Talk', 'TextGlyphs_Special'):
            for ch, gp in enumerate(self.font(fname)):
                if gp == 0:
                    continue
                for subx in range(8):
                    lut = luts[(ch + subx + n) % len(luts)]
                    pre = b'\0' * 0x180 if (n + subx) % 2 == 0 else self.rand_bytes(0x180)
                    self.write(dst - 0x40, pre)
                    self.note_hi(gp + 8, subx)
                    self.ops.drawglyph(lut, dst, gp + 8, subx, f'drawglyph-real {fname}[0x{ch:02X}] subx {subx}')
                    self.tally('drawglyph-real')
                    self.after_call(dst - 0x40, 0x180)
                n += 1

    def drawglyph_lines(self, nmsgs):
        luts = self.luts()
        talk = self.font('TextGlyphs_Talk')
        base = ARENA + 0x3000
        ids = sorted(self.decoded)[::max(1, len(self.decoded) // nmsgs)][:nmsgs]
        for m in ids:
            text = self.decoded[m]
            self.write(base, b'\0' * 0x900)
            lut = luts[m % len(luts)]
            x = i = 0
            drawn = 0
            while i < len(text) and x < 200:
                c = text[i]
                i += 1
                if c == 0:
                    break
                if c < 0x20:
                    i += 2 if c == 0x10 else 0                  # [LoadFace] arguments
                    continue
                if c == 0x80:
                    i += 1                                      # two-byte control code
                    continue
                gp = talk[c] or talk[ord('?')]
                width = self.rom[gp + 5 - ROM_BASE]
                self.note_hi(gp + 8, x & 7)
                self.ops.drawglyph(lut, base + (x >> 3) * 0x40, gp + 8, x & 7,
                                   f'drawglyph-line msg 0x{m:04X} x {x}')
                self.tally('drawglyph-line')
                x += width
                drawn += 1
            if drawn:
                self.after_call(base, 0x900)

    def drawglyph_fuzz(self, n):
        for k in range(n):
            r = self.rng.random()
            subx = self.rng.randrange(8)
            if r < 0.08:
                subx = self.rng.choice([8, 9, 10, 15, 31, 63, -1, -2, -7, -0x150])
                self.stats['dg_subx_rom'] += 1
            overlap = self.rng.random() < 0.2
            if overlap:
                region = ARENA + 0xB000
                self.write(region, self.rand_bytes(0x400))
                dst = region + 0x100
                src = dst + 4 * self.rng.randint(-16, 48)
                pal = (dst + 2 * self.rng.randint(-256, 96)) if self.rng.random() < 0.5 else ARENA + 0x5000
                dump = (region, 0x400)
                self.stats['dg_overlap'] += 1
            else:
                pal = ARENA + 0x5000 + self.rng.choice([0, 2, 4, 6])
                src = ARENA + 0x6000 + 4 * self.rng.randrange(4)
                dst = ARENA + 0x7000 + 4 * self.rng.randrange(4)
                dump = (ARENA + 0x7000, 0x200)
            if not overlap or pal == ARENA + 0x5000:
                self.write(ARENA + 0x5000, self.rand_bytes(0x210))
            if not overlap:
                style = self.rng.random()
                if style < 0.6:
                    srcb = self.rand_bytes(64)
                elif style < 0.75:
                    srcb = b'\xff' * 64
                elif style < 0.9:
                    srcb = b''.join(struct.pack('<I', 1 << self.rng.randrange(32)) for _ in range(16))
                else:
                    srcb = b''.join(struct.pack('<I', 0xC0000000 | self.rng.getrandbits(30)) for _ in range(16))
                self.write(ARENA + 0x6000, srcb + self.rand_bytes(16))
                self.write(ARENA + 0x7000, b'\0' * 0x200 if self.rng.random() < 0.3 else self.rand_bytes(0x200))
            if pal & 2:
                self.stats['dg_pal_2mod4'] += 1
            self.note_hi(src, subx)
            self.ops.drawglyph(pal, dst, src, subx,
                               f'drawglyph-fuzz {k} pal 0x{pal:08X} dst 0x{dst:08X} src 0x{src:08X} subx {subx}')
            self.tally('drawglyph-fuzz')
            self.after_call(*dump)


# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--elf', default='fireemblem8.elf')
    ap.add_argument('--out', default='build/difftest')
    ap.add_argument('--cc', default='gcc')
    ap.add_argument('--seed', type=int, default=8)
    ap.add_argument('--random-inputs', type=int, default=1000)
    ap.add_argument('--fuzz-tables', type=int, default=150)
    ap.add_argument('--per-table', type=int, default=10)
    ap.add_argument('--glyph-fuzz', type=int, default=4000)
    ap.add_argument('--lines', type=int, default=400)
    ap.add_argument('--c-source', default='port/asm/arm_pc.c',
                    help='C file to test (mutate_text.py passes altered copies)')
    args = ap.parse_args()

    root = os.getcwd()
    os.makedirs(args.out, exist_ok=True)
    rom, syms = read_elf(args.elf)
    for s in ('DrawGlyph', 'DecodeString', 'ARMCodeToCopy_Start', 'ARMCodeToCopy_End', 'RAMFuncArea',
              'gMsgHuffmanTable', 'gMsgHuffmanTableRoot', 'gMsgTable', 's2bppTo4bppLutTable'):
        if s not in syms:
            sys.exit(f'{args.elf}: no symbol {s}')
    print(f'ELF: ROM segment 0x{len(rom):X} bytes; DrawGlyph 0x{syms["DrawGlyph"][0]:08X}, '
          f'DecodeString 0x{syms["DecodeString"][0]:08X}, gMsgHuffmanTable 0x{syms["gMsgHuffmanTable"][0]:08X} '
          f'(0x{syms["gMsgHuffmanTable"][1]:X} bytes), gMsgTable 0x{syms["gMsgTable"][1] // 4:X} messages')

    exe, cflags = build_harness(root, args.out, syms, args.cc, args.c_source)
    print(f'C side: {args.c_source} built with PORT_CFLAGS:', ' '.join(cflags))

    rom_path = os.path.join(args.out, 'rom_from_elf.bin')
    with open(rom_path, 'wb') as f:
        f.write(rom)

    c = Cases(rom, syms, args.seed)
    # Each section starts from fresh random EWRAM (no zero background).
    for section in (c.decode_real_messages,
                    lambda: c.decode_real_table_random(args.random_inputs),
                    c.drawglyph_real,
                    lambda: c.drawglyph_lines(args.lines),
                    lambda: c.drawglyph_fuzz(args.glyph_fuzz),
                    lambda: c.decode_fuzz_tables(args.fuzz_tables, args.per_table)):
        c.resync()
        section()
    c.ops.crc(ROM_BASE, ROM_SIZE)                                # stray writes into the ROM copy
    c.ops.crc(EWRAM_BASE, EWRAM_SIZE)

    ops_path = os.path.join(args.out, 'text_ops.bin')
    out_path = os.path.join(args.out, 'text_c_out.bin')
    with open(ops_path, 'wb') as f:
        f.write(c.ops.serialize())
    try:
        r = subprocess.run([exe, rom_path, ops_path, out_path], capture_output=True, text=True, timeout=600)
    except subprocess.TimeoutExpired:
        print('RESULT: FAIL (the C side did not finish in 600 s)')
        return 1
    sys.stderr.write(r.stderr)
    if r.returncode != 0:
        print(f'RESULT: FAIL (the C side stopped with status {r.returncode}, before the compare)')
        return 1
    with open(out_path, 'rb') as f:
        cout = f.read()

    arm = ArmWorld(rom, syms)
    aouts, stray = arm.run(c.ops)

    # Compare, op by op.
    pos = 0
    label = None
    bad = {}
    first = []
    k = 0
    for it in c.ops.items:
        if it[0] in (OP_DRAWGLYPH, OP_DECODE):
            label = it[-1]
            continue
        if it[0] not in (OP_DUMP, OP_CRC):
            continue
        a = aouts[k]
        k += 1
        cc_ = cout[pos:pos + len(a)]
        pos += len(a)
        if a != cc_:
            cat = label.split(' ')[0] if label else 'final'
            bad[cat] = bad.get(cat, 0) + 1
            if len(first) < 10:
                what = 'CRC' if it[0] == OP_CRC else f'dump 0x{it[1]:08X}+0x{it[2]:X}'
                diff = next((i for i in range(min(len(a), len(cc_))) if a[i] != cc_[i]), None)
                first.append(f'  {label}: {what} differs (first byte {diff}: ARM '
                             f'{a[diff] if diff is not None else "?"} C {cc_[diff] if diff is not None and diff < len(cc_) else "?"})')
    if pos != len(cout):
        sys.exit(f'C output has {len(cout) - pos} extra bytes')

    total = sum(c.count.values())
    print(f'Cases: {total} calls, each followed by a dump of its buffers and a CRC of the 64 KB arena; '
          f'then a CRC of the whole ROM copy and of EWRAM.')
    for cat in sorted(c.count):
        print(f'  {cat:28s} {c.count[cat]:6d} calls, {bad.get(cat, 0)} differing checks')
    print('Edge cases hit:', ', '.join(f'{k} {v}' for k, v in c.stats.items()))
    print(f'ARM writes outside the arena: {len(stray)}')
    for s in stray[:5]:
        print(f'  {s[0]}: 0x{s[1]:08X}')
    if bad or stray:
        print('RESULT: FAIL')
        print('\n'.join(first))
        return 1
    print('RESULT: PASS (every dump and CRC identical)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
