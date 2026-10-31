#include "global.h"

#include "port_hw.h"
#include "bios_math.h"

/*
 * BIOS arithmetic for the PC port (PLAN.md 3.6): SWI 6 Div (and the DivRem
 * wrapper), SWI 7 DivArm, SWI 8 Sqrt, SWI 9 ArcTan, SWI 10 ArcTan2, SWI 14
 * BgAffineSet and SWI 15 ObjAffineSet. Integer only: no floating point.
 *
 * Each routine returns what the GBA BIOS returns. port/test/bios checks them
 * against the hardware test vectors of mgba-emu/suite (bios-math.c, MIT),
 * which give r0, r1 and r3 after the SWI on a real GBA.
 *
 * Provenance (full entry: port-plan/pending/merged/bios-math.md). This file was
 * written for the port from the descriptions in the comments below: GBATEK
 * (inputs, outputs, ranges, "endless loop" on division by zero), the suite's
 * hardware vectors, and the BIOS facts listed in
 * port-plan/community-reuse-audio-bios.md 2.2 (ArcTan coefficients, the
 * truncated sine table, the affine formula). Cult-of-GBA's replacement BIOS
 * (MIT) was read to cross-check the affine entry sizes and start-point
 * formula. No code was copied from it, from mGBA, from VBA-M or from any
 * other BIOS implementation. The ArcTan/ArcTan2 code that was here before had
 * mGBA's (MPL-2.0) structure; it has been removed, and the versions below are
 * a rewrite with a different structure.
 */

/* ---- 32-bit ARM arithmetic --------------------------------------------- */

/* ARM MUL: the low 32 bits of the product. */
static s32 Mul32(s32 a, s32 b)
{
    return (s32)((u32)a * (u32)b);
}

/* ARM ADD: wraps at 32 bits. */
static s32 Add32(s32 a, s32 b)
{
    return (s32)((u32)a + (u32)b);
}

/* ARM SUB: wraps at 32 bits. */
static s32 Sub32(s32 a, s32 b)
{
    return (s32)((u32)a - (u32)b);
}

/* ARM ASR: shift right, filling with the sign bit (rounds toward minus infinity). */
static s32 Asr32(s32 v, int n)
{
    return v < 0 ? ~(~v >> n) : v >> n;
}

/* ---- SWI 6 Div, SWI 7 DivArm ------------------------------------------- */

/*
 * The results are those of dividing the magnitudes as unsigned 32-bit
 * numbers: the quotient gets the sign of num XOR denom, the remainder the
 * sign of num, and r3 is the unsigned quotient magnitude (GBATEK: -1234 / 10
 * gives -123, -4, +123). This model fits GBATEK and all four hardware rows.
 *
 * INT_MIN / -1: the magnitudes are 0x80000000 and 1, so r0 = 0x80000000
 * (INT_MIN), r1 = 0, r3 = 0x80000000, as the hardware vector shows. The host
 * never runs a signed divide, so nothing traps (x86 idiv raises SIGFPE here).
 *
 * Division by zero: GBATEK says the BIOS "usually gets caught in an endless
 * loop". The hardware vectors show that it returns for three numerators:
 * 0 / 0 = 1 rem 0, 1 / 0 = 1 rem 1, -1 / 0 = -1 rem -1 (r3 = 1 each time).
 * That is the result of taking the zero denominator away once, with the signs
 * applied as above. A model that explains it [inferred, not a hardware fact]:
 * the BIOS first doubles the denominator while it is below half the
 * numerator's magnitude; a zero denominator never gets there unless that
 * magnitude is 0 or 1. For every other numerator the BIOS never returns.
 */
int PortBiosDivRegs(int32_t num, int32_t denom, struct PortBiosDivRegs *out)
{
    u32 n = num < 0 ? 0u - (u32)num : (u32)num;
    u32 d = denom < 0 ? 0u - (u32)denom : (u32)denom;
    u32 q, r;

    if (d != 0)
    {
        q = n / d;
        r = n % d;
    }
    else if (n <= 1)
    {
        q = 1;
        r = n;
    }
    else
    {
        return 0;
    }

    out->r0 = (s32)((num < 0) != (denom < 0) ? 0u - q : q);
    out->r1 = (s32)(num < 0 ? 0u - r : r);
    out->r3 = q;
    return 1;
}

/*
 * The BIOS does not return from this division. On the GBA the game's code
 * stops here for good, while interrupts go on: the VBlank and sound handlers
 * still run, the game's main loop does not. (IRQs stay enabled inside a SWI
 * [inferred]: VBlankIntrWait waits inside the BIOS for the game's own IRQ
 * handler to run.) The port does the same: it logs the call, then runs
 * frames forever. A headless run still ends at its frame limit (FE8PC_FRAMES).
 */
static void __attribute__((noreturn)) DivNeverReturns(const char *name, s32 num, uintptr_t site)
{
    PortLog("[bios] %s: %d / 0 (host caller 0x%08X, frame %u). The GBA BIOS never returns from this "
            "division (only 0, 1 and -1 divided by 0 return), so the game stops here; frames and "
            "interrupts go on, as on the GBA.\n",
            name, (int)num, (unsigned)site, gPortFrameCount);
    for (;;)
        PortRunFrame();
}

static s32 DivSwi(const char *name, s32 num, s32 denom, int wantRemainder, uintptr_t site)
{
    struct PortBiosDivRegs regs;

    if (!PortBiosDivRegs(num, denom, &regs))
        DivNeverReturns(name, num, site);
    return wantRemainder ? regs.r1 : regs.r0;
}

/* Div is SWI 6 returning r0; DivRem is SWI 6 returning r1 (libagbsyscall.s). */
int Div(int num, int denom)
{
    return DivSwi("Div", num, denom, 0, (uintptr_t)__builtin_return_address(0));
}

int DivRem(int num, int denom)
{
    return DivSwi("DivRem", num, denom, 1, (uintptr_t)__builtin_return_address(0));
}

/* SWI 7: SWI 6 with the operands swapped (r0 = denom, r1 = num). */
int DivArm(int denom, int num)
{
    return DivSwi("DivArm", num, denom, 0, (uintptr_t)__builtin_return_address(0));
}

/* ---- SWI 8 Sqrt -------------------------------------------------------- */

/*
 * The integer square root rounded down (GBATEK: "Sqrt(2) would return 1").
 * Bit-by-bit method: each step decides one bit of the result, from the top.
 * There are no hardware vectors for Sqrt; floor(sqrt(num)) for every u32 is
 * [inferred] from GBATEK and is what port/test/bios checks.
 */
u16 Sqrt(u32 num)
{
    u32 res = 0, bit = 1u << 30;

    while (bit > num)
        bit >>= 2;
    while (bit != 0)
    {
        if (num >= res + bit)
        {
            num -= res + bit;
            res = (res >> 1) + bit;
        }
        else
        {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (u16)res;
}

/* ---- SWI 9 ArcTan ------------------------------------------------------ */

/*
 * tan is 1.14 fixed point; the angle is in units of 1/0x10000 of a turn
 * (0x4000 = pi/2). The BIOS evaluates a polynomial in s = -tan^2:
 *   r1 = s = -(tan * tan >> 14)
 *   r3 = p, by Horner's rule: p = c[0], then p = (p * s >> 14) + c[k], k = 1..7
 *   r0 = tan * p >> 16
 * The coefficients c[] are BIOS facts (they give p = 0xA2F9 at s = 0 and
 * p = 0x8000 at tan = 1.0; the suite's r3 values check p at 24 points).
 * Every product keeps its low 32 bits and every shift is arithmetic, like ARM
 * MUL and ASR: the suite has inputs where the products wrap (0x8001, 0xBFFF).
 * The negation comes after the shift (tan = 0xFFFF gives r1 = 8, not 7).
 */
static const s32 sArcTanCoeffs[8] = { 0xA9, 0x390, 0x91C, 0xFB6, 0x16AA, 0x2081, 0x3651, 0xA2F9 };

int32_t PortBiosArcTan(int32_t tan, int32_t *r1, int32_t *r3)
{
    s32 s = -Asr32(Mul32(tan, tan), 14);
    s32 p = sArcTanCoeffs[0];
    unsigned k;

    for (k = 1; k < sizeof(sArcTanCoeffs) / sizeof(sArcTanCoeffs[0]); k++)
        p = Add32(Asr32(Mul32(p, s), 14), sArcTanCoeffs[k]);
    *r1 = s;
    *r3 = p;
    return Asr32(Mul32(tan, p), 16);
}

/* ---- SWI 10 ArcTan2 ---------------------------------------------------- */

/* (a << 14) / b, rounded toward zero, as the BIOS divides (b != 0 here). */
static s32 Ratio14(s32 a, s32 b)
{
    struct PortBiosDivRegs regs;

    PortBiosDivRegs((s32)((u32)a << 14), b, &regs);
    return regs.r0;
}

/*
 * The angle of the point (x, y): 0 to 0xFFFF for 0 to 2*pi (GBATEK). The BIOS
 * folds the angle into one octant and uses ArcTan on a ratio in [-1, 1]:
 * - y = 0: 0 if x >= 0, else 0x8000. Else x = 0: 0x4000 if y > 0, else 0xC000.
 *   r1 keeps y in both cases (hardware vectors).
 * - |x| > |y|: base + ArcTan((y << 14) / x), with base 0 for x > 0, y > 0;
 *   0x8000 for x < 0; 0x10000 for x > 0, y < 0.
 * - |x| <= |y|: base - ArcTan((x << 14) / y), with base 0x4000 for y > 0 and
 *   0xC000 for y < 0.
 * The ratio is rounded toward zero (vector x = 0xFFFF, y = -0x10000: r1 shows
 * ArcTan got -0x3FFF, not -0x4000). When |x| = |y| both forms give the same
 * angle, since ArcTan(+-0x4000) is exactly +-0x2000. r1 is ArcTan's r1.
 * A result of 0x10000 is possible in the x > 0, y < 0 octant; the game's
 * ArcTan2 returns u16, so it reads 0 there.
 * The suite has no rows with |x| > |y| and y < 0. Those two octants follow
 * the same rule [inferred]; port/test/bios checks them against atan2 only.
 */
int32_t PortBiosArcTan2(int32_t x, int32_t y, int32_t *r1)
{
    u32 ax, ay;
    s32 r3;

    if (y == 0)
    {
        *r1 = y;
        return x >= 0 ? 0 : 0x8000;
    }
    if (x == 0)
    {
        *r1 = y;
        return y > 0 ? 0x4000 : 0xC000;
    }

    ax = x < 0 ? 0u - (u32)x : (u32)x;
    ay = y < 0 ? 0u - (u32)y : (u32)y;
    if (ax > ay)
    {
        s32 base = x < 0 ? 0x8000 : (y < 0 ? 0x10000 : 0);
        return Add32(base, PortBiosArcTan(Ratio14(y, x), r1, &r3));
    }
    return Sub32(y < 0 ? 0xC000 : 0x4000, PortBiosArcTan(Ratio14(x, y), r1, &r3));
}

/* The game passes s16 values; the SWI sees them sign-extended in r0 and r1. */
u16 ArcTan2(s16 x, s16 y)
{
    s32 r1;

    return (u16)PortBiosArcTan2(x, y, &r1);
}

/* ---- SWI 14 BgAffineSet, SWI 15 ObjAffineSet --------------------------- */

/*
 * Sine table: entry i is sin(2 * pi * i / 256) in 1.14 fixed point, truncated
 * toward zero. Generated by port/bios/gen_sine_table.py from that formula; the
 * GBA BIOS table has the same 256 values (community-reuse-audio-bios.md 2.2).
 * Rounding instead of truncating would change 120 of the entries.
 */
const s16 gPortBiosSineTable[256] = {
#include "bios_sine_table.inc"
};

/*
 * Both affine SWIs use the upper 8 bits of the angle only (GBATEK) as the
 * table index; cos is the entry 64 places (a quarter turn) further on. With
 * the 8.8 scales sx and sy, every term is a 32-bit product shifted right 14
 * (arithmetic):
 *   pa = sx * cos >> 14       pb = -(sx * sin >> 14)
 *   pc = sy * sin >> 14       pd = sy * cos >> 14
 * pb is negated after the shift. Negating first rounds the other way, so pb
 * would be 1 lower whenever sx * sin is not a multiple of 0x4000.
 */
#define AFFINE_SIN(angle) ((s32)gPortBiosSineTable[((angle) >> 8) & 0xFF])
#define AFFINE_COS(angle) ((s32)gPortBiosSineTable[(((angle) >> 8) + 0x40) & 0xFF])

/* GBATEK source and destination layouts: 20 and 16 bytes per entry. */
_Static_assert(sizeof(struct BgAffineSrcData) == 20, "BgAffineSet source entry is 20 bytes");
_Static_assert(sizeof(struct BgAffineDstData) == 16, "BgAffineSet destination entry is 16 bytes");

/*
 * The start point (dx, dy) is the texture point that lands on screen (0, 0):
 *   dx = texX - pa * scrX - pb * scrY
 *   dy = texY - pc * scrX - pd * scrY
 * all in 32 bits with wrap-around. The BIOS uses the 32-bit pa..pd here, before
 * they are cut to 16 bits for the destination. The two differ only when a
 * scale is -0x8000 and the angle index is a multiple of 64 [inferred; not
 * confirmed on hardware, and FE8 never passes such a scale].
 */
void BgAffineSet(struct BgAffineSrcData *src, struct BgAffineDstData *dest, s32 count)
{
    for (; count > 0; count--, src++, dest++)
    {
        s32 sn = AFFINE_SIN(src->alpha), cs = AFFINE_COS(src->alpha);
        s32 pa = Asr32(Mul32(src->sx, cs), 14);
        s32 pb = -Asr32(Mul32(src->sx, sn), 14);
        s32 pc = Asr32(Mul32(src->sy, sn), 14);
        s32 pd = Asr32(Mul32(src->sy, cs), 14);

        dest->pa = (s16)pa;
        dest->pb = (s16)pb;
        dest->pc = (s16)pc;
        dest->pd = (s16)pd;
        dest->dx = Sub32(Sub32(src->texX, Mul32(pa, src->scrX)), Mul32(pb, src->scrY));
        dest->dy = Sub32(Sub32(src->texY, Mul32(pc, src->scrX)), Mul32(pd, src->scrY));
    }
}

/*
 * ObjAffineSet writes pa, pb, pc and pd `offset` bytes apart (2 = packed,
 * 8 = the OAM layout). The source is read 8 bytes per entry: sx, sy, the
 * angle and 2 bytes of padding, so for count > 1 it does not walk a C array
 * of the 6-byte struct ObjAffineSrcData. [inferred: Cult-of-GBA's replacement
 * BIOS steps 8 bytes, as BgAffineSet's entries are padded to a word; not
 * confirmed on hardware. FE8 output does not depend on it: every FE8 call
 * passes count 1.]
 */
void ObjAffineSet(struct ObjAffineSrcData *src, void *dest, s32 count, s32 offset)
{
    const u8 *in = (const u8 *)src;
    u8 *out = dest;

    for (; count > 0; count--, in += 8)
    {
        const struct ObjAffineSrcData *e = (const struct ObjAffineSrcData *)in;
        s32 sn = AFFINE_SIN(e->rotation), cs = AFFINE_COS(e->rotation);

        *(s16 *)out = (s16)Asr32(Mul32(e->xScale, cs), 14);
        out += offset;
        *(s16 *)out = (s16)-Asr32(Mul32(e->xScale, sn), 14);
        out += offset;
        *(s16 *)out = (s16)Asr32(Mul32(e->yScale, sn), 14);
        out += offset;
        *(s16 *)out = (s16)Asr32(Mul32(e->yScale, cs), 14);
        out += offset;
    }
}
