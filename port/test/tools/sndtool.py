#!/usr/bin/env python3
"""Sound checks for the FE8 PC port (port-plan/audio-design.md sections 5 and 6).

Inputs (ROM-derived; they stay in build/, never in git):
  X.gba.snd   mGBA records, port/test/oracle/sound_capture.lua
  X.pc.snd    PC records, FE8PC_SND_RECORD (port/audio/fifo_pc.c)
  X.pc.fifo8  PC FIFO stream, FE8PC_AUDIO_DUMP: per VBlank 224 bytes of FIFO A, then B
  X.pc.lr16   PC output, FE8PC_AUDIO_DUMP: per VBlank 224 s16 L,R frames

Record (8,236 bytes): "FE8S", u16 version, u16 0, u32 v, u32 gGameClock; IWRAM
0x03001E38+3120; IWRAM 0x03005410+5008; SOUND_INFO_PTR; I/O 0x04000060+80;
DMA1CNT; DMA2CNT. Record v is taken at line 160 before VBlank v's IRQ, so it
holds the state after VBlank v-1's handler.

Commands:
  p31 PREFIX              P3.1: sizes of PREFIX.lr16/.fifo8, VBlank count, and
                          the pcmDmaCounter phase in PREFIX.snd
  selfcheck PREFIX        the PC .fifo8 equals the slot rule applied to the PC records
  fifo GBA.snd PC.snd PC.fifo8 --song N [--count K]
                          P3.3 compare 1: the GBA stream (slot 7-c of record v+1)
                          against the PC stream, aligned at the start of song N
  psg GBA.snd PC.snd --song N [--count K]
                          P3.4a: gCgbChans and the readable sound register bits,
                          per VBlank, aligned the same way
  wav LR16 OUT.wav [--from-vblank V] [--seconds S]
                          raw 13,379 Hz s16 stereo to WAV, no processing
"""

import argparse
import struct
import sys
import wave

REC = 8236
VB = 224
LANE = 1584
RATE = 13379

O_HDR = 0
O_TRK = 16                       # 0x03001E38
O_SND = O_TRK + 3120             # 0x03005410
O_PTR = O_SND + 5008             # 0x03007FF0
O_IO = O_PTR + 4                 # 0x04000060
O_DMA = O_IO + 80                # DMA1CNT, DMA2CNT
SND_BASE = 0x03005410
TRK_BASE = 0x03001E38

SI_COUNTER, SI_PERIOD, SI_LANES = 0x04, 0x0B, 0x350
BGM1 = 0x03006440                # gMPlayInfo_BGM1 (player 0)
CGB = 0x03006510                 # gCgbChans, 4 x 0x40
SONG_TABLE = 0x08224470


def records(path):
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) % REC:
        # A run that faults ends without flushing (port/hw/main_pc.c OnFault).
        print(f"{path}: ends with a partial record ({len(data) % REC} bytes), ignored")
        data = data[:len(data) - len(data) % REC]
    out = []
    for i in range(0, len(data), REC):
        r = data[i:i + REC]
        if r[:4] != b"FE8S":
            sys.exit(f"{path}: record {i // REC} has no FE8S magic")
        out.append(r)
    return out


def hdr(r):
    _, ver, ticks, v, clock = struct.unpack_from("<4sHHII", r, 0)
    return v, clock


def lag_records(recs):
    """Indices of records whose frame completed no logic tick (GBA lag VBlanks),
    after the first tick. The tick count is header bytes 6-7."""
    out = []
    seen = False
    for i in range(1, len(recs)):
        t0 = struct.unpack_from("<H", recs[i - 1], 6)[0]
        t1 = struct.unpack_from("<H", recs[i], 6)[0]
        if t1 != t0:
            seen = True
        elif seen:
            out.append(i)
    return out


def snd(r, addr, n=1):
    o = O_SND + addr - SND_BASE
    return r[o:o + n]


def u32(b):
    return struct.unpack("<I", b)[0]


def slot_bytes(r):
    """The 448 bytes this record says the FIFOs play next frame (A then B), or None."""
    if u32(r[O_PTR:O_PTR + 4]) != SND_BASE:
        return None
    c = r[O_SND + SI_COUNTER]
    period = r[O_SND + SI_PERIOD]
    if not (1 <= c <= period) or period * VB > LANE:
        return None
    dma1, dma2 = struct.unpack_from("<II", r, O_DMA)
    p = period - c
    lanes = r[O_SND + SI_LANES:O_SND + SI_LANES + 2 * LANE]
    a = lanes[p * VB:(p + 1) * VB] if dma1 & 0x80000000 else bytes(VB)
    b = lanes[LANE + p * VB:LANE + (p + 1) * VB] if dma2 & 0x80000000 else bytes(VB)
    return a + b


def stream_from_records(recs):
    """Entry v (VBlank v) comes from record v+1: list of 448-byte blocks."""
    out = []
    for r in recs[1:]:
        s = slot_bytes(r)
        out.append(s if s is not None else bytes(2 * VB))
    return out


MPLAY_TABLE = 0x08224404


def song_header(song, rom):
    """(header, player info address) of song N, from gSongTable and gMPlayTable in the ROM."""
    header, ms = struct.unpack_from("<IH", rom, SONG_TABLE - 0x08000000 + 8 * song)
    info = struct.unpack_from("<I", rom, MPLAY_TABLE - 0x08000000 + 12 * ms)[0]
    return header, info


def song_start(recs, header, info):
    """First record index where the song's player holds this song."""
    for i, r in enumerate(recs):
        if u32(snd(r, info, 4)) == header:
            return i
    return None


def cmd_p31(a):
    lr = open(a.prefix + ".lr16", "rb").read()
    f8 = open(a.prefix + ".fifo8", "rb").read()
    n = len(lr) // (4 * VB)
    print(f"lr16: {len(lr)} bytes = {len(lr) // 4} frames = {n} VBlanks x 224"
          f" (rest {len(lr) % (4 * VB)}); fifo8: {len(f8)} bytes = {len(f8) // (2 * VB)} VBlanks x 448")
    ok = len(lr) % (4 * VB) == 0 and len(f8) == n * 2 * VB
    if a.vblanks is not None:
        want = a.vblanks * VB
        print(f"expected {a.vblanks} VBlanks: {want} frames, {want * 4} bytes lr16, {a.vblanks * 448} bytes fifo8")
        ok = ok and len(lr) == want * 4 and len(f8) == a.vblanks * 448
    recs = records(a.prefix + ".snd") if a.snd else []
    bad = 0
    for v in range(len(recs) - 1):
        c = recs[v + 1][O_SND + SI_COUNTER]
        want = 7 - (v % 7)
        if c != want:
            if bad < 5:
                print(f"  counter after VBlank {v}: {c}, expected {want}")
            bad += 1
    if recs:
        si = recs[1][O_SND:O_SND + 0x1C]
        spv, freq, div = struct.unpack_from("<iii", si, 0x10)
        print(f"VBlank 0 constants: pcmSamplesPerVBlank {spv}, pcmDmaPeriod {si[0x0B]}, pcmFreq {freq},"
              f" divFreq {div}; TM0CNT_L image: see the PC I/O image")
        print(f"counter phase 7 - (v mod 7) for v = 0..{len(recs) - 2}: {len(recs) - 1 - bad} equal, {bad} differ")
        ok = ok and bad == 0 and (spv, si[0x0B], freq, div) == (224, 7, 13379, 627)
    print("P3.1", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def cmd_selfcheck(a):
    recs = records(a.prefix + ".snd")
    f8 = open(a.prefix + ".fifo8", "rb").read()
    blocks = [f8[i:i + 2 * VB] for i in range(0, len(f8), 2 * VB)]
    st = stream_from_records(recs)
    n = min(len(st), len(blocks))
    diff = [v for v in range(n) if st[v] != blocks[v]]
    print(f"PC .fifo8 against the slot rule on the PC records: {n} VBlanks, {len(diff)} differ"
          + (f" (first {diff[:5]})" if diff else ""))
    return 1 if diff else 0


def align(gba, pc, song, rom):
    h, info = song_header(song, rom)
    sg, sp = song_start(gba, h, info), song_start(pc, h, info)
    if sg is None or sp is None:
        sys.exit(f"song 0x{song:X} (header 0x{h:08X}) not found: GBA {sg}, PC {sp}")
    return h, sg, sp


def cmd_fifo(a):
    rom = open(a.rom, "rb").read()
    gba, pc = records(a.gba), records(a.pc)
    h, sg, sp = align(gba, pc, a.song, rom)
    gs = stream_from_records(gba)
    f8 = open(a.fifo8, "rb").read()
    ps = [f8[i:i + 2 * VB] for i in range(0, len(f8), 2 * VB)]
    # Record s holds the header for the first time; VBlank s-1's... stream entry
    # s-1 is what played in the frame after the handler that saw it first.
    g0, p0 = sg - 1, sp - 1
    n = min(len(gs) - g0, len(ps) - p0)
    if a.count:
        n = min(n, a.count)
    first = None
    ndiff = 0
    silent = 0
    ranges = []
    for k in range(n):
        if gs[g0 + k] != ps[p0 + k]:
            ndiff += 1
            if first is None:
                first = k
            if ranges and ranges[-1][1] == k - 1:
                ranges[-1][1] = k
            else:
                ranges.append([k, k])
        if not any(gs[g0 + k]):
            silent += 1
    if ranges:
        shown = ", ".join(f"{a}-{b}" if a != b else f"{a}" for a, b in ranges[:12])
        print(f"differing VBlanks (offsets from the start): {shown}" + (" ..." if len(ranges) > 12 else ""))
    print(f"song 0x{a.song:X} (header 0x{h:08X}) first seen in GBA record {sg} (gGameClock {hdr(gba[sg])[1]}),"
          f" PC record {sp} (gGameClock {hdr(pc[sp])[1]})")
    print(f"compared {n} VBlanks ({n * 448} FIFO bytes) from the song start; {silent} all-zero on the GBA side")
    lags_g, lags_p = lag_records(gba), lag_records(pc)
    print(f"lag VBlanks in the whole capture: GBA {len(lags_g)}, PC {len(lags_p)}")
    if first is None:
        print("P3.3 compare 1: EQUAL, 0 differing bytes")
        return 0
    g, p = gs[g0 + first], ps[p0 + first]
    nb = sum(1 for x, y in zip(g, p) if x != y)
    print(f"P3.3 compare 1: first difference {first} VBlanks after the start (GBA VBlank {g0 + first},"
          f" PC VBlank {p0 + first}): {nb} of 448 bytes; {ndiff} VBlanks differ in all;"
          f" equal run before it {first} VBlanks = {first * VB / RATE:.2f} s")
    # Allowed (audio-design 5.4): after a GBA lag VBlank the game's next sound
    # call lands one VBlank later on the GBA than on the PC, until a song start
    # re-aligns both; a song cut at a different point also leaves different
    # release tails. Report the nearest GBA lag VBlank before the difference.
    rec = g0 + first + 1
    before = [i for i in lags_g if i <= rec]
    if before:
        d = rec - before[-1]
        where = "after" if before[-1] > sg else "before"
        print(f"  nearest GBA lag VBlank: record {before[-1]}, {d} VBlanks before the difference"
              f" ({where} the song start); GBA lag VBlanks since the start: "
              f"{sum(1 for i in lags_g if sg <= i <= rec)}")
    else:
        print("  no GBA lag VBlank before the difference: UNEXPLAINED")
    return 1


# Readable bits of 0x04000060-0x0400009F (GBATEK "GBA Sound Channel 1-4" and
# "Sound Control Registers"); wave RAM (0x90-0x9F) is banked and left out.
READ_MASK16 = {
    0x60: 0x007F, 0x62: 0xFFC0, 0x64: 0x4000,
    0x68: 0xFFC0, 0x6C: 0x4000,
    0x70: 0x00E0, 0x72: 0xE000, 0x74: 0x4000,
    0x78: 0xFF00, 0x7C: 0x40FF,
    0x80: 0xFF77, 0x82: 0x770F, 0x84: 0x0080, 0x88: 0xC3FE,
}


def psg_image(r):
    out = {}
    for off, m in READ_MASK16.items():
        out[off] = struct.unpack_from("<H", r, O_IO + off - 0x60)[0] & m
    return out


def cmd_psg(a):
    rom = open(a.rom, "rb").read()
    gba, pc = records(a.gba), records(a.pc)
    h, sg, sp = align(gba, pc, a.song, rom)
    n = min(len(gba) - sg, len(pc) - sp)
    if a.count:
        n = min(n, a.count)
    first_cgb = first_io = None
    ncgb = nio = 0
    io_fields = {}
    cgb_fields = {}
    for k in range(n):
        rg, rp = gba[sg + k], pc[sp + k]
        cg, cp = snd(rg, CGB, 0x100), snd(rp, CGB, 0x100)
        if cg != cp:
            ncgb += 1
            if first_cgb is None:
                first_cgb = k
            for i in range(0x100):
                if cg[i] != cp[i]:
                    cgb_fields[i] = cgb_fields.get(i, 0) + 1
        ig, ip = psg_image(rg), psg_image(rp)
        if ig != ip:
            nio += 1
            if first_io is None:
                first_io = k
            for off in ig:
                if ig[off] != ip[off]:
                    io_fields[off] = io_fields.get(off, 0) + 1
    print(f"song 0x{a.song:X}: aligned at GBA record {sg}, PC record {sp}; {n} records compared")
    print(f"gCgbChans (0x03006510, 256 bytes): {n - ncgb} equal, {ncgb} differ"
          + (f", first at +{first_cgb}; bytes (chan*0x40+off: count) {dict(sorted(cgb_fields.items())[:12])}"
             if ncgb else ""))
    print(f"sound registers, readable bits (wave RAM left out): {n - nio} equal, {nio} differ"
          + (f", first at +{first_io}; registers {{off: count}} "
             f"{ {hex(0x04000000 + k): v for k, v in sorted(io_fields.items())} }" if nio else ""))
    if first_io is not None:
        k = first_io
        print(f"  at +{k}: GBA {[hex(x) for x in psg_image(gba[sg + k]).values()]}")
        print(f"          PC  {[hex(x) for x in psg_image(pc[sp + k]).values()]}")
    return 0 if ncgb == 0 and nio == 0 else 1


def cmd_wav(a):
    data = open(a.lr16, "rb").read()
    start = a.from_vblank * VB * 4
    end = len(data) if a.seconds is None else start + int(round(a.seconds * RATE)) * 4
    pcm = data[start:end]
    with wave.open(a.out, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm)
    print(f"{a.out}: {len(pcm) // 4} frames, {len(pcm) / 4 / RATE:.2f} s at {RATE} Hz, s16 stereo (VBlank"
          f" {a.from_vblank} on); samples copied unchanged")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("p31")
    p.add_argument("prefix")
    p.add_argument("--vblanks", type=int)
    p.add_argument("--snd", action="store_true", help="also check PREFIX.snd")
    p = sub.add_parser("selfcheck")
    p.add_argument("prefix")
    for name in ("fifo", "psg"):
        p = sub.add_parser(name)
        p.add_argument("gba")
        p.add_argument("pc")
        if name == "fifo":
            p.add_argument("fifo8")
        p.add_argument("--song", type=lambda s: int(s, 0), required=True)
        p.add_argument("--count", type=int)
        p.add_argument("--rom", default="baserom.gba")
    p = sub.add_parser("wav")
    p.add_argument("lr16")
    p.add_argument("out")
    p.add_argument("--from-vblank", type=int, default=0)
    p.add_argument("--seconds", type=float)
    a = ap.parse_args()
    return {"p31": cmd_p31, "selfcheck": cmd_selfcheck, "fifo": cmd_fifo, "psg": cmd_psg,
            "wav": cmd_wav}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
