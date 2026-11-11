# Gate 0 harness: GBA oracle and PC trace comparison

To run every gate of the port at once (GBA build, PC builds, unit tests, sound, Gate 0, routes): `make -f port/check-all.mk` (add `CHECK_LONG=1` for the long ones; the rows are listed in `port/check-all.mk`).

port-plan/PLAN.md section 4, Gate 0. Every phase checkpoint compares the PC
build against the decomp's `fireemblem8.gba` running in mGBA. This directory
holds that harness. Nothing here is compiled into the game, and no ROM or
ROM-derived file is kept in git: the harness reads `fireemblem8.gba` and
`baserom.gba` from the build tree and writes only to `build/gate0/`.

## Tools (WSL Ubuntu 26.04, installed 2026-10-19)

Installed with apt from the Ubuntu archive (`resolute/universe`):

| Package | Version | Why |
|---|---|---|
| `mgba-qt` | 0.10.5+dfsg-3build1 | mGBA frontend (GUI). Not used headless: it has no `--script` option |
| `mgba-sdl` | 0.10.5+dfsg-3build1 | mGBA SDL frontend. Also has no `--script` option |
| `libmgba0.10t64` | 0.10.5+dfsg-3build1 | The emulator core. Built with `ENABLE_SCRIPTING`, `USE_LUA`, `USE_DEBUGGERS` (`/usr/include/mgba/flags.h`) |
| `libmgba-dev` | 0.10.5+dfsg-3build1 | Headers for the runner below |
| `liblua5.4-0` | 5.4.8-1build1 | Lua used by libmgba (pulled in as a dependency) |
| `python3-unicorn` | 2.1.4-3 | Unicorn 2.1.4 for the later audio step tests (`import unicorn` works with the system `python3` 3.14.4) |

Host compiler: gcc 15.2.0 (Ubuntu 15.2.0-16ubuntu1). No pip packages were needed.

## Headless Lua: `oracle/mgba_lua_run.c`

Neither packaged frontend can load a Lua script from the command line or run
without a window. The packaged library can. `oracle/mgba_lua_run.c` (about 320
lines, built against `libmgba-dev` only) drives it:

```
build/gate0/mgba_lua_run ROM SCRIPT.lua MAX_FRAMES
```

- Loads the ROM with mGBA's built-in HLE BIOS and default settings. The user's
  mGBA config is never read, so no colour correction, BIOS file or save path
  from there applies.
- Creates a Lua scripting context with `emu`, `callbacks`, `console` and the
  standard Lua libraries, loads SCRIPT and runs it.
- Runs at most MAX_FRAMES frames. After each `runFrame` it fires the scripting
  `"frame"` callback once (checked: one call per frame, `emu:currentFrame()`
  equals the call count). At that point the frame has just ended at line 160,
  and the CPU has not yet taken the VBlank IRQ: `OnVBlank` has not run.
- The script ends the run with `os.exit()`. Exit status 3 means the frame
  limit was hit first.
- Breakpoints (optional): `MGBA_RUN_BP="name@0xADDR[,name@0xADDR...]"` (at most
  64). The runner attaches a custom libmgba debugger (`mDebuggerAttach`, no CLI
  or GDB stub) and sets a hardware breakpoint at each ADDR. Each time the CPU is
  about to run the instruction at ADDR, the runner fires the scripting callback
  `name` (`callbacks:add("name", fn)`), so the script reads memory before that
  instruction runs. With breakpoints the runner steps the core one instruction
  at a time, as `mDebuggerRun` does, instead of `runFrame`; `"frame"` still
  fires once per frame, when the frame counter moves. mGBA 0.10's Lua has no
  breakpoints of its own; the library's debugger does.
- Breakpoint cost (2026-10-21, review rv-h8). In libmgba 0.10.5,
  `mDebuggerRun` runs one instruction (`core->step`) and then the ARM
  platform's `checkBreakpoints`, which walks the whole breakpoint list, with
  two library calls per breakpoint, on every instruction. With `gba-trace`'s
  13 breakpoints that walk was about 44% of the capture's time (perf). The
  runner's `RunnerStep` makes the same `core->step` call and calls the
  library's own `checkBreakpoints` only when the next instruction's address
  (PC less 4 in ARM state, 2 in Thumb, as the library takes it) is a
  breakpoint's, found in a small hash table. Anywhere else that check finds
  nothing: no breakpoint has a condition, and the runner refuses to start if
  the debugger has watchpoints or a stack trace. Entering the debugger at a
  hit is still the library's (`mDebuggerEnter`: the debugger pauses and
  `cpu->nextEvent` is set to `cpu->cycles`), and any state other than running
  still goes through `mDebuggerRun` (after a hit, that step runs no
  instruction, as before). So a breakpoint costs its hits (the script
  callbacks), not every instruction. The raster log (below) skips the copy of
  watched halfwords for a store whose canonical range touches no watched
  region, which was most of the rest of its cost. Checked (2026-10-21): the
  runner before this change (`88d742e8`) and this one, run one after the
  other with the same paths, write byte-identical outputs (trace, raw dumps,
  PNGs, raster log, the per-breakpoint counts, and an mGBA save state) for
  the scripted 60,000 ticks, route ch5 (135,505 ticks) and route ch1, the
  sound capture `SND=title` (34 breakpoints with returns), a save state made
  at tick 3,000, the ready-made route ch5 photo at 20,000 resumed for 1,200
  ticks, and the TAS frame mode (14 breakpoints, ticks 0-2,999). Times, one
  run each: the scripted 60,000 ticks' GBA capture 69.7 s (before: 171.9 s;
  `port-tas2`, eight breakpoints and no raster log: 127.5 s); route ch1 26.1 s
  (65.2 s; 47.4 s); route ch5 175 s (441 s, while another capture ran beside
  it); `SND=title` 2.1 s (12.3 s).
- An IRQ taken at a breakpoint address, before its instruction runs, returns
  to that instruction, and the debugger reports it again. The runner counts
  that second report as the same call (an IRQ shows as the step that puts the
  CPU into IRQ mode) and does not pass it on. It knows the second report by
  the stack pointer and CPU mode of the interrupted code, kept at the IRQ
  step: FE8's `IrqMain` (`crt0.s`) runs the handler in System mode on the main
  loop's stack, so a call the handler makes to the same address (`OnVBlank`'s
  `Proc_Run` of tree 0 reaches `RunProcessRecursive` and `RunProcessScript`)
  comes lower on that stack, and is passed on as a call of its own. (Until
  2026-10-20 the runner took the first report at that address after the IRQ
  for the repeat; section "Lag position" has what that did.) At exit it prints, per
  breakpoint, the calls passed on and the repeated reports skipped. Scripted
  route: 63 repeats each at `OnMain` and `VBlankIntrWait`, none at `OnVBlank`;
  without this step the capture saw two samples in one frame.
- Around such an IRQ the runner also fires `"<name>_irq"` (at the step that
  enters IRQ mode) and `"<name>_resume"` (at the skipped second report, when
  the CPU is back at the instruction). A script that registers them knows the
  call it was told about had not run its instruction while the IRQ ran; the
  capture uses this for the lag position of its two entry breakpoints.
- Returns (optional): `name@0xADDR:ret` also fires `name_ret` when that call
  returns. At the breakpoint the runner keeps the caller's return address (LR)
  and stack pointer; the call has returned when the next instruction is at that
  address with that stack pointer. An IRQ handler that runs the same code does
  so lower on the stack, and calls return in reverse order of entry, also
  across IRQs, so one stack of open calls is enough. The sound capture uses
  this ("Sound" below); the exit statistics then list returns too.
- Save states (optional, section "Save states"): `MGBA_RUN_STATE_IN=FILE`
  loads an mGBA state (SRAM included) right after reset, before the first
  instruction; the script sees its path as `runner_state_loaded`. The script
  saves one with `runner_save_state(FILE)` (refused while an
  IRQ-at-breakpoint report is pending or a `:ret` call is open: that runner
  state is not in the file). `runner_rom_crc32` is the CRC-32 of the ROM file.
- `runner_cycles()` (section "TAS route") returns the CPU cycles since reset
  (`mTimingGlobalTime`); at a breakpoint, the time before that instruction.
- `runner_irq_sp()` returns the IRQ mode's stack pointer (banked while the CPU
  runs in another mode): `__sp_irq` when no IRQ frame is open, which tells
  the main loop's BIOS calls from the handlers' (section "Lag position").
- `runner_irq_reg(sp, k)` (2026-10-21, cov9 H-col-2) returns `r<k>` (4 to 14;
  13 and 14 the interrupted mode's banked `sp` and `lr`) of the code that an
  IRQ interrupted, as it was when the CPU entered IRQ mode with the IRQ stack
  pointer at `sp` (`__sp_irq` for the IRQ that interrupted the main loop), or
  -1 when no IRQ entered there is open. The BIOS vector and `IrqMain` push
  only `r0`-`r3`, `r12` and the return address; `r4`-`r11` stay live, so once
  an IRQ nests inside a handler (`IrqMain` runs handlers in System mode with
  IRQs on) they are the outer handler's. The run loop keeps them at each IRQ
  entry; an entry is dropped when a later IRQ enters at or above its stack
  pointer. Not in a save state: snapshots are made while the main loop waits
  in `VBlankIntrWait`, where no open IRQ interrupted the main loop's work.
- Raster log (optional, section "Raster-timing frames"):
  `MGBA_RUN_RASTERLOG=FILE` with `MGBA_RUN_RASTER_RAM` and
  `MGBA_RUN_RASTER_IRQ` wraps the CPU's store functions and the renderer's
  `drawScanline`, and notes each line handler call at `IrqMain`'s `bx r0`;
  `runner_raster_write(tick)`, called in a `"frame"` callback, appends the
  frame that just ended to FILE and returns the block's CRC-32. The format is
  in `oracle/mgba_lua_run.c`.
- Checked: with the three Gate 0 breakpoints, the `boot300` capture equals the
  old `runFrame` capture byte for byte in every old field (including
  `gBmSt.prevVCount` and the raw buffers), so the stepping does not change the
  emulated machine. The scripted route's 16,000 ticks take 25 seconds.

mGBA 0.10 note: `mScriptContextLoadFile` only compiles a script. The runner
calls the Lua engine's `load` and `run` itself, as the Qt frontend does.

Build: `make -f port/test/Makefile oracle`.

## Boot check (stage 2)

`make -f port/test/Makefile boot-check` runs 400 frames of `fireemblem8.gba`
(SHA-1 `c25b145e37456171ada4b0d440bf88a19f4d509f`, built in this tree) with no
keys and saves `build/gate0/boot.png`. Result on 2026-10-19: `gGameClock=386`,
`gMainCallback=0x080152F5` (`OnMain`), `DISPCNT=0x0300`, and the screenshot
shows the health and safety screen.

## PPU test ROM

`make -f port/test/Makefile pputest` asks the oracle itself how it draws a few
cases the PC renderer must draw the same way. `oracle/pputest/` (C and a
10-line `crt0.s`, no FE8 code, nothing ROM-derived) builds with the host's
`arm-none-eabi-gcc` into `build/gate0/pputest/pputest0.gba` to `pputest2.gba`
(never committed), one still screen each. The runner shoots frame 30
(`oracle/pputest.lua`) and `tools/pputest_check.py` checks the listed pixels
against the tables it holds, what mGBA 0.10.5 drew on 2026-10-21.
`port/render/test_obj.c` RT19 (variants 0 and 1) and `test_bg.c`'s "mGBA BG
clip" case (variant 2) draw the same scenes with the PC renderer and expect the
same pixels, so the renderer and the oracle are held to one table. Result
(2026-10-21): `PPU TEST ROM: PASS`, 33 of 33.

**Variants 0 and 1, OBJ against OBJ** (8x8 sprites over two BGs, 14 cases side
by side; variant 1 with the OBJ window on). What they show: sprites are
drawn in OAM order, and a colour sprite's **transparent** pixels are not a
no-op. Where an earlier entry left a pixel whose priority value is higher than
the later entry's, the pixel keeps its colour but takes the later entry's
priority and its semi-transparent mode (mGBA `software-obj.c`, the
`FLAG_UNWRITTEN` branch). So a later high-priority sprite's empty pixels lift
an earlier low-priority sprite in front of the BGs and of later sprites of the
same priority, and change whether it blends. OBJ-window sprites lift nothing;
equal priorities change nothing; clear pixels over nothing do nothing; affine
sprites lift too. Before 2026-10-21 the PC renderer skipped transparent pixels,
so FE8's preparations item screens showed the wrong sprite in a few pixels
(DIVERGENCES, cov9 group G8); `port/render/obj_line.c` `ObjPlot` now follows
the rule.

Limits of the rule, found by the round-9 review with more test scenes (not in
`pputest`): (1) with brighten or darken and OBJ as first target, mGBA gives a
lifted pixel its pre-effect colour or skips the effect (its variant palette in
`software-obj.c`), while the PC applies the effect when it composites, which
looks like the hardware but is not proven; 4 of 10 such cases differ from mGBA.
FE8 has such a blend (`prep_menuproc.c`, `SetBlendDarken(16)` with every first
target); no run has shown it, and a compare would report it, not hide it.
(2) The OBJ-window part (lifts nothing) is mGBA 0.10.5's; mGBA 0.11's change
log fixes "transparent OBJWIN pixel/sprite priority interaction" (mgba.io/i/2809),
so the hardware probably lifts there: an oracle choice, not a proven PPU rule.

**Variant 2, text BG tiles cut by window segments** (two BGs of horizontally
flipped tiles seen through 5-pixel windows). mGBA draws each BG once per
window segment of the line, and its code for a segment's first, partial tile
(`software-mode0.c`, `DRAW_BACKGROUND_MODE_0_TILE_SUFFIX_16`, the flip branch)
puts the tile's pixel 0 at the segment's last pixel. When a segment starts
inside a flipped tile and ends before that tile's last pixel, mGBA's pixels are
shifted by the pixels the segment leaves at the tile's end: through WIN0
[130, 135) a tile on 128-135 shows indices 5 4 3 2 1 where the hardware rule
gives 6 5 4 3 2. That is the oracle's own fault, not the hardware's (the PPU
draws a BG whatever the windows are), so the shipped renderer is unchanged;
Gate 0's PC runs set `FE8PC_MGBA_BGCLIP=1` (next to `FE8PC_BLEND=mgba8`, the
same kind of switch), which copies mGBA's cut in `port/render/bg_line.c`
(`RenderMgbaBgClip`, mGBA's `_breakWindow` segments) so a compare does not
report it. Seen in FE8 at the minimap window's tips (round 8's 85 "minimap"
frames; DIVERGENCES section 5). The switch copies the 16-colour case only:
mGBA's 256-colour suffix (`DRAW_BACKGROUND_MODE_0_TILE_SUFFIX_256`) has the same
flip-branch cut, which the switch leaves out (a review scene with a flipped
8bpp text BG through a narrow window differs by 320 pixels); harmless unless
FE8 shows one.

## Input file (stage 3)

A key file gives one 10-bit key mask per logic tick. Both the mGBA capture
and the PC hook replay it. Example: `inputs/boot300.keys`.

- One entry per line: the active-high key mask in hex, `000` to `3FF`. Bits
  follow `REG_KEYINPUT` (`io_reg.h:663-673`): A 0x001, B 0x002, Select 0x004,
  Start 0x008, Right 0x010, Left 0x020, Up 0x040, Down 0x080, R 0x100, L 0x200.
- `MASK*N` stands for N ticks of the same mask. `#` starts a comment. Blank
  lines are ignored. Entry k (counting repeats) is tick k, from 0. Lines may be
  of any length (the PC hook read them into a 256-byte buffer until
  2026-10-21, so a longer comment line stopped the PC at tick 0 with status 4
  as a bad entry: cov9 H-A).
- Ticks after the last entry have mask `000`.
- A logic tick is one completed `OnMain` (see Trace). The mask for tick t is in
  effect from the end of tick t-1 to the end of tick t, so `UpdateKeyStatus` in
  `OnMain` t reads it. The boot-time `UpdateKeyStatus` call (`main.c:53`),
  before tick 0, also reads tick 0's mask.

## Trace (stages 4 and 5)

`oracle/gate0_capture.lua` (GBA) and `pc/gate0_hook.c` (PC) write the same
JSON-lines format. The first line is a header (`"source"`, `"format":
"fe8-gate0-trace-4"`, `"keys"`, `"ticks"`; a PC trace made with the lag replay
also has `"lag_from"`, one made with the clock replay `"clock_from"`, one made
with the affine replay `"affine_from"`, one given the GBA's raster log
`"raster_from"`; a GBA trace captured with the raster log has `"raster_log"`,
the watched regions). Each further line is one logic tick
(an affine replay adds `affine` and `frame15_pc` on its `frame15` ticks,
section "Affine replay"; the raster keys are in the table and in section
"Raster-timing frames"). Format 3 (2026-10-19, branch `port-lagcheck`) added
`events` and `lag_at`; format 4 (2026-10-20, TAS route group 5) counts two more
kinds of event, the BIOS copies into video memory and `FlipBgVerticalScroll`
(section "Lag position"), so its `events` and `lag_at` mean something else;
the compare and the lag replay refuse older traces (exit 2). Every other
field is the same as in format 3.

| Key | Contents |
|---|---|
| `tick` | Logic tick index from 0 |
| `frame` | VBlanks since power-on, counting the VBlank this tick waits for |
| `vblanks` | VBlanks since the previous tick (1 = no lag; GBA lag frames show as 2 or more) |
| `keys` | The mask replayed for this tick |
| `gGameClock` (0x03000014), `gKeyStatusIgnoredSt` (0x03000010), `gKeyComboResetEN` (0x0300000C), `gSoftwareResetFlag` (0x0300001A), `gMainCallback` (0x02024CB8), `gLCGRNValue` (0x03000008) | Value in hex at the variable's width (4, 2, 1, 1, 4 and 4 bytes) |
| `gBmSt` (0x0202BCB0, 0x40 bytes), `gPlaySt` (0x0202BCF0, 0x4C bytes), `gRNSeeds` (0x03000000, 6 bytes), `sKeyStatusBuffer` (0x02024CC0, 0x14 bytes) | Raw bytes in hex |
| `gLCDControlBuffer` (0x03003080, 0x69 bytes), `gPaletteBuffer` (0x020228A8, 0x400), `gBG0TilemapBuffer` to `gBG3TilemapBuffer` (0x02022CA8, 0x020234A8, 0x02023CA8, 0x020244A8, 0x800 each), `gOam` (0x03003140, 0x400) | FNV-1a 32 of the bytes, hex |
| `sTrapPool` (0x0203A614, 0x200 bytes; since integration 11) | FNV-1a 32 of the bytes, hex, with no raw dump (not in `GATE0_RAW`'s record): a difference names the field, not the byte. `bmtrap.c`'s file-static trap pool (traps, ballistae, map changes; it is in suspend data and drives `ApplyEnabledMapChanges`), traced since round 10's C10-10 (a) changed it with no other field showing it (`RemoveTrap`, DIVERGENCES D-72). The compare refuses a trace whose tick lines lack it (a capture or PC binary from before integration 11), except a resumed trace's prologue line from an older photo |
| `onmain_calls`, `vblank_waits`, `onvblank_calls` | Counters since power-on, as decimal numbers: entries to `OnMain`, calls to `VBlankIntrWait` (including the one this tick is sampled in) and runs of `OnVBlank`. GBA: debugger breakpoints at 0x080152F4, 0x080D16DC and 0x080152A4. PC: the port's `--wrap=OnMain` counter and the hook's `VBlankIntrWait` and `OnVBlank` wraps. Both sides take them at the sample, before the frame step |
| `events` | The tick's main-loop events since the previous sample (tick 0: since power-on), section "Lag position". Compared in RAW only |
| `lag_at` | Only on a tick with lag VBlanks: for each, in order, the number of the tick's main-loop events that came before it (section "Lag position"). GBA: where the lag VBlank came. Lag replay: where the PC ran it. Absent elsewhere |
| `early_flush` | Only on a tick whose last `lag_at` entry is an early flush VBlank (section "Early flush VBlank"): `1`. GBA: one came. Lag replay: the PC ran it. Absent elsewhere (a format-3 trace made before 2026-10-20 has none; its capture stopped at such a tick) |
| `lag_split` | Only on a tick with a lag VBlank that came inside a main-loop `CpuSet` or `CpuFastSet` into video memory (section "Lag position", kind 6): `[[i, stored, loaded], ...]`, lag VBlank `i` of `lag_at` and the bytes of that copy stored and loaded before it. GBA: from the interrupted registers. Lag replay: the split it ran. Compared with `lag_at` |
| `lag_in_other_copy` | GBA only: the lag VBlanks (indices into `lag_at`) that came inside a main-loop BIOS copy into other memory, which is no event; the lag replay runs them after the copy. Not compared (the compare lists the ticks) |
| `cheat_items`, `cheat_refills` | Only in a cheat-assisted trace (header `"cheat"`, `GATE0_CHEAT`; section "Test cheats"), on every tick line: the FNV-1a 32 of every unit's and the convoy's item bytes after the tick boundary's refill, hex, and how many item words that refill changed, decimal. Compared on every tick like the fields above; absent without the cheat |
| `raster` | GBA: on a `frame15` tick of a capture with the raster log, the CRC-32 of that frame's block in it. PC: on a `frame15` tick of a run given `GATE0_RASTER_FROM`, the redraw's mark (section "Raster-timing frames"), with `raster_rec` (the block's CRC-32), `frame15_raster` (the hash of the frame drawn with the GBA's timing) on `"gba"`, and `raster_note` on some other marks. Not compared as fields |
| `frame15` | Every 60 ticks (tick % 60 == 0). GBA: `"png:PATH"`, a screenshot of the frame this tick waits for; the compare script hashes it in 15-bit space (FNV-1a 32 over the 240x160 pixels as little-endian `r5 \| g5<<5 \| b5<<10`, the top 5 bits of each 8-bit channel). PC: that hash of the renderer's frame drawn while the tick waits (lines 0-159 of the frame step after the sample), which is the same frame. `null` on other ticks. `GATE0_SHOT_TICKS="a-b,c,..."` adds ticks on both sides; on PC, `GATE0_SHOTDIR` also writes each such frame as raw BGR555 (`tickNNNNNN.555`) for `tools/frame15.py` |

Addresses are from `fireemblem8.map`, `sym_iwram.txt` and `fireemblem8.elf`
of this commit. `gRNSeeds` and `gLCGRNValue` (`rng.c:4-5`),
`sKeyStatusBuffer` (`hardware.c:21`) and `sTrapPool` (`bmtrap.c`) are
file-static, so they are not in the map; their addresses come from the ELF
(`arm-none-eabi-nm`). The compare
script checks all twenty-five harness addresses (the four statics, the
thirteen breakpoints, as function plus offset, and the eight places of the
raster log and `lag_split`: `IrqMain`'s `bx r0` in ROM and in
`IntrMain_Buffer`, `__sp_irq`, the minimap window's buffers and pointers and
`gBmSt.altBlendACa`) against `fireemblem8.elf`, and the statics and the five
watched RAM places against the PC binary, and stops with status 2 if any has
moved. With `GATE0_RAW=PATH` both
sides also write the bytes of the hashed buffers, in table order, 0x2869 bytes
per tick, so the compare script can name the first differing byte.

**SRAM.** With `GATE0_SRAM=PATH` both sides write the 64 KiB at `0x0E000000`
after the last tick: mGBA through `emu:readRange`, the PC from its SRAM image
(D1-a keeps it at the GBA address). FE8's SRAM is 32 KiB (`WipeSram` clears
0x8000 bytes, and every save block lies below that). mGBA mirrors it in the
upper 32 KiB; the PC leaves that half at `0xFF`. So compare the first 32 KiB
(`port-plan/DIVERGENCES.md` D-18).

**When a tick is sampled.** `OnMain` ends by setting `gBmSt.main_loop_ended`
and calling `VBlankIntrWait`. `OnVBlank` flushes the shadow state and clears
the flag. A tick is sampled once per completed `OnMain`, at that
`VBlankIntrWait` call, before it runs and so before the flush. Both sides now
sample at this same program point:
- GBA: at the `gate0_wait` breakpoint (`VBlankIntrWait`, 0x080D16DC), when
  `main_loop_ended == 1`. The line is written at the next `"frame"` callback,
  the VBlank the call waits for, so `frame15` is still the frame that ends
  there. Before this change the GBA sampled at line 160 and inferred ticks
  from `main_loop_ended`; on `boot300` both methods give the same bytes.
- PC: on entry to `__wrap_VBlankIntrWait` when `main_loop_ended == 1`, before
  the frame step.
- The one exception is an early flush VBlank (section "Early flush VBlank"):
  the GBA's VBlank came between `main_loop_ended = 1` and the call, so the
  flush has already run and cleared the flag when the main loop calls
  `VBlankIntrWait`. The GBA tick is then sampled at that call (flag 0, after
  the flush), and the PC's lag replay runs that `OnVBlank` at its sample point
  first, so it samples after the flush too.
- Each tick's `onmain_calls` must be equal on both sides; the compare checks
  it, so a missed or doubled sample shows as a difference, not as a shifted
  trace. It is tick + 1 while `OnMain` is the main callback. The battle
  animations run their own main loop, `MainUpdate_8055C68`
  (`banim-ekrbattlestarting.c:72`), which also ends by setting
  `main_loop_ended` and waiting, so their ticks have no `OnMain` (the compare
  prints those tick ranges; `gMainCallback` is compared on every tick).

GBA capture, `make -f port/test/Makefile gba-trace` (defaults: `boot300.keys`,
300 ticks): writes `build/gate0/gba.jsonl`, `build/gate0/gba.raw` and PNGs in
`build/gate0/gba_frames/`. 300 ticks took 316 frames: tick 0 came at frame 15,
and boot had one lag frame (tick 1, `vblanks` 2).

## PC trace (stage 5)

`pc/gate0_hook.c` is linked only into a separate test binary,
`build/pc-gate0/fe8pc`. **No file in `port/` outside `port/test/` changes, and
no game source changes.** `make -f port/test/Makefile pc` runs `port/Makefile`
with `FULL=1` in its own build directory and adds, on the command line:
- the hook to `PORT_SRCS` (so it is compiled like other port code, with
  `-DPC_PORT=1`, and `gen_host_link.py` binds its symbols);
- `-Wl,--wrap=VBlankIntrWait -Wl,--wrap=UpdateKeyStatus -Wl,--wrap=OnVBlank
  -Wl,--wrap=RenderBeginFrame -Wl,--wrap=RenderEndFrame
  -Wl,--wrap=GetGameClock -Wl,--wrap=SetGameTime` to `HOST_LDFLAGS`.
  Every game call to the first two, and every reference to `OnVBlank` (the
  `SetInterrupt_LCDVBlank(OnVBlank)` calls in `main.c`, `bmio.c`,
  `gamecontrol.c`, `bmdebug.c` and two battle-animation files), comes from
  another object than the one that defines it, so the wraps catch all of them.
  The next two catch the frame step's calls into the renderer
  (`port/hw/frame.c` into `port/render/render.c`), for the affine replay only.
  The last two count clock reads and writes for the lag position: every
  caller of `GetGameClock` and `SetGameTime` is outside `time.o` (`time.c`
  makes no call of its own; the only port caller, `main_pc.c`'s exit dump,
  runs after the trace has ended);
- a second makefile, `pc/gate0.mk`, that compiles `src/proc.o` with
  `-finstrument-functions` (`PC_INSTRUMENT`), for the lag position (section
  "Lag position"). GCC then calls the hook's `__cyg_profile_func_enter` and
  `__cyg_profile_func_exit` at the entry and return of each `proc.c` function;
  the hook only counts them, and only when `GATE0_OUT` is set. Checked on the
  60,000-tick scripted route: the plain PC trace and `pc.raw` from this binary
  equal those of the uninstrumented `port-p22b` binary on every tick, apart
  from the new `events` key and `gMainCallback`'s host address. A changed wrap
  list relinks, and a changed `PC_INSTRUMENT` recompiles `proc.o`
  (`build/pc-gate0/gate0_wraps`);
- `pc/proc_call_arg_check.c` to `PORT_SRCS`, and (in `pc/gate0.mk`)
  `-DFE8PC_DEBUG=1` to the game code's `HOST_CPPFLAGS`: the game's checks for
  test builds, which the shipped builds never compile. Today there is one.
  `ProcCmd_CALL_ROUTINE_ARG` (`src/proc.c`) returns the GBA's `r0` for the six
  void `PROC_CALL_ARG` targets of the ROM (port-plan/DIVERGENCES.md D-29, host-ABI
  values); any other target not vetted as returning its `s8` is reported, once,
  on a `[gate0] PROC_CALL_ARG:` line, and check-all's Gate 0 rows fail on that
  line. The check changes nothing in the run.
  `python3 port/tools/proc_call_arg_scan.py --check` checks both lists against
  the built ROM;
- `FIXPTR_REPORT` in the build directory, so the tracked
  `port/tools/fixptr_unresolved.txt` is not rewritten.
`PORT_SRCS` and `HOST_LDFLAGS` are read back from `port/Makefile` with
`make -p`, so the test build follows it.

The hook:
- `__wrap_UpdateKeyStatus` writes the tick's mask to `REG_KEYINPUT` (active
  low) and calls the game's function. That is the only read of `REG_KEYINPUT`
  in `src/` (`hardware.c:342`), so the per-frame default that
  `port/hw/frame.c` writes does not reach the game.
- `__wrap_VBlankIntrWait` counts the call, samples a tick when
  `gBmSt.main_loop_ended` is set, then runs the frame step and closes the
  tick's line with `frame15`. It exits after `GATE0_TICKS` ticks. Every
  complete tick is flushed, so a run that faults later keeps every complete
  tick (a fault exits with `_exit`, so a half-written line is never written).
- `__wrap_OnVBlank` counts the run (and does the clock replay, below, only
  when `GATE0_CLOCK_FROM` is set). While it runs, events are not main-loop
  events.
- `__wrap_GetGameClock`, `__wrap_SetGameTime` and the two
  `__cyg_profile_func_*` functions count the tick's main-loop events (section
  "Lag position"); with `GATE0_LAG_FROM` set they also run the GBA's lag
  VBlanks where they came (section "Lag replay").
- `__wrap_RenderBeginFrame` and `__wrap_RenderEndFrame` call the renderer and
  do nothing else unless `GATE0_AFFINE_FROM` is set (the affine replay, below).
- Fields are read through the game's symbols. All 14 globals are at their GBA
  addresses in this build (the hook logs any that are not). `sKeyStatusBuffer`
  is read through `gKeyStatusPtr`; `gRNSeeds` and `gLCGRNValue` at their GBA
  addresses, where `gen_host_link.py` places `rng.o`'s `.bss` (the compare
  checks this in the PC binary's symbol table).
- `frame15` comes from the renderer (`port/render`), written after the frame
  step returns. Run the PC side with `FE8PC_BLEND=mgba8` when comparing frames
  (`pc-trace` and the replay targets do): mGBA blends in 8-bit space, the shipped
  renderer in the GBA's 5-bit space (render-design-obj.md 7.6).
- `gMainCallback` holds `__wrap_OnMain` on PC, because `SetMainUpdateRoutine`
  takes `OnMain`'s address in another object. The compare script maps
  `__wrap_X` to `X`.

`make -f port/test/Makefile pc-trace` writes `build/gate0/pc.jsonl`,
`build/gate0/pc.raw` and `build/gate0/pc.log`. 300 ticks: 299 frames, 300
`OnMain` calls, no lag (the PC has none by design, PLAN.md 3.4). With
`PC_FAULT_OK=1` a run that stops early (a fault) is reported but does not stop
`make`; the compare then reports the ticks the PC completed, and its verdict is
`INCOMPLETE` (exit 1; section "Compare").

## Compare (stage 6)

`tools/gate0_compare.py` diffs the two traces tick by tick and reports the
first differing tick, every field that differs there (with the first
differing byte and the struct member, palette entry, tilemap cell or OAM entry
when raw dumps exist), the first differing tick of each field, and the GBA lag
ticks. `gMainCallback` is compared by symbol name (`arm-none-eabi-nm` on the
GBA ELF, `nm` on the PC binary). `frame15` is compared only where both sides
have one. Exit status: 0 when RESULT matches on every tick (and the lag
replay, when given, has no replay-only tick); 3 when it does not but the lag
check (below) labels every difference lag-only, reproduced by the lag replay
at the GBA's positions, and no tick is replay-only; 1 otherwise, so `make`
stops there; 2 on bad input or a failed harness check (trace format, symbol
addresses, a trace or log that is not there). The `compare` target reports
exit 3 and carries on.

Complete runs (integration review, 2026-10-20): every compared trace (the
GBA's, the PC's, and the lag replay and the affine replay when given) must
have all `--ticks` ticks, and the log of each PC run (`--pc-log`,
`--lag-replay-log`, `--affine-replay-log`; the targets pass `pc.log`,
`pc_lagreplay.log` and `pc_affine.log`) must have no `[fault]` line.
Otherwise the last line is `GATE 0: INCOMPLETE: ...` with the reason (exit
1), after the verdict on the ticks that were compared (`GATE 0 on the ticks
compared: MATCH ...`). Before, a lag replay that stopped early gave `MATCH`
on the ticks it had in the route compare, and nothing read the replays' logs.

It makes three passes over the same ticks and prints each:
- **RAW**: every field byte for byte, no rule. Reference only.
- **RULES**: the timing rules below.
- **RESULT**: the rules plus the oracle-limit masks, and the oracle-limit
  frame rule (P2.2b, below), which masks a `frame15` difference when the
  affine replay shows that the masked `bg2affin` difference is its only
  cause. This is the result. It prints, apart from the differences, how many
  ticks each mask covered, a line listing every masked frame
  (`oracle-limit frames (P2.2b), masked: ...`) and a line listing the frames
  after a tick that differs only inside masks that were still reported, with
  the reason.

With `--affine-replay` (the `compare` target passes it when
`build/gate0/pc_affine.jsonl` exists, which `gate0` and `scripted` make), the
compare also prints what the affine replay redrew and whether it is the same
run as the PC trace (section "Affine replay"). Without it no frame passes
under P2.2b. Then, with `--lag-replay` (the `compare` target passes it when
`build/gate0/pc_lagreplay.jsonl` exists, which `gate0` and `scripted` make),
the **LAG CHECK** labels every RESULT difference (section "Lag replay"), and a
last line gives the verdict: `GATE 0: MATCH`, `GATE 0: LAG-ONLY` or
`GATE 0: DIFFERENT` with the first tick that is not lag-only and the
replay-only ticks, if any. With
`--clock-replay` (the older `--replay`; passed when `pc_replay.jsonl` exists,
which only `make pc-replay` makes) it also prints the **CLOCK REPLAY**
diagnostic (section "Clock replay"), which excuses nothing. The compare also
checks that the GBA trace's `lag_at` entries add up to its lag counters on
every tick (exit 2 otherwise).

`--mask-oracle-limits` is the older spelling of `--masks
port/test/oracle_limits.json`. Before the upgrade it masked, in the
supplementary pass only, `bg2affin` pa..pd on ticks 5,876-5,922 from a table
in the script; OL-2 below replaces that table (it also covers x and y, on any
tick where its bounds hold).

**PNG hash cache** (`--png-hashes FILE`, coverage 12). The compare hashes each
GBA `"png:PATH"` frame itself, in pure Python (about 27 ms a frame), so a
compare of a capture with every frame (100,000 frames) spends most of its
45 minutes there, under the compare lock. `tools/png_hashes.py GBA.jsonl
OUT.json [--check N]` makes those values beforehand, outside the lock (PIL
decodes the PNG; numpy runs the FNV-1a loop over 256 frames at once; about
1.3 ms a frame; without PIL or numpy it falls back to the compare's own
function), and `--check N` hashes N frames spread over the capture the slow way
too and exits 1 on any difference. `gate0_compare.py --png-hashes OUT.json`
then takes the values from the file: it first hashes three of its frames
(first, middle, last) again and refuses the file (exit 2) if one differs; the
output is the same as without it. Check (route_e21 to tick 59,500 with 1,111
GBA frames, plain PC): 60 of 60 frames equal the slow hash, both compares give
byte-identical text and JSON, 32.8 s without the file and 2.6 s with it; a file
with one wrong value is refused. Opt-in: no target passes it yet.

`make -j24 -f port/test/Makefile gate0` runs stages 2 to 6 in order (about 7
seconds after the GBA `make`). Result on 2026-10-19 (`boot300.keys`): RAW
differs on every tick (clock, `prevVCount`, `onvblank_calls`); RULES and RESULT
match to tick 299. The GBA has lag VBlanks at ticks 0, 1 and 271; the PC has
none.

## Rules and masks

### Tick alignment (compared, never excused)

A logic tick is one main-loop iteration: one `OnMain`, or one run of another
main callback that ends the same way (the battle animations'
`MainUpdate_8055C68`). Each tick's `onmain_calls` must be equal on both sides
(tick + 1 while `OnMain` runs; the first rule, until 2026-10-19, required
tick + 1 and so flagged every battle animation on both sides), so ticks are
matched by `OnMain` count and main callback, not by frame. Each tick's
`vblank_waits` must be equal on both sides: the game asked for the same number
of VBlanks. A failure of either is reported as a difference in that counter.
`frame` and `vblanks` are timing and are not compared; they are printed.

### `gGameClock` (compared as the logic clock)

`gGameClock` is incremented only by `IncrementGameClock` in `OnVBlank`
(`bm.c:307`, `time.c:29`), and written only by `SetGameTime` (`bmsave.c:119`
new game, `bmsave.c:214` and `559` loading `time_saved`, `bmdifficulty.c`). So
it counts `OnVBlank` runs since the last write.

- **What is excused:** lag VBlanks. A VBlank that comes while the CPU is still
  busy (in `OnMain`, or in boot code after `main.c:69` installs `OnVBlank`)
  runs `OnVBlank` once more than the game waited for. Per side, the lag is
  `lag(t) = onvblank_calls(t) - (vblank_waits(t) - 1)`: handler runs that no
  completed `VBlankIntrWait` accounts for (the wait being entered at the
  sample has not completed). The GBA lags; the PC never does (PLAN.md 3.4).
- **What is compared, exactly, every tick:** let E be the excused offset. On a
  tick with no clock write, E grows by the GBA's new lag minus the PC's, and
  `gGameClock` GBA minus PC must equal E. A clock write shows as a tick where
  the clock moved by something other than that tick's `OnVBlank` runs. Both
  sides must write on the same tick, and the new offset must be a known base
  (0, the same value written on both sides; the previous E; or the offset of a
  `gPlaySt` clock copy, for `SetGameTime(time_saved)`) plus at most the lag
  VBlanks of that tick that came after the write. Anything else is reported.
- **Why:** the clock is the game's VBlank counter, so its absolute value on
  the GBA includes CPU overruns that the PC does not model. Taking out exactly
  the measured lag leaves what game logic controls: one increment per waited
  VBlank, and the values the game writes.
- **Clock copies:** `gPlaySt.time_saved` (+0x00, `bmsave.c:176`, `499`) and
  `gPlaySt.time_chapter_started` (+0x04, `bmio.c:1005`, `sio_battlemap.c:379`)
  store `GetGameClock()`. A copy is taken on the tick its value changes, on both
  sides. Its GBA minus PC offset must then lie between the clock offsets at the
  start and end of that tick (0 also counts if the clock was written in that
  tick) and stays fixed until the next copy. Then those four bytes are equal
  for the compare; otherwise they are reported.
- **Not excused:** what the game does with a clock read, other than the two
  copies. Sprites and effects animated from `GetGameClock()` (for example the
  "press A" arrow, `scene.c:1405`) differ when the GBA has lagged, and are
  reported. The clock replay (below) shows which differences these are.

### `gBmSt.prevVCount` (left out on every tick)

- **What is excused:** all of it (`gBmSt` +0x06, u16), on every tick.
- **What is compared:** nothing; the other 62 bytes of `gBmSt` are compared
  as before.
- **Why:** it is `REG_VCOUNT` read when `OnMain` ends (`bm.c:349`) and in two
  battle-animation loops (`banim-ekrbattlestarting.c:94`, `banim-ekrbattle.c:219`):
  how far the raster got while the CPU ran the tick, a measure of CPU time. The
  PC does not model CPU time, so its value is whatever line the frame step
  left. Nothing reads it: no read in `src/*.c` or `include/`, and the only
  handwritten asm (`asm/arm.s`, `asm/arm_call.s`) does not touch `gBmSt`. So
  leaving it out cannot hide a logic difference.

### Oracle-limit masks: `oracle_limits.json`

One entry per known limit of the oracle (mGBA 0.10.5 with its HLE BIOS), from
`port-plan/DIVERGENCES.md` section 5. Each entry has `id`, `title`, `field`,
`check`, `condition` (in words), `reason` and `ref` (the DIVERGENCES section 5
item); entries that can be applied also have `groups` or `params`, and may
have `chapter` (applied only on ticks where both sides have that
`gPlaySt.chapterIndex`). The compare applies an entry only where it has a
`check`:

| Id | Field | Covers | Condition | Status |
|---|---|---|---|---|
| OL-1 | none | nothing | Open-bus reads from the BIOS area: no trace field is known to carry it | Listed only, never applied |
| OL-2 | `gLCDControlBuffer` | `bg2affin` (+0x48) and `bg3affin` (+0x58), 16 bytes each | In a covered struct, pa, pb, pc and pd (s16) differ by at most 1, x and y (s32) by at most 201 = 1 x (120 + 80) + 1: the largest rotation centre FE8 passes to `BgAffineSet` is (0x78, 0x50) (`opanim-main.c`, `titlescreen.c`; `sysutil.c` passes 0, 0) | Applied; also to frames, `bg2affin` only and proved by the affine replay (`"frames": true`, next section) |
| OL-3 | `gOam` | attr3 of each entry (bytes 6-7: the OBJ affine parameters) | Each covered halfword differs by at most 1 as s16 | Applied; first hit 2026-10-20 on the route compares from ch3 (lag replay against the GBA), 2 ticks in 67,778-67,813, delta 1 |
| OL-5 | `gBG3TilemapBuffer` | 6 tilemap cells, (col 25, row 4) and (cols 21-25, row 5) (check `cells`) | Chapter `0x38` (Castle Frelia, the 2x2 map) loaded on both sides, and each cell holds exactly its listed GBA and PC halfwords: mGBA's AGB Print window sign-extends the tile-id `LDRH` past the 16 MiB image (`0xFFFFFFF3`) where the ARM7TDMI and the port (`PortGbaLoad16`, D-27) zero-extend | Applied; hit on the routes from ch1 at ticks 20,272-20,277 |
| OL-7 | `gPlaySt` | `unk_30.survivalRank`, +0x3E bits 5-7 (check `bitfield`) | Chapter `0x13` (Eirika's Chapter 19, which holds Chapter 18's ranks from the chapter change on: `GameControl_ChapterSwitch` computes them and sets the next chapter in one tick) loaded on both sides, the GBA's value 2 and the PC's 3, every other bit of the byte equal: mGBA's stale bus word after the sound DMA (`GBALoadBad`, `gba->dmaPC`) gives Chapter 18's fire-trap death-record load other bytes at `route_e18` 26,515, so mGBA writes trap deaths for Seth and Franz that the PC (GBATEK's prefetch word, D-60, was working ID D-R10-TRAPS-1) does not, and at the chapter's end counts Seth as a death in Chapter 18 | Applied; hit on `route_e18` at ticks 92,398-92,762 (the 18 -> 19 change to the route's end), nothing else differing |

The checks: `affine_bg` and `affine_oam` (deltas within bounds), `cells`
(listed u16 cells, each only with one of its listed [GBA, PC] pairs) and
`bitfield` (one bit field of one byte: `params` `offset` (a string such as
`"0x3E"`), `shift` and `bits` inside that byte, and `pairs`, the [GBA, PC]
values of the bit field it accepts; the byte is covered only when the two
sides differ inside the bit field alone and hold one of those pairs). The
first three work on the hashed fields' raw dumps; `bitfield` also works on
the fields the trace writes out whole (`gBmSt`, `gPlaySt`, `gRNSeeds`,
`sKeyStatusBuffer`), as OL-7 does on `gPlaySt`.

**Unknown differences are never masked.** A mask covers only its own bytes,
only on ticks where its condition holds, and (for a hashed field) only when
both raw dumps exist; any other byte of the same field is still compared and
reported. RESULT prints,
for each mask, how many ticks it covered, the tick range and the largest
deltas it accepted. A new limit needs a DIVERGENCES section 5 entry first, then
a line here with a `check` the script implements (`CHECKS` in
`tools/gate0_compare.py`).

### Oracle-limit frames (P2.2b, decided 2026-10-19)

The user's rule (`port-plan/DIVERGENCES.md` section 5, P2.2b): a
rotated-affine frame passes when the only difference is the known `bg2affin`
oracle limit. A limit with `"frames": true` in `oracle_limits.json` (only
OL-2; its `frames_condition` gives the rule in words) carries it into the
frame compare. Since the strict version (branch `port-p22b`, second round) the
compare does not take that on trust: the affine replay (next section) must
show it, frame by frame.

- **What is excused:** a `frame15` difference at tick t (an "oracle-limit
  frame"), in RESULT only, when all of these hold:
  1. At tick t-1 every difference, in every field (`frame15` included), lies
     inside applied masks, there is at least one, every limit hit there has
     `"frames": true`, and all of it is in `bg2affin` (+0x48). The decision
     names `bg2affin` only, so a tick t-1 with a `bg3affin` difference (even
     one inside OL-2's tick mask) leaves frame t reported.
  2. At tick t-1 BG2 is shown: `DISPCNT` (`gLCDControlBuffer` +0x00;
     condition 1 makes it equal on both sides) is not force-blanked, BG2 is
     on, and the mode is 1-5.
  3. Nothing else differs at tick t (RESULT).
  4. The affine replay proves it: the PC run again, with frame t drawn with
     the GBA's `bg2affin` of tick t-1 in the BG2 affine registers, draws
     exactly the GBA's frame (`frame15` equal); the same frame redrawn with
     the PC's own register values is the PC trace's frame (`frame15_pc`
     equal), so both pictures come from the same machine state; and the
     replay equals the PC trace outside the frames at ticks t-1 and t.
- **What is compared:** every other frame. A frame difference whose previous
  tick passes the first part of 1 but fails the rest is reported with a note
  saying why (`[not an oracle-limit frame: ...]`), and RESULT lists those
  frames on their own line. Without `--affine-replay` no frame passes this
  way. A frame right after a reported frame is reported too (condition 1
  includes `frame15`). RAW and RULES still report every frame difference.
- **Why:** the frame of tick t is drawn with the shadow registers that
  `OnVBlank` flushes after tick t-1's sample (`FlushLCDControl`, `bm.c:314-321`,
  `hardware.c:91-103`), so a `bg2affin` that differs only by OL-2 at t-1 moves
  the rotated BG's pixels in frame t. mGBA's HLE `BgAffineSet` is not the GBA
  BIOS; the port follows the hardware's integer method (D-11). Condition 4
  turns "only `bg2affin` differs in the traced fields" into "only `bg2affin`
  differs in the picture": with the GBA's `bg2affin` everything else the PC
  draws (VRAM, OAM, palette, the other registers, the renderer) gives the
  GBA's frame pixel for pixel.
- **Not seen by the rule:** whether the PC draws correctly with its own
  `bg2affin` values. The proof shows that the PC's machine state plus the
  GBA's matrix gives the GBA's picture; the PC's own frame uses the same
  renderer path with a matrix that differs by OL-2's bounds, and no oracle
  draws that matrix. A renderer fault that shows only with the PC's exact
  values would pass. Every frame whose previous tick has an equal `bg2affin`
  (all others, including the lights' angle-0 frames) is still compared
  directly. A GBA-side counterpart (mGBA drawing with the PC's `bg2affin`)
  would close this; it is not built.

RESULT counts masked frames apart from differing ticks and lists them;
`--json` writes them as `oracle_limit_frames` (`tick`, `prev`, `masks`,
`bgs`, `affine_replay`), and the reported ones with their reason as
`oracle_limit_frames_reported`.

### Affine replay (the P2.2b proof)

`make -f port/test/Makefile pc-affine-replay KEYS=... TICKS=...` (after
`gba-trace`, with the same `GATE0_SHOT_TICKS` if any) runs the PC again with
`GATE0_AFFINE_FROM=build/gate0/gba.raw`. Output: `build/gate0/pc_affine.jsonl`
and `pc_affine.log` (no raw dump). `gate0` and `scripted` run it after
`pc-replay`, and `compare` passes it as `--affine-replay`.

For each `frame15` tick t >= 1, the hook compares the GBA's `bg2affin` of tick
t-1 (16 bytes at +0x48 of that tick's raw record) with the PC's own sample of
tick t-1, and writes the outcome as `"affine"` on that tick's line:
- `"equal"`: the two are equal; nothing is replaced, so the frame must equal
  the PC trace's (the compare checks it).
- `"gba"`: at the start of line 0 of that frame (`__wrap_RenderBeginFrame`)
  the BG2 affine registers (BG2PA-BG2Y, 0x04000020-0x0400002F, where
  `FlushLCDControl` copies `bg2affin`) are set to the GBA's values, so lines
  0-159 are drawn with them; that frame is the line's `frame15`. After line
  159 (`__wrap_RenderEndFrame`) the hook draws the same frame again, all 160
  lines at once, with the PC's own register values (`frame15_pc`), puts the
  replayed picture back as the frame of record (and in `GATE0_SHOTDIR`) and
  leaves the PC's values in the registers, as without the replay.
- `"regs"`: not replaced, because at line 0 the registers did not hold the
  PC's own `bg2affin` of tick t-1 (a tree-0 proc in `OnVBlank` or a direct
  register write changed them after the sample); P2.2b's premise does not hold
  for that frame, so it cannot pass.
- `"midframe"`: replaced, but the game wrote the registers during the frame
  (an HBlank or VCount handler); its write stands and the frame cannot pass.
- `"none"`: tick 0, or no GBA record for tick t-1.

Nothing the game reads changes: the registers are written back before
line 159's HBlank IRQ, and the renderer relatches them at the next line 0. The
compare checks this on every tick: the replay must equal the PC trace in
every field but `frame15`, `affine` and `frame15_pc`, every `"equal"` frame
must equal the PC's, and every `frame15_pc` must equal the PC's frame (it
prints the three results under `Affine replay against the PC trace`). The
redraw after line 159 sees the end-of-frame state, so a frame changed by
HBlank handlers mid-frame would not be reproduced; then `frame15_pc` differs
and the frame is reported, never passed. The end of the log gives the counts
per outcome.

### Raster-timing frames (P-17, decision 9)

The user's rule (`port-plan/DECISIONS.md`, 2026-10-20 afternoon, item 9;
DIVERGENCES P-17): the PC keeps running `OnMain` in zero time and then steps
the frame (D-15, D2), and Gate 0 labels a frame whose only difference is
where the GBA's raster was when the main loop wrote, backed by an exact replay
of what the GBA's frame saw, like the affine replay. No timing heuristic in
the port. One mechanism covers every case seen so far:

- **The cause.** The GBA draws a frame while its CPU runs: `OnVBlank` and
  then `OnMain` run into the frame's lines, so a line shows (and a line
  handler reads) the main loop's memory from before or after a write, as CPU
  time decides. The PC's lines all see the tick's last state. Seen as: the
  phase banner's fade (`PhaseIntroVMatchHi`, the line-0 VCount handler, reads
  `gBmSt.altBlendBCa/BCb` before `PhaseIntroBlendBox` writes the tick's fade
  step: P-17's 27 route frames); the minimap window (`Minimap_OnHBlank`
  latches `gMinimapFrontWinBuf` on the VBlank lines before
  `InitMinimapWindowBuffers` swaps it: TAS group 3); text and menu tiles
  written into VRAM after the raster passed their lines (TAS group 4, tick 460,
  `chapsave-ch2` 2,100); an HBlank handler that `OnMain` turns off while the
  GBA has already run it on the first VBlank lines (the subtitle-help
  darkener, `src/bb.c`, which restores the blend registers there: cov9 group
  G7, labelled since 2026-10-21); a BG scroll register that `OnMain` writes
  directly after the raster passed the top lines (`BackgroundSlide_Loop`,
  `src/mural_background.c`: the Guide's scrolling background, round 8's
  "top band" and Guide lag frames, labelled since 2026-10-21).
- **GBA record** (`oracle/mgba_lua_run.c`, `MGBA_RUN_RASTERLOG`; `gba-trace`
  always writes `build/gate0/gba_raster.bin`). For each `frame15` frame (from
  the start of line 160 to the next) the runner keeps the frame's **line
  events** in the order the PC's frame step has them (an HBlank or VCount
  handler entered, at `IrqMain`'s `bx r0`; a line drawn, mGBA's
  `drawScanline`; line 160's left out, since the PC's frame step after the
  sample starts at line 161), every **store that changes a watched halfword**
  with the number of line events before it, and each watched region's FNV-1a
  at the frame's end. Watched: palette RAM, VRAM and OAM (what the renderer
  reads; stores from the frame's first visible line on: earlier ones show on
  every line) and the RAM the line handlers read (`GBA_RASTER_RAM`: the
  minimap's two window buffers with their front and back pointers,
  0x02000000-0x02000507, not `gMinimapDisplayedWinBuf`, which the handler
  itself writes; the banner's four blend bytes, `gBmSt`+0x38-0x3B; every
  store of the frame), and since 2026-10-21 two I/O ranges (every store of
  the frame): `REG_IE` (0x04000200, `ie`), which decides whether the HBlank
  and VCount handlers run at all (the main loop writes it in
  `UpdateHBlankHandlerState` and `SetInterrupt_LCDVCountMatch`), and the BG
  scroll registers (0x04000010-0x0400001F, `bgofs`), which `OnMain` also
  writes directly (`BackgroundSlide_Loop`'s `REG_BG3HOFS`, the Guide's
  scrolling background); and since round 10 (2026-10-22) `REG_DISPSTAT`
  (0x04000004, `dispstat`), whose HBlank and VCount IRQ enables and VCount
  line `OnVBlank`'s `FlushLCDControl` copies from the shadow
  `gLCDControlBuffer` (`UpdateHBlankHandlerState` sets the shadow's HBlank
  enable and `REG_IE` at once; the hardware register follows at the next
  flush). The runner reads an I/O register back from mGBA's
  register image and takes I/O regions below `REG_IME` (0x04000208); an I/O
  halfword that a line handler stored in the frame (the store's flag bit 0) is
  left to the redraw's live handlers, as an unwatched register is, and its
  values are compared in the bits the hardware keeps (9 for a scroll
  register, 14 for `REG_IE`, bits 3-15 for `REG_DISPSTAT`: the PC's I/O image
  holds what the game wrote). `REG_DISPSTAT`'s bits 0-2, the status flags the
  video unit changes on every line without a store, are left out of the log,
  its self-check and the region's hash (`RasterKeep`); the PC's frame step
  sets its own.
  The stores come through `cpu->memory` (CPU stores,
  `STM`, DMA, the HLE BIOS), which the runner wraps. It checks itself: at the
  frame's end each region must equal its copy from where its logging started
  with the logged stores applied in order (each old value matching), or the
  block is marked incomplete. The trace's tick line gets `"raster"`, the
  block's CRC-32, and the header `"raster_log"`, the regions.
- **PC redraw** (`pc/gate0_hook.c`, `GATE0_RASTER_FROM`; `pc-trace` and
  `pc-lag-replay` pass it when the log is there, not the affine or the clock
  replay). On a `frame15` tick whose GBA block has stores, the hook keeps the
  GBA memory the frame step can change (EWRAM, IWRAM, I/O, palette RAM, VRAM,
  OAM) at the frame step's start, and after line 159 checks that every
  watched region with GBA stores equals the GBA's at the frame's end (the
  FNV-1a) and was not changed by the PC's own frame step. Then it runs the
  frame step's lines 161-227 and 0-159 again (`PortRedrawFrameLines` in
  `port/hw/frame.c`, the same `Line` code; compiled into this binary only),
  the game's HBlank and VCount handlers live, twice: as is (the **control**:
  it must be the PC's frame, so the redraw reproduces the frame step), and
  with the GBA's timing: each watched halfword the GBA stored put back to the
  GBA's value from before the frame (newest first, each one's new value
  checked against the PC's memory), then each store made again just before
  the line event it came before on the GBA (its old value checked), the line
  events counted through trampolines in `gIRQHandlers[1]`/`[2]` and a
  `--wrap=RenderLine`. The stores due are also made before every IRQ
  check: ahead of `Irq()`'s IME and IE test and, since round 10, of
  `LineStart`'s VCount and `LineHBlank`'s `REG_DISPSTAT` HBlank tests
  (`PortGate0IrqDue`, called from `port/hw/frame.c` in this binary only), so
  the main loop's `REG_IE` store turns a handler off, or on, at the point of
  the frame where the GBA made it, and so does the `REG_DISPSTAT` store of
  `OnVBlank`'s flush: an IRQ a test then refuses is no line event, as on the
  GBA. The log places a store between two line events only, and no line is
  drawn between them, so only these checks can tell when in that gap the
  store came; the GBA took no IRQ in the gap. So a store that turns no IRQ
  source on is made at the first check after the events before it (a handler
  turned off, G7), and one that turns an IRQ source on (a `REG_IE` bit,
  `REG_DISPSTAT`'s IRQ enables, or its VCount line while that IRQ is on)
  waits, with every store after it, for the check of the GBA's next line
  event (that line's HBlank or VCount IRQ, or the event itself): after a slow
  VBlank proc tree the GBA's flush turns the minimap's HBlank IRQ on at line
  182, where the PC's flush was at line 160 (C10-10 (b), `mk10A_route_ch7`
  21,060; round 9's two monkey frames, the HBlank events from line 176). The
  redraw's line events must be the GBA's, event for
  event. Then the frame step's memory, the frame of record, its RGBA and hash
  are put back: nothing the game reads changes. The tick line gets `"raster"`:
  `"gba"` (with `"raster_rec"`, the block's CRC-32, and `"frame15_raster"`,
  the redraw's hash; `GATE0_SHOTDIR` gets `tickNNNNNN.raster.555`),
  `"nostores"` (the GBA stored nothing watched while it drew the frame),
  `"differs"` (a watched region with stores is not the GBA's at the end;
  `"raster_note"` names it), `"handler"` (the PC's frame step changed such a
  region), `"incomplete"`, `"control"`, `"rollback"` (a GBA store does not fit
  the PC's memory, for example a line handler writing the same palette
  entries) or `"slots"` (the line events differ; the note names the first),
  `"none"` (no block). The log's last line counts them.
- **What is labelled**: a `frame15` difference at tick t that is not an
  oracle-limit frame, in RESULT, only when the trace's tick t has `"raster":
  "gba"`, its `raster_rec` is the GBA trace's `raster` of tick t,
  `frame15_raster` is exactly the GBA's frame, no other field differs at tick
  t, and no field but `frame15` differs at tick t-1 (the frame shows tick
  t-1's flushed registers). Each trace carries its own proof, so the rule
  applies to `--pc` (plain, or a lag replay as on the route rows) and to the
  lag replay on the lag check (there such a tick is counted as "lag-only,
  raster-timing frame", section "Lag replay"). Labelled frames are left out
  of RESULT, listed (`raster-timing frames ..., labelled`) and counted apart
  (`--json`: `raster_timing_frames`, and `raster_timing_frames_reported` for
  frames with a mark that did not pass); MATCH, LAG-ONLY and DIFFERENT keep
  their meaning. RAW and RULES still report every frame difference.
- **Why it is exact.** The redraw is the PC's own frame step from the PC's
  own machine; the only change is the timing of the GBA's own stores, and
  only of those that end in the PC's memory as it is. A PC whose watched
  memory is not the GBA's at the frame's end (a wrong window shape, wrong
  text tiles, a wrong blend step) is marked `"differs"` and never labelled,
  whether or not the GBA's frame would show it (the G3 review's hole: the last
  late frame of a window animation). Anything else that differs (unwatched
  memory, I/O the main loop wrote mid-frame, a line handler the PC runs at
  another line) makes the redraw differ from the GBA's frame, or its line
  events, and the frame stays reported. As with the affine replay, the
  compare trusts the hook's mark and hash (a forged `"raster":"gba"` with the
  GBA's hash would pass); what it checks itself is that the block is the GBA
  trace's own (`raster_rec`) and the surrounding fields.
- **Not covered** (reported): frames whose line handlers write watched memory
  (`"rollback"`, `"handler"`: the world map's palette effects), frames where
  the GBA's line events differ from the PC's (`"slots"`: a handler the GBA ran
  mid-`OnMain` from an IF bit latched while IE was off, which `Irq()` in
  `port/hw/frame.c` never does, P-25; so a handler the main loop turns on
  mid-frame is labelled only when no latched IRQ came; one it turns off
  mid-frame is covered since 2026-10-21, `REG_IE` above, and the HBlank IRQ
  that `OnVBlank`'s late flush turns on since round 10, `REG_DISPSTAT`), I/O
  registers the main loop writes mid-frame other than `REG_IE`, `REG_DISPSTAT`
  and the BG scroll registers (and those three where a line handler also wrote
  the same halfword in the frame), and memory the
  line handlers read that `GBA_RASTER_RAM` does not list (add a range there and
  in `RASTER_LOG` of `tools/gate0_compare.py`).

Result (2026-10-20, `port-harness8b`; `make -f port/check-all.mk CHECK_LONG=1`:
PASS, 24 of 24, peak memory 1.7 GB): routes ch0-ch5 through the lag replay are
`MATCH`, with 1, 4, 9, 20, 22 and 27 raster-timing frames labelled; route
ch5's 27 are exactly P-17's 27 phase-banner frames (3,780 ... 133,140), and
nothing else differed on any route. The 60,000-tick scripted Gate 0 is
unchanged (`LAG-ONLY`, 25,557 lag-only, 0 not, 0 replay-only, no frame
labelled). `chapsave-ch2`: P-17's first case, the world-map name box at 2,100,
is labelled in the plain run, and the ENEMY PHASE intro at 5,160 is lag-only
with a raster-timing frame (before: both not lag-only). TAS same-buttons run,
every frame of ticks 0-8,254 (section "TAS route"): all 72 differing frames
are labelled. Checks that it labels nothing wrong (scratch, not committed): a
PC binary that damaged its own frame right after a tick's sample (the minimap
front buffer's left edges +4 px at 5,995, 6,003 and 6,140; the item menu's
text tiles at 3,025; `gBmSt.altBlendBCa` at 2,782; `REG_BG0HOFS` at 3,043)
got each of those frames reported, never labelled (`differs` for the first
three kinds, a redraw that is not the GBA's frame for the register); and
mutated copies of a trace (`frame15_raster`, `raster_rec`, a `gRNSeeds` byte at
the tick, a `gBmSt` byte the tick before, the mark) each left the frame
reported. A save state made by `port-tas2`'s binary still resumes (relinked),
and a fresh one resumed at 3,700 on route ch5 labels the same banner frames as
the full run.

### Negative checks (2026-10-19, not committed)

Mutated copies of the `boot300` PC trace, each compared against the GBA trace:
`bg2affin.pb` +1 is masked by OL-2 (1 tick); +2 is reported; `pb` +1 with x
+150 is masked, with x +300 reported; `pb` +1 together with a `dispcnt` change
is reported (the mask covers only its bytes). OAM attr3 +1 is masked by OL-3;
attr0 +1 is reported. `gGameClock` +1 from tick 120, one changed `gRNSeeds`
byte and one extra `OnMain` are each reported at their first tick; a changed
`prevVCount` is not.

Frame rule (P2.2b), first version, on mutated copies of ticks 0-5,999 of the
60,000-tick scripted traces (tick 5,879 differs only inside OL-2, `DISPCNT`
0x0501: mode 1, BG0 and BG2 on). Unchanged: RESULT matches, frame 5,880
masked, exit 0 (RULES still reports it). Each of these reports frame 5,880: one
`gRNSeeds` byte changed at tick 5,880 (note: other fields differ); one palette
byte changed at tick 5,879; BG2 off, forced blank or mode 0 at tick 5,879 on
both sides (note: no BG that uses the matrix is shown); `"frames"` taken off
OL-2 (note: no frame rule); `bg2affin.pb` +2 at 5,879 (outside OL-2); only
`bg3affin` off by 1 at 5,879 in mode 1 (note: BG3's matrix not shown). A
changed frame at tick 5,820, whose previous tick is equal, is reported while
5,880 stays masked. Extra frames at 5,879 and 5,880 (ticks 5,878 and 5,879
both inside OL-2) are both masked. The review then found the gap the strict
version closes: the PC frame at 5,880 replaced by an arbitrary hash was still
masked.

Strict version (affine replay), on the same ticks 0-5,999 (PC, clock replay
and affine replay from this branch's binary; mutation script not committed).
Unchanged:
`GATE 0: MATCH`, frame 5,880 masked (the replay draws the GBA's `7f8e62bc`; its
redraw with the PC's registers is the PC's `2e28b4d6`). Each of these reports
frame 5,880 with the reason, exit 1: the PC trace's frame replaced by an
arbitrary hash (the reviewer's case: the redraw with the PC's registers is not
the PC's frame); the affine replay's frame arbitrary (not the GBA's frame); a
PC-side picture change carried consistently into both runs (not the GBA's
frame); no `--affine-replay`; the replay changed outside the frame at 5,879
(`gRNSeeds`); the replay marked `regs` (not replaced); `bg3affin` pa +1 at
5,879 on top of the `bg2affin` difference (still inside OL-2's tick mask; the
frame rule covers `bg2affin` only); the replay trace ending before 5,880.
`"frames": 1` and a normal PC trace given as `--affine-replay` stop with exit
2. A changed frame at 5,820 (previous tick equal) is reported while 5,880 stays
masked. The same cases on the no-key lights (frame 6,600) give the same
results; a frame after a reported frame is then reported too (condition 1
includes `frame15`).

### Lag position (where a GBA lag VBlank came)

A GBA lag VBlank is a VBlank that comes while the main loop is still running
(`OnVBlank` entered with `gBmSt.main_loop_ended == 0`, not the VBlank of an
inner `VBlankIntrWait`). It runs all of `OnVBlank` (`bm.c:303-324`) in the
middle of the tick: `IncrementGameClock`, the sound driver, `Proc_Run` of tree
0 (`PROC_TREE_VSYNC`) and `SyncLoOam`, without the flush. What it changes
depends on where in the tick it came: which clock reads see the new value,
whether a tree-0 proc started in this tick gets its first step now or one
tick later, which proc is freed first. The PC has no lag (PLAN.md 3.4), so to
reproduce a lag effect the harness has to run that `OnVBlank` at the same
place on the PC, and so needs a position that both sides measure the same way.

**The measure.** The tick's main-loop events, counted from the previous sample
(tick 0: from power-on), in program order. Seven kinds:
1. `RunProcessRecursive` entered (`proc.c:212`): a proc tree node, in the
   tree walks of trees 1-5 in `OnMain` (and of the battle-animation loops);
2. `RunProcessRecursive` returning: that node's own step and its subtree are
   done;
3. `RunProcessScript` entered (`proc.c:714`): a proc script runs, from a tree
   walk or from `Proc_Start` inside another proc's step (`proc.c:84`);
4. `GetGameClock` (a clock read);
5. `SetGameTime` (a clock write);
6. a BIOS copy or LZ77 call (`CpuSet`, `CpuFastSet`, `LZ77UnCompWram`,
   `LZ77UnCompVram`) whose destination is video memory (palette RAM, VRAM or
   OAM, 0x05000000-0x07FFFFFF), made by the main loop, not by an IRQ handler
   such as `OnVBlank`'s flush and tree-0 tasks (format 4, 2026-10-20). The
   other decompressions (`RLUnComp*`, `HuffUnComp`) are only named in
   `Decompress`'s table and no route calls them, so they are left out on both
   sides;
7. a line handler's hand-over: `FlipBgVerticalScroll` (`bmlib-hblank.c`),
   which gives `HBlank_BgVerticalScroll` the next frame's scroll table,
   counted once the flip is stored (format 4).
Events inside `OnVBlank` (its tree-0 walk, the clock reads of tree-0 procs
such as `SyncUnitSpriteSheet`) are not counted. A lag VBlank's position is the
number of events of its tick that came before it.

**Why these.** Every effect of a lag VBlank reaches the main loop through
state that `OnVBlank` changes: the clock (read and written only through
`GetGameClock` and `SetGameTime`, `time.c`), tree-0 procs and the proc pool
(changed by the main loop only through proc code), `gOam`'s low part, and the
sound driver. Kinds 1 and 2 cut the tick into proc steps: in a walk
`RunProcessRecursive` enters a node, recurses into older siblings first, then
runs the node's own step, then its children, so each proc's step lies between
two consecutive events. Kind 3 cuts a step where it starts another proc,
which is where a tree-0 proc can appear in the middle of a tick. Kinds 4 and 5
place the clock exactly. Round 1 used clock reads only; tick 12,402 showed why
that is not enough: the lag VBlank came between two proc steps with no clock
read in between. A count is reproducible because both sides run the same C
code: the same proc walks, scripts and clock calls in the same order (checked
below, tick by tick). Finer measures (CPU cycles, the interrupted PC) have no
PC counterpart.

Kind 6 places the main loop's video-memory writes against the tree-0 tasks
that write video memory themselves in a lag VBlank
(`BMapVSync_UpdateMapImgAnimations` copies the map's animated tiles to VRAM,
`SyncUnitSpriteSheet` the unit sprites). VRAM is not traced, so a wrong order
shows only in the frames, and it lasts until the next write there. Found on
the TAS route (2026-10-20, group 5): at tick 2,349 (the Prologue map load,
14 lag VBlanks) the GBA's lag VBlank 9 came during the stall of a
`LZ77UnCompVram` to 0x06000000, so the river tiles' first animation frame went
to 0x0600A000 and the next call, `UnpackChapterMapGraphics`'
`LZ77UnCompVram` of the tileset to 0x06008000, wrote the tileset's own river
tiles over it; with five kinds that VBlank was placed before the next proc
event, after the second call, so the PC kept the animation frame, and frames
2,351-2,371 showed other river tiles until the next animation step at 2,372.

Where a lag VBlank can fall inside one of these calls depends on the call.
mGBA 0.10.5's HLE BIOS (its BIOS image read with `emu.memory.bios` and
disassembled, 2026-10-20, `port-harness8b`): the SWI handler at 0x40 takes
the routine from its table at 0xD4, switches to System mode keeping the
caller's IRQ bit (so IRQs are taken from 0x78 on) and calls the routine at
0xA0.
- `LZ77UnCompWram` and `LZ77UnCompVram` (SWI 0x11, 0x12): the table entry is
  0x348, which makes the handler run the HLE in C (`svc 0xF00000`): it writes
  all of the data at once, then the routine at 0x348 is a stall loop. A lag
  VBlank in such a call comes after every write (checked: at tick 2,349's
  first lag VBlank in the second call, 0x0600A140 already holds the tileset's
  word), and the replay, which runs it before the next event, puts it there
  too: exact.
- `CpuSet` (SWI 0x0B, routine 0x214) and `CpuFastSet` (SWI 0x0C, 0x2A8): ARM
  copy loops in the BIOS image, interruptible between any two instructions,
  so a lag VBlank can come part way through a copy. `CpuFastSet` loads 8 words
  (`ldm r0!`) and then stores them (`stm r1!`); `CpuSet` loads and stores one
  unit at a time (`r0`/`r1` for words, `r12`/`r5` for halfwords); a fill loads
  its value once, before its loop. (The G5 analysis said all four write their
  data when the SWI starts; that holds for the two LZ77 calls only, G5 review.)
  So the capture records such a VBlank's place in the copy: at `OnVBlank`'s
  entry the main loop's interrupted registers are in the outermost IRQ frame,
  the BIOS vector's `push {r0-r3, r12, lr}` just below `__sp_irq`
  (0x03007FA0; `lr` is the interrupted instruction + 4), and `r4`-`r11` are
  the runner's record of them at that IRQ's entry (`runner_irq_reg`, section
  "Headless Lua"). Until 2026-10-21 the capture took the live `r4`-`r11` at
  `OnVBlank`'s entry, which are the main loop's only when that VBlank is the
  outermost IRQ: on `ch4_start.sav` + `mkB_ch4_start_s2.keys` at tick 3,527
  (cov9 H-col-2) the VBlank came inside line 159's `HBlank_BgVerticalScroll`
  (two IRQ frames, 80 bytes; the inner return address 0x08014D7E, the
  handler's table hand-over), which had interrupted the main loop's 16-bit
  `CpuSet` fill of 0x24 halfwords into IWRAM (dest 0x03007C88, BIOS 0x264),
  so the handler's `r5` (0x78C) was read as the fill's destination and the
  capture stopped ("progress -50361596 written, 2 read does not add up"). The
  copy's own `r5`, 0x03007CA0 (24 bytes stored), is what the runner kept, and
  what the handler's `push {r4, r5, r6, lr}` holds on the System stack (read
  there with a scratch probe). Only the 16-bit `CpuSet` needs `r5`; the other
  copies' progress is in `r0`/`r1`. The capture checks the record at every
  lag VBlank (`r12` against the push, and at one IRQ frame `r4`-`r11` against
  the live registers and `sp`/`lr` against `IrqMain`'s push). The PC side
  needs no change: it reads no registers, and the lag replay runs a lag
  frame's line 159 handler to its end before that `OnVBlank` (the GBA ran
  the handler's rest after it; that state is the handler's own scroll
  table, which no lag `OnVBlank` reads). With the record the capture plays
  `mkB_ch4_start_s2.keys` through (10,000 ticks; the copy at 3,527 is a
  `lag_in_other_copy`), the lag replay runs all 445 lag VBlanks at the GBA's
  positions with every traced field equal, and every frame of 3,520-3,620 is
  equal; from 4,033 the frames show cov9's G8 (B3) kind. The tick line gets `"lag_split": [[i,
  stored, loaded], ...]` (lag VBlank `i`, the bytes stored and the source
  bytes loaded by then, from the call's own `r0`-`r2` noted at its `swi`), and
  the lag replay makes the same call in three parts around that VBlank: the
  stored part, the VBlank at the GBA's event position, then the loaded unit
  (read before the VBlank, as on the GBA) and the rest (read after it); a fill
  takes its value before or after as the GBA did. Exact to the instruction.
  A lag VBlank inside a main-loop BIOS copy into other memory (no event) is
  listed as `lag_in_other_copy`, and the replay runs it after the copy; the
  compare lists those ticks (TAS same-buttons run, ticks 0-8,254: 673, 2,158,
  2,169, 2,428 and 7,218; the lag replay equals the GBA in every field and
  frame there). On that run one lag VBlank came inside a counted copy: tick
  6,431, `Copy2dChr`'s `CpuFastSet` to 0x06010000, `[[0, 384, 416]]` (12
  blocks stored, the 13th loaded but not stored), which the replay now splits
  as the GBA's record says (those words already held the copied values, so no
  frame changed). Route ch5: 17 such splits (some in the copy's prologue,
  `[i, 0, 0]`; the call's `r0`/`r1` can be unaligned, as at 26,482 in a copy
  into other memory, so the progress is taken from the call's own values),
  each run the
  same on the PC, and 123 ticks with `lag_in_other_copy`; every traced field
  equal on all 135,505 ticks.

Main-loop writes to video memory by CPU stores (the text glyphs) or by DMA are
not events; their timing within the frame is the raster replay's (section
"Raster-timing frames").

Kind 7 goes with the line interrupts of lag frames, which the lag replay runs
(section "Lag replay"). `HBlank_BgVerticalScroll` (the save menu's fog) takes
`frame_flip` at line 159 of every frame, and `SaveDraw_ScrollFogBG` flips it
at the end of the `savedraw` step, after a loop of about 50 lines. On the TAS
route both orders happen inside that one step: at tick 549 the flip came at
line 154 of the frame before the last lag VBlank, so the GBA's frame15 has the
new table; at tick 525 the lag VBlank came inside the loop, before the flip,
so the GBA's frame15 keeps the old one. With the flip as an event the replay
runs that lag frame's lines (and so the line-159 take) before or after it as
the GBA did, and both frames match, to the event: the take is at line 159's
HBlank, about one line before the VBlank, so a flip in the last line of a lag
frame would be taken old on the GBA and new by the replay (not seen; at 549
the flip was 5 lines earlier; G5 review). Other line handlers that take main-loop
state on a line (the minimap window, the world map, the map-animation and
battle-animation scanline buffers) have no hand-over event yet: their take
in a lag frame is placed at the lag VBlank's event, so a hand-over inside the
same proc step can still land on the other side. None did on the TAS route
(every frame of ticks 0-8,254) or route ch5 (every traced field, frame15
every 60 ticks): with the line interrupts and kind 7, nothing there changed
but tick 549's frame (section "Lag replay"). Without kind 7 the line
interrupts alone fixed 549 and broke 525.

**At a script's head** (kind 3, 2026-10-21, cov9 A2). A lag VBlank that
comes after a `RunProcessScript` entry and before anything of that script has
run is placed there on the PC too, not before the next event. Found on Ch2's
Supply screen (`ch2_start.sav` + `inputs/monkey/mkA_ch2_pp2_s3.keys`, tick
4,490): `RestartMuralBackground` starts `ProcScr_SlidingWallBg` on tree 0 with
`Proc_Start`, which sets `PROC_FLAG_STARTING`, runs the script's first commands
(`BackgroundSlide_Init`, then `PROC_REPEAT(BackgroundSlide_Loop)`) and clears
the flag. The GBA's fourth lag VBlank came at event 20, that
`RunProcessScript`'s entry, with the CPU at `_call_via_r1`'s `bx r1` (the jump
to the first command), so its tree-0 walk skipped the new proc (starting).
Placed before event 21, after `Proc_Start` had returned, it ran the loop once
more, and `gLCDControlBuffer` `bgoffset[3]` (BG3's x scroll) stayed one step
ahead on every 4th tick until the screen closed (291 ticks, 4,491-5,651).
The capture now notes such a VBlank as `"lag_head": [i, ...]` and the lag
replay runs it from `RunProcessScript`'s `-finstrument-functions` entry hook,
right after the event, before the body (`LagReplayHead`). The head is
`RunProcessScript` after its first instruction up to its `bl _call_via_r1`
(0x08003396-0x080033CE), then the veneer's `bx r1` (0x080D18C4) and the
command's first instruction, with the main loop's `lr` (from
`runner_irq_reg`) still the call's return address 0x080033D1. The loop's later
passes run the same instructions, so a head also needs that no command of
this call has returned: a fourteenth breakpoint, `gate0_rpscmd` at
`RunProcessScript+0x3C` (0x080033D0, after the call), not an event. The PC
runs it before anything `RunProcessScript` reads of the proc. The GBA's may
have come after some of those reads (inside `RunProcessScript` past its first,
the `proc_script` load at `RunProcessScript+4`, 0x08003398; the IRQ's pc is
the instruction not yet run) or after all of them, with the command already chosen: at the veneer's `bx`,
or at the command's first instruction (every `ProcCmd_*` handler,
0x080030E8-0x0800337C, sits below `RunProcessScript`, so the pc's order does
not tell). In all those cases the capture checks, after that VBlank's tree-0
walk, that the walk left what was read as it was: the proc's script, lock
count, idle callback and current command, and for a script in RAM that
command's opcode (`sProcessCmdTable` and scripts in ROM cannot change). If not,
it stops (never seen); the lag log (`GATE0_LAGLOG`) gives each head's place
("before its first read", "reads begun", "in the call") and a "head check"
line for each one checked. The replay's tick line gets the same `lag_head`,
and the compare checks it with `lag_at`. Results (2026-10-21, r9 harness):
`mkA_ch2_pp2_s3.keys` from `ch2_start.sav`, all 9,585 ticks through the lag
replay, went from `DIFFERENT` at 4,491 (`bgoffset[3]`) to `MATCH` (one head
VBlank, at 4,490; frame 6,540 a raster-timing frame), and every frame of
4,485-5,700 from the snapshot matches (4,999 a raster-timing frame); `mkB_ch4_start_s2.keys` has
one head VBlank too, also run there. The scripted 60,000 ticks have none: its
GBA trace, lag replay and compare are byte for byte what they were. The
CHECK_LONG routes have 24 (GBA captures, 2026-10-21, r9 harness review and
fix pass): `route_ch5` 4 (34,181, 41,777, 59,429 and 102,811; `route_ch1`-`ch4`
are its prefixes), ch7 1, ch8 3, e9 4, e10 3, e11 3, e12 3, e13 3, ch5x and
ch6 none. Six came in the call: three at the veneer's `bx` (e9 20,334, e12
78,550, e13 33,055) and three at a command's first instruction (`route_ch5`
59,429 and e10 42,116 at `ProcCmd_SET_NAME`, e9 27,678 at
`ProcCmd_WHILE_EXISTS`). One came before the first read (e12 8,556, pc
0x08003398) and the other 17 inside `RunProcessScript` after it. All 23 past
the first read were checked, and none stopped the capture. On the PC:
`route_ch5` through `make route` is `MATCH` on 0-135,504, frames included; on
ch7, e9, e11, e12 and e13 the lag replays with and without the head rule give
the same traced fields (`lag_head` and the frame15 samples aside) up to each
route's known stop, and every head the PC reaches has the GBA's `lag_head`.
The heads after those stops (all of ch8 and e10, two of e9 and of e11, one of e12, the
three of e13) are not proven on the PC yet. The new breakpoint is hit 390,146
times on the scripted ticks (`RunProcessRecursive` 742,066), which costs the
capture 1.3% more instructions (perf; 0.84% in the review's own ABBA runs);
its time was within the run-to-run spread (71.4 s against 71.2 s, two runs
each in ABBA order, other agents' runs beside them). Not covered: a
lag VBlank inside such a starting script's commands (inside
`BackgroundSlide_Init`, say) is still run before the next event, after
`Proc_Start` has cleared the flag; an event at that clear would place it, if a
run ever shows one.

**GBA side** (`oracle/gate0_capture.lua`): five more debugger breakpoints for
kinds 1-5: `RunProcessRecursive` at 0x08002E28 and at its only epilogue
0x08002E7E (both returns go there), `RunProcessScript` at 0x08003394, and the
`bx lr` of `GetGameClock` (0x08000D2C, the value already in `r0`) and of
`SetGameTime` (0x08000D38, the clock already written). Events while
`OnVBlank`'s tree-0 walk runs are left out: at `OnVBlank` the capture notes the
`RunProcessRecursive` nesting depth (if tree 0 has a root) and ignores events
until the walk returns to it. An IRQ taken at one of the two entry
breakpoints, before its instruction ran, means that event had not started:
the runner's `_irq`/`_resume` callbacks say so, and a lag VBlank in between is
placed before that event. The handler can reach the same function before the
CPU is back (`OnVBlank`'s tree-0 walk calls both): the runner passes that on
as a call of its own (it knows the CPU is back by the interrupted code's stack
pointer, section "Headless Lua"), inside `OnVBlank`, so it is left out like
the walk's other events. The capture checks on every tick that it placed as
many lag VBlanks as the lag counters show (`onvblank_calls - (vblank_waits -
1)`), and stops otherwise. `GATE0_LAGLOG=PATH` also writes one line per lag
VBlank with the event it followed.

Kind 6 (format 4): four more breakpoints, at the `swi` of each libagbsyscall
wrapper (`CpuSet` 0x080D1678, `CpuFastSet` 0x080D1674, `LZ77UnCompWram`
0x080D1694, `LZ77UnCompVram` 0x080D1690; the destination is `r1`). Kind 7:
one at `FlipBgVerticalScroll`'s `bx lr` (0x08014EB8, after the flip's store;
an exit like the clock events, so a lag VBlank inside the function comes
before it). A call is the main loop's when no IRQ frame is on the IRQ stack
(`runner_irq_sp()` is `__sp_irq`): every game IRQ handler runs inside one
(IrqMain), so the calls of `OnVBlank` (the flush, the tree-0 tasks) and of
the HBlank and VCount handlers are left out, which is the PC's rule
(`gPortIrqDepth` 0). (The G5 patch left out `OnVBlank`'s calls only, with a
breakpoint at its `pop {r0}`, and showed that leaving out every handler gives
the same `events` and `lag_at` on all 135,505 ticks of route ch5; the IRQ
stack does that without the breakpoint.) Thirteen breakpoints in all
(fourteen since 2026-10-21: `gate0_rpscmd`, "At a script's head" above). An IRQ
taken at a wrapper's `swi` before it ran is handled as at the other entry
breakpoints. Cost: route ch1's GBA capture (21,273 ticks) took 62 s instead of
47 s with the eight breakpoints of format 3 (+34%, G5's fourteen, two runs
each, 2026-10-20); the trace is the same apart from `events` (997 ticks) and
`lag_at`. Since 2026-10-21 the runner checks the breakpoint addresses itself
("Headless Lua"): the same capture, with the raster log, takes 26.1 s (65.2 s
with the runner before, 47.4 s for `port-tas2`'s eight breakpoints without the
raster log; one run each, one after the other), with byte-identical outputs.

**PC side** (`pc/gate0_hook.c`): `GetGameClock` and `SetGameTime` through
`--wrap`, the two `proc.c` functions through `-finstrument-functions` on
`proc.o` (they are file-static and called from inside `proc.o`, which `--wrap`
cannot reach; the hook finds their addresses in the binary's own symbol
table). Events inside `OnVBlank` are left out the same way. Kind 6: the four
BIOS functions of `port/bios/bios.c` through `--wrap` (the game's calls, also
through `bmlib.c`'s `Decompress` table, come from other objects), counted
when the destination is video memory and no game IRQ handler runs
(`gPortIrqDepth`, `port/hw/frame.c`); a `CpuSet` or `CpuFastSet` that the
GBA's `lag_split` names is made in three parts around the lag VBlank
(`LagSplitCall`), and the replay's tick line gets the same `lag_split` (the
compare checks it with `lag_at`). Kind 7: `FlipBgVerticalScroll` through
`--wrap` (its only caller is `savedraw.c`), counted before the call: the GBA
counts it after the flip, so a lag VBlank before it ran before the flip.
Still not resolved there: `HBlank_BgVerticalScroll` takes the table at line
159's HBlank, about one line before the VBlank, and the replay runs that take
with the lag VBlank, so a flip in the last line of a lag frame is taken old on
the GBA and new here (none seen; G5 review).

**Checked on the GBA** (60,000-tick scripted route, `GATE0_LAGLOG`): the lag
positions agree with round 1's probes. At tick 9,342 the lag VBlank comes 42
events into the tick, after the step before the talk proc's (`ProcScr_GmapRM`
returns), so during the talk's glyph loop and before `TalkSpritePrepNextChar`
starts `ProcScr_TalkSpriteShiftClear`; no extra scroll step. At tick 12,402 it
comes after event 51, the talk proc (`gProcScr_Talk`) returning, so after
`CleanTalkObjects` has started the tree-0 scroll proc; one extra step. At tick
9,072 it comes right after `RunProcessScript` of `ProcScr_0859160C`, inside
`CleanTalkObjects`. At tick 4,661 all three come after the new-game
`SetGameTime(0)` (`WriteNewGameSave`). At tick 41,188 two come before and one
after `gProcScr_SideWindowMaker`'s script starts (it calls
`InitPlayerPhaseInterface`), as round 1's probe found. The
lag replay's event count equals the GBA's on every tick (below), so the PC
sees the same sequence. No lag VBlank on this route came at the first
instruction of an entry breakpoint (0 of 501 `before` lines in the log), so
the `_irq`/`_resume` correction is in place but not exercised yet.

**The routes exercise it** (2026-10-20, branch `port-events`). Route ch1 has
two enemy-phase intros where the VBlank IRQ came at `RunProcessScript`'s first
instruction: tick 6,541 (`gProcScr_PhaseIntroBlendBox`, started by
`ProcScr_PhaseIntro`) and tick 11,912 (`gProcScr_ResetCursorPosition`). Until
then the runner took the first report at that address after the IRQ for the
CPU being back. Here it was tree 0's call (`sProc_BMVSync`'s script, from
`OnVBlank`): the runner dropped it as the repeat and then passed the main
loop's own report on as a second call, so the capture counted that
`RunProcessScript` twice. The GBA traces had one main-loop event too many
(34 and 32; the lag replay had 33 and 31), and at 11,912 the tick's last lag
VBlank, after the event, was recorded at 31 instead of 30, so the replay ran
it at the sample instead of before `gProc_MapTask`'s `RunProcessRecursive`
returned. (The same at `RunProcessRecursive`'s entry would have dropped tree
0's entry and kept its return, so the capture's nesting count would have been
one short and it would have left out main-loop events after it; no route had
that.) Traced with a scratch per-event log on both sides (`GATE0_LAGLOG`'s
`describe` for every event of the tick, and the PC hook's events with each
proc's script): the two sequences are the same apart from that one repeated
`RunProcessScript`. With the runner fixed the GBA traces have the PC's counts
(33 and 31; `lag_at` `[3,3,3,7]` and `[3,3,3,30]`) and nothing else changes:
route ch5's GBA trace (135,505 ticks) differs from the one before the fix
only in `events` at those two ticks and `lag_at` at 11,912 (raw bytes the
same), the scripted route's GBA trace, raw bytes and frames are byte for byte
the same (its Gate 0 verdict too: LAG-ONLY, 25,557 lag-only, 0 not, 0
replay-only), and the runner's exit statistics (calls and repeated reports
per breakpoint) are the same numbers on both routes. The lag replay's event
count and lag positions now equal the GBA's on every tick of route ch5; its
compare still ends DIFFERENT, on the 27 phase-intro frames only (section
"Chapter saves", tick 5,160).

### Early flush VBlank

Every main loop ends the same way: `gBmSt.main_loop_ended = 1`, then
`gBmSt.prevVCount = REG_VCOUNT`, then `VBlankIntrWait()` (`OnMain`, `bm.c:348`;
the battle animations' `MainUpdate_8055C68`,
`banim-ekrbattlestarting.c:93`; `InBattleMainRoutine`,
`banim-ekrbattle.c:218`). Rarely a VBlank comes in the few instructions
between the flag and the call. `OnVBlank` then sees the flag set, so it
flushes the shadow state and clears the flag, as the tick's own VBlank would.
`VBlankIntrWait` (the BIOS waits for a new VBlank) is entered with the flag
clear and waits one more frame; the VBlank that ends it runs `OnVBlank`
without a flush. So the tick has one VBlank more than usual, and its flush
came first.

Until 2026-10-20 the GBA capture (lag check v2) did not follow this: it saw a
wait with the flag clear, took it for an inner wait, and stopped at the next
sample ("4 lag VBlanks placed, the counters say 5"; route ch2 at tick 28,865,
so the long check's route rows ch2-ch5 had no compare). Now
(`port-integrate4`):
- **GBA** (`oracle/gate0_capture.lua`): an `OnVBlank` entered with the flag set
  while the main loop's `VBlankIntrWait` has not been entered is an early
  flush VBlank. It is recorded as a lag VBlank at the tick's last event (its
  `lag_at` entry is the tick's event count), the tick line gets
  `"early_flush":1`, and the tick is sampled at the main loop's
  `VBlankIntrWait` call that follows (flag 0, after the flush). The VBlank that
  call waits for is the tick's own, not lag, so the lag counters still add up
  (`onvblank_calls - (vblank_waits - 1)` counts the early one). Each key
  mask still goes to one `OnMain`, in order. `oracle/sram_capture.lua` counts
  ticks the same way.
- **The bot's ticks.** `route_bot.lua` (and `route_check.lua`) count a tick at
  each frame callback that sees the flag set. At an early flush the callback
  usually sees it (route ch5 ticks 28,865 and 75,618: the bot counts the tick
  one frame before the capture writes it, and the next ticks line up again).
  At 108,956 the frame event came a few cycles before the flag store and the
  IRQ after it, so the bot saw the flag clear, counted no tick there and gave
  the next `OnMain` the same key mask again: from there the run the bot
  recorded is one `OnMain` behind the capture's (checked 2026-10-20 with a
  scratch copy of `route_check.lua` that logs each tick). It is the same play:
  both runs go through the same 1,190 changes of RNG state, chapter, phase and
  turn at the same tick numbers, and end in the same state (chapter 5, turn 1,
  RNG `a3b069620808`/`02652cda`); only the frames around the changes differ.
  Not so in Eirika's Chapter 13 (2026-10-21, cov9 bot step 3): at an early
  flush in a battle animation (tick 19,196) the bot counted the tick a frame
  after the capture, and for 632 ticks (and again from 61,228) each key mask
  reached the game one `OnMain` late; the capture of that file played a
  different game (it never reached turn 12, so `sram-capture save:14` found
  no save; it was stopped by hand after 7 minutes). So the route tools now have `ROUTE_TICK=capture`
  (`oracle/route_lib.lua`, "Tick rules"; it needs `MGBA_RUN_BP=$(GBA_BP)`):
  the capture's own rule, early flushes included. `route_e11.keys` to
  `route_e13.keys` and Ephraim's `route_ch8i.keys`, `route_i9.keys` and
  `route_i10.keys` are made with it (their `# Generated` line says so);
  the older files keep the frame rule, which still makes them byte for byte.
  (`route_ch8i.keys` plays Chapter 8 as `route_ch8.keys` does, but from tick
  18,362 its masks come one tick later, three by the seize: the frame rule's
  late ticks at early flushes.)
- **PC** (`pc/gate0_hook.c`, lag replay): the early flush VBlank runs at the
  sample point, after the tick's other lag VBlanks, with
  `main_loop_ended` as the main loop left it (1): the game's `OnVBlank`
  flushes and clears it there. Then the tick is sampled (flag 0, flushed, as
  on the GBA), and the frame step's `OnVBlank` finds the flag clear and does
  not flush, as the GBA's next VBlank does. The replay's tick line has
  `"early_flush":1` too, and the compare checks it with the positions: a
  replay that ran no early flush where the GBA had one (or one where it had
  none) leaves the GBA's sequence there.
- The plain lag-free PC run has no such VBlank: there the tick is sampled
  before its flush, so it differs from the GBA in the flushed state and is
  judged by the lag replay, like any other lag effect.
- **At the call itself** (2026-10-22, `port-r10-sound`, H-story-2): the IRQ
  can also be taken at `VBlankIntrWait`'s first instruction (`movs r2, #0` at
  0x080D16DC, the `gate0_wait` breakpoint), after the breakpoint was reported
  and before that instruction ran. The call has not been entered then either,
  so when that IRQ's `OnVBlank` flushes, it is an early flush: the BIOS's
  `IntrWait`, entered after it, discards that VBlank's flag (mGBA's HLE BIOS:
  `bic` of the requested bits in 0x03007FF8 before its `halt`) and the wait
  returns on the next VBlank, which does not flush. The capture had sampled
  the tick at the report, before the flush, and then counted that next VBlank
  as a lag VBlank of the following tick at event 0. Route ch7 tick 41,152
  (probe of the GBA from `ch7_start.sav`: the frame callback of VBlank 43,300
  came before the report, `gate0_wait_irq` fired at the next step, `OnVBlank`
  ran with the flag set, interrupted PC 0x080D16DC, and the CPU was back at the
  call at line 217 with `INTR_CHECK` 1; VBlank 43,301 then ended the wait,
  `OnMain` 41,153 entered at line 206): the trace said `"vblanks":2` and no
  `lag_at` for 41,152, `lag_at [0,21]` for 41,153, and the sound index put
  41,152 on record 43,301 where the PC's lag replay, which flushes in its
  frame step, put it on its record for 43,300 (D one more on that tick,
  `snd-route` check 3). Now the capture withdraws the sample taken at the
  report (its counters, the per-tick lists and the raw dump's position are
  kept until the instruction runs; `gate0_wait_irq` comes at the very next
  step, so nothing of the game ran in between) when the IRQ taken there runs a
  flushing `OnVBlank`, records that `OnVBlank` as the tick's early flush, and
  samples the tick at `gate0_wait_resume`, the CPU back at the call: tick
  41,152 is `lag_at [20]`, `"early_flush":1`, its fields after the flush;
  41,153 is `lag_at [21]`; the index stays 43,301 (the VBlank the wait
  returned on). The PC side needs no change: its early flush runs at the
  sample, as above. An IRQ there whose handler does not flush (an HBlank or
  VCount handler) leaves the sample; one whose frame callback came after the
  report (the line written already) is left as before. It was not seen, but
  by the measured timing it can occur (an event at the `bl2` step's start
  makes the IRQ due before the `movs` step), probably as the known OnMain-count
  records of a lag VBlank before the main callback's entry; it stays
  consistent with the PC (the line indexes the flushing VBlank, the next one
  is tick T+1's event-0 lag VBlank). Unverified. The end
  line of the capture counts them ("... came at the main loop's
  VBlankIntrWait call"). `route_lib.lua`, `sram_capture.lua` and
  `sram_boost.lua` count the tick at the report as before: the same tick, one
  flushing `OnVBlank` earlier in the same frame (about 57 lines, some 70,000
  cycles: the report at line 160, the retake at `gate0_wait_resume` at line
  217 at ch7 41,152), before the next `OnMain` (keys are read only in
  `UpdateKeyStatus`, hardware.c).

On the routes (2026-10-20): none on ch0, ch1 or the 60,000-tick scripted
route (its traces are byte for byte those of the capture before this
change, GBA and lag replay); route ch2 has one, at tick 28,865 (after 19
main-loop events, in the battle animations' `MainUpdate_8055C68`), and route
ch5 two more, at 75,618 (31 events) and 108,956 (23 events), both in
`InBattleMainRoutine`. The lag replay runs all three at the GBA's position.

### Lag replay (the lag check, part of the result)

Decided 2026-10-19 (`port-lagcheck`, replacing the clock replay as the lag
check; tightened in the round 2 review, `port-integrate`): a difference is
labelled lag-only only when it is reproduced by the lag replay at the GBA's
positions, that is, when replaying the GBA's lag VBlanks where they came
reproduces the GBA there. That is what "lag-only" means everywhere in this
file: reproduced by the lag replay, not shown by other means to be GBA lag.

`make -f port/test/Makefile pc-lag-replay KEYS=... TICKS=...` (after
`gba-trace`) runs the PC again with `GATE0_LAG_FROM` set to the GBA trace. For
each GBA lag VBlank of tick t at position k (`lag_at`), the hook runs the
game's own `OnVBlank` just before main-loop event k of tick t, or at the
sample if k is the tick's last position, with `gBmSt.main_loop_ended` 0 as on
the GBA (no flush). An early flush VBlank (`early_flush`, section "Early flush
VBlank") runs last, at the sample, with the flag set, so it flushes as on the
GBA. It does not touch the clock: `IncrementGameClock` in those
runs, and the game's own `SetGameTime` calls in the same order, give the GBA's
values. Inner `VBlankIntrWait` VBlanks are not lag VBlanks; the PC runs them
anyway. Each lag VBlank (the early flush one too) also gets the line
interrupts of the frame it ends, which the PC never steps through (format 4,
2026-10-20): `port/hw/frame.c`'s `PortLagFrameLines` runs lines 161-227 and
0-159 (VCount and HBlank IRQs, nothing drawn) and line 160's VCount IRQ
before that `OnVBlank`, and line 160's HBlank IRQ after it, in the frame
step's order (with the frame step's own line code; compiled into the Gate 0
binary only, `FE8PC_GATE0` from `pc/gate0.mk`, so no shipped build has it).
Without them a handler that takes main-loop state on a line
missed every take in a lag frame (TAS route tick 549, `HBlank_BgVerticalScroll`
and the save menu's fog; kind 7 in "Lag position"). The line handlers make no
main-loop events (the event counts stay the GBA's). Besides frames, the only
field this changed on the TAS route and route ch5 is `gBmSt.prevVCount` at
tick 0 (0x0000 to 0x00A0: the lag frame's lines set `REG_VCOUNT` before tick
0's lag VBlank), which the rules leave out (G5 review). The replay writes `lag_at` with the event count at which it ran each
one (the GBA's, unless the PC tick ran out of events first) and its own
`events`; the trace header records `lag_from`. Output:
`build/gate0/pc_lagreplay.jsonl`, `pc_lagreplay.raw`, `pc_lagreplay.log` (whose
last line counts the VBlanks run at the GBA's position and the ticks whose
event count is not the GBA's). `gate0` and `scripted` run it between
`pc-trace` and `compare`. Since `port-snd10` each lag VBlank also makes its
frame's sound (the sound record before the `OnVBlank`, the FIFO bytes, PSG
and samples after it, as line 160 of a frame step does), and a GBA trace with
sound positions (`lag_snd`) places each one among the main loop's m4a calls
too: section "Sound on any route". Neither changes a traced field (route
ch1: the traces and raw dumps of the old and new binary are byte-identical).

The compare (`--lag-replay`) first prints the replay as a whole: how many GBA
lag VBlanks it ran at the GBA's position, and on how many ticks its event
count differs from the GBA's. Then it runs its RESULT pass on the replay with
the same rules except the clock: the replay has the GBA's lag VBlanks, so its
`gGameClock` and the `gPlaySt` clock copies must equal the GBA's outright (the
compare's `clock_outright`; no offset is excused, so an extra or missing
`OnVBlank` in the replay shows as a clock difference). It labels every RESULT
difference of the PC trace:
- **lag-only**: the whole tick is reproduced by the lag replay at the GBA's
  positions. At tick t the replay's RESULT has no difference in any field,
  and on every tick up to and including t its event count is the GBA's and it
  ran that tick's lag VBlanks at the GBA's positions (strict prefix: a replay
  that ran one lag VBlank elsewhere, or did other main-loop work, is not the
  GBA's lag from there on, so no later tick is lag-only; the compare then
  prints a NOTE with the tick). Then every field that differs at t is
  lag-only. One replay reproduces every field of the tick; nothing is
  excused field by field or by two different runs.
- **not lag-only**: any other tick, every field of it. The label carries the
  replay's own difference in that field, or why the tick as a whole is not
  reproduced (other fields differ in the replay, or the event count or
  positions differ, at this tick or before it).
- **lag-only, raster-timing frame** (2026-10-20, `port-harness8b`; the G4
  review): a tick that would be lag-only, except that the replay's `frame15`
  matched only as a raster-timing frame (section "Raster-timing frames": the
  replay's frame, drawn again with the GBA's timing, is the GBA's). Its
  `frame15` gets this label, its other fields `lag-only`. Such ticks are
  counted and listed apart from the lag-only ones ("lag-only with a
  raster-timing frame", JSON `lag_only_raster_ticks`), and they do not make
  the verdict DIFFERENT. (Before, the replay's labelled frame turned the plain
  run's frame difference into a plain "lag-only" with nothing listed: TAS ticks
  3,025, 3,043, 3,948 and 8,241, where the plain run's VRAM is not the GBA's
  without the lag VBlanks.)
- **clock rule**: a `gGameClock` or clock-copy rule failure in the PC trace.
  The rule already takes the lag out, so this is never lag-only.
- **no replay tick**: past the end of the replay trace (a fault); not lag-only.

Oracle-limit frames (P2.2b) and raster-timing frames are left out of RESULT,
so they never get a label. On the replay a frame is masked as an oracle-limit
frame only when the normal run's frame at that tick was one (not a
raster-timing frame) and the replay drew the same frame (the affine replay
belongs to the normal run); a raster-timing frame of the replay is the
replay's own proof.

The replay changes the PC run, so it is never compared alone as the result;
the normal PC trace is still the one compared. Ticks where only the replay
differs (`replay-only`) fail the gate (round 2 review): there the replay
changed the run in a way the GBA did not, a sign that a lag VBlank was placed
wrong or that its effect is not reproducible, so the replay cannot vouch for
the other ticks either. The compare lists them, prints the first one's
fields, and gives `GATE 0: DIFFERENT` (exit 1) even when every RESULT
difference is lag-only or there is none. The verdict `GATE 0: LAG-ONLY` (exit
3) needs every RESULT difference lag-only (or lag-only with a raster-timing
frame) and no replay-only tick.

**The remaining risk, in words.** On a lag-only tick the PC's own lag-free run
differs from the GBA, and the gate accepts it because the PC run with the
GBA's lag VBlanks added reproduces the GBA there. So on those ticks the PC's
own run is checked only through the replay: a port fault that happens to be
hidden (or exactly undone) by the replayed lag VBlanks would not show. On the
60,000-tick scripted route that is 25,557 ticks, 43% of the route (the compare
prints this share under its LAG CHECK). The other 57% are compared directly.

**The lag replay compared on its own** (`--pc pc_lagreplay.jsonl`, no
`--lag-replay`) uses the same outright clock rule and ends with
`GATE 0 (LAG REPLAY vs GBA): ...`, and it is `DIFFERENT` unless the replay also
ran the GBA's lag positions and main-loop events on every tick, and
`INCOMPLETE` when the replay stopped before the requested ticks or logged a
fault (`--pc-log pc_lagreplay.log`). It is not the
plain PC run's Gate 0 result. It is a diagnostic for `scripted`, and it is the
route check: `make -f port/test/Makefile route ROUTE_KEYS=... ROUTE_TICKS=...`
(the `route-chN-lag` rows of `port/check-all.mk`, `inputs/README-routes.md`,
"PC check") runs the GBA capture, the lag replay and this compare. The plain
lag-free PC cannot follow the bot routes, because the GBA's lag shifts game
state against the ticks the keys were recorded for (the NULL probe found the
PC leaving every route at tick 5,666); the lag replay keeps them.

**Long compares take turns.** A compare of 60,000 ticks or more (about 3.4 GB
at 60,000) runs under `~/.fe8-compare.lock` (`COMPARE_LOCK`,
`COMPARE_LOCK_TICKS` in `port/test/Makefile`, taken by `port/tools/check_all.py
lock`, which uses an inherited lock as is). The GBA capture and the PC runs do
not lock.

**What it reproduces:** every path from a lag VBlank into the game that goes
through `OnVBlank`, at the right point in the tick: the clock seen by each
read (round 1, cluster B: the gauge arrows, the hand, the rescue icon, the
range and green-text palettes), tree-0 procs getting their extra steps
(cluster A: the talk scroll at 12,402, so the glyphs at 12,480), clock edges
seen inside lag VBlanks (`SyncUnitSpriteSheet` at 21,648, so the frame at
21,660), and tree-0 procs ending earlier, which changes the proc pool's free
order (cluster C: `efxFlashBG` at 39,083, so `windowQuadrant` at 41,189).
**Not resolved below a proc step:** two lag-relevant actions inside one proc
step with no event between them (a step that both pushes low-OAM sprites,
which a lag `SyncLoOam` would copy and clear, and does something else that a
lag VBlank changes), and a lag VBlank after a tick's last event but before
`OnMain`'s own tail (`PushSpriteLayerObjects`): the replay runs it at the
sample. Low OAM is used on weather maps only (`InitOam(0x10/0x20)`,
`bmio.c`); none on this route.

**Not a lag position (TAS route, 2026-10-20, group 5; frame-only, every
traced field equal):** the main loop running while the frame is drawn. On a
lag tick the GBA draws the tick's frame15 while the main loop still runs (and
on any tick whose `OnMain` ends after line 0). Tick 460: `Text_DrawCharacter`
writes the difficulty text's glyphs into VRAM from line 1 to line 128 of that
frame, and the 'h' of "high" lands at line 125, after lines 123-125 were
drawn (7 pixels); the PC draws the frame after the whole tick. VRAM and
palette at the frame's end are equal on both sides. CPU/raster timing, the
P-17 family (`port-plan/DIVERGENCES.md`): the raster replay labels it
(section "Raster-timing frames").

### Known limits of check-all rows (`known_limits.json`, coverage 12's collate)

A few long route rows differ from the GBA only by a recorded limit of the test tool, not by the port.
The main one: the lag replay runs a GBA lag VBlank only between lag-position events, so a lag VBlank
that came in the middle of one step (inside a BIOS decompression, between a proc's start and its setup)
runs a little later on the PC run (DIVERGENCES section 5, "Lag check v2", not resolved: C10-10 (c),
coverage 11's N-C11-1). The plain PC has no lag at all, so no player meets these; but such a row fails
on every run, and a new difference would hide behind the old one.

`port/test/known_limits.json` lists them per row ID: each limit's ID, its kind, a note and the exact
fields and ticks it covers (`windows`: `FIELD: [[first, last], ...]`, checked against compare.json's
`result.ticks_per_field`; `sequence`: the lag replay's off ticks, `lag_replay_sequence.ticks`;
`gate0_compare.py` writes both). A limit may also list `sram`: `[[first, last], ...]` SRAM offsets (hex) in which the row's end-SRAM compare (`sram_cross.txt`, the bytes it marks `[NOT KNOWN]`) may differ; any not-known byte outside them fails the row (`0xOFFSET outside the known limits (ID)`). `port/tools/check_all.py` gives such a row the status **KNOWN** (not
PASS, not FAIL) when its compare is DIFFERENT only inside those windows and every PC run of the row is
complete with no fault (make's non-zero status is then the compare's DIFFERENT). The row's note names
each limit and its ticks, and the table's last line counts the KNOWN rows (`CHECK-ALL: PASS (3 known):
...`). A difference anywhere else fails the row as before (`outside the known limits (ID): FIELD at
TICK`), and a listed row that passes outright says `known limit ID not seen (fixed?)`, so a stale entry
shows. The windows were measured on `port-cover12` (21189271's game code, main `9ef060ea`) with the row
itself; a change that moves them shows as a failure, never as a pass.

| Row | Limit | Fields and ticks | What it is |
|---|---|---|---|
| `route-br9-lag` | N-C11-1 (the lag replay's limit, C10-10 (c)'s kind) | `gPaletteBuffer` 62,549-62,563 and 96,999-97,215 | Lagdou Ruins floor 9: palette 9 colours 12-15 (the map's flickering palette) one step out of order after GBA lag VBlanks inside an LZ77 decompression (coverage 11, `pending/c11-findings.md` 2a); the same colours again at the change to floor 10 (GBA lag ticks 96,999 and 97,000; inferred the same kind, not probed). No frame differs |
| `route-br9-lag` | P-44 (the reference GBA's DMA open-bus timing) | `sram` 6 bytes: pidStats[6] +0, +1 in suspend.0, suspend.1, game.0 | Tick 94,747: D-60's NULL-unit pid read (0x08026454) returned 0x07 after a DMA stall (mGBA `GBALoadBad`, dmaPC), so the GBA counted a loss for pid 7; an extra breakpoint before the read makes the GBA's SRAM equal to the PC's (port-plan `pending/fix-bwl.md`) |
| `route-e18-lag` | P-44 | `sram` 22 bytes: pidStats[1], pidStats[3] (deathLoc, deathTurn, deathCause, killerPid bits) in suspend.0, suspend.1, game.0, and playSt+0x3E (0x4002, the survival rank) | Frame 28,166: the same reads gave pid 4 and pid 2, so the GBA wrote trap-death records for them and its rank changed (0x40 against the PC's 0x60); same proof |
| `route-i21-lag` | LAG-FRAME (a frame on a GBA lag tick, every traced field equal: round 8's kind 2; "Raster-timing frames", "Not covered") | `frame15` 62,520 | Lyon's Naglfar in Ephraim's final part 1 |
| `route-er3-lag` | H-c12-pr-1 (the lag replay's placement after a `Proc_Start`, then the proc-slot knock-on and a retail uninitialised read: section 5's "Knock-on") | `gBG0TilemapBuffer`, `gBG1TilemapBuffer` 3 ticks at 66,706, 88,310, 101,076, 133,303; `sequence` (the main-loop events 45 against 44) next to them | Lagdou Ruins floor 3 with the real army: the map's side windows slide in a tick apart at four phase changes (`TerrainDisplay_Loop_OnSideChange` reads `windowQuadrant` from a recycled proc slot) |
| `route-gallery-lag` | H-r13-gal-1 (P-41: the same kind as H-c12-pr-1; the GBA's lag VBlank at 93,316 freed proc slot 56 before the next `Proc_Start`'s `AllocateProcess`, the replay runs it after) | `gBG0TilemapBuffer`, `gBG1TilemapBuffer` 1-4 ticks at 94,512, 96,645, 98,134, 104,697, 110,506; `frame15` 104,700; `sequence` 16 ticks 94,511-110,505 | the item gallery (cheat-assisted), turns 21-26: the cursor resting on a unit, the side windows' cells a tick apart (`gProcScr_TerrainDisplay`'s step reads leftover proc-slot data) |
| `route-gallery-lag` | LAG-FRAME (C12-GAL-6: a frame on a GBA lag tick, the lag VBlank inside a step; fix round 14) | `frame15` 27,278; 120,882, 120,888, 120,894; 127,456; 129,436-129,448 (every 4th), 129,488, 129,492; 130,087 | the item gallery's Ivaldi, Gleipnir, Recover, Naglfar and Physic: one frame each on a GBA lag tick, every traced field equal; seen with every frame only |

### Clock replay (diagnostic)

The lag check from 2026-10-19 until the lag replay. It excuses nothing now.
`make -f port/test/Makefile pc-replay KEYS=... TICKS=...` (after `gba-trace`)
runs the PC again with `GATE0_CLOCK_FROM` set to the GBA trace. Before each
`OnVBlank` after tick t's sample, the hook sets `gGameClock` to the GBA's
value at tick t, so the next `OnMain` reads the GBA's clock, lag included. The
trace header records `clock_from`. Output: `build/gate0/pc_replay.jsonl`,
`pc_replay.raw`, `pc_replay.log`. `gate0` and `scripted` no longer run it.

With `pc_replay.jsonl` present, `compare` passes it as `--clock-replay` and
prints a **CLOCK REPLAY** section: how many RESULT-differing ticks it would
match field by field (the old rule), its replay-only ticks, and which ticks
only one of the two replays matches. Its limits are why it was replaced: it
sets the clock at tick boundaries, so a read after a mid-tick lag VBlank sees
the old value (cluster B), it runs no tree-0 proc in a lag VBlank (clusters A
and C), and it skips the clock values a lag VBlank goes through
(`SyncUnitSpriteSheet`'s edges). `make -f port/test/Makefile clock-replay`
still compares the clock replay on its own (`compare_replay.json`).

## Scripted route

`inputs/scripted_a90.keys` is the `FE8PC_AUTOPRESS=0x001,90,4300` run in tick
terms: A on ticks 4,301 and 4,302, then 2 ticks of A every 90 ticks, 40,031
entries. `inputs/scripted_a90_long.keys` is the same pattern to tick 61,001
(its first 795 entries are the same). `make -f port/test/Makefile scripted`
(defaults: `scripted_a90_long.keys`, `SCRIPTED_TICKS` 60000) captures the GBA,
runs the PC, the lag replay and the affine replay with `PC_FAULT_OK=1` and
compares the ticks both reached, with the lag check. Its compare (60,000
ticks) takes `~/.fe8-compare.lock` by itself (section "Lag replay", "Long
compares take turns"); the capture and the PC runs may run beside other jobs. The older result further below used
`scripted_a90.keys` and 16,000 ticks (`SCRIPTED_KEYS=port/test/inputs/scripted_a90.keys
SCRIPTED_TICKS=16000`).

### Result (2026-10-19, branch `port-lagcheck`: lag replay at the GBA's positions)

`flock ~/.fe8-heavy.lock make -f port/test/Makefile scripted` (60,000 ticks,
trace format 3), then `pc-replay` and `compare` again for the clock-replay
diagnostic. The GBA capture with eight breakpoints took about 2 minutes
(60,514 frames, 245 lag ticks, 501 lag VBlanks, 1,638,876 main-loop events,
at most 91 in a tick); every old key of every tick equals the format-2
capture's and `gba.raw` is byte-identical, so the extra breakpoints do not
change the emulated machine. The PC trace and `pc.raw` equal the
uninstrumented `port-p22b` run's on every tick (outside `events` and
`gMainCallback`'s host address). RAW, RULES and RESULT are as before.

```
Lag replay: 501 GBA lag VBlanks in 245 ticks, run at the GBA's position on every one of those ticks;
  main-loop events equal to the GBA's on every tick
RESULT differs on 25557 ticks: 25557 lag-only (the lag replay matches every field there), 0 not
lag-only: first tick per field: gOam 8630, frame15 8940, gPaletteBuffer 33733, gBG0TilemapBuffer 41189,
  gBG1TilemapBuffer 41189, gLCDControlBuffer 56940
replay-only differences (the lag replay differs where the PC trace does not): none
== CLOCK REPLAY (diagnostic, excuses nothing)
field by field it matches 25390 of the 25557 ticks RESULT differs on, not 167; replay-only 0
matched by the clock replay but not the lag replay: none
matched by the lag replay but not the clock replay: 167: 12480, 21660, 25383, 33660, ...
GATE 0: LAG-ONLY on ticks 0-59999: every RESULT difference is reproduced by the lag replay at the
  GBA's positions (first at tick 8630); ...
```

(The last line is given in today's wording; the run printed the older text.)

Before (clock replay as the lag check, same route): 25,390 lag-only, 167 not
lag-only, 0 replay-only. After: 25,557 lag-only, 0 not lag-only, 0
replay-only. The 167 are round 1's three clusters, which round 1 traced to
the GBA's lag and which the lag replay at the GBA's positions now reproduces:
the talk scroll and the sprite sheet edge (12,480, 21,660 and the frames that
follow lag through tree-0 procs), the mid-tick clock reads (`gOam` from
25,383, palettes from 41,950), and the proc slot swap (BG0/BG1
41,189-41,191).

The lag replay compared on its own with the GBA (`--pc pc_lagreplay.jsonl`, a
diagnostic) is equal byte for byte in every field on every tick, `gGameClock`,
`onvblank_calls`, `events`, `gOam`, the palettes and all four BG maps
included, apart from `gBmSt.prevVCount` (raster timing), `bg2affin` on the 44
OL-2 ticks and the OL-2 frame 5,880 (no affine replay passed there). So on
this route the PC run with the GBA's lag VBlanks, placed by this measure,
reproduces the GBA trace in every traced field apart from those three.

Controls (ticks 0-12,999, each replay compared on its own with the GBA): with
the exact positions only the OL-2 frame 5,880 differs; with every lag VBlank
moved to the end of its tick (round 1's prototype) frames 9,420, 11,580 and
12,840 also differ and the event counts differ on 8 ticks; with every one
moved to the start of its tick 383 ticks differ (380 in `gOam`). The position
matters, and this one is right on this route.

Negative checks (ticks 0-12,999, mutated copies, not committed): unchanged,
exit 3 (LAG-ONLY). Exit 1 (reported): one `gRNSeeds` byte changed in both the
PC trace and the lag replay at 8,630 or at 100 (a port bug in both runs); the
replay alone changed in another field at 8,630 (the whole tick is then not
lag-only, `gOam` included); the replay's `events` changed at 8,630; the
replay's `lag_at` moved by one at 8,609 (a tick with no RESULT difference:
every later tick is then not lag-only); the replay's clock +1 at 8,630; the
replay trace ending at 9,999; only the clock replay given (no lag check).
Exit 3 with a listed replay-only tick: the replay alone changed at tick 100
(that was `port-lagcheck`; since the round 2 review a replay-only tick fails
the gate, exit 1). Exit 2: a GBA `lag_at` entry removed at 4,143 (does not add up to the lag
counters); the PC trace or the clock replay given as `--lag-replay`; a lag
replay from another binary (`exe_crc32`) or with other keys; `--pc-elf` not
the binary that made the PC trace.

Round 2 review (`port-integrate`, 300-tick `gate0` traces, mutated copies of
the lag replay, not committed): its `gGameClock` +1 at tick 150 only gives a
replay-only tick at 150 and `GATE 0: DIFFERENT`, exit 1 (before: listed,
exit 0). Its `gGameClock` and `onvblank_calls` both +1 from tick 150 on (an
extra `OnVBlank` the replay did not record) gives 150 replay-only ticks and
exit 1 under the outright clock rule; the compare before it gave `GATE 0:
MATCH`, exit 0, because the clock rule took the extra VBlank out as lag.

The 300-tick `gate0` (`boot300.keys`) gives `GATE 0: MATCH` (3 lag VBlanks in
3 ticks, all placed, events equal). The no-key lights (`nokeys.keys`, 6,700
ticks, `GATE0_SHOT_TICKS=6560-6660`) give `GATE 0: MATCH`, all 88 rotated
frames passing through the affine replay as before (29 lag VBlanks, all
placed, events equal).

### Result (2026-10-19, `port-d1a-spike` after merging `port-gate0`, `port-sdl` and `port-win`)

`make -f port/test/Makefile scripted` (60,000 ticks). The GBA took 60,514
frames (245 lag ticks, 501 lag VBlanks); the PC and the clock replay ran all
60,000 ticks with no fault (55,744 `OnMain` calls on all three). About 4
minutes in all. Compared again with the strict P2.2b frame rule (branch
`port-p22b` on `0da9e831`: the same GBA trace; the PC run, the clock replay
and the affine replay from this branch's test binary, 53-56 seconds each, with
`pc.raw` and `pc_replay.raw` byte-identical to the earlier runs'; compare 36
seconds, 3.4 GB):

```
Ticks without an OnMain (another main callback, such as the battle animations'
  MainUpdate_8055C68): 24965-25812, 38864-39424, 44353-44961, 45238-45604, 46479-47115, 51733-52966
gGameClock: excused offset at tick 59999: 472; clock writes at ticks: 4661
frame15: GBA has 1000 frames, PC has 1000
Affine replay: frames drawn with the GBA's bg2affin: 1, 1 of them equal to the GBA's frame;
  equal: 998; none: 1
Affine replay against the PC trace: outside the frames equal on every tick; redrawn with
  the PC's registers, all 1 equal the PC's frame; frames with nothing replaced all equal the PC's
== RULES: first differing tick 5876 (gLCDControlBuffer bg2affin); 25601 of 60000 ticks
== RESULT: first differing tick 8630 (gOam); 25557 of 60000 ticks
mask OL-2: covered 44 ticks, 5876-5922 (0x48 max delta (1, 183))
oracle-limit frames (P2.2b), masked: 1: 5880 (the previous tick differs only inside OL-2;
  BG2 shown; the affine replay draws the GBA's frame)
== LAG CHECK
RESULT differs on 25557 ticks: 25390 lag-only (the clock replay matches there), 167 not
lag-only: first tick per field: gOam 8630, frame15 8940, gPaletteBuffer 33733, gLCDControlBuffer 56940
not lag-only: first tick per field: frame15 12480, gOam 25383, gBG0TilemapBuffer 41189,
  gBG1TilemapBuffer 41189, gPaletteBuffer 41950
first difference left: tick 8630 (lag-only), gOam
first difference that is not lag-only: tick 12480, frame15
GATE 0: DIFFERENT on ticks 0-59999: first difference at tick 8630, first that is not lag-only at tick 12480
```

Before the frame rule the same traces gave `RESULT: first differing tick 5880
(frame15); 25558 of 60000 ticks`, 168 not lag-only, and tick 5,880 labelled
"oracle-limit frame" as the first difference left.

Reading it:
- Tick alignment, the clock rule and the clock copies hold on all 60,000
  ticks. `gLCGRNValue`, `gRNSeeds`, `sKeyStatusBuffer`, `gBmSt` (without
  `prevVCount`), `gPlaySt`, `gMainCallback` and the BG2 and BG3 tilemaps are
  equal on every tick.
- Tick 5,880: the title-screen lights (OL-2) seen in the frame (tick 5,879:
  only `bg2affin` differs, inside OL-2; mode 1, BG2 on). Masked by the P2.2b
  rule: drawn with the GBA's `bg2affin` of tick 5,879 the PC's frame is the
  GBA's (`7f8e62bc`), and redrawn with its own registers it is the PC trace's
  (`2e28b4d6`). It is the only frame inside OL-2's ticks (frames come every 60
  ticks). RAW, RULES, RESULT and the lag check are the same as with the first
  version of the rule; the PC frame at 5,880 replaced by an arbitrary hash now
  fails (`first that is not lag-only at tick 5880`).
- Tick 12,480: 32 pixels on lines 141-148, the story text "...threatened by
  a": the GBA frame already has the last glyph, the PC frame not yet; every
  traced buffer is equal at tick 12,479 (glyphs go to VRAM, which is not
  traced). The GBA lagged one VBlank at tick 12,402. Likely lag that does not
  go through the clock (a proc that runs once per VBlank), not checked then.
- 167 ticks are not lag-only in all: frames, `gOam` from 25,383 (inside a
  battle animation), BG0/BG1 at 41,189 (the sliding window one tick behind)
  and palettes from 41,950. Not traced yet. (Superseded: round 1 traced
  them, and since `port-lagcheck` the lag replay at the GBA's positions
  reproduces all 167; result above.)

### Result (2026-10-19, `port-gate0` at `ee874c9f` plus this harness)

The PC build faults at frame 15,800 (P-11 in `port-plan/DIVERGENCES.md`:
`GetFaceDisplayBits(NULL)` in a Prologue talk), so the PC trace has 15,800
complete ticks and ticks 0-15,799 are compared. The GBA trace took 16,142
frames for 16,000 ticks.

```
GBA: tick 0 at frame 15; ticks with lag VBlanks: 52, 129 VBlanks; first: 0 (+1), 1 (+1),
  3791 (+1), 4143 (+3), 4304 (+1), 4305 (+1), 4309 (+1), 4392 (+1), 4410 (+7), 4499 (+2),
  4600 (+10), 4661 (+3), ...
PC:  tick 0 at frame 1; ticks with lag VBlanks: 0
Ticks with more than one VBlankIntrWait (game waits inside OnMain): none
gGameClock: excused offset (GBA lag VBlanks counted by the clock) at tick 15799: 100,
  largest 100; clock writes at ticks: 4661

== RAW: first differing tick 0 (onvblank_calls, gGameClock, gBmSt); 15800 of 15800 ticks
== RULES: first differing tick 5876, gLCDControlBuffer +0x4A (bg2affin) GBA 25 PC 26;
   996 of 15800 ticks; fields: gLCDControlBuffer 5876, gOam 8630
== RESULT
first differing tick: 8630; first differing field: gOam
fields differing at tick 8630:
  gOam: hash GBA 9cfeb572, PC 77a870c0; first byte +0x000 (OAM entry 0, attr0) GBA 82 PC 80; 1 bytes differ
ticks with any difference: 952 of 15800
first differing tick per field: gOam 8630
fields equal on every tick: gGameClock, gKeyStatusIgnoredSt, gKeyComboResetEN,
  gSoftwareResetFlag, gMainCallback, gLCGRNValue, gBmSt, gPlaySt, gRNSeeds,
  sKeyStatusBuffer, gLCDControlBuffer, gPaletteBuffer, gBG0TilemapBuffer,
  gBG1TilemapBuffer, gBG2TilemapBuffer, gBG3TilemapBuffer, onmain_calls, vblank_waits
mask OL-1: listed only, never applied (Open-bus reads from the BIOS area)
mask OL-2: covered 44 ticks, 5876-5922 (HLE BgAffineSet uses floating point; 0x48 max delta (1, 183))
mask OL-3: covered 0 ticks (HLE ObjAffineSet uses floating point)
```

Reading it:
- Tick alignment holds on all 15,800 ticks: one `OnMain` per tick and the same
  `VBlankIntrWait` count on both sides. The GBA's 52 lag ticks (129 lag
  VBlanks) no longer show as clock differences.
- The clock rule holds on every tick, across the new-game `SetGameTime(0)` at
  tick 4,661 (the GBA lagged 3 VBlanks after the write in that tick) and the
  `time_chapter_started` copy at tick 15,184 (offset 73, between the clock
  offsets 68 and 74 at the start and end of that tick, which had 6 lag
  VBlanks). Without the rule, `gPlaySt` would differ from 15,184.
- RNG: `gLCGRNValue` changes 254 times (from tick 4,410) and matches on every
  tick. `gRNSeeds` does not change on either side before tick 15,800: the route
  reaches no `NextRN` draw yet, so battle and AI randomness is wired in but not
  yet exercised. `sKeyStatusBuffer` (4,391 distinct values) matches on every
  tick.
- OL-2 covers ticks 5,876-5,922 (44 ticks), all in `bg2affin`, with matrix
  deltas of at most 1 and x/y deltas of at most 183 (bound 201).
- **First difference that remains: tick 8,630, `gOam` entry 0 attr0 (y), GBA
  0x82, PC 0x80.** It is the talk's "press A" arrow,
  `TalkWaitForInput_OnIdle` (`scene.c:1405`), whose sprite frame is
  `(GetGameClock() / 2) & 0xf`. The GBA's clock is ahead by its lag since the
  tick-4,661 reset (27 VBlanks at tick 8,630), so the arrow is at another
  frame. Every remaining difference (952 ticks, all in `gOam`) is of this
  kind: with the clock replay, RESULT matches on all 15,800 ticks (RULES only
  differ in the 44 OL-2 ticks). So no port divergence is visible before the
  P-11 fault; what remains is the GBA's lag seen through clock-driven
  animation.

### First comparison (2026-10-19, `boot300.keys`, ticks 0-299, trace format 1)

This is the first run, before the rules, the breakpoints and the new fields.
Its SUPPLEMENTARY pass was the forerunner of RULES.

```
GBA: tick 0 at frame 15; lag ticks (vblanks > 1) after tick 0: 1 (2), 271 (2)
PC:  tick 0 at frame 1; lag ticks: none
frame15: GBA has 5 frames, PC has 0; not compared (no PC renderer yet)

== RESULT
first differing tick: 0; first differing field: gGameClock
fields differing at tick 0:
  gGameClock: GBA 0x00000001, PC 0x00000000
  gBmSt: +0x06 (prevVCount) GBA a8 PC 00
ticks with any difference: 300 of 300
first differing tick per field: gGameClock 0, gBmSt 0
fields equal on every tick: gKeyStatusIgnoredSt, gKeyComboResetEN,
  gSoftwareResetFlag, gMainCallback, gPlaySt, gLCDControlBuffer, gPaletteBuffer,
  gBG0TilemapBuffer, gBG1TilemapBuffer, gBG2TilemapBuffer, gBG3TilemapBuffer, gOam

== SUPPLEMENTARY (gGameClock and gBmSt.prevVCount left out)
match to tick 299 (300 ticks)
```

Reading it:
- `gGameClock` counts VBlanks. On the GBA, boot from reset to the end of the
  first `OnMain` spans 15 frames, and `OnVBlank` (installed at `main.c:69`) ran
  once before tick 0 ends; on PC it has not run yet. The GBA also lags at ticks
  1 and 271, so the offset grows from 1 to 3. The plan's checkpoint P1.5
  already scopes the clock to stretches without lag. (The clock rule above
  now takes this out exactly; the same run passes RULES and RESULT.)
- `gBmSt.prevVCount` is `REG_VCOUNT` at the end of `OnMain` (`bm.c:349`): the
  GBA scanline (0xA8 = 168 at tick 0), and on PC whatever line the frame step
  left (0 before the first VBlank, then 160).
- Everything else matches on all 300 ticks, including the five shadow video
  hashes. These values are not constant: the GBA trace has 61 distinct
  palette hashes, 20 LCD control hashes and 3 each for BG0 and BG1 over the
  300 ticks.
- Sensitivity check (not committed): a PC run with A pressed at tick 100
  instead of 200 differs from the GBA trace from tick 200 on
  (`gLCDControlBuffer` `dispcnt`), so the harness does see input-driven
  changes.

### Not covered yet

- Done since the first comparison: `OnMain` entries are counted on the GBA
  (debugger breakpoint), both sides sample at the same program point, the
  rule for `gGameClock` and `gBmSt.prevVCount` is written down (above), and
  the RNG and key buffer are traced.
- `frame15` was GBA-only in that first comparison. Since the renderer
  (2026-10-19, branch `port-render`), the PC side has it too: the boot300 run
  matches on all 5 frames, and a no-key run to tick 7000 matches on every
  compared frame except the title's rotating lights (BG2 affine, ticks
  6568-6655), where only the shadow `bg2affin` differs: mGBA's HLE
  `BgAffineSet` is not the GBA BIOS (`tools/frame15.py`, `inputs/nokeys.keys`).
  Since P2.2b such frames pass when the affine replay shows that `bg2affin`
  is the only cause (section "Oracle-limit frames"). A format-2 no-key run
  (`nokeys.keys`, 6,700 ticks, `GATE0_SHOT_TICKS=6560-6660`, 2026-10-19)
  gives `GATE 0: MATCH`: all 88 rotated frames (6,568-6,655) pass through the
  affine replay, and the other 13 frames of 6,560-6,660, the angle-0 frames
  6,566, 6,567 and 6,656 among them, are equal outright.
- The frame rule covers `bg2affin` only, as P2.2b names it; OL-2's tick mask
  still covers `bg3affin` too. No trace has a `bg3affin` difference yet.
- Clock-driven animation after GBA lag is labelled lag-only by the lag check
  (decided 2026-10-19), not excused by a per-site rule.
- `gRNSeeds` is traced; check in each long run whether the route draws from
  it (a random battle roll or enemy AI).
- OL-2's offset bound (201) rests on the rotation centres in this tree's
  `BgAffineSet` callers; a new caller with a larger centre needs the bound
  rechecked. OL-1 cannot be masked until a trace field is known to carry it.
- Tree-0 procs (`Proc_Run(gProcTreeRootArray[0])` in `OnVBlank`) run once per
  lag VBlank on the GBA. Since the lag replay (section "Lag replay") the lag
  check reproduces this at the GBA's position; what it cannot resolve is
  below one proc step (same section, "Not resolved below a proc step").
- The lag position counts events in HBlank, VCount, timer or serial IRQ
  handlers as main-loop events on both sides (kinds 1-5 and 7; kind 6 only
  outside them), but the PC runs those handlers in the frame step and the GBA
  while the main loop runs, so such an event could land in another tick on
  each side; in a lag frame the replay runs the line handlers without
  counting their events. None makes one on the routes run so far (the lag
  replay's event count equals the GBA's on every tick); a mismatch would show
  in the compare's `Lag replay:` line and stop the labelling from that tick on.
- The PC samples before the HBlank handlers of the remaining lines, the GBA
  after them.
- SRAM is not in the trace; `GATE0_SRAM` dumps it at the end on both sides
  (section "Trace", D-18 in `port-plan/DIVERGENCES.md`). `GATE0_SRAM_IN`
  boots both sides from a save (section "Chapter saves").
- The optional per-scanline register log is not attempted.

## Chapter saves (start a run near a spot)

A route replayed from power-on needs every tick before the spot under test:
20,271 ticks to the Castle Frelia change, 107,000 to Chapter 5. A chapter save
skips them. The GBA plays the route once and its SRAM is kept; then both the
GBA and the PC boot from power-on with that SRAM, and a short key file takes
the title menu to "Continue" or "Resume Chapter". Both sides still start at
power-on, so tick 0 and every rule above are unchanged.

A save is the game's own data (FE8 writes a game save at each chapter change
and a suspend save after each action and before each phase), and the game's
own code loads it on both sides. The other way to start near a spot is a
save state (next section): a photo of both machines at one tick of the route
itself, so the run goes on with the route's whole RAM history.

**Booting from a save: `GATE0_SRAM_IN=PATH`** (a 32 KiB `.sav`, or a 64 KiB
`GATE0_SRAM` dump; only the first 32 KiB are used, D-18).
- GBA: `oracle/mgba_lua_run.c` copies the bytes into a memory file and gives
  it to mGBA as the save before reset (`core->loadSave`), as a frontend loads
  a `.sav`. Nothing is written back to PATH. `gate0_capture.lua` checks that
  the SRAM it reads at the start is those bytes.
- PC: `pc/gate0_hook.c` writes the bytes to SRAM (0x0E000000 under D1-a) in
  its setup, at the boot-time `UpdateKeyStatus` (`main.c:53`), before
  `SramInit` (`main.c:55`). If `SramInit` has already run, the run stops.
  The upper 32 KiB stay 0xFF, as before.
- Every trace header names the file and the FNV-1a 32 of the 32 KiB
  (`"sram_in"`, `"sram_fnv"`). The compare stops with exit 2 when the GBA
  trace, the PC trace or a replay booted from other SRAM than the rest.
- `port/test/Makefile` exports the variable, so every target takes it
  (`gba-trace`, `pc-trace`, the lag, affine and clock replays, `route`, the
  `snd` steps). A value left in the shell environment is used too, and make
  prints a line saying so; `check-all` removes it, since its rows start from
  a blank save.

**Making a save: `make -f port/test/Makefile sram-capture SAVE_KEYS=FILE
SAVE_AT=WHEN SAVE_NAME=NAME`.** `oracle/sram_capture.lua` plays FILE on the
GBA (ticks and keys exactly as `gate0_capture.lua`) and writes
`port/test/saves/NAME.sav` (32 KiB) and `NAME.sav.json` (where it was taken).
WHEN is:
- `tick:T`: after T ticks. Checked: `tick:20343` on `route_ch1.keys` gives
  the same bytes as `gate0_capture.lua` with `GATE0_TICKS=20343` and
  `GATE0_SRAM` (its first 32 KiB; the upper half is mGBA's mirror).
- `save:C`: the end of the tick in which the game writes a game save (slot 0
  to 2) whose `chapterIndex` is C. Continue loads it.
- `suspend:C`: the SRAM right after the last suspend save written while
  `chapterIndex` was C, written once `chapterIndex` changes. Resume Chapter
  starts at that last action.

`make -f port/test/Makefile sram-info SAV=PATH` (`tools/sram_info.py`)
prints what a save holds, checked as the game checks it (header, block
magics and checksums): each game slot's and suspend slot's chapter, turn and
phase, the suspend point, and the entry the title menu opens on. Saves are
game data made by playing the ROM: `port/test/saves/` keeps them out of git
(its `.gitignore` ignores everything else), and they are never committed.

Saves made from the bot routes (2026-10-20; ticks are the routes'
milestones, `inputs/README-routes.md`; capture times with up to four running
at once):

| Save | SAVE_KEYS, SAVE_AT | Taken at route tick | Holds | Capture |
|---|---|---|---|---|
| `ch1_seize` | `route_ch1.keys`, `suspend:1` | 19,883 | suspend: Chapter 1, turn 6, during Eirika's seize | 35 s |
| `ch2_start` | `route_ch1.keys`, `save:2` | 20,342 | game save, Chapter 2 | 36 s |
| `ch3_start` | `route_ch2.keys`, `save:3` | 37,495 | game save, Chapter 3 | 46 s |
| `ch4_start` | `route_ch3.keys`, `save:4` | 66,705 | game save, Chapter 4 | 86 s |
| `ch5_start` | `route_ch4.keys`, `save:6` | 106,919 | game save, Chapter 5 (index 6) | 137 s |
| `ch5x_start` | `route_ch5.keys`, `save:5` | 134,765 | game save, Chapter 5x (index 5) | 174 s |

The routes go on from `ch5x_start` as a chain of chapter saves
(`inputs/README-routes.md`, "Routes from chapter saves"): `route_ch5x.keys`
from `ch5x_start` ends with `ch6_start` (`save:7`), `route_ch6.keys` with
`ch7_start` (`save:8`), `route_ch7.keys` with `ch8_start` (`save:9`),
`route_ch8.keys` with `e9_start` (`save:10`, Eirika's route after the split),
`route_e9.keys` with `e10_start` (`save:11`), `route_e10.keys` with
`e11_start` (`save:61`, Eirika's Chapter 11), `route_e11.keys` with
`e12_start` (`save:12`), `route_e12.keys` with `e13_start` (`save:13`) and
`route_e13.keys` with `e14_start` (`save:14`). `make -f port/test/Makefile
chain-save CHAIN_SAVE=e14_start` makes every missing save of the chain in
`port/test/saves/`, and `chain-route CHAIN=ch6` (also `ch5x`, `ch7`, `ch8`,
`e9` to `e13`) runs `route` from the route's own save. These saves load
the same on both sides (`resume.keys`, 2,600 ticks, release build):
`e9_start` and `e10_start` LAG-ONLY, `e11_start` MATCH; `e12_start`,
`e13_start` and `e14_start` LAG-ONLY.

**Loading it: `inputs/resume.keys`** (2,000 entries). A on the health and
safety screen (tick 250), Start in the opening (1,000), A on the title
(1,300), A on the title menu's first entry (1,500: Resume Chapter when the
save has a valid suspend, else Restart Chapter, which is Continue), A on the
slot list (1,700; it opens on the last saved slot). The game runs the save
from tick 1,735. Every press comes at least 150 ticks after the GBA began
waiting for it, so the lag-free PC takes the same path. Append keys from tick
2,000. With `resume.keys` alone, 2,600 ticks, every save above loads on both
sides with equal logic state: `ch1_seize`, `ch2_start`, `ch3_start` and
`ch4_start` give `LAG-ONLY`, `ch5_start` `MATCH` (round 5, `port-chapsave`).
`ch5x_start` gave `DIFFERENT` from tick 2,249 there, where its opening scene
runs and the lag replay left the GBA's sequence (19 main-loop events against
the GBA's 11: the host-ABI difference of `ChapterIntro_SetSkipTarget`, as in
`chapsave-ch2`'s first run at tick 3,244); with round 4's host-ABI fix (D-29)
applied the chapsave review saw `LAG-ONLY` (not rerun on `port-integrate4`).

**Gate 0 from a save: `make -f port/test/Makefile chapsave CHAPSAVE_SAV=...
CHAPSAVE_KEYS=... CHAPSAVE_TICKS=...`** runs it as `scripted` does (GBA
capture, PC trace, lag replay, affine replay, compare). Both sides also write
their SRAM at the end, and the first 32 KiB are compared and reported (not
part of the verdict). Two presets, each needing its save from the table:
- `chapsave-frelia`: `ch1_seize.sav` and `inputs/chapsave_frelia.keys`
  (7,600 ticks). Resume Chapter replays the seize, the end of Chapter 1, the
  world map scene and the Castle Frelia scenes (`chapterIndex` 56), each
  skipped with Start while its text waits, then the save menu, the world map
  and Chapter 2 to its first player phase (turn 1 at tick 6,971).
- `chapsave-ch2`: `ch2_start.sav` and `inputs/chapsave_ch2.keys` (7,000
  ticks). Continue, the world map (L to Ide, A), the opening skipped, the
  first player phase (turn 1 at tick 3,571), a unit selected and cancelled,
  End from the map menu, the enemy phase with a battle (tick 5,326), the
  other (green) phase with two, turn 2 at 6,561.

**Limit: a save start is not the route.** Both sides boot from the same
SRAM, but not with the route's RAM history: EWRAM left over from earlier
scenes, the order of the proc pool and the bytes on the stack are those of a
fresh boot plus the key file, not those of the playthrough. A site that reads
stale or uninitialised memory (group F's off-map tile reads, N-24's stack
words, the uninitialised-field sites) can read other values from a save than
on the route, on both sides alike. So a chapsave verdict does not replace a
route run at such a site; it shows that the PC follows the GBA from that
save. Both presets now run to their end (below); before the round 3 fixes
`chapsave-frelia` stopped on the same fog-pointer read as the route
(`0x0101010F`, group F) and `chapsave-ch2` on N-3, which was consistent with
the routes but no proof that the routes and the saves see the same values.

Result (2026-10-20, `port-integrate4`: the round 3 NULL fixes, round 4's
host-ABI fixes and the early flush capture; saves made with `sram-capture`
in this tree, the same bytes as round 5's):

```
chapsave-frelia (42 s in all, with other worktrees busy):
GBA: tick 0 at frame 7; 44 lag ticks, 149 lag VBlanks (0 early flush)
PC trace, lag replay and affine replay: 7,600 of 7,600 ticks, no fault
Lag replay: ... run at the GBA's position on every one of those ticks;
  main-loop events equal to the GBA's on every tick
mask OL-5: covered 6 ticks, 4309-4314
RESULT differs on 3022 ticks: 3022 lag-only, 0 not
GATE 0: LAG-ONLY on ticks 0-7599 (first at tick 1891)
SRAM at the end: DIFFERENT at 32 bytes

chapsave-ch2 (39 s in all):
GBA: tick 0 at frame 5; 70 lag ticks, 170 lag VBlanks (0 early flush)
PC trace, lag replay and affine replay: 7,000 of 7,000 ticks, no fault
Lag replay: ... run at the GBA's position on every one of those ticks;
  main-loop events equal to the GBA's on every tick
RESULT differs on 4022 ticks: 4020 lag-only, 2 not (2100, 5160)
GATE 0: DIFFERENT on ticks 0-6999: first difference at tick 1800, first that
  is not lag-only at tick 2100
SRAM at the end: DIFFERENT at 19 bytes
```

Reading it:
- In both runs the lag replay's traced RAM (`gMainCallback`, `gPlaySt`,
  `gRNSeeds`, `gLCGRNValue`, the key buffer, `gBmSt` without `prevVCount`,
  and the hashed buffers) equals the GBA's on every tick, except Frelia's
  six OL-5 cells at 4,309-4,314: the PC follows the key files from the saves
  through menus, the world map, scenes, Chapter 2's first player phase and,
  in `chapsave-ch2`, a phase change, the enemy phase's battle and the other
  phase's two. What differs is the plain lag-free run (lag-only, reproduced
  by the replay) and, in `chapsave-ch2`, two frames:
- `chapsave-ch2`, tick 2,100 (DIVERGENCES.md P-17): one frame, 370 pixels in
  the world map's name box (the PC already shows "Ide", the GBA still "Castle
  Frelia"; at 2,101 both show "Ide"). Every traced buffer is equal: the name
  is drawn into VRAM during the tick, on the GBA after the raster has passed
  those lines. A raster-timing effect that the PC, which draws after the
  tick, does not have; not traced further.
- `chapsave-ch2`, tick 5,160: one frame of the phase change's intro (the
  "ENEMY PHASE" banner and its diamond fade), lines 0-31 only, 2,310 pixels,
  each one fade step brighter on the PC (GBA `0x4F0C`, PC `0x532D`: red,
  green and blue each +1); the lag replay's palette, OAM and every other
  traced field equal the GBA's at that tick, and lines 32-159 are the GBA's.
  The routes show the same on 27 phase-intro frames (`route-ch5-lag`: the
  top 32 to 68 lines one fade step apart, from tick 3,780). A raster-timing
  effect of P-17's kind (on the GBA the step lands while the raster is
  partway down the frame); not traced further.
- The ticks 3,244-3,245 sequence break of the first run
  (`ChapterIntro_SetSkipTarget`, the void `PROC_CALL_ARG` return, route ch0's
  tick 1,591) is gone with round 4's host-ABI fix (D-29): the event counts are the
  GBA's on every tick.
- SRAM at the end: the PC side of this check is the plain lag-free run.
  The bytes are the play-time fields and the checksums over them, and
  DIVERGENCES.md P-12's uninitialised bytes that retail writes into saves
  (the last `supplyItems` byte at 0x1AC7/0x3A3F/0x480F, `GMapSaveInfo` and
  `SaveBlockInfo` padding); after a Continue also the suspend entries'
  `magic32`/`_pad_07`, which `InvalidateSuspendSave` (`bmsave.c`) writes from
  an uninitialised stack struct (the GBA writes 0x080A3121, a return address
  in `GetSaveReadAddr`). The game never reads any of them. (Round 5's review
  traced Frelia's with the lag replay's SRAM: 26 bytes, all P-12; not rerun
  here.)
- The same two spots from power-on: the `route-ch1-lag` row of
  `port/check-all.mk` runs route ch1 (21,273 ticks) through the lag replay,
  past both.

## Save checks (P5.3, P5.4)

PLAN.md P5.3 (all three save slots) and P5.4 (a suspend moved between the PC
and mGBA, both ways), run on the played builds. Tools, all read-only on the
saves they are given:

- `tools/savechk.sh pc full|sdl|win OUTDIR START.sav K1.keys K2.keys ...` plays
  a chain of sessions on a played build (`build/pc-full/fe8pc`,
  `build/pc-sdl/fe8pc-sdl` or `build/win32-sdl/fe8pc-sdl.exe`, the window
  builds under SDL's offscreen video and dummy audio, the Windows one through
  WSL's interop as check-all's `win-sdl-60000` row runs it): each session
  boots from the `.sav` the one before left, with `--save FE8.sav`, and
  `OUTDIR/NN_NAME.sav` keeps it. `savechk.sh gba OUTDIR START.sav K...` plays
  the same chain in mGBA (`sram-capture` with `SRAM_AT=tick:T` and
  `GATE0_SRAM_IN`). Each PC run has its own per-user folder (`FE8PC_USER_DIR`;
  the window builds honour it too, the Windows one through `WSLENV`'s path
  translation), so the player's own settings file is never read: every
  option is at its default. Before 2026-11-03 the Windows runs read
  `%APPDATA%\fe8pc`, the player's own options (a late-game death then
  retreated under casual mode on the Windows side only).
  `SAVECHK_RFO="unbreakable_weapons = 1"` gives the run its own Reforged
  Options settings file, and the sidecar `FE8.sav.reforged` goes along the
  chain. A session that fails stops the
  chain with exit 1 (a non-zero exit or timeout, or on pc a log without
  `frame limit T reached` and `exit 0: frames=T`): a run that died early
  leaves its input `.sav` as it was, which would look like a session that
  wrote nothing. Its file is kept as `NN_NAME.failed.sav`. A session given
  as `K.keys+VAR=VALUE[+VAR=VALUE...]` (full and sdl only) runs with those
  variables set for that session alone, e.g.
  `s1_frelia_slot2.keys+FE8PC_RFO_TEST_FALL=4` (the played builds' test fall,
  `port/reforged/reforged_casual.c`).
- `tools/keys_to_keyscript.py K.keys`: a played build has no key file, only
  `FE8PC_KEYSCRIPT` (`port/hw/frame.c`: at most 64 presses, each held 2
  frames). A key file whose presses are each 2 ticks long converts 1:1: on
  the lag-free PC frame N is harness tick N (one main-loop `VBlankIntrWait`
  per frame). `OnMain` calls can be fewer than frames, e.g. in battles
  (`frames=4000 OnMain calls=3202`), so they are not the measure.
- `FE8PC_KEYFILE=K.keys` (v1.1.5, `port/hw/frame.c` `KeyFileHeld`, every build):
  a played build plays a whole key file tick by tick, tick N on frame N, any
  press length and any number of presses, so a route's 1-tick presses replay
  exactly (`FE8PC_KEYSCRIPT` cannot). Past the file's last tick nothing more
  is pressed. Used for the randomizer's chapter replays (enemy class checks).
- `tools/sram_blockdiff.py A.sav B.sav` lists, region by region of
  `struct SaveBlocks` (header, the seven SaveBlockInfo entries, both suspend
  blocks, the three game blocks, arena, ranks, sound room, bonus claim,
  XMAP), what differs, by field. `--cross` (a PC save against an mGBA save
  made with the same keys) names and leaves out the bytes known to differ
  between the machines: the `PlaySt` clock fields (P-12 cause 3), the
  checksums over them, and the uninitialised stack bytes retail writes
  (P-18: SaveBlockInfo `_pad_07` and an invalidated entry's `magic32`, the
  last `supplyItems` byte, `GMapSaveInfo` +0x1C bits 4-7 and +0x21..+0x23,
  `gmUnit` bytes 6-7, the game block's padding); exit 1 when any other byte
  differs. `--block A:suspend.1 B:suspend.0` compares two blocks of one kind
  (a suspend that Resume Chapter loaded against the one the game wrote back
  from the resumed state); unit slots with no unit on either side are
  counted apart (the game packs every slot, so an empty slot keeps whatever
  its RAM slot held; mGBA writes the same bytes). `--summary` prints each
  block's chapter, turn, gold, slot and units. `--cross --line` gives the
  `--cross` verdict as one line (`SRAM CROSS: IDENTICAL`, `EQUAL apart from
  the known bytes (N bytes: clock, checksum, P-12, P-18 counts; the valid
  blocks)` or `DIFFERENT (N bytes not known, the first and the fields)`),
  the same exit status.
- `tools/sidecar_check.py FILE.sav`: each entry of `FILE.sav.reforged`, and
  whether its `<block>_check` is the `.sav`'s checksum32 for that block.
- `tools/savechk_all.py` (`make -f port/test/Makefile savechk`; check-all's
  opt-in row `saves`, `make -f port/check-all.mk CHECK_SAVES=1`): all of it
  as one standing check (the user's SAVE-3 B of 2026-10-30), from
  `$(SAVES)/ch1_seize.sav` (made with `sram-capture` when missing, and
  checked against its SHA-1), on the played builds `SAVECHK_BUILDS`
  (default `full,sdl,win`; win only where Windows programs run from here)
  and mGBA, parts `SAVECHK_PARTS` (default `p53,p53b,p54,rfo`): **p53** the
  nine P5.3 sessions; **p53b** the save-menu actions (SAVE-2: Copy Data slot
  1 to 3, slot 1 to 2 while slot 3 is full, Erase Data slot 2, a chapter end
  saved to slot 2 while slot 3 is full); **p54** both directions, each as a
  Gate 0 `chapsave` run (verdict MATCH or LAG-ONLY, the two end SRAMs
  `--cross`) and on every build; **rfo** Reforged Options on (SAVE-4, full
  and sdl: casual mode, unbreakable weapons for everyone, infinite gold; a
  test fall in Chapter 1 that must be back in Chapter 2, a fall mid-chapter
  on the suspend's fallen list and back after Resume, weapons at full uses
  after the enemy phase's battles, 999,999 gold saved, the mark copied by
  Copy Data and dropped by Erase Data, the side file's fingerprints after
  every session; a control chain with the options off; then the last `.sav`
  without its side file on mGBA and on FULL with the options off: mGBA
  plays on, and the two end saves are equal apart from the known bytes);
  **late** (opt-in, `--parts ...,late`; 2026-11-03) the late game from six
  chapter saves in `--late-saves` (default `~/fe8-chapsaves/`, each checked
  by SHA-1): Eirika's Chapter 20 and final chapter, Ephraim's final chapter
  part 1, his real clear save (the post-game world map), Tower of Valni
  floor 8 and Lagdou Ruins floor 10, each with the real army.
  `late_*_k1` saves into slot 2 from the preparations or the world map
  where the start offers that (e20, i21, iclear: a full late-game game
  block) and suspends; `late_*_k2` resumes, ends the turn and plays the
  enemy phase to the next turn's suspend saves (br10: two deaths, their
  records written into slot 1). Both on mGBA and on each build, then
  `late_*_k2` again from the FULL build's k1 save and from mGBA's, on every
  side (P5.4 in both directions with late-game saves; mGBA must play on).
  Each session is also checked for "other slots untouched": the game slots
  whose block or SaveBlockInfo entry changed must be exactly the ones it
  writes (a table in the script). Prints a PASS/FAIL line per check and
  `SAVE CHECKS: PASS (N checks ...)` or `SAVE CHECKS: FAIL (...)`; outputs
  in `build/savechk/` (game data: keep out of git). About 9 minutes with
  all three builds (2026-10-30; the Windows exe's runs are the slowest).

**End SRAM of a route** (the user's SAVE-1 B of 2026-10-30): `route` (so
`chain-route`, `path-route` and check-all's route, chain and path rows) has
both sides write the SRAM at the route's last tick (`GATE0_SRAM`:
`build/gate0/gba_end.sav` from the GBA capture, `pc_end.sav` from the PC lag
replay), and `make -f port/test/Makefile route-sram` compares them with
`sram_blockdiff.py --cross` (report and the `SRAM CROSS:` line in
`build/gate0/sram_cross.txt`); `route` exits 1 after its Gate 0 compare when
a byte other than the known ones differs. The lag replay runs the GBA's lag
VBlanks, so the play clock usually matches too; what is left is the P-12 and
P-18 bytes and their checksums. Every chapter-end and suspend save a route
writes is in that SRAM (the last one of each block). check-all's row note
ends with `end SRAM vs GBA ...`, and a byte not known, or no compare, fails
the row (`check_all.py --gate0-sram`); the two saves and the report are kept
in `build/check-all/<row>/`. `ROUTE_SRAM=0` leaves it out.

Session key files: `inputs/savechk/` (hand-made from frame dumps of the
played build). P5.3 starts from `ch1_seize.sav` (section "Chapter saves"):
`s1_frelia_slot2` (end of Chapter 1, the save menu's slot 2, Chapter 2's
first player phase), `r_resume` (Resume, which writes the resumed state
back), `s2_resume2_suspend`, `s3_newgame3_suspend` (New Game, Difficult,
slot 3), `s4_restart1_suspend`, `s5_restart2`, `s6_restart3` (Restart
Chapter on each slot). The `s*_suspend` files end with the map menu's
Suspend and Yes. P5.4: `p54_k1_end_turn` (Resume, End, an enemy phase with
two battles), `p54_k2_wait_end`. The late game: `late_<save>_k1` and `late_<save>_k2` for
e20, e22, i21, iclear, bt8 and br10 (each file's header says what it does).
The save-menu actions (port-savechk2):
`c1_copy13` (Copy Data, slot 1 to slot 3), `c2_copy` (Copy Data from the
slot the source list opens on, the last game saved, to the slot the
destination list opens on, the first empty one) and `c3_erase` (Erase Data:
one slot down from the one the list opens on). Results:
`port-plan/pending/savechk.md`, `savechk2.md`.

## Test cheats (cheat-assisted runs, test coverage only)

Test-only cheats let the bot get through stretches that fair play makes too
long to cover (Lagdou Ruins floor 10, long post-game and skirmish runs), with a
same-buttons compare that stays exact: both sides apply them the same way at
the same point of every tick. The user's decisions: an "unlimited durability"
cheat (2026-10-22), the idea taken from the FE8 Reforged romhack's options (only
the idea: nothing of the romhack is used, the port refuses modified ROMs and
the oracle needs the original); unlimited funds and a convoy with no limit
(2026-10-23; the convoy's 100 slots are the ROM's own code, which the GBA
oracle runs as it is, so the convoy cheat keeps the slots full instead, a
"rolling" convoy, below). A route made with any of them is labelled
**cheat-assisted** everywhere, as a boosted start is labelled boosted. They are
not for Eirika's Chapter 20 and final chapter: every tool stops there (below,
"Not in Eirika's Chapter 20 and final chapter"); the post-game of her clear
saves is allowed (the user's decision of 2026-10-23). Never in a played build.

```
make -f port/test/Makefile route ROUTE_KEYS=port/test/inputs/route_br9.keys ROUTE_TICKS=5000 \
    GATE0_SRAM_IN=port/test/saves/br9_start.sav GATE0_CHEAT=durability
make -f port/test/Makefile route ROUTE_KEYS=port/test/inputs/route_e15.keys ROUTE_TICKS=12000 \
    GATE0_SRAM_IN=~/fe8-chapsaves/e15_start.sav GATE0_CHEAT=durability+funds+convoy
```

- **The cheats:** `durability`, `funds` and `convoy`, one or several joined
  with `+` in any order (`GATE0_CHEAT=convoy+funds` is the same as
  `funds+convoy`); every tool writes the name in the order durability, funds,
  convoy (`oracle/test_cheat.lua` `C.check`, `pc/gate0_hook.c` `CheatSetup`),
  and that is what the trace headers, the key files and the saves say. At a
  tick boundary they run in this order: the refusal (below), convoy,
  durability, funds.
  - **`durability`:** every item in every unit's inventory and in the convoy
    gets its uses back to the item's maximum. The units: the five item slots
    (+0x1E) of each `struct Unit` whose `pCharacterData` (+0x00) is set, in
    `gUnitArrayBlue[62]`, `gUnitArrayRed[50]`, `gUnitArrayGreen[20]` and
    `gUnitArrayPurple[5]`; the convoy: `gConvoyItemArray[100]`. The maximum:
    `GetItemMaxUses` (`src/bmitem.c`: `maxUses` of the ROM's `gItemData`). An
    empty slot, an item with `IA_UNBREAKABLE` and one whose maximum is 0 stay
    as they are; the word becomes `ITEM_INDEX | max << 8`. Nothing else
    changes: not stats, not the RNG, not the battle units' copies
    (`gBattleActor`, `gBattleTarget`). A battle takes its uses from the battle
    unit while it is computed, so a weapon breaks only if one battle uses up
    all of them; the tick boundary after the game writes the battle unit back
    fills it again. A single-use item (a key, a stat booster, a promotion item)
    is still used up: its slot empties inside the tick, and an empty slot
    stays empty.
  - **`funds`:** the party's gold, `gPlaySt.partyGoldAmount` (+0x08), becomes
    999,999, the game's own maximum (`SetPartyGoldAmount` and
    `AddPartyGoldAmount` cap it there, `src/bmgold.c`), at every tick sample.
    So a shop sells anything (`src/bmshop.c` checks `price >
    GetPartyGoldAmount()`), a purchase shows for one tick (999,999 less the
    price) and is filled again, and a sale changes nothing (999,999 plus the
    price is capped by `SetPartyGoldAmount`).
  - **`convoy`, a rolling convoy:** every empty convoy slot (the word 0, as
    `AddItemToConvoy` sees it) gets the next item of a fixed list, made as the
    game makes a new item (`MakeNewItem`: its maximum uses, 0 for an
    unbreakable one). The list: the ids 0x01 to 0xCD of `gItemData` (0xCE
    entries, `ITEM_NONE` to `ITEM_UNK_CD`, `src/data_items.c`) whose name is
    not the placeholder text `MSG_403` ("Dummy", `nameTextId` +0x00): **177
    items**. The 28 left out are dummies: `ITEM_SWORD_MKATTI`,
    `ITEM_ANIMA_FORBLAZE`, `ITEM_LIGHT_LUCE`, `ITEM_MINE`, `ITEM_LIGHTRUNE`,
    the four FE7 blessings (0x7D to 0x80), `ITEM_HEAVENSEAL` and `ITEM_UNK_BC`
    to `ITEM_UNK_CD`. So an empty convoy gets the first 100 items (0x01 to
    0x67, `ITEM_SWORD_IRON` to `ITEM_ELYSIANWHIP`, without 0x0A, 0x3D and
    0x44), and whenever a slot empties afterwards (an item taken out:
    `RemoveItemFromConvoy` shrinks the list, so the empty slot is the last one)
    the next item goes in. The **cursor** counts the items placed so far; item
    number cursor mod 177 of the list is next, so after the 177th the list
    **goes round again** from `ITEM_SWORD_IRON` and the convoy never runs dry
    (decided so that a long cheat-assisted run keeps its supply; each item is
    placed once in the first 177 placements). The cursor starts at 0 at
    power-on (a resumed run takes its photo's, "Save states" below; a run from
    a save starts at 0 again). The cheat acts only while an army is loaded
    (some unit of `gUnitArrayBlue` has `pCharacterData` set): before a save
    loads, the convoy in RAM is not the game's yet, and the load
    (`ReadSupplyItems`) overwrites all 100 slots, so the first fill is at the
    first tick sample after the load (tick 708 from a chapter save). The
    game's own items stay where they are: only empty slots are filled (a save
    with 11 items in its convoy gets 89, the first 89 of the list). The slots
    stay 100, the ROM's limit, and the convoy is always full, so with this
    cheat an item sent to the convoy finds no empty slot (`AddItemToConvoy`,
    `src/bmcontainer.c`), as with a full convoy in fair play.
    **The supply screen loses items** (found by coverage 12's item gallery,
    port/test/inputs/gallery/README.md): a take on the supply screen
    (`PrepItemSupply_TakeItemFromSupply`, `src/prep_itemsupply.c`) rewrites the
    convoy from the screen's own list (`sub_80982B8`: `ClearSupplyItems`, then
    the list back) and rebuilds the list, so an item this cheat put in after the
    list was built is wiped by the next take of the same visit: k takes in one
    visit place k(k+1)/2 items and keep k (the cursor still moves, so those
    items come again only when the list goes round). The same on both sides
    (the compare stays exact); a test that needs every item takes one item a
    visit, as the gallery does.
- **The point:** the tick's sample point (section "Trace", "When a tick is
  sampled"). The GBA capture runs the cheats right after it has taken the
  tick's line at the main loop's `VBlankIntrWait` call (`oracle/test_cheat.lua`
  `refill()`, called from `gate0_capture.lua`'s `waitCall`); the PC hook right
  after `Sample()` in `__wrap_VBlankIntrWait`, before the frame step
  (`CheatRefill` in `pc/gate0_hook.c`, with the game's own
  `GetItemAttributes`, `GetItemMaxUses` and `MakeNewItem`). So tick T's traced
  fields are the game's own, and both sides run T's VBlank and `OnMain` T+1
  with the same items, gold and convoy. A sample the GBA capture takes back
  (an early flush VBlank at the call, section "Early flush VBlank") takes the
  cheats' changes back too (the item words, the gold and the convoy cursor),
  before that `OnVBlank` runs, and runs them again with the new sample: the
  PC's order, whose lag replay runs that VBlank before its sample. The GBA
  tools that never take a sample back (`sram_capture.lua`, `sram_boost.lua`,
  and the capture tick rule of `route_lib.lua` for `route_bot.lua` and
  `route_check.lua`) do the same through the runner's `gate0_wait_irq`,
  `gate0_onvblank` and `gate0_wait_resume` callbacks (`test_cheat.lua`,
  `C.auto()`), so every tool leaves the game as the capture does; the bot's
  search keeps the convoy cursor with its save states (`C.save`). Nothing that
  runs between a tick's sample and the next `OnMain` (the VBlank the tick
  waits for and its `OnVBlank`) reads a unit's items, the gold or the convoy,
  so where inside that window the cheats land cannot be seen: the compare
  cannot tell it, and checks their results (the fields below) on every tick,
  and the game's behaviour, rather than the exact instruction. (The review
  moved the PC's refill to after the frame step on purpose, and `route_bt8`
  0-16,999, all 14 of its refill ticks, still MATCHed:
  port-plan/pending/rv-cheat12.md, mutant A.) "The same point on both sides"
  holds by reading the code.
- **Trace and compare:** both headers say `"cheat":"<name>"`, and each tick
  line has `cheat_items` (the FNV-1a 32, after the cheats, of the item bytes of
  all 137 unit slots, 10 each whatever `pCharacterData` is, then the convoy's
  200), and each cheat its own field: `cheat_refills` (durability: the item
  words it changed), `cheat_funds` (funds: 1 when it changed the gold, 0 when
  it was 999,999 already) and `cheat_convoy` (convoy: the cursor after this
  tick's placements). The gold itself is in the traced `gPlaySt`. So every
  unit's inventory, the convoy and the cursor are traced fields of a
  cheat-assisted run. The compare (`tools/gate0_compare.py`) stops (exit 2)
  when the traces it compares (GBA, PC, lag replay, affine replay) do not all
  name the same cheats, when a header names something else, or when a
  cheat-assisted trace lacks one of its fields; it compares them on every tick
  like the others and prints a NOTE; `compare.json` gets `"cheat"`. For
  `durability` alone the fields, the NOTE and the totals are what they were
  before the other two existed. Without a cheat none of this is there, and the
  traces, the compare's output and `compare.json` are what they were. The GBA
  capture's end line and the PC log's `[gate0] test cheat` lines give the
  totals (words refilled, ticks the gold was set, items placed and the cursor).
- **Settings:** `GATE0_CHEAT=<name>` for `gate0_capture.lua`,
  `sram_capture.lua`, `sram_boost.lua` and the PC hook (port/test/Makefile
  exports it, like `GATE0_SRAM_IN`, and stops on a word that is not one of the
  three); `ROUTE_CHEAT=<name>` for `route_bot.lua` and `route_check.lua` (with
  `ROUTE_TICK=capture`; the bot stops at once with `ROUTE_E7=1`, which plays
  only the chapters of the next item). Another name, or one twice, stops the
  run; so does `GATE0_CHEAT` on a PC run without `GATE0_OUT`, and on a
  frame-mode capture (`GATE0_FRAME_KEYS`). Unset or empty: none of the
  cheats' code runs (`test_cheat.lua` is not even loaded).
- **Not in Eirika's Chapter 20 and final chapter** (the user's decision: they
  are played fair; the boosted starts `b20_start` to `b22_start` have no need of
  the cheats either). The refusal goes by chapter, not by a setting, and holds
  for every cheat: every tick boundary, on both sides, first reads
  `gPlaySt.chapterIndex`, and at 0x14 (Chapter 20, and the world map before
  it), 0x15 or 0x16 (the final chapter's two parts), the indices
  `route_bot.lua`'s `L.E7.chapter` plays, the run stops before anything is
  changed, with or without `ROUTE_E7`, the real army or a boosted start. The
  GBA tools (`test_cheat.lua`, so `gate0_capture.lua`, `sram_capture.lua`,
  `sram_boost.lua`, `route_bot.lua` and `route_check.lua`) print `test cheat:
  GATE0_CHEAT=<name> refused at tick T: chapterIndex 0x14 is Eirika's Chapter
  20 or final chapter ...` (`ROUTE_CHEAT=` for the bot and `route_check.lua`)
  and exit 5; the PC hook prints `gate0_hook: GATE0_CHEAT=<name> refused at
  tick T ...` and exits 4, at the same tick sample. The first tick sample in
  those chapters is before the bot's first decision there, so the run stops:
  - at a save that loads into them: `e20_start`, `e21_start`, `e22_start` and
    `b20_start` to `b22_start` at tick 708, where Continue loads the save;
  - at a chapter change into them: the end of Eirika's Chapter 19, where the
    index moves to 0x14 before the end save (`route_e19.keys`, tick 154,137,
    the save at 154,201), so a cheat-assisted Chapter 19 cannot end with
    `saveto:20` and no cheat-made army reaches Chapter 20.
  With `ROUTE_E7=1` (which plays only those chapters) the bot stops at once.
  Two exceptions, where these indices are not Eirika's chapters:
  - On Ephraim's route (`gPlaySt.chapterModeIndex` 3, `CHAPTER_MODE_EPHRAIM`)
    they are only a scene's map: his Chapter 20 ends with a scene on map 0x15
    (`route_i20.keys`, ticks 163,673 to 163,689, before its end save).
  - **The post-game of a clear save** (the user's decision of 2026-10-23):
    once the game is complete, `gPlaySt.chapterStateBits` (+0x14) has
    `PLAY_FLAG_COMPLETE` (0x20), and the index is the post-game's. A clear save
    (`e_clear`, `b_clear`) loads with 0x16 and that flag, for the ending it
    replays (Continue, tick 708) or the post-game world map it opens on (Extras,
    Map, tick 736), until a floor or a skirmish gives the index another value;
    so a cheat-assisted post-game route can start from Eirika's clear saves as
    from Ephraim's (index 0x23). The flag is set by
    `GameCtrl_DeclareCompletedChapter` (`src/bmio.c`) when the final chapter is
    won, after its boss falls: every tick sample of the final chapter before
    that refuses, so no run reaches those last ticks with a cheat (and a
    resumed photo must have had the run's own cheats, "Save states" below).
  Of the 69 route files, only Eirika's `route_e19` to `route_e22`,
  `route_b20` to `route_b22`, the four from the clear saves (`route_eend`,
  `route_bend`, `route_bpost`, `route_bt1`, which the flag lets through) and
  that scene of `route_i20` ever have these indices. The harness has no other
  way in: `route`, `chain-route`, `path-route`, `snd-route`, `snapshot` and
  `chain-save` turn the cheats on only through these tools, so they stop the
  same way, and the menu monkey and the movie scripts cannot run with a cheat
  at all (next item).
- **Routes and saves:** a key file the bot made with `ROUTE_CHEAT` names it in
  its `# Generated` line and has a `# Cheat-assisted` line saying what the
  cheats do (inputs/README-routes.md, "Cheat-assisted routes"). `route` (so
  `chain-route`, `path-route` and check-all's route rows), `snd-route` and
  `snapshot` run such a file with its cheats when `GATE0_CHEAT` is not set
  (port/test/Makefile `KEYS_CHEAT`); check-all titles its row "cheat-assisted
  (GATE0_CHEAT=..., test coverage only)" and passes `GATE0_CHEAT` on the row's
  make line. `chain-save` makes each save with the cheats of the key file it
  plays, so the save after a cheat-assisted route is that route's end and
  every other save is played without one, whatever `GATE0_CHEAT` is. **The
  mark goes on:** a save made with a cheat has `"cheat"` in its `.sav.json`,
  and `sram_capture.lua` and `sram_boost.lua` copy the start save's
  (`GATE0_SRAM_IN.json`) `"cheat"` into the save they make, joined with their
  own run's cheats (`durability` then `funds` makes `"durability+funds"`), so
  every later save of a chain says so, however many hops after the cheat, and
  so does the bot's key file from any of them (`# Start save made with the
  test cheat ...`, which does not make the route itself cheat-assisted).
  check-all refuses a `GATE0_CHEAT` or `ROUTE_CHEAT` in its own environment
  (and `port/tools/check_all.py` drops both from every row's). The targets
  whose GBA side has no cheat refuse `GATE0_CHEAT` and `ROUTE_CHEAT` (the
  setting `route_lib.lua` would act on): `snd` (`snd-gba`, `snd-pc`),
  `monkey`, `monkey-check`, `movie-check`, `frame-trace`; and
  `menu_monkey.lua`, `movie_check.lua` and `movie_presses.lua` stop at once
  when `ROUTE_CHEAT` is set, run from make or not.
- **Save states:** the GBA photo's `meta.json` names the cheats (when on) and
  its `capture.lua` keeps the convoy cursor (`cheatConvoy`); a resumed GBA
  capture stops when its `GATE0_CHEAT` is not the photo's. The PC photo's
  prologue (its tick T-1 line) has the cheats' fields, the cursor among them:
  a resumed PC run stops (exit 4, `the save state's run had other test cheats
  ...`) when they are not its own cheats', and takes the cursor from it
  (`CheatResume`). The PC hook's cheat variables are session state
  (`tools/savestate.py`), so photos made before the cheats existed still
  resume.
- **Never in a played build:** the PC side is `pc/gate0_hook.c`, linked into
  the Gate 0 test binary only (`build/pc-gate0`, and the sweep builds made
  through it, port/test/sweep.mk); `port/Makefile`, `port/Makefile.sdl` and
  `port/Makefile.mingw` never compile it. The results below check the played
  builds for any symbol or string of it.

### Result (2026-10-23, branch `port-cheat12`: integration 11 `9ef060ea` plus `port-cover11`)

- **Exact with the cheat on** (`route`, lag replay vs GBA, every traced field
  and `cheat_items`/`cheat_refills` on every tick): `route_br9` from
  `br9_start` 5,000 ticks MATCH (32 item words refilled when the save's army
  loads at tick 708, then 1 a battle: 36 words on 5 ticks); `route_ch6` (a
  story chapter) from `ch6_start` 10,000 ticks MATCH (25 words on 10 ticks);
  and `route_br9` whole, 97,417 ticks, MATCH (58 words on 24 ticks; two
  frame15 differences, both labelled raster-timing frames, decision 9).
- **The game leaves the plain route:** the cheat's GBA trace parts from the
  plain one at tick 1,723 of `route_br9` and 7,411 of `route_ch6`, in the
  weapon menu's uses (`gBG0TilemapBuffer` cells (12,2)-(12,3), and
  `gBmSt.prevVCount`): at `route_br9`'s tick 1,724 the plain game's menu shows
  "Spear 6", the cheat game's "Spear 15" (GBA screenshots, kept out of git).
  Weapons stop breaking: on the whole of `route_br9` a GBA probe of every blue
  unit's items at each tick (scratch, not committed) sees two weapons break in
  the plain game (a Spear at tick 27,108 and a Silver Lance at 40,756, each with
  2 uses left before its battle) and none with the cheat; the 24 items that left
  an inventory there were 15 single-use items (Chest and Door Keys, a Guiding
  Ring, two gems) and 9 at their full 3 uses (an Elixir, an Antitoxin, the
  Latona staff), which the off-script menu presses used up or moved. (A
  single-use item is still used up: its slot empties inside the tick, and the
  cheat leaves an empty slot alone.) The bot's buttons were made for the plain game, so on the whole of
  `route_br9` the cheat game ends on floor 9's turn 6 (99,219 GBA frames), where
  the plain game won the floor on turn 35 (103,248 frames); the GBA and the PC
  still did the same on every tick.
- **The bot with it:** `ROUTE_CHEAT=durability` from `br9_start`, 4,000 ticks
  with the search: the file's `# Generated` line names it and the
  `# Cheat-assisted` line follows; `route` on that file, with no `GATE0_CHEAT`
  given, turns the cheat on for both sides from the header and MATCHes (35
  words on 4 ticks); `route_check.lua` with `ROUTE_CHEAT` finds the header's
  milestones line for line.
- **Off, nothing changed:** the scripted 60,000 ticks (`make scripted`, as
  check-all's `gate0-60000` row) give a GBA trace byte for byte the one of the
  tree before the cheat (`21189271`, harness unchanged, in its own worktree),
  the same raw dumps (`gba.raw`, `pc.raw` SHA-1s equal) and the same 60,000 PC
  tick lines (only the header's `exe_crc32` differs: another binary); its
  compare: LAG-ONLY, first lag-only tick 8,630 (the row's pass; the baseline's
  own compare was not run). The changed bot makes `route_ch0_prologue.keys` (frame
  rule) and `route_f_suspend_ch6.keys` (capture rule) byte for byte. Default
  check-all: PASS, 23 of 23 (GBA OK, SHA-1 c25b145e...).
- **Not in a played build:** after that check-all, `build/pc/fe8pc`,
  `build/pc-full/fe8pc`, `build/pc-sdl/fe8pc-sdl`, `build/win32/fe8pc.exe`,
  `build/win32-sdl/fe8pc-sdl.exe` and its `.debug` hold no cheat symbol
  (`Cheat*`, `sCheat*`, `__wrap_VBlankIntrWait`) and no cheat string
  (`GATE0_CHEAT`, `cheat_items`, `cheat_refills`, `durability`), and none of
  those build directories has a `gate0_hook.o`; `build/pc-gate0/fe8pc` has both.
- **Refused in Eirika's Chapter 20 and final chapter** (the review's blocking
  item, port-plan/pending/rv-cheat12.md; rework of 2026-10-23,
  port-plan/pending/cheat12-rework.md). The bot with `ROUTE_CHEAT=durability`
  from `e20_start`, `e21_start` and `e22_start`: with `ROUTE_E7=1` it stops at
  once; without it, it stops at tick 708 ("refused at tick 708: chapterIndex
  0x14", 0x15, 0x16) and writes no key file. The same at 708 from `b20_start`
  to `b22_start`, `e_clear` and `b_clear`, with the search from `e20_start`,
  and for `route_check.lua` on `route_e20.keys`. With `GATE0_CHEAT=durability`
  on `route_e20.keys` from `e20_start`, `make route` stops in the GBA capture
  at tick 708 (ticks 0-707 written), the PC hook alone (`pc-trace`) at tick
  708 (exit 4), and `sram_capture.lua` at 708 (no save written). At a chapter
  change, the plain `route_e19.keys` replayed with the refill on from tick
  154,000 stops at 154,137, the first sample with 0x14 (`chapterModeIndex`
  2), before the end save at 154,201; `route_bpost.keys` from `b_clear` with
  it on from tick 0 stops at 736 (0x16, mode 2: the post-game world map); and
  `route_i20.keys` with it on from 163,600 passes Ephraim's scene on map 0x15
  (163,673 to 163,689, mode 3) to his end save at 163,760 with no refusal (43
  words refilled). Elsewhere it is as before: `route_br9` 5,000 ticks MATCH
  (36 words on 5 ticks). A save made with the cheat (`sram_capture.lua` on a
  cheat-assisted floor-10 file) gives a `.sav.json` with `"cheat"`, and the
  bot's file from it has the `# Start save made with the test cheat` line,
  with and without `ROUTE_CHEAT`. Off: `route_ch0_prologue.keys` and
  `route_f_suspend_ch6.keys` made byte for byte; default check-all PASS, 23 of
  23 (GBA OK, SHA-1 c25b145e...); the played builds still hold no cheat symbol
  or string.

### Result: `funds`, `convoy` and the clear saves' post-game (2026-10-23, coverage 12's cheat13, `port-cheat12`)

(port-plan/pending/cheat13.md has the details. Since this change the clear saves `e_clear` and
`b_clear` are no longer refused: the results above that say so are from before it.)

- **Exact with each cheat on** (`route`, lag replay vs GBA, every traced field and the
  cheats' fields on every tick):
  - `funds`, `route_e17` from `e17_start`, 3,500 ticks: **MATCH** 0-3,499. The save loads
    with 12,605 gold (tick 708), set to 999,999 at that sample; at node 5's armory Gerik sells
    a Divine (999,999 plus its price is capped by `SetPartyGoldAmount`: no change) and buys a
    Steel Axe (999,639 at tick 2,589's sample, set back to 999,999 there): `cheat_funds` is 1
    on ticks 0, 708 and 2,589 only.
  - `convoy`, `route_e15` from `e15_start`, 12,000 ticks: **MATCH** 0-11,999. The save's
    convoy has 11 items; at 708 the cheat fills the other 89 slots with the list's first 89
    (0x01 to 0x5C without 0x0A, 0x3D and 0x44); on turn 2 the route's supply presses take out
    an Angelic Robe (0x5B) at 6,053, an Iron Lance at 6,092 and an Iron Bow at 7,001, and each
    of those ticks' samples puts in the next item (0x5D, 0x5E, 0x5F, into slot 99):
    `cheat_convoy` 89, 90, 91, 92.
  - All three, given as `convoy+funds+durability`, the same 12,000 ticks: **MATCH**; every
    header and the NOTE say `durability+funds+convoy`; 34 item words refilled on 8 ticks, the
    gold set on 2 ticks, 92 items placed.
- **A save state keeps the cursor:** `snapshot` of that convoy run at tick 6,000 (parts `gba`
  and `pclag`; `capture.lua` has `cheatConvoy = 89`, the PC prologue `"cheat_convoy":89`),
  resumed to 8,000: **MATCH** 5,999-7,999, and `cheat_items`/`cheat_convoy` equal the whole
  run's on all 2,001 ticks, on both sides (the three take-outs refilled with the same items).
  Resumed with another cheat or none, the GBA capture stops (`the snapshot's run had
  GATE0_CHEAT=convoy, this one has ...`) and so does the PC hook (`the save state's run had
  other test cheats ...`), which before did not check a photo's cheat
  (port-plan/pending/rv2-cheat12.md, non-blocking 3).
- **The post-game of Eirika's clear saves is allowed:** `route_bpost` from `b_clear` with
  `durability`, 5,000 ticks: **MATCH** (the post-game world map at 736 with 0x16 and
  stateBits 0x20, Tower floor 1 at 1,601; 30 words on 3 ticks). The bot from `e_clear` with
  `ROUTE_CHEAT=funds+durability+convoy`, `ROUTE_POSTGAME=1 ROUTE_WM_NODE=26`, 8,000 ticks
  (Extras, Map, the Tower, floor 1's first turns): its `# Generated` line names
  `ROUTE_CHEAT=durability+funds+convoy` and its `# Cheat-assisted` line says what the three
  do; `route` on that file, the cheats taken from its header: **MATCH** 0-7,999 (22 items in
  the save's convoy, 78 placed at 736; 35 words on 6 ticks; the gold set at 0 and 736). The
  bot with `funds` from each clear save, 1,500 ticks: no refusal, a key file.
- **Chapter 20 and the final chapter still refused:** the bot from `e20_start` (funds),
  `e21_start` (convoy), `e22_start` (all three), `b20_start` (funds), `b21_start` (convoy)
  and `b22_start` (durability): "refused at tick 708: chapterIndex 0x14" (0x15, 0x16, 0x14,
  0x15, 0x16), exit 5, no key file. `gba-trace` on `route_e21` with `funds`: refused at 708
  (708 tick lines). `pc-trace` on `route_e22` with `convoy`: the hook refused at 708. The plain
  `route_e19.keys` replayed with `funds+convoy` on from tick 154,000: refused at 154,137 (0x14,
  stateBits 0), before the end save. In the final chapter (`route_e22.keys` replayed plain)
  the index is 0x16 with stateBits 0 from 708 to 31,760; `PLAY_FLAG_COMPLETE` is set at 31,761,
  after the Demon King falls, and the index moves to 0x13 at 31,887 (the ending). Those 126
  ticks (the clear save at 31,826 among them) are the only ones of the final chapter the flag
  lets through, and no cheat-assisted run gets there: every sample before them refuses, and a
  resumed photo must have had the run's own cheats.
- **No cheat in the monkey and the movie scripts** (rv2-cheat12, non-blocking 1): `make
  monkey`, `monkey-check`, `movie-check` and `snd-gba` with `ROUTE_CHEAT` stop at once (exit
  2, "GATE0_CHEAT and ROUTE_CHEAT are not supported here"; no key file); check-all with it
  stops; `menu_monkey.lua`, `movie_check.lua` and `movie_presses.lua` run straight from the
  runner with it stop at their first line.
- **The mark through five saves** (rv2-cheat12, non-blocking 2), from `b_clear`:
  `sram_capture.lua` with `funds` gives `"cheat":"funds"`; then without a cheat, `"funds"`;
  then with `durability`, `"durability+funds"`; then `sram_boost.lua` without a cheat,
  `"durability+funds"`; then `sram_capture.lua` without, `"durability+funds"`. The bot
  (plain) from the fifth save writes `# Start save made with the test cheat (its .sav.json:
  "cheat":"durability+funds") ...`, and that file's `KEYS_CHEAT` is empty.
- **`durability` unchanged:** `route_br9` 5,000 ticks with it from `br9_start` gives a GBA
  trace byte for byte the one of `c4fd4111` (the first cheat commit; 36 words on 5 ticks),
  and a PC trace whose tick lines differ from that run's only in `gMainCallback` (a host code
  address of another binary, which the compare maps); MATCH. The bot makes
  `route_br10.keys` (`ROUTE_CHEAT=durability`, with the search) again byte for byte.
- **Off, nothing changed:** `route_br9` 5,000 ticks without a cheat: GBA trace byte for
  byte cheat12's (`c4fd4111`), PC tick lines equal but `gMainCallback`; MATCH.
  `route_ch0_prologue.keys` (frame rule) and `route_f_suspend_ch6.keys` (capture rule) made
  again byte for byte. Default check-all (`CHECK_JOBS=4`, 18:38-18:43): 22 of 23 PASS (GBA
  OK, SHA-1 c25b145e...; 0 compiler warnings; gate0-300 MATCH; attract-60000 VRAM
  0xE8A769C8; snd-title and snd-map PASS). `win-sdl-60000` failed in it and again run alone:
  WSL's Windows interop did not start the program (`UtilAcceptVsock: accept4 failed 110`; the
  program never ran, and the same WSL service time-outs hit other commands that hour). The
  same binary (not rebuilt: 0 files compiled; this change has no file in its build), run
  straight from Windows with the row's settings, printed every line the row requires
  (`[port] exit 0: frames=60000 OnMain calls=60000`, `[audio] 60000 VBlanks, 13440000 frames
  made`, `gGameClock=60000 DISPCNT=0x3F00`, VRAM 0xE8A769C8 with 264 palette entries, SDL
  3.4.2 offscreen, SRAM reserved, no gamepad).
- **Not in a played build:** after that check-all, `build/pc/fe8pc`,
  `build/pc-full/fe8pc`, `build/pc-sdl/fe8pc-sdl`, `build/win32/fe8pc.exe`,
  `build/win32-sdl/fe8pc-sdl.exe` and its `.debug` hold no cheat symbol (`Cheat*`, `sCheat*`,
  `__wrap_VBlankIntrWait`) and no cheat string (`GATE0_CHEAT`, `cheat_items`,
  `cheat_refills`, `cheat_funds`, `cheat_convoy`, `durability`, `refused at tick`, `played
  fair`, `rolling convoy`, `other test cheats`), and none of those build directories has a
  `gate0_hook.o` or the test `savestate.o`; `build/pc-gate0/fe8pc` has 14 such symbols and
  25 such strings.

## Save states (start a run mid-route)

A test of a spot late in a route used to replay every tick before it on both
sides. A save state is a photo of both machines at one tick T, taken once;
later runs start from it. Test harness only: the PC side is
`pc/savestate.c`, linked into the Gate 0 test binary next to the hook, and
`tools/savestate.py`; nothing of it is in a shipped build, and it does
nothing unless a `GATE0_STATE_*` variable is set. Design and the proof:
`port-plan/pending/savestate.md`.

```
make -f port/test/Makefile snapshot KEYS=inputs/route_ch5.keys AT=100000 [SNAP=DIR] [SNAP_PARTS="gba pc pclag pcaff"]
make -f port/test/Makefile route GATE0_STATE_IN=DIR/tick100000 ROUTE_KEYS=port/test/inputs/route_ch5.keys ROUTE_TICKS=106000
```

`snapshot` (`ROUTE_KEYS=` works as `KEYS=`) plays the key file from power-on
to tick T: the GBA capture (saved as tick T-1's line is written, and run on
through tick T: the lag replay runs tick T's lag VBlanks inside the main loop
before T's sample point, where its photo is taken, so it needs the GBA's tick
T), then the PC once per PC part (`pc` the plain run, `pclag` the lag replay,
`pcaff` the affine replay), each saved and stopped at T. A lag-replay
snapshot whose GBA trace has no tick T is refused (the hook checks
`GATE0_LAG_FROM` against `GATE0_STATE_AT`). Output: `DIR/tick<T>/<part>/`, and the runs' own traces in
`DIR/tick<T>/run/`. DIR defaults to `build/gate0-snap/<key file name>`
(under `build/`: game data, never committed). `GATE0_STATE_IN=DIR/tick<T>`
then resumes `gba-trace`, `pc-trace`, `pc-lag-replay`, `pc-affine-replay`,
`route`, `compare`, `scripted` and `chapsave` from there, with the same key
file and `TICKS` (or `ROUTE_TICKS`) still the end tick. It is exported like
`GATE0_SRAM_IN`; check-all drops it.

**Where the photo is taken.**
- PC: at the Gate 0 sample point of tick T (`__wrap_VBlankIntrWait` with
  `main_loop_ended` set), before the hook does anything for tick T (its
  end-of-tick lag VBlanks and early flush, the sample, the frame step). The
  host call stack there is `main`, `AgbMain`, `ExecMainUpdate`, the main
  callback, `__wrap_VBlankIntrWait`, and each of the three main callbacks
  (`OnMain`, `MainUpdate_8055C68`, `InBattleMainRoutine`) returns right after
  its `VBlankIntrWait()`. A resumed PC run boots as usual to its first sample
  point (tick 0, the same program point), replaces the whole state there and
  goes on as tick T.
- GBA: at the `"frame"` callback in which the capture writes tick T-1's line
  (the CPU halted in `VBlankIntrWait`, before the VBlank IRQ): mGBA's own state
  (`mCoreSaveStateNamed`, SRAM included) and every counter of
  `gate0_capture.lua`. The runner refuses the save while it has an
  IRQ-at-breakpoint report pending or a `:ret` call open. A resumed capture
  loads the state before the first instruction (`MGBA_RUN_STATE_IN`) and sets
  the keys of tick T.

**What the PC photo holds** (`DIR/tick<T>/<part>/`): the GBA memory the PC
keeps (`regions.bin`: EWRAM, IWRAM with the GBA stack area, the 4 KiB I/O page
with every shadow and latched register the port keeps there, palette, VRAM,
OAM, SRAM); all of the binary's host `.data` and `.bss` (`hostdata.bin`); a
copy of the binary and its link map; tick T-1's trace line and raw record
(`prologue.*`); `meta.json`. The ROM image is not saved: its CRC-32 is taken at
boot and checked at the save. `savestate.py relink --part DIR/tick<T>/<part>
--exe BINARY` (the PC targets run it) makes `relink-<crc>/patch.bin` for that
binary, with `relink.log` (every host address rewritten or left alone) and
`summary.json`:
- what is restored: all of `regions.bin`; host globals by name, classed by
  the object that defines them (the link map): every `.data`/`.bss` symbol of
  a port object is listed in `PORT_TABLE` (`state`, restored; `raw`, restored
  without the pointer scan; `session`, the new process keeps its own:
  settings read from the environment, FILE handles, heap pointers, the
  soft-reset `jmp_buf`, the harness's inputs), and an unlisted one stops the
  tool; a game object's `.bss` outside GBA RAM is restored, its `.data` only if
  the run changed it, except a game object's own `PC_PORT` log flag (a "logged
  once" beside a guard) listed in `GAME_SESSION`, which the new process keeps
  (`session`: `fontgrp.c`'s `sLogged`, round 10); crt/libc symbols are left to
  the new process. A
  generated stub's call counter (`autostubs.o`, "was its first call logged")
  is restored; one the new binary does not have, because that function is
  built there now (round 10: Crimson Eye, Shine, the event earthquake), is
  dropped and listed (`DROPPED` in `relink.log`). Bytes of
  `.data`/`.bss` that no symbol covers must be unchanged;
- host addresses: C function pointers (`gMainCallback`, proc callbacks, IRQ
  handlers, the sound driver's jump table, font and menu callbacks) and
  pointers to host-built data (proc scripts, proc names, CONST_DATA tables such
  as `gCharacterData` and `gClassData` that units point into) are host
  addresses, which move with every rebuild. Each word of EWRAM and IWRAM that
  holds an address inside the saved binary's image is decided by provenance
  first: the known fields (`gMainCallback`, `gIRQHandlers`,
  `gMPlayJumpTable`, the script, current command, end and idle callbacks and
  name of each of the 64 procs) and the words their variable's declared type
  (the saved binary's DWARF) makes a pointer are rewritten and must resolve,
  except a declared pointer that an overlaid variable (`EWRAM_OVERLAY`)
  declares as no pointer, where the GBA holds the same 32-bit value: that is
  the other variable's data, left alone and listed (`[overlay]`; 2026-10-21,
  cov9 H-col-1: `gStatusScreenSt.font.vramDest`, +0x8, shares its bytes with
  `gBanimOaml`'s OAM words 0x10004000/0x10008000, and `gPrepUnitList.units[33]`
  and `[61]`, +0x84 and +0xF4, with `gBanimScrRight`'s bytes 0x1000F222 and
  0x10000F88; on the GBA, too, the same words, which are no address there. A
  binary's image starts at 0x10000254, so those values looked like addresses
  with no symbol, and five of the r9 photos refused every binary but their
  own);
  words declared as integers narrower than 32 bits (tilemaps, palettes, OAM,
  coordinates, bitfields) are never pointers; every other word (4-byte
  integers, byte buffers, untyped areas, the user area of each proc) is
  rewritten only under a strict check: the first byte of a function whose
  address the binary takes somewhere (a word of its data or 4 bytes of its
  code hold it), or a place in a game data object that its declared type
  allows (its start, an element's or a member's; from a byte buffer only a
  start or an element of a table of 4-byte or larger elements). Everything
  else is left alone and listed. A name (function, object + offset, or, for an
  anonymous literal such as a proc name, the named object that holds it) that
  the new binary does not have stops the relink with that name. The prologue
  line's `gMainCallback` is relinked too;
- the GBA check of the strict scan (`--gba-elf`, which the Makefile passes):
  the GBA cannot hold a host address, so the gba part's own memory (mGBA's
  state, one main-loop step older than the PC's photo) and the GBA ELF's
  symbols say what such a word is. Where the GBA holds the same 32-bit value,
  it is data (tilemap words in `gGenericBuffer` at route ch5 tick 550 looked
  like an address inside a compressed-text table) and is left alone; where
  the GBA holds the address of the place the word resolves to (or one inside
  the same object), the rewrite is confirmed. Where the GBA is on the same
  path at that place (the lag replay while it is on the GBA's sequence, and
  any proc slot running the same proc script on both sides), a word whose low
  16 bits are the GBA's (a 16-bit field beside the high half of an old
  address) and a word the GBA holds as something that is no address are left
  alone. Elsewhere (the plain and affine runs, whose game leaves the GBA's
  path, and a slot whose proc changed) the strict rule alone decides; the
  summary counts each case. Without a gba part, or without the ELF, the check
  is skipped and the summary says so;
- the new binary is checked as the saved one is: each of its port globals
  must be in `PORT_TABLE` (an unlisted one stops the relink, in either
  binary), and each global it would restore must be in the photo: a state
  global added after the snapshot stops the relink (make the snapshot again
  with that binary). New game `.data` stops it too: the photo cannot say
  whether the new binary's own run would have written it before tick T (a
  counter `OnMain` counts up resumed with its initial value). The port builds
  `CONST_DATA` tables as plain `.data`, so a new decomp or `PC_PORT` table
  counts too; when nothing writes it, `SAVESTATE_NEW_GAME_DATA=initial`
  resumes with the new binary's initial values (a loud warning, and its own
  patch). A photo taken before round 10 (`~/fe8-snapshots/ready10/`, made by
  `port-integrate10`, and every older one) needs it on a round-10 binary: the
  Crimson Eye, Shine and event earthquake files that round 10 builds (C10-1)
  add 23 such tables (11, 11 and 1; 26 from some older binaries), all
  read-only. Those photos also need `port/hw/memory.o`'s `sMapped` and `buf`
  classified (`session`, H-11, was working ID H-r10-1) and `fontgrp.c`'s `sLogged` in
  `GAME_SESSION`, both there since integration 11; with the three, all 15
  `ready10/stops/` photos resume on `port-integrate11`;
- each restored host global must have the same size and, for another binary,
  the same declared layout (DWARF: member names, offsets and bit fields,
  element counts, base types; a pointer counts as a pointer), or the relink
  stops with the first place they differ. A same-size change (two fields
  swapped, an `int` made a `float`) would otherwise put the photo's bytes in
  the wrong places. A function-static (GCC's `name.N`) is matched by its
  file, name and the function it is declared in (DWARF), never by GCC's
  number, which follows the order of the source: moving `ClearOam` below
  `DrawGlyph` swaps their two `sLogged.N`. A function-static whose function
  the new binary does not have stops the relink;
- a patch is made once per snapshot content, binary and tool: `relink-<crc>/`
  is reused only while the part's files are the ones it was made from and
  `savestate.py` (its `PORT_TABLE`, `REFUSE_IF_SET`, `KNOWN_FIELDS` and
  rules), the GBA ELF and `SAVESTATE_NEW_GAME_DATA` are the same (SHA-1s in
  `summary.json`); otherwise it is made again. The patch carries the CRC-32s
  of the part's `meta.json`, `regions.bin` and `hostdata.bin`, and the hook
  refuses it when one changed since (a part saved again into the same
  directory with other lag or affine input files keeps `meta.json` byte for
  byte);
- refused: the NULL probe mode, a player save file, the clock replay, and a
  part of another kind than the run.
The hook checks that the patch is for its own binary (CRC-32), the same key
file and the same ROM (CRC-32 of the ROM file; the GBA runner takes the same
of its ROM), and that the restore put its tick at T.

**Same binary: exact. Rebuilt binary: best effort.** A resumed run of the
binary that made the photo is exact (every rewrite is the identity; the
resumed traces are byte-identical to the full runs). A photo resumed by a
rebuilt binary is best effort: the relink rewrites the whole 32-bit host
addresses it can show, and some things in GBA memory are not whole
addresses. So a gate (a check-all row, any result that matters) takes its
photo with the binary under test (`make snapshot` first, minutes); resuming an
older photo in a changed binary is for quick looks. What the audit has
found on route ch5 (review of `port-savestate2`, a real code change; none of
it changed a resumed trace):
- halves of host addresses. A proc's user area is typed as that proc's own
  struct, which the relink cannot see (it is scanned with the strict rule).
  Where the game keeps half of an address in a 16-bit field, or a 16-bit
  field sits beside padding that an earlier proc in the slot left holding
  half of an address, the GBA keeps half of its own address there and the PC
  half of a host address. A half keeps the saved binary's value in another
  binary. The audit lists a high half inside the binary's image, and a low
  half only when it is a plausible place (a function's start, or a game data
  object's start, element or member) whose low 16 bits are A's, B's and the
  GBA's own half there (`EventScr_08592114+0x28`: PC 0xEE80 and 0xFE80, GBA
  0x213C). Without the GBA's half, a low half proves nothing: any object of
  64 KiB or more ends in every half (a NULL-probe table once "explained" a
  16-bit change by its own shift);
- a leftover byte of an old host address, `UNEXPLAINED` in the audit:
  `BMapMainProc` +0x35 from about tick 5,064 on (17,848, 28,865 and 48,648
  tried; GBA 0x21, 0xEE, 0xEF, 0xF0 or 0xFE in the PC binaries tried). A
  `StdEventEngine` proc in that slot kept
  `EventScr_08592114` at +0x34 (tick 4,938); the save menu then wrote only
  +0x34, +0x36 and +0x37, so +0x35 is byte 1 of that old address, on the GBA
  as on the PC. Nothing reads it later (a watchpoint from 4,945 to 28,866,
  plain run and lag replay). The same kind in other slots at chapter ends in
  the lag replay (0x02025340 at 28,865, 0x02025778 at 48,648, 0x02025B44 at
  75,618). Harmless, and no relink can carry a byte; one byte has too little
  information for the audit to tell it from a real difference;
- a live 16-bit field read as an address, `UNEXPLAINED` in the audit:
  `MenuItemProc.yTile` in the plain and affine runs (slot 17, a
  `sProc_MenuItem` from tick 6,717 to at least 48,648): yTile 4 sits beside
  padding that holds the high half 0x102B of an older proc's
  `gProcScr_E_FACE` pointer, the word reads as `ProcScr_MapAnimEnd+0x20`, and
  the relink rewrites it, so yTile becomes 4 plus that table's shift (0x1004,
  0xE8, 0x314 in the binaries tried). `uimenu.c` reads yTile for the hover
  and the cursor. Those runs leave the GBA's path, so the GBA check cannot
  see it. It went unread in every window tried (even with 12 extra cursor
  presses), so it is harmless on route ch5, but not by construction: a
  rebuilt-binary resume of a plain or affine part can move a menu item's
  cursor row. (Typing procs' user areas from the DWARF of the functions their
  scripts call would fix it; not done.)
- fixed by the GBA check (no longer in the audit): tilemap words in
  `gGenericBuffer` at 550 to 601 (all three PC kinds; `0x10131012` was read
  as `CompressedText_MSG_9F9+0xA` and rewritten), and the lag replay's free
  slot 25 +0x34 at 48,648 (`0x102A0040`, GBA `0x08590040`, read as
  `UnitDef_088C8BDC+0x1F4`).

**Resumed traces.** The header has `"resume":{"tick":T,"set":ID,"part":P,
"rom_crc32":...,"keys_crc32":...}` (the PC's also `saved_exe_crc32`); the first
line is tick T-1's (the prologue: the snapshot run's own line, its raw record
first in the dump; a GBA frame15 PNG is copied into the snapshot), then ticks
T on. The hook's readers of a GBA trace (lag and affine replays) index a
resumed one by absolute tick. `gate0_compare.py` stops (exit 2) unless every
trace is resumed from the same snapshot set, tick, ROM and key file, each from
its own part (`--pc`: pc, or pclag for a lag replay compared on its own;
`--lag-replay`: pclag; `--affine-replay`: pcaff). It compares from the
prologue tick on: there the clock rule's offset and the clock copies are
seeded from what the two sides show, and the frame rule has no previous tick.
A resumed lag replay's header also carries its own record of ticks 0 to T-1
(`resume.prefix`: ticks whose main-loop event count was not the GBA's, lag
VBlanks run off the GBA's position, and `left_at_tick`, the first tick of
either, from the hook's restored totals and `sLagOffTick`): if it had left
the GBA's sequence there, no tick of the window is lag-only and the route
check is `DIFFERENT`, as in the full run's compare; the verdict line and
`sequence_left_at_tick` name that real tick ("left the GBA's sequence at tick
6541 (before the snapshot at tick 17849: the run it resumes)"), with
`sequence_left_before_first_tick` true. A PC trace resumed by another binary
than the one that saved the snapshot is noted (its host addresses were
relinked). **Limit:** a resumed compare otherwise sees only its window: a
difference before T is in the full run's compare, not in this one, and the
rules that carry state from tick to tick start at the prologue. The clock
rule and the clock copies in `gPlaySt` take their offsets from what the two
sides show at tick T-1, so an offset the full compare had already refused
before T is the resumed compare's starting point (route ch5, plain run, at
17,848: the full compare labels every tick of the window not lag-only
through the `gPlaySt` clock copies, which it refuses from 5,726 on; the
resumed compare from 18,633; both `DIFFERENT`). The route check (the lag replay on its
own) agreed with the full run on every window tried (below).

**Not supported (refused):** a snapshot at tick 0, `GATE0_STATE_OUT` and
`GATE0_STATE_IN` together, mixing kinds of runs, the clock replay, a
`GATE0_STATE_AT` tick the run does not reach (`GATE0_TICKS` must be past it),
a resume whose `GATE0_TICKS` is not past the snapshot's tick, `GATE0_STATE_*`
without `GATE0_OUT` (PC), and `MGBA_RUN_STATE_IN` without `GATE0_STATE_IN`
(GBA).

**`savestate.py audit --a A --b B`** relinks snapshot A for B's binary and
compares the result with B, word by word: A and B are the same run and tick
saved by two binaries (for example one rebuilt with `-falign-functions=32`, so
every host address moves; or a real code change). It relinks as `relink`
does, with A's gba part and the GBA ELF (`--gba-elf`, default
`fireemblem8.elf` in the current directory) for the GBA check. Every word
that differs is either rewritten correctly or listed with what it is (halves
of host addresses, above, a low half only with the GBA's own half; the last
DMA source or destination in the write-only DMA address registers, with what
each value is; log-once code addresses); anything else is `UNEXPLAINED` and
fails (on route ch5 with a real code change: the leftover byte and the live
`MenuItemProc.yTile` above, from about tick 5,000 on).

### Result (2026-10-20, branch `port-harness8a`: the review's eight smaller findings)

Route ch5. The branch's own photos at 550, 17,848 and 48,648; scratch
rebuilt binaries (never committed): M0 (new code, a const table and no-ops
in `src/bmitem.c`, `OnVBlank` and `port/render/bg_line.c`; host addresses
moved up to +0x10E4), M (M0 plus a writable game table), D (a new game
counter `OnMain` counts up), S (`ClearOam` moved below `DrawGlyph`), R
(`ClearOam`'s body moved into a new function).
- Audit, the branch's photo relinked for the rebuilt binary against the
  rebuilt binary's own photo: tick 550 PASS for M0, M (with
  `SAVESTATE_NEW_GAME_DATA=initial`) and S on all three PC parts (the
  previous tool fails the same photos: the `gGenericBuffer` false rewrite,
  and S's two `sLogged` swapped). 17,848 and 48,648 (M0): what is left is
  the two classes above, the leftover byte (all parts) and the live
  `MenuItemProc.yTile` (plain and affine); the lag replay's slot 25 +0x34 is
  now left alone by the GBA check; the low halves listed are confirmed by
  the GBA's own half.
- Resumes: the same binary from 550 (the Makefile flow: GBA, plain, lag
  replay, affine) and from a fresh photo at 20,000 equal the full runs, 1,201
  of 1,201 lines each, raw byte-identical. M0 from the branch's photos at 550
  (against the full runs) and 48,648 (against the branch's own resume): 1,201
  of 1,201 lines on all three PC kinds, raw identical (`gMainCallback` by
  name).
- Refused as they should be: M and D (new game `.data`, named; M accepted
  with `SAVESTATE_NEW_GAME_DATA=initial` and a warning), R (the
  function-static of a function the new binary does not have); a lag-replay
  part saved again into the same directory with other tick-550 lag positions
  (`meta.json` byte-identical): the direct resume with the first patch is
  refused ("its regions.bin differs"), `relink` remakes the patch; a patch
  of the previous format (`FE8SNPT2`, wrong magic). The patch cache remakes
  the patch after a `PORT_TABLE` edit (`gPortFrameCount` made session) and
  again after the edit is undone.

### Result (2026-10-20, branch `port-savestate2`: merged with main, route ch5)

With main's runner (the IRQ-resume fix), the lag replay of route ch5 no
longer leaves the GBA's sequence (0 ticks with another event count to
30,100). Snapshots by `make snapshot` at 1, 550 (both with lag VBlanks inside
tick T's main loop), 2,450 (battle animation), 17,848 (lag inside the main
loop and at the end of the tick) and 28,865 (the early flush tick); full runs
from power-on to 30,100 taken separately; each snapshot resumed for 1,200
ticks through the Makefile (`route`, `pc-trace`, `pc-affine-replay`,
`compare`):
- GBA capture, plain run, lag replay and affine replay: 1,201 of 1,201 lines
  equal to the full runs at all five ticks, raw records byte-identical (the
  lag replay at 1, 550 and 17,848 included: tick T's lag VBlanks run at their
  GBA positions before the photo). The snapshot runs' own traces up to T equal
  the full runs'.
- Route check (lag replay vs GBA): `MATCH` on every resumed window; the full
  route compare has no differing tick inside any of the windows (`MATCH` on
  0-1,200, 0-1,749 and 0-3,649; `DIFFERENT` on 0-19,047 and 0-30,064 only
  for ticks before the windows: 4 before 17,847 and 6 before 28,864, the
  first a `frame15` difference at 3,780).
- A rebuilt binary with a real code change (scratch, not committed: a
  function, a const table and a writable table added at the top of
  `src/animedrv.c`, a table and a function in `src/bmunit.c`, a function in
  `port/hw/frame.c` and nine no-ops in `PortRunFrame`; host addresses moved by
  +0x40 to +0x320): the merged binary's snapshots at 2,450, 17,848 and 28,865,
  relinked (243-275 host addresses moved), resumed in it: plain run, lag
  replay and affine replay 1,201 of 1,201 lines equal to the full runs
  (`gMainCallback` by name), raw byte-identical; route check `MATCH` on each
  window. Audit against the rebuilt binary's own snapshots: PASS at 2,450 (3
  parts); at 17,848 and 28,865 it lists the identified halves of host
  addresses and fails on the two cases above.
- Refused as they should be: a lag-replay snapshot at T with a GBA trace that
  ends at T-1; a resume with `GATE0_TICKS` at T (PC and GBA); `GATE0_STATE_AT`
  the run does not reach (PC and GBA); `MGBA_RUN_STATE_IN` alone;
  `GATE0_STATE_IN` without `GATE0_OUT`; a binary with `psg.c`'s `sPsg` fields
  swapped (same size: "laid out differently"); a binary with a new port
  global `PORT_TABLE` does not list, and with that global listed as state
  ("not in the snapshot's binary"); a part saved again into the same
  directory (the hook refuses the old patch; `relink` makes a new one).

### Result (2026-10-20, branch `port-savestate`, route ch5)

(Before the merge with main: that runner dropped a call at 6,541 and 11,912,
so the lag replay left the GBA's sequence there.)

Snapshots at 20,000 and 100,000 (`make snapshot`, all four parts), full runs
from power-on to 106,000 taken separately (no snapshot in them), and each
snapshot resumed for 6,000 ticks (to 25,999 and 105,999):
- **Same binary:** the resumed GBA capture, plain PC run, lag replay and affine
  replay each equal the full run from tick T-1 (the prologue) to T+5,999, every
  line text for text (6,001 of 6,001; GBA frames compared as PNG files, all
  equal) and every raw record byte for byte.
- **Rebuilt binary** (`CC="gcc -falign-functions=32"`, every function and data
  address moved, for example `__wrap_OnMain` 0x100FA16D to 0x10119040): the
  snapshots of the first binary, relinked, resumed in it: plain run, lag replay
  and affine replay equal the full run on all 6,001 lines and raw records
  (`gMainCallback` compared by the function it names in each binary). The
  relink rewrote 271 to 313 host addresses per snapshot.
- **Audit** (the same ticks saved by the rebuilt binary from power-on; lag
  replay and plain run at 20,000 and 100,000): PASS on all four. What no
  relink can carry, each listed: the host stack address of the last DMA
  source in the write-only DMA3SAD register; one or two words in procs' user
  areas that hold the high 16 bits of a host address beside a 16-bit field
  (at 20,000 the padding after `EventEngineProc.mapSpritePalIdOverride`,
  where the GBA holds 0x080D, the high half of one of its own addresses, and
  the PC 0x1010); and the log-once list of BIOS-copy call sites in `bios.c`,
  whose functions the rebuilt binary laid out differently.
- The snapshot runs' own traces up to T equal the full runs' (all four
  kinds), so taking a snapshot changes nothing in the run.
- `make route GATE0_STATE_IN=.../tick100000 ROUTE_TICKS=106000`: RESULT
  matches on every tick of 99,999-105,999, and the verdict is `DIFFERENT`
  because the lag replay's prefix had left the GBA's sequence at 6,541
  (`resume.prefix`), as in the full route compare.

Time (one route check of ticks 100,000-105,999; up to three runs at once on
this machine): from power-on the GBA capture took 245 s and the PC lag replay
99 s to tick 106,000, before the compare; resumed, `make route` took 23 s in
all (GBA capture 14 s, relink 2 s, PC lag replay 6 s, then the compare).
Making the snapshot at 100,000 once took 227 s (GBA) and 92 to 97 s per PC
part.

## TAS route (a published TAS played by frame, then as a tick route)

A tool-assisted run of the whole game would cover far more of it than the bot
routes. The inputs used (TASVideos 1843M by Vykan12, and alyosha's console
list of the same run; credited in THANK-YOU.md rows 22-23) are third-party
data: the tools below only parse them, and everything made from them (the
per-frame key files, tick routes, milestone lists, traces) is local only and
never committed. It goes to `port/test/tas/`, whose `.gitignore` ignores
everything else, or to `build/`.

**Decoding: `tools/tas_inputs.py`.**
- `vbm MOVIE.vbm OUT.keys`: a VBA-rr movie, VBM version 1 (header, start,
  controller, system and emulator flags, the input offset, one 16-bit word per
  frame). Entry k of OUT is the word of frame k (bits 0-9, the KEYINPUT order
  of "Input file"). It refuses a movie that starts from a snapshot or SRAM,
  uses other controllers, is not for the GBA, or has reset or motion bits (the
  oracle has no movie reset). The 1843M movie: 247,872 frames (247,873 words),
  start flags 0 (power-on, blank SRAM), emulator flags 0x70 (no BIOS file: the
  emulator's HLE BIOS and a start at the cartridge entry, as the oracle; new
  GBA timing), ROM BE8E, no reset bits, R never pressed.
- `console LIST.txt OUT.keys`: a console list, one `HHHHHHHH KKKK` line per
  key change. The time unit is 4096 CPU cycles from console power-on (BIOS
  boot included): the common gaps are 68 and 69 units (a frame is 68.578), and
  every change lies within 2.96 lines of a VBlank start (line 160), so the list
  is per console frame, latched at VBlank start as a movie player does. The
  mask is KEYINPUT order with L and R swapped (0x100 is L: the VBM's L presses
  are the list's 0x100). Entry k of OUT is console VBlank interval k.
- `compare MOVIE.vbm LIST.txt`: the two lined up. 12,198 of the VBM's 12,200
  key changes are in the list, in order; the list has 3,404 more change
  entries; the console runs 273 frames behind the VBM at the first press and
  6,594 at the end (the console is slower than VBA-rr).

**Playing by frame: `oracle/movie_check.lua`** (`make -f port/test/Makefile
movie-check FRAME_KEYS=FILE FRAME0=N`). Interval 0 runs from power-on to the
first `"frame"` callback (the first VBlank start), interval n from callback n
to n+1; interval n plays entry n + FRAME0, set at callback n, which is when a
movie player latches its next frame (VBlank start). No breakpoints, so a whole
run takes about two minutes. It prints milestones (route_lib.lua's; every turn
change; blue units that die; each top-level screen as it starts: health and
safety, opening animation, title, save menu, difficulty, opening subtitles,
world map, battle map, chapter intro, preparations, game over, credits) with
frame, tick, chapter, turn, phase and RNG; `MOVIE_OUT` writes them as JSON
lines. `MOVIE_TICKS=1` plays an ordinary tick key file instead, with the same
milestones, so a tick route made from a movie can be checked against it.

**Did the game take the press: `oracle/movie_presses.lua`** (diagnostic).
For each press that starts in a frame window it saves the core's state, runs
`SCAN_K` frames (10) with and without that press, and compares a hash of the
proc array, `gBmSt` (without `prevVCount`), `gPlaySt`, the RNG, the first blue
units, the action data and `gMainCallback`: equal means the game did not react
(`IGNORED`). A TAS makes no press the game ignores on the machine it was made
for (a few deliberate no-ops aside), so the first ignored press of a run is
where it left that machine's timing.

**Frame mode in the Gate 0 capture** (`make -f port/test/Makefile frame-trace
FRAME_KEYS=FILE FRAME0=N TICKS=N`, writes `build/gate0/frame.jsonl` and
`frame_frames/`): `gate0_capture.lua` with `GATE0_FRAME_KEYS` sets the keys per
frame as `movie_check.lua` does, and needs one more breakpoint, `gate0_keyread`
at UpdateKeyStatus's load of `REG_KEYINPUT` (0x080013E6, the only read of it in
`src/`). Each tick line's `keys` is then the mask the game read in that tick
(taken again at `_resume` when an IRQ came at the breakpoint), `key_reads`
counts the reads, and `key_reads_differ` marks a tick whose reads saw different
masks. Save states are refused in frame mode (they are made from the tick
route). In tick mode nothing changed: the route ch1 capture (21,273 ticks) and
its raw dump are byte-identical to those of the previous script and runner.

**Console timing: `runner_cycles()`.** The runner gives scripts the CPU cycles
since reset (`mTimingGlobalTime`), so a key change could be applied at its
exact cycle instead of at VBlank start. Measured on the console list's first
5,000 frames: the game reads `REG_KEYINPUT` 4 lines or more after the VBlank
start (OnVBlank runs first; most reads 30 to 60 lines after it), never within
the 2.96 lines in which the list's changes lie, so there the per-frame model
gives the same reads as the exact one. mGBA's skip-BIOS start
mimics the BIOS hand-off (the first VBlank start comes 41,046 cycles, 33.3
lines, after reset: VCOUNT 126 at the start).

**From frames to ticks: `tools/tas_route.py`.**
- `make FRAME_TRACE OUT.keys --last-tick N [--milestones M.jsonl] [--note T]`:
  ticks 0 to N of a frame-mode trace as a key file (entry t = the mask read in
  tick t), refused if a tick's reads differ; the milestones up to N go in the
  header.
- `verify FRAME_TRACE TICK_TRACE --last-tick N`: the ordinary Gate 0 capture
  of that key file must equal the frame-mode trace in every field of every
  tick (`key_reads` aside), frame15 PNGs byte for byte.
- `diff-milestones A B --last-tick N`: two `movie_check.lua` milestone lists.

### Result (2026-10-20, branch `port-tas2`, mGBA 0.10.5, HLE BIOS)

Neither source plays far on the oracle by frame. Each run stays on script
while every press lands when the game takes it, and leaves at the first press
that comes before mGBA's game is ready for it:

| Source, start | Last on-script point | Leaves because |
|---|---|---|
| VBM, frame 0 = interval 0 | none: the first press (Start, frame 133) is ignored | mGBA's health and safety screen takes keys from frame 137: VBA-rr boots 4 frames faster |
| VBM, shifted 4 frames (first press at 137) | the H&S press | the second press (Start, VBM 364, here 368) is ignored; mGBA takes it from 369 |
| VBM, shifts -1 and +1 | none | as frame 0 |
| Console list, interval 0 = console frame 269 (first press at 137) | tick 767, frame 830: the Prologue's world map, its region-map intro | the 10th press (Start, console frame 1099, mGBA frame 830, read in tick 768) is ignored; mGBA's world map takes it from frame 831 |
| Console list, 264 to 268 | the same press, later | a constant shift moves every press and every wait alike |

Console list, interval 0 = console frame 269: the first nine presses land as
on the console (the tick that read each): Start on the health and safety
screen (122), Start twice through the logos and the opening animation (353,
359), Start on the title (384), A on New Game (424), Up then A in the
difficulty select (460, 465: **Hard**), A to confirm (514: new game,
`chapterStateBits` 0x40), Start through the opening subtitles (652); the world
map from tick 673. On the console the region-map intro was
skippable one frame earlier: between the subtitle skip and that press mGBA has
7 lag VBlanks (tick 673: 3, tick 715: 4) where the console had 6. Tick 715 is
one `LZ77UnCompVram` of 19,488 bytes, which mGBA's HLE BIOS charges 1,001,101
cycles (51 a byte), ending 132 lines into its fifth frame; tick 673 (four VRAM
and five WRAM LZ77 decompressions, 609,000 cycles of HLE BIOS time) ends 68
lines into its fourth. The real BIOS decompressing tick 673's data 14 % or tick
715's 16 % faster than mGBA's HLE BIOS charges would explain it (oracle limit:
no BIOS dump; not proven). After that
press the run leaves for good: the opening event is not skipped, the RNG at
turn 1 is `8087 3b4e 8bb3` (the routes' `f4a1 e163 66d8`), the Prologue takes
5 turns, and the movie ends in Chapter 1, turn 23. In tick terms mGBA may well
agree with the console there: the press works from mGBA's tick 769, which is
where the console, one lag frame ahead, would have read it.

The tick route `port/test/tas/route_tas_console.keys` (ticks 0-767, made from
`frame-trace ... FRAME0=269 TICKS=1100`) checked:
- the ordinary capture (`gba-trace`, 768 ticks in 830 frames) equals the frame
  run on every field of every tick, 13 frame15 PNGs byte-identical (`MATCH`);
- `movie_check.lua MOVIE_TICKS=1` gives the frame run's 9 milestones to tick
  767 (frame, tick, text, RNG); `route_check.lua` the new game at tick 514;
- `make route ROUTE_KEYS=port/test/tas/route_tas_console.keys
  ROUTE_TICKS=768`: the PC lag replay (49 GBA lag VBlanks in 21 ticks) matches
  the GBA on ticks 0-767.

No chapter starts on it (the Prologue's map loads at tick 975), so no save
states were made: a snapshot is tied to its key file's CRC-32, so one made on
this file could not start a longer route either.

### Fault run (2026-10-20, `port-tas2`): PC lag replay with the NULL probe

- **On script, ticks 0-767:** `make route` with `FE8PC_NULLPROBE=1`: 0 probe
  sites, no fault, `MATCH` on ticks 0-767 (frame15 every 60 ticks). With
  `GATE0_SHOT_TICKS="0-767"` on both sides (every frame, `tools/frame15.py
  batch`), 2 of 768 frames differ, both in ticks with a GBA lag VBlank and every
  traced field equal: 460 (difficulty select, 7 pixels) and 549 (save menu,
  561 pixels, colours one step apart).
- **Off script, ticks 768-21,299** (extra coverage, not the TAS's plan: the
  same frame run past the lost press, as `route_tas_console_off21300.keys`,
  checked with `tas_route.py verify`: MATCH, 355 PNGs): the Prologue on Hard.
  Without the division probe the PC stops at tick 5,990 with SIGFPE in
  `AddEdgeToWinBuf` (`src/bmlib.c:385`, the minimap window's edge slope, divisor
  0 on the open animation's first frame): every minimap open (Start on the
  map) crashes the PC (fixed on `port-integrate5`: `AddEdgeToWinBuf` divides with
  `PortDivsi3`, the GBA's `__divsi3` result, DIVERGENCES D-46). With the
  probe the run reaches 21,300; the 8 division sites
  are listed (`Minimap_OpenAnim` and `Minimap_CloseAnim`, 4 calls each). The
  lag replay equals the GBA on every traced field to tick 8,254, frames aside
  (every frame compared to 8,254: 94 differ, 35 of them the phase banners'
  fade, P-17). At 8,255 the off-script run opens the map menu's Guide, which
  the PC did not have then (`src/bmguide.c` was not built, P-20: `Guide_Init`
  and `Guide_MainLoop` were generated stubs), and the PC left the GBA's path.
  With `bmguide.c` built (P-20 fixed) the PC follows the GBA through the Guide
  (open 8,255-9,938) on every traced field, frames aside, to tick 15,973; at
  15,974 the run picks Suspend, whose `StartSuspendPrompt` (`src/uiarena.c`, not
  built) is a stub. Results and groups: `port-plan/pending/tas-route.md`,
  "Fault run".
- **Groups 3-5 and the banners again** (2026-10-20, `port-harness8b`, before
  the Guide was built: the format-4 lag events with `lag_split`, the lag
  frames' line interrupts and the raster-timing frames; same route, every
  frame of ticks 0-8,254, the lag replay with `FE8PC_NULLPROBE=1`): 72 frames
  differ (RULES), and all 72 are raster-timing frames (`GATE 0 (LAG REPLAY vs
  GBA): MATCH`): the 35 banner fade frames, the 32 minimap window frames
  (group 3), 3,025, 3,043, 3,948, 8,241 and 460 (group 4 and the text of
  group 5). 549 and the river, 2,351-2,371 (group 5), no longer differ at all.
  The full compare (plain run with `--lag-replay`) is `LAG-ONLY`: 6,080
  lag-only ticks, 71 lag-only with a raster-timing frame, 0 not lag-only, 0
  replay-only, and 460 labelled in the plain run itself. Against the run
  before (`port-tas2` `cdcc59a4`): all 8,255 GBA PNGs are byte-identical, the
  GBA trace differs only in `events` and `lag_at` (format 4) and the new keys,
  and every PC frame of record is byte-identical but the 22 that the lag
  events fixed (549, 2,351-2,371).

### Wider same-buttons run (2026-10-20, `port-integrate5`)

The console list played by frame (`FRAME0=269`) to the end of its inputs:
249,800 ticks in 254,264 GBA frames, the Prologue on Hard and Chapter 1 to
turn 23 with the run's suspends, soft resets and resumes. It is off the TAS's
plan from tick 768 (the lost press above), so it is a same-buttons-in,
same-game-out test, not the TAS. The tick route is made with
`tas_route.py make ... --last-tick 249799` (local only, like every TAS-derived
file) and its ordinary capture equals the frame run on every field of every
tick (`verify`: MATCH, 4,164 PNGs).
- GBA side: the only libgcc divide by zero on the whole run is
  `AddEdgeToWinBuf`'s (620 calls, the minimap; breakpoints on the four `Ldiv0`
  labels). The bot routes ch0-ch5 have none (route ch5's GBA run: 0 calls).
- This branch: the lag replay (NULL probe on) follows the GBA to tick 15,973
  and leaves its sequence at 15,974 (map-menu Suspend, the `uiarena.c` stub
  above). No division site on any run. (History: since `port-integrate9`
  `uiarena.c` is built (DIVERGENCES B-19), and the branch itself plays all
  249,800 ticks; see "Wider same-buttons run" below.)
- A scratch look-ahead build with `uiarena.c` also built (not committed)
  follows the GBA's sequence on all 249,800 ticks (all 4,451 lag VBlanks at
  the GBA's positions) with every traced field equal; the only probe sites
  past the known H-1 drop are three NULL-page reads in
  `PrepareBattleGraphicsMaybe` after a Vulnerary at tick 89,522 (P-21's
  function), which crash without the probe. Every frame compared: 1,771 of
  249,800 differ, all with every traced field equal. Groups and counts:
  `port-plan/pending/integrate5.md`.
- The probe refuses save states, so the 249,800-tick compare was cut into
  windows of the full power-on traces (a resumed trace equals the full run
  line for line, "Save states"), each under 2 GB.

### Wider run with the raster-timing labels (2026-10-21, `port-integrate6`)

The same route, every frame of all 249,800 ticks, with `port-harness8b`'s
capture (format 4, raster log) and labels:
- GBA capture with a frame15 PNG on every tick (19.5 min, 128 MB peak):
  254,264 frames, 242,572 `OnMain` calls; on every tick every field of
  `port-integrate5`'s format-3 capture is equal, apart from `events` and
  `lag_at` (counted in format 4) and the frame15 paths.
- This branch (as it was then), lag replay without the NULL probe, ticks
  0-16,499 (21 s): no fault; it leaves the GBA's sequence at 15,974 (the
  Suspend stub; built since `port-integrate9`, DIVERGENCES B-19, which plays
  all 249,800 ticks plain: MATCH on every tick, 1,684 frames differ, all
  labelled raster-timing frames). On
  0-15,973, 169 frames differ (191 before; the 22 that the format-4 lag events
  fixed are equal), 163 are raster-timing frames and 6 are reported: 9,226 and
  9,234 (GBA lag ticks in the Guide), 9,238 and 9,922 (the Guide's top band),
  10,929 (a minimap frame), 11,093 (the darker map).
- The look-ahead build (this branch plus `uiarena.c`, scratch, not
  committed), lag replay with the NULL probe, all 249,800 ticks (4 min 50 s):
  every traced field equal on every tick, all 4,451 lag VBlanks at the GBA's
  positions (13 split inside a BIOS copy, 33 ticks with `lag_in_other_copy`);
  probe sites: the H-1 drop and the three NULL reads at 89,522. One compare of
  all 249,800 ticks (10 s, 2.3 GB peak, with the GBA's PNGs hashed first):
  1,721 frames differ (1,771 before: 50 frames on or after GBA lag are now
  equal), 1,585 labelled, 136 reported; `DIFFERENT`, on `frame15` only.
- By `port-integrate5`'s kinds: phase-banner fades 630 of 630 labelled; text a
  step ahead 238 of 238; menu text blank 73 of 73; unit-window portrait 75 of
  75; minimap window 560 of 645 (85 reported: the redraw with the GBA's timing
  is not the GBA's frame); frames on or after GBA lag 9 of 21 (12 reported,
  the same); the whole map darker for one frame 0 of 16 (`slots`: the GBA runs
  an HBlank handler from line 161 that the PC's frame step does not run); the
  Guide's top band one frame late 0 of 23 (`nostores`: no watched store while
  the GBA drew the frame, so not the main loop's memory writes).
- 159 frames that are equal, 89,534-89,692 (right after the three NULL reads),
  are marked `differs` (palette RAM): the PC's palette RAM at the frame's end
  is not the GBA's, while the picture and every traced field are.
- The hashing of the GBA's PNGs was checked: on 0-16,500 the `compare.json`
  from the PNG paths (419 s) and from the hashes (1.5 s) are identical, and
  five 50,000-tick windows give the full compare's result.

## Menu monkey (seeded button sequences that open every screen)

Decided 2026-10-20 (port-plan/DECISIONS.md, "evening"): later-chapter
coverage comes from longer bot routes and from "menu monkey" runs. A monkey
run is a seeded, structured button sequence started from a chapter save, a
save state's spot or power-on. It opens the screens the bot routes never do:
the unit menu and every command in it, items and their submenus, trade,
supply, rescue and drop, talk, visits and shops, attack forecasts (sometimes
the attack itself), the stat screen with its pages and help, the map menu's
Unit list, Status, Guide and Options (every option changed and changed back,
and battle animations sometimes left off), the minimap, enemy ranges, R-button
help, the preparations and the world map, and now and then End so that the
enemy and other phases run. The GBA oracle is the referee: the same buttons
go into the PC, and the PC must play the same game. A run does not have to
make sense as play.

**Making one: `make -f port/test/Makefile monkey START=... SEED=n TICKS=n
[MONKEY_OUT=FILE.keys]`** (`oracle/menu_monkey.lua`). It plays the ROM in
mGBA as `route_bot.lua` does (no breakpoints, its tick and key timing) and
writes the keys it pressed as an ordinary key file ("Input file"), which the
normal Gate 0 capture and the PC lag replay take.
- `START`: a `.sav` (booted with `GATE0_SRAM_IN`; `inputs/resume.keys`, 2,000
  ticks, loads it with Continue or Resume Chapter), a save state directory
  `DIR/tick<T>` (its key file's first T ticks played from power-on as the
  prefix, with the save its run booted from; the state itself is not loaded,
  since a key file is tied to its snapshot by CRC and the NULL probe refuses
  states, "Save states"), or `power-on` (`route_ch0_prologue.keys` to the
  Prologue's first free cursor, tick 1,984).
- `TICKS` (default 6,000): the monkey's own ticks after the prefix.
- `SEED` (default 1): the same START, SEED, TICKS and ROM give the same file,
  byte for byte. The script uses only its own xorshift32 numbers, game RAM and
  its own state (no time, no `pairs()` order).
- Output (default `build/gate0/monkey/<start>_s<SEED>.keys`; `OUT` is this
  makefile's output directory, hence `MONKEY_OUT`): the key file (header: seed,
  start and its SRAM's FNV-1a, prefix, tick count, the screens it opened);
  `.coverage.json` (every screen of a fixed list with its first tick, the
  monkey's states, activities, menu commands and target kinds, and every proc
  script that appeared, by symbol name); `.trace.jsonl` (its own view of every
  tick in the Gate 0 trace's fields, plus its state, for the checks below).
- It finds procs, menus, idle callbacks and RAM by name from
  `arm-none-eabi-nm fireemblem8.elf` (`build/gate0/fireemblem8.nm`, made by the
  target), not by hard-coded address.

**How it aims.** Each time its queue of keys runs out it looks at the screen
in front (the first rule that matches: game over, an event, a talk box, a
help box, the stat screen, the unit list, trade, supply, a shop, the Guide,
Options, Status, the minimap, the save menu, the support screen, a menu, a
target selection, the preparations, the free map cursor, the move range, a
battle animation, another phase, the world map) and queues the next few keys
for it:
- Free cursor: an activity, weighted: a unit to act with, an enemy or green
  unit (its range), a unit's stat screen (R), the map menu on an empty tile,
  the minimap (Start), L, a wander (taps, held keys with repeat, B held for the
  fast cursor), or End (more likely the longer the turn has gone on). The
  cursor goes there one tile at a time, as the bot's does.
- Move range: a destination among the reachable tiles: next to an enemy,
  toward the nearest one, next to an ally (trade, rescue, talk), on or next to
  a village, house, shop, chest or door, staying, or anywhere; sometimes B, R
  or L first. The lord prefers safe tiles and rarely confirms an attack.
- Menus by their `MenuDef`: the unit menu's commands are weighted (Attack,
  Item, Trade, Supply, Rescue and Drop, Talk, Visit, shops and so on; Wait
  more often the more the same unit's menus have come back), the map menu's
  entries likewise, other menus evenly; R (the help box) or B now and then.
- Target selections by their `SelectInfo`: a few cursor moves, sometimes the
  help (the battle forecast's for attacks), then A or B.
- Screens (stat screen, unit list, trade, supply, shop, Guide, support
  screen, Status, minimap, preparations' sub-screens): a seeded number of
  presses from the screen's keys, then B out. Options: on the first visit
  every row is changed and changed back (Right, or Left at the last value),
  later a few rows; the battle animation row is sometimes left changed.
- Preparations: the main and map menus' entries, Check Map, sub-screens, for
  a seeded number of ticks (900-2,400), then Start.
- Events: mostly Start to skip, sometimes A or B; talk boxes: A (sometimes B);
  battle animations, other phases and busy ticks: mostly waits with a few
  random presses. Every press is a tap, a fast double tap or a hold, and
  about one in 30 gets one more random key right after it, while the screen
  changes (never Start during the preparations).
- A watchdog presses B, B, A when one screen lasts 3,000 ticks.

**Kept off on purpose**: the map menu's Suspend, Retreat, and the Arena
command. Suspend and the Arena were known stops on the PC when the monkey was
made (`src/uiarena.c` was a generated stub; built since P-24), and they stay
off so the committed shard files keep their meaning. It never presses the
soft-reset combination or opposite d-pad directions together.

**Checking one: `make -f port/test/Makefile monkey-check MONKEY_KEYS=FILE.keys
[NULLPROBE=1] [EVERY_FRAME=1]`.**
1. The route check ("Lag replay"): the GBA capture, the PC lag replay with the
   NULL probe (`FE8PC_NULLPROBE=1`, unless `NULLPROBE=0`; its table in
   `FILE.sites.jsonl`), and that replay compared with the GBA
   (`GATE 0 (LAG REPLAY vs GBA)`, frame15 every 60 ticks).
2. `tools/monkey_check.py replay`: the normal capture must be the monkey's own
   run on every field of every tick, frame and lag counts included
   (`MONKEY REPLAY: MATCH`): the key file really is the run the monkey saw.
   An early flush tick ("Early flush VBlank") is listed, not compared: the
   capture samples after that VBlank, the monkey's frame callback before it.
   With the frame rule the monkey's own ticks can also go a frame late at an
   early flush, and stay late (cov9 H-B: `ch5_start_s5`, from the early flush
   at 9,306 the monkey's `frame` and `gGameClock` were the capture's + 1 on all
   694 later ticks, `MONKEY REPLAY: DIFFERENT`, and the key file was no longer
   exactly the run the monkey saw). `make monkey MONKEY_TICK=capture`
   (`ROUTE_TICK=capture`, `oracle/route_lib.lua` "Tick rules", with the Gate 0
   breakpoints, so slower) makes the monkey sample at the capture's own point,
   early flushes included, as the bot routes from `route_e11.keys` on do. The
   default stays `frame`: every file in `inputs/monkey/` was made with it and
   regenerates byte for byte with it; a file made with `capture` says so in
   its header (`# Tick rule: capture`).
3. With `EVERY_FRAME=1`: a second GBA capture and lag replay that keep every
   frame (`FILE.frames/`, about 100 MB per 1,000 ticks: delete it after), the
   check that this replay is the same run as step 1's on every field but the
   frames (`SAME RUN`; frame15 and the raster-timing redraw's keys are the
   frames' own), and `monkey_check.py frames`: every frame compared in
   chunks of 1,000 ticks on 2 processes (numpy). This pass also has the GBA's
   raster log (as `gba-trace`), and the PC redraws every frame from it
   (`GATE0_RASTER_FROM`, "Raster-timing frames"). Each differing frame is
   labelled, first by that exact rule: `raster-timing frame (P-17)` when the
   PC's redraw with the GBA's timing (from the GBA trace's own raster log
   block) is exactly the GBA's frame (decision 9). Otherwise by the
   heuristics: `phase banner (P-17)`, `minimap (P-17 family)`, `after GBA lag`
   (a lag VBlank in that tick or the one before), `busy menu tick` (a single
   frame in a menu-type screen that matches no GBA frame within two ticks),
   or `other`, which is the one to look at (`FILE.frames.jsonl` has the
   lines, pixel counts, the PC's raster mark and why the redraw did not pass,
   and whether the PC frame, or just its differing lines, equals the GBA's
   frame one or two ticks before or after). The frames pass when none
   differs or every one that differs is a raster-timing frame.
The last line is `MONKEY CHECK: PASS` only when the route check, the replay
check, the probe and every frame (if run) all pass. The probe passes with no
site other than NULL-page writes that H-1 already drops (`how` `h1-drop`;
`port/tools/nullprobe.py report --out DIR --routes NAME` symbolises a table)
and a normal end. A known raster-timing frame on a 60-tick sample fails the
route check too, so read the frame labels: every differing frame of a run is
in `FILE.frames.jsonl`, and the route check's frame15 ticks are among them.

**Limits.** A save start is not the route ("Chapter saves", the RAM history
differs). The monkey reads RAM to aim, so a port difference only shows in the
check, never in the key file (it is made on the GBA). Under 60,000 ticks a
run needs no compare lock and stays well under 1 GB. The frame labels are
heuristics from the monkey's own view of each tick; `other` is what is left.
The fixed screen list of the coverage report names the save menu by
`ProcScr_SaveMenu` only, so the preparations' save screen shows as the
`savemenu` state, not in that list.

### Result (2026-10-20, branch `port-monkey9`, menu-monkey-1)

Two starts from the coverage scout's shards (`ch2_start.sav`, shard A:
Continue, the world map, Chapter 2 with green units; `ch5_start.sav`, shard B:
Chapter 5 with its preparations), two seeds each, `TICKS=6000` (8,000 ticks
with the `resume.keys` prefix). The key files are in `inputs/monkey/`
(`<start>_s<seed>.keys`; made by us, from saves that are not in git).

| Run | SHA-1 (key file) | Determinism | Replay check | Route check (lag replay vs GBA) | NULL probe | Every frame (8,000) |
|---|---|---|---|---|---|---|
| `ch2_start_s1` | `da81323a` | same file and digest twice | MATCH, 20 fields x 8,000 ticks | every traced field equal; frame15 differs on 1 sample (5,520: after GBA lag) | 1 site, H-1 write drop | 54 differ: 19 minimap, 15 banner, 15 busy menu, 3 after lag, 1 world-map name box, 1 other |
| `ch2_start_s2` | `fa0dbd62` | same | MATCH | every field equal; frame15 on 2 samples (4,680, 7,620: banners) | 2 sites, both H-1 write drops | 76 differ: 47 banner, 20 busy menu, 3 after lag, 1 name box, 5 other |
| `ch5_start_s1` | `f8679c65` | same | MATCH | every field equal; frame15 on 1 sample (4,500: minimap) | none | 146 differ: 65 minimap, 64 banner, 8 busy menu, 5 name box, 1 after lag, 3 other |
| `ch5_start_s2` | `b65986ea` | same | MATCH | **MATCH** | none | 79 differ: 10 busy menu, 6 name box, 3 after lag, 60 other |

Seeds 1 and 2 leave each other at tick 2,005, the monkey's sixth tick. The
`other` frames are two kinds, both frame-only (every traced field equal), in
`port-plan/pending/cov9-monkey.md`: **M1**, one PC frame with the map's background
layers darker, which the GBA never shows (10 times, when the map is redrawn
behind a window: the trade screen, a Trade/Rescue/Drop target confirmed or
cancelled, a unit picked in Check Map), and **M2**, a 7-pixel row of one sprite
(OAM 42, 8x8 at x 233, y 24, tile 0x15) at the right edge of the preparations'
item list for 20 to 40 frames. Screens opened
across the four runs: the map menu (Unit list, Status, Guide, Options with
every option changed and changed back), the unit menu (Item, Trade, Supply,
Rescue, Drop, Visit, Wait), the item list and its submenu (Use, Equip, Trade,
Discard, the yes/no box), Trade/Rescue/Drop target selections, the trade and
supply screens, the stat screen and help boxes, the minimap, enemy ranges,
battle animations and map battles (in the enemy phase), the world map, and the
preparations (Check Map, Items, Trade, List, Options, Save). Not reached in
these four: attack forecasts (the armies had not met yet), staves, shops, talk
choices, game over.

Two more runs, same version, also in `inputs/monkey/`: `ch2_start_s3`
(`TICKS=14000`, SHA-1 `88af4b1e`) and `dj_e12_s1` (the debug-jump save of
Eirika's Chapter 12, shard C, `TICKS=6000`, `424b682d`). Both: the replay check
MATCH, every traced field equal in the route check (frame15 only on banner
samples), M1's dim frame again (11 and 1 times). They add a shop, a talk
choice, the weapon select, the battle forecast and the attack target, and two
NULL probe findings, each a crash of the PC without the probe (checked by
resuming each snapshot with the plain lag replay): **M3**, `dj_e12_s1` tick
4,442, B in the attack target selection: `BackToAttackMenu_RestartMenu` calls
`UnitActionMenu_Attack(NULL, NULL)`, which reads `menuItem->availability`
(`src/bmmenu.c:443`, address 0x3D); **M4**, `ch2_start_s3` tick 9,634, at the
start of a phase after End: the terrain-heal animation (`TerrainHealDisplay`
→ `BeginUnitHealAnim` → `PrepareBattleGraphicsMaybe`) reads
`pClassData->number` through a NULL class pointer (address 0x4,
`src/banim-ekrbattleintro.c:1023`, 1374, 1386). With the probe the run went on
and every traced field stayed equal. Details and snapshots:
`port-plan/pending/cov9-monkey.md`.

The files above were made by `menu-monkey-1` (commit `6606a839`; the
version is in each header). `menu-monkey-2` (the next commit) picks attack
positions by the unit's weapon range (a sword user no longer "attacks" from
two tiles away), ends turns a little sooner, and lists the post-chapter save
menu. Checked on `ch2_start` and `ch5_start`, seed 1, `TICKS=6000`: each file
twice byte-identical (SHA-1 `631a29c9`, `6eb0e938`), the replay check MATCH,
every traced field equal in the route check, the probe with H-1 write drops
only, and every frame only known kinds, plus M1 once in `ch2_start` (70 and
112 differing frames). Attack forecasts still need the armies to meet: more ticks, or a
start a few turns in.

### Shard runs kept as key files (cov9, 2026-10-21)

The three monkey shards (`port-plan/pending/cov9-monkey-A-early.md`,
`cov9-monkey-B-middle.md`, `cov9-mk9-C-late.md`) ran `menu-monkey-2`. The runs
behind the finding groups of `port-plan/pending/cov9-findings.md` are kept in
`inputs/monkey/`, byte for byte: a save state checks its key file's CRC-32, not
its path, so these copies resume the snapshots that note names. A bot prefix
(`mkA_ch1_pp1`, `mkA_ch2_pp2`: the bot's `ROUTE_STOP=pp:N` from the save) or
`resume.keys` (the `mkB_` and `mkC_` files) is part of each file. Boot with
`GATE0_SRAM_IN=~/fe8-chapsaves/<save>` (named in the header; none for the
Prologue).

| File | Start | Ticks | Shows |
|---|---|---|---|
| `mkA_prologue_s1.keys` | power-on | 9,984 | Vulnerary with animations on: PC crash at 5,219 (P-21) |
| `mkA_ch1_pp1_s4.keys` | `ch1_start.sav` + bot `pp:1` | 9,449 | B in the attack target selection: crash at 4,963 (M3), story Chapter 1 |
| `mkA_ch2_pp2_s3.keys` | `ch2_start.sav` + bot `pp:2` | 9,585 | the lag replay's Supply-screen gap at 4,491 (harness, A2) |
| `mkB_ch3_start_s5.keys` | `ch3_start.sav` | 10,000 | attacking a cracked wall: crash at 7,840 |
| `mkB_ch4_start_s1.keys` | `ch4_start.sav` | 10,000 | Vulnerary: crash at 8,709 (P-21) |
| `mkB_ch4_start_s2.keys` | `ch4_start.sav` | 10,000 | 5 sprite pixels in the preparations' Items screen from 4,033 |
| `mkB_ch5_start_s6_t16000.keys` | `ch5_start.sav` | 18,000 | the same pixels at 2,846; the crit shake's swapped offsets at 10,435 |
| `mkC_ch5x_start_s1.keys` | `ch5x_start.sav` | 10,000 | M3 at 5,788 (story Chapter 5x), P-21 at 7,727 |
| `mkC_dj_ch6_s3.keys` | `dj_ch6.sav` (debug-jump start) | 10,000 | P-21 at 8,936 |
| `mkC_dj_e12_s5.keys` | `dj_e12.sav` (debug-jump start) | 16,000 | the crit shake at 7,407 (M3 first, at 6,491) |
| `mkC_dj_e21_s2.keys` | `dj_e21.sav` (debug-jump start) | 10,000 | Restart Chapter text at 8,069-8,082: raster-timing frames on `port-integrate6` (P-17, known) |

### Shard runs kept as key files (coverage round 10, 2026-10-22)

The four monkey shards of coverage round 10 (`port-plan/pending/c10-mkA.md`,
`c10-mkB.md`, `c10-monkey-C.md`, `c10-mkD.md`) ran `make monkey` and `make
monkey-check EVERY_FRAME=1` with the NULL probe on port-integrate7. The runs
behind the groups of `port-plan/pending/c10-findings.md` are kept here byte for
byte (a snapshot checks its key file's CRC-32, not its path). Boot with
`GATE0_SRAM_IN=~/fe8-chapsaves/<save>`; a `route_*_tick*` start is that chapter
save plus the bot route's prefix to its first free cursor (in the file), a `dj_`
save is a debug-jump start (the Prologue army). `mk10C_dj_e21x_end.keys` and
`mk10C_dj_e21x_dl.keys` are hand-made (resume, then the few keys that show it).

| File | Start | Ticks | Shows (group) |
|---|---|---|---|
| `mk10A_route_ch8_tick1587_s8.keys` | `ch8_start.sav` | 21,587 | Game Over, title, Extras, Link Arena: the stat screen reads the map with no map loaded, 12,755 (C10-9) |
| `mk10A_route_ch8_tick1587_s4.keys` | `ch8_start.sav` | 21,587 | the same Link Arena read at 17,173 (C10-9; B in attack targeting first, 2,853) |
| `mk10A_route_ch6_tick1668_s6.keys` | `ch6_start.sav` | 21,668 | the same, address 0xFFFFFFFF, 21,143 (C10-9) |
| `mk10A_route_ch7_tick1639_s5.keys` | `ch7_start.sav` | 21,639 | one minimap frame after a slow GBA frame, 21,060 (C10-10) |
| `mk10A_route_ch8_tick1587_s1.keys` | `ch8_start.sav` | 15,587 | the heal-animation reads after Pure Water, 15,379 (round 9's heal group) |
| `mk10A_route_ch8_tick1587_s5.keys` | `ch8_start.sav` | 21,587 | the crit shake's swapped offsets, 9,205 (round 9's argument-order group) |
| `mk10A_dj_e13sk_s3.keys` | `dj_e13sk.sav` | 12,000 | the monkey replay's early-flush slip at 11,861 (harness, H-A1) |
| `mk10B_dj_e18sk_s1.keys` | `dj_e18sk.sav` | 12,000 | Crimson Eye with animations on: the PC stays in the battle from 4,778 (C10-1) |
| `mk10B_dj_e18sk_s3.keys` | `dj_e18sk.sav` | 12,000 | Chapter 18's fire traps: the defeat record for no unit, 2,639 (C10-3) |
| `mk10C_dj_e21x_end.keys` | `dj_e21x.sav` | 3,200 | resume, End: the Demon King's summon goes into the wall, 2,441 (C10-6) |
| `mk10C_dj_e21x_s1.keys` | `dj_e21x.sav` | 12,000 | the same summon in a monkey run, 3,614 (C10-6) |
| `mk10C_dj_e21x_s2.keys` | `dj_e21x.sav` | 12,000 | the summon at 3,877 and the Demon Light read at 3,007 (C10-6, C10-7) |
| `mk10C_dj_e21x_dl.keys` | `dj_e21x.sav` | 3,600 | seed 2 to 2,210, then A: the Demon Light animation's read at 2,960 (C10-7) |
| `mkD_e14_start_s4.keys` | `e14_start.sav` | 24,000 | Eirika's Chapter 14 with the world map, preparations and a Game Over; the prep-screen sprite pixels at two new spots, 3,414 and 15,499 (round 9's frames group) |

### Shard runs kept as key files (coverage round 12, monkey-i, 2026-10-23)

Coverage round 12's shard monkey-i (`port-plan/pending/c12-monkey-i.md`) ran
`menu-monkey-2` from Ephraim's story saves `i9_start` to `i21_start` and the
boosted `ib22_start` (his final chapter's part 2), seeds 1-3 from each save
(the `resume.keys` prefix, 2,000 ticks, then 10,000 monkey ticks: 12,000 ticks
a file). Each was checked with `make -f port/test/Makefile monkey-check
NULLPROBE=0 EVERY_FRAME=1` on `port-c12-monkey-i` (21189271: integration 11 +
`port-cover11`), the plain PC: every traced field equal on every tick of every
file, every frame compared. From `i15_start` to `i20_start` the monkey often
walks the free world map for the whole file (it reaches the chapter in some
seeds only); from `i16_start` seed 3 it entered a skirmish on Chapter 6's map
(0x07) and from `i17_start` seed 3 the Tower of Valni's first floor (0x24).
The frames that are not raster-timing frames are of three known kinds:
P-25's latched HBlank call (DIVERGENCES P-25: the GBA takes an HBlank request
latched while `REG_IE` had it off, right after `UpdateHBlankHandlerState` sets
the bit mid-frame; the PC's `Irq()` drops such requests) in the preparations'
Check Map when `SubtitleHelpDarkener_Init` starts, and when the minimap opens
(`Minimap_InitOpenAnim`) on a GBA lag tick, where the extra line event keeps a
frame of P-17's kind (lines 8-39, drawn before the call) from being labelled;
and one world-map frame (`mk12i_i19_start_s2.keys` 10,396, lines 0-1) that is
the lag replay's limit (section "Lag position"; DIVERGENCES section 5,
"Lag check v2"; the note's N-C12-I1): the GBA's
lag VBlank 10,586 came inside `GMScreenVSync_AddCopyRequest`, between its
stores and its count increment, so the GBA lost one tile-copy request, which
the lag replay, putting the VBlank at a step's edge, does not. Those files end
with `MONKEY CHECK: DIFFERENT` for that reason only (`mk12i_i14_start_s3.keys`
also with `GATE 0: DIFFERENT`: its P-25 frame 4,680 is a `frame15` tick). Four
files also end so for H-A1 (round 10: `monkey_check.py replay` one tick after
an early flush; the GBA-against-PC check is equal there). Boot with
`GATE0_SRAM_IN=~/fe8-chapsaves/<save>` (named in the header).

| File | Start | Ticks | Shows |
|---|---|---|---|
| `mk12i_i9_start_s1.keys` | `i9_start.sav` | 12,000 | I9: world map 2,000, preparations 3,497, guide 5,902, map battle 6,519, level up 6,684; every traced field equal |
| `mk12i_i9_start_s2.keys` | `i9_start.sav` | 12,000 | I9: world map 2,000, preparations 3,346, prep: supply 2,360, unit list 5,738, guide 9,405, battle animation 10,340, supply screen 7,136; every traced field equal |
| `mk12i_i9_start_s3.keys` | `i9_start.sav` | 12,000 | I9: world map 2,000, preparations 3,308, unit list 5,827, map battle 6,212, level up 6,285; every traced field equal |
| `mk12i_i10_start_s1.keys` | `i10_start.sav` | 12,000 | I10: world map 2,000, preparations 3,358, guide 10,885, battle animation 9,179, map battle 11,295, level up 11,369; every traced field equal |
| `mk12i_i10_start_s2.keys` | `i10_start.sav` | 12,000 | I10: world map 2,000, preparations 3,316, prep: supply 2,366, unit list 7,480, battle animation 8,095; every traced field equal |
| `mk12i_i10_start_s3.keys` | `i10_start.sav` | 12,000 | I10: world map 2,000, preparations 3,056, guide 5,422, battle animation 8,386; every traced field equal |
| `mk12i_i11_start_s1.keys` | `i11_start.sav` | 12,000 | I11: world map 2,000, preparations 3,247, unit list 5,818, map battle 8,471, level up 8,544; every traced field equal |
| `mk12i_i11_start_s2.keys` | `i11_start.sav` | 12,000 | I11: world map 2,000, shop 2,446, talk choice 2,503, preparations 6,035, unit list 10,562, guide 8,939; every traced field equal; P-25 frames at 6,682, 6,908 (preparations) |
| `mk12i_i11_start_s3.keys` | `i11_start.sav` | 12,000 | I11: world map 2,000, preparations 3,151, guide 7,033, battle animation 7,972, supply screen 5,500; every traced field equal; P-25 frame at 3,268 (preparations) |
| `mk12i_i12_start_s1.keys` | `i12_start.sav` | 12,000 | I12: preparations 2,396, unit list 4,585, battle animation 5,852, map battle 11,310, level up 8,449; every traced field equal; P-25 frame at 3,198 (minimap on a GBA lag tick) |
| `mk12i_i12_start_s2.keys` | `i12_start.sav` | 12,000 | I12: preparations 2,229, map battle 6,833, level up 7,036; every traced field equal |
| `mk12i_i12_start_s3.keys` | `i12_start.sav` | 12,000 | I12: preparations 2,402, guide 5,099, battle animation 5,708, map battle 4,889; every traced field equal; P-25 frames at 2,668, 3,193, 3,907 (preparations) and 3,988, 4,422 (minimap on a GBA lag tick) |
| `mk12i_i13_start_s1.keys` | `i13_start.sav` | 12,000 | I13: world map 2,000, preparations 3,121, battle animation 6,798, supply screen 11,965; every traced field equal |
| `mk12i_i13_start_s2.keys` | `i13_start.sav` | 12,000 | I13: world map 2,000, shop 2,169, talk choice 2,232; every traced field equal |
| `mk12i_i13_start_s3.keys` | `i13_start.sav` | 12,000 | I13: world map 2,000, preparations 3,094, unit list 4,831, guide 4,421, battle animation 5,408, level up 10,491; every traced field equal |
| `mk12i_i14_start_s1.keys` | `i14_start.sav` | 12,000 | I14: world map 2,000, preparations 3,358, unit list 6,038, battle animation 9,010, supply screen 8,193; every traced field equal |
| `mk12i_i14_start_s2.keys` | `i14_start.sav` | 12,000 | I14: world map 2,000, preparations 4,020, prep: use 2,338, unit list 10,203, battle animation 7,728, level up 8,509; every traced field equal; P-25 frames at 4,647 (preparations) and 4,571, 4,705 (minimap on a GBA lag tick) |
| `mk12i_i14_start_s3.keys` | `i14_start.sav` | 12,000 | I14: world map 2,000, preparations 3,077, unit list 11,516, guide 6,686, battle animation 9,385, supply screen 6,291; every traced field equal; P-25 frames at 4,680 (preparations) and 4,556 (minimap on a GBA lag tick) |
| `mk12i_i15_start_s1.keys` | `i15_start.sav` | 12,000 | I15's world map only (the monkey walked the free world map from 2,000 to the end); every traced field equal |
| `mk12i_i15_start_s2.keys` | `i15_start.sav` | 12,000 | I15: world map 2,000, preparations 10,210, prep: use 2,338; every traced field equal; P-25 frames at 11,205, 11,579 (preparations) |
| `mk12i_i15_start_s3.keys` | `i15_start.sav` | 12,000 | I15: world map 2,000, preparations 5,855, unit list 8,037, battle animation 11,703; every traced field equal; P-25 frame at 7,259 (preparations) |
| `mk12i_i16_start_s1.keys` | `i16_start.sav` | 12,000 | I16's world map only (the monkey walked the free world map from 2,000 to the end); every traced field equal; `MONKEY REPLAY` one tick after the early flush at 6,223 (H-A1) |
| `mk12i_i16_start_s2.keys` | `i16_start.sav` | 12,000 | I16: world map 2,000, shop 2,169, talk choice 2,232; every traced field equal |
| `mk12i_i16_start_s3.keys` | `i16_start.sav` | 12,000 | I16: world map 2,000, preparations 9,525, Ch6's map (skirmish) (0x07) from about 8,874; every traced field equal; P-25 frame at 9,839 (minimap on a GBA lag tick) |
| `mk12i_i17_start_s1.keys` | `i17_start.sav` | 12,000 | I17's world map only (the monkey walked the free world map from 2,000 to the end); every traced field equal |
| `mk12i_i17_start_s2.keys` | `i17_start.sav` | 12,000 | I17: world map 2,000, prep: use 2,338; every traced field equal |
| `mk12i_i17_start_s3.keys` | `i17_start.sav` | 12,000 | I17: world map 2,000, preparations 9,360, prep: trade 10,263, Tower of Valni 1 (0x24) from about 8,880; every traced field equal |
| `mk12i_i18_start_s1.keys` | `i18_start.sav` | 12,000 | I18's world map only (the monkey walked the free world map from 2,000 to the end); every traced field equal |
| `mk12i_i18_start_s2.keys` | `i18_start.sav` | 12,000 | I18: world map 2,000, shop 2,446, talk choice 2,503; every traced field equal |
| `mk12i_i18_start_s3.keys` | `i18_start.sav` | 12,000 | I18's world map only (the monkey walked the free world map from 2,000 to the end); every traced field equal; `MONKEY REPLAY` one tick after the early flush at 10,122 (H-A1) |
| `mk12i_i19_start_s1.keys` | `i19_start.sav` | 12,000 | I19's world map only (the monkey walked the free world map from 2,000 to the end); every traced field equal |
| `mk12i_i19_start_s2.keys` | `i19_start.sav` | 12,000 | I19: world map 2,000, prep: use 2,338; every traced field equal; world-map lag frame at 10,396 (N-C12-I1) |
| `mk12i_i19_start_s3.keys` | `i19_start.sav` | 12,000 | I19's world map only (the monkey walked the free world map from 2,000 to the end); every traced field equal; `MONKEY REPLAY` one tick after the early flush at 7,411 (H-A1) |
| `mk12i_i20_start_s1.keys` | `i20_start.sav` | 12,000 | I20's world map only (the monkey walked the free world map from 2,000 to the end); every traced field equal |
| `mk12i_i20_start_s2.keys` | `i20_start.sav` | 12,000 | I20: world map 2,000, shop 2,446, talk choice 2,503; every traced field equal |
| `mk12i_i20_start_s3.keys` | `i20_start.sav` | 12,000 | I20's world map only (the monkey walked the free world map from 2,000 to the end); every traced field equal; `MONKEY REPLAY` one tick after the early flush at 2,521 (H-A1) |
| `mk12i_i21_start_s1.keys` | `i21_start.sav` | 12,000 | I21: preparations 2,755, prep: trade 3,478, unit list 6,372, guide 6,043, battle animation 10,838, supply screen 4,692; every traced field equal |
| `mk12i_i21_start_s2.keys` | `i21_start.sav` | 12,000 | I21: preparations 2,445, battle animation 7,504; every traced field equal |
| `mk12i_i21_start_s3.keys` | `i21_start.sav` | 12,000 | I21: preparations 2,758, unit list 3,410, guide 7,662, battle animation 8,634; every traced field equal |
| `mk12i_ib22_start_s1.keys` | `ib22_start.sav` | 12,000 | I21x (final part 2): unit list 6,121, guide 10,110, battle animation 11,004, supply screen 2,637; every traced field equal |
| `mk12i_ib22_start_s2.keys` | `ib22_start.sav` | 12,000 | I21x (final part 2): unit list 3,838, battle animation 5,135, level up 6,966, supply screen 2,067; every traced field equal |
| `mk12i_ib22_start_s3.keys` | `ib22_start.sav` | 12,000 | I21x (final part 2): unit list 2,013, battle animation 4,688, level up 6,527, supply screen 11,889; every traced field equal |

### Shard runs kept as key files (coverage round 12, monkey-e, 2026-10-23)

Coverage round 12's shard monkey-e (`port-plan/pending/c12-monkey-e.md`) ran
`menu-monkey-2` from Eirika's story saves `e15_start` to `e22_start`, the
chapters that had only bot coverage: seeds 1-3 from the save (the `resume.keys`
prefix, 2,000 ticks; from `e16_start` to `e20_start` the monkey walks the world
map to the chapter by itself), seed 4 from the save plus the bot route's prefix
to its first free cursor (in the file), 12,000 monkey ticks each. Each was
checked with `make -f port/test/Makefile monkey-check NULLPROBE=0 EVERY_FRAME=1`
on `port-c12-monkey-e` (21189271: integration 11 + `port-cover11`), the plain
PC: every traced field equal on every tick of every file but one tick in two
Chapter 18 files (`gPaletteBuffer` palette 9 colours 12-15 on a 5-VBlank GBA
tick of the opening: N-C11-1's kind, the lag replay's limit; the plain PC has no
lag VBlanks), `MONKEY REPLAY: MATCH` on every file but one (H-A1's early-flush
slip), every frame compared. The only frames that are not raster-timing frames are
P-25's latched HBlank call (DIVERGENCES P-25: the GBA takes an HBlank request
latched while `REG_IE` had it off, right after `UpdateHBlankHandlerState` sets
the bit mid-frame; the PC's `Irq()` drops such requests): the preparations'
Check Map when `SubtitleHelpDarkener_Init` starts, and the minimap opening
(`Minimap_InitOpenAnim`) on a GBA lag tick. Those files end with `MONKEY CHECK:
DIFFERENT` for these reasons only. Boot with
`GATE0_SRAM_IN=~/fe8-chapsaves/<save>` (named in the header).

| File | Start | Ticks | Shows |
|---|---|---|---|
| `mk12e_e15_start_s1.keys` | `e15_start.sav` | 14,000 | E15: preparations 2,411, level up 13,358; every traced field equal; P-25 frame at 3,603 (preparations) |
| `mk12e_e15_start_s2.keys` | `e15_start.sav` | 14,000 | E15: preparations 2,209, level up 7,846, Game Over 12,325; every traced field equal |
| `mk12e_e15_start_s3.keys` | `e15_start.sav` | 14,000 | E15: preparations 2,412, shop 8,124, talk choice 8,225, level up 13,584; every traced field equal; P-25 frames at 2,976, 3,134, 3,369, 3,434 (preparations; snapshot `~/fe8-snapshots/c12-me-p25dark/tick2970`) and 3,191 (minimap) |
| `mk12e_route_e15_tick1501_s4.keys` | `e15_start.sav` + `route_e15` to 1,501 | 13,501 | E15: level up 3,848; every traced field equal |
| `mk12e_e16_start_s1.keys` | `e16_start.sav` | 14,000 | Ch16 E: world map 2,000, preparations 3,525, map battle 7,958, level up 8,035, Game Over 12,320; every traced field equal; P-25 frame at 4,574 (preparations) |
| `mk12e_e16_start_s2.keys` | `e16_start.sav` | 14,000 | Ch16 E: world map 2,000, shop 2,169, talk choice 2,232; every traced field equal |
| `mk12e_e16_start_s3.keys` | `e16_start.sav` | 14,000 | Ch16 E: world map 2,000, preparations 3,180, level up 7,714; every traced field equal; P-25 frame at 4,197 (minimap) |
| `mk12e_route_e16_tick5882_s4.keys` | `e16_start.sav` + `route_e16` to 5,882 | 17,882 | Ch16 E: map battle 10,454, level up 10,531; every traced field equal |
| `mk12e_e17_start_s1.keys` | `e17_start.sav` | 14,000 | Ch17 E: world map 2,000, preparations 3,152, map battle 9,685; every traced field equal; P-25 frames at 3,650, 3,681, 3,816 (preparations) and 3,854 (minimap; snapshot `~/fe8-snapshots/c12-me-p25mm/tick3848`), 4,604, 4,710, 5,338 (minimap) |
| `mk12e_e17_start_s2.keys` | `e17_start.sav` | 14,000 | Ch17 E: world map 2,000, preparations 3,934, map battle 11,417, level up 13,125; every traced field equal |
| `mk12e_e17_start_s3.keys` | `e17_start.sav` | 14,000 | Ch17 E: world map 2,000, preparations 3,842, map battle 13,139, level up 13,448; every traced field equal |
| `mk12e_route_e17_tick4274_s4.keys` | `e17_start.sav` + `route_e17` to 4,274 | 16,274 | Ch17 E: shop 6,101, talk choice 6,166, map battle 9,159, level up 10,041; every traced field equal; the monkey replay's early-flush slip at 6,496 (`vblanks` monkey 2, capture 1, after the early flush at 6,495: harness, H-A1) |
| `mk12e_e18_start_s1.keys` | `e18_start.sav` | 14,000 | Ch18 E: world map 2,000, preparations 3,171; `gPaletteBuffer` differs at 3,063 only (palette 9 colours 12-15, the chapter's opening on a 5-VBlank GBA tick: N-C11-1's kind; snapshot `~/fe8-snapshots/c12-me-pal18/tick3055`), every other traced field equal; P-25 frame at 4,222 (preparations) |
| `mk12e_e18_start_s2.keys` | `e18_start.sav` | 14,000 | Ch18 E: world map 2,000, world map base menu 2,142, shop 2,446, talk choice 2,503, preparations 9,342; `gPaletteBuffer` differs at 9,236 only (the same opening tick, N-C11-1's kind), every other traced field equal |
| `mk12e_e18_start_s3.keys` | `e18_start.sav` | 14,000 | Ch18 E: world map 2,000, preparations 3,021; every traced field equal; P-25 frames at 4,425, 4,747 (preparations) |
| `mk12e_route_e18_tick2665_s4.keys` | `e18_start.sav` + `route_e18` to 2,665 | 14,665 | Ch18 E: supply 3,312, map battle 12,322, level up 12,395; every traced field equal |
| `mk12e_e19_start_s1.keys` | `e19_start.sav` | 14,000 | Ch19 E: world map 2,000, preparations 3,573, map battle 7,134, level up 7,215; every traced field equal |
| `mk12e_e19_start_s2.keys` | `e19_start.sav` | 14,000 | Ch19 E: world map 2,000, world map base menu 2,137 and its supply 2,366, preparations 3,663, staff select 6,634; every traced field equal |
| `mk12e_e19_start_s3.keys` | `e19_start.sav` | 14,000 | Ch19 E: world map 2,000, preparations 3,130, map battle 12,764; every traced field equal |
| `mk12e_route_e19_tick1802_s4.keys` | `e19_start.sav` + `route_e19` to 1,802 | 13,802 | Ch19 E: map battle 7,395, staff select 11,481; every traced field equal |
| `mk12e_e20_start_s1.keys` | `e20_start.sav` | 14,000 | Ch20 E: world map 2,000, preparations 3,642; every traced field equal |
| `mk12e_e20_start_s2.keys` | `e20_start.sav` | 14,000 | Ch20 E: world map 2,000, shop 2,446, talk choice 2,503, preparations 9,416; every traced field equal |
| `mk12e_e20_start_s3.keys` | `e20_start.sav` | 14,000 | Ch20 E: world map 2,000, preparations 3,529; every traced field equal |
| `mk12e_route_e20_tick7440_s4.keys` | `e20_start.sav` + `route_e20` to 7,440 | 19,440 | Ch20 E: map battle 10,990, level up 11,114, Game Over 14,868; every traced field equal |
| `mk12e_e21_start_s1.keys` | `e21_start.sav` | 14,000 | Final part 1 E: preparations 2,758; every traced field equal |
| `mk12e_e21_start_s2.keys` | `e21_start.sav` | 14,000 | Final part 1 E: preparations 2,448, Game Over 10,988; every traced field equal |
| `mk12e_e21_start_s3.keys` | `e21_start.sav` | 14,000 | Final part 1 E: preparations 2,761, level up 11,229, map battle 13,231; every traced field equal |
| `mk12e_route_e21_tick1480_s4.keys` | `e21_start.sav` + `route_e21` to 1,480 | 13,480 | Final part 1 E: convoy 6,558, level up 11,996; every traced field equal |
| `mk12e_e22_start_s1.keys` | `e22_start.sav` | 14,000 | Final part 2 E: level up 13,762; every traced field equal |
| `mk12e_e22_start_s2.keys` | `e22_start.sav` | 14,000 | Final part 2 E: Game Over 5,605; every traced field equal |
| `mk12e_e22_start_s3.keys` | `e22_start.sav` | 14,000 | Final part 2 E: Game Over 13,488; every traced field equal |
| `mk12e_route_e22_tick729_s4.keys` | `e22_start.sav` + `route_e22` to 729 | 12,729 | Final part 2 E: map battle 4,548, level up 4,675, Game Over 5,195; the save menu's resume into the same enemy phase and Game Over five more times, then the slot's save of the final chapter's part 1 (0x15 from 10,466) and its preparations (11,479); every traced field equal |

### The post-game (coverage round 12, shard monkey-post, 2026-10-23)

`port-plan/pending/c12-monkey-post.md`. Continue on a clear save plays the ending
again (`GameCtrl_CheckGameCompleteAndBranch`), so a post-game run starts from a bot
prefix made from the clear save (`route_bot.lua`, `ROUTE_POSTGAME=1`: the title's
Extras, Map, the slot; `inputs/README-routes.md`, "The post-game") instead of
`resume.keys`, and the monkey gets its post-game rules:

    make -f port/test/Makefile monkey START=~/fe8-chapsaves/e_clear.sav \
        MONKEY_PREFIX_KEYS=port/test/inputs/monkey/pg_e_clear_wm.keys MONKEY_WM=post \
        MONKEY_TICK=capture SEED=1 TICKS=12000

- `MONKEY_PREFIX_KEYS=FILE`: that key file, all its ticks, is the prefix (the
  header names it); `START` still names the save both sides boot with.
- `MONKEY_WM=post` (`oracle/menu_monkey.lua`): after its usual exploring presses
  on the world map, the monkey picks a node of its own instead of the node of
  `gPlaySt.chapterIndex`: the Tower of Valni (node 0x1A) or the Lagdou Ruins
  (0x1B) 30, a node with monsters (`gGMData.units[4..6]`, a skirmish) 25, a node
  with an armory, vendor or secret shop (A walks the lord there, A again opens its
  menu) 25, the lord's own node (its menu) 10, any node 10; L moves the cursor to
  the next node, as the bot does (at most 30 L, then a new pick). On the title
  menu (`SameMenu_CtrlLoop`, `SaveMenu_ExtraMenuLoop`) it takes Extras, Map, the
  slot 80% of the visits (the bot's rule), else its usual presses, so a Game Over
  in the post-game leads back to the world map. The header gets the line
  `# World map: post-game (...)`. Unset, nothing changes: the default monkey
  makes `mkD_e14_start_s4.keys` (the world map, preparations, a Game Over) and
  `mk10A_route_ch8_tick1587_s8.keys` (a Game Over, the title menu, Extras, the
  Link Arena) again byte for byte.
- An edited start (test coverage, not story play): `sram_boost.lua`'s edit file
  takes a line `FLAGS set=N,...` (permanent event flags, as `SetPermanentFlag`).
  `e_clear_rc.sav` is `e_clear.sav` with the ten post-game recruits' flags 103-112
  (`EVFLAG_EXTRA_UNIT(0..9)`, Caellach to Lyon, `src/bmdifficulty.c`) set at the
  end of tick 895 on the world map, then the world map's own Save:
  `MGBA_RUN_BP=$(GBA_BP) GATE0_SRAM_IN=e_clear.sav GATE0_KEYS=port/test/inputs/monkey/pg_e_clear_rcsave.keys
  BOOST_AT=895 BOOST_STATS=0 BOOST_EDIT_FILE=port/test/inputs/monkey/pg_e_clear_rc.edit SRAM_AT=tick:1400
  SRAM_OUT=e_clear_rc.sav build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/sram_boost.lua 3000000`
  (first 32 KiB FNV-1a `49bf9dcb`; `~/fe8-chapsaves/`, never committed). The next
  post-game preparations add all ten (`PrepScreenProc_AddPostgameUnits`, a "new
  ally" popup each).

Prefixes (bot output, `ROUTE_TICK=capture`, but the hand-made `pg_e_clear_rcsave.keys`,
`pg_e_clear_shop.keys` and `pg_ib_clear_shop.keys`):

| File | Start | Ticks | What it plays |
|---|---|---|---|
| `pg_e_clear_wm.keys` | `e_clear` (44491be0) | 891 | `ROUTE_POSTGAME=1 ROUTE_STOP=wm:0`: Extras, Map, the slot, to the post-game world map's free cursor (890; the lord at node 25, monsters at nodes 15, 21, 22) |
| `pg_ib_clear_wm.keys` | `ib_clear` (05d7fd4a) | 891 | the same from Ephraim's boosted clear save |
| `pg_e_clear_t1.keys` | `e_clear` | 2,386 | `ROUTE_POSTGAME=1 ROUTE_WM_NODE=26 ROUTE_STOP=pp:36`: the walk to the Tower of Valni, its preparations (Start), floor 1's first free cursor |
| `pg_e_clear_r1.keys` | `e_clear` | 2,408 | `ROUTE_WM_NODE=27 ROUTE_STOP=pp:46`: the same to Lagdou Ruins floor 1 |
| `pg_e_clear_sk21.keys` | `e_clear` | 2,858 | `ROUTE_POSTGAME=1 ROUTE_SKIRMISH=21 ROUTE_STOP=pp:17`: the walk to node 21's monsters and the skirmish on Chapter 17's map, to its first free cursor (2,857) |
| `pg_e_clear_rcsave.keys` | `e_clear` | 1,409 | hand-made: `pg_e_clear_wm.keys`, then Up 12 ticks (off the node), A (the world map's general menu, 945), Down 4 times, A (Save, 1,076), A (the slot), A; the save is written and the world map is back at 1,313 |
| `pg_e_clear_rc_wm.keys` | `e_clear_rc` (49bf9dcb) | 817 | as `pg_e_clear_wm.keys`, from the edited start |
| `pg_e_clear_rc_t1.keys` | `e_clear_rc` | 3,709 | as `pg_e_clear_t1.keys`: the Tower's preparations add the ten recruits (1,560-3,633), floor 1's first free cursor |
| `pg_e_clear_shop.keys` | `e_clear` | 4,150 | hand-made: the first 2,058 ticks of `mk12P_e_wm_s1.keys` (the monkey opens node 23's vendor, `gProcScr_Shop` shop type 1, from the node's menu; Buy/Sell at 2,057), then by hand: Buy, the first item (700 gold; the lord's hands are full, "Send it to storage?", Yes: 2,620), Sell: the first item cannot be sold, the second is sold (50 gold, 3,494); B (the sell list), B (the "anything else" choice), A: out of the shop (3,872), back on the node's item screen |
| `pg_ib_clear_shop.keys` | `ib_clear` | 4,470 | the same from `mk12P_i_wm_s1.keys`'s first 2,058 ticks with Ephraim: bought (700 gold, to storage, 2,620); his first and second items cannot be sold, the third is sold (600 gold, 3,814); out of the shop at 4,192 |
| `pg_i_clear_wm.keys` | `i_clear` (5e8e92c8) | 891 | as `pg_e_clear_wm.keys`, from Ephraim's REAL clear save (`port-c12-iend-real`, `route_i22.keys`) |

Every run was checked with `make monkey-check NULLPROBE=0 EVERY_FRAME=1` (the plain
PC: every traced field on every tick, every frame) on `21189271` (main 9ef060ea +
port-cover11):

| File | Start, prefix | Ticks | Shows |
|---|---|---|---|
| `mk12P_e_wm_s1.keys` | `e_clear`, `pg_e_clear_wm` | 12,891 | a shop plan (node 23: its menu, the base menu, the shop screen `gProcScr_Shop` from 2,000), Lagdou Ruins floor 1 with its preparations; MONKEY CHECK: PASS |
| `mk12P_e_wm_s2.keys` | `e_clear`, `pg_e_clear_wm` | 12,891 | a world-map skirmish (Ephraim's Chapter 10 map, index 0x18, from 3,075), a Game Over (11,900), the title, the Link Arena (12,393); one P-25 frame in the preparations, 4,851 |
| `mk12P_e_wm_s3.keys` | `e_clear`, `pg_e_clear_wm` | 12,891 | the world map's unit list and Options, the Tower of Valni floor 1 with its preparations (Pick Units); PASS |
| `mk12P_i_wm_s1.keys` | `ib_clear`, `pg_ib_clear_wm` | 12,891 | as `e_wm_s1` (same seed) with Ephraim's boosted army; one P-25 frame, 5,526 |
| `mk12P_i_wm_s4.keys` | `ib_clear`, `pg_ib_clear_wm` | 12,891 | the Tower's preparations' Check Map; four P-25 frames (the subtitle help's darkener), 4,361, 4,397, 4,553, 4,803 (snapshot `~/fe8-snapshots/c12-mkP-p25/tick4355`) |
| `mk12P_i_wm_s5.keys` | `ib_clear`, `pg_ib_clear_wm` | 12,891 | a world-map skirmish (index 0x1F's map, from 2,165); `gPaletteBuffer` palette 9 colours 12-15 at 2,801, 2,817 and 4,547-4,561 after lag VBlanks inside the BIOS LZ77 (N-C11-1's kind, the lag replay's limit; no frame differs; snapshot `c12-mkP-pal/tick2795`); one P-25 frame, 4,129 |
| `mk12P_e_t1_s1.keys` | `e_clear`, `pg_e_clear_t1` | 14,386 | Tower of Valni floor 1 with the real army; PASS |
| `mk12P_e_t1_s2.keys` | `e_clear`, `pg_e_clear_t1` | 14,386 | the same, map battles and a level-up; PASS |
| `mk12P_e_r1_s1.keys` | `e_clear`, `pg_e_clear_r1` | 14,408 | Lagdou Ruins floor 1, a Game Over (4,828), the title, the Link Arena's team set-up (8,056); PASS |
| `mk12P_e_r1_s2.keys` | `e_clear`, `pg_e_clear_r1` | 14,408 | Ruins floor 1, a level-up, a Game Over (11,448); PASS |
| `mk12P_e_sk21_s1.keys` | `e_clear`, `pg_e_clear_sk21` | 14,858 | the node-21 skirmish (Chapter 17's map); PASS |
| `mk12P_e_sk21_s2.keys` | `e_clear`, `pg_e_clear_sk21` | 14,858 | the same, battles and a level-up; PASS |
| `mk12P_rc_wm_s1.keys` | `e_clear_rc` (edited), `pg_e_clear_rc_wm` | 12,817 | a shop, then Lagdou Ruins: its preparations add the ten recruits (popups from 4,547), Pick Units with them; one P-25 frame, 7,010 |
| `mk12P_rc_t1_s1.keys` | `e_clear_rc` (edited), `pg_e_clear_rc_t1` | 15,710 | the bot's prefix through the Tower's preparations with the ten recruits joining (1,560-3,633), then the monkey on floor 1 with them; PASS |
| `mk12P_rc_wm_s2.keys` | `e_clear_rc` (edited), `pg_e_clear_rc_wm` | 12,817 | the lord's node menu, a world-map skirmish (node 15, index 0x18's map) whose preparations add the ten recruits (popups from 3,604); PASS |
| `mk12P_e_shop_s1.keys` | `e_clear`, `pg_e_clear_shop` | 16,150 | after the purchase and the sale: a world-map skirmish (index 0x12's map, from 7,252), its preparations, a battle; `gPaletteBuffer` palette 9 colours 12-15 on 7,984-7,998 after lag VBlanks inside BIOS calls (LZ77, a copy: N-C11-1's kind as in `i_wm_s5`; no frame differs; snapshot `c12-mkP-pal2/tick7978`) |
| `mk12P_e_shop_s2.keys` | `e_clear`, `pg_e_clear_shop` | 16,150 | after the shop: Lagdou Ruins floor 1 (from 5,566), Pick Units, battles, a level-up; PASS |
| `mk12P_i_shop_s1.keys` | `ib_clear`, `pg_ib_clear_shop` | 16,470 | after the shop: a world-map skirmish (index 0x1F's map, from 7,572), its preparations; the same palette kind on 8,306-8,320 (the very values of `e_shop_s1`'s); two P-25 frames, 9,276 and 9,306 |
| `mk12P_i_shop_s2.keys` | `ib_clear`, `pg_ib_clear_shop` | 16,470 | after the shop: Lagdou Ruins floor 1 (from 5,886), Pick Units, a battle; PASS |
| `mk12P_ir_wm_s6.keys` | `i_clear` (REAL), `pg_i_clear_wm` | 12,891 | Ephraim's real clear save: the Tower of Valni floor 1 (from 1,899) with his real army, a battle; PASS |
| `mk12P_ir_wm_s7.keys` | `i_clear` (REAL), `pg_i_clear_wm` | 12,891 | the node's base menu and its supply screen (2,497), the Tower of Valni floor 1 (from 4,731), Pick Units, a map battle; PASS |

21 runs, 297,016 ticks. The replay check (`MONKEY REPLAY`) and `SAME RUN` hold on
all 21; every traced field is equal on every tick in 18 (`mk12P_i_wm_s5`,
`mk12P_e_shop_s1`, `mk12P_i_shop_s1`: the palette kind above); `MONKEY CHECK: PASS`
on 14. The other differing frames, 4,382 of them, are all raster-timing frames
(P-17, the exact rule). The 10 P-25 frames are each the first frame of the subtitle help in the preparations' Check
Map (`gProcScr_SubtitleHelp`, `gProcScr_SubtitleHelpDarkener`): raster mark
`slots`, the GBA's line events have one HBlank IRQ more, at one line between 46
and 98 (P-25's documented case: the request latched while IE's HBlank bit was off,
taken when `SubtitleHelpDarkener_Init` sets it), so the redraw cannot label them;
the heuristics call them `busy menu tick`. Every traced field is equal there.

## Sound (phase 3: P3.1, P3.3, P3.4a)

port-plan/audio-design.md sections 5 and 6. `make -f port/test/Makefile snd
SND=title` (or `SND=map`) runs the three steps below. Everything is written to
`build/gate0/snd/` (ROM-derived, never committed).

- **GBA records:** `oracle/sound_capture.lua`, one 8,236-byte record per frame
  callback (line 160, before the VBlank IRQ): the track arrays, the
  0x03005410-0x0300679F block (`gSoundInfo` with both PCM lanes, the players,
  `gMPlayJumpTable`, `gCgbChans`, `gMPlayMemAccArea`), `SOUND_INFO_PTR`, the
  sound registers and DMA1CNT/DMA2CNT. The header carries the logic tick count,
  so GBA lag VBlanks show. Keys come from `GATE0_KEYS`, one mask per tick: the
  mask for tick t+1 is set at the frame callback that ends tick t
  (`main_loop_ended` set there). `gate0_capture.lua` sets it instead at the
  `VBlankIntrWait` breakpoint where it samples tick t, as the PC hook does. The
  two rules differ only if a VBlank falls between `OnMain` setting
  `main_loop_ended` and its `VBlankIntrWait` call, so the capture also counts
  ticks at that breakpoint and prints the frames where the counts differ
  ("key rule"; title and map, 2026-10-19: none).
- **GBA sound calls** (`X.gba.calls`, same run): debugger breakpoints with
  returns (`:ret`, above) at the 33 m4a entry points in
  `audio/snd_calls.py` (`WATCHED`: the VBlank handler's `m4aSoundVSync` and
  `m4aSoundMain`, every m4a function the game calls, and the rest of the public
  API). A call is top-level when its caller (LR) is outside the driver code
  (`m4a_1.o` and `m4a.o` `.text` from the map, and the IWRAM copy of
  `SoundMainRAM`); the driver's own nested calls (`MPlayStart` from
  `m4aSongNumStart` ...) are only counted. Each top-level call is written with
  its arguments (r0-r3 at entry), the windows of its entry and return (window
  w: between record w and record w+1) and whether it is clean: no record was
  taken and no other context entered the driver (the VBlank handler, a VSync
  proc) while it was open. Nothing outside the driver writes the driver's
  memory directly (grep over `src/`, 2026-10-19).
- **PC side:** the Gate 0 build with the same key file, plus
  `FE8PC_SND_RECORD` (the same records, from `port/audio/fifo_pc.c`),
  `FE8PC_AUDIO_DUMP` (`.fifo8`: the 224 bytes FIFO A and FIFO B played per
  VBlank; `.lr16`: the s16 output) and `FE8PC_AUDIO_LOG` (song starts).
- **Compares:**
  - `tools/sndtool.py selfcheck`: the PC `.fifo8` equals the slot rule applied
    to the PC's own records.
  - `audio/m4a_replay.py` (P3.3 compare 3, the pass/fail, and P3.4a): every
    GBA record S(v) is loaded into the C driver (`m4a_harness.c`), which makes
    the clean top-level calls of window v from `X.gba.calls`, in the GBA's
    order and with its arguments (the handler's `m4aSoundVSync` and
    `m4aSoundMain`, and the game's calls, also those made inside the handler
    by VSync procs). The result must equal S(v+1) in every compared byte:
    tracks, the whole sound block, the readable sound register bits and
    DMAxCNT_H (destination bits left out: mGBA stores "fixed" for FIFO DMA;
    nothing reads them). A call that is not clean is not run (the GBA ran part
    of it on each side of a record or of the handler); on the VBlanks it spans
    only what it can touch is left out: its player's block and tracks, the
    channels on those tracks (`gCgbChans` and the registers when one is a PSG
    channel), the SoundInfo header for a song start with reverb set, and
    everything for a call without a player (`m4aSoundMode` ...). Round 2
    review: the records taken before the boot `m4aSoundInit` returns are
    skipped, not excused (the first pair replayed starts at the first record
    after its return); PASS also needs every PCM lane equal on every
    replayed VBlank, excused ones included; a call still open when the
    capture ended (`W_OUT` -1) fails the run unless it entered in the last
    window (then it spans only that one, like any call interrupted by one
    record). Integration review (2026-10-20): the same bound for every call
    that is not clean: one open across more than one record (`W_OUT - W_IN`
    above 1, `MAX_OPEN_RECORDS` in `m4a_replay.py`) excuses nothing and fails
    the run, since a real m4a call returns within a frame (the boot
    `m4aSoundInit`, open across two, is skipped). A top-level call
    the harness cannot make fails the run. `--show-excused` prints what each
    excused VBlank differs in; `--legacy` runs the rule before 2026-10-19 for
    comparison (handler only; any VBlank whose players or tracks differed was
    taken as a main-thread call, so a driver bug that changes only player or
    track state always passed).
  - `audio/mutate_replay.py` (`make -f port/test/Makefile snd-mutate`): checks
    the replay itself. Two altered copies of the driver change only player or
    track state, never the PCM lanes: `MPlayMain` counting `clock` by 2
    (handler path) and `m4aMPlayImmInit` setting `lfoSpeed` 23 (game call
    path). The unaltered driver must pass, both mutants must fail with every
    PCM lane equal. The legacy rule is run too and passes both (title,
    2026-10-19: clock mutant 1,894 VBlanks differ under the current rule;
    the legacy rule saw 1,899 differ and excused them all).
  - `tools/sndtool.py fifo` (P3.3 compare 1): the FIFO byte stream per VBlank,
    GBA (slot 7 - c of record v+1) against the PC `.fifo8`, aligned at a song
    start. It reports the equal run and the GBA lag VBlank that explains the
    first difference (after a lag the game's next sound call lands later on the
    GBA; audio-design 5.4).
  - `tools/sndtool.py psg`: P3.4a per VBlank (`gCgbChans` and the readable
    sound register bits, wave RAM left out), aligned at a song start. Only
    meaningful while the VBlank count since boot agrees modulo 15 (`c15`).
  - `tools/sndtool.py p31 PREFIX --vblanks N --snd`: P3.1 (sizes, constants,
    counter phase) for a plain `FE8PC_FRAMES=N` run.
  - `tools/sndtool.py wav`: the `.lr16` as a 13,379 Hz WAV, samples unchanged.

Result on 2026-10-19 (`port-integrate`, round 2 review rule: boot records
skipped until `m4aSoundInit` returns, every PCM lane required equal; same
captures as `port-sndcheck`'s numbers in brackets):

| | title | map |
|---|---|---|
| records skipped (taken before the boot `m4aSoundInit`, windows 11-13, returned) | 14 (12) | 14 (12) |
| VBlanks replayed | 6,485 (6,487) | 16,285 (16,287) |
| fully equal | 6,485 | 16,283 |
| equal outside a not-clean call's scope | 0 (2: boot `m4aSoundInit`) | 2 (4): `m4aSongNumStart(0x7A)` interrupted, only player 3's `ident` differs |
| differ | 0 | 0 |
| PCM lanes equal (required) | 6,485 of 6,485 | 16,285 of 16,285 |
| game calls replayed | 13 (`m4aMPlayFadeOut` 8, `m4aSongNumStart` 2, `m4aMPlayImmInit` 2, `m4aSoundMode` 1; the other boot `m4aSoundMode` is in the skipped windows) | 1,650 (`m4aSongNumStart` 1,434, `m4aMPlayVolumeControl` 134, `m4aMPlayFadeOut` 43, `m4aMPlayImmInit` 22, `m4aMPlayPanpotControl` 14, `m4aSoundMode` 2, `m4aSongNumStop` 1) |
| VBlanks with a game call, fully equal | 6 of 6 | 553 of 555 |
| calls still open at capture end | 0 | 0 |
| RESULT | PASS | PASS |

Negative checks (mutated copies, not committed): the map record's
interrupted `m4aSongNumStart` at window 9,961 made "still open at capture
end" fails (the `port-sndcheck` rule passed it, its scope reaching to the
end); a title `m4aSoundMode` marked not clean across windows 4,161-4,162,
with one PCM lane byte of record 4,162 changed, fails on the PCM lanes (the
`port-sndcheck` rule excused both VBlanks, no player: everything). `make
snd-mutate`: both mutants still fail on title and map with every PCM lane
equal; the baseline passes.

PCM lanes, `gCgbChans` and the registers are equal on every VBlank of both.
The old rule reported 6,481 / 15,732 fully equal and 6 / 555 "after a
main-thread call": those VBlanks now match byte for byte with the game's calls
replayed. In the map run `Talk_OnIdle` starts the text sound 0x7A once per
glyph, up to 29 times between two records when the talk draws at instant
speed (56 such windows, 32 of them lag frames), so 3 of its 1,382 calls are
open across a record. The records themselves are the same as before the
breakpoints (title and map: every byte the compares read is equal; only the
open-bus bytes 0x0400008C-8F and 0x040000A0-AF, which no compare reads, change
with the CPU's last fetch).

## Sound on any route (bot routes, monkey files, chapter saves, save states)

In plain words: the snd rows above check sound on two fixed runs from
power-on. `snd-route` checks it on any key file. The GBA plays the route in
mGBA and writes down the sound driver's whole state at every frame; the PC
plays the same route frame for frame (the GBA's slow frames included) and
writes down its own; then the two are compared frame by frame, byte for byte,
and the snd rows' own check runs on the GBA's frames as well. Long runs are
compared in pieces, so memory does not grow with the route. On route ch1 (all
21,273 ticks), a monkey file with battles from `ch2_start.sav`, route ch1
resumed from a save state at tick 10,000, and routes ch0, ch2, ch5x and ch5
(135,505 ticks), every PC sound frame equals the GBA's. To get there the
PC's replay of the GBA's slow frames had to learn two things: to make sound
in those frames too, and to place them among the game's sound calls exactly
where the GBA had them, even inside one.

```
make -f port/test/Makefile snd-route ROUTE_KEYS=port/test/inputs/route_ch1.keys
GATE0_SRAM_IN=~/fe8-chapsaves/ch2_start.sav make -f port/test/Makefile snd-route \
    MONKEY_KEYS=port/test/inputs/monkey/ch2_start_s2.keys
make -f port/test/Makefile snapshot KEYS=port/test/inputs/route_ch1.keys AT=10000 SNAP_SND=1 SNAP_PARTS="gba pclag"
make -f port/test/Makefile snd-route ROUTE_KEYS=port/test/inputs/route_ch1.keys \
    GATE0_STATE_IN=build/gate0-snap/route_ch1/tick10000
```

Settings: `ROUTE_TICKS` (default: the key file's length), `GATE0_SRAM_IN` (a
key file whose header names its start save by FNV-1a needs that save; checked;
a menu monkey file that starts at power-on, "start power-on (SRAM FNV-1a
4f731dc5)", the blank 0xFF SRAM, needs none: H-sndmk-1, fixed 2026-10-22),
`GATE0_STATE_IN` (a snapshot made with `SNAP_SND=1` and a `pclag` part; others
are refused), `SND_CHUNK` (records per chunk, 4096), `SND_JOBS` (worker
processes, 4), `SND_GATE0=1` (also the route's Gate 0 compare of the lag
replay, with raw dumps and the raster log; reported, not part of the verdict;
it takes `COMPARE_LOCK` from 60,000 ticks), `SND_KEEP=1` (keep the record
files, which are otherwise deleted after a PASS), `SND_ROUTE_DIR` (default
`build/gate0/snd-route/<key file name>[-tick<T>]`, ROM-derived, never
committed). The last line is `SND ROUTE: PASS`, `FAIL` or `INCOMPLETE`.
`make -f port/test/Makefile chain-snd-route CHAIN=NAME` runs it on a route
from its chapter save (made as `chain-route` makes it); check-all's
`snd-route-<route>` rows (`CHECK_LONG=1`, while `CHECK_SND_LONG=1`, the
default) run it on the story routes ch5 (power-on; ch0-ch4 are its prefixes),
ch5x, ch6, ch7, ch8 and e9-e13 (`CHECK_SND_ROUTES`).

**The GBA side.** One capture, `oracle/gate0_capture.lua`: the route's own
trace (its keys, ticks, lag positions; the same trace `route` makes) and, with
`SND_OUT`, `SND_CALLS` and `SND_INDEX`, the sound:
- one 8,236-byte record per frame callback, `sound_capture.lua`'s record
  (line 160, before the VBlank IRQ; record v at callback v from power-on),
  except that bytes 6-7 hold the OnMain entries so far, as the PC record does;
- the m4a calls between the records, by `sound_capture.lua`'s rules (the 33
  entry points with returns, top-level by LR, windows, "clean"); the
  breakpoints come from `audio/snd_calls.py setup-gate0`: `GBA_BP`, the m4a
  ones without `snd_wait`, and `sndpos_trackstop` (TrackStop's entry), 47 of
  the runner's 64, no two at one address;
- the index: for each tick, the record of the VBlank it waits for;
- sound positions: the header gets `"lag_snd":1` and every tick with `lag_at`
  gets `lag_snd`, per lag VBlank the main loop's top-level m4a calls (no IRQ
  frame) since the event of its `lag_at` entry, or since the sample (a call
  whose entry the IRQ preempted, before its first instruction, is not counted;
  a preempted event takes the count before it). A lag VBlank that came inside
  such a call (entered, not preempted) also gets an entry in
  `lag_snd_split`, `[i, stops, locks]`: the TrackStop calls that call had made
  (an IRQ at TrackStop's entry comes before that one) and what the handler
  found locked (bit k: player k of `gMPlayTable` with an ident that is not
  `ID_NUMBER`; bits 16-23: `gSoundInfo.ident - ID_NUMBER`), read at
  `OnVBlank`'s entry. Those are what the VBlank handler's work depends on: a
  locked player is not played, nor the players after it in `MPlayMain`'s chain;
  a locked SoundInfo stops `SoundMain`; and inside an m4a API call only
  TrackStop changes the channels the mixer plays. A lag VBlank whose IRQ came
  inside an IRQ handler's top-level m4a call (an `OnVBlank` that ran into the
  next frame, its `m4aSoundMain` still mixing: `IrqMain` runs every handler
  with IRQs on) gets `lag_snd_nested`, `[i, locks]` (2026-10-22, route_e15
  tick 1,301): its own `m4aSoundMain` finds SoundMain locked and mixes
  nothing, while its `m4aSoundVSync` runs.
Checked against `sound_capture.lua` on `title_start.keys` (6,450 ticks): the
6,500 records both have are byte-identical apart from bytes 6-7, and the
12,987 calls both have are the same lines. The m4a breakpoints change nothing
else: on route ch1 the sound capture's trace (without `lag_snd`), `gba.raw`
and raster log are byte-identical to a plain `gba-trace`, and a plain
`gba-trace` of this branch is byte-identical to `port-integrate7`'s.

**The PC side.** The lag replay of that trace (`GATE0_LAG_FROM`), with
`FE8PC_SND_RECORD`, `FE8PC_AUDIO_DUMP` and `GATE0_SND_INDEX` (the hook's
index: per tick the next record after the tick's lag VBlanks). These harness
changes make it run the GBA's sound frame for frame (D4: the frame clock is
master):
- each lag VBlank makes its sound frame (`port/hw/frame.c`
  `PortLagFrameLines`, Gate 0 binary only): the sound record just before its
  `OnVBlank`, the FIFO model, PSG and DAC after it, as `Line(160)` of a frame
  step does. Before, a lag frame ran the driver but played nothing and wrote
  no record, so the PC's sound had one frame less per GBA lag VBlank;
- sound positions: the 33 m4a entry points are wrapped (`--wrap`, `PC_WRAPS`;
  `pc/gate0_hook.c` `SndCallEnter`): a call made while none of them is open,
  outside every IRQ handler and `OnVBlank`, is the main loop's top-level call,
  as the GBA's LR rule says. With a trace that has `lag_snd` the replay runs a
  lag VBlank at the main loop's next such call once that many calls were made
  since its event, not only before the next event, and writes `lag_snd` with
  the count each ran at; `gate0_compare.py` checks it with `lag_at` (a
  mismatch takes the replay off the GBA's sequence). An m4a call is no
  main-loop event, so `lag_at` alone had run such a VBlank after a call that
  came after it on the GBA: route ch1's first run without them had 7,427 PC
  records differ in four runs, each starting at a tick with lag VBlanks and an
  `m4aSongNumStart` after them on the GBA and before them on the PC (ticks 754,
  5,008 and 20,342: sound 0x60 after the tick's three, two and two lag
  VBlanks; 6,810: song 9 after three of its four, so the PC's song ran three
  frames ahead, and the records differed for 2,923 frames). With them: 0;
- inside a call (`lag_snd_split`): `m4a_1_pc.o` is compiled with
  `-finstrument-functions` in this build too (`pc/gate0.mk`; a `--wrap` of
  TrackStop would change the pointer `gMPlayJumpTable` holds), and the hook
  acts on TrackStop's entry for the main loop's open call only. The replay
  runs such a VBlank at the first place in that call where it finds what the
  GBA's handler found: before the call (no lock taken, no track stopped: the
  m4a API changes nothing before its lock), at the entry of TrackStop number
  stops + 1 with the same locks, or at the call's return with the GBA's locks
  set for that VBlank and put back after it (what is left of the call at the
  GBA's place writes only its locked player, which the handler does not play).
  Its line gets `lag_snd_split` with the place it ran at, which the compare
  checks with `lag_at`. Route ch5 tick 102,053 is one: the lag VBlank came
  inside `m4aSongNumStart(0x5A)` after two TrackStop calls with player 7
  locked (`[0,2,128]`), so on the GBA the music player missed one step. Run
  after the call, as with `lag_snd` alone, the PC's music was one step ahead
  and 958 records differed (to v 108,165, the next song start); run at
  TrackStop number 3: 0. The call is closed (`sSndMainOpen` cleared) before
  a VBlank placed at its return runs: that VBlank's own `m4aSoundVSync` and
  `m4aSoundMain` are no main-loop calls (H-story-1, fixed 2026-10-22: they had
  found the call still open and run the same VBlank again inside themselves
  until the tick's 256 slots were full, route_ch6 tick 31,576 and route_ch8
  20,426);
- nested in an IRQ handler's call (`lag_snd_nested`): the replay cannot run
  it inside the PC's `m4aSoundMain`, so it runs it at its `lag_at` place with
  the GBA's locks set for its whole lag frame (`SndForceLocks`, shared with
  the return place above) and put back after it, and writes `lag_snd_nested`
  on its line, which the compare checks with `lag_at`. GBA-exact because the
  outer call had passed every read of what the nested VBlank changes: the
  GBA probe at route_e15 tick 1,301 found the outer frame interrupted at
  0x03002FB8, inside the IWRAM copy of `SoundMainRAM` (the mixing loop, after
  `SoundMain` read `pcmDmaCounter`), and `gMaxLines` is 0 (no mixing
  deadline). Before (2026-10-22's sweep): the PC mixed one frame and stepped
  the music once more there, and 164,462 records of route_e15 differed.
None of them changes a traced field. On route ch1 this branch's lag replay,
with a plain trace or with sound positions, equals `port-integrate7`'s plain
lag replay on every tick line (apart from `lag_snd`; `gMainCallback` by
name) and in the raw dump. On a resumed lag replay the count of m4a calls
since the last event, the open call's TrackStop count and the split places of
the snapshot tick's own runs start empty (the hook keeps no new state global,
so older snapshots still relink): only those fields of the snapshot tick's own
end-of-tick lag VBlanks could read 0 or be missing where the full run had
them.

**The compare** (`audio/snd_route.py`, after both runs; chunks of
`SND_CHUNK` records in `SND_JOBS` processes, each with its own C driver):
1. the replay, the snd rows' pass/fail: `m4a_replay.py`'s own `CallPlan`
   (the call record classified once for the whole capture), `replay_range`
   (VBlank by VBlank) and `report`, so a chunk is judged exactly as one run
   judges it. A resumed capture has no boot `m4aSoundInit`: nothing is
   skipped, and its first record must show the driver set up and idle
   (`SOUND_INFO_PTR` set, `gSoundInfo.ident` `ID_NUMBER`);
2. the PC self-check, as the snd rows;
3. the PC against the GBA, record by record: D = GBA record - PC record must
   be one constant on every tick of both indexes (each tick spans the same
   VBlanks on both sides), the runs must end at the same VBlank, and every PC
   record must have its GBA record. Each must equal it in every byte the replay
   compares (tracks, the sound block with code pointers translated by name,
   the readable sound register bits, DMAxCNT_H without the destination bits),
   in `SOUND_INFO_PTR` and in the header's `gGameClock`. Records taken before
   the GBA's boot `m4aSoundInit` returned are skipped, as in the replay (the
   PC's init waits for no VBlank). The only excuse is the replay's: on a
   record that a GBA call that was not clean is open across, what that call
   can touch. Every PCM lane must be equal on every record, but on a record
   a not-clean `m4aSoundMain` is open across (the mixer caught half-way,
   below: integration 11), counted apart. The header's
   OnMain count is reported, not compared (no sound state: a lag VBlank that
   came on the GBA before the main callback's entry, because `OnVBlank` ran
   into the next frame, runs on the PC at the tick's first event, inside
   `OnMain`; route ch1 tick 5,696, one record);
4. the PC run complete: every tick in its trace, no `[fault]` in its log.

Result on 2026-10-21 (`port-snd10`, base `port-integrate7`):

| | route ch1, power-on | route ch1, resumed at tick 10,000 | monkey `ch2_start_s2`, `ch2_start.sav` |
|---|---|---|---|
| ticks | 21,273 | 11,273 (10,000-21,272) | 8,000 (913 in the battle main loops: `MainUpdate_8055C68` 214, `InBattleMainRoutine` 699) |
| GBA capture / PC lag replay | 30 s / 23 s (compare 5 s) | 16 s / 13 s | 10 s / 9 s |
| GBA records | 22,314 (49,550 top-level m4a calls, the boot `m4aSoundInit` the only one not clean) | 11,753 from v 10,561 (26,332 calls, all clean) | 8,348 (17,747 calls) |
| frame clock D | 13 on all 21,273 ticks | 13 on all 11,273 ticks | 3 on all 8,000 ticks |
| replay | 22,299 VBlanks, all fully equal (14 boot records skipped); 1,195 with a game sound call | 11,752, all fully equal | 8,343, all fully equal (4 skipped) |
| PC self-check | 22,300 VBlanks, 0 differ | 11,751, 0 differ | 8,344, 0 differ |
| PC against GBA | 22,300 records fully equal (1 boot record skipped); OnMain count differs on 1 (reported) | 11,752 fully equal | 8,344 fully equal (1 skipped) |
| PCM lanes equal | every record | every record | every record |
| lag VBlanks run at the GBA's place | 1,028 of 1,028 | 1,028 of 1,028 (the whole run's count) | 345 of 345 |
| Gate 0 compare of the lag replay (`SND_GATE0=1`) | MATCH, ticks 0-21,272 | MATCH, ticks 9,999-21,272 | MATCH, ticks 0-7,999 |
| SND ROUTE | PASS | PASS | PASS |

More runs, same day, without `SND_GATE0`:

| Run | Ticks | GBA / PC | PC records equal to the GBA's | Replay | Notes | SND ROUTE |
|---|---|---|---|---|---|---|
| route ch0 | 5,803 | 7 s / 6 s | 6,150 of 6,150 | 6,149 equal | D 13; 348 lag VBlanks | PASS |
| route ch2 | 38,391 | 55 s / 48 s | 40,311 of 40,311 | 40,310 equal | D 13; 1,921 lag VBlanks, one an early flush | PASS |
| route ch5x, `ch5x_start.sav` | 43,540 | 64 s / 50 s | 46,193 of 46,193 | 46,192 equal | D 3; 2,654 lag VBlanks | PASS |
| route ch5 | 135,505 | 166 s / 147 s | 142,326 of 142,327 equal, 1 excused | 142,324 equal, 2 excused | D 13; 6,823 lag VBlanks; one inside `m4aSongNumStart(0x5A)` (tick 102,053, `[0,2,128]`), its record excused for that call | PASS |
| monkey `mkB_ch5_start_s6_t16000`, `ch5_start.sav` | 18,000 | 22 s / 12 s | 10,956 of the 10,956 the PC wrote | 19,020 equal | the PC faults at tick 10,360 (SIGSEGV at address 0x3D, the address of M3 in "Menu monkey"; not looked into here) | INCOMPLETE |

Each side's records of route ch5 were 1.1 GB; the compare read them in 35
chunks of 4,096 records per side, in 4 workers.

The resumed run's GBA records, PC records and calls are byte-identical to the
full run's over the same VBlanks (its first GBA record is the photo's own
callback, the full run's v 10,561; its first PC record is the first VBlank
after the PC photo, at tick 10,000's sample). Chunks: the full route ch1
compare in one chunk, in chunks of 4,096, 777 and 64 records (1 to 6 workers)
prints the same lines apart from the chunk count, and its replay summary is the
one `m4a_replay.py` prints in one run over the same records.

**Checks of the check** (`make -f port/test/Makefile snd-route-mutate
SND_ROUTE_DIR=build/gate0/snd-route/route_ch1`, `audio/mutate_snd_route.py`,
on a run kept with `SND_KEEP=1`; altered copies, the run itself is never
changed): the baseline passes; one PCM lane byte of a PC record fails (PC
against GBA, PCM lanes equal on 22,299 of 22,300); one track byte of a PC
record fails with every PCM lane equal; one PC VBlank left out fails on the
frame clock (D 13, then 14 from tick 10,586); one track byte of a GBA record
fails the replay; one clean `m4aSongNumStart` left out of the calls fails the
replay; the same call marked not clean, open across the record that starts a
chunk, passes with one excused VBlank and the same output in one chunk as with
that chunk edge. Result on 2026-10-21 (route ch1): all seven as expected.

**Round 10** (2026-10-22, `port-r10-sound` on `port-integrate10` `fd46f35e`;
`port-plan/pending/folded/r10-sound.md`): the plain PC (no NULL probe) on all 39
route files and 15 monkey files, each to where the PC stops today (the
coverage-10 groups, being fixed on their own branches; a hang or a split
from the GBA's path is cut off before its tick). Fixed on the way: H-story-1
(above), H-story-2 ("Early flush VBlank", at the call itself: ch7, i9, i12,
e16 and i20 one each, e15 two; each had failed check 3), H-sndmk-1 and the
nested lag VBlank (above). Every PC sound record of every run is equal to
the GBA's, the existing excuse aside, except two places the replay cannot
reproduce (Limits, below):

| Runs | Result |
|---|---|
| route ch5 (power-on; covers ch0-ch4), ch5x, ch6, ch7, ch8, ch8i, e9-e13, e16, e17, b20, bpost, i9-i13, i15-i17, i20 | PASS, the whole key file (24 runs, 2,397,116 ticks): 2,524,490 PC records compared, all fully equal but 5 excused (the replay's not-clean calls); 127,398 lag VBlanks, all at the GBA's place |
| route b21 to 31,794, b22 to 3,134, i14 to 76,094 | PASS (cut before the Crimson Eye stub at 31,795, the Demon King's summon at 3,135, the Shine stub at 76,095: C10-1, C10-6) except b21's 20 records at GBA v 20,585 (Limits) |
| route e14, e18, e19, bend, i18, i19 | the PC stops (C10-9 a at 52,297; C10-3 at 6,499 and 6,395; C10-5 at 54,324 and 68,694; C10-8 at 19,063); every record before it equal |
| monkey `mkA_prologue_s1` (power-on), `mkD_e14_start_s4`, `mk10A_dj_e13sk_s3`, `mk10A_route_ch7_tick1639_s5`, `mk10A_route_ch8_tick1587_s1`, `_s5` | PASS, whole |
| monkey `mk10B_dj_e18sk_s1` to 4,777, `mk10C_dj_e21x_end` to 2,439, `mk10C_dj_e21x_s1` to 3,613 | PASS (cut before C10-1, C10-6) |
| monkey `mk10A_route_ch6_tick1668_s6`, `mk10A_route_ch8_tick1587_s4`, `_s8`, `mk10B_dj_e18sk_s3`, `mk10C_dj_e21x_dl`, `_s2` | the PC stops (C10-9 b at 21,143, 17,173, 12,755; C10-3 at 2,639; C10-7 at 2,960, 3,007); every record before it equal |
| route e15 (whole) | 165,829 of 165,830 PC records fully equal, 1 excused (with `lag_snd_nested`; 1,367 before), but the PCM lanes of GBA record 1,371 (Limits): FAIL by the rule |
| all 54 runs | 3,210,873 PC records compared: 3,210,845 fully equal, 8 excused, 20 differ (b21); and e15's PCM lanes on one record |

**Coverage 12** (2026-10-23, `port-c12-sound` on `21189271`, integration 11
`9ef060ea` + `port-cover11`; `port-plan/pending/c12-sound.md`): the plain PC
(no NULL probe) on every route file that is not a `CHECK_SND_ROUTES` row (59)
and on all 31 monkey files, each whole key file, from its chapter save
(ch0-ch4 and `mkA_prologue_s1` from power-on). No PC run stops any more, none
has a fault, and every lag VBlank ran at the GBA's place. Four runs fail by
the rule, each on a place the replay cannot reproduce (Limits, below); none
is a difference in the PC's game or driver code:

| Runs | Result |
|---|---|
| routes ch0-ch4, ch8i, e14-e22, eend, i9-i13, i15-i21, ib22, ibend, ibpost, b20, b22, bpost, bend; Tower bt5-bt8; Ruins br3-br9; skirmishes sk4, sk10, sk15, sk17; features f_arena_ch5, f_chest_ch8, f_promo_ch6, f_support_e12, f_suspend_ch6 | PASS, whole (55 runs, 4,345,449 ticks): 4,572,019 PC records compared, all fully equal but 11 excused (the replay's not-clean calls: e15's `m4aSoundMain`, the mixer caught half-way; the other ten a call's own player); 226,625 lag VBlanks |
| all 31 monkey files | PASS, whole (395,273 ticks): 416,645 PC records compared, all fully equal; 21,403 lag VBlanks |
| route bt1 (`b_clear`, Tower floors 1-4), br2 (`br2r_start`, Ruins floor 2) | one record each differs, in `gCgbChans +9` (GBA v 79,315 at tick 75,802; GBA v 799 at tick 750): a lag VBlank inside a TrackStop (Limits) |
| route i14 (`i14_start`) | check 1 fails on one VBlank (GBA v 83,460, tick 79,159): a lock taken after the record (Limits); check 3: every PC record equal |
| route b21 (`b21_start`, whole: 170,079 ticks) | the same 20 records as round 10 (GBA v 20,585-20,604, tick 19,566: a lag VBlank inside `m4aMPlayImmInit`, Limits), nothing else |
| all 90 runs | 5,274,964 ticks; 5,548,616 PC records compared: 5,548,579 fully equal, 15 excused, 22 differ (b21 20, bt1 1, br2 1); 273,742 lag VBlanks, all at the GBA's place |

Snapshots a few ticks before each (`SNAP_SND=1`, parts `gba pclag`; a
resumed `snd-route` shows the same records): `~/fe8-snapshots/c12-snd-bt1/tick75790`,
`c12-snd-br2/tick740`, `c12-snd-i14/tick79150`, `c12-snd-b21/tick19560`.

**Limits.**
- A record taken while the GBA's mixer runs cannot be reproduced. On route
  e15 (tick 1,301, above) the GBA's record 1,371 was taken at line 160 inside
  `OnVBlank`'s `m4aSoundMain`, half-way through its mix: its PCM lanes hold a
  half-mixed buffer. The replay excuses what a not-clean `m4aSoundMain` can
  touch (everything but the lanes: it has no player). Since integration 11
  (round 10's sound review, decision 2, taken as recommended) the PCM lanes
  are excused too, only there: on the VBlanks a not-clean `m4aSoundMain`
  spans (the replay's pair that ends at that record and the one that starts
  at it) and, in check 3, on the record it is open across
  (`m4a_replay.MIXER`); both tools count and print them apart ("PCM lanes
  excused on N"). No other call excuses a lane, and nothing else on those
  VBlanks is excused beyond the replay's rule. Before it, e15 failed on its
  2 VBlanks and check 3 on that record's lanes.
- A lag VBlank inside a call that writes without a lock is placed before the
  call. The placement rule above runs a VBlank that came inside a main-loop
  call with no lock taken and no track stopped before that call, as "the m4a
  API changes nothing before its lock". `m4aMPlayImmInit` takes no lock: it
  clears and re-initialises each started track as it goes. Route b21 tick
  19,566: `m4aSongNumStart(0xA0)`, then `m4aMPlayImmInit(0x030066D0)`, whose
  VBlank (`lag_snd_split [5,0,0]`) came at 0x080D0414 (the GBA probe: right
  after that track's `flags = 0x80`, before its `bendRange`, `volX`,
  `lfoSpeed` and `tone.type` stores; r4 0x03002478, r5 4 tracks left); the
  PC ran it before the call, so its handler saw that player's tracks not yet
  re-initialised, and 20 PC records differ (GBA v 20,585-20,604: one PCM
  channel's envelope volumes, two track fields, PCM lanes), then all are
  equal again. Not fixed: an exact replay splits the call by track and by
  store (as `LagSplitCall` splits a BIOS copy).
- A lag VBlank inside a TrackStop call, before that TrackStop has stopped
  its channel, is placed after it (coverage 12, 2026-10-23). The capture
  counts a TrackStop at its entry, and the replay runs a VBlank with
  `stops` = k at the entry of TrackStop number k + 1, so after TrackStop k
  has done all its work. On the GBA the VBlank can come inside TrackStop k
  itself. Route bt1 tick 75,802 and route br2 tick 750 (both
  `lag_snd_split [0,1,4]`, inside `m4aSongNumStart(0x69)`, the "unit
  selected" sound of `PlayerPhase_InitUnitMovementSelect`, MPlayStart's
  first TrackStop, player 2 locked): the GBA probe found the main loop at
  `0x080CFDF4` (bt1: TrackStop's channel loop, after the CGB type test,
  before `CgbOscOff`) and `0x080D0C92` (br2: inside `CgbOscOff`, before its
  first store), r4 `0x03006510` (`gCgbChans[0]`, the track's only channel,
  `statusFlags` 2). The handler's `CgbSound` stepped that channel's envelope
  once more on the GBA (`envelopeVolume` 2 to 1) before TrackStop cleared
  it; on the PC the channel was already off. One record differs, in that
  one byte (`gCgbChans +9`; the PCM lanes and readable registers equal),
  and the next note on the channel sets it again. Both times TrackStop had
  written nothing yet, so the entry of TrackStop k would have been the
  GBA's place. Not fixed: the capture could count a TrackStop only once it
  has written (from the interrupted PC and r4, as the ImmInit split above
  proposes), or the hook could split TrackStop by channel.
- Check 1 (the replay of the GBA's own records) can miss a lock that a
  not-clean call took after the record (coverage 12, 2026-10-23). A GBA
  record is the state at the frame callback; the main loop runs a few more
  instructions before it takes the VBlank IRQ. Route i14 tick 79,159:
  `m4aSongNumStart(0x69)` open across GBA record v 83,460, which still
  shows player 2 (`0x03006690`) unlocked. The GBA probe: at the frame
  callback MPlayStart had just read the player's `ident` (r0 `0x68736D53`,
  before `adds r0,#1` at `0x080D0974`); at `OnVBlank` the main loop was at
  `0x080D0978`, right after the store that locks it (r0 `0x68736D54`), and
  the capture's `lag_snd_split` says `[0,0,4]`. A locked player stops
  `MPlayMain`'s chain, so on the GBA player 1 (`0x03006650`, music, after
  it in the chain) was not stepped on that VBlank. The replay ran that
  VBlank from record v 83,460 with no lock and stepped player 1, and its
  excuse covers only the call's own player: check 1 fails on that one
  VBlank (player 1's `clock` and `tempoC`, its six playing tracks' `wait`
  counters, their channels). The PC lag replay ran the VBlank with the
  GBA's locks, and check 3 found every PC record equal to the GBA's. Not
  fixed: the replay
  could run each VBlank's handler with the locks the capture read at
  `OnVBlank` (as the hook's `SndForceLocks` does), or scope the players
  after a locked one in the chain.
- The PC side is the Gate 0 lag replay, a test harness: a shipped PC run has
  no lag VBlanks. It shows the PC's own game and driver make the GBA's sound
  frame for frame on the GBA's VBlank sequence; the plain run's sound (no
  lag) is checked by the snd rows' self-check only.
- Inside an m4a call the replay matches what the VBlank handler can tell
  (the locks and the TrackStop calls), not the GBA's instruction. The record
  of that VBlank still differs in the call's half-done writes, which the
  replay's excuse covers (route ch5: one record). Two places it cannot tell
  apart: after the unlock of a call that stops no track (taken for the place
  before its lock, so the call's writes are missing on the PC), and between two
  locked parts of one call (`m4aMPlayAllStop`, which FE8 does not call). Either
  shows as a difference after that record and fails; it is never excused.
- A snapshot without sound positions is refused: its `pclag` part ran the lag
  VBlanks before the photo by events only, so its sound can already differ
  (route ch1 from tick 799 on).
- Each side's records are about 8 KiB per VBlank (route ch5, 135,505 ticks:
  about 1.1 GiB each), written once and read chunk by chunk; the compare's
  memory is a chunk per worker, so it takes no compare lock.

## NULL probe (BIOS-area and NULL-page reads, diagnostic)

The PC cannot map the NULL page, so a game read of the GBA BIOS area
(`0x00000000-0x00003FFF`) faults, where the GBA returns the BIOS open-bus
latch (`port-plan/DIVERGENCES.md`, H-3 and "Open-bus latch rule"). Each such
site needs its own guarded fix. The probe lists them all at once instead of one
fault per run.

- **Mode:** `FE8PC_NULLPROBE=1` (`port/hw/nullprobe.c`, any Linux build). A
  read below `0x02000000` (or at `0xFFFF0000` and up) no longer faults: the
  faulting instruction is run again against a shadow page full of the latch
  word, so it gets the latch bytes for its own address and width, and the run
  goes on. Writes are dropped, as on the GBA (H-1 still drops the ones it
  handles). Latch: post-IRQ `0xE55EC002` when the HBlank IRQ is on, post-SWI
  `0xE3A02004` when only VBlank (and Game Pak) IRQs are on. Elsewhere (inside
  an IRQ handler, another IRQ source on, an address at `0x4000` and up) the
  site is marked undecided and gets the post-SWI latch. A fault in a GBA
  memory mirror that the host does not map (EWRAM above `0x02040000`, IWRAM
  above `0x03008000`, palette, VRAM, OAM, ROM, SRAM) reaches the mirrored
  address, as on the GBA, and is listed too: such a pointer usually comes from
  an open-bus value. A fault in unused GBA space higher up (I/O above the
  host's 4 KiB, `0x10000000` and up where the host maps nothing) is open bus
  on the GBA and is handled like the low range (undecided). Each site (faulting
  instruction and its caller) is recorded with its addresses, first and last
  tick, hits, the latch rule of its hits and the call chain;
  `FE8PC_NULLPROBE_OUT=PATH` writes the table as JSON lines at exit, at a fatal
  fault and on SIGTERM. Unset, nothing changes: the fault handler goes on as
  before. Not a fix and never for a shipped build.
- **Integer division traps** (added 2026-10-20, `port-tas2`): a game C `/` or
  `%` is a call of libgcc's `__divsi3`, `__modsi3`, `__udivsi3` or
  `__umodsi3` on the GBA (`fireemblem8.elf` `0x080D18FC-0x080D1C0B`), which
  never traps: a zero divisor goes to `Ldiv0` (`bl __div0`, an empty
  function, then `r0 = 0`), and `INT_MIN / -1` gives `INT_MIN`, remainder 0.
  On the host it is an x86 `div`/`idiv`, which raises SIGFPE for both. In
  this mode a 32-bit `div`/`idiv r/m32` with a plain 32-bit dividend (`edx`
  the sign of `eax`, or 0) gets those results in `eax` and `edx` and the run
  goes on after it; the site is listed with how `div0` or `div-overflow`,
  rule `div`, the dividend as its address and the operands and results in its
  note. Any other division trap stays fatal. Only the quotient and remainder
  are the GBA's: code the host compiler arranged on the assumption that the
  divisor is not zero (C undefined behaviour) can still differ, and the
  compare shows it. Checked with a scratch test (not committed): `/` and `%`
  by 0 and `INT_MIN / -1`, signed and unsigned, register and memory divisors
  (`esp`- and `ecx`-based), at `-O0` and `-O2`, all with the GBA's results.
- **Routes:** `make -f port/test/Makefile nullprobe` builds the Gate 0 binary and
  runs `port/tools/nullprobe.py run` then `report`: each route of
  `inputs/README-routes.md` to its last tick, outputs in `build/nullprobe/<route>/`
  (`pc.log`, `pc.jsonl`, `sites.jsonl`, `check.json`) and the merged list in
  `build/nullprobe/report.md` (one row per call site, symbolised, with the
  existing D- or H-entry a site falls under). Routes of 60,000 ticks or more
  take the compare lock (`~/.fe8-compare.lock`). Settings: `NULLPROBE_ROUTES`, `NULLPROBE_OUT`,
  `NULLPROBE_ARGS` (options of `nullprobe.py run`).
- **Any other key file** (a TAS route, for example): run `make ... route`
  (or `pc-lag-replay`) with `FE8PC_NULLPROBE=1
  FE8PC_NULLPROBE_OUT=DIR/NAME/sites.jsonl FE8PC_NULLPROBE_ROUTE=NAME` in the
  environment, then `python3 port/tools/nullprobe.py report --out DIR
  --routes NAME ...` merges those tables as it does the bot routes'. A probe
  run cannot take a save state (`GATE0_STATE_AT`/`GATE0_STATE_OUT` refuse it:
  "snapshots of an FE8PC_NULLPROBE run are not supported"), but it can resume
  one (`GATE0_STATE_IN`; round 9's resumes from `~/fe8-snapshots/r9/` ran with
  the probe), so such a run starts from power-on or from a photo taken without
  the probe.
- **On the route or not:** the routes are timed to the tick on the GBA, and the
  PC has no lag frames, so a GBA lag effect that changes timing puts the PC off
  the route (README-routes "Limits"); every site after that is on the PC's own
  path, not the GBA's. `check.json` compares the key file's milestones with the
  PC trace, and with `--gba-trace GBA.jsonl` (a Gate 0 trace of the same or a
  longer route) the game state tick by tick. `--lag-replay` passes the GBA
  trace as `GATE0_LAG_FROM`, so the PC runs the GBA's lag VBlanks where they
  came ("Lag replay"; the trace must be in format `fe8-gate0-trace-4`; the run
  log says which replays ran).

## Bug sweeps (UBSan and uninitialised locals, diagnostic)

In plain words: two kinds of test build of the Gate 0 binary look for bugs
that a normal run cannot see, because the run looks fine. The first (UBSan)
has the compiler put a check in front of every risky operation: a shift by too
many places, a sum too big for its type, an array index past the end, a NULL
or misaligned pointer. A check that trips is written down (where, how often,
first and last tick, who called it) and the game goes on. The second is built
twice, once filling every local variable the code forgot to set with zeros and
once with a fixed pattern. Both get the same buttons; if anything they trace
ever differs, some code read a variable it never set, and what it found there
changed the game. On the GBA such a read gets whatever the GBA's stack held, on
the PC whatever the host's stack held, so it is a place where the port can
leave the GBA. None of these builds is ever shipped, and nothing outside
`port/test/` changes for them (`port-plan/pending/sweep11.md`).

**Builds** (`port/test/sweep.mk`, which includes `port/test/Makefile`; each
is that file's `pc` in its own directory, with `pc/sweep_flags.mk` read after
`pc/gate0.mk` to add the flags to the game code and the port code and to the
directory's compile settings):

| Sweep | Binary | Flags |
|---|---|---|
| `ubsan` | `build/pc-gate0-ubsan/fe8pc` | `-fsanitize=undefined,bounds -fsanitize-recover=all` (`UBSAN_CHECKS`), `-fwrapv` dropped, linked with `pc/ubsan_log.c` |
| `ubsan` with `UBSAN_WRAPV=1` | `build/pc-gate0-ubsan-wrapv/fe8pc` | the same with `-fwrapv` kept |
| `zero` | `build/pc-gate0-zero/fe8pc` | `-ftrivial-auto-var-init=zero` |
| `pattern` | `build/pc-gate0-pattern/fe8pc` | `-ftrivial-auto-var-init=pattern` (GCC's `0xFE` bytes) |

`make -f port/test/sweep.mk sweep-builds` makes all three (about 20 seconds
each at `-j4`); `sweep-pc SWEEP=NAME` one. `-fwrapv` (port/Makefile) makes
signed overflow defined, and GCC then checks none, so the default UBSan build
drops it to report the overflows the GBA wraps; `UBSAN_WRAPV=1` keeps the
Gate 0 binary's arithmetic and checks everything else. The RAM layout of every
sweep binary is the Gate 0 binary's: the same 430 variables at their GBA
addresses (`check_ram_addrs`), and UBSan's check data goes to host sections
(`.data..Lubsan_*`), never into GBA RAM.

**The UBSan runtime** (`pc/ubsan_log.c`, compiled without the checks). GCC's
libubsan is not linked: it reports each site once and counts nothing, and it
marks a reported site by writing into the check's data, which the save states
would refuse. Every check recovers and the code goes on with what the
operation gave (on x86: the wrapped sum, the masked shift; a NULL load still
faults after its report, as in every PC build; only `__builtin_unreachable`
stops the run, with a `[fault]` line, as nothing follows it). A site is one
check GCC emitted and its kind of failure. For each: the check name (`bounds`,
`null`, `alignment`, `object-size`, `shift-exponent`, `shift-base`,
`signed-integer-overflow`, `pointer-overflow`, `bool-enum`, ...),
file:line:column, the hit count, the first and last Gate 0 tick, the first
hit's message with its values (worded as libubsan words them) and its return
addresses, named `Function+offset` from the binary's own symbol table. A new
site is logged at once as an `[ubsan] site N (tick T): FILE:L:C: check:
message [in Function+off]` line in the run's log. `FE8PC_UBSAN_OUT=PATH.jsonl`
(the targets set it) writes the table at exit, and at most every 1,000 ticks
while sites are hit (a run that faults leaves with `_exit`, so its table is
the last one written; the log lines have every site), and `PATH.txt` beside
it, one line per site. `tools/ubsan_collate.py TABLE...` merges tables (one
row per check and file:line:col, each run's ticks kept), adds function and
file:line to the first hit's callers (addr2line, inlined calls included) and
sorts the rows by area (game: `src/`, `include/`; port; harness), check and
place. Every site is kept, the ones the GBA means too (a shift or an overflow
its arithmetic wraps the same way): the collate only sorts.
`make -f port/test/sweep.mk ubsan-selftest` checks the runtime against GCC's
libubsan (`lib32ubsan1`) on `pc/ubsan_selftest.c`, which fails one check of
each kind twice: the same 19 sites with the same messages and functions, every
one with 2 hits, plus the one site libubsan hides (a shift whose exponent check
it reported first; one check's data serves both kinds).

**Targets** (KEYS and TICKS as for `gba-trace`):

```
make -f port/test/sweep.mk sweep-route SWEEP=ubsan|zero|pattern|gate0 KEYS=FILE TICKS=N [SWEEP_GBA=DIR]
make -f port/test/sweep.mk ubsan-run KEYS=FILE TICKS=N [SWEEP_LAG=1 | SWEEP_GBA=DIR]
make -f port/test/sweep.mk uninit-compare KEYS=FILE TICKS=N [SWEEP_LAG=1 | SWEEP_GBA=DIR]
```

- `sweep-route`: does the sweep binary still follow the GBA? The GBA capture
  of KEYS (made once per run directory, or `SWEEP_GBA=DIR` from an earlier
  `gba-trace`), the lag replay with the sweep binary, then that run compared
  with the GBA on its own, as `route` checks the Gate 0 binary (exit 0 only on
  a MATCH of every tick with no fault). `SWEEP=gate0` runs the Gate 0 binary,
  the baseline. Each run is timed (`tools/sweep_run.sh`: wall and user seconds,
  per 10,000 ticks, peak memory, in `<sweep>.cost.json`). For ubsan the table
  is sorted too.
- `ubsan-run`: the UBSan table of one run, sorted (no GBA compare). The plain
  run, or the lag replay with `SWEEP_LAG=1` (makes the GBA capture) or
  `SWEEP_GBA=DIR`.
- `uninit-compare`: zero and pattern with the same buttons from the same start,
  both at once, then `tools/sweep_pair.py`: every field of every tick, the
  frames too, `gMainCallback` by function name (the two binaries' addresses
  differ; no other traced field holds a host address). It prints the first
  differing tick with every field that differs there (first differing byte and
  member, palette entry, tilemap cell or OAM entry, from the raw records), and
  the first differing tick of each field. Exit 0: the same on every tick and
  both runs complete; 1: DIFFERENT, or INCOMPLETE (a run stopped early); 2:
  the runs did not get the same buttons from the same start (key file, SRAM,
  save state, GBA lag source). Plain run, or lag replay as for `ubsan-run`.
  The plain run is lag-free, so a bot route leaves the GBA's path at its first
  lag effect, but both binaries still get the same buttons; the lag replay
  keeps both on the route.
- **Start:** `GATE0_SRAM_IN=SAVE.sav` (a chapter save; every binary boots from
  power-on with it) or `GATE0_STATE_IN=SNAP/tick<T>` (a save state, its pc or
  pclag part relinked for each binary by `sweep_run.sh`; best effort for a
  binary that did not take the photo, "Save states"; the GBA capture resumes
  too), as for `port/test/Makefile`; without either, power-on.
  `FE8PC_NULLPROBE=1` in the environment goes to every run, each with its own
  table (`<sweep>.sites.jsonl`), so a run goes on past a NULL-page read that
  stops the binary.
- **Output:** `build/gate0-sweep/<run>/` (`<run>` = `SWEEP_RUN`, by default the
  key file's name plus the save's or the snapshot's): the GBA capture in `gba/`,
  and per binary `<sweep>.jsonl`, `.raw`, `.log`, `.cost.json`,
  `.compare.txt`, `.ubsan.jsonl`, `.ubsan.txt`, `.ubsan-sorted.txt`, and
  `uninit.txt` and `uninit.json`. Game data: never commit it, and delete it
  when done (each binary keeps about 10 KB of raw records a tick). Compares of
  `COMPARE_LOCK_TICKS` ticks or more take `COMPARE_LOCK`.

### Result (2026-10-21, branch `port-sweep11` from `port-integrate7`)

Route ch1 (21,273 ticks from power-on, lag replay, one GBA capture for all,
`sweep-route`):

| Binary | GATE 0 (lag replay vs GBA) | Wall s / 10,000 ticks | User s / 10,000 ticks | Peak memory |
|---|---|---|---|---|
| Gate 0 (`SWEEP=gate0`, the baseline) | MATCH on ticks 0-21,272 | 9.5 | 9.4 | 28 MB |
| `zero` | MATCH on ticks 0-21,272 | 9.8 | 9.7 | 28 MB |
| `pattern` | MATCH on ticks 0-21,272 | 9.6 | 9.5 | 28 MB |
| `ubsan` | MATCH on ticks 0-21,272 | 15.4-16.3 | 15.2-16.1 | 37 MB |
| `ubsan`, `UBSAN_WRAPV=1` | MATCH on ticks 0-21,272 | 16.7 | 16.5 | 36 MB |

None of the instrumentation changed what the game did on this route: not
UBSan's checks with the stack layout and register use they change, not
dropping `-fwrapv` (every signed overflow on the route still came out as the
GBA wraps it), not the filled locals. UBSan costs about 1.7 times the Gate 0
binary; zero and pattern cost nothing measurable (the GBA capture took 28 s).
The times follow the machine's load: they were taken beside 2-4 other heavy
runs (24 cores); a last check at load average 7, zero and pattern running side
by side, took 12.2-12.5 s (zero, pattern) and 17.7 s (ubsan) per 10,000 ticks. Longer runs:
route ch5 from power-on (135,505 ticks), ubsan 15.4 s, zero 10.5 s and pattern
10.2 s per 10,000 ticks; the ubsan run MATCHes the GBA on all 135,505 ticks
too (its compare, under the lock, took 66 s).

Uninitialised locals (`uninit-compare`, lag replay): zero and pattern were
equal on every field of every tick on route ch1, on route ch5 (all 135,505
ticks: the Prologue to Chapter 5), from a save (`chapsave_ch2.keys` from
`ch2_start.sav`, 7,000 ticks) and from a save state (route ch1 resumed at
tick 12,000 from a photo the Gate 0 binary took; zero, pattern and ubsan
relinked, and zero and ubsan MATCH the GBA's resumed capture on ticks
11,999-21,272). On route ch8 from `ch8_start.sav` (87,077 ticks, with
`FE8PC_NULLPROBE=1`: this base faults at tick 9,938, round 9's heal group)
they differ first at tick 86,715, `gPlaySt` +0x39, on 362 ticks to the end,
and on nothing else: `AutolevelSecondaryLord`'s local `struct BattleUnit`
whose `expGain` is never set (round 9's ch8exp group). The pattern's `0xFE`
makes `PidStatsAddExpGained` run; zero equals the GBA there (its stack held 0),
and the Gate 0 binary's host stack gave the 362-tick `gPlaySt` difference that
cov9 called B5. So the sweep finds a known read of this kind by itself.
Negative checks of `sweep_pair.py` (scratch, not committed): a byte of
`gPlaySt`, an OAM byte (raw record and hash), `gMainCallback` set to another
function, `frame15`, a run cut short with a `[fault]` line and another key
file in a header were each reported at their tick and field (the last as
exit 2).

UBSan tables (lag replay): route ch1 16 sites, 3,914 hits; route ch5 20 sites,
19,076 hits; route ch8 29 sites, 11,065 hits; merged ch5 and ch8: 29 sites, all
in game code (16 bounds, 8 null, 1 object size, 2 pointer overflow, 2 signed
overflow), none in the port or the harness. The sorted list, with the places
round 9 already has, is in `port-plan/pending/sweep11.md`.

### Late game (coverage 12, 2026-10-23, branch `port-c12-sweeps` from `21189271`)

The three sweep binaries on the late game: 44 routes, 3,668,393 ticks, each the lag replay of one
GBA capture per route from the route's chapter save (`wf-fe8\c12sw_route4.sh`; notes
`port-plan/pending/c12-sweeps.md`). The 20 late story routes with every GBA frame; the Tower, Ruins,
skirmish, feature and post-game routes with a frame every 60 ticks (`route_f_support_e12` every
frame); ubsan with a frame every 60 ticks, zero and pattern with every frame. Compared: GBA vs ubsan
and GBA vs zero (`gate0_compare.py`, every traced field of every tick, every frame both sides have)
and zero vs pattern (`sweep_pair.py`, every field of every tick).

- **Uninitialised locals:** zero and pattern are equal on every field of every tick of all 44 routes.
- **The game:** every traced field equals the GBA's on every tick of every route but route_br9
  (`gPaletteBuffer` from 62,549: coverage 11's N-C11-1, the lag replay's limit), in all three
  builds. Frames differ only on GBA lag ticks (the known "frame on a GBA lag tick" kind; the plain
  Gate 0 binary draws the same frames there): Naglfar on route_e21 (6, from 59,346) and route_i21
  (50, from 62,399), one fade frame on route_e17 (94,125) and route_e18 (26,662).
- **UBSan:** 54 sites, 472,985 hits, all in game code. New to this sweep (not in sweep11's four
  tables): 21 sites at 12 places. Two make the PC's memory differ from the GBA's: the world-map
  monster odds before Chapter 20 read one row past `WmMonsterGenerateRates_*Mode`
  (`worldmap_timemons.c:220`): the next table on the GBA and on Linux, other bytes on Windows, where
  MinGW sorts the tables by name (C12-SW-2); and the event registry overflow (`eventinfo.c:1813-1815`,
  11 or more events in one phase change, Ruins floors 7 and 8): agbcc reads `idx` again after each
  store, GCC keeps it in a register, so the registries differ and the PC writes the 13th event's flag
  into `gUdefCnt` (C12-SW-3; no traced field changes on any route). The other ten are harmless: a
  byte read through a misaligned `struct Anim *`, three signed overflows the GBA's code wraps the same
  way, three more places of sweep11's group G (arrays declared smaller than the game uses, same
  bytes), and three NULL-page writes the PC already drops as the GBA ignores them (H-1: the Demon
  King's `EkrDragonBodyAnimeSet54`, the trainee promotion's faces). The rest are sweep11's sites, one
  with a new index (`ranks[11]` in `HasBattleUnitGainedWeaponLevel`, same memory, value unused).

| Routes | Ticks | GBA frames | GBA vs ubsan, GBA vs zero | zero vs pattern | UBSan sites (per route) |
|---|---|---|---|---|---|
| Eirika: `e14`-`e22`, `eend` (10) | 1,061,815 | every | MATCH; zero: lag-tick frames on e17 (1), e18 (1), e21 (6) | SAME | 6-39 |
| Ephraim: `i14`-`i21`, `ib22`, `ibend` (10) | 1,099,120 | every | MATCH; i21: lag-tick frames (ubsan 1, zero 50) | SAME | 6-41 |
| Tower: `bt1`, `bt5`-`bt8` (5) | 346,822 | every 60 | MATCH | SAME | 24-36 |
| Ruins: `br2`-`br9` (8) | 666,088 | every 60 | MATCH; br9: N-C11-1 | SAME | 24-28 |
| Skirmishes: `sk4`, `sk10`, `sk15`, `sk17` | 188,726 | every 60 | MATCH | SAME | 29-31 |
| Features: `f_arena_ch5`, `f_chest_ch8`, `f_promo_ch6`, `f_support_e12`, `f_suspend_ch6` | 108,667 | every 60 (support: every) | MATCH | SAME | 14-39 |
| Post-game: `ibpost`, `bpost` | 197,155 | every 60 | MATCH | SAME | 30-32 |

Cost on the shared machine (2-10 other heavy runs): ubsan 19-31 s, zero and pattern 21-34 s per
10,000 ticks with every frame, 13-17 s with a frame every 60 ticks.



**Limits.** UBSan sees only what GCC checks: `-fsanitize=bounds` leaves out an
array that ends a struct (`UBSAN_CHECKS=undefined,bounds-strict` adds those,
expect many reports from the decomp's `[1]` and `[0]` tails), signed overflow
only without `-fwrapv`, and no check sees a read of the wrong but valid
memory. zero and pattern fill automatic variables only (not the heap, not
statics, not the GBA RAM a function reads before writing), and a read shows
only when it changes a traced field or frame (`gBmSt`, `gPlaySt`, the RNG, the
key buffer, the hashed buffers, the frame every 60 ticks, the lag position
events), on a path the buttons take. The two fills can also agree by chance
(code that only tests a value against something that both 0 and `0xFE` fail).
x86 traps `INT_MIN / -1` and a division by zero right after UBSan's report
(the NULL probe handles both).
