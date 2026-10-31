# Difftest: the m4a mixer and sequencer against FE8's ARM code

`m4a_difftest.py` runs FE8's own `src/m4a_1.s` (from the GBA build) in
Unicorn and the C port `port/audio/m4a_1_pc.c` in `m4a_harness.c`. Both start
from the same memory, run one routine, and every byte they could write must
match. Nothing here is built into the game. Output goes to
`build/difftest/audio` (ignored by git). No ROM-derived file is kept in git.

Needs: the GBA `make` in this tree (`fireemblem8.elf`), `python3-unicorn`
(2.1.4), and `gcc-multilib` (the port's `-m32`).

```
python3 port/test/audio/m4a_difftest.py           # about 2 minutes
python3 port/test/audio/m4a_difftest.py --quick   # about 20 seconds
python3 port/test/audio/mutate_m4a.py             # about 3 minutes; checks the test itself
```

## How it works

- **ARM side** (Unicorn, ARM926 model). It loads the ROM segment of
  `fireemblem8.elf` at 0x08000000, and maps EWRAM, IWRAM, the I/O page and
  the area below 0x02000000 (so `chk_adr_r2` can read there first, as the
  asm does). It runs `m4aSoundInit` itself. That copies `SoundMainRAM` to
  `SoundMainRAM_Buffer`, and the mixer then runs from IWRAM as on the GBA.
  `CpuSet` and `CpuFastSet` (SWI 0x0B, 0x0C) are done in Python. VCOUNT
  advances while `SampleFreqSet` spins on it.
- **C side** (`m4a_harness.c`, a 32-bit host process). It links
  `m4a_1_pc.o` (built with `port/Makefile`'s `PORT_CFLAGS`) with
  `src/m4a.c` and `src/m4a_tables.c` (built with `HOST_CFLAGS` and
  `HOST_CPPFLAGS`, plus `-Dasm(x)=((void)0)`, as audio-design.md 1.6
  plans). GBA memory sits at GBA addresses (D1-a). The ROM copy is read-only,
  so a stray write faults. `gSoundInfo`, `gMPlayJumpTable`, `gCgbChans`,
  `gMPlayMemAccArea`, `SoundMainRAM_Buffer`, `SoundMainRAM`, `gMPlayTable`
  and `gSongTable` are bound to their GBA addresses with `--defsym`.
- **Code pointers.** The SoundInfo callbacks, `MusicPlayerInfo.func` and the
  36 `gMPlayJumpTable` entries hold GBA addresses on one side and host
  addresses on the other. They are translated by function name on load and
  before the compare.
- **Compare.** After every call: all of IWRAM except the ARM stack window
  (0x03007B00-0x03007EFF), the I/O bytes 0x04000000-0x040003FF, a CRC of
  EWRAM, and the return value where there is one. A write hook also lists
  every ARM write outside the driver image of audio-design.md 2.3.
- **Pre-states.** The ARM driver makes them: `m4aSoundInit`, then 9
  scenarios of 1,200 VBlanks each. Each VBlank runs `m4aSoundVSync`, then
  `MPlayMain` alone on the chain head (compared, then undone), then
  `SoundMain`. Scenarios: song 1 (opening, reverb), song 9 with SEs, song
  0x43 with the PSG system SEs 0x65-0x6B, and 6 random mixes of BGMs, SEs
  and the m4a API (fades, stop, continue, tempo, volume, pitch, pan, LFO,
  start-or-change). Every API call is compared too. Fuzzed states start from
  states taken every 7th VBlank.
- **maxLines (5e).** `SoundMain`'s `maxLines` check is dead in FE8
  (`gMaxLines` is 0), but it is tested. A second harness builds
  `m4a_1_pc.c` with `-DPORT_M4A_TEST_VCOUNT`, so VCOUNT advances per read on
  both sides by the same rule. The ARM side sets the register right after
  each VCOUNT load.

## Cases (seed 8, full run)

| Step (audio-design 2.3) | Section | Cases | What |
|---|---|---|---|
| 1 | `1-m4aSoundInit` | 1 | Driver image after `m4aSoundInit` (C `src/m4a.c` + port vs ARM) |
| 1 | `1-umul3232H32` | 100,000 | Random and edge operands |
| 1 | `1-ChnVolSetAsm` | 100,000 | Random velocity, rhythm pan, volMR, volML; DirectSound and PSG channel |
| 1 | `1-chk_adr_r2`, `1-ldrb_r3_r2`, `1-ld_r3_tp_adr_i`, `..._unchecked` | 11,984 each (6,821 unchecked) | Addresses in BIOS, 0x4000-0x01FFFFFF, EWRAM, IWRAM, ROM, edges and the template |
| 1 | `1-MPlayJumpTableCopy` | 9 | Into `gMPlayJumpTable` and 8 IWRAM places |
| 1 | `1-SoundMainBTM` | 1,000 | Random aligned addresses |
| 1 | `1-RealClearChain`, `1-TrackStop`, `1-clear_modM`, `1-chain-cmds` | 150 each | Random chains over the 12 DirectSound and 4 PSG channels, random sound registers; `ply_fine` and `ply_endtie` on them |
| 2 | `2-vsync` | 10,800 | Every VBlank of the scenarios |
| 3 | `3-soundmain-idle`, `3-soundmain-idle-fuzz` | 150 + 500 | No channel on: reverb 0/20/random/255, DMA counter 0-8, random PCM ring |
| 4 | `4-mplaymain` | 10,800 | `MPlayMain` on the chain head, every VBlank |
| 4 | `4-handlers` | 6,000 | The 20 command handlers, 300 each, random track bytes and arguments (EWRAM or ROM) |
| 4 | `4-ply_note` | 3,000 | Real voices (plain, rhythm, key split, PSG), random keys, priorities and channel states |
| 5 | `5-soundmain` | 10,650 | `SoundMain` on scenario VBlanks with a channel on |
| 5a-5d | `5-soundmain-fuzz` | 2,000 | Envelope phases; fixed-rate; interpolated; near the sample end; random reverb, counter, master volume, PCM ring |
| 5e | `5e-maxLines` | 500 | `maxLines` 1-39 with VCOUNT moving |
| - | `api` | 569 | m4a API calls of the scenarios |

Result on 2026-10-19 (`port-audio`): **289,353 cases, 0 failures; no ARM
write outside the driver image.** Every listed ARM path was taken (the run
prints the hit counts): reverb on and off, the deadline exit, each envelope
phase, the fixed-rate fast and per-sample loops, sample ends, loop wraps,
ends in mid-word, LFO steps, `TrackStop`'s `CgbOscOff`, and each `ply_note`
branch.

`mutate_m4a.py` runs the difftest (`--quick --frames 60`) on 16 altered
copies of `m4a_1_pc.c`. The source file is never edited. The 15 that change
behaviour must fail, and the one equivalent mutant (a different fast-path
threshold in the fixed-rate mixer) must pass. Result on 2026-10-19: all 16
as expected.

## Limits

- I/O is plain memory on both sides. Hardware read-back of write-only
  registers is the PSG and I/O model's business, not this test's.
- The states come from the ARM driver in Unicorn, not from mGBA captures.
  The mGBA side is `m4a_replay.py` (the same C driver replayed on mGBA
  records, with the game's own m4a calls from the capture's call record) and
  its own mutation check `mutate_replay.py`; both are described in
  `port/test/README.md`, "Sound". `build_harness` takes an altered `src/m4a.c`
  (`m4a_source`) for that check.
- Not reachable from FE8 data, so not fuzzed: `maxChans` above 12, PSG
  voices naming channels 5-7, a data byte with no running status. The C
  does what the ARM does for the first two. For the third see
  `m4a_1_pc.c`'s header.
