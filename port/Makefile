# PC host build for the D1-a spike (32-bit x86, GBA memory at GBA addresses).
#
# Run from the repository root:
#   make -f port/Makefile                 # build build/pc/fe8pc
#   make -f port/Makefile survey          # try to compile every src/*.c
#   ./build/pc/fe8pc baserom.gba          # run (needs the user's ROM)
#
# GBA_DIR holds a GBA build of the same commit (default: this tree, after the
# GBA `make`): fireemblem8.map, fireemblem8.elf, its objects and baserom.gba.
# They are the address oracle: the host linker script places every RAM
# variable at the address the GBA build gave it, resolves symbols that are not
# built on the host (asm data, unported files) to their GBA addresses, and
# fixptr.py turns the GBA relocations in ROM data into a host table.

CC      ?= gcc
PYTHON  ?= python3
BUILD   := build/pc
GBA_DIR ?= .
GBA_MAP ?= $(GBA_DIR)/fireemblem8.map
GBA_ELF ?= $(GBA_DIR)/fireemblem8.elf
GBA_ROM ?= $(GBA_DIR)/baserom.gba

# -fno-delete-null-pointer-checks: agbcc keeps a NULL test that follows a write
# through the pointer; GCC -O1 drops it, and a loop like SetWmPlaceDotFlagForIndex
# then spins forever once H-1 drops the write (DIVERGENCES.md B-5, was P-4).
# -fdata-sections: one section per variable, so gen_host_link.py can put every
# RAM variable at its own GBA address (B-12, was P-14; port.h leaves EWRAM_DATA,
# IWRAM_DATA and EWRAM_OVERLAY empty for it). check_ram_addrs.py checks the link.
# -fno-aggressive-loop-optimizations: agbcc keeps a loop's end test even when
# the loop indexes past an array; GCC -O1 takes those reads as undefined and
# drops the test. PromoHandler_SetupAndStartUI (src/classchg-handler.c:106-161)
# runs the retail for (i = 0; i < 7; i++) over its 3-entry trainees[], and the
# host loop then walked up the host stack with no end (DIVERGENCES.md B-15;
# found by the NULL probe, Chapter 4 preparations, tick 67,669). Since D-36 the
# PC's trainees[] holds the GBA's 7 entries, so that loop stays in bounds; the
# flag stays for every other loop that indexes past an array.
HOST_ARCH    := -m32
HOST_CFLAGS  := $(HOST_ARCH) -std=gnu89 -O1 -g -fno-pie -funsigned-char -fno-toplevel-reorder \
                -fno-common -fno-strict-aliasing -fwrapv -fpermissive -malign-data=abi \
                -fno-delete-null-pointer-checks -fno-aggressive-loop-optimizations -fdata-sections -w
HOST_CPPFLAGS := -DPC_PORT=1 -DNONMATCHING=1 -Iinclude -Iport/include -include port/include/port.h
PORT_CFLAGS  := $(HOST_ARCH) -std=gnu11 -O1 -g -fno-pie -funsigned-char -fno-strict-aliasing \
                -fwrapv -Wall -Wno-unused-function
# Code goes above the GBA map; 0x08048000 (the i386 default) is inside the ROM window.
HOST_LDFLAGS := $(HOST_ARCH) -no-pie -Wl,-Ttext-segment=0x10000000 -Wl,--no-check-sections \
                -Wl,--wrap=OnMain

# Test builds (port-plan/DIVERGENCES.md TB-1): FE8PC_TESTBUILD=1 compiles the
# test-only code, all of it under #ifdef FE8PC_TESTBUILD: on the map, the keys
# Up Up Down Down Left Right Left Right B A open the game's own leftover debug
# menu (src/playerphase.c, src/bmdebug.c, src/gamecontrol.c, port/hw/frame.c),
# and in the window Shift is a second Select key (port/sdl3). Off by default:
# a release build compiles none of it, and its loaded code and data are those
# of a tree without this code (no extra object either; the build-id and debug
# info differ). The switch comes from the make command line only
# (port/testbuild.mk). A test build gets its own build directory, whose name
# ends in -test: BUILD with -test appended (below; Makefile.sdl,
# Makefile.mingw and port/test/Makefile pick theirs), and a BUILD given by the
# caller without -test is refused, so its objects never mix with a release
# build's. The flag is in the compile settings (COMPILE_STAMP).
# Checks: port/test/konami.mk.
include port/testbuild.mk
ifeq ($(FE8PC_TESTBUILD),1)
TESTBUILD_FLAGS := -DFE8PC_TESTBUILD=1
HOST_CPPFLAGS   += $(TESTBUILD_FLAGS)
PORT_CFLAGS     += $(TESTBUILD_FLAGS)
endif

# Compiler safety checks (port-plan/DECISIONS.md, 2026-10-22; audit CC-2 in
# port-plan/pending/folded/audit10-compiler.md): the stack protector and
# _FORTIFY_SOURCE are set here for every build, game and port code alike, and
# the same way with GCC and MinGW. Ubuntu's GCC turns them on by itself
# (-fstack-protector-strong, _FORTIFY_SOURCE=3, also -fstack-clash-protection)
# and MinGW's turns none on, so before this a Linux build stopped on a write
# past a local array ("*** stack smashing detected ***") where the GBA and the
# Windows exe carry on.
#   PORT_SAFETY_CHECKS=0, the builds the player runs (build/pc, build/pc-full,
#     Makefile.sdl's build/pc-sdl, Makefile.mingw's build/win32 and
#     build/win32-sdl): all of them off, so a retail overflow does there what
#     it does on the GBA. -fcf-protection=none only says so: GCC's -m32 code
#     has no CET marks either way.
#   PORT_SAFETY_CHECKS=1, every test build (FE8PC_TESTBUILD=1, and the Gate 0
#     binary and the sweep builds, whose port/test/Makefile `pc` sets it on its
#     sub-make's command line): the stack protector and _FORTIFY_SOURCE on, at
#     Ubuntu's own levels, so a retail overflow stops a bot's or a monkey's run
#     loudly. The Linux test builds' code is then what Ubuntu's defaults gave
#     it before (stack-clash probes stay at the compiler's default: on with GCC;
#     MinGW probes large frames with ___chkstk_ms anyway). MinGW links these
#     checks from mingw-w64's own libmingwex (__stack_chk_fail, __stack_chk_guard,
#     the __*_chk functions), not GCC's -lssp, which imports ADVAPI32.dll, and
#     port/Makefile.mingw lets an exe import only KERNEL32.dll and msvcrt.dll.
# From the make command line only (a value from the environment is replaced by
# the default). The flags are part of HOST_CFLAGS and PORT_CFLAGS, so the
# compile settings record them (B-16) and a change recompiles every object.
SAFETY_ON_CFLAGS  := -fstack-protector-strong -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3
SAFETY_OFF_CFLAGS := -fno-stack-protector -fno-stack-clash-protection -fcf-protection=none \
                     -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
ifneq ($(origin PORT_SAFETY_CHECKS),command line)
PORT_SAFETY_CHECKS := $(if $(filter 1,$(FE8PC_TESTBUILD)),1,0)
endif
# A test build with any other value (0, empty, "no" ...) is refused: only 1 turns them on.
ifeq ($(FE8PC_TESTBUILD),1)
ifneq ($(PORT_SAFETY_CHECKS),1)
$(error port/Makefile: FE8PC_TESTBUILD=1 with PORT_SAFETY_CHECKS=$(PORT_SAFETY_CHECKS): a test build always has the compiler safety checks (PORT_SAFETY_CHECKS=1))
endif
endif
SAFETY_CFLAGS := $(if $(filter 1,$(PORT_SAFETY_CHECKS)),$(SAFETY_ON_CFLAGS),$(SAFETY_OFF_CFLAGS))
HOST_CFLAGS   += $(SAFETY_CFLAGS)
PORT_CFLAGS   += $(SAFETY_CFLAGS)

# Shared game sources compiled unchanged for the spike (section 4, D1 recommendation).
GAME_SRCS := src/main.c src/bm.c src/hardware.c src/proc.c src/time.c src/rng.c \
             src/gamecontrol.c src/irq.c

# FULL=1: link every shared C file that compiles on the host (see `survey`).
# Replaced by port code: ramfunc.c, agb_sram.c. Left out by the plan:
# sio_multiboot.c. The rest do not compile yet (SPIKE.md). m4a.c, the sound
# driver's C half, is built since phase 3 with the m4a.o rules below.
# eventcall.c and bmdifficulty.c are built with the two host-only fixes below
# (DIVERGENCES.md B-6, B-7); before them their functions were generated stubs.
# bmguide.c (the Guide) is built since its PC_PORT compile fixes (P-20); before
# them Guide_Init, Guide_MainLoop and the Guide-flag functions were stubs, so
# the map menu's Guide opened nothing and BmGuideTextSetAllGreen set no flags.
# uiarena.c (the arena screens and the map menu's Suspend) is built since the
# same compile fix (P-24); before it StartSuspendPrompt and WriteSuspendPlayerIdle
# were stubs, so Suspend did nothing, and the Arena command opened nothing.
# banim-efxmagic-crimsoneye.c, banim-efxmagic-shine.c (the Crimson Eye and
# Shine spell animations) and eventfx-earthquake.c (the event earthquake) are
# built since round 10 with B-6's lvalue-cast copy below (C10-1); before it
# StartSpellAnimCrimsonEye and StartSpellAnimShine were stubs that started no
# spell proc, so a battle with either spell never ended.
FULL_EXCLUDE := src/ramfunc.c src/agb_sram.c src/sio_multiboot.c \
                src/chapterdata.c src/cp_data.c \
                src/fontgrp-data.c src/fontgrp.c src/menu_def.c
# Shared files that compile unchanged once the GBA `make` has generated their
# headers (jsonproc, gbagfx). They include those headers by repository-relative
# paths, hence -iquote . (as the GBA Makefile does). chapterdata.c holds
# GetROMChapterStruct: without it that is a generated stub returning NULL, and
# the save menu faults at frame 4,672 (port-plan/DECISIONS.md, frame-4672 fix
# (a); port-plan/DIVERGENCES.md). This is the decomp's own path, not a source
# change: the files are compiled as they are.
GENERATED_SRCS := src/chapterdata.c src/fontgrp-data.c src/fontgrp.c
GENERATED_HDRS := src/data/chapter_settings.h graphics/debug_font.4bpp.h
ifeq ($(FULL),1)
BUILD     := build/pc-full
GAME_SRCS := $(filter-out $(FULL_EXCLUDE),$(wildcard src/*.c))
# WITH_GENERATED (default 1 for FULL builds since the frame-4672 fixes; needs
# the GBA `make` in this tree first): also build GENERATED_SRCS, 347 files.
# WITH_GENERATED=0 gives the older build without them (344 files) in build/pc-full-nogen.
WITH_GENERATED ?= 1
ifeq ($(WITH_GENERATED),1)
GAME_SRCS += $(GENERATED_SRCS)
HOST_CPPFLAGS += -iquote .
else
BUILD     := build/pc-full-nogen
endif
endif
ifeq ($(FE8PC_TESTBUILD),1)
ifneq ($(filter command line override,$(origin BUILD)),)
ifeq ($(filter %-test,$(BUILD)),)
$(error port/Makefile: FE8PC_TESTBUILD=1 with BUILD=$(BUILD): a test build goes only to a directory whose name ends in -test, never to a release build's (BUILD=$(BUILD)-test, or leave BUILD out))
endif
else
BUILD := $(BUILD)-test
endif
endif

# Port layer (replaces crt0.s, libagbsyscall.s, ramfunc.c, agb_sram.c, arm.s),
# plus the software renderer (port/render). port/render/test_*.c are the
# renderer's stand-alone unit tests (they define main), not part of the game.
# port/reforged: the played builds' PC-only Reforged Options (screen, options, settings file)
# (port/include/port_reforged.h); game-side code, compiled as the game is (the
# rule below), and empty in every test build (PORT_PLAYED_BUILD).
PORT_SRCS := $(wildcard port/hw/*.c port/bios/*.c port/asm/*.c port/save/*.c port/audio/*.c) \
             $(filter-out port/render/test_%.c,$(wildcard port/render/*.c)) \
             $(wildcard port/reforged/*.c)

GAME_OBJS := $(GAME_SRCS:%.c=$(BUILD)/%.o)
PORT_OBJS := $(PORT_SRCS:%.c=$(BUILD)/%.o)

# hardware.c:818 has asm("swi 3") inside EnterSleepMode (debug menu only).
# The flags this file adds for single objects are named variables, because the
# compile settings below record them (COMPILE_SETTINGS).
NO_ASM_CPPFLAGS := '-Dasm(x)=((void)0)'
$(BUILD)/src/hardware.o: HOST_CPPFLAGS += $(NO_ASM_CPPFLAGS)

# Sound driver, C half (port-plan/audio-design.md 1.6; its asm half is
# port/audio/m4a_1_pc.c). m4a.c:297 has asm("swi 0x2A") in
# MusicPlayerJumpTableCopy, which has no callers: hidden as for hardware.o
# (DIVERGENCES.md B-13; audio-design's B-6). Its variables (gSoundInfo,
# gMPlayJumpTable, gCgbChans, gMPlayMemAccArea; SoundMainRAM_Buffer in its own
# .bss.code) go to their GBA addresses like every game variable:
# -fdata-sections above, gen_host_link.py and check_ram_addrs.py (B-12, which
# covers the placement half of audio-design's B-7; its SoundMainRAM half is
# B-14, in gen_host_link.py).
$(BUILD)/src/m4a.o: HOST_CPPFLAGS += $(NO_ASM_CPPFLAGS)

# B-7: bmdifficulty.c defines both const and non-const CONST_DATA objects, and
# GCC will not put both in the one named section (".data") that CONST_DATA
# asks for. For this file port.h leaves the section to GCC: .rodata for the
# const objects, .data for the rest. Only the host's own copies move; the GBA
# addresses come from the map, and the EWRAM sections are placed as before.
BMDIFFICULTY_CPPFLAGS := -DPORT_CONST_DATA_DEFAULT_SECTION
$(BUILD)/src/bmdifficulty.o: HOST_CPPFLAGS += $(BMDIFFICULTY_CPPFLAGS)

.PHONY: all survey clean
all: $(BUILD)/fe8pc

# The port rules have no automatic header dependencies: a change to the port's
# shared headers rebuilds every port object (port/win32 among them, through
# Makefile.mingw's PORT_SRCS). A struct whose size changes (struct PortArgs)
# would otherwise leave an object built with the old size. (After "all": the
# first rule of this file is the default goal.)
$(PORT_OBJS): port/include/port_hw.h port/platform.h port/include/port_controls.h

# Each compile also keeps its compiler messages in OBJ.warn (empty when clean),
# next to the object, and shows them as before. port/check-all.mk's warnings
# gate reads these logs (port/tools/check_all.py --warn-dir), so it also sees
# the warnings of objects that a later make did not recompile.
WARNLOG = 2> $@.warn; status=$$?; cat $@.warn >&2; exit $$status

# The compile settings of this build directory: every object depends on this
# file, which is rewritten only when the settings change, so a new compiler
# flag (or the warning logs above, "warnlog-1") recompiles every object of an
# existing build once. Make does not see a flag change by itself.
# COMPILE_SETTINGS is expanded here, once (:=). GNU make hands a target's
# target-specific variables down to its prerequisites, so with a recursive `=`
# the stamp took the flags of whichever object reached it first: `make -f
# port/Makefile build/pc/src/hardware.o` broke the recipe's shell quoting on
# hardware.o's '-Dasm(x)=...', and a FULL=1 goal of bmdifficulty.o rewrote the
# stamp, so the next plain make recompiled every object. The per-object flags
# are recorded here by name instead, so a change to one of them recompiles too
# (every object, as for any other flag). Makefiles that include this one
# append their own (Makefile.sdl); port/test's Gate 0 build keeps its proc.o
# flag in its own stamp (gate0_wraps). The recipe reads the settings from the
# environment, so no quote in them reaches a shell command line.
COMPILE_STAMP    := $(BUILD)/compile-settings.txt
COMPILE_SETTINGS := warnlog-1 | $(CC) | $(HOST_CFLAGS) | $(HOST_CPPFLAGS) | $(PORT_CFLAGS) \
                    | hardware.o m4a.o: $(NO_ASM_CPPFLAGS) | bmdifficulty.o: $(BMDIFFICULTY_CPPFLAGS)
$(COMPILE_STAMP): export FE8_COMPILE_SETTINGS = $(COMPILE_SETTINGS)
$(COMPILE_STAMP): FORCE_SETTINGS
	@mkdir -p $(@D); printf '%s\n' "$$FE8_COMPILE_SETTINGS" | cmp -s - $@ \
	    || printf '%s\n' "$$FE8_COMPILE_SETTINGS" > $@
.PHONY: FORCE_SETTINGS
FORCE_SETTINGS:

$(BUILD)/src/%.o: src/%.c port/include/port.h $(COMPILE_STAMP)
	@mkdir -p $(@D)
	$(CC) $(HOST_CFLAGS) $(HOST_CPPFLAGS) -c $< -o $@ $(WARNLOG)

# port.h includes port_gbabus.h (the GBA bus loads, the open-bus latch words
# and the latch rule) into every game object, as the prelude above; the two
# port/hw files and port/asm/arm_pc.c (the flood's GBA-bus cells) that use it
# include it too. A change to it recompiles them,
# as one to port.h does. (No -MD tracking: other headers still need a clean
# build after a change.)
$(GAME_OBJS) $(BUILD)/port/hw/gbabus.o $(BUILD)/port/hw/nullprobe.o $(BUILD)/port/asm/arm_pc.o: port/include/port_gbabus.h

# B-6: eventcall.c has six lines like "(u16)gBmSt.camera.x &= ~2;", a cast used
# as an lvalue (an agbcc extension that GCC rejects). The host compiles a copy
# made in the build directory, with each "(u16)gBmSt.camera.x" or ".y" written
# as "*(u16 *)&gBmSt.camera.x", which is what the cast means for these 16-bit
# fields. Nothing else changes: the copy has the same lines, and #line keeps
# the src/ name and line numbers in messages and debug info. A new cast of this
# kind elsewhere in the file still fails the build, so none is missed silently.
# Round 10 (C10-1) adds three files with the same kind of cast, each storing
# the same bits as agbcc does:
#   eventfx-earthquake.c:78-79 "(u16)gBmSt.camera.x &= 0xFFFC;" (the pattern above);
#   banim-efxmagic-shine.c:421 "(s16) proc->unk30 += 2;" (a u16 field, taken as
#     "*(s16 *)&proc->unk30 += 2": the same 16-bit store of the low half);
#   banim-efxmagic-crimsoneye.c:1196, 1201 "(struct Anim *)(proc->unk44) = ..."
#     (an int field holding a pointer, taken as "*(struct Anim **)&proc->unk44 =
#     ...": the same 32-bit store, as pointers are 32 bits on the i386 host).
LVCAST_SRCS := src/eventcall.c src/eventfx-earthquake.c src/banim-efxmagic-shine.c \
               src/banim-efxmagic-crimsoneye.c
LVCAST_SED  := -e 's/\(u16\)(gBmSt\.camera\.[xy])\b/*(u16 *)\&\1/g' \
               -e 's/\(s16\) (proc->unk30) \+=/*(s16 *)\&\1 +=/g' \
               -e 's/\(struct Anim \*\)\((proc->unk4[48])\) =/*(struct Anim **)\&\1 =/g'
.SECONDARY: $(LVCAST_SRCS:src/%.c=$(BUILD)/lvcast/%.c)
$(BUILD)/lvcast/%.c: src/%.c port/Makefile
	@mkdir -p $(@D)
	{ printf '#line 1 "%s"\n' '$<'; \
	  sed -E $(LVCAST_SED) '$<'; } > $@
$(LVCAST_SRCS:%.c=$(BUILD)/%.o): $(BUILD)/src/%.o: $(BUILD)/lvcast/%.c port/include/port.h $(COMPILE_STAMP)
	@mkdir -p $(@D)
	$(CC) $(HOST_CFLAGS) $(HOST_CPPFLAGS) -iquote src -c $< -o $@ $(WARNLOG)

# GENERATED_SRCS need the headers the GBA `make` generates; say so if they are missing.
ifeq ($(FULL)$(WITH_GENERATED),11)
$(GENERATED_SRCS:%.c=$(BUILD)/%.o): | $(GENERATED_HDRS)
$(GENERATED_HDRS):
	@echo "port/Makefile: error: $@ is missing; run the GBA make in this tree first (it generates it)" >&2
	@exit 1
endif

# port/reforged (above) is game-side code: the game's flags and port.h, as
# src/ is (a shorter stem, so this rule wins over the next for these files).
$(BUILD)/port/reforged/%.o: port/reforged/%.c port/include/port.h port/include/port_reforged.h port/reforged/reforged_rando.h port/platform.h $(COMPILE_STAMP)
	@mkdir -p $(@D)
	$(CC) $(HOST_CFLAGS) $(HOST_CPPFLAGS) -c $< -o $@ $(WARNLOG)

# Reforged Options' save mark (port-rfo-casual): a played build's savefile_pc.o
# gets PORT_RFO_SAVEMARK=1, its two calls into port/reforged/reforged_savemark.c
# (the sidecar <sav>.reforged). A test build (FE8PC_TESTBUILD) and the Gate 0
# binary (FE8PC_DEBUG, which port/test/pc/gate0.mk adds to HOST_CPPFLAGS after
# this file is read; the recipe expands it when it runs) compile the file as
# the next rule does, so their object is unchanged.
RFO_SAVEMARK_DEFS = $(if $(filter -DFE8PC_DEBUG=1 -DFE8PC_TESTBUILD=1,$(HOST_CPPFLAGS) $(PORT_CFLAGS)),,-DPORT_RFO_SAVEMARK=1)
$(BUILD)/port/save/savefile_pc.o: port/save/savefile_pc.c port/include/port_reforged.h $(COMPILE_STAMP)
	@mkdir -p $(@D)
	$(CC) $(PORT_CFLAGS) $(RFO_SAVEMARK_DEFS) -DPC_PORT=1 -Iinclude -Iport/include -Iport/render -c $< -o $@ $(WARNLOG)

# Reforged Options' fast-forward (port-rfo-ff): the window builds'
# port/sdl3/platform_sdl3.o gets PORT_RFO_FASTFORWARD=1, its fast-forward
# block, on the same condition as port/reforged/reforged_fastforward.c's
# PORT_PLAYED_BUILD (port/include/port.h: neither FE8PC_TESTBUILD nor
# FE8PC_DEBUG), as the save mark above does: a window build with either flag
# then has neither the calls nor the functions, where keying the block on
# FE8PC_TESTBUILD alone would fail to link one with FE8PC_DEBUG (rfo-ff
# refute note 7). Port/Makefile.sdl's SDL flags still apply (a pattern
# variable on port/sdl3); the headless builds never build this object.
RFO_FF_DEFS = $(if $(filter -DFE8PC_DEBUG=1 -DFE8PC_TESTBUILD=1,$(HOST_CPPFLAGS) $(PORT_CFLAGS)),,-DPORT_RFO_FASTFORWARD=1)
$(BUILD)/port/sdl3/platform_sdl3.o: port/sdl3/platform_sdl3.c port/include/port_reforged.h $(COMPILE_STAMP)
	@mkdir -p $(@D)
	$(CC) $(PORT_CFLAGS) $(RFO_FF_DEFS) -DPC_PORT=1 -Iinclude -Iport/include -Iport/render -c $< -o $@ $(WARNLOG)

# Reforged Options' map HP bars & warnings (port-rfo-hpbars): the renderer's
# port/render/obj_line.o gets PORT_RFO_MAPOVERLAY=1, the pass that draws
# port/reforged/reforged_hpbars.c's pictures (gReforgedOverlay), on the same
# PORT_PLAYED_BUILD condition as the save mark and fast-forward above. Every
# test build (FE8PC_TESTBUILD, the Gate 0 binary's FE8PC_DEBUG) compiles it as
# the next rule does, so its object is unchanged; the renderer's unit tests
# (render-tests, below) never have it.
RFO_OVL_DEFS = $(if $(filter -DFE8PC_DEBUG=1 -DFE8PC_TESTBUILD=1,$(HOST_CPPFLAGS) $(PORT_CFLAGS)),,-DPORT_RFO_MAPOVERLAY=1)
$(BUILD)/port/render/obj_line.o: port/render/obj_line.c port/include/port_reforged.h $(COMPILE_STAMP)
	@mkdir -p $(@D)
	$(CC) $(PORT_CFLAGS) $(RFO_OVL_DEFS) -DPC_PORT=1 -Iinclude -Iport/include -Iport/render -c $< -o $@ $(WARNLOG)

$(BUILD)/port/%.o: port/%.c $(COMPILE_STAMP)
	@mkdir -p $(@D)
	$(CC) $(PORT_CFLAGS) -DPC_PORT=1 -Iinclude -Iport/include -Iport/render -c $< -o $@ $(WARNLOG)

# Renderer unit tests (host only, no game code or ROM): make -f port/Makefile render-tests
RENDER_TEST_CFLAGS := $(HOST_ARCH) -std=gnu11 -O1 -g -Wall -Werror -fno-pie -no-pie -funsigned-char \
                      -fno-strict-aliasing -fwrapv -DPC_PORT=1 -Iinclude -Iport/include -Iport/render
$(BUILD)/render-tests/test_bg: port/render/test_bg.c port/render/bg_line.c port/render/bg_affine.c \
                               port/render/bg_line.h
	@mkdir -p $(@D)
	$(CC) $(RENDER_TEST_CFLAGS) -DRENDER_BG_TEST port/render/test_bg.c port/render/bg_line.c \
	    port/render/bg_affine.c -o $@
$(BUILD)/render-tests/test_obj: port/render/test_obj.c port/render/obj_line.c port/render/window.c \
                                port/render/blend.c port/render/out_rgba.c port/render/obj_line.h
	@mkdir -p $(@D)
	$(CC) $(RENDER_TEST_CFLAGS) port/render/test_obj.c port/render/obj_line.c port/render/window.c \
	    port/render/blend.c port/render/out_rgba.c -o $@
.PHONY: render-tests
render-tests: $(BUILD)/render-tests/test_bg $(BUILD)/render-tests/test_obj
	$(BUILD)/render-tests/test_bg
	$(BUILD)/render-tests/test_obj

# D-18: compare every struct and union layout with agbcc's (needs the GBA tools
# and a FULL=1 build for the file list): make -f port/Makefile FULL=1 layout-check
# Writes build/layout/report.txt; fails if any type differs.
.PHONY: layout-check
layout-check: $(BUILD)/fe8pc
	$(PYTHON) port/tools/layout_probe.py --out build/layout

# Risk 12 (code pointers in ROM data), step 1: converted copies of data/*.s in
# the build directory (literal GBA addresses -> symbol references), assembled
# with the GBA assembler. The GBA tree is only read, so the ROM stays identical.
FIXPTR_DIR    := $(BUILD)/fixptr
FIXPTR_REPORT := $(BUILD)/fixptr_unresolved.txt
# The tracked list is the played FULL build's (build/pc-full); a FULL test build
# (FE8PC_TESTBUILD=1, build/pc-full-test) writes its own beside its objects, so it
# no longer rewrites the tracked file with its paths (integration 11).
ifeq ($(FULL),1)
ifneq ($(FE8PC_TESTBUILD),1)
FIXPTR_REPORT := port/tools/fixptr_unresolved.txt
endif
endif
$(FIXPTR_DIR)/stamp: port/tools/fixptr.py $(GBA_ELF) $(GBA_ROM) $(wildcard data/*.s)
	$(PYTHON) port/tools/fixptr.py convert --elf $(GBA_ELF) --rom $(GBA_ROM) --gba-dir $(GBA_DIR) \
	    --out $(FIXPTR_DIR)
	@touch $@

# Step 2: every relocated ROM-data word whose host value differs (code pointers,
# pointers to data the host builds). port/hw/romreloc.c applies it at startup.
$(BUILD)/romreloc.c: $(FIXPTR_DIR)/stamp $(GBA_MAP) $(GAME_OBJS) $(PORT_OBJS)
	$(PYTHON) port/tools/fixptr.py relocs --elf $(GBA_ELF) --map $(GBA_MAP) --rom $(GBA_ROM) \
	    --gba-dir $(GBA_DIR) --fixptr-dir $(FIXPTR_DIR) --host-objs $(GAME_OBJS) $(PORT_OBJS) \
	    --out-c $@ --report $(FIXPTR_REPORT)

$(BUILD)/romreloc.o: $(BUILD)/romreloc.c $(COMPILE_STAMP)
	$(CC) $(PORT_CFLAGS) -fno-builtin -w -c $< -o $@ $(WARNLOG)

# The stub generator needs the undefined-symbol list of the real objects. It
# also places every RAM variable at its GBA address (B-12), from the GBA map,
# the GBA ELF and the GBA objects in $(GBA_DIR)/src.
$(BUILD)/gba_mem.ld $(BUILD)/autostubs.c &: $(GAME_OBJS) $(PORT_OBJS) $(BUILD)/romreloc.o \
                                            port/tools/gen_host_link.py $(GBA_MAP) $(GBA_ELF)
	$(PYTHON) port/tools/gen_host_link.py --map $(GBA_MAP) --elf $(GBA_ELF) --gba-dir $(GBA_DIR) \
	    --out-ld $(BUILD)/gba_mem.ld --out-stubs $(BUILD)/autostubs.c \
	    --game-objs $(GAME_OBJS) --port-objs $(PORT_OBJS) $(BUILD)/romreloc.o

$(BUILD)/autostubs.o: $(BUILD)/autostubs.c $(COMPILE_STAMP)
	$(CC) $(PORT_CFLAGS) -w -c $< -o $@ $(WARNLOG)

# B-12: after the link, every RAM symbol must be at its GBA address (host-built
# variables, statics included, and the absolute ones); any other address fails
# the build and removes the binary. Usage: $(RAM_CHECK) --binary EXE --map EXE.map
# (Makefile.sdl uses it too). Re-run on an existing build: make ... ram-check
RAM_CHECK = $(PYTHON) port/tools/check_ram_addrs.py --gba-map $(GBA_MAP) --gba-elf $(GBA_ELF) \
            --gba-dir $(GBA_DIR) --game-objs $(GAME_OBJS)

$(BUILD)/fe8pc: $(GAME_OBJS) $(PORT_OBJS) $(BUILD)/romreloc.o $(BUILD)/autostubs.o $(BUILD)/gba_mem.ld \
                port/tools/check_ram_addrs.py
	$(CC) $(HOST_LDFLAGS) -Wl,-T,$(BUILD)/gba_mem.ld -Wl,-Map,$(BUILD)/fe8pc.map \
	    $(GAME_OBJS) $(PORT_OBJS) $(BUILD)/romreloc.o $(BUILD)/autostubs.o -lm -o $@
	$(RAM_CHECK) --binary $@ --map $(BUILD)/fe8pc.map || { rm -f $@; exit 1; }

.PHONY: ram-check
ram-check: $(BUILD)/fe8pc
	$(RAM_CHECK) --binary $(BUILD)/fe8pc --map $(BUILD)/fe8pc.map

# Compile every shared C file once, to measure how much of the tree builds as-is.
SURVEY_SRCS := $(wildcard src/*.c)
SURVEY_OBJS := $(SURVEY_SRCS:src/%.c=$(BUILD)/survey/%.o)
$(BUILD)/survey/hardware.o: HOST_CPPFLAGS += $(NO_ASM_CPPFLAGS)
$(BUILD)/survey/%.o: src/%.c port/include/port.h
	@mkdir -p $(@D)
	@if $(CC) $(HOST_CFLAGS) $(HOST_CPPFLAGS) -c $< -o $@ 2> $@.err; then \
	    echo "OK   $<" > $@.status; else echo "FAIL $<" > $@.status; touch $@; fi
survey: $(SURVEY_OBJS)
	@cat $(BUILD)/survey/*.status | sort > $(BUILD)/survey/result.txt
	@echo "survey: $$(grep -c '^OK' $(BUILD)/survey/result.txt) compiled," \
	      "$$(grep -c '^FAIL' $(BUILD)/survey/result.txt) failed (see $(BUILD)/survey/result.txt)"

clean:
	rm -rf $(BUILD)
