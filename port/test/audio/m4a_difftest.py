#!/usr/bin/env python3
"""Difftest: port/audio/m4a_1_pc.c against FE8's own src/m4a_1.s in Unicorn.

Runs the ARM code of the GBA build (fireemblem8.elf: the ROM bytes, with
SoundMainRAM copied to SoundMainRAM_Buffer as m4aSoundInit does) in Unicorn,
and the C port in m4a_harness.c (a 32-bit host process with GBA memory at GBA
addresses, D1-a). Every case starts both sides from the same memory (EWRAM,
IWRAM, I/O; the ROM is shared and read-only on the C side), calls one routine,
and requires every byte of EWRAM, IWRAM (except the ARM stack window) and the
I/O page to match afterwards, and the return value. Code pointers in driver
memory (SoundInfo callbacks, MusicPlayerInfo.func, gMPlayJumpTable) are
translated between GBA and host addresses by name.

Pre-states come from the ARM driver itself (m4aSoundInit, then songs started
and run frame by frame, with the m4a API calls the game uses), and from fuzzed
copies of those states. The C side also runs src/m4a.c (compiled for the host
as audio-design.md 1.6 plans), so the API calls and the C routines the asm
calls (CgbSound, TrkVolPitSet, FadeOutBody, MidiKeyToFreq ...) are compared too.

Run from the repository root, after the GBA `make` (fireemblem8.elf):

    python3 port/test/audio/m4a_difftest.py            # all steps, about 5 minutes
    python3 port/test/audio/m4a_difftest.py --quick    # fewer cases

Output goes to build/difftest/audio (ignored by git). Nothing ROM-derived is
written to the tree.
"""

import argparse
import os
import random
import struct
import subprocess
import sys
import time
import zlib

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_INTR, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn import UcError
from unicorn import arm_const as A

LOW_BASE, LOW_SIZE = 0x00000000, 0x02000000
EWRAM_BASE, EWRAM_SIZE = 0x02000000, 0x00040000
IWRAM_BASE, IWRAM_SIZE = 0x03000000, 0x00008000
IO_BASE, IO_SIZE, IO_CMP = 0x04000000, 0x00001000, 0x400
ROM_BASE, ROM_SIZE = 0x08000000, 0x01000000

STACK_TOP = 0x03007F00
STACK_LO = 0x03007B00          # excluded from the compare (ARM stack)
STOP_ADDR = 0x08FFFF00         # sentinel return address (ROM padding)
SOUND_INFO_PTR = 0x03007FF0
ID_NUMBER = 0x68736D53
VCOUNT = 0x04000006

# Driver image (audio-design.md 2.3): ARM writes outside it are reported.
DRIVER_IMAGE = [(0x03001E38, 0x03002A68), (0x03005410, 0x030067A0), (0x03007FF0, 0x03007FF4),
                (0x04000060, 0x040000B0), (0x040000BC, 0x040000D4)]

SOUNDINFO_CODE_FIELDS = [0x20, 0x28, 0x2C, 0x30, 0x38, 0x3C]


# --------------------------------------------------------------------------
# ELF (32-bit little-endian): ROM segment and symbols.

def read_elf(path):
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'\x7fELF' or data[4] != 1 or data[5] != 1:
        sys.exit(f'{path}: not a 32-bit little-endian ELF')
    e_phoff, e_shoff = struct.unpack_from('<II', data, 0x1C)
    e_phentsize, e_phnum, e_shentsize, e_shnum = struct.unpack_from('<HHHH', data, 0x2A)
    rom = bytearray(b'\xff' * ROM_SIZE)
    for i in range(e_phnum):
        p_type, p_offset, _vaddr, p_paddr, p_filesz = struct.unpack_from('<5I', data, e_phoff + i * e_phentsize)
        if p_type == 1 and p_filesz and ROM_BASE <= p_paddr < ROM_BASE + ROM_SIZE:
            off = p_paddr - ROM_BASE
            rom[off:off + p_filesz] = data[p_offset:p_offset + p_filesz]
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
    return bytes(rom), syms


def run(cmd, cwd):
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(' '.join(cmd) + '\n' + r.stdout + r.stderr)
        sys.exit('build failed')
    return r


def make_var(root, var):
    env = dict(os.environ, MAKEFLAGS='')
    r = subprocess.run(['make', '-f', 'port/Makefile', '-pn', '.difftest-none'], cwd=root,
                       capture_output=True, text=True, env=env)
    for line in r.stdout.splitlines():
        if line.startswith(var + ' := '):
            return line[len(var) + 4:].split()
    sys.exit(f'cannot read {var} from port/Makefile')


def build_harness(root, out, syms, cc, c_source, extra_port_flags=(), tag='', m4a_source='src/m4a.c'):
    port_cflags = make_var(root, 'PORT_CFLAGS')
    host_cflags = make_var(root, 'HOST_CFLAGS')
    host_cppflags = make_var(root, 'HOST_CPPFLAGS')
    objs = []
    obj = os.path.join(out, f'm4a_1_pc{tag}.o')
    # The same command as port/Makefile's rule for port/%.o.
    run([cc] + port_cflags + ['-DPC_PORT=1', '-Iinclude', '-Iport/include', '-Iport/audio'] + list(extra_port_flags)
        + ['-c', c_source, '-o', obj], root)
    objs.append(obj)
    # src/m4a.c as audio-design.md 1.6 plans: HOST flags plus the asm() hiding of
    # DIVERGENCES B-13 (audio-design's B-6).
    # (m4a_source: an altered copy, for the replay's mutation check.)
    for src in (m4a_source, 'src/m4a_tables.c'):
        o = os.path.join(out, os.path.basename(src)[:-2] + '.o')
        run([cc] + host_cflags + host_cppflags + ["-Dasm(x)=((void)0)", '-c', src, '-o', o], root)
        objs.append(o)
    h = os.path.join(out, f'm4a_harness{tag}.o')
    run([cc, '-m32', '-O2', '-Wall', '-fno-pie', '-c', 'port/test/audio/m4a_harness.c', '-o', h], root)
    objs.append(h)
    bound = ['gSoundInfo', 'gMPlayJumpTable', 'gCgbChans', 'gMPlayMemAccArea', 'SoundMainRAM_Buffer',
             'SoundMainRAM', 'gMPlayTable', 'gSongTable', 'gNumMusicPlayers', 'gMaxLines']
    defsyms = [f'-Wl,--defsym,{s}=0x{syms[s][0]:08X}' for s in bound]
    exe = os.path.join(out, f'm4a_harness{tag}')
    run([cc, '-m32', '-no-pie', '-Wl,-Ttext-segment=0x10000000'] + objs + defsyms
        + ['-lpthread', '-o', exe], root)
    return exe, port_cflags


# --------------------------------------------------------------------------
# The ARM side.

class Arm:
    def __init__(self, rom, syms):
        self.syms = syms
        mu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        mu.ctl_set_cpu_model(A.UC_CPU_ARM_926)   # ARMv5; m4a_1.s and agbcc use ARMv4T
        mu.mem_map(LOW_BASE, LOW_SIZE)
        mu.mem_map(EWRAM_BASE, EWRAM_SIZE)
        mu.mem_map(IWRAM_BASE, IWRAM_SIZE)
        mu.mem_map(IO_BASE, IO_SIZE)
        mu.mem_map(ROM_BASE, ROM_SIZE)
        mu.mem_write(ROM_BASE, rom)
        self.mu = mu
        self.cpuset = syms['CpuSet'][0] & ~1
        self.cpufastset = syms['CpuFastSet'][0] & ~1
        mu.hook_add(UC_HOOK_INTR, self._on_intr)
        mu.hook_add(UC_HOOK_MEM_WRITE, self._on_write)
        self.writes_outside = {}
        self.track_writes = False
        self.vcount_seq = None
        self.vcount_hook = None

    def fn(self, name):
        return self.syms[name][0] | 1

    def _on_write(self, mu, access, addr, size, value, user):
        if not self.track_writes:
            return
        if STACK_LO <= addr < STACK_TOP:
            return
        for lo, hi in DRIVER_IMAGE:
            if lo <= addr < hi:
                return
        self.writes_outside[addr & ~0xF] = self.writes_outside.get(addr & ~0xF, 0) + 1

    def _on_intr(self, mu, intno, user):
        # SWI from libagbsyscall's CpuSet/CpuFastSet stubs (swi; bx lr).
        pc = mu.reg_read(A.UC_ARM_REG_PC)
        cpsr = mu.reg_read(A.UC_ARM_REG_CPSR)
        if cpsr & 0x20:
            num = mu.mem_read(pc - 2, 2)[0]
        else:
            num = mu.mem_read(pc - 4, 4)[2]
        src = mu.reg_read(A.UC_ARM_REG_R0)
        dst = mu.reg_read(A.UC_ARM_REG_R1)
        ctrl = mu.reg_read(A.UC_ARM_REG_R2)
        if num == 0x0B:
            self._cpuset(src, dst, ctrl)
        elif num == 0x0C:
            n = ((ctrl & 0x1FFFFF) + 7) & ~7
            self._cpuset(src, dst, (ctrl & ~0x1FFFFF) | n | (1 << 26))
        else:
            raise RuntimeError(f'unexpected SWI 0x{num:02X} at 0x{pc:08X}')

    def _cpuset(self, src, dst, ctrl):
        n = ctrl & 0x1FFFFF
        fill = ctrl & (1 << 24)
        if ctrl & (1 << 26):
            src &= ~3
            dst &= ~3
            unit = 4
        else:
            src &= ~1
            dst &= ~1
            unit = 2
        if fill:
            v = bytes(self.mu.mem_read(src, unit))
            self.mu.mem_write(dst, v * n)
        else:
            self.mu.mem_write(dst, bytes(self.mu.mem_read(src, unit * n)))

    def set_vcount_seq(self, seq):
        """Each read of VCOUNT returns the next value of seq (None: plain memory)."""
        if self.vcount_hook is not None:
            self.mu.hook_del(self.vcount_hook)
            self.vcount_hook = None
        self.vcount_seq = seq
        if seq is not None:
            def on_read(mu, access, addr, size, value, user):
                if addr <= VCOUNT < addr + size:
                    mu.mem_write(VCOUNT, bytes([next(self.vcount_seq) & 0xFF]))
            self.vcount_hook = self.mu.hook_add(UC_HOOK_MEM_READ, on_read, begin=VCOUNT - 3, end=VCOUNT + 1)

    def find_vcount_reads(self):
        """The two places the mixer reads VCOUNT (the maxLines check): in
        SoundMain `ldrb r2, [r2]; cmp r2, #160`, and in the IWRAM copy of
        SoundMainRAM `ldrb r1, [r1]; cmp r1, #160`. Returns the address of
        each cmp and the register it tests."""
        found = []
        sm = self.syms['SoundMain'][0] & ~1
        code = self.read(sm, 0x80)
        for off in range(0, len(code) - 4, 2):
            if code[off:off + 4] == bytes([0x12, 0x78, 0xA0, 0x2A]):
                found.append((sm + off + 2, A.UC_ARM_REG_R2))
        buf = self.syms['SoundMainRAM_Buffer'][0]
        code = self.read(buf, 0x400)
        for off in range(0, len(code) - 4, 2):
            if code[off:off + 4] == bytes([0x09, 0x78, 0xA0, 0x29]):
                found.append((buf + off + 2, A.UC_ARM_REG_R1))
        if len(found) != 2:
            raise RuntimeError(f'VCOUNT reads not found: {found}')
        return found

    def set_vcount_regs(self, seq):
        """maxLines test: each VCOUNT read the mixer makes gives the next value
        of seq. Set in the register right after the load (a code hook), so no
        memory is written from inside a read hook."""
        for h in getattr(self, 'vc_reg_hooks', []):
            self.mu.hook_del(h)
        self.vc_reg_hooks = []
        if seq is None:
            return
        for addr, reg in self.find_vcount_reads():
            def on_code(mu, address, size, user, reg=reg):
                mu.reg_write(reg, next(seq) & 0xFF)
            self.vc_reg_hooks.append(self.mu.hook_add(UC_HOOK_CODE, on_code, begin=addr, end=addr))

    def call(self, name, args=(), regs=None):
        mu = self.mu
        a = list(args) + [0] * (4 - len(args))
        for i, r in enumerate((A.UC_ARM_REG_R0, A.UC_ARM_REG_R1, A.UC_ARM_REG_R2, A.UC_ARM_REG_R3)):
            mu.reg_write(r, a[i] & 0xFFFFFFFF)
        for r, v in (regs or {}).items():
            mu.reg_write(r, v & 0xFFFFFFFF)
        mu.reg_write(A.UC_ARM_REG_SP, STACK_TOP)
        mu.reg_write(A.UC_ARM_REG_LR, STOP_ADDR | 1)
        mu.emu_start(self.fn(name), STOP_ADDR, count=200_000_000)
        if mu.reg_read(A.UC_ARM_REG_PC) != STOP_ADDR:
            raise RuntimeError(f'{name}: did not return (pc 0x{mu.reg_read(A.UC_ARM_REG_PC):08X})')
        return mu.reg_read(A.UC_ARM_REG_R0), mu.reg_read(A.UC_ARM_REG_R3)

    def read(self, addr, n):
        return bytes(self.mu.mem_read(addr, n))

    def write(self, addr, data):
        self.mu.mem_write(addr, bytes(data))


# --------------------------------------------------------------------------
# The C side.

class CSide:
    def __init__(self, exe, rom_path):
        self.p = subprocess.Popen([exe, rom_path], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.sym = {}
        self.fnid = {}
        while True:
            line = self.p.stdout.readline().decode().strip()
            if line == 'END':
                break
            parts = line.split()
            if parts[0] == 'SYM':
                self.sym[parts[1]] = int(parts[2], 16)
            elif parts[0] == 'FN':
                self.fnid[parts[2]] = int(parts[1])

    def _send(self, *words, data=b''):
        self.p.stdin.write(struct.pack(f'<{len(words)}I', *words) + data)

    def _recv(self, n):
        self.p.stdin.flush()
        out = b''
        while len(out) < n:
            chunk = self.p.stdout.read(n - len(out))
            if not chunk:
                raise RuntimeError(f'C harness died (exit {self.p.wait()})')
            out += chunk
        return out

    def write(self, addr, data):
        data = bytes(data)
        pad = (-len(data)) % 4
        self._send(1, addr, len(data), data=data + b'\0' * pad)

    def read(self, addr, n):
        self._send(2, addr, n)
        return self._recv(n)

    def crc(self, addr, n):
        self._send(3, addr, n)
        return struct.unpack('<I', self._recv(4))[0]

    def call(self, name, args=()):
        a = (list(args) + [0, 0, 0])[:3]
        self._send(4, self.fnid[name], *[x & 0xFFFFFFFF for x in a])
        return struct.unpack('<I', self._recv(4))[0]

    def vcount_toggle(self, on):
        self._send(5, 1 if on else 0)

    def umul(self, pairs):
        flat = [x for p in pairs for x in p]
        self._send(6, len(pairs), *flat)
        return struct.unpack(f'<{len(pairs)}I', self._recv(4 * len(pairs)))

    def chnvol(self, chan, track, recs):
        self._send(7, len(recs), chan, track, *recs)
        return struct.unpack(f'<{len(recs)}I', self._recv(4 * len(recs)))

    def vcount_seq(self, start, every):
        self._send(8, start, every)

    def close(self):
        try:
            self._send(0)
            self.p.stdin.flush()
        except BrokenPipeError:
            pass
        self.p.wait()


# --------------------------------------------------------------------------

class Difftest:
    def __init__(self, arm, c, syms, seed, verbose):
        self.arm = arm
        self.c = c
        self.syms = syms
        self.rng = random.Random(seed)
        self.verbose = verbose
        self.results = {}       # step -> [cases, failures]
        self.fail_examples = []
        self.players = []
        self.g2h = {}
        self.h2g = {}
        self.code_fields = []
        self.ewram_dirty = True
        self.exclude_vcount = False

    # ---- code pointer translation -------------------------------------

    def setup_translation(self, sides):
        for side in sides:
            side.g2h, side.h2g = {}, {}
            for name, host in side.sym.items():
                gba = self.syms[name][0] | 1
                side.g2h[gba] = host
                side.h2g[host] = gba
        si = 0x03005410
        self.code_fields = [si + o for o in SOUNDINFO_CODE_FIELDS]
        self.code_fields += [self.syms['gMPlayJumpTable'][0] + 4 * i for i in range(36)]
        self.code_fields += [p + 0x38 for p in self.players]

    def translate(self, iw, table):
        b = bytearray(iw)
        for addr in self.code_fields:
            off = addr - IWRAM_BASE
            v = struct.unpack_from('<I', b, off)[0]
            if v in table:
                struct.pack_into('<I', b, off, table[v])
        return bytes(b)

    # ---- one case -------------------------------------------------------

    def record(self, step, ok, label):
        r = self.results.setdefault(step, [0, 0])
        r[0] += 1
        if not ok:
            r[1] += 1
            if len(self.fail_examples) < 40:
                self.fail_examples.append(label)

    def wboth(self, addr, data):
        """EWRAM bytes written to both sides."""
        self.arm.write(addr, data)
        self.c.write(addr, data)

    def load_c(self, ewram=None):
        iw = self.arm.read(IWRAM_BASE, IWRAM_SIZE)
        self.c.write(IWRAM_BASE, self.translate(iw, self.c.g2h))
        self.c.write(IO_BASE, self.arm.read(IO_BASE, IO_CMP))
        if self.ewram_dirty:
            self.c.write(EWRAM_BASE, self.arm.read(EWRAM_BASE, EWRAM_SIZE))
            self.ewram_dirty = False

    def compare(self, step, label, aret=None, cret=None):
        a_iw = self.arm.read(IWRAM_BASE, IWRAM_SIZE)
        a_io = self.arm.read(IO_BASE, IO_CMP)
        c_iw = self.translate(self.c.read(IWRAM_BASE, IWRAM_SIZE), self.c.h2g)
        c_io = self.c.read(IO_BASE, IO_CMP)
        lo, hi = STACK_LO - IWRAM_BASE, STACK_TOP - IWRAM_BASE
        ok = a_iw[:lo] == c_iw[:lo] and a_iw[hi:] == c_iw[hi:]
        if self.exclude_vcount:
            a_io = a_io[:6] + a_io[8:]
            c_io = c_io[:6] + c_io[8:]
        ok = ok and a_io == c_io
        a_crc = zlib.crc32(self.arm.read(EWRAM_BASE, EWRAM_SIZE))
        ok = ok and a_crc == self.c.crc(EWRAM_BASE, EWRAM_SIZE)
        if aret is not None and aret != cret:
            ok = False
        if not ok:
            diffs = []
            for i in list(range(0, lo)) + list(range(hi, IWRAM_SIZE)):
                if a_iw[i] != c_iw[i]:
                    diffs.append(f'{IWRAM_BASE + i:08X}:{a_iw[i]:02X}/{c_iw[i]:02X}')
                    if len(diffs) >= 12:
                        break
            for i in range(len(a_io)):
                if a_io[i] != c_io[i] and len(diffs) < 20:
                    diffs.append(f'IO+{i:03X}:{a_io[i]:02X}/{c_io[i]:02X}')
            msg = f'[{step}] {label}: ' + (' '.join(diffs) or 'EWRAM CRC or return value differs')
            if aret is not None and aret != cret:
                msg += f' ret arm=0x{aret:08X} c=0x{cret:08X}'
            if self.verbose:
                print('FAIL', msg)
            self.record(step, False, msg)
            # resync the C side to the ARM result so later cases start equal
            self.ewram_dirty = True
            self.load_c()
            return False
        self.record(step, True, label)
        return True

    def case(self, step, label, name, args=(), regs=None, ret=None):
        """Both sides from the ARM's current memory: call, compare. ret names
        the ARM register that holds the result ('r0', 'r3'), None for void."""
        self.load_c()
        try:
            r0, r3 = self.arm.call(name, args, regs)
        except (UcError, RuntimeError) as e:
            self.record(step + '-arm-error', False, f'[{step}] {label}: ARM side {e}')
            if self.verbose:
                print('ARM ERROR', step, label, e)
            return False
        cret = self.c.call(name, args)
        aret = {'r0': r0, 'r3': r3, None: None}[ret]
        if ret is None:
            cret = None
        return self.compare(step, label, aret, cret)

    # ---- helpers ----------------------------------------------------------

    def chan_nodes(self):
        si = 0x03005410
        return [si + 0x50 + 0x40 * i for i in range(12)] + [self.syms['gCgbChans'][0] + 0x40 * i for i in range(4)]

    def sanitize_chain(self, t):
        """A track that was never started holds random bytes: cut a chain
        that leaves the channel arrays (the GBA would read garbage there)."""
        nodes = set(self.chan_nodes())
        head = struct.unpack('<I', self.arm.read(t + 0x20, 4))[0]
        seen = set()
        nd = head
        while nd:
            if nd not in nodes or nd in seen:
                self.arm.write(t + 0x20, b'\0\0\0\0')
                return
            seen.add(nd)
            nd = struct.unpack('<I', self.arm.read(nd + 0x34, 4))[0]
        nd = head
        while nd:
            nxt = struct.unpack('<I', self.arm.read(nd + 0x34, 4))[0]
            if nxt and struct.unpack('<I', self.arm.read(nxt + 0x30, 4))[0] != nd:
                self.arm.write(t + 0x20, b'\0\0\0\0')
                return
            nd = nxt

    def near_code_field(self, addr):
        return any(f - 3 <= addr < f + 4 for f in self.code_fields)

    def song_list(self):
        base, size = self.syms['gSongTable']
        n = size // 8 if size else 1000
        songs = []
        for i in range(n):
            hdr, ms, me = struct.unpack('<IHH', self.arm.read(base + 8 * i, 8))
            if not (ROM_BASE <= hdr < ROM_BASE + ROM_SIZE):
                continue
            tc = self.arm.read(hdr, 1)[0]
            if 1 <= tc <= 16 and ms < len(self.players):
                songs.append((i, ms, hdr))
        return songs

    def load_players(self):
        base = self.syms['gMPlayTable'][0]
        n = self.syms['gNumMusicPlayers'][0]
        self.players = []
        self.player_tracks = []
        for i in range(n):
            info, track, count = struct.unpack('<IIB', self.arm.read(base + 12 * i, 9))
            self.players.append(info)
            self.player_tracks.append((track, count))

    def frame(self, step_tag, mplay_alone=True):
        """One VBlank: m4aSoundVSync, then MPlayMain alone (compared, then
        undone on the ARM side), then SoundMain."""
        si = 0x03005410
        self.case('2-vsync', f'{step_tag} vsync', 'm4aSoundVSync')
        active = any(self.arm.read(si + 0x50 + 0x40 * i, 1)[0] & 0xC7 for i in range(12))
        if mplay_alone:
            head = struct.unpack('<I', self.arm.read(si + 0x24, 4))[0]
            func = struct.unpack('<I', self.arm.read(si + 0x20, 4))[0]
            if func and head:
                snap_iw = self.arm.read(IWRAM_BASE, IWRAM_SIZE)
                snap_io = self.arm.read(IO_BASE, IO_SIZE)
                self.case('4-mplaymain', f'{step_tag} MPlayMain', 'MPlayMain', (head,))
                self.arm.write(IWRAM_BASE, snap_iw)
                self.arm.write(IO_BASE, snap_io)
        step = '5-soundmain' if active else '3-soundmain-idle'
        self.case(step, f'{step_tag} SoundMain', 'SoundMain')

    # ---- step 1: small routines -----------------------------------------

    def step1(self, quick):
        rng = self.rng
        # umul3232H32
        n = 20000 if quick else 100000
        special = [0, 1, 2, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFFFFFE, 0x10000]
        pairs = []
        for i in range(n):
            a = rng.choice(special) if i % 10 == 0 else rng.getrandbits(32)
            b = rng.choice(special) if i % 7 == 0 else rng.getrandbits(32)
            pairs.append((a, b))
        bad = 0
        for k in range(0, n, 4096):
            chunk = pairs[k:k + 4096]
            cres = self.c.umul(chunk)
            for (a, b), cr in zip(chunk, cres):
                ar, _ = self.arm.call('umul3232H32', (a, b))
                if ar != cr:
                    bad += 1
                    if len(self.fail_examples) < 40:
                        self.fail_examples.append(f'umul {a:08X}*{b:08X}: {ar:08X}/{cr:08X}')
        self.results['1-umul3232H32'] = [n, bad]

        # ChnVolSetAsm (r4 = channel, r5 = track), DirectSound and PSG channel
        n = 20000 if quick else 100000
        bad = 0
        track = self.player_tracks[0][0]
        for chan in (0x03005410 + 0x50 + 0x40 * 3, self.syms['gCgbChans'][0] + 0x40):
            recs = [rng.getrandbits(32) for _ in range(n // 2)]
            for k in range(0, len(recs), 4096):
                chunk = recs[k:k + 4096]
                cres = self.c.chnvol(chan, track, chunk)
                for rec, cr in zip(chunk, cres):
                    self.arm.write(chan + 0x12, bytes([rec & 0xFF]))
                    self.arm.write(chan + 0x14, bytes([(rec >> 8) & 0xFF]))
                    self.arm.write(track + 0x10, bytes([(rec >> 16) & 0xFF, rec >> 24]))
                    self.arm.call('ChnVolSetAsm', (), {A.UC_ARM_REG_R4: chan, A.UC_ARM_REG_R5: track})
                    v = self.arm.read(chan + 2, 2)
                    ar = v[0] | (v[1] << 8)
                    if ar != cr:
                        bad += 1
                        if len(self.fail_examples) < 40:
                            self.fail_examples.append(f'ChnVolSetAsm rec {rec:08X}: {ar:04X}/{cr:04X}')
        self.results['1-ChnVolSetAsm'] = [n, bad]
        # leave both sides equal again
        self.load_c()
        self.compare('1-ChnVolSetAsm-state', 'after ChnVolSetAsm batch')

        # chk_adr_r2, ldrb_r3_r2, ld_r3_tp_adr_i(_unchecked)
        tpl = self.syms['gMPlayJumpTableTemplate'][0]
        n = 3000 if quick else 12000
        track = self.player_tracks[1][0]
        for i in range(n):
            kind = i % 6
            if kind == 0:
                addr = rng.randrange(0, 0x4000)
            elif kind == 1:
                addr = rng.randrange(0x4000, 0x02000000)
            elif kind == 2:
                addr = rng.randrange(EWRAM_BASE, EWRAM_BASE + EWRAM_SIZE)
            elif kind == 3:
                addr = rng.randrange(IWRAM_BASE, IWRAM_BASE + 0x7000)
            elif kind == 4:
                addr = rng.randrange(ROM_BASE, ROM_BASE + 0x00E00000)
            else:
                addr = rng.choice([0, 0x3FFF, 0x4000, 0x01FFFFFF, 0x02000000, tpl, tpl - 1])
            if addr >= 0x02000000 and not (EWRAM_BASE <= addr < EWRAM_BASE + EWRAM_SIZE
                                           or IWRAM_BASE <= addr < IWRAM_BASE + IWRAM_SIZE
                                           or ROM_BASE <= addr < ROM_BASE + ROM_SIZE):
                addr = EWRAM_BASE
            if self.near_code_field(addr):
                continue   # code pointers hold different (translated) bytes on the two sides
            val = rng.getrandbits(32)
            self.case('1-chk_adr_r2', f'chk_adr_r2 {addr:08X}', 'chk_adr_r2', (addr, val),
                      {A.UC_ARM_REG_R2: addr, A.UC_ARM_REG_R3: val}, ret='r3')
            self.case('1-ldrb_r3_r2', f'ldrb_r3_r2 {addr:08X}', 'ldrb_r3_r2', (addr,),
                      {A.UC_ARM_REG_R2: addr}, ret='r3')
            self.arm.write(track + 0x40, struct.pack('<I', addr))
            self.case('1-ld_r3_tp_adr_i', f'ld_r3_tp_adr_i {addr:08X}', 'ld_r3_tp_adr_i', (track,),
                      {A.UC_ARM_REG_R1: track}, ret='r3')
            if addr >= 0x02000000:
                self.arm.write(track + 0x40, struct.pack('<I', addr))
                self.case('1-ld_r3_tp_adr_i_unchecked', f'unchecked {addr:08X}', 'ld_r3_tp_adr_i_unchecked',
                          (track,), {A.UC_ARM_REG_R1: track}, ret='r3')

        # MPlayJumpTableCopy into gMPlayJumpTable and into free IWRAM (the copies
        # hold code pointers, translated by name like gMPlayJumpTable's)
        self.case('1-MPlayJumpTableCopy', 'to gMPlayJumpTable', 'MPlayJumpTableCopy',
                  (self.syms['gMPlayJumpTable'][0],), ret=None)
        saved = list(self.code_fields)
        for i in range(8):
            dst = IWRAM_BASE + 0x100 + 4 * rng.randrange(0, 0x600)
            self.code_fields = saved + [dst + 4 * k for k in range(36)]
            self.case('1-MPlayJumpTableCopy', f'to {dst:08X}', 'MPlayJumpTableCopy', (dst,), ret=None)
            self.arm.write(dst, bytes(144))
        self.code_fields = saved

        # SoundMainBTM on random aligned addresses
        for i in range(200 if quick else 1000):
            dst = rng.choice([EWRAM_BASE + 4 * rng.randrange(0, 0xFF00),
                              rng.choice(self.player_tracks)[0] + 0x50 * rng.randrange(0, 2)])
            self.case('1-SoundMainBTM', f'at {dst:08X}', 'SoundMainBTM', (dst,), ret=None)

    def fuzz_chains(self):
        """Random channel chains over the DirectSound and PSG channels."""
        rng = self.rng
        si = 0x03005410
        nodes = [si + 0x50 + 0x40 * i for i in range(12)] + [self.syms['gCgbChans'][0] + 0x40 * i for i in range(4)]
        tracks = []
        for t, cnt in self.player_tracks:
            tracks += [t + 0x50 * k for k in range(cnt)]
        rng.shuffle(nodes)
        for t in tracks:
            self.arm.write(t + 0x20, b'\0\0\0\0')
        # build a few chains
        while nodes:
            t = rng.choice(tracks)
            k = rng.randrange(1, 5)
            chain, nodes = nodes[:k], nodes[k:]
            prev = 0
            for nd in chain:
                self.arm.write(nd + 0x2C, struct.pack('<I', t))
                self.arm.write(nd + 0x30, struct.pack('<I', prev))
                self.arm.write(nd + 0x34, b'\0\0\0\0')
                if prev:
                    self.arm.write(prev + 0x34, struct.pack('<I', nd))
                prev = nd
            # prepend to the track's existing chain
            head = struct.unpack('<I', self.arm.read(t + 0x20, 4))[0]
            if head:
                self.arm.write(chain[-1] + 0x34, struct.pack('<I', head))
                self.arm.write(head + 0x30, struct.pack('<I', chain[-1]))
            self.arm.write(t + 0x20, struct.pack('<I', chain[0]))
            for nd in chain:
                st = rng.choice([0, 0x80, 0x83, 0x42, 0x03, 0x01, 0x44, 0x02, 0x40, 0x13])
                self.arm.write(nd, bytes([st]))
                self.arm.write(nd + 0x10, bytes([rng.choice([0, 1, 2, rng.randrange(256)])]))
                self.arm.write(nd + 0x11, bytes([rng.randrange(128)]))
            if rng.random() < 0.3:
                self.arm.write(t, bytes([rng.choice([0x80, 0xC0, 0x8F, 0x00, 0x83])]))
        return tracks

    def step1_chains(self, quick, base_iw, base_io):
        rng = self.rng
        n = 150 if quick else 600
        for i in range(n):
            self.arm.write(IWRAM_BASE, base_iw)
            self.arm.write(IO_BASE, base_io)
            # random sound registers, so CgbOscOff's writes (from TrackStop) show
            self.arm.write(IO_BASE + 0x60, bytes(rng.randrange(256) for _ in range(0x40)))
            tracks = self.fuzz_chains()
            si = 0x03005410
            which = i % 4
            if which == 0:
                node = rng.choice([si + 0x50 + 0x40 * k for k in range(12)]
                                  + [self.syms['gCgbChans'][0] + 0x40 * k for k in range(4)])
                self.case('1-RealClearChain', f'#{i}', 'RealClearChain', (node,), ret=None)
            elif which == 1:
                t = rng.choice(tracks)
                p = rng.choice(self.players)
                if rng.random() < 0.7:
                    # the track of a sounding PSG channel, so CgbOscOff runs
                    cgb = self.syms['gCgbChans'][0] + 0x40 * rng.randrange(4)
                    self.arm.write(cgb, bytes([rng.choice([0x80, 0x83, 0x42, 0x01])]))
                    t = struct.unpack('<I', self.arm.read(cgb + 0x2C, 4))[0]
                    self.arm.write(t, bytes([self.arm.read(t, 1)[0] | 0x80]))
                self.case('1-TrackStop', f'#{i}', 'TrackStop', (p, t), ret=None)
            elif which == 2:
                t = rng.choice(tracks)
                self.arm.write(t + 0x18, bytes([rng.choice([0, rng.randrange(256)])]))
                self.case('1-clear_modM', f'#{i}', 'clear_modM', (t,), {A.UC_ARM_REG_R1: t}, ret=None)
            else:
                t = rng.choice(tracks)
                p = rng.choice(self.players)
                # ply_fine and ply_endtie walk chains too
                key = rng.randrange(128)
                cmd = EWRAM_BASE + 0x100 * rng.randrange(1, 64)
                self.wboth(cmd, bytes([rng.choice([key, 0x80 + rng.randrange(128)])]))
                self.arm.write(t + 0x40, struct.pack('<I', cmd))
                name = rng.choice(['ply_fine', 'ply_endtie'])
                self.case('1-chain-cmds', f'{name} #{i}', name, (p, t), ret=None)

    # ---- step 4b: the command handlers, fuzzed --------------------------

    def step_handlers(self, quick, base_iw, base_io, voicegroups):
        rng = self.rng
        names = ['ply_goto', 'ply_patt', 'ply_pend', 'ply_rept', 'ply_prio', 'ply_tempo', 'ply_keysh',
                 'ply_voice', 'ply_vol', 'ply_pan', 'ply_bend', 'ply_bendr', 'ply_lfodl', 'ply_modt',
                 'ply_tune', 'ply_port', 'ply_lfos', 'ply_mod', 'ply_endtie', 'ply_fine']
        per = 60 if quick else 300
        self.arm.write(IWRAM_BASE, base_iw)
        self.arm.write(IO_BASE, base_io)
        for name in names:
            for i in range(per):
                pi = rng.randrange(len(self.players))
                p = self.players[pi]
                t0, cnt = self.player_tracks[pi]
                t = t0 + 0x50 * rng.randrange(max(cnt, 1))
                # random track bytes (not the chain pointer) and player tempo
                tb = bytearray(self.arm.read(t, 0x50))
                for off in range(0, 0x20):
                    if rng.random() < 0.5:
                        tb[off] = rng.randrange(256)
                self.arm.write(t, bytes(tb[:0x20]))
                self.arm.write(p + 0x1C, struct.pack('<HHHH', rng.randrange(65536), rng.randrange(65536),
                                                      rng.randrange(65536), rng.randrange(65536)))
                self.arm.write(p + 0x30, struct.pack('<I', rng.choice(voicegroups)))
                # command arguments: random bytes in EWRAM, or a pointer into the ROM
                if rng.random() < 0.8:
                    cmd = EWRAM_BASE + rng.randrange(0x100, 0x20000)
                    args = bytes(rng.randrange(256) for _ in range(8))
                    if name in ('ply_goto', 'ply_patt', 'ply_rept') and rng.random() < 0.3:
                        args = args[:1] + struct.pack('<I', rng.choice([0x1234, 0x01FFFFF0, 0x3FF0])) + args[5:]
                    if name in ('ply_goto', 'ply_patt') and rng.random() < 0.2:
                        args = struct.pack('<I', rng.randrange(0, 0x4000)) + args[4:]
                    self.wboth(cmd, args)
                else:
                    cmd = ROM_BASE + rng.randrange(0x200000, 0xD00000)
                for k in range(3):
                    self.arm.write(t + 0x44 + 4 * k, struct.pack('<I', rng.choice([cmd, EWRAM_BASE + 0x400])))
                self.arm.write(t + 0x40, struct.pack('<I', cmd))
                self.sanitize_chain(t)
                self.case('4-handlers', f'{name} #{i}', name, (p, t), ret=None)

    # ---- ply_note fuzz ------------------------------------------------------

    def step_note(self, quick, states, tones):
        rng = self.rng
        n = 600 if quick else 3000
        si = 0x03005410
        for i in range(n):
            iw, io = rng.choice(states)
            self.arm.write(IWRAM_BASE, iw)
            self.arm.write(IO_BASE, io)
            pi = rng.randrange(len(self.players))
            p = self.players[pi]
            t0, cnt = self.player_tracks[pi]
            t = t0 + 0x50 * rng.randrange(max(cnt, 1))
            tone, keys = rng.choice(tones)
            self.arm.write(t + 0x24, tone)
            key = rng.choice(keys)
            cmd = EWRAM_BASE + 0x100 * rng.randrange(1, 0x100)
            nargs = rng.randrange(0, 4)
            args = [key, rng.randrange(128), rng.randrange(128)][:nargs] + [0x80 + rng.randrange(128)] * 2
            self.wboth(cmd, bytes(args))
            self.arm.write(t + 0x40, struct.pack('<I', cmd))
            self.arm.write(t + 5, bytes([key]))
            for off in (0x04, 0x06, 0x08, 0x09, 0x1B, 0x1D, 0x1E, 0x1F, 0x10, 0x11):
                if rng.random() < 0.5:
                    self.arm.write(t + off, bytes([rng.randrange(256)]))
            self.arm.write(t + 0x08, bytes([rng.randrange(256) if rng.random() < 0.3 else rng.randrange(0, 13)]))
            self.arm.write(p + 9, bytes([rng.randrange(256)]))
            # random channel states
            for k in range(12):
                c = si + 0x50 + 0x40 * k
                if rng.random() < 0.4:
                    self.arm.write(c, bytes([rng.choice([0, 0x80, 0x83, 0x42, 0x02, 0x43, 0x01])]))
                    self.arm.write(c + 0x13, bytes([rng.choice([0, 0x7F, 0xFF, rng.randrange(256)])]))
            for k in range(4):
                c = self.syms['gCgbChans'][0] + 0x40 * k
                if rng.random() < 0.4:
                    self.arm.write(c, bytes([rng.choice([0, 0x80, 0x83, 0x42, 0x02, 0x43])]))
                    self.arm.write(c + 0x13, bytes([rng.randrange(256)]))
            if rng.random() < 0.1:
                self.arm.write(si + 6, bytes([rng.randrange(1, 13)]))  # maxChans
            length = rng.randrange(49)
            self.sanitize_chain(t)
            self.case('4-ply_note', f'ply_note #{i}', 'ply_note', (length, p, t), ret=None)

    # ---- mixer fuzz ---------------------------------------------------------

    def step_mixer_fuzz(self, quick, states, waves, vcount_harness=None):
        rng = self.rng
        n = 600 if quick else 3000
        si = 0x03005410
        for i in range(n):
            iw, io = rng.choice(states)
            self.arm.write(IWRAM_BASE, iw)
            self.arm.write(IO_BASE, io)
            sub = i % 6
            self.arm.write(si + 5, bytes([rng.choice([0, 0, 20, rng.randrange(1, 128), 255])]))   # reverb
            self.arm.write(si + 4, bytes([rng.randrange(0, 9)]))                                   # pcmDmaCounter
            self.arm.write(si + 7, bytes([rng.randrange(16)]))                                     # masterVolume
            if rng.random() < 0.3:
                pcm = bytes(rng.randrange(256) for _ in range(0x630 * 2))
                self.arm.write(si + 0x350, pcm)
            if sub == 5:          # step 3: no channel on; reverb, clear and frame choice
                for k in range(12):
                    self.arm.write(si + 0x50 + 0x40 * k, bytes([rng.choice([0, 0x10, 0x20, 0x08, 0x38])]))
                self.case('3-soundmain-idle-fuzz', f'idle-fuzz #{i}', 'SoundMain')
                continue
            for k in range(rng.randrange(1, 13)):
                c = si + 0x50 + 0x40 * rng.randrange(12)
                wav = rng.choice(waves)
                size = struct.unpack('<I', self.arm.read(wav + 0xC, 4))[0]
                loopf = self.arm.read(wav + 3, 1)[0] & 0xC0
                if sub == 0:      # 5a envelope phases
                    st = rng.choice([0x80, 0xC0, 0x83, 0x82, 0x81, 0x80, 0x43, 0x42, 0x41, 0x44, 0x47,
                                     0x04, 0x05, 0x03, 0x02, 0x01, 0x13, 0x12, 0x11, 0x14, 0x53])
                else:
                    st = rng.choice([0x80, 0x83, 0x82, 0x81, 0x43, 0x03, 0x02, 0x01]) | (0x10 if loopf else 0)
                typ = rng.choice([0, 0, 8]) if sub != 1 else 8
                if sub == 2:
                    typ = 0
                pos = rng.randrange(0, max(size, 1))
                ct = size - pos
                if sub == 3:      # 5d: near the end of the sample
                    ct = rng.randrange(1, 40)
                    pos = max(0, size - ct)
                    ct = size - pos
                fw = rng.getrandbits(23)
                freq = rng.choice([rng.randrange(0x1000, 0x40000), rng.randrange(0, 0x2000000),
                                   0x3FFF, 13379 * 1024 // 627])
                body = struct.pack('<BBBBBBBBBBBBBB', st, typ, rng.randrange(256), rng.randrange(256),
                                   rng.randrange(256), rng.randrange(256), rng.randrange(256), rng.randrange(256),
                                   rng.randrange(128), rng.randrange(256), rng.randrange(256), rng.randrange(256),
                                   rng.randrange(256), rng.choice([0, 1, 2, rng.randrange(256)]))
                self.arm.write(c, body)
                self.arm.write(c + 0x18, struct.pack('<IIII', ct, fw, freq, wav))
                self.arm.write(c + 0x28, struct.pack('<I', wav + 0x10 + pos))
            if sub == 4:          # 5e: maxLines with VCOUNT moving
                self.arm.write(si + 0xC, bytes([rng.randrange(1, 40)]))   # maxLines
                if vcount_harness is not None:
                    start = rng.randrange(0, 228)
                    every = rng.randrange(1, 4)
                    vcount_harness.run_vcount_case(self, start, every, f'mixer-fuzz #{i} maxLines')
                    continue
            self.case('5-soundmain-fuzz', f'mixer-fuzz sub{sub} #{i}', 'SoundMain')


# --------------------------------------------------------------------------

class VcountRunner:
    """Second harness, built with PORT_M4A_TEST_VCOUNT: VCOUNT reads advance."""

    def __init__(self, c):
        self.c = c

    def run_vcount_case(self, dt, start, every, label):
        def seq():
            v, k = start, 0
            while True:
                yield v
                k += 1
                if k % every == 0:
                    v = (v + 1) % 228
        main_c = dt.c
        dt.c = self.c
        dt.ewram_dirty = True
        dt.load_c()
        dt.c.vcount_seq(start, every)
        dt.arm.set_vcount_regs(seq())
        try:
            dt.arm.call('SoundMain')
        except (UcError, RuntimeError) as e:
            mu = dt.arm.mu
            regs = ' '.join(f'r{i}={mu.reg_read(getattr(A, f"UC_ARM_REG_R{i}")):08X}' for i in range(13))
            dt.record('5e-maxLines-arm-error', False,
                      f'{label}: ARM {e} pc={mu.reg_read(A.UC_ARM_REG_PC):08X} sp={mu.reg_read(A.UC_ARM_REG_SP):08X} {regs}')
            dt.arm.set_vcount_regs(None)
            dt.c = main_c
            dt.ewram_dirty = True
            return
        dt.arm.set_vcount_regs(None)
        dt.c.call('SoundMain')
        dt.exclude_vcount = True
        dt.compare('5e-maxLines', label)
        dt.exclude_vcount = False
        dt.c = main_c
        dt.ewram_dirty = True


# Paths through the ARM code, counted with code hooks (labels of src/m4a_1.s).
# (name, label, extra offset); labels inside SoundMainRAM are hooked in its
# IWRAM copy, where the ARM runs them.
COVERAGE = [
    ('mixer: reverb on', 'SoundMainRAM_Reverb', 0),
    ('mixer: reverb off (clear)', 'SoundMainRAM_NoReverb', 0),
    ('mixer: maxLines deadline exit', '_081DCF54', 4),
    ('env: channel off', '_081DCFB0', 0),
    ('env: release', '_081DCFB6', 0),
    ('env: echo (iev/iel) entered', '_081DCFC8', 0),
    ('env: decay/sustain', '_081DCFD6', 0),
    ('env: attack', '_081DCFF8', 0),
    ('fixed rate (type & 8)', '_081DD07C', 0),
    ('fixed: 4-sample fast loop', '_081DD0A8', 0),
    ('fixed: per-sample loop', '_081DD0EC', 0),
    ('fixed: sample end reached', '_081DD164', 0),
    ('interpolated', '_081DD19C', 0),
    ('interp: sample end reached', '_081DD134', 0),
    ('interp: loop wrap', '_081DD148', 0),
    ('interp: end, no loop', '_081DD158', 0),
    ('end in mid-word (status 0)', '_081DD174', 0),
    ('MPlayMain: command', '_081DD8E0', 0),
    ('MPlayMain: wait command', '_081DD92E', 0),
    ('MPlayMain: LFO step', '_081DD95A', 0),
    ('TrackStop: PSG CgbOscOff call', 'TrackStop_1', -4),
    ('ply_note: plain voice', '_081DDB98', 0),
    ('ply_note: rhythm voice', '_081DDB66', 0),
    ('ply_note: rhythm pan', '_081DDB94', 0),
    ('ply_note: PSG channel', '_081DDBC4', 0),
    ('ply_note: DirectSound scan', '_081DDBEC', 0),
    ('ply_note: channel taken', '_081DDC40', 0),
    ('ply_note: no channel', '_081DDCEA', 0),
]


def add_coverage(arm, syms):
    counts = {}
    smr = syms['SoundMainRAM'][0] & ~1
    buf = syms['SoundMainRAM_Buffer'][0]
    for name, label, extra in COVERAGE:
        addr = (syms[label][0] & ~1) + extra
        if smr <= addr < smr + 0x400:
            addr = addr - smr + buf
        counts[name] = 0

        def hit(mu, address, size, user, name=name):
            counts[name] += 1
        arm.mu.hook_add(UC_HOOK_CODE, hit, begin=addr, end=addr)
    return counts


def collect_voices(arm, songs):
    """Voicegroups of the songs; DirectSound waves and playable tones."""
    vgs = set()
    for _, _, hdr in songs:
        vg = struct.unpack('<I', arm.read(hdr + 4, 4))[0]
        if ROM_BASE <= vg < ROM_BASE + ROM_SIZE:
            vgs.add(vg)
    waves, tones = set(), []

    def valid_ptr(x):
        return ROM_BASE <= x < ROM_BASE + 0x00F00000

    def sub_ok(tone):
        typ = tone[0]
        wav = struct.unpack_from('<I', tone, 4)[0]
        if typ & 0xC0:
            return False
        if typ & 7:
            return True
        if not valid_ptr(wav):
            return False
        size = struct.unpack('<I', arm.read(wav + 0xC, 4))[0]
        return 0 < size < 0x200000

    for vg in sorted(vgs):
        for k in range(128):
            tone = arm.read(vg + 12 * k, 12)
            typ = tone[0]
            wav = struct.unpack_from('<I', tone, 4)[0]
            if typ & 0xC0:
                if not valid_ptr(wav):
                    continue
                keys = []
                for key in range(128):
                    if typ & 0x40:
                        tbl = struct.unpack_from('<I', tone, 8)[0]
                        if not valid_ptr(tbl):
                            break
                        idx = arm.read(tbl + key, 1)[0]
                    else:
                        idx = key
                    sub = arm.read(wav + 12 * idx, 12)
                    if sub_ok(sub):
                        keys.append(key)
                        if not (sub[0] & 7):
                            waves.add(struct.unpack_from('<I', sub, 4)[0])
                if keys:
                    tones.append((tone, keys))
            elif sub_ok(tone):
                tones.append((tone, list(range(128))))
                if not (typ & 7):
                    waves.add(wav)
    return sorted(vgs), sorted(waves), tones


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--root', default='.')
    ap.add_argument('--cc', default='gcc')
    ap.add_argument('--seed', type=int, default=8)
    ap.add_argument('--quick', action='store_true')
    ap.add_argument('--frames', type=int, default=None, help='VBlanks per scenario')
    ap.add_argument('--c-source', default='port/audio/m4a_1_pc.c')
    ap.add_argument('-v', '--verbose', action='store_true')
    ap.add_argument('--out', default=None, help='build directory (default build/difftest/audio)')
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    out = os.path.abspath(args.out) if args.out else os.path.join(root, 'build', 'difftest', 'audio')
    os.makedirs(out, exist_ok=True)
    elf = os.path.join(root, 'fireemblem8.elf')
    if not os.path.exists(elf):
        sys.exit('fireemblem8.elf missing: run the GBA make first')
    t0 = time.time()
    rom, syms = read_elf(elf)
    rom_path = os.path.join(out, 'rom.bin')
    with open(rom_path, 'wb') as f:
        f.write(rom)
    exe, cflags = build_harness(root, out, syms, args.cc, args.c_source)
    exe_v, _ = build_harness(root, out, syms, args.cc, args.c_source, ['-DPORT_M4A_TEST_VCOUNT'], '_vcount')
    print('C side:', args.c_source, 'built with PORT_CFLAGS:', ' '.join(cflags))

    arm = Arm(rom, syms)
    c = CSide(exe, rom_path)
    cv = CSide(exe_v, rom_path)
    dt = Difftest(arm, c, syms, args.seed, args.verbose)
    rng = dt.rng

    # Random memory everywhere, so a stray write of any value is seen.
    arm.write(EWRAM_BASE, bytes(rng.getrandbits(8) for _ in range(EWRAM_SIZE)))
    arm.write(IWRAM_BASE, bytes(rng.getrandbits(8) for _ in range(IWRAM_SIZE)))
    arm.write(LOW_BASE, bytes(rng.getrandbits(8) for _ in range(0x4000)))
    arm.write(SOUND_INFO_PTR, b'\0\0\0\0')
    arm.write(IO_BASE, bytes(IO_SIZE))
    dt.load_players()
    coverage = add_coverage(arm, syms)
    dt.setup_translation([c, cv])

    # ---- step 1: m4aSoundInit (driver image after init) ----------------------
    def vseq():
        v = 150
        while True:
            yield v
            v = 150 if v >= 165 else v + 1
    dt.load_c()
    arm.set_vcount_seq(vseq())
    arm.call('m4aSoundInit')
    arm.set_vcount_seq(None)
    c.vcount_toggle(True)
    c.call('m4aSoundInit')
    c.vcount_toggle(False)
    dt.exclude_vcount = True
    dt.compare('1-m4aSoundInit', 'm4aSoundInit')
    dt.exclude_vcount = False
    arm.write(VCOUNT, b'\x00')
    si_plynote = struct.unpack('<I', arm.read(0x03005410 + 0x38, 4))[0]
    if si_plynote != syms['ply_note'][0] | 1:
        sys.exit(f'code pointer check failed: plynote 0x{si_plynote:08X}')
    init_iw = arm.read(IWRAM_BASE, IWRAM_SIZE)
    init_io = arm.read(IO_BASE, IO_SIZE)

    songs = dt.song_list()
    vgs, waves, tones = collect_voices(arm, songs)
    print(f'{len(songs)} songs, {len(vgs)} voicegroups, {len(waves)} DirectSound waves, {len(tones)} tones')

    # ---- scenarios: captured states, frame by frame ------------------------
    frames = args.frames or (300 if args.quick else 1200)
    by_player = {}
    for s in songs:
        by_player.setdefault(s[1], []).append(s[0])
    bgms = by_player.get(0, []) + by_player.get(1, [])
    ses = [s for p, lst in by_player.items() if p >= 2 for s in lst]
    scen = [
        ('opening', [(0, 'm4aSongNumStart', (1,))]),
        ('map9+se', [(0, 'm4aSongNumStart', (9,))] + [(f, 'm4aSongNumStart', (rng.choice(ses),))
                                                       for f in range(30, frames, 45)]),
        ('title+psg', [(0, 'm4aSongNumStart', (0x43,))] + [(f, 'm4aSongNumStart', (0x65 + (f // 13) % 7,))
                                                            for f in range(20, frames, 13)]),
    ]
    for k in range(3 if args.quick else 6):
        ev = [(0, 'm4aSongNumStart', (rng.choice(bgms),))]
        for f in range(5, frames, rng.randrange(8, 30)):
            r = rng.random()
            p = rng.choice(dt.players)
            if r < 0.55:
                ev.append((f, 'm4aSongNumStart', (rng.choice(ses + bgms),)))
            elif r < 0.62:
                ev.append((f, 'm4aMPlayFadeOut', (p, rng.randrange(1, 8))))
            elif r < 0.67:
                ev.append((f, 'm4aMPlayTempoControl', (p, rng.choice([0x80, 0x100, 0x180, 0x200]))))
            elif r < 0.72:
                ev.append((f, 'm4aMPlayVolumeControl', (p, 0xFFFF, rng.randrange(0, 0x200))))
            elif r < 0.76:
                ev.append((f, 'm4aMPlayPitchControl', (p, 0xFFFF, rng.randrange(-0x200, 0x200) & 0xFFFF)))
            elif r < 0.80:
                ev.append((f, 'm4aMPlayPanpotControl', (p, 0xFFFF, rng.randrange(-64, 64) & 0xFF)))
            elif r < 0.83:
                ev.append((f, 'm4aMPlayModDepthSet', (p, 0xFFFF, rng.randrange(0, 60))))
            elif r < 0.86:
                ev.append((f, 'm4aMPlayLFOSpeedSet', (p, 0xFFFF, rng.randrange(0, 60))))
            elif r < 0.90:
                ev.append((f, 'm4aMPlayStop', (p,)))
            elif r < 0.93:
                ev.append((f, 'm4aMPlayContinue', (p,)))
            elif r < 0.96:
                ev.append((f, 'm4aMPlayFadeOutTemporarily', (p, rng.randrange(1, 8))))
            else:
                ev.append((f, 'm4aSongNumStartOrChange', (rng.choice(bgms),)))
        scen.append((f'random{k}', ev))

    states = []
    for name, events in scen:
        arm.write(IWRAM_BASE, init_iw)
        arm.write(IO_BASE, init_io)
        evmap = {}
        for f, fn, a in events:
            evmap.setdefault(f, []).append((fn, a))
        for f in range(frames):
            for fn, a in evmap.get(f, []):
                dt.case('api', f'{name} f{f} {fn}{a}', fn, a, ret=None)
            arm.track_writes = True
            dt.frame(f'{name} f{f}')
            arm.track_writes = False
            if f % 7 == 3:
                states.append((arm.read(IWRAM_BASE, IWRAM_SIZE), arm.read(IO_BASE, IO_SIZE)))
        print(f'scenario {name}: {frames} VBlanks, {time.time() - t0:.0f} s')

    # ---- step 1: small routines and chains -----------------------------------
    arm.write(IWRAM_BASE, init_iw)
    arm.write(IO_BASE, init_io)
    dt.step1(args.quick)
    print(f'step 1 routines done, {time.time() - t0:.0f} s')
    base_iw, base_io = states[len(states) // 2]
    dt.step1_chains(args.quick, base_iw, base_io)
    dt.step_handlers(args.quick, base_iw, base_io, vgs)
    dt.step_note(args.quick, states, tones)
    print(f'handlers and ply_note done, {time.time() - t0:.0f} s')
    dt.step_mixer_fuzz(args.quick, states, waves, VcountRunner(cv))
    print(f'mixer fuzz done, {time.time() - t0:.0f} s')

    c.close()
    cv.close()

    print()
    print('Results (cases, failures):')
    total = fails = 0
    for step in sorted(dt.results):
        n, bad = dt.results[step]
        total += n
        fails += bad
        print(f'  {step:32s} {n:8d} {bad:6d}')
    print(f'  {"total":32s} {total:8d} {fails:6d}')
    outside = arm.writes_outside
    print(f'ARM writes outside the driver image during the scenario frames: {len(outside)} granules'
          + (': ' + ' '.join(f'{a:08X}' for a in sorted(outside)[:10]) if outside else ''))
    if dt.fail_examples:
        print('First failures:')
        for m in dt.fail_examples[:40]:
            print('  ' + m)
    print('ARM paths taken (code hook hits over the whole run):')
    for name, _, _ in COVERAGE:
        print(f'  {name:36s} {coverage[name]:10d}')
    missed = [n for n, _, _ in COVERAGE if coverage[n] == 0]
    if missed:
        print('NOT COVERED: ' + ', '.join(missed))
    print(f'time {time.time() - t0:.0f} s')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
