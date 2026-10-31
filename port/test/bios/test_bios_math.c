/*
 * Tests for the port's BIOS arithmetic (port/bios/bios_math.c).
 *
 * 1. Hardware vectors: the 123 rows of mgba-emu/suite src/bios-math.c (MIT,
 *    Copyright (c) 2015 Jeffrey Pfau; notice in suite_vectors.h): 4 Div,
 *    24 ArcTan, 95 ArcTan2. Every row must match. Compared: r0, r1 and r3 for
 *    Div and ArcTan; r0 and r1 for ArcTan2. Not compared, because they are not
 *    results of the routine: r2 (the suite stores the SWI opcode there and the
 *    BIOS keeps it), CPSR (0x1F, the state after any SWI) and ArcTan2's r3
 *    (0x170 on all 95 rows, a value the BIOS leaves behind). The test checks
 *    that those three really are constant in the data.
 *    The game's own entry points are checked on the same rows: Div, DivRem
 *    and DivArm on the Div rows, and ArcTan2(s16, s16) on the ArcTan2 rows
 *    whose inputs fit in s16 (FE8 passes s16; community-reuse-audio-bios.md
 *    2.3).
 * 2. Rules from port-plan/community-reuse-audio-bios.md 2.2 and GBATEK, for
 *    inputs the vectors do not cover: Div against exact integer division,
 *    the never-returning divisions by zero, Sqrt = floor(sqrt), the sine
 *    table = trunc(sin * 0x4000), and the affine formula (pb negated after
 *    the shift, 8-byte ObjAffineSet source stride, OAM offset).
 *
 * Run from the repository root: make -f port/test/bios/Makefile
 */

#include "global.h"

#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_hw.h"
#include "bios_math.h"

#include "suite_vectors.h"

/* ---- stubs for the port functions bios_math.c calls ---------------------- */

uint32_t gPortFrameCount;

static char sLastLog[1024];
static unsigned sLogCount;

void PortLog(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(sLastLog, sizeof(sLastLog), fmt, ap);
    va_end(ap);
    sLogCount++;
}

/* A division that never returns runs frames forever; the test leaves after 3. */
static jmp_buf sHangExit;
static int sHangExpected;
static unsigned sHangFrames;

void PortRunFrame(void)
{
    if (!sHangExpected)
    {
        printf("FAIL: PortRunFrame called outside an expected hang\n");
        exit(2);
    }
    if (++sHangFrames == 3)
        longjmp(sHangExit, 1);
}

/* ---- reporting ------------------------------------------------------------ */

static unsigned sChecks, sFailures;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        sChecks++;                                                                                 \
        if (!(cond))                                                                               \
        {                                                                                          \
            sFailures++;                                                                           \
            if (sFailures <= 40)                                                                   \
            {                                                                                      \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                                        \
                printf(__VA_ARGS__);                                                               \
                printf("\n");                                                                      \
            }                                                                                      \
        }                                                                                          \
    } while (0)

static uint32_t sRng = 0x12345678u;

static uint32_t Rand32(void)
{
    /* xorshift32 */
    sRng ^= sRng << 13;
    sRng ^= sRng >> 17;
    sRng ^= sRng << 5;
    return sRng;
}

/* ---- 1. hardware vectors -------------------------------------------------- */

static int FitsS16(uint32_t v)
{
    int32_t s = (int32_t)v;
    return s >= -0x8000 && s <= 0x7FFF;
}

static void TestSuiteVectors(void)
{
    unsigned n = sizeof(sSuiteMathVectors) / sizeof(sSuiteMathVectors[0]);
    unsigned run[16] = { 0 }, pass[16] = { 0 };
    unsigned s16Run = 0, s16Pass = 0, divApiRun = 0, divApiPass = 0;
    unsigned i, total = 0, totalPass = 0;

    for (i = 0; i < n; i++)
    {
        const struct SuiteMathVector *v = &sSuiteMathVectors[i];
        int ok = 0;

        /* The fields this test does not compare are constant in the data. */
        CHECK(v->r2 == (0xEF000000u | (v->swi << 16)), "%s: r2 0x%08X is not the SWI opcode", v->name, v->r2);
        CHECK((v->cpsr & 0xFF) == 0x1F, "%s: CPSR 0x%X", v->name, v->cpsr);

        if (v->swi == 6)
        {
            struct PortBiosDivRegs regs;
            int returned = PortBiosDivRegs((int32_t)v->in0, (int32_t)v->in1, &regs);

            ok = returned && (uint32_t)regs.r0 == v->r0 && (uint32_t)regs.r1 == v->r1 && regs.r3 == v->r3;
            if (!ok)
                printf("FAIL %s: got r0 %08X r1 %08X r3 %08X%s, want %08X %08X %08X\n", v->name,
                       returned ? (unsigned)regs.r0 : 0, returned ? (unsigned)regs.r1 : 0,
                       returned ? (unsigned)regs.r3 : 0, returned ? "" : " (no return)", v->r0, v->r1, v->r3);

            /* The game's entry points on the same row. */
            divApiRun++;
            if ((uint32_t)Div((int)v->in0, (int)v->in1) == v->r0 &&
                (uint32_t)DivRem((int)v->in0, (int)v->in1) == v->r1 &&
                (uint32_t)DivArm((int)v->in1, (int)v->in0) == v->r0)
                divApiPass++;
            else
                printf("FAIL %s: Div/DivRem/DivArm entry points\n", v->name);
        }
        else if (v->swi == 9)
        {
            int32_t r1, r3, r0 = PortBiosArcTan((int32_t)v->in0, &r1, &r3);

            ok = (uint32_t)r0 == v->r0 && (uint32_t)r1 == v->r1 && (uint32_t)r3 == v->r3;
            if (!ok)
                printf("FAIL %s: got r0 %08X r1 %08X r3 %08X, want %08X %08X %08X\n", v->name, (unsigned)r0,
                       (unsigned)r1, (unsigned)r3, v->r0, v->r1, v->r3);
        }
        else if (v->swi == 10)
        {
            int32_t r1, r0 = PortBiosArcTan2((int32_t)v->in0, (int32_t)v->in1, &r1);

            CHECK(v->r3 == 0x170, "%s: ArcTan2 r3 0x%X is not 0x170", v->name, v->r3);
            ok = (uint32_t)r0 == v->r0 && (uint32_t)r1 == v->r1;
            if (!ok)
                printf("FAIL %s: got r0 %08X r1 %08X, want %08X %08X\n", v->name, (unsigned)r0, (unsigned)r1,
                       v->r0, v->r1);

            if (FitsS16(v->in0) && FitsS16(v->in1))
            {
                u16 got = ArcTan2((s16)v->in0, (s16)v->in1);
                s16Run++;
                if (got == (u16)v->r0)
                    s16Pass++;
                else
                    printf("FAIL %s: ArcTan2(s16, s16) = 0x%04X, want 0x%04X\n", v->name, got, (u16)v->r0);
            }
        }
        run[v->swi]++;
        pass[v->swi] += ok;
        total++;
        totalPass += ok;
        sChecks++;
        sFailures += !ok;
    }

    printf("suite vectors: %u run, %u passed, %u failed (Div %u/%u, ArcTan %u/%u, ArcTan2 %u/%u)\n", total,
           totalPass, total - totalPass, pass[6], run[6], pass[9], run[9], pass[10], run[10]);
    printf("  game entry points on the same rows: Div/DivRem/DivArm %u/%u, ArcTan2(s16, s16) %u/%u "
           "(the rows whose inputs fit in s16)\n",
           divApiPass, divApiRun, s16Pass, s16Run);
    CHECK(total == 123 && run[6] == 4 && run[9] == 24 && run[10] == 95, "vector table has %u rows", total);
    CHECK(divApiPass == divApiRun, "Div entry points");
    CHECK(s16Pass == s16Run && s16Run > 0, "ArcTan2(s16, s16)");
}

/* ---- 2a. Div -------------------------------------------------------------- */

static void CheckDivAgainstExact(int32_t num, int32_t denom)
{
    struct PortBiosDivRegs regs;
    int64_t q = (int64_t)num / denom; /* exact in 64 bits, rounded toward zero */
    int64_t r = (int64_t)num - q * denom;

    CHECK(PortBiosDivRegs(num, denom, &regs), "%d / %d did not return", num, denom);
    CHECK(regs.r0 == (int32_t)(uint32_t)q && regs.r1 == (int32_t)r &&
              regs.r3 == (uint32_t)(q < 0 ? -q : q),
          "%d / %d: got %d %d %u, want %lld %lld", num, denom, regs.r0, regs.r1, regs.r3, (long long)q,
          (long long)r);
    CHECK(Div(num, denom) == regs.r0 && DivRem(num, denom) == regs.r1 && DivArm(denom, num) == regs.r0,
          "entry points for %d / %d", num, denom);
}

static void ExpectHang(const char *which, int32_t num)
{
    volatile int returned = 0;

    sHangExpected = 1;
    sHangFrames = 0;
    sLogCount = 0;
    if (setjmp(sHangExit) == 0)
    {
        if (!strcmp(which, "Div"))
            (void)Div(num, 0);
        else if (!strcmp(which, "DivRem"))
            (void)DivRem(num, 0);
        else
            (void)DivArm(0, num);
        returned = 1;
    }
    sHangExpected = 0;
    CHECK(!returned && sHangFrames == 3, "%s(%d, 0) returned instead of running frames", which, num);
    CHECK(sLogCount == 1 && strstr(sLastLog, "[bios] ") && strstr(sLastLog, which), "%s(%d, 0): log '%s'",
          which, num, sLastLog);
}

static void TestDiv(void)
{
    static const int32_t nums[] = { 0, 1, -1, 2, -2, 3, -3, 7, -7, 10, -10, 1234, -1234, 0x7FFF, -0x8000,
                                    0x7FFFFFFF, -0x7FFFFFFF, INT32_MIN, INT32_MIN + 1 };
    static const int32_t denoms[] = { 1, -1, 2, -2, 3, -3, 10, -10, 0x7FFF, -0x8000, 0x7FFFFFFF, INT32_MIN };
    static const int32_t hangNums[] = { 2, -2, 3, 100, -100, 0x7FFFFFFF, INT32_MIN, -0x7FFFFFFF };
    struct PortBiosDivRegs regs;
    unsigned i, j, before = sFailures;

    for (i = 0; i < sizeof(nums) / sizeof(nums[0]); i++)
        for (j = 0; j < sizeof(denoms) / sizeof(denoms[0]); j++)
            CheckDivAgainstExact(nums[i], denoms[j]);
    for (i = 0; i < 200000; i++)
    {
        int32_t d = (int32_t)Rand32() >> (Rand32() & 31);
        if (d != 0)
            CheckDivAgainstExact((int32_t)Rand32() >> (Rand32() & 31), d);
    }

    /* GBATEK's example. */
    CHECK(PortBiosDivRegs(-1234, 10, &regs) && regs.r0 == -123 && regs.r1 == -4 && regs.r3 == 123,
          "-1234 / 10");
    /* INT_MIN / -1 through the game's entry points: no host trap. */
    CHECK(Div(INT32_MIN, -1) == INT32_MIN && DivRem(INT32_MIN, -1) == 0 && DivArm(-1, INT32_MIN) == INT32_MIN,
          "INT_MIN / -1");

    /* Division by zero: returns for 0, 1 and -1 only. */
    for (i = 0; i < sizeof(hangNums) / sizeof(hangNums[0]); i++)
    {
        CHECK(!PortBiosDivRegs(hangNums[i], 0, &regs), "%d / 0 returned", hangNums[i]);
        ExpectHang("Div", hangNums[i]);
        ExpectHang("DivRem", hangNums[i]);
        ExpectHang("DivArm", hangNums[i]);
    }
    CHECK(Div(0, 0) == 1 && DivRem(0, 0) == 0 && Div(1, 0) == 1 && DivRem(1, 0) == 1 && Div(-1, 0) == -1 &&
              DivRem(-1, 0) == -1 && DivArm(0, -1) == -1,
          "x / 0 for x = 0, 1, -1");
    printf("Div: %s (exact division on %u fixed and 200000 random pairs; %u never-returning cases)\n",
           sFailures == before ? "pass" : "FAIL",
           (unsigned)(sizeof(nums) / sizeof(nums[0]) * sizeof(denoms) / sizeof(denoms[0])),
           (unsigned)(3 * sizeof(hangNums) / sizeof(hangNums[0])));
}

/* ---- 2b. Sqrt ------------------------------------------------------------- */

static int SqrtIsFloor(uint32_t x)
{
    uint64_t r = Sqrt(x);
    return r * r <= x && (r + 1) * (r + 1) > x;
}

static void TestSqrt(int exhaustive)
{
    unsigned before = sFailures;
    uint32_t n, i;

    for (n = 0; n <= 0xFFFF; n++)
    {
        uint32_t sq = n * n;
        CHECK(Sqrt(sq) == n, "Sqrt(%u^2)", n);
        CHECK(Sqrt(sq + 2 * n) == n, "Sqrt(%u^2 + 2*%u)", n, n);
        if (n != 0)
            CHECK(Sqrt(sq - 1) == n - 1, "Sqrt(%u^2 - 1)", n);
    }
    for (i = 0; i < 2000000; i++)
    {
        uint32_t x = Rand32() >> (Rand32() & 31);
        CHECK(SqrtIsFloor(x), "Sqrt(%u) = %u", x, Sqrt(x));
    }
    CHECK(Sqrt(2) == 1, "GBATEK: Sqrt(2) = 1");
    CHECK(Sqrt(2u << 30) == 46340, "GBATEK: Sqrt(2 << 30) = 1.41421 << 15");
    CHECK(Sqrt(0xFFFFFFFFu) == 0xFFFF, "Sqrt(0xFFFFFFFF)");
    if (exhaustive)
    {
        uint32_t x = 0;
        unsigned bad = 0;
        do
        {
            bad += !SqrtIsFloor(x);
        } while (++x != 0);
        CHECK(bad == 0, "exhaustive Sqrt: %u inputs are not floor(sqrt)", bad);
    }
    printf("Sqrt: %s (floor(sqrt) at every n^2 - 1, n^2, n^2 + 2n and 2000000 random inputs%s)\n",
           sFailures == before ? "pass" : "FAIL", exhaustive ? ", and all 2^32 inputs" : "");
}

/* ---- 2c. ArcTan2 beyond the vectors --------------------------------------- */

/* Distance between two angles in 1/0x10000 turns, both taken mod 0x10000. */
static int AngleDiff(int32_t a, double b)
{
    double d = fmod((double)(a & 0xFFFF) - b, 65536.0);
    if (d < 0)
        d += 65536.0;
    if (d > 32768.0)
        d = 65536.0 - d;
    return (int)ceil(d);
}

static void TestArcTan2Octants(void)
{
    unsigned before = sFailures, count = 0, octants[8] = { 0 }, minOct = ~0u, maxOct = 0, k;
    int maxErr = 0;
    int32_t x, y;

    /* s16 grid, every octant: the game's input range. */
    for (x = -0x8000; x <= 0x7FFF; x += 97)
        for (y = -0x8000; y <= 0x7FFF; y += 89)
        {
            double want = atan2((double)y, (double)x) * 65536.0 / (2.0 * M_PI);
            int32_t r1, got = PortBiosArcTan2(x, y, &r1);
            int err;

            if (x == 0 && y == 0)
                continue;
            err = AngleDiff(got, want < 0 ? want + 65536.0 : want);
            if (err > maxErr)
                maxErr = err;
            CHECK(ArcTan2((s16)x, (s16)y) == (u16)got, "ArcTan2(%d, %d) entry point", x, y);
            CHECK(got >= 0 && got <= 0x10000, "ArcTan2(%d, %d) = 0x%X out of range", x, y, got);
            octants[(unsigned)((want < 0 ? want + 65536.0 : want) / 8192.0) & 7]++;
            count++;
        }
    /* The polynomial is within a few units of the true angle; a wrong octant
       base would be off by thousands. */
    for (k = 0; k < 8; k++)
    {
        minOct = octants[k] < minOct ? octants[k] : minOct;
        maxOct = octants[k] > maxOct ? octants[k] : maxOct;
    }
    CHECK(maxErr <= 8, "ArcTan2 max error %d units against atan2", maxErr);
    CHECK(minOct > 0, "an octant has no points");
    printf("ArcTan2 octants: %s (%u s16 points, %u..%u per octant, max %d/65536 turn from atan2)\n",
           sFailures == before ? "pass" : "FAIL", count, minOct, maxOct, maxErr);
}

/* ---- 2d. sine table ------------------------------------------------------- */

static void TestSineTable(void)
{
    unsigned before = sFailures, rounding = 0;
    int i;

    for (i = 0; i < 256; i++)
    {
        double v = sin(2.0 * M_PI * i / 256.0) * 0x4000;
        CHECK(gPortBiosSineTable[i] == (int16_t)trunc(v), "sine[%d] = %d, trunc(sin) = %d", i,
              gPortBiosSineTable[i], (int)trunc(v));
        rounding += gPortBiosSineTable[i] != (int16_t)lround(v);
        if (i < 128)
            CHECK(gPortBiosSineTable[i + 128] == -gPortBiosSineTable[i], "sine[%d] symmetry", i);
    }
    CHECK(gPortBiosSineTable[0] == 0 && gPortBiosSineTable[64] == 0x4000 && gPortBiosSineTable[192] == -0x4000,
          "sine table quarter points");
    CHECK(rounding == 120, "rounded table would differ in %u entries, expected 120", rounding);
    printf("sine table: %s (256 entries = trunc(sin(2*pi*i/256) * 0x4000); rounding would change %u)\n",
           sFailures == before ? "pass" : "FAIL", rounding);
}

/* ---- 2e. affine ----------------------------------------------------------- */

/* Reference in 64-bit arithmetic: floor division instead of shifts. */
static int64_t FloorDiv(int64_t a, int64_t b)
{
    int64_t q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

static void ReferenceTerms(s16 sx, s16 sy, u16 angle, int64_t t[4])
{
    int64_t sn = gPortBiosSineTable[angle >> 8], cs = gPortBiosSineTable[((angle >> 8) + 64) & 0xFF];

    t[0] = FloorDiv((int64_t)sx * cs, 0x4000);
    t[1] = -FloorDiv((int64_t)sx * sn, 0x4000);
    t[2] = FloorDiv((int64_t)sy * sn, 0x4000);
    t[3] = FloorDiv((int64_t)sy * cs, 0x4000);
}

static void TestAffine(void)
{
    unsigned before = sFailures, i;
    struct BgAffineSrcData bs;
    struct BgAffineDstData bd;
    struct ObjAffineSrcData os;
    s16 od[16];

    /* Angle 0, scale 1.0: the identity, start point = tex - scr * 1.0. */
    memset(&bs, 0, sizeof(bs));
    bs.texX = 0x123400;
    bs.texY = 0x5600;
    bs.scrX = 120;
    bs.scrY = 80;
    bs.sx = bs.sy = 0x100;
    bs.alpha = 0;
    BgAffineSet(&bs, &bd, 1);
    CHECK(bd.pa == 0x100 && bd.pb == 0 && bd.pc == 0 && bd.pd == 0x100 && bd.dx == 0x123400 - 120 * 0x100 &&
              bd.dy == 0x5600 - 80 * 0x100,
          "identity: %d %d %d %d %d %d", bd.pa, bd.pb, bd.pc, bd.pd, bd.dx, bd.dy);

    /* Angle index 1 (sin 402, cos 16379): sx * sin >> 14 = 6.28 -> 6, so pb =
       -6. Negating before the shift would give -7. */
    bs.alpha = 0x0100;
    BgAffineSet(&bs, &bd, 1);
    CHECK(bd.pa == 255 && bd.pb == -6 && bd.pc == 6 && bd.pd == 255, "angle 1: %d %d %d %d", bd.pa, bd.pb,
          bd.pc, bd.pd);
    CHECK(bd.dx == 0x123400 - 255 * 120 + 6 * 80 && bd.dy == 0x5600 - 6 * 120 - 255 * 80, "angle 1 start: %d %d",
          bd.dx, bd.dy);
    /* The low 8 bits of the angle are ignored. */
    bs.alpha = 0x01FF;
    BgAffineSet(&bs, &bd, 1);
    CHECK(bd.pa == 255 && bd.pb == -6, "angle 0x01FF");
    /* Negative scale: the products round toward minus infinity. */
    bs.alpha = 0x0100;
    bs.sx = -0x100;
    BgAffineSet(&bs, &bd, 1);
    CHECK(bd.pa == -256 && bd.pb == 7, "negative scale: %d %d", bd.pa, bd.pb);

    /* Chosen behaviour, not confirmed on hardware: at scale -0x8000 and angle
       index 64, pb is 0x8000 in 32 bits and the start point uses that value,
       while the stored s16 is -0x8000. FE8 never passes such a scale. */
    memset(&bs, 0, sizeof(bs));
    bs.texX = 0x10000;
    bs.scrY = 1;
    bs.sx = -0x8000;
    bs.sy = 0x100;
    bs.alpha = 0x4000;
    BgAffineSet(&bs, &bd, 1);
    CHECK(bd.pb == -0x8000 && bd.dx == 0x10000 - 0x8000, "scale -0x8000: pb %d dx %d", bd.pb, bd.dx);

    /* Random inputs against the 64-bit reference; ObjAffineSet gives the same terms. */
    for (i = 0; i < 300000; i++)
    {
        int64_t t[4], dx, dy;

        bs.texX = (s32)Rand32();
        bs.texY = (s32)Rand32();
        bs.scrX = (s16)Rand32();
        bs.scrY = (s16)Rand32();
        bs.sx = (s16)Rand32();
        bs.sy = (s16)Rand32();
        bs.alpha = (u16)Rand32();
        if (bs.sx == -0x8000 || bs.sy == -0x8000)
            continue;
        BgAffineSet(&bs, &bd, 1);
        ReferenceTerms(bs.sx, bs.sy, bs.alpha, t);
        dx = (int64_t)bs.texX - t[0] * bs.scrX - t[1] * bs.scrY;
        dy = (int64_t)bs.texY - t[2] * bs.scrX - t[3] * bs.scrY;
        CHECK(bd.pa == t[0] && bd.pb == t[1] && bd.pc == t[2] && bd.pd == t[3], "random terms %u", i);
        CHECK(bd.dx == (s32)(uint32_t)(uint64_t)dx && bd.dy == (s32)(uint32_t)(uint64_t)dy, "random start %u",
              i);

        os.xScale = bs.sx;
        os.yScale = bs.sy;
        os.rotation = bs.alpha;
        ObjAffineSet(&os, od, 1, 2);
        CHECK(od[0] == bd.pa && od[1] == bd.pb && od[2] == bd.pc && od[3] == bd.pd, "ObjAffineSet terms %u", i);
    }

    /* ObjAffineSet: OAM layout (offset 8) writes every fourth halfword. */
    {
        struct ObjAffineSrcData one = { 0x100, 0x200, 0x0100 };
        memset(od, 0x55, sizeof(od));
        ObjAffineSet(&one, od, 1, 8);
        CHECK(od[0] == 255 && od[4] == -6 && od[8] == 12 && od[12] == 511, "OAM layout: %d %d %d %d", od[0],
              od[4], od[8], od[12]);
        CHECK(od[1] == 0x5555 && od[3] == 0x5555 && od[5] == 0x5555 && od[13] == 0x5555, "OAM layout gaps");
    }
    /* ObjAffineSet reads its source 8 bytes per entry: sx, sy, angle, padding. */
    {
        s16 srcWords[8] = { 0x100, 0x100, 0x0000, 0x7777, 0x200, 0x100, 0x4000, 0x7777 };
        memset(od, 0x55, sizeof(od));
        ObjAffineSet((struct ObjAffineSrcData *)srcWords, od, 2, 2);
        CHECK(od[0] == 0x100 && od[1] == 0 && od[2] == 0 && od[3] == 0x100, "entry 0");
        CHECK(od[4] == 0 && od[5] == -0x200 && od[6] == 0x100 && od[7] == 0, "entry 1 at source +8: %d %d %d %d",
              od[4], od[5], od[6], od[7]);
        CHECK(od[8] == 0x5555, "nothing past two entries");
    }
    /* A count of 0 or less writes nothing. */
    {
        struct ObjAffineSrcData one = { 0x100, 0x100, 0 };
        memset(od, 0x55, sizeof(od));
        memset(&bd, 0x55, sizeof(bd));
        ObjAffineSet(&one, od, 0, 2);
        ObjAffineSet(&one, od, -1, 2);
        BgAffineSet(&bs, &bd, 0);
        CHECK(od[0] == 0x5555 && bd.pa == 0x5555, "count 0");
    }
    printf("affine: %s (formula cases, 300000 random inputs against a 64-bit reference, OAM offset, "
           "8-byte source stride)\n",
           sFailures == before ? "pass" : "FAIL");
}

int main(void)
{
    const char *ex = getenv("BIOS_TEST_EXHAUSTIVE");

    TestSuiteVectors();
    TestDiv();
    TestSqrt(ex != NULL && ex[0] == '1');
    TestArcTan2Octants();
    TestSineTable();
    TestAffine();
    printf("%s: %u checks, %u failed\n", sFailures ? "FAIL" : "PASS", sChecks, sFailures);
    return sFailures ? 1 : 0;
}
