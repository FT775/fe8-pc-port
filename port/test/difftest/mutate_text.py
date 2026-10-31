#!/usr/bin/env python3
"""Checks that difftest_text.py catches wrong text routines.

Writes altered copies of port/asm/arm_pc.c to build/difftest/mutants (the
source file is never edited) and runs difftest_text.py on each with
--c-source. Every mutant marked "caught" must fail the difftest. Mutants
marked "same" change nothing the caller can see, so they must pass.

Run from the repository root, after the GBA `make`:

    python3 port/test/difftest/mutate_text.py
"""

import concurrent.futures
import os
import subprocess
import sys

SRC = 'port/asm/arm_pc.c'
OUT = 'build/difftest/mutants'

# (name, expected result, text in arm_pc.c, replacement)
MUTANTS = [
    ('decode-leaf-mask-7f00', 'caught', 'if (word & 0xFF00)', 'if (word & 0x7F00)'),
    ('decode-bits-restart-per-char', 'caught',
     '        const u8 *node = root;\n', '        const u8 *node = root;\n        left = 0;\n'),
    ('decode-two-byte-high-first', 'caught',
     '            out[0] = (u8)word;\n            out[1] = (u8)(word >> 8);',
     '            out[1] = (u8)word;\n            out[0] = (u8)(word >> 8);'),
    ('decode-leaf-test-bit30', 'caught', 'while (!(word & 0x80000000));', 'while (!(word & 0xC0000000));'),
    ('decode-7-bits-per-byte', 'caught', 'left = 7;', 'left = 6;'),
    # dst is a local: moving it after the end marker is written changes nothing.
    ('decode-advance-dst-after-end', 'same',
     '        out[0] = (u8)word;\n        if (!(word & 0xFF))\n            return;\n        out++;',
     '        out[0] = (u8)word;\n        out++;\n        if (!(word & 0xFF))\n            return;'),
    # Only bit 0 of each loaded byte is ever used, 8 times: sign extension never shows.
    ('decode-signed-source-byte', 'same', 'bits = *in++;', 'bits = (u32)(s32)(s8)*in++;'),
    ('drawglyph-no-umull-high-word', 'caught', 'u32 hi = (u32)(row >> 32);', 'u32 hi = 0;'),
    ('drawglyph-32-bit-product', 'caught', 'u64 row = (u64)*src * mul;', 'u64 row = (u32)(*src * mul);'),
    ('drawglyph-15-rows', 'caught', 'for (i = 0xF; i >= 0; i--)', 'for (i = 0xF; i > 0; i--)'),
    ('drawglyph-swap-0x40-0x80-writes', 'caught',
     '        dst[0x10] |= pal[(lo >> 16) & 0xFF] | ((u32)pal[(lo >> 24) & 0xFF] << 16);\n'
     '        dst[0x20] |= pal[hi & 0xFF] | ((u32)pal[(hi >> 8) & 0xFF] << 16);',
     '        dst[0x20] |= pal[hi & 0xFF] | ((u32)pal[(hi >> 8) & 0xFF] << 16);\n'
     '        dst[0x10] |= pal[(lo >> 16) & 0xFF] | ((u32)pal[(lo >> 24) & 0xFF] << 16);'),
    ('drawglyph-reread-src-after-first-write', 'caught',
     '        dst[0x00] |= pal[lo & 0xFF] | ((u32)pal[(lo >> 8) & 0xFF] << 16);\n',
     '        dst[0x00] |= pal[lo & 0xFF] | ((u32)pal[(lo >> 8) & 0xFF] << 16);\n'
     '        row = (u64)*src * mul; lo = (u32)row; hi = (u32)(row >> 32);\n'),
    ('drawglyph-mask-subx', 'caught',
     'mul = *(const u32 *)(uintptr_t)(0x08000540u + ((u32)subx << 2));', 'mul = 1u << ((subx & 7) * 2);'),
]


def run_one(m, text):
    name, expect, old, new = m
    if text.count(old) != 1:
        return name, expect, 'NOT APPLIED', ''
    path = os.path.join(OUT, name + '.c')
    with open(path, 'w') as f:
        f.write(text.replace(old, new))
    r = subprocess.run([sys.executable, 'port/test/difftest/difftest_text.py', '--c-source', path,
                        '--out', os.path.join(OUT, name)], capture_output=True, text=True)
    result = next((l for l in r.stdout.splitlines() if l.startswith('RESULT')), 'RESULT: none')
    got = 'same' if result.startswith('RESULT: PASS') else 'caught'
    return name, expect, got, result


def main():
    os.makedirs(OUT, exist_ok=True)
    with open(SRC) as f:
        text = f.read()
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as ex:
        results = list(ex.map(lambda m: run_one(m, text), MUTANTS))
    bad = 0
    for name, expect, got, result in results:
        ok = expect == got
        bad += not ok
        print(f'{"ok  " if ok else "BAD "} {name:40s} expected {expect:6s} got {got:11s} {result}')
    print(f'{len(results) - bad} of {len(results)} mutants behaved as expected')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
