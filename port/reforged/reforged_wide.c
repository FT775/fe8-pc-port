#include "global.h"

/*
 * Widescreen on the battle map (v1.26.0, milestone M1 of ~/fe8-notes/widescreen-design.txt; the Display
 * screen's "Widescreen" row, render.h gRenderWideOn). Played builds only; empty in every test build. The
 * game side: what the renderer (port/render/wide.c) needs to draw the 22-pixel sides of the 284x160 picture
 * on the battle map, gathered in host memory only. Nothing here writes GBA memory (EWRAM, IWRAM, VRAM, OAM,
 * I/O or gBmSt): plain reads, so the game, its saves and the 240x160 frame are the same with the row on or
 * off (port/tools/widecheck.sh proves it on the identity routes).
 *
 * - PortWide_PushObj (src/bmudisp.c, before each 240-pixel cull of the standing unit sprites and their
 *   icons): a push whose sprite reaches into a side (x < 0 or x + width > 240 within the sides' reach) is
 *   kept in a pending list, in push order, whole x and y (OAM's 9-bit x would alias past 256).
 * - PortWide_VBlankLatch (src/bm.c OnVBlank, right after SyncHiOam): the pending list becomes the shown one,
 *   as SyncHiOam makes the pushed OAM entries the shown ones, with the camera, the BG3 ring's origin and the
 *   BG3 words of the 32 x 16 squares around the camera (src/bmmap.c PortWide_BmTileWords, inside the map
 *   only; squares outside it are marked and drawn black). The frame drawn next shows exactly what this
 *   flush put in VRAM and OAM (port/hw/frame.c: lines 0-159 come after the VBlank at line 160).
 *   ok is set while the battle map is up: its main proc exists (bm.c gProc_BMapMain), the map is at least
 *   240x160 and the camera inside it (v1.28.0: or up to a square past its edge); suspended says a full-screen screen or a battle start suspended it
 *   (gameGfxSemaphore, BMapDispSuspend: v1.28.0, the renderer puts the strip in the middle and draws the map's sides
 *   at that x while BG3 still holds this map). Everything about VRAM (BG3 really holding this map at this origin, BG2, the mode) is
 *   checked by the renderer at the start of the frame it draws.
 * - off, where the game's x = 0 sits in the wide picture (design 2.1, variant A): the picture's left edge
 *   is the map pixel wl = clamp(camX - M, 0, mapW*16 - W), or (mapW*16 - W) / 2 (the map centred, black
 *   sides) when the map is narrower than W; off = camX - wl, 0 .. 2M. So at a map edge the open side shows
 *   more map instead of black, and the game's 240 strip (menus included) moves up to M off the middle.
 *
 * v1.27.0 (milestone M2, "the player and enemy phases look right in the sides"):
 * - while the range layer is up (BM_FLAG_0: move and attack ranges, the danger zone, which fills the same
 *   maps, port/reforged/reforged_dangerzone.c), the latch also keeps each square's class by
 *   DisplayMovementViewTile's rule from gMapMovementSigned / gMapRangeSigned, inside the map only
 *   (rangeClass); the renderer draws BG2 in the sides from it when VRAM's range squares on screen are of the
 *   same classes (the exact words of an attack square depend on what the square held before);
 * - more pushes into the side list: the movement arrow (src/bmpatharrowdisp.c DrawPathArrow) and the moving
 *   units (src/mu.c PutMu / PutMuSMS: the AP handle's current frame, pushed here before the cull, never
 *   through OAM, the AP never run; PortWide_PushObjEx keeps a dying unit's blend bit);
 * - PortWide_PushHpBar (port/reforged/reforged_hpbars.c): the HP bar / "!" picture of a unit whose square
 *   reaches into a side, matched at the latch with its sprite's push (its owner), drawn by the renderer as
 *   obj_line.c draws the middle's.
 *
 * v1.28.0 (milestone M3, polish):
 * - PortWide_PushPhaseBand (src/phasechangefx.c): the phase banner's band sprite is 240 pixels of affine
 *   sprites; copies of its outermost 8-pixel columns (8x32 sprites on the band's own tiles and affine group,
 *   OBJ VRAM being 2D-mapped: a column of tiles 32 apart) are pushed across both sides, so the band's lines
 *   run on into them as they open and close;
 * - PortWide_NoteMu (src/mu.c): counts of moving units pushed / hidden by the fog at a side, for wide.log;
 * - weather: no retail chapter sets a weather of particle sprites (no chapter's initialWeather, no event's
 *   weather command: only the debug menu's), so the particles are not drawn in the sides; should one be set
 *   (snow, snowstorm, rain, flames, sandstorm, clouds) the map is shown with black sides instead
 *   (PORT_WIDE_NO_WEATHER), never with particles that stop at the strip's edges. Night (a palette tint) is
 *   drawn as is: the sides use the same palettes.
 *
 * Session data (PORT_STATE_SESSION): rebuilt at every VBlank, never game state.
 */

#if PORT_PLAYED_BUILD

#include <string.h>

#include "proc.h"
#include "bm.h"
#include "bmmap.h"

#include "port_hw.h"
#include "port_reforged.h"
#include "port_state.h"
#include "../render/render.h"

#define WIDE_M RENDER_WIDE_MARGIN

struct PortWideLatch gPortWide;

static struct
{
    int count;
    unsigned frame; /* gPortFrameCount of the pushes held: a later frame's first push drops them, so pushes
                       no VBlank latched (main_loop_ended 0) never come back as ghosts in the sides */
    struct PortWideObj obj[PORT_WIDE_OBJ_MAX];
    int muCount, muFogCount; /* v1.28.0: PortWide_NoteMu's counts for this frame */
} sPending;

/* the pending list belongs to this frame (a later frame's first push or note drops what an unlatched one left) */
static void PendingFrame(void)
{
    if (sPending.frame != gPortFrameCount)
    {
        sPending.frame = gPortFrameCount;
        sPending.count = 0;
        sPending.muCount = 0;
        sPending.muFogCount = 0;
    }
}

/* v1.27.0: the HP bar pictures of this OnMain's units reaching into a side (same frame rule) */
static struct
{
    int count;
    unsigned frame;
    struct PortWideHp hp[PORT_WIDE_HP_MAX];
} sPendingHp;

/* [shape][size] = { width, height } (as port/render/obj_line.c). */
static const u8 sObjSize[3][4][2] = {
    { { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } },
    { { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } },
    { { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } },
};

int PortWide_On(void)
{
    return gRenderWideOn;
}

int PortWide_ReachesSide(int x, int w)
{
    if (!gRenderWideOn)
        return 0;
    return (x < 0 && x + w > -2 * WIDE_M) || (x + w > DISPLAY_WIDTH && x < DISPLAY_WIDTH + 2 * WIDE_M);
}

void PortWide_PushObj(int x, int y, const unsigned short *list, int oam2)
{
    PortWide_PushObjEx(x, y, list, oam2, 0, 0);
}

void PortWide_PushHpBar(int sqx, int sqy, int ox, int oy, const unsigned short *list, int oam2,
    const unsigned short pix[16][16])
{
    struct PortWideHp *h;

    if (!gRenderWideOn || list == NULL)
        return;
    if (sPendingHp.frame != gPortFrameCount)
    {
        sPendingHp.frame = gPortFrameCount;
        sPendingHp.count = 0;
    }
    if (sPendingHp.count >= PORT_WIDE_HP_MAX)
        return;
    h = &sPendingHp.hp[sPendingHp.count++];
    h->sqx = (short)sqx;
    h->sqy = (short)sqy;
    h->ox = (short)ox;
    h->oy = (short)oy;
    h->oam2 = (unsigned short)oam2;
    h->owner = -1;
    h->list = list;
    memcpy(h->pix, pix, sizeof(h->pix));
}

void PortWide_NoteMu(int kind, int kept)
{
    if (!gRenderWideOn)
        return;
    PendingFrame();
    if (kind == 0)
        sPending.muCount += kept != 0;
    else
        sPending.muFogCount++;
}

/* v1.28.0: the band's outer columns (src/phasechangefx.c obj_0859AEC8: 64+64+64+32+16 pixels of 64x32 /
   32x32 / 16x32 affine sprites on tiles 0x00.. of a 2D-mapped row, affine group 0): 8x32 copies of its first
   column (tile 0x00) at x -8 .. -48 and of its last (tile 0x1D) at 240 .. 280 (pushed at x 240: OAM's 9-bit
   x would read 256 and on as negative), the sides' widest reach (2 x 22) */
static const unsigned short sPhaseBandLeft[] = {
    6,
    0x8100, 0x41F8, 0x00, 0x8100, 0x41F0, 0x00, 0x8100, 0x41E8, 0x00,
    0x8100, 0x41E0, 0x00, 0x8100, 0x41D8, 0x00, 0x8100, 0x41D0, 0x00,
};
static const unsigned short sPhaseBandRight[] = {
    6,
    0x8100, 0x4000, 0x1D, 0x8100, 0x4008, 0x1D, 0x8100, 0x4010, 0x1D,
    0x8100, 0x4018, 0x1D, 0x8100, 0x4020, 0x1D, 0x8100, 0x4028, 0x1D,
};

void PortWide_PushPhaseBand(int y, int oam2)
{
    PortWide_PushObjEx(0, y, sPhaseBandLeft, oam2, 0, 0);
    PortWide_PushObjEx(DISPLAY_WIDTH, y, sPhaseBandRight, oam2, 0, 0);
}

int PortWide_PushObjEx(int x, int y, const unsigned short *list, int oam2, int a0or, int sideOnly)
{
    const unsigned short *it;
    int count, keep = 0;

    if (!gRenderWideOn || list == NULL)
        return 0;
    PendingFrame();
    if (sPending.count >= PORT_WIDE_OBJ_MAX)
        return 0;
    count = list[0];
    for (it = list + 1; count > 0; count--, it += 3)
    {
        unsigned a0 = it[0], a1 = it[1];
        int shape = (int)(a0 >> 14), size = (int)(a1 >> 14);
        int ox, oy, w, h;

        if (shape == 3 || (a0 & 0x300) == 0x200) /* not a shape, or disabled */
            continue;
        w = sObjSize[shape][size][0];
        h = sObjSize[shape][size][1];
        if ((a0 & 0x300) == 0x300) /* affine double size (v1.28.0: the renderer draws affine ones too) */
            w *= 2, h *= 2;
        ox = x + (int)((a1 & 0x1FF) ^ 0x100) - 0x100; /* 9-bit signed offset */
        oy = y + (int)(signed char)(a0 & 0xFF);
        if (oy + h <= 0 || oy >= DISPLAY_HEIGHT)
            continue;
        /* v1.27.0: a push the game culls whole must not show its part beside a seam with nothing inside */
        if (sideOnly && ox < DISPLAY_WIDTH && ox + w > 0)
            return 0;
        if ((ox < 0 && ox + w > -2 * WIDE_M) || (ox + w > DISPLAY_WIDTH && ox < DISPLAY_WIDTH + 2 * WIDE_M))
        {
            keep = 1;
            if (!sideOnly)
                break;
        }
    }
    if (!keep)
        return 0;
    sPending.obj[sPending.count].x = (short)x;
    sPending.obj[sPending.count].y = (short)y;
    sPending.obj[sPending.count].oam2 = (unsigned short)oam2;
    sPending.obj[sPending.count].a0or = (unsigned short)a0or;
    sPending.obj[sPending.count].list = list;
    sPending.count++;
    return 1;
}

static int Latch(struct PortWideLatch *w)
{
    int camX = gBmSt.camera.x, camY = gBmSt.camera.y;
    int mapW = gBmMapSize.x, mapH = gBmMapSize.y;
    int ix, iy, wl;

    if (Proc_Find(gProc_BMapMain) == NULL)
        return PORT_WIDE_NO_PROC;
    if (mapW * 16 < DISPLAY_WIDTH || mapH * 16 < DISPLAY_HEIGHT || mapW > 0x40 || mapH > 0x40)
        return PORT_WIDE_NO_SIZE;
    w->camX = (short)camX; /* (v1.28.0: set before the camera check, for wide.log's detail) */
    w->camY = (short)camY;
    w->mapW = (short)mapW;
    w->mapH = (short)mapH;
    /* v1.28.0: the camera may pass a map edge by up to a square (the staff target selection scrolls 4 pixels
       past the bottom so its help bar does not hide the last row: e11 frame 73931; until v1.27.0 that made
       the strip jump to the middle and back with black sides). The renderer does not check the squares
       outside the map (BG3 holds whatever DisplayBmTile read past the map there) and draws them black in the
       sides; off is clamped to 0 .. 2M below */
    if (camX < -16 || camY < -16 || camX > mapW * 16 - DISPLAY_WIDTH + 16 || camY > mapH * 16 - DISPLAY_HEIGHT + 16)
        return PORT_WIDE_NO_CAMERA;
    /* v1.28.0: weather particles (src/bmio.c, sprites in the middle only; never set by a retail chapter) */
    switch (gPlaySt.chapterWeatherId)
    {
    case WEATHER_SNOW:
    case WEATHER_SNOWSTORM:
    case WEATHER_RAIN:
    case WEATHER_FLAMES:
    case WEATHER_SANDSTORM:
    case WEATHER_CLOUDS:
        return PORT_WIDE_NO_WEATHER;
    }

    w->ox = gBmSt.mapRenderOrigin.x;
    w->oy = gBmSt.mapRenderOrigin.y;
    w->rangeView = (gBmSt.gameStateBits & BM_FLAG_0) != 0;

    if (mapW * 16 >= RENDER_WIDE_W)
    {
        wl = camX - WIDE_M;
        if (wl > mapW * 16 - RENDER_WIDE_W)
            wl = mapW * 16 - RENDER_WIDE_W;
        if (wl < 0)
            wl = 0;
    }
    else
        wl = (mapW * 16 - RENDER_WIDE_W) / 2;
    w->off = camX - wl;
    if (w->off < 0) /* v1.28.0: a camera past the map's edge (above) */
        w->off = 0;
    if (w->off > 2 * WIDE_M)
        w->off = 2 * WIDE_M;

    /* the squares around the camera: 8 columns to its left (the left side's 2M and a scroll), 3 rows above */
    w->sx0 = (short)((camX >> 4) - 8);
    w->sy0 = (short)((camY >> 4) - 3);
    for (iy = 0; iy < PORT_WIDE_PH; iy++)
        for (ix = 0; ix < PORT_WIDE_PW; ix++)
            w->inMap[iy][ix] = (unsigned char)PortWide_BmTileWords(w->sx0 + ix, w->sy0 + iy, w->words[iy][ix]);
    /* v1.27.0: the range layer's classes (DisplayMovementViewTile: a movement square first, then an attack
       square, else none), inside the map only (DisplayMovementViewTile itself reads past it unchecked) */
    if (w->rangeView)
    {
        for (iy = 0; iy < PORT_WIDE_PH; iy++)
        {
            for (ix = 0; ix < PORT_WIDE_PW; ix++)
            {
                int x = w->sx0 + ix, y = w->sy0 + iy;
                unsigned char c = PORT_WIDE_RANGE_NONE;

                if (w->inMap[iy][ix] && x >= 0 && y >= 0 && x < mapW && y < mapH)
                {
                    if (gMapMovementSigned[y][x] >= 0)
                        c = PORT_WIDE_RANGE_MOVE;
                    else if (gMapRangeSigned[y][x])
                        c = PORT_WIDE_RANGE_ATTACK;
                }
                w->rangeClass[iy][ix] = c;
            }
        }
    }
    else
        memset(w->rangeClass, 0, sizeof(w->rangeClass));
    /* suspended (BMapDispSuspend: a battle about to start, a full-screen menu): the latch is still filled,
       and while BG3 still holds this map (a battle animation drawn over it) the renderer slides the strip to
       the middle and shows the map at the sides (v1.28.0; until v1.27.0 only on frames that followed one
       which did, at the map's x) */
    return gBmSt.gameGfxSemaphore != 0 ? PORT_WIDE_NO_SUSPEND : PORT_WIDE_NO_NONE;
}

void PortWide_VBlankLatch(void)
{
    struct PortWideLatch *w = &gPortWide;

    int i, k;

    if (!gRenderWideOn)
    {
        sPending.count = 0;
        sPending.muCount = 0;
        sPending.muFogCount = 0;
        sPendingHp.count = 0;
        w->ok = 0;
        w->objCount = 0;
        w->hpCount = 0;
        w->muCount = 0;
        w->muFogCount = 0;
        return;
    }
    w->frame = gPortFrameCount;
    w->objCount = sPending.count;
    if (sPending.count != 0)
        memcpy(w->obj, sPending.obj, sizeof(sPending.obj[0]) * sPending.count);
    sPending.count = 0;
    w->muCount = sPending.muCount; /* v1.28.0 */
    w->muFogCount = sPending.muFogCount;
    sPending.muCount = 0;
    sPending.muFogCount = 0;
    /* v1.27.0: the HP bar pictures, each with its owner among the side sprites (the last push that matches) */
    w->hpCount = sPendingHp.count;
    for (i = 0; i < w->hpCount; i++)
    {
        w->hp[i] = sPendingHp.hp[i];
        for (k = 0; k < w->objCount; k++)
            if (w->obj[k].x == w->hp[i].ox && w->obj[k].y == w->hp[i].oy && w->obj[k].list == w->hp[i].list &&
                w->obj[k].oam2 == w->hp[i].oam2)
                w->hp[i].owner = (short)k;
    }
    sPendingHp.count = 0;
    w->reason = Latch(w);
    w->suspended = w->reason == PORT_WIDE_NO_SUSPEND;
    w->ok = w->reason == PORT_WIDE_NO_NONE || w->suspended;
}

PORT_STATE_SESSION(gPortWide)
PORT_STATE_SESSION(sPending)
PORT_STATE_SESSION(sPendingHp)

#endif /* PORT_PLAYED_BUILD */
