# Checks of the test-build shortcut (port-plan/DIVERGENCES.md TB-1 to TB-6;
# port-plan/pending/merged/konami.md). In a test
# build (FE8PC_TESTBUILD=1, port/Makefile) the keys Up Up Down Down Left Right
# Left Right B A, typed on the map in the player phase, open the game's own
# leftover debug menu; in a release build they are ordinary keys. Run from the
# repository root after the GBA `make` (fireemblem8.gba must be OK):
#
#   make -f port/test/konami.mk konami-release  # release build + keys against the GBA + keys
#   make -f port/test/konami.mk konami-test     # test build against the GBA running the same calls
#   make -f port/test/konami.mk konami          # both
#
# Keys: inputs/konami_ch2.keys. The Prologue to its first free player phase,
# the cursor to an empty tile, the keys (A in tick 2076), then the debug
# menu's Map item set to Chapter 2 and taken (A in tick 2168), then Start every
# 30 ticks. Output: KONAMI_OUT (build/konami). Frames there come from the
# ROM: never commit them. port/test/Makefile is included for GBA_BP, OUT and
# the oracle rule, and its targets do the Gate 0 work.
#
# konami-release: Gate 0 (port/test/Makefile gate0) with these keys and the
#   release build, to tick KONAMI_RELEASE_TICKS. The sequence's A opens the
#   map menu, as on the GBA. Passes on MATCH or LAG-ONLY.
#
# konami-test: the test build (TEST_PC_BUILD) against the GBA run by
#   oracle/konami_capture.lua, which makes on the GBA the two calls the test
#   build adds to the game: StartDebugMenu on the map menu the A opened (the
#   debug build's R in the map menu) and the stand-in for the empty nullsub_9
#   at the Map item's jump (src/gamecontrol.c). Everything else is the retail
#   ROM. Passes when
#   - the test build's log has the menu opening once, in tick 2076, and
#     chapterIndex is 2 from tick 2168 with Chapter 2's map load begun
#     (tools/konami_check.py run; its frames are also written as PNG);
#   - the lag replay's frames equal the GBA's everywhere except on the debug
#     menu's Weather row while the menu is open (tools/konami_check.py
#     frames): the GBA decodes that row's text from RAM that holds host
#     pointers on the PC (src/bmdebug.c).
#   The Gate 0 compare of the lag replay with the GBA (as the route rows do)
#   is printed as well; its verdict is not the pass, since the Weather row
#   makes every menu frame differ. Every other field should be equal on every
#   tick.
#   The run ends after tick 2396 (KONAMI_TICKS): in tick 2397 both the GBA and
#   the test build pop the event slot queue when it is empty (Chapter 2 runs as
#   a skirmish, since the jump skips the world map; DIVERGENCES.md TB-2), and the
#   GBA hangs while the PC faults.

include port/test/Makefile
.DEFAULT_GOAL := konami

KONAMI_KEYS          ?= port/test/inputs/konami_ch2.keys
KONAMI_OUT           ?= build/konami
KONAMI_A_TICK        ?= 2076
KONAMI_JUMP_TICK     ?= 2168
KONAMI_TICKS         ?= 2397
# Release: to the Map item's A (tick 2168). There the release build's A picks
# Unit in the map menu, and the unit list reads through NULL in
# GetUnitSupporterCount (bmreliance.c:24), a NULL-read site the PC does not
# guard yet (DIVERGENCES.md P-19): it faults at tick 2175.
KONAMI_RELEASE_TICKS ?= 2168
# Extra frames (GATE0_SHOT_TICKS; every 60th tick has one anyway): before the
# A, the menu opening, the Map item at Chapter 1 and 2, the jump, and
# Chapter 2's title.
KONAMI_SHOTS         ?= 2068,2075-2079,2108,2135,2137,2153,2167-2171,2312
# The debug menu's Weather row (item 3: tiles y 6-7) while the menu is open.
KONAMI_MENU_TICKS    ?= 2077-2168
KONAMI_WEATHER_LINES ?= 48-63
TEST_PC_BUILD        ?= build/pc-gate0-test
# fireemblem8.map: PlayerPhase_MainIdle, StartOrphanMenuAdjusted, nullsub_9;
# StartDebugMenu; nullsub_9, Proc_EndEach, Proc_Start, Proc_Goto, gProcScr_GameControl.
KONAMI_BP            := konami_idle@0x0801C940:ret,konami_menu@0x0804EB98:ret,konami_null@0x08009FD8
KONAMI_DEBUG_MENU    := 0x0801BB1C
KONAMI_JUMP          := 0x08009FD8,0x08003078,0x08002C7C,0x08002F24,0x085916D4
KT                   := $(KONAMI_OUT)/test

.PHONY: konami konami-release konami-test konami-test-pc

konami: konami-release konami-test

konami-test-pc:
	$(MAKE) -f port/test/Makefile pc PC_BUILD=$(TEST_PC_BUILD) FE8PC_TESTBUILD=1

konami-release:
	GATE0_SHOT_TICKS=$(KONAMI_SHOTS) \
	    $(MAKE) -f port/test/Makefile gate0 KEYS=$(KONAMI_KEYS) TICKS=$(KONAMI_RELEASE_TICKS) \
	    OUT=$(KONAMI_OUT)/release

konami-test: $(OUT)/mgba_lua_run konami-test-pc
	@rm -rf $(KT); mkdir -p $(KT)/gba_frames $(KT)/pc_frames $(KT)/lag_frames
	MGBA_RUN_BP=$(GBA_BP),$(KONAMI_BP) KONAMI_TICK=$(KONAMI_A_TICK) KONAMI_DEBUG_MENU=$(KONAMI_DEBUG_MENU) \
	KONAMI_JUMP=$(KONAMI_JUMP) GATE0_KEYS=$(KONAMI_KEYS) GATE0_TICKS=$(KONAMI_TICKS) \
	GATE0_SHOT_TICKS=$(KONAMI_SHOTS) GATE0_OUT=$(KT)/gba.jsonl GATE0_RAW=$(KT)/gba.raw \
	GATE0_SHOTDIR=$(KT)/gba_frames \
	    $(OUT)/mgba_lua_run $(GBA_ROM) port/test/oracle/konami_capture.lua $(GBA_MAX_FRAMES)
	@GATE0_KEYS=$(KONAMI_KEYS) GATE0_TICKS=$(KONAMI_TICKS) GATE0_SHOT_TICKS=$(KONAMI_SHOTS) \
	GATE0_OUT=$(KT)/pc.jsonl GATE0_RAW=$(KT)/pc.raw GATE0_SHOTDIR=$(KT)/pc_frames \
	FE8PC_BLEND=mgba8 FE8PC_MGBA_BGCLIP=1 FE8PC_FRAMES=0 timeout $(PC_TIMEOUT) $(TEST_PC_BUILD)/fe8pc $(GBA_DIR)/baserom.gba \
	    > $(KT)/pc.log 2>&1 || { echo "fe8pc (test build) failed, see $(KT)/pc.log"; tail -6 $(KT)/pc.log; }
	@GATE0_LAG_FROM=$(KT)/gba.jsonl GATE0_KEYS=$(KONAMI_KEYS) GATE0_TICKS=$(KONAMI_TICKS) \
	GATE0_SHOT_TICKS=$(KONAMI_SHOTS) GATE0_OUT=$(KT)/pc_lagreplay.jsonl GATE0_RAW=$(KT)/pc_lagreplay.raw \
	GATE0_SHOTDIR=$(KT)/lag_frames FE8PC_BLEND=mgba8 FE8PC_MGBA_BGCLIP=1 FE8PC_FRAMES=0 timeout $(PC_TIMEOUT) \
	    $(TEST_PC_BUILD)/fe8pc $(GBA_DIR)/baserom.gba > $(KT)/pc_lagreplay.log 2>&1 \
	    || { echo "fe8pc (test build, lag replay) failed, see $(KT)/pc_lagreplay.log"; exit 1; }
	-@$(PYTHON) port/test/tools/gate0_compare.py --gba $(KT)/gba.jsonl --pc $(KT)/pc_lagreplay.jsonl \
	    --gba-raw $(KT)/gba.raw --pc-raw $(KT)/pc_lagreplay.raw --gba-elf $(GBA_DIR)/fireemblem8.elf \
	    --pc-elf $(TEST_PC_BUILD)/fe8pc --ticks $(KONAMI_TICKS) --masks $(MASKS) \
	    --json $(KT)/compare.json > $(KT)/compare.txt 2>&1; \
	    awk '/== RESULT/{p=1} /^mask OL-1/{p=0} p' $(KT)/compare.txt; grep "^GATE 0" $(KT)/compare.txt
	$(PYTHON) port/test/tools/konami_check.py run --trace $(KT)/pc.jsonl --log $(KT)/pc.log \
	    --menu-tick $(KONAMI_A_TICK) --jump-tick $(KONAMI_JUMP_TICK) --chapter 2 --frames $(KT)/pc_frames
	$(PYTHON) port/test/tools/frame15.py batch $(KT)/gba_frames $(KT)/lag_frames $(KT)/diff > $(KT)/frames.jsonl
	$(PYTHON) port/test/tools/konami_check.py frames --jsonl $(KT)/frames.jsonl \
	    --ticks $(KONAMI_MENU_TICKS) --lines $(KONAMI_WEATHER_LINES)
