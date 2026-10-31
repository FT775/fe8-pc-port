# Read after port/Makefile by port/Makefile.mingw
# (make -f port/Makefile -f port/win32/vars.mk print-NAME): prints one of
# port/Makefile's variables, so the Windows build takes its source lists and
# flags from the Linux makefile instead of keeping a second copy.
print-%:
	@echo '$($*)'
