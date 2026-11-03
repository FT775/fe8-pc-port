#include "global.h"

/*
 * Reforged Options, "Game fixes >" (port/include/port_reforged.h RFO_FIX_*; their screen:
 * reforged_gamefixes_menu.c). Six changes to the original game's rules, each OFF by default:
 * off, its hook returns the original's value (or does nothing), so the game, its random
 * numbers and its saves are the original's. Each is a gameplay option (a save made with one
 * on is marked). Unlike the original-game bug fixes (RFO_BUG_FIXES, on by default, only where
 * the original hangs), these change results the original reaches without hanging, so the
 * player chooses them. Played builds only; empty in every test build. The game calls them
 * through same-line macros that are the original code in every other build:
 *
 *   RFO_FIX_EARLY_EXP    include/bmbattle.h RFO_EARLY_KILL_EXP. Before the route split
 *                        (Prologue to Chapter 8) a kill of a slightly stronger enemy gave less
 *                        EXP than a kill of an equal one (the pre-split bonus, P(t) - P(a) / 2,
 *                        stops when P(t) passes P(a)); the stronger kill's term is raised to the
 *                        equal kill's, ceil(P(a) / 2), so more strength never gives less EXP.
 *   RFO_FIX_PROMO_LEVELS include/bmunit.h RFO_PROMO_AUTOLEVEL. A generic unit loaded in a
 *                        promoted class gets 9 (Hard: 19) extra levels meant as its
 *                        pre-promotion levels (UnitAutolevel passes the base class), but
 *                        UnitAutolevelCore ignores the class it is given and rolls them with
 *                        the promoted class's growths. They are rolled with the base class's
 *                        (a real two-way promotion link only; placeholder links such as Cyclops
 *                        -> Armor Knight keep the original). The same random numbers are drawn.
 *                        A difficulty increase; Easy/Normal bosses re-levelled at load and arena
 *                        opponents (bmarena.c) too.
 *   RFO_FIX_AI_HEAL      include/cp_common.h RFO_AI_HEAL_WANTS. The AI's healers (Heal, Mend,
 *                        Recover, Physic, Rescue, Fortify's target count) could pick a unit at
 *                        full HP: every player unit with a usable tome is flagged as seeking
 *                        healing. A unit at full HP is skipped.
 *   RFO_FIX_CANTO        include/playerphase.h RFO_CANTO_MOVECOUNT. A unit that backs out of a
 *                        Trade or Supply screen after the menu showed an Attack or Staff range
 *                        lost Canto (the move count is read again from the erased movement map,
 *                        0xFF; the squares-moved record took the 255 too). The count stored by
 *                        the move itself is kept.
 *   RFO_FIX_DEVIL        include/bmbattle.h RFO_DEVIL_CHANCE. The Devil effect's backfire
 *                        chance, 31 - Luck, wrapped to 100% at Luck 32 and up (a randomized
 *                        weapon trait can give that); it stops at 0%.
 *   RFO_FIX_CH15_DROP    include/bmunit.h RFO_STAFF_DROP_ORDER. Chapter 15's dropping
 *                        Troubadour (both routes) carries Sleep or Berserk, then Mend; a unit
 *                        drops its last item, so the status staff could never be won (the
 *                        English release added the Mend). At load its two items swap, so the
 *                        status staff drops (and its stat screen marks it).
 *
 * Also here, as it needs the game's headers: one of the original-game bug fixes (RFO_BUG_FIXES, on by
 * default), Reforged_FixDancerWeaponBanim (include/anime.h BANIM_DANCER_WEAPON_MAPANIM).
 *
 * FE8PC_RFO_CHECK=1 (the test switch of reforged_dangerzone.c): at the first level-up check of
 * the run (with the cheats' self-check), a check of each fix on scratch units (the random
 * seeds, game state and options are put back), logged as "[reforged] game fixes check ...".
 */

#if PORT_PLAYED_BUILD

#include <stdlib.h>
#include <string.h>

#include "bmunit.h"
#include "bmbattle.h"
#include "bmitem.h"
#include "bmdifficulty.h"
#include "bmmap.h"
#include "bmmind.h"
#include "cp_common.h"
#include "playerphase.h"
#include "rng.h"
#include "variables.h"
#include "constants/chapters.h"
#include "constants/classes.h"
#include "constants/items.h"

#include "port_hw.h"
#include "port_reforged.h"

static int sInCheck; /* the self-check's own calls are not logged */

/* --- The run's fixes: fixed at New Game, kept with each save (reforged_savemark.c), never the menu's
   while a game is played, so a fix cannot be switched on or off in the middle of a run --- */
static u8 sRunFix[RFO_FIX_LAST - RFO_FIX_FIRST + 1];

int Reforged_RunFix(int id)
{
    /* the tutorial (GameControl_InitTutorial, its own session with no New Game): no fix */
    if (gPlaySt.chapterStateBits & PLAY_FLAG_TUTORIAL)
        return 0;
    return id >= RFO_FIX_FIRST && id <= RFO_FIX_LAST ? sRunFix[id - RFO_FIX_FIRST] : 0;
}

void Reforged_SetRunFix(int id, int value)
{
    if (id >= RFO_FIX_FIRST && id <= RFO_FIX_LAST)
        sRunFix[id - RFO_FIX_FIRST] = (u8)(value != 0);
}

void Reforged_GameFixesNewGame(void)
{
    int id, n = 0;

    for (id = RFO_FIX_FIRST; id <= RFO_FIX_LAST; id++)
    {
        Reforged_SetRunFix(id, Reforged_GetOption(id));
        n += Reforged_RunFix(id);
    }
    PortLog("[reforged] game fixes: the new game has %d of %d on (kept with its saves)\n", n,
        RFO_FIX_LAST - RFO_FIX_FIRST + 1);
}

/* --- Early-game kill EXP --- */

int Reforged_FixEarlyKillExp(int local, struct Unit *actor)
{
    int floorv;

    if (!Reforged_RunFix(RFO_FIX_EARLY_EXP))
        return local;
    floorv = GetUnitPowerLevel(actor) - GetUnitPowerLevel(actor) / 2; /* an equal-strength kill's term */
    if (local >= floorv)
        return local;
    if (!sInCheck)
        PortLog("[reforged] game fix: early kill EXP term %d raised to %d (an equal kill's)\n", local, floorv);
    return floorv;
}

/* --- Promoted enemies' pre-promotion levels --- */

/* The base class a promoted class's pre-promotion levels are rolled in, or the class itself:
   only for a real promotion (the base class is unpromoted and promotes to this one, by
   gPromoJidLut or its own .promotion). */
static const struct ClassData *PromoBaseClass(const struct ClassData *job)
{
    const struct ClassData *base;
    int b = job->promotion;

    if (b == 0 || b >= 0x80)
        return job;
    base = GetClassData(b);
    if (base == NULL || (base->attributes & CA_PROMOTED))
        return job;
    if (gPromoJidLut[b][0] == job->number || gPromoJidLut[b][1] == job->number || base->promotion == job->number)
        return base;
    return job;
}

const struct ClassData *Reforged_FixPromoAutolevelClass(struct Unit *unit)
{
    const struct ClassData *job = unit->pClassData, *base;

    if (!Reforged_RunFix(RFO_FIX_PROMO_LEVELS))
        return job;
    base = PromoBaseClass(job);
    if (base != job && !sInCheck)
        PortLog("[reforged] game fix: promoted autolevel: unit 0x%02X class 0x%02X levels %d in base class 0x%02X\n",
            unit->index, job->number, GetCurrentPromotedLevelBonus(), base->number);
    return base;
}

/* --- AI healers and full-HP units --- */

int Reforged_FixAiHealFullHp(struct Unit *unit, int wants)
{
    static int sLogged;

    if (!wants || !Reforged_RunFix(RFO_FIX_AI_HEAL))
        return wants;
    if (GetUnitCurrentHp(unit) < GetUnitMaxHp(unit))
        return 1;
    if (!sInCheck && sLogged < 20) /* each healer decision and Fortify count would log; the first 20 will do */
        sLogged++, PortLog("[reforged] game fix: AI unit 0x%02X skips unit 0x%02X at full HP as a healing target\n",
            gActiveUnitId, unit->index);
    return 0;
}

/* --- Canto after a Trade or Supply screen left with nothing moved --- */

int Reforged_FixCantoMoveCount(int mapValue, int kept, int movementMax)
{
    if (!Reforged_RunFix(RFO_FIX_CANTO) || mapValue < movementMax)
        return mapValue; /* the original; a real move's square is always below movementMax */
    if (!sInCheck)
        PortLog("[reforged] game fix: Canto kept after the trade/supply screen (move count %d kept; the "
                "original reads %d)\n", kept, mapValue);
    return kept;
}

/* --- Devil effect at Luck 32 and up --- */

int Reforged_FixDevilChance(int t)
{
    static int sLogged;

    if (!Reforged_RunFix(RFO_FIX_DEVIL) || t >= 0)
        return t;
    if (!sInCheck && sLogged < 20) /* every avoided backfire would log; the first 20 will do */
    {
        sLogged++;
        PortLog("[reforged] game fix: Devil effect at battle Luck %d: backfire chance 0%% (the original: 100%%)\n",
            31 - t);
    }
    return 0;
}

/* --- Chapter 15's Troubadour drop --- */

void Reforged_FixStaffDropOrder(struct Unit *unit)
{
    int first, tmp;

    if (unit == NULL || !Reforged_RunFix(RFO_FIX_CH15_DROP))
        return;
    if (gPlaySt.chapterIndex != CHAPTER_E_15 && gPlaySt.chapterIndex != CHAPTER_I_15)
        return;
    if (UNIT_FACTION(unit) != FACTION_RED || !(unit->state & US_DROP_ITEM) || unit->pClassData == NULL ||
        unit->pClassData->number != CLASS_TROUBADOUR || GetUnitItemCount(unit) != 2)
        return;
    first = ITEM_INDEX(unit->items[0]);
    if ((first != ITEM_STAFF_SLEEP && first != ITEM_STAFF_BERSERK) || ITEM_INDEX(unit->items[1]) != ITEM_STAFF_MEND)
        return;
    tmp = unit->items[0];
    unit->items[0] = unit->items[1];
    unit->items[1] = tmp;
    if (!sInCheck)
        PortLog("[reforged] game fix: Chapter 15 Troubadour (%d,%d): items Mend, %s; it now drops the %s staff\n",
            unit->xPos, unit->yPos, first == ITEM_STAFF_SLEEP ? "Sleep" : "Berserk",
            first == ITEM_STAFF_SLEEP ? "Sleep" : "Berserk");
}

/* --- Original-game bug fix (RFO_BUG_FIXES, on by default; include/anime.h BANIM_DANCER_WEAPON_MAPANIM):
   a Dancer fighting with a weapon gets the map animation, as her dance animation can hang the battle --- */

static int DancerWithWeapon(struct Unit *unit, int weapon)
{
    return unit != NULL && unit->pClassData != NULL && unit->pClassData->number == CLASS_DANCER &&
           weapon != 0 && (GetItemAttributes(weapon) & IA_WEAPON);
}

int Reforged_FixDancerWeaponBanim(struct Unit *unit1, int weapon1, struct Unit *unit2, int weapon2)
{
    int side;

    if (!Reforged_GetOption(RFO_BUG_FIXES))
        return 0;
    side = DancerWithWeapon(unit1, weapon1) ? 1 : DancerWithWeapon(unit2, weapon2) ? 2 : 0;
    if (side == 0)
        return 0;
    PortLog("[reforged] original-game bug fix: a Dancer fights with item 0x%02X: the map animation (her dance "
            "animation can hang the battle)\n", ITEM_INDEX(side == 1 ? weapon1 : weapon2));
    return 1;
}

/* --- FE8PC_RFO_CHECK self-check --- */

static int sFails;

static void Expect(int ok, const char *what, int a, int b)
{
    if (!ok)
    {
        sFails++;
        PortLog("[reforged] game fixes check MISMATCH: %s (%d, %d)\n", what, a, b);
    }
}

static void ScratchUnit(struct Unit *u, int jid, int level, int index)
{
    memset(u, 0, sizeof(*u));
    u->pCharacterData = GetCharacterData(0x01);
    u->pClassData = GetClassData(jid);
    u->level = level;
    u->index = index;
    u->maxHP = u->curHP = 20;
}

static void CheckEarlyExp(void)
{
    struct Unit a, t;
    u8 mode = gPlaySt.chapterModeIndex;
    u8 bits = gBmSt.gameStateBits;
    int lv, prevOn = -1, dips = 0, monotonic = 1, never = 1, sameBelow = 1;

    gPlaySt.chapterModeIndex = 1;
    gBmSt.gameStateBits &= ~0x40;
    ScratchUnit(&a, CLASS_CAVALIER, 5, 0x01);
    for (lv = 1; lv <= 20; lv++)
    {
        int off, on;

        ScratchUnit(&t, CLASS_CAVALIER, lv, 0x81);
        t.curHP = 0;
        Reforged_SetRunFix(RFO_FIX_EARLY_EXP, 0);
        off = GetUnitKillExpBonus(&a, &t);
        Reforged_SetRunFix(RFO_FIX_EARLY_EXP, 1);
        on = GetUnitKillExpBonus(&a, &t);
        monotonic &= on >= prevOn;
        never &= on >= off;
        if (GetUnitPowerLevel(&t) <= GetUnitPowerLevel(&a))
            sameBelow &= on == off;
        prevOn = on;
    }
    /* the original's dip: the level-6 kill gives less than the level-5 (equal) one */
    ScratchUnit(&t, CLASS_CAVALIER, 5, 0x81);
    t.curHP = 0;
    Reforged_SetRunFix(RFO_FIX_EARLY_EXP, 0);
    lv = GetUnitKillExpBonus(&a, &t);
    ScratchUnit(&t, CLASS_CAVALIER, 6, 0x81);
    t.curHP = 0;
    dips = GetUnitKillExpBonus(&a, &t) < lv;
    Expect(dips, "early EXP: the original's dip (level 6 below level 5)", lv, GetUnitKillExpBonus(&a, &t));
    Expect(monotonic, "early EXP: fix on is never lower for a stronger target", prevOn, 0);
    Expect(never, "early EXP: fix on never gives less than off", 0, 0);
    Expect(sameBelow, "early EXP: equal or weaker targets unchanged", 0, 0);
    gPlaySt.chapterModeIndex = 2; /* after the split: no change */
    ScratchUnit(&t, CLASS_CAVALIER, 6, 0x81);
    t.curHP = 0;
    Reforged_SetRunFix(RFO_FIX_EARLY_EXP, 0);
    lv = GetUnitKillExpBonus(&a, &t);
    Reforged_SetRunFix(RFO_FIX_EARLY_EXP, 1);
    Expect(GetUnitKillExpBonus(&a, &t) == lv, "early EXP: no change after the route split", lv,
        GetUnitKillExpBonus(&a, &t));
    gPlaySt.chapterModeIndex = mode;
    gBmSt.gameStateBits = bits;
}

static int SameStats(const struct Unit *x, const struct Unit *y)
{
    return x->maxHP == y->maxHP && x->pow == y->pow && x->skl == y->skl && x->spd == y->spd && x->def == y->def &&
           x->res == y->res && x->lck == y->lck;
}

static void CheckPromoLevels(void)
{
    static const u8 jids[] = { CLASS_PALADIN, CLASS_CYCLOPS, CLASS_DRACO_ZOMBIE, CLASS_GORGON };
    int i;

    for (i = 0; i < (int)sizeof(jids); i++)
    {
        struct Unit off, on, manual;
        u16 seeds[3], sOff[3], sOn[3], sMan[3];
        int base = PromoBaseClass(GetClassData(jids[i])) != GetClassData(jids[i]);

        StoreRNState(seeds);
        ScratchUnit(&off, jids[i], 5, 0x81);
        Reforged_SetRunFix(RFO_FIX_PROMO_LEVELS, 0);
        UnitAutolevel(&off);
        StoreRNState(sOff);
        LoadRNState(seeds);
        ScratchUnit(&on, jids[i], 5, 0x81);
        Reforged_SetRunFix(RFO_FIX_PROMO_LEVELS, 1);
        UnitAutolevel(&on);
        StoreRNState(sOn);
        LoadRNState(seeds);
        ScratchUnit(&manual, jids[i], 5, 0x81);
        manual.pClassData = PromoBaseClass(manual.pClassData);
        UnitAutolevelCore(&manual, 0, GetCurrentPromotedLevelBonus());
        manual.pClassData = GetClassData(jids[i]);
        UnitAutolevelCore(&manual, 0, manual.level - 1);
        StoreRNState(sMan);
        LoadRNState(seeds);

        Expect(sOff[0] == sOn[0] && sOff[1] == sOn[1] && sOff[2] == sOn[2] && sOn[0] == sMan[0] &&
            sOn[1] == sMan[1] && sOn[2] == sMan[2], "promoted autolevel: the same random numbers drawn", jids[i], 0);
        Expect(on.pClassData == GetClassData(jids[i]), "promoted autolevel: the class is put back", jids[i], 0);
        Expect(SameStats(&on, &manual), "promoted autolevel: on = base-class growths for the extra levels", jids[i], 0);
        if (!base)
            Expect(SameStats(&on, &off), "promoted autolevel: a placeholder link keeps the original", jids[i], 0);
        Expect(base == (jids[i] == CLASS_PALADIN), "promoted autolevel: which classes have a base class", jids[i], base);
    }
}

static void CheckAiHeal(void)
{
    struct Unit u;

    ScratchUnit(&u, CLASS_CAVALIER, 5, 0x01);
    Reforged_SetRunFix(RFO_FIX_AI_HEAL, 0);
    Expect(Reforged_FixAiHealFullHp(&u, 1) == 1 && Reforged_FixAiHealFullHp(&u, 0) == 0, "AI heal: off is the original", 0, 0);
    Reforged_SetRunFix(RFO_FIX_AI_HEAL, 1);
    Expect(Reforged_FixAiHealFullHp(&u, 1) == 0, "AI heal: a full-HP unit is skipped", 0, 0);
    Expect(Reforged_FixAiHealFullHp(&u, 0) == 0, "AI heal: an unflagged unit stays unflagged", 0, 0);
    u.curHP = 19;
    Expect(Reforged_FixAiHealFullHp(&u, 1) == 1, "AI heal: a wounded unit is still a target", 0, 0);
}

static void CheckCanto(void)
{
    Reforged_SetRunFix(RFO_FIX_CANTO, 0);
    Expect(Reforged_FixCantoMoveCount(255, 3, 120) == 255, "Canto: off reads the map", 0, 0);
    Reforged_SetRunFix(RFO_FIX_CANTO, 1);
    Expect(Reforged_FixCantoMoveCount(255, 3, 120) == 3, "Canto: an erased map keeps the count", 0, 0);
    Expect(Reforged_FixCantoMoveCount(4, 3, 120) == 4, "Canto: a real move reads the map", 0, 0);
}

static void CheckDevil(void)
{
    static const int lucks[] = { 0, 30, 31, 32, 35 };
    int i, k;

    for (i = 0; i < (int)(sizeof(lucks) / sizeof(lucks[0])); i++)
    {
        u16 seeds[3], sOff[3], sOn[3];
        int off = 0, on = 0;

        StoreRNState(seeds);
        Reforged_SetRunFix(RFO_FIX_DEVIL, 0);
        for (k = 0; k < 100; k++)
            off += Roll1RN((u16)Reforged_FixDevilChance(31 - lucks[i]));
        StoreRNState(sOff);
        LoadRNState(seeds);
        Reforged_SetRunFix(RFO_FIX_DEVIL, 1);
        for (k = 0; k < 100; k++)
            on += Roll1RN((u16)Reforged_FixDevilChance(31 - lucks[i]));
        StoreRNState(sOn);
        LoadRNState(seeds);
        Expect(sOff[0] == sOn[0] && sOff[1] == sOn[1] && sOff[2] == sOn[2], "Devil: the same random numbers drawn",
            lucks[i], 0);
        if (lucks[i] >= 32)
            Expect(off == 100 && on == 0, "Devil: Luck 32+ backfires 100/100 off, 0/100 on", off, on);
        else
            Expect(off == on, "Devil: Luck 0-31 unchanged", off, on);
        PortLog("[reforged] game fixes check: Devil at Luck %d: backfires off %d/100, on %d/100\n", lucks[i], off, on);
    }
}

static void CheckStaffDrop(void)
{
    struct Unit u;
    u8 chapter = gPlaySt.chapterIndex;
    int i;

    for (i = 0; i < 4; i++)
    {
        int status = i & 1 ? ITEM_STAFF_BERSERK : ITEM_STAFF_SLEEP;

        ScratchUnit(&u, CLASS_TROUBADOUR, 5, 0x81);
        u.state = US_DROP_ITEM;
        u.items[0] = MakeNewItem(status);
        u.items[1] = MakeNewItem(ITEM_STAFF_MEND);
        gPlaySt.chapterIndex = i & 2 ? CHAPTER_I_15 : CHAPTER_E_15;
        Reforged_SetRunFix(RFO_FIX_CH15_DROP, 0);
        Reforged_FixStaffDropOrder(&u);
        Expect(ITEM_INDEX(u.items[1]) == ITEM_STAFF_MEND, "Ch15 drop: off keeps Mend last", i, 0);
        Reforged_SetRunFix(RFO_FIX_CH15_DROP, 1);
        Reforged_FixStaffDropOrder(&u);
        Expect(ITEM_INDEX(GetUnitLastItem(&u)) == status && ITEM_INDEX(u.items[0]) == ITEM_STAFF_MEND &&
            ITEM_USES(u.items[1]) == GetItemMaxUses(MakeNewItem(status)), "Ch15 drop: on drops the status staff", i, 0);
    }
    /* not Chapter 15, or not the Troubadour's items: nothing */
    ScratchUnit(&u, CLASS_TROUBADOUR, 5, 0x81);
    u.state = US_DROP_ITEM;
    u.items[0] = MakeNewItem(ITEM_STAFF_SLEEP);
    u.items[1] = MakeNewItem(ITEM_STAFF_MEND);
    gPlaySt.chapterIndex = CHAPTER_E_15 - 1;
    Reforged_FixStaffDropOrder(&u);
    Expect(ITEM_INDEX(u.items[1]) == ITEM_STAFF_MEND, "Ch15 drop: another chapter unchanged", 0, 0);
    gPlaySt.chapterIndex = CHAPTER_E_15;
    u.items[0] = MakeNewItem(ITEM_STAFF_PHYSIC);
    Reforged_FixStaffDropOrder(&u);
    Expect(ITEM_INDEX(u.items[1]) == ITEM_STAFF_MEND, "Ch15 drop: other items unchanged", 0, 0);
    gPlaySt.chapterIndex = chapter;
}

void Reforged_GameFixesSelfCheck(void)
{
    static const int ids[] = { RFO_FIX_EARLY_EXP, RFO_FIX_PROMO_LEVELS, RFO_FIX_AI_HEAL, RFO_FIX_CANTO,
                               RFO_FIX_DEVIL, RFO_FIX_CH15_DROP };
    int keep[6], i;
    u16 seeds[3];

    if (getenv("FE8PC_RFO_CHECK") == NULL)
        return;
    for (i = 0; i < 6; i++)
        keep[i] = Reforged_RunFix(ids[i]);
    StoreRNState(seeds);
    sInCheck = 1;
    sFails = 0;
    CheckEarlyExp();
    CheckPromoLevels();
    CheckAiHeal();
    CheckCanto();
    CheckDevil();
    CheckStaffDrop();
    sInCheck = 0;
    LoadRNState(seeds);
    for (i = 0; i < 6; i++)
        Reforged_SetRunFix(ids[i], keep[i]);
    PortLog("[reforged] game fixes check: %s (%d mismatches)\n", sFails ? "FAIL" : "PASS", sFails);
}

#endif /* PORT_PLAYED_BUILD */
