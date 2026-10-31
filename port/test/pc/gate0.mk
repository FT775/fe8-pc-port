# Read after port/Makefile by port/test/Makefile's `pc` target only, for the
# Gate 0 test binary (build/pc-gate0/fe8pc); the shipped build never sees it.
#
# The lag position (port/test/README.md, "Lag position") counts entries to and
# returns from RunProcessRecursive and entries to RunProcessScript, which are
# file-static in proc.c and called from inside proc.o, where --wrap cannot
# reach. So this build compiles proc.o with -finstrument-functions
# (PC_INSTRUMENT, set by port/test/Makefile): GCC calls
# __cyg_profile_func_enter and __cyg_profile_func_exit (port/test/pc/
# gate0_hook.c) at the entry and return of each proc.c function. Nothing else
# in the object changes; the hook only counts, and the plain PC trace must stay
# the same as without it (checked on the 60,000-tick scripted route).
$(BUILD)/src/proc.o: HOST_CFLAGS += $(PC_INSTRUMENT)

# The lag replay's sound positions (port/test/README.md, "Sound on any route")
# count the TrackStop calls of the main loop's open m4a call, and TrackStop is
# also called from inside m4a_1_pc.o (and through gMPlayJumpTable), where a
# --wrap would change the table's pointer: so m4a_1_pc.o is compiled with
# -finstrument-functions too (PC_SND_INSTRUMENT), and the hook acts on
# TrackStop's entry only, for the main loop's call. The sound it makes does not
# change (the snd rows' self-check and the record compares).
$(BUILD)/port/audio/m4a_1_pc.o: PORT_CFLAGS += $(PC_SND_INSTRUMENT)

# FE8PC_GATE0 (PC_FRAME_DEFS, set by port/test/Makefile): port/hw/frame.c's
# test-harness functions, which run lines of the frame step for the hook (the
# lag replay's lag frame lines and the raster-timing redraw; port/test/README.md,
# "Lag replay" and "Raster-timing frames"). Only this object, only this build:
# the frame step itself (PortRunFrame, Line) is the same code with or without.
$(BUILD)/port/hw/frame.o: PORT_CFLAGS += $(PC_FRAME_DEFS)

# FE8PC_DEBUG: the game code's checks for test builds only. Today one:
# ProcCmd_CALL_ROUTINE_ARG (src/proc.c) reports a PROC_CALL_ARG target that is
# not known to return its s8 and has no GBA return value in the port
# (port/test/pc/proc_call_arg_check.c, linked by port/test/Makefile's `pc`).
# The checks only report; the run does not change.
HOST_CPPFLAGS += -DFE8PC_DEBUG=1
