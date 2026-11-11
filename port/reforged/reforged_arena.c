#include "global.h"

/*
 * Reforged Options, Helpers: "Arena odds" (RFO_ARENA_ODDS, v1.11.0; the owner's answer of 2026-11-07:
 * arena hit/crit odds). Display only, on by default, like True hit.
 *
 * The original shows the arena opponent's window (src/uiarena.c DrawArenaOpponentDetailsText: level,
 * class, name, weapon) only after the wager is paid, and no odds at all. With the option on:
 *   - the window is shown before the wager question (RFO_ARENA_ODDS_EARLY in ArenaUi_WagerGoldDialogue),
 *   - and it gets two more lines (RFO_ARENA_DETAILS): your unit and the opponent, each with Hit (True
 *     hit's number when that option is on), Dmg ("x2" when it strikes twice) and Crit.
 * The odds are the arena battle's own (src/bmbattle.c BattleGenerateArena: the arena weapon slots, the
 * weapon triangle, the arena's terrain, skills), computed as a simulation with no battle unwound, no
 * suspend written and no random number drawn; BattleGenerateArena sets every battle global up again
 * before the fight. The matchup was fixed when the arena opened (ArenaBegin), so nothing changes.
 *
 * Test switch: FE8PC_TEST_ARENA_CLASS=0xNN gives the arena fighter that class for the fight.
 */

#if PORT_PLAYED_BUILD

#include <stdlib.h>
#include <string.h>

#include "bmunit.h"
#include "bmbattle.h"
#include "bmitem.h"
#include "bmarena.h"
#include "fontgrp.h"
#include "uiutils.h"
#include "hardware.h"
#include "constants/classes.h"
#include "constants/items.h"

#include "port_reforged.h"

void DrawUiFrame2(int x, int y, int width, int height, int style);

/* The arena battle's numbers, as BattleGenerateArena sets them up, without its battle. */
static void ArenaForecast(void)
{
    gBattleStats.config = BATTLE_CONFIG_SIMULATE | BATTLE_CONFIG_ARENA;
    InitBattleUnit(&gBattleActor, gArenaState.playerUnit);
    InitBattleUnit(&gBattleTarget, gArenaState.opponentUnit);
    gBattleStats.range = gArenaState.range;
    gBattleTarget.unit.xPos = gBattleActor.unit.xPos + gArenaState.range;
    gBattleTarget.unit.yPos = gBattleActor.unit.yPos;
    SetBattleUnitWeapon(&gBattleActor, BU_ISLOT_ARENA_PLAYER);
    SetBattleUnitWeapon(&gBattleTarget, BU_ISLOT_ARENA_OPPONENT);
    BattleApplyWeaponTriangleEffect(&gBattleActor, &gBattleTarget);
    SetBattleUnitTerrainBonusesAuto(&gBattleActor);
    SetBattleUnitTerrainBonuses(&gBattleTarget, 8);
    ComputeBattleUnitStats(&gBattleActor, &gBattleTarget);
    ComputeBattleUnitStats(&gBattleTarget, &gBattleActor);
    Reforged_SkillBattleMods();
    ComputeBattleUnitEffectiveStats(&gBattleActor, &gBattleTarget);
    ComputeBattleUnitEffectiveStats(&gBattleTarget, &gBattleActor);
}

/* One line: name, Hit, Dmg (x2), Crit. */
static void OddsLine(int y, struct BattleUnit *bu, struct BattleUnit *foe, int twice)
{
    int dmg, crit, hit;

    if (bu->weapon == 0)
    {
        dmg = hit = crit = -1; /* "--" */
    }
    else
    {
        dmg = bu->battleAttack - foe->battleDefense;
        if (GetItemIndex(bu->weapon) == ITEM_MONSTER_STONE)
            dmg = 0xFF;
        if (dmg < 0)
            dmg = 0;
        hit = RFO_SHOWN_HIT(bu->battleEffectiveHitRate > 100 ? 100 : bu->battleEffectiveHitRate);
        crit = bu->battleEffectiveCritRate > 100 ? 100 : bu->battleEffectiveCritRate < 0 ? 0 : bu->battleEffectiveCritRate;
    }
    PutString(TILEMAP_LOCATED(gBG0TilemapBuffer, 8, y), TEXT_COLOR_SYSTEM_WHITE,
        GetStringFromIndex(bu->unit.pCharacterData->nameTextId));
    /* columns: name 8-13, Hit 14-15 and its number ending at 17, Dmg 18-20 and its number ending at 22,
       x2 23-24, Crt 25-26 and its number ending at 28 */
    PutString(TILEMAP_LOCATED(gBG0TilemapBuffer, 14, y), TEXT_COLOR_SYSTEM_GOLD, "Hit");
    PutNumberTwoChr(TILEMAP_LOCATED(gBG0TilemapBuffer, 17, y), TEXT_COLOR_SYSTEM_BLUE, hit);
    PutString(TILEMAP_LOCATED(gBG0TilemapBuffer, 18, y), TEXT_COLOR_SYSTEM_GOLD, "Dmg");
    PutNumberTwoChr(TILEMAP_LOCATED(gBG0TilemapBuffer, 22, y), TEXT_COLOR_SYSTEM_BLUE, dmg);
    if (twice && dmg > 0)
        PutString(TILEMAP_LOCATED(gBG0TilemapBuffer, 23, y), TEXT_COLOR_SYSTEM_WHITE, "x2");
    PutString(TILEMAP_LOCATED(gBG0TilemapBuffer, 25, y), TEXT_COLOR_SYSTEM_GOLD, "Crt");
    PutNumberTwoChr(TILEMAP_LOCATED(gBG0TilemapBuffer, 28, y), TEXT_COLOR_SYSTEM_BLUE, crit);
}

/* The test switch's fighter, while the arena is open: its own class, and the weapon kind whose rank was
   raised for the substitute weapon (-1: none) with the rank before and after. */
static struct {
    struct Unit *unit;
    const struct ClassData *cls;
    int type;
    u8 rank, raised;
} sTest;

/* src/uiarena.c ArenaUi_OnEnd (the arena closes, after the fight or without one): the test switch's
   fighter gets its own class back, so no test class (an FE7 one above all) stays on the unit and reaches
   a save; a raised rank goes back too, keeping the weapon experience the fight gave. */
void Reforged_ArenaTestEnd(void)
{
    struct Unit *unit = sTest.unit;

    if (unit == NULL)
        return;
    sTest.unit = NULL;
    if (unit->pCharacterData == NULL || unit->pClassData == sTest.cls)
        return; /* (already its own: a suspend written in the fight was resumed, Reforged_ArenaTestSuspend) */
    unit->pClassData = sTest.cls;
    if (sTest.type >= 0)
    {
        int gained = unit->ranks[sTest.type] - sTest.raised;

        unit->ranks[sTest.type] = (u8)(sTest.rank + (gained > 0 && sTest.cls->baseRanks[sTest.type] ? gained : 0)); /* the gain only where the own class uses that kind */
    }
    PortLog("[reforged] arena test: the fighter's class is 0x%02X again\n", unit->pClassData->number);
}

/* src/bmsave.c WriteSuspendSave (the arena writes one during the fight, SUSPEND_POINT_DURINGARENA): while
   it is encoded (before = 1 ... before = 0), the test switch's fighter carries its own class and rank,
   as Reforged_ArenaTestEnd would give them back. A suspend resumed in a new process (where sTest is
   empty) then never keeps the test class: the resumed fight is the unit's own (ArenaResume draws it
   again from the unit). */
void Reforged_ArenaTestSuspend(int before)
{
    static struct {
        struct Unit *unit;
        const struct ClassData *cls;
        u8 rank;
    } sHeld;
    struct Unit *unit = before ? sTest.unit : sHeld.unit;

    if (unit == NULL || unit->pCharacterData == NULL)
        return;
    if (before)
    {
        sHeld.unit = unit;
        sHeld.cls = unit->pClassData;
        unit->pClassData = sTest.cls;
        if (sTest.type >= 0)
        {
            int gained = unit->ranks[sTest.type] - sTest.raised;

            sHeld.rank = unit->ranks[sTest.type];
            unit->ranks[sTest.type] = (u8)(sTest.rank + (gained > 0 && sTest.cls->baseRanks[sTest.type] ? gained : 0)); /* the gain only where the own class uses that kind */
        }
        return;
    }
    sHeld.unit = NULL;
    unit->pClassData = sHeld.cls;
    if (sTest.type >= 0)
        unit->ranks[sTest.type] = sHeld.rank;
    PortLog("[reforged] arena test: suspend written with the fighter's own class 0x%02X\n", sTest.cls->number);
}

/* include/bmarena.h RFO_ARENA_REFUSES (src/bmarena.c ArenaIsUnitAllowed): "Armed dancers" (v1.18.0) gives a
   Dancer and the Bard a tome rank, but the arena would play their fight as the map animation over a black
   screen (it has no map behind it), or as the Dancer's dance, which can hang: they stay out of the arena, as
   in the original (its Arena command is shown greyed: "You can't fight, so you can't enter the arena"). */
int Reforged_ArenaRefuses(struct Unit *unit)
{
    return Reforged_RunFix(RFO_FIX_ARMED_DANCERS) && Reforged_ArmedDancerClass(unit);
}

/* The arena test switch's rank of class cd in weapon kind type: its base rank, or with "Armed dancers" on
   (v1.18.0, reforged_gamefixes.c) the fix's E for a Dancer's light and the Bard's anima. */
static int TestRank(const struct ClassData *cd, int type)
{
    if (cd->baseRanks[type] != 0 || !Reforged_RunFix(RFO_FIX_ARMED_DANCERS))
        return cd->baseRanks[type];
    if ((cd->number == CLASS_DANCER && type == ITYPE_LIGHT) ||
        (cd->number == REFORGED_FE7_JID_BARD && Reforged_Fe7ClassAvailable(cd->number) && type == ITYPE_ANIMA))
        return WPN_EXP_E;
    return 0;
}

/* Test switch FE8PC_TEST_ARENA_FOE_CLASS=0xNN[:0xII][:crit] (v1.21.0, for looking at a class's battle animation in
   enemy colours: the arena opponent is a red unit): the arena opponent takes that class for the fight, with item II
   (an FE8 weapon of a kind the class has a rank in) or its first weapon kind's iron weapon (":crit": Skill 120, so
   its blows land and are critical); the range and wager are worked out again. The opponent is the arena's own temporary unit (gArenaOpponent, drawn again for each arena), so nothing
   is put back. */
static void ArenaTestFoe(void)
{
    static const u8 sWeapons[8] = { ITEM_SWORD_IRON, ITEM_LANCE_IRON, ITEM_AXE_IRON, ITEM_BOW_IRON,
                                    ITEM_NONE, ITEM_ANIMA_FIRE, ITEM_LIGHT_LIGHTNING, ITEM_DARK_FLUX };
    const char *env = getenv("FE8PC_TEST_ARENA_FOE_CLASS");
    struct Unit *foe = gArenaState.opponentUnit;
    const struct ClassData *cd;
    char *end = NULL;
    int jid, item = 0, type;

    if (env == NULL || foe == NULL || gArenaState.playerUnit == NULL || foe->pClassData == NULL)
        return;
    jid = (int)strtol(env, &end, 0);
    if (end != NULL && *end == ':')
        item = (int)strtol(end + 1, NULL, 0);
    if (jid < 1 || jid > REFORGED_FOREIGN_LAST_JID || (jid > 0x7F && !Reforged_Fe7ClassAvailable(jid)) ||
        foe->pClassData->number == jid)
        return; /* refused, or already done for this arena */
    cd = GetClassData(jid);
    if (item > 0 && item <= 0xCD && GetItemType(item) < 8 && TestRank(cd, GetItemType(item)) != 0)
        type = GetItemType(item);
    else
    {
        for (type = 0; type < 8 && (TestRank(cd, type) == 0 || sWeapons[type] == ITEM_NONE); type++)
            ;
        if (type == 8)
            return;
        item = sWeapons[type];
    }
    foe->pClassData = cd;
    if (strstr(env, ":crit") != NULL)
        foe->skl = 120;
    if (foe->ranks[type] < TestRank(cd, type))
        foe->ranks[type] = (u8)TestRank(cd, type);
    gArenaState.opponentWeapon = MakeNewItem(item);
    gArenaState.opponentWpnType = (u8)type;
    gArenaState.opponentIsMagic = IsWeaponMagic(type);
    gArenaState.range = gArenaState.playerWpnType == ITYPE_BOW || type == ITYPE_BOW ? 2 : 1;
    gArenaState.playerPowerWeight = ArenaGetPowerRanking(gArenaState.playerUnit, gArenaState.opponentIsMagic);
    gArenaState.opponentPowerWeight = ArenaGetPowerRanking(foe, gArenaState.playerIsMagic);
    ArenaGenerateMatchupGoldValue();
    PortLog("[reforged] arena test: the opponent's class is now 0x%02X (%s), weapon 0x%02X, range %d%s\n", jid,
        GetStringFromIndex(cd->nameTextId), item, gArenaState.range, strstr(env, ":crit") != NULL ? ", Skill 120" : "");
}

int Reforged_ArenaOddsOn(void)
{
    /* test switch FE8PC_TEST_ARENA_CLASS=0xNN: the arena fighter takes that class for the fight (for
       recordings of class skills, e.g. Astra); the matchup was already drawn */
    const char *test = getenv("FE8PC_TEST_ARENA_CLASS");

    if (test != NULL && gArenaState.playerUnit != NULL)
    {
        static int sRefused;
        int jid = (int)strtol(test, NULL, 0);

        /* FE8's classes 0x01-0x7F; the FE7 classes 0x80-0x88 and the FE6 ones 0x89-0x8F (v1.19.0, v1.20.0) only when
           they were built (reforged_fe7.c) */
        if (jid < 1 || jid > REFORGED_FOREIGN_LAST_JID || (jid > 0x7F && !Reforged_Fe7ClassAvailable(jid)))
        {
            if (!sRefused)
                sRefused = 1, PortLog("[reforged] arena test: class 0x%02X refused (%s)\n", jid,
                    jid > REFORGED_FE7_LAST_JID && jid <= REFORGED_FOREIGN_LAST_JID ? "an FE6 class, and the FE6 classes are not loaded"
                    : jid > 0x7F && jid <= REFORGED_FE7_LAST_JID ? "an FE7 class, and the FE7 classes are not loaded"
                                                               : "no such class");
        }
        else if (sTest.unit != gArenaState.playerUnit)
        {
            struct Unit *unit = gArenaState.playerUnit;
            const struct ClassData *cd = GetClassData(jid);
            int type = GetItemType(gArenaState.playerWeapon);

            /* the unit's own class and ranks, put back when the arena closes (Reforged_ArenaTestEnd) */
            sTest.unit = unit;
            sTest.cls = unit->pClassData;
            sTest.type = -1;
            unit->pClassData = cd;
            /* FE8PC_TEST_ARENA_WEAPON=0xII[:2] (with the class switch): that weapon instead, when the class has
               a rank in its kind (":2": the fight at range 2, for a thrown axe or a tome) */
            const char *wenv = getenv("FE8PC_TEST_ARENA_WEAPON");
            char *wend = NULL;
            int forced = wenv != NULL ? (int)strtol(wenv, &wend, 0) : 0;

            if (forced > 0 && forced <= 0xCD /* ITEM_UNK_CD, the last FE8 item */ && GetItemType(forced) < 8 && TestRank(cd, GetItemType(forced)) != 0)
                type = 0x100 | GetItemType(forced); /* (bit 8: taken below) */
            else
                forced = 0;
            /* a class that cannot use the arena weapon gets its first weapon kind's (a Nomad: a bow) */
            if (forced != 0 || (type < 8 && TestRank(cd, type) == 0))
            {
                static const u8 sWeapons[8] = { ITEM_SWORD_IRON, ITEM_LANCE_IRON, ITEM_AXE_IRON, ITEM_BOW_IRON,
                                                ITEM_NONE, ITEM_ANIMA_FIRE, ITEM_LIGHT_LIGHTNING, ITEM_DARK_FLUX };

                if (forced != 0)
                    type &= 0xFF;
                else
                    for (type = 0; type < 8 && (TestRank(cd, type) == 0 || sWeapons[type] == ITEM_NONE); type++)
                        ;
                if (type < 8)
                {
                    gArenaState.playerWeapon = MakeNewItem(forced != 0 ? forced : sWeapons[type]);
                    gArenaState.playerWpnType = type;
                    sTest.type = type;
                    sTest.rank = unit->ranks[type];
                    if (unit->ranks[type] < TestRank(cd, type))
                        unit->ranks[type] = TestRank(cd, type);
                    sTest.raised = unit->ranks[type];
                    /* the range and the matchup again, as ArenaBeginInternal and ArenaGenerateBaseWeapons set
                       them (no random number: the opponent stays the one drawn) */
                    gArenaState.range = gArenaState.playerWpnType == ITYPE_BOW || gArenaState.opponentWpnType == ITYPE_BOW ||
                                        (forced != 0 && wend != NULL && strcmp(wend, ":2") == 0) ? 2 : 1;
                    gArenaState.playerIsMagic = IsWeaponMagic(gArenaState.playerWpnType);
                    gArenaState.playerPowerWeight = ArenaGetPowerRanking(unit, gArenaState.opponentIsMagic);
                    gArenaState.opponentPowerWeight = ArenaGetPowerRanking(gArenaState.opponentUnit, gArenaState.playerIsMagic);
                    ArenaGenerateMatchupGoldValue();
                }
            }
            if (!Reforged_RunFix(RFO_FIX_SKILLS))
                Reforged_SetRunFix(RFO_FIX_SKILLS, 1); /* class skills from here on (the route before stays as recorded) */
            PortLog("[reforged] arena test: the fighter's class is now 0x%02X (%s), weapon 0x%02X\n", cd->number,
                GetStringFromIndex(cd->nameTextId), GetItemIndex(gArenaState.playerWeapon));
        }
    }
    ArenaTestFoe();
    return Reforged_GetOption(RFO_ARENA_ODDS) != 0 && gArenaState.playerUnit != NULL && gArenaState.opponentUnit != NULL;
}

/* src/uiarena.c DrawArenaOpponentDetailsText (RFO_ARENA_DETAILS): the window with the odds; 1 when
   drawn (the original's own drawing is then skipped). */
int Reforged_ArenaDrawDetails(void)
{
    static int sLogged;
    struct BattleUnit *first, *second;
    int follow;

    if (!Reforged_ArenaOddsOn())
        return 0;
    ArenaForecast();
    follow = BattleGetFollowUpOrder(&first, &second);

    DrawUiFrame2(7, 9, 23, 10, 0);
    SetTextFont(0);
    InitSystemTextFont();
    /* the original's lines */
    PutString(TILEMAP_LOCATED(gBG0TilemapBuffer, 8, 10), 0, GetStringFromIndex(gMid_Lv));
    PutNumber(TILEMAP_LOCATED(gBG0TilemapBuffer, 12, 10), 2, gArenaState.opponentUnit->level);
    PutString(TILEMAP_LOCATED(gBG0TilemapBuffer, 8, 12), 0, GetStringFromIndex(gArenaState.opponentUnit->pCharacterData->nameTextId));
    PutString(TILEMAP_LOCATED(gBG0TilemapBuffer, 15, 10), 0, GetStringFromIndex(gArenaState.opponentUnit->pClassData->nameTextId));
    PutString(TILEMAP_LOCATED(gBG0TilemapBuffer, 15, 12), 0, GetItemName(gArenaState.opponentWeapon));
    /* the odds */
    OddsLine(14, &gBattleActor, &gBattleTarget, follow && first == &gBattleActor);
    OddsLine(16, &gBattleTarget, &gBattleActor, follow && first == &gBattleTarget);
    BG_EnableSyncByMask(BG0_SYNC_BIT | BG1_SYNC_BIT);
    if (sLogged < 10)
        sLogged++, PortLog("[reforged] arena odds: unit 0x%02X hit %d dmg %d crit %d%s vs unit 0x%02X hit %d dmg %d crit %d%s\n",
            (u8)gBattleActor.unit.index, gBattleActor.battleEffectiveHitRate,
            gBattleActor.battleAttack - gBattleTarget.battleDefense, gBattleActor.battleEffectiveCritRate,
            follow && first == &gBattleActor ? " x2" : "", (u8)gBattleTarget.unit.index,
            gBattleTarget.battleEffectiveHitRate, gBattleTarget.battleAttack - gBattleActor.battleDefense,
            gBattleTarget.battleEffectiveCritRate, follow && first == &gBattleTarget ? " x2" : "");
    return 1;
}

#endif /* PORT_PLAYED_BUILD */
