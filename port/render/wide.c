#include "global.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_hw.h"
#include "port_state.h"
#include "bg_line.h"
#include "obj_line.h"
#include "render.h"

/*
 * Widescreen on the battle map, the renderer's side (v1.26.0, milestone M1 of
 * ~/fe8-notes/widescreen-design.txt, sections 2.1-2.4). Played builds only:
 * port/Makefile gives this object and render.o PORT_RFO_WIDE on the
 * PORT_PLAYED_BUILD condition (as obj_line.o's PORT_RFO_MAPOVERLAY), so every
 * test build compiles neither the calls nor this code.
 *
 * The game's 240x160 frame is drawn exactly as without the row (RenderLine);
 * this file only adds the 44 pixels of the sides of the 284x160 picture, from
 * port/reforged/reforged_wide.c's latch (gPortWide: camera, BG3 ring origin,
 * the BG3 words of the squares around the camera, the side sprites), and
 * reads GBA memory (I/O, VRAM, palettes) only.
 *
 * At the start of each frame (RenderWideBeginFrame) the sides are taken from
 * the map only when all of these hold, else they are black and the 240 strip
 * is in the middle (the pillarbox of v1.25.0):
 *   - the latch is from the last 2 frame steps and says the battle map is up;
 *   - DISPCNT mode 0 with BG3 on, BG3 a 256x256 4bpp text BG without mosaic;
 *   - BG3's scroll puts its ring origin within 32 pixels of the camera;
 *   - every square on screen in VRAM's BG3 ring, at ((x - origin.x) & 15,
 *     (y - origin.y) & 15) (src/bmmap.c RenderBmMapColumn/Line), holds the
 *     four words the tile rule gives for that map square: so an event picture
 *     (BACG), a battle animation, the chapter intro or anything else drawn
 *     into BG3 turns the sides black, with no hook in those screens;
 *   - the latch is not of a suspended map (BMapDispSuspend; until v1.27.0
 *     unless the frame before showed the map; v1.28.0: a suspended map's
 *     sides are checked as the map's, with the strip in the middle);
 *   - BG2 is off, or shows nothing at its own scroll (every visible tile all
 *     colour 0, wherever it is scrolled), or every square on screen in its ring is empty (0), or it
 *     holds the map by the same rule (src/bmmap.c RenderBmMapOnBg2, the tile
 *     change cross-fade), or it is the range layer (BM_FLAG_0: move and
 *     attack ranges, the danger zone) or the map in the other fog state
 *     (the fog cross-fade after a move, left out of the sides). Since
 *     v1.27.0 the range layer is drawn in the sides too, from the latch's
 *     square classes (gPortWide.rangeClass), when every square on screen in
 *     BG2's ring is of the class the maps give (movement 0x4280.., attack
 *     0x5280.. or 0x5284.., none 0; the class, not the words: design 2.3);
 *     otherwise the sides show the map without it. Any other BG2 (BG
 *     weather, effects) turns the sides black, the strip kept where it was.
 *
 * v1.27.0 also draws, from the latch's side list, the movement arrow and the
 * moving units (src/bmpatharrowdisp.c, src/mu.c), and the HP bar pictures of
 * units in the sides (gPortWide.hp, obj_line.c's OvlDraw rule: over the owner
 * sprite's pixels and pixels no sprite drew, with its priority).
 *
 * Each line then (RenderWideLine, after RenderLine with the same register
 * snapshot): BG3 (and BG2 in the cross-fade) from the latched words, placed by
 * the line's own scroll (so a quake moves the sides with the middle), with
 * the tiles and palettes in VRAM now; BG0/BG1 transparent (the UI stays in
 * the 240 strip); OBJ only from the latched side sprites, never from OAM
 * (sprites the game parks off screen never show); the backdrop behind. The
 * line's blend registers go through the same compositor (blend.c
 * RenderComposeLine), so fades and dimming cover the sides too. Windows: the
 * sides are outside every window, except that a WIN0/WIN1 that reaches x = 0
 * also covers the left side and one that reaches 240 the right side, on its
 * lines (full-width bands). The OBJ window, mosaic and HBlank effects other
 * than the scroll are not drawn in the sides. Squares outside the map are
 * black (a map narrower than 284 pixels is shown centred).
 *
 * v1.28.0 (milestone M3, polish):
 *   - a suspended map (BMapDispSuspend: the battle animation and its level-up
 *     screen drawn over the map, the map being the battle's background) has
 *     the 240 strip in the middle, like every screen that is not the map; its
 *     sides show the map at that x whenever nothing else is in the way, else
 *     black. Between the map and such a frame the strip slides by at most
 *     WIDE_SLIDE pixels a frame, so the map behind the battle never jumps by
 *     up to 22 pixels; to and from any other screen (BG3 no longer the map,
 *     e.g. a battle with its own background: a cut) it moves at once;
 *   - a camera up to a square past the map's edge (the staff target
 *     selection's help bar) keeps the sides: squares outside the map are
 *     skipped by the VRAM checks and drawn black (until v1.27.0 the strip
 *     jumped to the middle with black sides);
 *   - BG2 holding something else (a staff's or an item's effect, a map
 *     animation's spell): when little of it shows in the strip's outer 8
 *     pixel columns on a side the picture has (BgEdgesClear), it is local and
 *     the sides show the map without it (no map / black / map flicker); else
 *     (a full-height flash or glow) black sides as before; BG2 holding the
 *     map but for a few squares (a tile change's cross-fade, the old map
 *     fading out over BG3's new one): the sides show BG3;
 *   - the phase banner: its square wipe (BG1, src/phasechangefx.c, 15 x 10
 *     squares) is carried into the sides by the wipe's own rule, once every
 *     square on screen in VRAM matches it at one step (PhaseSquares); its
 *     band sprite is carried by side copies of the band's edge columns
 *     (port/reforged/reforged_wide.c PortWide_PushPhaseBand): side sprites
 *     may now be affine (obj_line.c's rule, the parameters in OAM);
 *   - an attack square in the sides takes the shade (0x5280.. / 0x5284..) of
 *     the strip's attack square nearest to it on the same row of squares (the
 *     two differ only while 0x5280.. grows in), else the strip's majority.
 */

#if PORT_RFO_WIDE

#include "port_reforged.h"

#define WIDE_W    RENDER_WIDE_W
#define WIDE_M    RENDER_WIDE_MARGIN
#define WIDE_SIDE (RENDER_WIDE_W - RENDER_FRAME_WIDTH) /* both sides' pixels on a line */

/* Why the sides are black this frame (after the latch's own PORT_WIDE_NO_* codes). */
enum
{
    WIDE_NO_OFF = PORT_WIDE_NO_COUNT, /* the row is off */
    WIDE_NO_STALE,                    /* no latch in the last 2 frame steps (OnVBlank not run) */
    WIDE_NO_MODE,                     /* not mode 0 with BG3 on (or forced blank) */
    WIDE_NO_BG3CNT,                   /* BG3 not a plain 256x256 4bpp text BG */
    WIDE_NO_SCROLL,                   /* BG3 or BG2 scrolled away from the camera */
    WIDE_NO_RING,                     /* BG3 does not hold the map */
    WIDE_NO_BG2,                      /* BG2 shows something else (ranges, weather) */
};
static const char *const sReasonName[] = {
    "map", "no map proc", "map suspended", "small map", "camera", "weather", "row off", "stale latch", "mode",
    "bg3cnt", "scroll", "bg3 not the map", "bg2",
};

/* v1.28.0: what a frame is, for the strip's x (RenderWideBeginFrame) */
enum
{
    KIND_OTHER, /* not the map (BG3 something else, no latch): strip in the middle, moved there at once */
    KIND_MAP,   /* the sides show the map: strip at the latch's off */
    KIND_MAPFX, /* BG3 is the map, BG2 in the way (black sides): strip kept at the latch's off */
    KIND_HELD,  /* BG3 is the map, suspended (battle animation, level-up): strip in the middle */
};
#define WIDE_SLIDE 3 /* v1.28.0: pixels a frame the strip moves between the map and a suspended map */

static int sMap;      /* this frame's sides show the map */
static int sKind;     /* v1.28.0: this frame's KIND_* (the previous frame's until RenderWideBeginFrame) */
static int sSliding;  /* v1.28.0: the strip is on its way between KIND_HELD's and the map's x */
static int sOff;      /* this frame's x of the game's x = 0 */
static int sReason;   /* why not (0 when they do) */
static int sLastLogged = -1;
static int sLogOn;
static int sBg2Map;   /* BG2 holds the map (the cross-fade): drawn in the sides too */
static int sBg2Range; /* v1.27.0: BG2 is the range layer and matches the latch's classes: drawn in the sides */
static int sRangeMiss; /* v1.27.0: the range layer is up but VRAM's classes differ (sides without squares) */
static u16 sAttackWord = 0x5280; /* v1.27.0: the first word of an attack square in the sides (RangeCheck) */
/* v1.28.0: per latched row of squares, the first word of the strip's attack square nearest the left / right
   seam on that row (0: none on the row, sAttackWord then) */
static u16 sRowShade[2][PORT_WIDE_PH];
static int sBg2Local; /* v1.28.0: BG2 holds an effect that stays off the strip's outer columns: left out */
/* v1.28.0: the phase banner's square wipe (BG1) matched at one step: drawn in the sides */
static int sSquares;
static int sSqOut, sSqT; /* ... which loop (0 in, 1 out) and its timer */
static char sDetail[96]; /* FE8PC_WIDE_LOG: the registers behind a black-sides reason */
static uint8_t sLineDone[RENDER_FRAME_HEIGHT];
static struct
{
    int vx, vy; /* the map pixel at the BG's screen (0, 0) */
    int h, v;   /* its HOFS/VOFS then (line 0) */
} sRef[4];

static uint16_t BgPixel(u32 charBase, u16 entry, int px, int py);

/* x in the 256-pixel ring, as a step of -128 .. 127 */
static int Wrap128(int v)
{
    return ((v + 128) & 255) - 128;
}

/* BG n's screen origin in map pixels: its ring origin plus its scroll, taken near the camera. */
static int SetRef(int n, const struct RenderLineRegs *regs, const struct PortWideLatch *w)
{
    int h = regs->bghofs[n] & 0x1FF, v = regs->bgvofs[n] & 0x1FF;
    int dx = Wrap128(w->ox * 16 + h - w->camX), dy = Wrap128(w->oy * 16 + v - w->camY);

    if (dx < -32 || dx > 32 || dy < -32 || dy > 32)
        return 0;
    sRef[n].vx = w->camX + dx;
    sRef[n].vy = w->camY + dy;
    sRef[n].h = h;
    sRef[n].v = v;
    return 1;
}

/* v1.28.0: map square (sx, sy) on screen, for the VRAM checks below: 1 check it against the latch; 0 skip it,
   it is outside the map (a camera up to a square past the map's edge, reforged_wide.c Latch: BG3 and BG2
   hold whatever DisplayBmTile / DisplayMovementViewTile read past the map there; the sides draw such squares
   black); -1 the check fails (outside the latched squares, or a square the latch has no words for). */
static int CheckSquare(const struct PortWideLatch *w, int sx, int sy)
{
    int px = sx - w->sx0, py = sy - w->sy0;

    if (px < 0 || py < 0 || px >= PORT_WIDE_PW || py >= PORT_WIDE_PH)
        return -1;
    if (sx < 0 || sy < 0 || sx >= w->mapW || sy >= w->mapH)
        return 0;
    return w->inMap[py][px] ? 1 : -1;
}

/* Every square on screen of BG n's ring: 1 = holds the latched words, 0 = something else. how 1: instead,
   1 = every word is 0; how 2: the latched words, each in either fog palette (6 or 11, DisplayBmTile). */
static int RingCheck(int n, uint16_t cnt, const struct PortWideLatch *w, int how)
{
    u32 mapBase = ((cnt >> 8) & 0x1F) * 0x800u;
    int sx, sy, sx1 = (sRef[n].vx + RENDER_FRAME_WIDTH - 1) >> 4, sy1 = (sRef[n].vy + RENDER_FRAME_HEIGHT - 1) >> 4;

    for (sy = sRef[n].vy >> 4; sy <= sy1; sy++)
    {
        for (sx = sRef[n].vx >> 4; sx <= sx1; sx++)
        {
            int px = sx - w->sx0, py = sy - w->sy0;
            int rx = (sx - w->ox) & 15, ry = (sy - w->oy) & 15;
            int k, c = how != 1 ? CheckSquare(w, sx, sy) : 1;

            if (c < 0)
                return 0;
            if (c == 0)
                continue;
            for (k = 0; k < 4; k++)
            {
                u16 e = RENDER_VRAM16(mapBase + (u32)(((ry * 2 + (k >> 1)) * 32 + rx * 2 + (k & 1)) * 2));
                u16 t = how != 1 ? w->words[py][px][k] : 0;

                if (e != t && (how != 2 || (e != (u16)(t + 0x5000) && e != (u16)(t - 0x5000))))
                    return 0;
            }
        }
    }
    return 1;
}

/* v1.28.0: BG n's ring holds the latched words on every square on screen but a few (at most 16 of the about
   176): the map before a tile change (a door, a wall, a bridge: the change cross-fades BG2's old map out over
   BG3's new one). */
static int RingMostly(int n, uint16_t cnt, const struct PortWideLatch *w)
{
    u32 mapBase = ((cnt >> 8) & 0x1F) * 0x800u;
    int sx, sy, sx1 = (sRef[n].vx + RENDER_FRAME_WIDTH - 1) >> 4, sy1 = (sRef[n].vy + RENDER_FRAME_HEIGHT - 1) >> 4;
    int miss = 0;

    for (sy = sRef[n].vy >> 4; sy <= sy1; sy++)
    {
        for (sx = sRef[n].vx >> 4; sx <= sx1; sx++)
        {
            int px = sx - w->sx0, py = sy - w->sy0;
            int rx = (sx - w->ox) & 15, ry = (sy - w->oy) & 15;
            int k, c = CheckSquare(w, sx, sy);

            if (c < 0)
                return 0;
            if (c == 0)
                continue;
            for (k = 0; k < 4; k++)
                if (RENDER_VRAM16(mapBase + (u32)(((ry * 2 + (k >> 1)) * 32 + rx * 2 + (k & 1)) * 2)) !=
                    w->words[py][px][k])
                {
                    if (++miss > 16)
                        return 0;
                    break;
                }
        }
    }
    return 1;
}

/* v1.27.0: the class of a range square's four BG2 words as src/bmmap.c DisplayMovementViewTile writes them
   (0x4280.. a movement square; 0x5280.. or 0x5284.., which one depends on what the square held before, an
   attack square; all 0 none), or -1 for anything else. */
static int RangeWordsClass(const u16 *e)
{
    int k;

    if (e[0] == 0 && e[1] == 0 && e[2] == 0 && e[3] == 0)
        return PORT_WIDE_RANGE_NONE;
    if (e[0] == 0x4280)
    {
        for (k = 1; k < 4; k++)
            if (e[k] != 0x4280 + k)
                return -1;
        return PORT_WIDE_RANGE_MOVE;
    }
    if (e[0] == 0x5280 || e[0] == 0x5284)
    {
        for (k = 1; k < 4; k++)
            if (e[k] != e[0] + k)
                return -1;
        return PORT_WIDE_RANGE_ATTACK;
    }
    return -1;
}

/* v1.27.0: every square on screen of BG2's ring holds a range square of the class the latch worked out from
   the movement and range maps (design 2.3: the class, not the exact words). The sides' attack squares then
   take the shade most of the strip's attack squares have (0x5284.. when the range was drawn over squares
   already there, as DisplayMovementViewTile does; 0x5280.. else, and when there are none): the two only
   differ during the opening animation, where 0x5280.. grows in and 0x5284.. is full at once
   (src/playerphase.c MoveLimitViewChange_OnInit). */
static int RangeCheck(uint16_t cnt, const struct PortWideLatch *w)
{
    u32 mapBase = ((cnt >> 8) & 0x1F) * 0x800u;
    int sx, sy, sx1 = (sRef[2].vx + RENDER_FRAME_WIDTH - 1) >> 4, sy1 = (sRef[2].vy + RENDER_FRAME_HEIGHT - 1) >> 4;
    int n5280 = 0, n5284 = 0;

    memset(sRowShade, 0, sizeof(sRowShade));
    for (sy = sRef[2].vy >> 4; sy <= sy1; sy++)
    {
        for (sx = sRef[2].vx >> 4; sx <= sx1; sx++)
        {
            int px = sx - w->sx0, py = sy - w->sy0;
            int rx = (sx - w->ox) & 15, ry = (sy - w->oy) & 15;
            u16 e[4];
            int k, c = CheckSquare(w, sx, sy);

            if (c < 0)
                return 0;
            if (c == 0)
                continue;
            for (k = 0; k < 4; k++)
                e[k] = RENDER_VRAM16(mapBase + (u32)(((ry * 2 + (k >> 1)) * 32 + rx * 2 + (k & 1)) * 2));
            if (RangeWordsClass(e) != w->rangeClass[py][px])
            {
                if (sLogOn)
                    snprintf(sDetail, sizeof(sDetail), ": range square %d,%d is %04X in VRAM, class %d in the maps",
                        sx, sy, e[0], w->rangeClass[py][px]);
                return 0;
            }
            n5280 += e[0] == 0x5280;
            n5284 += e[0] == 0x5284;
            /* v1.28.0: the row's attack square nearest each seam (left: the first, right: the last) */
            if (e[0] == 0x5280 || e[0] == 0x5284)
            {
                if (sRowShade[0][py] == 0)
                    sRowShade[0][py] = e[0];
                sRowShade[1][py] = e[0];
            }
        }
    }
    sAttackWord = n5284 > n5280 ? 0x5284 : 0x5280;
    return 1;
}

/* v1.28.0: a text BG's screen entry at (mx, my) inside the BG, for any of the four sizes (256 or 512 pixels
   each way: 32 x 32-entry blocks in VRAM order; the level-up screen's BG2 is 512 wide) */
static u16 TextEntry(uint16_t cnt, int mx, int my)
{
    u32 block = (u32)((mx >> 8) + (my >> 8) * ((cnt & 0x4000) ? 2 : 1));

    return RENDER_VRAM16(((cnt >> 8) & 0x1F) * 0x800u + block * 0x800u +
        (u32)((((my & 255) >> 3) * 32 + ((mx & 255) >> 3)) * 2));
}

/* v1.28.0: what BG n holds (a staff's or item's effect, a map animation's spell) is local to the strip: in the
   strip's outer 8-pixel columns at its own scroll (line 0's), on each side the picture shows (the latch's off:
   no left side at off 0, no right side at off 2M), at most WIDE_FX_LINES lines have an opaque pixel. The sides
   then show the map without it: an effect at a seam is cut there, which shows less than black sides blinking
   for a second; one that fills the screen's height at an edge (a flash, a full-screen glow) keeps the sides
   black. */
#define WIDE_FX_LINES 48
static int BgEdgesClear(int n, const struct RenderLineRegs *regs)
{
    uint16_t cnt = regs->bgcnt[n];
    u32 charBase = ((cnt >> 2) & 3) * 0x4000u;
    int h = regs->bghofs[n] & 0x1FF, v = regs->bgvofs[n] & 0x1FF;
    int bw = (cnt & 0x4000) ? 512 : 256, bh = (cnt & 0x8000) ? 512 : 256;
    int left = gPortWide.off > 0, right = gPortWide.off < 2 * WIDE_M;
    int lines[2] = { 0, 0 };
    int y, i;

    if (cnt & (BGCNT_256COLOR | BGCNT_MOSAIC))
        return 0;
    for (y = 0; y < RENDER_FRAME_HEIGHT; y++)
    {
        int my = (v + y) & (bh - 1), hit[2] = { 0, 0 };

        for (i = 0; i < 16; i++)
        {
            int r = i >= 8, x = r ? RENDER_FRAME_WIDTH - 16 + i : i, mx = (h + x) & (bw - 1);

            if (hit[r] || !(r ? right : left))
                continue;
            if (BgPixel(charBase, TextEntry(cnt, mx, my), mx & 7, my & 7) != RENDER_TRANSPARENT)
                hit[r] = 1;
        }
        lines[0] += hit[0];
        lines[1] += hit[1];
        if (lines[0] > WIDE_FX_LINES || lines[1] > WIDE_FX_LINES)
            return 0;
    }
    return 1;
}

/* v1.28.0: BG2 can hardly be seen: the alpha blend's first target at 1/16 or less over BG3 and the backdrop,
   no window (the first step of a fade, e11 75758: the map on BG2 for a fog cross-fade, its scroll not at the
   camera yet). The sides show the map without it. */
static int Bg2Faint(const struct RenderLineRegs *regs)
{
    return (regs->dispcnt & 0xE000) == 0 && (regs->bldcnt & 0x00C4) == 0x0044 &&
        (regs->bldcnt & 0x2800) == 0x2800 && (regs->bldalpha & 0x1F) <= 1;
}

/* v1.28.0: the phase banner's square wipe (src/phasechangefx.c PhaseIntroSquares_InLoop / _OutLoop): the four
   BG1 words the loop at timer t writes for square (x, y), for any x (the sides' squares by the same rule). */
#define WIDE_SQ_CHR 0x100 /* BGCHR_PHASE_CHANGE_SQUARES */
#define WIDE_SQ_PAL 5     /* BGPAL_PHASE_CHANGE */
static void SquareWords(int out, int t, int x, int y, u16 *e)
{
    int val = out ? (1 - t) + (10 + x) + (10 - y) : (x - t) + (0x15 - y);
    u16 b;

    if (val > 0x10)
        val = 0x10;
    if (val < 0)
        val = 0;
    if (!out)
    {
        b = (u16)(WIDE_SQ_CHR + ((0x10 - val) & 0xFE) + (WIDE_SQ_PAL << 12));
        e[0] = b;
        e[1] = (u16)(b + 1);
        e[2] = (u16)(b + 0x20);
        e[3] = (u16)(b + 0x21);
    }
    else
    {
        b = (u16)(WIDE_SQ_CHR + (val & 0xFE) + (WIDE_SQ_PAL << 12) + 0x400); /* TILE_HFLIP */
        e[0] = (u16)(b + 1);
        e[1] = b;
        e[2] = (u16)(b + 0x21);
        e[3] = (u16)(b + 0x20);
    }
}

/* v1.28.0: BG1 holds the phase banner's square wipe, unscrolled, every one of its 15 x 10 squares as the wipe
   writes them at one step: sets sSqOut / sSqT. Anything else on BG1 (menus, windows) fails on the first squares. */
static int PhaseSquares(const struct RenderLineRegs *regs)
{
    uint16_t cnt = regs->bgcnt[1];
    u32 mapBase = ((cnt >> 8) & 0x1F) * 0x800u;
    int out, t, x, y, k;

    if (!(regs->dispcnt & 0x0200) || (cnt >> 14) != 0 || (cnt & (BGCNT_256COLOR | BGCNT_MOSAIC)) ||
        (regs->bghofs[1] & 0x1FF) != 0 || (regs->bgvofs[1] & 0x1FF) != 0)
        return 0;
    if ((RENDER_VRAM16(mapBase) >> 12) != WIDE_SQ_PAL || ((RENDER_VRAM16(mapBase) & 0x3FF) - WIDE_SQ_CHR) >= 0x40u)
        return 0;
    for (out = 0; out < 2; out++)
    {
        for (t = 0; t < 0x24; t++)
        {
            int ok = 1;

            for (y = 0; y < 10 && ok; y++)
            {
                for (x = 0; x < 15 && ok; x++)
                {
                    u16 e[4];

                    SquareWords(out, t, x, y, e);
                    for (k = 0; k < 4; k++)
                        if (RENDER_VRAM16(mapBase + (u32)(((y * 2 + (k >> 1)) * 32 + x * 2 + (k & 1)) * 2)) != e[k])
                        {
                            ok = 0;
                            break;
                        }
                }
            }
            if (ok)
            {
                sSqOut = out;
                sSqT = t;
                return 1;
            }
        }
    }
    return 0;
}

/* BG n shows nothing on screen at its own scroll (whatever the camera): every visible screen entry's tile is
   all colour 0 (FE8 leaves BG2 on with a cleared tilemap after the phase banner, a battle or a tile change,
   scrolled anywhere). */
static int BgEmpty(int n, const struct RenderLineRegs *regs)
{
    uint16_t cnt = regs->bgcnt[n];
    u32 charBase = ((cnt >> 2) & 3) * 0x4000u;
    int h = regs->bghofs[n] & 0x1FF, v = regs->bgvofs[n] & 0x1FF;
    int cols = (cnt & 0x4000) ? 64 : 32, rows = (cnt & 0x8000) ? 64 : 32; /* v1.28.0: any size */
    int tx, ty, last = -1;

    if (cnt & BGCNT_256COLOR)
        return 0;
    for (ty = 0; ty <= RENDER_FRAME_HEIGHT / 8; ty++)
    {
        for (tx = 0; tx <= RENDER_FRAME_WIDTH / 8; tx++)
        {
            int r = ((v >> 3) + ty) & (rows - 1), c = ((h >> 3) + tx) & (cols - 1);
            int tile = TextEntry(cnt, c * 8, r * 8) & 0x3FF;
            u32 addr = charBase + (u32)tile * TILE_SIZE_4BPP, k;

            if (tile == last)
                continue;
            if (addr + TILE_SIZE_4BPP > 0x10000u)
                return 0;
            for (k = 0; k < TILE_SIZE_4BPP; k += 2)
                if (RENDER_VRAM16(addr + k) != 0)
                    return 0;
            last = tile;
        }
    }
    return 1;
}

static int Decide(void)
{
    const struct PortWideLatch *w = &gPortWide;
    struct RenderLineRegs regs;
    uint16_t cnt;

    sSquares = 0;
    sBg2Local = 0;
    if (!gRenderWideOn)
        return WIDE_NO_OFF;
    if ((uint32_t)(gPortFrameCount - w->frame) > 2)
        return WIDE_NO_STALE;
    if (!w->ok)
    {
        if (sLogOn && w->reason == PORT_WIDE_NO_CAMERA) /* v1.28.0 */
            snprintf(sDetail, sizeof(sDetail), ": camera %d,%d map %dx%d", w->camX, w->camY, w->mapW, w->mapH);
        return w->reason != 0 ? w->reason : WIDE_NO_STALE;
    }
    RenderSnapshotRegs(&regs);
    if ((regs.dispcnt & 7) != 0 || !(regs.dispcnt & 0x0800) || (regs.dispcnt & 0x80))
        return WIDE_NO_MODE;
    cnt = regs.bgcnt[3];
    if ((cnt >> 14) != 0 || (cnt & (BGCNT_256COLOR | BGCNT_MOSAIC)))
        return WIDE_NO_BG3CNT;
    if (!SetRef(3, &regs, w))
        return WIDE_NO_SCROLL;
    if (!RingCheck(3, cnt, w, 0))
        return WIDE_NO_RING;
    /* suspended (BMapDispSuspend) with BG3 still this map: the battle animation and its level-up screen, drawn
       over the map. v1.28.0: the rest is checked as for the map; RenderWideBeginFrame makes it KIND_HELD
       (until v1.27.0 a frame that followed a map frame kept the sides and the strip stayed at the map's off
       for the whole battle, so the battle screen was off centre) */
    sBg2Map = 0;
    sBg2Range = 0;
    sRangeMiss = 0;
    sBg2Local = 0;
    sSquares = PhaseSquares(&regs);
    /* v1.27.0: the range layer (moves, attacks, the danger zone) at the camera with the latch's classes on
       every square on screen: its squares are drawn in the sides from those classes (even when the squares
       on screen are all empty and the ranges lie only in the sides) */
    cnt = regs.bgcnt[2];
    if ((regs.dispcnt & 0x0400) && w->rangeView && (cnt >> 14) == 0 && !(cnt & (BGCNT_256COLOR | BGCNT_MOSAIC)) &&
        SetRef(2, &regs, w))
    {
        if (RangeCheck(cnt, w))
        {
            sBg2Range = 1;
            return 0;
        }
        sRangeMiss = 1;
    }
    if ((regs.dispcnt & 0x0400) && !BgEmpty(2, &regs))
    {
        cnt = regs.bgcnt[2];
        if ((cnt >> 14) != 0 || (cnt & (BGCNT_256COLOR | BGCNT_MOSAIC)) || !SetRef(2, &regs, w))
        {
            /* v1.28.0: an effect that stays off the strip's outer columns, or BG2 at 1/16 or less: the sides
               show the map without it */
            if (BgEdgesClear(2, &regs) || Bg2Faint(&regs))
            {
                sBg2Local = 1;
                return 0;
            }
            if (sLogOn)
                snprintf(sDetail, sizeof(sDetail), ": BG2CNT %04X, scroll %d,%d off the camera %d,%d, BLDCNT %04X, BLDALPHA %04X",
                    cnt, regs.bghofs[2], regs.bgvofs[2], w->camX, w->camY, regs.bldcnt, regs.bldalpha);
            return WIDE_NO_BG2;
        }
        if (!RingCheck(2, cnt, w, 1))
        {
            if (RingCheck(2, cnt, w, 0))
                sBg2Map = 1;
            else if (!w->rangeView && RingMostly(2, cnt, w))
                ; /* v1.28.0: a tile change's cross-fade (BG2 the old map): the sides show BG3's new map */
            else if (!w->rangeView && !RingCheck(2, cnt, w, 2))
            {
                if (BgEdgesClear(2, &regs) || Bg2Faint(&regs)) /* v1.28.0: as above */
                {
                    sBg2Local = 1;
                    return 0;
                }
                if (sLogOn)
                    snprintf(sDetail, sizeof(sDetail), ": BG2CNT %04X, scroll %d,%d, BLDCNT %04X, BLDALPHA %04X",
                        cnt, regs.bghofs[2], regs.bgvofs[2], regs.bldcnt, regs.bldalpha);
                return WIDE_NO_BG2;
            }
            /* else the range layer whose squares on screen are not of the maps' classes (v1.27.0: rare, a
               frame where the maps changed before BG2 was redrawn), or the map in the other fog state (the
               fog cross-fade after a move, RenderBmMapOnBg2): the sides show BG3 without it (fog changes
               there at once) */
        }
    }
    return 0;
}

void RenderWideReset(void)
{
    sMap = 0;
    sKind = KIND_OTHER;
    sSliding = 0;
    sOff = WIDE_M;
    sReason = WIDE_NO_OFF;
    memset(sLineDone, 0, sizeof(sLineDone));
}

void RenderWideBeginFrame(void)
{
    int prevKind = sKind, kind, target, r;

    memset(sLineDone, 0, sizeof(sLineDone));
    sDetail[0] = '\0';
    r = Decide();
    /* v1.28.0: a suspended map (BG3 still the map at the camera: the battle animation drawn over the map,
       which is then the battle's background, and its level-up screen) is KIND_HELD: the strip in the middle,
       the sides showing the map at that x whenever nothing else is in the way (as on the map: the battle's
       opening effect, the battle itself, the closing effect), so neither the start nor the end of the battle
       cuts the sides to black over the same map; black sides otherwise. A battle with its own background
       (BG3 not the map) is KIND_OTHER. */
    if ((r == 0 || r == WIDE_NO_BG2) && gPortWide.suspended)
    {
        kind = KIND_HELD;
        sMap = r == 0;
        sReason = sMap ? 0 : PORT_WIDE_NO_SUSPEND;
    }
    else
    {
        sReason = r;
        sMap = r == 0;
        kind = sMap ? KIND_MAP : r == WIDE_NO_BG2 ? KIND_MAPFX : KIND_OTHER;
    }
    /* v1.28.0: the strip's x. On the map (and while only BG2 is in the way) the latch's off; on a suspended
       map (battle animation, level-up) and every other screen the middle. Between the map and a suspended map
       BG3 shows the same map on both frames, so a jump would move the whole picture: the strip slides there
       by WIDE_SLIDE pixels a frame (map frames draw the sides at the x reached; the latch's squares reach
       8 columns left of the camera, more than the 2 x 22 needed). To or from any other screen (a cut: the
       picture changes completely) it moves at once. */
    target = kind == KIND_MAP || kind == KIND_MAPFX ? gPortWide.off : WIDE_M;
    if (kind != KIND_OTHER && prevKind != KIND_OTHER && (sSliding || (kind == KIND_HELD) != (prevKind == KIND_HELD)))
    {
        if (sOff < target)
            sOff = sOff + WIDE_SLIDE < target ? sOff + WIDE_SLIDE : target;
        else if (sOff > target)
            sOff = sOff - WIDE_SLIDE > target ? sOff - WIDE_SLIDE : target;
        sSliding = sOff != target;
    }
    else
    {
        sOff = target;
        sSliding = 0;
    }
    sKind = kind;
    {
        /* the logged state: the reason, and on map frames whether the range layer is drawn (v1.27.0), a local
           BG2 effect left out and the square wipe drawn (v1.28.0) */
        const struct PortWideLatch *w = &gPortWide;
        int state = sReason + (sMap && sBg2Range ? 100 : 0) + (sMap && sRangeMiss ? 200 : 0) +
            (sMap && sBg2Range && sAttackWord == 0x5284 ? 300 : 0) + (sMap && sBg2Local ? 1000 : 0) +
            (sMap && sSquares ? 2000 : 0) + (sMap && w->muCount ? 10000 : 0) + (sMap && w->muFogCount ? 20000 : 0) +
            (sMap && w->hpCount ? 40000 : 0);

        if (sLogOn && state != sLastLogged && gRenderWideOn)
        {
            if (sMap)
                PortLog("[wide] frame %u: sides show the map%s%s%s%s%s%s%s%s\n", gPortFrameCount,
                    sBg2Range ? " with the range squares" : sRangeMiss ? " without the range squares" : "",
                    sBg2Range && sAttackWord == 0x5284 ? " (attack squares 5284)" : "",
                    sRangeMiss ? sDetail : "", sBg2Local ? " without BG2's effect (off the strip's edges)" : "",
                    sSquares ? " with the phase banner's squares" : "", w->muCount ? ", moving units" : "",
                    w->muFogCount ? ", a moving enemy hidden by the fog" : "", w->hpCount ? ", HP bars" : "");
            else
                PortLog("[wide] frame %u: sides black (%s%s)\n", gPortFrameCount, sReasonName[sReason], sDetail);
        }
        sLastLogged = state;
    }
}

/* v1.28.0: wide.log's extra columns (render.c): side sprites, moving units among them, moving units the fog
   hid at a side, HP bar pictures, and flags (1 the strip sliding, 2 range squares drawn, 4 a local BG2 effect
   left out, 8 the phase banner's squares drawn, 16 the latch suspended) */
void RenderWideLogExtra(char *buf, int size)
{
    const struct PortWideLatch *w = &gPortWide;
    int flags = (sSliding ? 1 : 0) | (sMap && sBg2Range ? 2 : 0) | (sMap && sBg2Local ? 4 : 0) |
        (sMap && sSquares ? 8 : 0) | (w->suspended ? 16 : 0);

    snprintf(buf, (size_t)size, " %d %d %d %d %d", sMap ? w->objCount : 0, sMap ? w->muCount : 0,
        sMap ? w->muFogCount : 0, sMap ? w->hpCount : 0, flags);
}

/* 4bpp text-BG pixel (bg_line.c's fetch, without its log): the colour or RENDER_TRANSPARENT. */
static uint16_t BgPixel(u32 charBase, u16 entry, int px, int py)
{
    u32 addr = charBase + (u32)(entry & 0x3FF) * TILE_SIZE_4BPP;
    u8 pair;
    int index;

    if (entry & 0x400)
        px = 7 - px;
    if (entry & 0x800)
        py = 7 - py;
    if (addr >= 0x10000u)
        return RENDER_TRANSPARENT;
    pair = RENDER_VRAM8(addr + (u32)(py * 4 + (px >> 1)));
    index = (px & 1) ? (pair >> 4) : (pair & 0xF);
    return index != 0 ? (uint16_t)(RENDER_PLTT((entry >> 12) * 16 + index) & 0x7FFF) : RENDER_TRANSPARENT;
}

/* The side pixels of BG n (2 or 3) on this line from the latched words (range: from the latched range classes,
   v1.27.0: 0x4280.. a movement square, 0x5280.. an attack square, 0 none, as DisplayMovementViewTile writes
   them); inMap[i] = 0 off the map. */
static void SideBg(int n, int line, const struct RenderLineRegs *regs, uint16_t *col, uint8_t *inMap, int range)
{
    const struct PortWideLatch *w = &gPortWide;
    u32 charBase = ((regs->bgcnt[n] >> 2) & 3) * 0x4000u;
    int mxBase = sRef[n].vx + Wrap128((regs->bghofs[n] & 0x1FF) - sRef[n].h) - sOff;
    int my = sRef[n].vy + Wrap128((regs->bgvofs[n] & 0x1FF) - sRef[n].v) + line;
    int py = (my >> 4) - w->sy0;
    int i;

    for (i = 0; i < WIDE_SIDE; i++)
    {
        int x = i < sOff ? i : i + RENDER_FRAME_WIDTH; /* the wide picture's x */
        int mx = mxBase + x;
        int px = (mx >> 4) - w->sx0;

        if (px < 0 || py < 0 || px >= PORT_WIDE_PW || py >= PORT_WIDE_PH || !w->inMap[py][px])
        {
            col[i] = RENDER_TRANSPARENT;
            if (inMap != NULL)
                inMap[i] = 0;
            continue;
        }
        if (range)
        {
            int k = ((my >> 3) & 1) * 2 + ((mx >> 3) & 1), c = w->rangeClass[py][px];
            u16 shade = sRowShade[i < sOff ? 0 : 1][py] ? sRowShade[i < sOff ? 0 : 1][py] : sAttackWord;
            u16 e = c == PORT_WIDE_RANGE_MOVE ? (u16)(0x4280 + k) : c == PORT_WIDE_RANGE_ATTACK ? (u16)(shade + k) : 0;

            col[i] = BgPixel(charBase, e, mx & 7, my & 7);
        }
        else
            col[i] = BgPixel(charBase, w->words[py][px][((my >> 3) & 1) * 2 + ((mx >> 3) & 1)], mx & 7, my & 7);
        if (inMap != NULL)
            inMap[i] = 1;
    }
}

/* v1.28.0: the side pixels of BG1 on this line while it holds the phase banner's square wipe (PhaseSquares:
   unscrolled), the side squares' words by the wipe's rule at the matched step. */
static void SideSquares(int line, const struct RenderLineRegs *regs, uint16_t *col)
{
    u32 charBase = ((regs->bgcnt[1] >> 2) & 3) * 0x4000u;
    int i;

    for (i = 0; i < WIDE_SIDE; i++)
    {
        int gx = (i < sOff ? i : i + RENDER_FRAME_WIDTH) - sOff; /* the game's x, -44 .. 283 */
        int qx = gx >= 0 ? gx >> 4 : -((15 - gx) >> 4);         /* floor(gx / 16) */
        int px = gx - qx * 16;
        u16 e[4];

        if (line >= 160)
        {
            col[i] = RENDER_TRANSPARENT;
            continue;
        }
        SquareWords(sSqOut, sSqT, qx, line >> 4, e);
        col[i] = BgPixel(charBase, e[((line >> 3) & 1) * 2 + (px >> 3)], px & 7, line & 7);
    }
}

/* [shape][size] = { width, height } (obj_line.c). */
static const uint8_t sObjSize[3][4][2] = {
    { { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } },
    { { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } },
    { { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } },
};

/* obj_line.c ObjFetchIndex4, without its tripwires. */
static int ObjIndex4(uint32_t tile, int w, int tx, int ty, int map1d)
{
    const uint8_t *objVram = (const uint8_t *)(uintptr_t)PORT_VRAM_START + RENDER_OBJ_VRAM_OFFSET;
    uint32_t charBase = tile * 32, addr;
    unsigned byte;

    if (map1d)
        addr = charBase + (uint32_t)((ty >> 3) * (w >> 3) + (tx >> 3)) * 32;
    else
        addr = (charBase & 0x7C00) + (uint32_t)(ty >> 3) * 0x400 + (((charBase & 0x3E0) + (uint32_t)(tx >> 3) * 32) & 0x3E0);
    addr = (addr + (uint32_t)(ty & 7) * 4 + (uint32_t)((tx & 7) >> 1)) & (RENDER_OBJ_VRAM_SIZE - 1);
    byte = objVram[addr];
    return (tx & 1) ? (int)(byte >> 4) : (int)(byte & 0xF);
}

/* v1.27.0: the side sprite (latched obj index) whose colour is at each side pixel, where has[] */
static int16_t sWinner[WIDE_SIDE];

/* v1.27.0: the HP bar pictures of units reaching into the sides on this line (obj_line.c OvlDraw's rule):
   over the owner sprite's own pixels and over pixels no sprite drew, never over another sprite's, with the
   owner's priority (unit sprites: mode 0). */
static void SideHp(int line, struct RenderObjLine *obj)
{
    const struct PortWideLatch *w = &gPortWide;
    int h;

    for (h = 0; h < w->hpCount; h++)
    {
        const struct PortWideHp *p = &w->hp[h];
        int uy = line - p->sqy, ux, prio = (int)(((unsigned)(uint16_t)(p->list[3] + p->oam2) >> 10) & 3);

        if (uy < 0 || uy >= 16 || p->owner < 0)
            continue; /* (no owner: its sprite is not drawn, so neither is the picture, as in the middle) */
        for (ux = 0; ux < 16; ux++)
        {
            unsigned pix = p->pix[uy][ux];
            int gx = p->sqx + ux, i;

            if (!(pix & RFO_OVERLAY_OPAQUE))
                continue;
            if (gx >= -sOff && gx < 0)
                i = gx + sOff;
            else if (gx >= RENDER_FRAME_WIDTH && gx < RENDER_FRAME_WIDTH + WIDE_SIDE - sOff)
                i = gx + sOff - RENDER_FRAME_WIDTH;
            else
                continue;
            if (obj->has[i] && sWinner[i] != p->owner)
                continue; /* another sprite's pixel: it stays */
            obj->color[i] = (uint16_t)(pix & 0x7FFF);
            if (!obj->has[i])
            {
                obj->prio[i] = (uint8_t)prio;
                obj->semi[i] = 0;
                obj->has[i] = 1;
                sWinner[i] = (int16_t)p->owner;
            }
        }
    }
}

/* The latched side sprites on this line, in push order with obj_line.c's winner rule (ObjPlot). */
static void SideObj(int line, uint16_t dispcnt, struct RenderObjLine *obj)
{
    const struct PortWideLatch *w = &gPortWide;
    int map1d = (dispcnt >> 6) & 1;
    int k;

    for (k = 0; k < w->objCount; k++)
    {
        const struct PortWideObj *e = &w->obj[k];
        const unsigned short *it = e->list;
        int count = it[0];

        for (it++; count > 0; count--, it += 3)
        {
            unsigned a0 = (unsigned)(it[0] | e->a0or), a1 = it[1], a2 = (unsigned)(uint16_t)(it[2] + e->oam2);
            int shape = (int)(a0 >> 14), size = (int)(a1 >> 14);
            int mode = (int)((a0 >> 10) & 3), prio = (int)((a2 >> 10) & 3);
            int affine = (a0 & 0x100) != 0, pa = 0x100, pb = 0, pc = 0, pd = 0x100;
            int ow, oh, bw, bh, ox, oy, row, ty, r;

            if ((a0 & 0x300) == 0x200 || shape == 3 || mode >= 2 || (a0 & 0x2000))
                continue; /* disabled, OBJ window, mode 3 or 8bpp: not in the sides */
            ow = sObjSize[shape][size][0];
            oh = sObjSize[shape][size][1];
            bw = ow;
            bh = oh;
            if (affine)
            {
                /* v1.28.0: affine (obj_line.c ObjDrawAffine): the group's parameters from OAM as flushed, the
                   box doubled with double size, the picture clipped to its own W x H */
                const uint8_t *oam = (const uint8_t *)(uintptr_t)PORT_OAM_START;
                unsigned base = ((a1 >> 9) & 0x1F) * 32u;

                pa = *(const int16_t *)(oam + base + 6);
                pb = *(const int16_t *)(oam + base + 14);
                pc = *(const int16_t *)(oam + base + 22);
                pd = *(const int16_t *)(oam + base + 30);
                if (a0 & 0x200)
                    bw *= 2, bh *= 2;
            }
            oy = e->y + (int)(signed char)(a0 & 0xFF);
            row = line - oy;
            if (row < 0 || row >= bh)
                continue;
            ox = e->x + (int)((a1 & 0x1FF) ^ 0x100) - 0x100;
            ty = !affine && (a1 & 0x2000) ? oh - 1 - row : row;
            for (r = 0; r < 2; r++)
            {
                /* the left side is the game's x -sOff .. -1 (obj index 0 ..), the right 240 .. */
                int lo = r == 0 ? -sOff : RENDER_FRAME_WIDTH;
                int hi = r == 0 ? 0 : RENDER_FRAME_WIDTH + WIDE_SIDE - sOff;
                int base = r == 0 ? sOff : sOff - RENDER_FRAME_WIDTH; /* obj index = game x + base */
                int x0 = ox > lo ? ox : lo, x1 = ox + bw < hi ? ox + bw : hi, gx;

                for (gx = x0; gx < x1; gx++)
                {
                    int i = gx + base, tx = gx - ox, tyy = ty, index;

                    if (affine)
                    {
                        int cx = tx - (bw >> 1), cy = row - (bh >> 1);

                        tx = (pa * cx + pb * cy + (ow << 7)) >> 8;
                        tyy = (pc * cx + pd * cy + (oh << 7)) >> 8;
                        if (tx < 0 || tx >= ow || tyy < 0 || tyy >= oh)
                            continue;
                    }
                    else if (a1 & 0x1000)
                        tx = ow - 1 - tx;
                    if (obj->has[i] && obj->prio[i] <= prio)
                        continue;
                    index = ObjIndex4(a2 & 0x3FF, ow, tx, tyy, map1d);
                    if (index == 0)
                    {
                        if (obj->has[i])
                        {
                            obj->prio[i] = (uint8_t)prio;
                            obj->semi[i] = (uint8_t)(mode == 1);
                        }
                        continue;
                    }
                    obj->color[i] = (uint16_t)(RENDER_PLTT(256 + (a2 >> 12) * 16 + index) & 0x7FFF);
                    obj->prio[i] = (uint8_t)prio;
                    obj->has[i] = 1;
                    obj->semi[i] = (uint8_t)(mode == 1);
                    sWinner[i] = (int16_t)k;
                }
            }
        }
    }
    if (w->hpCount > 0)
        SideHp(line, obj);
}

/* window.c's spans after mGBA's clamp; then whether the window reaches x = 0 / 240. */
static void WinReach(uint16_t winh, int *left, int *right)
{
    int r = winh & 0xFF, l = winh >> 8;

    if (l > RENDER_FRAME_WIDTH && l > r)
        l = 0;
    if (r > RENDER_FRAME_WIDTH)
    {
        r = RENDER_FRAME_WIDTH;
        if (l > RENDER_FRAME_WIDTH)
            l = RENDER_FRAME_WIDTH;
    }
    if (r < l) /* wraps: [0, r) and [l, 240) */
    {
        *left = r > 0;
        *right = l < RENDER_FRAME_WIDTH;
    }
    else
    {
        *left = l == 0 && r > 0;
        *right = r >= RENDER_FRAME_WIDTH && r > l;
    }
}

static int WinInV(uint16_t winv, int line)
{
    int bottom = winv & 0xFF, top = winv >> 8;

    if (bottom >= top)
        return line >= top && line < bottom;
    return line >= top || line < bottom;
}

static void SideMask(int line, const struct RenderFxRegs *fx, uint8_t *mask)
{
    uint16_t dispcnt = fx->dispcnt;
    int id, i;

    if (!(dispcnt & 0xE000))
    {
        memset(mask, RENDER_WIN_ALL, WIDE_SIDE);
        return;
    }
    memset(mask, fx->winout & RENDER_WIN_ALL, WIDE_SIDE); /* outside (the OBJ window is not drawn here) */
    for (id = 1; id >= 0; id--)
    {
        uint16_t winh = id ? fx->win1h : fx->win0h, winv = id ? fx->win1v : fx->win0v;
        uint8_t bits = (uint8_t)((id ? fx->winin >> 8 : fx->winin) & RENDER_WIN_ALL);
        int left, right;

        if (!(dispcnt & (id ? 0x4000 : 0x2000)) || !WinInV(winv, line))
            continue;
        WinReach(winh, &left, &right);
        for (i = 0; i < WIDE_SIDE; i++)
            if (i < sOff ? left : right)
                mask[i] = bits;
    }
}

/* Scratch for one line's sides, 240 wide for the compositor (only the first WIDE_SIDE are used). */
static uint16_t sCol3[RENDER_FRAME_WIDTH], sCol2[RENDER_FRAME_WIDTH], sCol1[RENDER_FRAME_WIDTH], sOut[RENDER_FRAME_WIDTH];
static uint8_t sInMap[WIDE_SIDE], sMask[RENDER_FRAME_WIDTH];
static struct RenderObjLine sObj;
static int sScratchReady;

void RenderWideLine(int line, const struct RenderLineRegs *regs, const struct RenderFxRegs *fx)
{
    struct RenderComposeBg bg;
    uint16_t *dst;
    int i;

    if (!sMap || line < 0 || line >= RENDER_FRAME_HEIGHT || (regs->dispcnt & 0x80))
        return;
    if (!sScratchReady)
    {
        for (i = 0; i < RENDER_FRAME_WIDTH; i++)
            sCol3[i] = sCol2[i] = sCol1[i] = RENDER_TRANSPARENT;
        memset(sMask, RENDER_WIN_ALL, sizeof(sMask));
        memset(&sObj, 0, sizeof(sObj));
        sScratchReady = 1;
    }

    memset(&bg, 0, sizeof(bg));
    memset(sInMap, 1, sizeof(sInMap));
    if ((regs->dispcnt & 7) == 0 && (regs->dispcnt & 0x0800))
    {
        SideBg(3, line, regs, sCol3, sInMap, 0);
        bg.color[3] = sCol3;
        bg.on[3] = 1;
        bg.prio[3] = regs->bgcnt[3] & 3;
    }
    if ((sBg2Map || sBg2Range) && (regs->dispcnt & 7) == 0 && (regs->dispcnt & 0x0400))
    {
        SideBg(2, line, regs, sCol2, NULL, sBg2Range);
        bg.color[2] = sCol2;
        bg.on[2] = 1;
        bg.prio[2] = regs->bgcnt[2] & 3;
    }
    /* v1.28.0: the phase banner's square wipe (BG1 matched at the start of the frame, still unscrolled here),
       on lines where BG1 is the alpha blend's first target, as the wipe always is (PhaseIntroVMatchHi / Lo).
       On the banner's first frame the VCount chain has not run yet and PhaseIntroVMatchMid's BLDCNT (BG0 the
       first target) is still in effect down to line 96: the game's squares are then opaque there, but the
       middle's are all empty above the band, while the sides' (further along the wipe) are not: drawn, they
       showed as a frame of black streaks (e16map 6638, 7847) */
    if (sSquares && (regs->dispcnt & 7) == 0 && (regs->dispcnt & 0x0200) && (regs->bghofs[1] & 0x1FF) == 0 &&
        (regs->bgvofs[1] & 0x1FF) == 0 && (fx->bldcnt & 0x00C2) == 0x0042)
    {
        SideSquares(line, regs, sCol1);
        bg.color[1] = sCol1;
        bg.on[1] = 1;
        bg.prio[1] = regs->bgcnt[1] & 3;
    }
    memset(sObj.has, 0, WIDE_SIDE);
    memset(sObj.semi, 0, WIDE_SIDE);
    if (regs->dispcnt & 0x1000)
        SideObj(line, regs->dispcnt, &sObj);
    SideMask(line, fx, sMask);

    RenderComposeLine(line, fx, &bg, (uint16_t)(RENDER_PLTT(0) & 0x7FFF), &sObj, sMask, sOut);

    dst = gRenderWide555[line];
    for (i = 0; i < WIDE_SIDE; i++)
        dst[i < sOff ? i : i + RENDER_FRAME_WIDTH] = sInMap[i] ? sOut[i] : 0;
    sLineDone[line] = 1;
}

int RenderWideLineDone(int line)
{
    return sMap && line >= 0 && line < RENDER_FRAME_HEIGHT && sLineDone[line];
}

int RenderWideFrameOff(int *map, int *reason)
{
    if (map != NULL)
        *map = sMap;
    if (reason != NULL)
        *reason = sReason;
    return sOff;
}

static void __attribute__((constructor)) WideLogInit(void)
{
    const char *s = getenv("FE8PC_WIDE_LOG");

    sLogOn = s != NULL && s[0] == '1';
}

/* Save states (port/include/port_state.h): not game state: per-frame working values and scratch. */
PORT_STATE_SESSION(sMap)
PORT_STATE_SESSION(sKind)
PORT_STATE_SESSION(sSliding)
PORT_STATE_SESSION(sOff)
PORT_STATE_SESSION(sReason)
PORT_STATE_SESSION(sLastLogged)
PORT_STATE_SESSION(sLogOn)
PORT_STATE_SESSION(sBg2Map)
PORT_STATE_SESSION(sBg2Range)
PORT_STATE_SESSION(sRangeMiss)
PORT_STATE_SESSION(sAttackWord)
PORT_STATE_SESSION(sRowShade)
PORT_STATE_SESSION(sBg2Local)
PORT_STATE_SESSION(sSquares)
PORT_STATE_SESSION(sSqOut)
PORT_STATE_SESSION(sSqT)
PORT_STATE_SESSION(sCol1)
PORT_STATE_SESSION(sWinner)
PORT_STATE_SESSION(sDetail)
PORT_STATE_SESSION(sLineDone)
PORT_STATE_SESSION(sRef)
PORT_STATE_SESSION(sCol3)
PORT_STATE_SESSION(sCol2)
PORT_STATE_SESSION(sOut)
PORT_STATE_SESSION(sInMap)
PORT_STATE_SESSION(sMask)
PORT_STATE_SESSION(sObj)
PORT_STATE_SESSION(sScratchReady)

#endif /* PORT_RFO_WIDE */
