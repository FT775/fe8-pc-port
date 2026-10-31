# Read after port/Makefile and port/test/pc/gate0.mk by port/test/Makefile's
# `pc` target, for the bug-sweep builds only (port/test/sweep.mk; README.md,
# "Bug sweeps"): the Gate 0 test binary built again with diagnostic compiler
# flags, in its own build directory. Never a shipped build.
#
# SWEEP_CFLAGS (from the command line): added to the game code (HOST_CFLAGS)
# and the port code (PORT_CFLAGS, which also compiles the hook, romreloc.c and
# autostubs.c). SWEEP_DROP: flags taken out of both first (the UBSan build
# drops -fwrapv, under which GCC checks no signed overflow). Both go into the
# build directory's compile settings, so a change recompiles every object.
SWEEP_CFLAGS ?=
SWEEP_DROP   ?=
HOST_CFLAGS := $(filter-out $(SWEEP_DROP),$(HOST_CFLAGS)) $(SWEEP_CFLAGS)
PORT_CFLAGS := $(filter-out $(SWEEP_DROP),$(PORT_CFLAGS)) $(SWEEP_CFLAGS)
COMPILE_SETTINGS += | sweep: $(SWEEP_CFLAGS) | sweep drops: $(SWEEP_DROP)

# The UBSan sweep's own runtime (port/test/pc/ubsan_log.c) is not checked:
# the checks call into it.
$(BUILD)/port/test/pc/ubsan_log.o: PORT_CFLAGS += -fno-sanitize=all
