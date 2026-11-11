#include "global.h"

/*
 * Reforged Options, Helpers: "Skill banners" (RFO_SKILL_BANNERS, v1.14.0; display-only, on by default).
 * Awakening-style: when a battle skill activates, its name shows.
 *
 *   - What procced is recorded by reforged_skills_battle.c (RecordProc) while a real battle is generated:
 *     gRfoSkillProcPc[hit][0 striker / 1 defender], one bit per RFO_PROC_*, and BATTLE_HIT_ATTR_18 on the
 *     hit as the "this hit has a record" mark (reforged_skills.h). Simulations and scripted battles record
 *     nothing; a hit array written any other way (staves, event battles, promotions) has no bit 18, so it
 *     never shows an old record.
 *   - Battle animations: Reforged_SkillBannerTick runs each frame of MainUpdateEkrBattle (include/bmbattle.h
 *     RFO_ASTRA_COPIES, after the Astra copies). When either side's anim starts a new round (its nextRoundId,
 *     the counter AstraDrawSide also reads), the round's hits (gRfoAstraSrcPc: an Astra round merged from
 *     5 strikes shows its skills once) give the banners: the striker's skills (Luna, Sol, Astra, Vantage,
 *     Lifetaker) on the striker's side, the defender's (Pavise, Aegis, Miracle) on the other, each a small
 *     panel with the name that slides in from that side's edge just above its HP box, holds, and slides
 *     back out. Two in one round stack.
 *   - Map animations: Reforged_SkillBannerMapTick runs in OnVBlank (Reforged_MapHpBarsVBlank). While
 *     ProcScr_MapAnimBattle runs, each round it starts (gManimSt.pCurrentRound, which
 *     MapAnim_AdvanceBattleRound steps past the hit it starts) shows a short popup by the unit that has
 *     the skill.
 *   - Drawing: gReforgedScreenOvl (port_reforged.h), a whole-screen picture the renderer lays over the
 *     finished lines (port/render/obj_line.c, PORT_RFO_MAPOVERLAY): no OAM, VRAM, palette or BG is used,
 *     and the game's state is only read. The text is the game's own system font (TextGlyphs_System).
 *   Armsthrift is recorded but shows nothing (Awakening shows none); Despoil, Poison Strike, Renewal and
 *   the passive skills have no banner.
 */

#if PORT_PLAYED_BUILD

#include <string.h>

#include "proc.h"
#include "bmunit.h"
#include "bmbattle.h"
#include "fontgrp.h"
#include "anime.h"
#include "ekrbattle.h"
#include "mapanim.h"
#include "variables.h"

#include "port_hw.h"
#include "port_reforged.h"
#include "reforged_skills.h"

struct ReforgedScreenOverlay gReforgedScreenOvl;
unsigned short gRfoSkillProcPc[BATTLE_HIT_MAX][2];
extern u8 gRfoAstraRoundPc[]; /* reforged_astra_anim.c */

void Reforged_SkillProcClear(void)
{
    memset(gRfoSkillProcPc, 0, sizeof(gRfoSkillProcPc));
}

/* RFO_PROC_* -> the skill (its name); 0: no banner */
static const u8 sProcSkill[RFO_PROC_COUNT] = {
    [RFO_PROC_LUNA] = SK_LUNA,           [RFO_PROC_SOL] = SK_SOL,
    [RFO_PROC_PAVISE] = SK_PAVISE,       [RFO_PROC_AEGIS] = SK_AEGIS,
    [RFO_PROC_MIRACLE] = SK_MIRACLE,     [RFO_PROC_ASTRA] = SK_ASTRA,
    [RFO_PROC_VANTAGE] = SK_VANTAGE,     [RFO_PROC_LIFETAKER] = SK_LIFETAKER,
    [RFO_PROC_ARMSTHRIFT] = 0,
};
/* the order shown when one round has several */
static const u8 sProcOrder[] = {
    RFO_PROC_VANTAGE, RFO_PROC_ASTRA, RFO_PROC_LUNA, RFO_PROC_SOL, RFO_PROC_LIFETAKER,
    RFO_PROC_PAVISE, RFO_PROC_AEGIS, RFO_PROC_MIRACLE,
};

static int BannersOn(void)
{
    return Reforged_GetOption(RFO_SKILL_BANNERS) != 0;
}

/* ---- the picture ---- */

#define COL_PANEL   0x2862                          /* dark blue, half over the game */
#define COL_EDGE    (RFO_SCREEN_OPAQUE | 0x1B1F)    /* gold */
#define COL_TEXT    (RFO_SCREEN_OPAQUE | 0x7FFF)    /* white */
#define COL_SHADOW  (RFO_SCREEN_OPAQUE | 0x0C63)    /* dark grey */
#define COL_TEXT2   (RFO_SCREEN_OPAQUE | 0x5EF7)    /* light grey */
#define BANNER_H 15

static int sOvlUsed; /* rows were written since the last clear */

static void OvlClear(void)
{
    int y;

    if (!sOvlUsed)
        return;
    for (y = 0; y < RFO_SCREEN_H; y++)
        if (gReforgedScreenOvl.rowUsed[y]) {
            memset(gReforgedScreenOvl.pix[y], 0, sizeof(gReforgedScreenOvl.pix[y]));
            gReforgedScreenOvl.rowUsed[y] = 0;
        }
    sOvlUsed = 0;
}

static void Put(int x, int y, unsigned short c)
{
    if (x < 0 || y < 0 || x >= RFO_SCREEN_W || y >= RFO_SCREEN_H)
        return;
    gReforgedScreenOvl.pix[y][x] = c;
    gReforgedScreenOvl.rowUsed[y] = 1;
    sOvlUsed = 1;
}

static struct Glyph *GlyphOf(char ch)
{
    struct Glyph *g = TextGlyphs_System[(u8)ch];

    return g != NULL ? g : TextGlyphs_System['?'];
}

static int TextWidth(const char *s)
{
    int w = 0;

    for (; *s; s++) {
        struct Glyph *g = GlyphOf(*s);
        w += g != NULL ? g->width : 0;
    }
    return w;
}

/* the 2-bit pixels: 1 the letter's face, 2 its shadow, 3 the edge shade (the system font as the
   game's white text uses it) */
static void DrawText(int x, int y, const char *s)
{
    static const unsigned short shade[4] = { 0, COL_TEXT, COL_SHADOW, COL_TEXT2 };

    for (; *s; s++) {
        struct Glyph *g = GlyphOf(*s);
        int r, c;

        if (g == NULL)
            continue;
        for (r = 0; r < 16; r++)
            for (c = 0; c < 16 && c < g->width + 1; c++) {
                int v = (g->bitmap[r] >> (c * 2)) & 3;

                if (v)
                    Put(x + c, y + r, shade[v]);
            }
        x += g->width;
    }
}

/* The banner: a rounded panel, a gold rim and a gold bar on the side it came from, the name inside.
   x is its left edge; fromRight: it belongs to the right-hand side. */
static void DrawBanner(int x, int y, int w, const char *name, int fromRight)
{
    int i, j;

    for (j = 0; j < BANNER_H; j++)
        for (i = 0; i < w; i++) {
            int edgeY = j == 0 || j == BANNER_H - 1, edgeX = i == 0 || i == w - 1;

            if (edgeX && edgeY)
                continue; /* the rounded corners */
            if (edgeX || edgeY)
                Put(x + i, y + j, COL_EDGE);
            else if ((!fromRight && i <= 2) || (fromRight && i >= w - 3))
                Put(x + i, y + j, COL_EDGE);
            else
                Put(x + i, y + j, COL_PANEL);
        }
    DrawText(x + (fromRight ? 4 : 6), y - 1, name);
}

static void OvlStamp(void)
{
    gReforgedScreenOvl.frame = gPortFrameCount;
    gReforgedScreenOvl.active = sOvlUsed;
}

/* ---- battle animations ---- */

#define BB_MAX 6
#define BB_IN 6      /* frames to slide in */
#define BB_HOLD 54
#define BB_OUT 6
#define BB_LIFE (BB_IN + BB_HOLD + BB_OUT)

struct BattleBanner {
    u8 on;
    u8 pos;  /* EKR_POS_L / EKR_POS_R */
    u8 slot; /* stacking, 0 the lowest */
    u8 skill;
    u16 age;
};

static struct BattleBanner sBB[BB_MAX];
static int sRoundSeen; /* the last animation round whose banners were queued (-1: none yet) */

void Reforged_SkillBannerBattleStart(void)
{
    memset(sBB, 0, sizeof(sBB));
    sRoundSeen = -1;
}

static void BattleQueue(int pos, int skill, int slot)
{
    int k, oldest = 0;

    for (k = 0; k < BB_MAX; k++)
        if (!sBB[k].on)
            break;
    if (k == BB_MAX) {
        for (k = 1; k < BB_MAX; k++)
            if (sBB[k].age > sBB[oldest].age)
                oldest = k;
        k = oldest;
    }
    sBB[k].on = 1;
    sBB[k].pos = (u8)pos;
    sBB[k].slot = (u8)slot;
    sBB[k].skill = (u8)skill;
    sBB[k].age = 0;
}

/* A new animation round r: its hits' records give the banners. */
static void BattleRound(int r)
{
    int src, run, k, n, side, pos, slot[2] = { 0, 0 };
    unsigned mask[2] = { 0, 0 };
    const struct BattleHit *h;

    if (r < 0 || r >= BATTLE_HIT_MAX)
        return;
    src = gRfoAstraSrcPc[r];
    run = gRfoAstraRoundPc[r] > 0 ? gRfoAstraRoundPc[r] : 1;
    for (k = src; k < src + run && k < BATTLE_HIT_MAX - 1; k++) {
        if (gBattleHitArray[k].info & BATTLE_HIT_INFO_END)
            break;
        if (!(gBattleHitArray[k].attributes & BATTLE_HIT_ATTR_18))
            continue;
        mask[0] |= gRfoSkillProcPc[k][0];
        mask[1] |= gRfoSkillProcPc[k][1];
    }
    if (!(mask[0] | mask[1]) || src >= BATTLE_HIT_MAX - 1)
        return;

    /* the striker's side: as Reforged_AstraMergeHits works it out */
    h = &gBattleHitArray[src];
    pos = (gBanimPositionIsEnemy[EKR_POS_L] == ((h->info & BATTLE_HIT_INFO_RETALIATION) ? 1 : 0)) ? EKR_POS_L : EKR_POS_R;

    /* a new round's banners replace what the same side still shows */
    for (side = 0; side < 2; side++) {
        int p = side == 0 ? pos : 1 - pos;

        if (!mask[side])
            continue;
        for (k = 0; k < BB_MAX; k++)
            if (sBB[k].on && sBB[k].pos == p && sBB[k].age < BB_LIFE - BB_OUT)
                sBB[k].age = BB_LIFE - BB_OUT; /* slide out now */
    }

    for (n = 0; n < (int)sizeof(sProcOrder); n++) {
        int proc = sProcOrder[n];

        for (side = 0; side < 2; side++)
            if ((mask[side] & (1u << proc)) && sProcSkill[proc] != 0) {
                int p = side == 0 ? pos : 1 - pos;

                BattleQueue(p, sProcSkill[proc], slot[side]++);
            }
    }
    {
        static int sLogged;
        if (sLogged < 40) {
            sLogged++;
            PortLog("[reforged] skill banners: battle round %d (hits %d-%d): striker side %d skills 0x%X, defender 0x%X (frame %u)\n",
                    r, src, src + run - 1, pos, mask[0], mask[1], gPortFrameCount);
        }
    }
}

static int SlideOffset(int age, int w)
{
    int dist = w + 8;

    if (age < BB_IN)
        return dist * (BB_IN - age) / BB_IN;
    if (age >= BB_LIFE - BB_OUT)
        return dist * (age - (BB_LIFE - BB_OUT) + 1) / BB_OUT;
    return 0;
}

/* Just above the battle screen's bottom panel (the HP boxes, y 112-159), at each side's edge. */
#define BB_Y 92
#define BB_STEP (BANNER_H + 2)

void Reforged_SkillBannerTick(void)
{
    int k, v = 0, any = 0, pos;

    if (!BannersOn()) {
        if (sOvlUsed)
            OvlClear(), OvlStamp();
        return;
    }

    /* a round started: the highest round either side's anim has reached */
    for (pos = 0; pos < 2; pos++) {
        struct Anim *anim = gAnims[pos * 2];
        int id;

        if (anim == NULL)
            continue;
        id = (int)(u16)anim->nextRoundId;
        if (id > 0 && id <= BATTLE_HIT_MAX && id > v)
            v = id;
    }
    while (sRoundSeen < v - 1)
        BattleRound(++sRoundSeen);

    OvlClear();
    for (k = 0; k < BB_MAX; k++) {
        struct BattleBanner *b = &sBB[k];
        const char *name;
        int w, x, y, off;

        if (!b->on)
            continue;
        if (b->age >= BB_LIFE) {
            b->on = 0;
            continue;
        }
        name = Reforged_SkillName(b->skill);
        w = TextWidth(name) + 14;
        off = SlideOffset(b->age, w);
        y = BB_Y - b->slot * BB_STEP;
        if (b->pos == EKR_POS_L)
            x = 3 - off;
        else
            x = RFO_SCREEN_W - 3 - w + off;
        DrawBanner(x, y, w, name, b->pos != EKR_POS_L);
        b->age++;
        any = 1;
    }
    (void)any;
    OvlStamp();
}

/* ---- map animations ---- */

#define MP_MAX 4
#define MP_LIFE 48

struct MapPopup {
    u8 on;
    u8 actor; /* gManimSt.actor[] */
    u8 slot;
    u8 skill;
    u16 age;
};

static struct MapPopup sMP[MP_MAX];
static const struct BattleHit *sMapRoundSeen;

static void MapQueue(int actor, int skill)
{
    int k, slot = 0, oldest = 0;

    for (k = 0; k < MP_MAX; k++)
        if (sMP[k].on && sMP[k].actor == actor && sMP[k].age == 0)
            slot++;
    for (k = 0; k < MP_MAX; k++)
        if (!sMP[k].on)
            break;
    if (k == MP_MAX) {
        for (k = 1; k < MP_MAX; k++)
            if (sMP[k].age > sMP[oldest].age)
                oldest = k;
        k = oldest;
    }
    sMP[k].on = 1;
    sMP[k].actor = (u8)actor;
    sMP[k].slot = (u8)slot;
    sMP[k].skill = (u8)skill;
    sMP[k].age = 0;
}

static void MapRound(void)
{
    const struct BattleHit *h = gManimSt.pCurrentRound - 1;
    int i = (int)(h - gBattleHitArray), n, side;

    if (i < 0 || i >= BATTLE_HIT_MAX - 1 || !(h->attributes & BATTLE_HIT_ATTR_18))
        return;
    /* the round MapAnim_AdvanceBattleRound has just started (its copies agree) */
    if (gManimSt.hitInfo != h->info || gManimSt.hitAttributes != (u16)h->attributes)
        return;
    if (!(gRfoSkillProcPc[i][0] | gRfoSkillProcPc[i][1]))
        return;

    memset(sMP, 0, sizeof(sMP)); /* a new round's popups replace the last round's */
    for (n = 0; n < (int)sizeof(sProcOrder); n++) {
        int proc = sProcOrder[n];

        for (side = 0; side < 2; side++)
            if ((gRfoSkillProcPc[i][side] & (1u << proc)) && sProcSkill[proc] != 0)
                MapQueue(side == 0 ? gManimSt.subjectActorId : gManimSt.targetActorId, sProcSkill[proc]);
    }
    {
        static int sLogged;
        if (sLogged < 40) {
            sLogged++;
            PortLog("[reforged] skill banners: map round, hit %d: striker (actor %d) skills 0x%X, defender (actor %d) 0x%X (frame %u)\n",
                    i, gManimSt.subjectActorId, gRfoSkillProcPc[i][0], gManimSt.targetActorId, gRfoSkillProcPc[i][1],
                    gPortFrameCount);
        }
    }
}

/* Where a popup goes: on the side of its unit away from the HP window (MapAnim's info box, rows
   hp_info_y*8 to +32); with the foe in another column, flush with the unit's outer edge (away from
   the foe), so the two units' popups never cover each other; with the foe in the same column, the
   unit farther from the window takes that side and the nearer one goes beside its square. */
static void MapPlace(const struct MapPopup *p, const struct Unit *u, int ux, int uy, int w, int *px, int *py)
{
    const struct Unit *o = NULL;
    int boxBelow = gManimSt.actor[0].hp_info_y * 8 > uy, step = BANNER_H + 1;

    if (gManimSt.actorCount > 1 && p->actor < 2)
        o = gManimSt.actor[1 - p->actor].unit;

    if (o != NULL && o != u && o->xPos == u->xPos && o->yPos != u->yPos
        && (boxBelow ? o->yPos < u->yPos : o->yPos > u->yPos)) {
        /* the nearer of two in one column: beside its square */
        *px = ux + 18;
        if (*px + w > RFO_SCREEN_W - 2)
            *px = ux - 2 - w;
        *py = uy + 1 + p->slot * step;
        return;
    }
    if (o != NULL && o != u && o->xPos != u->xPos)
        *px = o->xPos > u->xPos ? ux + 16 - w : ux;
    else
        *px = ux + 8 - w / 2;
    *py = boxBelow ? uy - BANNER_H - 1 - p->slot * step : uy + 17 + p->slot * step;
}

void Reforged_SkillBannerMapTick(void)
{
    int k, any = 0;

    if (!BannersOn() || Proc_Find(ProcScr_MapAnimBattle) == NULL) {
        int was = 0;

        for (k = 0; k < MP_MAX; k++)
            was |= sMP[k].on;
        memset(sMP, 0, sizeof(sMP));
        sMapRoundSeen = NULL;
        if (was)
            OvlClear(), OvlStamp();
        return;
    }

    if (gManimSt.pCurrentRound != sMapRoundSeen) {
        sMapRoundSeen = gManimSt.pCurrentRound;
        MapRound();
    }

    for (k = 0; k < MP_MAX; k++)
        any |= sMP[k].on;
    if (!any)
        return;

    OvlClear();
    for (k = 0; k < MP_MAX; k++) {
        struct MapPopup *p = &sMP[k];
        const struct Unit *u;
        const char *name;
        int w, ux, uy, x, y, rise;

        if (!p->on)
            continue;
        if (p->age >= MP_LIFE || p->actor >= 4 || (u = gManimSt.actor[p->actor].unit) == NULL) {
            p->on = 0;
            continue;
        }
        name = Reforged_SkillName(p->skill);
        w = TextWidth(name) + 14;
        ux = u->xPos * 16 - gBmSt.camera.x;
        uy = u->yPos * 16 - gBmSt.camera.y;
        rise = p->age < 4 ? 4 - p->age : 0;
        MapPlace(p, u, ux, uy, w, &x, &y);
        y += (y < uy) ? -rise : rise;
        if (x < 1)
            x = 1;
        if (x > RFO_SCREEN_W - 1 - w)
            x = RFO_SCREEN_W - 1 - w;
        if (y < 0)
            y = 0;
        if (y > RFO_SCREEN_H - BANNER_H)
            y = RFO_SCREEN_H - BANNER_H;
        DrawBanner(x, y, w, name, x + w / 2 > ux + 8);
        p->age++;
    }
    OvlStamp();
}

#endif /* PORT_PLAYED_BUILD */
