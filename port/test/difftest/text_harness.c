/*
 * Host side of the text-routine difftest (difftest_text.py). Not part of the
 * game build.
 *
 * Linked with the port's own port/asm/arm_pc.o, compiled with port/Makefile's
 * PORT_CFLAGS. Like the port (D1-a), this 32-bit process puts GBA memory at
 * GBA addresses: a private, writable copy of the ROM image at 0x08000000 and
 * a 256 KB EWRAM block at 0x02000000. gMsgHuffmanTable and
 * gMsgHuffmanTableRoot are bound to their GBA ROM addresses with --defsym,
 * as port/tools/gen_host_link.py binds data the host does not build. So the
 * C code reads the same bytes at the same addresses as the ARM code does in
 * Unicorn.
 *
 *   text_harness ROM_IMAGE OPS_FILE OUT_FILE
 *
 * OPS_FILE is a list of little-endian u32 records (see difftest_text.py):
 *   0 END
 *   1 WRITE addr len bytes[len] (padded to 4)
 *   2 DRAWGLYPH pal dst src subx
 *   3 DECODE src dst
 *   4 DUMP addr len          -> len bytes to OUT_FILE
 *   5 CRC addr len           -> CRC-32 (zlib) as u32 to OUT_FILE
 */

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

#define EWRAM_BASE 0x02000000u
#define EWRAM_SIZE 0x00040000u
#define ROM_BASE   0x08000000u
#define ROM_SIZE   0x01000000u

void DrawGlyph(const uint16_t *pal, uint32_t *dst, const uint32_t *src, int subx);
void DecodeString(const char *src, char *dst);

/* port_hw.h functions arm_pc.c calls. */
void PortLog(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void PortStubHit(const char *name, unsigned gbaAddr, unsigned *count)
{
    if ((*count)++ == 0)
        fprintf(stderr, "[stub] %s GBA 0x%08X\n", name, gbaAddr);
}

void PortExit(int code)
{
    fprintf(stderr, "PortExit(%d) called\n", code);
    exit(2);
}

/* The port_gbabus.h load MapFloodCoreStep uses for cells off RAM (port/hw/gbabus.c
   is not linked here). Neither text routine calls it. */
uint32_t PortGbaLoad8(uint32_t addr, uint32_t openBus, uint32_t biosLatch)
{
    fprintf(stderr, "PortGbaLoad8(0x%08X, 0x%08X, 0x%08X) called\n", (unsigned)addr, (unsigned)openBus,
        (unsigned)biosLatch);
    exit(2);
}

static uint32_t sCrcTable[256];

static void CrcInit(void)
{
    uint32_t i, c;
    int k;

    for (i = 0; i < 256; i++)
    {
        c = i;
        for (k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        sCrcTable[i] = c;
    }
}

static uint32_t Crc32(const uint8_t *p, uint32_t len)
{
    uint32_t c = 0xFFFFFFFFu;

    while (len--)
        c = sCrcTable[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void *MapFixed(uint32_t addr, uint32_t size)
{
    void *p = mmap((void *)(uintptr_t)addr, size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    if (p == MAP_FAILED || p != (void *)(uintptr_t)addr)
    {
        fprintf(stderr, "cannot map 0x%08X+0x%X: %s\n", addr, size, strerror(errno));
        exit(1);
    }
    return p;
}

static uint8_t *ReadFile(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long n;

    if (!f)
    {
        fprintf(stderr, "cannot open %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(n ? n : 1);
    if (fread(buf, 1, n, f) != (size_t)n)
    {
        fprintf(stderr, "short read %s\n", path);
        exit(1);
    }
    fclose(f);
    *size = (uint32_t)n;
    return buf;
}

static int InRange(uint32_t addr, uint32_t len)
{
    if (addr >= EWRAM_BASE && len <= EWRAM_SIZE && addr - EWRAM_BASE <= EWRAM_SIZE - len)
        return 1;
    if (addr >= ROM_BASE && len <= ROM_SIZE && addr - ROM_BASE <= ROM_SIZE - len)
        return 1;
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t romSize, opsSize, pos = 0, calls = 0;
    uint8_t *rom, *ops;
    FILE *out;

    if (argc != 4)
    {
        fprintf(stderr, "usage: %s ROM_IMAGE OPS_FILE OUT_FILE\n", argv[0]);
        return 1;
    }

    CrcInit();
    MapFixed(EWRAM_BASE, EWRAM_SIZE);
    MapFixed(ROM_BASE, ROM_SIZE);

    rom = ReadFile(argv[1], &romSize);
    if (romSize > ROM_SIZE)
    {
        fprintf(stderr, "ROM image too large\n");
        return 1;
    }
    memcpy((void *)(uintptr_t)ROM_BASE, rom, romSize);
    free(rom);

    ops = ReadFile(argv[2], &opsSize);
    out = fopen(argv[3], "wb");
    if (!out)
    {
        fprintf(stderr, "cannot open %s\n", argv[3]);
        return 1;
    }

#define NEXT() (pos + 4 <= opsSize ? (pos += 4, (uint32_t)ops[pos - 4] | (uint32_t)ops[pos - 3] << 8 \
        | (uint32_t)ops[pos - 2] << 16 | (uint32_t)ops[pos - 1] << 24) : (fprintf(stderr, "ops truncated\n"), exit(1), 0u))

    for (;;)
    {
        uint32_t op = NEXT();
        uint32_t a, b, c, d;

        switch (op)
        {
        case 0:
            fclose(out);
            fprintf(stderr, "text_harness: %u calls\n", calls);
            return 0;

        case 1:
            a = NEXT();
            b = NEXT();
            if (!InRange(a, b) || pos + b > opsSize)
            {
                fprintf(stderr, "bad WRITE 0x%08X+0x%X\n", a, b);
                return 1;
            }
            memcpy((void *)(uintptr_t)a, ops + pos, b);
            pos += (b + 3) & ~3u;
            break;

        case 2:
            a = NEXT();
            b = NEXT();
            c = NEXT();
            d = NEXT();
            DrawGlyph((const uint16_t *)(uintptr_t)a, (uint32_t *)(uintptr_t)b,
                (const uint32_t *)(uintptr_t)c, (int)d);
            calls++;
            break;

        case 3:
            a = NEXT();
            b = NEXT();
            DecodeString((const char *)(uintptr_t)a, (char *)(uintptr_t)b);
            calls++;
            break;

        case 4:
            a = NEXT();
            b = NEXT();
            if (!InRange(a, b))
            {
                fprintf(stderr, "bad DUMP 0x%08X+0x%X\n", a, b);
                return 1;
            }
            fwrite((const void *)(uintptr_t)a, 1, b, out);
            break;

        case 5:
            a = NEXT();
            b = NEXT();
            if (!InRange(a, b))
            {
                fprintf(stderr, "bad CRC 0x%08X+0x%X\n", a, b);
                return 1;
            }
            c = Crc32((const uint8_t *)(uintptr_t)a, b);
            fwrite(&c, 4, 1, out);
            break;

        default:
            fprintf(stderr, "bad op %u at 0x%X\n", op, pos - 4);
            return 1;
        }
    }
}
