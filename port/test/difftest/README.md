# Difftests: port C code against the decomp's own ARM code

A difftest runs a routine from `asm/arm.s` in Unicorn and the port's C version
of it (`port/asm/arm_pc.c`) on the same inputs, then requires every byte they
write to match. Nothing here is compiled into the game. The ELF is only read.
Output goes to `build/difftest` (ignored by git), and no ROM-derived file is
kept in git.

Needs: the GBA `make` in this tree (`fireemblem8.elf`), `python3-unicorn`
(2.1.4, README of `port/test`), and `gcc-multilib` (the port's `-m32`).

## Text routines: `DrawGlyph` and `DecodeString`

```
python3 port/test/difftest/difftest_text.py      # about 20 seconds
python3 port/test/difftest/mutate_text.py        # about 1 minute; checks the test itself
```

### How it works

- **ARM side** (`difftest_text.py`, Unicorn, ARM926 model; `arm.s` uses only
  ARMv4 instructions). It loads the ROM segment of `fireemblem8.elf` at
  0x08000000. Then it copies `ARMCodeToCopy_Start..End` to `RAMFuncArea`
  and calls the routines there, as `StoreRoutinesToIRAM` does. EWRAM is mapped
  at 0x02000000 and the stack at 0x03007F00. A write hook records every ARM
  store outside the stack.
- **C side** (`text_harness.c`). It links the port's own `arm_pc.o`, compiled
  with `port/Makefile`'s `PORT_CFLAGS` by the same command as the port rule.
  Like the port (D1-a), this 32-bit process maps a private copy of the same ROM
  bytes at 0x08000000 and EWRAM at 0x02000000. `gMsgHuffmanTable` and
  `gMsgHuffmanTableRoot` are bound to their GBA addresses with `--defsym`,
  which is how `gen_host_link.py` binds data the host does not build. So both
  sides read the same bytes at the same addresses.
- Both sides run one list of operations: memory writes, calls, dumps and
  CRCs. After every call: a dump of the call's buffers and a CRC-32 of the
  whole 64 KB arena. At the end: a CRC of the whole 16 MB ROM copy (which
  catches stray writes) and one of EWRAM. Each section starts from fresh
  random EWRAM, so nothing relies on zeroed memory.
- A small Python model of `DecodeString` only selects fuzz inputs that end
  inside their buffers. It is not one of the two sides that are compared.

### Cases (seed 8)

| Section | Calls | What |
|---|---|---|
| `decode-real` | 3,404 | Every entry of `gMsgTable`, decoded from the ROM with the real table, to dst offsets 0-3 mod 4. Up to 4,000 bytes out |
| `decode-realtable-random` | 1,000 | Random bytes (16-1,024) decoded with the real table; 20% decoded in place (dst = src +/- 24) |
| `decode-fuzz-tree` | 1,000 | 100 random Huffman trees (2-600 leaves) written over the table, with a random root. Leaves include two-byte values with low byte 0, bits 16-30 set, and end markers with bits 16-30 set. 40% of trees put nodes at indices 0x4000-0xFFFF, past the real table. Inputs are random code sequences ending in the end marker, plus random bytes |
| `decode-fuzz-graph` | 330 | 50 random node graphs (cycles allowed), random input bytes |
| `drawglyph-real` | 2,920 | Every glyph of `TextGlyphs_System`, `_Talk` and `_Special` at subx 0-7, with the 13 real colour LUTs |
| `drawglyph-line` | 10,377 | The first line of 400 real messages (every 8th), drawn glyph by glyph into one cleared buffer as `DrawTextGlyph` does (dst = base + (x / 8) * 0x40, subx = x & 7) |
| `drawglyph-fuzz` | 4,000 | Random LUT, glyph and destination. The LUT is at 2 mod 4 in 1,807 calls; 777 calls overlap dst with src or the LUT; 330 calls use subx outside 0-7 (8-63 and -1 to -0x150), where both sides read the ROM word past `bitTable` |

Result on 2026-10-19 (`port-text`): **23,031 calls, every dump and CRC
identical, no ARM write outside the arena.**

`mutate_text.py` writes 13 altered copies of `arm_pc.c` to
`build/difftest/mutants` and runs the difftest on each with `--c-source`. The
source file is never edited. All 11 that change behaviour are caught: wrong
leaf mask, bit buffer restarting per character, two-byte leaf byte order, a
leaf test on bit 30, 7 bits per byte, no `umull` high word, a 32-bit product,
15 rows, swapped write order, `src` re-read after a write, and `subx` masked
instead of read from the ROM. The 2 that cannot be seen from outside pass, as
expected: moving the local `dst` after the end marker, and a signed source
byte (only bit 0 of each byte is ever used).
