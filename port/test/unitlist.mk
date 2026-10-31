# Checks of the map menu's Unit list (port-plan/pending/unitlist.md; proposed
# DIVERGENCES.md D-39, src/bmreliance.c GetUnitSupporterCount). Opening the
# list runs CanUnitSupportNow for every support partner, which calls
# GetUnitSupporterCount(NULL) for a partner who is not in the army: on the GBA
# that reads open bus and returns 0; before D-39 the PC faulted there (tick
# 2051 of these keys). Run from the repository root after the GBA `make`
# (fireemblem8.gba must be OK):
#
#   make -f port/test/unitlist.mk unitlist       # Gate 0 through the Unit list (MATCH or LAG-ONLY)
#   make -f port/test/unitlist.mk unitlist-null  # the PC run with the NULL probe: no BIOS-area read
#   make -f port/test/unitlist.mk                # both
#
# Keys: inputs/unitlist_ch0.keys. The Prologue to its first free player phase
# (route_ch0_prologue.keys ticks 0-1995), then the map menu's Unit list opened
# (A in tick 2044), both units and all six pages scrolled, Eirika's and Seth's
# stat screens opened from it (R), and back to the map; 3,000 ticks.
#
# unitlist: port/test/Makefile gate0 with these keys (GBA trace, PC trace, lag
#   replay, affine replay, compare) into UNITLIST_OUT. The PC must reach every
#   tick with no fault; passes on MATCH or LAG-ONLY (gate0 treats the
#   compare's exit 3 as a pass). Extra frames: UNITLIST_SHOTS (GATE0_SHOT_TICKS)
#   adds frames on both sides. With the frames of tick 2401 added, that frame
#   differs on 98 pixels of lines 63-65 (P-17: the GBA redraws Eirika's
#   Support-page row while the raster passes it; pending/unitlist.md).
# unitlist-null: the PC Gate 0 build run with FE8PC_NULLPROBE=1 (README.md,
#   "NULL probe"), plain and with the GBA's lag VBlanks (after `unitlist`, which
#   writes the GBA trace); fails when the probe records any BIOS-area or
#   NULL-page read. Before D-39 it records 4 sites at tick 2051.

include port/test/Makefile
.DEFAULT_GOAL := unitlist-all

UNITLIST_KEYS  ?= port/test/inputs/unitlist_ch0.keys
UNITLIST_TICKS ?= 3000
UNITLIST_OUT   ?= build/unitlist
UNITLIST_SHOTS ?=

.PHONY: unitlist-all unitlist unitlist-null

# In order, also under make -j (unitlist-null's lag run reads unitlist's GBA trace).
unitlist-all:
	$(MAKE) -f port/test/unitlist.mk unitlist
	$(MAKE) -f port/test/unitlist.mk unitlist-null

unitlist:
	GATE0_SHOT_TICKS=$(UNITLIST_SHOTS) \
	    $(MAKE) -f port/test/Makefile gate0 KEYS=$(UNITLIST_KEYS) TICKS=$(UNITLIST_TICKS) OUT=$(UNITLIST_OUT)

unitlist-null: pc
	@mkdir -p $(UNITLIST_OUT)
	@for run in plain lag; do \
	    lag=""; if [ $$run = lag ]; then \
	        [ -f $(UNITLIST_OUT)/gba.jsonl ] || { echo "no $(UNITLIST_OUT)/gba.jsonl: run the unitlist target first"; exit 2; }; \
	        lag="GATE0_LAG_FROM=$(UNITLIST_OUT)/gba.jsonl"; fi; \
	    env $$lag FE8PC_NULLPROBE=1 FE8PC_NULLPROBE_OUT=$(UNITLIST_OUT)/nullprobe_$$run.jsonl \
	    GATE0_KEYS=$(UNITLIST_KEYS) GATE0_TICKS=$(UNITLIST_TICKS) GATE0_OUT=$(UNITLIST_OUT)/pc_null_$$run.jsonl \
	    FE8PC_FRAMES=0 timeout $(PC_TIMEOUT) $(PC_BUILD)/fe8pc $(GBA_DIR)/baserom.gba > $(UNITLIST_OUT)/pc_null_$$run.log 2>&1 \
	        || { echo "fe8pc (NULL probe, $$run) stopped, see $(UNITLIST_OUT)/pc_null_$$run.log"; exit 1; }; \
	    grep "^\[nullprobe\] \(new site\|exit\)" $(UNITLIST_OUT)/pc_null_$$run.log; \
	    grep -q "^\[nullprobe\] exit: 0 sites, 0 hits, tick $(UNITLIST_TICKS)," $(UNITLIST_OUT)/pc_null_$$run.log \
	        || { echo "UNIT LIST NULL PROBE ($$run): FAIL"; exit 1; }; \
	    echo "UNIT LIST NULL PROBE ($$run): no BIOS-area or NULL-page read in $(UNITLIST_TICKS) ticks"; \
	done
