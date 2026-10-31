#!/usr/bin/env python3
"""Gate 0, stage 6: compare a GBA trace and a PC trace tick by tick.

    python3 port/test/tools/gate0_compare.py --gba build/gate0/gba.jsonl \
        --pc build/gate0/pc.jsonl [--gba-raw F --pc-raw F] \
        [--gba-elf fireemblem8.elf --pc-elf build/pc-gate0/fe8pc] [--ticks 300] \
        [--masks port/test/oracle_limits.json | --mask-oracle-limits] \
        [--lag-replay build/gate0/pc_lagreplay.jsonl --lag-replay-raw F] \
        [--clock-replay build/gate0/pc_replay.jsonl --clock-replay-raw F] \
        [--affine-replay build/gate0/pc_affine.jsonl] [--json F] \
        [--pc-log build/gate0/pc.log] [--lag-replay-log F] [--affine-replay-log F]

Trace format: port/test/README.md, "Trace". Rules and masks: README.md,
"Rules and masks". Three passes over the same ticks:

  RAW     every field byte for byte, no rule. For reference only.
  RULES   the timing rules: ticks are aligned by OnMain count, gGameClock is
          compared as the logic clock (lag VBlanks taken out, see below), and
          gBmSt.prevVCount (write-only raster timing) is left out.
  RESULT  RULES plus the oracle-limit masks from --masks. A mask covers only
          its own bytes, and only on ticks where its condition holds; any other
          difference in the same field is still reported. A frame15 difference
          is masked as an oracle-limit frame (P2.2b, below) or labelled a
          raster-timing frame (P-17, below). This is the result.

Oracle-limit frame (README.md, "Oracle-limit frames"; decided 2026-10-19,
port-plan/DIVERGENCES.md section 5, P2.2b): the frame of tick t shows tick t-1's
shadow registers. A frame15 difference at tick t passes in RESULT only when
every difference at tick t-1 lies inside applied masks whose limit has
"frames": true (OL-2), all of it in bg2affin, and nothing else differs there;
BG2 is shown at tick t-1 (DISPCNT); no other field differs at tick t; and the
affine replay (--affine-replay, README.md "Affine replay": the PC run again
with that frame drawn with the GBA's bg2affin of tick t-1) draws exactly the
GBA's frame, so bg2affin is the only cause. Without --affine-replay no frame
passes this way. Such frames are counted and listed apart, like masked ticks.

Raster-timing frame (README.md, "Raster-timing frames"; port-plan DECISIONS.md
2026-10-20 afternoon, item 9; DIVERGENCES P-17): the GBA draws a frame while
its CPU runs on, so a line can show the main loop's memory from before a write
of the tick (or a line handler can read it), while the PC runs OnMain in zero
time and then draws (D-15). A frame15 difference at tick t that is not an
oracle-limit frame is labelled a raster-timing frame only when the PC trace
(--pc, or the lag replay on the lag check) redrew that frame with the GBA's
timing ("raster": "gba": its own frame step's lines again, the game's line
handlers live, every watched store of the GBA's frame made just before the line
event it came before on the GBA, from a machine whose watched memory equals the
GBA's at the frame's end; port/test/pc/gate0_hook.c), from the GBA trace's own
raster log block of tick t ("raster_rec" equals the GBA trace's "raster"), that
redraw is exactly the GBA's frame ("frame15_raster"), no other field differs at
tick t, and no field but frame15 differs at tick t-1. Such frames are counted
and listed apart, and a lag-only tick whose lag replay frame is one is counted
apart from the lag-only ticks (README.md, "Lag replay").

--mask-oracle-limits is the older spelling of --masks with the default file
(port/test/oracle_limits.json next to this script's directory).

Logic clock: gGameClock counts OnVBlank runs. A VBlank that comes while the CPU
is still busy (GBA lag; the PC has none) also runs OnVBlank. Per side,
lag(t) = onvblank_calls(t) - (vblank_waits(t) - 1): handler runs that no
completed VBlankIntrWait accounts for (the wait being entered at the sample has
not completed). The logic clock is gGameClock(t) - lag(t), compared exactly.

Lag check (README.md, "Lag replay"), part of the result when --lag-replay is
given: the lag replay trace is the PC run again with every GBA lag VBlank run
at the place it came on the GBA (GATE0_LAG_FROM: before main-loop event k of
its tick, k from the GBA trace's "lag_at"; README.md, "Lag position"; an early
flush VBlank, "early_flush", at the sample point with the flag set, README.md,
"Early flush VBlank"). A tick
t that differs in RESULT is lag-only only when the lag replay reproduces all
of it: the replay's RESULT pass (the same rules, except that the replay has
the GBA's lag, so its gGameClock and the gPlaySt clock copies must equal the
GBA's outright) has no difference at t, and on every tick up to t the
replay's event count is the GBA's and it ran that tick's lag VBlanks at the
GBA's positions, early flush included (strict prefix: after the replay's first sequence mismatch no
later tick is lag-only). Every field of such a tick is labelled lag-only;
every field of any other tick is not. A gGameClock or gPlaySt clock-copy rule
failure in the PC trace is never lag-only (the rule has already taken the lag
out), and a tick past the end of the replay trace is not labelled lag-only.
A replay-only tick (the replay's RESULT differs where the PC trace's does
not) fails the gate. Oracle-limit frames are masked in RESULT, so they never
reach the lag check; on the replay a frame is masked when the normal run's
frame at that tick was and the replay drew the same frame. "Lag-only" means
reproduced by the lag replay at the GBA's positions; on those ticks the PC's
own lag-free run is checked only through the replay.

A lag replay trace given as --pc (a diagnostic, and the route check of
port/check-all.mk, make -f port/test/Makefile route) is compared with the
same outright clock rule, and is DIFFERENT unless it also ran the GBA's lag
positions and main-loop events on every tick.

The clock replay (--clock-replay, the older --replay; README.md, "Clock
replay"; GATE0_CLOCK_FROM: the GBA's clock set before each OnVBlank) is a
diagnostic: the compare prints which RESULT differences it would match, field
by field, and excuses nothing with it.

Complete runs (integration review, 2026-10-20): every compared trace (the
GBA's, --pc, and the lag replay and affine replay when given) must have all
--ticks ticks, and the log of each PC run (--pc-log, --lag-replay-log,
--affine-replay-log: the run's stdout and stderr) must have no "[fault]" line
(port/hw/main_pc.c, OnFault). Otherwise the verdict is INCOMPLETE, whatever
the ticks that were compared show: a run that stopped early proves nothing
about the ticks it did not reach, and a replay that stopped early leaves the
plain run's later ticks unchecked. The compare still reports every tick that
all traces have.

Exit status: 0 if RESULT matches on every compared tick (and, with
--lag-replay, the lag replay has no replay-only tick); 3 if it does not but
every difference is lag-only and no tick is replay-only (only with
--lag-replay); 1 otherwise, INCOMPLETE included; 2 on bad input or a harness
check that fails (symbol addresses, trace format, GBA lag positions that do
not add up to the lag counters, a trace or log that is not there).
"""
import argparse
import json
import mmap
import os
import struct
import subprocess
import sys
import zlib

TRACE_FORMAT = "fe8-gate0-trace-4"
MASKS_FORMAT = "fe8-gate0-oracle-limits-1"

SCALARS = [("gGameClock", 4), ("gKeyStatusIgnoredSt", 2), ("gKeyComboResetEN", 1),
           ("gSoftwareResetFlag", 1), ("gMainCallback", 4), ("gLCGRNValue", 4)]
RAWS = ["gBmSt", "gPlaySt", "gRNSeeds", "sKeyStatusBuffer"]
HASHED = [("gLCDControlBuffer", 0x69), ("gPaletteBuffer", 0x400),
          ("gBG0TilemapBuffer", 0x800), ("gBG1TilemapBuffer", 0x800),
          ("gBG2TilemapBuffer", 0x800), ("gBG3TilemapBuffer", 0x800), ("gOam", 0x400)]
# FNV-1a 32 only, no raw dump (integration 11; README.md, "Trace"): the trap pool,
# bmtrap.c's static struct Trap sTrapPool[64] at 0x0203A614 (STATIC_ADDRS), which round
# 10's C10-10 (a) (RemoveTrap, D-72) changed with no traced field showing it. A
# difference names the field, not the byte. Every tick line has it, but a resumed
# trace's prologue line may come from a photo taken before it was traced
# (load_trace): that line is compared without it.
HASHED_ONLY = [("sTrapPool", 0x200)]
# The RAWS fields' sizes (port/test/oracle/gate0_capture.lua's table), for a
# mask on one of them (check "bitfield").
RAW_SIZES = {"gBmSt": 0x40, "gPlaySt": 0x4C, "gRNSeeds": 0x06, "sKeyStatusBuffer": 0x14}
COUNTERS = ["onmain_calls", "vblank_waits", "onvblank_calls", "events"]
RAW_TICK_SIZE = sum(size for _, size in HASHED)
FIELDS = ([name for name, _ in SCALARS] + RAWS + [name for name, _ in HASHED] + [name for name, _ in HASHED_ONLY]
          + ["frame15"]
          + COUNTERS)
# The test cheats (README.md, "Test cheats"; GATE0_CHEAT, test coverage only): a cheat-assisted
# trace's header has "cheat" (durability, funds, convoy or several joined with "+", in that order)
# and each tick line "cheat_items" (FNV-1a 32 of every unit's items and the convoy's after the tick
# boundary's cheats) and each cheat's own field: "cheat_refills" (durability: the item words it
# changed), "cheat_funds" (funds: 1 when it changed the gold), "cheat_convoy" (convoy: its cursor,
# the items placed so far). Every compared trace must name the same cheats (check_cheat); with them,
# their fields (cheat_fields) are compared on every tick like the others and join FIELDS (before
# frame15). Without one, nothing changes.
CHEAT_NAMES = ("durability", "funds", "convoy")
CHEAT_FIELDS = ["cheat_items", "cheat_refills"]


def cheat_fields(name, path):
    """The tick-line fields of a trace made with the test cheats NAME (its header's "cheat"). A
    harness check, exit 2, when NAME is not the cheats' names joined with "+" in their order."""
    parts = name.split("+")
    if (any(p not in CHEAT_NAMES for p in parts) or len(set(parts)) != len(parts)
            or parts != sorted(parts, key=CHEAT_NAMES.index)):
        print(f"gate0_compare: {path}: the header's \"cheat\" is {name!r}, not test cheats ("
              + ", ".join(CHEAT_NAMES) + ", joined with + in that order)")
        sys.exit(2)
    return ["cheat_items"] + [f for p, f in zip(CHEAT_NAMES, ("cheat_refills", "cheat_funds", "cheat_convoy"))
                              if p in parts]

# Addresses the harness relies on (fireemblem8.elf of this commit). The GBA
# capture sets breakpoints at these places (port/test/Makefile GBA_BP: function
# plus offset), all but the first three for the lag position (README.md, "Lag
# position"); both sides read the three file-static variables at these
# addresses (README.md, "Trace").
GBA_BREAKPOINTS = {"OnMain": ("OnMain", 0, 0x080152F4),
                   "VBlankIntrWait": ("VBlankIntrWait", 0, 0x080D16DC),
                   "OnVBlank": ("OnVBlank", 0, 0x080152A4),
                   "RunProcessRecursive": ("RunProcessRecursive", 0, 0x08002E28),
                   "RunProcessRecursive epilogue": ("RunProcessRecursive", 0x56, 0x08002E7E),
                   "RunProcessScript": ("RunProcessScript", 0, 0x08003394),
                   "RunProcessScript after a command": ("RunProcessScript", 0x3C, 0x080033D0),
                   "GetGameClock bx lr": ("GetGameClock", 4, 0x08000D2C),
                   "SetGameTime bx lr": ("SetGameTime", 4, 0x08000D38),
                   "CpuSet": ("CpuSet", 0, 0x080D1678),
                   "CpuFastSet": ("CpuFastSet", 0, 0x080D1674),
                   "LZ77UnCompWram": ("LZ77UnCompWram", 0, 0x080D1694),
                   "LZ77UnCompVram": ("LZ77UnCompVram", 0, 0x080D1690),
                   "FlipBgVerticalScroll bx lr": ("FlipBgVerticalScroll", 0x10, 0x08014EB8)}
STATIC_ADDRS = {"gRNSeeds": 0x03000000, "gLCGRNValue": 0x03000008, "sKeyStatusBuffer": 0x02024CC0,
                "sTrapPool": 0x0203A614}
# The raster log and the lag_split records (README.md, "Raster-timing frames"
# and "Lag position"): IrqMain's `bx r0` in ROM and in its IWRAM copy
# (port/test/Makefile GBA_RASTER_IRQ), the top of the IRQ stack the capture
# reads interrupted registers from, and the watched RAM ranges
# (GBA_RASTER_RAM), which the PC redraws at the same addresses (D1-a).
# label: (symbol, offset, address, also checked in the PC binary).
RASTER_ADDRS = {"IrqMain bx r0": ("IrqMain", 0xF8, 0x080001F4, False),
                "IrqMain bx r0 (IWRAM copy)": ("IntrMain_Buffer", 0xF8, 0x03004258, False),
                "__sp_irq": ("__sp_irq", 0, 0x03007FA0, False),
                "gMinimapWinBuf": ("gMinimapWinBuf", 0, 0x02000000, True),
                "gMinimapFrontWinBuf": ("gMinimapFrontWinBuf", 0, 0x02000500, True),
                "gMinimapBackWinBuf": ("gMinimapBackWinBuf", 0, 0x02000504, True),
                "gMinimapDisplayedWinBuf (after the mmwin range)": ("gMinimapDisplayedWinBuf", 0, 0x02000508, True),
                "gBmSt.altBlendACa": ("gBmSt", 0x38, 0x0202BCE8, True),
                "gManimScanlineBufs": ("gManimScanlineBufs", 0, 0x0203E754, True),
                "gManimActiveScanlineBuf (after the manim range)": ("gManimActiveScanlineBuf", 0, 0x0203E75C, True)}
# The watched regions a GBA trace's "raster_log" header must name (REG_IE,
# the BG scroll registers and REG_DISPSTAT, the last three, are hardware
# addresses: nothing to check in either binary; REG_DISPSTAT since round 10).
RASTER_LOG = ("pltt@0x05000000+0x400,vram@0x06000000+0x18000,oam@0x07000000+0x400,"
              "mmwin@0x02000000+0x508,banner@0x0202BCE8+0x4,manim@0x0203E754+0x8,ie@0x04000200+0x2,"
              "bgofs@0x04000010+0x10,"
              "dispstat@0x04000004+0x2")
PREVVCOUNT = (0x06, 0x08)  # gBmSt.prevVCount, u16

# Member names by byte offset (include/types.h struct BmSt, include/hardware.h
# struct LCDControlBuffer and struct KeyStatusBuffer), only to make reports readable.
BMST_MEMBERS = [(0x00, "main_loop_ended"), (0x01, "lock"), (0x02, "gameGfxSemaphore"),
                (0x03, "_unk04"), (0x04, "gameStateBits"), (0x05, "_unk05"),
                (0x06, "prevVCount"), (0x08, "_unk08"), (0x0C, "camera"),
                (0x10, "cameraPrevious"), (0x14, "playerCursor"), (0x18, "cursorPrevious"),
                (0x1C, "cursorTarget"), (0x20, "playerCursorDisplay"), (0x24, "mapRenderOrigin"),
                (0x28, "cameraMax"), (0x2C, "um_tmp_item"), (0x2E, "itemUnk2E"), (0x30, "unk30"),
                (0x32, "unk32"), (0x34, "unk34"), (0x36, "unk36"), (0x37, "unk37"),
                (0x38, "altBlendACa"), (0x39, "altBlendACb"), (0x3A, "altBlendBCa"),
                (0x3B, "altBlendBCb"), (0x3C, "just_resumed"), (0x3D, "taken_action"),
                (0x3E, "swapActionRangeCount"), (0x3F, "unk3F")]
PLAYST_MEMBERS = [(0x00, "time_saved"), (0x04, "time_chapter_started"), (0x08, "partyGoldAmount"),
                  (0x0C, "gameSaveSlot"), (0x0D, "chapterVisionRange"), (0x0E, "chapterIndex"),
                  (0x0F, "faction"), (0x10, "chapterTurnNumber"), (0x12, "xCursor"), (0x13, "yCursor"),
                  (0x14, "chapterStateBits"), (0x15, "chapterWeatherId"),
                  (0x16, "chapterTotalSupportGain"), (0x18, "playthroughIdentifier"), (0x19, "unk19"),
                  (0x1A, "lastUnitSortType"), (0x1B, "chapterModeIndex"), (0x1C, "unk1C"),
                  (0x20, "playerName"), (0x2B, "unk_2B"), (0x2C, "(bitfields)")]
# u32 copies of gGameClock in gPlaySt (bmsave.c:176 and 499; bmio.c:1005 and
# sio_battlemap.c:379). They carry the clock offset of the tick they were taken.
CLOCK_COPIES = [(0x00, "time_saved"), (0x04, "time_chapter_started")]
LCD_MEMBERS = [(0x00, "dispcnt"), (0x04, "dispstat"), (0x08, "(pad)"), (0x0C, "bg0cnt"),
               (0x10, "bg1cnt"), (0x14, "bg2cnt"), (0x18, "bg3cnt"), (0x1C, "bgoffset[0]"),
               (0x20, "bgoffset[1]"), (0x24, "bgoffset[2]"), (0x28, "bgoffset[3]"),
               (0x2C, "win0/win1 left,right"), (0x30, "win0/win1 top,bottom"), (0x34, "wincnt"),
               (0x38, "mosaic"), (0x3A, "(pad)"), (0x3C, "bldcnt"), (0x44, "blendCoeffA"),
               (0x45, "blendCoeffB"), (0x46, "blendY"), (0x47, "(pad)"), (0x48, "bg2affin"),
               (0x58, "bg3affin"), (0x68, "colorAddition")]
KEYSTATUS_MEMBERS = [(0x00, "repeatDelay"), (0x01, "repeatInterval"), (0x02, "repeatTimer"),
                     (0x03, "(pad)"), (0x04, "heldKeys"), (0x06, "repeatedKeys"), (0x08, "newKeys"),
                     (0x0A, "prevKeys"), (0x0C, "LastPressState"), (0x0E, "ABLRPressed"),
                     (0x10, "newKeys2"), (0x12, "TimeSinceStartSelect")]
RNSEEDS_MEMBERS = [(0x00, "gRNSeeds[0]"), (0x02, "gRNSeeds[1]"), (0x04, "gRNSeeds[2]")]
MEMBER_TABLES = {"gBmSt": BMST_MEMBERS, "gPlaySt": PLAYST_MEMBERS, "gLCDControlBuffer": LCD_MEMBERS,
                 "sKeyStatusBuffer": KEYSTATUS_MEMBERS, "gRNSeeds": RNSEEDS_MEMBERS}


def member(table, off):
    name = table[0][1]
    for start, n in table:
        if start <= off:
            name = n
    return name


def describe_offset(field, off):
    if field in MEMBER_TABLES:
        return f"+0x{off:02X} ({member(MEMBER_TABLES[field], off)})"
    if field == "gPaletteBuffer":
        return f"+0x{off:03X} (palette {off // 32}, colour {(off % 32) // 2})"
    if field.endswith("TilemapBuffer"):
        cell = off // 2
        return f"+0x{off:03X} (cell x={cell % 32}, y={cell // 32})"
    if field == "gOam":
        return f"+0x{off:03X} (OAM entry {off // 8}, attr{(off % 8) // 2})"
    return f"+0x{off:02X}"


# Save states (README.md, "Save states"): a resumed trace's header has a
# "resume" block, and its first line is tick T-1 (the prologue, from the
# snapshot's run). FIRST is that tick for every compared trace (0 for runs
# from power-on); the tick lists are indexed by tick, with None before FIRST.
FIRST = 0


def load_trace(path):
    try:
        with open(path) as fh:
            lines = [json.loads(line) for line in fh if line.strip()]
    except OSError as e:
        print(f"gate0_compare: {path}: {e.strerror} (did that run start?)")
        sys.exit(2)
    if not lines or lines[0].get("format") != TRACE_FORMAT:
        print(f"gate0_compare: {path}: not a {TRACE_FORMAT} file (rerun the capture)")
        sys.exit(2)
    head, ticks = lines[0], lines[1:]
    res = head.get("resume")
    if res is not None:
        first = res.get("tick", 0) - 1
        if first < 0 or not ticks or ticks[0].get("tick") != first:
            print(f"gate0_compare: {path}: a resumed trace (tick {res.get('tick')}) must start with tick {first} "
                  "(the prologue)")
            sys.exit(2)
        ticks = [None] * first + ticks
    # HASHED_ONLY: on every tick line, but a resumed trace's prologue (an older photo's own line).
    prologue = first if res is not None else -1
    for f, _ in HASHED_ONLY:
        lacking = [i for i, t in enumerate(ticks) if t is not None and f not in t and i != prologue]
        if lacking:
            print(f"gate0_compare: {path}: {len(lacking)} tick lines have no {f}, first tick {lacking[0]} (a capture "
                  "or PC binary from before integration 11: rerun it)")
            sys.exit(2)
    if head.get("cheat"):  # the test cheats' fields: on every tick line of a cheat-assisted trace
        for f in cheat_fields(head["cheat"], path):
            lacking = [i for i, t in enumerate(ticks) if t is not None and f not in t and i != prologue]
            if lacking:
                print(f"gate0_compare: {path}: a cheat-assisted trace (GATE0_CHEAT={head['cheat']}), but {len(lacking)} "
                      f"tick lines have no {f}, first tick {lacking[0]}")
                sys.exit(2)
    return head, ticks


def check_resume(heads):
    """Save states: the traces compared are all resumed from one snapshot, at
    the same tick, from the same set, ROM and key file, each from its own part
    (or none is resumed). Sets FIRST. A harness check, exit 2."""
    global FIRST
    res = {name: h.get("resume") for name, (h, _) in heads.items()}
    if not any(res.values()):
        return None
    if not all(res.values()):
        print("gate0_compare: some traces are resumed from a save state and some are not: "
              + ", ".join(f"{n} {'resumed' if r else 'from power-on'}" for n, r in res.items()))
        sys.exit(2)
    ref_name = "GBA"
    ref = res[ref_name]
    problems = []
    for name, r in res.items():
        for k in ("tick", "set", "rom_crc32", "keys_crc32"):
            if r.get(k) != ref.get(k):
                problems.append(f"{name}: {k} {r.get(k)!r}, GBA {ref.get(k)!r}")
        want = heads[name][1]
        if r.get("part") not in want:
            problems.append(f"{name}: resumed from the snapshot part {r.get('part')!r}, expected {' or '.join(want)}")
    if problems:
        print("gate0_compare: the traces are not resumed from the same snapshot:\n  " + "\n  ".join(problems))
        sys.exit(2)
    FIRST = ref["tick"] - 1
    return ref


def check_cheat(heads):
    """The test cheat (README.md, "Test cheats"): every compared trace was made with
    the same GATE0_CHEAT (its header's "cheat"; none when it has none). A harness
    check, exit 2. Returns the cheat, or None."""
    cheats = {name: h.get("cheat") for name, (h, _) in heads.items()}
    ref = cheats["GBA"]
    if any(c != ref for c in cheats.values()):
        print("gate0_compare: the traces were not made with the same test cheat (GATE0_CHEAT; README.md, \"Test "
              "cheats\"): " + ", ".join(f"{n} {c or 'none'}" for n, c in cheats.items()))
        sys.exit(2)
    return ref


def first_fault(path):
    """The first "[fault]" line of a PC run's log (port/hw/main_pc.c OnFault: a
    fatal signal), or None. A log given but not there is a harness error."""
    try:
        with open(path, errors="replace") as fh:
            for line in fh:
                if line.startswith("[fault]"):
                    return line.strip()
    except OSError as e:
        print(f"gate0_compare: {path}: {e.strerror}")
        sys.exit(2)
    return None


def file_crc32(path):
    crc = 0
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            crc = zlib.crc32(chunk, crc)
    return f"{crc:08x}"


def check_same_binary(what, head, phead):
    """Every PC-side trace names the binary that made it ("exe_crc32", the
    hook's CRC-32 of its own file). A replay that is not the PC trace's binary
    proves nothing about it (a harness check, exit 2). It must also have
    booted from the same SRAM as the PC trace (check_sram_in)."""
    if head.get("exe_crc32") != phead.get("exe_crc32"):
        print(f"gate0_compare: {what} was made by another PC binary than the PC trace "
              f"(exe_crc32 {head.get('exe_crc32')}, PC trace {phead.get('exe_crc32')})")
        sys.exit(2)
    check_sram_in(phead, head, what)


def check_sram_in(head0, head, name):
    """README.md, "Chapter saves": the runs compared must boot from the same
    SRAM (GATE0_SRAM_IN; no "sram_fnv" in a header is a blank SRAM). A harness
    check, exit 2."""
    g, o = head0.get("sram_fnv"), head.get("sram_fnv")
    if g != o:
        print(f"gate0_compare: {name} booted from other SRAM than the {head0.get('source', '?')} trace: "
              f"{head0.get('sram_in', 'blank')} (fnv1a {g or 'none'}), "
              f"{name} {head.get('sram_in', 'blank')} (fnv1a {o or 'none'})")
        sys.exit(2)
    return g


def load_raw(path, ticks):
    """Raw records by tick (a resumed run's dump starts at FIRST), as views of
    the file mapped read-only: a route ch5 dump is 1.4 GB, and only the ticks
    with a hashed-field difference are ever read (the compare's peak memory,
    port-plan DECISIONS test throughput: under about 5 GB)."""
    if not path or not os.path.exists(path):
        return None
    with open(path, "rb") as fh:
        size = os.fstat(fh.fileno()).st_size
        data = memoryview(mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)) if size else memoryview(b"")
    return [None] * FIRST + [data[i * RAW_TICK_SIZE:(i + 1) * RAW_TICK_SIZE]
                             for i in range(min(ticks - FIRST, len(data) // RAW_TICK_SIZE))]


def raw_region(raw_ticks, tick, field):
    if raw_ticks is None or tick >= len(raw_ticks) or raw_ticks[tick] is None:
        return None
    off = 0
    for name, size in HASHED:
        if name == field:
            return raw_ticks[tick][off:off + size]
        off += size
    return None


def nm_table(nm, path):
    """name -> (address, type letter) for every symbol, or None if nm fails."""
    if not path or not os.path.exists(path):
        return None
    try:
        out = subprocess.run([nm, path], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    table = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            table.setdefault(parts[2], (int(parts[0], 16), parts[1]))
    return table


def function_names(table, thumb):
    """address -> function name, with __wrap_/__real_ taken off."""
    names = {}
    for name, (addr, kind) in (table or {}).items():
        if kind in "tTwW":
            if thumb:
                addr &= ~1
            for prefix in ("__wrap_", "__real_"):
                if name.startswith(prefix):
                    name = name[len(prefix):]
            names.setdefault(addr, name)
    return names


def check_addresses(gba_table, pc_table):
    """The harness's hard-coded addresses against both symbol tables."""
    problems, notes = [], []
    if gba_table is None:
        notes.append("GBA ELF symbols not read: breakpoint and static addresses not checked")
    else:
        places = [(label, sym, off, addr) for label, (sym, off, addr) in GBA_BREAKPOINTS.items()]
        places += [(name, name, 0, addr) for name, addr in STATIC_ADDRS.items()]
        places += [(label, sym, off, addr) for label, (sym, off, addr, _) in RASTER_ADDRS.items()]
        for label, sym, off, addr in places:
            got = gba_table.get(sym)
            if got is None or (got[0] & ~1) + off != addr:
                problems.append(f"GBA ELF: {label} is at "
                                f"{'missing' if got is None else hex((got[0] & ~1) + off)}, "
                                f"harness uses 0x{addr:08X}")
    if pc_table is None:
        notes.append("PC binary symbols not read: static addresses not checked")
    else:
        for name, addr in STATIC_ADDRS.items():
            got = pc_table.get(name)
            if got is None or got[0] != addr:
                problems.append(f"PC binary: {name} is at {'missing' if got is None else hex(got[0])}, "
                                f"the hook reads 0x{addr:08X}")
        for label, (sym, off, addr, in_pc) in RASTER_ADDRS.items():
            got = pc_table.get(sym)
            if in_pc and (got is None or got[0] + off != addr):
                problems.append(f"PC binary: {label} is at {'missing' if got is None else hex(got[0] + off)}, "
                                f"the raster-timing redraw writes the GBA's 0x{addr:08X}")
    return problems, notes


def png_15bit_hash(path):
    """FNV-1a 32 of a 240x160 PNG as little-endian r5|g5<<5|b5<<10 values."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, width, height, ctype = 8, b"", 0, 0, 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError(f"{path}: unsupported PNG (depth {depth}, type {ctype})")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = width * bpp
    prev = bytearray(stride)
    h = 0x811C9DC5
    p = 0
    for _ in range(height):
        ftype = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ftype == 1:
                line[i] = (line[i] + a) & 0xFF
            elif ftype == 2:
                line[i] = (line[i] + b) & 0xFF
            elif ftype == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif ftype == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        for x in range(width):
            r, g, bl = line[x * bpp], line[x * bpp + 1], line[x * bpp + 2]
            v = (r >> 3) | ((g >> 3) << 5) | ((bl >> 3) << 10)
            for byte in (v & 0xFF, v >> 8):
                h = ((h ^ byte) * 0x01000193) & 0xFFFFFFFF
        prev = line
    return f"{h:08x}"


_PNG_HASHES = {}


def load_png_hashes(path, spot_checks=3):
    """--png-hashes: fill the PNG hash cache from tools/png_hashes.py's file, after hashing
    `spot_checks` of its frames (first, middle, last) again with png_15bit_hash."""
    try:
        with open(path) as fh:
            cache = json.load(fh)
    except (OSError, ValueError) as e:
        print(f"--png-hashes {path}: cannot read it ({e})", file=sys.stderr)
        return False
    keys = sorted(cache)
    picks = sorted({keys[0], keys[len(keys) // 2], keys[-1]}) if keys else []
    for k in picks[:spot_checks]:
        got = png_15bit_hash(k[4:])
        if got != cache[k]:
            print(f"--png-hashes {path}: {k} is {got}, the file says {cache[k]}: not used", file=sys.stderr)
            return False
    _PNG_HASHES.update(cache)
    return True


def frame15_value(value):
    if value is None:
        return None
    if isinstance(value, str) and value.startswith("png:"):
        if value not in _PNG_HASHES:  # every pass compares the same frames
            _PNG_HASHES[value] = png_15bit_hash(value[4:])
        return _PNG_HASHES[value]
    return value


# --- Oracle-limit masks (port/test/oracle_limits.json) -----------------------

def s16(b, off):
    return struct.unpack_from("<h", b, off)[0]


def s32(b, off):
    return struct.unpack_from("<i", b, off)[0]


def check_affine_bg(m, g, p, offs):
    """BgAffineDstData groups (s16 pa, pb, pc, pd; s32 x, y) at m['groups']."""
    covered, worst = set(), {}
    for base in m["groups"]:
        group = [o for o in offs if base <= o < base + 16]
        if not group:
            continue
        dm = max(abs(s16(g, base + k) - s16(p, base + k)) for k in (0, 2, 4, 6))
        do = max(abs(s32(g, base + k) - s32(p, base + k)) for k in (8, 12))
        if dm <= m["params"]["max_matrix_delta"] and do <= m["params"]["max_offset_delta"]:
            covered.update(group)
            worst[f"0x{base:02X}"] = (dm, do)
    return covered, worst


def check_affine_oam(m, g, p, offs):
    """OAM attr3 halfwords (affine parameters): bytes 6-7 of each 8-byte entry."""
    covered, worst = set(), {}
    for o in offs:
        if o % 8 not in (6, 7):
            continue
        base = o - o % 8 + 6
        d = abs(s16(g, base) - s16(p, base))
        if d <= m["params"]["max_matrix_delta"]:
            covered.add(o)
            worst["attr3"] = max(worst.get("attr3", 0), d)
    return covered, worst


def check_cells(m, g, p, offs):
    """Listed u16 cells (params "cells": offset -> [[GBA value, PC value], ...]):
    a cell is covered only when the two sides hold one of its listed pairs
    exactly, so nothing but the known values passes."""
    covered, worst, diff = set(), {}, set(offs)
    for off, pairs in m["cells"].items():
        cell = {off, off + 1} & diff
        if not cell:
            continue
        pair = (struct.unpack_from("<H", g, off)[0], struct.unpack_from("<H", p, off)[0])
        if pair in pairs:
            covered |= cell
            worst["cells"] = worst.get("cells", 0) + 1
    return covered, worst


def check_bitfield(m, g, p, offs):
    """One bit field of one byte (params "offset", "shift", "bits", "pairs":
    [[GBA value, PC value], ...]; the field is bits shift .. shift + bits - 1
    of the byte at offset): the byte is covered only when the two sides differ
    inside that bit field alone and the field holds one of the listed pairs
    exactly, so a difference in any other bit of the byte, or any other pair
    of values, is still reported. Works on a RAWS field (gPlaySt) as on a
    HASHED one."""
    b = m["bitfield"]
    off = b["offset"]
    if off not in offs:
        return set(), {}
    field = ((1 << b["bits"]) - 1) << b["shift"]
    if (g[off] ^ p[off]) & ~field & 0xFF:
        return set(), {}
    pair = ((g[off] & field) >> b["shift"], (p[off] & field) >> b["shift"])
    if pair not in b["pairs"]:
        return set(), {}
    return {off}, {}


CHECKS = {"affine_bg": check_affine_bg, "affine_oam": check_affine_oam, "cells": check_cells,
          "bitfield": check_bitfield}

# gPlaySt.chapterIndex (include/types.h, struct PlaySt +0x0E), for a limit's
# "chapter" condition.
PLAYST_CHAPTER = 0x0E


def mask_condition(m, g, p):
    """A limit's "chapter" condition: both sides have that chapter loaded."""
    if m["chapter"] is None:
        return True
    return all(bytes.fromhex(rec["gPlaySt"])[PLAYST_CHAPTER] == m["chapter"] for rec in (g, p))

# The BG that each BgAffineDstData of gLCDControlBuffer drives (FlushLCDControl,
# hardware.c:91-103: +0x48 to BG2PA..BG2Y, +0x58 to BG3PA..BG3Y), the DISPCNT
# enable bit of that BG (include/hardware.h struct DispCnt) and the BG modes in
# which the hardware uses that matrix (GBATEK, "LCD I/O BG Rotation/Scaling":
# BG2 in modes 1-5, BG3 in mode 2).
AFFINE_BGS = {0x48: ("BG2", 10, (1, 2, 3, 4, 5)), 0x58: ("BG3", 11, (2,))}
# The frame rule (P2.2b) covers bg2affin only: the decision names bg2affin, and
# the affine replay (GATE0_AFFINE_FROM) sets BG2's registers only. A tick t-1
# with a bg3affin difference (masked or not) leaves frame t reported.
FRAME_RULE_GROUPS = (0x48,)
# Keys that only the frame step writes, so the affine replay may differ in them.
AFFINE_FRAME_KEYS = ("frame15", "affine", "frame15_pc")
# Keys the raster-timing redraw adds to a PC tick line (GATE0_RASTER_FROM; the
# affine replay runs without it). They change nothing else in the run.
RASTER_KEYS = ("raster", "raster_rec", "raster_note", "frame15_raster")
RASTER_FRAME = "raster-timing frame"
# Why a PC run's redraw mark is not "gba" (port/test/pc/gate0_hook.c, OpenGbaRaster).
RASTER_MARKS = {
    "nostores": "the GBA stored nothing watched while it drew this frame (its raster log), so the GBA's timing "
                "cannot explain the difference",
    "differs": "a watched region the GBA stored to in this frame is not the GBA's at the frame's end",
    "handler": "the PC's frame step changed a watched region the GBA stored to",
    "incomplete": "the GBA's raster log does not account for every change of a watched region in this frame",
    "control": "the redraw without the GBA's timing is not the PC's frame",
    "rollback": "the GBA's stores do not fit the PC's memory",
    "slots": "the PC's line events (HBlank and VCount handlers, lines drawn) are not the GBA's",
    "none": "the GBA's raster log has no block for this tick",
}


def affine_bgs_shown(lcd):
    """Group bases (0x48, 0x58) whose matrix a frame drawn with this
    gLCDControlBuffer uses: the BG is on, the mode uses its matrix and the
    screen is not force-blanked (DISPCNT bit 7)."""
    dispcnt = lcd[0] | lcd[1] << 8
    if dispcnt & 0x80:
        return set()
    return {base for base, (_, bit, modes) in AFFINE_BGS.items()
            if dispcnt >> bit & 1 and dispcnt & 7 in modes}


def load_masks(path):
    if not path:
        return []
    with open(path) as fh:
        data = json.load(fh)
    if data.get("format") != MASKS_FORMAT:
        print(f"gate0_compare: {path}: not a {MASKS_FORMAT} file")
        sys.exit(2)
    masks = []
    for lim in data["limits"]:
        for key in ("id", "title", "field", "condition", "reason", "ref"):
            if key not in lim:
                print(f"gate0_compare: {path}: limit {lim.get('id', '?')} has no '{key}'")
                sys.exit(2)
        check = lim.get("check")
        frames = lim.get("frames", False)
        if not isinstance(frames, bool):  # 1 and 0 are not booleans here
            print(f"gate0_compare: {path}: limit {lim['id']}: 'frames' must be true or false")
            sys.exit(2)
        if check is None:
            if frames:
                print(f"gate0_compare: {path}: limit {lim['id']}: 'frames' needs a check")
                sys.exit(2)
            masks.append(dict(lim, apply=False, frames=False))
            continue
        # A bit field may be in a RAWS field too (gPlaySt); the other checks
        # work on the HASHED fields' raw dumps only.
        sizes = dict(HASHED, **RAW_SIZES) if check == "bitfield" else dict(HASHED)
        if check not in CHECKS or lim["field"] not in sizes:
            print(f"gate0_compare: {path}: limit {lim['id']}: unknown check '{check}' or field '{lim['field']}'")
            sys.exit(2)
        m = dict(lim, apply=True, frames=frames)
        m["groups"] = [int(x, 0) for x in lim.get("groups", [])]
        m["chapter"] = int(lim["chapter"], 0) if "chapter" in lim else None
        if check == "bitfield":
            try:
                par = lim["params"]
                b = {"offset": int(par["offset"], 0), "shift": int(par["shift"]), "bits": int(par["bits"]),
                     "pairs": {(int(gv), int(pv)) for gv, pv in par["pairs"]}}
            except (KeyError, TypeError, ValueError, AttributeError):
                b = None
            if (b is None or not 0 <= b["offset"] < sizes[lim["field"]] or not 1 <= b["bits"] <= 8
                    or not 0 <= b["shift"] <= 8 - b["bits"] or not b["pairs"]
                    or any(gv == pv or not 0 <= gv < 1 << b["bits"] or not 0 <= pv < 1 << b["bits"]
                           for gv, pv in b["pairs"])):
                print(f"gate0_compare: {path}: limit {lim['id']}: check 'bitfield' needs params offset (a string, "
                      "inside the field), shift and bits (a bit field inside that byte) and pairs: [GBA, PC] "
                      "values of the bit field that differ")
                sys.exit(2)
            m["bitfield"] = b
        if check == "cells":
            try:
                cells = lim["params"]["cells"]
                m["cells"] = {int(off, 0): {(int(gv, 0), int(pv, 0)) for gv, pv in pairs}
                              for off, pairs in cells.items()}
            except (KeyError, TypeError, ValueError, AttributeError):
                m["cells"] = None
            size = dict(HASHED)[lim["field"]]
            if (not m["cells"] or any(off % 2 or off + 2 > size for off in m["cells"])
                    or any(not pairs or any(gv == pv or not 0 <= gv <= 0xFFFF or not 0 <= pv <= 0xFFFF
                                            for gv, pv in pairs) for pairs in m["cells"].values())):
                print(f"gate0_compare: {path}: limit {lim['id']}: check 'cells' needs params.cells: "
                      "even offsets inside the field, each with [GBA, PC] u16 pairs that differ")
                sys.exit(2)
        # The frame rule knows only the BG affine matrices of gLCDControlBuffer
        # (AFFINE_BGS); it needs the rule in words too.
        if frames and (check != "affine_bg" or lim["field"] != "gLCDControlBuffer"
                       or not m["groups"] or any(b not in AFFINE_BGS for b in m["groups"])
                       or "frames_condition" not in lim):
            print(f"gate0_compare: {path}: limit {lim['id']}: 'frames' needs check 'affine_bg' on "
                  "gLCDControlBuffer, groups among " + ", ".join(f"0x{b:02X}" for b in AFFINE_BGS)
                  + ", and a 'frames_condition'")
            sys.exit(2)
        masks.append(m)
    return masks


# --- Clock rule (README.md, "Rules and masks") --------------------------------

def lag(rec):
    """OnVBlank runs that no completed VBlankIntrWait accounts for."""
    return rec["onvblank_calls"] - (rec["vblank_waits"] - 1)


def clock(rec):
    return int(rec["gGameClock"], 16)


def u32_at(rec, field, off):
    return struct.unpack_from("<I", bytes.fromhex(rec[field]), off)[0]


def clock_rule(gticks, pticks, n):
    """Per tick: {'ok', 'detail', 'E', 'dlag'}.

    E is the excused clock offset (GBA clock minus PC clock) that the lag
    VBlanks explain. Without a clock write, E grows by each side's new lag
    VBlanks, and the clocks must differ by exactly E. A clock write
    (SetGameTime, the cap in IncrementGameClock) shows as a tick where the clock
    moved by something other than the OnVBlank runs. Both sides must write on
    the same tick, and the new offset must be a known base (0: the same value
    written; the previous E; a clock copy's offset, for SetGameTime(time_saved))
    plus at most the lag VBlanks that came after the write in that tick.

    Resumed traces (FIRST > 0): the prologue tick FIRST seeds the offset E and
    the clock copies' offsets with what the two sides show there."""
    out, E = [None] * FIRST, 0
    copy_offsets = {}
    for t in range(FIRST, n):
        g, p = gticks[t], pticks[t]
        if t == FIRST and FIRST:
            E = clock(g) - clock(p)
            for off, name in CLOCK_COPIES:
                copy_offsets[name] = u32_at(g, "gPlaySt", off) - u32_at(p, "gPlaySt", off)
            out.append({"ok": True, "detail": "", "dlag": (0, 0), "write": False, "E": E, "seed": True})
            continue
        dlag_g = lag(g) - (lag(gticks[t - 1]) if t else 0)
        dlag_p = lag(p) - (lag(pticks[t - 1]) if t else 0)
        if t:
            wg = clock(g) - clock(gticks[t - 1]) != g["onvblank_calls"] - gticks[t - 1]["onvblank_calls"]
            wp = clock(p) - clock(pticks[t - 1]) != p["onvblank_calls"] - pticks[t - 1]["onvblank_calls"]
        else:
            wg, wp = clock(g) != g["onvblank_calls"], clock(p) != p["onvblank_calls"]
        d = clock(g) - clock(p)
        rec = {"ok": True, "detail": "", "dlag": (dlag_g, dlag_p), "write": wg or wp}
        if wg != wp:
            rec["ok"] = False
            rec["detail"] = (f"clock written on the {'GBA' if wg else 'PC'} side only "
                             f"(GBA {clock(g)}, PC {clock(p)})")
        elif wg:
            now = {u32_at(g, "gPlaySt", off) - u32_at(p, "gPlaySt", off) for off, _ in CLOCK_COPIES}
            bases = sorted({0, E} | set(copy_offsets.values()) | now)
            fit = [b for b in bases if -dlag_p <= d - b <= dlag_g]
            if fit:
                E = d
            else:
                rec["ok"] = False
                rec["detail"] = (f"clock written on both sides; offset GBA-PC {d} is not a known base "
                                 f"{bases} plus the tick's lag after the write (GBA +{dlag_g}, PC +{dlag_p}); "
                                 f"GBA {clock(g)}, PC {clock(p)}")
        else:
            E += dlag_g - dlag_p
            if d != E:
                rec["ok"] = False
                rec["detail"] = (f"GBA {clock(g)}, PC {clock(p)}: offset {d}, lag explains {E} "
                                 f"(GBA lag VBlanks {lag(g)}, PC {lag(p)})")
        if not rec["ok"]:
            E = d  # report once, then follow the traces
        rec["E"] = E
        for off, name in CLOCK_COPIES:
            copy_offsets[name] = u32_at(g, "gPlaySt", off) - u32_at(p, "gPlaySt", off)
        out.append(rec)
    return out


def copy_rule(gticks, pticks, n, clk):
    """Per tick: {offset: (ok, detail)} for the gPlaySt clock copies that differ.

    A copy is taken on the tick its value changes (both sides on the same tick).
    Its GBA-PC offset must then lie between the clock offsets at the start and
    the end of that tick (0 counts as a start if the clock was written that
    tick), and stays the same until the next copy."""
    out = [dict() for _ in range(n)]
    for off, name in CLOCK_COPIES:
        held = 0
        for t in range(FIRST, n):
            g, p = gticks[t], pticks[t]
            vg, vp = u32_at(g, "gPlaySt", off), u32_at(p, "gPlaySt", off)
            if t == FIRST and FIRST:
                held = vg - vp  # resumed traces: the prologue seeds the copy's offset
                if held:
                    out[t][off] = (True, f"{name} offset {held} (seeded at the prologue)")
                continue
            cg = t and vg != u32_at(gticks[t - 1], "gPlaySt", off)
            cp = t and vp != u32_at(pticks[t - 1], "gPlaySt", off)
            d = vg - vp
            if cg != cp:
                if d != held:
                    out[t][off] = (False, f"{name} changed on the {'GBA' if cg else 'PC'} side only")
                continue
            if cg:
                lo = clk[t - 1]["E"] if t else 0
                hi = clk[t]["E"]
                cands = {lo, hi} | ({0} if clk[t]["write"] else set())
                if min(cands) <= d <= max(cands):
                    held = d
                else:
                    out[t][off] = (False, f"{name} copied with offset {d}, clock offset in that tick "
                                          f"{min(cands)}..{max(cands)}")
                    held = d
                    continue
            if d != 0:
                out[t][off] = (d == held, f"{name} offset {d}" + ("" if d == held else f", copy offset {held}"))
    return out


# --- Per-tick comparison ------------------------------------------------------


def compare_tick(g, p, t, ctx, level):
    """(diffs, mask_hits, frame_hit): diffs is a list of (field, detail); level is
    raw, rules or result. frame_hit is set (frame_rule) when a frame15
    difference was masked as an oracle-limit frame, else None."""
    rules = level in ("rules", "result")
    diffs, mask_hits, frame_hit = [], [], None

    # Counters: ticks aligned by OnMain count, and the same number of VBlank waits,
    # on both sides. A tick is one main-loop iteration; OnMain is not the only
    # main callback (battle animations run MainUpdate_8055C68,
    # banim-ekrbattlestarting.c:72, which also sets main_loop_ended), so
    # onmain_calls is tick + 1 only while OnMain runs. gMainCallback is compared
    # below.
    if g["onmain_calls"] != p["onmain_calls"]:
        diffs.append(("onmain_calls", f"GBA {g['onmain_calls']}, PC {p['onmain_calls']} OnMain calls "
                                      f"at tick {t}: ticks not aligned"))
    if g["vblank_waits"] != p["vblank_waits"]:
        diffs.append(("vblank_waits", f"GBA {g['vblank_waits']}, PC {p['vblank_waits']}"))
    if not rules and g["onvblank_calls"] != p["onvblank_calls"]:
        diffs.append(("onvblank_calls", f"GBA {g['onvblank_calls']}, PC {p['onvblank_calls']}"))
    # Main-loop events (README.md, "Lag position"): RAW only, like
    # onvblank_calls; the lag check uses them to place the GBA's lag VBlanks.
    if not rules and g["events"] != p["events"]:
        diffs.append(("events", f"GBA {g['events']}, PC {p['events']}"))

    for f, _ in SCALARS:
        gv, pv = g.get(f), p.get(f)
        if f == "gMainCallback":
            ga, pa = int(gv, 16), int(pv, 16)
            gn = ctx["gba_syms"].get(ga & ~1, f"0x{ga:08X}")
            pn = ctx["pc_syms"].get(pa, f"0x{pa:08X}")
            if not ctx["gba_syms"] or not ctx["pc_syms"]:
                diffs.append((f, f"not comparable without both symbol tables (GBA 0x{ga:08X}, PC 0x{pa:08X})"))
            elif gn != pn:
                diffs.append((f, f"GBA {gn} (0x{ga:08X}), PC {pn} (0x{pa:08X})"))
        elif f == "gGameClock" and rules and ctx.get("clock_outright"):
            # A lag replay ran the GBA's lag VBlanks, so nothing is excused:
            # its clock must be the GBA's (README.md, "Lag replay").
            if gv != pv:
                diffs.append((f, f"GBA {int(gv, 16)}, lag replay {int(pv, 16)}: the lag replay ran the GBA's lag "
                                 "VBlanks, so its clock must equal the GBA's outright"))
        elif f == "gGameClock" and rules:
            if not ctx["clock_replayed"] and not ctx["clock"][t]["ok"]:
                diffs.append((f, ctx["clock"][t]["detail"]))
        elif gv != pv:
            diffs.append((f, f"GBA 0x{gv}, PC 0x{pv}"))

    for f in RAWS:
        gb, pb = bytearray.fromhex(g[f]), bytearray.fromhex(p[f])
        notes = []
        if rules and f == "gBmSt":
            for i in range(*PREVVCOUNT):
                gb[i] = pb[i] = 0
        if rules and f == "gPlaySt" and ctx.get("clock_outright"):
            pass  # lag replay: the clock copies are compared byte for byte, like the clock
        elif rules and f == "gPlaySt" and ctx["clock_replayed"]:
            for off, _ in CLOCK_COPIES:
                gb[off:off + 4] = pb[off:off + 4] = b"\0\0\0\0"
        elif rules and f == "gPlaySt":
            for off, (ok, detail) in ctx["copies"][t].items():
                if ok:
                    gb[off:off + 4] = pb[off:off + 4] = b"\0\0\0\0"
                else:
                    notes.append(detail)
        if gb != pb:
            offs = [i for i in range(len(gb)) if gb[i] != pb[i]]
            # Oracle-limit masks on a RAWS field (check "bitfield": OL-7's
            # survival rank in gPlaySt), as for the HASHED fields below.
            covered = set()
            if level == "result":
                for m in ctx["masks"]:
                    if m["apply"] and m["field"] == f and mask_condition(m, g, p):
                        c, worst = CHECKS[m["check"]](m, gb, pb, offs)
                        if c:
                            covered |= c
                            mask_hits.append((m["id"], worst))
            offs = [i for i in offs if i not in covered]
            if offs:
                parts = [f"{describe_offset(f, i)} GBA {gb[i]:02x} PC {pb[i]:02x}" for i in offs[:6]]
                more = f", {len(offs) - 6} more bytes" if len(offs) > 6 else ""
                inside = f" (and {len(covered)} bytes inside an oracle-limit mask)" if covered else ""
                diffs.append((f, "; ".join(parts) + more + inside + "".join(f" [{x}]" for x in notes)))

    for f, _ in HASHED:
        if g[f] == p[f]:
            continue
        ga, pa = raw_region(ctx["gba_raw"], t, f), raw_region(ctx["pc_raw"], t, f)
        if ga is None or pa is None:
            diffs.append((f, f"hash GBA {g[f]}, PC {p[f]} (no raw dump: masks not evaluated)"))
            continue
        offs = [i for i in range(len(ga)) if ga[i] != pa[i]]
        covered = set()
        if level == "result":
            for m in ctx["masks"]:
                if m["apply"] and m["field"] == f and mask_condition(m, g, p):
                    c, worst = CHECKS[m["check"]](m, ga, pa, offs)
                    if c:
                        covered |= c
                        mask_hits.append((m["id"], worst))
        rest = [o for o in offs if o not in covered]
        if rest:
            o = rest[0]
            detail = (f"hash GBA {g[f]}, PC {p[f]}; first byte {describe_offset(f, o)} "
                      f"GBA {ga[o]:02x} PC {pa[o]:02x}; {len(rest)} bytes differ")
            if covered:
                detail += f" (and {len(covered)} bytes inside an oracle-limit mask)"
            diffs.append((f, detail))
    for f, size in HASHED_ONLY:
        gv, pv = g.get(f), p.get(f)
        if gv is None and pv is None:
            continue  # a resumed trace's prologue from an older photo (load_trace)
        if gv != pv:
            diffs.append((f, f"hash GBA {gv}, PC {pv} (0x{STATIC_ADDRS[f]:08X} + 0x{size:X}; hashed only, no raw "
                             "dump: the bytes are not named)"))
    for f in CHEAT_FIELDS:  # the test cheats' fields (absent from both without one)
        gv, pv = g.get(f), p.get(f)
        if gv != pv:
            diffs.append((f, f"GBA {gv}, PC {pv} (the test cheats at the tick boundary, README.md \"Test "
                             "cheats\")"))
    gf, pf = frame15_value(g.get("frame15")), frame15_value(p.get("frame15"))
    if gf is not None and pf is not None and gf != pf:
        detail = f"GBA {gf}, PC {pf}"
        if level == "result":
            frame_hit, note = frame_rule(t, ctx, diffs)
            if frame_hit is None:
                frame_hit, rnote = raster_rule(t, ctx, diffs, gf)
                if rnote:
                    detail += f" [not a {RASTER_FRAME}: {rnote}]"
                    if ctx.get("raster_notes") is not None:
                        ctx["raster_notes"][t] = rnote
            if note and frame_hit is None:
                detail += f" [not an {ORACLE_FRAME}: {note}]"
                if ctx.get("frame_notes") is not None:
                    ctx["frame_notes"][t] = note
        if frame_hit is None:
            diffs.append(("frame15", detail))
    return diffs, mask_hits, frame_hit


ORACLE_FRAME = "oracle-limit frame"


def raster_rule(t, ctx, others, gf):
    """The raster-timing frame rule (README.md, "Raster-timing frames";
    DECISIONS.md item 9, P-17) for a frame15 difference at tick t that is not
    an oracle-limit frame. The PC trace (--pc, or the lag replay on the lag
    check) redrew frame t with the GBA's timing ("raster": "gba"): the same
    frame step lines again, from the PC's own machine (whose watched memory
    equals the GBA's at the frame's end), with every watched store of the
    GBA's frame made just before the GBA line event it came before. It is
    labelled only when that redraw came from the GBA trace's own raster log
    block of tick t ("raster_rec" is the GBA's "raster"), it is exactly the
    GBA's frame (gf), nothing else differs at tick t (others) and nothing but
    frame15 at tick t-1. Returns (hit, note): hit is a dict when labelled, else
    None; note says why not when the trace has a raster mark, else None."""
    p, g = ctx["pticks"][t], ctx["gticks"][t]
    mark = p.get("raster")
    if mark is None:
        return None, None
    if mark != "gba":
        note = RASTER_MARKS.get(mark, f"raster mark {mark}")
        return None, note + (f" ({p['raster_note']})" if p.get("raster_note") else "")
    if p.get("raster_rec") != g.get("raster"):
        return None, (f"the PC run redrew it from raster log block {p.get('raster_rec')}, not from the GBA trace's "
                      f"{g.get('raster')}")
    if p.get("frame15_raster") != gf:
        return None, (f"with the GBA's timing the PC draws {p.get('frame15_raster')}, not the GBA's frame {gf}: "
                      "something besides the timing differs")
    if others:
        return None, f"other fields differ at tick {t}: " + ", ".join(f for f, _ in others)
    if t > FIRST and "result_cache" in ctx:
        prev = [f for f, _ in result_tick(t - 1, ctx)[0] if f != "frame15"]
        if prev:
            return None, f"fields other than frame15 differ at tick {t - 1}: " + ", ".join(prev)
    return {"kind": "raster", "rec": g.get("raster")}, None


def result_tick(t, ctx):
    """compare_tick at level result for tick t of ctx's traces, computed once
    per tick (the frame rule looks back at tick t-1)."""
    cache = ctx["result_cache"]
    if t not in cache:
        cache[t] = compare_tick(ctx["gticks"][t], ctx["pticks"][t], t, ctx, "result")
    return cache[t]


def frame_rule(t, ctx, others):
    """The oracle-limit frame rule (P2.2b) for a frame15 difference at tick t.

    The frame of tick t is drawn with the shadow registers that OnVBlank
    flushed after tick t-1's sample (bm.c:314-321), so it shows tick t-1's
    gLCDControlBuffer. The difference is masked only when all of these hold:
    every difference at tick t-1 (RESULT, every field) lies inside applied
    masks and at least one mask hit there; every limit hit at t-1 has
    "frames": true and covers bg2affin only (FRAME_RULE_GROUPS); at t-1 BG2
    is shown (affine_bgs_shown); others (every other RESULT difference at tick
    t) is empty; and the affine replay proves that bg2affin is the only cause
    (affine_replay_check). Returns (hit, note): hit is a dict when masked, else
    None; note says why not when tick t-1 differs only inside masks, else None."""
    if t < 1 or t <= FIRST or "result_cache" not in ctx:
        return None, None
    prev_diffs, prev_hits, _ = result_tick(t - 1, ctx)
    if prev_diffs or not prev_hits:
        return None, None  # t-1 equal, or a difference outside the masks: not explained
    limits = {m["id"]: m for m in ctx["masks"]}
    ids = sorted({mid for mid, _ in prev_hits})
    where = f"tick {t - 1} differs only inside {', '.join(ids)}"
    no_rule = [i for i in ids if not limits[i]["frames"]]
    if no_rule:
        return None, f"{where}, and {', '.join(no_rule)} has no frame rule"
    groups = {int(k, 0) for _, worst in prev_hits for k in worst}
    outside = sorted(groups - set(FRAME_RULE_GROUPS))
    if outside:
        return None, (f"{where}, including " + ", ".join(
            f"{LCD_MEMBERS_BY_OFF.get(b, hex(b))} (+0x{b:02X})" for b in outside)
            + "; the frame rule covers bg2affin only (P2.2b)")
    lcd = raw_region(ctx["gba_raw"], t - 1, "gLCDControlBuffer")
    shown = affine_bgs_shown(lcd) & groups
    if not shown:
        dispcnt = lcd[0] | lcd[1] << 8
        return None, (f"{where}, but DISPCNT 0x{dispcnt:04X} at tick {t - 1} shows no BG that uses "
                      + ", ".join(f"{AFFINE_BGS[b][0]}'s matrix (+0x{b:02X})" for b in sorted(groups)))
    if others:
        return None, f"{where}, but other fields differ at tick {t}: " + ", ".join(f for f, _ in others)
    why = affine_replay_check(t, ctx)
    if why:
        return None, f"{where}, but {why}"
    return {"prev": t - 1, "masks": ids, "bgs": [AFFINE_BGS[b][0] for b in sorted(shown)],
            "affine_replay": "GBA frame"}, None


LCD_MEMBERS_BY_OFF = dict(LCD_MEMBERS)


def tick_ranges(ticks):
    """'5880' or '6568-6655, 6700' for a sorted list of ticks."""
    out = []
    for t in ticks:
        if out and out[-1][1] == t - 1:
            out[-1][1] = t
        else:
            out.append([t, t])
    return ", ".join(str(a) if a == b else f"{a}-{b}" for a, b in out)


def same_run(a, p):
    """The affine replay's tick line equals the PC trace's outside the frame
    keys (the PC trace's raster-timing redraw keys included: the affine replay
    runs without it)."""
    skip = AFFINE_FRAME_KEYS + RASTER_KEYS
    return {k: v for k, v in a.items() if k not in skip} == {k: v for k, v in p.items() if k not in skip}


def affine_replay_check(t, ctx):
    """Condition 4 of the frame rule: None when the affine replay proves that
    frame t differs only through bg2affin, else the reason it does not.

    Normal run: the affine replay (GATE0_AFFINE_FROM) drew frame t with the
    GBA's bg2affin of tick t-1 ("affine": "gba"); that frame equals the GBA's;
    the same frame drawn again with the PC's own registers ("frame15_pc")
    equals the PC trace's frame, so both are the frame of the same machine
    state; and the replay equals the PC trace at ticks t-1 and t outside the
    frame. Lag replay or clock replay (lag check): the normal run's frame t
    passed, and the replay drew the same frame."""
    base = ctx.get("frame_base")
    if base is not None:
        hit = result_tick(t, base)[2]
        if hit is None or hit.get("kind") == "raster":  # a raster-timing frame is not an oracle-limit frame
            return "the normal PC run's frame at this tick is not an oracle-limit frame"
        if ctx["pticks"][t].get("frame15") != base["pticks"][t].get("frame15"):
            return f"the {ctx['replay_kind']} drew another frame than the normal PC run"
        return None
    aff = ctx.get("affine")
    if aff is None:
        return "there is no affine replay to show that bg2affin is the only cause (--affine-replay)"
    if t >= len(aff):
        return f"the affine replay trace ends before tick {t}"
    a = aff[t]
    mark = a.get("affine")
    if mark != "gba":
        return f"the affine replay did not draw this frame with the GBA's bg2affin (affine: {mark})"
    for k in (t - 1, t):
        if not same_run(aff[k], ctx["pticks"][k]):
            return f"the affine replay differs from the PC trace at tick {k} outside the frame"
    pf = frame15_value(ctx["pticks"][t].get("frame15"))
    if a.get("frame15_pc") != pf:
        return (f"the affine replay's frame redrawn with the PC's own registers ({a.get('frame15_pc')}) "
                f"is not the PC trace's frame ({pf})")
    gf = frame15_value(ctx["gticks"][t].get("frame15"))
    if a.get("frame15") != gf:
        return (f"with the GBA's bg2affin the PC draws {a.get('frame15')}, not the GBA's frame {gf}: "
                "something besides bg2affin differs")
    return None


def run_pass(gticks, pticks, n, ctx, level, keep=None):
    """keep: if a dict, filled with tick -> list of (field, detail) for every differing tick.
    Returns (first, per_field, ticks_with_diff, mask_stats, frames); frames lists
    (tick, frame_hit) for each oracle-limit frame (RESULT only)."""
    first, per_field, ticks_with_diff = None, {}, 0
    mask_stats, frames = {}, []
    for t in range(FIRST, n):
        g, p = gticks[t], pticks[t]
        if g["tick"] != t or p["tick"] != t:
            print(f"gate0_compare: tick numbering broken at {t}")
            sys.exit(2)
        if level == "result":
            d, hits, frame_hit = result_tick(t, ctx)
        else:
            d, hits, frame_hit = compare_tick(g, p, t, ctx, level)
        if frame_hit is not None:
            frames.append((t, frame_hit))
        for mid, worst in hits:
            s = mask_stats.setdefault(mid, {"ticks": 0, "first": t, "last": t, "worst": {}})
            if s["last"] != t or s["ticks"] == 0:
                s["ticks"] += 1
            s["last"] = t
            for k, v in worst.items():
                old = s["worst"].get(k)
                s["worst"][k] = v if old is None else (tuple(map(max, old, v)) if isinstance(v, tuple) else max(old, v))
        if d:
            ticks_with_diff += 1
            if keep is not None:
                keep[t] = d
            if first is None:
                first = (t, d)
            for f, _ in d:
                per_field.setdefault(f, t)
    return first, per_field, ticks_with_diff, mask_stats, frames


# --- Lag check (README.md, "Lag replay"; "Clock replay" for the diagnostic) ---

def lag_positions_check(gticks, n):
    """The GBA trace's lag positions against its lag counters (a harness
    check): each tick has one "lag_at" entry per new lag VBlank. Returns the
    ticks that do not, as (tick, entries, new lag VBlanks)."""
    bad = []
    for t in range(FIRST + 1 if FIRST else 0, n):  # a resumed trace: the prologue has no previous tick
        new = lag(gticks[t]) - (lag(gticks[t - 1]) if t else 0)
        got = len(gticks[t].get("lag_at", []))
        if got != new:
            bad.append((t, got, new))
    return bad


def same_lag(g, r):
    """The lag replay ran tick g's lag VBlanks where the GBA's came: the same
    "lag_at" positions, and an early flush VBlank (README.md, "Early flush
    VBlank") exactly where the GBA had one, a lag VBlank that came inside a
    CpuSet or CpuFastSet ("lag_split", README.md "Lag position") run at the
    same point of that copy, one that came at a RunProcessScript's head
    ("lag_head") run there, and, in a GBA trace with sound positions
    ("lag_snd", README.md "Sound on any route"), after as many of the main
    loop's m4a calls, inside one at the same place ("lag_snd_split") and with
    the locks of an IRQ handler's call it came inside ("lag_snd_nested")."""
    return (r.get("lag_at", []) == g.get("lag_at", []) and bool(r.get("early_flush")) == bool(g.get("early_flush"))
            and r.get("lag_split", []) == g.get("lag_split", []) and r.get("lag_head", []) == g.get("lag_head", [])
            and r.get("lag_snd", []) == g.get("lag_snd", [])
            and r.get("lag_snd_split", []) == g.get("lag_snd_split", [])
            and r.get("lag_snd_nested", []) == g.get("lag_snd_nested", []))


def prefix_left(prefix):
    """A resumed lag replay's prefix (savestate.py lag_prefix) says it had left
    the GBA's sequence before the snapshot."""
    return bool(prefix) and bool(prefix.get("event_mismatch_ticks") or prefix.get("left_at_tick") is not None
                                 or prefix.get("lag_vblanks_run") != prefix.get("lag_vblanks_at_gba_position"))


def sequence_left_at(sequence, prefix=None):
    """The first tick at which the lag replay left the GBA's sequence: for a
    resumed replay that had left it before the snapshot, the real tick from its
    prefix (left_at_tick, or its first tick with another event count), not the
    first compared tick that carries it; None if it never left."""
    if not sequence:
        return None
    t = min(sequence)
    if t == FIRST and FIRST and prefix_left(prefix):
        real = prefix.get("left_at_tick")
        if real is None:
            real = min(prefix.get("first_event_mismatch_ticks") or [t])
        return min(real, t)
    return t


def replay_sequence(gticks, rticks, n_r, prefix=None):
    """Per tick of the lag replay: absent when it ran the GBA's number of
    main-loop events and its lag VBlanks at the GBA's positions ("lag_at",
    "early_flush"), else the reason (README.md, "Lag replay").

    prefix: a resumed lag replay's record of its ticks before the snapshot
    (its header's resume.prefix, README.md "Save states"). If it had left the
    GBA's sequence there, the first compared tick carries that, so no tick of
    the window is lag-only, as in the full run's compare."""
    out = {}
    if prefix_left(prefix):
        first = prefix.get("first_event_mismatch_ticks") or []
        off = prefix.get("lag_vblanks_run", 0) - prefix.get("lag_vblanks_at_gba_position", 0)
        out[FIRST] = (f"before the snapshot at tick {FIRST + 1}: its run from power-on had "
                      f"{prefix.get('event_mismatch_ticks')} ticks with another main-loop event count"
                      + (f", first at {', '.join(map(str, first))}" if first else "")
                      + (f", and {off} lag VBlanks run off the GBA's position" if off else ""))
    for t in range(FIRST, n_r):
        g, r = gticks[t], rticks[t]
        why = []
        if r["events"] != g["events"]:
            why.append(f"the lag replay has {r['events']} main-loop events at this tick, the GBA {g['events']}")
        if r.get("lag_at", []) != g.get("lag_at", []):
            why.append(f"the lag replay ran this tick's lag VBlanks after {r.get('lag_at', [])} events, "
                       f"the GBA's came after {g.get('lag_at', [])}")
        if bool(r.get("early_flush")) != bool(g.get("early_flush")):
            why.append("the GBA's last lag VBlank here is " + ("" if g.get("early_flush") else "not ")
                       + "an early flush VBlank, the lag replay's is " + ("" if r.get("early_flush") else "not"))
        if r.get("lag_split", []) != g.get("lag_split", []):
            why.append(f"the GBA's lag VBlanks inside a BIOS copy were {g.get('lag_split', [])} (index, bytes "
                       f"stored, bytes loaded), the lag replay's {r.get('lag_split', [])}")
        if r.get("lag_head", []) != g.get("lag_head", []):
            why.append(f"the GBA's lag VBlanks at a RunProcessScript's head were {g.get('lag_head', [])} (index), "
                       f"the lag replay ran {r.get('lag_head', [])} there")
        if r.get("lag_snd", []) != g.get("lag_snd", []):
            why.append(f"the GBA's lag VBlanks came after {g.get('lag_snd', [])} m4a calls since their events, the lag"
                       f" replay ran them after {r.get('lag_snd', [])}")
        if r.get("lag_snd_split", []) != g.get("lag_snd_split", []):
            why.append(f"the GBA's lag VBlanks inside an m4a call came at {g.get('lag_snd_split', [])} (index, TrackStop"
                       f" calls, locks), the lag replay ran them at {r.get('lag_snd_split', [])}")
        if r.get("lag_snd_nested", []) != g.get("lag_snd_nested", []):
            why.append(f"the GBA's lag VBlanks inside an IRQ handler's m4a call found the locks"
                       f" {g.get('lag_snd_nested', [])} (index, locks), the lag replay ran them with"
                       f" {r.get('lag_snd_nested', [])}")
        if why:
            out[t] = "; ".join(why) if t not in out else out[t] + "; " + "; ".join(why)
    return out


def lag_check(result_diffs, rticks, n, ctx, ctx_replay, sequence=None, left_at=None):
    """Label each RESULT difference against a replay. Returns (labels, replay_only):
    labels: tick -> list of (field, detail, label, replay detail), label one of
    'lag-only', 'clock rule', 'not lag-only' or 'no replay tick' (oracle-limit
    frames are masked in RESULT, on both runs, and so are not here);
    replay_only: ticks where the replay's RESULT differs and the PC trace's
    does not (the replay changed the run there). left_at: the tick named as
    where the replay left the GBA's sequence (sequence_left_at: for a resumed
    replay that had left it before the snapshot, the real tick).

    With sequence (the lag replay; replay_sequence's result) a tick is
    lag-only only as a whole: the replay's RESULT has no difference at all
    there, and the replay ran the GBA's events and lag VBlanks on every tick
    up to and including this one (no tick of sequence at or before it: a
    replay that ran a lag VBlank elsewhere, or did other main-loop work, is
    not the GBA's lag from there on); otherwise every field of it is not
    lag-only. Without it (the clock replay, a diagnostic) fields are labelled
    one by one, as the lag check did before the lag replay."""
    n_r = min(n, len(rticks))
    labels, replay_only = {}, []
    replay_fields = {}
    first_off = min(sequence) if sequence else None
    for t in range(FIRST, n_r):
        if rticks[t]["tick"] != t:
            print(f"gate0_compare: {ctx_replay['replay_kind']} trace tick numbering broken at {t}")
            sys.exit(2)
        d, _, _ = result_tick(t, ctx_replay)
        if d:
            replay_fields[t] = d
            if t not in result_diffs:
                replay_only.append(t)
    for t, d in result_diffs.items():
        out = []
        rlist = replay_fields.get(t, [])
        rd = {f: x for f, x in rlist}
        # The replay's frame of this tick passed only as a raster-timing frame
        # (G4 review): then its frame15 is not "lag-only" but its own label.
        rhit = result_tick(t, ctx_replay)[2] if t < n_r else None
        replay_raster = rhit is not None and rhit.get("kind") == "raster"
        whole = None  # lag replay: why the tick as a whole is not explained
        if sequence is not None and t < n_r:
            if rlist:
                whole = "the lag replay differs at this tick in " + ", ".join(f for f, _ in rlist)
            elif t in sequence:
                whole = sequence[t]
            elif first_off is not None and first_off < t:
                whole = (f"the lag replay left the GBA's sequence at tick "
                         f"{first_off if left_at is None else left_at} ({sequence[first_off]}), so it is not the "
                         "GBA's lag after that")
        for f, detail in d:
            rdetail = rd.get(f)
            if t >= n_r:
                label = "no replay tick"
            elif f == "gGameClock" or (f == "gPlaySt" and any(
                    not ok for ok, _ in ctx["copies"][t].values())):
                label = "clock rule"
            elif f in rd:
                label = "not lag-only"
            elif whole is not None:
                label, rdetail = "not lag-only", whole
            elif f == "frame15" and replay_raster:
                label, rdetail = LAG_RASTER, ("the lag replay draws the GBA's frame only with the GBA's timing "
                                              f"({RASTER_FRAME}, raster log block {rhit.get('rec')})")
            else:
                label = "lag-only"
            out.append((f, detail, label, rdetail))
        labels[t] = out
    return labels, replay_only


# A lag-only tick whose lag replay frame is a raster-timing frame: reproduced by
# the lag replay at the GBA's positions, its frame with the GBA's timing too.
LAG_RASTER = "lag-only, raster-timing frame"


def lag_only_ticks(labels):
    return sorted(t for t, ls in labels.items() if all(x[2] == "lag-only" for x in ls))


def lag_raster_ticks(labels):
    """Ticks every field of which is lag-only or LAG_RASTER, with at least one LAG_RASTER."""
    return sorted(t for t, ls in labels.items() if all(x[2] in ("lag-only", LAG_RASTER) for x in ls)
                  and any(x[2] == LAG_RASTER for x in ls))


def range_pairs(ticks):
    """[[a, b], ...] for sorted ticks (compare.json's ticks_per_field; check_all.py's known limits)."""
    out = []
    for t in ticks:
        if out and t == out[-1][1] + 1:
            out[-1][1] = t
        else:
            out.append([t, t])
    return out


def short_ranges(ticks, limit=24):
    """tick_ranges of at most `limit` ranges, then the count of the rest."""
    parts = tick_ranges(ticks).split(", ") if ticks else []
    return ", ".join(parts[:limit]) + (f", ... ({len(parts) - limit} more ranges)" if len(parts) > limit else "")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--gba", required=True)
    ap.add_argument("--pc", required=True)
    ap.add_argument("--gba-raw")
    ap.add_argument("--pc-raw")
    ap.add_argument("--gba-elf")
    ap.add_argument("--pc-elf")
    ap.add_argument("--ticks", type=int, default=300)
    ap.add_argument("--masks", help="oracle-limit masks (port/test/oracle_limits.json)")
    ap.add_argument("--mask-oracle-limits", action="store_true",
                    help="the older spelling: --masks with port/test/oracle_limits.json")
    ap.add_argument("--lag-replay", help="lag check: the PC trace rerun with the GBA's lag VBlanks at their GBA "
                                         "positions (GATE0_LAG_FROM)")
    ap.add_argument("--lag-replay-raw", help="raw dump of the --lag-replay run")
    ap.add_argument("--clock-replay", "--replay", dest="clock_replay",
                    help="diagnostic only: the PC trace rerun with the GBA clock (GATE0_CLOCK_FROM)")
    ap.add_argument("--clock-replay-raw", "--replay-raw", dest="clock_replay_raw",
                    help="raw dump of the --clock-replay run")
    ap.add_argument("--affine-replay", help="P2.2b proof: the PC trace rerun with rotated frames drawn with "
                                            "the GBA's bg2affin (GATE0_AFFINE_FROM)")
    ap.add_argument("--json", help="also write a machine-readable summary here")
    ap.add_argument("--pc-log", help="the log of the --pc run: a [fault] line in it makes the verdict INCOMPLETE")
    ap.add_argument("--lag-replay-log", help="the log of the --lag-replay run (the same)")
    ap.add_argument("--affine-replay-log", help="the log of the --affine-replay run (the same)")
    ap.add_argument("--png-hashes", help="frame15 values of the GBA capture's PNGs, made beforehand by "
                                         "tools/png_hashes.py (README.md, \"PNG hash cache\"): no PNG decode here")
    args = ap.parse_args()
    if args.ticks < 1:
        ap.error("--ticks must be at least 1")
    for log, trace in (("--lag-replay-log", args.lag_replay), ("--affine-replay-log", args.affine_replay)):
        if getattr(args, log[2:].replace("-", "_")) and not trace:
            ap.error(f"{log} needs {log[:-4]}")
    if args.mask_oracle_limits and not args.masks:
        args.masks = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "oracle_limits.json")

    if args.png_hashes:
        if not load_png_hashes(args.png_hashes):
            return 2
    ghead, gticks = load_trace(args.gba)
    phead, pticks = load_trace(args.pc)
    # Save states: every compared trace from the same snapshot (its part per role), or none.
    heads = {"GBA": (ghead, ("gba",)),
             "--pc": (phead, ("pclag",) if phead.get("lag_from") else ("pc",))}
    for name, path, parts in (("--lag-replay", args.lag_replay, ("pclag",)),
                              ("--affine-replay", args.affine_replay, ("pcaff",))):
        if path and os.path.exists(path):
            with open(path) as fh:
                heads[name] = (json.loads(fh.readline() or "{}"), parts)
    resume = check_resume(heads)
    cheat = check_cheat(heads)
    if cheat:
        CHEAT_FIELDS[:] = cheat_fields(cheat, args.gba)
        FIELDS[FIELDS.index("frame15"):FIELDS.index("frame15")] = CHEAT_FIELDS
        print(f"NOTE: a cheat-assisted run (test coverage only; README.md, \"Test cheats\"): GATE0_CHEAT={cheat} on "
              + ("every compared trace; the refill's " if cheat == "durability" else "every compared trace; the cheats' ")
              + " and ".join(CHEAT_FIELDS) + " are compared on every tick")
    if resume:
        print(f"Resumed traces (README.md, \"Save states\"): snapshot set {resume['set']}, tick {resume['tick']}, "
              f"ROM CRC-32 {resume['rom_crc32']}; ticks {FIRST}-{args.ticks - 1} are compared, tick {FIRST} being "
              f"the snapshot run's own line (the prologue), which also seeds the clock rule")
        for name, (h, _) in heads.items():
            r = h.get("resume") or {}
            if r.get("saved_exe_crc32") and h.get("exe_crc32") and r["saved_exe_crc32"] != h["exe_crc32"]:
                print(f"NOTE: {name}: resumed by PC binary {h['exe_crc32']} from a snapshot made by binary "
                      f"{r['saved_exe_crc32']}: its host addresses were relinked by name (savestate.py relink; "
                      "halves of host addresses in procs' 16-bit fields cannot be carried, README.md \"Save states\")")
    if ghead.get("keys") != phead.get("keys"):
        print(f"WARNING: key files differ: GBA {ghead.get('keys')}, PC {phead.get('keys')}")
    if not phead.get("exe_crc32"):
        print(f"gate0_compare: {args.pc}: the PC trace does not name its binary (exe_crc32)")
        sys.exit(2)
    if args.pc_elf and os.path.exists(args.pc_elf) and file_crc32(args.pc_elf) != phead["exe_crc32"]:
        print(f"gate0_compare: {args.pc} was not made by {args.pc_elf} (exe_crc32 {phead['exe_crc32']}, "
              f"file {file_crc32(args.pc_elf)}): rebuild or rerun the PC side")
        sys.exit(2)
    if check_sram_in(ghead, phead, "the PC trace"):
        print(f"Both sides boot from the save {ghead.get('sram_in')} (fnv1a {ghead['sram_fnv']})")
    n = min(args.ticks, len(gticks), len(pticks))
    gba_table = nm_table("arm-none-eabi-nm", args.gba_elf)
    pc_table = nm_table("nm", args.pc_elf)
    problems, notes = check_addresses(gba_table, pc_table)
    for note in notes:
        print(f"WARNING: {note}")
    if problems:
        for prob in problems:
            print(f"gate0_compare: {prob}")
        sys.exit(2)
    ctx = {
        "gba_syms": function_names(gba_table, thumb=True),
        "pc_syms": function_names(pc_table, thumb=False),
        "gba_raw": load_raw(args.gba_raw, n),
        "pc_raw": load_raw(args.pc_raw, n),
        "masks": load_masks(args.masks),
    }
    ctx["clock_replayed"] = bool(phead.get("clock_from"))
    ctx["lag_replayed"] = bool(phead.get("lag_from"))
    # A lag replay has the GBA's lag VBlanks, so its clock is compared outright.
    ctx["clock_outright"] = ctx["lag_replayed"]
    if ctx["clock_replayed"]:
        print(f"DIAGNOSTIC: the PC clock was replayed from {phead['clock_from']} (README.md, \"Clock replay\"); "
              "gGameClock and its gPlaySt copies are not compared, and this is not a Gate 0 result")
    if ctx["lag_replayed"]:
        print(f"NOTE: --pc is the lag replay (the GBA's lag VBlanks from {phead['lag_from']}, README.md, "
              "\"Lag replay\"), compared on its own with the outright clock rule: a diagnostic, or the route check "
              "of port/check-all.mk; the plain PC run's Gate 0 result needs the plain trace with --lag-replay")
    bad = lag_positions_check(gticks, n)
    if bad:
        print(f"gate0_compare: {args.gba}: the lag positions (lag_at) do not match the lag counters on "
              f"{len(bad)} ticks, first: " + ", ".join(f"tick {t} has {a}, the counters say {b}" for t, a, b in bad[:4]))
        sys.exit(2)
    ctx["clock"] = clock_rule(gticks, pticks, n)
    ctx["copies"] = copy_rule(gticks, pticks, n, ctx["clock"])
    ctx["gticks"], ctx["pticks"] = gticks, pticks
    ctx["result_cache"] = {}
    ctx["frame_notes"] = {}
    ctx["raster_notes"] = {}
    ctx["affine"] = None
    # Raster-timing frames (README.md, "Raster-timing frames"): the GBA trace's
    # raster log must watch the regions this script was written for.
    if ghead.get("raster_log") is not None and ghead["raster_log"] != RASTER_LOG:
        print(f"gate0_compare: {args.gba}: its raster log watches {ghead['raster_log']}, this compare knows "
              f"{RASTER_LOG} (port/test/Makefile GBA_RASTER_RAM and RASTER_LOG here must agree)")
        sys.exit(2)
    if args.affine_replay:
        if ctx["clock_replayed"] or ctx["lag_replayed"]:
            print("gate0_compare: --pc is a replay trace; the affine replay belongs to the normal PC trace")
            sys.exit(2)
        ahead, aticks = load_trace(args.affine_replay)
        if not ahead.get("affine_from") or ahead.get("clock_from") or ahead.get("lag_from"):
            print(f"gate0_compare: {args.affine_replay}: not an affine replay of the normal PC run "
                  "(its header needs affine_from, and no clock_from or lag_from)")
            sys.exit(2)
        check_same_binary(args.affine_replay, ahead, phead)
        if ahead.get("keys") != phead.get("keys"):
            print(f"WARNING: key files differ: PC {phead.get('keys')}, affine replay {ahead.get('keys')}")
        for t, a in enumerate(aticks):
            if a is not None and a["tick"] != t:
                print(f"gate0_compare: affine replay trace tick numbering broken at {t}")
                sys.exit(2)
        ctx["affine"] = aticks
    if (args.lag_replay or args.clock_replay) and (ctx["clock_replayed"] or ctx["lag_replayed"]):
        print("gate0_compare: --pc is itself a replay trace; give the normal PC trace with --lag-replay "
              "or --clock-replay")
        sys.exit(2)
    # The lag check (README.md, "Lag replay"): the replay is compared with the
    # GBA under the same rules, except the clock: it has the GBA's lag, so its
    # gGameClock and the gPlaySt clock copies must equal the GBA's outright
    # (clock_outright; no excused offset).
    rticks, ctx_replay, sequence, sequence_left = None, None, None, None
    if args.lag_replay:
        rhead, rticks = load_trace(args.lag_replay)
        if not rhead.get("lag_from") or rhead.get("clock_from") or rhead.get("affine_from"):
            print(f"gate0_compare: {args.lag_replay}: not a lag replay trace (its header needs lag_from, "
                  "and no clock_from or affine_from)")
            sys.exit(2)
        if rhead.get("keys") != phead.get("keys"):
            print(f"gate0_compare: key files differ: PC {phead.get('keys')}, lag replay {rhead.get('keys')}")
            sys.exit(2)
        check_same_binary(args.lag_replay, rhead, phead)
        n_r = min(n, len(rticks))
        clk = clock_rule(gticks, rticks, n_r)
        ctx_replay = dict(ctx, pc_raw=load_raw(args.lag_replay_raw, n), lag_replayed=True, clock_outright=True,
                          pticks=rticks, clock=clk, copies=copy_rule(gticks, rticks, n_r, clk), result_cache={},
                          frame_notes=None, raster_notes=None, frame_base=ctx, replay_kind="lag replay")
        rprefix = (rhead.get("resume") or {}).get("prefix")
        sequence = replay_sequence(gticks, rticks, n_r, rprefix)
        sequence_left = sequence_left_at(sequence, rprefix)
    # The clock replay (README.md, "Clock replay"): a diagnostic, never an excuse.
    cticks, ctx_clock = None, None
    if args.clock_replay:
        chead, cticks = load_trace(args.clock_replay)
        if not chead.get("clock_from"):
            print(f"gate0_compare: {args.clock_replay}: not a clock replay trace (no clock_from in its header)")
            sys.exit(2)
        if chead.get("keys") != phead.get("keys"):
            print(f"WARNING: key files differ: PC {phead.get('keys')}, clock replay {chead.get('keys')}")
        check_same_binary(args.clock_replay, chead, phead)
        ctx_clock = dict(ctx, pc_raw=load_raw(args.clock_replay_raw, n), clock_replayed=True, clock_outright=False,
                         pticks=cticks,
                         result_cache={}, frame_notes=None, raster_notes=None, frame_base=ctx,
                         replay_kind="clock replay")

    # Complete runs (integration review, 2026-10-20; docstring): every
    # compared trace has the requested ticks and no PC run logged a fault, or
    # the verdict is INCOMPLETE. The clock replay given with --clock-replay is
    # a diagnostic that excuses nothing, so it is not one of them.
    pc_label = ("lag replay (--pc)" if ctx["lag_replayed"] else
                "clock replay (--pc)" if ctx["clock_replayed"] else "PC")
    runs = [("gba", "GBA", args.gba, len(gticks), None),
            ("pc", pc_label, args.pc, len(pticks), args.pc_log)]
    if rticks is not None:
        runs.append(("lag_replay", "lag replay", args.lag_replay, len(rticks), args.lag_replay_log))
    if ctx["affine"] is not None:
        runs.append(("affine_replay", "affine replay", args.affine_replay, len(ctx["affine"]),
                     args.affine_replay_log))
    incomplete, summary_runs = [], {}
    for key, label, path, count, log in runs:
        rec = {"trace": path, "ticks": count, "log": log, "fault": None}
        if count < args.ticks:
            incomplete.append(f"the {label} trace has {count:,} of {args.ticks:,} ticks"
                              + (f" (it ends at tick {count - 1})" if count else ""))
        if log:
            rec["fault"] = first_fault(log)
            if rec["fault"]:
                incomplete.append(f"the {label} run logged a fault ({log}): {rec['fault'][:120]}")
        summary_runs[key] = rec

    print(f"Gate 0 compare: GBA {args.gba} ({len(gticks) - FIRST} ticks), PC {args.pc} ({len(pticks) - FIRST} ticks), "
          f"comparing ticks {FIRST}-{n - 1}")
    for why in incomplete:
        print(f"INCOMPLETE: {why}")
    if not any(log for _, _, _, _, log in runs):
        print("NOTE: no PC run log given (--pc-log ...): faults are not checked, only tick counts")
    pc_kind = "lag replay" if ctx["lag_replayed"] else ("clock replay" if ctx["clock_replayed"] else "pc")
    if n <= FIRST:  # a run that stopped before its first tick: nothing to compare
        who = "GATE 0 (LAG REPLAY vs GBA)" if ctx["lag_replayed"] else "GATE 0"
        print(f"{who}: INCOMPLETE: " + "; ".join(incomplete) + " (no tick to compare)")
        if args.json:
            with open(args.json, "w") as fh:
                json.dump({"ticks_compared": 0, "ticks_requested": args.ticks, "gba_ticks": len(gticks),
                           "pc_ticks": len(pticks), "runs": summary_runs, "incomplete": incomplete,
                           "verdict_on_compared_ticks": None, "verdict": "incomplete", "pc_kind": pc_kind,
                           **({"cheat": cheat} if cheat else {})},
                          fh, indent=1)
        return 1
    if n < args.ticks:
        shorter = "PC" if len(pticks) < len(gticks) else "GBA"
        print(f"NOTE: only {n} of {args.ticks} requested ticks are in both traces ({shorter} trace ends at tick {n - 1})")
    if gba_table is not None:
        print("Addresses checked against the symbol tables: breakpoints "
              + ", ".join(f"{k} 0x{v[2]:08X}" for k, v in GBA_BREAKPOINTS.items())
              + "; statics " + ", ".join(f"{k} 0x{v:08X}" for k, v in STATIC_ADDRS.items())
              + (" (GBA and PC)" if pc_table is not None else " (GBA only)"))

    # Timing summary: VBlanks per tick, and lag VBlanks (GBA only, by design).
    def lag_ticks(ticks):
        out = []
        for t in range(FIRST + 1 if FIRST else 0, n):
            prev = lag(ticks[t - 1]) if t else 0
            if lag(ticks[t]) != prev:
                out.append((t, lag(ticks[t]) - prev))
        return out

    glag, plag = lag_ticks(gticks), lag_ticks(pticks)
    print(f"GBA: tick {FIRST} at frame {gticks[FIRST]['frame']}; ticks with lag VBlanks: {len(glag)}, "
          f"{sum(d for _, d in glag)} VBlanks; first: "
          + (", ".join(f"{t} (+{d})" for t, d in glag[:12]) if glag else "none")
          + (", ..." if len(glag) > 12 else ""))
    print(f"PC:  tick {FIRST} at frame {pticks[FIRST]['frame']}; ticks with lag VBlanks: {len(plag)}"
          + (": " + ", ".join(f"{t} (+{d})" for t, d in plag[:12]) if plag else ""))
    waits_extra = [t for t in range(FIRST + 1, n) if gticks[t]["vblank_waits"] - gticks[t - 1]["vblank_waits"] != 1]
    print("Ticks with more than one VBlankIntrWait (game waits inside OnMain): "
          + (", ".join(map(str, waits_extra[:12])) + (", ..." if len(waits_extra) > 12 else "")
             if waits_extra else "none"))
    other_main = []  # runs of ticks whose main callback is not OnMain (GBA side)
    for t in range(FIRST + 1, n):
        if gticks[t]["onmain_calls"] == gticks[t - 1]["onmain_calls"]:
            if other_main and other_main[-1][1] == t - 1:
                other_main[-1][1] = t
            else:
                other_main.append([t, t])
    print("Ticks without an OnMain (another main callback, such as the battle animations' "
          "MainUpdate_8055C68): " + (", ".join(f"{a}-{b}" for a, b in other_main[:8])
                                     + (", ..." if len(other_main) > 8 else "") if other_main else "none"))
    writes = [t for t in range(FIRST, n) if ctx["clock"][t]["write"]]
    print(f"gGameClock: excused offset (GBA lag VBlanks counted by the clock) at tick {n - 1}: "
          f"{ctx['clock'][n - 1]['E']}, largest {max(c['E'] for c in ctx['clock'] if c)}; clock writes at ticks: "
          + (", ".join(map(str, writes[:12])) + (", ..." if len(writes) > 12 else "") if writes else "none")
          + (f" (seeded at tick {FIRST} with offset {ctx['clock'][FIRST]['E']})" if FIRST else ""))
    summary_clock = {"final_offset": ctx["clock"][n - 1]["E"], "write_ticks": writes}
    gframes = sum(1 for g in gticks[FIRST:n] if g.get("frame15"))
    pframes = sum(1 for p in pticks[FIRST:n] if p.get("frame15"))
    print(f"frame15: GBA has {gframes} frames, PC has {pframes}; "
          + ("compared where both exist" if pframes else "not compared (the PC trace has no frames)"))
    # Raster-timing redraw (README.md, "Raster-timing frames"): how the PC run(s) used the GBA's raster log.
    graster = sum(1 for g in gticks[FIRST:n] if g.get("raster"))
    summary_raster = {"gba_frames_logged": graster}
    if graster or ghead.get("raster_log"):
        for what, head, ticks in (("PC trace", phead, pticks),) + ((("lag replay", rhead, rticks),) if rticks else ()):
            marks = {}
            for p in ticks[FIRST:min(n, len(ticks))]:
                if p.get("raster"):
                    marks[p["raster"]] = marks.get(p["raster"], 0) + 1
            print(f"Raster-timing redraw: the GBA's raster log has {graster} frame15 frames; the {what} "
                  + (f"redrew them from {head['raster_from']} ("
                     + ", ".join(f"{k} {v}" for k, v in sorted(marks.items(), key=lambda kv: -kv[1])) + ")"
                     if head.get("raster_from") else "ran without it (GATE0_RASTER_FROM): no frame can be labelled"))
            summary_raster["lag_replay" if what == "lag replay" else "pc"] = marks
    other_copy = [t for t in range(FIRST, n) if gticks[t].get("lag_in_other_copy")]
    if other_copy:
        print(f"NOTE: on {len(other_copy)} ticks a GBA lag VBlank came inside a main-loop BIOS copy into other "
              f"memory than video memory (no event; the lag replay runs it after the copy; README.md, \"Lag "
              f"position\"): {short_ranges(other_copy, 12)}")
    summary_raster["gba_lag_in_other_copy_ticks"] = other_copy
    if ctx["masks"]:
        print("Oracle-limit masks: " + ", ".join(
            f"{m['id']} ({m['field'] or 'no field'}{'' if m['apply'] else ', listed only'}"
            f"{', and frames (P2.2b)' if m['frames'] else ''})" for m in ctx["masks"]))
    summary_affine = None
    if ctx["affine"] is not None:
        # The affine replay as a whole (README.md, "Affine replay"): which frames
        # it redrew, and whether it is the same run as the PC trace.
        aticks = ctx["affine"]
        n_a = min(n, len(aticks))
        off = [t for t in range(FIRST, n_a) if not same_run(aticks[t], pticks[t])]
        marks = {}
        for t in range(FIRST, n_a):
            if aticks[t].get("affine") is not None:
                marks.setdefault(aticks[t]["affine"], []).append(t)
        gba_drawn = marks.get("gba", [])
        as_gba = [t for t in gba_drawn if aticks[t].get("frame15") == frame15_value(gticks[t].get("frame15"))]
        pc_bad = [t for t in gba_drawn if aticks[t].get("frame15_pc") != pticks[t].get("frame15")]
        equal_bad = [t for t in marks.get("equal", []) if aticks[t].get("frame15") != pticks[t].get("frame15")]
        print(f"Affine replay: {args.affine_replay} ({len(aticks)} ticks); frames drawn with the GBA's bg2affin: "
              f"{len(gba_drawn)}, {len(as_gba)} of them equal to the GBA's frame"
              + "".join(f"; {k}: {len(v)}" for k, v in sorted(marks.items()) if k != "gba"))
        print("Affine replay against the PC trace: outside the frames "
              + ("equal on every tick" if not off else f"differs on {len(off)} ticks, first {off[0]}")
              + "; redrawn with the PC's registers, "
              + (f"all {len(gba_drawn)} equal the PC's frame" if not pc_bad
                 else f"{len(pc_bad)} differ from the PC's frame, first {pc_bad[0]}")
              + "; frames with nothing replaced "
              + ("all equal the PC's" if not equal_bad else f"{len(equal_bad)} differ from the PC's, first {equal_bad[0]}"))
        if n_a < n:
            print(f"NOTE: the affine replay ends at tick {n_a - 1}; later frames cannot pass under P2.2b")
        summary_affine = {"replay": args.affine_replay, "ticks": len(aticks),
                          "marks": {k: len(v) for k, v in marks.items()},
                          "gba_frames_equal_to_gba": len(as_gba), "pc_redraw_mismatch_ticks": pc_bad,
                          "ticks_differing_outside_frames": len(off),
                          "first_tick_differing_outside_frames": off[0] if off else None,
                          "unreplaced_frames_differing_from_pc": equal_bad}

    summary = {"ticks_compared": n - FIRST, "ticks_requested": args.ticks, "gba_ticks": len(gticks),
               "pc_ticks": len(pticks), "runs": summary_runs, "incomplete": incomplete,
               "gba_lag_ticks": glag, "pc_lag_ticks": plag, "clock": summary_clock,
               "first_tick": FIRST, "resume": resume}
    if cheat:
        summary["cheat"] = cheat  # the test cheat (README.md, "Test cheats"); absent without one
    if summary_affine is not None:
        summary["affine_replay"] = summary_affine
    summary["raster_redraw"] = summary_raster
    passes = (("raw", "RAW (every field byte for byte, no rule; reference only)"),
              ("rules", "RULES (OnMain-aligned ticks, logic clock, prevVCount left out)"),
              ("result", "RESULT (rules and oracle-limit masks)"
               + (" -- DIAGNOSTIC, PC clock replayed: not a Gate 0 result" if ctx["clock_replayed"] else "")
               + (" -- the lag replay on its own, clock compared outright" if ctx["lag_replayed"] else "")))
    result_diffs = {}
    for level, label in passes:
        first, per_field, ticks_with_diff, mask_stats, frames = run_pass(
            gticks, pticks, n, ctx, level, keep=result_diffs if level == "result" else None)
        print()
        print(f"== {label}")
        if first is None:
            print(f"match to tick {n - 1} ({n - FIRST} ticks)")
        else:
            t, d = first
            print(f"first differing tick: {t}; first differing field: {d[0][0]}")
            if level != "raw":
                print(f"fields differing at tick {t}:")
                for f, detail in d:
                    print(f"  {f}: {detail}")
            else:
                print(f"fields differing at tick {t}: " + ", ".join(f for f, _ in d))
            print(f"ticks with any difference: {ticks_with_diff} of {n - FIRST}")
            print("first differing tick per field: "
                  + ", ".join(f"{f} {per_field[f]}" for f in FIELDS if f in per_field))
            same = [f for f in FIELDS if f not in per_field and f != "frame15"
                    and not (level != "raw" and f in ("onvblank_calls", "events"))]
            print("fields equal on every tick: " + (", ".join(same) if same else "none"))
        if level == "result":
            for m in ctx["masks"]:
                if not m["apply"]:
                    print(f"mask {m['id']}: listed only, never applied ({m['title']})")
                    continue
                s = mask_stats.get(m["id"])
                if s is None:
                    print(f"mask {m['id']}: covered 0 ticks ({m['title']})")
                else:
                    if m["check"] == "cells":
                        worst = f"at most {s['worst'].get('cells', 0)} of its {len(m['cells'])} cells a tick"
                    elif m["check"] == "bitfield":
                        b = m["bitfield"]
                        worst = (f"+0x{b['offset']:02X} bits {b['shift']}-{b['shift'] + b['bits'] - 1}, GBA/PC "
                                 + ", ".join(f"{gv}/{pv}" for gv, pv in sorted(b["pairs"])))
                    else:
                        worst = "; ".join(f"{k} max delta {v}" for k, v in sorted(s["worst"].items()))
                    print(f"mask {m['id']}: covered {s['ticks']} ticks, {s['first']}-{s['last']} "
                          f"({m['title']}; {worst})")
            summary["masks"] = {mid: {"ticks": s["ticks"], "first": s["first"], "last": s["last"]}
                                for mid, s in mask_stats.items()}
            # Raster-timing frames, apart from the masked ones (README.md, "Raster-timing frames").
            raster_frames = [t for t, h in frames if h.get("kind") == "raster"]
            frames = [(t, h) for t, h in frames if h.get("kind") != "raster"]
            rnotes = sorted(ctx["raster_notes"].items())
            if raster_frames or rnotes or graster:
                print(f"{RASTER_FRAME}s (P-17, decision 9: the PC's frame drawn again with the GBA's timing is the "
                      "GBA's frame), labelled: "
                      + (f"{len(raster_frames)}: {short_ranges(raster_frames)}" if raster_frames else "none"))
                if rnotes:
                    print(f"frame differences the PC run had a raster mark for, reported (not {RASTER_FRAME}s): "
                          f"{len(rnotes)}: " + "; ".join(f"{t}: {note}" for t, note in rnotes[:6])
                          + ("; ..." if len(rnotes) > 6 else ""))
            summary["raster_timing_frames"] = raster_frames
            summary["raster_timing_frames_reported"] = [{"tick": t, "note": note} for t, note in rnotes]
            if any(m["frames"] for m in ctx["masks"]):
                # Masked frames, apart from the masked ticks (README.md, "Oracle-limit frames").
                kinds = {}
                for t, h in frames:
                    kinds.setdefault((tuple(h["masks"]), tuple(h["bgs"])), []).append(t)
                print(f"{ORACLE_FRAME}s (P2.2b), masked: "
                      + (f"{len(frames)}: " + "; ".join(
                          f"{tick_ranges(ts)} (the previous tick differs only inside {', '.join(mids)}; "
                          f"{', '.join(bgs)} shown; the affine replay draws the GBA's frame)"
                          for (mids, bgs), ts in kinds.items()) if frames else "none"))
                notes = sorted(ctx["frame_notes"].items())
                if notes:
                    print(f"frame differences after a tick that differs only inside masks, reported "
                          f"(not {ORACLE_FRAME}s): {len(notes)}: "
                          + "; ".join(f"{t}: {note}" for t, note in notes[:6])
                          + ("; ..." if len(notes) > 6 else ""))
            summary["oracle_limit_frames"] = [dict(h, tick=t) for t, h in frames]
            summary["oracle_limit_frames_reported"] = [{"tick": t, "note": note}
                                                       for t, note in sorted(ctx["frame_notes"].items())]
        summary[level] = {
            "first_tick": None if first is None else first[0],
            "first_field": None if first is None else first[1][0][0],
            "fields_at_first_tick": [] if first is None else [f for f, _ in first[1]],
            "details_at_first_tick": [] if first is None else [f"{f}: {x}" for f, x in first[1]],
            "first_tick_per_field": per_field,
            "ticks_with_diff": ticks_with_diff,
        }
        if level == "result":
            # Every differing tick of each field, as [first, last] ranges: check_all.py holds a row whose
            # differences all lie inside port/test/known_limits.json's windows for it as KNOWN, not FAIL.
            per_ticks = {}
            for t in sorted(result_diffs):
                for f, _ in result_diffs[t]:
                    per_ticks.setdefault(f, []).append(t)
            summary[level]["ticks_per_field"] = {f: range_pairs(ts) for f, ts in per_ticks.items()}

    verdict = "match" if summary["result"]["first_tick"] is None else "different"
    if ctx["lag_replayed"]:
        # The lag replay on its own (the route check of port/check-all.mk): it
        # must also have run the GBA's lag VBlanks where they came and the
        # GBA's main-loop events on every tick, or it is not the GBA's lag.
        pprefix = (phead.get("resume") or {}).get("prefix")
        seq = replay_sequence(gticks, pticks, n, pprefix)
        seq_left = sequence_left_at(seq, pprefix)
        gba_lag_ticks = [t for t in range(FIRST, n) if gticks[t].get("lag_at")]
        early = [t for t in range(FIRST, n) if gticks[t].get("early_flush")]
        print()
        print("== LAG REPLAY SEQUENCE (README.md, \"Lag replay\")")
        print(f"Lag replay: {sum(len(gticks[t]['lag_at']) for t in gba_lag_ticks)} GBA lag VBlanks in "
              f"{len(gba_lag_ticks)} ticks ({len(early)} early flush"
              + (f": {short_ranges(early, 8)}" if early else "") + "); lag positions and main-loop events equal "
              "to the GBA's "
              + ("on every tick" if not seq else f"except on {len(seq)} ticks, first {seq_left}: {seq[min(seq)]}"))
        summary["lag_replay_sequence"] = {"off_ticks": len(seq), "ticks": range_pairs(sorted(seq)),
                                          "left_at_tick": seq_left,
                                          "left_before_first_tick": seq_left is not None and seq_left < FIRST,
                                          "gba_early_flush_ticks": early}
        if seq:
            verdict = "different"
    lag_set = None
    if rticks is not None:
        labels, replay_only = lag_check(result_diffs, rticks, n, ctx, ctx_replay, sequence, sequence_left)
        n_r = min(n, len(rticks))
        print()
        print(f"== LAG CHECK (lag replay {args.lag_replay}, {len(rticks)} ticks; README.md, \"Lag replay\")")
        if n_r < n:
            print(f"NOTE: the lag replay trace ends at tick {n_r - 1}; later differences are not labelled lag-only")
        # The replay as a whole: did it run the GBA's lag VBlanks where they came?
        gba_lag_ticks = [t for t in range(FIRST, n_r) if gticks[t].get("lag_at")]
        gba_lag_vblanks = sum(len(gticks[t]["lag_at"]) for t in gba_lag_ticks)
        misplaced = [t for t in range(FIRST, n_r) if not same_lag(gticks[t], rticks[t])]
        events_off = [t for t in range(FIRST, n_r) if rticks[t]["events"] != gticks[t]["events"]]
        early = [t for t in range(FIRST, n_r) if gticks[t].get("early_flush")]
        print(f"Lag replay: {gba_lag_vblanks} GBA lag VBlanks in {len(gba_lag_ticks)} ticks ({len(early)} early flush"
              + (f": {short_ranges(early, 8)}" if early else "") + "), run at the GBA's "
              + ("position on every one of those ticks" if not misplaced
                 else f"position except on {len(misplaced)} ticks (first {misplaced[0]})")
              + "; main-loop events equal to the GBA's "
              + ("on every tick" if not events_off
                 else f"except on {len(events_off)} ticks: {short_ranges(events_off, 8)}"))
        if sequence:
            t_off = min(sequence)
            print(f"NOTE: the lag replay leaves the GBA's sequence at tick {sequence_left} ({sequence[t_off]}); "
                  f"no tick from there on is labelled lag-only"
                  + (f" (here: none of ticks {FIRST}-{n - 1})" if sequence_left < FIRST else ""))
        lag_ticks_only = lag_only_ticks(labels)
        lag_set = set(lag_ticks_only)
        lag_raster = lag_raster_ticks(labels)
        real = sorted(t for t in labels if t not in lag_set and t not in set(lag_raster))
        print(f"RESULT differs on {len(labels)} ticks: {len(lag_ticks_only)} lag-only "
              f"(the lag replay matches every field there), {len(lag_raster)} lag-only with a {RASTER_FRAME} "
              f"(the lag replay matches every field there, its frame with the GBA's timing), {len(real)} not")
        if lag_raster:
            print(f"lag-only ticks whose lag replay frame is a {RASTER_FRAME} (counted apart): "
                  f"{short_ranges(lag_raster)}")
        per_label = {}
        for t in sorted(labels):
            for f, _, lab, _ in labels[t]:
                per_label.setdefault(lab, {}).setdefault(f, t)
        for lab in ("lag-only", LAG_RASTER, "not lag-only", "clock rule", "no replay tick"):
            if lab in per_label:
                print(f"{lab}: first tick per field: "
                      + ", ".join(f"{f} {t}" for f, t in sorted(per_label[lab].items(), key=lambda x: x[1])))
        if real:
            print(f"ticks not lag-only: {short_ranges(real)}")
        if labels:
            t0 = min(labels)
            kind = "lag-only" if t0 in lag_set else LAG_RASTER if t0 in set(lag_raster) else "NOT lag-only"
            print(f"first difference left: tick {t0} ({kind})")
            for f, detail, lab, _ in labels[t0]:
                print(f"  {f} [{lab}]: {detail}")
        if real:
            t1 = real[0]
            print(f"first difference that is not lag-only: tick {t1}")
            for f, detail, lab, rdetail in labels[t1]:
                print(f"  {f} [{lab}]: {detail}" + (f"; with the lag replay: {rdetail}" if rdetail else ""))
        print("replay-only differences (the lag replay differs where the PC trace does not; they fail the gate): "
              + (f"{len(replay_only)} ticks: {short_ranges(replay_only)}" if replay_only else "none"))
        if replay_only:
            t2 = replay_only[0]
            print(f"first replay-only tick: {t2}")
            for f, detail in result_tick(t2, ctx_replay)[0]:
                print(f"  {f} [replay-only]: {detail}")
        if lag_ticks_only:
            # The remaining risk, in words (port-plan/DECISIONS.md, round 2 review).
            print(f"NOTE: on the {len(lag_ticks_only)} lag-only ticks ({100.0 * len(lag_ticks_only) / (n - FIRST):.0f}% "
                  f"of the {n - FIRST} compared) the PC's own lag-free run is checked only through the lag replay: a port fault "
                  "that the replayed lag VBlanks happened to hide there would not show")
        summary["lag_check"] = {
            "replay": args.lag_replay, "kind": "lag replay", "replay_ticks": len(rticks),
            "gba_lag_vblanks": gba_lag_vblanks, "gba_lag_ticks": len(gba_lag_ticks),
            "gba_early_flush_ticks": early, "misplaced_lag_ticks": misplaced, "event_mismatch_ticks": events_off,
            "sequence_left_at_tick": sequence_left,
            "sequence_left_before_first_tick": sequence_left is not None and sequence_left < FIRST,
            "ticks_with_diff": len(labels), "lag_only_ticks": len(lag_ticks_only),
            "lag_only_raster_ticks": len(lag_raster), "lag_only_raster_tick_list": lag_raster,
            "not_lag_only_ticks": len(real), "not_lag_only_tick_list": real,
            "first_tick": min(labels) if labels else None,
            "first_tick_lag_only": bool(labels) and min(labels) in lag_set,
            "first_not_lag_only_tick": real[0] if real else None,
            "details_at_first_not_lag_only_tick":
                [f"{f} [{lab}]: {x}" + (f"; with the lag replay: {r}" if r else "")
                 for f, x, lab, r in labels[real[0]]] if real else [],
            "first_tick_per_label": per_label,
            "replay_only_ticks": len(replay_only), "replay_only_tick_list": replay_only,
            "first_replay_only_tick": replay_only[0] if replay_only else None,
            "lag_only_share_of_compared_ticks": round(len(lag_ticks_only) / (n - FIRST), 4) if n > FIRST else 0.0,
        }
        if replay_only:
            verdict = "different"  # replay-only ticks fail the gate (round 2 review)
        elif labels and not real:
            verdict = "lag-only"
    if cticks is not None:
        # Diagnostic (README.md, "Clock replay"): what the clock replay alone
        # would match, field by field, as the lag check did before. Nothing is
        # excused by it.
        clabels, creplay_only = lag_check(result_diffs, cticks, n, ctx, ctx_clock)
        c_only = lag_only_ticks(clabels)
        print()
        print(f"== CLOCK REPLAY (diagnostic, excuses nothing; {args.clock_replay}, {len(cticks)} ticks; "
              "README.md, \"Clock replay\")")
        print(f"field by field it matches {len(c_only)} of the {len(clabels)} ticks RESULT differs on, "
              f"not {len(clabels) - len(c_only)}; replay-only {len(creplay_only)}"
              + (f" (first {creplay_only[0]})" if creplay_only else ""))
        diag = {"replay": args.clock_replay, "replay_ticks": len(cticks), "matched_ticks": len(c_only),
                "not_matched_ticks": len(clabels) - len(c_only), "replay_only_ticks": len(creplay_only)}
        if lag_set is not None:
            only_clock = sorted(set(c_only) - lag_set)
            only_lag = sorted(lag_set - set(c_only))
            print(f"matched by the clock replay but not the lag replay: "
                  + (f"{len(only_clock)}: {short_ranges(only_clock, 12)}" if only_clock else "none"))
            print(f"matched by the lag replay but not the clock replay: "
                  + (f"{len(only_lag)}: {short_ranges(only_lag, 12)}" if only_lag else "none"))
            diag.update(clock_not_lag=only_clock, lag_not_clock=len(only_lag))
        summary["clock_replay_diagnostic"] = diag
    print()
    # A lag replay given as --pc is compared on its own (a diagnostic, and the
    # route check of port/check-all.mk): its verdict is named so.
    who = "GATE 0 (LAG REPLAY vs GBA)" if ctx["lag_replayed"] else "GATE 0"
    # With a run incomplete, the verdict on the ticks compared is printed
    # first, then INCOMPLETE, which is the verdict.
    on = f"{who} on the ticks compared" if incomplete else who
    if verdict == "match":
        print(f"{on}: MATCH on ticks {FIRST}-{n - 1}")
    elif verdict == "lag-only":
        print(f"{on}: LAG-ONLY on ticks {FIRST}-{n - 1}: every RESULT difference is reproduced by the lag replay at "
              f"the GBA's positions (first at tick {summary['result']['first_tick']}); on those ticks the PC's "
              "own lag-free run is checked only through the replay")
    else:
        why = []
        if summary["result"]["first_tick"] is not None:
            why.append(f"first difference at tick {summary['result']['first_tick']}")
        if rticks is not None:
            lc = summary["lag_check"]
            if lc["first_not_lag_only_tick"] is not None:
                why.append(f"first that is not lag-only at tick {lc['first_not_lag_only_tick']}")
            if lc["replay_only_ticks"]:
                why.append(f"{lc['replay_only_ticks']} replay-only ticks, first {lc['first_replay_only_tick']} "
                           "(the lag replay differs where the PC trace does not)")
        elif summary["result"]["first_tick"] is not None and not ctx["lag_replayed"]:
            why.append("no lag check: run with --lag-replay")
        left = (summary.get("lag_replay_sequence") or {}).get("left_at_tick")
        if left is not None:
            why.append(f"the lag replay left the GBA's sequence at tick {left}"
                       + (f" (before the snapshot at tick {FIRST + 1}: the run it resumes)" if left < FIRST else ""))
        print(f"{on}: DIFFERENT on ticks {FIRST}-{n - 1}: " + ", ".join(why))
    summary["verdict_on_compared_ticks"] = verdict
    if incomplete:
        print(f"{who}: INCOMPLETE: " + "; ".join(incomplete) + f" (requested ticks {FIRST}-{args.ticks - 1}; "
              f"{verdict.upper()} on the {n:,} ticks compared)")
        verdict = "incomplete"
    summary["verdict"] = verdict
    summary["pc_kind"] = pc_kind

    if args.json:
        with open(args.json, "w") as fh:
            json.dump(summary, fh, indent=1)
    return {"match": 0, "lag-only": 3}.get(verdict, 1)


if __name__ == "__main__":
    sys.exit(main())
