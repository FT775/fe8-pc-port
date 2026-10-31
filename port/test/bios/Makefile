# Tests for the port's BIOS arithmetic, port/bios/bios_math.c. Run from the
# repository root (no GBA build needed):
#
#   make -f port/test/bios/Makefile             # build and run all checks
#   make -f port/test/bios/Makefile exhaustive  # also Sqrt on all 2^32 inputs
#
# Three checks run:
# - test_bios_math: the 123 mgba-emu/suite hardware vectors and the rules in
#   port-plan/community-reuse-audio-bios.md 2.2 (see test_bios_math.c). It is
#   built twice: with the port's own flags (PORT_CFLAGS, read back from
#   port/Makefile), and with -O2, no -fwrapv and UBSan, so the code does not
#   rely on signed overflow wrapping.
# - sine-table: port/bios/bios_sine_table.inc is what gen_sine_table.py writes.
# - no-float: bios_math.o has no floating-point instruction and calls no libm.
#
# Output goes to build/test-bios (ignored by git).

OUT    := build/test-bios
CC     ?= gcc
PYTHON ?= python3

PORT_CFLAGS = $(shell MAKEFLAGS= make -f port/Makefile -pn .bios-test-none 2>/dev/null \
                | sed -n 's/^PORT_CFLAGS := //p')
CPPFLAGS    := -DPC_PORT=1 -Iinclude -Iport/include -Iport/bios -Iport/test/bios
STRICT_CFLAGS := -m32 -std=gnu11 -O2 -g -fno-pie -funsigned-char -fno-strict-aliasing -Wall \
                 -Wno-unused-function -fsanitize=undefined -fno-sanitize-recover=all

SRCS := port/bios/bios_math.c port/test/bios/test_bios_math.c
DEPS := $(SRCS) port/bios/bios_math.h port/bios/bios_sine_table.inc port/test/bios/suite_vectors.h

.PHONY: all test exhaustive sine-table no-float
all: test sine-table no-float

$(OUT)/test_bios_math: $(DEPS)
	@mkdir -p $(@D)
	$(CC) $(PORT_CFLAGS) $(CPPFLAGS) -o $@ $(SRCS) -lm

$(OUT)/test_bios_math_strict: $(DEPS)
	@mkdir -p $(@D)
	$(CC) $(STRICT_CFLAGS) $(CPPFLAGS) -o $@ $(SRCS) -lm

$(OUT)/bios_math.o: port/bios/bios_math.c port/bios/bios_math.h port/bios/bios_sine_table.inc
	@mkdir -p $(@D)
	$(CC) $(PORT_CFLAGS) $(CPPFLAGS) -c $< -o $@

test: $(OUT)/test_bios_math $(OUT)/test_bios_math_strict
	@echo "== port flags"
	$(OUT)/test_bios_math
	@echo "== -O2, no -fwrapv, UBSan"
	$(OUT)/test_bios_math_strict

exhaustive: $(OUT)/test_bios_math
	BIOS_TEST_EXHAUSTIVE=1 $(OUT)/test_bios_math

sine-table:
	@mkdir -p $(OUT)
	$(PYTHON) -I port/bios/gen_sine_table.py > $(OUT)/bios_sine_table.inc
	cmp $(OUT)/bios_sine_table.inc port/bios/bios_sine_table.inc
	@echo "sine-table: port/bios/bios_sine_table.inc matches gen_sine_table.py"

# x87 (every mnemonic starting with f) and SSE scalar/packed float mnemonics,
# taken from the instruction column of the disassembly.
FLOAT_OPS := ^(f[a-z0-9]*|cvt[a-z0-9]*|(mov|add|sub|mul|div|sqrt|max|min|ucomi|comi|and|andn|or|xor)(ss|sd|ps|pd)|movap[sd]|movup[sd]|unpck[lh]p[sd])$$

no-float: $(OUT)/bios_math.o
	@if objdump -d --no-show-raw-insn $< | awk -F'\t' 'NF >= 2 { split($$2, w, " "); print w[1] }' \
	    | grep -E '$(FLOAT_OPS)'; then \
	    echo "no-float: FAIL, floating-point instructions above"; exit 1; fi
	@if nm -u $< | grep -E '\s(sin|cos|sincos|tan|sqrt|atan2?|floor|ceil|trunc|round|lround|pow|exp|log)[fl]?$$'; then \
	    echo "no-float: FAIL, libm calls above"; exit 1; fi
	@echo "no-float: bios_math.o has no floating-point instructions and no libm calls"
