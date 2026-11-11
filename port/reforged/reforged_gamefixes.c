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
 *   RFO_FIX_SMART_AI     include/cp_common.h RFO_AI_BAD_FIGHT / RFO_AI_SCORE_RISK ("Smarter enemies",
 *                        v1.7.0, the owner's request of 2026-11-07: generic enemies should not throw
 *                        their bad stats away). The AI attacks any target it can reach, even when the
 *                        best fight scores nothing (it then falls back to the damage dealt, 0 too). A
 *                        regular enemy (red, not a boss, not berserk) now skips a target it can do no
 *                        real damage to (under 1 expected) while the target hits back, and one whose
 *                        counter would likely kill it (50%+, from the simulation's damage, hit rates and
 *                        strike counts; crits not counted) unless it would likely kill the target first
 *                        (50%+). Among the fights left, its chance to die / 5 comes off the score. No
 *                        random numbers are used. Bosses, NPCs and the player's side are unchanged.
 *
 *   RFO_FIX_MYRRH        "Myrrh buff" (v1.10.0, the owner's answer of 2026-11-07, agreed: Myrrh joins
 *                        weak and her Dragonstone runs out). Myrrh's Dragonstone (ITEM_DIVINESTONE, 50
 *                        uses) is never used up in her hands (reforged_items.c Reforged_Refills, as the
 *                        "Unbreakable weapons" option refills, and its uses shown as "--" on her screens),
 *                        and her Speed is +2 (include/bmunit.h RFO_SKILL_STAT, reforged_skills_unit.c, the
 *                        battle copies excluded). No random numbers are used.
 *
 *   RFO_FIX_JOIN_RANKS   "Late joiners' ranks" (v1.13.0, the owner's answer of 2026-11-07: late-joining
 *                        units start with weapon ranks 1-2 higher). include/bmunit.h RFO_JOIN_RANKS in
 *                        LoadUnit and UnitChangeFaction: a character joining in Chapters 9-12 has each
 *                        weapon rank it has raised by 1 letter over its starting rank, from Chapter 13 by
 *                        2 (sJoinTier, by route), at most A (C for trainees). A floor: never lower, never
 *                        a new type, never S; a second call changes nothing.
 *
 *   RFO_FIX_ARMED_DANCERS "Armed dancers" (v1.18.0, the owner's pick of 2026-11-08: dancers should be able
 *                        to fight). A blue Dancer has light rank E (Lightning), the FE7 Bard (0x88,
 *                        reforged_fe7.c) anima E (Fire), as a floor, through the late joiners' hook
 *                        (RFO_JOIN_RANKS: LoadUnit, UnitChangeFaction), the unit class test switch and a
 *                        save block's FE7 marks (a Bard's stand-in Dancer). The class data is not changed:
 *                        the game reads only the unit's ranks to allow a weapon and to give weapon EXP.
 *                        Their fights with a weapon always play the map animation, whatever the Animations
 *                        option (Reforged_FixDancerWeaponBanim: the Dancer's dance animation hangs a fight
 *                        in which she strikes twice in a row; the Bard has no weapon animation). Dance and
 *                        Attack are both in the unit menu. No random numbers are used.
 *
 *   RFO_FIX_PROMO3       "Third promotion" (v1.23.0, the owner's table of 2026-11-09): in a game without the
 *                        randomizer every promotion screen offers three classes (the game's own, then the
 *                        table's, FE8 stand-ins for a missing ROM's classes); all of it is in
 *                        reforged_rando_classes.c (Promo3ListEx, Reforged_Promo3Active, Reforged_Promo3Tables),
 *                        behind v1.21.0's promotion hooks. No random numbers are used. Its check is
 *                        FE8PC_PROMO3_CHECK (reforged_rando.c, at the first boot).
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
#include "bmarena.h"
#include "bmdifficulty.h"
#include "bmmap.h"
#include "bmmind.h"
#include "cp_common.h"
#include "playerphase.h"
#include "rng.h"
#include "variables.h"
#include "constants/chapters.h"
#include "constants/characters.h"
#include "constants/classes.h"
#include "constants/items.h"

#include "port_hw.h"
#include "port_reforged.h"
#include "reforged_rando.h"
#include "reforged_skills.h"

static int sInCheck; /* the self-check's own calls are not logged */

/* --- The run's fixes: fixed at New Game, kept with each save (reforged_savemark.c), never the menu's
   while a game is played, so a fix cannot be switched on or off in the middle of a run --- */
static u8 sRunFix[RFO_FIX_LAST - RFO_FIX_FIRST + 1];

int Reforged_RunFix(int id)
{
    /* the tutorial (GameControl_InitTutorial, its own session with no New Game): no fix */
    if (gPlaySt.chapterStateBits & PLAY_FLAG_TUTORIAL)
        return 0;
    /* "Third promotion" (v1.23.0) does nothing in a randomized game (or one carried from another randomizer
       version): there the run's value is Off, for the save mark and the in-chapter menu's "This run" row as for
       the rule itself, so such a game's mark is v1.22.0's whatever the Game fixes screen said at New Game */
    if (id == RFO_FIX_PROMO3 && (gRandoRunOn || gRandoCarriedOn))
        return 0;
    return id >= RFO_FIX_FIRST && id <= RFO_FIX_LAST ? sRunFix[id - RFO_FIX_FIRST] : 0;
}

void Reforged_SetRunFix(int id, int value)
{
    if (id >= RFO_FIX_FIRST && id <= RFO_FIX_LAST)
        sRunFix[id - RFO_FIX_FIRST] = (u8)(value < 0 ? 0 : value > 255 ? 255 : value); /* Skills: 0-2 */
}

void Reforged_GameFixesNewGame(void)
{
    int id, n = 0;

    for (id = RFO_FIX_FIRST; id <= RFO_FIX_LAST; id++)
    {
        Reforged_SetRunFix(id, Reforged_GetOption(id));
        n += sRunFix[id - RFO_FIX_FIRST] != 0; /* (the screen's rows: the randomizer starts after this) */
    }
    Reforged_SkillKeptReset(); /* Skills: no promoted character keeps a skill yet */
    Reforged_RuinsSet(0); /* Ruin floor select (reforged_ruins.c): no floor cleared yet */
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
    /* the vanilla tables: "Random promotions" rewrites gPromoJidLut and .promotion */
    if (Rando_VanillaPromo(b, 0) == job->number || Rando_VanillaPromo(b, 1) == job->number ||
        (Rando_VanillaClass(b) != NULL && Rando_VanillaClass(b)->promotion == job->number))
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

/* --- Smarter enemies (v1.7.0) --- */

/* One fight as the smart AI sees it: side 0 the attacker (the AI unit), side 1 the target. */
struct SmartFight {
    int hp[2];      /* HP at the start */
    int dmg[2];     /* damage per hit (999: the Monster Stone, petrified) */
    int hit[2];     /* the real chance of a hit in % (two numbers are rolled: Reforged_TrueHitChance) */
    int burst[2];   /* strikes each time that side strikes: 2 with a brave weapon, 0 when it cannot */
    int cap[2];     /* uses left of a weapon that breaks (99: no limit); a round starts only while some are left */
    int missUses[2]; /* a miss uses the weapon too (magic and uncounterable weapons: bmbattle.c BattleGenerateHitEffects) */
    int follow;     /* the side that strikes again (-1: nobody) */
    int first;      /* the side that opens (1: the target's Vantage) */
};

/* Every path of the battle's strikes (bmbattle.c BattleUnwind): order[at..] are the strikes still to
   come (side | 2 on the first strike of a round: BattleGenerateRoundHits tests the weapon only
   there, so a brave weapon that breaks on its first strike still makes its second); a battle ends
   when either side reaches 0 HP. Adds up the chances that the target dies (kill) and that the
   attacker dies (die). */
static void SmartWalk(const struct SmartFight *f, const int *order, int len, int at, int hp0, int hp1,
    int used0, int used1, double p, double *kill, double *die)
{
    int side, hp0Hit, hp1Hit;
    double q;

    if (p <= 0)
        return;
    if (hp1 <= 0)
    {
        *kill += p;
        return;
    }
    if (hp0 <= 0)
    {
        *die += p;
        return;
    }
    if (at >= len)
        return;
    side = order[at] & 1;
    if (order[at] & 2) /* a new round: none of this side's strikes once its weapon is used up */
    {
        if ((side ? used1 : used0) >= f->cap[side])
        {
            while (at + 1 < len && order[at + 1] == side)
                at++;
            SmartWalk(f, order, len, at + 1, hp0, hp1, used0, used1, p, kill, die);
            return;
        }
    }
    if (f->dmg[side] <= 0 || f->hit[side] <= 0)
    {
        SmartWalk(f, order, len, at + 1, hp0, hp1, used0, used1, p, kill, die);
        return;
    }
    q = (f->hit[side] > 100 ? 100 : f->hit[side]) / 100.0;
    hp0Hit = side ? hp0 - f->dmg[1] : hp0;
    hp1Hit = side ? hp1 : hp1 - f->dmg[0];
    /* the hit (uses the weapon), then the miss (uses it only for magic and uncounterable weapons) */
    SmartWalk(f, order, len, at + 1, hp0Hit, hp1Hit, used0 + !side, used1 + side, p * q, kill, die);
    SmartWalk(f, order, len, at + 1, hp0, hp1, used0 + (!side && f->missUses[0]), used1 + (side && f->missUses[1]),
        p * (1 - q), kill, die);
}

static void SmartOdds(const struct SmartFight *f, int *kill, int *die)
{
    int order[6], len = 0, i, n, s;
    double k = 0, d = 0;

    for (s = f->first, i = 0; i < 2; i++, s ^= 1) /* the opener, then the other side */
        for (n = 0; n < f->burst[s] && n < 2; n++)
            order[len++] = s | (n == 0 ? 2 : 0);
    if (f->follow >= 0)
        for (n = 0; n < f->burst[f->follow] && n < 2; n++)
            order[len++] = f->follow | (n == 0 ? 2 : 0);
    SmartWalk(f, order, len, 0, f->hp[0], f->hp[1], 0, 0, 1.0, &k, &d);
    *kill = (int)(k * 100 + 0.5);
    *die = (int)(d * 100 + 0.5);
}

/* The decision on the numbers alone (the self-check calls it): 1 skip the fight. *risk: the
   attacker's chance in % to die in it. */
static int SmartAiVerdict(const struct SmartFight *f, int *risk)
{
    int kill, die, strikes;
    int counters = f->burst[1] > 0 && f->cap[1] > 0 && f->dmg[1] > 0 && f->hit[1] > 0;

    SmartOdds(f, &kill, &die);
    *risk = die;
    if (kill >= 50)
        return 0; /* a likely kill is always worth it */
    strikes = f->follow == 0 ? 2 : 1; /* rounds; each needs a use left to start */
    if (strikes > f->cap[0])
        strikes = f->cap[0];
    strikes *= f->burst[0];
    if (counters && f->dmg[0] * f->hit[0] * strikes < 100)
        return 1; /* under 1 expected damage, and it gets hit back */
    return die >= 50;
}

/* A regular enemy deciding on its own (AI phase): red, not a boss, not berserk. */
static int SmartAiUnit(void)
{
    if (!Reforged_RunFix(RFO_FIX_SMART_AI) || gActiveUnit == NULL)
        return 0;
    if (UNIT_FACTION(gActiveUnit) != FACTION_RED || (UNIT_CATTRIBUTES(gActiveUnit) & CA_BOSS))
        return 0;
    return gActiveUnit->statusIndex != UNIT_STATUS_BERSERK;
}

static int SmartAiApplies(void)
{
    return SmartAiUnit() && gBattleActor.unit.index == gActiveUnit->index; /* the simulation just made is this unit's */
}

/* A weapon that does no plain damage: the Monster Stone (petrifies) and the halving ones (Eclipse). */
static int SmartSpecial(int weapon)
{
    return weapon && (GetItemIndex(weapon) == ITEM_MONSTER_STONE || GetItemWeaponEffect(weapon) == WPN_EFFECT_HPHALVE);
}

/* One side of the simulation just made. The simulation lands every hit, so a weapon can break in
   it (weapon 0 after): weaponBefore is the weapon held, and its uses then cap the strikes. */
static void SmartSide(struct SmartFight *f, int side, struct BattleUnit *bu, struct BattleUnit *foe, int strikes)
{
    int w = bu->weaponBefore;

    f->burst[side] = 0;
    f->cap[side] = 99;
    f->dmg[side] = 0;
    f->missUses[side] = (bu->weaponAttributes & (IA_MAGIC | IA_UNCOUNTERABLE)) != 0;
    f->hit[side] = Reforged_TrueHitChance(bu->battleEffectiveHitRate > 100 ? 100 : bu->battleEffectiveHitRate);
    if (!strikes || w == 0)
        return;
    f->burst[side] = (bu->weaponAttributes & IA_BRAVE) ? 2 : 1;
    if (bu->weapon == 0) /* it broke in the simulation, so it is not unbreakable */
        f->cap[side] = ITEM_USES(w) > 0 ? ITEM_USES(w) : 1;
    if (GetItemIndex(w) == ITEM_MONSTER_STONE)
        f->dmg[side] = 999; /* petrified: as good as dead */
    else if (GetItemWeaponEffect(w) != WPN_EFFECT_HPHALVE && bu->battleAttack < 0xFF)
        f->dmg[side] = bu->battleAttack > foe->battleDefense ? bu->battleAttack - foe->battleDefense : 0;
}

/* The simulation just made (gBattleActor = the AI unit, gBattleTarget = its target) judged. */
static int SmartAiJudge(int *risk)
{
    struct BattleUnit *a, *d;
    struct Unit *target = GetUnit(gBattleTarget.unit.index);
    struct SmartFight f;

    *risk = 0;
    if (gBattleActor.weaponBefore == 0 || SmartSpecial(gBattleActor.weaponBefore) || !UNIT_IS_VALID(target))
        return 0; /* the Monster Stone, halving weapons, no target unit: the original's choice */
    f.hp[0] = GetUnitCurrentHp(gActiveUnit);
    f.hp[1] = GetUnitCurrentHp(target);
    SmartSide(&f, 0, &gBattleActor, &gBattleTarget, 1);
    SmartSide(&f, 1, &gBattleTarget, &gBattleActor, gBattleTarget.canCounter);
    f.follow = -1;
    if (BattleGetFollowUpOrder(&a, &d))
        f.follow = a == &gBattleActor ? 0 : 1;
    /* Vantage (skills): a target at half HP or less strikes first (reforged_skills_battle.c) */
    f.first = f.burst[1] && f.hp[1] * 2 <= GetUnitMaxHp(target) && Reforged_SkillHas(target, SK_VANTAGE) ? 1 : 0;
    return SmartAiVerdict(&f, risk);
}

/* The targets this unit skipped this turn: their reach is kept away from (Reforged_FixAiAvoidMove). */
static u8 sSkipIds[8];
static int sSkipCount, sSkipUnit = -1, sSkipTurn = -1;
static u32 sSkipClock, sEscapeClock;
static int sEscapeUnit = -1;

/* The list belongs to one unit's decision: the same unit, turn and moment (a unit decides within a
   few frames; a restarted chapter or a loaded suspend is long after). */
static void SmartSkipReset(void)
{
    u32 now = GetGameClock();

    if (sSkipUnit != gActiveUnitId || sSkipTurn != gPlaySt.chapterTurnNumber || now - sSkipClock > 60)
    {
        sSkipUnit = gActiveUnitId;
        sSkipTurn = gPlaySt.chapterTurnNumber;
        sSkipCount = 0;
    }
    sSkipClock = now;
}

/* src/cp_0803E2F4.c AiTryMoveTowardsEscape (RFO_AI_ESCAPE_MARK): the move that follows heads for an
   escape point, and the escape action is set on its square: it is never steered away. */
void Reforged_FixAiEscapeMark(void)
{
    sEscapeUnit = gActiveUnitId;
    sEscapeClock = GetGameClock();
}

int Reforged_FixAiBadFight(void)
{
    static int sLogged;
    int risk, i;

    if (!SmartAiApplies() || !SmartAiJudge(&risk))
        return 0;
    if (sInCheck)
        return 1;
    SmartSkipReset();
    for (i = 0; i < sSkipCount && sSkipIds[i] != (u8)gBattleTarget.unit.index; i++)
        ;
    if (i == sSkipCount && sSkipCount < (int)sizeof(sSkipIds))
        sSkipIds[sSkipCount++] = (u8)gBattleTarget.unit.index;
    if (sLogged < 30)
        sLogged++, PortLog("[reforged] game fix: smarter enemies: unit 0x%02X skips unit 0x%02X from (%d,%d) "
            "(attack %d - def %d at %d%%, counter %d - def %d at %d%%, death %d%%)\n", (u8)gActiveUnit->index,
            (u8)gBattleTarget.unit.index, gBattleActor.unit.xPos, gBattleActor.unit.yPos, gBattleActor.battleAttack,
            gBattleTarget.battleDefense, gBattleActor.battleEffectiveHitRate, gBattleTarget.battleAttack,
            gBattleActor.battleDefense, gBattleTarget.battleEffectiveHitRate, risk);
    return 1;
}

int Reforged_FixAiRiskPenalty(void)
{
    int risk;

    if (!SmartAiApplies())
        return 0;
    SmartAiJudge(&risk);
    return risk / 5;
}

/* How far unit u could strike next turn, roughly: its Mov plus its longest usable weapon range
   (walls and terrain not counted, so a little farther than it really reaches). -1: no weapon. */
static int SmartReach(struct Unit *u)
{
    int i, best = 0;

    for (i = 0; i < UNIT_ITEM_COUNT && u->items[i]; i++)
    {
        int r;

        if (!CanUnitUseWeapon(u, u->items[i]))
            continue;
        r = GetItemMaxRange(u->items[i]);
        if (r <= 0)
            r = 10; /* Mag/2 range (Bolting and the like) */
        if (r > best)
            best = r;
    }
    return best ? UNIT_MOV(u) + best : -1;
}

static int SmartUnsafe(int x, int y)
{
    int i;

    for (i = 0; i < sSkipCount; i++)
    {
        struct Unit *u = GetUnit(sSkipIds[i]);
        int reach;

        if (!UNIT_IS_VALID(u) || (u->state & (US_DEAD | US_RESCUED | US_BIT16 | US_HIDDEN)))
            continue;
        reach = SmartReach(u);
        if (reach >= 0 && RECT_DISTANCE(u->xPos, u->yPos, x, y) <= reach)
            return 1;
    }
    return 0;
}

/* src/cp_utility.c AiTryMoveTowards (RFO_AI_AVOID_MOVE): a unit that skipped targets this turn and so
   walks on instead does not end its move inside a skipped target's reach: of the squares it can
   reach (by the same rules: free, allowed danger), the one nearest its goal outside every such
   reach, even back from where it stands. None outside: the original's square. */
void Reforged_FixAiAvoidMove(s16 *xOut, s16 *yOut, int maxDanger)
{
    static int sLogged;
    int ix, iy, bx = -1, by = -1, best = 0x7FFF;
    int cx, cy;

    if (!SmartAiUnit())
        return;
    if (sEscapeUnit == gActiveUnitId && GetGameClock() - sEscapeClock <= 1)
        return; /* heading for an escape point */
    cx = *xOut >= 0 ? *xOut : gActiveUnit->xPos;
    cy = *xOut >= 0 ? *yOut : gActiveUnit->yPos;
    if (gBmMapRange[cy][cx] == 0)
        return; /* the square is the goal itself (a door, a chest, a village): the original's */
    SmartSkipReset();
    if (sSkipCount == 0)
        return;
    if (!SmartUnsafe(cx, cy))
    {
        if (sLogged < 20)
            sLogged++, PortLog("[reforged] game fix: smarter enemies: unit 0x%02X already out of reach at (%d,%d)\n",
                (u8)gActiveUnit->index, cx, cy);
        return;
    }
    for (iy = gBmMapSize.y - 1; iy >= 0; iy--)
        for (ix = gBmMapSize.x - 1; ix >= 0; ix--)
        {
            if (gBmMapMovement[iy][ix] > MAP_MOVEMENT_MAX)
                continue;
            if (gBmMapUnit[iy][ix] != 0 && gBmMapUnit[iy][ix] != gActiveUnitId)
                continue;
            if (!AiCheckDangerAt(ix, iy, (u8)maxDanger) || SmartUnsafe(ix, iy))
                continue;
            if (gBmMapRange[iy][ix] >= best)
                continue;
            best = gBmMapRange[iy][ix];
            bx = ix;
            by = iy;
        }
    if (bx < 0)
    {
        if (sLogged < 20)
            sLogged++, PortLog("[reforged] game fix: smarter enemies: unit 0x%02X finds no square out of reach (goes to (%d,%d))\n",
                (u8)gActiveUnit->index, cx, cy);
        return;
    }
    if (sLogged < 20)
        sLogged++, PortLog("[reforged] game fix: smarter enemies: unit 0x%02X stays out of reach at (%d,%d), not (%d,%d)\n",
            (u8)gActiveUnit->index, bx, by, cx, cy);
    *xOut = (s16)bx;
    *yOut = (s16)by;
}

/* --- Myrrh buff (v1.10.0) --- */

static int IsMyrrh(const struct Unit *unit)
{
    return unit != NULL && unit->pCharacterData != NULL && unit->pCharacterData->number == CHARACTER_MYRRH;
}

int Reforged_MyrrhStone(int item, const struct Unit *owner)
{
    return item != 0 && ITEM_INDEX(item) == ITEM_DIVINESTONE && Reforged_RunFix(RFO_FIX_MYRRH) && IsMyrrh(owner);
}

int Reforged_MyrrhSpeed(const struct Unit *unit)
{
    /* the battle's copies already hold the totals (InitBattleUnit): never twice there */
    if (!Reforged_RunFix(RFO_FIX_MYRRH) || !IsMyrrh(unit) || Reforged_SkillIsBattleCopy(unit))
        return 0;
    return 2;
}

/* --- Late joiners' ranks (v1.13.0) --- */

/* The tier (0, 1 or 2 letters) of the place a character joins in (the printed chapter: 9-12 +1,
   13 and later +2), on Eirika's route [0] and Ephraim's [1]. A table, not the chapter being played,
   so the floor depends only on the unit and a second call (a faction change back, a fresh load)
   changes nothing. With the recruitment shuffle the slot's tier goes with the slot. Everyone else
   (the Prologue to Chapter 8 and 5x, Myrrh, postgame recruits, monsters): 0. */
static const struct { u8 pid, tier[2]; } sJoinTier[] = {
    { CHARACTER_TANA,     { 1, 1 } }, /* Ch9 */
    { CHARACTER_AMELIA,   { 1, 1 } }, /* Ch9 */
    { CHARACTER_INNES,    { 1, 2 } }, /* Ch10 / Ch15 */
    { CHARACTER_GERIK,    { 1, 2 } }, /* Ch10 / Ch13 */
    { CHARACTER_TETHYS,   { 1, 2 } }, /* Ch10 / Ch13 (no ranks of her own) */
    { CHARACTER_MARISA,   { 1, 1 } }, /* Ch10 / Ch12 */
    { CHARACTER_CORMAG,   { 2, 1 } }, /* Ch13 / Ch10 */
    { CHARACTER_DUESSEL,  { 2, 1 } }, /* Ch15 / Ch10 */
    { CHARACTER_LARACHEL, { 1, 1 } }, /* Ch11 */
    { CHARACTER_DOZLA,    { 1, 1 } }, /* Ch11 */
    { CHARACTER_EWAN,     { 1, 1 } }, /* Ch11 / Ch12 */
    { CHARACTER_SALEH,    { 1, 2 } }, /* Ch12 / Ch15 */
    { CHARACTER_RENNAC,   { 2, 2 } }, /* Ch14 */
    { CHARACTER_KNOLL,    { 2, 2 } }, /* Ch15 / the end of Ch14 */
    { CHARACTER_SYRENE,   { 2, 2 } }, /* Ch17 */
};

static int JoinTier(int pid)
{
    int i, route = gPlaySt.chapterModeIndex == 3 ? 1 : 0; /* 3: Ephraim's route */

    for (i = 0; i < (int)(sizeof(sJoinTier) / sizeof(sJoinTier[0])); i++)
        if (sJoinTier[i].pid == pid)
            return sJoinTier[i].tier[route];
    return 0;
}

static int RankExpOfLevel(int level)
{
    static const u8 exp[] = { WPN_EXP_0, WPN_EXP_E, WPN_EXP_D, WPN_EXP_C, WPN_EXP_B, WPN_EXP_A };

    return level <= 0 ? 0 : level >= 5 ? WPN_EXP_A : exp[level];
}

static int RankLevelOfExp(int wexp)
{
    return wexp >= WPN_EXP_A ? 5 : wexp >= WPN_EXP_B ? 4 : wexp >= WPN_EXP_C ? 3 : wexp >= WPN_EXP_D ? 2 : wexp >= WPN_EXP_E ? 1 : 0;
}

/* src/bmunit.c LoadUnit / UnitChangeFaction (RFO_JOIN_RANKS): a blue character who joins at Chapter 9
   or later has each weapon rank it already has raised to at least its starting rank plus the tier
   (letters E D C B A), at most A (C for trainees, whose class caps there). Never a new weapon type,
   never lower, never S. */
void Reforged_FixJoinRanks(struct Unit *unit)
{
    static int sLogged;
    int tier, cap, t, changed = 0;

    /* "Armed dancers" (v1.18.0) shares this hook, and goes first: with both fixes on, a late-joining Dancer's
       light rank (her start there: none) is then raised by her tier as any rank is (+1 letter: E, +2: D),
       so the first call already gives the final ranks and a second changes nothing */
    Reforged_FixArmedDancer(unit);
    if (!Reforged_RunFix(RFO_FIX_JOIN_RANKS) || unit == NULL || unit->pCharacterData == NULL || unit->pClassData == NULL)
        return;
    if (UNIT_FACTION(unit) != FACTION_BLUE || (tier = JoinTier(unit->pCharacterData->number)) <= 0)
        return;
    cap = (UNIT_CATTRIBUTES(unit) & CA_MAXLEVEL10) ? WPN_EXP_C : WPN_EXP_A;
    for (t = 0; t < 8; t++)
    {
        int start, floor;

        if (unit->ranks[t] == 0)
            continue;
        start = unit->pCharacterData->baseRanks[t] ? unit->pCharacterData->baseRanks[t] : unit->pClassData->baseRanks[t];
        floor = RankExpOfLevel(RankLevelOfExp(start) + tier);
        if (floor > cap)
            floor = cap;
        if (unit->ranks[t] < floor)
        {
            unit->ranks[t] = (u8)floor;
            changed = 1;
        }
    }
    if (changed && !sInCheck && sLogged < 20)
        sLogged++, PortLog("[reforged] game fix: late joiners' ranks: pid 0x%02X joins with ranks +%d\n",
            unit->pCharacterData->number, tier);
}

/* --- Armed dancers (v1.18.0) --- */

/* 1 for a unit in the Dancer class or in the FE7 Bard (0x88, reforged_fe7.c, only when it is loaded). */
int Reforged_ArmedDancerClass(const struct Unit *unit)
{
    int jid;

    if (unit == NULL || unit->pClassData == NULL)
        return 0;
    jid = unit->pClassData->number;
    return jid == CLASS_DANCER || (jid == REFORGED_FE7_JID_BARD && Reforged_Fe7ClassAvailable(jid));
}

/* The owner's pick of 2026-11-08: a dancer can fight instead of being a sitting duck. A blue Dancer gets
   light rank E (Lightning), the Bard anima E (Fire): a floor (a higher rank, earned with the weapon, stays;
   a second call changes nothing). Nothing else changes: the class data stays the game's (CanUnitUseWeapon
   and the weapon-EXP gain read the unit's ranks only), so the randomizer's tables never see it. The fight
   then plays the map animation (Reforged_FixDancerWeaponBanim, below). */
void Reforged_FixArmedDancer(struct Unit *unit)
{
    static int sLogged;
    int type;

    if (!Reforged_RunFix(RFO_FIX_ARMED_DANCERS) || !Reforged_ArmedDancerClass(unit) || UNIT_FACTION(unit) != FACTION_BLUE)
        return;
    type = unit->pClassData->number == CLASS_DANCER ? ITYPE_LIGHT : ITYPE_ANIMA;
    if (unit->ranks[type] >= WPN_EXP_E)
        return;
    unit->ranks[type] = WPN_EXP_E;
    if (!sInCheck && sLogged < 20)
        sLogged++, PortLog("[reforged] game fix: armed dancers: pid 0x%02X (class 0x%02X) has %s rank E\n",
            unit->pCharacterData != NULL ? unit->pCharacterData->number : 0, unit->pClassData->number,
            type == ITYPE_LIGHT ? "light" : "anima");
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

static int ArmedDancerWithWeapon(struct Unit *unit, int weapon)
{
    return Reforged_ArmedDancerClass(unit) && weapon != 0 && (GetItemAttributes(weapon) & IA_WEAPON);
}

int Reforged_FixDancerWeaponBanim(struct Unit *unit1, int weapon1, struct Unit *unit2, int weapon2)
{
    int side;

    /* test switch FE8PC_TEST_MAP_ANIMS (played builds; the skill banners' map popups): every battle
       that would play a battle animation plays the map animation instead (a replayed key file then
       goes its own way after the first such battle) */
    if (getenv("FE8PC_TEST_MAP_ANIMS") != NULL)
        return 1;
    /* "Armed dancers" (v1.18.0): a Dancer or the Bard fighting with a weapon always gets the map animation,
       whatever the Animations option and "Original-game bug fixes" say (the Dancer's only attack animation
       is her dance, above; the Bard has none for a weapon: reforged_fe7.c keeps only its play animation) */
    if (Reforged_RunFix(RFO_FIX_ARMED_DANCERS))
    {
        side = ArmedDancerWithWeapon(unit1, weapon1) ? 1 : ArmedDancerWithWeapon(unit2, weapon2) ? 2 : 0;
        if (side != 0)
        {
            static int sLogged;

            if (sLogged < 20)
                sLogged++, PortLog("[reforged] game fix: armed dancers: class 0x%02X fights with item 0x%02X: the map "
                                   "animation\n", (side == 1 ? unit1 : unit2)->pClassData->number,
                    ITEM_INDEX(side == 1 ? weapon1 : weapon2));
            return 1;
        }
    }
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

static void CheckSmartAi(void)
{
    struct SmartFight f;
    int risk, kill, die;

    /* the reviewers' case: a double that needs both hits against a counter that kills */
    f.hp[0] = 20; f.hp[1] = 20; f.dmg[0] = 10; f.dmg[1] = 20; f.hit[0] = 80; f.hit[1] = 90;
    f.burst[0] = 1; f.burst[1] = 1; f.cap[0] = 99; f.cap[1] = 99; f.follow = 0; f.first = 0;
    f.missUses[0] = 0; f.missUses[1] = 0;
    SmartOdds(&f, &kill, &die);
    Expect(kill == 6 && die == 90, "smarter enemies: strike order counted", kill, die);
    Expect(SmartAiVerdict(&f, &risk) == 1 && risk == 90, "smarter enemies: a likely death is skipped", 0, risk);
    /* a likely kill is kept */
    f.dmg[0] = 10; f.hit[0] = 90; f.dmg[1] = 5; f.hit[1] = 50;
    Expect(SmartAiVerdict(&f, &risk) == 0, "smarter enemies: a likely kill is kept", 0, risk);
    /* no damage while countered: skipped; with no counter: kept */
    f.dmg[0] = 0; f.dmg[1] = 5;
    Expect(SmartAiVerdict(&f, &risk) == 1, "smarter enemies: no damage, countered: skipped", 0, 0);
    f.burst[1] = 0;
    Expect(SmartAiVerdict(&f, &risk) == 0, "smarter enemies: no damage, no counter: kept", 0, 0);
    /* a counter whose weapon breaks after one use: 12 damage once cannot kill 20 HP */
    f.dmg[0] = 3; f.hit[0] = 70; f.follow = 1; f.burst[1] = 1; f.dmg[1] = 12; f.hit[1] = 85; f.cap[1] = 1;
    SmartOdds(&f, &kill, &die);
    Expect(die == 0, "smarter enemies: a breaking weapon strikes once", kill, die);
    /* two uses left: both counters come (both hit: 72%) */
    f.cap[1] = 2;
    SmartOdds(&f, &kill, &die);
    Expect(die == 72, "smarter enemies: two uses, two counters", kill, die);
    /* one use left, a counter that kills in one hit: a physical miss leaves the use for the follow-up */
    f.cap[1] = 1;
    f.dmg[1] = 20;
    SmartOdds(&f, &kill, &die);
    Expect(die == 98, "smarter enemies: a missed counter keeps its last use for the follow-up", kill, die);
    f.missUses[1] = 1;
    SmartOdds(&f, &kill, &die);
    Expect(die == 85, "smarter enemies: a missed magic counter uses it", kill, die);
    f.missUses[1] = 0;
    /* a brave weapon at its last use still strikes twice in its round */
    f.dmg[1] = 10; f.burst[1] = 2; f.follow = -1; f.cap[1] = 1;
    SmartOdds(&f, &kill, &die);
    Expect(die == 72, "smarter enemies: a breaking brave weapon finishes its round", kill, die);
    f.dmg[1] = 12; f.burst[1] = 1; f.follow = 1; f.cap[1] = 99;
    SmartOdds(&f, &kill, &die);
    Expect(die == 72, "smarter enemies: two counters", kill, die);
    /* Vantage: the target opens and kills */
    f.hp[0] = 10; f.hp[1] = 10; f.dmg[0] = 10; f.hit[0] = 100; f.dmg[1] = 10; f.hit[1] = 100; f.follow = -1; f.first = 1;
    SmartOdds(&f, &kill, &die);
    Expect(kill == 0 && die == 100, "smarter enemies: Vantage strikes first", kill, die);
    Expect(Reforged_TrueHitChance(70) == 82 && Reforged_TrueHitChance(30) == 18 && Reforged_TrueHitChance(100) == 100,
        "smarter enemies: two-number hit chances", Reforged_TrueHitChance(70), Reforged_TrueHitChance(30));
    Reforged_SetRunFix(RFO_FIX_SMART_AI, 0);
    Expect(Reforged_FixAiBadFight() == 0 && Reforged_FixAiRiskPenalty() == 0, "smarter enemies: off is the original", 0, 0);
}

static void CheckMyrrh(void)
{
    struct Unit u;
    int stone = MakeNewItem(ITEM_DIVINESTONE) & 0xFF, spdOff, spdOn;

    stone |= 10 << 8; /* 10 uses left */
    ScratchUnit(&u, CLASS_MANAKETE_MYRRH, 5, 0x05);
    u.pCharacterData = GetCharacterData(CHARACTER_MYRRH);
    u.spd = 10;
    Reforged_SetRunFix(RFO_FIX_MYRRH, 0);
    spdOff = GetUnitSpeed(&u);
    Expect(GetItemUses(Reforged_ItemAfterUse(stone, &u)) == 9 || Reforged_GetOption(RFO_UNBREAKABLE) != 0,
        "Myrrh buff: off, the Dragonstone wears down", GetItemUses(Reforged_ItemAfterUse(stone, &u)), 0);
    Reforged_SetRunFix(RFO_FIX_MYRRH, 1);
    spdOn = GetUnitSpeed(&u);
    Expect(spdOn == spdOff + 2, "Myrrh buff: Speed +2", spdOff, spdOn);
    Expect(GetItemUses(Reforged_ItemAfterUse(stone, &u)) == GetItemMaxUses(stone), "Myrrh buff: the Dragonstone stays full",
        GetItemUses(Reforged_ItemAfterUse(stone, &u)), 0);
    u.pCharacterData = GetCharacterData(0x01);
    Expect(GetUnitSpeed(&u) == spdOff, "Myrrh buff: only Myrrh", spdOff, GetUnitSpeed(&u));
    Expect(GetItemUses(Reforged_ItemAfterUse(stone, &u)) == 9 || Reforged_GetOption(RFO_UNBREAKABLE) != 0,
        "Myrrh buff: another holder's Dragonstone wears down", 0, 0);
}

static void CheckJoinRanks(void)
{
    struct Unit u;
    u8 mode = gPlaySt.chapterModeIndex;
    int base;

    ScratchUnit(&u, CLASS_WYVERN_RIDER, 5, 0x05);
    u.pCharacterData = GetCharacterData(CHARACTER_CORMAG);
    base = u.pCharacterData->baseRanks[ITYPE_LANCE] ? u.pCharacterData->baseRanks[ITYPE_LANCE] : u.pClassData->baseRanks[ITYPE_LANCE];
    u.ranks[ITYPE_LANCE] = (u8)base;
    Reforged_SetRunFix(RFO_FIX_JOIN_RANKS, 0);
    Reforged_FixJoinRanks(&u);
    Expect(u.ranks[ITYPE_LANCE] == base, "join ranks: off is the original", base, u.ranks[ITYPE_LANCE]);
    Reforged_SetRunFix(RFO_FIX_JOIN_RANKS, 1);
    gPlaySt.chapterModeIndex = 2; /* Eirika: Ch13, +2 */
    Reforged_FixJoinRanks(&u);
    Expect(RankLevelOfExp(u.ranks[ITYPE_LANCE]) == (RankLevelOfExp(base) + 2 > 5 ? 5 : RankLevelOfExp(base) + 2),
        "join ranks: Cormag on Eirika's route +2", base, u.ranks[ITYPE_LANCE]);
    base = u.ranks[ITYPE_LANCE];
    Reforged_FixJoinRanks(&u);
    Expect(u.ranks[ITYPE_LANCE] == base, "join ranks: a second call changes nothing", base, u.ranks[ITYPE_LANCE]);
    Expect(u.ranks[ITYPE_SWORD] == 0, "join ranks: no new weapon type", 0, u.ranks[ITYPE_SWORD]);
    u.pCharacterData = GetCharacterData(0x02); /* Seth: joins in the Prologue */
    u.ranks[ITYPE_LANCE] = 31;
    Reforged_FixJoinRanks(&u);
    Expect(u.ranks[ITYPE_LANCE] == 31, "join ranks: an early joiner unchanged", 31, u.ranks[ITYPE_LANCE]);
    gPlaySt.chapterModeIndex = mode;
}

static void CheckArmedDancers(void)
{
    struct Unit u;

    ScratchUnit(&u, CLASS_DANCER, 5, 0x05);
    u.pCharacterData = GetCharacterData(CHARACTER_TETHYS);
    Reforged_SetRunFix(RFO_FIX_ARMED_DANCERS, 0);
    Reforged_FixArmedDancer(&u);
    Expect(u.ranks[ITYPE_LIGHT] == 0 && !CanUnitUseWeapon(&u, MakeNewItem(ITEM_LIGHT_LIGHTNING)),
        "armed dancers: off, no light rank and no Lightning", u.ranks[ITYPE_LIGHT], 0);
    Reforged_SetRunFix(RFO_FIX_ARMED_DANCERS, 1);
    Reforged_FixArmedDancer(&u);
    Expect(u.ranks[ITYPE_LIGHT] == WPN_EXP_E && CanUnitUseWeapon(&u, MakeNewItem(ITEM_LIGHT_LIGHTNING)),
        "armed dancers: on, light E and Lightning", u.ranks[ITYPE_LIGHT], 0);
    Expect(!CanUnitUseWeapon(&u, MakeNewItem(ITEM_ANIMA_FIRE)) && !CanUnitUseWeapon(&u, MakeNewItem(ITEM_LIGHT_SHINE)),
        "armed dancers: not Fire, not a D tome", 0, 0);
    u.ranks[ITYPE_LIGHT] = WPN_EXP_D;
    Reforged_FixArmedDancer(&u);
    Expect(u.ranks[ITYPE_LIGHT] == WPN_EXP_D, "armed dancers: a floor (D kept)", WPN_EXP_D, u.ranks[ITYPE_LIGHT]);
    Expect(!ArenaIsUnitAllowed(&u), "armed dancers: the arena refuses a Dancer", 0, 0);
    Reforged_SetRunFix(RFO_FIX_ARMED_DANCERS, 0);
    Expect(ArenaIsUnitAllowed(&u), "armed dancers: off, a Dancer with a light rank is let in (the original)", 0, 0);
    Reforged_SetRunFix(RFO_FIX_ARMED_DANCERS, 1);
    ScratchUnit(&u, CLASS_CLERIC, 5, 0x05);
    Reforged_FixArmedDancer(&u);
    Expect(u.ranks[ITYPE_LIGHT] == 0 && u.ranks[ITYPE_ANIMA] == 0, "armed dancers: another class unchanged", 0, 0);
    ScratchUnit(&u, CLASS_DANCER, 5, 0x81);
    Reforged_FixArmedDancer(&u);
    Expect(u.ranks[ITYPE_LIGHT] == 0, "armed dancers: an enemy Dancer unchanged", 0, u.ranks[ITYPE_LIGHT]);
    if (Reforged_Fe7ClassAvailable(REFORGED_FE7_JID_BARD))
    {
        ScratchUnit(&u, REFORGED_FE7_JID_BARD, 5, 0x05);
        Reforged_FixArmedDancer(&u);
        Expect(u.ranks[ITYPE_ANIMA] == WPN_EXP_E && u.ranks[ITYPE_LIGHT] == 0 &&
            CanUnitUseWeapon(&u, MakeNewItem(ITEM_ANIMA_FIRE)), "armed dancers: the Bard, anima E and Fire",
            u.ranks[ITYPE_ANIMA], u.ranks[ITYPE_LIGHT]);
    }
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
                               RFO_FIX_DEVIL, RFO_FIX_CH15_DROP, RFO_FIX_SMART_AI, RFO_FIX_MYRRH, RFO_FIX_JOIN_RANKS,
                               RFO_FIX_ARMED_DANCERS };
    int keep[10], i;
    u16 seeds[3];

    if (getenv("FE8PC_RFO_CHECK") == NULL)
        return;
    for (i = 0; i < 10; i++)
        keep[i] = Reforged_RunFix(ids[i]);
    StoreRNState(seeds);
    sInCheck = 1;
    sFails = 0;
    CheckEarlyExp();
    CheckPromoLevels();
    CheckAiHeal();
    CheckSmartAi();
    CheckMyrrh();
    CheckJoinRanks();
    CheckArmedDancers();
    Expect(Reforged_SkillPassCheck(), "Pass: a boxed-in unit moves only with Pass", 0, 0);
    CheckCanto();
    CheckDevil();
    CheckStaffDrop();
    sInCheck = 0;
    LoadRNState(seeds);
    for (i = 0; i < 10; i++)
        Reforged_SetRunFix(ids[i], keep[i]);
    PortLog("[reforged] game fixes check: %s (%d mismatches)\n", sFails ? "FAIL" : "PASS", sFails);
}

#endif /* PORT_PLAYED_BUILD */
