# Gate 0 harness (port-plan/PLAN.md section 4, Gate 0). Run from the repository
# root, after the GBA `make` (fireemblem8.gba must be `OK`):
#
#   make -f port/test/Makefile oracle       # headless mGBA Lua runner
#   make -f port/test/Makefile boot-check   # stage 2: boot to health and safety
#   make -f port/test/Makefile pputest      # how mGBA draws OBJ priorities and window-cut BGs: test ROM (README)
#   make -f port/test/Makefile gba-trace    # stage 4: per-tick GBA trace
#   make -f port/test/Makefile pc-trace     # stage 5: the same trace from the PC build
#   make -f port/test/Makefile pc-lag-replay # stage 5b: PC rerun with the GBA's lag VBlanks where they came (lag check)
#   make -f port/test/Makefile pc-affine-replay # stage 5c: PC rerun drawing frames with the GBA's bg2affin (P2.2b)
#   make -f port/test/Makefile compare      # stage 6: first differing tick and field, lag-only or not
#   make -f port/test/Makefile gate0        # all of the above
#   make -f port/test/Makefile scripted     # stages 4-6 on the scripted route
#   make -f port/test/Makefile route ROUTE_KEYS=... ROUTE_TICKS=...  # a bot route through the lag replay
#   make -f port/test/Makefile chain-route CHAIN=ch6  # a route that starts from a chapter save (ch5x ... e19, b20 ... bpost, ch8i, i9 ... i21, ib22 ... ibpost)
#   make -f port/test/Makefile path-route PATH_NAME=pe14  # a round-10 "paths" key file (inputs/paths/README.md)
#   make -f port/test/Makefile pc-replay    # diagnostic: PC rerun with the GBA clock (the old lag check)
#   make -f port/test/Makefile clock-replay # diagnostic: that replay compared on its own
#   make -f port/test/Makefile snd SND=title   # sound checks P3.3/P3.4a (README "Sound")
#   make -f port/test/Makefile snd-mutate      # checks the sound replay with driver mutants
#   make -f port/test/Makefile snd-route ROUTE_KEYS=... [GATE0_SRAM_IN=...|GATE0_STATE_IN=...]  # sound on any
#                                           # route or monkey key file, in chunks (README "Sound on any route")
#   make -f port/test/Makefile chain-snd-route CHAIN=ch6  # the same on a route from its chapter save
#   make -f port/test/Makefile nullprobe    # diagnostic: every BIOS-area/NULL-page read on the
#                                           # ch0-ch5 routes (README "NULL probe"; heavy)
#   make -f port/test/Makefile sram-capture SAVE_KEYS=... SAVE_AT=... SAVE_NAME=...  # a save from a GBA run
#   make -f port/test/Makefile chapsave-ch2    # Gate 0 from a save (README "Chapter saves"); also chapsave-frelia
#   GATE0_SRAM_IN=port/test/saves/NAME.sav ... # any target above, booting both sides from that save
#   make -f port/test/Makefile snapshot KEYS=... AT=T  # save states of both sides at tick T (README "Save states")
#   GATE0_STATE_IN=SNAP/tick<T> ...            # gba-trace, pc-trace, the replays, route, compare from tick T
#   make -f port/test/Makefile movie-check FRAME_KEYS=... FRAME0=N  # a TAS movie per frame: milestones
#   make -f port/test/Makefile frame-trace FRAME_KEYS=... FRAME0=N TICKS=N  # its Gate 0 capture (README "TAS route")
#   make -f port/test/Makefile monkey START=SAV|SNAP/tick<T>|power-on SEED=n TICKS=n  # a menu monkey key file
#   make -f port/test/Makefile monkey-check MONKEY_KEYS=... [EVERY_FRAME=1]  # its checks (README "Menu monkey")
#
# Output goes to build/gate0 and build/pc-gate0 (build/gate0-test and
# build/pc-gate0-test for a test build; ignored by git); saves go to
# port/test/saves (ignored by git too). The ROM is only read.

# A test build (FE8PC_TESTBUILD=1 on the command line only, port/testbuild.mk;
# port-plan/DIVERGENCES.md TB-1) is built and run in build/pc-gate0-test
# (PC_BUILD, below) and its output goes to build/gate0-test, never to the
# release build's directories (port/Makefile also refuses a test build in a
# directory whose name does not end in -test).
include port/testbuild.mk
TEST_SUFFIX := $(if $(filter 1,$(FE8PC_TESTBUILD)),-test)
OUT      := build/gate0$(TEST_SUFFIX)
GBA_DIR  ?= .
GBA_ROM  ?= $(GBA_DIR)/fireemblem8.gba
CC_HOST  ?= gcc
PYTHON   ?= python3

KEYS  ?= port/test/inputs/boot300.keys
TICKS ?= 300
GBA_MAX_FRAMES ?= 1000000
# Oracle-limit masks (README.md, "Rules and masks").
MASKS ?= port/test/oracle_limits.json

.PHONY: oracle boot-check gba-trace pc pc-trace pc-lag-replay pc-replay pc-affine-replay compare gate0 scripted \
        clock-replay

oracle: $(OUT)/mgba_lua_run

# Built against the distribution's libmgba-dev (0.10.5, Lua 5.4); see README.md.
$(OUT)/mgba_lua_run: port/test/oracle/mgba_lua_run.c
	@mkdir -p $(@D)
	$(CC_HOST) -O2 -Wall -o $@ $< -lmgba

boot-check: $(OUT)/mgba_lua_run
	GATE0_FRAMES=400 GATE0_SHOT=$(OUT)/boot.png \
	    $(OUT)/mgba_lua_run $(GBA_ROM) port/test/oracle/boot_check.lua 100000

# PPU test ROM (README.md, "PPU test ROM"): how the oracle, mGBA, draws a few
# cases the PC renderer must draw the same way (OBJ against OBJ; text BGs cut
# at window segments), read from mGBA itself. Still screens built from
# port/test/oracle/pputest with the host's arm-none-eabi-gcc into
# $(OUT)/pputest (no FE8 code, never committed), shot by the runner and
# checked by tools/pputest_check.py; port/render/test_obj.c RT19 and
# test_bg.c's mGBA BG clip case hold the PC renderer to the same pixels.
PPUTEST_SRC := port/test/oracle/pputest
.PHONY: pputest
pputest: $(OUT)/mgba_lua_run
	@mkdir -p $(OUT)/pputest
	@set -e; for v in 0 1 2; do \
	  arm-none-eabi-gcc -marm -mcpu=arm7tdmi -O1 -nostdlib -ffreestanding -DVARIANT=$$v \
	    -Wl,-Ttext=0x08000000 -Wl,-e,_start -Wl,--section-start=.text.start=0x08000000 \
	    $(PPUTEST_SRC)/crt0.s $(PPUTEST_SRC)/pputest.c -o $(OUT)/pputest/pputest$$v.elf; \
	  arm-none-eabi-objcopy -O binary $(OUT)/pputest/pputest$$v.elf $(OUT)/pputest/pputest$$v.gba; \
	  rm -f $(OUT)/pputest/pputest$$v.png; \
	  PPUTEST_PNG=$(OUT)/pputest/pputest$$v.png $(OUT)/mgba_lua_run $(OUT)/pputest/pputest$$v.gba \
	      port/test/oracle/pputest.lua 40 > $(OUT)/pputest/run$$v.log 2>&1 || true; \
	done
	$(PYTHON) port/test/tools/pputest_check.py $(OUT)/pputest

# Stage 4: per-tick GBA trace (README.md, "Trace"). Debugger breakpoints at
# OnMain, VBlankIntrWait and OnVBlank, and the lag-position events (README.md,
# "Lag position"): RunProcessRecursive's entry and epilogue, RunProcessScript
# and the return from each of its commands (gate0_rpscmd, a script's head,
# kind 3), the bx lr of GetGameClock and SetGameTime, the swi of the four BIOS
# copy and LZ77 wrappers and the bx lr of FlipBgVerticalScroll
# (fireemblem8.elf of this commit; the compare checks them against the ELF).
# Fourteen (the runner takes up to 64 since port-sndcheck). The runner checks
# the addresses itself, so a breakpoint costs its hits (the script calls), not
# every instruction the runner steps (README.md, "Headless Lua").
GBA_BP := gate0_onmain@0x080152F4,gate0_wait@0x080D16DC,gate0_onvblank@0x080152A4,gate0_rpr@0x08002E28,gate0_rpr_end@0x08002E7E,gate0_rps@0x08003394,gate0_rpscmd@0x080033D0,gate0_clock@0x08000D2C,gate0_settime@0x08000D38,gate0_cpuset@0x080D1678,gate0_cpufastset@0x080D1674,gate0_lz77wram@0x080D1694,gate0_lz77vram@0x080D1690,gate0_flipscroll@0x08014EB8

# Raster-timing frames (README.md, "Raster-timing frames"; decision 9): the
# runner's raster log of each frame15 frame (oracle/mgba_lua_run.c,
# MGBA_RUN_RASTERLOG), which the PC runs below redraw frames from. Watched
# besides palette RAM, VRAM and OAM: the RAM the line handlers read, as
# name@address+size: the minimap window's two buffers and its front and back
# pointers (gMinimapWinBuf, gMinimapFrontWinBuf, gMinimapBackWinBuf; not
# gMinimapDisplayedWinBuf, which Minimap_OnHBlank itself writes) and the phase
# banner's blend coefficients (gBmSt.altBlendACa/ACb/BCa/BCb, +0x38-0x3B,
# PhaseIntroVMatchHi/Mid/Lo); and two I/O ranges: REG_IE, which decides
# whether the HBlank and VCount handlers run at all (UpdateHBlankHandlerState
# and SetInterrupt_LCDVCountMatch write it from the main loop: a handler the
# GBA still runs on the VBlank lines before OnMain turns it off, G7), and the
# BG scroll registers BG0HOFS-BG3VOFS, which OnMain also writes directly
# (BackgroundSlide_Loop's REG_BG3HOFS, round 8's Guide frames); and since round
# 10 REG_DISPSTAT, whose HBlank and VCount IRQ enables and VCount line
# OnVBlank's FlushLCDControl copies from the shadow gLCDControlBuffer: after a
# slow VBlank proc tree the GBA makes that store many lines after line 160 and
# takes no HBlank IRQ before it (C10-10 (b), the minimap's first frame), while
# the PC's flush is at line 160 (bits 0-2, the status flags, are not logged).
# Since fix round 13 (P-42) also the map animations' two scanline buffer
# pointers gManimScanlineBufs (mapanim_scanline.c; not gManimActiveScanlineBuf,
# which the line handlers write): OnHBlank_WinHScanlines and its kin take
# gManimScanlineBufs[0] on the VBlank lines, and the Torch and Unlock map
# effects swap the two from the main loop, which on the GBA can come after
# line 227 (the frame then shows the tick before's circle).
# An I/O halfword a line handler stores in a frame is left to the redraw's live
# handlers (pc/gate0_hook.c). GBA_RASTER_IRQ: IrqMain's `bx r0`, in ROM and in
# its IWRAM copy (IntrMain_Buffer). The compare checks all of them against both
# binaries (the I/O ranges are hardware addresses). Six ranges is the runner's
# and the hook's limit (nine regions with the three video ones).
GBA_RASTER_RAM := mmwin@0x02000000+0x508,banner@0x0202BCE8+0x4,manim@0x0203E754+0x8,ie@0x04000200+0x2,bgofs@0x04000010+0x10,dispstat@0x04000004+0x2
GBA_RASTER_IRQ := 0x080001F4,0x03004258
GBA_RASTER_ENV  = MGBA_RUN_RASTERLOG=$(OUT)/gba_raster.bin MGBA_RUN_RASTER_RAM=$(GBA_RASTER_RAM) \
                  MGBA_RUN_RASTER_IRQ=$(GBA_RASTER_IRQ)

# TAS route (README.md, "TAS route"): a per-FRAME key file played as a movie
# player does. movie-check prints the milestones (no breakpoints, fast);
# frame-trace is the Gate 0 capture in frame mode, whose "keys" are the masks
# the game read (the gate0_keyread breakpoint at UpdateKeyStatus's load of
# REG_KEYINPUT, 0x080013E6), for port/test/tools/tas_route.py. FRAME_KEYS is
# TAS-derived data from tools/tas_inputs.py (port/test/tas/, git-ignored);
# FRAME0 is the movie entry that interval 0 plays.
FRAME_KEYS ?=
FRAME0     ?= 0
GBA_BP_KEYREAD := $(GBA_BP),gate0_keyread@0x080013E6
.PHONY: movie-check frame-trace
movie-check: $(OUT)/mgba_lua_run
	$(NO_CHEAT)
	@[ -n "$(FRAME_KEYS)" ] || { echo "usage: make -f port/test/Makefile movie-check FRAME_KEYS=FILE [FRAME0=N]"; exit 2; }
	MOVIE_KEYS=$(FRAME_KEYS) MOVIE_FRAME0=$(FRAME0) MOVIE_OUT=$(OUT)/movie_milestones.jsonl \
	    $(OUT)/mgba_lua_run $(GBA_ROM) port/test/oracle/movie_check.lua 4000000
frame-trace: $(OUT)/mgba_lua_run
	$(NO_CHEAT)
	@[ -n "$(FRAME_KEYS)" ] || { echo "usage: make -f port/test/Makefile frame-trace FRAME_KEYS=FILE [FRAME0=N] TICKS=N"; exit 2; }
	@rm -rf $(OUT)/frame_frames; mkdir -p $(OUT)/frame_frames
	MGBA_RUN_BP=$(GBA_BP_KEYREAD) GATE0_FRAME_KEYS=$(FRAME_KEYS) GATE0_FRAME0=$(FRAME0) GATE0_TICKS=$(TICKS) \
	GATE0_OUT=$(OUT)/frame.jsonl GATE0_SHOTDIR=$(OUT)/frame_frames \
	    $(OUT)/mgba_lua_run $(GBA_ROM) port/test/oracle/gate0_capture.lua $(GBA_MAX_FRAMES)

gba-trace: $(OUT)/mgba_lua_run
	@rm -rf $(OUT)/gba_frames $(OUT)/gba_raster.bin; mkdir -p $(OUT)/gba_frames
	$(STATE_CHECK)
	$(GBA_STATE_ENV) MGBA_RUN_BP=$(GBA_BP) GATE0_KEYS=$(KEYS) GATE0_TICKS=$(TICKS) GATE0_OUT=$(OUT)/gba.jsonl \
	GATE0_RAW=$(OUT)/gba.raw GATE0_SHOTDIR=$(OUT)/gba_frames $(GBA_RASTER_ENV) \
	    $(OUT)/mgba_lua_run $(GBA_ROM) port/test/oracle/gate0_capture.lua $(GBA_MAX_FRAMES)

# Stage 5: the full PC build (port/Makefile FULL=1) in its own directory, plus
# port/test/pc/gate0_hook.c and thirteen link-time wraps, proc.o compiled
# with -finstrument-functions (port/test/pc/gate0.mk, read as a second
# makefile; README.md, "Lag position") and port/hw/frame.o with FE8PC_GATE0
# (the frame step's lines for the lag replay and the raster-timing redraw,
# gate0.mk; no shipped build has them). The game code is also compiled with
# FE8PC_DEBUG (gate0.mk), the test builds' checks, with
# port/test/pc/proc_call_arg_check.c linked in, and with the compiler safety
# checks of every test build (PORT_SAFETY_CHECKS=1: the stack protector and
# _FORTIFY_SOURCE, port/Makefile; the sweep builds come through here too).
# port/Makefile is not edited: its own
# PORT_SRCS and HOST_LDFLAGS are read back from `make -p` and extended on the
# command line. FIXPTR_REPORT is redirected so the FULL build does not rewrite
# the tracked port/tools/fixptr_unresolved.txt.
PC_BUILD := build/pc-gate0$(TEST_SUFFIX)
PC_BASE_SRCS = $(shell MAKEFLAGS= make -f port/Makefile -pn FULL=1 GBA_DIR=$(GBA_DIR) .gate0-none 2>/dev/null \
                 | sed -n 's/^PORT_SRCS := //p')
PC_BASE_LDFLAGS = $(shell MAKEFLAGS= make -f port/Makefile -pn FULL=1 GBA_DIR=$(GBA_DIR) .gate0-none 2>/dev/null \
                    | sed -n 's/^HOST_LDFLAGS := //p')
# The m4a entry points of audio/snd_calls.py WATCHED: the lag replay's sound
# positions (README.md, "Sound on any route"; pc/gate0_hook.c). They only count
# the main loop's top-level m4a calls, and with a GBA trace that has "lag_snd"
# run a lag VBlank before the call it came before.
comma := ,
PC_SND_WRAPS := $(foreach f,m4aSoundVSync m4aSoundMain m4aSoundInit m4aSongNumStart m4aSongNumStop MPlayStart \
                  m4aSoundMode m4aMPlayStop m4aMPlayImmInit m4aMPlayVolumeControl m4aMPlayPanpotControl \
                  m4aMPlayFadeOut m4aMPlayFadeOutTemporarily m4aMPlayFadeIn m4aSongNumStartOrChange \
                  m4aSongNumStartOrContinue m4aSongNumContinue m4aMPlayAllStop m4aMPlayContinue \
                  m4aMPlayAllContinue m4aMPlayTempoControl m4aMPlayPitchControl m4aMPlayModDepthSet \
                  m4aMPlayLFOSpeedSet MPlayContinue MPlayFadeOut MPlayOpen SoundInit SoundClear \
                  m4aSoundVSyncOff m4aSoundVSyncOn SampleFreqSet MPlayExtender,-Wl$(comma)--wrap=$(f))
PC_WRAPS := -Wl,--wrap=VBlankIntrWait -Wl,--wrap=UpdateKeyStatus -Wl,--wrap=OnVBlank \
            -Wl,--wrap=RenderBeginFrame -Wl,--wrap=RenderEndFrame -Wl,--wrap=RenderLine \
            -Wl,--wrap=GetGameClock -Wl,--wrap=SetGameTime \
            -Wl,--wrap=CpuSet -Wl,--wrap=CpuFastSet -Wl,--wrap=LZ77UnCompWram -Wl,--wrap=LZ77UnCompVram \
            -Wl,--wrap=FlipBgVerticalScroll $(PC_SND_WRAPS)
PC_INSTRUMENT := -finstrument-functions
PC_SND_INSTRUMENT := -finstrument-functions
PC_FRAME_DEFS := -DFE8PC_GATE0=1
# The bug-sweep builds (README.md, "Bug sweeps"; port/test/sweep.mk) add a
# makefile and port sources to this sub-make: PC_EXTRA_MK (read after
# pc/gate0.mk) and PC_EXTRA_SRCS (more PORT_SRCS); their settings reach it as
# command-line variables of this make. Empty for the Gate 0 binary itself.
PC_EXTRA_MK   ?=
PC_EXTRA_SRCS ?=

# The link rule does not see a flag change, so a changed wrap list relinks,
# and a changed proc.o, m4a_1_pc.o or frame.o flag recompiles that object.
PC_FLAGS_KEY = $(PC_WRAPS) proc.o: $(PC_INSTRUMENT) m4a_1_pc.o: $(PC_SND_INSTRUMENT) frame.o: $(PC_FRAME_DEFS)
pc:
	@mkdir -p $(PC_BUILD)
	@echo "$(PC_FLAGS_KEY)" | cmp -s - $(PC_BUILD)/gate0_wraps \
	    || { rm -f $(PC_BUILD)/fe8pc $(PC_BUILD)/src/proc.o $(PC_BUILD)/port/hw/frame.o \
	             $(PC_BUILD)/port/audio/m4a_1_pc.o; \
	         echo "$(PC_FLAGS_KEY)" > $(PC_BUILD)/gate0_wraps; }
	$(MAKE) -f port/Makefile -f port/test/pc/gate0.mk $(addprefix -f ,$(PC_EXTRA_MK)) FULL=1 GBA_DIR=$(GBA_DIR) \
	    BUILD=$(PC_BUILD) FIXPTR_REPORT=$(PC_BUILD)/fixptr_unresolved.txt PORT_SAFETY_CHECKS=1 \
	    PORT_SRCS="$(PC_BASE_SRCS) port/test/pc/gate0_hook.c port/test/pc/proc_call_arg_check.c port/test/pc/savestate.c $(PC_EXTRA_SRCS)" \
	    HOST_LDFLAGS="$(PC_BASE_LDFLAGS) $(PC_WRAPS)" PC_INSTRUMENT="$(PC_INSTRUMENT)" \
	    PC_SND_INSTRUMENT="$(PC_SND_INSTRUMENT)" PC_FRAME_DEFS="$(PC_FRAME_DEFS)"

# PC_FAULT_OK=1: a PC run that stops before TICKS (a fault such as P-11 in
# port-plan/DIVERGENCES.md) is reported but does not stop make here. The hook
# flushes every complete tick, so the compare then reports every tick the PC
# completed, and its verdict is INCOMPLETE (exit 1): every run it compares
# must reach TICKS with no fault in its log (integration review, 2026-10-20).
# FE8PC_BLEND=mgba8: frame15 is compared with mGBA, which blends in 8-bit space
# (render-design-obj.md 7.6). The shipped build keeps the GBA's 5-bit math.
# FE8PC_MGBA_BGCLIP=1: the same for mGBA's way of cutting a horizontally
# flipped text-BG tile at a window segment (port/render/bg_line.c; README.md,
# "PPU test ROM"). The shipped build draws BGs as the hardware does.
PC_FAULT_OK ?=
PC_TIMEOUT  ?= 300
# Raster-timing frames (README.md, "Raster-timing frames"): the plain run and
# the lag replay also draw each frame15 frame again with the GBA's timing from
# the raster log of the gba-trace before them, when it is there. Nothing the
# game reads changes. (Not the affine replay: it redraws its own frames.)
RASTER_FROM = $(if $(wildcard $(OUT)/gba_raster.bin),GATE0_RASTER_FROM=$(OUT)/gba_raster.bin)
pc-trace: pc
	@mkdir -p $(OUT)
	$(call STATE_RELINK,pc)
	@$(RASTER_FROM) GATE0_KEYS=$(KEYS) GATE0_TICKS=$(TICKS) GATE0_OUT=$(OUT)/pc.jsonl GATE0_RAW=$(OUT)/pc.raw \
	FE8PC_BLEND=mgba8 FE8PC_MGBA_BGCLIP=1 FE8PC_FRAMES=0 timeout $(PC_TIMEOUT) $(PC_BUILD)/fe8pc $(GBA_DIR)/baserom.gba > $(OUT)/pc.log 2>&1; \
	status=$$?; if [ $$status -ne 0 ]; then \
	    echo "fe8pc stopped with status $$status, see $(OUT)/pc.log"; tail -6 $(OUT)/pc.log; \
	    [ "$(PC_FAULT_OK)" = 1 ] || exit 1; fi
	@grep "^\[gate0\]" $(OUT)/pc.log || true

# Stage 5b, the lag check (README.md, "Lag replay"): the PC run again with
# each GBA lag VBlank run where it came on the GBA (GATE0_LAG_FROM: its
# position in the tick's main-loop events, from gba.jsonl). The compare uses
# it to label each difference lag-only or not. Run after gba-trace (same KEYS
# and TICKS). The replay changes the PC run, so it is never compared on its
# own as a result.
pc-lag-replay: pc
	@mkdir -p $(OUT)
	$(call STATE_RELINK,pclag)
	@$(RASTER_FROM) GATE0_LAG_FROM=$(OUT)/gba.jsonl GATE0_KEYS=$(KEYS) GATE0_TICKS=$(TICKS) \
	GATE0_OUT=$(OUT)/pc_lagreplay.jsonl GATE0_RAW=$(OUT)/pc_lagreplay.raw \
	FE8PC_BLEND=mgba8 FE8PC_MGBA_BGCLIP=1 FE8PC_FRAMES=0 timeout $(PC_TIMEOUT) $(PC_BUILD)/fe8pc $(GBA_DIR)/baserom.gba > $(OUT)/pc_lagreplay.log 2>&1; \
	status=$$?; if [ $$status -ne 0 ]; then \
	    echo "fe8pc (lag replay) stopped with status $$status, see $(OUT)/pc_lagreplay.log"; tail -6 $(OUT)/pc_lagreplay.log; \
	    [ "$(PC_FAULT_OK)" = 1 ] || exit 1; fi
	@grep "^\[gate0\]" $(OUT)/pc_lagreplay.log || true

# Diagnostic (README.md, "Clock replay"; the lag check before the lag
# replay): the PC run again with the GBA trace's gGameClock set before each
# OnVBlank. With pc_replay.jsonl present the compare prints what it would
# match, as a diagnostic; it excuses nothing. Run after gba-trace.
pc-replay: pc
	@mkdir -p $(OUT)
	@GATE0_CLOCK_FROM=$(OUT)/gba.jsonl GATE0_KEYS=$(KEYS) GATE0_TICKS=$(TICKS) \
	GATE0_OUT=$(OUT)/pc_replay.jsonl GATE0_RAW=$(OUT)/pc_replay.raw \
	FE8PC_BLEND=mgba8 FE8PC_MGBA_BGCLIP=1 FE8PC_FRAMES=0 timeout $(PC_TIMEOUT) $(PC_BUILD)/fe8pc $(GBA_DIR)/baserom.gba > $(OUT)/pc_replay.log 2>&1; \
	status=$$?; if [ $$status -ne 0 ]; then \
	    echo "fe8pc (clock replay) stopped with status $$status, see $(OUT)/pc_replay.log"; tail -6 $(OUT)/pc_replay.log; \
	    [ "$(PC_FAULT_OK)" = 1 ] || exit 1; fi
	@grep "^\[gate0\]" $(OUT)/pc_replay.log || true

# Stage 5c, the affine replay (README.md, "Affine replay"; decision P2.2b): the
# PC run again, with each frame15 frame whose previous tick's bg2affin differs
# from the GBA's drawn with the GBA's bg2affin (read from gba.raw). Nothing the
# game reads changes. The compare masks a rotated-affine frame (P2.2b) only
# when this frame equals the GBA's. Run after gba-trace (same KEYS, TICKS and
# GATE0_SHOT_TICKS).
pc-affine-replay: pc
	@mkdir -p $(OUT)
	$(call STATE_RELINK,pcaff)
	@GATE0_AFFINE_FROM=$(OUT)/gba.raw GATE0_KEYS=$(KEYS) GATE0_TICKS=$(TICKS) \
	GATE0_OUT=$(OUT)/pc_affine.jsonl \
	FE8PC_BLEND=mgba8 FE8PC_MGBA_BGCLIP=1 FE8PC_FRAMES=0 timeout $(PC_TIMEOUT) $(PC_BUILD)/fe8pc $(GBA_DIR)/baserom.gba > $(OUT)/pc_affine.log 2>&1; \
	status=$$?; if [ $$status -ne 0 ]; then \
	    echo "fe8pc (affine replay) stopped with status $$status, see $(OUT)/pc_affine.log"; tail -6 $(OUT)/pc_affine.log; \
	    [ "$(PC_FAULT_OK)" = 1 ] || exit 1; fi
	@grep "^\[gate0\]" $(OUT)/pc_affine.log || true

# Stage 6. Rules and oracle-limit masks: README.md, "Rules and masks". With
# pc_lagreplay.jsonl present (stage 5b) the result includes the lag check.
# Exit 3 from the compare means every remaining difference is lag-only
# (reproduced by the lag replay at the GBA's positions) and no tick is
# replay-only: reported, not fatal here. NO_LAG_CHECK=1 leaves the replay out. With
# pc_affine.jsonl present (stage 5c) a rotated-affine frame can pass under
# P2.2b; without it every frame difference is reported. With pc_replay.jsonl
# present (make pc-replay) the clock replay is printed as a diagnostic.
# Each PC run's log goes with its trace (--pc-log, --lag-replay-log,
# --affine-replay-log): a run that logged a fault, or any compared trace
# with fewer than TICKS ticks, makes the verdict INCOMPLETE (exit 1). A
# replay is passed when its trace or its log is there, so a replay that
# stopped before writing its trace is not silently left out (the compare
# then stops with exit 2).
#
# Compares of COMPARE_LOCK_TICKS ticks or more (about 3.4 GB at 60,000) take
# turns across worktrees under COMPARE_LOCK (port-plan/DECISIONS.md, test
# throughput); emulator and PC runs do not lock. The lock is taken by
# port/tools/check_all.py lock, which uses an inherited lock as is, so an
# outer `flock ~/.fe8-compare.lock make ...` does not wait on itself.
# COMPARE_LOCK= (empty) turns it off.
NO_LAG_CHECK ?=
COMPARE_LOCK       ?= $(HOME)/.fe8-compare.lock
COMPARE_LOCK_TICKS ?= 60000
# A run resumed from a save state (GATE0_STATE_IN, below) compares only the
# ticks from its snapshot's tick on.
STATE_FROM     = $(if $(GATE0_STATE_IN),$(shell sed -n 's/.*"tick":\([0-9]*\).*/\1/p' \
                   $(firstword $(wildcard $(GATE0_STATE_IN)/*/meta.json)) 2>/dev/null),0)
COMPARE_LOCKED = $(if $(COMPARE_LOCK),$(if $(shell [ $$(( $(TICKS) - $(or $(STATE_FROM),0) )) -ge $(COMPARE_LOCK_TICKS) ] \
                   && echo y),$(PYTHON) port/tools/check_all.py lock $(COMPARE_LOCK) --))
compare:
	@replay=""; if [ "$(NO_LAG_CHECK)" != 1 ] && [ -f $(OUT)/pc_lagreplay.jsonl -o -f $(OUT)/pc_lagreplay.log ]; then \
	    replay="--lag-replay $(OUT)/pc_lagreplay.jsonl --lag-replay-raw $(OUT)/pc_lagreplay.raw \
	            --lag-replay-log $(OUT)/pc_lagreplay.log"; fi; \
	clock=""; if [ -f $(OUT)/pc_replay.jsonl ]; then \
	    clock="--clock-replay $(OUT)/pc_replay.jsonl --clock-replay-raw $(OUT)/pc_replay.raw"; fi; \
	affine=""; if [ -f $(OUT)/pc_affine.jsonl -o -f $(OUT)/pc_affine.log ]; then \
	    affine="--affine-replay $(OUT)/pc_affine.jsonl --affine-replay-log $(OUT)/pc_affine.log"; fi; \
	$(COMPARE_LOCKED) $(PYTHON) port/test/tools/gate0_compare.py --gba $(OUT)/gba.jsonl --pc $(OUT)/pc.jsonl \
	    --pc-log $(OUT)/pc.log --gba-raw $(OUT)/gba.raw --pc-raw $(OUT)/pc.raw \
	    --gba-elf $(GBA_DIR)/fireemblem8.elf --pc-elf $(PC_BUILD)/fe8pc --ticks $(TICKS) \
	    --masks $(MASKS) $$replay $$clock $$affine --json $(OUT)/compare.json; \
	status=$$?; if [ $$status -eq 3 ]; then echo "Gate 0: lag-only differences (exit 3), not fatal"; \
	elif [ $$status -ne 0 ]; then exit $$status; fi

# In order, also under make -j. The old replays and their logs are removed
# first so a stale one is never used (the clock replay is a diagnostic: not
# run here).
STALE_REPLAYS = $(OUT)/pc_lagreplay.jsonl $(OUT)/pc_lagreplay.raw $(OUT)/pc_lagreplay.log \
                $(OUT)/pc_replay.jsonl $(OUT)/pc_replay.raw $(OUT)/pc_replay.log \
                $(OUT)/pc_affine.jsonl $(OUT)/pc_affine.log
gate0:
	$(MAKE) -f port/test/Makefile boot-check
	@rm -f $(STALE_REPLAYS)
	$(MAKE) -f port/test/Makefile gba-trace
	$(MAKE) -f port/test/Makefile pc-trace
	$(MAKE) -f port/test/Makefile pc-lag-replay
	$(MAKE) -f port/test/Makefile pc-affine-replay
	$(MAKE) -f port/test/Makefile compare

# The scripted route (README.md, "Scripted route"): A on ticks 4,301 and 4,302,
# then every 90 ticks, the FE8PC_AUTOPRESS=0x001,90,4300 run in tick terms.
# scripted_a90.keys ends at tick 40,031; scripted_a90_long.keys is the same
# pattern to tick 61,001 (its first 795 entries are the same). SCRIPTED_TICKS
# is the GBA length; the PC stops at its first fault.
SCRIPTED_KEYS  ?= port/test/inputs/scripted_a90_long.keys
SCRIPTED_TICKS ?= 60000
scripted:
	@rm -f $(STALE_REPLAYS)
	$(MAKE) -f port/test/Makefile gba-trace KEYS=$(SCRIPTED_KEYS) TICKS=$(SCRIPTED_TICKS)
	$(MAKE) -f port/test/Makefile pc-trace KEYS=$(SCRIPTED_KEYS) TICKS=$(SCRIPTED_TICKS) PC_FAULT_OK=1
	$(MAKE) -f port/test/Makefile pc-lag-replay KEYS=$(SCRIPTED_KEYS) TICKS=$(SCRIPTED_TICKS) PC_FAULT_OK=1
	$(MAKE) -f port/test/Makefile pc-affine-replay KEYS=$(SCRIPTED_KEYS) TICKS=$(SCRIPTED_TICKS) PC_FAULT_OK=1
	$(MAKE) -f port/test/Makefile compare KEYS=$(SCRIPTED_KEYS) TICKS=$(SCRIPTED_TICKS)

# A bot route (inputs/README-routes.md) through the lag replay: the GBA
# capture, the PC run with the GBA's lag VBlanks where they came (the harness
# run, stage 5b), and that run compared with the GBA on its own (outright
# clock, the GBA's lag positions and events on every tick). The plain lag-free
# PC cannot follow the routes: the GBA's lag shifts the windows the keys were
# recorded for (the NULL probe found the PC leaving every route at tick
# 5,666), and the lag replay keeps them. port/check-all.mk's route rows run
# this. Exit 1 unless the lag replay reaches every tick with no fault in its
# log and matches the GBA on every tick (INCOMPLETE when it stops early).
ROUTE_KEYS  ?= port/test/inputs/route_ch0_prologue.keys
ROUTE_TICKS ?= 5803
# A cheat-assisted key file (its "# Generated" line names ROUTE_CHEAT) runs with
# its cheat on both sides; GATE0_CHEAT, when set, is used as it is (README.md,
# "Test cheats"). Empty: the run is the plain one, its command lines unchanged.
ROUTE_CHEAT_RUN = $(or $(GATE0_CHEAT),$(call KEYS_CHEAT,$(ROUTE_KEYS)))
# The end SRAM (README.md, "Save checks", "End SRAM of a route"; port-savechk2, the user's SAVE-1 B
# of 2026-10-30): both sides also write the SRAM at the route's last tick (GATE0_SRAM: gba_end.sav
# from the GBA capture, pc_end.sav from the PC lag replay, the run that is compared), and route-sram
# compares the two with tools/sram_blockdiff.py --cross: the bytes known to differ between the two
# machines (the play clock, its checksums, the uninitialised bytes of P-12/P-18) are named and not
# counted. It prints one "SRAM CROSS:" line, kept with the full report in sram_cross.txt, and
# `route` exits 1 after the Gate 0 compare when any other byte differs. Every chapter-end and
# suspend save a route makes on the way is in that SRAM (the last one of each block). check-all's
# route and path rows report the line (check_all.py --gate0-sram). ROUTE_SRAM=0 leaves it out.
ROUTE_SRAM     ?= 1
ROUTE_SRAM_ENV  = $(if $(filter 1,$(ROUTE_SRAM)),GATE0_SRAM=$(OUT)/$(1)_end.sav )
ROUTE_SRAM_OUT  = $(OUT)/gba_end.sav $(OUT)/pc_end.sav $(OUT)/sram_cross.txt
.PHONY: route route-sram compare-lag-replay
route:
	@rm -f $(STALE_REPLAYS) $(OUT)/pc.jsonl $(OUT)/pc.raw $(OUT)/pc.log $(OUT)/compare.json $(ROUTE_SRAM_OUT)
	$(if $(ROUTE_CHEAT_RUN),@echo "route: $(ROUTE_KEYS): cheat-assisted run (test coverage only): GATE0_CHEAT=$(ROUTE_CHEAT_RUN) on both sides")
	$(call ROUTE_SRAM_ENV,gba)$(MAKE) -f port/test/Makefile gba-trace KEYS=$(ROUTE_KEYS) TICKS=$(ROUTE_TICKS)$(if $(ROUTE_CHEAT_RUN), GATE0_CHEAT=$(ROUTE_CHEAT_RUN))
	$(call ROUTE_SRAM_ENV,pc)$(MAKE) -f port/test/Makefile pc-lag-replay KEYS=$(ROUTE_KEYS) TICKS=$(ROUTE_TICKS) PC_FAULT_OK=1$(if $(ROUTE_CHEAT_RUN), GATE0_CHEAT=$(ROUTE_CHEAT_RUN))
	$(if $(filter 1,$(ROUTE_SRAM)),@$(MAKE) -s --no-print-directory -f port/test/Makefile route-sram)
	$(MAKE) -f port/test/Makefile compare-lag-replay KEYS=$(ROUTE_KEYS) TICKS=$(ROUTE_TICKS)
	$(if $(filter 1,$(ROUTE_SRAM)),@! grep -q '^SRAM CROSS: DIFFERENT' $(OUT)/sram_cross.txt \
	    || { echo "route: the end SRAM differs from the GBA's in bytes that are not known ones ($(OUT)/sram_cross.txt)"; exit 1; })

# The end SRAM of a route against the GBA's (above); also by hand after any run that wrote both.
route-sram:
	@if [ ! -f $(OUT)/gba_end.sav ]; then \
	  echo "SRAM CROSS: not compared (no $(OUT)/gba_end.sav: the GBA capture did not reach its last tick)"; \
	elif [ ! -f $(OUT)/pc_end.sav ]; then \
	  echo "SRAM CROSS: not compared (no $(OUT)/pc_end.sav: the PC lag replay did not reach its last tick)"; \
	else $(PYTHON) port/test/tools/sram_blockdiff.py --cross $(OUT)/gba_end.sav $(OUT)/pc_end.sav; \
	  $(PYTHON) port/test/tools/sram_blockdiff.py --cross --line $(OUT)/gba_end.sav $(OUT)/pc_end.sav; fi \
	  > $(OUT)/sram_cross.txt; tail -n 1 $(OUT)/sram_cross.txt

compare-lag-replay:
	$(COMPARE_LOCKED) $(PYTHON) port/test/tools/gate0_compare.py --gba $(OUT)/gba.jsonl --pc $(OUT)/pc_lagreplay.jsonl \
	    --pc-log $(OUT)/pc_lagreplay.log --gba-raw $(OUT)/gba.raw --pc-raw $(OUT)/pc_lagreplay.raw \
	    --gba-elf $(GBA_DIR)/fireemblem8.elf --pc-elf $(PC_BUILD)/fe8pc --ticks $(TICKS) \
	    --masks $(MASKS) --json $(OUT)/compare.json

# Chained bot routes (inputs/README-routes.md, "Routes from chapter saves"):
# route_ch5x, route_ch6, route_ch7, route_ch8 and route_e9 to route_e19 each
# start from the game save that ends the route before them (Continue on the
# title menu), not from power-on. route_e9 to route_e19 are Eirika's route
# (the bot's first choice at the route split, after Chapter 8); route_ch8i
# (Chapter 8 again, then the split's second entry) and route_i9 to route_i20
# are Ephraim's.
#
#   make -f port/test/Makefile chain-route CHAIN=ch5x|ch6|ch7|ch8|e9|e10|e11|e12|e13|e14|e15|e16|e17|e18|e19
#   make -f port/test/Makefile chain-route CHAIN=e20|e21|e22|eend   # the real army, tactics E7 (README-routes.md)
#   make -f port/test/Makefile chain-route CHAIN=b20|b21|b22|bend|bpost   # boosted starts (README-routes.md)
#   make -f port/test/Makefile chain-route CHAIN=ch8i|i9|i10|i11|i12|i13|i14|i15|i16|i17|i18|i19|i20|i21
#   make -f port/test/Makefile chain-route CHAIN=ib22|ibend|ibpost   # boosted part 2 on Ephraim's route
#   make -f port/test/Makefile chain-route CHAIN=i22|iend   # part 2 and his ending with the real army (coverage 12)
#
# makes the route's start save in port/test/saves/ when it is not there
# (chain-save: sram-capture from the route before, back to route_ch5.keys from
# a blank save), checks that the save is the one the key file was made from
# (the FNV-1a of its first 32 KiB against the key file's "# Start:" line), and
# runs `route` from it. Saves are game data: port/test/saves/ keeps them out of
# git. CHAIN_SAVES lists each save as NAME:KEYS:AT:FROM (AT is save:N, tick:T or
# boost:B-T, FROM - = a blank save); chain-save CHAIN_SAVE=e20_start makes the
# end save of route_e19 too (Chapter 20 on Eirika's route, chapterIndex 20), and
# CHAIN_SAVE=i21_start the end save of route_i20 (the final chapter on Ephraim's
# route, chapterIndex 34).
# boost:B-T is a boosted start (README-routes.md, "Boosted starts", test
# coverage only): sram_boost.lua plays KEYS, raises the blue units' stats to
# their class caps and their items' uses at the end of tick B, and writes the
# SRAM at tick:T. route_b21 cannot be checked past tick 31,795 on the PC yet
# (MB1, the Crimson Eye stub), nor route_b22 past 3,135 (the Demon King's
# summon); route_bend (Continue on the clear save) covers the ending.
# Coverage 11 (README-routes.md, "The post-game floors" and "Feature routes"):
#   make -f port/test/Makefile chain-route CHAIN=bt1|bt5|bt6|bt7|bt8          # the Tower of Valni (boosted)
#   make -f port/test/Makefile chain-route CHAIN=br2|br3|...|br9               # the Lagdou Ruins (boosted, refitted)
#   make -f port/test/Makefile chain-route CHAIN=br10   # Ruins floor 10 (boosted, refitted, cheat-assisted)
#   (br10_start, the end of route_br9, is floor 10's start; br10r_start its refit, boost_r10, coverage 12)
#   make -f port/test/Makefile chain-route CHAIN=f_arena_ch5|f_support_e12|f_chest_ch8|f_suspend_ch6|f_promo_ch6
#   make -f port/test/Makefile chain-route CHAIN=sk4|sk10|sk15|sk17           # world-map skirmishes
#   make -f port/test/Makefile chain-route CHAIN=gallery   # the item gallery (boosted, edited, cheat-assisted, coverage 12)
# A boost:B-T entry also takes the settings in port/test/inputs/KEYS.boost when
# that file is there (BOOST_LEVEL, BOOST_ITEMS: sram_boost.lua): boost_ch6
# (f_promo_start, Ross at level 9), boost_r2 (br2r_start, the Ruins refit) and
# boost_r10 (br10r_start, the same refit on floor 10, coverage 12).
# A cheat-assisted route (README-routes.md, "Cheat-assisted routes") runs with its
# cheat on both sides (route, ROUTE_CHEAT_RUN), and each save chain-save makes is
# played with the cheat of its own KEYS (that file's "# Generated" line; none for
# the rest, whatever GATE0_CHEAT is), so a save after a cheat-assisted route is
# that route's end and every other save stays the plain one.
CHAIN            ?=
CHAIN_ROUTES     := ch5x ch6 ch7 ch8 e9 e10 e11 e12 e13 e14 e15 e16 e17 e18 e19 e20 e21 e22 eend b20 b21 b22 bend bpost \
                    ch8i i9 i10 i11 i12 i13 i14 i15 i16 i17 i18 i19 i20 i21 ib22 ibend ibpost i22 iend \
                    bt1 bt5 bt6 bt7 bt8 br2 br3 br4 br5 br6 br7 br8 br9 br10 \
                    f_arena_ch5 f_support_e12 f_chest_ch8 f_suspend_ch6 f_promo_ch6 sk4 sk10 sk15 sk17 gallery
CHAIN_START_bt1  := b_clear
CHAIN_START_bt5  := bt5_start
CHAIN_START_bt6  := bt6_start
CHAIN_START_bt7  := bt7_start
CHAIN_START_bt8  := bt8_start
CHAIN_START_br2  := br2r_start
CHAIN_START_br3  := br3_start
CHAIN_START_br4  := br4_start
CHAIN_START_br5  := br5_start
CHAIN_START_br6  := br6_start
CHAIN_START_br7  := br7_start
CHAIN_START_br8  := br8_start
CHAIN_START_br9  := br9_start
CHAIN_START_br10 := br10r_start
# Coverage 12's item gallery (inputs/gallery/README.md: boosted, edited, cheat-assisted, test coverage only):
# route_gallery from gal_start (i21_start edited by gallery/gal_prep.edit at tick 1513 of gallery/gal_prep.keys).
# Every frame: chain-route CHAIN=gallery GATE0_SHOT_TICKS=0-<its ticks less one>.
CHAIN_START_gallery := gal_start
CHAIN_START_f_arena_ch5   := ch5_start
CHAIN_START_f_support_e12 := e12_start
CHAIN_START_f_chest_ch8   := ch8_start
CHAIN_START_f_suspend_ch6 := ch6_start
CHAIN_START_f_promo_ch6   := f_promo_start
CHAIN_START_sk4  := e10_start
CHAIN_START_sk10 := e14_start
CHAIN_START_sk15 := i13_start
CHAIN_START_sk17 := e16_start
# Coverage 12 (port-c12-skirmish; README-routes.md "World-map skirmishes, coverage 12"): the nodes
# not played before, each from a story save with monsters on it:
#   make -f port/test/Makefile chain-route CHAIN=sk6|sk12|sk17i|sk21|sk28   # sk28: Melkaen Coast (0x39)
CHAIN_ROUTES     += sk6 sk12 sk17i sk21 sk28
CHAIN_START_sk6   := e11_start
CHAIN_START_sk12  := i17_start
CHAIN_START_sk17i := i18_start
CHAIN_START_sk21  := e19_start
CHAIN_START_sk28  := e20_start
CHAIN_START_ch5x := ch5x_start
CHAIN_START_ch6  := ch6_start
CHAIN_START_ch7  := ch7_start
CHAIN_START_ch8  := ch8_start
CHAIN_START_e9   := e9_start
CHAIN_START_e10  := e10_start
CHAIN_START_e11  := e11_start
CHAIN_START_e12  := e12_start
CHAIN_START_e13  := e13_start
CHAIN_START_e14  := e14_start
CHAIN_START_e15  := e15_start
CHAIN_START_e16  := e16_start
CHAIN_START_e17  := e17_start
CHAIN_START_e18  := e18_start
CHAIN_START_e19  := e19_start
# Coverage 11 (port-bots11e): route_e20 (Darkling Woods, index 20) with the real
# army (ROUTE_E7=1, tactics E7); e21_start starts the final chapter (index 21).
# route_e21 (the final chapter's part 1) from e21_start; route_e22 (part 2, then the ending) from
# e22_start (the turn-1 suspend save of part 2: Resume Chapter); route_eend (Continue on the clear
# save: the ending again, the credits, Fin) from e_clear.
CHAIN_START_e20  := e20_start
CHAIN_START_e21  := e21_start
CHAIN_START_e22  := e22_start
CHAIN_START_eend := e_clear
CHAIN_START_b20  := b20_start
CHAIN_START_b21  := b21_start
CHAIN_START_b22  := b22_start
CHAIN_START_bend := b_clear
CHAIN_START_bpost := b_clear
# Ephraim's route (the split's second entry): route_ch8i is Chapter 8 again from
# ch8_start, then Ephraim at the split; route_i9 and route_i10 are his Chapters 9
# and 10 (chapterIndex 23 and 24); i11_start starts his Chapter 11 (index 62).
CHAIN_START_ch8i := ch8_start
CHAIN_START_i9   := i9_start
CHAIN_START_i10  := i10_start
# Step I2: route_i11 (Phantom Ship, index 62), route_i12 (Landing at Taizel,
# index 25) and route_i13 (Fluorspar's Oath, index 26); i14_start starts his
# Chapter 14 (index 27).
CHAIN_START_i11  := i11_start
CHAIN_START_i12  := i12_start
CHAIN_START_i13  := i13_start
# Step I3: route_i14 (Father and Son, index 27), route_i15 (Scorched Sand,
# index 28) and route_i16 (Ruled by Madness, index 29); i17_start starts
# Chapter 17 on his route (River of Regrets, index 30).
CHAIN_START_i14  := i14_start
CHAIN_START_i15  := i15_start
CHAIN_START_i16  := i16_start
# Step I4: route_i17 (River of Regrets, index 30), route_i18 (Two Faces of
# Evil, index 31) and route_i19 (Last Hope, index 32); i20_start starts Chapter
# 20 on his route (Darkling Woods, index 33).
CHAIN_START_i17  := i17_start
CHAIN_START_i18  := i18_start
CHAIN_START_i19  := i19_start
# Step I5: route_i20 (Darkling Woods, index 33); i21_start starts the final
# chapter on his route (Sacred Stone, part 1, index 34).
CHAIN_START_i20  := i20_start
# Coverage 11 (port-bots11i): route_i21 (the final chapter's part 1, index 34, won with the real
# army); its end SRAM i22_start (part 2's turn-1 suspend). Part 2 is played from a BOOSTED start
# (ib22_start: route_i21 with sram_boost.lua at its tick 80,205, after part 2's map loads and before
# its turn-1 suspend save; test coverage only, README-routes.md "Boosted starts"): route_ib22 (part 2,
# the ending, the credits, the title), its clear save ib_clear, route_ibend (Continue on it: the
# ending again) and route_ibpost (the post-game: Tower of Valni and Lagdou Ruins floor 1).
CHAIN_START_i21  := i21_start
CHAIN_START_ib22 := ib22_start
CHAIN_START_ibend := ib_clear
CHAIN_START_ibpost := ib_clear
# Coverage 12 (port-c12-iend-real): part 2 with the REAL army after all (ROUTE_E7=1, "Tactics E7 on
# Ephraim's part 2" in README-routes.md): route_i22 from the real i22_start (part 2 won on turn 21,
# then the clear save, the ending, the credits, the title); its end SRAM i_clear (the real clear
# save); route_iend (Continue on it: the ending again, the credits, Fin, the title).
CHAIN_START_i22  := i22_start
CHAIN_START_iend := i_clear
CHAIN_SAVES      := ch5x_start:route_ch5:save:5:- ch6_start:route_ch5x:save:7:ch5x_start \
                    ch7_start:route_ch6:save:8:ch6_start ch8_start:route_ch7:save:9:ch7_start \
                    e9_start:route_ch8:save:10:ch8_start e10_start:route_e9:save:11:e9_start \
                    e11_start:route_e10:save:61:e10_start e12_start:route_e11:save:12:e11_start \
                    e13_start:route_e12:save:13:e12_start e14_start:route_e13:save:14:e13_start \
                    e15_start:route_e14:save:15:e14_start e16_start:route_e15:save:16:e15_start \
                    e17_start:route_e16:save:17:e16_start \
                    e18_start:route_e17:save:18:e17_start \
                    e19_start:route_e18:save:19:e18_start \
                    e20_start:route_e19:save:20:e19_start \
                    e21_start:route_e20:save:21:e20_start \
                    e22_start:route_e21:tick:60394:e21_start \
                    e_clear:route_e22:tick:67933:e22_start \
                    b20_start:boost_e20:boost:1900-2721:e20_start \
                    b21_start:route_b20:save:21:b20_start \
                    b22_start:route_b21:tick:123230:b21_start \
                    b_clear:route_b21:tick:170079:b21_start \
                    i9_start:route_ch8i:save:23:ch8_start i10_start:route_i9:save:24:i9_start \
                    i11_start:route_i10:save:62:i10_start i12_start:route_i11:save:25:i11_start \
                    i13_start:route_i12:save:26:i12_start i14_start:route_i13:save:27:i13_start \
                    i15_start:route_i14:save:28:i14_start i16_start:route_i15:save:29:i15_start \
                    i17_start:route_i16:save:30:i16_start i18_start:route_i17:save:31:i17_start \
                    i19_start:route_i18:save:32:i18_start i20_start:route_i19:save:33:i19_start \
                    i21_start:route_i20:save:34:i20_start \
                    ch2_start:route_ch1:save:2:- \
                    ch2g_start:monkey/ch2_start_s1:tick:7340:ch2_start \
                    ch8r_start:route_ch8:tick:72800:ch8_start \
                    pe14_start:paths/pe14_prep:edit:1726-1990:e14_start \
                    pc7b_start:paths/pc7b_prep:edit:1640-1900:ch7_start \
                    ps14_start:paths/ps14_prep:edit:1726-1990:e14_start \
                    pd14_start:paths/pd14_prep:edit:1726-1990:e14_start \
                    i22_start:route_i21:tick:80292:i21_start \
                    ib22_start:route_i21:boost:80205-80292:i21_start \
                    ib_clear:route_ib22:tick:59906:ib22_start \
                    i_clear:route_i22:tick:65624:i22_start \
                    ch5_start:route_ch4:save:6:- b_r2_start:route_bpost:tick:116095:b_clear \
                    bt5_start:route_bt1:tick:118458:b_clear bt6_start:route_bt5:tick:61654:bt5_start \
                    bt7_start:route_bt6:tick:63020:bt6_start bt8_start:route_bt7:tick:55743:bt7_start \
                    br2r_start:boost_r2:boost:735-936:b_r2_start br3_start:route_br2:tick:64304:br2r_start \
                    br4_start:route_br3:tick:107697:br3_start br5_start:route_br4:tick:66193:br4_start \
                    br6_start:route_br5:tick:54229:br5_start br7_start:route_br6:tick:87737:br6_start \
                    br8_start:route_br7:tick:98493:br7_start br9_start:route_br8:tick:90018:br8_start \
                    br10_start:route_br9:tick:97417:br9_start br10r_start:boost_r10:boost:735-936:br10_start \
                    f_promo_start:boost_ch6:boost:800-1669:ch6_start \
                    gal_start:gallery/gal_prep:edit:1513-1700:i21_start
# Coverage 12 (port-c12-postgame-real; README-routes.md, "The post-game with the real army"): the
# post-game with the REAL army of e_clear (Eirika's clear save, no boost; the bot's ROUTE_PG_REAL=1):
# route_et1 (the title's Extras, Map, the world map's shopping, the Tower of Valni floor 1) to
# route_et8 (floor 8, the dungeon record, the world map); er1_start is the SRAM at route_et8's end
# (the game save after the Tower).
#   make -f port/test/Makefile chain-route CHAIN=et1|et2|et3|et4|et5|et6|et7|et8
CHAIN_ROUTES     += et1 et2 et3 et4 et5 et6 et7 et8
CHAIN_START_et1  := e_clear
CHAIN_START_et2  := et2_start
CHAIN_START_et3  := et3_start
CHAIN_START_et4  := et4_start
CHAIN_START_et5  := et5_start
CHAIN_START_et6  := et6_start
CHAIN_START_et7  := et7_start
CHAIN_START_et8  := et8_start
CHAIN_SAVES      += et2_start:route_et1:tick:26429:e_clear et3_start:route_et2:tick:46582:et2_start \
                    et4_start:route_et3:tick:36542:et3_start et5_start:route_et4:tick:23413:et4_start \
                    et6_start:route_et5:tick:64847:et5_start et7_start:route_et6:tick:71178:et6_start \
                    et8_start:route_et7:tick:59936:et7_start er1_start:route_et8:tick:60388:et8_start
# Lagdou Ruins with the same army (route_er1: the world map's shopping at node 9, the Ruins at node 27, floor 1;
# each next file from the turn-1 suspend save the one before ends on):
# route_er4 (floor 4) is not won: it stops at turn 99 (README-routes.md).
#   make -f port/test/Makefile chain-route CHAIN=er1|er2|er3|er4
CHAIN_ROUTES     += er1 er2 er3 er4
CHAIN_START_er1  := er1_start
CHAIN_START_er2  := er2_start
CHAIN_START_er3  := er3_start
CHAIN_START_er4  := er4_start
CHAIN_SAVES      += er2_start:route_er1:tick:88602:er1_start er3_start:route_er2:tick:94878:er2_start \
                    er4_start:route_er3:tick:136724:er3_start
# Coverage 12 (port-c12-features; README-routes.md, "Feature routes (coverage 12)"): houses and villages,
# a door and a chest key, two arenas, the post-game Tower's supports, Chapter 16's Lord promotion, the
# secret shop, a trainee's third tier and a promotion choice, from story saves, the clear saves and two
# edited starts (test coverage only: f_mc_start, Rennac and L'Arachel moved by the secret shop of Eirika's
# Chapter 14; f_tt_start, Ross a level-10 Fighter with a Master Seal at Chapter 6's start; their edits are
# port/test/inputs/features/*.edit, CHAIN_EDITS below). ch1_start and ch4_start are the game saves the
# Prologue and Chapter 3 end on (route_ch0_prologue and route_ch3 are prefixes of route_ch5).
#   make -f port/test/Makefile chain-route CHAIN=f_house_ch1|f_village_ch2|...|f_promochoice_i14
CHAIN_ROUTES     += f_house_ch1 f_village_ch2 f_village_ch4 f_village_ch5 f_doorkey_ch8 f_chestkey_ch8 f_arena_e10 f_arena_i12 f_supports_tower_e f_supports_tower_i f_lordpromo_e16 f_secretshop_e14 f_thirdtier_ch6 f_promochoice_i14
CHAIN_START_f_house_ch1        := ch1_start
CHAIN_START_f_village_ch2      := ch2_start
CHAIN_START_f_village_ch4      := ch4_start
CHAIN_START_f_village_ch5      := ch5_start
CHAIN_START_f_doorkey_ch8      := ch8_start
CHAIN_START_f_chestkey_ch8     := ch8_start
CHAIN_START_f_arena_e10        := e10_start
CHAIN_START_f_arena_i12        := i12_start
CHAIN_START_f_supports_tower_e := e_clear
CHAIN_START_f_supports_tower_i := ib_clear
CHAIN_START_f_lordpromo_e16    := e16_start
CHAIN_START_f_secretshop_e14   := f_mc_start
CHAIN_START_f_thirdtier_ch6    := f_tt_start
CHAIN_START_f_promochoice_i14  := i14_start
CHAIN_SAVES      += ch1_start:route_ch0_prologue:save:1:- ch4_start:route_ch3:save:4:- \
                    f_mc_start:route_e14:edit:1600-1725:e14_start f_tt_start:boost_ch6:edit:800-1669:ch6_start
# An edit:B-T entry's edit file is port/test/inputs/KEYS.edit, or the file CHAIN_EDITS names for that save.
CHAIN_EDITS      := f_mc_start:features/mc_e14.edit f_tt_start:features/tt_ch6.edit
CHAIN_SAVE       ?=
.PHONY: chain-save chain-route
# edit:B-T (round 10 "paths", inputs/paths/README.md, test coverage only): sram_boost.lua plays KEYS,
# applies port/test/inputs/KEYS.edit (BOOST_EDIT_FILE: single fields of single units, the RN state)
# at the end of tick B with the stats left alone (BOOST_STATS=0), and writes the SRAM at tick:T.
chain-save: $(OUT)/mgba_lua_run
	@[ -n "$(CHAIN_SAVE)" ] || { echo "usage: make -f port/test/Makefile chain-save CHAIN_SAVE=NAME" \
	    "(one of: $(foreach e,$(CHAIN_SAVES),$(firstword $(subst :, ,$(e)))))"; exit 2; }
	@set -e; lookup() { for e in $(CHAIN_SAVES); do case $$e in "$$1":*) echo $$e; return;; esac; done; }; \
	editfile() { for e in $(CHAIN_EDITS); do case $$e in "$$1":*) echo port/test/inputs/$${e#*:}; return;; esac; done; \
	  echo port/test/inputs/$$2.edit; }; \
	need=$(CHAIN_SAVE); todo=""; \
	while [ "$$need" != "-" ] && [ ! -f $(SAVES)/$$need.sav ]; do \
	  e=$$(lookup $$need); [ -n "$$e" ] || { echo "chain-save: no rule for $$need"; exit 2; }; \
	  todo="$$e $$todo"; need=$${e##*:}; done; \
	for e in $$todo; do \
	  name=$${e%%:*}; r=$${e#*:}; keys=$${r%%:*}; r=$${r#*:}; at=$${r%:*}; from=$${r##*:}; \
	  sav=""; [ "$$from" = "-" ] || sav=$(SAVES)/$$from.sav; \
	  cheat=$$(sed -n 's/^# Generated.*ROUTE_CHEAT=\([a-z+]*\).*/\1/p' port/test/inputs/$$keys.keys); \
	  echo "chain-save: $(SAVES)/$$name.sav from $$keys.keys, $$at$${sav:+, booted from $$sav}$${cheat:+, cheat-assisted (GATE0_CHEAT=$$cheat)}"; \
	  case $$at in \
	    boost:*) b=$${at#boost:}; extra=""; [ ! -f port/test/inputs/$$keys.boost ] || extra=$$(cat port/test/inputs/$$keys.boost); \
	      env $$extra GATE0_CHEAT=$$cheat MGBA_RUN_BP=$(GBA_BP) GATE0_SRAM_IN=$$sav GATE0_KEYS=port/test/inputs/$$keys.keys BOOST_AT=$${b%-*} BOOST_USES=1 \
	        SRAM_AT=tick:$${b#*-} SRAM_OUT=$(SAVES)/$$name.sav \
	        $(OUT)/mgba_lua_run $(GBA_ROM) port/test/oracle/sram_boost.lua $(GBA_MAX_FRAMES); \
	      $(PYTHON) port/test/tools/sram_info.py $(SAVES)/$$name.sav;; \
	    edit:*) b=$${at#edit:}; \
	      GATE0_CHEAT=$$cheat MGBA_RUN_BP=$(GBA_BP) GATE0_SRAM_IN=$$sav GATE0_KEYS=port/test/inputs/$$keys.keys BOOST_AT=$${b%-*} BOOST_STATS=0 \
	        BOOST_EDIT_FILE=$$(editfile $$name $$keys) SRAM_AT=tick:$${b#*-} SRAM_OUT=$(SAVES)/$$name.sav \
	        $(OUT)/mgba_lua_run $(GBA_ROM) port/test/oracle/sram_boost.lua $(GBA_MAX_FRAMES); \
	      $(PYTHON) port/test/tools/sram_info.py $(SAVES)/$$name.sav;; \
	    *) $(MAKE) -f port/test/Makefile sram-capture SAVE_KEYS=port/test/inputs/$$keys.keys SAVE_AT=$$at \
	      SAVE_NAME=$$name GATE0_SRAM_IN=$$sav GATE0_CHEAT=$$cheat;; \
	  esac; \
	done; \
	echo "chain-save: $(SAVES)/$(CHAIN_SAVE).sav is there"

chain-route:
	@case " $(CHAIN_ROUTES) " in *" $(CHAIN) "*) ;; \
	  *) echo "usage: make -f port/test/Makefile chain-route CHAIN=NAME (one of: $(CHAIN_ROUTES))"; exit 2;; esac
	$(MAKE) -f port/test/Makefile chain-save CHAIN_SAVE=$(CHAIN_START_$(CHAIN))
	@keys=port/test/inputs/route_$(CHAIN).keys; sav=$(SAVES)/$(CHAIN_START_$(CHAIN)).sav; \
	want=$$(sed -n 's/^# Start: .*FNV-1a \([0-9a-f]\{8\}\).*/\1/p' $$keys); \
	got=$$($(PYTHON) port/test/tools/sram_info.py $$sav | sed -n '1s/.*fnv1a \([0-9a-f]\{8\}\).*/\1/p'); \
	if [ -z "$$want" ] || [ "$$want" != "$$got" ]; then \
	  echo "chain-route: $$sav (fnv1a $$got) is not the save $$keys was made from (fnv1a $$want)"; exit 1; fi; \
	echo "chain-route: $$keys from $$sav (fnv1a $$got, as its header says)"
	$(MAKE) -f port/test/Makefile route ROUTE_KEYS=port/test/inputs/route_$(CHAIN).keys \
	    ROUTE_TICKS=$$($(PYTHON) port/tools/check_all.py keyticks port/test/inputs/route_$(CHAIN).keys) \
	    GATE0_SRAM_IN=$(SAVES)/$(CHAIN_START_$(CHAIN)).sav

# Round 10 "paths" (port/test/inputs/paths/README.md): short hand-made key files that reach a code path no
# bot route reaches (a Bishop on the UI screens, the staff target selections, a Devil Axe backfire on a
# wall, the ballista back-out, the one-unit heal with its target on the right), each from its start save
# (chain-save makes it: a chapter save, a save taken mid-route, or an edited start) and through the lag
# replay, every frame of PATH_SHOTS_<name> compared too.
#
#   make -f port/test/Makefile path-route PATH_NAME=pe14|pc7b|ch2g|ch8r|i14b|ps14|pd14
#
# Integration 11 "nullunit2" added ps14 (a Summon phantom killed by the counter to its own attack, the
# audit's N2) and pd14 (a Devil Axe backfire on a wall by a unit with US_DROP_ITEM: BATTLE_HandleItemDrop's
# NULL unitB), both edited starts like pe14.
PATH_ROUTES     := pe14 pc7b ch2g ch8r i14b ps14 pd14
PATH_START_pe14 := pe14_start
PATH_START_pc7b := pc7b_start
PATH_START_ch2g := ch2g_start
PATH_START_ch8r := ch8r_start
PATH_START_i14b := i14_start
PATH_START_ps14 := ps14_start
PATH_START_pd14 := pd14_start
PATH_SHOTS_pe14 := 2000-6253
PATH_SHOTS_pc7b := 2000-3112
PATH_SHOTS_ch2g := 1700-3499
PATH_SHOTS_ch8r := 1700-3499
PATH_SHOTS_i14b := 1700-2199
PATH_SHOTS_ps14 := 2000-3568
PATH_SHOTS_pd14 := 2000-3507
PATH_NAME       ?=
.PHONY: path-route
path-route:
	@case " $(PATH_ROUTES) " in *" $(PATH_NAME) "*) ;; \
	  *) echo "usage: make -f port/test/Makefile path-route PATH_NAME=NAME (one of: $(PATH_ROUTES))"; exit 2;; esac
	$(MAKE) -f port/test/Makefile chain-save CHAIN_SAVE=$(PATH_START_$(PATH_NAME))
	@keys=port/test/inputs/paths/$(PATH_NAME).keys; sav=$(SAVES)/$(PATH_START_$(PATH_NAME)).sav; \
	want=$$(sed -n 's/^# Start: .*FNV-1a \([0-9a-f]\{8\}\).*/\1/p' $$keys); \
	got=$$($(PYTHON) port/test/tools/sram_info.py $$sav | sed -n '1s/.*fnv1a \([0-9a-f]\{8\}\).*/\1/p'); \
	if [ -z "$$want" ] || [ "$$want" != "$$got" ]; then \
	  echo "path-route: $$sav (fnv1a $$got) is not the save $$keys was made from (fnv1a $$want)"; exit 1; fi; \
	echo "path-route: $$keys from $$sav (fnv1a $$got, as its header says)"
	GATE0_SHOT_TICKS=$(PATH_SHOTS_$(PATH_NAME)) $(MAKE) -f port/test/Makefile route \
	    ROUTE_KEYS=port/test/inputs/paths/$(PATH_NAME).keys \
	    ROUTE_TICKS=$$($(PYTHON) port/tools/check_all.py keyticks port/test/inputs/paths/$(PATH_NAME).keys) \
	    GATE0_SRAM_IN=$(SAVES)/$(PATH_START_$(PATH_NAME)).sav

# Coverage 12 "scenes-e" (port/test/inputs/watch/README.md): Eirika's bot routes again with every story
# scene watched instead of skipped (route_bot.lua ROUTE_WATCH=fast, wf_*, and slow, ws_*), each from
# the start save of the route it repeats (power-on for wf_ch5/ws_ch5). Every frame of every scene is
# compared: a replay of the file (route_check.lua, ROUTE_SCENES_OUT) lists the scene ticks,
# tools/scene_shots.py makes them GATE0_SHOT_TICKS, and the route runs as `route` does (gba-trace,
# pc-lag-replay, compare-lag-replay), with the GBA's frames hashed first in WATCH_JOBS processes
# (tools/frame_prehash.py: the compare's own hash, so the result is the same). The replay takes the
# file's own tick rule (a "# Generated" line without ROUTE_TICK=capture: the frame rule, as wf_e10t and
# ws_e10t, whose prefix is route_e10.keys).
#
#   make -f port/test/Makefile watch-route WATCH=wf_ch5|ws_ch5|wf_ch5x|ws_ch5x|...|wf_eend|ws_eend
WATCH      ?=
WATCH_JOBS ?= 4
.PHONY: watch-route
watch-route: $(OUT)/mgba_lua_run
	@[ -n "$(WATCH)" ] && [ -f port/test/inputs/watch/$(WATCH).keys ] || { echo "usage: make -f port/test/Makefile" \
	    "watch-route WATCH=NAME (port/test/inputs/watch/NAME.keys)"; exit 2; }
	@set -e; keys=port/test/inputs/watch/$(WATCH).keys; \
	start=$$(sed -n 's/^# Start: boot both sides with GATE0_SRAM_IN=\([^ ]*\)\.sav .*/\1/p' $$keys); sav=""; \
	if [ -n "$$start" ]; then \
	  $(MAKE) -f port/test/Makefile chain-save CHAIN_SAVE=$$start; sav=$(SAVES)/$$start.sav; \
	  want=$$(sed -n 's/^# Start: .*FNV-1a \([0-9a-f]\{8\}\).*/\1/p' $$keys); \
	  got=$$($(PYTHON) port/test/tools/sram_info.py $$sav | sed -n '1s/.*fnv1a \([0-9a-f]\{8\}\).*/\1/p'); \
	  if [ "$$want" != "$$got" ]; then echo "watch-route: $$sav (fnv1a $$got) is not the save $$keys was made from" \
	    "(fnv1a $$want)"; exit 1; fi; \
	  echo "watch-route: $$keys from $$sav (fnv1a $$got, as its header says)"; \
	else echo "watch-route: $$keys from power-on (a blank save)"; fi; \
	ticks=$$($(PYTHON) port/tools/check_all.py keyticks $$keys); mkdir -p $(OUT); \
	rm -f $(STALE_REPLAYS) $(OUT)/pc.jsonl $(OUT)/pc.raw $(OUT)/pc.log $(OUT)/compare.json; \
	rule="ROUTE_TICK=capture MGBA_RUN_BP=$(GBA_BP)"; sed -n 2p $$keys | grep -q "ROUTE_TICK=capture" || rule=""; \
	env -u GATE0_SRAM_IN $${sav:+GATE0_SRAM_IN=$$sav} GATE0_KEYS=$$keys $$rule \
	  ROUTE_SCENES_OUT=$(OUT)/scenes.txt $(OUT)/mgba_lua_run $(GBA_ROM) port/test/oracle/route_check.lua $(GBA_MAX_FRAMES) \
	  > $(OUT)/watch_check.log; \
	shots=$$($(PYTHON) port/test/tools/scene_shots.py $(OUT)/scenes.txt); \
	env -u GATE0_SRAM_IN $${sav:+GATE0_SRAM_IN=$$sav} GATE0_SHOT_TICKS=$$shots \
	  $(MAKE) -f port/test/Makefile gba-trace KEYS=$$keys TICKS=$$ticks; \
	env -u GATE0_SRAM_IN $${sav:+GATE0_SRAM_IN=$$sav} GATE0_SHOT_TICKS=$$shots \
	  $(MAKE) -f port/test/Makefile pc-lag-replay KEYS=$$keys TICKS=$$ticks PC_FAULT_OK=1 PC_TIMEOUT=7200; \
	$(PYTHON) port/test/tools/frame_prehash.py $(OUT) --jobs $(WATCH_JOBS); \
	$(MAKE) -f port/test/Makefile compare-lag-replay KEYS=$$keys TICKS=$$ticks

# The sound check of such a route (README.md, "Sound on any route"; check-all's
# snd-route rows): the start save as chain-route makes it, then snd-route over
# the whole key file from it (snd-route checks the save against the header).
#   make -f port/test/Makefile chain-snd-route CHAIN=ch5x|ch6|...   [SND_KEEP=1] [SND_JOBS=4]
.PHONY: chain-snd-route
chain-snd-route:
	@case " $(CHAIN_ROUTES) " in *" $(CHAIN) "*) ;; \
	  *) echo "usage: make -f port/test/Makefile chain-snd-route CHAIN=NAME (one of: $(CHAIN_ROUTES))"; exit 2;; esac
	$(MAKE) -f port/test/Makefile chain-save CHAIN_SAVE=$(CHAIN_START_$(CHAIN))
	$(MAKE) -f port/test/Makefile snd-route ROUTE_KEYS=port/test/inputs/route_$(CHAIN).keys \
	    GATE0_SRAM_IN=$(SAVES)/$(CHAIN_START_$(CHAIN)).sav

# Diagnostic (README.md, "Clock replay"): the clock replay compared on its
# own, with the clock and its gPlaySt copies left out. Run after gba-trace
# (same KEYS and TICKS). It takes COMPARE_LOCK like every other compare.
clock-replay: pc-replay
	$(COMPARE_LOCKED) $(PYTHON) port/test/tools/gate0_compare.py --gba $(OUT)/gba.jsonl --pc $(OUT)/pc_replay.jsonl \
	    --pc-log $(OUT)/pc_replay.log --gba-raw $(OUT)/gba.raw --pc-raw $(OUT)/pc_replay.raw \
	    --gba-elf $(GBA_DIR)/fireemblem8.elf --pc-elf $(PC_BUILD)/fe8pc --ticks $(TICKS) \
	    --masks $(MASKS) --json $(OUT)/compare_replay.json

# Chapter saves (README.md, "Chapter saves"): start a Gate 0 run from a save
# instead of replaying everything from power-on.
#
# GATE0_SRAM_IN=PATH (a 32 KiB .sav, or a 64 KiB GATE0_SRAM dump) boots both
# sides with that SRAM: the GBA runner gives it to mGBA before reset, the PC
# hook writes it to SRAM before SramInit. Both still start from power-on, so
# tick 0 is the same program point as always; a short key file then takes the
# title menu to "Continue" or "Resume Chapter". It is exported, so every
# target here (gba-trace, pc-trace, the lag, affine and clock replays, route,
# the snd steps) takes it. Every trace header names it ("sram_in",
# "sram_fnv"), and the compare stops (exit 2) when the GBA trace, the PC trace
# or a replay booted from other SRAM than the rest. A value left in the shell
# environment is used too, so make says so; check-all drops it
# (port/tools/check_all.py), since its rows start from a blank save.
GATE0_SRAM_IN ?=
export GATE0_SRAM_IN
ifeq ($(origin GATE0_SRAM_IN),environment)
ifneq ($(GATE0_SRAM_IN),)
$(info port/test/Makefile: GATE0_SRAM_IN=$(GATE0_SRAM_IN) from the environment: these runs boot from that save)
endif
endif

# The test cheats (README.md, "Test cheats"; test coverage only, never a played
# build): GATE0_CHEAT=durability, funds, convoy or several joined with "+" runs
# both sides with every unit's items and the convoy's back to their maximum
# uses, the party's gold at 999,999, a rolling convoy, at each tick's sample (the
# GBA capture with oracle/test_cheat.lua, the PC with pc/gate0_hook.c, which only
# the Gate 0 test binary has). Exported like GATE0_SRAM_IN, so every run of one
# make has the same; the trace headers say "cheat" and the compare stops (exit 2)
# when the traces it compares do not all have the same. A cheat-assisted key file
# names its cheats in its "# Generated" line (ROUTE_CHEAT=..., inputs/README-routes.md
# "Cheat-assisted routes"): route (and so chain-route, path-route and the route
# rows of check-all), snd-route and snapshot run it with those cheats when
# GATE0_CHEAT is not set, and chain-save makes each save with the cheats of the key
# file it plays (KEYS_CHEAT). The targets whose GBA side has no cheat (snd,
# monkey, monkey-check, movie-check, frame-trace) refuse GATE0_CHEAT and
# ROUTE_CHEAT (the bot's and route_check.lua's setting, which route_lib.lua, and
# so the menu monkey, would read).
GATE0_CHEAT ?=
export GATE0_CHEAT
ifneq ($(filter-out durability funds convoy,$(subst +, ,$(GATE0_CHEAT))),)
$(error GATE0_CHEAT=$(GATE0_CHEAT): the test cheats are durability, funds and convoy, joined with + (port/test/README.md, "Test cheats"))
endif
KEYS_CHEAT = $(shell sed -n 's/^\# Generated.*ROUTE_CHEAT=\([a-z+]*\).*/\1/p' $(1) 2>/dev/null)
NO_CHEAT   = @[ -z "$(GATE0_CHEAT)$(ROUTE_CHEAT)" ] || { echo "$@: GATE0_CHEAT and ROUTE_CHEAT are not supported here:" \
               "its GBA side has no test cheat (port/test/README.md, \"Test cheats\")"; exit 2; }

# sram-capture: play SAVE_KEYS on the GBA and write SAVE_NAME.sav (and .json)
# in port/test/saves/ at SAVE_AT: tick:T (after T ticks), save:C (the game
# save written for chapterIndex C) or suspend:C (the last suspend save of
# chapterIndex C). Then sram-info prints what the save holds. The .sav is
# game-save data made with the ROM: port/test/saves/ keeps it out of git.
# GATE0_SRAM_IN also applies here, so one save can be made from another.
SAVES     := port/test/saves
SAVE_KEYS ?=
SAVE_AT   ?=
SAVE_NAME ?=
.PHONY: sram-capture sram-info chapsave chapsave-ch2 chapsave-frelia
sram-capture: $(OUT)/mgba_lua_run
	@if [ -z "$(SAVE_KEYS)" ] || [ -z "$(SAVE_AT)" ] || [ -z "$(SAVE_NAME)" ]; then \
	    echo "usage: make -f port/test/Makefile sram-capture SAVE_KEYS=FILE SAVE_AT=tick:T|save:C|suspend:C SAVE_NAME=NAME"; \
	    exit 2; fi
	@mkdir -p $(SAVES)
	MGBA_RUN_BP=$(GBA_BP) GATE0_KEYS=$(SAVE_KEYS) SRAM_AT=$(SAVE_AT) SRAM_OUT=$(SAVES)/$(SAVE_NAME).sav \
	    $(OUT)/mgba_lua_run $(GBA_ROM) port/test/oracle/sram_capture.lua $(GBA_MAX_FRAMES)
	$(PYTHON) port/test/tools/sram_info.py $(SAVES)/$(SAVE_NAME).sav

SAV ?= $(GATE0_SRAM_IN)
sram-info:
	$(PYTHON) port/test/tools/sram_info.py $(SAV)

# chapsave: Gate 0 from a save, run as `scripted` is (GBA capture, PC trace,
# lag replay, affine replay, compare with the lag check). CHAPSAVE_SAV is the
# save, CHAPSAVE_KEYS the key file played from power-on with it (resume.keys
# loads the save; append keys from its tick 2,000). Both sides also write
# their SRAM at the end, and the first 32 KiB are compared (the save writes
# made during the run; reported, not part of the verdict). The two presets
# below are the README's examples; each needs its save made first (the
# sram-capture line in its comment).
CHAPSAVE_SAV   ?= $(SAVES)/ch2_start.sav
CHAPSAVE_KEYS  ?= port/test/inputs/chapsave_ch2.keys
CHAPSAVE_TICKS ?= 7000
CHAPSAVE_ARGS   = KEYS=$(CHAPSAVE_KEYS) TICKS=$(CHAPSAVE_TICKS) GATE0_SRAM_IN=$(CHAPSAVE_SAV)
chapsave:
	@[ -f "$(CHAPSAVE_SAV)" ] || { echo "no $(CHAPSAVE_SAV): make it with sram-capture (README.md, \"Chapter saves\")"; exit 2; }
	@rm -f $(STALE_REPLAYS) $(OUT)/gba_end.sav $(OUT)/pc_end.sav
	GATE0_SRAM=$(OUT)/gba_end.sav $(MAKE) -f port/test/Makefile gba-trace $(CHAPSAVE_ARGS)
	GATE0_SRAM=$(OUT)/pc_end.sav $(MAKE) -f port/test/Makefile pc-trace $(CHAPSAVE_ARGS) PC_FAULT_OK=1
	$(MAKE) -f port/test/Makefile pc-lag-replay $(CHAPSAVE_ARGS) PC_FAULT_OK=1
	$(MAKE) -f port/test/Makefile pc-affine-replay $(CHAPSAVE_ARGS) PC_FAULT_OK=1
	@if [ ! -f $(OUT)/pc_end.sav ]; then echo "SRAM at the end: not compared (the PC run stopped early)"; \
	elif cmp -s -n 32768 $(OUT)/gba_end.sav $(OUT)/pc_end.sav; then echo "SRAM at the end: equal (first 32 KiB)"; \
	else echo "SRAM at the end: DIFFERENT at $$(cmp -l -n 32768 $(OUT)/gba_end.sav $(OUT)/pc_end.sav | wc -l) bytes" \
	    "(cmp -l -n 32768 $(OUT)/gba_end.sav $(OUT)/pc_end.sav)"; fi
	$(MAKE) -f port/test/Makefile compare $(CHAPSAVE_ARGS)

# Chapter 2 from its start: the game save the routes write after Chapter 1
# (route tick 20,342). Save: sram-capture SAVE_KEYS=port/test/inputs/route_ch1.keys SAVE_AT=save:2 SAVE_NAME=ch2_start
chapsave-ch2:
	$(MAKE) -f port/test/Makefile chapsave CHAPSAVE_SAV=$(SAVES)/ch2_start.sav \
	    CHAPSAVE_KEYS=port/test/inputs/chapsave_ch2.keys CHAPSAVE_TICKS=7000

# The Castle Frelia change (route ticks 19,913-20,342): the last suspend save
# of Chapter 1 (Eirika's seize), resumed, through to Chapter 2's first player
# phase. Save: sram-capture SAVE_KEYS=port/test/inputs/route_ch1.keys SAVE_AT=suspend:1 SAVE_NAME=ch1_seize
chapsave-frelia:
	$(MAKE) -f port/test/Makefile chapsave CHAPSAVE_SAV=$(SAVES)/ch1_seize.sav \
	    CHAPSAVE_KEYS=port/test/inputs/chapsave_frelia.keys CHAPSAVE_TICKS=7600

# The save checks as one standing check (README.md, "Save checks"; tools/savechk_all.py; port-savechk2,
# the user's SAVE-3 B of 2026-10-30): P5.3, the save-menu actions, P5.4 and Reforged Options on, on
# the played builds against mGBA, ending with "SAVE CHECKS: PASS" or "SAVE CHECKS: FAIL (...)".
# check-all's opt-in row runs it (make -f port/check-all.mk CHECK_SAVES=1). Needs the GBA build and
# the played builds named (SAVECHK_BUILDS); its start save, ch1_seize.sav, is made with sram-capture
# (chapsave-frelia above) when it is not in $(SAVES). Outputs in build/savechk/.
#   make -f port/test/Makefile savechk [SAVECHK_BUILDS=full,sdl,win] [SAVECHK_PARTS=p53,p53b,p54,rfo]
SAVECHK_BUILDS ?= full,sdl,win
SAVECHK_PARTS  ?= p53,p53b,p54,rfo
.PHONY: savechk
savechk: $(OUT)/mgba_lua_run
	$(PYTHON) port/test/tools/savechk_all.py --builds $(SAVECHK_BUILDS) --parts $(SAVECHK_PARTS)

# Save states (README.md, "Save states"; port-plan/pending/savestate.md): a
# photo of both machines at one tick, so later runs start there instead of at
# power-on. Test harness only: nothing of it is in a shipped build.
#
#   make -f port/test/Makefile snapshot KEYS=FILE (or ROUTE_KEYS=FILE) AT=T [SNAP=DIR]
#        [SNAP_PARTS="gba pc pclag pcaff"]
#
# plays FILE from power-on to tick T: the GBA capture (part gba: mGBA's state
# and the capture's counters, saved as tick T-1's line is written; the
# capture goes on through tick T, because the lag replay runs tick T's lag
# VBlanks inside the main loop before T's sample point, where its photo is
# taken), then the PC once per PC part (pc: the plain run; pclag: the lag
# replay, with that GBA capture; pcaff: the affine replay), each saved at
# tick T's sample point and stopped there. Output:
# DIR/tick<T>/<part>/, and the runs' own traces and logs in DIR/tick<T>/run/.
# DIR (default build/gate0-snap/<key file name>) is under build/: game data,
# never committed.
#
#   make -f port/test/Makefile route GATE0_STATE_IN=DIR/tick<T> ROUTE_KEYS=FILE ROUTE_TICKS=N
#   (also gba-trace, pc-trace, pc-lag-replay, pc-affine-replay, compare, scripted, chapsave)
#
# resumes each run from that photo: the GBA runner loads the state before the
# first instruction (MGBA_RUN_STATE_IN), and each PC run first gets a patch for
# this PC binary (port/test/tools/savestate.py relink: the host addresses in
# the photo are rewritten by name for this binary, checked against the GBA's
# own memory and fireemblem8.elf; the same binary is exact, a rebuilt one is
# best effort: README "Save states") and applies it at its first sample
# point. The keys are the same file;
# TICKS stays the last tick + 1. Each trace starts with tick T-1's line (the
# prologue, from the snapshot's run), then ticks T on. The compare refuses
# traces from other snapshots, ticks or ROMs. Exported like GATE0_SRAM_IN;
# check-all drops it.
SNAP_KEYS  ?= $(if $(filter command line,$(origin ROUTE_KEYS)),$(ROUTE_KEYS),$(KEYS))
SNAP       ?= build/gate0-snap/$(basename $(notdir $(SNAP_KEYS)))
AT         ?=
SNAP_PARTS ?= gba pc pclag pcaff
# SNAP_SND=1: the gba part's capture also has the m4a breakpoints, so its
# trace has sound positions ("lag_snd") and the pclag part's lag replay runs
# by them: a snapshot snd-route can resume (README.md, "Sound on any route").
SNAP_SND   ?=
# A cheat-assisted key file is photographed with its cheat on both sides, as route runs it
# (README.md, "Test cheats"; the GBA photo's meta.json names it, and a resume checks it).
SNAP_CHEAT_RUN = $(or $(GATE0_CHEAT),$(call KEYS_CHEAT,$(SNAP_KEYS)))
GATE0_STATE_IN ?=
export GATE0_STATE_IN
ifeq ($(origin GATE0_STATE_IN),environment)
ifneq ($(GATE0_STATE_IN),)
$(info port/test/Makefile: GATE0_STATE_IN=$(GATE0_STATE_IN) from the environment: these runs resume from that save state)
endif
endif
GBA_STATE_ENV = $(if $(GATE0_STATE_IN),MGBA_RUN_STATE_IN=$(GATE0_STATE_IN)/gba/state.ss)
STATE_CHECK   = $(if $(GATE0_STATE_IN),$(PYTHON) port/test/tools/savestate.py check $(GATE0_STATE_IN))
STATE_RELINK  = $(if $(GATE0_STATE_IN),$(PYTHON) port/test/tools/savestate.py relink --part $(GATE0_STATE_IN)/$(1) \
                  --exe $(PC_BUILD)/fe8pc --gba-elf $(GBA_DIR)/fireemblem8.elf)
.PHONY: snapshot
snapshot: $(OUT)/mgba_lua_run pc
	@if [ -z "$(AT)" ] || [ -z "$(SNAP_KEYS)" ]; then \
	    echo "usage: make -f port/test/Makefile snapshot KEYS=FILE AT=T [SNAP=DIR] [SNAP_PARTS=\"gba pc pclag pcaff\"]"; \
	    exit 2; fi
	@if [ -n "$(GATE0_STATE_IN)" ]; then echo "snapshot: a snapshot is made from power-on: unset GATE0_STATE_IN"; exit 2; fi
	@case " $(SNAP_PARTS) " in *" gba "*) ;; *" pclag "*|*" pcaff "*) \
	    echo "snapshot: the pclag and pcaff parts need the gba part (its capture)"; exit 2;; esac
	@set -e; run=$(SNAP)/tick$(AT)/run; rm -rf $(SNAP)/tick$(AT); mkdir -p $$run/gba_frames; \
	set=$$(date +%s%N | sha1sum | cut -c1-12); \
	echo "snapshot: set $$set, tick $(AT), keys $(SNAP_KEYS), parts $(SNAP_PARTS)$(if $(SNAP_CHEAT_RUN), (cheat-assisted: GATE0_CHEAT=$(SNAP_CHEAT_RUN))) -> $(SNAP)/tick$(AT)"; \
	export GATE0_STATE_SET=$$set GATE0_STATE_OUT=$(SNAP) GATE0_STATE_AT=$(AT) GATE0_KEYS=$(SNAP_KEYS); \
	export GATE0_CHEAT=$(SNAP_CHEAT_RUN); \
	bp="$(GBA_BP)"; snd=""; if [ "$(SNAP_SND)" = 1 ]; then \
	  spec=$$($(PYTHON) port/test/audio/snd_calls.py setup-gate0 $(GBA_DIR)/fireemblem8.elf $(GBA_DIR)/fireemblem8.map \
	      "$(GBA_BP)") || exit 1; set -- $$spec; bp=$$1; snd="SND_M4A_CODE=$$2 SND_MPLAY=$$3 SND_CALLS=$$run/snd.gba.calls"; fi; \
	for part in $(SNAP_PARTS); do \
	  start=$$(date +%s.%N); \
	  case $$part in \
	  gba) env $$snd MGBA_RUN_BP=$$bp GATE0_TICKS=$$(( $(AT) + 1 )) GATE0_OUT=$$run/gba.jsonl GATE0_RAW=$$run/gba.raw \
	       GATE0_SHOTDIR=$$run/gba_frames $(OUT)/mgba_lua_run $(GBA_ROM) port/test/oracle/gate0_capture.lua \
	       $(GBA_MAX_FRAMES) > $$run/gba.log 2>&1 || { tail -5 $$run/gba.log; exit 1; };; \
	  pc|pclag|pcaff) \
	       extra=""; if [ $$part = pclag ]; then extra="GATE0_LAG_FROM=$$run/gba.jsonl"; fi; \
	       if [ $$part = pcaff ]; then extra="GATE0_AFFINE_FROM=$$run/gba.raw"; fi; \
	       env $$extra GATE0_TICKS=$$(( $(AT) + 1 )) GATE0_STATE_EXIT=1 GATE0_OUT=$$run/$$part.jsonl \
	       GATE0_RAW=$$run/$$part.raw FE8PC_BLEND=mgba8 FE8PC_MGBA_BGCLIP=1 FE8PC_FRAMES=0 timeout $(PC_TIMEOUT) \
	       $(PC_BUILD)/fe8pc $(GBA_DIR)/baserom.gba > $$run/$$part.log 2>&1 || { tail -5 $$run/$$part.log; exit 1; };; \
	  *) echo "snapshot: unknown part $$part (gba, pc, pclag, pcaff)"; exit 2;; \
	  esac; \
	  echo "snapshot: $$part saved ($$(awk "BEGIN { printf \"%.1f\", $$(date +%s.%N) - $$start }") s)"; \
	done
	$(PYTHON) port/test/tools/savestate.py check $(SNAP)/tick$(AT)

# Menu monkey (README.md, "Menu monkey"): a seeded, structured button sequence
# that opens the screens the bot routes never do, written as an ordinary key
# file by oracle/menu_monkey.lua (mGBA, no breakpoints, the bot's tick timing).
#
#   make -f port/test/Makefile monkey START=SAVE.sav|SNAP/tick<T>|power-on [SEED=1] [TICKS=6000] [MONKEY_OUT=FILE.keys]
#                                     [MONKEY_TICK=frame|capture]
#
# START: a chapter save (booted with GATE0_SRAM_IN; inputs/resume.keys loads it,
# Continue or Resume Chapter, 2,000 ticks), a save state's spot (its key file's
# first T ticks from power-on, with the save it booted from; the state itself is
# not loaded, so the NULL probe can run), or power-on (route_ch0_prologue.keys to
# the Prologue's first free cursor, tick 1,984). TICKS are the monkey's own ticks
# after that prefix. MONKEY_OUT (default build/gate0/monkey/<start>_s<SEED>.keys;
# not OUT, which is this file's output directory) also gets .coverage.json (the
# screens it opened) and .trace.jsonl (its view of every tick in the Gate 0
# trace's fields). Same START, SEED, TICKS and ROM: the same file.
# Coverage 12 (the post-game, README.md "Menu monkey"): MONKEY_PREFIX_KEYS=FILE.keys
# plays that key file (all its ticks: a bot route from the START save to where the
# monkey begins, e.g. the post-game world map) instead of inputs/resume.keys, and
# MONKEY_WM=post gives the monkey its post-game world-map and title-menu rules
# (oracle/menu_monkey.lua). Both empty by default: nothing else changes.
#
#   make -f port/test/Makefile monkey-check MONKEY_KEYS=FILE.keys [NULLPROBE=1] [EVERY_FRAME=1]
#
# replays it: the route check (GBA capture, PC lag replay, compare; with the NULL
# probe unless NULLPROBE=0, its sites in FILE.sites.jsonl), then
# tools/monkey_check.py replay (the capture must be the monkey's own run on every
# field of every tick), then with EVERY_FRAME=1 a second GBA capture and lag
# replay that keep every frame (FILE.frames/, delete it after), compared frame by
# frame in chunks (FILE.frames.jsonl, labelled). That pass has the GBA's raster
# log too (as gba-trace), so each frame the PC redraws with the GBA's timing as
# exactly the GBA's frame is a raster-timing frame (decision 9) and passes. The
# last line is MONKEY CHECK: PASS only when the route check, the replay check and
# (if run) every frame pass.
START        ?= power-on
SEED         ?= 1
MONKEY_TICKS ?= $(if $(filter command line,$(origin TICKS)),$(TICKS),6000)
MONKEY_DIR   := $(OUT)/monkey
MONKEY_OUT   ?=
MONKEY_KEYS  ?=
NULLPROBE    ?= 1
EVERY_FRAME  ?=
FRAME_JOBS   ?= 2
# The monkey's tick rule (oracle/route_lib.lua, "Tick rules"): frame (the
# default; every file in inputs/monkey/ was made with it and regenerates with
# it) or capture, the Gate 0 capture's own rule, which also follows an early
# flush VBlank (with the breakpoints, so slower; cov9 H-B).
MONKEY_TICK  ?= frame
MONKEY_PREFIX_KEYS ?=
MONKEY_WM    ?=
.PHONY: monkey monkey-check
$(OUT)/fireemblem8.nm: $(GBA_DIR)/fireemblem8.elf
	@mkdir -p $(@D)
	arm-none-eabi-nm $< > $@

monkey: $(OUT)/mgba_lua_run $(OUT)/fireemblem8.nm
	$(NO_CHEAT)
	@set -e; eval "$$($(PYTHON) port/test/tools/monkey_check.py start '$(START)')"; \
	if [ -n "$(MONKEY_PREFIX_KEYS)" ]; then prefix="$(MONKEY_PREFIX_KEYS)"; ptick=""; \
	    name="$$(basename "$(MONKEY_PREFIX_KEYS)" .keys)"; fi; \
	out="$(MONKEY_OUT)"; [ -n "$$out" ] || out="$(MONKEY_DIR)/$${name}_s$(SEED).keys"; \
	mkdir -p "$$(dirname "$$out")"; base="$${out%.keys}"; \
	echo "monkey: start $$label, seed $(SEED), prefix $$prefix ($$ptick ticks), $(MONKEY_TICKS) monkey ticks, tick rule $(MONKEY_TICK) -> $$out"; \
	ROUTE_TICK=$(MONKEY_TICK) $(if $(filter capture,$(MONKEY_TICK)),MGBA_RUN_BP=$(GBA_BP)) \
	GATE0_SRAM_IN="$$sav" MONKEY_OUT="$$out" MONKEY_SYMS=$(OUT)/fireemblem8.nm MONKEY_SEED=$(SEED) \
	MONKEY_TICKS=$(MONKEY_TICKS) MONKEY_PREFIX="$$prefix" MONKEY_PREFIX_TICKS="$$ptick" MONKEY_START="$$label" \
	MONKEY_TRACE="$$base.trace.jsonl" MONKEY_COVERAGE="$$base.coverage.json" MONKEY_WM="$(MONKEY_WM)" \
	    $(OUT)/mgba_lua_run $(GBA_ROM) port/test/oracle/menu_monkey.lua $(GBA_MAX_FRAMES)

monkey-check: $(OUT)/mgba_lua_run pc
	$(NO_CHEAT)
	@[ -n "$(MONKEY_KEYS)" ] || { echo "usage: make -f port/test/Makefile monkey-check MONKEY_KEYS=FILE.keys [NULLPROBE=1] [EVERY_FRAME=1]"; exit 2; }
	@set -e; base="$(basename $(MONKEY_KEYS))"; name="$$(basename "$$base")"; \
	eval "$$($(PYTHON) port/test/tools/monkey_check.py info "$$base.coverage.json")"; \
	probe=""; if [ "$(NULLPROBE)" = 1 ]; then rm -f "$$base.sites.jsonl"; \
	    probe="FE8PC_NULLPROBE=1 FE8PC_NULLPROBE_OUT=$$base.sites.jsonl FE8PC_NULLPROBE_ROUTE=$$name"; fi; \
	route=0; env $$probe GATE0_SRAM_IN="$$sav" $(MAKE) -f port/test/Makefile route ROUTE_KEYS=$(MONKEY_KEYS) \
	    ROUTE_TICKS=$$ticks || route=$$?; \
	replay=0; $(PYTHON) port/test/tools/monkey_check.py replay --digest "$$base.trace.jsonl" --gba $(OUT)/gba.jsonl \
	    --keys $(MONKEY_KEYS) || replay=$$?; \
	frames=0; if [ "$(EVERY_FRAME)" = 1 ]; then \
	  fd="$$base.frames"; rm -rf "$$fd"; mkdir -p "$$fd/gba" "$$fd/pc"; \
	  GATE0_SRAM_IN="$$sav" MGBA_RUN_BP=$(GBA_BP) GATE0_KEYS=$(MONKEY_KEYS) GATE0_TICKS=$$ticks \
	  GATE0_SHOT_TICKS="0-$$((ticks - 1))" GATE0_OUT="$$fd/gba.jsonl" GATE0_SHOTDIR="$$fd/gba" \
	  MGBA_RUN_RASTERLOG="$$fd/gba_raster.bin" MGBA_RUN_RASTER_RAM=$(GBA_RASTER_RAM) MGBA_RUN_RASTER_IRQ=$(GBA_RASTER_IRQ) \
	      $(OUT)/mgba_lua_run $(GBA_ROM) port/test/oracle/gate0_capture.lua $(GBA_MAX_FRAMES) > "$$fd/gba.log" 2>&1; \
	  env $$probe FE8PC_NULLPROBE_OUT="$$fd/sites.jsonl" GATE0_SRAM_IN="$$sav" GATE0_LAG_FROM="$$fd/gba.jsonl" \
	  GATE0_KEYS=$(MONKEY_KEYS) GATE0_TICKS=$$ticks GATE0_SHOT_TICKS="0-$$((ticks - 1))" GATE0_SHOTDIR="$$fd/pc" \
	  GATE0_RASTER_FROM="$$fd/gba_raster.bin" \
	  GATE0_OUT="$$fd/pc_lagreplay.jsonl" FE8PC_BLEND=mgba8 FE8PC_MGBA_BGCLIP=1 FE8PC_FRAMES=0 timeout $(PC_TIMEOUT) \
	      $(PC_BUILD)/fe8pc $(GBA_DIR)/baserom.gba > "$$fd/pc_lagreplay.log" 2>&1 || true; \
	  $(PYTHON) port/test/tools/monkey_check.py same --a $(OUT)/pc_lagreplay.jsonl --b "$$fd/pc_lagreplay.jsonl" \
	      || frames=$$?; \
	  $(PYTHON) port/test/tools/monkey_check.py frames --gba-dir "$$fd/gba" --pc-dir "$$fd/pc" --gba "$$fd/gba.jsonl" \
	      --pc "$$fd/pc_lagreplay.jsonl" --digest "$$base.trace.jsonl" --out "$$base.frames.jsonl" \
	      --jobs $(FRAME_JOBS) || frames=$$?; fi; \
	sites=0; if [ "$(NULLPROBE)" = 1 ]; then \
	  $(PYTHON) port/test/tools/monkey_check.py sites "$$base.sites.jsonl" || sites=$$?; fi; \
	echo "route check exit $$route, monkey replay exit $$replay, NULL probe $(if $(filter 1,$(NULLPROBE)),exit $$sites,not run)," \
	     "every frame $(if $(filter 1,$(EVERY_FRAME)),exit $$frames,not run)"; \
	if [ $$route -eq 0 ] && [ $$replay -eq 0 ] && [ $$frames -eq 0 ] && [ $$sites -eq 0 ]; then echo "MONKEY CHECK: PASS"; \
	else echo "MONKEY CHECK: DIFFERENT (see above)"; exit 1; fi

# Sound (phase 3; port-plan/audio-design.md sections 5-6, README "Sound").
# SND=title: Start during the opening, then the title theme (song 0x43).
# SND=map: the scripted route (A every 90 ticks) to the Prologue map. The PC
# side runs all SND_TICKS_map ticks since D-13 (was P-11, the frame-15,800
# fault); a PC run that stops early is still reported, not fatal.
SND ?= title
SND_DIR := $(OUT)/snd
SND_KEYS_title := port/test/inputs/title_start.keys
SND_TICKS_title := 6450
SND_FRAMES_title := 6500
SND_KEYS_map := port/test/inputs/scripted_a90.keys
SND_TICKS_map := 16500
SND_FRAMES_map := 16300
.PHONY: snd snd-gba snd-pc snd-compare snd-mutate

# The GBA records, plus the m4a calls between them (X.gba.calls): debugger
# breakpoints at every m4a entry point with their returns, and VBlankIntrWait
# for the key-rule check (audio/snd_calls.py makes the list from the ELF/map).
snd-gba: $(OUT)/mgba_lua_run
	$(NO_CHEAT)
	@mkdir -p $(SND_DIR)
	spec=$$($(PYTHON) port/test/audio/snd_calls.py setup $(GBA_DIR)/fireemblem8.elf $(GBA_DIR)/fireemblem8.map) \
	    || exit 1; set -- $$spec; \
	MGBA_RUN_BP=$$1 SND_M4A_CODE=$$2 SND_CALLS=$(SND_DIR)/$(SND).gba.calls \
	SND_OUT=$(SND_DIR)/$(SND).gba.snd SND_FRAMES=$(SND_FRAMES_$(SND)) GATE0_KEYS=$(SND_KEYS_$(SND)) \
	    $(OUT)/mgba_lua_run $(GBA_ROM) port/test/oracle/sound_capture.lua 10000000

snd-pc: pc
	$(NO_CHEAT)
	@mkdir -p $(SND_DIR)
	-GATE0_OUT=$(SND_DIR)/$(SND).pc.jsonl GATE0_KEYS=$(SND_KEYS_$(SND)) GATE0_TICKS=$(SND_TICKS_$(SND)) \
	FE8PC_FRAMES=0 FE8PC_AUDIO_DUMP=$(SND_DIR)/$(SND).pc FE8PC_SND_RECORD=$(SND_DIR)/$(SND).pc.snd \
	FE8PC_AUDIO_LOG=1 $(PC_BUILD)/fe8pc $(GBA_DIR)/baserom.gba > $(SND_DIR)/$(SND).pc.log 2>&1
	@grep "starts song\|^\[audio\] [0-9]\|fault" $(SND_DIR)/$(SND).pc.log | head -20

# Compare 3 (replay) is the pass/fail; compare 1 (stream) lists its first
# difference per song with the GBA lag that explains it.
snd-compare:
	$(PYTHON) port/test/tools/sndtool.py selfcheck $(SND_DIR)/$(SND).pc
	$(PYTHON) port/test/audio/m4a_replay.py $(SND_DIR)/$(SND).gba.snd --calls $(SND_DIR)/$(SND).gba.calls --show 4
	$(PYTHON) port/test/tools/sndtool.py fifo $(SND_DIR)/$(SND).gba.snd $(SND_DIR)/$(SND).pc.snd \
	    $(SND_DIR)/$(SND).pc.fifo8 --rom $(GBA_DIR)/baserom.gba --song $(if $(filter map,$(SND)),0x04,0x43) || true

snd:
	$(MAKE) -f port/test/Makefile snd-gba
	$(MAKE) -f port/test/Makefile snd-pc
	$(MAKE) -f port/test/Makefile snd-compare

# Checks compare 3 itself (README "Sound"): driver mutants that change only
# player or track state must fail it. Run after snd-gba (same SND).
snd-mutate:
	$(PYTHON) port/test/audio/mutate_replay.py $(SND_DIR)/$(SND).gba.snd

# Sound on any route (README.md, "Sound on any route"): the snd checks on any
# bot route or monkey key file, from power-on, a chapter save (GATE0_SRAM_IN)
# or a save state (GATE0_STATE_IN), compared in chunks.
#
#   make -f port/test/Makefile snd-route ROUTE_KEYS=FILE [ROUTE_TICKS=N] [GATE0_SRAM_IN=SAV]
#        [GATE0_STATE_IN=SNAP/tick<T>] [SND_CHUNK=4096] [SND_JOBS=4] [SND_GATE0=1] [SND_KEEP=1]
#   make -f port/test/Makefile snd-route MONKEY_KEYS=FILE GATE0_SRAM_IN=SAV   (the same; a monkey file)
#
# One GBA capture (gate0_capture.lua: the route's own trace, keys and ticks,
# plus one sound record per VBlank, the m4a calls between them and the
# tick-to-record index: SND_OUT, SND_CALLS, SND_INDEX), then the PC lag
# replay of that trace (every GBA VBlank, lag VBlanks too, each with its
# sound record, FIFO bytes and index), then audio/snd_route.py: the replay
# (the snd rows' pass/fail), the PC self-check, the PC records against the
# GBA's record by record (the frame clock is master, D4) and the PC run
# complete. ROUTE_TICKS defaults to the key file's length. A key file whose
# header names its start save ("FNV-1a xxxxxxxx") needs that save in
# GATE0_SRAM_IN; a menu monkey file that starts at power-on ("start power-on
# (SRAM FNV-1a 4f731dc5)": the blank 0xFF SRAM it booted with) needs none
# (H-sndmk-1). Output: SND_ROUTE_DIR (default build/gate0/snd-route/<key
# file name>[-<snapshot tick>]); the record files (about 8 KiB per VBlank and
# side) are deleted after a PASS unless SND_KEEP=1. SND_GATE0=1 also runs the
# route's Gate 0 compare (the lag replay against the GBA, with raw dumps and
# the raster log; it takes COMPARE_LOCK from 60,000 ticks), reported, not part
# of SND ROUTE.
SND_KEYS        ?= $(if $(MONKEY_KEYS),$(MONKEY_KEYS),$(ROUTE_KEYS))
SND_ROUTE_TICKS ?= $(if $(filter command line,$(origin ROUTE_TICKS)),$(ROUTE_TICKS))
SND_ROUTE_DIR   ?= $(OUT)/snd-route/$(basename $(notdir $(SND_KEYS)))$(if $(GATE0_STATE_IN),-$(notdir $(GATE0_STATE_IN)))
SND_CHUNK       ?= 4096
SND_JOBS        ?= 4
SND_GATE0       ?=
SND_KEEP        ?=
SND_PC_TIMEOUT  ?= 7200
# A cheat-assisted key file runs with its cheat on both sides, as in route (README.md, "Test cheats").
SND_CHEAT_RUN    = $(or $(GATE0_CHEAT),$(call KEYS_CHEAT,$(SND_KEYS)))
.PHONY: snd-route
snd-route: $(OUT)/mgba_lua_run pc
	@set -e; keys=$(SND_KEYS); d=$(SND_ROUTE_DIR); x=$$d/snd; \
	ticks=$(SND_ROUTE_TICKS); [ -n "$$ticks" ] || ticks=$$($(PYTHON) port/tools/check_all.py keyticks $$keys); \
	want=$$(sed -n '/^#.* start power-on /d; s/^#.*FNV-1a \([0-9a-f]\{8\}\).*/\1/p' $$keys | head -1); \
	if [ -n "$$want" ]; then \
	  [ -n "$(GATE0_SRAM_IN)" ] || { echo "snd-route: $$keys starts from a save (FNV-1a $$want): set GATE0_SRAM_IN"; exit 2; }; \
	  got=$$($(PYTHON) port/test/tools/sram_info.py $(GATE0_SRAM_IN) | sed -n '1s/.*fnv1a \([0-9a-f]\{8\}\).*/\1/p'); \
	  [ "$$want" = "$$got" ] || { echo "snd-route: $(GATE0_SRAM_IN) (fnv1a $$got) is not the save $$keys was made from ($$want)"; exit 1; }; \
	fi; \
	rm -rf $$d; mkdir -p $$d; \
	echo "snd-route: $$keys, $$ticks ticks$${GATE0_SRAM_IN:+, from $(GATE0_SRAM_IN)}$(if $(GATE0_STATE_IN), resumed from $(GATE0_STATE_IN))$(if $(SND_CHEAT_RUN), cheat-assisted (GATE0_CHEAT=$(SND_CHEAT_RUN)) on both sides) -> $$d"; \
	export GATE0_CHEAT=$(SND_CHEAT_RUN); \
	$(if $(STATE_CHECK),$(STATE_CHECK);) \
	if [ -n "$(GATE0_STATE_IN)" ]; then \
	  grep -q '"lag_snd":true' $(GATE0_STATE_IN)/gba/meta.json && [ -d $(GATE0_STATE_IN)/pclag ] || { \
	    echo "snd-route: $(GATE0_STATE_IN) is not a snapshot with sound positions and a pclag part (make it with"; \
	    echo "  make -f port/test/Makefile snapshot KEYS=$$keys AT=T SNAP_SND=1 SNAP_PARTS=\"gba pclag\")"; exit 2; }; fi; \
	spec=$$($(PYTHON) port/test/audio/snd_calls.py setup-gate0 $(GBA_DIR)/fireemblem8.elf $(GBA_DIR)/fireemblem8.map \
	    "$(GBA_BP)") || exit 1; set -- $$spec; \
	g0=""; p0=""; if [ "$(SND_GATE0)" = 1 ]; then mkdir -p $$d/gba_frames; \
	  g0="GATE0_RAW=$$d/gba.raw GATE0_SHOTDIR=$$d/gba_frames MGBA_RUN_RASTERLOG=$$d/gba_raster.bin \
	      MGBA_RUN_RASTER_RAM=$(GBA_RASTER_RAM) MGBA_RUN_RASTER_IRQ=$(GBA_RASTER_IRQ)"; \
	  p0="GATE0_RAW=$$d/pc_lagreplay.raw GATE0_RASTER_FROM=$$d/gba_raster.bin"; fi; \
	start=$$(date +%s); \
	env $$g0 $(GBA_STATE_ENV) MGBA_RUN_BP=$$1 SND_M4A_CODE=$$2 SND_MPLAY=$$3 SND_OUT=$$x.gba.snd SND_CALLS=$$x.gba.calls \
	    SND_INDEX=$$x.gba.idx GATE0_KEYS=$$keys GATE0_TICKS=$$ticks GATE0_OUT=$$d/gba.jsonl \
	    $(OUT)/mgba_lua_run $(GBA_ROM) port/test/oracle/gate0_capture.lua $(GBA_MAX_FRAMES) > $$d/gba.log 2>&1 \
	    || { echo "snd-route: the GBA capture stopped, see $$d/gba.log"; tail -5 $$d/gba.log; exit 1; }; \
	grep '^gate0_capture:' $$d/gba.log || true; echo "snd-route: GBA capture $$(( $$(date +%s) - start )) s"; \
	$(if $(GATE0_STATE_IN),$(call STATE_RELINK,pclag);) \
	start=$$(date +%s); status=0; \
	env $$p0 GATE0_LAG_FROM=$$d/gba.jsonl GATE0_KEYS=$$keys GATE0_TICKS=$$ticks GATE0_OUT=$$x.pc.jsonl \
	    GATE0_SND_INDEX=$$x.pc.idx FE8PC_SND_RECORD=$$x.pc.snd FE8PC_AUDIO_DUMP=$$x.pc FE8PC_BLEND=mgba8 \
	    FE8PC_FRAMES=0 timeout $(SND_PC_TIMEOUT) $(PC_BUILD)/fe8pc $(GBA_DIR)/baserom.gba > $$x.pc.log 2>&1 || status=$$?; \
	if [ $$status -ne 0 ]; then echo "snd-route: fe8pc (lag replay) stopped with status $$status, see $$x.pc.log"; \
	  tail -4 $$x.pc.log; fi; \
	grep '^\[gate0\] lag replay: [0-9]* GBA lag VBlanks run' $$x.pc.log || true; \
	echo "snd-route: PC lag replay $$(( $$(date +%s) - start )) s"; \
	gate0=""; if [ "$(SND_GATE0)" = 1 ]; then gate0=0; lock=""; \
	  if [ -n "$(COMPARE_LOCK)" ] && [ $$(( ticks - $(or $(STATE_FROM),0) )) -ge $(COMPARE_LOCK_TICKS) ]; then \
	    lock="$(PYTHON) port/tools/check_all.py lock $(COMPARE_LOCK) --"; fi; \
	  $$lock $(PYTHON) port/test/tools/gate0_compare.py --gba $$d/gba.jsonl --pc $$x.pc.jsonl --pc-log $$x.pc.log \
	    --gba-raw $$d/gba.raw --pc-raw $$d/pc_lagreplay.raw --gba-elf $(GBA_DIR)/fireemblem8.elf \
	    --pc-elf $(PC_BUILD)/fe8pc --ticks $$ticks --masks $(MASKS) --json $$d/compare.json > $$d/compare.log 2>&1 \
	    || gate0=$$?; grep '^GATE 0' $$d/compare.log || tail -3 $$d/compare.log; fi; \
	rc=0; $(PYTHON) port/test/audio/snd_route.py --gba $$x.gba --pc $$x.pc --pc-exe $(PC_BUILD)/fe8pc \
	    --ticks $$ticks --chunk $(SND_CHUNK) --jobs $(SND_JOBS) --out $$d/harness || rc=$$?; \
	[ -z "$$gate0" ] || echo "snd-route: Gate 0 compare of the lag replay exit $$gate0 (reported, not part of SND ROUTE)"; \
	if [ $$rc -eq 0 ] && [ "$(SND_KEEP)" != 1 ]; then rm -f $$x.gba.snd $$x.pc.snd $$x.pc.fifo8 $$x.pc.lr16 \
	    $$d/gba.raw $$d/pc_lagreplay.raw; rm -rf $$d/gba_frames; \
	  echo "snd-route: record files deleted (SND_KEEP=1 keeps them)"; fi; \
	exit $$rc

# Checks snd-route's compare itself (README.md, "Sound on any route"): altered
# copies of a passing run kept with SND_KEEP=1 must get their verdicts.
.PHONY: snd-route-mutate
snd-route-mutate:
	$(PYTHON) port/test/audio/mutate_snd_route.py $(SND_ROUTE_DIR) --pc-exe $(PC_BUILD)/fe8pc

# NULL probe (README.md, "NULL probe"; port/hw/nullprobe.c): each route of
# README-routes.md run to its end by the PC with FE8PC_NULLPROBE=1, so every
# BIOS-area/NULL-page read returns the open-bus latch instead of faulting and is
# recorded; then one list of all sites (build/nullprobe/report.md). PC only,
# no mGBA run. Routes of 60,000 ticks or more take the compare lock
# themselves (an inherited lock is reused). A diagnostic, not a gate.
# NULLPROBE_ARGS: more options for `nullprobe.py run`, for example
# "--gba-trace build/gate0/route_ch5.jsonl" to compare the game state with a GBA
# trace of the routes (the PC leaves a route at the first GBA lag effect that
# changes its timing), and "--lag-replay" to run the PC with that trace's lag
# VBlanks (GATE0_LAG_FROM, "Lag replay"; a trace in format 3).
NULLPROBE_ROUTES ?= ch0 ch1 ch2 ch3 ch4 ch5
NULLPROBE_OUT    ?= build/nullprobe
NULLPROBE_ARGS   ?=
.PHONY: nullprobe nullprobe-report
nullprobe: pc
	$(PYTHON) port/tools/nullprobe.py run --routes $(NULLPROBE_ROUTES) --out $(NULLPROBE_OUT) --gba-dir $(GBA_DIR) \
	    $(NULLPROBE_ARGS)
	$(PYTHON) port/tools/nullprobe.py report --out $(NULLPROBE_OUT)

nullprobe-report:
	$(PYTHON) port/tools/nullprobe.py report --out $(NULLPROBE_OUT)
