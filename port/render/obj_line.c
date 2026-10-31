#include <stdio.h>
#include <string.h>

#include "port_hw.h"
#include "obj_line.h"

/*
 * Software renderer, OBJ unit and the per-line entry point of the sprite and
 * effects half (port-plan/render-design-obj.md sections 3, 4, 6 and 10).
 *
 * Rules: read OAM, OBJ VRAM and PLTT when the line is drawn; never cache;
 * decode whatever bits are in OAM and never "correct" them (design 4.1).
 * Where GBATEK and mGBA disagree, follow mGBA, the checkpoint oracle. The
 * mGBA facts used here were read from mGBA 0.10.3 software-obj.c and
 * video-software.c on 2026-10-19:
 *   - affine: xAccum = pa * (inX - totalWidth/2) + pb * (inY - totalHeight/2)
 *     + (width << 7), localX = xAccum >> 8 (same for y with pc, pd);
 *   - 4bpp address: 2D wraps the tile column inside the 1 KB row that holds
 *     the base tile (mask 0x3FE), and every address wraps inside 32 KB;
 *   - cycle budget: a sprite on the line is drawn, then its cost is taken off,
 *     then the loop stops once the budget is <= 0. Cost = width, or
 *     10 + 2 * box width for an affine sprite (GBAVideoRendererCleanOAM).
 */

int gRenderObjCycleLimit = 1;
void (*gRenderFxTripHook)(int id, int line, uint32_t detail);

static uint32_t sTripCount[RENDER_TRIP_COUNT];
static uint32_t sPeakCycles;

/* [shape][size] = { width, height } in pixels (include/ctc.h:24-52, GBATEK). */
static const uint8_t sObjSizes[3][4][2] = {
    { { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } }, /* square */
    { { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } }, /* wide */
    { { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } }, /* tall */
};

/* One OAM entry, decoded for one line (design 4.1-4.2). */
struct ObjInfo
{
    int index;
    int x;    /* attr1 bits 0-8, sign-extended */
    int row;  /* row inside the box on this line */
    int w;    /* sprite size */
    int h;
    int boxW; /* drawn box: 2W x 2H for affine double size */
    int boxH;
    int affine;
    int mode; /* 0 normal, 1 semi-transparent, 2 OBJ window */
    int hflip;
    int vflip;
    int group; /* affine parameter group, attr1 bits 9-13 */
    uint32_t tile;
    int prio;
    int bank;
    int draw; /* 0: on the line but not drawn (8bpp, mode 3); it still costs cycles */
};

static inline unsigned Read16(const uint8_t *p, unsigned off)
{
    return (unsigned)p[off] | ((unsigned)p[off + 1] << 8);
}

#if PORT_RFO_MAPOVERLAY
/*
 * Reforged Options "HP bars & warnings" (port/reforged/reforged_hpbars.c): the
 * played builds only. port/Makefile defines PORT_RFO_MAPOVERLAY for this
 * object on the PORT_PLAYED_BUILD condition, so every test build (and the
 * renderer's unit tests) compiles this file without any of it.
 *
 * gReforgedOverlay lists map sprites by their OAM words, each with a 16x16
 * picture of its unit's square. On a line, an entry drawn whose three words
 * equal an owner's is that unit's sprite (RenderObjLine notes it); after all
 * 128 entries, its picture's row is drawn where that entry's own colour won
 * or where no entry left a colour, never over another entry's pixel, with
 * the entry's priority and mode. Nothing is read from or written to GBA
 * memory for it; OAM, VRAM and the palettes are only read as before.
 */
#include "port_reforged.h"

#define OVL_MATCH_MAX 128
struct OvlMatch
{
    int owner;
    int index; /* the OAM entry */
    int x;     /* its box's x on screen */
    int row;   /* the line's row inside its box */
    int prio;
    int semi;
};
static uint8_t sPixelEntry[RENDER_WIDTH]; /* the entry whose colour is at x (where has[x]) */
static struct OvlMatch sOvlMatch[OVL_MATCH_MAX];
static int sOvlMatchCount;

static int OvlActive(void)
{
    return gReforgedOverlay.count > 0 && (uint32_t)(gPortFrameCount - gReforgedOverlay.frame) <= 2;
}

static void OvlNote(const uint8_t *oam, const struct ObjInfo *o)
{
    unsigned a0 = Read16(oam, (unsigned)o->index * 8);
    unsigned a1 = Read16(oam, (unsigned)o->index * 8 + 2);
    unsigned a2 = Read16(oam, (unsigned)o->index * 8 + 4);
    int k;

    if (sOvlMatchCount >= OVL_MATCH_MAX)
        return;
    for (k = 0; k < gReforgedOverlay.count; k++)
    {
        const struct ReforgedOverlayOwner *ow = &gReforgedOverlay.owner[k];

        if (ow->a2 == a2 && ow->a1 == a1 && ow->a0 == a0)
        {
            struct OvlMatch *m = &sOvlMatch[sOvlMatchCount++];

            m->owner = k;
            m->index = o->index;
            m->x = o->x;
            m->row = o->row;
            m->prio = o->prio;
            m->semi = o->mode == 1;
            return;
        }
    }
}

static void OvlDraw(struct RenderObjLine *out)
{
    int i;

    for (i = 0; i < sOvlMatchCount; i++)
    {
        const struct OvlMatch *m = &sOvlMatch[i];
        const struct ReforgedOverlayOwner *ow = &gReforgedOverlay.owner[m->owner];
        int uy = m->row - ow->dy, ux;

        if (uy < 0 || uy >= 16)
            continue;
        for (ux = 0; ux < 16; ux++)
        {
            unsigned pix = ow->pix[uy][ux];
            int sx = m->x + ow->dx + ux;

            if (!(pix & RFO_OVERLAY_OPAQUE) || sx < 0 || sx >= RENDER_WIDTH)
                continue;
            if (out->has[sx] && sPixelEntry[sx] != m->index)
                continue; /* another sprite's pixel: it stays */
            out->color[sx] = (uint16_t)(pix & 0x7FFF);
            if (!out->has[sx])
            {
                out->prio[sx] = (uint8_t)m->prio;
                out->semi[sx] = (uint8_t)m->semi;
                out->has[sx] = 1;
            }
        }
    }
}
#endif /* PORT_RFO_MAPOVERLAY */

void RenderFxTrip(int id, int line, uint32_t detail)
{
    uint32_t hits;

    if (id <= RENDER_TRIP_NONE || id >= RENDER_TRIP_COUNT)
        return;
    hits = sTripCount[id];
    if (hits != 0xFFFFFFFFu)
        sTripCount[id] = hits + 1;
    if (hits != 0)
        return;
    if (gRenderFxTripHook != NULL)
        gRenderFxTripHook(id, line, detail);
    else
        fprintf(stderr, "[render] tripwire T%d first hit: line %d, detail 0x%X\n", id, line,
            (unsigned)detail);
}

uint32_t RenderFxTripCount(int id)
{
    if (id <= RENDER_TRIP_NONE || id >= RENDER_TRIP_COUNT)
        return 0;
    return sTripCount[id];
}

void RenderFxTripReset(void)
{
    memset(sTripCount, 0, sizeof(sTripCount));
}

uint32_t RenderObjTakePeakCycles(void)
{
    uint32_t peak = sPeakCycles;

    sPeakCycles = 0;
    return peak;
}

void RenderMemLive(struct RenderMem *mem)
{
    mem->io = (const uint8_t *)(uintptr_t)PORT_IO_START;
    mem->pltt = (const uint8_t *)(uintptr_t)PORT_PLTT_START;
    mem->vram = (const uint8_t *)(uintptr_t)PORT_VRAM_START;
    mem->oam = (const uint8_t *)(uintptr_t)PORT_OAM_START;
}

void RenderLatchFxRegs(const uint8_t *io, struct RenderFxRegs *fx)
{
    fx->dispcnt = (uint16_t)Read16(io, 0x00);
    fx->win0h = (uint16_t)Read16(io, 0x40);
    fx->win1h = (uint16_t)Read16(io, 0x42);
    fx->win0v = (uint16_t)Read16(io, 0x44);
    fx->win1v = (uint16_t)Read16(io, 0x46);
    fx->winin = (uint16_t)Read16(io, 0x48);
    fx->winout = (uint16_t)Read16(io, 0x4A);
    fx->mosaic = (uint16_t)Read16(io, 0x4C);
    fx->bldcnt = (uint16_t)Read16(io, 0x50);
    fx->bldalpha = (uint16_t)Read16(io, 0x52);
    fx->bldy = (uint16_t)Read16(io, 0x54);
}

/* Decode entry `index` and say whether it touches `line` (design 4.1-4.2). */
static int ObjDecode(const uint8_t *oam, int index, int line, struct ObjInfo *o)
{
    unsigned a0 = Read16(oam, (unsigned)index * 8);
    unsigned a1 = Read16(oam, (unsigned)index * 8 + 2);
    unsigned a2 = Read16(oam, (unsigned)index * 8 + 4);
    int shape = (int)(a0 >> 14);
    int size = (int)(a1 >> 14);

    o->affine = (a0 >> 8) & 1;
    if (!o->affine && (a0 & 0x200))
        return 0; /* attr0 bit 9 without affine: disabled */

    if (shape == 3)
    {
        RenderFxTrip(RENDER_TRIP_OBJ_SHAPE3, line, (uint32_t)index);
        return 0;
    }

    o->index = index;
    o->w = sObjSizes[shape][size][0];
    o->h = sObjSizes[shape][size][1];
    o->boxW = o->w;
    o->boxH = o->h;
    if (o->affine && (a0 & 0x200))
    {
        o->boxW *= 2;
        o->boxH *= 2;
    }

    /* Equals mGBA's "Y + height - 256" rule for every box height up to 128. */
    o->row = (line - (int)(a0 & 0xFF)) & 0xFF;
    if (o->row >= o->boxH)
        return 0;

    o->mode = (int)((a0 >> 10) & 3);
    o->x = (int)(a1 & 0x1FF);
    if (o->x >= 256)
        o->x -= 512;
    o->group = (int)((a1 >> 9) & 0x1F);
    o->hflip = !o->affine && (a1 & 0x1000);
    o->vflip = !o->affine && (a1 & 0x2000);
    o->tile = a2 & 0x3FF;
    o->prio = (int)((a2 >> 10) & 3);
    o->bank = (int)(a2 >> 12);
    o->draw = 1;

    if (o->mode == 3)
    {
        RenderFxTrip(RENDER_TRIP_OBJ_MODE3, line, (uint32_t)index);
        o->draw = 0;
    }
    if (a0 & 0x2000)
    {
        RenderFxTrip(RENDER_TRIP_OBJ_8BPP, line, (uint32_t)index);
        o->draw = 0;
    }
    return 1;
}

/* OBJ cycles for one entry on one line (GBATEK; mGBA GBAVideoRendererCleanOAM). */
static int ObjCycleCost(const struct ObjInfo *o)
{
    if (o->affine)
        return 10 + 2 * o->boxW;
    return o->w;
}

/* 4bpp colour index of sprite pixel (tx, ty), 0 = transparent (design 4.5). */
static int ObjFetchIndex4(const uint8_t *objVram, const struct ObjInfo *o, int tx, int ty, int map1d,
    int line)
{
    uint32_t charBase = o->tile * 32;
    uint32_t addr;
    unsigned byte;

    if (map1d)
    {
        addr = charBase + (uint32_t)((ty >> 3) * (o->w >> 3) + (tx >> 3)) * 32;
    }
    else
    {
        uint32_t col = (charBase & 0x3E0) + (uint32_t)(tx >> 3) * 32;

        if (col >= 0x400)
            RenderFxTrip(RENDER_TRIP_OBJ_WRAP, line, (uint32_t)o->index);
        addr = (charBase & 0x7C00) + (uint32_t)(ty >> 3) * 0x400 + (col & 0x3E0);
    }
    addr += (uint32_t)(ty & 7) * 4 + (uint32_t)((tx & 7) >> 1);
    if (addr >= RENDER_OBJ_VRAM_SIZE)
    {
        RenderFxTrip(RENDER_TRIP_OBJ_WRAP, line, (uint32_t)o->index | 0x100);
        addr &= RENDER_OBJ_VRAM_SIZE - 1;
    }
    byte = objVram[addr];
    return (tx & 1) ? (int)(byte >> 4) : (int)(byte & 0xF);
}

/*
 * Winner rule and OBJ window mask for one pixel of the sprite's W x H source
 * (design 4.6); index 0 is a transparent pixel. Entries are drawn in OAM order.
 * A colour entry's transparent pixel is not a no-op: where an earlier entry
 * left a pixel whose priority value is higher than this entry's, that pixel
 * keeps its colour but takes this entry's priority and its semi-transparent
 * mode (mGBA 0.10.5 software-obj.c, SPRITE_DRAW_PIXEL_16_NORMAL: the
 * FLAG_UNWRITTEN branch copies FLAG_ORDER_MASK, FLAG_REBLEND and FLAG_TARGET_1).
 * So a later high-priority sprite's empty pixels lift an earlier low-priority
 * sprite above the BGs and above later sprites of the same priority. An OBJ
 * window entry lifts nothing. Checked on the oracle with the PPU test ROM
 * (port/test/oracle/pputest, variants 0 and 1; README.md, "PPU test ROM") and
 * test_obj.c RT19; FE8 shows it in the preparations item screens (cov9 group
 * G8).
 */
static void ObjPlot(struct RenderObjLine *out, const struct ObjInfo *o, const uint8_t *pltt, int x,
    int index)
{
    if (o->mode == 2)
    {
        if (index != 0)
            out->win[x] = 1;
        return;
    }
    /* Equal priority keeps the earlier entry (mGBA: stored order > new flags). */
    if (out->has[x] && out->prio[x] <= o->prio)
        return;
    if (index == 0)
    {
        if (out->has[x])
        {
            out->prio[x] = (uint8_t)o->prio;
            out->semi[x] = (uint8_t)(o->mode == 1);
        }
        return;
    }
    out->color[x] = (uint16_t)(Read16(pltt, RENDER_OBJ_PLTT_OFFSET + (unsigned)(o->bank * 16 + index) * 2)
        & 0x7FFF);
    out->prio[x] = (uint8_t)o->prio;
    out->has[x] = 1;
    out->semi[x] = (uint8_t)(o->mode == 1);
#if PORT_RFO_MAPOVERLAY
    sPixelEntry[x] = (uint8_t)o->index; /* HP bars & warnings (above) */
#endif
}

static void ObjDrawNormal(const struct ObjInfo *o, const struct RenderMem *mem, int map1d, int line,
    struct RenderObjLine *out)
{
    const uint8_t *objVram = mem->vram + RENDER_OBJ_VRAM_OFFSET;
    int sx0 = o->x < 0 ? 0 : o->x;
    int sx1 = o->x + o->w > RENDER_WIDTH ? RENDER_WIDTH : o->x + o->w;
    int ty = o->vflip ? o->h - 1 - o->row : o->row;
    int sx;

    for (sx = sx0; sx < sx1; sx++)
    {
        int tx = sx - o->x;
        int index;

        if (o->hflip)
            tx = o->w - 1 - tx;
        index = ObjFetchIndex4(objVram, o, tx, ty, map1d, line);
        /* A clear pixel over nothing changes nothing (ObjPlot): skip the call. */
        if (index != 0 || out->has[sx])
            ObjPlot(out, o, mem->pltt, sx, index);
    }
}

static void ObjDrawAffine(const struct ObjInfo *o, const struct RenderMem *mem, int map1d, int line,
    struct RenderObjLine *out)
{
    const uint8_t *objVram = mem->vram + RENDER_OBJ_VRAM_OFFSET;
    unsigned base = (unsigned)o->group * 32; /* attr3 of entries 4g..4g+3 */
    int pa = (int16_t)Read16(mem->oam, base + 6);
    int pb = (int16_t)Read16(mem->oam, base + 14);
    int pc = (int16_t)Read16(mem->oam, base + 22);
    int pd = (int16_t)Read16(mem->oam, base + 30);
    int cy = o->row - (o->boxH >> 1);
    int sx0 = o->x < 0 ? 0 : o->x;
    int sx1 = o->x + o->boxW > RENDER_WIDTH ? RENDER_WIDTH : o->x + o->boxW;
    int sx;

    for (sx = sx0; sx < sx1; sx++)
    {
        int cx = sx - o->x - (o->boxW >> 1);
        int tx = (pa * cx + pb * cy + (o->w << 7)) >> 8; /* arithmetic (flooring) shift */
        int ty = (pc * cx + pd * cy + (o->h << 7)) >> 8;
        int index;

        /* Clipped to W x H even when the box is bigger (design 4.4). */
        if (tx < 0 || tx >= o->w || ty < 0 || ty >= o->h)
            continue;
        index = ObjFetchIndex4(objVram, o, tx, ty, map1d, line);
        if (index != 0 || out->has[sx]) /* as in ObjDrawNormal */
            ObjPlot(out, o, mem->pltt, sx, index);
    }
}

void RenderObjLine(int line, uint16_t dispcnt, const struct RenderMem *mem, struct RenderObjLine *out)
{
    struct ObjInfo o;
    int map1d = (dispcnt >> 6) & 1;
    int remaining = (dispcnt & 0x20) ? RENDER_OBJ_CYCLES_HBFREE : RENDER_OBJ_CYCLES;
    int dropped = 0;
    int i;

    memset(out->has, 0, sizeof(out->has));
    memset(out->semi, 0, sizeof(out->semi));
    memset(out->win, 0, sizeof(out->win));
    out->cycles = 0;

    if (!(dispcnt & 0x1000))
        return; /* OBJ layer off: no colour and no OBJ window */

#if PORT_RFO_MAPOVERLAY
    sOvlMatchCount = 0;
#endif
    for (i = 0; i < 128; i++)
    {
        int cost;

        if (!ObjDecode(mem->oam, i, line, &o))
            continue;
        cost = ObjCycleCost(&o);
        out->cycles += cost; /* demand, counted past the budget too, for the peak log */
        if (gRenderObjCycleLimit && remaining <= 0)
        {
            if (!dropped)
                RenderFxTrip(RENDER_TRIP_OBJ_CYCLES, line, (uint32_t)i);
            dropped = 1;
            continue;
        }
        if (o.draw)
        {
            if (o.affine)
                ObjDrawAffine(&o, mem, map1d, line, out);
            else
                ObjDrawNormal(&o, mem, map1d, line, out);
#if PORT_RFO_MAPOVERLAY
            if (!o.affine && o.mode != 2 && OvlActive())
                OvlNote(mem->oam, &o);
#endif
        }
        remaining -= cost;
    }
#if PORT_RFO_MAPOVERLAY
    if (sOvlMatchCount != 0)
        OvlDraw(out);
#endif
    if ((uint32_t)out->cycles > sPeakCycles)
        sPeakCycles = (uint32_t)out->cycles;
}

/* Line-level checks that report legal-but-unexpected register values (design 10). */
static void FxLineTrips(int line, const struct RenderFxRegs *fx)
{
    int mode = fx->dispcnt & 7;

    if (fx->mosaic != 0)
        RenderFxTrip(RENDER_TRIP_MOSAIC, line, fx->mosaic);
    if (mode > 1 || (fx->dispcnt & 0x38))
        RenderFxTrip(RENDER_TRIP_DISPCNT, line, fx->dispcnt);
    if ((fx->bldalpha & 0x1F) > 16 || ((fx->bldalpha >> 8) & 0x1F) > 16 || (fx->bldy & 0x1F) > 16)
        RenderFxTrip(RENDER_TRIP_EV, line, ((uint32_t)fx->bldalpha << 16) | fx->bldy);
}

void RenderObjFxLine(int line, const struct RenderFxRegs *fx, const struct RenderMem *mem,
    const struct RenderComposeBg *bg, uint16_t *dst)
{
    struct RenderFxRegs latched;
    struct RenderObjLine obj;
    uint8_t mask[RENDER_WIDTH];
    uint16_t backdrop;
    int x;

    if (fx == NULL)
    {
        RenderLatchFxRegs(mem->io, &latched);
        fx = &latched;
    }

    /* Forced blank (design 8): white, and nothing else runs. */
    if (fx->dispcnt & 0x80)
    {
        for (x = 0; x < RENDER_WIDTH; x++)
            dst[x] = RENDER_WHITE;
        return;
    }

    FxLineTrips(line, fx);
    backdrop = (uint16_t)(Read16(mem->pltt, 0) & 0x7FFF);
    RenderObjLine(line, fx->dispcnt, mem, &obj);
    RenderWindowLine(line, fx, obj.win, mask);
    RenderComposeLine(line, fx, bg, backdrop, &obj, mask, dst);
}
