#include "global.h"

/*
 * Reforged Options, Game fixes: "Pair Up" (RFO_FIX_PAIR_UP; the user's request of 2026-11-07:
 * "add pairing up from Fates as well? it would help balance out the difficulty of having
 * terrible units at the very beginning"). Off by default; fixed at New Game like the other
 * game fixes (Reforged_RunFix), so a save is marked. Built on FE8's Rescue, for the player's
 * own units only (enemy, NPC and event rescues stay the original's):
 *
 *   - Pairing: Rescue, Take and Give need no Aid >= Con (CanUnitRescue, RFO_PAIR_ANY_RESCUE).
 *   - The lead (the rescuer) keeps its full Skill and Speed (GetUnitSkill/Speed, the stat
 *     screen: RFO_RESCUE_HALVES) and gets the partner's boost (Reforged_PairBonus, added in
 *     each stat getter: RFO_PAIR_BONUS): the partner's two best stats (Str/Mag, Skl, Spd, Lck,
 *     Def, Res, from its own numbers) give +2 each, +3 if the partner's class is promoted, +1
 *     more per support rank (C/B/A) between the two (the owner's "Moderate" choice). The
 *     partner's support bonuses (Hit, Avoid, ...) count as if it stood next to the lead
 *     (bmreliance.c GetUnitSupportBonuses: RFO_PAIR_SUPPORT_NEAR). The boost counts in the arena
 *     too (its opponent is tuned to the stats the getters give); the gauge does not fill there.
 *   - Dual Guard: a gauge on the lead (0-10 steps of 10%, kept in unit state bits 28-31, which
 *     FE8 never uses, so suspend saves, Rewind and battles carry it; a pair that breaks clears
 *     it, UnitDrop: RFO_PAIR_RESET): +10% per hit the
 *     lead lands, +20% per hit it takes (the owner's "Fates-like" choice). When it is full
 *     the partner blocks the next enemy hit completely (no damage, no poison or other effect;
 *     the battle shows the Great Shield flash) and the gauge empties. No random number is
 *     used, so the AI's and the forecast's simulations see the same thing.
 *   - Swap: a free command, as often as the player likes (the owner's choice): the partner
 *     becomes the lead on the same square and carries on the turn's action (a partner that
 *     has already acted cannot swap in). The move is committed, as after a trade.
 *   - Supports: a pair gains support points twice as fast (bmreliance.c
 *     ProcessTurnSupportExp, RFO_PAIR_SUPPORT_EXTRA); the extra is not counted in the
 *     chapter's support total (the tactics rank).
 */

#if PORT_PLAYED_BUILD

#include "bmunit.h"
#include "bmbattle.h"
#include "bmreliance.h"
#include "bmmap.h"
#include "bmudisp.h"
#include "bmmind.h"
#include "uimenu.h"
#include "mu.h"
#include "variables.h"
#include "statscreen.h"
#include "uiutils.h"
#include "bmlib.h"
#include "bm.h"
#include "menu_def.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "port_hw.h"
#include "port_reforged.h"
#include "reforged_skills.h"

#define GAUGE_FULL  10 /* steps of 10% */
#define GAUGE_HIT   1  /* +10%: a hit the lead lands */
#define GAUGE_TAKEN 2  /* +20%: a hit the lead takes */
#define GAUGE_MASK  0xF0000000u /* unit state bits 28-31 (bit 24 is the map sprite's shake, bmudisp.c) */

#define RFO_VMSG_SWAP      0x7F40 /* reforged_items.c Reforged_VirtualMsg */
#define RFO_VMSG_SWAP_HELP 0x7F41
#define BM_TAKEN_ACTION_SWAP (1 << 4) /* include/types.h BmSt_TakenAction: bits 0-3 are the game's */

static int On(void)
{
    return Reforged_RunFix(RFO_FIX_PAIR_UP);
}

static int Blue(const struct Unit *u)
{
    return u != NULL && u->pCharacterData != NULL && UNIT_FACTION(u) == FACTION_BLUE;
}

/* The partner of a pair's lead, or NULL (not a player pair, or the option off). */
static struct Unit *PartnerOf(const struct Unit *lead)
{
    struct Unit *p;

    if (!On() || !Blue(lead) || !(lead->state & US_RESCUING) || lead->rescue == 0)
        return NULL;
    p = GetUnit(lead->rescue);
    if (!Blue(p) || !(p->state & US_RESCUED))
        return NULL;
    return p;
}

int Reforged_PairOn(const struct Unit *unit)
{
    if (unit == NULL || !On() || !Blue(unit))
        return 0;
    if (unit->state & US_RESCUING)
        return PartnerOf(unit) != NULL;
    if ((unit->state & US_RESCUED) && unit->rescue != 0)
        return PartnerOf(GetUnit(unit->rescue)) != NULL;
    return 0;
}

int Reforged_RescueHalves(const struct Unit *unit)
{
    /* Savior (class skills, reforged_skills.h): no Skl/Spd halving while rescuing; also what
       the stat screen's halved-stat arrows ask, so they agree. */
    return (unit->state & US_RESCUING) && PartnerOf(unit) == NULL && !Reforged_SkillHas(unit, SK_SAVIOR);
}

int Reforged_PairAnyRescue(const struct Unit *actor, const struct Unit *target)
{
    return On() && Blue(actor) && Blue(target);
}

/* ---- the stat boost ---- */

int Reforged_PairBonus(const struct Unit *unit, int stat)
{
    const struct Unit *p = PartnerOf(unit);
    int v[6], a = 0, b = -1, i, amt, num;

    if (p == NULL || stat < 0 || stat > 5)
        return 0;
    v[0] = p->pow; v[1] = p->skl; v[2] = p->spd; v[3] = p->lck; v[4] = p->def; v[5] = p->res;
    for (i = 1; i < 6; i++)
        if (v[i] > v[a])
            a = i;
    for (i = 0; i < 6; i++)
        if (i != a && (b < 0 || v[i] > v[b]))
            b = i;
    if (stat != a && stat != b)
        return 0;
    amt = (UNIT_CATTRIBUTES(p) & CA_PROMOTED) ? 3 : 2;
    num = GetUnitSupporterNum((struct Unit *)unit, p->pCharacterData->number);
    if (num >= 0)
        amt += GetUnitSupportLevel((struct Unit *)unit, num);
    return amt;
}

/* The partner's support bonuses count wherever the pair stands. */
int Reforged_PairPartnerOf(const struct Unit *unit, const struct Unit *other)
{
    return other != NULL && PartnerOf(unit) == other;
}

/* A pair gains support points twice as fast; the extra is not in the chapter's total. */
void Reforged_PairSupportExtra(struct Unit *unit, int num, struct Unit *other)
{
    int total, n;

    if (!Reforged_PairOn(unit) || unit->rescue != other->index ||
        GetUnitTotalSupportLevel(other) >= MAX_SIMULTANEOUS_SUPPORT_COUNT)
        return;
    total = gPlaySt.chapterTotalSupportGain;
    for (n = Reforged_SupportMultiplier(); n > 0; n--) /* Faster supports (reforged_supports.c): as many times */
        UnitGainSupportExp(unit, num);
    gPlaySt.chapterTotalSupportGain = total;
}

/* ---- Dual Guard ---- */

int Reforged_PairGauge(const struct Unit *unit)
{
    return (int)((unit->state >> 28) & 0xF);
}

static void SetGauge(struct Unit *unit, int g)
{
    if (g < 0)
        g = 0;
    if (g > GAUGE_FULL)
        g = GAUGE_FULL;
    unit->state = (unit->state & ~GAUGE_MASK) | ((u32)(g & 0xF) << 28);
}

void Reforged_PairReset(struct Unit *unit)
{
    unit->state &= ~GAUGE_MASK;
}

/* BattleGenerateHitAttributes, after the hit roll: the defender's partner blocks the hit. */
int Reforged_PairDualGuard(struct BattleUnit *attacker, struct BattleUnit *defender)
{
    (void)attacker;
    if (PartnerOf(&defender->unit) == NULL || (gBattleStats.config & BATTLE_CONFIG_ARENA) ||
        Reforged_PairGauge(&defender->unit) < GAUGE_FULL)
        return 0;
    SetGauge(&defender->unit, 0);
    gBattleHitIterator->attributes |= BATTLE_HIT_ATTR_GREATSHLD | BATTLE_HIT_ATTR_17;
    gBattleStats.damage = 0;
    if (gBattleStats.config & BATTLE_CONFIG_REAL)
        PortLog("[reforged] pair up: Dual Guard: unit 0x%02X's partner blocks a hit\n",
            defender->unit.pCharacterData->number);
    return 1;
}

/* BattleGenerateHit, after the hit: the gauges fill. */
void Reforged_PairGaugeFill(struct BattleUnit *attacker, struct BattleUnit *defender)
{
    if (gBattleHitIterator->attributes & (BATTLE_HIT_ATTR_MISS | BATTLE_HIT_ATTR_17))
        return;
    if (gBattleStats.config & BATTLE_CONFIG_ARENA)
        return;
    if (PartnerOf(&attacker->unit) != NULL)
        SetGauge(&attacker->unit, Reforged_PairGauge(&attacker->unit) + GAUGE_HIT);
    if (PartnerOf(&defender->unit) != NULL)
        SetGauge(&defender->unit, Reforged_PairGauge(&defender->unit) + GAUGE_TAKEN);
}

/* The stat screen's Cond line (no status): "Guard" and the gauge in percent ("Guard 60%"), in the
   status text's own 9 tiles (the label takes the first 22 pixels). */
const char *Reforged_PairStatusName(const struct Unit *unit, const char *name)
{
    static char buf[16];

    if (PartnerOf(unit) == NULL)
        return name;
    if (Reforged_PairGauge(unit) >= GAUGE_FULL)
        return "Guard full"; /* "Guard 100%" would pass the text's 72 pixels */
    snprintf(buf, sizeof(buf), "Guard %d%%", Reforged_PairGauge(unit) * 10);
    return buf;
}

int Reforged_PairStatusX(const struct Unit *unit, int x)
{
    return PartnerOf(unit) != NULL ? 22 : x;
}

void Reforged_PairStatusGauge(const struct Unit *unit)
{
    (void)unit;
}

/* ---- Swap ---- */

u8 Reforged_SwapUsability(const struct MenuItemDef *def, int number)
{
    struct Unit *p = PartnerOf(gActiveUnit);

    (void)def;
    (void)number;
    if (p == NULL || (p->state & (US_UNSELECTABLE | US_DEAD)) || (gActiveUnit->state & US_IN_BALLISTA) ||
        p->statusIndex == UNIT_STATUS_SLEEP || p->statusIndex == UNIT_STATUS_BERSERK ||
        (UNIT_CATTRIBUTES(p) & CA_UNSELECTABLE))
        return MENU_NOTSHOWN; /* as the player could not select it (GetPlayerSelectKind) */
    return MENU_ENABLED;
}

u8 Reforged_SwapEffect(struct MenuProc *menu, struct MenuItemProc *menuItem)
{
    struct Unit *lead = gActiveUnit, *p = PartnerOf(gActiveUnit);
    int g;

    (void)menu;
    (void)menuItem;
    if (p == NULL)
        return MENU_ACT_SND6B;
    g = Reforged_PairGauge(lead);
    lead->state = (lead->state & ~US_RESCUING) | US_RESCUED | US_HIDDEN;
    p->state = (p->state & ~(US_RESCUED | US_HIDDEN)) | US_RESCUING;
    p->xPos = lead->xPos;
    p->yPos = lead->yPos;
    Reforged_PairReset(lead);
    SetGauge(p, g); /* the pair's gauge goes with the new lead */
    PortLog("[reforged] pair up: unit 0x%02X swaps in for 0x%02X\n", p->pCharacterData->number,
        lead->pCharacterData->number);

    gActiveUnit = p;
    gActionData.subjectIndex = p->index;
    gActionData.unitActionType = UNIT_ACTION_TRADED_1D; /* back to the action menu (playerphase.c) */
    gBmSt.taken_action |= BM_TAKEN_ACTION_SWAP;         /* committed: B no longer undoes the move */

    RefreshEntityBmMaps();
    RefreshUnitSprites();
    HideUnitSprite(p);
    EndAllMus();
    StartMu(p);
    return MENU_ACT_SKIPCURSOR | MENU_ACT_END | MENU_ACT_SND6A | MENU_ACT_CLEAR;
}

/* The unit action menu with Swap: the game's menus are its own ROM data in the port (src/menu_def.c
   is not compiled; port/Makefile FULL_EXCLUDE), so StartMenuCore (RFO_MENU_DEF) is given a copy of
   gUnitActionMenuDef whose rows are the game's with Swap before Wait. Made once; with Pair Up off
   the game's own menu is used. */
#define UNIT_MENU_ROWS_MAX 48
static struct MenuItemDef sUnitMenuRows[UNIT_MENU_ROWS_MAX];
static struct MenuDef sUnitMenuDef;
static int sUnitMenuMade;

const struct MenuDef *Reforged_MenuDef(const struct MenuDef *def)
{
    int i, n = 0, put = 0;

    if (def != &gUnitActionMenuDef || !On())
        return def;
    if (!sUnitMenuMade)
    {
        for (i = 0; def->menuItems[i].isAvailable && n < UNIT_MENU_ROWS_MAX - 2; i++)
        {
            if (!put && def->menuItems[i].overrideId == 0x6B) /* Wait */
            {
                struct MenuItemDef *w = &sUnitMenuRows[n++];

                *w = def->menuItems[i];
                w->name = "Swap";
                w->nameMsgId = RFO_VMSG_SWAP;
                w->helpMsgId = RFO_VMSG_SWAP_HELP;
                w->color = 4;
                w->overrideId = 0x6A;
                w->isAvailable = Reforged_SwapUsability;
                w->onDraw = NULL;
                w->onSelected = Reforged_SwapEffect;
                w->onIdle = NULL;
                w->onSwitchIn = NULL;
                w->onSwitchOut = NULL;
                put = 1;
            }
            sUnitMenuRows[n++] = def->menuItems[i];
        }
        memset(&sUnitMenuRows[n], 0, sizeof(sUnitMenuRows[n])); /* MenuItemsEnd */
        sUnitMenuDef = *def;
        sUnitMenuDef.menuItems = sUnitMenuRows;
        sUnitMenuMade = put ? 1 : -1;
        PortLog("[reforged] pair up: the unit menu has %d rows%s\n", n, put ? ", Swap before Wait" : " (no Wait row: no Swap)");
    }
    return sUnitMenuMade > 0 ? &sUnitMenuDef : def;
}

/* StartMenuCore: never more than MENU_ITEM_MAX rows (a paired lord with every command
   would have twelve); at the last row only the menu's last shown command goes in, so Wait
   stays and a row before it (Swap) is left out. */
int Reforged_MenuFits(const struct MenuDef *def, int i, int itemCount)
{
    int k;

    if (itemCount < MENU_ITEM_MAX - 1)
        return 1;
    if (itemCount >= MENU_ITEM_MAX)
        return 0;
    for (k = i + 1; def->menuItems[k].isAvailable; k++)
        if (def->menuItems[k].isAvailable(&def->menuItems[k], k) != MENU_NOTSHOWN)
            return 0;
    return 1;
}


/* ---- test switch ----
   FE8PC_PAIR_TEST=1: at the first free map of a player phase, Pair Up is switched on for the run,
   the first blue unit takes the second as its partner, and the log shows the lead's stats before
   and after, a simulated attack on the nearest enemy, and an enemy attack on the lead with a full
   gauge (Dual Guard). Nothing else changes; the pair stays for the frames that follow. */
static void LogStats(const char *when, struct Unit *u)
{
    PortLog("[reforged] pair test: %s: unit 0x%02X pow %d skl %d spd %d lck %d def %d res %d (raw skl %d spd %d)\n",
        when, u->pCharacterData->number, GetUnitPower(u), GetUnitSkill(u), GetUnitSpeed(u), GetUnitLuck(u),
        GetUnitDefense(u), GetUnitResistance(u), u->skl, u->spd);
}

void Reforged_PairTest(void *proc)
{
    static int done;
    struct Unit *a = NULL, *b = NULL, *e = NULL, *u;
    int i, best = 999;

    if (done || getenv("FE8PC_PAIR_TEST") == NULL)
        return;
    done = 1;
    Reforged_SetRunFix(RFO_FIX_PAIR_UP, 1);
    for (i = 1; i < 0x40 && b == NULL; i++)
    {
        u = GetUnit(i);
        if (!UNIT_IS_VALID(u) || (u->state & (US_DEAD | US_NOT_DEPLOYED | US_RESCUED | US_RESCUING)))
            continue;
        if (a == NULL)
            a = u;
        else
            b = u;
    }
    if (a == NULL || b == NULL)
    {
        PortLog("[reforged] pair test: two player units not found\n");
        return;
    }
    LogStats("alone", a);
    UnitRescue(a, b);
    RefreshEntityBmMaps();
    RefreshUnitSprites();
    LogStats("paired", a);
    PortLog("[reforged] pair test: can rescue with no Aid limit: %d (Aid %d, Con %d)\n",
        CanUnitRescue(b, a), GetUnitAid(b), UNIT_CON(a));
    PortLog("[reforged] pair test: partner 0x%02X raw pow %d skl %d spd %d lck %d def %d res %d, promoted %d; pair on %d %d\n",
        b->pCharacterData->number, b->pow, b->skl, b->spd, b->lck, b->def, b->res,
        (UNIT_CATTRIBUTES(b) & CA_PROMOTED) != 0, Reforged_PairOn(a), Reforged_PairOn(b));
    for (i = 0x81; i < 0xC0; i++)
    {
        u = GetUnit(i);
        if (UNIT_IS_VALID(u) && !(u->state & (US_DEAD | US_NOT_DEPLOYED)) &&
            RECT_DISTANCE(u->xPos, u->yPos, a->xPos, a->yPos) < best)
        {
            best = RECT_DISTANCE(u->xPos, u->yPos, a->xPos, a->yPos);
            e = u;
        }
    }
    if (e == NULL)
        return;
    BattleGenerateSimulation(a, e, e->xPos, e->yPos > 0 ? e->yPos - 1 : e->yPos + 1, -1);
    PortLog("[reforged] pair test: lead attacks 0x%02X at %d,%d: atk %d hit %d crit %d spd %d (enemy atk %d hit %d)\n",
        e->pCharacterData->number, e->xPos, e->yPos, gBattleActor.battleAttack, gBattleActor.battleHitRate, gBattleActor.battleCritRate,
        gBattleActor.battleSpeed, gBattleTarget.battleAttack, gBattleTarget.battleHitRate);
    SetGauge(a, GAUGE_FULL);
    BattleGenerateSimulation(e, a, a->xPos, a->yPos > 0 ? a->yPos - 1 : a->yPos + 1, -1);
    for (i = 0; i < BATTLE_HIT_MAX && !(gBattleHitArray[i].info & BATTLE_HIT_INFO_END); i++)
        PortLog("[reforged] pair test: full gauge, hit %d: attributes 0x%05X damage %d%s\n", i,
            gBattleHitArray[i].attributes, gBattleHitArray[i].hpChange,
            (gBattleHitArray[i].attributes & BATTLE_HIT_ATTR_17) ? " (Dual Guard)" : "");
    PortLog("[reforged] pair test: gauge after (the simulation's copy) %d, the real lead's %d\n",
        Reforged_PairGauge(&gBattleTarget.unit), Reforged_PairGauge(a));
    if (atoi(getenv("FE8PC_PAIR_TEST")) == 6)
    {
        /* 6: a real battle generation (not a simulation): the enemy, set next to the lead for the
           call, attacks the lead with a full gauge; the hits are logged, nothing is applied */
        int ex = e->xPos, ey = e->yPos, hp = a->curHP;

        SetGauge(a, GAUGE_FULL);
        e->xPos = a->xPos;
        e->yPos = a->yPos > 0 ? a->yPos - 1 : a->yPos + 1;
        for (i = 0; i < 12; i++)
        {
            SetGauge(a, GAUGE_FULL);
            BattleGenerateReal(e, a); /* again until the enemy's first swing would hit (the RNG moves on) */
            if (!(gBattleHitArray[0].attributes & BATTLE_HIT_ATTR_MISS))
                break;
        }
        e->xPos = ex;
        e->yPos = ey;
        for (i = 0; i < BATTLE_HIT_MAX && !(gBattleHitArray[i].info & BATTLE_HIT_INFO_END); i++)
            PortLog("[reforged] pair test: real battle, hit %d: attributes 0x%05X damage %d\n", i,
                gBattleHitArray[i].attributes, gBattleHitArray[i].hpChange);
        PortLog("[reforged] pair test: real battle: lead HP %d -> %d in the battle copy, gauge after %d\n", hp,
            gBattleTarget.unit.curHP, Reforged_PairGauge(&gBattleTarget.unit));
    }
    SetGauge(a, atoi(getenv("FE8PC_PAIR_TEST")) == 4 ? GAUGE_FULL : 6);
    PortLog("[reforged] pair test: paired at frame %u, lead at %d,%d\n", (unsigned)gPortFrameCount, a->xPos, a->yPos);
    if (atoi(getenv("FE8PC_PAIR_TEST")) >= 3)
        SetCursorMapPosition(a->xPos, a->yPos); /* 3: the cursor on the lead, for a key script */
    if (atoi(getenv("FE8PC_PAIR_TEST")) == 2 && proc != NULL)
        StartStatScreen(a, proc); /* 2: the lead's stat screen (gauge 60%) */
}

#endif /* PORT_PLAYED_BUILD */
