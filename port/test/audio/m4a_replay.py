#!/usr/bin/env python3
"""P3.3 compare 3 (replay) and P3.4a: the PC driver against mGBA, one VBlank at a time.

port-plan/audio-design.md 5.4 item 3, with mGBA's own records as the reference
instead of the Unicorn runner. Between two records S(v) and S(v+1) of an mGBA
capture (port/test/oracle/sound_capture.lua) the GBA ran its VBlank handler
(m4aSoundVSync, m4aSoundMain) and whatever m4a API calls the game made. The
capture records those calls on their own, at debugger breakpoints on every m4a
entry point and its return (X.gba.calls; sound_capture.lua, snd_calls.py), not
from the state. The C side (m4a_harness.c: src/m4a.c and port/audio/m4a_1_pc.c,
the same objects' sources as the port) is loaded with S(v) and makes the same
top-level calls, with the GBA's arguments, in the GBA's order. The result must
equal S(v+1):
  - the track arrays and the whole 0x03005410-0x0300679F block (gSoundInfo with
    both PCM lanes, the players, gMPlayJumpTable, gCgbChans, gMPlayMemAccArea),
    code pointers translated by name;
  - the sound registers' readable bits (0x04000060-0x0400008F; wave RAM is
    banked and SOUNDCNT_X bits 0-3 are the PSG's own status, both left out);
  - DMA1CNT_H and DMA2CNT_H (destination bits left out, see below).
A call that ran whole between two records is replayed and nothing is excused
for it. A call that is not clean (a record was taken, or the VBlank handler
entered the driver, while it was open) cannot be replayed: the GBA ran part of
it on each side. It is not run, and on the VBlanks it spans only the bytes it
can touch are left out: its player's block and tracks and the channels on those
tracks (with gCgbChans and the sound registers when one is a PSG channel), the
SoundInfo header too for a song start whose header sets reverb, and everything
for a call with no player (m4aSoundMode ...). Lag and the VBlank count since
boot do not matter, because every case starts from the GBA's state.

Round 2 review (2026-10-19): the records taken before the boot m4aSoundInit
returns are skipped, not excused (the first pair replayed starts after it);
PASS needs every PCM lane equal on every replayed VBlank, excused ones too
(but on the VBlanks a not-clean m4aSoundMain spans: integration 11, below);
a call still open when the capture ended (W_OUT -1) that was open across more
than the last record fails the run (nothing bounds what it touched), while
one that entered in the last window is scoped like any interrupted call.
Integration review (2026-10-20): the same bound for every call that is not
clean. A real m4a call is open across at most one record (MAX_OPEN_RECORDS;
the boot m4aSoundInit, which spans two, is skipped), so one open across more
(W_OUT - W_IN > 1) excuses nothing and fails the run: before, its scope was
left out of every VBlank it spanned, however many.

Integration 11 (2026-10-23; round 10's sound review, decision 2, taken as recommended: the user
said "I trust your judgement"): the one excuse for PCM lanes. A record taken while the GBA's mixer
ran (a not-clean m4aSoundMain open across it: an IRQ handler's call that the next VBlank's record
interrupted half-way through its mix, route e15 v 1,371) holds a half-written buffer that no
replay can make. On the VBlanks such a call spans (the pair that ends at that record and the pair
that starts at it) the PCM lanes are excused (MIXER), counted and printed apart; everything else
on those VBlanks is judged as before (Scope: m4aSoundMain has no player, so everything else is
left out already). No other call excuses a lane.

Sound on any route (2026-10-21, port-snd10): the rule is CallPlan (the call
record classified once), replay_range (the VBlank-by-VBlank judgement) and
report (the summary); audio/snd_route.py runs the same three on any route in
chunks. A capture resumed from a save state (gate0_capture.lua, "# resumed")
has no boot m4aSoundInit: nothing is skipped, and its first record must show
the driver set up and idle (SOUND_INFO_PTR set, gSoundInfo.ident ID_NUMBER).

    python3 port/test/audio/m4a_replay.py build/gate0/snd/title.gba.snd [--calls F] [--from N] [--count K]
    ... --legacy   the rule before 2026-10-19, for comparison: the handler only, and a
                   VBlank whose players or tracks differ counted as a main-thread call
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import m4a_difftest as dt  # noqa: E402  (read_elf, build_harness, CSide)
import snd_calls  # noqa: E402

REC = 8236
O_TRK, O_SND = 16, 16 + 3120
O_PTR = O_SND + 5008
O_IO = O_PTR + 4
O_DMA = O_IO + 80
TRK, SND = 0x03001E38, 0x03005410
TRK_LEN, SND_LEN = 3120, 5008
SOUNDINFO_CODE_FIELDS = [0x20, 0x28, 0x2C, 0x30, 0x38, 0x3C]
CGB = 0x03006510
LANES = (0x350, 0xFB0)
CHAN0, NCHAN, NCGB, CHAN_LEN = SND + 0x50, 12, 4, 0x40
PLAYER_LEN, TRACK_SIZE = 0x40, 0x50
P_TRACKCOUNT, P_TRACKS, CH_TRACK = 0x08, 0x2C, 0x2C
NUM_PLAYERS = 9

HANDLER = ('m4aSoundVSync', 'm4aSoundMain')
# What a call that is not clean can touch (src/m4a.c).
PLAYER_ARG = {'m4aMPlayStop', 'm4aMPlayContinue', 'm4aMPlayFadeOut', 'm4aMPlayFadeOutTemporarily',
              'm4aMPlayFadeIn', 'm4aMPlayImmInit', 'm4aMPlayTempoControl', 'm4aMPlayVolumeControl',
              'm4aMPlayPitchControl', 'm4aMPlayPanpotControl', 'm4aMPlayModDepthSet', 'm4aMPlayLFOSpeedSet',
              'MPlayContinue', 'MPlayFadeOut', 'MPlayStart'}
SONG_ARG = {'m4aSongNumStart', 'm4aSongNumStartOrChange', 'm4aSongNumStartOrContinue', 'm4aSongNumStop',
            'm4aSongNumContinue'}
ALL_PLAYERS = {'m4aMPlayAllStop', 'm4aMPlayAllContinue'}
SONG_START = {'MPlayStart', 'm4aSongNumStart', 'm4aSongNumStartOrChange', 'm4aSongNumStartOrContinue'}
# The most records a call that is not clean may be open across and still be
# scoped (integration review, 2026-10-20). Every m4a call returns well within
# a frame: in the title and map captures the calls that are not clean, the
# boot m4aSoundInit aside (windows 11-13, skipped), are three
# m4aSongNumStart(0x7A) open across one record each (map windows 9,961-9,962,
# 10,324-10,325 and 11,051-11,052). One open longer cannot be bounded.
MAX_OPEN_RECORDS = 1
# The mixer: the one call whose not-clean span excuses the PCM lanes (integration 11, above).
MIXER = ('m4aSoundMain',)

# Readable bits, 0x04000060-0x0400008F (GBATEK; mGBA stores writes masked so).
READ_MASK16 = {
    0x60: 0x007F, 0x62: 0xFFC0, 0x64: 0x4000, 0x68: 0xFFC0, 0x6C: 0x4000,
    0x70: 0x00E0, 0x72: 0xE000, 0x74: 0x4000, 0x78: 0xFF00, 0x7C: 0x40FF,
    0x80: 0xFF77, 0x82: 0x770F, 0x84: 0x0080, 0x88: 0xC3FE,
}


def io_image(io80):
    return {off: struct.unpack_from("<H", io80, off - 0x60)[0] & m for off, m in READ_MASK16.items()}


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


# DMAxCNT_H, destination control (bits 5-6) left out: mGBA stores "fixed" (0x40)
# for a FIFO DMA when it is enabled, the C side keeps what the driver wrote
# (0xB600, increment). Nothing reads those bits back: the driver tests only bit 9
# (repeat; m4a.c:315, m4a_1.s:894-903), and the FIFO model only the enable bit
# and the timing.
def cnth(b, o):
    return struct.unpack_from("<H", b, o)[0] & ~0x0060


class State:
    """One side of a compare: tracks, sound block (GBA code pointers), I/O, DMA."""
    __slots__ = ("trk", "snd", "io", "dma")

    def __init__(self, trk, snd, io, dma):
        self.trk, self.snd, self.io, self.dma = trk, snd, io, dma

    @classmethod
    def of_record(cls, r):
        return cls(r[O_TRK:O_SND], r[O_SND:O_PTR], r[O_IO:O_DMA], r[O_DMA:O_DMA + 8])

    def io_ok(self, o):
        return io_image(self.io) == io_image(o.io)

    def dma_ok(self, o):
        return (cnth(self.dma, 2), cnth(self.dma, 6)) == (cnth(o.dma, 2), cnth(o.dma, 6))

    def equal(self, o):
        return self.trk == o.trk and self.snd == o.snd and self.io_ok(o) and self.dma_ok(o)


class Scope:
    """Bytes a call that is not clean can touch, left out of the compare."""

    def __init__(self):
        self.trk = bytearray(TRK_LEN)
        self.snd = bytearray(SND_LEN)
        self.io = False
        self.all = False
        self.what = []

    def mark(self, which, base, addr, n):
        buf = self.trk if which == "trk" else self.snd
        lo, hi = max(addr - base, 0), min(addr - base + n, len(buf))
        if lo < hi:
            buf[lo:hi] = b"\x01" * (hi - lo)


class Ctx:
    def __init__(self, rom, syms):
        self.rom = rom
        self.song_table = syms["gSongTable"][0]
        self.mplay_table = syms["gMPlayTable"][0]
        self.players = [self.rom32(self.mplay_table + 12 * i) for i in range(NUM_PLAYERS)]

    def rom32(self, addr):
        return u32(self.rom, addr - 0x08000000)

    def song(self, n):
        """(header, player info) of song n (gSongTable, gMPlayTable)."""
        header = self.rom32(self.song_table + 8 * n)
        ms = struct.unpack_from("<H", self.rom, self.song_table + 8 * n + 4 - 0x08000000)[0]
        return header, self.rom32(self.mplay_table + 12 * ms)

    def add_scope(self, scope, call, states):
        name = call.name
        if name in PLAYER_ARG:
            players = [call.args[0]]
        elif name in SONG_ARG:
            players = [self.song(call.args[0] & 0xFFFF)[1]]
        elif name in ALL_PLAYERS:
            players = self.players
        else:
            scope.all = True
            scope.what.append(f"{name} (no player: everything)")
            return
        for p in players:
            scope.mark("snd", SND, p, PLAYER_LEN)
            for st in states:
                t, n = u32(st.snd, p - SND + P_TRACKS), st.snd[p - SND + P_TRACKCOUNT]
                scope.mark("trk", TRK, t, TRACK_SIZE * n)
                for i in range(NCHAN + NCGB):
                    ch = CHAN0 + CHAN_LEN * i if i < NCHAN else CGB + CHAN_LEN * (i - NCHAN)
                    if t <= u32(st.snd, ch - SND + CH_TRACK) < t + TRACK_SIZE * n:
                        scope.mark("snd", SND, ch, CHAN_LEN)
                        if i >= NCHAN:
                            scope.io = True
        if name in SONG_START:
            header = call.args[1] if name == "MPlayStart" else self.song(call.args[0] & 0xFFFF)[0]
            if 0x08000000 <= header < 0x0A000000 and self.rom[header + 3 - 0x08000000] & 0x80:
                scope.mark("snd", SND, SND, 8)       # m4aSoundMode(reverb): ident, reverb
        scope.what.append(f"{name} (player{'s' if len(players) > 1 else ''} "
                          + ",".join(f"{p:08X}" for p in players) + (", PSG" if scope.io else "") + ")")


def where_differs(got, want, players, scope=None):
    """Descriptions of the differences outside scope (empty: equal there)."""
    if scope is not None and scope.all:
        return []
    out = []
    ms = scope.snd if scope else None
    mt = scope.trk if scope else None
    diffs = [i for i in range(SND_LEN) if got.snd[i] != want.snd[i] and not (ms and ms[i])]
    for p in players:
        lo = p - SND
        if any(lo <= i < lo + PLAYER_LEN for i in diffs):
            out.append(f"player@{p:08X} " + ",".join(f"+{i - lo:X}" for i in diffs if lo <= i < lo + PLAYER_LEN)[:60])
    head = [i for i in diffs if i < LANES[0]]
    if head:
        out.append("SoundInfo/channels " + ",".join(f"+{i:X}" for i in head)[:80])
    if any(LANES[0] <= i < LANES[1] for i in diffs):
        out.append("PCM lanes")
    cg = [i for i in diffs if CGB - SND <= i < CGB - SND + 0x100]
    if cg:
        out.append("gCgbChans " + ",".join(f"+{i - (CGB - SND):X}" for i in cg)[:60])
    rest = [i for i in diffs if i >= LANES[1] and not (CGB - SND <= i < CGB - SND + 0x100)
            and not any(p - SND <= i < p - SND + PLAYER_LEN for p in players)]
    if rest:
        out.append("other " + ",".join(f"{SND + i:08X}" for i in rest)[:60])
    td = [i for i in range(TRK_LEN) if got.trk[i] != want.trk[i] and not (mt and mt[i])]
    if td:
        out.append("tracks " + ",".join(f"{TRK + i:08X}:{want.trk[i]:02X}/{got.trk[i]:02X}" for i in td[:6]))
    if not (scope and scope.io) and not got.io_ok(want):
        g, h = io_image(want.io), io_image(got.io)
        out.append("I/O " + ",".join(f"{0x04000000 + k:08X}" for k in g if g[k] != h[k]))
    if not got.dma_ok(want):
        out.append(f"DMA1CNT_H/DMA2CNT_H GBA {want.dma[2:4][::-1].hex()}/{want.dma[6:8][::-1].hex()}"
                   f" PC {got.dma[2:4][::-1].hex()}/{got.dma[6:8][::-1].hex()}")
    return out


class Runner:
    def __init__(self, c, syms, players):
        self.c = c
        g2h, h2g = {}, {}
        for name, host in c.sym.items():
            gba = syms[name][0] | 1
            g2h[gba], h2g[host] = host, gba
        self.g2h, self.h2g = g2h, h2g
        code = [SND + o for o in SOUNDINFO_CODE_FIELDS]
        code += [syms["gMPlayJumpTable"][0] + 4 * i for i in range(36)]
        code += [p + 0x38 for p in players]
        self.code = code

    def translate(self, block, table):
        b = bytearray(block)
        for addr in self.code:
            off = addr - SND
            if 0 <= off <= len(b) - 4:
                v = struct.unpack_from("<I", b, off)[0]
                if v in table:
                    struct.pack_into("<I", b, off, table[v])
        return bytes(b)

    def load(self, r):
        c = self.c
        c.write(TRK, r[O_TRK:O_SND])
        c.write(SND, self.translate(r[O_SND:O_PTR], self.g2h))
        c.write(0x03007FF0, r[O_PTR:O_IO])
        c.write(0x04000060, r[O_IO:O_DMA])
        c.write(0x040000C4, r[O_DMA:O_DMA + 4])
        c.write(0x040000D0, r[O_DMA + 4:O_DMA + 8])

    def result(self):
        c = self.c
        return State(c.read(TRK, TRK_LEN), self.translate(c.read(SND, SND_LEN), self.h2g),
                     c.read(0x04000060, 80), c.read(0x040000C4, 4) + c.read(0x040000D0, 4))


def run_legacy(a, run, data, n, end, players):
    """The rule before 2026-10-19: m4aSoundVSync and m4aSoundMain only; a differing
    VBlank whose players or tracks differ is taken as a main-thread call."""
    equal = differ = skipped = 0
    lane_equal = 0
    no_api = []
    for v in range(a.start, end):
        r = data[v * REC:(v + 1) * REC]
        r1 = data[(v + 1) * REC:(v + 2) * REC]
        if u32(r, O_PTR) != SND:
            skipped += 1
            continue
        run.load(r)
        run.c.call("m4aSoundVSync")
        run.c.call("m4aSoundMain")
        got, want = run.result(), State.of_record(r1)
        lane_equal += got.snd[LANES[0]:LANES[1]] == want.snd[LANES[0]:LANES[1]]
        if got.equal(want):
            equal += 1
            continue
        differ += 1
        api = got.trk != want.trk or any(got.snd[p - SND:p - SND + PLAYER_LEN] != want.snd[p - SND:p - SND + PLAYER_LEN]
                                         for p in players)
        if not api:
            no_api.append(v)
    total = equal + differ
    print(f"legacy rule: replayed {total} VBlanks ({skipped} before sound init skipped): {equal} fully equal,"
          f" {differ} differ, {differ - len(no_api)} of them taken as a main-thread call; PCM lanes equal on"
          f" {lane_equal}")
    ok = lane_equal == total and not no_api
    print("RESULT (legacy rule):", "PASS" if ok else "FAIL")
    return 0 if ok else 1


ID_NUMBER = 0x68736D53   # SoundInfo.ident when the driver is set up and no m4a call holds it


class CallPlan:
    """The capture's call record, classified once for the whole capture: every
    chunk of a chunked run (audio/snd_route.py) uses the same plan, so a chunk
    is judged exactly as the same records are in one run.

    From power-on, the records before the boot m4aSoundInit returns are
    skipped (round 2 review). A capture resumed from a save state
    (gate0_capture.lua, a "# resumed" line) has no boot call: it starts after
    it by construction (the photo is taken at a tick, after the boot), and its
    first record must show the driver set up and idle (SOUND_INFO_PTR set,
    gSoundInfo.ident ID_NUMBER), or nothing is replayed."""

    def __init__(self, calls, v_first, n, resumed=None):
        self.v_first, self.n = v_first, n
        self.last_window = v_first + n - 2
        self.resumed = resumed
        self.boot_init = None
        self.init_end = v_first - 1          # resumed: nothing skipped
        self.error = None
        if resumed is None:
            inits = [cl for cl in calls if cl.name == "m4aSoundInit"]
            self.boot_init = inits[0] if inits else None
            if self.boot_init is None or self.boot_init.w_out < 0:
                self.error = ("no m4aSoundInit call recorded" if self.boot_init is None
                              else f"m4aSoundInit (window {self.boot_init.w_in}) never returned")
            else:
                self.init_end = self.boot_init.w_out
        self.clean_by_window, self.unclean, self.unbounded = {}, [], []
        for cl in calls:
            if cl is self.boot_init:
                continue
            if cl.clean:
                self.clean_by_window.setdefault(cl.w_in, []).append(cl)
                continue
            # The records taken while it was open: W_OUT - W_IN. A call still open
            # when the capture ended was open at least across the last record
            # (v = last_window + 1). Open across more than MAX_OPEN_RECORDS,
            # nothing bounds what it may have touched, so it excuses nothing and
            # fails the run (round 2 review for a call open at the end, integration
            # review for one that returned). One that entered in the last window
            # and is still open spans only that window, like any call interrupted
            # by one record, and is scoped as usual.
            records = (cl.w_out if cl.w_out >= 0 else self.last_window + 1) - cl.w_in
            if records > MAX_OPEN_RECORDS or records < 0:
                self.unbounded.append(cl)
            else:
                self.unclean.append(cl)

    def touching(self, v):
        """The calls that are not clean and were open during window v."""
        return [u for u in self.unclean if u.spans(v, self.last_window)]


class Stats:
    """What a range of record pairs gave; chunks add up (merge)."""
    COUNTS = ("equal", "excused", "differ", "skipped", "lane_equal", "lane_excused", "cgb_equal", "io_equal",
              "api_windows", "api_equal")

    def __init__(self):
        for k in self.COUNTS:
            setattr(self, k, 0)
        self.replayed, self.excused_by = {}, {}
        self.odd_handler, self.unreplayable, self.differing, self.no_ptr = [], [], [], []
        self.lines = []          # (v, text): the differing VBlanks described, and --show-excused lines
        self.bad_start = None    # resumed: the first record does not show the driver idle

    def merge(self, o):
        for k in self.COUNTS:
            setattr(self, k, getattr(self, k) + getattr(o, k))
        for d, od in ((self.replayed, o.replayed), (self.excused_by, o.excused_by)):
            for k, n_ in od.items():
                d[k] = d.get(k, 0) + n_
        for k in ("odd_handler", "unreplayable", "differing", "no_ptr", "lines"):
            getattr(self, k).extend(getattr(o, k))
        self.bad_start = self.bad_start or o.bad_start
        return self

    @property
    def total(self):
        return self.equal + self.excused + self.differ


def replay_range(run, ctx, plan, data, base, lo, hi, show, show_excused, path="records"):
    """Replays the record pairs (i, i + 1) for i in [lo, hi). data holds the
    records base, base + 1, ... (it must reach record hi). The rule of the
    whole-capture run, VBlank by VBlank."""
    c = run.c
    st = Stats()
    shown = 0
    rec = lambda i: data[(i - base) * REC:(i - base + 1) * REC]   # noqa: E731
    if plan.resumed is not None and lo == 0:
        r0 = rec(lo)
        if u32(r0, O_PTR) != SND or u32(r0, O_SND) != ID_NUMBER:
            st.bad_start = (f"the first record (v {u32(r0, 8)}) has SOUND_INFO_PTR {u32(r0, O_PTR):08X} and"
                            f" gSoundInfo.ident {u32(r0, O_SND):08X}: the driver is not set up and idle")
    for i in range(lo, hi):
        r, r1 = rec(i), rec(i + 1)
        v = u32(r, 8)
        if u32(r1, 8) != v + 1:
            sys.exit(f"{path}: record {i + 1} is v {u32(r1, 8)}, expected {v + 1}")
        if v <= plan.init_end:
            st.skipped += 1   # taken before m4aSoundInit returned
            continue
        if u32(r, O_PTR) != SND:
            st.no_ptr.append(v)   # after m4aSoundInit returned: a failure, not a skip
            continue
        run.load(r)
        evs = plan.clean_by_window.get(v, [])
        if tuple(e.name for e in evs if e.name in HANDLER) != HANDLER:
            st.odd_handler.append(v)
        api = False
        for e in evs:
            if e.name not in c.fnid:
                st.unreplayable.append(e)
                continue
            c.call(e.name, e.args[:3])
            st.replayed[e.name] = st.replayed.get(e.name, 0) + 1
            api |= e.name not in HANDLER
        got, want = run.result(), State.of_record(r1)
        lanes_ok = got.snd[LANES[0]:LANES[1]] == want.snd[LANES[0]:LANES[1]]
        st.lane_equal += lanes_ok
        if not lanes_ok and any(u.name in MIXER for u in plan.touching(v)):
            st.lane_excused += 1   # the mixer was caught half-way (MIXER)
        st.cgb_equal += got.snd[CGB - SND:CGB - SND + 0x100] == want.snd[CGB - SND:CGB - SND + 0x100]
        st.io_equal += got.io_ok(want)
        st.api_windows += api
        if got.equal(want):
            st.equal += 1
            st.api_equal += api
            continue
        touching = plan.touching(v)
        if touching:
            scope = Scope()
            for u in touching:
                ctx.add_scope(scope, u, [State.of_record(r), want, got])
            if not where_differs(got, want, ctx.players, scope):
                st.excused += 1
                key = "; ".join(scope.what)
                st.excused_by[key] = st.excused_by.get(key, 0) + 1
                if show_excused:
                    st.lines.append((v, f"  record {i} -> {i + 1} (v {v}): excused for {key}; inside that scope it"
                                        " differs in " + "; ".join(where_differs(got, want, ctx.players))))
                continue
        else:
            scope = None
        st.differ += 1
        st.differing.append(v)
        if shown < show:
            shown += 1
            names = [e.name for e in evs]
            calls_here = ", ".join(f"{k} x{names.count(k)}" if names.count(k) > 1 else k
                                   for k in dict.fromkeys(names)) or "none"
            st.lines.append((v, f"  record {i} -> {i + 1} (v {v}; calls: {calls_here}): differs in "
                                + "; ".join(where_differs(got, want, ctx.players, scope))))
    return st


def report(st, plan, first_record, end, show):
    """The summary of the whole-capture run (also for the merged chunks); True on PASS."""
    lines = sorted(st.lines, key=lambda x: x[0])
    shown = 0
    for v, text in lines:
        if "excused for" in text:
            print(text)
        elif shown < show:
            shown += 1
            print(text)
    total = st.total
    differing = sorted(st.differing)
    print(f"replayed {total} VBlanks (records {first_record}-{end}; {st.skipped} taken before m4aSoundInit returned"
          f" skipped): {st.equal} fully equal, {st.excused} equal outside the scope of a call that was not clean,"
          f" {st.differ} differ" + (f" (v {differing[:10]})" if differing else ""))
    if plan.resumed is not None:
        print(f"  resumed from a save state at tick {plan.resumed[0]} (record v {plan.resumed[1]}): no boot"
              " m4aSoundInit in this capture; its first record must show the driver set up and idle")
    else:
        print(f"  m4aSoundInit (boot) windows {plan.boot_init.w_in}-{plan.boot_init.w_out}: the records up to v"
              f" {plan.init_end} are not replayed")
    print("  calls replayed, as the GBA made them: "
          + ", ".join(f"{k} {n_}" for k, n_ in sorted(st.replayed.items(), key=lambda x: -x[1])))
    print(f"  VBlanks with a game sound call (besides the handler's two): {st.api_windows},"
          f" {st.api_equal} of them fully equal")
    print(f"  PCM lanes (the FIFO bytes) equal on {st.lane_equal}; gCgbChans on {st.cgb_equal};"
          f" readable sound registers on {st.io_equal}")
    if st.lane_excused:
        print(f"  PCM lanes excused on {st.lane_excused} VBlanks, only where a not-clean m4aSoundMain spans them"
              " (the GBA's mixer caught half-way through its mix: a half-written buffer no replay makes)")
    for u in plan.unclean:
        print(f"  not clean (not replayed): {u.name} {u.args[0]:08X} windows {u.w_in}-"
              + (f"{u.w_out}" if u.w_out >= 0 else f"end (open at capture end, last window {plan.last_window})"))
    for k, n_ in st.excused_by.items():
        print(f"  excused on {n_} VBlanks, only for: {k}")
    odd = sorted(st.odd_handler)
    if odd:
        print(f"  VBlanks whose handler calls were not m4aSoundVSync then m4aSoundMain: {len(odd)}"
              f" (v {odd[:10]})")
    if st.unreplayable:
        print(f"  top-level calls the C harness cannot make: {len(st.unreplayable)}"
              f" ({sorted(set(e.name for e in st.unreplayable))}): FAIL")
    for u in plan.unbounded:
        if u.w_out < 0:
            print(f"  still open at capture end, open across more than the last record: {u.name} {u.args[0]:08X}"
                  f" from window {u.w_in} (last window {plan.last_window}): nothing bounds it, FAIL")
        elif u.w_out < u.w_in:
            print(f"  not clean, returns (window {u.w_out}) before it enters (window {u.w_in}): {u.name}"
                  f" {u.args[0]:08X}: a broken call record, FAIL")
        else:
            print(f"  not clean and open across {u.w_out - u.w_in} records: {u.name} {u.args[0]:08X} windows"
                  f" {u.w_in}-{u.w_out} (a real m4a call is open across at most {MAX_OPEN_RECORDS}): nothing"
                  " bounds it, FAIL")
    no_ptr = sorted(st.no_ptr)
    if no_ptr:
        print(f"  records after m4aSoundInit returned without SOUND_INFO_PTR set: {len(no_ptr)} (v {no_ptr[:10]}): FAIL")
    if st.bad_start:
        print(f"  resumed capture: {st.bad_start}: FAIL")
    if st.lane_equal + st.lane_excused != total:
        print(f"  PCM lanes differ on {total - st.lane_equal - st.lane_excused} VBlanks: FAIL")
    ok = (st.differ == 0 and not st.unreplayable and not plan.unbounded and not no_ptr
          and st.lane_equal + st.lane_excused == total and total > 0 and not st.bad_start)
    print("RESULT:", "PASS" if ok else "FAIL",
          "(every VBlank equal after the GBA's own sound calls, every PCM lane equal but where a not-clean"
          " m4aSoundMain spans; a call that was not clean excused only for what it can touch; records before"
          " m4aSoundInit returned skipped)")
    return ok


def harness(root, out, c_source="port/audio/m4a_1_pc.c", m4a_source="src/m4a.c"):
    """(rom, syms, CSide, Ctx, Runner): the C driver built and started."""
    rom, syms = dt.read_elf(os.path.join(root, "fireemblem8.elf"))
    os.makedirs(out, exist_ok=True)
    rom_path = os.path.join(out, "rom.bin")
    with open(rom_path, "wb") as f:
        f.write(rom)
    exe, _ = dt.build_harness(root, out, syms, "gcc", c_source, m4a_source=m4a_source)
    c = dt.CSide(exe, rom_path)
    ctx = Ctx(rom, syms)
    return rom, syms, c, ctx, Runner(c, syms, ctx.players)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("gba_snd")
    ap.add_argument("--calls", help="the capture's call record (default: GBA_SND with .snd replaced by .calls)")
    ap.add_argument("--from", dest="start", type=int, default=0)
    ap.add_argument("--count", type=int)
    ap.add_argument("--root", default=".")
    ap.add_argument("--out", default="build/difftest/replay")
    ap.add_argument("--show", type=int, default=8, help="differing VBlanks to describe")
    ap.add_argument("--show-excused", action="store_true", help="also describe the excused VBlanks")
    ap.add_argument("--legacy", action="store_true", help="the old rule (no call record), for comparison")
    ap.add_argument("--c-source", default="port/audio/m4a_1_pc.c", help="C driver source (mutation check)")
    ap.add_argument("--m4a-source", default="src/m4a.c", help="m4a.c source (mutation check)")
    a = ap.parse_args()

    root = os.path.abspath(a.root)
    out = os.path.abspath(a.out)
    rom, syms, c, ctx, run = harness(root, out, a.c_source, a.m4a_source)

    data = open(a.gba_snd, "rb").read()
    n = len(data) // REC
    end = n - 1 if a.count is None else min(n - 1, a.start + a.count)
    if a.legacy:
        rc = run_legacy(a, run, data, n, end, ctx.players)
        c.close()
        return rc

    calls_path = a.calls or (a.gba_snd[:-4] if a.gba_snd.endswith(".snd") else a.gba_snd) + ".calls"
    info = {}
    calls, _, (cap_records, cap_from) = snd_calls.load(calls_path, syms, info)
    v_first = u32(data, 8) if n else 0
    if cap_records != n or cap_from != v_first:
        sys.exit(f"{calls_path}: written for {cap_records} records from {cap_from}, {a.gba_snd} has {n} from"
                 f" {v_first}: not the same capture")
    # Records before m4aSoundInit returns (AgbMain's boot call, the first one)
    # are skipped, not excused: the driver is not set up yet, and a record taken
    # while it runs is half-initialised state (round 2 review). The first pair
    # replayed starts at the first record taken after it returned. A capture
    # resumed from a save state has none (CallPlan).
    plan = CallPlan(calls, v_first, n, info.get("resumed"))
    if plan.error:
        print(f"{calls_path}: {plan.error}: nothing can be replayed")
        print("RESULT: FAIL")
        c.close()
        return 1
    st = replay_range(run, ctx, plan, data, 0, a.start, end, a.show, a.show_excused, a.gba_snd)
    c.close()
    return 0 if report(st, plan, a.start, end, a.show) else 1


if __name__ == "__main__":
    sys.exit(main())
