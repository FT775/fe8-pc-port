/*
 * Host side of the m4a difftest (m4a_difftest.py). Not part of the game build.
 *
 * Links port/audio/m4a_1_pc.o (built with port/Makefile's PORT_CFLAGS) with
 * src/m4a.c and src/m4a_tables.c (built with HOST_CFLAGS and HOST_CPPFLAGS,
 * plus -Dasm(x)=((void)0), as audio-design.md 1.6 plans). Like the port
 * (D1-a), this 32-bit process puts GBA memory at GBA addresses: EWRAM at
 * 0x02000000, IWRAM at 0x03000000, the I/O page at 0x04000000 and a
 * read-only copy of the ROM at 0x08000000 (a stray write to it faults).
 * m4a.c's IWRAM variables (gSoundInfo, gMPlayJumpTable, gCgbChans,
 * gMPlayMemAccArea, SoundMainRAM_Buffer) and the ROM data it uses
 * (gMPlayTable, gSongTable, SoundMainRAM) are bound to their GBA addresses
 * with --defsym, so both sides touch the same bytes at the same addresses.
 *
 *   m4a_harness ROM_IMAGE
 *
 * On start it prints "SYM <name> <hex host address>" for every function
 * that can appear as a code pointer in driver memory, then "END". Then it
 * reads little-endian u32 commands on stdin and answers on stdout:
 *   0 QUIT
 *   1 WRITE addr len bytes[len, padded to 4]
 *   2 READ addr len                  -> bytes[len]
 *   3 CRC addr len                   -> u32 (zlib CRC-32)
 *   4 CALL fn a0 a1 a2               -> u32 (return value, 0 for void)
 *   5 VCOUNT on                      (1: a thread toggles VCOUNT 159/160)
 *   6 UMUL n pairs[2n]               -> u32[n]
 *   7 CHNVOL n chan track recs[n]    (rec: ve | rp<<8 | volMR<<16 | volML<<24)
 *                                    -> u32[n] (rightVolume | leftVolume<<8)
 *   8 VCSEQ start every              (the _vcount build: VCOUNT reads advance)
 */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;

#define EWRAM_BASE 0x02000000u
#define EWRAM_SIZE 0x00040000u
#define IWRAM_BASE 0x03000000u
#define IWRAM_SIZE 0x00008000u
#define IO_BASE    0x04000000u
#define IO_SIZE    0x00001000u
#define ROM_BASE   0x08000000u
#define ROM_SIZE   0x01000000u

/* port/audio/m4a_1_pc.c (C signatures of the asm routines) */
u32 umul3232H32(u32, u32);
void SoundMain(void);
void m4aSoundVSync(void);
void MPlayMain(void *);
void ply_note(u32, void *, void *);
void SoundMainBTM(void *);
void RealClearChain(void *);
void TrackStop(void *, void *);
void ChnVolSetAsm(void *, void *);
void clear_modM(void *);
u32 ld_r3_tp_adr_i(void *);
u32 ld_r3_tp_adr_i_unchecked(void *);
int chk_adr_r2(u32);
u32 ldrb_r3_r2(u32);
void MPlayJumpTableCopy(void **);
typedef void (*Cmd)(void *, void *);
void ply_fine(void *, void *);
void ply_goto(void *, void *);
void ply_patt(void *, void *);
void ply_pend(void *, void *);
void ply_rept(void *, void *);
void ply_prio(void *, void *);
void ply_tempo(void *, void *);
void ply_keysh(void *, void *);
void ply_voice(void *, void *);
void ply_vol(void *, void *);
void ply_pan(void *, void *);
void ply_bend(void *, void *);
void ply_bendr(void *, void *);
void ply_lfodl(void *, void *);
void ply_modt(void *, void *);
void ply_tune(void *, void *);
void ply_port(void *, void *);
void ply_lfos(void *, void *);
void ply_mod(void *, void *);
void ply_endtie(void *, void *);

/* src/m4a.c */
void m4aSoundInit(void);
void m4aSoundMain(void);
void m4aSongNumStart(u16);
void m4aSongNumStartOrChange(u16);
void m4aSongNumStartOrContinue(u16);
void m4aSongNumStop(u16);
void m4aSongNumContinue(u16);
void m4aMPlayAllStop(void);
void m4aMPlayAllContinue(void);
void m4aMPlayContinue(void *);
void m4aMPlayStop(void *);
void m4aMPlayFadeOut(void *, u16);
void m4aMPlayFadeOutTemporarily(void *, u16);
void m4aMPlayFadeIn(void *, u16);
void m4aMPlayImmInit(void *);
void m4aMPlayTempoControl(void *, u16);
void m4aMPlayVolumeControl(void *, u16, u16);
void m4aMPlayPitchControl(void *, u16, s16);
void m4aMPlayPanpotControl(void *, u16, s8);
void m4aMPlayModDepthSet(void *, u16, u8);
void m4aMPlayLFOSpeedSet(void *, u16, u8);
void m4aSoundMode(u32);
void MPlayStart(void *, void *);
void MPlayFadeOut(void *, u16);
void FadeOutBody(void *);
void TrkVolPitSet(void *, void *);
void CgbSound(void);
void CgbOscOff(u8);
u32 MidiKeyToCgbFreq(u8, u8, u8);
u32 MidiKeyToFreq(void *, u8, u8);
void DummyFunc(void);
void SampleFreqSet(u32);
void ply_memacc(void *, void *);
void ply_xcmd(void *, void *);
void ClearChain(void *);
void Clear64byte(void *);

/* ------------------------------------------------------------------ */
/* BIOS CpuSet, enough for src/m4a.c (CpuCopy32, CpuFill32). GBATEK:
 * r2 bits 0-20 count, bit 24 fill, bit 26 32-bit units.              */

void CpuSet(const void *src, void *dst, u32 ctrl)
{
    u32 n = ctrl & 0x1FFFFF, i;

    if (ctrl & (1u << 26))
    {
        const u32 *s = (const u32 *)((uintptr_t)src & ~3u);
        u32 *d = (u32 *)((uintptr_t)dst & ~3u);
        u32 v = *s;

        for (i = 0; i < n; i++)
            d[i] = (ctrl & (1u << 24)) ? v : s[i];
    }
    else
    {
        const u16 *s = (const u16 *)((uintptr_t)src & ~1u);
        u16 *d = (u16 *)((uintptr_t)dst & ~1u);
        u16 v = *s;

        for (i = 0; i < n; i++)
            d[i] = (ctrl & (1u << 24)) ? v : s[i];
    }
}

void CpuFastSet(const void *src, void *dst, u32 ctrl)
{
    CpuSet(src, dst, (ctrl & ~0x1FFFFFu) | (((ctrl & 0x1FFFFF) + 7) & ~7u) | (1u << 26));
}

/* ------------------------------------------------------------------ */

static void Die(const char *what)
{
    fprintf(stderr, "m4a_harness: %s (%s)\n", what, strerror(errno));
    exit(1);
}

static void *MapFixed(u32 addr, u32 size)
{
    void *p = mmap((void *)(uintptr_t)addr, size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    if (p == MAP_FAILED || p != (void *)(uintptr_t)addr)
        Die("cannot map GBA memory");
    return p;
}

static u32 sCrcTable[256];

static void CrcInit(void)
{
    u32 i, c;
    int k;

    for (i = 0; i < 256; i++)
    {
        c = i;
        for (k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        sCrcTable[i] = c;
    }
}

static u32 Crc32(const u8 *p, u32 len)
{
    u32 c = 0xFFFFFFFFu;

    while (len--)
        c = sCrcTable[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static int InRange(u32 addr, u32 len, int write)
{
    static const u32 base[] = { EWRAM_BASE, IWRAM_BASE, IO_BASE, ROM_BASE };
    static const u32 size[] = { EWRAM_SIZE, IWRAM_SIZE, IO_SIZE, ROM_SIZE };
    int i;

    for (i = 0; i < (write ? 3 : 4); i++)
        if (addr >= base[i] && len <= size[i] && addr - base[i] <= size[i] - len)
            return 1;
    return 0;
}

static void ReadAll(void *buf, size_t n)
{
    u8 *p = buf;

    while (n)
    {
        ssize_t r = read(0, p, n);

        if (r <= 0)
            exit(r == 0 ? 0 : 1);
        p += r;
        n -= (size_t)r;
    }
}

static u32 In32(void)
{
    u32 v;

    ReadAll(&v, 4);
    return v;
}

static u8 sOut[1 << 16];
static u32 sOutLen;

static void Flush(void)
{
    u32 off = 0;

    while (off < sOutLen)
    {
        ssize_t w = write(1, sOut + off, sOutLen - off);

        if (w <= 0)
            exit(1);
        off += (u32)w;
    }
    sOutLen = 0;
}

static void Out(const void *p, u32 n)
{
    const u8 *b = p;

    while (n)
    {
        u32 k = sizeof(sOut) - sOutLen;

        if (k > n)
            k = n;
        memcpy(sOut + sOutLen, b, k);
        sOutLen += k;
        b += k;
        n -= k;
        if (sOutLen == sizeof(sOut))
            Flush();
    }
}

static void Out32(u32 v) { Out(&v, 4); }

/* VCOUNT toggler, for SampleFreqSet's two spin loops (m4a.c:382-386). */
static volatile int sToggle;
static pthread_t sToggleThread;

static void *ToggleMain(void *arg)
{
    volatile u8 *vcount = (volatile u8 *)(uintptr_t)(IO_BASE + 6);
    u8 v = 158;

    (void)arg;
    while (sToggle)
    {
        v = (u8)(v == 160 ? 158 : v + 1);
        *vcount = v;
        usleep(20);
    }
    return NULL;
}

#define P(x) ((void *)(uintptr_t)(x))

/* The _vcount build: m4a_1_pc.c reads VCOUNT through this. Each read returns
 * the current value; every `every` reads it moves on by one line. */
static u32 sVcValue, sVcEvery = 1, sVcReads;

u32 PortM4aTestVcount(void)
{
    u32 v = sVcValue;

    if (++sVcReads % sVcEvery == 0)
        sVcValue = (sVcValue + 1) % 228;
    return v;
}

struct Fn
{
    const char *name;
    void *code; /* for the SYM table: functions that may be stored as code pointers */
};

/* Functions whose addresses the driver keeps in memory. */
static const struct Fn sCodePtrs[] = {
#define F(n) { #n, (void *)(n) }
    F(MPlayMain), F(ply_note), F(CgbSound), F(CgbOscOff), F(MidiKeyToCgbFreq), F(DummyFunc),
    F(ply_fine), F(ply_goto), F(ply_patt), F(ply_pend), F(ply_rept), F(ply_memacc), F(ply_prio),
    F(ply_tempo), F(ply_keysh), F(ply_voice), F(ply_vol), F(ply_pan), F(ply_bend), F(ply_bendr),
    F(ply_lfos), F(ply_lfodl), F(ply_mod), F(ply_modt), F(ply_tune), F(ply_port), F(ply_xcmd),
    F(ply_endtie), F(SampleFreqSet), F(TrackStop), F(FadeOutBody), F(TrkVolPitSet),
    F(RealClearChain), F(SoundMainBTM),
#undef F
};

static const char *const sCallNames[] = {
    "umul3232H32", "SoundMain", "m4aSoundVSync", "MPlayMain", "ply_note", "SoundMainBTM",
    "RealClearChain", "TrackStop", "ChnVolSetAsm", "clear_modM", "ld_r3_tp_adr_i",
    "ld_r3_tp_adr_i_unchecked", "chk_adr_r2", "ldrb_r3_r2", "MPlayJumpTableCopy",
    "ply_fine", "ply_goto", "ply_patt", "ply_pend", "ply_rept", "ply_prio", "ply_tempo",
    "ply_keysh", "ply_voice", "ply_vol", "ply_pan", "ply_bend", "ply_bendr", "ply_lfodl",
    "ply_modt", "ply_tune", "ply_port", "ply_lfos", "ply_mod", "ply_endtie",
    "m4aSoundInit", "m4aSoundMain", "m4aSongNumStart", "m4aSongNumStartOrChange",
    "m4aSongNumStartOrContinue", "m4aSongNumStop", "m4aSongNumContinue", "m4aMPlayAllStop",
    "m4aMPlayAllContinue", "m4aMPlayContinue", "m4aMPlayStop", "m4aMPlayFadeOut",
    "m4aMPlayFadeOutTemporarily", "m4aMPlayFadeIn", "m4aMPlayImmInit", "m4aMPlayTempoControl",
    "m4aMPlayVolumeControl", "m4aMPlayPitchControl", "m4aMPlayPanpotControl",
    "m4aMPlayModDepthSet", "m4aMPlayLFOSpeedSet", "m4aSoundMode", "MPlayStart", "MPlayFadeOut",
    "FadeOutBody", "TrkVolPitSet", "CgbSound", "CgbOscOff", "MidiKeyToCgbFreq", "MidiKeyToFreq",
    "ClearChain", "Clear64byte",
};

static u32 Call(u32 fn, u32 a0, u32 a1, u32 a2)
{
    static const Cmd cmds[] = {
        ply_fine, ply_goto, ply_patt, ply_pend, ply_rept, ply_prio, ply_tempo, ply_keysh,
        ply_voice, ply_vol, ply_pan, ply_bend, ply_bendr, ply_lfodl, ply_modt, ply_tune,
        ply_port, ply_lfos, ply_mod, ply_endtie,
    };

    switch (fn)
    {
    case 0: return umul3232H32(a0, a1);
    case 1: SoundMain(); return 0;
    case 2: m4aSoundVSync(); return 0;
    case 3: MPlayMain(P(a0)); return 0;
    case 4: ply_note(a0, P(a1), P(a2)); return 0;
    case 5: SoundMainBTM(P(a0)); return 0;
    case 6: RealClearChain(P(a0)); return 0;
    case 7: TrackStop(P(a0), P(a1)); return 0;
    case 8: ChnVolSetAsm(P(a0), P(a1)); return 0;
    case 9: clear_modM(P(a0)); return 0;
    case 10: return ld_r3_tp_adr_i(P(a0));
    case 11: return ld_r3_tp_adr_i_unchecked(P(a0));
    case 12: return chk_adr_r2(a0) ? a1 : 0; /* asm: r2 = address, r3 = value */
    case 13: return ldrb_r3_r2(a0);
    case 14: MPlayJumpTableCopy(P(a0)); return 0;
    case 15: case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23: case 24:
    case 25: case 26: case 27: case 28: case 29: case 30: case 31: case 32: case 33: case 34:
        cmds[fn - 15](P(a0), P(a1));
        return 0;
    case 35: m4aSoundInit(); return 0;
    case 36: m4aSoundMain(); return 0;
    case 37: m4aSongNumStart((u16)a0); return 0;
    case 38: m4aSongNumStartOrChange((u16)a0); return 0;
    case 39: m4aSongNumStartOrContinue((u16)a0); return 0;
    case 40: m4aSongNumStop((u16)a0); return 0;
    case 41: m4aSongNumContinue((u16)a0); return 0;
    case 42: m4aMPlayAllStop(); return 0;
    case 43: m4aMPlayAllContinue(); return 0;
    case 44: m4aMPlayContinue(P(a0)); return 0;
    case 45: m4aMPlayStop(P(a0)); return 0;
    case 46: m4aMPlayFadeOut(P(a0), (u16)a1); return 0;
    case 47: m4aMPlayFadeOutTemporarily(P(a0), (u16)a1); return 0;
    case 48: m4aMPlayFadeIn(P(a0), (u16)a1); return 0;
    case 49: m4aMPlayImmInit(P(a0)); return 0;
    case 50: m4aMPlayTempoControl(P(a0), (u16)a1); return 0;
    case 51: m4aMPlayVolumeControl(P(a0), (u16)a1, (u16)a2); return 0;
    case 52: m4aMPlayPitchControl(P(a0), (u16)a1, (s16)a2); return 0;
    case 53: m4aMPlayPanpotControl(P(a0), (u16)a1, (s8)a2); return 0;
    case 54: m4aMPlayModDepthSet(P(a0), (u16)a1, (u8)a2); return 0;
    case 55: m4aMPlayLFOSpeedSet(P(a0), (u16)a1, (u8)a2); return 0;
    case 56: m4aSoundMode(a0); return 0;
    case 57: MPlayStart(P(a0), P(a1)); return 0;
    case 58: MPlayFadeOut(P(a0), (u16)a1); return 0;
    case 59: FadeOutBody(P(a0)); return 0;
    case 60: TrkVolPitSet(P(a0), P(a1)); return 0;
    case 61: CgbSound(); return 0;
    case 62: CgbOscOff((u8)a0); return 0;
    case 63: return MidiKeyToCgbFreq((u8)a0, (u8)a1, (u8)a2);
    case 64: return MidiKeyToFreq(P(a0), (u8)a1, (u8)a2);
    case 65: ClearChain(P(a0)); return 0;
    case 66: Clear64byte(P(a0)); return 0;
    }
    fprintf(stderr, "m4a_harness: bad function %u\n", fn);
    exit(1);
}

int main(int argc, char **argv)
{
    static u8 buf[0x40000];
    FILE *f;
    u8 *rom;
    size_t n;
    u32 i;

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s ROM_IMAGE\n", argv[0]);
        return 1;
    }
    CrcInit();
    MapFixed(EWRAM_BASE, EWRAM_SIZE);
    MapFixed(IWRAM_BASE, IWRAM_SIZE);
    MapFixed(IO_BASE, IO_SIZE);
    rom = MapFixed(ROM_BASE, ROM_SIZE);
    f = fopen(argv[1], "rb");
    if (!f)
        Die("cannot open ROM image");
    n = fread(rom, 1, ROM_SIZE, f);
    fclose(f);
    if (n == 0)
        Die("empty ROM image");
    if (mprotect(rom, ROM_SIZE, PROT_READ) != 0)
        Die("mprotect");

    for (i = 0; i < sizeof(sCodePtrs) / sizeof(sCodePtrs[0]); i++)
        printf("SYM %s %08X\n", sCodePtrs[i].name, (u32)(uintptr_t)sCodePtrs[i].code);
    for (i = 0; i < sizeof(sCallNames) / sizeof(sCallNames[0]); i++)
        printf("FN %u %s\n", i, sCallNames[i]);
    printf("END\n");
    fflush(stdout);

    for (;;)
    {
        u32 op = In32();

        switch (op)
        {
        case 0:
            Flush();
            return 0;
        case 1:
        {
            u32 addr = In32(), len = In32(), padded = (len + 3) & ~3u;

            if (!InRange(addr, len, 1) || padded > sizeof(buf))
            {
                fprintf(stderr, "m4a_harness: bad WRITE 0x%08X+%u\n", addr, len);
                return 1;
            }
            ReadAll(buf, padded);
            memcpy(P(addr), buf, len);
            break;
        }
        case 2:
        {
            u32 addr = In32(), len = In32();

            if (!InRange(addr, len, 0))
            {
                fprintf(stderr, "m4a_harness: bad READ 0x%08X+%u\n", addr, len);
                return 1;
            }
            Out(P(addr), len);
            break;
        }
        case 3:
        {
            u32 addr = In32(), len = In32();

            if (!InRange(addr, len, 0))
            {
                fprintf(stderr, "m4a_harness: bad CRC 0x%08X+%u\n", addr, len);
                return 1;
            }
            Out32(Crc32(P(addr), len));
            break;
        }
        case 4:
        {
            u32 fn = In32(), a0 = In32(), a1 = In32(), a2 = In32();

            Out32(Call(fn, a0, a1, a2));
            break;
        }
        case 5:
        {
            u32 on = In32();

            if (on && !sToggle)
            {
                sToggle = 1;
                if (pthread_create(&sToggleThread, NULL, ToggleMain, NULL) != 0)
                    Die("pthread_create");
            }
            else if (!on && sToggle)
            {
                sToggle = 0;
                pthread_join(sToggleThread, NULL);
            }
            break;
        }
        case 6:
        {
            u32 k = In32();

            while (k--)
            {
                u32 a = In32(), b = In32();

                Out32(umul3232H32(a, b));
            }
            break;
        }
        case 7:
        {
            u32 k = In32(), chan = In32(), track = In32();

            while (k--)
            {
                u32 rec = In32();

                *(u8 *)P(chan + 0x12) = (u8)rec;
                *(u8 *)P(chan + 0x14) = (u8)(rec >> 8);
                *(u8 *)P(track + 0x10) = (u8)(rec >> 16);
                *(u8 *)P(track + 0x11) = (u8)(rec >> 24);
                ChnVolSetAsm(P(chan), P(track));
                Out32(*(u8 *)P(chan + 2) | ((u32) * (u8 *)P(chan + 3) << 8));
            }
            break;
        }
        case 8:
            sVcValue = In32();
            sVcEvery = In32();
            sVcReads = 0;
            if (sVcEvery == 0)
                sVcEvery = 1;
            break;
        default:
            fprintf(stderr, "m4a_harness: bad op %u\n", op);
            return 1;
        }
        Flush();
    }
}
