# FE8PC_TESTBUILD, the test-build switch (port-plan/DIVERGENCES.md TB-1; what it
# compiles: port/Makefile). It is read from the make command line only:
#   make -f port/Makefile FE8PC_TESTBUILD=1      (a sub-make gets it from its
#                                                 parent's command line)
# A value from the environment is ignored, and the top-level make says so:
# FE8PC_* is also the prefix of the runtime settings (FE8PC_FRAMES,
# FE8PC_SAVE, ...), and an exported FE8PC_TESTBUILD=1 must not quietly make
# every port build a test build. Included by port/Makefile, port/Makefile.sdl,
# port/Makefile.mingw and port/test/Makefile, before they read the switch.
ifneq ($(origin FE8PC_TESTBUILD),command line)
ifneq ($(filter environment%,$(origin FE8PC_TESTBUILD)),)
ifeq ($(MAKELEVEL),0)
ifneq ($(FE8PC_TESTBUILD),0)
$(warning FE8PC_TESTBUILD=$(FE8PC_TESTBUILD) in the environment is ignored: a test build needs FE8PC_TESTBUILD=1 on the make command line)
endif
endif
endif
override FE8PC_TESTBUILD := 0
endif
