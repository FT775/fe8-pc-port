#include "global.h"

#include <string.h>

#include "port_hw.h"
#include "../audio/psg.h"

/*
 * C replacements for the SWI wrappers in src/libagbsyscall.s (PLAN.md 3.6).
 * The copy/fill and decompression routines follow GBATEK. The arithmetic SWIs
 * (Div, DivRem, DivArm, Sqrt, ArcTan2, BgAffineSet, ObjAffineSet) are in
 * bios_math.c. VBlankIntrWait lives in port/hw/frame.c.
 */

static u32 Load32(const u8 *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24);
}

/*
 * BIOS-area source (DIVERGENCES.md D-8, was P-7). On the GBA, CpuSet and
 * CpuFastSet do nothing when the source start or end address reaches into the
 * BIOS area (GBATEK, "BIOS Memory Copy"; mGBA's HLE BIOS skips the SWI with
 * "Cannot CpuSet from BIOS"). The game relies on it: UpdateBanimFrame
 * (banim-ekrmain.c:233, 263) copies from gBanimTriAtkPalettes[], which is NULL
 * for every non-ballista animation. The BIOS tests address bits 25-27; that
 * test cannot be copied, because host code and data at 0x10000000 and up have
 * those bits clear too. The host tests the BIOS area itself, 0x0000-0x3FFF:
 * - the start of the source;
 * - the end of a moving source: a range that runs past 0xFFFFFFFF wraps into
 *   the BIOS area on a 32-bit bus;
 * - not the end of a fixed source (CpuFill*): it reads one unit, and on the
 *   host the fill value may sit on the stack near 0xFFFFFFFF, where start plus
 *   length wraps although nothing is read there.
 * The first hit from each caller is logged; the call then returns.
 */
#define PORT_BIOS_AREA_END 0x4000u

static int SourceInBiosArea(const char *name, const void *src, void *dest, u32 bytes, int fixed,
                            uintptr_t site)
{
    static uintptr_t sLoggedSites[16];
    static unsigned sLoggedCount;
    uint64_t start = (uintptr_t)src;
    unsigned i;

    if (start >= PORT_BIOS_AREA_END && (fixed || bytes == 0 || start + bytes - 1 <= 0xFFFFFFFFu))
        return 0;
    for (i = 0; i < sLoggedCount && sLoggedSites[i] != site; i++)
        ;
    if (i == sLoggedCount && sLoggedCount < sizeof(sLoggedSites) / sizeof(sLoggedSites[0]))
    {
        sLoggedSites[sLoggedCount++] = site;
        PortLog("[bios-copy] %s from 0x%08X (0x%X bytes) to 0x%08X skipped, as the GBA BIOS does: "
                "the source is in the BIOS area (host caller 0x%08X, frame %u; first time at this site)\n",
                name, (unsigned)start, bytes, (unsigned)(uintptr_t)dest, (unsigned)site, gPortFrameCount);
    }
    return 1;
}

void CpuSet(const void *src, void *dest, u32 control)
{
    u32 count = control & 0x1FFFFF;
    int fixed = (control & CPU_SET_SRC_FIXED) != 0;
    u32 i;

    if (SourceInBiosArea("CpuSet", src, dest, count << ((control & CPU_SET_32BIT) ? 2 : 1), fixed,
                         (uintptr_t)__builtin_return_address(0)))
        return;

    if (control & CPU_SET_32BIT)
    {
        const vu32 *s = (const vu32 *)((uintptr_t)src & ~3u);
        vu32 *d = (vu32 *)((uintptr_t)dest & ~3u);
        for (i = 0; i < count; i++)
        {
            *d++ = *s;
            if (!fixed)
                s++;
        }
    }
    else
    {
        const vu16 *s = (const vu16 *)((uintptr_t)src & ~1u);
        vu16 *d = (vu16 *)((uintptr_t)dest & ~1u);
        for (i = 0; i < count; i++)
        {
            *d++ = *s;
            if (!fixed)
                s++;
        }
    }
}

void CpuFastSet(const void *src, void *dest, u32 control)
{
    /* Word count rounded up to a multiple of 8 (GBATEK). Unconfirmed: plan section 8. */
    u32 count = ((control & 0x1FFFFF) + 7) & ~7u;
    int fixed = (control & CPU_FAST_SET_SRC_FIXED) != 0;
    const vu32 *s = (const vu32 *)((uintptr_t)src & ~3u);
    vu32 *d = (vu32 *)((uintptr_t)dest & ~3u);
    u32 i;

    if (SourceInBiosArea("CpuFastSet", src, dest, count << 2, fixed, (uintptr_t)__builtin_return_address(0)))
        return;

    for (i = 0; i < count; i++)
    {
        *d++ = *s;
        if (!fixed)
            s++;
    }
}

static void LZ77UnComp(const void *src, void *dest)
{
    const u8 *s = src;
    u8 *d = dest;
    u32 size = Load32(s) >> 8;
    u32 out = 0;

    s += 4;
    while (out < size)
    {
        u8 flags = *s++;
        int bit;
        for (bit = 0; bit < 8 && out < size; bit++, flags <<= 1)
        {
            if (flags & 0x80)
            {
                u32 len = (s[0] >> 4) + 3;
                u32 disp = (((s[0] & 0xF) << 8) | s[1]) + 1;
                s += 2;
                while (len-- && out < size)
                {
                    d[out] = d[out - disp];
                    out++;
                }
            }
            else
            {
                d[out++] = *s++;
            }
        }
    }
}

/* The Wram and Vram variants produce the same bytes on PC (PLAN.md 3.6). */
void LZ77UnCompWram(const void *src, void *dest)
{
    LZ77UnComp(src, dest);
}

void LZ77UnCompVram(const void *src, void *dest)
{
    LZ77UnComp(src, dest);
}

static void RLUnComp(const void *src, void *dest)
{
    const u8 *s = src;
    u8 *d = dest;
    u32 size = Load32(s) >> 8;
    u32 out = 0;

    s += 4;
    while (out < size)
    {
        u8 flag = *s++;
        u32 len;
        if (flag & 0x80)
        {
            u8 value = *s++;
            for (len = (flag & 0x7F) + 3; len && out < size; len--)
                d[out++] = value;
        }
        else
        {
            for (len = (flag & 0x7F) + 1; len && out < size; len--)
                d[out++] = *s++;
        }
    }
}

void RLUnCompWram(const void *src, void *dest)
{
    RLUnComp(src, dest);
}

void RLUnCompVram(const void *src, void *dest)
{
    RLUnComp(src, dest);
}

void HuffUnComp(void const *src, void *dst)
{
    const u8 *s = src;
    u32 header = Load32(s);
    int bitsPerUnit = header & 0xF;
    u32 size = header >> 8;
    const u8 *tree = s + 4;
    const u8 *root = tree + 1;
    const u8 *stream = tree + (tree[0] + 1) * 2;
    const u8 *node = root;
    u8 *d = dst;
    u32 written = 0, acc = 0;
    int accBits = 0;

    while (written < size)
    {
        u32 word = Load32(stream);
        int i;
        stream += 4;
        for (i = 0; i < 32 && written < size; i++, word <<= 1)
        {
            int bit = (word >> 31) & 1;
            u8 n = *node;
            const u8 *next = (const u8 *)(((uintptr_t)node & ~(uintptr_t)1) + (n & 0x3F) * 2 + 2 + bit);
            int isData = bit ? (n & 0x40) : (n & 0x80);

            if (!isData)
            {
                node = next;
                continue;
            }
            acc |= (u32)*next << accBits;
            accBits += bitsPerUnit;
            node = root;
            if (accBits == 32)
            {
                memcpy(d + written, &acc, 4);
                written += 4;
                acc = 0;
                accBits = 0;
            }
        }
    }
}

void RegisterRamReset(u32 resetFlags)
{
    if (resetFlags & RESET_EWRAM)
        memset((void *)(uintptr_t)PORT_EWRAM_START, 0, 0x40000);
    if (resetFlags & RESET_IWRAM)
        memset((void *)(uintptr_t)PORT_IWRAM_START, 0, 0x7E00); /* top 0x200 bytes kept */
    if (resetFlags & RESET_PALETTE)
        memset((void *)(uintptr_t)PORT_PLTT_START, 0, 0x400);
    if (resetFlags & RESET_VRAM)
        memset((void *)(uintptr_t)PORT_VRAM_START, 0, 0x18000);
    if (resetFlags & RESET_OAM)
        memset((void *)(uintptr_t)PORT_OAM_START, 0, 0x400);
    if (resetFlags & RESET_SIO_REGS)
    {
        /* KEYINPUT (0x130) sits inside this block. It is read-only and always
           shows the buttons held now, so no BIOS reset changes it (mGBA's HLE
           BIOS never stores to it; P-13 measured 0x03F0 with the combo held at
           every reset point). Keep the image: the held keys stay held after the
           reset, so they are not read as a new press. */
        u16 keyinput = REG_KEYINPUT;

        memset((void *)(uintptr_t)(PORT_IO_START + 0x120), 0, 0x40);
        REG_KEYINPUT = keyinput;
    }
    if (resetFlags & RESET_SOUND_REGS)
    {
        /* 4000060h-40000AFh cleared, then SOUNDBIAS back to its BIOS value
           0x0200 (as mGBA's HLE BIOS does; audio-core.md item 3). */
        memset((void *)(uintptr_t)(PORT_IO_START + 0x60), 0, 0x50);
        PortPsg_ResetIo((volatile uint8_t *)(uintptr_t)PORT_IO_START);
    }
    if (resetFlags & RESET_REGS)
    {
        /* As mGBA's HLE BIOS leaves them (P-13, measured after AgbMain's
           RegisterRamReset(~2)): the video registers, DMA, timers, IE, IF,
           WAITCNT and IME 0, the BG2 and BG3 matrices at scale 1.0 as at power-on
           (D-17). WAITCNT too: AgbMain reads it before its own RegisterRamReset
           (main.c:31-34), so SoftReset(0) still counts as a software reset, while
           a SoftReset with this flag (0xFE, 0xFF) leaves 0 and the next boot
           counts as a power-on, as on the GBA. */
        memset((void *)(uintptr_t)PORT_IO_START, 0, 0x60);
        memset((void *)(uintptr_t)(PORT_IO_START + 0xB0), 0, 0x70);
        memset((void *)(uintptr_t)(PORT_IO_START + 0x200), 0, 0x10);
        REG_BG2PA = 0x100;
        REG_BG2PD = 0x100;
        REG_BG3PA = 0x100;
        REG_BG3PD = 0x100;
    }
    /* P-13: forced blank whatever the flags (GBATEK, "BIOS Reset Functions";
       mGBA's HLE BIOS writes it first). The GBA draws the first frame after a
       soft reset white, as at power-on. */
    REG_DISPCNT = DISPCNT_FORCED_BLANK;
}

void SoftReset(u32 resetFlags)
{
    /* The libagbsyscall.s wrapper (0x080D16B0): IME = 0 (a byte store), the
       stack to 0x03007F00, `svc 1` (RegisterRamReset with the same flags), then
       `svc 0`. That one clears 0x03007E00-0x03007FFF (the BIOS's stacks and its
       words SOUND_INFO_PTR, INTR_CHECK and INTR_VECTOR; GBATEK, mGBA's HLE BIOS)
       and enters the cartridge again. */
    *(vu8 *)(uintptr_t)(PORT_IO_START + 0x208) = 0;
    RegisterRamReset(resetFlags);
    memset((void *)(uintptr_t)(PORT_IWRAM_START + 0x7E00), 0, 0x200);
    PortSoftReset();
}

void SoundBiasReset(void)
{
}

void SoundBiasSet(void)
{
}
