# Tests for the PSG model, port/audio/psg.c (audio-design.md 4.5, P3.4b).
# Run from the repository root (no GBA build needed):
#
#   make -f port/test/audio/psg.mk
#
# test_psg.c includes psg.c, so it is one translation unit. It is built and
# run twice:
# - with the port's own flags (PORT_CFLAGS read back from port/Makefile;
#   -m32, gnu11, -fwrapv), as the game build would compile psg.c;
# - with -O2, no -fwrapv and UBSan, plus -Wextra -Wconversion -Werror on
#   psg.c alone, so the code does not rely on signed overflow wrapping.
# Then no-float checks that psg.o has no floating-point instruction.
#
# Output goes to build/test-audio-psg (ignored by git).

OUT := build/test-audio-psg
CC  ?= gcc

PORT_CFLAGS = $(shell MAKEFLAGS= make -f port/Makefile -pn .psg-test-none 2>/dev/null \
                | sed -n 's/^PORT_CFLAGS := //p')
CPPFLAGS      := -Iport/audio
STRICT_CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -fsanitize=undefined -fno-sanitize-recover=all
WARN_CFLAGS   := -std=c99 -O2 -Wall -Wextra -Wconversion -Wsign-conversion -pedantic -Werror

DEPS := port/audio/psg.c port/audio/psg.h port/test/audio/test_psg.c

.PHONY: all test warnings no-float
all: warnings test no-float

$(OUT)/test_psg: $(DEPS)
	@mkdir -p $(@D)
	$(CC) $(PORT_CFLAGS) $(CPPFLAGS) -no-pie -o $@ port/test/audio/test_psg.c

$(OUT)/test_psg_strict: $(DEPS)
	@mkdir -p $(@D)
	$(CC) $(STRICT_CFLAGS) $(CPPFLAGS) -o $@ port/test/audio/test_psg.c

$(OUT)/psg.o: port/audio/psg.c port/audio/psg.h
	@mkdir -p $(@D)
	$(CC) $(PORT_CFLAGS) $(CPPFLAGS) -c $< -o $@

warnings:
	$(CC) $(WARN_CFLAGS) $(CPPFLAGS) -fsyntax-only port/audio/psg.c
	@echo "warnings: psg.c is clean under $(WARN_CFLAGS)"

test: $(OUT)/test_psg $(OUT)/test_psg_strict
	@echo "== port flags"
	$(OUT)/test_psg
	@echo "== -O2, no -fwrapv, UBSan"
	$(OUT)/test_psg_strict

# x87 (every mnemonic starting with f) and SSE float mnemonics.
FLOAT_OPS := ^(f[a-z0-9]*|cvt[a-z0-9]*|(mov|add|sub|mul|div|sqrt|max|min|ucomi|comi|and|andn|or|xor)(ss|sd|ps|pd)|movap[sd]|movup[sd]|unpck[lh]p[sd])$$

no-float: $(OUT)/psg.o
	@if objdump -d --no-show-raw-insn $< | awk -F'\t' 'NF >= 2 { split($$2, w, " "); print w[1] }' \
	    | grep -E '$(FLOAT_OPS)'; then \
	    echo "no-float: FAIL, floating-point instructions above"; exit 1; fi
	@echo "no-float: psg.o has no floating-point instructions"
