/*
 * Save states for the Gate 0 harness (port/test/README.md, "Save states";
 * port-plan/pending/savestate.md). Test-only: linked into the Gate 0 test
 * binary next to gate0_hook.c (port/test/Makefile `pc`), never into a shipped
 * build, and every entry point does nothing unless one of the variables
 * below is set.
 *
 *   GATE0_STATE_OUT=DIR, GATE0_STATE_AT=T[,T...]
 *       At the sample point of each tick T (gate0_hook.c, __wrap_VBlankIntrWait
 *       with gBmSt.main_loop_ended set, before anything is done for tick T),
 *       write DIR/tick<T>/<part>/: part is pc, pclag or pcaff for the plain
 *       run, the lag replay or the affine replay. It holds the GBA memory the
 *       PC keeps (regions.bin), the whole host .data and .bss (hostdata.bin),
 *       a copy of this binary and its link map (the relink reads its symbol
 *       table), tick T-1's trace line and raw record (prologue.*) and
 *       meta.json.
 *   GATE0_STATE_EXIT=1   exit after the last listed tick is saved.
 *   GATE0_STATE_SET=ID   the snapshot set id, copied into meta.json.
 *   GATE0_STATE_IN=DIR/tick<T>
 *       Resume: at the first sample point of this run (tick 0, the same
 *       program point), apply DIR/tick<T>/<part>/relink-<crc32>/patch.bin,
 *       which port/test/tools/savestate.py relink made for this binary, write
 *       tick T-1's line and raw record, and go on as tick T.
 *
 * What is restored, and how host addresses survive a rebuild, is decided by
 * savestate.py; this file only dumps raw bytes and applies a patch made for
 * its own binary (it checks the CRC-32 of /proc/self/exe).
 */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "port_hw.h"

#define SNAP_NONE 0xFFFFFFFFu
#define RAW_RECORD 0x2869u /* gate0_hook.c sHashed: 0x69 + 0x400 + 4 * 0x800 + 0x400 */
#define PATCH_MAGIC "FE8SNPT3"

struct PortRomReloc
{
    uint32_t site;
    uint32_t gbaValue;
    const void *target;
    int32_t addend;
};
extern const struct PortRomReloc gPortRomRelocs[];
extern const unsigned gPortRomRelocCount;

/* The GBA memory the PC keeps (port/hw/memory.c), in this order in regions.bin.
   The ROM is not saved: it must be unchanged (checked). */
static const struct
{
    const char *name;
    uint32_t addr, size;
} sRegions[] = {
    { "EWRAM", PORT_EWRAM_START, 0x40000 },
    { "IWRAM", PORT_IWRAM_START, 0x8000 },
    { "IO", PORT_IO_START, 0x1000 },
    { "PLTT", PORT_PLTT_START, 0x1000 },
    { "VRAM", PORT_VRAM_START, 0x18000 },
    { "OAM", PORT_OAM_START, 0x1000 },
    { "SRAM", PORT_SRAM_START, 0x10000 },
};
#define REGION_COUNT (sizeof(sRegions) / sizeof(sRegions[0]))

static int sMode;                   /* 0 off, 1 save, 2 resume */
static const char *sKind;           /* pc, pclag, pcaff */
static const char *sOutDir;
static uint32_t sAt[16], sAtCount;
static uint32_t sRomImageCrcBoot;   /* CRC-32 of the whole ROM window at boot (save mode) */
static uint32_t sExeCrc, sRomCrc, sKeysCrc;
static uint32_t sDataAddr, sDataSize, sBssAddr, sBssSize;
static const char *sSramHeader = "";

/* Resume: the patch, read whole. */
static uint8_t *sPatch;
static size_t sPatchSize;
static uint32_t sResumeTick;
static const char *sPrologue, *sPrologueRaw;
static uint32_t sPrologueLen, sPrologueRawLen;
static const uint8_t *sRecords;
static uint32_t sRecordCount;
static int sResumed;
static char sResumeDir[1024];

static void Fail(const char *msg, const char *arg)
{
    fprintf(stderr, "savestate: %s%s\n", msg, arg ? arg : "");
    fflush(stderr);
    exit(4);
}

static uint32_t Crc32(uint32_t crc, const uint8_t *p, size_t n)
{
    static uint32_t table[256];
    unsigned i, k;

    if (table[1] == 0)
        for (i = 0; i < 256; i++)
        {
            uint32_t v = i;
            for (k = 0; k < 8; k++)
                v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            table[i] = v;
        }
    crc = ~crc;
    while (n--)
        crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static uint32_t FileCrc(const char *path, int *ok)
{
    FILE *f = fopen(path, "rb");
    uint8_t buf[65536];
    size_t n;
    uint32_t crc = 0;

    *ok = f != NULL;
    if (f == NULL)
        return 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        crc = Crc32(crc, buf, n);
    fclose(f);
    return crc;
}

/* The CRC-32 of the ROM file this run loaded: the image at 0x08000000 with the
   words PortApplyRomRelocs rewrote put back to the GBA's values. The GBA side
   (oracle/mgba_lua_run.c) takes the CRC-32 of the ROM file it runs. */
static uint32_t RomFileCrc(void)
{
    uint32_t size = PortRomSize(), i;
    uint8_t *copy = malloc(size ? size : 1);
    uint32_t crc;

    if (copy == NULL)
        Fail("out of memory for the ROM CRC", NULL);
    memcpy(copy, (const void *)(uintptr_t)PORT_ROM_START, size);
    for (i = 0; i < gPortRomRelocCount; i++)
    {
        const struct PortRomReloc *r = &gPortRomRelocs[i];
        uint32_t want = (uint32_t)(uintptr_t)r->target + (uint32_t)r->addend, off = r->site - PORT_ROM_START;

        if (r->site >= PORT_ROM_START && off + 4 <= size && memcmp(copy + off, &want, 4) == 0)
            memcpy(copy + off, &r->gbaValue, 4);
    }
    crc = Crc32(0, copy, size);
    free(copy);
    return crc;
}

static uint32_t RomImageCrc(void)
{
    return Crc32(0, (const uint8_t *)(uintptr_t)PORT_ROM_START, PORT_ROM_MAX);
}

/* This binary: its CRC-32 (as gate0_hook.c's exe_crc32) and where .data and .bss are. */
static void ReadSelf(void)
{
    const uint8_t *map;
    const Elf32_Ehdr *eh;
    const Elf32_Shdr *sh;
    const char *names;
    struct stat st;
    unsigned i;
    int fd = open("/proc/self/exe", O_RDONLY);

    if (fd < 0 || fstat(fd, &st) != 0)
        Fail("cannot read /proc/self/exe", NULL);
    map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED)
        Fail("cannot map /proc/self/exe", NULL);
    sExeCrc = Crc32(0, map, (size_t)st.st_size);
    eh = (const Elf32_Ehdr *)map;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS32)
        Fail("/proc/self/exe is not a 32-bit ELF file", NULL);
    sh = (const Elf32_Shdr *)(map + eh->e_shoff);
    names = (const char *)(map + sh[eh->e_shstrndx].sh_offset);
    for (i = 0; i < eh->e_shnum; i++)
    {
        if (strcmp(names + sh[i].sh_name, ".data") == 0)
        {
            sDataAddr = sh[i].sh_addr;
            sDataSize = sh[i].sh_size;
        }
        else if (strcmp(names + sh[i].sh_name, ".bss") == 0)
        {
            sBssAddr = sh[i].sh_addr;
            sBssSize = sh[i].sh_size;
        }
    }
    munmap((void *)map, (size_t)st.st_size);
    if (sDataSize == 0 || sBssSize == 0)
        Fail("no .data or .bss section in /proc/self/exe", NULL);
}

static const char *Kind(void)
{
    if (getenv("GATE0_LAG_FROM") != NULL)
        return "pclag";
    if (getenv("GATE0_AFFINE_FROM") != NULL)
        return "pcaff";
    if (getenv("GATE0_CLOCK_FROM") != NULL)
        return "pcclock";
    return "pc";
}

static void MkdirP(const char *path)
{
    char buf[1024];
    char *p;

    if (strlen(path) >= sizeof(buf))
        Fail("path too long: ", path);
    strcpy(buf, path);
    for (p = buf + 1; *p != '\0'; p++)
        if (*p == '/')
        {
            *p = '\0';
            if (mkdir(buf, 0777) != 0 && errno != EEXIST)
                Fail("cannot make directory ", buf);
            *p = '/';
        }
    if (mkdir(buf, 0777) != 0 && errno != EEXIST)
        Fail("cannot make directory ", buf);
}

static FILE *OpenIn(const char *dir, const char *name, const char *mode)
{
    char path[1200];
    FILE *f;

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, mode);
    if (f == NULL)
        Fail("cannot open ", path);
    return f;
}

static void CopyFile(const char *from, const char *dir, const char *name, int required)
{
    FILE *in = fopen(from, "rb"), *out;
    uint8_t buf[65536];
    size_t n;

    if (in == NULL)
    {
        if (required)
            Fail("cannot read ", from);
        return;
    }
    out = OpenIn(dir, name, "wb");
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        if (fwrite(buf, 1, n, out) != n)
            Fail("cannot write into ", dir);
    fclose(in);
    if (fclose(out) != 0)
        Fail("cannot write into ", dir);
}

static void WriteJsonString(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s != '\0'; s++)
    {
        if (*s == '"' || *s == '\\')
            fprintf(f, "\\%c", *s);
        else if ((unsigned char)*s < 0x20)
            fprintf(f, "\\u%04x", (unsigned char)*s);
        else
            fputc(*s, f);
    }
    fputc('"', f);
}

/* The last complete line of a trace file (tick T-1's), without its newline. */
static char *LastLine(const char *path, uint32_t *len)
{
    FILE *f = fopen(path, "rb");
    off_t size, from;
    char *buf, *end, *start;
    size_t n;

    if (f == NULL || fseeko(f, 0, SEEK_END) != 0 || (size = ftello(f)) <= 0)
        Fail("cannot read the trace for the prologue: ", path);
    from = size > 1048576 ? size - 1048576 : 0;
    buf = malloc((size_t)(size - from) + 1);
    if (buf == NULL || fseeko(f, from, SEEK_SET) != 0 || (n = fread(buf, 1, (size_t)(size - from), f)) != (size_t)(size - from))
        Fail("cannot read the trace for the prologue: ", path);
    fclose(f);
    buf[n] = '\0';
    if (n == 0 || buf[n - 1] != '\n')
        Fail("the trace does not end with a complete line: ", path);
    end = buf + n - 1;
    *end = '\0';
    start = strrchr(buf, '\n');
    start = start ? start + 1 : buf;
    if (strncmp(start, "{\"tick\":", 8) != 0)
        Fail("the trace's last line is not a tick line: ", path);
    *len = (uint32_t)strlen(start);
    memmove(buf, start, *len + 1);
    return buf;
}

static void SaveSnapshot(uint32_t tick)
{
    char dir[1024], exePath[1024], mapPath[1100];
    const char *out = getenv("GATE0_OUT"), *raw = getenv("GATE0_RAW"), *keys = getenv("GATE0_KEYS");
    const char *set = getenv("GATE0_STATE_SET");
    FILE *f;
    ssize_t n;
    unsigned i;

    if (strcmp(sKind, "pcclock") == 0)
        Fail("snapshots of the clock replay (GATE0_CLOCK_FROM, a diagnostic) are not supported", NULL);
    if (PortNullProbe_Enabled())
        Fail("snapshots of an FE8PC_NULLPROBE run are not supported", NULL);
    if (gPortIrqDepth != 0)
        Fail("snapshot asked for inside an IRQ handler", NULL);
    if (RomImageCrc() != sRomImageCrcBoot)
        Fail("the ROM image at 0x08000000 changed since boot (the run wrote to it); a snapshot does not hold "
             "the ROM, so it cannot be taken", NULL);

    snprintf(dir, sizeof(dir), "%s/tick%u/%s", sOutDir, tick, sKind);
    MkdirP(dir);

    f = OpenIn(dir, "regions.bin", "wb");
    for (i = 0; i < REGION_COUNT; i++)
        if (fwrite((const void *)(uintptr_t)sRegions[i].addr, 1, sRegions[i].size, f) != sRegions[i].size)
            Fail("cannot write regions.bin in ", dir);
    if (fclose(f) != 0)
        Fail("cannot write regions.bin in ", dir);

    f = OpenIn(dir, "hostdata.bin", "wb");
    if (fwrite((const void *)(uintptr_t)sDataAddr, 1, sDataSize, f) != sDataSize
        || fwrite((const void *)(uintptr_t)sBssAddr, 1, sBssSize, f) != sBssSize || fclose(f) != 0)
        Fail("cannot write hostdata.bin in ", dir);

    n = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (n <= 0)
        Fail("cannot read the path of /proc/self/exe", NULL);
    exePath[n] = '\0';
    CopyFile("/proc/self/exe", dir, "fe8pc", 1);
    snprintf(mapPath, sizeof(mapPath), "%s.map", exePath);
    CopyFile(mapPath, dir, "fe8pc.map", 1);

    if (tick > 0)
    {
        uint32_t len;
        char *line = LastLine(out, &len);

        f = OpenIn(dir, "prologue.jsonl", "wb");
        fwrite(line, 1, len, f);
        fputc('\n', f);
        fclose(f);
        free(line);
        if (raw != NULL)
        {
            FILE *r = fopen(raw, "rb");
            uint8_t rec[RAW_RECORD];

            if (r == NULL || fseeko(r, -(off_t)RAW_RECORD, SEEK_END) != 0 || fread(rec, 1, RAW_RECORD, r) != RAW_RECORD)
                Fail("cannot read the last raw record of ", raw);
            fclose(r);
            f = OpenIn(dir, "prologue.raw", "wb");
            fwrite(rec, 1, RAW_RECORD, f);
            fclose(f);
        }
    }

    f = OpenIn(dir, "meta.json", "w");
    fprintf(f, "{\"format\":\"fe8-pc-snapshot-1\",\"part\":\"%s\",\"tick\":%u,\"set\":", sKind, tick);
    WriteJsonString(f, set ? set : "");
    fprintf(f, ",\"exe\":");
    WriteJsonString(f, exePath);
    fprintf(f, ",\"exe_crc32\":\"%08x\",\"keys\":", sExeCrc);
    WriteJsonString(f, keys ? keys : "");
    fprintf(f, ",\"keys_crc32\":\"%08x\",\"rom_crc32\":\"%08x\",\"sram_header\":", sKeysCrc, sRomCrc);
    WriteJsonString(f, sSramHeader);
    fprintf(f, ",\"raw\":%s,\"frame\":%u,\"onmain_calls\":%u,\"regions\":[", raw && tick > 0 ? "true" : "false",
        gPortFrameCount, gPortOnMainCalls);
    for (i = 0; i < REGION_COUNT; i++)
        fprintf(f, "%s[\"%s\",%u,%u]", i ? "," : "", sRegions[i].name, sRegions[i].addr, sRegions[i].size);
    fprintf(f, "],\"data\":[%u,%u],\"bss\":[%u,%u]}\n", sDataAddr, sDataSize, sBssAddr, sBssSize);
    if (fclose(f) != 0)
        Fail("cannot write meta.json in ", dir);
    PortLog("[savestate] tick %u saved to %s (frame %u, OnMain calls %u)\n", tick, dir, gPortFrameCount,
        gPortOnMainCalls);
}

static uint32_t Get32(const uint8_t **p, const uint8_t *end)
{
    uint32_t v;

    if (end - *p < 4)
        Fail("patch.bin is cut short in ", sResumeDir);
    memcpy(&v, *p, 4);
    *p += 4;
    return v;
}

static const char *GetBlob(const uint8_t **p, const uint8_t *end, uint32_t *len)
{
    const char *s;

    *len = Get32(p, end);
    if ((size_t)(end - *p) < *len)
        Fail("patch.bin is cut short in ", sResumeDir);
    s = (const char *)*p;
    *p += *len;
    return s;
}

/* Every patch record must lie in the GBA memory saved above or in this
   binary's .data/.bss. */
static int RecordAllowed(uint32_t addr, uint32_t len)
{
    unsigned i;

    for (i = 0; i < REGION_COUNT; i++)
        if (addr >= sRegions[i].addr && len <= sRegions[i].size && addr - sRegions[i].addr <= sRegions[i].size - len)
            return 1;
    if (addr >= sDataAddr && len <= sDataSize && addr - sDataAddr <= sDataSize - len)
        return 1;
    return addr >= sBssAddr && len <= sBssSize && addr - sBssAddr <= sBssSize - len;
}

static void LoadPatch(const char *stateDir, char *header, size_t cap)
{
    char path[1300];
    const char *keys = getenv("GATE0_KEYS");
    const uint8_t *p, *end;
    const char *part, *frag, *sram;
    uint32_t exeCrc, keysCrc, romCrc, metaCrc, regionsCrc, hostCrc, partLen, fragLen, sramLen, i;
    const char *madeFrom[3] = { "meta.json", "regions.bin", "hostdata.bin" };
    uint32_t madeCrc[3];
    FILE *f;
    off_t size;
    int ok;

    snprintf(sResumeDir, sizeof(sResumeDir), "%s/%s", stateDir, sKind);
    snprintf(path, sizeof(path), "%s/relink-%08x/patch.bin", sResumeDir, sExeCrc);
    f = fopen(path, "rb");
    if (f == NULL)
    {
        fprintf(stderr, "savestate: no %s: make it for this binary first with\n"
                        "  python3 port/test/tools/savestate.py relink --part %s --exe <this binary>\n"
                        "(port/test/Makefile does it for GATE0_STATE_IN)\n", path, sResumeDir);
        exit(4);
    }
    if (fseeko(f, 0, SEEK_END) != 0 || (size = ftello(f)) < 8 || fseeko(f, 0, SEEK_SET) != 0)
        Fail("cannot read ", path);
    sPatchSize = (size_t)size;
    sPatch = malloc(sPatchSize);
    if (sPatch == NULL || fread(sPatch, 1, sPatchSize, f) != sPatchSize)
        Fail("cannot read ", path);
    fclose(f);
    p = sPatch;
    end = sPatch + sPatchSize;
    if (memcmp(p, PATCH_MAGIC, 8) != 0)
        Fail("not a savestate patch (wrong magic): ", path);
    p += 8;
    exeCrc = Get32(&p, end);
    sResumeTick = Get32(&p, end);
    keysCrc = Get32(&p, end);
    romCrc = Get32(&p, end);
    metaCrc = Get32(&p, end);
    regionsCrc = Get32(&p, end);
    hostCrc = Get32(&p, end);
    part = GetBlob(&p, end, &partLen);
    frag = GetBlob(&p, end, &fragLen);
    sram = GetBlob(&p, end, &sramLen);
    sPrologue = GetBlob(&p, end, &sPrologueLen);
    sPrologueRaw = GetBlob(&p, end, &sPrologueRawLen);
    sRecordCount = Get32(&p, end);
    sRecords = p;
    for (i = 0; i < sRecordCount; i++)
    {
        uint32_t addr = Get32(&p, end), len = Get32(&p, end);

        if ((size_t)(end - p) < len)
            Fail("patch.bin is cut short in ", sResumeDir);
        if (!RecordAllowed(addr, len))
        {
            fprintf(stderr, "savestate: patch record 0x%08X+0x%X is outside the GBA memory and this binary's "
                            ".data/.bss (%s)\n", addr, len, path);
            exit(4);
        }
        p += len;
    }
    if (p != end)
        Fail("trailing bytes in ", path);
    if (exeCrc != sExeCrc)
    {
        fprintf(stderr, "savestate: %s was made for the binary with CRC-32 %08x, this one is %08x\n", path, exeCrc,
            sExeCrc);
        exit(4);
    }
    /* A part saved again into the same directory leaves the older relink-*
       directories behind: a patch is only good for the snapshot it was made
       from: its meta.json (the set, tick, keys and binary) and its memory
       (a re-save with other lag or affine input files keeps meta.json byte
       for byte). */
    madeCrc[0] = metaCrc;
    madeCrc[1] = regionsCrc;
    madeCrc[2] = hostCrc;
    for (i = 0; i < 3; i++)
    {
        char file[1100];

        snprintf(file, sizeof(file), "%s/%s", sResumeDir, madeFrom[i]);
        if (FileCrc(file, &ok) != madeCrc[i] || !ok)
        {
            fprintf(stderr, "savestate: %s/relink-%08x/patch.bin was made from another snapshot than the one now in "
                            "%s (its %s differs: saved again since?); make it again with\n"
                            "  python3 port/test/tools/savestate.py relink --part %s --exe <this binary>\n",
                sResumeDir, sExeCrc, sResumeDir, madeFrom[i], sResumeDir);
            exit(4);
        }
    }
    if (partLen != strlen(sKind) || memcmp(part, sKind, partLen) != 0)
        Fail("the patch is for another kind of run than this one: ", sKind);
    if (sResumeTick == 0)
        Fail("a snapshot at tick 0 is not supported (start from power-on): ", path);
    if (keysCrc != sKeysCrc)
    {
        fprintf(stderr, "savestate: the key file %s (CRC-32 %08x) is not the snapshot's (%08x)\n",
            keys ? keys : "(none)", sKeysCrc, keysCrc);
        exit(4);
    }
    if (romCrc != sRomCrc)
    {
        fprintf(stderr, "savestate: the ROM this run loaded (CRC-32 %08x) is not the snapshot's (%08x)\n", sRomCrc,
            romCrc);
        exit(4);
    }
    /* GATE0_SRAM_IN may be given again (the Makefile exports it), but it must
       be the save the snapshot's run booted from. */
    if (sSramHeader[0] != '\0' && (strlen(sSramHeader) != sramLen || memcmp(sSramHeader, sram, sramLen) != 0))
        Fail("GATE0_SRAM_IN is not the save the snapshot's run booted from", NULL);
    if (fragLen + 1 > cap)
        Fail("the snapshot's trace header is too long", NULL);
    memcpy(header, frag, fragLen);
    header[fragLen] = '\0';
    PortLog("[savestate] resuming at tick %u from %s (%u patch records)\n", sResumeTick, path, sRecordCount);
}

/* Called from gate0_hook.c's Setup, before the trace header is written.
   header holds the sram_in fragment of GATE0_SRAM_IN (or ""); in resume mode
   it is replaced by the snapshot's sram fragment and the "resume" block.
   ticks: GATE0_TICKS (the run ends after tick ticks - 1). */
int PortSnap_Setup(char *header, size_t cap, uint32_t ticks)
{
    const char *out = getenv("GATE0_STATE_OUT"), *in = getenv("GATE0_STATE_IN");
    const char *keys = getenv("GATE0_KEYS");
    char num[16];
    unsigned i;

    if ((out == NULL || *out == '\0') && (in == NULL || *in == '\0'))
        return 0;
    if (out != NULL && *out != '\0' && in != NULL && *in != '\0')
        Fail("GATE0_STATE_OUT and GATE0_STATE_IN together are not supported", NULL);
    sKind = Kind();
    sSramHeader = strdup(header);
    ReadSelf();
    sRomCrc = RomFileCrc();
    if (keys != NULL)
    {
        int ok;

        sKeysCrc = FileCrc(keys, &ok);
        if (!ok)
            Fail("cannot read the key file ", keys);
    }
    if (strcmp(sKind, "pcclock") == 0)
        Fail("save states do not support the clock replay (GATE0_CLOCK_FROM, a diagnostic)", NULL);
    if (in != NULL && *in != '\0')
    {
        sMode = 2;
        LoadPatch(in, header, cap);
        if (ticks <= sResumeTick)
        {
            snprintf(num, sizeof(num), "%u", sResumeTick);
            Fail("GATE0_TICKS must be past the snapshot's tick (the last tick + 1, as from power-on): tick ", num);
        }
        return 0;
    }
    sMode = 1;
    sOutDir = out;
    {
        const char *s = getenv("GATE0_STATE_AT");
        char *end;

        if (s == NULL || *s == '\0')
            Fail("GATE0_STATE_OUT needs GATE0_STATE_AT=T[,T...]", NULL);
        while (*s != '\0')
        {
            unsigned long t = strtoul(s, &end, 10);

            if (end == s || sAtCount == sizeof(sAt) / sizeof(sAt[0]) || t == 0)
                Fail("bad GATE0_STATE_AT (ticks from 1, at most 16): ", getenv("GATE0_STATE_AT"));
            sAt[sAtCount++] = (uint32_t)t;
            s = end + (*end == ',');
        }
    }
    for (i = 0; i < sAtCount; i++)
        if (sAt[i] >= ticks)
        {
            snprintf(num, sizeof(num), "%u", sAt[i]);
            Fail("GATE0_STATE_AT is not before GATE0_TICKS (the run ends before that sample point): tick ", num);
        }
    sRomImageCrcBoot = RomImageCrc();
    PortLog("[savestate] %s run: snapshots at %u tick(s) into %s\n", sKind, sAtCount, sOutDir);
    return 0;
}

/* The sample point of the tick in progress (gate0_hook.c). Resume: on the
   first call, apply the patch, write the prologue (tick T-1's line and raw
   record) and return T, the tick the hook must now be at. Save: write the
   snapshot when the tick is listed. Otherwise SNAP_NONE. */
uint32_t PortSnap_AtSample(uint32_t tick, FILE *out, FILE *raw)
{
    unsigned i;

    if (sMode == 2 && !sResumed)
    {
        const uint8_t *p = sRecords;

        sResumed = 1;
        if (tick != 0)
            Fail("the resume must happen at the run's first sample point", NULL);
        for (i = 0; i < sRecordCount; i++)
        {
            uint32_t addr, len;

            memcpy(&addr, p, 4);
            memcpy(&len, p + 4, 4);
            memcpy((void *)(uintptr_t)addr, p + 8, len);
            p += 8 + len;
        }
        fwrite(sPrologue, 1, sPrologueLen, out);
        fputc('\n', out);
        fflush(out);
        if (raw != NULL)
        {
            if (sPrologueRawLen != RAW_RECORD)
                Fail("GATE0_RAW is set, but the snapshot's run wrote no raw dump (no prologue record)", NULL);
            fwrite(sPrologueRaw, 1, sPrologueRawLen, raw);
            fflush(raw);
        }
        PortLog("[savestate] resumed at tick %u (frame %u, OnMain calls %u)\n", sResumeTick, gPortFrameCount,
            gPortOnMainCalls);
        return sResumeTick;
    }
    if (sMode == 1)
        for (i = 0; i < sAtCount; i++)
            if (sAt[i] == tick)
            {
                unsigned j;
                uint32_t last = 0;

                SaveSnapshot(tick);
                for (j = 0; j < sAtCount; j++)
                    if (sAt[j] > last)
                        last = sAt[j];
                if (tick == last && getenv("GATE0_STATE_EXIT") != NULL && strcmp(getenv("GATE0_STATE_EXIT"), "1") == 0)
                {
                    PortLog("[savestate] GATE0_STATE_EXIT=1: stopping after the snapshot at tick %u\n", tick);
                    fflush(NULL);
                    exit(0);
                }
            }
    return SNAP_NONE;
}

/* A resumed run's prologue, tick T-1's line of the photo's run, as a
   NUL-terminated copy the caller frees, for the test cheats' check
   (gate0_hook.c CheatResume); NULL when this run is not resumed. */
char *PortSnap_Prologue(void)
{
    char *copy;

    if (sMode != 2 || sPrologue == NULL)
        return NULL;
    copy = malloc(sPrologueLen + 1);
    if (copy == NULL)
        Fail("out of memory", NULL);
    memcpy(copy, sPrologue, sPrologueLen);
    copy[sPrologueLen] = '\0';
    return copy;
}

/* The first tick of a resumed GBA trace (its prologue, T-1), so the hook's
   readers of GATE0_LAG_FROM index by absolute tick; 0 otherwise. */
uint32_t PortSnap_TraceBase(void)
{
    return sMode == 2 ? sResumeTick - 1 : 0;
}

/* The last tick a snapshot is saved at in this run (GATE0_STATE_AT); 0 when
   none is. The lag replay checks that its GBA trace has that tick: a tick's
   lag VBlanks inside the main loop run before its sample point, where the
   photo is taken. */
uint32_t PortSnap_LastSaveTick(void)
{
    uint32_t last = 0, i;

    if (sMode != 1)
        return 0;
    for (i = 0; i < sAtCount; i++)
        if (sAt[i] > last)
            last = sAt[i];
    return last;
}

/* The absolute tick of the first record of a GBA raw dump (GATE0_AFFINE_FROM).
   Resume: a resumed GBA capture's dump starts at tick T-1, a full one at 0;
   which it is shows by comparing with the GBA snapshot's prologue record. */
uint32_t PortSnap_GbaRawBase(FILE *f, uint32_t recordSize, uint32_t records)
{
    uint8_t want[RAW_RECORD], got[RAW_RECORD];
    char path[1200];
    FILE *g;
    uint32_t t;

    if (sMode != 2)
        return 0;
    if (recordSize != RAW_RECORD)
        Fail("unexpected raw record size", NULL);
    snprintf(path, sizeof(path), "%.1100s/../gba/prologue.raw", sResumeDir);
    g = fopen(path, "rb");
    if (g == NULL || fread(want, 1, RAW_RECORD, g) != RAW_RECORD)
        Fail("cannot read the GBA snapshot's prologue record ", path);
    fclose(g);
    t = sResumeTick - 1;
    if (records >= 1 && fseeko(f, 0, SEEK_SET) == 0 && fread(got, 1, RAW_RECORD, f) == RAW_RECORD
        && memcmp(got, want, RAW_RECORD) == 0)
        return t;
    if (records > t && fseeko(f, (off_t)t * RAW_RECORD, SEEK_SET) == 0 && fread(got, 1, RAW_RECORD, f) == RAW_RECORD
        && memcmp(got, want, RAW_RECORD) == 0)
        return 0;
    Fail("GATE0_AFFINE_FROM is neither the resumed GBA capture's raw dump nor a full one of this snapshot's run", NULL);
    return 0;
}
