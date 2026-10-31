/*
 * Unit tests for the sprite and effects half of the renderer
 * (port-plan/render-design-obj.md section 11.1, RT1-RT18; RT19 from the PPU
 * test ROM, port/test/README.md).
 *
 * Headless, no game code. Each test fills small synthetic I/O, PLTT, VRAM and
 * OAM images, draws a few lines and checks exact 15-bit values. Expected
 * values are worked by hand from the rule the design cites, not taken from
 * the renderer.
 *
 * Build and run from the worktree root (host cc, not the game build):
 *   gcc -std=gnu11 -Wall -Werror -O1 -Iport/include -Iport/render \
 *       port/render/test_obj.c port/render/obj_line.c port/render/window.c \
 *       port/render/blend.c port/render/out_rgba.c -o /tmp/test_obj && /tmp/test_obj
 */

#include <stdio.h>
#include <string.h>

#include "obj_line.h"

static uint8_t sIo[0x400];
static uint8_t sPltt[0x400];
static uint8_t sVram[0x18000];
static uint8_t sOam[0x400];
static const struct RenderMem sMem = { sIo, sPltt, sVram, sOam };

static uint16_t sRow[RENDER_WIDTH];
static uint16_t sBgPx[4][RENDER_WIDTH];
static struct RenderComposeBg sBg;

static const char *sTestName;
static int sTestFails;

#define BACKDROP 0x7C00 /* blue: never equal to a test pattern colour */
#define C1 0x7D51       /* r 17, g 10, b 31 */
#define C2 0x0290       /* r 16, g 20, b 0 */
#define COLOR_A 0x001F
#define COLOR_B 0x03E0

#define EXPECT(cond) \
    do \
    { \
        if (!(cond)) \
        { \
            sTestFails++; \
            fprintf(stderr, "  %s:%d: expected %s\n", sTestName, __LINE__, #cond); \
        } \
    } while (0)

#define EXPECT_EQ(got, want) \
    do \
    { \
        long g_ = (long)(got); \
        long w_ = (long)(want); \
        if (g_ != w_) \
        { \
            sTestFails++; \
            fprintf(stderr, "  %s:%d: %s = 0x%lX, want 0x%lX\n", sTestName, __LINE__, #got, g_, w_); \
        } \
    } while (0)

/* ---- image helpers ---- */

static void SetIo16(unsigned off, uint16_t v)
{
    sIo[off] = (uint8_t)v;
    sIo[off + 1] = (uint8_t)(v >> 8);
}

static void SetPltt(unsigned entry, uint16_t c)
{
    sPltt[entry * 2] = (uint8_t)c;
    sPltt[entry * 2 + 1] = (uint8_t)(c >> 8);
}

static void SetObjPal(int bank, int idx, uint16_t c)
{
    SetPltt(256 + (unsigned)(bank * 16 + idx), c);
}

static void SetOam(int i, uint16_t a0, uint16_t a1, uint16_t a2)
{
    uint8_t *p = &sOam[i * 8];

    p[0] = (uint8_t)a0;
    p[1] = (uint8_t)(a0 >> 8);
    p[2] = (uint8_t)a1;
    p[3] = (uint8_t)(a1 >> 8);
    p[4] = (uint8_t)a2;
    p[5] = (uint8_t)(a2 >> 8);
}

/* Affine group g lives in the attr3 slots of entries 4g..4g+3 (bytes 32g + 6, 14, 22, 30). */
static void SetAffine(int g, int16_t pa, int16_t pb, int16_t pc, int16_t pd)
{
    int16_t v[4] = { pa, pb, pc, pd };
    int k;

    for (k = 0; k < 4; k++)
    {
        sOam[g * 32 + k * 8 + 6] = (uint8_t)v[k];
        sOam[g * 32 + k * 8 + 7] = (uint8_t)((uint16_t)v[k] >> 8);
    }
}

static void PutObjPixel(unsigned tile, int px, int py, int idx)
{
    uint8_t *b = &sVram[0x10000 + ((tile * 32 + (unsigned)(py * 4 + px / 2)) & 0x7FFF)];

    if (px & 1)
        *b = (uint8_t)((*b & 0x0F) | (idx << 4));
    else
        *b = (uint8_t)((*b & 0xF0) | idx);
}

static void FillTile(unsigned tile, int idx)
{
    memset(&sVram[0x10000 + tile * 32], idx | (idx << 4), 32);
}

/* Test pattern: index 1-15, gray colour index * 0x0421 in bank 0. */
static int Pat(int tx, int ty)
{
    return 1 + (tx + 3 * ty) % 15;
}

static uint16_t PatColor(int tx, int ty)
{
    return (uint16_t)(Pat(tx, ty) * 0x0421);
}

/* A 16x16 pattern sprite at tile t0, laid out for 1D mapping (2 tiles per row). */
static void PutPattern16(unsigned t0)
{
    int tx, ty, i;

    for (i = 1; i < 16; i++)
        SetObjPal(0, i, (uint16_t)(i * 0x0421));
    for (ty = 0; ty < 16; ty++)
        for (tx = 0; tx < 16; tx++)
            PutObjPixel(t0 + (unsigned)((ty >> 3) * 2 + (tx >> 3)), tx & 7, ty & 7, Pat(tx, ty));
}

static void BgSet(int n, int prio, uint16_t color)
{
    int x;

    for (x = 0; x < RENDER_WIDTH; x++)
        sBgPx[n][x] = color;
    sBg.color[n] = sBgPx[n];
    sBg.on[n] = 1;
    sBg.prio[n] = (uint8_t)prio;
}

static void Draw(int line, const struct RenderComposeBg *bg)
{
    RenderObjFxLine(line, NULL, &sMem, bg, sRow);
}

static int CountColor(uint16_t c)
{
    int x, n = 0;

    for (x = 0; x < RENDER_WIDTH; x++)
        n += sRow[x] == c;
    return n;
}

static void QuietTrip(int id, int line, uint32_t detail)
{
    (void)id;
    (void)line;
    (void)detail;
}

static void Reset(void)
{
    int i;

    memset(sIo, 0, sizeof(sIo));
    memset(sPltt, 0, sizeof(sPltt));
    memset(sVram, 0, sizeof(sVram));
    memset(sOam, 0, sizeof(sOam));
    memset(&sBg, 0, sizeof(sBg));
    for (i = 0; i < 128; i++)
        SetOam(i, 0x0200, 0, 0); /* disabled */
    SetPltt(0, BACKDROP);
    RenderFxTripReset();
    RenderObjTakePeakCycles();
    gRenderBlendMode = RENDER_BLEND_HW;
    gRenderSemiIgnoresWinFx = 1;
    gRenderObjCycleLimit = 1;
    gRenderFxTripHook = QuietTrip;
}

/* ---- RT1: all 12 shape and size pairs give the right box (4.1) ---- */
static void Rt1Sizes(void)
{
    static const int want[3][4][2] = {
        { { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } },
        { { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } },
        { { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } },
    };
    int shape, size, line;

    memset(sVram + 0x10000, 0x11, 0x8000); /* every pixel index 1 */
    SetObjPal(0, 1, COLOR_A);
    SetIo16(0x00, 0x1040); /* OBJ on, 1D */
    for (shape = 0; shape < 3; shape++)
    {
        for (size = 0; size < 4; size++)
        {
            int w = 0, h = 0;

            SetOam(0, (uint16_t)(shape << 14), (uint16_t)(size << 14), 0);
            for (line = 0; line < RENDER_HEIGHT; line++)
            {
                int n;

                Draw(line, NULL);
                n = CountColor(COLOR_A);
                if (line == 0)
                    w = n;
                if (n != 0)
                    h++;
            }
            EXPECT_EQ(w, want[shape][size][0]);
            EXPECT_EQ(h, want[shape][size][1]);
        }
    }
    /* Shape 3 is prohibited: not drawn, T3. */
    SetOam(0, 3 << 14, 0, 0);
    Draw(0, NULL);
    EXPECT_EQ(CountColor(COLOR_A), 0);
    EXPECT(RenderFxTripCount(RENDER_TRIP_OBJ_SHAPE3) > 0);
}

/* ---- RT2: H flip, V flip and both (4.3) ---- */
static void Rt2Flips(void)
{
    int f, tx, ty;

    SetIo16(0x00, 0x1040);
    PutPattern16(0);
    for (f = 0; f < 4; f++)
    {
        /* y 20, x 40, square size 1 (16x16), attr1 bit 12 H flip, bit 13 V flip */
        SetOam(0, 20, (uint16_t)(40 | (f << 12) | (1 << 14)), 0);
        for (ty = 0; ty < 16; ty++)
        {
            Draw(20 + ty, NULL);
            for (tx = 0; tx < 16; tx++)
            {
                int wx = (f & 1) ? 15 - tx : tx;
                int wy = (f & 2) ? 15 - ty : ty;

                EXPECT_EQ(sRow[40 + tx], PatColor(wx, wy));
            }
            EXPECT_EQ(sRow[39], BACKDROP);
            EXPECT_EQ(sRow[56], BACKDROP);
        }
    }
}

/* ---- RT3: a 32x16 sprite reads different tiles in 2D and 1D mapping (4.5) ---- */
static void Rt3Mapping(void)
{
    SetObjPal(0, 5, 0x1111);
    SetObjPal(0, 6, 0x2222);
    SetObjPal(0, 7, 0x3333);
    SetObjPal(0, 8, 0x4444);
    SetObjPal(0, 9, 0x5555);
    PutObjPixel(37, 0, 0, 5); /* 2D: t0 4 + 1 row * 32 + 1 column */
    PutObjPixel(9, 0, 0, 6);  /* 1D: t0 4 + 1 row * 4 tiles + 1 column */
    PutObjPixel(5, 0, 0, 7);  /* row 0, column 1 in both */
    PutObjPixel(0, 0, 0, 8);  /* 2D column wrap target */
    PutObjPixel(32, 0, 0, 9); /* 1D: no wrap */

    /* Wide (shape 1), size 2 = 32x16, at (0, 0), tile 4. */
    SetOam(0, 0x4000, 0x8000, 4);
    SetIo16(0x00, 0x1000); /* 2D */
    Draw(8, NULL);
    EXPECT_EQ(sRow[8], 0x1111);
    Draw(0, NULL);
    EXPECT_EQ(sRow[8], 0x3333);
    SetIo16(0x00, 0x1040); /* 1D */
    Draw(8, NULL);
    EXPECT_EQ(sRow[8], 0x2222);
    Draw(0, NULL);
    EXPECT_EQ(sRow[8], 0x3333);
    EXPECT_EQ(RenderFxTripCount(RENDER_TRIP_OBJ_WRAP), 0);

    /* Tile 30: column 2 is tile 32 in 1D, but wraps to tile 0 in 2D (T7). */
    SetOam(0, 0x4000, 0x8000, 30);
    Draw(0, NULL);
    EXPECT_EQ(sRow[16], 0x5555);
    SetIo16(0x00, 0x1000);
    Draw(0, NULL);
    EXPECT_EQ(sRow[16], 0x4444);
    EXPECT(RenderFxTripCount(RENDER_TRIP_OBJ_WRAP) > 0);
}

/* ---- RT4: X 500 shows at x -12; Y 250, H 16 shows rows 6-15 on lines 0-9 (4.2) ---- */
static void Rt4Wrap(void)
{
    int line, sx, any;

    SetIo16(0x00, 0x1040);
    PutPattern16(0);
    SetOam(0, 250, (uint16_t)(500 | (1 << 14)), 0);
    Draw(0, NULL);
    for (sx = 0; sx < 4; sx++)
        EXPECT_EQ(sRow[sx], PatColor(12 + sx, 6));
    EXPECT_EQ(sRow[4], BACKDROP);
    Draw(9, NULL);
    EXPECT_EQ(sRow[0], PatColor(12, 15));
    EXPECT_EQ(sRow[3], PatColor(15, 15));
    Draw(10, NULL);
    EXPECT_EQ(sRow[0], BACKDROP);
    Draw(159, NULL);
    EXPECT_EQ(sRow[0], BACKDROP);

    /* ClearOam's y = 160 with an 8x8 box is never visible. */
    SetOam(0, 160, 0, 0);
    any = 0;
    for (line = 0; line < RENDER_HEIGHT; line++)
    {
        Draw(line, NULL);
        any += RENDER_WIDTH - CountColor(BACKDROP);
    }
    EXPECT_EQ(any, 0);
}

/* ---- RT5: attr0 bit 9 without affine hides the sprite (4.1) ---- */
static void Rt5Disable(void)
{
    SetIo16(0x00, 0x1040);
    PutPattern16(0);
    SetAffine(0, 0x100, 0, 0, 0x100);
    SetOam(0, 0x0200, 0, 0);
    Draw(0, NULL);
    EXPECT_EQ(CountColor(BACKDROP), RENDER_WIDTH);
    SetOam(0, 0x0000, 0, 0);
    Draw(0, NULL);
    EXPECT_EQ(sRow[0], PatColor(0, 0));
    /* With the affine bit, bit 9 means double size: 8x8 centred in 16x16. */
    SetOam(0, 0x0300, 0, 0);
    Draw(4, NULL);
    EXPECT_EQ(sRow[3], BACKDROP);
    EXPECT_EQ(sRow[4], PatColor(0, 0));
    EXPECT_EQ(sRow[11], PatColor(7, 0));
    EXPECT_EQ(sRow[12], BACKDROP);
}

/* ---- RT6: affine identity, double size, scale 2, groups (4.4) ---- */
static void Rt6Affine(void)
{
    uint16_t normal[16][RENDER_WIDTH];
    int line, sx, ok;

    SetIo16(0x00, 0x1040);
    PutPattern16(0);
    for (line = 0; line < 16; line++)
    {
        SetOam(0, 20, 40 | (1 << 14), 0);
        Draw(20 + line, NULL);
        memcpy(normal[line], sRow, sizeof(sRow));
    }

    /* Identity, group 0, no double size: equals the normal sprite. */
    SetAffine(0, 0x100, 0, 0, 0x100);
    SetOam(0, 0x0100 | 20, 40 | (1 << 14), 0);
    ok = 1;
    for (line = 0; line < 16; line++)
    {
        Draw(20 + line, NULL);
        ok &= memcmp(normal[line], sRow, sizeof(sRow)) == 0;
    }
    EXPECT(ok);

    /* Group 8 sets attr1 bit 12 (H flip when not affine): no flip, group 8's parameters. */
    SetAffine(0, 0, 0, 0, 0);
    SetAffine(8, 0x100, 0, 0, 0x100);
    SetOam(0, 0x0100 | 20, (uint16_t)(40 | (8 << 9) | (1 << 14)), 0);
    ok = 1;
    for (line = 0; line < 16; line++)
    {
        Draw(20 + line, NULL);
        ok &= memcmp(normal[line], sRow, sizeof(sRow)) == 0;
    }
    EXPECT(ok);

    /* Identity with double size: 32x32 box, sprite pixel (tx, ty) at (48 + tx, 28 + ty). */
    SetAffine(0, 0x100, 0, 0, 0x100);
    SetOam(0, 0x0300 | 20, 40 | (1 << 14), 0);
    for (line = 20; line < 52; line++)
    {
        Draw(line, NULL);
        for (sx = 40; sx < 72; sx++)
        {
            int tx = sx - 48, ty = line - 28;

            if (tx >= 0 && tx < 16 && ty >= 0 && ty < 16)
                EXPECT_EQ(sRow[sx], PatColor(tx, ty));
            else
                EXPECT_EQ(sRow[sx], BACKDROP);
        }
    }

    /* Scale 2 without double size: the middle 8x8 doubled, clipped to the 16x16 box. */
    SetAffine(0, 0x80, 0, 0, 0x80);
    SetOam(0, 0x0100 | 20, 40 | (1 << 14), 0);
    for (line = 20; line < 36; line++)
    {
        Draw(line, NULL);
        for (sx = 40; sx < 56; sx++)
            EXPECT_EQ(sRow[sx], PatColor(4 + ((sx - 40) >> 1), 4 + ((line - 20) >> 1)));
        EXPECT_EQ(sRow[39], BACKDROP);
        EXPECT_EQ(sRow[56], BACKDROP);
    }
    Draw(36, NULL);
    EXPECT_EQ(sRow[40], BACKDROP);

    /* Scale 2 with double size: the whole sprite doubled in the 32x32 box. */
    SetOam(0, 0x0300 | 20, 40 | (1 << 14), 0);
    Draw(20, NULL);
    EXPECT_EQ(sRow[40], PatColor(0, 0));
    EXPECT_EQ(sRow[71], PatColor(15, 0));
    Draw(51, NULL);
    EXPECT_EQ(sRow[41], PatColor(0, 15));

    /* Group 5 mirror (pa = -1.0): tx = 56 - sx, so sx 40 samples tx 16 (outside). */
    SetAffine(5, (int16_t)-0x100, 0, 0, 0x100);
    SetOam(0, 0x0100 | 20, (uint16_t)(40 | (5 << 9) | (1 << 14)), 0);
    Draw(20, NULL);
    EXPECT_EQ(sRow[40], BACKDROP);
    EXPECT_EQ(sRow[41], PatColor(15, 0));
    EXPECT_EQ(sRow[55], PatColor(1, 0));
}

/* ---- RT7: OBJ against OBJ (4.6) ---- */
static void Rt7ObjOrder(void)
{
    SetIo16(0x00, 0x1000);
    SetObjPal(0, 1, COLOR_A);
    SetObjPal(0, 2, COLOR_B);
    FillTile(1, 1);
    FillTile(2, 2);
    FillTile(3, 1);
    PutObjPixel(3, 1, 0, 0); /* tile 3: transparent at column 1 */

    /* Sprite 0 at x 0 (A), sprite 1 at x 4 (B), both priority 1. */
    SetOam(0, 0, 0, 1 | (1 << 10));
    SetOam(1, 0, 4, 2 | (1 << 10));
    Draw(0, NULL);
    EXPECT_EQ(sRow[5], COLOR_A); /* tie: lower index */
    EXPECT_EQ(sRow[9], COLOR_B);

    /* Sprite 0 priority 2: sprite 1's lower priority value wins. */
    SetOam(0, 0, 0, 1 | (2 << 10));
    Draw(0, NULL);
    EXPECT_EQ(sRow[5], COLOR_B);
    EXPECT_EQ(sRow[3], COLOR_A);

    /* A transparent pixel of sprite 0 does not hide sprite 1. */
    SetOam(0, 0, 4, 3 | (1 << 10));
    Draw(0, NULL);
    EXPECT_EQ(sRow[5], COLOR_B);
    EXPECT_EQ(sRow[4], COLOR_A);
}

/* ---- RT8: OBJ against BG, BG against BG (6) ---- */
static void Rt8ObjBg(void)
{
    SetIo16(0x00, 0x1000);
    SetObjPal(0, 1, COLOR_A);
    FillTile(1, 1);
    SetOam(0, 0, 0, 1 | (1 << 10)); /* 8x8 at (0, 0), priority 1 */

    BgSet(0, 1, 0x0010);
    Draw(0, &sBg);
    EXPECT_EQ(sRow[0], COLOR_A); /* equal priority: OBJ in front */
    EXPECT_EQ(sRow[8], 0x0010);

    BgSet(0, 0, 0x0010);
    Draw(0, &sBg);
    EXPECT_EQ(sRow[0], 0x0010); /* BG priority 0 beats OBJ priority 1 */

    BgSet(0, 2, 0x0010);
    BgSet(1, 2, 0x0011);
    Draw(0, &sBg);
    EXPECT_EQ(sRow[0], COLOR_A);
    EXPECT_EQ(sRow[100], 0x0010); /* equal BG priorities: BG0 */

    sBgPx[0][100] = RENDER_TRANSPARENT;
    Draw(0, &sBg);
    EXPECT_EQ(sRow[100], 0x0011);

    /* BG1 at a lower priority value than BG0 is in front. */
    BgSet(1, 1, 0x0011);
    Draw(0, &sBg);
    EXPECT_EQ(sRow[50], 0x0011);

    sBg.on[0] = 0;
    sBg.on[1] = 0;
    Draw(0, &sBg);
    EXPECT_EQ(sRow[100], BACKDROP);
}

/* ---- RT9: window order, empty window, wraps (5) ---- */
static void Rt9Windows(void)
{
    struct RenderFxRegs fx;
    uint8_t objWin[RENDER_WIDTH];
    uint8_t mask[RENDER_WIDTH];
    int x, all;

    memset(&fx, 0, sizeof(fx));
    memset(objWin, 0, sizeof(objWin));

    fx.dispcnt = 0;
    RenderWindowLine(0, &fx, objWin, mask);
    for (all = 1, x = 0; x < RENDER_WIDTH; x++)
        all &= mask[x] == RENDER_WIN_ALL;
    EXPECT(all);

    fx.dispcnt = 0x1000 | 0x2000 | 0x4000 | 0x8000;
    fx.win0h = (10 << 8) | 20;
    fx.win0v = (0 << 8) | 160;
    fx.win1h = (15 << 8) | 30;
    fx.win1v = (0 << 8) | 160;
    fx.winin = 0x01 | (0x02 << 8);
    fx.winout = 0x04 | (0x08 << 8);
    for (x = 40; x < 48; x++)
        objWin[x] = 1;
    objWin[18] = 1;
    objWin[25] = 1;
    RenderWindowLine(0, &fx, objWin, mask);
    EXPECT_EQ(mask[9], 0x04);
    EXPECT_EQ(mask[10], 0x01);
    EXPECT_EQ(mask[17], 0x01); /* WIN0 over WIN1 */
    EXPECT_EQ(mask[18], 0x01); /* WIN0 over the OBJ window */
    EXPECT_EQ(mask[19], 0x01);
    EXPECT_EQ(mask[20], 0x02); /* right edge exclusive */
    EXPECT_EQ(mask[25], 0x02); /* WIN1 over the OBJ window */
    EXPECT_EQ(mask[30], 0x04);
    EXPECT_EQ(mask[40], 0x08);
    EXPECT_EQ(mask[47], 0x08);
    EXPECT_EQ(mask[48], 0x04);
    EXPECT_EQ(mask[100], 0x04);

    /* Empty 0xF0F0 window (left = right = 240): no pixel is inside. */
    fx.dispcnt = 0x2000;
    fx.win0h = 0xF0F0;
    RenderWindowLine(0, &fx, objWin, mask);
    for (all = 1, x = 0; x < RENDER_WIDTH; x++)
        all &= mask[x] == 0x04;
    EXPECT(all);
    EXPECT_EQ(RenderFxTripCount(RENDER_TRIP_WINDOW), 0);

    /* Left > right wraps: [0, 20) and [200, 240). T6 reports it. */
    fx.win0h = (200 << 8) | 20;
    RenderWindowLine(0, &fx, objWin, mask);
    EXPECT_EQ(mask[0], 0x01);
    EXPECT_EQ(mask[19], 0x01);
    EXPECT_EQ(mask[20], 0x04);
    EXPECT_EQ(mask[199], 0x04);
    EXPECT_EQ(mask[200], 0x01);
    EXPECT_EQ(mask[239], 0x01);
    EXPECT(RenderFxTripCount(RENDER_TRIP_WINDOW) > 0);

    /* mGBA's clamp order: left 250 > 240 and > right 245, so left = 0; right = 240. */
    fx.win0h = (250 << 8) | 245;
    RenderWindowLine(0, &fx, objWin, mask);
    EXPECT_EQ(mask[0], 0x01);
    EXPECT_EQ(mask[239], 0x01);
    /* Left 245 < right 250: right = 240, then left = 240: empty. */
    fx.win0h = (245 << 8) | 250;
    RenderWindowLine(0, &fx, objWin, mask);
    EXPECT_EQ(mask[0], 0x04);
    EXPECT_EQ(mask[239], 0x04);

    /* Vertical wrap: top 150 > bottom 10, inside on lines >= 150 or < 10. */
    fx.win0h = (0 << 8) | 240;
    fx.win0v = (150 << 8) | 10;
    RenderWindowLine(5, &fx, objWin, mask);
    EXPECT_EQ(mask[100], 0x01);
    RenderWindowLine(10, &fx, objWin, mask);
    EXPECT_EQ(mask[100], 0x04);
    RenderWindowLine(50, &fx, objWin, mask);
    EXPECT_EQ(mask[100], 0x04);
    RenderWindowLine(150, &fx, objWin, mask);
    EXPECT_EQ(mask[100], 0x01);

    /* Through the compositor: WIN0 [10, 20) shows only BG1, outside only BG0. */
    BgSet(0, 0, 0x0010);
    BgSet(1, 1, 0x0011);
    SetIo16(0x00, 0x2000);
    SetIo16(0x40, (10 << 8) | 20);
    SetIo16(0x44, (0 << 8) | 160);
    SetIo16(0x48, 0x02);
    SetIo16(0x4A, 0x01);
    Draw(0, &sBg);
    EXPECT_EQ(sRow[9], 0x0010);
    EXPECT_EQ(sRow[12], 0x0011);
    EXPECT_EQ(sRow[20], 0x0010);
}

/* ---- RT10: OBJ window from a mode-2 sprite needs DISPCNT bits 12 and 15 (4.6, 5.1) ---- */
static void Rt10ObjWindow(void)
{
    SetObjPal(0, 1, COLOR_A);
    FillTile(1, 1);
    SetOam(0, 0x0800, 40, 1); /* mode 2, 8x8 at (40, 0) */
    BgSet(0, 0, 0x0010);
    SetIo16(0x4A, 0x00 | (0x01 << 8)); /* outside: nothing; OBJ window: BG0 */

    SetIo16(0x00, 0x1000 | 0x8000);
    Draw(0, &sBg);
    EXPECT_EQ(sRow[39], BACKDROP);
    EXPECT_EQ(sRow[40], 0x0010); /* the window sprite is never drawn as colour */
    EXPECT_EQ(sRow[47], 0x0010);
    EXPECT_EQ(sRow[48], BACKDROP);

    SetIo16(0x00, 0x8000); /* OBJ layer off: no OBJ window */
    Draw(0, &sBg);
    EXPECT_EQ(sRow[40], BACKDROP);

    SetIo16(0x00, 0x1000 | 0x2000); /* windows on (empty WIN0), bit 15 clear */
    SetIo16(0x40, 0xF0F0);
    Draw(0, &sBg);
    EXPECT_EQ(sRow[40], BACKDROP);
}

/* ---- RT11: alpha, brighten and darken at EV 0, 8, 16 and 31, both modes (7.2, 7.6) ---- */
static void Rt11BlendValues(void)
{
    static const int evs[4] = { 0, 8, 16, 31 };
    /* [mode][ev index]: worked by hand from 7.2 (HW) and 7.6 (mGBA 8-bit) */
    static const uint16_t wantAlpha[2][4] = {
        { 0x0000, 0x3DF0, 0x7FDF, 0x7FDF },
        { 0x0000, 0x3DF1, 0x7FDF, 0x7FDF },
    };
    static const uint16_t wantBrighten[2][4] = {
        { C1, 0x7E98, 0x7FFF, 0x7FFF },
        { C1, 0x7EB8, 0x7FFF, 0x7FFF },
    };
    static const uint16_t wantDarken[2][4] = {
        { C1, 0x40A9, 0x0000, 0x0000 },
        { C1, 0x3CA8, 0x0000, 0x0000 },
    };
    int mode, k;

    BgSet(0, 0, C1);
    BgSet(1, 1, C2);
    for (mode = 0; mode < 2; mode++)
    {
        gRenderBlendMode = mode == 0 ? RENDER_BLEND_HW : RENDER_BLEND_MGBA8;
        for (k = 0; k < 4; k++)
        {
            int ev = evs[k];

            SetIo16(0x52, (uint16_t)(ev | (ev << 8)));
            SetIo16(0x54, (uint16_t)ev);
            SetIo16(0x50, 0x0001 | (0x02 << 8) | (1 << 6));
            Draw(0, &sBg);
            EXPECT_EQ(sRow[0], wantAlpha[mode][k]);
            SetIo16(0x50, 0x0001 | (2 << 6));
            Draw(0, &sBg);
            EXPECT_EQ(sRow[0], wantBrighten[mode][k]);
            SetIo16(0x50, 0x0001 | (3 << 6));
            Draw(0, &sBg);
            EXPECT_EQ(sRow[0], wantDarken[mode][k]);
        }
    }
    EXPECT(RenderFxTripCount(RENDER_TRIP_EV) > 0); /* EV 31 */

    /* The design's worked examples, red channel (brighten also lifts the zero channels). */
    EXPECT_EQ(RenderBlendAlpha(17, 16, 8, 8), 16);
    EXPECT_EQ(RenderBlendAlphaMgba8(17, 16, 8, 8), 17);
    EXPECT_EQ(RenderBrighten(16, 1) & 31, 16);
    EXPECT_EQ(RenderBrightenMgba8(16, 1) & 31, 17);
    /* Black brightened by 1: hardware 0 + (31 >> 4) = 1; mGBA 255 / 16 = 15, 15 >> 3 = 1. */
    EXPECT_EQ(RenderBrighten(0, 1), 0x0421);
    EXPECT_EQ(RenderBrightenMgba8(0, 1), 0x0421);
    /* mGBA's packed darken: blue 255 - 127.5 rounds to 127 (15), red would floor to 128 (16). */
    EXPECT_EQ(RenderDarkenMgba8(0x7C00, 8), 0x3C00);
    EXPECT_EQ(RenderDarkenMgba8(0x001F, 8), 0x0010);
}

/* ---- RT12: alpha needs `second` to be target 2 (7.3) ---- */
static void Rt12AlphaSecond(void)
{
    BgSet(0, 0, C1);
    BgSet(1, 1, C2);
    BgSet(2, 2, 0x7FFF);
    sBgPx[1][1] = RENDER_TRANSPARENT;
    SetIo16(0x50, 0x0001 | (0x04 << 8) | (1 << 6)); /* BG0 target 1, BG2 target 2, alpha */
    SetIo16(0x52, 8 | (8 << 8));
    Draw(0, &sBg);
    EXPECT_EQ(sRow[0], C1);     /* second is BG1, not target 2: no blend */
    EXPECT_EQ(sRow[1], 0x7E98); /* BG1 clear here: second is BG2 */
}

/* ---- RT13: semi-transparent OBJ (7.3) ---- */
static void Rt13SemiObj(void)
{
    SetIo16(0x00, 0x1000);
    SetObjPal(0, 1, C1);
    FillTile(1, 1);
    SetOam(0, 0x0400, 0, 1); /* mode 1, 8x8 at (0, 0), priority 0 */
    BgSet(0, 1, C2);
    SetIo16(0x52, 8 | (8 << 8));
    SetIo16(0x54, 8);

    SetIo16(0x50, 0x0100); /* BG0 target 2, effect none, bit 4 clear */
    Draw(0, &sBg);
    EXPECT_EQ(sRow[0], 0x3DF0);
    EXPECT_EQ(sRow[8], C2);

    SetIo16(0x50, 0x0100 | 0x0010 | (2 << 6)); /* brighten with OBJ target 1 */
    Draw(0, &sBg);
    EXPECT_EQ(sRow[0], 0x3DF0); /* alpha only, no brightness */

    SetIo16(0x50, 0x0010 | (3 << 6)); /* BG0 not target 2; darken, OBJ target 1 */
    Draw(0, &sBg);
    EXPECT_EQ(sRow[0], 0x40A9);

    SetIo16(0x50, (3 << 6)); /* bit 4 clear: nothing */
    Draw(0, &sBg);
    EXPECT_EQ(sRow[0], C1);

    /* Over the backdrop as target 2. */
    sBg.on[0] = 0;
    SetPltt(0, C2);
    SetIo16(0x50, 0x2000);
    Draw(0, &sBg);
    EXPECT_EQ(sRow[0], 0x3DF0);
}

/* ---- RT14: the window effect bit (7.4) ---- */
static void Rt14WindowFx(void)
{
    SetObjPal(0, 1, C1);
    FillTile(1, 1);
    BgSet(0, 1, C2);
    SetIo16(0x40, (0 << 8) | 240);
    SetIo16(0x44, (0 << 8) | 160);
    SetIo16(0x52, 8 | (8 << 8));
    SetIo16(0x54, 8);

    /* Darken BG0: blocked when WIN0 has the effect bit clear. */
    SetIo16(0x00, 0x2000);
    SetIo16(0x50, 0x0001 | (3 << 6));
    SetIo16(0x48, 0x1F);
    Draw(0, &sBg);
    EXPECT_EQ(sRow[100], C2);
    SetIo16(0x48, 0x3F);
    Draw(0, &sBg);
    EXPECT_EQ(sRow[100], 0x0148); /* r 16 - 8, g 20 - 10, b 0 */

    /* Semi-transparent OBJ over target 2 with the effect bit clear: the 7.4 constant. */
    SetIo16(0x00, 0x1000 | 0x2000);
    SetOam(0, 0x0400, 0, 1);
    SetIo16(0x50, 0x0100);
    SetIo16(0x48, 0x1F);
    gRenderSemiIgnoresWinFx = 1;
    Draw(0, &sBg);
    EXPECT_EQ(sRow[0], 0x3DF0);
    gRenderSemiIgnoresWinFx = 0;
    Draw(0, &sBg);
    EXPECT_EQ(sRow[0], C1);
    EXPECT_EQ(RENDER_SEMI_OBJ_IGNORES_WIN_FX, 1);
}

/* ---- RT15: forced blank; backdrop as target 1 and target 2 (8) ---- */
static void Rt15BlankBackdrop(void)
{
    struct RenderFxRegs fx;
    uint8_t mask[RENDER_WIDTH];

    SetObjPal(0, 1, COLOR_A);
    FillTile(1, 1);
    SetOam(0, 0, 0, 1);
    BgSet(0, 0, C1);
    SetIo16(0x00, 0x0080 | 0x1100);
    Draw(0, &sBg);
    EXPECT_EQ(CountColor(RENDER_WHITE), RENDER_WIDTH);

    memset(&fx, 0, sizeof(fx));
    memset(mask, RENDER_WIN_ALL, sizeof(mask));
    fx.dispcnt = 0x0080;
    RenderComposeLine(0, &fx, &sBg, BACKDROP, NULL, mask, sRow);
    EXPECT_EQ(CountColor(RENDER_WHITE), RENDER_WIDTH);

    /* Backdrop as target 1, brighten 8. */
    SetIo16(0x00, 0x0000);
    SetPltt(0, C1);
    SetIo16(0x50, 0x0020 | (2 << 6));
    SetIo16(0x54, 8);
    Draw(0, NULL);
    EXPECT_EQ(sRow[0], 0x7E98);

    /* Backdrop as target 2 under BG0, alpha 8/8. */
    SetPltt(0, C2);
    SetIo16(0x50, 0x0001 | 0x2000 | (1 << 6));
    SetIo16(0x52, 8 | (8 << 8));
    sBgPx[0][5] = RENDER_TRANSPARENT;
    Draw(0, &sBg);
    EXPECT_EQ(sRow[0], 0x3DF0);
    EXPECT_EQ(sRow[5], C2); /* the backdrop on top is not target 1 */
}

/* ---- RT16: OBJ cycle budget (4.7) ---- */
static void Rt16Cycles(void)
{
    int i;

    memset(sVram + 0x10000, 0x11, 0x8000);
    SetObjPal(1, 1, COLOR_A);
    SetObjPal(2, 1, COLOR_B);
    SetIo16(0x00, 0x1040);

    /* 18 64x64 sprites off screen (x = 300 - 512), then two on screen. */
    for (i = 0; i < 18; i++)
        SetOam(i, 0, 300 | (3 << 14), 0);
    SetOam(18, 0, 0 | (3 << 14), 1 << 12);
    SetOam(19, 0, 100 | (3 << 14), 2 << 12);
    Draw(0, NULL);
    EXPECT_EQ(sRow[0], COLOR_A); /* 1152 used, 58 left > 0: drawn */
    EXPECT_EQ(sRow[100], BACKDROP); /* -6 left: dropped */
    EXPECT(RenderFxTripCount(RENDER_TRIP_OBJ_CYCLES) > 0);
    EXPECT_EQ(RenderObjTakePeakCycles(), 20 * 64);

    gRenderObjCycleLimit = 0;
    Draw(0, NULL);
    EXPECT_EQ(sRow[100], COLOR_B);
    gRenderObjCycleLimit = 1;

    /* Affine double size 64x64 costs 10 + 2 * 128 = 266. Four of them leave 146. */
    for (i = 0; i < 20; i++)
        SetOam(i, 0x0200, 0, 0);
    SetAffine(0, 0x100, 0, 0, 0x100);
    for (i = 0; i < 4; i++)
        SetOam(i, 0x0300, 300 | (3 << 14), 0);
    for (i = 0; i < 4; i++)
        SetOam(4 + i, 0, (uint16_t)((64 * i) | (3 << 14)), 1 << 12);
    Draw(0, NULL);
    EXPECT_EQ(sRow[0], COLOR_A);   /* 146 left: drawn, 82 left */
    EXPECT_EQ(sRow[64], COLOR_A);  /* drawn, 18 left */
    EXPECT_EQ(sRow[128], COLOR_A); /* drawn, -46 left */
    EXPECT_EQ(sRow[192], BACKDROP);
    EXPECT_EQ(RenderObjTakePeakCycles(), 4 * 266 + 4 * 64);
}

/* Reference FNV-1a 32, straight from its definition. */
static uint32_t RefFnv1a(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    size_t i;

    for (i = 0; i < n; i++)
    {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/* ---- RT17: D3, the LUT, the packing and the hash (9) ---- */
static void Rt17Output(void)
{
    static uint16_t frame[RENDER_WIDTH * RENDER_HEIGHT];
    static uint32_t rgba[RENDER_WIDTH * RENDER_HEIGHT];
    static uint8_t bytes[RENDER_WIDTH * RENDER_HEIGHT * 2];
    uint32_t h1, h2;
    int i, ok;

    EXPECT_EQ(RenderExpand5(0), 0);
    EXPECT_EQ(RenderExpand5(16), 132);
    EXPECT_EQ(RenderExpand5(31), 255);
    EXPECT_EQ(RenderBgr555ToRgba8888(0x0000), 0x000000FFu);
    EXPECT_EQ(RenderBgr555ToRgba8888(0x001F), 0xFF0000FFu);
    EXPECT_EQ(RenderBgr555ToRgba8888(0x03E0), 0x00FF00FFu);
    EXPECT_EQ(RenderBgr555ToRgba8888(0x7C00), 0x0000FFFFu);
    EXPECT_EQ(RenderBgr555ToRgba8888(0x4210), 0x848484FFu);
    EXPECT_EQ(RenderBgr555ToRgba8888(0x7FFF), 0xFFFFFFFFu);

    /* 38,400 pixels cover all 32,768 values. */
    for (i = 0; i < RENDER_WIDTH * RENDER_HEIGHT; i++)
        frame[i] = (uint16_t)(i & 0x7FFF);
    RenderFrameToRgba8888(frame, rgba);
    for (ok = 1, i = 0; i < RENDER_WIDTH * RENDER_HEIGHT; i++)
        ok &= rgba[i] == RenderBgr555ToRgba8888(frame[i]);
    EXPECT(ok);

    EXPECT_EQ(RefFnv1a((const uint8_t *)"a", 1), 0xE40C292Cu);
    for (i = 0; i < RENDER_WIDTH * RENDER_HEIGHT; i++)
    {
        bytes[i * 2] = (uint8_t)frame[i];
        bytes[i * 2 + 1] = (uint8_t)(frame[i] >> 8);
    }
    h1 = RenderHashFrame555(frame);
    EXPECT_EQ(h1, RefFnv1a(bytes, sizeof(bytes)));
    h2 = RenderHashFrame555(frame);
    EXPECT_EQ(h1, h2);
    frame[12345] ^= 1;
    EXPECT(RenderHashFrame555(frame) != h1);
}

/* ---- RT18: registers and PLTT[0] changed between lines (3) ---- */
static void Rt18PerLine(void)
{
    uint16_t rows[4][RENDER_WIDTH];

    SetPltt(0, 0x001F);
    Draw(0, NULL);
    memcpy(rows[0], sRow, sizeof(sRow));

    SetPltt(0, 0x03E0);
    SetIo16(0x50, 0x0020 | (2 << 6)); /* backdrop target 1, brighten */
    SetIo16(0x54, 16);
    Draw(1, NULL);
    memcpy(rows[1], sRow, sizeof(sRow));

    SetIo16(0x00, 0x2000); /* WIN0 [0, 120): no effects inside */
    SetIo16(0x40, (0 << 8) | 120);
    SetIo16(0x44, (0 << 8) | 160);
    SetIo16(0x48, 0x00);
    SetIo16(0x4A, 0x3F);
    Draw(2, NULL);
    memcpy(rows[2], sRow, sizeof(sRow));

    SetIo16(0x40, (100 << 8) | 240); /* WIN0 [100, 240) */
    Draw(3, NULL);
    memcpy(rows[3], sRow, sizeof(sRow));

    EXPECT_EQ(rows[0][50], 0x001F);
    EXPECT_EQ(rows[0][200], 0x001F);
    EXPECT_EQ(rows[1][50], 0x7FFF);
    EXPECT_EQ(rows[2][50], 0x03E0);
    EXPECT_EQ(rows[2][200], 0x7FFF);
    EXPECT_EQ(rows[3][50], 0x7FFF);
    EXPECT_EQ(rows[3][200], 0x03E0);
}

/*
 * ---- RT19: a later sprite's transparent pixels lift an earlier one (4.6) ----
 * The scene of the PPU test ROM's variants 0 and 1
 * (port/test/oracle/pputest/pputest.c), on line 0. The expected pixels are not
 * worked by hand: they are what the oracle, mGBA 0.10.5, drew for that ROM
 * (tools/pputest_check.py, the same table; `make -f port/test/Makefile
 * pputest`). Test k's sprites are 8x8 at x = 16k + 4, in OAM order; "clear" =
 * every pixel transparent.
 */
#define RT19_A 0x7C00
#define RT19_B 0x03FF
#define RT19_C 0x7FFF
#define RT19_BG0 0x001F
#define RT19_BG1 0x03E0

static int sRt19Next;

static void Rt19Obj(int k, int dx, int mode, int prio, int bank, int opaque, int affine)
{
    SetOam(sRt19Next++, (uint16_t)((mode << 10) | (affine ? 0x100 : 0)), (uint16_t)((16 * k + 4 + dx) & 0x1FF),
        (uint16_t)((opaque ? 1 : 0) | (prio << 10) | (bank << 12)));
}

static void Rt19ClearLift(void)
{
    static const struct
    {
        int x;
        uint16_t want;
    } want[] = {
        { 6, RT19_A },     /* 0: A prio 2; clear prio 0; C prio 1 */
        { 22, RT19_C },    /* 1: A prio 2; C prio 1 */
        { 38, RT19_A },    /* 2: A prio 2; clear prio 0: in front of BG0 (prio 1) */
        { 54, RT19_A },    /* 3: semi A prio 2; normal clear prio 0: no longer blended */
        { 70, 0x3DE0 },    /* 4: normal A prio 2; semi clear prio 0: blended with BG1 */
        { 86, RT19_BG0 },  /* 5: A prio 2; OBJ-window clear prio 0: no lift */
        { 102, RT19_BG0 }, /* 6: A prio 2; OBJ-window opaque prio 0: no lift */
        { 118, RT19_A },   /* 7: A prio 1; clear prio 1; C prio 1 */
        { 134, RT19_A },   /* 8: A prio 3; clear prio 1; C prio 2 */
        { 150, RT19_BG0 }, /* 9: clear prio 0; B prio 2 */
        { 166, RT19_A },   /* 10: A prio 2; affine clear prio 0 */
        { 182, RT19_A },   /* 11: A prio 1; clear prio 3 */
        { 198, RT19_A },   /* 12: A prio 2; clear prio 0; C prio 0 */
        { 213, RT19_BG0 }, /* 13: A prio 2; clear prio 0 at x + 4: left half */
        { 217, RT19_A },   /*     right half */
    };
    unsigned i;
    int x, variant;

    for (variant = 0; variant < 2; variant++)
    {
        Reset();
        SetPltt(0, 0x0000);
        SetObjPal(1, 1, RT19_A);
        SetObjPal(2, 1, RT19_B);
        SetObjPal(3, 1, RT19_C);
        FillTile(1, 1);
        SetAffine(0, 0x100, 0, 0, 0x100);
        BgSet(0, 1, RT19_BG0);
        for (x = 48; x < 80; x++)
            sBgPx[0][x] = RENDER_TRANSPARENT;
        BgSet(1, 3, RT19_BG1);
        SetIo16(0x50, (1 << 6) | (1 << 9)); /* alpha, BG1 target 2 */
        SetIo16(0x52, 8 | (8 << 8));
        SetIo16(0x4A, 0x3F3F);
        SetIo16(0x00, (uint16_t)(0x1040 | (variant ? 0x8000 : 0)));
        gRenderBlendMode = RENDER_BLEND_MGBA8;

        sRt19Next = 0;
        Rt19Obj(0, 0, 0, 2, 1, 1, 0);
        Rt19Obj(0, 0, 0, 0, 2, 0, 0);
        Rt19Obj(0, 0, 0, 1, 3, 1, 0);
        Rt19Obj(1, 0, 0, 2, 1, 1, 0);
        Rt19Obj(1, 0, 0, 1, 3, 1, 0);
        Rt19Obj(2, 0, 0, 2, 1, 1, 0);
        Rt19Obj(2, 0, 0, 0, 2, 0, 0);
        Rt19Obj(3, 0, 1, 2, 1, 1, 0);
        Rt19Obj(3, 0, 0, 0, 2, 0, 0);
        Rt19Obj(4, 0, 0, 2, 1, 1, 0);
        Rt19Obj(4, 0, 1, 0, 2, 0, 0);
        Rt19Obj(5, 0, 0, 2, 1, 1, 0);
        Rt19Obj(5, 0, 2, 0, 2, 0, 0);
        Rt19Obj(6, 0, 0, 2, 1, 1, 0);
        Rt19Obj(6, 0, 2, 0, 2, 1, 0);
        Rt19Obj(7, 0, 0, 1, 1, 1, 0);
        Rt19Obj(7, 0, 0, 1, 2, 0, 0);
        Rt19Obj(7, 0, 0, 1, 3, 1, 0);
        Rt19Obj(8, 0, 0, 3, 1, 1, 0);
        Rt19Obj(8, 0, 0, 1, 2, 0, 0);
        Rt19Obj(8, 0, 0, 2, 3, 1, 0);
        Rt19Obj(9, 0, 0, 0, 1, 0, 0);
        Rt19Obj(9, 0, 0, 2, 2, 1, 0);
        Rt19Obj(10, 0, 0, 2, 1, 1, 0);
        Rt19Obj(10, 0, 0, 0, 2, 0, 1);
        Rt19Obj(11, 0, 0, 1, 1, 1, 0);
        Rt19Obj(11, 0, 0, 3, 2, 0, 0);
        Rt19Obj(12, 0, 0, 2, 1, 1, 0);
        Rt19Obj(12, 0, 0, 0, 2, 0, 0);
        Rt19Obj(12, 0, 0, 0, 3, 1, 0);
        Rt19Obj(13, 0, 0, 2, 1, 1, 0);
        Rt19Obj(13, 4, 0, 0, 2, 0, 0);

        Draw(0, &sBg);
        for (i = 0; i < sizeof(want) / sizeof(want[0]); i++)
        {
            if (sRow[want[i].x] != want[i].want)
            {
                sTestFails++;
                fprintf(stderr, "  %s: variant %d, x %d: 0x%04X, want 0x%04X\n", sTestName, variant, want[i].x,
                    sRow[want[i].x], want[i].want);
            }
        }
    }
}

struct TestCase
{
    const char *name;
    void (*fn)(void);
};

static const struct TestCase sTests[] = {
    { "RT1 sizes", Rt1Sizes },
    { "RT2 flips", Rt2Flips },
    { "RT3 1D/2D mapping", Rt3Mapping },
    { "RT4 X/Y wrap", Rt4Wrap },
    { "RT5 disable bit", Rt5Disable },
    { "RT6 affine", Rt6Affine },
    { "RT7 OBJ vs OBJ", Rt7ObjOrder },
    { "RT8 OBJ vs BG", Rt8ObjBg },
    { "RT9 windows", Rt9Windows },
    { "RT10 OBJ window", Rt10ObjWindow },
    { "RT11 blend values", Rt11BlendValues },
    { "RT12 alpha second", Rt12AlphaSecond },
    { "RT13 semi OBJ", Rt13SemiObj },
    { "RT14 window effect bit", Rt14WindowFx },
    { "RT15 forced blank, backdrop", Rt15BlankBackdrop },
    { "RT16 OBJ cycles", Rt16Cycles },
    { "RT17 output", Rt17Output },
    { "RT18 per-line registers", Rt18PerLine },
    { "RT19 clear pixels lift", Rt19ClearLift },
};

int main(void)
{
    int passed = 0, failed = 0;
    size_t i;

    for (i = 0; i < sizeof(sTests) / sizeof(sTests[0]); i++)
    {
        Reset();
        sTestName = sTests[i].name;
        sTestFails = 0;
        sTests[i].fn();
        printf("%-30s %s\n", sTests[i].name, sTestFails == 0 ? "PASS" : "FAIL");
        if (sTestFails == 0)
            passed++;
        else
            failed++;
    }
    printf("passed %d, failed %d\n", passed, failed);
    return failed != 0;
}
