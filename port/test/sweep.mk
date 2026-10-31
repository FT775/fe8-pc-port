# Bug sweeps (port/test/README.md, "Bug sweeps"; port-plan/pending/sweep11.md):
# the Gate 0 test binary (port/test/Makefile `pc`) built again with diagnostic
# compiler flags, each in its own build directory. Test harness only: never a
# shipped build, and no file outside port/test changes for them.
#
#   ubsan    build/pc-gate0-ubsan/fe8pc: game and port code compiled with
#            -fsanitize=$(UBSAN_CHECKS) (default undefined,bounds) and
#            -fsanitize-recover=all, linked with port/test/pc/ubsan_log.c: every
#            check is logged and the run goes on; one line per site with its
#            hit count, first and last tick and the first hit's callers.
#   zero     build/pc-gate0-zero/fe8pc: -ftrivial-auto-var-init=zero
#   pattern  build/pc-gate0-pattern/fe8pc: -ftrivial-auto-var-init=pattern
#            (GCC fills every automatic variable that has no initialiser, at
#            its declaration). The same buttons into both: any traced field or
#            frame that differs means game (or port) code read an uninitialised
#            local and the value it found changed the game.
#
# Run from the repository root after the GBA `make` (fireemblem8.gba: OK):
#
#   make -f port/test/sweep.mk sweep-pc SWEEP=ubsan|zero|pattern   # one binary
#   make -f port/test/sweep.mk sweep-builds                        # all three
#   make -f port/test/sweep.mk sweep-route SWEEP=NAME KEYS=FILE TICKS=N [SWEEP_GBA=DIR]
#       does the binary still follow the GBA? The lag replay of KEYS with that
#       binary compared with the GBA (as port/test/Makefile `route` checks the
#       Gate 0 binary), and its cost (wall and user seconds per 10,000 ticks).
#       SWEEP=gate0 runs the Gate 0 binary itself, the baseline. For ubsan it
#       also writes and sorts the site table.
#   make -f port/test/sweep.mk ubsan-run KEYS=FILE TICKS=N [SWEEP_LAG=1 | SWEEP_GBA=DIR]
#       the UBSan site table of one run (no GBA compare).
#   make -f port/test/sweep.mk ubsan-selftest
#       the UBSan runtime against GCC's libubsan on a small test program.
#   make -f port/test/sweep.mk uninit-compare KEYS=FILE TICKS=N [SWEEP_LAG=1 | SWEEP_GBA=DIR]
#       zero and pattern with the same buttons; the first differing tick and
#       field (port/test/tools/sweep_pair.py). Exit 0: equal on every tick.
#
# Where a run starts: GATE0_SRAM_IN=SAVE.sav (a chapter save; both binaries boot
# from power-on with it) or GATE0_STATE_IN=SNAP/tick<T> (a save state: its pc or
# pclag part is relinked for each binary, which is best effort for a binary that
# did not take the photo, README.md "Save states"), as for port/test/Makefile.
# Without either: power-on.
# SWEEP_GBA=DIR: a GBA capture of the same keys and start (gba.jsonl, gba.raw and
# gba_raster.bin, as `make -f port/test/Makefile gba-trace` writes them). The PC
# runs are then the lag replay, which keeps a bot route on the GBA's path.
# sweep-route makes one when SWEEP_GBA is not given (SWEEP_GBA_OUT, below; a
# save state's start resumes the GBA too), and so do ubsan-run and
# uninit-compare with SWEEP_LAG=1; without either they are the plain run
# (lag-free: a bot route leaves the GBA's path at its first lag effect, but both
# binaries still get the same buttons).
# FE8PC_NULLPROBE=1 in the environment (README.md, "NULL probe") goes to every
# run, each with its own site table (OUT.sites.jsonl), so a run goes on past a
# NULL-page read that stops the plain binaries.
#
# Output: build/gate0-sweep/<run>/ (game data, never committed; delete it when
# done, a 20,000-tick run keeps about 200 MB of raw records per binary). <run>
# is SWEEP_RUN (default: the key file's name, plus the save's or the snapshot's
# name).

include port/test/Makefile
.DEFAULT_GOAL := sweep-builds

SWEEPS        := ubsan zero pattern
SWEEP         ?= ubsan
UBSAN_CHECKS  ?= undefined,bounds
# UBSAN_WRAPV=1 keeps -fwrapv: GCC then checks no signed overflow at all, and
# the arithmetic is compiled as in the Gate 0 binary. Default 0: -fwrapv is
# dropped so signed overflow is checked (and reported; the GBA wraps).
UBSAN_WRAPV   ?= 0
SWEEP_CFLAGS_ubsan   = -fsanitize=$(UBSAN_CHECKS) -fsanitize-recover=all
SWEEP_DROP_ubsan     = $(if $(filter 1,$(UBSAN_WRAPV)),,-fwrapv)
SWEEP_SRCS_ubsan     = port/test/pc/ubsan_log.c
SWEEP_CFLAGS_zero    = -ftrivial-auto-var-init=zero
SWEEP_CFLAGS_pattern = -ftrivial-auto-var-init=pattern
SWEEP_TIMEOUT ?= 3600
export SWEEP_TIMEOUT GBA_DIR

# A sweep's name in build and output paths: UBSAN_WRAPV=1 gives ubsan-wrapv,
# so that variant has its own build directory. (Another UBSAN_CHECKS rebuilds
# build/pc-gate0-ubsan: its compile settings change.)
sweep_tag   = $(1)$(if $(and $(filter ubsan,$(1)),$(filter 1,$(UBSAN_WRAPV))),-wrapv)
sweep_build = $(if $(filter gate0,$(1)),$(PC_BUILD),build/pc-gate0-$(call sweep_tag,$(1))$(TEST_SUFFIX))
SWEEP_KEYS   = $(KEYS)
SWEEP_TICKS  = $(TICKS)
SWEEP_RUN   ?= $(basename $(notdir $(SWEEP_KEYS)))$(if $(GATE0_SRAM_IN),-$(basename $(notdir $(GATE0_SRAM_IN))))$(if \
               $(GATE0_STATE_IN),-$(notdir $(patsubst %/,%,$(dir $(GATE0_STATE_IN))))-$(notdir $(GATE0_STATE_IN)))
SWEEP_DIR    = build/gate0-sweep$(TEST_SUFFIX)/$(SWEEP_RUN)
SWEEP_GBA   ?=
SWEEP_LAG   ?=
SWEEP_GBA_OUT = $(SWEEP_DIR)/gba
SWEEP_RUN_SH  = bash port/test/tools/sweep_run.sh
# The GBA capture of ubsan-run and uninit-compare: SWEEP_GBA, else with
# SWEEP_LAG=1 the one sweep-gba makes, else none (the plain run).
SWEEP_LAG_GBA = $(or $(SWEEP_GBA),$(if $(filter 1,$(SWEEP_LAG)),$(SWEEP_GBA_OUT)))
SWEEP_LAG_DEP = $(if $(SWEEP_LAG_GBA),sweep-gba)

.PHONY: sweep-pc sweep-builds sweep-route sweep-gba ubsan-run ubsan-selftest uninit-compare

# One sweep binary: port/test/Makefile's `pc` in its own build directory, with
# port/test/pc/sweep_flags.mk read after pc/gate0.mk.
sweep-pc:
	@case " $(SWEEPS) gate0 " in *" $(SWEEP) "*) ;; \
	  *) echo "usage: make -f port/test/sweep.mk sweep-pc SWEEP=NAME (one of: $(SWEEPS))"; exit 2;; esac
ifeq ($(SWEEP),gate0)
	$(MAKE) -f port/test/Makefile pc
else
	$(MAKE) -f port/test/Makefile pc PC_BUILD=$(call sweep_build,$(SWEEP)) PC_EXTRA_MK=port/test/pc/sweep_flags.mk \
	    PC_EXTRA_SRCS="$(SWEEP_SRCS_$(SWEEP))" SWEEP_CFLAGS="$(SWEEP_CFLAGS_$(SWEEP))" \
	    SWEEP_DROP="$(SWEEP_DROP_$(SWEEP))"
endif

# One after the other (each build is -j as given; the load rule says -j4).
sweep-builds:
	@set -e; for s in $(SWEEPS); do $(MAKE) -f port/test/sweep.mk sweep-pc SWEEP=$$s; done

# The GBA capture the PC runs replay the lag of: SWEEP_GBA when given
# (sweep_run.sh checks that it is of the same keys), else one made here once
# per run directory.
sweep-gba:
	@if [ -n "$(SWEEP_GBA)" ]; then echo "sweep: GBA capture $(SWEEP_GBA) (SWEEP_GBA)"; \
	elif [ -f $(SWEEP_GBA_OUT)/gba.jsonl ] && [ -f $(SWEEP_GBA_OUT)/done ] \
	     && [ "$$(cat $(SWEEP_GBA_OUT)/done)" = "$(SWEEP_KEYS) $(SWEEP_TICKS) $(GATE0_SRAM_IN) $(GATE0_STATE_IN)" ]; then \
	  echo "sweep: GBA capture $(SWEEP_GBA_OUT) (made before, same keys, ticks and start)"; \
	else \
	  rm -rf $(SWEEP_GBA_OUT); mkdir -p $(SWEEP_GBA_OUT); \
	  $(MAKE) -f port/test/Makefile gba-trace KEYS=$(SWEEP_KEYS) TICKS=$(SWEEP_TICKS) OUT=$(SWEEP_GBA_OUT) \
	      > $(SWEEP_GBA_OUT)/gba-trace.log 2>&1 || { tail -5 $(SWEEP_GBA_OUT)/gba-trace.log; exit 1; }; \
	  echo "$(SWEEP_KEYS) $(SWEEP_TICKS) $(GATE0_SRAM_IN) $(GATE0_STATE_IN)" > $(SWEEP_GBA_OUT)/done; \
	  echo "sweep: GBA capture $(SWEEP_GBA_OUT) made"; fi
SWEEP_GBA_DIR = $(or $(SWEEP_GBA),$(SWEEP_GBA_OUT))

# Does the sweep binary still follow the GBA? Its lag replay compared with the
# GBA capture on its own, as port/test/Makefile's `route` (compare-lag-replay)
# checks the Gate 0 binary: exit 0 only when it matches on every tick and ran
# them all with no fault. The verdict goes to $(SWEEP_DIR)/<sweep>.compare.txt;
# its last lines are printed. For ubsan, ubsan_collate.py sorts the table.
sweep-route: sweep-gba
	$(MAKE) -f port/test/sweep.mk sweep-pc SWEEP=$(SWEEP)
	@set -u; o=$(SWEEP_DIR)/$(call sweep_tag,$(SWEEP)); exe=$(call sweep_build,$(SWEEP))/fe8pc; run=0; \
	$(SWEEP_RUN_SH) $(call sweep_tag,$(SWEEP)) $$exe $$o $(SWEEP_KEYS) $(SWEEP_TICKS) $(SWEEP_GBA_DIR) || run=$$?; \
	cmp=0; $(COMPARE_LOCKED) $(PYTHON) port/test/tools/gate0_compare.py --gba $(SWEEP_GBA_DIR)/gba.jsonl --pc $$o.jsonl \
	    --pc-log $$o.log --gba-raw $(SWEEP_GBA_DIR)/gba.raw --pc-raw $$o.raw --gba-elf $(GBA_DIR)/fireemblem8.elf \
	    --pc-elf $$exe --ticks $(SWEEP_TICKS) --masks $(MASKS) --json $$o.compare.json > $$o.compare.txt 2>&1 || cmp=$$?; \
	awk '/^== RESULT/{p=1} /^mask OL-1/{p=0} p' $$o.compare.txt | head -40; grep "^GATE 0" $$o.compare.txt; \
	if [ "$(SWEEP)" = ubsan ]; then $(PYTHON) port/test/tools/ubsan_collate.py $$o.ubsan.jsonl \
	    --out $$o.ubsan-sorted.txt | head -60; fi; \
	echo "sweep-route $(call sweep_tag,$(SWEEP)): run exit $$run, compare exit $$cmp ($$o.compare.txt)"; \
	[ $$cmp -eq 0 ]

# The runtime against GCC's own libubsan, on a stand-alone test program
# (port/test/pc/ubsan_selftest.c): same sites, same messages, every hit
# counted. No game code, no ROM; a second or so.
ubsan-selftest:
	$(PYTHON) port/test/tools/ubsan_selftest.py --checks $(UBSAN_CHECKS)

# The UBSan table of one run (plain, or the lag replay), sorted.
ubsan-run: $(SWEEP_LAG_DEP)
	$(MAKE) -f port/test/sweep.mk sweep-pc SWEEP=ubsan
	@o=$(SWEEP_DIR)/$(call sweep_tag,ubsan); run=0; \
	$(SWEEP_RUN_SH) $(call sweep_tag,ubsan) $(call sweep_build,ubsan)/fe8pc $$o $(SWEEP_KEYS) $(SWEEP_TICKS) \
	    $(SWEEP_LAG_GBA) || run=$$?; \
	$(PYTHON) port/test/tools/ubsan_collate.py $$o.ubsan.jsonl --out $$o.ubsan-sorted.txt; \
	echo "ubsan-run: run exit $$run; table $$o.ubsan.txt, sorted $$o.ubsan-sorted.txt"

# zero and pattern, the same buttons from the same start (both at once), then
# every traced field of every tick compared (sweep_pair.py; gMainCallback by
# name, as the binaries' addresses differ). The first differing tick and field
# are printed and written to $(SWEEP_DIR)/uninit.txt and uninit.json. Compares
# of COMPARE_LOCK_TICKS ticks or more take COMPARE_LOCK, as in
# port/test/Makefile.
uninit-compare: $(SWEEP_LAG_DEP)
	$(MAKE) -f port/test/sweep.mk sweep-pc SWEEP=zero
	$(MAKE) -f port/test/sweep.mk sweep-pc SWEEP=pattern
	@d=$(SWEEP_DIR); z=0; p=0; \
	$(SWEEP_RUN_SH) zero $(call sweep_build,zero)/fe8pc $$d/zero $(SWEEP_KEYS) $(SWEEP_TICKS) $(SWEEP_LAG_GBA) & zp=$$!; \
	$(SWEEP_RUN_SH) pattern $(call sweep_build,pattern)/fe8pc $$d/pattern $(SWEEP_KEYS) $(SWEEP_TICKS) \
	    $(SWEEP_LAG_GBA) || p=$$?; wait $$zp || z=$$?; \
	$(COMPARE_LOCKED) $(PYTHON) port/test/tools/sweep_pair.py --a $$d/zero.jsonl --b $$d/pattern.jsonl --a-raw $$d/zero.raw \
	    --b-raw $$d/pattern.raw --a-log $$d/zero.log --b-log $$d/pattern.log \
	    --a-exe $(call sweep_build,zero)/fe8pc --b-exe $(call sweep_build,pattern)/fe8pc --names zero,pattern \
	    --ticks $(SWEEP_TICKS) --json $$d/uninit.json > $$d/uninit.txt 2>&1; status=$$?; cat $$d/uninit.txt; \
	echo "uninit-compare: zero run exit $$z, pattern run exit $$p, compare exit $$status"; exit $$status
