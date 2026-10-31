#include "global.h"

/*
 * Unit tests for the renderer's background half (port-plan/render-design-bg.md
 * section 8.2, U1-U15, plus the snapshot, mode table, 8bpp text, layer mask
 * and one-time warnings). Host only: no game code and no ROM. The test maps
 * I/O, PLTT and VRAM at their GBA addresses, as port/hw/memory.c does, writes
 * synthetic values there and checks the pixels RenderBgLine produces.
 *
 * Build and run from the repository root (WSL):
 *   gcc -m32 -std=gnu11 -O1 -g -Wall -Werror -fno-pie -no-pie -funsigned-char \
 *       -fno-strict-aliasing -fwrapv -DPC_PORT=1 -DRENDER_BG_TEST \
 *       -Iinclude -Iport/include port/render/test_bg.c port/render/bg_line.c \
 *       port/render/bg_affine.c -o /tmp/test_bg && /tmp/test_bg
 *
 * Everything below is inside RENDER_BG_TEST, so a wildcard over port/render in
 * the game build compiles this file to nothing (it defines main and PortLog).
 */

#ifdef RENDER_BG_TEST

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "port_hw.h"
#include "bg_line.h"

/* ---- PortLog stub: counts calls so "logged once" can be checked ---- */

static int sPortLogCalls;
static char sPortLogLast[512];
static int sVerbose;

void PortLog(const char *fmt, ...)
{
    va_list ap;

    sPortLogCalls++;
    va_start(ap, fmt);
    vsnprintf(sPortLogLast, sizeof(sPortLogLast), fmt, ap);
    va_end(ap);
    if (sVerbose)
        fputs(sPortLogLast, stdout);
}

/* ---- check macros ---- */

static int sCaseFails;
static unsigned sChecks;

#define EXPECT_EQ(a, b)                                                                            \
    do                                                                                             \
    {                                                                                              \
        long long ea_ = (long long)(a), eb_ = (long long)(b);                                      \
        sChecks++;                                                                                 \
        if (ea_ != eb_ && sCaseFails++ < 8)                                                        \
            printf("    %s:%d: %s == %s: got 0x%llX, want 0x%llX\n", __FILE__, __LINE__, #a, #b,   \
                (unsigned long long)ea_, (unsigned long long)eb_);                                 \
    } while (0)

#define EXPECT_TRUE(c) EXPECT_EQ(!!(c), 1)

/* ---- emulated memory ---- */

#define T RENDER_TRANSPARENT

static struct RenderBgLine sOut;

static int MapRegion(u32 addr, u32 size)
{
    void *want = (void *)(uintptr_t)addr;
    void *got = mmap(want, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
        -1, 0);

    if (got == want)
        return 0;
    if (got != MAP_FAILED)
        munmap(got, size);
    printf("cannot map 0x%08X +0x%X\n", addr, size);
    return -1;
}

static u8 *Vram8(void)
{
    return (u8 *)(uintptr_t)VRAM;
}

static u16 *Pltt(void)
{
    return (u16 *)(uintptr_t)PLTT;
}

static void ResetHw(void)
{
    memset((void *)(uintptr_t)REG_BASE, 0, 0x1000);
    memset((void *)(uintptr_t)PLTT, 0, 0x1000);
    memset((void *)(uintptr_t)VRAM, 0, 0x18000);
    RenderBgInit();
    sPortLogCalls = 0;
    sPortLogLast[0] = '\0';
}

static void PutPixel4(u32 charBase, int tile, int px, int py, int index)
{
    u8 *p = Vram8() + charBase + tile * 32 + py * 4 + px / 2;

    if (px & 1)
        *p = (u8)((*p & 0x0F) | (index << 4));
    else
        *p = (u8)((*p & 0xF0) | (index & 0xF));
}

static void FillTile4(u32 charBase, int tile, int index)
{
    memset(Vram8() + charBase + tile * 32, index * 0x11, 32);
}

static void PutPixel8(u32 charBase, int tile, int px, int py, int index)
{
    Vram8()[charBase + tile * 64 + py * 8 + px] = (u8)index;
}

static void FillTile8(u32 charBase, int tile, int index)
{
    memset(Vram8() + charBase + tile * 64, index, 64);
}

/* Text map entry (tx, ty) of screen block `block` (0x800 bytes each). */
static void SetText(u32 mapBase, int block, int tx, int ty, u16 entry)
{
    *(u16 *)(Vram8() + mapBase + block * 0x800 + (ty * 32 + tx) * 2) = entry;
}

static void DrawLine(int line)
{
    struct RenderLineRegs regs;

    RenderSnapshotRegs(&regs);
    RenderBgLine(line, &regs, &sOut);
}

static int FloorDiv(int a, int b)
{
    int q = a / b;

    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

/* ---- text BG tests ---- */

/* U1: 4bpp byte 0x21 gives left pixel index 1, right pixel index 2. */
static void TestU1NibbleOrder(void)
{
    int x;

    ResetHw();
    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON;
    REG_BG0CNT = BGCNT_CHARBASE(0) | BGCNT_SCREENBASE(31);
    Pltt()[1] = 0x001F;
    Pltt()[2] = 0x03E0;
    Vram8()[32] = 0x21; /* tile 1, row 0, pixels 0 and 1 */
    SetText(0xF800, 0, 0, 0, 1);

    RenderBgBeginFrame();
    DrawLine(0);
    EXPECT_EQ(sOut.on[0], 1);
    EXPECT_EQ(sOut.color[0][0], 0x001F);
    EXPECT_EQ(sOut.color[0][1], 0x03E0);
    for (x = 2; x < 8; x++)
        EXPECT_EQ(sOut.color[0][x], T);
    DrawLine(1);
    EXPECT_EQ(sOut.color[0][0], T);
}

/* U2: index 0 is transparent; the backdrop is PLTT[0] masked to 15 bits. */
static void TestU2TransparentAndBackdrop(void)
{
    int x;

    ResetHw();
    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON;
    REG_BG0CNT = BGCNT_SCREENBASE(31);
    Pltt()[0] = 0xFC1F;
    Pltt()[5] = 0x1234;
    PutPixel4(0, 1, 3, 0, 5);
    SetText(0xF800, 0, 0, 0, 1);

    RenderBgBeginFrame();
    DrawLine(0);
    EXPECT_EQ(sOut.backdrop, 0x7C1F);
    for (x = 0; x < 3; x++)
        EXPECT_EQ(sOut.color[0][x], T);
    EXPECT_EQ(sOut.color[0][3], 0x1234);
    EXPECT_EQ(sOut.color[0][4], T);
    /* BGs that are off give an all-transparent row. */
    EXPECT_EQ(sOut.on[1], 0);
    for (x = 0; x < RENDER_WIDTH; x++)
        EXPECT_EQ(sOut.color[1][x], T);
}

/* U3: map entry bits 12-15 pick the 16-colour bank; colours lose bit 15. */
static void TestU3PaletteBank(void)
{
    int x;

    ResetHw();
    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG1_ON;
    REG_BG1CNT = BGCNT_CHARBASE(1) | BGCNT_SCREENBASE(30);
    FillTile4(0x4000, 1, 3);
    SetText(0xF000, 0, 0, 0, 1 | (5 << 12));
    SetText(0xF000, 0, 1, 0, 1 | (15 << 12));
    SetText(0xF000, 0, 2, 0, 1);
    Pltt()[5 * 16 + 3] = 0x8123;
    Pltt()[15 * 16 + 3] = 0x7FFF;
    Pltt()[3] = 0x0042;

    RenderBgBeginFrame();
    DrawLine(4);
    for (x = 0; x < 8; x++)
    {
        EXPECT_EQ(sOut.color[1][x], 0x0123);
        EXPECT_EQ(sOut.color[1][8 + x], 0x7FFF);
        EXPECT_EQ(sOut.color[1][16 + x], 0x0042);
    }
}

/* U4: HFLIP (bit 10), VFLIP (bit 11) and both mirror columns and rows. */
static int FlipTestIndex(int px, int py)
{
    return ((px * 3 + py * 5) % 15) + 1;
}

static void TestU4Flips(void)
{
    int px, py, f, line, x;

    ResetHw();
    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG2_ON;
    REG_BG2CNT = BGCNT_SCREENBASE(31);
    for (py = 0; py < 8; py++)
        for (px = 0; px < 8; px++)
            PutPixel4(0, 1, px, py, FlipTestIndex(px, py));
    for (f = 1; f < 16; f++)
        Pltt()[f] = (u16)(f * 0x0421);
    for (f = 0; f < 4; f++)
        SetText(0xF800, 0, f, 0, (u16)(1 | (f << 10))); /* none, H, V, H+V */

    RenderBgBeginFrame();
    for (line = 0; line < 8; line++)
    {
        DrawLine(line);
        for (x = 0; x < 32; x++)
        {
            int flip = x >> 3;
            int sx = (flip & 1) ? 7 - (x & 7) : (x & 7);
            int sy = (flip & 2) ? 7 - line : line;

            EXPECT_EQ(sOut.color[2][x], FlipTestIndex(sx, sy) * 0x0421);
        }
    }
}

/* Marker tiles for the scroll tests: tile 1 shows its column, tile 2 its row.
   Colour of index i is 0x100 | i. */
static void SetupScrollMarkers(int bg, int size)
{
    int i;

    REG_DISPCNT = DISPCNT_MODE_0 | (DISPCNT_BG0_ON << bg);
    *(vu16 *)(uintptr_t)(REG_BASE + REG_OFFSET_BG0CNT + 2 * bg) =
        (u16)(BGCNT_CHARBASE(0) | BGCNT_SCREENBASE(28) | (size << 14));
    for (i = 0; i < 64; i++)
    {
        PutPixel4(0, 1, i & 7, i >> 3, (i & 7) + 1);
        PutPixel4(0, 2, i & 7, i >> 3, (i >> 3) + 1);
    }
    for (i = 1; i < 16; i++)
        Pltt()[i] = (u16)(0x100 | i);
}

static void SetScroll(int bg, u16 hofs, u16 vofs)
{
    *(vu16 *)(uintptr_t)(REG_BASE + REG_OFFSET_BG0HOFS + 4 * bg) = hofs;
    *(vu16 *)(uintptr_t)(REG_BASE + REG_OFFSET_BG0HOFS + 4 * bg + 2) = vofs;
}

/* U5: HOFS 0x1FF and 0xFFFF both mean -1; VOFS 0xFFFF likewise; text maps
   wrap at 256 or 512. */
static void TestU5ScrollWrap(void)
{
    int x;

    /* 256 wide, HOFS 0x1FF and 0xFFFF: x=0 shows column 255. */
    ResetHw();
    SetupScrollMarkers(0, 0);
    SetText(0xE000, 0, 31, 0, 1);
    SetScroll(0, 0x1FF, 0);
    RenderBgBeginFrame();
    DrawLine(0);
    EXPECT_EQ(sOut.color[0][0], 0x108);
    EXPECT_EQ(sOut.color[0][1], T);
    SetScroll(0, 0xFFFF, 0);
    DrawLine(0);
    EXPECT_EQ(sOut.color[0][0], 0x108);
    /* Bit 8 of HOFS does nothing on a 256-wide map. */
    SetScroll(0, 0x0FF, 0);
    DrawLine(0);
    EXPECT_EQ(sOut.color[0][0], 0x108);

    /* 256 wide, HOFS 20: the right edge wraps back to column 0. */
    ResetHw();
    SetupScrollMarkers(0, 0);
    SetText(0xE000, 0, 0, 0, 1);
    SetScroll(0, 20, 0);
    RenderBgBeginFrame();
    DrawLine(0);
    EXPECT_EQ(sOut.color[0][235], T);
    for (x = 236; x < 240; x++)
        EXPECT_EQ(sOut.color[0][x], 0x100 | (x - 236 + 1));

    /* 512 wide (size 1), HOFS 0x1FF: x=0 shows column 511, in block 1. */
    ResetHw();
    SetupScrollMarkers(3, 1);
    SetText(0xE000, 1, 31, 0, 1);
    SetText(0xE000, 1, 0, 0, 2);
    SetScroll(3, 0x1FF, 0);
    RenderBgBeginFrame();
    DrawLine(0);
    EXPECT_EQ(sOut.color[3][0], 0x108);
    EXPECT_EQ(sOut.color[3][1], T);
    /* HOFS 0xFF: column 255 is block 0 (empty), column 256 is block 1. */
    SetScroll(3, 0x0FF, 0);
    DrawLine(0);
    EXPECT_EQ(sOut.color[3][0], T);
    EXPECT_EQ(sOut.color[3][1], 0x101);

    /* 256 high, VOFS 0xFFFF: line 0 shows row 255, line 1 shows row 0. */
    ResetHw();
    SetupScrollMarkers(1, 0);
    SetText(0xE000, 0, 0, 31, 2);
    SetScroll(1, 0, 0xFFFF);
    RenderBgBeginFrame();
    DrawLine(0);
    EXPECT_EQ(sOut.color[1][0], 0x108);
    DrawLine(1);
    EXPECT_EQ(sOut.color[1][0], T);

    /* 512 high (size 2), VOFS 0xFFFF: line 0 shows row 511, in block 1. */
    ResetHw();
    SetupScrollMarkers(2, 2);
    SetText(0xE000, 0, 0, 31, 1); /* row 255 of block 0: must not be used */
    SetText(0xE000, 1, 0, 31, 2);
    SetScroll(2, 0, 0xFFFF);
    RenderBgBeginFrame();
    DrawLine(0);
    EXPECT_EQ(sOut.color[2][0], 0x108);
    DrawLine(1);
    EXPECT_EQ(sOut.color[2][0], T);
}

/* U6: screen blocks for sizes 0-3 (design 5.3 step 3). Each block gets its own
   palette bank at tile (0, 0) and (5, 7). The expected block uses the GBATEK
   picture: 512x256 is "0 1", 256x512 is "0 over 1", 512x512 is "0 1 over 2 3". */
static void TestU6ScreenBlocks(void)
{
    static const u16 sOffsets[4][2] = { { 0, 0 }, { 256, 0 }, { 0, 256 }, { 256, 256 } };
    int size, v, b;

    for (size = 0; size < 4; size++)
    {
        ResetHw();
        REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON;
        REG_BG0CNT = (u16)(BGCNT_SCREENBASE(28) | (size << 14));
        FillTile4(0, 3, 1);
        for (b = 0; b < 4; b++)
        {
            SetText(0xE000, b, 0, 0, (u16)(3 | ((b + 1) << 12)));
            SetText(0xE000, b, 5, 7, (u16)(3 | ((b + 5) << 12)));
            Pltt()[(b + 1) * 16 + 1] = (u16)(0x1000 + b);
            Pltt()[(b + 5) * 16 + 1] = (u16)(0x2000 + b);
        }
        for (v = 0; v < 4; v++)
        {
            int hofs = sOffsets[v][0];
            int vofs = sOffsets[v][1];
            int right = (size & 1) && hofs >= 256;
            int lower = (size & 2) && vofs >= 256;
            int block = (right ? 1 : 0) + (lower ? ((size & 1) ? 2 : 1) : 0);

            SetScroll(0, (u16)hofs, (u16)vofs);
            RenderBgBeginFrame();
            DrawLine(0);
            EXPECT_EQ(sOut.color[0][0], 0x1000 + block);
            DrawLine(56);
            EXPECT_EQ(sOut.color[0][40], 0x2000 + block);
        }
    }
}

/* U7: char base 0xC000 with tile 512 or more reads past BG VRAM: transparent,
   logged once. Tile 511 (0xFFE0) is still drawn. */
static void TestU7TilePastVram(void)
{
    int x;

    ResetHw();
    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON;
    REG_BG0CNT = BGCNT_CHARBASE(3) | BGCNT_SCREENBASE(0);
    FillTile4(0xC000, 511, 2);
    FillTile4(0xC000, 600, 4); /* 0x10B00, in OBJ VRAM: must not be read */
    FillTile4(0xC000, 512, 4); /* exactly 0x10000 */
    SetText(0x0000, 0, 0, 0, 511);
    SetText(0x0000, 0, 1, 0, 600);
    SetText(0x0000, 0, 2, 0, 512);
    Pltt()[2] = 0x22;
    Pltt()[4] = 0x44;

    RenderBgBeginFrame();
    DrawLine(0);
    DrawLine(1);
    for (x = 0; x < 8; x++)
    {
        EXPECT_EQ(sOut.color[0][x], 0x22);
        EXPECT_EQ(sOut.color[0][8 + x], T);
        EXPECT_EQ(sOut.color[0][16 + x], T);
    }
    EXPECT_EQ(sPortLogCalls, 1);
    EXPECT_TRUE(strstr(sPortLogLast, "past BG VRAM") != NULL);
    EXPECT_EQ(RenderLogHits(RENDER_LOG_TILE_PAST_VRAM), 32);
}

/* U8: the renderer shows VRAM, not the shadow tilemap. */
static void TestU8VramNotShadow(void)
{
    static u16 sShadow[0x400];
    int x;

    ResetHw();
    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON;
    REG_BG0CNT = BGCNT_SCREENBASE(31);
    FillTile4(0, 1, 1);
    FillTile4(0, 2, 2);
    Pltt()[1] = 0x0011;
    Pltt()[2] = 0x0022;
    for (x = 0; x < 0x400; x++)
        sShadow[x] = 1; /* what the game's buffer would hold before a flush */
    SetText(0xF800, 0, 0, 0, 2);

    RenderBgBeginFrame();
    DrawLine(0);
    EXPECT_EQ(sShadow[0], 1);
    EXPECT_EQ(sOut.color[0][0], 0x0022);
}

/* U15 and per-line registers: PLTT, VRAM, BGnCNT and priority changes between
   two lines show on the next line (no caching). */
static void TestU15NoCaching(void)
{
    ResetHw();
    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG2_ON;
    REG_BG2CNT = BGCNT_PRIORITY(1) | BGCNT_SCREENBASE(30);
    FillTile4(0, 1, 1);
    FillTile4(0, 2, 2);
    SetText(0xF000, 0, 0, 0, 1);
    SetText(0xE800, 0, 0, 0, 2);
    Pltt()[0] = 0x0005;
    Pltt()[1] = 0x001F;
    Pltt()[2] = 0x03E0;

    RenderBgBeginFrame();
    DrawLine(0);
    EXPECT_EQ(sOut.color[2][0], 0x001F);
    EXPECT_EQ(sOut.backdrop, 0x0005);
    EXPECT_EQ(sOut.prio[2], 1);

    /* PLTT changes (mapanim_scanline.c:109-110, bmio.c:556-558). */
    Pltt()[1] = 0x7C00;
    Pltt()[0] = 0x0006;
    DrawLine(1);
    EXPECT_EQ(sOut.color[2][0], 0x7C00);
    EXPECT_EQ(sOut.backdrop, 0x0006);

    /* A glyph written straight to VRAM (fontgrp.c:703-708). */
    PutPixel4(0, 1, 0, 2, 2);
    DrawLine(2);
    EXPECT_EQ(sOut.color[2][0], 0x03E0);
    EXPECT_EQ(sOut.color[2][1], 0x7C00);

    /* Map base 0xF000 -> 0xE800 and priority 1 -> 3 between lines
       (opanim-main.c:805-821, opinfo.c:960-976). */
    REG_BG2CNT = BGCNT_PRIORITY(3) | BGCNT_SCREENBASE(29);
    DrawLine(3);
    EXPECT_EQ(sOut.color[2][0], 0x03E0);
    EXPECT_EQ(sOut.prio[2], 3);
}

/* Mode table (design 5.1), on/off from DISPCNT and priority from BGnCNT. */
static void TestModeTableAndPriority(void)
{
    int n;

    ResetHw();
    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON | DISPCNT_BG2_ON;
    REG_BG0CNT = BGCNT_PRIORITY(3);
    REG_BG1CNT = BGCNT_PRIORITY(2);
    REG_BG2CNT = BGCNT_PRIORITY(1);
    REG_BG3CNT = BGCNT_PRIORITY(0);
    RenderBgBeginFrame();
    DrawLine(0);
    EXPECT_EQ(sOut.on[0], 1);
    EXPECT_EQ(sOut.on[1], 0);
    EXPECT_EQ(sOut.on[2], 1);
    EXPECT_EQ(sOut.on[3], 0);
    for (n = 0; n < 4; n++)
        EXPECT_EQ(sOut.prio[n], 3 - n);
    EXPECT_EQ(sPortLogCalls, 0);

    /* Modes 2-7: no BGs drawn, one warning. */
    ResetHw();
    REG_DISPCNT = DISPCNT_MODE_3 | DISPCNT_BG_ALL_ON;
    FillTile4(0, 0, 1);
    Pltt()[1] = 0x1111;
    RenderBgBeginFrame();
    DrawLine(0);
    DrawLine(1);
    for (n = 0; n < 4; n++)
    {
        EXPECT_EQ(sOut.on[n], 0);
        EXPECT_EQ(sOut.color[n][0], T);
    }
    EXPECT_EQ(sPortLogCalls, 1);
    EXPECT_EQ(RenderLogHits(RENDER_LOG_MODE_UNUSED), 2);
}

/* U14: mode 1 never draws BG3, even with DISPCNT bit 11 set; mode 0 does. */
static void TestU14Mode1Bg3Off(void)
{
    int x;

    ResetHw();
    REG_DISPCNT = DISPCNT_MODE_1 | DISPCNT_BG_ALL_ON;
    REG_BG3CNT = BGCNT_PRIORITY(2) | BGCNT_SCREENBASE(31);
    FillTile4(0, 1, 1);
    SetText(0xF800, 0, 0, 0, 1);
    Pltt()[1] = 0x0101;

    RenderBgBeginFrame();
    DrawLine(0);
    EXPECT_EQ(sOut.on[0], 1);
    EXPECT_EQ(sOut.on[1], 1);
    EXPECT_EQ(sOut.on[2], 1);
    EXPECT_EQ(sOut.on[3], 0);
    EXPECT_EQ(sOut.prio[3], 2);
    for (x = 0; x < RENDER_WIDTH; x++)
        EXPECT_EQ(sOut.color[3][x], T);

    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG3_ON;
    RenderBgBeginFrame();
    DrawLine(0);
    EXPECT_EQ(sOut.on[3], 1);
    EXPECT_EQ(sOut.color[3][0], 0x0101);
}

/* 8bpp text BG (BGnCNT bit 7): 64-byte tiles, PLTT[index], no bank; logged once. */
static void TestText8bpp(void)
{
    int px, py;

    ResetHw();
    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON;
    REG_BG0CNT = BGCNT_CHARBASE(1) | BGCNT_256COLOR | BGCNT_SCREENBASE(31);
    for (py = 0; py < 8; py++)
        for (px = 0; px < 8; px++)
            PutPixel8(0x4000, 2, px, py, 0x40 + py * 8 + px);
    for (px = 0; px < 256; px++)
        Pltt()[px] = (u16)(px * 3);
    SetText(0xF800, 0, 0, 0, 2 | (7 << 12)); /* bank bits are ignored */
    SetText(0xF800, 0, 1, 0, 2 | 0x400);

    RenderBgBeginFrame();
    DrawLine(3);
    for (px = 0; px < 8; px++)
    {
        EXPECT_EQ(sOut.color[0][px], (0x40 + 3 * 8 + px) * 3);
        EXPECT_EQ(sOut.color[0][8 + px], (0x40 + 3 * 8 + (7 - px)) * 3);
    }
    DrawLine(4);
    EXPECT_EQ(sPortLogCalls, 1);
    EXPECT_EQ(RenderLogHits(RENDER_LOG_TEXT_8BPP), 2);
}

/* ---- affine BG2 tests ---- */

/* Affine ramp map: cell (tx, ty) holds tile tx, and tile t's pixel (px, py) is
   index t * 8 + px, so a pixel's colour index is its texture x (index 0, at
   x = 0, is transparent). Colour of index i is 0x4000 | i. */
#define AFF_CHAR 0x8000u /* char base 2 */
#define AFF_MAP  0x2000u /* screen base 4 */

static void SetupAffineRampX(int size, int wrap)
{
    int dim = 128 << size;
    int tx, ty, px, py;

    REG_DISPCNT = DISPCNT_MODE_1 | DISPCNT_BG2_ON;
    REG_BG2CNT = (u16)(BGCNT_CHARBASE(2) | BGCNT_SCREENBASE(4) | (size << 14) | (wrap ? BGCNT_WRAP : 0));
    for (ty = 0; ty < dim / 8; ty++)
        for (tx = 0; tx < dim / 8; tx++)
            Vram8()[AFF_MAP + ty * (dim / 8) + tx] = (u8)tx;
    for (tx = 0; tx < 32; tx++)
        for (py = 0; py < 8; py++)
            for (px = 0; px < 8; px++)
                PutPixel8(AFF_CHAR, tx, px, py, tx * 8 + px);
    for (px = 1; px < 256; px++)
        Pltt()[px] = (u16)(0x4000 | px);
    REG_BG2PA = 0x100;
    REG_BG2PB = 0;
    REG_BG2PC = 0;
    REG_BG2PD = 0x100;
}

static u16 RampColor(int texX)
{
    return texX == 0 ? T : (u16)(0x4000 | texX);
}

/* U9: identity matrix, reference 0: pixels equal the byte map and 8bpp tiles
   laid out flat; outside the 128x128 map is transparent (no wrap). */
static int U9Cell(int tx, int ty)
{
    return ((tx + 3 * ty) % 7) + 1;
}

static int U9Pixel(int tile, int px, int py)
{
    return (tile * 37 + py * 8 + px) & 0xFF;
}

static void TestU9AffineIdentity(void)
{
    int i, t, px, py, line, x;

    ResetHw();
    REG_DISPCNT = DISPCNT_MODE_1 | DISPCNT_BG2_ON;
    REG_BG2CNT = BGCNT_CHARBASE(2) | BGCNT_SCREENBASE(4) | BGCNT_AFF128x128;
    REG_BG2PA = 0x100;
    REG_BG2PD = 0x100;
    for (i = 0; i < 16 * 16; i++)
        Vram8()[AFF_MAP + i] = (u8)U9Cell(i & 15, i >> 4);
    for (t = 1; t <= 7; t++)
        for (py = 0; py < 8; py++)
            for (px = 0; px < 8; px++)
                PutPixel8(AFF_CHAR, t, px, py, U9Pixel(t, px, py));
    for (i = 0; i < 256; i++)
        Pltt()[i] = (u16)((i * 0x81) & 0x7FFF);

    RenderBgBeginFrame();
    for (line = 0; line < RENDER_HEIGHT; line++)
    {
        DrawLine(line);
        for (x = 0; x < RENDER_WIDTH; x++)
        {
            u16 want = T;

            if (x < 128 && line < 128)
            {
                int index = U9Pixel(U9Cell(x >> 3, line >> 3), x & 7, line & 7);

                want = index != 0 ? Pltt()[index] : T;
            }
            EXPECT_EQ(sOut.color[2][x], want);
        }
    }
}

/* U10: PA = PD = 0x80 is a 2x zoom. Reference (-16.0, -8.0): negative texture
   coordinates are transparent without wrap and wrap at 128 with it. */
static void TestU10AffineZoom(void)
{
    int wrap, line, x;

    for (wrap = 0; wrap < 2; wrap++)
    {
        ResetHw();
        SetupAffineRampX(0, wrap);
        REG_BG2PA = 0x80;
        REG_BG2PD = 0x80;
        REG_BG2X = 0x0FFFF000; /* -16.0 */
        REG_BG2Y = 0x0FFFF800; /* -8.0 */
        RenderBgBeginFrame();
        for (line = 0; line < 40; line++)
        {
            int ty = FloorDiv(line - 16, 2); /* texture y on this line */

            DrawLine(line);
            for (x = 0; x < RENDER_WIDTH; x++)
            {
                int tx = FloorDiv(x - 32, 2);
                u16 want;

                if (wrap)
                    want = RampColor(tx & 127);
                else if (tx < 0 || tx >= 128 || ty < 0 || ty >= 128)
                    want = T;
                else
                    want = RampColor(tx);
                EXPECT_EQ(sOut.color[2][x], want);
            }
        }
    }
}

/* U11: BG2X/BG2Y are 28-bit signed. 0x0FFFFF00 is -1.0. */
static void TestU11Sext28(void)
{
    s32 rx, ry;

    EXPECT_EQ(RenderSext28(0x0FFFFF00), -256);
    EXPECT_EQ(RenderSext28(0x08000000), -134217728);
    EXPECT_EQ(RenderSext28(0x07FFFFFF), 134217727);
    EXPECT_EQ(RenderSext28(0xF0000100), 256);
    EXPECT_EQ(RenderSext28(0xFFFFFFFF), -1);
    EXPECT_EQ(RenderSext28(0), 0);

    ResetHw();
    SetupAffineRampX(0, 1);
    REG_BG2X = 0x0FFFFF00;
    REG_BG2Y = 0x00000280;
    RenderBgBeginFrame();
    RenderAffineGetRef(&rx, &ry);
    EXPECT_EQ(rx, -256);
    EXPECT_EQ(ry, 640);
    DrawLine(0);
    EXPECT_EQ(sOut.color[2][0], RampColor(127)); /* texture x -1 wraps to 127 */
    EXPECT_EQ(sOut.color[2][2], RampColor(1));

    ResetHw();
    SetupAffineRampX(0, 0);
    REG_BG2X = 0x0FFFFF00;
    RenderBgBeginFrame();
    DrawLine(0);
    EXPECT_EQ(sOut.color[2][0], T); /* texture x -1, no wrap */
    EXPECT_EQ(sOut.color[2][2], RampColor(1));
}

/* U12: PB = 0x10, PD = 0x100. Line L starts at ref + L * (PB, PD). A change to
   BG2X before line 80 is latched again, with no step on that line. */
static void TestU12AffineStepAndReload(void)
{
    s32 rx, ry;
    int line;

    ResetHw();
    SetupAffineRampX(1, 1);
    REG_BG2PB = 0x10;
    REG_BG2PD = 0x100;
    REG_BG2X = 0xA00; /* 10.0 */
    REG_BG2Y = 0;
    RenderBgBeginFrame();
    for (line = 0; line < RENDER_HEIGHT; line++)
    {
        int startX, wantX;

        if (line == 80)
            REG_BG2X = 0x6400; /* 100.0, as an HBlank handler at line 79 would */
        startX = line < 80 ? 0xA00 + line * 0x10 : 0x6400 + (line - 80) * 0x10;
        DrawLine(line);
        EXPECT_EQ(sOut.color[2][0], RampColor(startX >> 8));
        EXPECT_EQ(sOut.color[2][5], RampColor((startX >> 8) + 5));
        RenderAffineGetRef(&rx, &ry);
        wantX = startX + 0x10;
        EXPECT_EQ(rx, wantX);
        EXPECT_EQ(ry, (line + 1) * 0x100);
    }
    EXPECT_EQ(RenderLogHits(RENDER_LOG_AFFINE_RELOAD), 1);
    EXPECT_EQ(sPortLogCalls, 1);

    /* Next frame: the line-0 latch takes the live value, unstepped. */
    RenderBgBeginFrame();
    RenderAffineGetRef(&rx, &ry);
    EXPECT_EQ(rx, 0x6400);
    EXPECT_EQ(ry, 0);
    DrawLine(0);
    EXPECT_EQ(sOut.color[2][0], RampColor(100));

    /* A Y change reloads Y only; X keeps its stepped value. */
    REG_BG2Y = 0x3000;
    DrawLine(1);
    RenderAffineGetRef(&rx, &ry);
    EXPECT_EQ(rx, 0x6400 + 2 * 0x10);
    EXPECT_EQ(ry, 0x3000 + 0x100);
}

/* The step runs only when the mode is not 0 and DISPCNT bit 10 is set (mGBA). */
static void TestAffineStepConditions(void)
{
    static const u16 sDispcnt[4] = {
        DISPCNT_MODE_0 | DISPCNT_BG2_ON,
        DISPCNT_MODE_1 | DISPCNT_BG0_ON,
        DISPCNT_MODE_1 | DISPCNT_BG2_ON,
        DISPCNT_MODE_2 | DISPCNT_BG2_ON,
    };
    static const int sSteps[4] = { 0, 0, 1, 1 };
    s32 rx, ry;
    int i, line;

    for (i = 0; i < 4; i++)
    {
        ResetHw();
        SetupAffineRampX(0, 0);
        REG_DISPCNT = sDispcnt[i];
        REG_BG2PB = 0x20;
        REG_BG2PD = 0x40;
        REG_BG2X = 0x300;
        REG_BG2Y = 0x500;
        RenderBgBeginFrame();
        for (line = 0; line < 10; line++)
            DrawLine(line);
        RenderAffineGetRef(&rx, &ry);
        EXPECT_EQ(rx, 0x300 + sSteps[i] * 10 * 0x20);
        EXPECT_EQ(ry, 0x500 + sSteps[i] * 10 * 0x40);
    }
}

/* U13: a u16 0x0201 copied as bytes from the shadow is affine cells 1 and 2. */
static void TestU13ShadowBytes(void)
{
    int x;

    ResetHw();
    REG_DISPCNT = DISPCNT_MODE_1 | DISPCNT_BG2_ON;
    REG_BG2CNT = BGCNT_CHARBASE(2) | BGCNT_SCREENBASE(4) | BGCNT_AFF128x128;
    REG_BG2PA = 0x100;
    REG_BG2PD = 0x100;
    *(u16 *)(Vram8() + AFF_MAP) = 0x0201;
    FillTile8(AFF_CHAR, 1, 11);
    FillTile8(AFF_CHAR, 2, 22);
    Pltt()[11] = 0x0B0B;
    Pltt()[22] = 0x1616;

    RenderBgBeginFrame();
    DrawLine(0);
    for (x = 0; x < 8; x++)
    {
        EXPECT_EQ(sOut.color[2][x], 0x0B0B);
        EXPECT_EQ(sOut.color[2][8 + x], 0x1616);
    }
    EXPECT_EQ(sOut.color[2][16], T);
}

/* Affine grid map: the colour index of texture pixel (X, Y) is
   (X & 15) | ((Y & 15) << 4), built from four tiles chosen by the parity of
   the cell. Index 0 (X and Y both multiples of 16) is transparent. */
static void SetupAffineGrid(int size, int wrap)
{
    int dim = 128 << size;
    int tx, ty, t, px, py;

    SetupAffineRampX(size, wrap);
    for (ty = 0; ty < dim / 8; ty++)
        for (tx = 0; tx < dim / 8; tx++)
            Vram8()[AFF_MAP + ty * (dim / 8) + tx] = (u8)((tx & 1) | ((ty & 1) << 1));
    for (t = 0; t < 4; t++)
        for (py = 0; py < 8; py++)
            for (px = 0; px < 8; px++)
                PutPixel8(AFF_CHAR, t, px, py, (((t & 1) * 8 + px) & 15) | ((((t >> 1) * 8 + py) & 15) << 4));
}

static u16 GridColor(s32 texX, s32 texY)
{
    int index = (texX & 15) | ((texY & 15) << 4);

    return index == 0 ? T : (u16)(0x4000 | index);
}

/* Rotation (PB and PC non-zero), with and without wrap on a 256x256 map,
   checked against the direct formula ref + L * (PB, PD) + x * (PA, PC). */
static void TestAffineRotationFormula(void)
{
    s32 pa = 0xEC, pb = -0x62, pc = 0x62, pd = 0xEC; /* about 22.5 degrees, scale 1 */
    s32 x0 = -0x1234, y0 = 0x5678;
    int wrap, line, x;

    for (wrap = 0; wrap < 2; wrap++)
    {
        ResetHw();
        SetupAffineGrid(1, wrap);
        REG_BG2PA = (u16)pa;
        REG_BG2PB = (u16)pb;
        REG_BG2PC = (u16)pc;
        REG_BG2PD = (u16)pd;
        REG_BG2X = (u32)x0 & 0x0FFFFFFF;
        REG_BG2Y = (u32)y0 & 0x0FFFFFFF;
        RenderBgBeginFrame();
        for (line = 0; line < RENDER_HEIGHT; line++)
        {
            DrawLine(line);
            for (x = 0; x < RENDER_WIDTH; x++)
            {
                s32 texX = (x0 + line * pb + x * pa) >> 8;
                s32 texY = (y0 + line * pd + x * pc) >> 8;
                u16 want;

                if (!wrap && (texX < 0 || texX >= 256 || texY < 0 || texY >= 256))
                    want = T;
                else
                    want = GridColor(texX, texY);
                EXPECT_EQ(sOut.color[2][x], want);
            }
        }
    }
}

/* ---- snapshot, layer mask and warnings ---- */

static void TestSnapshot(void)
{
    struct RenderLineRegs r;
    int off, n;

    ResetHw();
    for (off = 0; off < 0x56; off += 2)
        *(vu16 *)(uintptr_t)(REG_BASE + off) = (u16)(0x1000 + off);
    RenderSnapshotRegs(&r);
    EXPECT_EQ(r.dispcnt, 0x1000);
    for (n = 0; n < 4; n++)
    {
        EXPECT_EQ(r.bgcnt[n], 0x1008 + 2 * n);
        EXPECT_EQ(r.bghofs[n], 0x1010 + 4 * n);
        EXPECT_EQ(r.bgvofs[n], 0x1012 + 4 * n);
    }
    EXPECT_EQ(r.bg2pa, 0x1020);
    EXPECT_EQ(r.bg2pb, 0x1022);
    EXPECT_EQ(r.bg2pc, 0x1024);
    EXPECT_EQ(r.bg2pd, 0x1026);
    EXPECT_EQ(r.bg2x, 0x102A1028);
    EXPECT_EQ(r.bg2y, 0x102E102C);
    EXPECT_EQ(r.win0h, 0x1040);
    EXPECT_EQ(r.win1h, 0x1042);
    EXPECT_EQ(r.win0v, 0x1044);
    EXPECT_EQ(r.win1v, 0x1046);
    EXPECT_EQ(r.winin, 0x1048);
    EXPECT_EQ(r.winout, 0x104A);
    EXPECT_EQ(r.mosaic, 0x104C);
    EXPECT_EQ(r.bldcnt, 0x1050);
    EXPECT_EQ(r.bldalpha, 0x1052);
    EXPECT_EQ(r.bldy, 0x1054);
}

static void TestLayerMask(void)
{
    ResetHw();
    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON | DISPCNT_BG1_ON;
    REG_BG0CNT = BGCNT_SCREENBASE(31);
    REG_BG1CNT = BGCNT_SCREENBASE(31);
    FillTile4(0, 1, 1);
    SetText(0xF800, 0, 0, 0, 1);
    Pltt()[1] = 0x0077;

    RenderSetLayerMask(0x1);
    RenderBgBeginFrame();
    DrawLine(0);
    EXPECT_EQ(sOut.on[0], 1);
    EXPECT_EQ(sOut.on[1], 0);
    EXPECT_EQ(sOut.color[0][0], 0x0077);
    EXPECT_EQ(sOut.color[1][0], T);

    setenv("FE8PC_RENDER_LAYERS", "0x2", 1);
    ResetHw();
    EXPECT_EQ(RenderGetLayerMask(), 0x2);
    unsetenv("FE8PC_RENDER_LAYERS");
    RenderBgInit();
    EXPECT_EQ(RenderGetLayerMask(), 0xF);
}

static void TestWarnings(void)
{
    /* MOSAIC non-zero with a drawn BG's mosaic bit set: once. */
    ResetHw();
    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON;
    REG_BG0CNT = BGCNT_MOSAIC;
    RenderBgBeginFrame();
    DrawLine(0);
    EXPECT_EQ(sPortLogCalls, 0);
    REG_MOSAIC = 0x11;
    DrawLine(1);
    DrawLine(2);
    EXPECT_EQ(sPortLogCalls, 1);
    EXPECT_EQ(RenderLogHits(RENDER_LOG_MOSAIC), 2);

    /* DISPCNT change between lines 1 and 159: once. A new frame is quiet. */
    ResetHw();
    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON;
    RenderBgBeginFrame();
    DrawLine(0);
    DrawLine(1);
    EXPECT_EQ(sPortLogCalls, 0);
    REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON | DISPCNT_BG1_ON;
    DrawLine(2);
    DrawLine(3);
    EXPECT_EQ(sPortLogCalls, 1);
    EXPECT_EQ(RenderLogHits(RENDER_LOG_DISPCNT_CHANGE), 1);
    RenderBgBeginFrame();
    DrawLine(0);
    DrawLine(1);
    EXPECT_EQ(RenderLogHits(RENDER_LOG_DISPCNT_CHANGE), 1);
}

/* ---- runner ---- */

struct TestCase
{
    const char *name;
    void (*func)(void);
};

/*
 * mGBA BG clip (FE8PC_MGBA_BGCLIP, gRenderMgbaBgClip): the scene of the PPU
 * test ROM's variant 2 (port/test/oracle/pputest/pputest.c), whose pixels mGBA
 * 0.10.5 drew on 2026-10-21 (tools/pputest_check.py holds the same table). Tile
 * 1: column c has colour index c + 1; BG0 and BG1 are all tile 1 horizontally
 * flipped. WIN0 [130, 135) on lines 0-79 shows BG0 (HOFS 0), WIN1 [130, 135)
 * on lines 80-119 shows BG1 (HOFS 3). BG0's segment starts at column 2 of the
 * tile on screen 128-135 and ends before its last pixel, so mGBA shifts it by
 * one; BG1's segment reaches its first tile's end and is drawn right.
 */
static void TestMgbaBgClip(void)
{
    static const int bg0Hw[5] = { 6, 5, 4, 3, 2 };   /* x 130-134, hardware (the switch off) */
    static const int bg0Mgba[5] = { 5, 4, 3, 2, 1 }; /* mGBA */
    static const int bg1[5] = { 3, 2, 1, 8, 7 };     /* both */
    int i, x, pass;

    for (pass = 0; pass < 2; pass++)
    {
        ResetHw();
        gRenderMgbaBgClip = pass;
        REG_DISPCNT = DISPCNT_MODE_0 | DISPCNT_BG0_ON | DISPCNT_BG1_ON | DISPCNT_WIN0_ON | DISPCNT_WIN1_ON;
        REG_BG0CNT = BGCNT_SCREENBASE(31);
        REG_BG1CNT = 1 | BGCNT_SCREENBASE(30);
        REG_BG0HOFS = 0;
        REG_BG1HOFS = 3;
        REG_WIN0H = (130 << 8) | 135;
        REG_WIN0V = (0 << 8) | 80;
        REG_WIN1H = (130 << 8) | 135;
        REG_WIN1V = (80 << 8) | 120;
        REG_WININ = 0x01 | (0x02 << 8);
        REG_WINOUT = 0;
        for (i = 1; i <= 8; i++)
            Pltt()[i] = (u16)(i * 0x0421);
        for (i = 0; i < 64; i++)
            PutPixel4(0, 1, i & 7, i >> 3, (i & 7) + 1);
        for (i = 0; i < 32 * 32; i++)
        {
            SetText(0xF800, 0, i & 31, i >> 5, 0x0401);
            SetText(0xF000, 0, i & 31, i >> 5, 0x0401);
        }
        RenderBgBeginFrame();
        DrawLine(10);
        for (x = 130; x < 135; x++)
            EXPECT_EQ(sOut.color[0][x], (pass ? bg0Mgba : bg0Hw)[x - 130] * 0x0421);
        EXPECT_EQ(sOut.color[0][129], 7 * 0x0421); /* outside the segment: as drawn (masked later) */
        EXPECT_EQ(sOut.color[0][135], 1 * 0x0421);
        DrawLine(90);
        for (x = 130; x < 135; x++)
            EXPECT_EQ(sOut.color[1][x], bg1[x - 130] * 0x0421);
        /* No window on line 130: one segment, nothing cut. */
        DrawLine(130);
        for (x = 130; x < 135; x++)
            EXPECT_EQ(sOut.color[0][x], bg0Hw[x - 130] * 0x0421);
    }
    gRenderMgbaBgClip = 0;
}

static const struct TestCase sTests[] = {
    { "U1 4bpp nibble order", TestU1NibbleOrder },
    { "U2 index 0 transparent, backdrop", TestU2TransparentAndBackdrop },
    { "U3 palette bank", TestU3PaletteBank },
    { "U4 flip bits", TestU4Flips },
    { "U5 scroll wrap", TestU5ScrollWrap },
    { "U6 screen blocks, sizes 0-3", TestU6ScreenBlocks },
    { "U7 tile past BG VRAM", TestU7TilePastVram },
    { "U8 VRAM, not the shadow", TestU8VramNotShadow },
    { "U9 affine identity", TestU9AffineIdentity },
    { "U10 affine 2x zoom, wrap and no wrap", TestU10AffineZoom },
    { "U11 28-bit sign extension and latch", TestU11Sext28 },
    { "U12 affine step and mid-frame reload", TestU12AffineStepAndReload },
    { "U13 shadow bytes as affine cells", TestU13ShadowBytes },
    { "U14 mode 1 never draws BG3", TestU14Mode1Bg3Off },
    { "U15 no caching across lines", TestU15NoCaching },
    { "mode table and priority", TestModeTableAndPriority },
    { "8bpp text BG", TestText8bpp },
    { "affine step conditions", TestAffineStepConditions },
    { "affine rotation formula", TestAffineRotationFormula },
    { "register snapshot", TestSnapshot },
    { "layer mask", TestLayerMask },
    { "one-time warnings", TestWarnings },
    { "mGBA BG clip (FE8PC_MGBA_BGCLIP)", TestMgbaBgClip },
};

int main(int argc, char **argv)
{
    unsigned i, passed = 0, failed = 0;

    sVerbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    unsetenv("FE8PC_RENDER_LAYERS");
    if (MapRegion(PORT_IO_START, 0x1000) != 0 || MapRegion(PORT_PLTT_START, 0x1000) != 0 ||
        MapRegion(PORT_VRAM_START, 0x18000) != 0)
        return 2;

    for (i = 0; i < ARRAY_COUNT(sTests); i++)
    {
        sCaseFails = 0;
        sTests[i].func();
        if (sCaseFails == 0)
        {
            passed++;
            printf("PASS %s\n", sTests[i].name);
        }
        else
        {
            failed++;
            printf("FAIL %s (%d failed checks)\n", sTests[i].name, sCaseFails);
        }
    }
    printf("%u passed, %u failed, %u checks\n", passed, failed, sChecks);
    return failed == 0 ? 0 : 1;
}

#endif /* RENDER_BG_TEST */
