#include "global.h"

/*
 * Reforged Options, the Cheats (port-plan/reforged-next-briefs.md item 6;
 * DECISIONS.md 2026-10-29 "No Konami code" and "Fixed growths cheat"; the
 * research and the checked code facts: reforged-qol-research.md "Cheats: gold,
 * growths, EXP"). Played builds only; empty in every test build. Every cheat
 * is OFF by default, and OFF runs the retail code untouched (each hook returns
 * before doing anything).
 *
 * Infinite gold (cheat_infinite_gold): GetPartyGoldAmount (src/bmgold.c) tops
 *   the party's gold back up to 999,999 (the game's own cap) each time the game
 *   reads it, so a spend (a shop, the arena's wager, an event) is refilled at
 *   the next read and everything shows and can pay 999,999. Chapter index 5
 *   (CHAPTER_L_5X, Ephraim's separate party), where retail returns 0, is left
 *   alone: no refill, 0 as retail.
 *
 * Growths (cheat_growths), in CheckBattleUnitLevelUp (src/bmbattle.c; battle,
 *   staff, dance and other action level-ups, on gBattleActor/gBattleTarget;
 *   not the non-battle UnitLevelUp, and not the stack copies that
 *   UnitAutolevelRealistic and Chapter 8's lord catch-up level up):
 *   - Fixed: TR143's rule (FEBuilder "fixed growths mode", feuniverse.us
 *     /t/fe6-fe7-fe8-fixed-growths-mode/4482; our own code from its
 *     description): with g = the growth left after the guaranteed +1 per full
 *     100 above 100 (Metis' +5, US_GROWTH_BOOST, counted) and L = the level
 *     just reached (L + 19 for a promoted unit), the stat rises by 1 when
 *     ((L - 2) * g + 50) mod 100 + g >= 100: the running total is the rounded
 *     average, from a half point. The retail rolls are still made, the reroll
 *     included (which leaves out Metis' bonus, as retail does), so the random
 *     number sequence advances exactly as it would with the cheat off; their
 *     results are then replaced. Stat caps apply afterwards, as retail.
 *   - +25% / +50%: added to every growth before each GetStatIncrease, the
 *     reroll included (still without Metis' bonus there, as retail). Caps
 *     apply.
 *
 * EXP gained (cheat_exp, x2/x3): a battle's EXP (GetBattleUnitExpGain, after
 *   ModifyUnitSpecialExp), staff EXP that hit (a miss's 1 is kept), dance EXP
 *   and the other actions' 10 (BattleApplyItemExpGains,
 *   BattleApplyMiscActionExpGains) are multiplied, then capped at 100 again.
 *   0 stays 0, and a battle without damage keeps retail's 1. EXP before is
 *   below 100 and the gain at most 100, so the level-up check (an if) still
 *   gives at most one level per battle.
 *
 * FE8PC_RFO_CHECK=1 (the test switch of reforged_dangerzone.c): at the first
 * level-up check of the run (CheckBattleUnitLevelUp, called after every battle
 * for both units, whatever their faction), a self-check with the save's own units on scratch
 * copies (the random seeds, gold, the battle units and every option are put
 * back afterwards, so the game goes on as if it had not run), logged as
 * "[reforged] cheats check ...": fixed gains against the rounded-average total
 * over every level to 20 and the random seeds against the cheat-off run;
 * boosted gains against random numbers drawn ahead; EXP x1/x2/x3 for a hit,
 * a kill and no damage, staff and dance; the gold refill and Chapter 5x. Also,
 * while it is set, each real level-up and EXP gain with a cheat on is logged.
 */

#if PORT_PLAYED_BUILD

#include <string.h>

#include "bmunit.h"
#include "bmbattle.h"
#include "bmitem.h"
#include "rng.h"
#include "constants/items.h"

#include "port_reforged.h"

void PortLog(const char *fmt, ...);
char *getenv(const char *name);

#define CHEAT_GOLD_MAX 999999
#define CHEAT_STATS    7 /* HP, Str/Mag, Skl, Spd, Def, Res, Lck: the level-up order */

static int sInSelfCheck; /* the self-check's scratch level-ups and EXP are not logged one by one */

static int CheckOn(void)
{
    static int on = -1;

    if (on < 0)
        on = getenv("FE8PC_RFO_CHECK") != NULL;
    return on && !sInSelfCheck;
}

/* --- Infinite gold --- */

void Reforged_CheatGoldRefill(void)
{
    if (Reforged_GetOption(RFO_CHEAT_GOLD) && gPlaySt.partyGoldAmount < CHEAT_GOLD_MAX)
        gPlaySt.partyGoldAmount = CHEAT_GOLD_MAX;
}

/* --- Growths --- */

static void UnitGrowths(const struct Unit *unit, int growth[CHEAT_STATS])
{
    const struct CharacterData *c = unit->pCharacterData;

    growth[0] = c->growthHP;
    growth[1] = c->growthPow;
    growth[2] = c->growthSkl;
    growth[3] = c->growthSpd;
    growth[4] = c->growthDef;
    growth[5] = c->growthRes;
    growth[6] = c->growthLck;
}

/* The retail rolls of CheckBattleUnitLevelUp, each growth + boost: seven
   GetStatIncrease calls with Metis' bonus, then, if nothing rose, up to two
   passes without it that stop at the first stat that rises. */
static void RollGains(const struct Unit *unit, int boost, s8 out[CHEAT_STATS])
{
    int growth[CHEAT_STATS], bonus = (unit->state & US_GROWTH_BOOST) ? 5 : 0;
    int i, pass, total = 0;

    UnitGrowths(unit, growth);
    for (i = 0; i < CHEAT_STATS; i++)
    {
        out[i] = GetStatIncrease(growth[i] + bonus + boost);
        total += out[i];
    }
    if (total != 0)
        return;
    for (pass = 0; pass < 2; pass++)
    {
        for (i = 0; i < CHEAT_STATS; i++)
        {
            out[i] = GetStatIncrease(growth[i] + boost);
            if (out[i])
                return;
        }
    }
}

/* TR143's fixed gain for a growth (Metis' bonus included) at the level just
   reached (promoted: + 19). */
static int FixedGain(int growth, int level)
{
    int gain = 0;

    while (growth > 100)
    {
        gain++;
        growth -= 100;
    }
    if (((level - 2) * growth + 50) % 100 + growth >= 100)
        gain++;
    return gain;
}

static int FixedLevel(const struct Unit *unit)
{
    return unit->level + ((UNIT_CATTRIBUTES(unit) & CA_PROMOTED) ? 19 : 0);
}

static void FixedGains(const struct Unit *unit, s8 out[CHEAT_STATS])
{
    int growth[CHEAT_STATS], bonus = (unit->state & US_GROWTH_BOOST) ? 5 : 0, i;

    UnitGrowths(unit, growth);
    for (i = 0; i < CHEAT_STATS; i++)
        out[i] = FixedGain(growth[i] + bonus, FixedLevel(unit));
}

/* CheckBattleUnitLevelUp with the cheat on: the retail function (src/bmbattle.c)
   step for step, with the gains from RollGains/FixedGains. */
static void SelfCheck(void);

int Reforged_CheatLevelUp(struct BattleUnit *bu)
{
    static int checked;
    int mode;
    s8 gain[CHEAT_STATS], rolled[CHEAT_STATS];

    if (!checked)
    {
        checked = 1;
        if (CheckOn())
        {
            SelfCheck();
            Reforged_GameFixesSelfCheck(); /* the Game fixes' check (reforged_gamefixes.c) */
        }
    }

    mode = Reforged_GetOption(RFO_CHEAT_GROWTHS);
    if (mode == 0)
        return 0;

    /* Only the level-ups of a battle, staff, dance or other action, which the
       game checks on gBattleActor / gBattleTarget (src/bmbattle.c). The game
       also calls CheckBattleUnitLevelUp on a scratch copy on the stack to level
       units up when they load (UnitAutolevelRealistic, src/bmunit.c: enemy
       bosses and red characters below 0x40 copied into a blue slot, and blue
       recruits above their base level) and for Chapter 8's lord catch-up
       (ch8-eventscript.h): those stay retail (refute rfo-cheats bug 1). */
    if (!sInSelfCheck && bu != &gBattleActor && bu != &gBattleTarget)
        return 0;

    if (CanBattleUnitGainLevels(bu) && bu->unit.exp >= 100)
    {
        bu->unit.exp -= 100;
        bu->unit.level++;

        if (UNIT_CATTRIBUTES(&bu->unit) & CA_MAXLEVEL10)
        {
            if (bu->unit.level == 10)
            {
                bu->expGain -= bu->unit.exp;
                bu->unit.exp = UNIT_EXP_DISABLED;
            }
        }
        else if (bu->unit.level == 20)
        {
            bu->expGain -= bu->unit.exp;
            bu->unit.exp = UNIT_EXP_DISABLED;
        }

        if (mode == 1)
        {
            RollGains(&bu->unit, 0, rolled); /* the retail random numbers, used up as retail */
            FixedGains(&bu->unit, gain);
        }
        else
        {
            RollGains(&bu->unit, mode == 2 ? 25 : 50, gain);
            memcpy(rolled, gain, sizeof(rolled));
        }

        bu->changeHP = gain[0];
        bu->changePow = gain[1];
        bu->changeSkl = gain[2];
        bu->changeSpd = gain[3];
        bu->changeDef = gain[4];
        bu->changeRes = gain[5];
        bu->changeLck = gain[6];

        CheckBattleUnitStatCaps(GetUnit(bu->unit.index), bu);

        if (CheckOn())
            PortLog("[reforged] cheat level-up: unit 0x%02X char 0x%02X to level %d, growths %s: "
                "HP %d Str %d Skl %d Spd %d Def %d Res %d Lck %d (rolls %d%d%d%d%d%d%d)\n",
                bu->unit.index, bu->unit.pCharacterData->number, bu->unit.level,
                mode == 1 ? "fixed" : mode == 2 ? "+25%" : "+50%",
                bu->changeHP, bu->changePow, bu->changeSkl, bu->changeSpd, bu->changeDef, bu->changeRes,
                bu->changeLck, rolled[0], rolled[1], rolled[2], rolled[3], rolled[4], rolled[5], rolled[6]);
    }
    return 1;
}

/* --- EXP gained --- */

static int MultiplyExp(int exp)
{
    int mult = Reforged_GetOption(RFO_CHEAT_EXP) + 1;
    int out = exp;

    if (mult > 1 && exp > 0)
    {
        out = exp * mult;
        if (out > 100)
            out = 100;
    }
    return out;
}

int Reforged_CheatBattleExp(int exp)
{
    int out = MultiplyExp(exp);

    if (CheckOn() && out != exp)
        PortLog("[reforged] cheat EXP: battle %d -> %d\n", exp, out);
    return out;
}

int Reforged_CheatStaffExp(int exp)
{
    int out;

    if (gBattleHitArray->attributes & BATTLE_HIT_ATTR_MISS)
        return exp; /* a miss: retail's 1 */
    out = MultiplyExp(exp);
    if (CheckOn() && out != exp)
        PortLog("[reforged] cheat EXP: staff %d -> %d\n", exp, out);
    return out;
}

int Reforged_CheatActionExp(int exp)
{
    int out = MultiplyExp(exp);

    if (CheckOn() && out != exp)
        PortLog("[reforged] cheat EXP: action %d -> %d\n", exp, out);
    return out;
}

/* --- FE8PC_RFO_CHECK self-check --- */

static int sCheckFails;

static void Expect(int ok, const char *what)
{
    if (!ok)
    {
        sCheckFails++;
        PortLog("[reforged] cheats check MISMATCH: %s\n", what);
    }
}

static int SeedsEqual(const u16 *a, const u16 *b)
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

static void ScratchLevelUp(struct BattleUnit *tmp, const struct Unit *unit, int levelFrom, s8 out[CHEAT_STATS])
{
    memset(tmp, 0, sizeof(*tmp));
    tmp->unit = *unit;
    tmp->unit.level = levelFrom;
    tmp->unit.exp = 100;
    CheckBattleUnitLevelUp(tmp);
    out[0] = tmp->changeHP;
    out[1] = tmp->changePow;
    out[2] = tmp->changeSkl;
    out[3] = tmp->changeSpd;
    out[4] = tmp->changeDef;
    out[5] = tmp->changeRes;
    out[6] = tmp->changeLck;
}

/* Fixed: every level-up from the unit's level to its last, each gain against
   the rounded-average running total (a different form of TR143's rule:
   total(L) = floor(((L - 1) * g + 50) / 100) for the leftover g, plus the
   guaranteed part), and the seeds after each against a cheat-off run. */
static void CheckFixed(const struct Unit *unit)
{
    struct BattleUnit tmp;
    int growth[CHEAT_STATS], bonus = (unit->state & US_GROWTH_BOOST) ? 5 : 0;
    int top = (UNIT_CATTRIBUTES(unit) & CA_MAXLEVEL10) ? 10 : 20;
    int lv, i, total[CHEAT_STATS] = { 0 }, seedsSame = 0, levels = 0, fails = sCheckFails;
    char line[CHEAT_STATS][24];

    UnitGrowths(unit, growth);
    memset(line, 0, sizeof(line));
    for (lv = unit->level; lv < top; lv++)
    {
        u16 seeds[3], afterOff[3], afterOn[3];
        s8 off[CHEAT_STATS], on[CHEAT_STATS];
        int L = lv + 1 + ((UNIT_CATTRIBUTES(unit) & CA_PROMOTED) ? 19 : 0);

        StoreRNState(seeds);
        Reforged_SetOption(RFO_CHEAT_GROWTHS, 0);
        ScratchLevelUp(&tmp, unit, lv, off);
        StoreRNState(afterOff);
        LoadRNState(seeds);
        Reforged_SetOption(RFO_CHEAT_GROWTHS, 1);
        ScratchLevelUp(&tmp, unit, lv, on);
        StoreRNState(afterOn);
        seedsSame += SeedsEqual(afterOff, afterOn);
        levels++;

        for (i = 0; i < CHEAT_STATS; i++)
        {
            int g = growth[i] + bonus, sure = 0, want;

            while (g > 100)
            {
                sure++;
                g -= 100;
            }
            want = sure + ((L - 1) * g + 50) / 100 - ((L - 2) * g + 50) / 100;
            Expect(on[i] == want, "fixed gain");
            total[i] += on[i];
            if (lv - unit->level < 23)
                line[i][lv - unit->level] = '0' + on[i];
        }
    }
    Expect(seedsSame == levels, "fixed growths: random seeds differ from the cheat-off run");
    PortLog("[reforged] cheats check fixed: char 0x%02X %s level %d to %d, growths %d/%d/%d/%d/%d/%d/%d%s: "
        "gains HP %s Str %s Skl %s Spd %s Def %s Res %s Lck %s; totals %d/%d/%d/%d/%d/%d/%d; "
        "seeds as cheat-off %d of %d; %s\n",
        unit->pCharacterData->number, (UNIT_CATTRIBUTES(unit) & CA_PROMOTED) ? "promoted" : "unpromoted",
        unit->level, top, growth[0], growth[1], growth[2], growth[3], growth[4], growth[5], growth[6],
        bonus ? " +5 Metis" : "", line[0], line[1], line[2], line[3], line[4], line[5], line[6],
        total[0], total[1], total[2], total[3], total[4], total[5], total[6], seedsSame, levels,
        sCheckFails == fails ? "OK" : "MISMATCH");
}

/* +25%/+50% (and off as a control): the gains of one level-up against the
   next seven random numbers drawn ahead (GetStatIncrease = +1 per full 100
   above 100, then one Roll1RN: rise when the leftover > NextRN_100()).
   Level-ups where nothing rose (the reroll) are skipped. */
static void CheckBoost(const struct Unit *unit, int mode)
{
    struct BattleUnit tmp;
    int boost = mode == 2 ? 25 : mode == 3 ? 50 : 0;
    int growth[CHEAT_STATS], bonus = (unit->state & US_GROWTH_BOOST) ? 5 : 0;
    int n, i, compared = 0, skipped = 0, fails = sCheckFails;
    int risen[CHEAT_STATS] = { 0 }, top = (UNIT_CATTRIBUTES(unit) & CA_MAXLEVEL10) ? 9 : 19;

    UnitGrowths(unit, growth);
    for (n = 0; n < 200; n++)
    {
        u16 seeds[3];
        s8 want[CHEAT_STATS], got[CHEAT_STATS];
        int total = 0;

        StoreRNState(seeds);
        for (i = 0; i < CHEAT_STATS; i++)
        {
            int g = growth[i] + bonus + boost, sure = 0;

            while (g > 100)
            {
                sure++;
                g -= 100;
            }
            want[i] = sure + (g > NextRN_100());
            total += want[i];
        }
        LoadRNState(seeds);
        Reforged_SetOption(RFO_CHEAT_GROWTHS, mode);
        ScratchLevelUp(&tmp, unit, unit->level < top ? unit->level : top - 1, got);
        if (total == 0)
        {
            skipped++;
            continue;
        }
        compared++;
        for (i = 0; i < CHEAT_STATS; i++)
        {
            /* the caps can only lower a gain (the scratch unit has the real stats) */
            Expect(got[i] == want[i] || (got[i] < want[i] && got[i] >= 0), "boosted gain");
            risen[i] += got[i];
        }
    }
    PortLog("[reforged] cheats check growths %s: char 0x%02X, growths %d/%d/%d/%d/%d/%d/%d: %d level-ups "
        "as drawn ahead (%d with a reroll skipped); risen per 100: %d/%d/%d/%d/%d/%d/%d; %s\n",
        mode == 0 ? "off" : mode == 2 ? "+25%" : "+50%", unit->pCharacterData->number,
        growth[0], growth[1], growth[2], growth[3], growth[4], growth[5], growth[6], compared, skipped,
        risen[0] * 100 / (compared ? compared : 1), risen[1] * 100 / (compared ? compared : 1),
        risen[2] * 100 / (compared ? compared : 1), risen[3] * 100 / (compared ? compared : 1),
        risen[4] * 100 / (compared ? compared : 1), risen[5] * 100 / (compared ? compared : 1),
        risen[6] * 100 / (compared ? compared : 1), sCheckFails == fails ? "OK" : "MISMATCH");
}

/* EXP: the real GetBattleUnitExpGain on scratch copies of a blue and a red
   unit (a hit, a kill, no damage), then staff and dance through
   BattleApplyItemExpGains on a scratch gBattleActor. */
static void CheckExp(const struct Unit *blue, const struct Unit *red)
{
    struct BattleUnit actor, target, savedActor;
    struct BattleHit savedHit;
    static const char *const kinds[] = { "hit", "kill", "no damage" };
    int k, m, v[3], fails = sCheckFails;

    for (k = 0; k < 3; k++)
    {
        for (m = 0; m < 3; m++)
        {
            memset(&actor, 0, sizeof(actor));
            memset(&target, 0, sizeof(target));
            actor.unit = *blue;
            target.unit = *red;
            actor.unit.curHP = actor.unit.maxHP;
            target.unit.curHP = k == 1 ? 0 : 1;
            actor.nonZeroDamage = k != 2;
            Reforged_SetOption(RFO_CHEAT_EXP, m);
            v[m] = GetBattleUnitExpGain(&actor, &target);
        }
        for (m = 1; m < 3; m++)
        {
            int want = k == 2 ? v[0] : v[0] * (m + 1) > 100 ? 100 : v[0] * (m + 1);
            Expect(v[m] == want, "battle EXP");
        }
        PortLog("[reforged] cheats check EXP %s: char 0x%02X on char 0x%02X: x1 %d, x2 %d, x3 %d\n",
            kinds[k], blue->pCharacterData->number, red->pCharacterData->number, v[0], v[1], v[2]);
    }

    savedActor = gBattleActor;
    savedHit = gBattleHitArray[0];
    for (k = 0; k < 3; k++)
    {
        for (m = 0; m < 3; m++)
        {
            memset(&gBattleActor, 0, sizeof(gBattleActor));
            gBattleActor.unit = *blue;
            gBattleActor.unit.exp = 0;
            gBattleHitArray[0].attributes = k == 1 ? BATTLE_HIT_ATTR_MISS : 0;
            if (k < 2)
            {
                gBattleActor.weapon = ITEM_STAFF_HEAL | (30 << 8);
                gBattleActor.weaponAttributes = IA_STAFF;
            }
            else
                gBattleActor.weaponType = ITYPE_DANCE;
            Reforged_SetOption(RFO_CHEAT_EXP, m);
            BattleApplyItemExpGains();
            v[m] = gBattleActor.expGain;
            Expect(gBattleActor.unit.exp == v[m], "staff/dance EXP added");
        }
        for (m = 1; m < 3; m++)
        {
            int want = k == 1 ? v[0] : v[0] * (m + 1) > 100 ? 100 : v[0] * (m + 1);
            Expect(v[m] == want, "staff/dance EXP");
        }
        PortLog("[reforged] cheats check EXP %s: char 0x%02X: x1 %d, x2 %d, x3 %d\n",
            k == 0 ? "Heal staff" : k == 1 ? "Heal staff missed" : "dance", blue->pCharacterData->number,
            v[0], v[1], v[2]);
    }
    gBattleActor = savedActor;
    gBattleHitArray[0] = savedHit;
    PortLog("[reforged] cheats check EXP: %s\n", sCheckFails == fails ? "OK" : "MISMATCH");
}

static void CheckGold(void)
{
    u32 gold = gPlaySt.partyGoldAmount;
    s8 chapter = gPlaySt.chapterIndex;
    int fails = sCheckFails, a, b, c, d, e;

    Reforged_SetOption(RFO_CHEAT_GOLD, 0);
    gPlaySt.partyGoldAmount = 1234;
    a = GetPartyGoldAmount();                       /* off: 1234 */
    Reforged_SetOption(RFO_CHEAT_GOLD, 1);
    b = GetPartyGoldAmount();                       /* on: refilled, 999999 */
    SetPartyGoldAmount(GetPartyGoldAmount() - 4600); /* a spend */
    c = gPlaySt.partyGoldAmount;                     /* 995399 stored ... */
    d = GetPartyGoldAmount();                        /* ... refilled at the next read */
    gPlaySt.chapterIndex = 5;                        /* CHAPTER_L_5X */
    gPlaySt.partyGoldAmount = 1234;
    e = GetPartyGoldAmount();                        /* 0, as retail; nothing refilled */
    Expect(a == 1234 && b == CHEAT_GOLD_MAX && c == CHEAT_GOLD_MAX - 4600 && d == CHEAT_GOLD_MAX
        && e == 0 && gPlaySt.partyGoldAmount == 1234, "gold");
    PortLog("[reforged] cheats check gold: off %d; on %d, after a 4600 spend %d stored, %d read; "
        "chapter 5x %d (stored %d); %s\n", a, b, c, d, e, (int)gPlaySt.partyGoldAmount,
        sCheckFails == fails ? "OK" : "MISMATCH");

    gPlaySt.partyGoldAmount = gold;
    gPlaySt.chapterIndex = chapter;
}

static void SelfCheck(void)
{
    u16 seeds[3];
    int options[RFO_COUNT], i, n = 0;
    const struct Unit *red = NULL, *expUnit = NULL;

    for (i = 0; i < RFO_COUNT; i++)
        options[i] = Reforged_GetOption(i);
    StoreRNState(seeds);
    sCheckFails = 0;
    sInSelfCheck = 1;

    PortLog("[reforged] cheats check: chapter %d, gold %d, seeds %04X %04X %04X\n", gPlaySt.chapterIndex,
        (int)gPlaySt.partyGoldAmount, seeds[0], seeds[1], seeds[2]);

    /* the EXP target: the boss if there is one (a big kill, to show the cap
       at 100), else the red unit of the highest level */
    for (i = 0; i < 50; i++)
    {
        const struct Unit *u = &gUnitArrayRed[i];

        if (u->pCharacterData == NULL || u->pClassData == NULL)
            continue;
        if (red == NULL || (UNIT_CATTRIBUTES(u) & CA_BOSS)
            || (!(UNIT_CATTRIBUTES(red) & CA_BOSS) && u->level > red->level))
            red = u;
    }

    for (i = 0; i < 62; i++)
    {
        const struct Unit *u = &gUnitArrayBlue[i];

        if (u->pCharacterData == NULL || u->pClassData == NULL || u->exp == UNIT_EXP_DISABLED)
            continue;
        CheckFixed(u);
        if (n == 0)
        {
            CheckBoost(u, 0);
            CheckBoost(u, 2);
            CheckBoost(u, 3);
        }
        if (expUnit == NULL || ((UNIT_CATTRIBUTES(expUnit) & CA_PROMOTED) && !(UNIT_CATTRIBUTES(u) & CA_PROMOTED)))
            expUnit = u; /* an unpromoted unit if there is one: more EXP */
        if (++n == 4)
            break;
    }
    if (expUnit != NULL && red != NULL)
        CheckExp(expUnit, red);
    CheckGold();

    for (i = 0; i < RFO_COUNT; i++)
        Reforged_SetOption(i, options[i]);
    LoadRNState(seeds);
    sInSelfCheck = 0;
    PortLog("[reforged] cheats check: %d units, %d mismatches: %s\n", n, sCheckFails, sCheckFails ? "FAIL" : "PASS");
}

#endif /* PORT_PLAYED_BUILD */
