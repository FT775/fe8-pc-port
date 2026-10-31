#include "global.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_hw.h"
#include "bg_line.h"

/*
 * Software renderer, background half: the per-line entry point, the register
 * snapshot and text BGs (port-plan/render-design-bg.md sections 1.2, 3, 5).
 * Affine BG2 is in bg_affine.c.
 *
 * Rules (design 3.4): read live I/O, BG VRAM and PLTT when the line is drawn;
 * never read the shadow buffers; never cache tiles, maps or colours across
 * lines. Windows, blending, OBJs and forced blank belong to the OBJ half and
 * to RenderLine.
 */

#define BG_VRAM_END 0x10000u

static u8 sLayerMask = 0xF;   /* FE8PC_RENDER_LAYERS (design 8.6) */
static u16 sFrameDispcnt;     /* DISPCNT seen at line 0, for the mid-frame warning */
static u32 sLogHits[RENDER_LOG_COUNT];

void RenderLogOnce(int id, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    u32 hits;

    if (id < 0 || id >= RENDER_LOG_COUNT)
        return;
    hits = sLogHits[id];
    if (hits != 0xFFFFFFFFu)
        sLogHits[id] = hits + 1;
    if (hits != 0)
        return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    PortLog("[render] %s (logged once)\n", buf);
}

u32 RenderLogHits(int id)
{
    if (id < 0 || id >= RENDER_LOG_COUNT)
        return 0;
    return sLogHits[id];
}

void RenderSetLayerMask(u8 mask)
{
    sLayerMask = mask & 0xF;
}

u8 RenderGetLayerMask(void)
{
    return sLayerMask;
}

void RenderBgInit(void)
{
    const char *s = getenv("FE8PC_RENDER_LAYERS");

    memset(sLogHits, 0, sizeof(sLogHits));
    sFrameDispcnt = 0;
    sLayerMask = 0xF;
    if (s != NULL && *s != '\0')
    {
        sLayerMask = (u8)(strtoul(s, NULL, 0) & 0xF);
        PortLog("[render] FE8PC_RENDER_LAYERS: drawing BG mask 0x%X only\n", sLayerMask);
    }
    RenderAffineInit();
}

/* Line-0 work (design 4.3 step 4): the affine latch, and the DISPCNT that the
   mid-frame warning compares against. */
void RenderBgBeginFrame(void)
{
    sFrameDispcnt = REG_DISPCNT;
    RenderAffineBeginFrame();
}

void RenderSnapshotRegs(struct RenderLineRegs *regs)
{
    int n;

    regs->dispcnt = REG_DISPCNT;
    for (n = 0; n < 4; n++)
    {
        regs->bgcnt[n] = *(vu16 *)(uintptr_t)(REG_BASE + REG_OFFSET_BG0CNT + 2 * n);
        regs->bghofs[n] = *(vu16 *)(uintptr_t)(REG_BASE + REG_OFFSET_BG0HOFS + 4 * n);
        regs->bgvofs[n] = *(vu16 *)(uintptr_t)(REG_BASE + REG_OFFSET_BG0HOFS + 4 * n + 2);
    }
    regs->bg2pa = REG_BG2PA;
    regs->bg2pb = REG_BG2PB;
    regs->bg2pc = REG_BG2PC;
    regs->bg2pd = REG_BG2PD;
    regs->bg2x = REG_BG2X;
    regs->bg2y = REG_BG2Y;
    regs->win0h = REG_WIN0H;
    regs->win1h = REG_WIN1H;
    regs->win0v = REG_WIN0V;
    regs->win1v = REG_WIN1V;
    regs->winin = REG_WININ;
    regs->winout = REG_WINOUT;
    regs->mosaic = REG_MOSAIC;
    regs->bldcnt = REG_BLDCNT;
    regs->bldalpha = REG_BLDALPHA;
    regs->bldy = *(vu16 *)(uintptr_t)(REG_BASE + REG_OFFSET_BLDY);
}

/* BG2X/BG2Y: bits 0-27 are a signed 19.8 value; bits 28-31 are ignored. */
s32 RenderSext28(u32 value)
{
    return (s32)(value << 4) >> 4;
}

/*
 * Byte offset of the map entry for tile (tx, ty) from the map base, tx and ty
 * 0-63 (design 5.3 step 3). Blocks are 0x800 bytes: 512x256 has blocks 0 and
 * 1 side by side, 256x512 has 0 above 1, 512x512 has 0 1 above 2 3.
 */
static u32 RenderTextMapOffset(u16 bgcnt, int tx, int ty)
{
    int size = bgcnt >> 14;
    u32 block = 0;

    if (tx >= 32)
    {
        block += 1;
        tx -= 32;
    }
    if (ty >= 32)
    {
        block += (size == 3) ? 2 : 1;
        ty -= 32;
    }
    return block * 0x800 + (u32)(ty * 32 + tx) * 2;
}

/* 4bpp: 32 bytes per tile, low nibble is the left pixel. Tile data at or past
   0x10000 is skipped, as mGBA does (design 5.4). Returns the colour index. */
static int RenderFetch4bpp(u32 charBase, int tile, int px, int py)
{
    u32 addr = charBase + (u32)tile * TILE_SIZE_4BPP;
    u8 pair;

    if (addr >= BG_VRAM_END)
    {
        RenderLogOnce(RENDER_LOG_TILE_PAST_VRAM,
            "4bpp BG tile %d at char base 0x%X is past BG VRAM: drawn transparent", tile, charBase);
        return 0;
    }
    pair = RENDER_VRAM8(addr + py * 4 + (px >> 1));
    return (px & 1) ? (pair >> 4) : (pair & 0xF);
}

/* 8bpp: 64 bytes per tile, one byte per pixel. Used by affine BG2 and by the
   (unused) 8bpp text BG. Same 0x10000 rule as 4bpp. */
int RenderFetch8bpp(u32 charBase, int tile, int px, int py)
{
    u32 addr = charBase + (u32)tile * TILE_SIZE_8BPP;

    if (addr >= BG_VRAM_END)
    {
        RenderLogOnce(RENDER_LOG_TILE_PAST_VRAM,
            "8bpp BG tile %d at char base 0x%X is past BG VRAM: drawn transparent", tile, charBase);
        return 0;
    }
    return RENDER_VRAM8(addr + py * 8 + px);
}

/* Text BG n on line `line` (design 5.3). */
static void RenderTextBgLine(int bg, int line, const struct RenderLineRegs *regs, u16 *out)
{
    u16 cnt = regs->bgcnt[bg];
    u32 charBase = ((cnt >> 2) & 3) * 0x4000u;
    u32 mapBase = ((cnt >> 8) & 0x1F) * 0x800u;
    int size = cnt >> 14;
    int widthMask = ((size & 1) ? 512 : 256) - 1;
    int heightMask = ((size & 2) ? 512 : 256) - 1;
    int hofs = regs->bghofs[bg] & 0x1FF;
    int y = (line + (regs->bgvofs[bg] & 0x1FF)) & heightMask;
    int is8bpp = (cnt & BGCNT_256COLOR) != 0;
    int x;

    if (is8bpp)
        RenderLogOnce(RENDER_LOG_TEXT_8BPP, "BG%d is an 8bpp text BG (BG%dCNT 0x%04X): using the 8bpp fetch",
            bg, bg, cnt);

    for (x = 0; x < RENDER_WIDTH; x++)
    {
        int sx = (x + hofs) & widthMask;
        u16 entry = RENDER_VRAM16(mapBase + RenderTextMapOffset(cnt, sx >> 3, y >> 3));
        int tile = entry & 0x3FF;
        int px = sx & 7;
        int py = y & 7;
        int index;

        if (entry & 0x400)
            px = 7 - px;
        if (entry & 0x800)
            py = 7 - py;

        if (!is8bpp)
        {
            index = RenderFetch4bpp(charBase, tile, px, py);
            out[x] = index != 0 ? (RENDER_PLTT((entry >> 12) * 16 + index) & 0x7FFF) : RENDER_TRANSPARENT;
        }
        else
        {
            index = RenderFetch8bpp(charBase, tile, px, py);
            out[x] = index != 0 ? (RENDER_PLTT(index) & 0x7FFF) : RENDER_TRANSPARENT;
        }
    }
}

static void RenderFillTransparent(u16 *out)
{
    int x;

    for (x = 0; x < RENDER_WIDTH; x++)
        out[x] = RENDER_TRANSPARENT;
}

/*
 * FE8PC_MGBA_BGCLIP=1 (gRenderMgbaBgClip; checkpoint compares only, like
 * FE8PC_BLEND=mgba8; RenderInit reads it). The oracle, mGBA 0.10.5, draws each
 * BG once per window segment of the line (video-software.c, _breakWindow and
 * the segment loop of DrawScanline), and its text-BG code for the segment's
 * first, partial tile (software-mode0.c, DRAW_BACKGROUND_MODE_0_TILE_SUFFIX_16,
 * the horizontal-flip branch) puts the tile's pixel 0 at the segment's last
 * pixel. When a segment starts inside a horizontally flipped tile and ends
 * before that tile's last pixel, its pixels come out shifted by the pixels the
 * segment leaves at the tile's end. The hardware draws a BG whatever the
 * windows are, as the PC does by default; this copies the oracle's drawing so
 * a Gate 0 compare does not report it (round 8's minimap window tips;
 * DIVERGENCES section 5). Checked against mGBA with the PPU test ROM
 * (port/test/oracle/pputest, variant 2; README.md, "PPU test ROM") and
 * test_bg.c's mGBA BG clip case. 8bpp text BGs and mosaic, which take other
 * mGBA paths, are left as they are (both are one-time warnings above).
 */
int gRenderMgbaBgClip;

struct RenderMgbaWindow
{
    int endX;
    u8 control;
};

#define RENDER_MGBA_MAX_WINDOWS 16

/* mGBA's _breakWindowInner, as it is: insert [start, end) with `control` into the segment list. */
static void RenderMgbaBreakInner(struct RenderMgbaWindow *w, int *n, int start, int end, u8 control)
{
    int active;
    int startX = 0;

    if (end <= 0)
        return;
    for (active = 0; active < *n; ++active)
    {
        if (start < w[active].endX)
        {
            struct RenderMgbaWindow old = w[active];
            int next;

            if (start > startX)
            {
                next = (*n)++;
                for (; next > active; --next)
                    w[next] = w[next - 1];
                w[active].endX = start;
                ++active;
            }
            w[active].control = control;
            w[active].endX = end;
            if (end >= old.endX)
            {
                for (++active; *n > active + 1 && end >= w[active].endX; ++active)
                {
                    w[active] = w[active + 1];
                    --*n;
                }
            }
            else
            {
                ++active;
                next = (*n)++;
                for (; next > active; --next)
                    w[next] = w[next - 1];
                w[active] = old;
            }
            break;
        }
        startX = w[active].endX;
    }
}

/* mGBA's WINnH write (the clamp port/render/window.c follows) and _breakWindow. */
static void RenderMgbaBreak(struct RenderMgbaWindow *w, int *n, u16 winh, u8 control)
{
    int end = winh & 0xFF;
    int start = winh >> 8;

    if (start > RENDER_WIDTH && start > end)
        start = 0;
    if (end > RENDER_WIDTH)
    {
        end = RENDER_WIDTH;
        if (start > RENDER_WIDTH)
            start = RENDER_WIDTH;
    }
    if (end < start)
    {
        RenderMgbaBreakInner(w, n, 0, end, control);
        RenderMgbaBreakInner(w, n, start, RENDER_WIDTH, control);
    }
    else
        RenderMgbaBreakInner(w, n, start, end, control);
}

/* Vertical: as port/render/window.c (mGBA 0.10.3). */
static int RenderMgbaWinOn(u16 winv, int line)
{
    int bottom = winv & 0xFF;
    int top = winv >> 8;

    if (bottom >= top)
        return line >= top && line < bottom;
    return line >= top || line < bottom;
}

static void RenderMgbaBgClip(int line, const struct RenderLineRegs *regs, struct RenderBgLine *out)
{
    struct RenderMgbaWindow w[RENDER_MGBA_MAX_WINDOWS];
    u16 dispcnt = regs->dispcnt;
    int nWin = 1;
    int objwin = (dispcnt & 0x8000) != 0;
    int bg;

    if (!(dispcnt & 0xE000))
        return; /* one segment, the whole line: nothing is cut */
    w[0].endX = RENDER_WIDTH;
    w[0].control = (u8)(regs->winout & 0xFF);
    if ((dispcnt & 0x4000) && RenderMgbaWinOn(regs->win1v, line))
        RenderMgbaBreak(w, &nWin, regs->win1h, (u8)(regs->winin >> 8));
    if ((dispcnt & 0x2000) && RenderMgbaWinOn(regs->win0v, line))
        RenderMgbaBreak(w, &nWin, regs->win0h, (u8)(regs->winin & 0xFF));

    for (bg = 0; bg < 4; bg++)
    {
        u16 cnt = regs->bgcnt[bg];
        u32 charBase = ((cnt >> 2) & 3) * 0x4000u;
        u32 mapBase = ((cnt >> 8) & 0x1F) * 0x800u;
        int size = cnt >> 14;
        int widthMask = ((size & 1) ? 512 : 256) - 1;
        int heightMask = ((size & 2) ? 512 : 256) - 1;
        int hofs = regs->bghofs[bg] & 0x1FF;
        int y = (line + (regs->bgvofs[bg] & 0x1FF)) & heightMask;
        int k, s, e = 0;

        if (!out->on[bg] || ((dispcnt & 7) == 1 && bg == 2) || (cnt & BGCNT_256COLOR)
            || (regs->mosaic != 0 && (cnt & BGCNT_MOSAIC)))
            continue;
        for (k = 0; k < nWin; k++)
        {
            int sx, mod8, x;
            u16 entry;

            s = e;
            e = w[k].endX;
            if (e <= s)
                continue;
            if (!((w[k].control >> bg) & 1) && !(objwin && ((regs->winout >> (8 + bg)) & 1)))
                continue; /* mGBA does not draw this BG in this segment */
            sx = (s + hofs) & widthMask;
            mod8 = sx & 7;
            if (mod8 == 0 || s + 8 - mod8 <= e)
                continue; /* the first tile reaches its end: drawn right */
            entry = RENDER_VRAM16(mapBase + RenderTextMapOffset(cnt, sx >> 3, y >> 3));
            if (!(entry & 0x400))
                continue;
            for (x = s; x < e; x++)
            {
                int py = (entry & 0x800) ? 7 - (y & 7) : (y & 7);
                int index = RenderFetch4bpp(charBase, entry & 0x3FF, e - 1 - x, py);

                out->color[bg][x] = index != 0 ? (RENDER_PLTT((entry >> 12) * 16 + index) & 0x7FFF) : RENDER_TRANSPARENT;
            }
        }
    }
}

/*
 * The BG half for one visible line (design 1.2, 5.1, 6.3). The caller has
 * taken the snapshot and handled forced blank. Order: affine reload check
 * (lines 1-159), draw, affine step. A BG that is not drawn gets an all
 * transparent row, so the OBJ half may read every row.
 */
void RenderBgLine(int line, const struct RenderLineRegs *regs, struct RenderBgLine *out)
{
    u16 dispcnt = regs->dispcnt;
    int mode = dispcnt & 7;
    int n;

    if (line > 0 && dispcnt != sFrameDispcnt)
    {
        RenderLogOnce(RENDER_LOG_DISPCNT_CHANGE,
            "DISPCNT changed at line %d (0x%04X -> 0x%04X); the BG enable delay is not modelled", line,
            sFrameDispcnt, dispcnt);
        sFrameDispcnt = dispcnt;
    }
    if (line > 0)
        RenderAffineCheckReload(regs);

    out->backdrop = RENDER_PLTT(0) & 0x7FFF;
    for (n = 0; n < 4; n++)
    {
        out->prio[n] = regs->bgcnt[n] & 3;
        out->on[n] = 0;
    }

    switch (mode)
    {
    case 0:
        for (n = 0; n < 4; n++)
            out->on[n] = (dispcnt >> (8 + n)) & 1;
        break;
    case 1:
        for (n = 0; n < 3; n++) /* BG3 is never drawn in mode 1 */
            out->on[n] = (dispcnt >> (8 + n)) & 1;
        break;
    default:
        RenderLogOnce(RENDER_LOG_MODE_UNUSED, "DISPCNT mode %d at line %d: no BGs drawn", mode, line);
        break;
    }

    for (n = 0; n < 4; n++)
    {
        if (out->on[n] && !((sLayerMask >> n) & 1))
            out->on[n] = 0;
        if (out->on[n] && regs->mosaic != 0 && (regs->bgcnt[n] & BGCNT_MOSAIC))
            RenderLogOnce(RENDER_LOG_MOSAIC, "MOSAIC 0x%04X with BG%d mosaic on at line %d: mosaic ignored",
                regs->mosaic, n, line);
    }

    for (n = 0; n < 4; n++)
    {
        if (!out->on[n])
            RenderFillTransparent(out->color[n]);
        else if (mode == 1 && n == 2)
            RenderAffineBgLine(regs, out->color[n]);
        else
            RenderTextBgLine(n, line, regs, out->color[n]);
    }
    if (gRenderMgbaBgClip)
        RenderMgbaBgClip(line, regs, out);

    RenderAffineStep(regs);
}
