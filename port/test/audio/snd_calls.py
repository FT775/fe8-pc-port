#!/usr/bin/env python3
"""The m4a sound calls the GBA capture records (port/test/oracle/sound_capture.lua, SND_CALLS).

    python3 port/test/audio/snd_calls.py setup fireemblem8.elf fireemblem8.map
        prints two words: the MGBA_RUN_BP list and SND_M4A_CODE (port/test/Makefile snd-gba)
    python3 port/test/audio/snd_calls.py setup-gate0 fireemblem8.elf fireemblem8.map GBA_BP
        the same for gate0_capture.lua (port/test/Makefile snd-route): GBA_BP with the m4a
        breakpoints appended, without snd_wait (that capture samples ticks at gate0_wait),
        and sndpos_trackstop; SND_M4A_CODE; and SND_MPLAY (three words). No two
        breakpoints may share an address (the runner reports one name per address)
    python3 port/test/audio/snd_calls.py show build/gate0/snd/title.gba.calls
        counts per function, clean and not

WATCHED is every entry point of src/m4a.c and src/m4a_1.s that code outside the
driver calls, or could call: the VBlank handler's two calls (bm.c OnVBlank,
sioerror.c), the game's calls (src/soundwrapper.c, bmlib.c, banim-efxsound.c,
bmdifficulty.c, mapanim_expbar.c, sio_battlemap.c, sio_postbattle.c, main.c:
found with grep over src/ and asm/, 2026-10-19), and the rest of the public m4a
API, which the game does not call today. Internal routines that only the
driver calls (SoundMain, MPlayMain, TrackStop, the ply_ handlers, FadeOutBody,
CgbSound ...) are not watched; a call to one of them from outside the driver
would show as a difference the replay cannot explain. Nothing outside the
driver writes the driver's memory directly (same grep; uiarena.c and
soundroom.c only read).
"""

import os
import struct
import sys

WATCHED = [
    # VBlank handler (bm.c OnVBlank; sioerror.c)
    'm4aSoundVSync', 'm4aSoundMain',
    # called by the game
    'm4aSoundInit', 'm4aSongNumStart', 'm4aSongNumStop', 'MPlayStart', 'm4aSoundMode',
    'm4aMPlayStop', 'm4aMPlayImmInit', 'm4aMPlayVolumeControl', 'm4aMPlayPanpotControl',
    'm4aMPlayFadeOut', 'm4aMPlayFadeOutTemporarily', 'm4aMPlayFadeIn',
    # the rest of the public API (not called by FE8 today)
    'm4aSongNumStartOrChange', 'm4aSongNumStartOrContinue', 'm4aSongNumContinue',
    'm4aMPlayAllStop', 'm4aMPlayContinue', 'm4aMPlayAllContinue', 'm4aMPlayTempoControl',
    'm4aMPlayPitchControl', 'm4aMPlayModDepthSet', 'm4aMPlayLFOSpeedSet', 'MPlayContinue',
    'MPlayFadeOut', 'MPlayOpen', 'SoundInit', 'SoundClear', 'm4aSoundVSyncOff', 'm4aSoundVSyncOn',
    'SampleFreqSet', 'MPlayExtender',
]

# Driver code: the .text of these objects (fireemblem8.map) and the IWRAM copy
# of SoundMainRAM, which SoundMain calls.
DRIVER_OBJECTS = ('src/m4a_1.o', 'src/m4a.o')
SOUNDMAINRAM_BUFFER_SIZE = 0x400


def elf_syms(path):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import m4a_difftest as dt  # noqa: E402
    return dt.read_elf(path)[1]


def driver_ranges(syms, map_path):
    ranges = []
    with open(map_path) as f:
        for line in f:
            parts = line.split()
            if len(parts) == 4 and parts[0] == '.text' and parts[3] in DRIVER_OBJECTS:
                lo, size = int(parts[1], 16), int(parts[2], 16)
                if size:
                    ranges.append((lo, lo + size))
    if len(ranges) != len(DRIVER_OBJECTS):
        sys.exit(f'{map_path}: .text of {DRIVER_OBJECTS} not found (got {ranges})')
    buf = syms['SoundMainRAM_Buffer'][0]
    ranges.append((buf, buf + SOUNDMAINRAM_BUFFER_SIZE))
    return ranges


def setup(elf, map_path):
    syms = elf_syms(elf)
    ranges = driver_ranges(syms, map_path)
    for name in WATCHED:
        a = syms[name][0] & ~1
        if not any(lo <= a < hi for lo, hi in ranges):
            sys.exit(f'{name} at 0x{a:08X} is outside the driver code {ranges}')
    bp = [f'snd_{n}@0x{syms[n][0] & ~1:08X}:ret' for n in WATCHED]
    bp.append(f'snd_wait@0x{syms["VBlankIntrWait"][0] & ~1:08X}')
    code = ','.join(f'0x{lo:08X}-0x{hi:08X}' for lo, hi in ranges)
    return ','.join(bp), code


def setup_gate0(elf, map_path, gate0_bp):
    """GBA_BP plus the m4a breakpoints (no snd_wait) and sndpos_trackstop (TrackStop's
    entry, for the sound positions inside an m4a call), SND_M4A_CODE, and SND_MPLAY
    (gMPlayTable:gNumMusicPlayers)."""
    bp, code = setup(elf, map_path)
    syms = elf_syms(elf)
    snd = [b for b in bp.split(',') if not b.startswith('snd_wait@')]
    snd.append(f'sndpos_trackstop@0x{syms["TrackStop"][0] & ~1:08X}')
    items = [b for b in gate0_bp.split(',') if b] + snd
    seen = {}
    for b in items:
        name, rest = b.split('@', 1)
        addr = int(rest.split(':')[0], 16)
        if addr in seen:
            sys.exit(f'breakpoints {seen[addr]} and {name} share 0x{addr:08X}: the runner reports one name per address')
        seen[addr] = name
    if len(items) > 64:
        sys.exit(f'{len(items)} breakpoints: the runner takes 64 (oracle/mgba_lua_run.c MAX_BREAKPOINTS)')
    mplay = f'0x{syms["gMPlayTable"][0]:08X}:{syms["gNumMusicPlayers"][0]}'
    return ','.join(items), code, mplay


class Call:
    __slots__ = ('seq', 'w_in', 'w_out', 'clean', 'name', 'args', 'lr')

    def __init__(self, line):
        p = line.split()
        self.seq, self.w_in, self.w_out, clean = int(p[0]), int(p[1]), int(p[2]), int(p[3])
        self.clean = clean == 1
        self.name = p[4]
        self.args = [int(x, 16) for x in p[5:9]]
        self.lr = int(p[9], 16)

    def spans(self, w, last):
        """True if the call was open during window w (last: the last window of the capture)."""
        end = last if self.w_out < 0 else self.w_out
        return self.w_in <= w <= end


def load(path, syms=None, info=None):
    """(calls sorted by entry, {name: address} watched, (records, from)) from a .calls
    file. With syms, the watched addresses must be this ELF's. With info (a dict),
    a capture resumed from a save state (gate0_capture.lua) sets info['resumed'] to
    (tick, first record v)."""
    calls, watch, end = [], {}, None
    with open(path) as f:
        first = f.readline()
        if not first.startswith('# fe8-sndcalls-1'):
            sys.exit(f'{path}: not a fe8-sndcalls-1 file')
        for line in f:
            if line.startswith('# watch '):
                _, _, name, addr = line.split()
                watch[name] = int(addr, 16)
            elif line.startswith('# resumed tick '):
                p = line.split()
                if info is not None:
                    info['resumed'] = (int(p[3]), int(p[5]))
            elif line.startswith('# end records '):
                p = line.split()
                end = (int(p[3]), int(p[5]))
            elif line.startswith('#') or not line.strip():
                continue
            else:
                calls.append(Call(line))
    if end is None:
        sys.exit(f'{path}: no "# end" line (the capture did not finish)')
    if syms is not None:
        for name, addr in watch.items():
            if name not in syms or syms[name][0] & ~1 != addr:
                sys.exit(f'{path}: {name} watched at 0x{addr:08X}, the ELF has it elsewhere; recapture')
    calls.sort(key=lambda c: c.seq)
    return calls, watch, end


def show(path):
    calls, watch, _ = load(path)
    counts = {}
    for c in calls:
        k = counts.setdefault(c.name, [0, 0])
        k[0 if c.clean else 1] += 1
    print(f'{path}: {len(watch)} functions watched, {len(calls)} top-level calls')
    for name, (cl, un) in sorted(counts.items(), key=lambda x: -sum(x[1])):
        print(f'  {name:28s} {cl:7d} clean {un:5d} not clean')
    return 0


def main():
    if len(sys.argv) == 4 and sys.argv[1] == 'setup':
        bp, code = setup(sys.argv[2], sys.argv[3])
        print(bp, code)
        return 0
    if len(sys.argv) == 5 and sys.argv[1] == 'setup-gate0':
        print(*setup_gate0(sys.argv[2], sys.argv[3], sys.argv[4]))
        return 0
    if len(sys.argv) == 3 and sys.argv[1] == 'show':
        return show(sys.argv[2])
    sys.exit(__doc__)


if __name__ == '__main__':
    sys.exit(main())
