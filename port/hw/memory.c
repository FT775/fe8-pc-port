#include "global.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "port_hw.h"
#include "../audio/psg.h"

/*
 * D1-a memory model: every GBA region lives at its GBA address in this 32-bit
 * process. EWRAM and IWRAM are mapped by the ELF loader (the host linker
 * script places the game's RAM variables there, see gen_host_link.py). The
 * other regions are reserved here at startup.
 */

struct PortRegion
{
    const char *name;
    uint32_t addr;
    uint32_t size;
    int mappedByElf;
};

static const struct PortRegion sRegions[] = {
    { "EWRAM", PORT_EWRAM_START, 0x40000, 1 },
    { "IWRAM", PORT_IWRAM_START, 0x8000, 1 },
    { "IO", PORT_IO_START, 0x1000, 0 },
    { "PLTT", PORT_PLTT_START, 0x1000, 0 },
    { "VRAM", PORT_VRAM_START, 0x18000, 0 },
    { "OAM", PORT_OAM_START, 0x1000, 0 },
    { "ROM", PORT_ROM_START, PORT_ROM_MAX, 0 },
    { "SRAM", PORT_SRAM_START, 0x10000, 0 },
};

static uint32_t sRomSize;
static uint32_t sMapped; /* bit i: sRegions[i] is this run's (reserved here, or mapped by the ELF loader) */

uint32_t PortRomSize(void)
{
    return sRomSize;
}

int PortMemInGbaMap(uint32_t addr)
{
    unsigned i;

    for (i = 0; i < ARRAY_COUNT(sRegions); i++)
        if ((sMapped >> i & 1) && addr - sRegions[i].addr < sRegions[i].size)
            return 1;
    return 0;
}

static int ReserveRegion(const struct PortRegion *r)
{
    void *want = (void *)(uintptr_t)r->addr;
    void *got = mmap(want, r->size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    if (got == want)
    {
        PortLog("[mem] %-5s 0x%08X +0x%07X reserved%s\n", r->name, r->addr, r->size,
            r->mappedByElf ? " (ELF did not map it)" : "");
        return 0;
    }
    if (got != MAP_FAILED)
        munmap(got, r->size);
    if (errno == EEXIST && r->mappedByElf)
    {
        /* Expected: the ELF loader mapped it from the .gba_* output sections. */
        PortLog("[mem] %-5s 0x%08X +0x%07X mapped by ELF loader\n", r->name, r->addr, r->size);
        return 0;
    }
    PortLog("[mem] %-5s 0x%08X +0x%07X RESERVATION FAILED: %s\n", r->name, r->addr, r->size,
        strerror(errno));
    return -1;
}

/*
 * The ROM check. The port runs the game's code built from this tree on the
 * data of the user's own FE8 (USA) ROM, read in place at its GBA addresses,
 * so only the original ROM will do: the header (rom_header.s: title
 * FIREEMBLEM2E at 0xA0, game code BE8E at 0xAC), its size, 16 MiB, and its
 * SHA-1, the one checksum.sha1 gives the GBA build of this tree. A dump cut
 * short has the header but not the size; a ROM hack, a translation or a
 * patched ROM has the size but not the SHA-1. Each is refused before the game
 * starts, with what is wrong, instead of faulting later on data that is not
 * there or not FE8's. SHA-1 as FIPS 180-4 describes it.
 */
#define FE8_USA_SIZE 0x1000000u
static const char sFe8UsaSha1[] = "c25b145e37456171ada4b0d440bf88a19f4d509f";

struct Sha1
{
    uint32_t h[5];
    uint64_t bytes;
    uint8_t block[64];
    uint32_t used;
};

static uint32_t Rol32(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

static void Sha1Block(uint32_t h[5], const uint8_t *p)
{
    uint32_t w[80], a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f, k, t;
    int i;

    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (; i < 80; i++)
        w[i] = Rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (i = 0; i < 80; i++)
    {
        if (i < 20)
        {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        t = Rol32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = Rol32(b, 30);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

static void Sha1Init(struct Sha1 *s)
{
    static const uint32_t init[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };

    memcpy(s->h, init, sizeof(init));
    s->bytes = 0;
    s->used = 0;
}

static void Sha1Add(struct Sha1 *s, const uint8_t *p, size_t n)
{
    s->bytes += n;
    if (s->used != 0)
    {
        while (n != 0 && s->used < 64)
        {
            s->block[s->used++] = *p++;
            n--;
        }
        if (s->used < 64)
            return;
        Sha1Block(s->h, s->block);
        s->used = 0;
    }
    for (; n >= 64; p += 64, n -= 64)
        Sha1Block(s->h, p);
    memcpy(s->block, p, n);
    s->used = (uint32_t)n;
}

/* The digest as 40 lower-case hex digits. */
static void Sha1Hex(struct Sha1 *s, char out[41])
{
    static const char digits[] = "0123456789abcdef";
    uint64_t bits = s->bytes * 8;
    int i;

    s->block[s->used++] = 0x80;
    if (s->used > 56)
    {
        memset(s->block + s->used, 0, 64 - s->used);
        Sha1Block(s->h, s->block);
        s->used = 0;
    }
    memset(s->block + s->used, 0, 56 - s->used);
    for (i = 0; i < 8; i++)
        s->block[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    Sha1Block(s->h, s->block);
    for (i = 0; i < 20; i++)
    {
        uint8_t byte = (uint8_t)(s->h[i / 4] >> (24 - 8 * (i % 4)));

        out[2 * i] = digits[byte >> 4];
        out[2 * i + 1] = digits[byte & 15];
    }
    out[40] = '\0';
}

static int HeaderIsFe8Usa(const uint8_t *rom, size_t n)
{
    return n >= 0xC0 && memcmp(rom + 0xA0, "FIREEMBLEM2E", 12) == 0 && memcmp(rom + 0xAC, "BE8E", 4) == 0;
}

/* What the ROM file at path is, without loading it (port/win32's search for
   the ROM next to the exe). sha1: the file's, when it was taken (else ""). */
int PortRomFileKind(const char *path, char sha1[41])
{
    static uint8_t buf[0x10000];
    FILE *fp = fopen(path, "rb");
    struct Sha1 s;
    size_t n, total = 0;
    int kind = PORT_ROM_NOT_FE8;

    sha1[0] = '\0';
    if (fp == NULL)
        return PORT_ROM_NOT_FE8;
    n = fread(buf, 1, sizeof(buf), fp);
    if (HeaderIsFe8Usa(buf, n))
    {
        Sha1Init(&s);
        while (n != 0 && total + n <= FE8_USA_SIZE)
        {
            Sha1Add(&s, buf, n);
            total += n;
            n = fread(buf, 1, sizeof(buf), fp);
        }
        if (ferror(fp) || n != 0 || total != FE8_USA_SIZE)
            kind = PORT_ROM_WRONG_SIZE;
        else
        {
            Sha1Hex(&s, sha1);
            kind = strcmp(sha1, sFe8UsaSha1) == 0 ? PORT_ROM_FE8 : PORT_ROM_WRONG_SHA1;
        }
    }
    fclose(fp);
    return kind;
}

static int LoadRom(const char *path)
{
    FILE *fp = fopen(path, "rb");
    uint8_t *rom = (uint8_t *)(uintptr_t)PORT_ROM_START;
    struct Sha1 s;
    char sha1[41];
    size_t n;
    int more;

    if (fp == NULL)
    {
        PortLog("[mem] cannot open ROM '%s': %s\n", path, strerror(errno));
        return -1;
    }
    n = fread(rom, 1, PORT_ROM_MAX, fp);
    more = n == PORT_ROM_MAX && fgetc(fp) != EOF;
    fclose(fp);
    sRomSize = (uint32_t)n;

    if (!HeaderIsFe8Usa(rom, n))
    {
        PortLog("[mem] '%s' is not FE8 (USA): header mismatch\n", path);
        return -1;
    }
    if (n != FE8_USA_SIZE)
    {
        PortLog("[mem] '%s' has the FE8 (USA) header but is %u bytes%s, not 16,777,216 (16 MiB): an incomplete "
                "or damaged copy of the ROM. Copy (dump) the ROM from the cartridge again.\n", path, (unsigned)n,
            more ? " or more" : "");
        return -1;
    }
    Sha1Init(&s);
    Sha1Add(&s, rom, n);
    Sha1Hex(&s, sha1);
    if (strcmp(sha1, sFe8UsaSha1) != 0)
    {
        PortLog("[mem] '%s' has the FE8 (USA) header and size, but not the original ROM's contents: its SHA-1 is "
                "%s, the original's %s. It is a changed ROM (a hack, a translation or a patch) or a damaged "
                "copy; the port runs only the original FE8 (USA) ROM.\n", path, sha1, sFe8UsaSha1);
        return -1;
    }
    PortLog("[mem] ROM '%s' loaded, %u bytes, header FIREEMBLEM2E/BE8E OK, SHA-1 %s OK\n", path, (unsigned)n, sha1);
    return 0;
}

int PortMemInit(const char *romPath)
{
    unsigned i;

    for (i = 0; i < ARRAY_COUNT(sRegions); i++)
    {
        if (ReserveRegion(&sRegions[i]) != 0)
            return -1;
        sMapped |= 1u << i;
    }

    /* Cold boot state. */
    memset((void *)(uintptr_t)PORT_SRAM_START, 0xFF, 0x10000); /* blank SRAM, as mGBA */
    REG_KEYINPUT = 0x03FF;                                     /* active low: nothing held */
    REG_WAITCNT = 0;                                           /* main.c:31 reads 0 on a cold boot */
    /* M1 (render-design-bg.md 7.3 item 4): the video registers as mGBA's
       GBAIOInit leaves them. Forced blank shows white until the first flush. */
    REG_DISPCNT = DISPCNT_FORCED_BLANK;
    REG_BG2PA = 0x100;
    REG_BG2PD = 0x100;
    REG_BG3PA = 0x100;
    REG_BG3PD = 0x100;
    /* The sound registers as the BIOS leaves them at power-on: SOUNDBIAS
       0x0200, the rest 0 (audio-core.md item 3). With 0 the bias level is 0
       and every negative DirectSound sample clips. */
    PortPsg_ResetIo((volatile uint8_t *)(uintptr_t)PORT_IO_START);

    if (LoadRom(romPath) != 0)
        return -1;
    PortApplyRomRelocs(); /* risk 12: ROM code pointers -> host functions (romreloc.c) */
    return 0;
}
