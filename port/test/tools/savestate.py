#!/usr/bin/env python3
"""Save states for the Gate 0 harness: the PC side's relink, and checks.

    python3 port/test/tools/savestate.py relink --part SNAP/tick<T>/<part> --exe build/pc-gate0/fe8pc \
        [--gba-elf fireemblem8.elf]
    python3 port/test/tools/savestate.py check SNAP/tick<T>
    python3 port/test/tools/savestate.py classify --exe build/pc-gate0/fe8pc
    python3 port/test/tools/savestate.py audit --a SNAP_A/tick<T>/<part> --b SNAP_B/tick<T>/<part> \
        [--gba-elf fireemblem8.elf]

port/test/README.md, "Save states"; port-plan/pending/savestate.md.

A PC snapshot (port/test/pc/savestate.c, GATE0_STATE_OUT) holds, at the Gate 0
sample point of tick T: the GBA memory the PC keeps (regions.bin), the whole
host .data and .bss of the binary that made it (hostdata.bin), a copy of that
binary and its link map, tick T-1's trace line and raw record, and meta.json.

`relink` turns it into patch.bin for one binary (the one that will resume; the
same binary or a rebuilt one), which savestate.c applies at the resumed run's
first sample point (GATE0_STATE_IN). It decides what is restored:
  - all of the GBA memory (regions.bin);
  - host globals by name, by the class of the object that defines them (the
    link map): port objects by PORT_TABLE below (every symbol of a port
    object must be listed, or the tool stops), game objects (src/*.o) outside
    GBA RAM by rule (.bss restored; .data restored only if the run changed it),
    everything else (crt, libc) left to the new process;
and rewrites the host addresses in them for the new binary. On the PC, C
function pointers and pointers to host-built data are host addresses, which
move on every rebuild. Each word of EWRAM and IWRAM that holds an address
inside the saved binary's image is decided by provenance first (relink()):
  - known pointer fields (KNOWN_FIELDS: gMainCallback, the IRQ table, the
    sound driver's jump table, the five pointer fields of every proc of the
    proc pool) are rewritten, and must resolve, or the relink stops;
  - a word its variable's declared type (the saved binary's DWARF) makes a
    pointer is rewritten the same way, except where an overlaid variable
    (EWRAM_OVERLAY) declares it no pointer and the GBA holds the same 32-bit
    value: that is the other variable's data (an OAM word of gBanimOaml under
    gStatusScreenSt.font), left alone and listed;
  - a word declared as integers narrower than 32 bits (tilemaps, palettes,
    OAM, coordinates, bitfields) is never a pointer and is left alone;
  - any other word (4-byte integers, byte buffers, untyped areas, the user
    area of each proc, whose type is the proc's) is rewritten only under a
    strict check: the first byte of a function whose address the binary takes
    somewhere, or a place in a game data object that its declared type allows
    (its start, an element's or a member's; from a byte buffer only a start
    or an element of a table of 4-byte or larger elements). Other words are
    left alone and listed. The strict rule is checked against the GBA (the
    snapshot's gba part and the GBA ELF, relink()): a word the GBA holds as
    the same 32-bit value is data; where the GBA is on the same path, a word
    with the GBA's low 16 bits, or one the GBA holds as no address, is data.
Resolution: a function by its name; a data object by name + offset (same size
required); an anonymous literal (a proc name) through a named object of the
saved binary that holds it ("the PROC_NAME entry of script X"): the new
binary's word there. A name the new binary does not have stops the relink
with that name. Restored host globals are scanned with the strict check;
lists of code addresses kept for logging are relinked as function + offset.
Every rewrite and every word left alone is written to relink.log.

The new binary is checked as the saved one is: its port globals must all be
in PORT_TABLE, and each global it would restore must be in the photo (a
state global, or game .data, added after the snapshot stops the relink: the
photo cannot say what the new binary's run would have written there;
SAVESTATE_NEW_GAME_DATA=initial resumes new game .data with its initial
values). Each restored global must have the same size and, for another
binary, the same declared layout (DWARF: member names, offsets, bit fields,
element counts, base types), or the relink stops: a same-size change such as
two fields swapped would put the photo's bytes in the wrong places. A
function-static is matched by the function it is declared in (DWARF), not by
GCC's ".N" number (Matcher).

The patch is made once per snapshot content, binary and tool: relink-<crc>/
is reused only while the part's files, this tool's text (PORT_TABLE,
REFUSE_IF_SET, KNOWN_FIELDS, the rules), the GBA ELF and
SAVESTATE_NEW_GAME_DATA are the ones it was made from (SHA-1s in
summary.json), and savestate.c refuses a patch whose part's meta.json,
regions.bin or hostdata.bin has changed since.

A resumed run of the same binary is exact (every rewrite is the identity).
Another binary is best effort (README.md, "Save states"): not carried by any
relink are halves of host addresses (procs' user areas are typed as each
proc's own struct, which the relink cannot see; where the game keeps half of
an address in a 16-bit field, the GBA keeps the same half of its own address
there and the PC half of a host address), single leftover bytes of old host
addresses, and a 16-bit field beside the high half of an old address that
the strict scan reads as a whole address where the GBA check cannot see it
(MenuItemProc.yTile in the plain run).

`audit` checks the relink against a second snapshot of the same run and tick
made by another binary: relinking A for B's binary must give B's memory, apart
from listed things no relink can carry (halves of host addresses, with the
GBA's own half as evidence for a low half; the last DMA addresses; log-once
code addresses).
"""
import argparse
import bisect
import hashlib
import json
import os
import pickle
import re
import struct
import subprocess
import sys
import zlib
from collections import defaultdict

HOST_IMAGE_MIN = 0x10000000
GBA_RAM = (("EWRAM", 0x02000000, 0x40000), ("IWRAM", 0x03000000, 0x8000))
REGIONS = (("EWRAM", 0x02000000, 0x40000), ("IWRAM", 0x03000000, 0x8000), ("IO", 0x04000000, 0x1000),
           ("PLTT", 0x05000000, 0x1000), ("VRAM", 0x06000000, 0x18000), ("OAM", 0x07000000, 0x1000),
           ("SRAM", 0x0E000000, 0x10000))
PATCH_MAGIC = b"FE8SNPT3"
TOOL_VERSION = 9  # a type cache made by another version of this tool is made again (patches: also tool_sha1)
PC_FORMAT = "fe8-pc-snapshot-1"
GBA_FORMAT = "fe8-gba-snapshot-1"
PARTS = ("gba", "pc", "pclag", "pcaff")

SESSION, STATE, RAW, CODE = "session", "state", "raw", "code"

# Every .data/.bss symbol of the port objects, by object and base name (a
# function-static without its ".N"). state: restored, pointer words relinked;
# raw: restored as bytes (pixel and sample buffers); code: restored, each word
# that is an address inside a function relinked as that function + offset
# (lists of host code addresses kept only to log each site once); session: the
# new process keeps its own value (settings read from the environment at
# start-up, FILE handles, heap pointers, the soft-reset jmp_buf, caches derived
# from settings, the harness's inputs).
PORT_TABLE = {
    "port/asm/arm_pc.o": {"sLogged": STATE},
    # Host audio device queue (FE8PC_AUDIO, the window build): nothing in a headless run.
    "port/audio/audio_out.o": {n: SESSION for n in (
        "sRatioMax", "sRatioMin", "sQMin", "sRatio", "sPushes", "sQMax", "sRatioSets", "sUnderruns",
        "sDropped", "sQueued", "sQs", "sDropping", "sPlaying", "sDevice")},
    "port/audio/fifo_pc.o": {
        "sClock": STATE, "sHeader": STATE, "sWarnedSrc": STATE, "sWarnedRate": STATE, "buf": RAW,
        "sSongLog": SESSION, "sRecordFrom": SESSION, "sRecord": SESSION, "sDumpFifo": SESSION,
        "sDumpLr": SESSION, "sPlayed": STATE, "sPreVBlanks": STATE, "sVBlanks": STATE, "sLr": RAW},
    "port/audio/m4a_1_pc.o": {"gPortM4aHostImageHi": SESSION, "gPortM4aHostImageLo": SESSION},
    "port/audio/psg.o": {"sPsg": STATE},
    "port/bios/bios.o": {"sLoggedSites": CODE, "sLoggedCount": STATE},
    "port/hw/dma.o": {"sDeferredLogged": STATE},
    "port/hw/frame.o": {
        "sKeyLog": SESSION, "sPressHold": SESSION, "gPortFrameLimit": SESSION, "gPortIrqDepth": STATE,
        "gPortFrameCount": STATE, "gPortIfFrameStart": SESSION, "gPortIfOnMainStart": SESSION,
        "gPortOnMainIrqIoPre": SESSION, "gPortOnMainIrqIoMask": SESSION, "sIoBefore": SESSION, "sHits": STATE, "sLoggedHeld": STATE, "sAutoHeld": STATE,
        "sPressParsed": SESSION, "sPressStart": SESSION, "sPressPeriod": SESSION, "sPressMask": SESSION,
        # FE8PC_KEYSCRIPT (any build): the environment's one-off presses, read at
        # the first frame, as sPress* is; a resumed run keeps its own.
        "sScriptFrame": SESSION, "sScriptMask": SESSION, "sScriptCount": SESSION},
    "port/hw/main_pc.o": {
        "sDropBiosWrites": SESSION, "gPortOnMainCalls": STATE, "sDroppedCount": STATE, "sDroppedSites": CODE,
        "sResetJmp": SESSION, "sStubsHit": STATE},
    # sMapped: the regions this process reserved or its loader mapped; buf:
    # PortRomFileKind's read buffer (the Windows ROM search). Both since
    # 79e83239 (Windows polish); unclassified, they made every relink onto a
    # binary with them refuse.
    "port/hw/memory.o": {"sRomSize": SESSION, "sMapped": SESSION, "buf": SESSION},
    # FE8PC_NULLPROBE (refused when on, see REFUSE_IF_SET).
    "port/hw/nullprobe.o": {n: SESSION for n in (
        "sAltStack", "sPend", "sKeys", "sRoute", "sOutPath", "sShadow", "sOn", "sHits", "sSitesLost",
        "sSiteCount", "sSites")},
    "port/hw/pacer.o": {n: SESSION for n in (
        "sRestarts", "sFrames", "sFrac", "sLimitNs", "sDeadlineNs", "sStartNs", "sPaceOn")},
    "port/hw/platform_headless.o": {"sDir": SESSION},
    # Reforged Options fast-forward (port/reforged/reforged_fastforward.c): played window builds only, empty
    # in every test build. Host timing, the key's state and the test hooks' settings: the new process's own.
    "port/reforged/reforged_fastforward.o": {n: SESSION for n in (
        "sSpeed", "sShown", "sBaseReal", "sBaseVirt", "sRatioBase", "sGain", "sPressFrame", "sLogged",
        "sPressReal", "sHoldCount", "sHoldFrom", "sHoldTo", "sCheck", "sCheckOn", "sSndHash", "sMul",
        "sGlideAt", "sShortCount", "sShortBytes", "sShortLogged", "sCheckReal", "sShortBig",
        "sShortMax", "sGlideNs", "sCheckQueued")},
    "port/render/bg_affine.o": {"sAffineRawY": STATE, "sAffineRawX": STATE, "sAffineRefY": STATE,
                                "sAffineRefX": STATE},
    "port/render/bg_line.o": {"sLayerMask": SESSION, "sLogHits": STATE, "sFrameDispcnt": STATE,
                              "gRenderMgbaBgClip": SESSION},
    "port/render/blend.o": {"gRenderSemiIgnoresWinFx": SESSION, "gRenderBlendMode": SESSION},
    "port/render/obj_line.o": {"gRenderObjCycleLimit": SESSION, "gRenderFxTripHook": SESSION,
                               "sPeakCycles": STATE, "sTripCount": STATE,
                               # Reforged Options' HP bars pass (played builds only, PORT_RFO_MAPOVERLAY):
                               # one line's scratch
                               "sPixelEntry": SESSION, "sOvlMatch": SESSION, "sOvlMatchCount": SESSION},
    "port/render/out_rgba.o": {"sLutReady": SESSION, "sRgbaLut": SESSION},
    "port/render/render.o": {
        "sDumpDir": SESSION, "gRenderEnabled": SESSION, "gRenderFramesDone": STATE, "gRenderFrameHash": RAW,
        "gRenderFrameRgba": RAW, "gRenderFrame555": RAW, "sDumpLast": SESSION, "sDumpFirst": SESSION,
        "sDumpOn": SESSION, "sBgLine": RAW, "sMem": SESSION},
    # The player's save file (--save; refused when on, see REFUSE_IF_SET). All
    # of it is the new process's own: paths (also ChooseDefault's "other"
    # place), the file's stamp and image, the lock (sLockFd is an open
    # descriptor of this process and must never come from a photo; GCC drops
    # it from the Gate 0 build, where it is only written), the log-once flags.
    "port/save/savefile_pc.o": {n: SESSION for n in (
        "sFailRun", "sFailures", "sWrites", "sRetryFrame", "sFileImage", "sTitleFailed", "sFailNote",
        "sTitleNote", "sTmpPath", "sWritePath", "sPath", "sOn", "sLockPath", "sCheckBuf", "sStamp", "sLock",
        "sLockErr", "sNoSaving", "sTouchedLogged", "sGoneLogged", "sLockFd", "sLoggedErr", "other")},
    # Reforged Options, casual mode and saves marked (port-rfo-casual; played builds only: the objects
    # are empty in every test build, so a test binary lists these as entries with no symbol). The
    # fallen list and the per-block marks are game state, like SRAM; the mark file's last text, the
    # scratch text and the test switch's once-flag are this process's own.
    "port/reforged/reforged_casual.o": {"sFallen": STATE, "sFallenCount": STATE, "sTestFallDone": SESSION,
                                        "buf": SESSION},
    "port/reforged/reforged_savemark.o": {"sBlocks": STATE, "sLastText": SESSION, "sScratch": SESSION},
    "port/test/pc/gate0_hook.o": {
        # inputs and outputs of this run
        "sEventsOn": SESSION, "sKeysPath": SESSION, "sTicks": SESSION, "sRaws": SESSION, "table": SESSION,
        "sLagPosCount": SESSION, "sLagTicks": SESSION, "sGbaEarly": SESSION, "sGbaEvents": SESSION,
        "sLagFirst": SESSION, "sLagPos": SESSION, "sLagPath": SESSION, "sExeCrc": SESSION, "sFnRps": SESSION,
        "sFnRpr": SESSION, "sAffineRecords": SESSION, "sAffineRecord": SESSION, "sAffineBase": SESSION,
        "sAffinePath": SESSION, "sAffineFrom": SESSION, "sGbaClockCount": SESSION, "sGbaClock": SESSION,
        "sSramHeader": SESSION, "sShotRangeCount": SESSION, "sShotRanges": SESSION, "sShotDir": SESSION,
        "sMaskCount": SESSION, "sMasks": SESSION, "sRaw": SESSION, "sOut": SESSION, "sState": SESSION,
        "sSndIndex": SESSION,
        # the test cheats (GATE0_CHEAT, README.md "Test cheats"): the setting, and the run's own totals for
        # its end line (session, so a photo from before them still resumes; a resumed run counts from there);
        # the convoy cheat's item list (from the setting) and cursor (a resumed run takes it from the photo's
        # prologue line, gate0_hook.c CheatResume)
        "sCheat": SESSION, "sCheatWords": SESSION, "sCheatTicks": SESSION, "sCheatName": SESSION,
        "sCheatOn": SESSION, "sCheatFundsTicks": SESSION, "sCheatPlaced": SESSION, "sCheatItems": SESSION,
        "sCheatItemCount": SESSION, "sCheatConvoy": SESSION,
        # sound positions: the GBA's (input) and the count of m4a calls since the last event, which is
        # 0 at the sample point but for calls after a tick's last event (gate0_hook.c, "Sound positions")
        "sLagSnd": SESSION, "sLagSndOn": SESSION, "sSndSince": SESSION, "sSndDepth": SESSION,
        # and inside an m4a call: the GBA's split records (input), the open call's TrackStop count and the
        # split place of each run this tick (0/-1 at the sample point but for a split run in the snapshot tick)
        "sLagSndStops": SESSION, "sLagSndLocks": SESSION, "sSndMainOpen": SESSION, "sSndStops": SESSION,
        "sRunSnd": SESSION, "sRunStops": SESSION, "sRunLocks": SESSION, "sLagUsedStops": SESSION,
        "sLagUsedLocks": SESSION, "sFnTrackStop": SESSION,
        # and nested in an IRQ handler's m4a call: the GBA's locks (input), each run's this tick (-1 at the sample
        # point but for such a run in the snapshot tick)
        "sLagSndNested": SESSION, "sLagUsedNested": SESSION,
        # scratch of one frame step (idle at the sample point)
        "sAffineFrame": SESSION, "sAffineGba": SESSION, "sAffineRegs": SESSION, "sAffinePcHash": SESSION,
        "sAffineStep": SESSION, "sAffineMark": SESSION,
        # the raster-timing redraw (its input and one frame step's scratch) and the GBA's lag_split and
        # lag_head records
        "sRaster": SESSION, "sLagSplit": SESSION, "sLagHead": SESSION,
        # the trace's counters and the replays' progress
        "sEventMismatchTicks": STATE, "sEventMismatch": STATE, "sEarlyRuns": STATE, "sLagExact": STATE,
        "sLagRuns": STATE, "sEarlyRan": STATE, "sLagUsedCount": STATE, "sLagUsed": STATE, "sLagDone": STATE,
        "sLagOffTick": STATE,
        "sInVBlank": STATE, "sEvents": STATE, "sAffineCounts": STATE, "sAffinePcCur": STATE,
        "sAffinePcPrev": STATE, "sOnVBlankCalls": STATE, "sVBlankWaits": STATE, "sLastTickFrame": STATE,
        "sTick": STATE},
    "port/test/pc/proc_call_arg_check.o": {"sReportedCount": STATE, "sReported": STATE},
    "port/test/pc/savestate.o": {n: SESSION for n in (
        "sMode", "sKind", "sOutDir", "sAt", "sAtCount", "sRomImageCrcBoot", "sExeCrc", "sRomCrc", "sKeysCrc",
        "sDataAddr", "sDataSize", "sBssAddr", "sBssSize", "sSramHeader", "sPatch", "sPatchSize",
        "sResumeTick", "sPrologue", "sPrologueRaw", "sPrologueLen", "sPrologueRawLen", "sRecords",
        "sRecordCount", "sResumed", "sResumeDir", "table")},
    # The UBSan sweep binary's runtime (port/test/sweep.mk): each run keeps its own site table.
    "port/test/pc/ubsan_log.o": {n: SESSION for n in (
        "sSites", "sSiteCount", "sDroppedHits", "sHits", "sOutPath", "sNextDumpTick", "sStarted",
        "sDumping", "sFuncs", "sFuncCount", "sStrtab", "sFuncsTried", "sTextLo", "sTextHi", "order")},
    # --- Reforged Options, the Cheats (port-rfo-cheats): played builds only (empty objects in
    # every test build). on: FE8PC_RFO_CHECK, read once; the self-check's progress; the Cheats
    # screen's text slots (as the options screen's). ---
    "port/reforged/reforged_cheats.o": {"on": SESSION, "checked": STATE, "sCheckFails": STATE,
                                        "sInSelfCheck": STATE},
    "port/reforged/reforged_cheats_menu.o": {n: STATE for n in (
        "sCheatTitleText", "sCheatHelpText", "sCheatRowText", "sCheatValueText")},
    # --- end of the Cheats block ---
    # Reforged Options, growth rates on Select (port-rfo-growth): played builds only (an empty object in
    # every test build). The stat screen's view state (display only), and FE8PC_RFO_CHECK read once.
    "port/reforged/reforged_growths.o": {"sShown": STATE, "sHpShown": STATE, "sLabelsColored": STATE,
                                         "sUndo": STATE, "sDefer": STATE, "on": SESSION},
    # Reforged Options, HP bars & warnings (port-rfo-hpbars): played builds only (an empty object in
    # every test build). The shown and pending pictures are made again from the game state every frame
    # (display only, never game state); sMark, sMarkFor: the "!" marks against the selected unit, made
    # again at each selection (port-rfo-int6; a new process starts with none and makes them at once);
    # on: FE8PC_RFO_CHECK, read once; sLast, last, lastShown, lastFound, lastAge: that check's log state.
    "port/reforged/reforged_hpbars.o": {"gReforgedOverlay": SESSION, "sPending": SESSION, "sLast": SESSION, "last": SESSION, "lastShown": SESSION, "lastFound": SESSION, "lastAge": SESSION,
                                        "sMark": SESSION, "sMarkFor": SESSION, "on": SESSION},
}
# A game object's own PC_PORT log state (a "logged once" flag beside a guard),
# not game state: the new process keeps its own, as PORT_TABLE's session
# entries do. Unlisted, such a global is game .bss, restored, and a binary
# that adds one refuses every older photo ("the new binary's game-bss global
# ... is not in the snapshot's binary"): integration 11, round 10's
# PortFontGbaPointer flag (tableend, D-62, was working ID D-R10-TABLEEND-2) refused all 15 ready10
# stop photos. Object -> base names.
GAME_SESSION = {"src/fontgrp.o": ("sLogged",)}

# A run with one of these on cannot be snapshotted (object, base name).
REFUSE_IF_SET = {("port/hw/nullprobe.o", "sOn"): "FE8PC_NULLPROBE is on",
                 ("port/save/savefile_pc.o", "sOn"): "a player save file is in use (--save)"}

# Known pointer fields in GBA RAM (provenance): (symbol key, word offsets).
PROC_SIZE, PROC_COUNT = 0x6C, 64
KNOWN_FIELDS = (
    ("gMainCallback", [0]),
    ("gIRQHandlers", [4 * i for i in range(14)]),
    ("gMPlayJumpTable", [4 * i for i in range(36)]),
    ("proc.c:sProcArray", [p * PROC_SIZE + f for p in range(PROC_COUNT) for f in (0x00, 0x04, 0x08, 0x0C, 0x10)]),
)
PROC_FIELDS = {0x00: "proc_script", 0x04: "proc_scrCur", 0x08: "proc_endCb", 0x0C: "proc_idleCb", 0x10: "proc_name"}

STATIC_SUFFIX = re.compile(r"^(.*)\.\d+$")
# Word classes from the declared types (Types), weakest claim first: where
# variables overlap (EWRAM_OVERLAY) or a union has several members, the class
# that allows the most wins.
CLASS_RANK = {"half": 0, "bytes": 1, "int": 2, "ptr": 3}


def fail(msg, code=2):
    print(f"savestate: {msg}")
    sys.exit(code)


def base_name(name):
    m = STATIC_SUFFIX.match(name)
    return m.group(1) if m else name


def cstr(data, off):
    end = data.index(b"\0", off)
    return data[off:end].decode("utf-8", "replace")


class Sym:
    __slots__ = ("name", "value", "size", "type", "bind", "section", "file", "key")

    def __init__(self, **kw):
        for k, v in kw.items():
            setattr(self, k, v)


class Elf:
    """A 32-bit ELF executable: sections, PT_LOAD segments, symbols (locals keyed
    by their source file, from the STT_FILE entries), and its file image."""

    def __init__(self, path):
        self.path = path
        with open(path, "rb") as fh:
            d = self.data = fh.read()
        if d[:4] != b"\x7fELF" or d[4] != 1:
            fail(f"{path}: not a 32-bit ELF file")
        self.crc = zlib.crc32(d) & 0xFFFFFFFF
        (_t, _m, _v, _e, phoff, shoff, _f, _eh, phentsize, phnum, shentsize, shnum,
         shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", d, 16)
        self.sections = []
        for i in range(shnum):
            name, typ, flags, addr, off, size, link, _info, _al, _es = struct.unpack_from(
                "<IIIIIIIIII", d, shoff + i * shentsize)
            self.sections.append({"name_off": name, "type": typ, "flags": flags, "addr": addr, "offset": off,
                                  "size": size, "link": link})
        shstr = self.sections[shstrndx]["offset"]
        for s in self.sections:
            s["name"] = cstr(d, shstr + s["name_off"])
        self.by_name = {s["name"]: s for s in self.sections}
        segs = []
        for i in range(phnum):
            p_type, _o, vaddr, _pa, _fs, memsz, _fl, _al = struct.unpack_from("<IIIIIIII", d, phoff + i * phentsize)
            if p_type == 1 and vaddr >= HOST_IMAGE_MIN:
                segs.append((vaddr, vaddr + memsz))
        if not segs:
            fail(f"{path}: no loaded segment at or above 0x{HOST_IMAGE_MIN:08X}")
        self.lo, self.hi = min(a for a, _ in segs), max(b for _, b in segs)
        self.symbols = []
        symtab = next((s for s in self.sections if s["type"] == 2), None)
        if symtab is None:
            fail(f"{path}: no symbol table")
        strtab = self.sections[symtab["link"]]["offset"]
        cur_file = None
        for i in range(symtab["size"] // 16):
            st_name, value, size, info, _other, shndx = struct.unpack_from("<IIIBBH", d, symtab["offset"] + 16 * i)
            typ, bind = info & 15, info >> 4
            name = cstr(d, strtab + st_name)
            if typ == 4:  # STT_FILE
                cur_file = name
                continue
            if not name or typ not in (0, 1, 2) or shndx == 0:
                continue
            section = self.sections[shndx]["name"] if shndx < len(self.sections) else "*ABS*"
            local = bind == 0
            key = f"{cur_file}:{name}" if local else name
            self.symbols.append(Sym(name=name, value=value, size=size, type=typ, bind=bind, section=section,
                                    file=cur_file if local else None, key=key))
        self.by_key = defaultdict(list)
        self.by_base = defaultdict(list)
        for s in self.symbols:
            self.by_key[s.key].append(s)
            if s.file is not None:
                self.by_base[f"{s.file}:{base_name(s.name)}"].append(s)

    def section_at(self, addr):
        for s in self.sections:
            if s["flags"] & 2 and s["addr"] <= addr < s["addr"] + s["size"]:  # SHF_ALLOC
                return s
        return None

    def file_bytes(self, addr, n):
        """The bytes at addr in the file image (zeros in .bss)."""
        s = self.section_at(addr)
        if s is None or addr + n > s["addr"] + s["size"]:
            return None
        if s["type"] == 8:  # SHT_NOBITS
            return bytes(n)
        off = s["offset"] + addr - s["addr"]
        return self.data[off:off + n]

    def lookup(self, key, want_func=None):
        """The symbol with this key; a function-static also by its base name
        (GCC's ".N" numbers may change). None if absent or ambiguous."""
        cands = self.by_key.get(key, [])
        if want_func is not None:
            cands = [s for s in cands if (s.type == 2) == want_func]
        if len(cands) == 1:
            return cands[0]
        if not cands and ":" in key:
            file, name = key.split(":", 1)
            alt = self.by_base.get(f"{file}:{base_name(name)}", [])
            if want_func is not None:
                alt = [s for s in alt if (s.type == 2) == want_func]
            if len(alt) == 1:
                return alt[0]
        return None


class Types:
    """Declared types of the GBA RAM variables, from the binary's DWARF
    (readelf --debug-dump=info): word_class(addr) says what the variable that
    holds addr declares there: "ptr" (a pointer or function pointer), "half"
    (integers narrower than 32 bits only: arrays of 16-bit integers such as
    tilemaps, palettes and OAM, byte and halfword fields, bitfields), "int"
    (anything else: 4-byte integers, enums, byte arrays, mixed or padding; it
    may hold a pointer, as decomp fields typed int and byte buffers used for
    other structs often do), or None (no variable: an untyped area).
    pointable() checks a pointer target against the target object's declared
    type. host_fn maps the address of each host variable declared inside a
    function (a function-static) to that function's name, which matches
    function-statics between two binaries (GCC's ".N" numbers follow the
    order of the source, so moving a function swaps them). The parse is cached
    next to the binary (NAME.types.pickle)."""

    KEEP = {"DW_TAG_variable", "DW_TAG_member", "DW_TAG_base_type", "DW_TAG_pointer_type", "DW_TAG_typedef",
            "DW_TAG_const_type", "DW_TAG_volatile_type", "DW_TAG_array_type", "DW_TAG_subrange_type",
            "DW_TAG_structure_type", "DW_TAG_union_type", "DW_TAG_enumeration_type", "DW_TAG_subroutine_type",
            "DW_TAG_restrict_type", "DW_TAG_atomic_type"}

    def __init__(self, exe):
        cache = exe.path + ".types.pickle"
        try:
            with open(cache, "rb") as fh:
                version, crc, self.dies, self.vars, self.host_fn = pickle.load(fh)
            if crc == exe.crc and version == TOOL_VERSION:
                self._index()
                return
        except (OSError, ValueError, TypeError, EOFError, pickle.UnpicklingError):
            pass
        self.dies, self.vars, self.host_fn = {}, [], {}
        self._parse(exe.path)
        try:
            with open(cache + ".tmp", "wb") as fh:
                pickle.dump((TOOL_VERSION, exe.crc, self.dies, self.vars, self.host_fn), fh, protocol=4)
            os.replace(cache + ".tmp", cache)
        except OSError:
            pass
        self._index()

    def _parse(self, path):
        head = re.compile(r"^\s*<(\d+)><([0-9a-f]+)>: Abbrev Number: \d+ \((DW_TAG_\w+)\)")
        attr = re.compile(r"^\s*<[0-9a-f]+>\s+(DW_AT_\w+)\s*:\s*(.*)$")
        proc = subprocess.Popen(["readelf", "--debug-dump=info", path], stdout=subprocess.PIPE, text=True,
                                errors="replace", bufsize=1 << 20)
        stack = {}
        subs = {}  # DW_TAG_subprogram DIEs: their name, or the DIE that names them
        cur = None
        for line in proc.stdout:
            if "Abbrev Number: 0" in line:
                cur = None
                continue
            m = head.match(line)
            if m:
                depth, off, tag = int(m.group(1)), int(m.group(2), 16), m.group(3)
                parent = stack.get(depth - 1)
                stack[depth] = off
                if tag in self.KEEP:
                    cur = {"tag": tag, "kids": []}
                    self.dies[off] = cur
                    if parent is not None and parent in self.dies and tag in ("DW_TAG_member", "DW_TAG_subrange_type"):
                        self.dies[parent]["kids"].append(off)
                    if tag == "DW_TAG_variable":
                        # the innermost enclosing function (through lexical blocks), if any
                        cur["scope"] = next((stack[k] for k in range(depth - 1, -1, -1) if stack.get(k) in subs),
                                            None)
                elif tag == "DW_TAG_subprogram":
                    cur = subs[off] = {"tag": tag}
                else:
                    cur = None
                continue
            if cur is None:
                continue
            m = attr.match(line)
            if not m:
                continue
            k, v = m.group(1), m.group(2).strip()
            if k in ("DW_AT_type", "DW_AT_specification", "DW_AT_abstract_origin"):
                r = re.search(r"<0x([0-9a-f]+)>", v)
                if r:
                    cur[k[6:]] = int(r.group(1), 16)
            elif k in ("DW_AT_byte_size", "DW_AT_data_member_location", "DW_AT_upper_bound", "DW_AT_count",
                       "DW_AT_bit_size", "DW_AT_data_bit_offset"):
                r = re.match(r"(0x[0-9a-f]+|-?\d+)", v)
                if r:
                    cur[k[6:]] = int(r.group(1), 0)
            elif k == "DW_AT_location":
                r = re.search(r"DW_OP_addr: ([0-9a-f]+)\)\s*$", v)
                if r:
                    cur["addr"] = int(r.group(1), 16)
            elif k == "DW_AT_name":
                cur["name"] = v.split(": ")[-1]
        proc.wait()
        self.host = {}
        for off, d in self.dies.items():
            if d["tag"] != "DW_TAG_variable" or "addr" not in d:
                continue
            t, ref, hops = d.get("type"), d, 0
            while t is None and hops < 4:  # a definition's declaration, an inlined copy's origin
                ref = self.dies.get(ref.get("specification", ref.get("abstract_origin")))
                if ref is None:
                    break
                t, hops = ref.get("type"), hops + 1
            if t is None:
                continue
            if any(a <= d["addr"] < a + n for _, a, n in GBA_RAM):
                size = self.size(t)
                if size:
                    self.vars.append((d["addr"], size, t,
                                      d.get("name") or self.dies.get(d.get("specification"), {}).get("name")))
            elif d["addr"] >= HOST_IMAGE_MIN:
                self.host[d["addr"]] = t
                if d.get("scope") is not None:
                    fn = self._sub_name(subs, d["scope"])
                    if fn:
                        self.host_fn[d["addr"]] = fn
        self.vars.append(("host", self.host))  # kept with the cache; split off in _index

    def _sub_name(self, subs, off):
        """A function DIE's name, through its declaration or abstract origin."""
        for _ in range(4):
            s = subs.get(off)
            if s is None:
                return None
            if s.get("name"):
                return s["name"]
            off = s.get("specification", s.get("abstract_origin"))
        return None

    def _index(self):
        self.host = next((v[1] for v in self.vars if v[0] == "host"), {})
        self.vars = sorted(v for v in self.vars if v[0] != "host")
        self.starts = [a for a, _, _, _ in self.vars]
        self.max_size = max((n for _, n, _, _ in self.vars), default=0)

    def pointable(self, addr, off, min_elem=1):
        """Whether a pointer to host data object addr + off is plausible from its
        declared type: its start, the start of an element of an array (any byte
        of a byte array: strings, scripts, compressed text), or a member of a
        struct. min_elem: only arrays whose elements have at least that many
        bytes count (a table of structs or pointers), and no struct members.
        Unknown type: yes."""
        t = self.strip(self.host.get(addr))
        d = self.dies.get(t)
        if d is None or off == 0:
            return True
        if d["tag"] == "DW_TAG_array_type":
            es = self.size(d.get("type"))
            return es >= min_elem and (es <= 1 or off % es == 0)
        if d["tag"] == "DW_TAG_structure_type" and min_elem <= 1:
            return any(self.dies[k].get("data_member_location") == off for k in d["kids"]
                       if self.dies[k]["tag"] == "DW_TAG_member")
        return False

    def strip(self, t):
        while t is not None and self.dies.get(t, {}).get("tag") in (
                "DW_TAG_typedef", "DW_TAG_const_type", "DW_TAG_volatile_type", "DW_TAG_restrict_type",
                "DW_TAG_atomic_type"):
            t = self.dies[t].get("type")
        return t

    def size(self, t, depth=0):
        t = self.strip(t)
        d = self.dies.get(t)
        if d is None or depth > 32:
            return 0
        if "byte_size" in d:
            return d["byte_size"]
        if d["tag"] == "DW_TAG_pointer_type":
            return 4
        if d["tag"] == "DW_TAG_array_type":
            n = 1
            for k in d["kids"]:
                s = self.dies[k]
                n *= s["count"] if "count" in s else s.get("upper_bound", -1) + 1
            return n * self.size(d.get("type"), depth + 1)
        return 0

    def layout(self, t, depth=0):
        """The memory layout a type declares, as text: through typedefs and
        qualifiers, base types by name and size, enums by size, arrays by
        element counts, structs and unions by size and each member's name,
        offset, bit position and layout; a pointer is "*" (what it points to
        is not this variable's memory). Two binaries' restored globals must
        have the same (relink)."""
        if not hasattr(self, "_layouts"):
            self._layouts = {}
        t = self.strip(t)
        if t in self._layouts:
            return self._layouts[t]
        d = self.dies.get(t)
        if d is None:
            return "?"
        if depth > 32:
            return "..."
        tag = d["tag"]
        if tag == "DW_TAG_pointer_type":
            out = "*"
        elif tag == "DW_TAG_base_type":
            out = f"{d.get('name', '?')}:{d.get('byte_size', '?')}"
        elif tag == "DW_TAG_enumeration_type":
            out = f"enum:{d.get('byte_size', '?')}"
        elif tag == "DW_TAG_array_type":
            dims = []
            for k in d["kids"]:
                s = self.dies[k]
                dims.append(str(s["count"] if "count" in s else s.get("upper_bound", -1) + 1))
            out = f"[{','.join(dims)}]{self.layout(d.get('type'), depth + 1)}"
        elif tag in ("DW_TAG_structure_type", "DW_TAG_union_type"):
            parts = []
            for k in d["kids"]:
                m = self.dies[k]
                if m["tag"] != "DW_TAG_member":
                    continue
                pos = str(m.get("data_member_location", ""))
                if "bit_size" in m:
                    pos += f".{m.get('data_bit_offset', '')}/{m['bit_size']}"
                parts.append(f"{m.get('name', '')}@{pos}={self.layout(m.get('type'), depth + 1)}")
            out = f"{'struct' if tag == 'DW_TAG_structure_type' else 'union'}:{d.get('byte_size', '?')}" \
                  f"{{{';'.join(parts)}}}"
        else:
            out = tag[7:]
        self._layouts[t] = out
        return out

    def layout_at(self, addr):
        """The layout of the host variable at addr (layout()), or None if the
        binary's DWARF declares none there."""
        t = self.host.get(addr)
        return None if t is None else self.layout(t)

    def small_scalar(self, t):
        d = self.dies.get(self.strip(t))
        return d is not None and d["tag"] in ("DW_TAG_base_type", "DW_TAG_enumeration_type") \
            and d.get("byte_size", 4) <= 2

    def classify(self, t, off, depth=0):
        """What the type t declares in the 4 bytes at off."""
        t = self.strip(t)
        d = self.dies.get(t)
        if d is None or depth > 48:
            return "int"
        tag = d["tag"]
        if tag == "DW_TAG_pointer_type":
            return "ptr" if off == 0 else "int"
        if tag in ("DW_TAG_base_type", "DW_TAG_enumeration_type"):
            return "half" if d.get("byte_size", 4) == 2 else "int"
        if tag == "DW_TAG_array_type":
            es = self.size(d.get("type"))
            if es <= 0:
                return "int"
            within = off % es
            if within + 4 <= es:
                return self.classify(d.get("type"), within, depth + 1)
            # the word spans elements: half if they are 16-bit integers, bytes for a byte array
            if es == 1 and self.small_scalar(d.get("type")):
                return "bytes"
            return "half" if es == 2 and self.classify(d.get("type"), 0, depth + 1) == "half" else "int"
        if tag in ("DW_TAG_structure_type", "DW_TAG_union_type"):
            got, covered = [], set()
            for k in d["kids"]:
                m = self.dies[k]
                if m["tag"] != "DW_TAG_member":
                    continue
                bits = "bit_size" in m
                mo = m["data_bit_offset"] // 8 if "data_bit_offset" in m else m.get("data_member_location", 0)
                ms = max(1, (m["bit_size"] + (m.get("data_bit_offset", 0) % 8) + 7) // 8) if bits \
                    else self.size(m.get("type"))
                if not (mo < off + 4 and off < mo + ms):
                    continue
                covered.update(range(max(mo, off), min(mo + ms, off + 4)))
                if mo <= off and off + 4 <= mo + ms and not bits:
                    got.append(self.classify(m.get("type"), off - mo, depth + 1))
                elif (bits and m["bit_size"] < 32) or (not bits and self.small_scalar(m.get("type"))) or \
                        (ms == 2 and not bits and self.classify(m.get("type"), 0, depth + 1) == "half"):
                    got.append("half")  # a byte or halfword field, a bitfield, a 16-bit pair
                else:
                    got.append("int")
            if len(covered) < 4:
                return "int"  # padding, or bytes no member declares
            if tag == "DW_TAG_union_type":
                return max(got, key=CLASS_RANK.get)
            if len(got) == 1:
                return got[0]
            return "half" if all(g == "half" for g in got) else "int"
        return "int"

    def word_classes(self, addr):
        """[(class, variable name)] of every variable that covers the aligned word at addr."""
        top = bisect.bisect_right(self.starts, addr)
        low = bisect.bisect_left(self.starts, addr - self.max_size)
        got = []
        for i in range(low, top):
            a, n, t, name = self.vars[i]
            if a <= addr < a + n:
                got.append((self.classify(t, addr - a) if addr + 4 <= a + n else "int", name))
        return got

    def word_class(self, addr):
        """(class, variable name) for the aligned word at addr; (None, None) outside every variable.
        Overlaid variables (EWRAM_OVERLAY) all count: a pointer in any of them wins, half only if all agree."""
        got = self.word_classes(addr)
        if not got:
            return None, None
        return max(got, key=lambda x: CLASS_RANK[x[0]])


def parse_map(path):
    """[(addr, size, object)] for the input sections of .data, .bss and .rodata of a link map."""
    out, pending = [], None
    one = re.compile(r"^ (\.(?:data|bss|rodata)\S*)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)")
    name_only = re.compile(r"^ (\.(?:data|bss|rodata)\S*)\s*$")
    cont = re.compile(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)")
    with open(path, errors="replace") as fh:
        for line in fh:
            m = one.match(line)
            if m:
                out.append((int(m.group(2), 16), int(m.group(3), 16), m.group(4)))
                pending = None
                continue
            m = name_only.match(line)
            if m:
                pending = m.group(1)
                continue
            m = cont.match(line)
            if m and pending:
                out.append((int(m.group(1), 16), int(m.group(2), 16), m.group(3)))
            pending = None
    out.sort()
    return out


def norm_obj(obj):
    """build/pc-gate0/port/hw/frame.o -> port/hw/frame.o; build/x/src/a.o -> src/a.o;
    build/x/autostubs.o -> autostubs.o; libraries and crt files as they are."""
    for marker in ("/port/", "/src/"):
        i = obj.find(marker)
        if i >= 0 and obj.startswith("build/"):
            return obj[i + 1:]
    if obj.startswith("build/"):
        return os.path.basename(obj)
    return obj


class Image:
    """One snapshot part (port/test/pc/savestate.c) and the binary that made it."""

    def __init__(self, part):
        self.part = part
        try:
            with open(os.path.join(part, "meta.json")) as fh:
                self.meta = json.load(fh)
        except OSError as e:
            fail(f"{part}: {e.strerror}: not a PC snapshot part (meta.json)")
        if self.meta.get("format") != PC_FORMAT:
            fail(f"{part}/meta.json: not a {PC_FORMAT} snapshot")
        self.exe = Elf(os.path.join(part, "fe8pc"))
        if f"{self.exe.crc:08x}" != self.meta["exe_crc32"]:
            fail(f"{part}: the saved binary's CRC-32 is {self.exe.crc:08x}, meta.json says {self.meta['exe_crc32']}")
        with open(os.path.join(part, "regions.bin"), "rb") as fh:
            regions = fh.read()
        self.regions, off = {}, 0
        for (name, addr, size), (mname, maddr, msize) in zip(REGIONS, self.meta["regions"]):
            if (name, addr, size) != (mname, maddr, msize):
                fail(f"{part}: region {mname} 0x{maddr:08X}+0x{msize:X} is not the expected {name}")
            self.regions[name] = bytearray(regions[off:off + size])
            off += size
        if off != len(regions):
            fail(f"{part}: regions.bin has {len(regions)} bytes, the regions {off}")
        with open(os.path.join(part, "hostdata.bin"), "rb") as fh:
            self.host = fh.read()
        (self.data_addr, self.data_size), (self.bss_addr, self.bss_size) = self.meta["data"], self.meta["bss"]
        if len(self.host) != self.data_size + self.bss_size:
            fail(f"{part}: hostdata.bin has {len(self.host)} bytes, .data + .bss {self.data_size + self.bss_size}")
        for sec, (a, n) in ((".data", self.meta["data"]), (".bss", self.meta["bss"])):
            s = self.exe.by_name.get(sec)
            if s is None or (s["addr"], s["size"]) != (a, n):
                fail(f"{part}: {sec} of the saved binary is not where meta.json says")
        self.map = parse_map(os.path.join(part, "fe8pc.map"))

    def host_bytes(self, addr, n):
        if self.data_addr <= addr and addr + n <= self.data_addr + self.data_size:
            off = addr - self.data_addr
        elif self.bss_addr <= addr and addr + n <= self.bss_addr + self.bss_size:
            off = self.data_size + addr - self.bss_addr
        else:
            return None
        return self.host[off:off + n]

    def region_of(self, addr):
        for name, a, n in REGIONS:
            if a <= addr < a + n:
                return name, addr - a
        return None, None


def objects_of(exe, mapped):
    """{symbol: normalised object} for the .data/.bss symbols of exe, from its link map."""
    starts = [a for a, _, _ in mapped]
    out = {}
    for s in exe.symbols:
        if s.section not in (".data", ".bss", ".rodata") or s.type == 2:
            continue
        top = bisect.bisect_right(starts, s.value) - 1
        obj = None
        for i in range(top, max(top - 8, -1), -1):
            a, n, o = mapped[i]
            if a <= s.value < a + max(n, 1):
                obj = o
                break
        out[s] = norm_obj(obj) if obj else "?"
    return out


def classify(obj, sym):
    """(class, rule) for one .data/.bss symbol, or (None, problem)."""
    base = base_name(sym.name)
    if obj.startswith("src/"):
        if base in GAME_SESSION.get(obj, ()):
            return SESSION, "GAME_SESSION (a PC_PORT log flag)"
        return ("game-bss", "game .bss") if sym.section == ".bss" else ("game-data", "game .data")
    if obj.startswith("port/"):
        table = PORT_TABLE.get(obj)
        if table is None:
            return None, f"port object {obj} is not in PORT_TABLE (savestate.py): classify its globals"
        cls = table.get(base)
        if cls is None:
            return None, f"{obj}: {sym.name} is not in PORT_TABLE (savestate.py): classify it"
        return cls, f"PORT_TABLE {cls}"
    if obj == "autostubs.o":
        return STATE, "stub call counters"
    return SESSION, "crt/libc/link symbols"


def host_entries(exe, mapped, who):
    """A binary's .data/.bss symbols with their class: [(sym, obj, class,
    rule)]. Stops on a port symbol PORT_TABLE does not list (who names the
    binary in the message)."""
    objs = objects_of(exe, mapped)
    entries, problems = [], []
    for s, obj in sorted(objs.items(), key=lambda x: x[0].value):
        if s.size == 0 or s.section == ".rodata":
            continue
        cls, rule = classify(obj, s)
        if cls is None:
            problems.append(rule)
            continue
        entries.append((s, obj, cls, rule))
    if problems:
        fail(f"unclassified host globals in {who} (a new global must be classified in PORT_TABLE):\n  "
             + "\n  ".join(sorted(set(problems))))
    return entries


def plan_host(img):
    """The saved binary's .data/.bss symbols with their class (host_entries),
    after the run's own checks: no refused mode on, and no byte outside every
    symbol changed."""
    entries = host_entries(img.exe, img.map, f"the snapshot's binary ({img.part}/fe8pc)")
    for (obj, base), why in REFUSE_IF_SET.items():
        for s, o, _, _ in entries:
            if o == obj and base_name(s.name) == base and any(img.host_bytes(s.value, s.size)):
                fail(f"{img.part}: {why}; such a run cannot be snapshotted")
    # Bytes of .data/.bss no symbol covers must be what the binary starts with.
    covered = []
    for s, _, _, _ in entries:
        covered.append((s.value, s.value + s.size))
    for s in img.exe.symbols:
        if s.section in (".data", ".bss") and s.size and s.type != 2:
            covered.append((s.value, s.value + s.size))
    covered.sort()
    gaps, pos = [], img.data_addr
    for a, b in covered + [(img.bss_addr + img.bss_size, img.bss_addr + img.bss_size)]:
        if a > pos:
            gaps.append((pos, a))
        pos = max(pos, b)
    bad = []
    for a, b in gaps:
        for lo, hi in ((img.data_addr, img.data_addr + img.data_size), (img.bss_addr, img.bss_addr + img.bss_size)):
            x, y = max(a, lo), min(b, hi)
            if x < y and img.host_bytes(x, y - x) != img.exe.file_bytes(x, y - x):
                bad.append(f"0x{x:08X}-0x{y:08X}")
    if bad:
        fail(f"{img.part}: bytes of .data/.bss that no symbol covers changed during the run: " + ", ".join(bad[:8]))
    return entries


class Relinker:
    """Old host value (an address in the saved binary) -> the new binary's.
    objmap: {symbol: object} of the saved binary's data symbols (link map)."""

    def __init__(self, old, new, objmap, types, matcher=None):
        self.old, self.new, self.objmap, self.types, self.matcher = old, new, objmap, types, matcher
        self.target = None  # (symbol, offset) of the saved binary that the last resolve() found
        self.funcs = {}
        objs = []
        for s in old.symbols:
            if not (old.lo <= s.value < old.hi):
                continue
            if s.type == 2:
                prev = self.funcs.get(s.value)
                if prev is None or (prev.bind == 0 and s.bind != 0):
                    self.funcs[s.value] = s
            elif s.type == 1 and s.size > 0 and not s.section.startswith(".gba"):
                objs.append((s.value, s.value + s.size, 0 if s.bind else 1, s))
        objs.sort(key=lambda x: (x[0], x[2]))
        self.obj_starts = [a for a, _, _, _ in objs]
        self.objs = objs
        self.refs = None
        self.fn_list = sorted((s.value, s.value + max(s.size, 1), s) for s in self.funcs.values())
        self.fn_starts = [a for a, _, _ in self.fn_list]

    def resolve_code(self, v):
        """An address inside a function of the saved binary: (that function's
        address in the new binary + the same offset, how) or (None, reason)."""
        i = bisect.bisect_right(self.fn_starts, v) - 1
        if i < 0 or not (self.fn_list[i][0] <= v < self.fn_list[i][1]):
            return None, "not inside a function"
        a, _, f = self.fn_list[i]
        n, why = self.new_sym(f, True)
        if n is None:
            return None, why
        if v - a >= max(n.size, 1):
            return None, f"{f.key}+0x{v - a:X} is past the function's end in the new binary"
        return n.value + v - a, f"code {f.key}+0x{v - a:X}"

    def containing(self, v):
        """The sized data object of the saved binary that contains v (a global
        before a local alias), or None. Objects do not overlap except aliases,
        so the few last ones starting at or before v decide."""
        top = bisect.bisect_right(self.obj_starts, v) - 1
        best = None
        for i in range(top, max(top - 8, -1), -1):
            a, b, pri, s = self.objs[i]
            if a <= v < b and (best is None or pri < best[0]):
                best = (pri, s)
        return best[1] if best else None

    def address_taken(self):
        """Function starts of the saved binary whose address the program can
        store: held by an aligned word of its data (.data, .rodata, ...: proc
        scripts, tables, the ROM relocation table), or by any 4 bytes of its
        code (an immediate operand). A function only ever called directly
        never appears as a pointer in memory."""
        if getattr(self, "_taken", None) is None:
            taken = set()
            funcs = self.funcs
            for s in self.old.sections:
                if not s["flags"] & 2 or s["type"] == 8 or s["addr"] < HOST_IMAGE_MIN:  # ALLOC, not NOBITS
                    continue
                if s["name"].startswith((".eh_frame", ".gcc_except_table")):
                    continue  # unwind tables name every function; they are not pointers the program stores

                data = self.old.data[s["offset"]:s["offset"] + s["size"]]
                phases = range(4) if s["flags"] & 4 else (0,)  # SHF_EXECINSTR: every byte offset
                for ph in phases:
                    n = (len(data) - ph) // 4
                    for (w,) in struct.iter_unpack("<I", data[ph:ph + 4 * n]):
                        if w in funcs:
                            taken.add(w)
            self._taken = taken
        return self._taken

    def ref_index(self):
        """value -> [(sym, offset)]: words of named data objects of the saved
        binary's file image that hold a host address no symbol covers."""
        if self.refs is None:
            self.refs = defaultdict(list)
            for a, b, _, s in self.objs:
                if s.section not in (".data", ".rodata") and not s.section.startswith((".data.", ".rodata.")):
                    continue
                raw = self.old.file_bytes(a, b - a)
                if raw is None:
                    continue
                for off in range(0, (b - a) & ~3, 4):
                    v = struct.unpack_from("<I", raw, off)[0]
                    if self.old.lo <= v < self.old.hi and v not in self.funcs and self.containing(v) is None:
                        self.refs[v].append((s, off))
        return self.refs

    def new_sym(self, s, func):
        if self.matcher is not None:
            n, why = self.matcher.find(s, want_func=func)
            if n is None:
                return None, why
        else:
            n = self.new.lookup(s.key, want_func=func)
            if n is None:
                return None, f"{'function' if func else 'object'} {s.key} is not in the new binary (or not unique)"
        if not func and n.size != s.size:
            return None, f"object {s.key} has size {s.size} in the saved binary, {n.size} in the new one"
        return n, None

    def resolve(self, v, strict, starts_only=False):
        """(new value, how) or (None, reason). strict: the scan's rule (a function's
        first byte, or a plausible place in a sized data object of a game object),
        no anonymous literals. starts_only: a data object's first byte only (for a
        word of a byte array, which holds a pointer only when used for a struct)."""
        self.target = None
        f = self.funcs.get(v)
        if f is not None:
            if strict and v not in self.address_taken():
                return None, (f"the first byte of {f.key}, but the binary never takes that function's address "
                              "(no word of its data and no 4 bytes of its code hold it), so no pointer to it exists")
            n, why = self.new_sym(f, True)
            self.target = (f, 0)
            return (n.value, f"func {f.key}") if n else (None, why)
        s = self.containing(v)
        if s is not None:
            obj = self.objmap.get(s, "?")
            if strict and not obj.startswith("src/"):
                return None, f"inside {s.key}, data of {obj}, not of the game: the game's memory does not point there"
            if strict and not self.types.pointable(s.value, v - s.value, 4 if starts_only else 1):
                return None, (f"inside {s.key} at +0x{v - s.value:X}, not the start of an element or member of its "
                              "declared type" + (" (from a byte array: an object's start or an element of a table "
                                                 "of 4-byte or larger elements)" if starts_only else ""))
            n, why = self.new_sym(s, False)
            self.target = (s, v - s.value)
            return (n.value + v - s.value, f"obj {s.key}+0x{v - s.value:X}") if n else (None, why)
        if strict:
            return None, "no symbol starts at or contains it (code inside a function, or anonymous data)"
        refs = self.ref_index().get(v)
        if not refs:
            return None, "no symbol starts at or contains it, and no named data object holds it"
        vals = set()
        for s, off in refs:
            n, why = self.new_sym(s, False)
            if n is None:
                return None, f"anonymous data held by {s.key}+0x{off:X}: {why}"
            w = self.new.file_bytes(n.value + off, 4)
            vals.add(struct.unpack("<I", w)[0] if w else None)
        if len(vals) != 1 or None in vals:
            return None, f"anonymous data: its {len(refs)} holders give different new values"
        nv = vals.pop()
        s, off = refs[0]
        return nv, f"ref {s.key}+0x{off:X} (anonymous, {len(refs)} holder{'s' if len(refs) > 1 else ''})"


def gba_owner(exe):
    """addr -> 'name+0xoff' for GBA RAM variables (symbols at GBA addresses)."""
    syms = sorted((s.value, s.size, s.key) for s in exe.symbols
                  if s.size and any(a <= s.value < a + n for _, a, n in GBA_RAM))
    starts = [a for a, _, _ in syms]

    def owner(addr):
        top = bisect.bisect_right(starts, addr) - 1
        for i in range(top, max(top - 16, -1), -1):
            a, n, k = syms[i]
            if a <= addr < a + n:
                return f"{k}+0x{addr - a:X}"
        return "?"
    return owner


def known_words(exe):
    """{GBA address: label} of the known pointer fields."""
    out = {}
    for key, offs in KNOWN_FIELDS:
        s = exe.lookup(key)
        if s is None:
            fail(f"{exe.path}: known pointer field {key} not found")
        for off in offs:
            if s.size and off + 4 > s.size:
                fail(f"{exe.path}: {key} is 0x{s.size:X} bytes, the known field +0x{off:X} is outside it")
            label = key.split(":")[-1]
            if key.endswith("sProcArray"):
                label = f"sProcArray[{off // PROC_SIZE}].{PROC_FIELDS[off % PROC_SIZE]}"
            elif off:
                label = f"{label}[{off // 4}]"
            out[s.value + off] = label
    return out


class Matcher:
    """The saved binary's data symbol -> the new binary's. By key (a local by
    its source file and name); a function-static (GCC's name.N) of another
    binary by its file, base name and enclosing function (Types.host_fn, the
    DWARF), never by GCC's number: the numbers follow the order of the
    functions in the source, so moving a function swaps them (two statics
    named sLogged in ClearOam and DrawGlyph)."""

    def __init__(self, old, old_types, new, new_types):
        self.old, self.old_types, self.new, self.new_types = old, old_types, new, new_types
        self.same = new.crc == old.crc

    @staticmethod
    def _static(s):
        return s.file is not None and s.type != 2 and STATIC_SUFFIX.match(s.name) is not None

    def find(self, s, want_func=False, reverse=False):
        """(symbol, None) or (None, why). reverse: s is the new binary's, find the saved one's."""
        src, dst = (self.new, self.old) if reverse else (self.old, self.new)
        st, dt = (self.new_types, self.old_types) if reverse else (self.old_types, self.new_types)
        if self.same or not self._static(s) or st is None or dt is None:
            n = dst.lookup(s.key, want_func=want_func)
            return (n, None) if n else (None, f"{'function' if want_func else 'object'} {s.key} is not in the "
                                               f"{'saved' if reverse else 'new'} binary (or not unique)")
        base = f"{s.file}:{base_name(s.name)}"
        cands = [c for c in dst.by_base.get(base, []) if c.type != 2 and self._static(c)]
        fn = st.host_fn.get(s.value)
        if fn is None:
            if len(cands) == 1 and len([c for c in src.by_base.get(base, []) if c.type != 2]) == 1:
                return cands[0], None  # the only one of that name in its file, in both binaries
            return None, (f"function-static {s.key}: the DWARF names no function it is declared in, and the "
                          f"{'saved' if reverse else 'new'} binary has {len(cands)} of that name in its file")
        got = [c for c in cands if dt.host_fn.get(c.value) == fn]
        if len(got) == 1:
            return got[0], None
        return None, (f"function-static {base_name(s.name)} of {fn}() ({s.key}): {len(got)} in the "
                      f"{'saved' if reverse else 'new'} binary")


GBA_STATE_RAM = ((0x02000000, 0x21000, 0x40000), (0x03000000, 0x19000, 0x8000))  # mGBA 0.10 GBASerializedState


class GbaSide:
    """The GBA's own memory and symbols, as evidence for the relink's strict
    scan and the audit. Memory: the snapshot's gba part (mGBA's state, saved
    as tick T-1's line is written: one main-loop step before the PC's photo,
    which is taken at tick T's sample point). Symbols: the GBA build's ELF
    (fireemblem8.elf), by name: S's GBA address for the PC's object or
    function S (a Thumb function's value has bit 0 set, as a stored pointer)."""

    def __init__(self, tickdir, gba_elf):
        self.why = None
        self.state = None
        sp = os.path.join(tickdir, "gba", "state.ss")
        if not os.path.exists(sp):
            self.why = "no gba part in this snapshot"
            return
        if not gba_elf or not os.path.exists(gba_elf):
            self.why = f"no GBA ELF ({gba_elf or 'none given'}; --gba-elf fireemblem8.elf)"
            return
        with open(sp, "rb") as fh:
            st = fh.read()
        if len(st) < 0x61000 or struct.unpack_from("<I", st, 0)[0] & 0xFF000000 != 0x01000000:
            self.why = f"{sp} is not an mGBA 0.10 GBA save state"
            return
        self.state = st
        with open(gba_elf, "rb") as fh:
            d = fh.read()
        if d[:4] != b"\x7fELF" or d[4] != 1:
            fail(f"{gba_elf}: not a 32-bit ELF file")
        self.elf_crc = zlib.crc32(d) & 0xFFFFFFFF
        shoff, = struct.unpack_from("<I", d, 32)
        shentsize, shnum = struct.unpack_from("<HH", d, 46)
        secs = [struct.unpack_from("<IIIIIIIIII", d, shoff + i * shentsize) for i in range(shnum)]
        symtab = next((s for s in secs if s[1] == 2), None)
        if symtab is None:
            fail(f"{gba_elf}: no symbol table")
        strtab = secs[symtab[6]][4]
        self.syms = defaultdict(list)
        self.funcs = set()
        for i in range(symtab[5] // 16):
            st_name, value, size, info, _o, shndx = struct.unpack_from("<IIIBBH", d, symtab[4] + 16 * i)
            typ = info & 15
            if shndx == 0 or typ not in (0, 1, 2) or not st_name:
                continue
            end = d.index(b"\0", strtab + st_name)
            name = d[strtab + st_name:end].decode("utf-8", "replace")
            self.syms[name].append((value, size, typ))
            if typ == 2:
                self.funcs.add(value & ~1)

    def word(self, addr):
        """The GBA's aligned word at a GBA RAM address, or None."""
        if self.state is None:
            return None
        for base, off, n in GBA_STATE_RAM:
            if base <= addr < base + n:
                return struct.unpack_from("<I", self.state, off + addr - base)[0]
        return None

    def addr_of(self, s):
        """(GBA address, size) of the PC's symbol s by its name, or None (no
        such GBA symbol, or several)."""
        name = base_name(s.name) if s.file is not None and s.type != 2 else s.name
        for prefix in ("__wrap_", "__real_"):
            if name.startswith(prefix):
                name = name[len(prefix):]
        got = [(v, n) for v, n, t in self.syms.get(name, ()) if (t == 2) == (s.type == 2) or t == 0]
        return got[0] if len(got) == 1 else None

    @staticmethod
    def is_address(g):
        """A GBA bus address the game can hold (EWRAM to the end of the ROM, SRAM)."""
        return 0x02000000 <= g < 0x0A000000 or 0x0E000000 <= g < 0x0E010000


def relink_layout_problem(old, new):
    """None when a restored global's declared layouts (Types.layout_at) in the
    saved and the new binary are the same, else where they first differ. Same
    size is not enough: two fields swapped, or an int made a float, would put
    the photo's bytes in the wrong places."""
    if old == new:
        return None
    if old is None or new is None:
        return f"the {'saved' if old is None else 'new'} binary's DWARF declares no type for it"
    n = min(len(old), len(new))
    i = next((k for k in range(n) if old[k] != new[k]), n)
    a = max(0, i - 48)
    return f"saved '...{old[a:i + 32]}...', new '...{new[a:i + 32]}...'"


def relink(img, new, log, gba=None):
    """The patch records [(addr, bytes)] that bring img into binary new, and a summary.

    Each aligned word of EWRAM and IWRAM that holds an address inside the saved
    binary's loaded image is decided by provenance first:
      known  a known pointer field (KNOWN_FIELDS): must resolve, or the relink stops;
      typed  declared a pointer by its variable's type (DWARF): resolved like a
             known field; unresolved, it stops the relink for another binary.
             Where an overlaid variable declares the word no pointer, the GBA
             check comes first: the GBA's same 32-bit value makes it that
             variable's data, left alone ("overlay");
      half   declared integers narrower than 32 bits (16-bit arrays such as
             tilemaps, palettes and OAM; byte and halfword fields; bitfields):
             never a pointer, left alone;
      bytes  inside a declared byte array (a buffer, perhaps used for a
             struct): rewritten only if it is a function's first byte or a game
             data object's first byte, else left alone;
      scan   anything else (4-byte integers, untyped areas, the user part of
             each proc of the proc pool, whose real type is the proc's):
             rewritten only under the strict rule (a function's first byte, or
             a place in a game data object that its declared type allows: its
             start, an element's or a member's), else left alone.
    Restored host globals (not raw) are scanned with the strict rule.

    gba (GbaSide): the GBA's own memory and symbols check each strict-scan
    word of GBA RAM first (the GBA cannot hold a host address, so its word
    tells what the place holds):
      - the GBA holds the same 32-bit value: data (a tilemap word, say), left alone;
      - the GBA holds the address of the place the word resolves to (S, or
        inside S: its photo is one main-loop step older): rewritten, confirmed;
    and where the GBA is on the same path at that place (the lag replay on
    the GBA's sequence; a proc slot running the same proc script on both):
      - the GBA's word has the same low 16 bits: a 16-bit value beside the high
        half of an address, left alone;
      - the GBA holds something that is no address (and not 0): data, left alone.
    Otherwise the strict rule alone decides (counted as not checked)."""
    objmap = objects_of(img.exe, img.map)
    types = Types(img.exe)
    same_binary = new.crc == img.exe.crc
    types_new = None if same_binary else Types(new)
    matcher = Matcher(img.exe, types, new, types_new)
    rel = Relinker(img.exe, new, objmap, types, matcher)
    known = known_words(img.exe)
    known_new = known_words(new)
    if sorted(known) != sorted(known_new):
        fail("the known pointer fields are at other GBA addresses in the new binary")
    proc = img.exe.lookup("proc.c:sProcArray")
    proc_lo = proc.value
    proc_hi = proc_lo + PROC_SIZE * PROC_COUNT
    owner = gba_owner(new)
    st = defaultdict(int)
    records = []
    errors = []

    # Where the GBA's photo shows the same path as the PC's (gba docstring above).
    use_gba = gba is not None and gba.state is not None
    path_all = use_gba and img.meta["part"] == "pclag" and lag_prefix(img)["left_at_tick"] is None
    slot_path = {}
    if use_gba:
        ew = img.regions["EWRAM"]
        for p in range(PROC_COUNT):
            a = proc_lo + p * PROC_SIZE
            v, g = struct.unpack_from("<I", ew, a - 0x02000000)[0], gba.word(a)
            if v == 0:
                slot_path[p] = path_all  # free here: the last occupant's bytes, as on the GBA if on its path
                continue
            if v == g:
                slot_path[p] = True  # a script the host does not build: at its GBA address on both sides
                continue
            s = rel.containing(v)
            ga = gba.addr_of(s) if s is not None and s.value == v else None
            slot_path[p] = ga is not None and ga[0] == g

    def on_path(addr):
        if proc_lo <= addr < proc_hi:
            return slot_path.get((addr - proc_lo) // PROC_SIZE, False)
        return path_all

    def gba_holds_target(g, tgt):
        """Whether the GBA's word is the address of the place tgt (or inside it)."""
        if tgt is None:
            return False
        s, off = tgt
        ga = gba.addr_of(s)
        if ga is None:
            return False
        gv, gsize = ga
        if s.type == 2:
            return (g & ~1) == (gv & ~1)
        return gv <= g < gv + max(gsize or s.size, 1)

    def left(where, addr, v, how, kind, tag=None):
        st["left"] += 1
        st[f"left_{kind}"] += 1
        if tag:
            st[tag] += 1
        log.append(f"LEFT    {where:<44} 0x{addr:08X} 0x{v:08X}  {how} [{kind}]")

    def rewrite(buf, off, where, addr, kind, v, overlay=None):
        if where is None:
            where = owner(addr)
        strict = kind in ("scan", "bytes")
        # A declared pointer that an overlaid variable (EWRAM_OVERLAY) declares as no pointer
        # (overlay: those variables' [(class, name)]): the declarations do not say which variable
        # the word holds now. The GBA cannot hold a host address, so when its word is the same
        # 32-bit value the word is the other variable's data (cov9 H-col-1: gStatusScreenSt.font
        # over gBanimOaml's OAM words, gPrepUnitList.units over gBanimScrRight's bytes); otherwise
        # it is a declared pointer as any other.
        if overlay and use_gba:
            g = gba.word(addr)
            if g is not None and g == v:
                left(where, addr, v, "declared a pointer, but overlaid by "
                     + ", ".join(f"{n} ({c})" for c, n in overlay)
                     + ", and the GBA holds the same 32-bit value here: that variable's data, not a host address "
                     "(GBA check)", "overlay")
                return
        g = gba.word(addr) if use_gba and strict else None
        if g is not None and g == v:
            left(where, addr, v, "the GBA holds the same 32-bit value here: data, not a host address (GBA check)",
                 kind, "left_gba_same")
            return
        nv, how = rel.resolve(v, strict=strict, starts_only=kind == "bytes")
        if nv is None:
            if kind == "known" or (kind == "typed" and not same_binary):
                errors.append(f"{where} (0x{addr:08X}, {kind} pointer) holds 0x{v:08X}: {how}")
            else:
                left(where, addr, v, how, kind)
            return
        if g is not None:
            path = on_path(addr)
            if gba_holds_target(g, rel.target):
                how += f" (GBA check: the GBA holds its own address there, 0x{g:08X})"
                st["scan_gba_confirmed"] += 1
            elif path and (g & 0xFFFF) == (v & 0xFFFF):
                left(where, addr, v, f"the GBA holds 0x{g:08X} here, the same low 16 bits: a 16-bit value beside "
                     f"the high half of an address, not a pointer (GBA check; read whole: {how})", kind,
                     "left_gba_half")
                return
            elif path and g != 0 and not GbaSide.is_address(g):
                left(where, addr, v, f"on the GBA's path the GBA holds 0x{g:08X} here, which is no address: data, "
                     f"not a pointer (GBA check; read whole: {how})", kind, "left_gba_data")
                return
            else:
                how += (f" (GBA check: not confirmed; the GBA holds 0x{g:08X}"
                        + ("" if path else ", another proc or path there") + ")")
                st["scan_gba_unconfirmed"] += 1
        elif strict and addr < HOST_IMAGE_MIN:
            st["scan_gba_unchecked"] += 1
        struct.pack_into("<I", buf, off, nv)
        st[f"rewritten_{kind}"] += 1
        st["moved"] += nv != v
        log.append(f"REWRITE {where:<44} 0x{addr:08X} 0x{v:08X} -> 0x{nv:08X}  {how} [{kind}]")

    # GBA memory: every region; EWRAM and IWRAM relinked.
    for name, base, size in REGIONS:
        buf = bytearray(img.regions[name])
        if any(name == r for r, _, _ in GBA_RAM):
            for off in range(0, size, 4):
                v = struct.unpack_from("<I", buf, off)[0]
                if not (img.exe.lo <= v < img.exe.hi):
                    continue
                addr = base + off
                lab = known.get(addr)
                if lab:
                    rewrite(buf, off, lab, addr, "known", v)
                elif proc_lo <= addr < proc_hi and (addr - proc_lo) % PROC_SIZE >= 0x2C:
                    p, o = divmod(addr - proc_lo, PROC_SIZE)
                    rewrite(buf, off, f"sProcArray[{p}]+0x{o:X} (proc data)", addr, "scan", v)
                else:
                    cls, var = types.word_class(addr)
                    if cls == "ptr":
                        rewrite(buf, off, None, addr, "typed", v,
                                overlay=[w for w in types.word_classes(addr) if w[0] != "ptr"])
                    elif cls == "bytes":
                        rewrite(buf, off, None, addr, "bytes", v)
                    elif cls == "half":
                        st["left"] += 1
                        st["left_half"] += 1
                        log.append(f"LEFT    {owner(addr):<44} 0x{addr:08X} 0x{v:08X}  declared integers narrower "
                                   f"than 32 bits ({var}) [half]")
                    else:
                        rewrite(buf, off, None, addr, "scan", v)
        records.append((base, bytes(buf)))

    # Host globals by name. The new binary's globals are classified too (a port
    # global PORT_TABLE does not list stops the relink, as in the saved one),
    # and each one restorable there must be in the photo; each restored one
    # must have the same size and, for another binary, the same declared
    # layout (relink_layout_problem).
    new_map = new.path + ".map"
    if not os.path.exists(new_map):
        fail(f"{new_map}: no link map next to the binary (the Gate 0 build writes one; the relink needs it to "
             "classify the binary's globals)")
    new_entries = host_entries(new, parse_map(new_map), f"the new binary ({new.path})")
    restored = defaultdict(int)
    game_data_changed = []
    for s, obj, cls, rule in plan_host(img):
        if cls == SESSION:
            st["session"] += 1
            continue
        data = img.host_bytes(s.value, s.size)
        if cls == "game-data":
            if data == img.exe.file_bytes(s.value, s.size):
                st["game_data_unchanged"] += 1
                continue
            game_data_changed.append(s.key)
            log.append(f"GAMEDATA {s.key} ({obj}): changed during the run, restored")
        n, why = matcher.find(s)
        if n is None and obj == "autostubs.o":
            # A generated stub's call counter (only "was its first call logged")
            # that the new binary does not have: that function is built there now
            # (round 10's crimsoneye, shine and earthquake files). Nothing to restore.
            log.append(f"DROPPED {s.key} ({obj}): a stub counter the new binary does not have (the function is "
                       "built there)")
            continue
        if n is None:
            errors.append(f"host global {s.key} ({obj}): {why}")
            continue
        if n.key != s.key:
            log.append(f"STATIC  {s.key} ({obj}) is {n.key} in the new binary (the same function-static, matched by "
                       f"its function {types.host_fn.get(s.value)})")
        if n.size != s.size:
            errors.append(f"host global {s.key} ({obj}) has size {s.size} in the saved binary, {n.size} in the new one")
            continue
        if types_new is not None:
            why = relink_layout_problem(types.layout_at(s.value), types_new.layout_at(n.value))
            if why:
                errors.append(f"host global {s.key} ({obj}) is laid out differently in the new binary (same size): "
                              f"{why}")
                continue
        buf = bytearray(data)
        if cls == CODE:
            for off in range((-s.value) % 4, s.size - 3, 4):
                v = struct.unpack_from("<I", buf, off)[0]
                if img.exe.lo <= v < img.exe.hi:
                    nv, how = rel.resolve_code(v)
                    if nv is None:
                        st["left"] += 1
                        log.append(f"LEFT    {s.key + '+0x%X' % off:<44} {'':10} 0x{v:08X}  {how} [code]")
                    else:
                        struct.pack_into("<I", buf, off, nv)
                        st["rewritten_code"] += 1
                        st["moved"] += nv != v
                        log.append(f"REWRITE {s.key + '+0x%X' % off:<44} {'':10} 0x{v:08X} -> 0x{nv:08X}  {how} [code]")
        elif cls != RAW:
            for off in range((-s.value) % 4, s.size - 3, 4):
                v = struct.unpack_from("<I", buf, off)[0]
                if img.exe.lo <= v < img.exe.hi:
                    rewrite(buf, off, f"{s.key}+0x{off:X}", n.value + off, "scan", v)
        records.append((n.value, bytes(buf)))
        restored[cls] += 1
    # The new binary's restorable globals that the photo does not hold (a global
    # added after the snapshot): its value at tick T is unknown, so the relink
    # stops. New game .data stops it too: the new binary's own run might have
    # written it before tick T (a counter, a table the new code fills), and
    # nothing in the photo says. The port builds CONST_DATA tables as plain
    # .data, so a new decomp or PC_PORT table is such a global too; for that
    # case SAVESTATE_NEW_GAME_DATA=initial resumes with the new binary's initial
    # values (right only if nothing writes them before tick T).
    new_game_data = []
    for s, obj, cls, rule in new_entries:
        if cls == SESSION or matcher.find(s, reverse=True)[0] is not None:
            continue
        if cls == "game-data":
            new_game_data.append((s.key, obj))
            continue
        errors.append(f"the new binary's {cls} global {s.key} ({obj}) is not in the snapshot's binary: the photo "
                      "cannot hold its value (make the snapshot again with this binary)")
    if new_game_data:
        names = ", ".join(f"{k} ({o})" for k, o in new_game_data[:12]) + \
            (f", ... {len(new_game_data) - 12} more" if len(new_game_data) > 12 else "")
        if os.environ.get("SAVESTATE_NEW_GAME_DATA") == "initial":
            for k, o in new_game_data:
                log.append(f"NEWDATA {k} ({o}): game .data the snapshot's binary did not have: the new binary's "
                           "initial value (SAVESTATE_NEW_GAME_DATA=initial)")
            print(f"savestate: WARNING: {len(new_game_data)} game .data globals the snapshot's binary did not have "
                  f"keep the new binary's initial values (SAVESTATE_NEW_GAME_DATA=initial): {names}. A resumed run is "
                  "wrong if the new binary writes them before the snapshot's tick.")
        else:
            errors.append(f"the new binary has {len(new_game_data)} game .data globals the snapshot's binary did not "
                          f"have: {names}. The photo cannot say whether this binary's run would have written them "
                          "before the snapshot's tick: make the snapshot again with this binary, or, if nothing "
                          "writes them (a new read-only table), set SAVESTATE_NEW_GAME_DATA=initial")
    # The prologue (tick T-1's trace line, from the saved binary) names the main
    # callback by its host address ("gMainCallback"): relinked like the field.
    prologue = b""
    pro_path = os.path.join(img.part, "prologue.jsonl")
    if os.path.exists(pro_path):
        with open(pro_path, "rb") as fh:
            prologue = fh.read().rstrip(b"\n")
        m = re.search(rb'"gMainCallback":"([0-9a-f]{8})"', prologue)
        if m is None:
            errors.append(f"{pro_path}: no gMainCallback in the prologue line")
        else:
            v = int(m.group(1), 16)
            if img.exe.lo <= v < img.exe.hi:
                nv, how = rel.resolve(v, strict=False)
                if nv is None:
                    errors.append(f"prologue gMainCallback 0x{v:08X}: {how}")
                else:
                    prologue = prologue[:m.start(1)] + f"{nv:08x}".encode() + prologue[m.end(1):]
                    log.append(f"REWRITE {'prologue line gMainCallback':<44} {'':10} 0x{v:08X} -> 0x{nv:08X}  {how}")
    if errors:
        fail("cannot relink this snapshot for " + new.path + ":\n  " + "\n  ".join(errors[:40])
             + (f"\n  ... {len(errors) - 40} more" if len(errors) > 40 else ""))
    summary = {"restored": dict(restored), "game_data_changed": game_data_changed,
               "new_game_data": [k for k, _ in new_game_data],
               "rewritten_known": st["rewritten_known"], "rewritten_typed": st["rewritten_typed"],
               "rewritten_scan": st["rewritten_scan"] + st["rewritten_bytes"],
               "rewritten_code": st["rewritten_code"], "moved": st["moved"],
               "left": st["left"], "left_half": st["left_half"], "left_scan": st["left_scan"] + st["left_bytes"],
               "left_typed": st["left_typed"], "left_overlay": st["left_overlay"],
               "gba_check": "done" if use_gba else (gba.why if gba is not None else "not asked"),
               "scan_gba_confirmed": st["scan_gba_confirmed"], "scan_gba_unconfirmed": st["scan_gba_unconfirmed"],
               "scan_gba_unchecked": st["scan_gba_unchecked"], "left_gba_same": st["left_gba_same"],
               "left_gba_half": st["left_gba_half"], "left_gba_data": st["left_gba_data"],
               "gba_path_all": path_all,
               "session": st["session"], "game_data_unchanged": st["game_data_unchanged"], "prologue": prologue}
    return records, summary


def lag_prefix(img):
    """The lag replay's own record of ticks 0..T-1 (gate0_hook.c's totals, in
    the snapshot): ticks whose main-loop event count was not the GBA's, and lag
    VBlanks run later than the GBA's position. The compare uses it so that a
    resumed lag check labels the window as the full run's would (README.md,
    "Lag replay": after the first tick off the GBA's sequence nothing is
    lag-only)."""
    def u32s(key, n=1):
        s = img.exe.lookup(key)
        if s is None or s.size < 4 * n:
            fail(f"{img.part}: {key} not in the saved binary")
        return list(struct.unpack(f"<{n}I", img.host_bytes(s.value, 4 * n)))
    mism = u32s("gate0_hook.c:sEventMismatch")[0]
    ticks = u32s("gate0_hook.c:sEventMismatchTicks", 8)[:min(mism, 8)]
    runs, exact = u32s("gate0_hook.c:sLagRuns")[0], u32s("gate0_hook.c:sLagExact")[0]
    # The first tick that left the GBA's sequence: the first with another event
    # count, or with a lag VBlank run off its GBA position (gate0_hook.c
    # sLagOffTick, 1 + that tick), whichever came first.
    left = ticks[:1]
    off = img.exe.lookup("gate0_hook.c:sLagOffTick")
    if off is not None and off.size >= 4:
        v = struct.unpack("<I", img.host_bytes(off.value, 4))[0]
        if v:
            left.append(v - 1)
    return {"event_mismatch_ticks": mism, "first_event_mismatch_ticks": ticks, "lag_vblanks_run": runs,
            "lag_vblanks_at_gba_position": exact, "left_at_tick": min(left) if left else None}


def write_patch(path, new_crc, img, records, pro):
    m = img.meta
    resume = {"tick": m["tick"], "set": m["set"], "part": m["part"], "rom_crc32": m["rom_crc32"],
              "keys_crc32": m["keys_crc32"], "saved_exe_crc32": m["exe_crc32"]}
    if m["part"] == "pclag":
        resume["prefix"] = lag_prefix(img)
    frag = m["sram_header"] + ',"resume":' + json.dumps(resume, separators=(",", ":"))
    raw = b""
    raw_path = os.path.join(img.part, "prologue.raw")
    if os.path.exists(raw_path):
        with open(raw_path, "rb") as fh:
            raw = fh.read()

    def blob(b):
        return struct.pack("<I", len(b)) + b
    # savestate.c refuses the patch unless the part's meta.json, regions.bin and
    # hostdata.bin are still the ones it was made from (a part saved again into
    # the same directory: a re-save with other lag or affine input files keeps
    # meta.json byte for byte, but not the memory).
    crcs = []
    for name in ("meta.json", "regions.bin", "hostdata.bin"):
        with open(os.path.join(img.part, name), "rb") as fh:
            crcs.append(zlib.crc32(fh.read()) & 0xFFFFFFFF)
    out = [PATCH_MAGIC, struct.pack("<IIIIIII", new_crc, m["tick"], int(m["keys_crc32"], 16), int(m["rom_crc32"], 16),
                                    *crcs),
           blob(m["part"].encode()), blob(frag.encode()), blob(m["sram_header"].encode()), blob(pro), blob(raw),
           struct.pack("<I", len(records))]
    for addr, data in records:
        out.append(struct.pack("<II", addr, len(data)))
        out.append(data)
    tmp = path + ".tmp"
    with open(tmp, "wb") as fh:
        fh.write(b"".join(out))
    os.replace(tmp, path)


def check_set(tickdir, quiet=False):
    """Every part of one snapshot tick must come from the same set, tick, key file,
    ROM and SRAM. Returns {part: meta}."""
    metas = {}
    for p in PARTS:
        f = os.path.join(tickdir, p, "meta.json")
        if os.path.exists(f):
            with open(f) as fh:
                metas[p] = json.load(fh)
    if not metas:
        fail(f"{tickdir}: no snapshot part (gba, pc, pclag or pcaff with meta.json)")
    ref_part = "gba" if "gba" in metas else sorted(metas)[0]
    ref = metas[ref_part]
    problems = []
    for p, m in metas.items():
        if m.get("format") != (GBA_FORMAT if p == "gba" else PC_FORMAT) or m.get("part") != p:
            problems.append(f"{p}: not a {p} snapshot part")
        for k in ("tick", "set", "keys_crc32", "rom_crc32"):
            if m.get(k) != ref.get(k):
                problems.append(f"{p}: {k} {m.get(k)!r}, {ref_part} {ref.get(k)!r}")
        if sram_fnv(m) != sram_fnv(ref):
            problems.append(f"{p}: booted from other SRAM ({sram_fnv(m)}) than {ref_part} ({sram_fnv(ref)})")
    if problems:
        fail(f"{tickdir}: the parts do not belong together:\n  " + "\n  ".join(problems))
    if not quiet:
        print(f"{tickdir}: tick {ref['tick']}, set {ref['set']}, parts {', '.join(sorted(metas))}, "
              f"ROM CRC-32 {ref['rom_crc32']}, keys {ref.get('keys')} (CRC-32 {ref['keys_crc32']})"
              + (f", SRAM fnv1a {sram_fnv(ref)}" if sram_fnv(ref) else ""))
    return metas


def sram_fnv(meta):
    m = re.search(r'"sram_fnv":"([0-9a-f]+)"', meta.get("sram_header", ""))
    return m.group(1) if m else None


SNAPSHOT_FILES = ("meta.json", "regions.bin", "hostdata.bin", "prologue.jsonl", "prologue.raw")


def snapshot_hash(part):
    """SHA-1 of what a PC part holds (its meta.json, memory and prologue): a
    made patch is reused only for the same content (a part saved again into
    the same directory gets a new patch)."""
    h = hashlib.sha1()
    for name in SNAPSHOT_FILES:
        h.update(name.encode() + b"\0")
        try:
            with open(os.path.join(part, name), "rb") as fh:
                for chunk in iter(lambda: fh.read(1 << 20), b""):
                    h.update(chunk)
        except FileNotFoundError:
            h.update(b"(none)")
        h.update(b"\0")
    return h.hexdigest()


def tool_hash(gba_elf):
    """SHA-1 of what decides a patch besides the snapshot and the binary: this
    tool's own text (PORT_TABLE, REFUSE_IF_SET, KNOWN_FIELDS and every rule),
    the GBA ELF the GBA check reads (its CRC-32), and SAVESTATE_NEW_GAME_DATA."""
    h = hashlib.sha1()
    with open(os.path.abspath(__file__), "rb") as fh:
        h.update(fh.read())
    if gba_elf and os.path.exists(gba_elf):
        with open(gba_elf, "rb") as fh:
            h.update(b"gba-elf %08x" % (zlib.crc32(fh.read()) & 0xFFFFFFFF))
    h.update(b"newdata " + os.environ.get("SAVESTATE_NEW_GAME_DATA", "").encode())
    return h.hexdigest()


def default_gba_elf(given):
    if given:
        return given
    return "fireemblem8.elf" if os.path.exists("fireemblem8.elf") else None


def cmd_relink(args):
    part = args.part.rstrip("/")
    tickdir = os.path.dirname(part)
    check_set(tickdir, quiet=True)
    img = Image(part)
    new = Elf(args.exe)
    gba_elf = default_gba_elf(args.gba_elf)
    outdir = os.path.join(part, f"relink-{new.crc:08x}")
    patch = os.path.join(outdir, "patch.bin")
    content = snapshot_hash(part)
    tool = tool_hash(gba_elf)
    if os.path.exists(patch) and not args.force:
        try:
            with open(os.path.join(outdir, "summary.json")) as fh:
                s = json.load(fh)
        except (OSError, ValueError):
            s = {}
        if s.get("tool") == TOOL_VERSION and s.get("snapshot_sha1") == content and s.get("tool_sha1") == tool:
            print(f"savestate: {patch} is there for this binary, this snapshot and this tool ({s['line']})")
            return 0
        if s.get("snapshot_sha1") not in (None, content):
            print(f"savestate: {patch} was made from another snapshot in {part} (saved again since): making it again")
        else:
            print(f"savestate: {patch} was made by another version of savestate.py (its tables or rules), another "
                  "GBA ELF or another SAVESTATE_NEW_GAME_DATA: making it again")
    log = []
    gba = GbaSide(tickdir, gba_elf)
    records, summary = relink(img, new, log, gba)
    os.makedirs(outdir, exist_ok=True)
    write_patch(patch, new.crc, img, records, summary.pop("prologue"))
    same = new.crc == img.exe.crc
    line = (f"tick {img.meta['tick']}, {img.meta['part']}: saved by {img.exe.crc:08x}, for {new.crc:08x}"
            f"{' (the same binary)' if same else ''}; {len(records)} records; "
            f"host addresses rewritten: {summary['rewritten_known']} known fields + {summary['rewritten_typed']} "
            f"declared pointers + {summary['rewritten_scan']} by the strict scan, {summary['moved']} of them moved; "
            f"host-range words left alone: {summary['left_half']} declared narrow integers, {summary['left_scan']} failing "
            f"the strict rule, {summary['left_typed']} declared pointers not resolved (same binary only), "
            f"{summary.get('left_overlay', 0)} declared pointers of an overlay that hold the other variable's data "
            "(GBA check); "
            f"host globals restored {summary['restored']}, kept from the new process {summary['session']}; "
            f"game .data changed by the run: {len(summary['game_data_changed'])}"
            + (f"; game .data only the new binary has (its initial values): {len(summary['new_game_data'])}"
               if summary["new_game_data"] else "")
            + (f"; GBA check of the strict scan: {summary['scan_gba_confirmed']} confirmed, "
               f"{summary['scan_gba_unconfirmed']} not confirmed, left alone {summary['left_gba_same']} same value + "
               f"{summary['left_gba_half']} same low half + {summary['left_gba_data']} data on the GBA's path"
               if summary["gba_check"] == "done" else f"; GBA check of the strict scan not done: {summary['gba_check']}"))
    summary["line"] = line
    summary["tool"] = TOOL_VERSION
    summary["snapshot_sha1"] = content
    summary["tool_sha1"] = tool
    with open(os.path.join(outdir, "relink.log"), "w") as fh:
        fh.write(f"# savestate.py relink {part} for {args.exe} (CRC-32 {new.crc:08x})\n# {line}\n")
        fh.write("\n".join(log) + "\n")
    with open(os.path.join(outdir, "summary.json"), "w") as fh:
        json.dump(summary, fh, indent=1)
    print(f"savestate: {line}")
    print(f"savestate: wrote {patch} and relink.log")
    return 0


def cmd_check(args):
    check_set(args.tickdir.rstrip("/"))
    return 0


def cmd_classify(args):
    exe = Elf(args.exe)
    mapped = parse_map(args.map or args.exe + ".map")
    objs = objects_of(exe, mapped)
    count, problems = defaultdict(int), []
    objs = {s: o for s, o in objs.items() if s.section != ".rodata"}  # read-only: nothing to restore
    for s, obj in sorted(objs.items(), key=lambda x: x[0].value):
        if not s.size:
            continue
        cls, rule = classify(obj, s)
        if cls is None:
            problems.append(rule)
            continue
        count[cls] += 1
        if args.verbose and not obj.startswith("src/"):
            print(f"  {cls:9s} {obj:40s} {s.name} ({s.size} bytes)")
    print("classes: " + ", ".join(f"{k} {v}" for k, v in sorted(count.items())))
    listed = {(o, n) for o, t in PORT_TABLE.items() for n in t}
    seen = {(o, base_name(s.name)) for s, o in objs.items() if o.startswith("port/")}
    stale = sorted(listed - seen)
    if stale:
        print("PORT_TABLE entries with no symbol in this binary (removed or renamed globals, or ones the compiler "
              "dropped because nothing reads them): "
              + ", ".join(f"{o}:{n}" for o, n in stale))
    if problems:
        print("unclassified:\n  " + "\n  ".join(sorted(set(problems))))
        return 1
    return 0


def cmd_audit(args):
    """Relink snapshot A for B's binary and compare with B, word by word."""
    a, b = Image(args.a.rstrip("/")), Image(args.b.rstrip("/"))
    for k in ("tick", "part", "keys_crc32", "rom_crc32"):
        if a.meta[k] != b.meta[k]:
            fail(f"audit: the snapshots differ in {k}: {a.meta[k]!r}, {b.meta[k]!r}")
    log = []
    # The GBA's own memory (the gba part's mGBA state, taken one main-loop step
    # earlier: tick T-1's frame) and symbols: the relink's GBA check, and here
    # evidence for the low halves.
    gba = GbaSide(os.path.dirname(a.part), default_gba_elf(args.gba_elf))
    records, summary = relink(a, b.exe, log, gba)
    summary.pop("prologue")
    by_addr = dict(records)
    print(f"audit: A {a.part} (binary {a.exe.crc:08x}), B {b.part} (binary {b.exe.crc:08x}), tick {a.meta['tick']}")
    print(f"audit: relink of A for B: {summary['rewritten_known']} known + {summary['rewritten_typed']} declared + "
          f"{summary['rewritten_scan']} scanned words rewritten ({summary['moved']} moved), {summary['left']} left "
          f"alone ({summary['left_half']} declared narrow integers, {summary['left_scan']} failing the strict rule"
          + (f", of them {summary['left_gba_same']} + {summary['left_gba_half']} + {summary['left_gba_data']} by the "
             "GBA check)" if gba.state is not None else f"; no GBA check: {gba.why})"))
    gba_word = gba.word
    types_a, types_b = Types(a.exe), Types(b.exe)
    matcher = Matcher(a.exe, types_a, b.exe, types_b)
    objmap_a = objects_of(a.exe, a.map)
    proc = a.exe.lookup("proc.c:sProcArray")

    def what_host(v):
        if 0x02000000 <= v < 0x0E010000:
            return "a GBA address"
        if a.exe.lo <= v < a.exe.hi or b.exe.lo <= v < b.exe.hi:
            return "an address in the binary's image"
        return "a host stack or heap address"

    def image_high(h, exe):
        return exe.lo >> 16 <= h < (exe.hi + 0xFFFF) >> 16

    def low_half_place(la, lb, gh):
        """A plausible place whose address ends in la in A's binary, in lb in
        B's and in gh (the GBA's own half there) on the GBA, or None. Plausible:
        a function's start, or a place in a game data object that its declared
        type allows (its start, an element's or a member's). Any object of 64
        KiB or more ends in every low half, so A and B alone prove nothing; the
        GBA's half is the independent check."""
        if gh is None:
            return None
        for s in a.exe.symbols:
            if not (a.exe.lo <= s.value < a.exe.hi) or s.type not in (1, 2):
                continue
            if s.type == 1 and not objmap_a.get(s, "?").startswith("src/"):
                continue
            off = (la - s.value) & 0xFFFF
            if off >= max(s.size, 1) or (s.type == 2 and off):
                continue
            ga = gba.addr_of(s)
            if ga is None:
                continue
            n = matcher.find(s, want_func=s.type == 2)[0]
            if n is None:
                continue
            while off < min(max(s.size, 1), max(n.size, 1)):
                gl = (ga[0] + off) & 0xFFFF
                if (n.value + off) & 0xFFFF == lb and (gh == gl or (s.type == 2 and gh == gl ^ 1)) \
                        and (s.type == 2 or types_a.pointable(s.value, off, 1)):
                    return f"{s.key}+0x{off:X}"
                off += 0x10000
        return None

    def explain(name, addr, va, vb):
        """The differences that a relink cannot remove, by what they are (the
        GBA's word at the same place, one step older, is the evidence for a
        low half)."""
        if name == "IO" and 0x040000B0 <= addr < 0x040000E0 and (addr - 0x040000B0) % 12 in (0, 4):
            return (f"the last DMA's source or destination address (A: {what_host(va)}, B: {what_host(vb)}; a host "
                    "address there is a buffer of that process) in a write-only DMA address register that nothing "
                    "reads (port/hw/dma.c)")
        if not (proc and proc.value <= addr < proc.value + PROC_SIZE * PROC_COUNT
                and (addr - proc.value) % PROC_SIZE >= 0x2C):
            return None
        # A proc's user area (typed as the proc's own struct, which the relink
        # cannot see): 16-bit fields that hold half of a host address. The
        # relink rewrites whole 32-bit host addresses only; a half is just a
        # 16-bit number to it, so neither half can be carried to another binary.
        for shift in (16, 0):
            keep = 16 - shift
            if (va >> keep) & 0xFFFF != (vb >> keep) & 0xFFFF:
                continue  # the other half must be the same
            ha, hb = (va >> shift) & 0xFFFF, (vb >> shift) & 0xFFFF
            if ha == hb:
                continue
            if image_high(ha, a.exe) and image_high(hb, b.exe):
                return ("the high 16 bits of a host address beside a 16-bit field in a proc's user area (not a "
                        "whole pointer: the other half is the field; on the same path the GBA keeps the high half "
                        "of its own address there)")
            g = gba_word(addr)
            place = low_half_place(ha, hb, None if g is None else (g >> shift) & 0xFFFF)
            if place:
                return (f"the low 16 bits of a host address ({place} in both binaries) in a 16-bit field of a "
                        f"proc's user area (the GBA holds the low half of its own address of {place} there): only "
                        "a half, so no relink can carry it")
        return None
    bad, explained = 0, 0
    # GBA memory
    for name, base, size in REGIONS:
        ra, rb, rr = a.regions[name], b.regions[name], by_addr[base]
        diff_ab = sum(1 for i in range(0, size, 4) if ra[i:i + 4] != rb[i:i + 4])
        wrong = [i for i in range(0, size, 4) if rr[i:i + 4] != rb[i:i + 4]]
        print(f"audit: {name:5s}: {diff_ab} words differ between A and B; relinked A differs from B in {len(wrong)}")
        for i in wrong:
            va, vb, vr = (struct.unpack_from("<I", x, i)[0] for x in (ra, rb, rr))
            kind = "missed pointer" if va == vr else ("false rewrite" if va == vb else "wrong rewrite")
            why = explain(name, base + i, va, vb) if va == vr else None
            g = gba_word(base + i)
            gtxt = f", GBA 0x{g:08X}" if g is not None else ""
            if why:
                explained += 1
                print(f"   0x{base + i:08X} A 0x{va:08X} B 0x{vb:08X}{gtxt}: not relinkable: {why}")
            else:
                bad += 1
                print(f"   0x{base + i:08X} A 0x{va:08X} B 0x{vb:08X} relinked 0x{vr:08X}{gtxt}: {kind}  <-- UNEXPLAINED")
    # host globals restored
    entries_b = {s.key: s for s, _, cls, _ in plan_host(b) if cls not in (SESSION,)}
    host_wrong = 0
    for s, obj, cls, _ in plan_host(a):
        if cls == SESSION:
            continue
        n = matcher.find(s)[0]
        if n is None or n.value not in by_addr:
            continue
        rb = b.host_bytes(n.value, n.size)
        rr = by_addr[n.value]
        if rr != rb:
            offs = [i for i in range(len(rb)) if rr[i] != rb[i]]
            if cls == CODE:
                # Code addresses kept only to log each site once: relinked as the same function + offset.
                # Explained when every differing word is inside the same function in both binaries.
                relb = Relinker(b.exe, b.exe, {}, None)
                same_fn = True
                for i in range(0, len(rb) - 3, 4):
                    wr, wb = struct.unpack_from("<I", rr, i)[0], struct.unpack_from("<I", rb, i)[0]
                    if wr != wb:
                        fr, fb = relb.resolve_code(wr)[1], relb.resolve_code(wb)[1]
                        same_fn &= fr.split("+")[0] == fb.split("+")[0] and fr.startswith("code ")
                        print(f"   host {s.key}+0x{i:X}: relinked {fr}, B {fb}")
                if same_fn:
                    explained += 1
                    print(f"   host {s.key} ({obj}, {cls}): not relinkable exactly: a code address inside the same "
                          "function, whose code the other build laid out differently; kept only to log each site "
                          "once, so a resumed run may log a site once more")
                    continue
            host_wrong += 1
            print(f"   host {s.key} ({obj}, {cls}): relinked A differs from B at {len(offs)} bytes, first +0x{offs[0]:X}"
                  "  <-- UNEXPLAINED")
    bad += host_wrong
    print(f"audit: host globals: {host_wrong} restored symbols differ from B ({len(entries_b)} restored in B)")
    print("audit: " + (f"PASS: relinking A for B's binary gives B's memory, apart from {explained} items no relink "
                       "can carry (listed above with what they are)" if bad == 0
                       else f"FAIL: {bad} unexplained differences ({explained} explained)"))
    return 0 if bad == 0 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("relink", help="make patch.bin of a PC snapshot part for a binary")
    p.add_argument("--part", required=True)
    p.add_argument("--exe", required=True)
    p.add_argument("--force", action="store_true", help="remake the patch even if one is there for this binary")
    p.add_argument("--gba-elf", help="the GBA build's ELF for the GBA check of the strict scan (default: "
                                     "fireemblem8.elf here, if it exists)")
    p.set_defaults(fn=cmd_relink)
    p = sub.add_parser("check", help="check that the parts of a snapshot tick belong together")
    p.add_argument("tickdir")
    p.set_defaults(fn=cmd_check)
    p = sub.add_parser("classify", help="classify a binary's host globals (fails on unclassified ones)")
    p.add_argument("--exe", required=True)
    p.add_argument("--map")
    p.add_argument("--verbose", action="store_true")
    p.set_defaults(fn=cmd_classify)
    p = sub.add_parser("audit", help="relink snapshot A for B's binary and compare with B")
    p.add_argument("--a", required=True)
    p.add_argument("--b", required=True)
    p.add_argument("--gba-elf", help="the GBA build's ELF (default: fireemblem8.elf here, if it exists)")
    p.set_defaults(fn=cmd_audit)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
