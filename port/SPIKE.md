# D1-a spike: 32-bit host with GBA memory at GBA addresses

Date: 2026-10-19. Branch `port-d1a-spike`, based on `port-pc` at `35ea0410` (the D7 commit).
Host: WSL2 Ubuntu (kernel 6.18), gcc 15.2.0 `-m32`, GNU ld 2.46, and i686-w64-mingw32-gcc 13 for the Windows probe.
Time used: about 2 hours, inside the time box.

**Answer: yes, it reached `OnMain`.** The game's own `AgbMain` ran from boot to the first `OnMain` call with no crash. No shared file in `src/` or `include/` was edited. All work is under `port/`.

## 1. Results

| Build | Game files linked | Port stubs | Result |
|---|---|---|---|
| core (PLAN.md D1 list) | 8: `main.c`, `bm.c`, `hardware.c`, `proc.c`, `time.c`, `rng.c`, plus `gamecontrol.c` (for `StartGame`) and `irq.c` | 87 generated function stubs, 56 data symbols bound to GBA addresses | First `OnMain` call at frame 0. The first VBlank (`OnVBlank`: flushes, OAM sync) also completes. The second `OnMain` crashes at EIP `0x080CC1E1` (section 3). |
| full (`FULL=1`) | 342: every `src/*.c` that compiles, except `ramfunc.c` and `agb_sram.c`, which port code replaces | 143 function stubs (audio and text), 6,241 data symbols bound to GBA addresses | First `OnMain` call at frame 0. The game then runs 4,142 frames: the health and safety screen (it timed out at 3,600 frames because no keys were pressed), then the Nintendo and Intelligent Systems logos. At frame 4,142 it crashes at EIP `0x080C6A55` (section 3). |

Log from the full build at frame 3,000:

```
[port] *** first OnMain call reached (frame 0, gMainCallback=0x100f1bc8) ***
[port] exit 0: frames=3000 OnMain calls=3000 distinct stubs hit=8
[state] gGameClock=3000 DISPCNT=0x0300 gMainCallback=0x100f1bc8
[state] VRAM non-zero bytes=12495 (FNV-1a 0x2F0F6AA1), PLTT non-zero entries=2
[state]   proc 0x2024e68 script 0x101e3080 name GAMECTRL
[state]   proc 0x2024ed4 script 0x101e6518 name (no name)      <- ProcScr_GameEarlyStartUI
```

The proc array sits at the GBA address `0x02024E68`. `gGameClock` equals the frame count, as checkpoint P1.5 expects. DISPCNT shows BG0 and BG1 on, which is what `GameIntroHealthSafetyWaitButton` sets.

Only 8 stubs were hit in the full build: `m4aSoundInit`, `m4aSoundMode`, `m4aSoundVSync`, `m4aSoundMain`, `m4aMPlayFadeOut` (audio, phase 3), `SetLang` and `ResetText` (`fontgrp.c` does not compile yet), and one discarded BIOS-area write (section 3, item 3).

## 2. Address reservation (the main D1-a unknown)

Probe: `port/tools/reserve_probe.c` reserves EWRAM 0x02000000, IWRAM 0x03000000, I/O 0x04000000, PLTT 0x05000000, VRAM 0x06000000, OAM 0x07000000, ROM 0x08000000 (+32 MB) and SRAM 0x0E000000, then writes to the first and last byte of each region.

| Host | Result |
|---|---|
| Linux i386, PIE (gcc default) | All regions reserved, **40 of 40 runs** (`port/tools/probe_repeat.sh`) |
| Linux i386, `-no-pie` with the default base | **ROM fails** (`EEXIST`): the i386 default text base 0x08048000 is inside the ROM window. The fix is `-Wl,-Ttext-segment=0x10000000`, which the spike build uses |
| Windows 11 (build 26300), 32-bit MinGW exe under WOW64, ASLR on | All regions reserved with `VirtualAlloc`, **40 of 40 runs**. In 5 sampled runs the image and stack landed between 0x00100000 and 0x01400000 |

Reservation works on both hosts. Nothing else was mapped in 0x02000000-0x0E00FFFF in any run.

## 3. What blocked, with the first failing addresses

1. **Core build, first failure: EIP `0x080CC1E1`**, the GBA Thumb address of `SetOpAnimHsStatus0`. It happens on the second `OnMain`. `gProcScr_GameControl` (compiled) starts `ProcScr_GameEarlyStartUI`. That file is not compiled in the core build, so the symbol resolves to its ROM copy at `0x08AA7680`, and `proc.c` calls a GBA code pointer from it. This is expected. Compiling `opanim-healthsafetyscreen.c` fixes it, and the full build shows that.
2. **Full build, first failure: EIP `0x080C6A55`** (`OpAnimInit`), at frame 4,142. `ProcScr_OpAnim` at `0x08AA71C8` is still assembly data (`data/*.s`), so its code pointers are GBA addresses. **This is plan risk 12 (code pointers in ROM data)**, and it appears under D1-a and D1-b alike. The function itself is compiled. Only the pointer in the ROM table is wrong.
3. **A NULL write that the GBA ignores.** On the first VBlank, `SyncHiOam` calls `ClearOAMBuffer(sOamHi.buf, ...)` while `sOamHi.buf` is still NULL, because `InitOam` has not run yet. The GBA discards writes to the BIOS area. The host has no page at 0, and Linux (`mmap_min_addr`) and Windows (NULL-page protection) both refuse to map one. The port's `ClearOam` drops writes below 0x4000 and logs them. This is a port-wide hazard, not a D1-a one: under D1-b it would fault the same way.

None of the three is caused by the address model.

## 4. What compiled

`make -f port/Makefile survey` compiles every `src/*.c` with the host compiler and no shared-file edits. **344 of 358 compile.** The 14 that fail:

| Cause | Files |
|---|---|
| Generated headers missing in a fresh worktree (needs the GBA `make` generation step; not tried) | `chapterdata.c`, `fontgrp-data.c`, `fontgrp.c` |
| Cast used as an lvalue (an agbcc extension; needs `NONMATCHING` code) | `eventcall.c` (built since 2026-10-19 from a `sed`-made copy, B-6), `banim-efxmagic-crimsoneye.c`, `banim-efxmagic-shine.c`, `eventfx-earthquake.c` (built since 2026-10-22 the same way, C10-1) |
| `const` and non-`const` objects in the same `.data` section | `bmdifficulty.c`, `cp_data.c` |
| Conflicting type qualifiers | `menu_def.c` |
| Array of incomplete `struct Text` (header order in `bmshop.h`) | `bmguide.c` (built since 2026-10-20 with `PC_PORT` compile fixes, P-20), `uiarena.c` (built since 2026-10-21 with the same fix, P-24) |
| ARM inline asm (both left out by the plan anyway) | `m4a.c` (`swi 0x2A`), `sio_multiboot.c` |

Host flags that turned out to be required (`port/Makefile`):
- `-funsigned-char`: plain `char` is unsigned on ARM.
- `-malign-data=abi`: without it, x86 GCC over-aligns arrays. The `ewram_data` sections of `bm.o`, `hardware.o` and `proc.o` grew by 0xC-0x18 bytes, and the link failed with "cannot move location counter backwards".
- `-std=gnu89 -fpermissive`: GCC 14 and later turn implicit declarations and int/pointer conversions into errors.
- `-fno-toplevel-reorder`, `-fno-common`, `-fwrapv` and `-fno-strict-aliasing`.

The force-included prelude `port/include/port.h` does two things:
- It routes `DmaSet` to `PortDmaSet`. A write to the I/O image does not start a transfer on the host, so `main.c:27`'s IWRAM clear would otherwise do nothing.
- It includes `global.h` and `muctrl.h` first, because `eventcall.h` uses `struct REDA` without including `muctrl.h`.

`hardware.c` also gets `-Dasm(x)=((void)0)`, to hide `asm("swi 3")` in `EnterSleepMode`.

## 5. What linked, and how variables land at GBA addresses

`port/tools/gen_host_link.py` builds the link inputs from the GBA build of the same commit, using `fireemblem8.map` and `fireemblem8.elf`, which were read from `~/fe8-feu` and not changed. It produces:
- **`gba_mem.ld`**, used with `-T` and `INSERT AFTER .bss`. It places each host object's `ewram_data`, `ewram_overlay_*` and IWRAM `.bss` section at the address the GBA map gives that object. The sections sit inside `.gba_ewram` (0x02000000, NOLOAD, spanning 256 KB) and `.gba_iwram` (0x03000000, 32 KB), so the ELF loader itself maps EWRAM and IWRAM at those addresses.
- **Absolute symbols** for every undefined data symbol, at its GBA address. These cover the 130 linker-only IWRAM symbols (`gGameClock` = 0x03000014, `gIRQHandlers`, `gLCDControlBuffer`), RAM variables of unbuilt files, and ROM data. ROM data reads the user's ROM, which is mapped at 0x08000000. **No `data/*.s` converter was needed to run.**
- **`autostubs.c`**: a stub for every undefined function. Each one logs its name and GBA address on its first call.

Placement check (`port/tools/check_ram_addrs.py`, host symbols compared with the GBA ELF):
- **Core build: 25 of 25** RAM variables are at their GBA addresses. This covers the Gate 0 fields `gMainCallback` 0x02024CB8, `gBmSt` 0x0202BCB0 and `gPlaySt` 0x0202BCF0, plus `gProcTreeRootArray` 0x02026A70.
- **Full build: 366 of 407** match. The 41 that do not are spread over about a dozen objects (for example `unitlistscreen`, `sio_core`, `banim-ekrbattleintro`, `bmshop`, `bmunit`, `bmsave-lib`, `bmmap`):
  - agbcc orders or pads variables differently from GCC within some objects.
  - A few objects are a different size: `icon.o` `ewram_data` is 0x3A0 on GBA and 0x1E0 on host, `bonusclaim.o`'s overlay is 0x100 smaller, `prepscreen.o`'s is 0x280 smaller, and `scene.o` `.bss` is 0xC0 on GBA and 0xB8 on host.

  The game logic still links and runs, because it uses symbols. Gate 0 traces and overlay aliasing will need per-variable placement, with each variable at the GBA ELF address rather than per object.

  **Done since (DIVERGENCES.md B-12, was P-14):** the game files are compiled with `-fdata-sections`, and `port/include/port.h` leaves `EWRAM_DATA`, `IWRAM_DATA` and `EWRAM_OVERLAY` empty, so every variable has its own section. `gen_host_link.py` places each one at its GBA address (globals from the GBA ELF; statics from the GBA object and the GBA map). After every link, `check_ram_addrs.py --binary ... --map ...` checks each RAM symbol per object and fails the build on any difference (also in `Makefile.sdl` and `Makefile.mingw`). Full build: 425 of 425 host-built RAM variables and 188 absolute RAM symbols at their GBA addresses. The two-argument form below still gives the old report by name.

Code goes at 0x10000000 (`-no-pie -Wl,-Ttext-segment=0x10000000`). I/O, PLTT, VRAM, OAM, ROM and SRAM are reserved at startup with `mmap(MAP_FIXED_NOREPLACE)` (`port/hw/memory.c`). `OnMain` is detected with `-Wl,--wrap=OnMain`, so `bm.c` stays unedited.

Port layer written for the spike:

| Path | Contents |
|---|---|
| `port/hw/main_pc.c` | Replaces `crt0.s`; fault handler that names GBA code addresses |
| `port/hw/frame.c` | `VBlankIntrWait` frame step from PLAN.md 3.4: lines 161-227, then 0-159, then 160 with the VBlank IRQ; IRQ dispatch through the game's `gIRQHandlers`; renderer slot empty |
| `port/hw/dma.c` | Immediate DMA. Timed DMA (VBlank, HBlank, sound FIFO) is only logged |
| `port/bios/bios.c` | `CpuSet`, `CpuFastSet`, LZ77, RL, Huffman, `Div`/`DivArm`/`DivRem`, `Sqrt`, `ArcTan2`, `RegisterRamReset`, `SoftReset`. `BgAffineSet` and `ObjAffineSet` use floating point and are **not bit-exact yet** (P1.4) |
| `port/asm/arm_pc.c` | `ColorFadeTick`, `ClearOam`, `Checksum32`, `TmFillRect`, `TmCopyRect`, `TmApplyTsa`, `PutOamHi`/`PutOamLo` and the `arm_call.s` veneers, with the asm's semantics. `DrawGlyph`, `DecodeString` and `MapFloodCore`/`MapFloodCoreStep` are logging stubs |
| `port/asm/ramfunc_pc.c` | Replaces `ramfunc.c`: binds the routine pointers straight to the C versions |
| `port/save/agb_sram_pc.c` | Replaces `agb_sram.c`: calls the C cores directly. SRAM is a blank 64 KB image at 0x0E000000 |
| Audio | Not built. The m4a entry points are generated stubs |

## 6. Recommendation for D1

**Keep D1-a.** Do not fall back to D1-b.

Evidence:
1. The fixed-address reservation, which was D1-a's main open question, works on Linux i386 and on 32-bit Windows (WOW64, ASLR on): 40 of 40 runs each. The only collision is the i386 non-PIE default base, and moving the text segment fixes it.
2. Variable placement works on Linux with no shared-file edits. 25 of 25 core variables, and 366 of 407 across the whole game, sit at their GBA addresses. They come straight from the GBA map and ELF, so nothing is hand-maintained.
3. Everything D1-b would have had to rewrite works as is: `REG_*` macros, `INTR_CHECK` at 0x3007FF8, the absolute RAM clears in `main.c`, the linker-only IWRAM symbols, and the pointer-sized `ActionData`/`PlaySt` save structs. ROM data is used straight from the user's ROM, so the 1,297 `baserom.gba` incbins and the 576 symbol words need no converter to run.
4. 344 of 358 game files compile unchanged. The full build (342 files) runs 4,142 frames of real game logic, and the first failure is ROM-data code pointers (risk 12), which D1-b shares.

Open items D1-a still carries (none of them is a reason to switch):
- **Windows variable placement is not proven.** The ELF loader trick does not carry over to PE, because a PE image is one contiguous block. Candidates: make the 0x02000000-0x0E010000 window part of the image as BSS, below a low image base; or link the core as i386 ELF and load it with a small custom loader. Test this next. Until then, Linux i386 is the verified host.
- **Code pointers in ROM data.** Under D1-a, GBA ROM addresses never collide with host code, so a generated table from GBA function address to host function (from the GBA ELF plus host symbols) can be used. Use it either in the fault handler (redirect EIP; the cdecl frame is already right) or when `data/*.s` tables are converted. Converting the proc-script tables to C removes the problem at the source.
- **The 41 misplaced variables** need per-variable placement before GBA-address traces are compared.
- **32-bit SDL3** was not tested.
- **Writes the GBA silently drops**, such as BIOS-area writes, need handling at each site (section 3, item 3).

## 7. Reproduce

```sh
# in WSL, from ~/fe8-port; needs gcc-multilib (and gcc-mingw-w64-i686 for the Windows probe)
sh port/tools/run_spike.sh core 60        # 8 game files: reaches OnMain, then the risk-12 crash
sh port/tools/run_spike.sh full 3000      # 342 game files: health and safety screen, no crash
make -f port/Makefile survey              # 344 of 358 compile
python3 port/tools/check_ram_addrs.py build/pc-full/fe8pc ~/fe8-feu/fireemblem8.elf
gcc -m32 -O1 -o build/pc/reserve_probe port/tools/reserve_probe.c
i686-w64-mingw32-gcc -O1 -o build/pc/reserve_probe.exe port/tools/reserve_probe.c
sh port/tools/probe_repeat.sh 40
```

`GBA_BUILD_DIR` (default since section 8: this tree when it has `fireemblem8.elf`, else `~/fe8-feu`) must hold `fireemblem8.map`, `fireemblem8.elf` and `baserom.gba` for the same commit. The tool only reads them.

**Note on the address oracle:** during this run another job switched `~/fe8-feu` from `port-pc` (`35ea0410`) to `round4-b` (`9bc283d5`, naming batch B) and rebuilt it at 09:05. The early core links used the `port-pc` map, and the later links used the `round4-b` map and ELF. Naming batches must keep the ROM byte-identical (`fireemblem8.gba: OK`), so every address is the same. The generator reported 0 unknown symbols in every link, so no name the host referenced had been renamed. This still shows why D6 matters: the next step should build its own GBA oracle from the pinned commit in the port worktree, not read a shared tree. (Done in section 8.6.)

## 8. Step 2: code pointers in ROM data (risk 12), `port/tools/fixptr.py`

Date: 2026-10-19, same branch. Time used: about 1 hour 45 minutes.

**Result: the OpAnim crash is gone.** The full build (342 files) now plays the opening, then the class reels, and loops through attract mode. It reaches the 60,000-frame limit with no crash. The first new failure (frame 8,349) is not a code pointer: it is a NULL write that the GBA ignores (8.4). The port now drops such writes the way the GBA does. With scripted A presses, the game reaches the title screen and the save menu. There it stops on a NULL **read** whose GBA result needs a fidelity decision (8.5).

### 8.1 What the OpAnim crash really was

`data/*.s` in this tree has almost no literal addresses left. The decomp already writes pointer words as symbols: `ProcScr_OpAnim` in `data/opanim.s` says `.word OpAnimInit`. The crash happened because the host reads ROM data in place (D1-a), from the user's ROM. In the ROM, that word is the GBA value 0x080C6A55, whether the source spells it as a symbol or a number. So converting literals is not enough on its own. The host also has to relocate the ROM words. `fixptr.py` does both, in two steps, and it only reads the GBA tree.

### 8.2 `fixptr.py convert`: literal addresses to symbols

- **Input:** the 59 `data/*.s` files.
  - Numeric `.word`/`.4byte`/`.long`/`.int` operands: the only ones that look like addresses are in `data/data_fe6sio.s`. That file is a multiboot image for the other GBA, so its addresses are in that machine's memory. It is excluded on purpose.
  - The 1,297 `.incbin "baserom.gba", off, size` blobs: 1.1 MB, about 280,000 aligned words. They hold the ROM data that is not decompiled yet, so any remaining literal pointers would be in them. A blob with pointers in it is split around them (`.incbin` ... `.word Sym` ... `.incbin`).
- **Thumb bit:** a Thumb function's ELF symbol value has bit 0 set, so `.word Func` assembles to the same word as the literal. `arm-none-eabi-nm -n` prints these values with bit 0 cleared (`080c6a54 T OpAnimInit`, while `st_value` is 0x080C6A55). nm's type letters also mark ROM data as `T`. So the tool reads `st_value`, `st_size` and `STT_FUNC` straight from the ELF symbol table, not from nm. A non-blob literal that equals a Thumb function start without bit 0 becomes `Func-1`.
- **Precision:** the first pass converted every blob word that equalled a symbol. That gave 571 hits, almost all of them noise from compressed graphics. For example, `GetPortraitData` sat in the middle of LZ77 bytes. Blob words now have to meet two rules:
  - The word equals the exact start of a global symbol (no `+offset`, no missing Thumb bit).
  - At least 5 of the 8 neighbouring words look like struct fields (small values, halfword pairs, RAM, I/O or symbol addresses).
- **Result: 1 word converted** (`gProcScr_TalkWaitForInput` at 0x08B29EA4, alone among zeros). 4,413 blob words were left alone, each listed with its reason in `port/tools/fixptr_unresolved.txt`. That includes the 5 `blob-unlikely-code` hits, which should be reviewed by hand. They all have 0 to 4 of 8 struct-like neighbours, so they are most likely graphics.
- **Output:** the converted copies go to `build/pc*/fixptr/data/*.s` and are assembled with `arm-none-eabi-as` and the GBA Makefile's `ASFLAGS`, from the GBA tree (so `.include` and `.incbin` resolve the same way). Nothing in the GBA tree is written.

### 8.3 `fixptr.py relocs`: the host relocation table

- **Collect:** every `R_ARM_ABS32` relocation that lands in ROM data, from three sources:
  - the assembled copies of `data/*.s`;
  - the GBA objects of `src/*.c`, data sections only (literal pools in `.text` are read only by GBA code);
  - `sound/`.
  Each site's address comes from `fireemblem8.map`. The addend is the implicit REL addend in the section bytes.
- **Verify:** each relocated value must equal the ROM word. **All 35,158 matched (0 mismatches).**
- **Host value:**
  - Code pointers become the host function. If the host does not build that function yet, a reference to it makes `gen_host_link.py` generate a logging stub, so the call logs instead of crashing.
  - Data pointers become the host object when the host builds that data itself. For the 41 misplaced RAM variables (section 5), that is their host address.
  - Everything else is bound to its GBA address already and needs no change.
- **Full build: 11,940 table entries** (5,039 code, 6,901 data). 23,158 relocations need no change. **0 unresolved code-like entries.** The 60 static-function targets all sit in ROM copies of objects the host compiles itself, and the host uses its own copies.
- **Core build:** 34 static-function targets are unresolved, because those objects are not built there.
- **At startup:** `port/hw/romreloc.c` rewrites the words in the private ROM copy and checks each original value first. Full build: `11669 now point at host code or data, 271 already right`.
- **Build:** `port/Makefile` runs both steps. The report is written to `port/tools/fixptr_unresolved.txt` for `FULL=1`, and to the build directory otherwise.

### 8.4 Runs

| Build and input | Result |
|---|---|
| core (8 files), 60 frames | No crash. Before this step it crashed on the second `OnMain` at 0x080CC1E1. The ROM copy of `ProcScr_GameEarlyStartUI` now calls the host stub for `SetOpAnimHsStatus0` |
| full (342), no input, `FE8PC_DROP_BIOS_WRITES=0` | Opening plays (`m4aSongNumStart` at frame 4,143, where it used to crash at 4,142). **Fault at frame 8,349**: a write to 0x00000034 at host EIP 0x10001638, in `AnimDelete` (`src/animedrv.c:170`), from `EndEkrMainMini` <- `ClassInfoDisplay_OnEnd` (`src/opinfo.c:1373`). When it unlinks the last `Anim`, it writes `anim->pNext->pPrev` with `pNext == NULL`. The GBA ignores writes to the BIOS area |
| full (342), no input (default) | The fault handler drops that write (the next table explains how) and logs the site once. **60,000 frames, no crash.** At exit: DISPCNT 0x3F00, 66,910 non-zero VRAM bytes, 234 palette entries. Live procs: `opinfo`, `opinfoicon`, `opinfogaugedraw`, `ekrUnitMainMini`, `ekrsubAnimeEmulator` (the class reels) |
| full (342), `FE8PC_AUTOPRESS=0x001,90,4300` (A every 90 frames) | Title screen at frame 4,409, save menu at 4,499. **Fault at frame 4,672**: `GetROMChapterStruct` (GBA 0x08034619, `src/chapterdata.c`) is a stub returning NULL. Then `GetChapterTitleExtra` (`src/chapter_title.c:115`) reads 0x0000000E |
| `FULL=1 WITH_GENERATED=1` (345 files: adds `chapterdata.c`, `fontgrp-data.c`, `fontgrp.c`), same input | Those three only needed headers that the GBA `make` generates, plus `-iquote .`, and all three now compile. The text stubs are gone. **Fault at frame 4,672**: `SaveMenuInitSaveSlotData` (`src/difficultymenu.c:59`, marked `// BUG?` in the decomp) calls `IsGameNotFirstChapter((struct PlaySt *)slot)`, and `src/bmsave.c:268` **reads** 0x00000014 |

Added for these runs (all port-only):

| Addition | What it does |
|---|---|
| BIOS-write drop (`port/hw/main_pc.c`) | On a fault, if the access is a write (`REG_ERR` bit 1) below 0x4000 and EIP is in the game's code, the handler decodes the length of the x86 integer store and resumes after it. Turn it off with `FE8PC_DROP_BIOS_WRITES=0` |
| Headless input (`port/hw/frame.c`) | `FE8PC_AUTOPRESS="mask,period,start[,hold]"` |

### 8.5 Next blocker (needs a decision): reads from the BIOS area

On hardware, a game read from 0x00000000-0x00003FFF while the CPU runs outside the BIOS does not fault. It returns the last opcode fetched from the BIOS, which depends on whether an IRQ or a SWI returned last. Here the value decides a save-menu flag (`proc->unk_3a[slot] |= 1`), so the choice changes game behaviour. Options:
- **(a)** Emulate it in the fault handler: decode the load and fill the destination register with a policy value (for example, track the last BIOS path in the frame step and use that opcode). Port-only, and it logs every site.
- **(b)** Guarded `#ifdef PC_PORT` fixes in shared files at each site (`difficultymenu.c:59`). Each fix must keep `fireemblem8.gba: OK`. That is a new kind of shared-file edit, outside the list in PLAN.md 3.2.
- **(c)** Both: (a) to keep running, then (b) once the faithful value is known.

My recommendation is (c), with (a) using an mGBA-matching value. The fidelity policy is the decision to take. The same applies to the dropped writes: the write drop is exact (the GBA discards them), but it hides game bugs that a later fix pass may want to log.

### 8.6 GBA check and the address oracle

- The GBA ROM is now built in this worktree, as D6 recommended: copy `tools/agbcc` and `baserom.gba` from `~/fe8-feu`, run `make -f make_tools.mk`, then `make -j24`. That takes 39 seconds.
- **`fireemblem8.gba: OK`** (SHA-1 c25b145e37456171ada4b0d440bf88a19f4d509f). This held at the start and again after a `make clean` rebuild at the end of this step.
- The branch differs from `port-pc` only under `port/` (`git diff port-pc -- . ':!port'` is empty), so this is the `port-pc` ROM.
- `GBA_DIR` and `run_spike.sh` now default to this tree whenever `fireemblem8.elf` is present, so `~/fe8-feu` is no longer read.
- **Note:** `make clean` deletes every `*.o` in the tree, including those under `build/pc*`, so rebuild the PC variants afterwards.

### 8.7 Reproduce

These are the commands as of this step. Section 9 changed the default: `FULL=1` now also builds the three files that need generated headers, and the 342-file build is `WITH_GENERATED=0` in `build/pc-full-nogen`. Use 9.4 for the current commands.

```sh
# in WSL, from ~/fe8-port, after the GBA build above
sh port/tools/run_spike.sh core 60
FE8PC_TIMEOUT=300 sh port/tools/run_spike.sh full 60000                              # attract mode, no crash
FE8PC_DROP_BIOS_WRITES=0 FE8PC_FRAMES=60000 ./build/pc-full/fe8pc baserom.gba          # fault at 8,349
FE8PC_AUTOPRESS=0x001,90,4300 FE8PC_FRAMES=40000 ./build/pc-full/fe8pc baserom.gba     # fault at 4,672
make -f port/Makefile -j24 FULL=1 WITH_GENERATED=1
FE8PC_AUTOPRESS=0x001,90,4300 FE8PC_FRAMES=40000 ./build/pc-full-gen/fe8pc baserom.gba # BIOS read
```

## 9. Step 3: the two frame-4,672 fixes (guarded by PC_PORT)

Date: 2026-10-19, same branch. Time used: about 1 hour. The decision is the frame-4672 entry in `port-plan/DECISIONS.md`. Every divergence is listed in `port-plan/DIVERGENCES.md`, which is in the Windows project folder, not in this tree.

### 9.1 What changed

| Fix | Change | GBA build |
|---|---|---|
| (a) `GetROMChapterStruct` returned NULL | `port/Makefile`: `FULL=1` now builds `chapterdata.c`, `fontgrp-data.c` and `fontgrp.c` by default (`WITH_GENERATED` defaults to 1). This is the decomp's own path. The GBA `make` generates `src/data/chapter_settings.h` (with jsonproc) and `graphics/debug_font.4bpp.h`, and the three files then compile unchanged with `-iquote .`. If those headers are missing, the build stops and says to run the GBA `make` first. 345 shared files are linked. | No shared file touched |
| (b) `src/difficultymenu.c:59` passed the slot number as a `PlaySt *` | Under `#ifdef PC_PORT` only, the call is now `IsGameNotFirstChapter(&playSt)`. `playSt` is the save's own `PlaySt`, read a few lines earlier, with its chapter index restored, so the result equals `IsGameSaveNotFirstChapter(slot)`. The `#else` branch keeps the GBA call. | `fireemblem8.gba: OK`, SHA-1 c25b145e37456171ada4b0d440bf88a19f4d509f, with `difficultymenu.o` rebuilt from the edited file |

**What (b) does on each machine:**
- **GBA:** the call reads two open-bus bytes from the BIOS area. Two open-bus values can occur after boot: `0xE3A02004` after a SWI, and `0xE55EC002` after an IRQ. Both make the function return true for every slot.
- **PC:** the function returns true only for a save past the Prologue.
- **Effect:** only one place reads that bit, `savemenu.c:1190` (Extras option 6, `CallExtraMap`), and the retail Extras menu never offers option 6. The player sees no difference, but one `SaveMenuProc` byte differs in RAM traces (DIVERGENCES.md, D-2).

### 9.2 Runs (`build/pc-full`, 345 files)

| Input | Result |
|---|---|
| None, 60,000 frames | No crash, as before. At exit: DISPCNT 0x3F00, 61,742 non-zero VRAM bytes, 264 palette entries, and the class-reel procs are live |
| `FE8PC_AUTOPRESS=0x001,90,4300` | Title screen at frame 4,409, save menu at 4,499, then New Game. **Frame 4,672 passes.** `m4aSongNumStop` is first hit at 4,712. **Fault at frame 8,412**, the same in two runs |

### 9.3 Next blocker: `EndFace(NULL)` reads the BIOS area

- **Where:** frame 8,412, read of `0x00000040`, host EIP `0x1007C7C0`. The symbol is `EndFace` (GBA `0x08005738`) at `src/face.c:421`: `gFaces[proc->faceSlot] = NULL` runs with `proc == NULL`.
- **How it gets there:** `ResetFaces` (`face.c:289`) calls `EndFaceById(i)` for empty slots. The chain is `NewWorldMap`, then `WorldMap_Init`, then `SetupGraphicSystemsForWorldMap` (`worldmap_main.c:320`), as the Prologue's first world map opens.
- **What the GBA does:**
  - It reads lane 0 of the open-bus word: `0x04` after a SWI, or `0x02` after an IRQ.
  - It then stores NULL to `gFaces[4]` or to `gFaces[2]`. `gFaces[4]` is past the end of the array: it is `gpBgVerticalScrollSt`, at `0x03004990`.
  - `Proc_End(NULL)` then does nothing.
- **Candidate fix:** it is the same class as fix (b). Add a NULL check in `EndFace` under `PC_PORT`.

**Look-ahead.** I patched P-1 temporarily to see the next fault. The patch is not committed.
- **Where:** frame 8,677. `SetFaceBlinkControl` (`face.c:1275`) runs with `pBlinkProc == NULL` for a world-map portrait. It is called from `WmDrawFace` (`worldmap_face.c:358`), which `EventC4_WmShowPortrait` calls.
- **What happens:** the port drops the store (H-1). Then `FaceBlinkProc_GenBlinkInterval` (`face.c:1292`) reads `0x30` and faults.
- **For an exact fix:** it must still call `AdvanceGetLCGRNValue()` once, as the GBA does.

There were two such reads in the first 265 world-map frames, so the list of per-site fixes will probably keep growing. DIVERGENCES.md section 4 has the details.

### 9.4 Reproduce

```sh
# in WSL, from ~/fe8-port, after the GBA make (it generates the headers)
make -j24                                                                              # fireemblem8.gba: OK
make -f port/Makefile -j24 FULL=1                                                      # 345 files
FE8PC_AUTOPRESS=0x001,90,4300 FE8PC_FRAMES=40000 ./build/pc-full/fe8pc baserom.gba     # fault at 8,412 (EndFace)
FE8PC_FRAMES=60000 ./build/pc-full/fe8pc baserom.gba                                   # attract mode, no crash
make -f port/Makefile -j24 FULL=1 WITH_GENERATED=0                                     # old 342-file build
```
