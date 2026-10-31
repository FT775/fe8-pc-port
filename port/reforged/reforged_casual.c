#include "global.h"

/*
 * Reforged Options, "Casual mode" (port-plan/reforged-next-briefs.md item 4;
 * design: port-plan/reforged-qol-research.md "Casual mode and rewind
 * conventions", "Our design"). OFF by default; played builds only, empty in
 * every test build.
 *
 * With the option on, a player unit that falls in combat (bmmind.c
 * KillUnitOnCombatDeath: battles, enemy phase, Devil Axe backfire, ballista)
 * or to a hazard (bmusailment.c ApplyHazardHealing: traps, gas, poison)
 * retreats instead of dying: for the rest of the chapter it is dead exactly as
 * in the original (US_DEAD | US_HIDDEN, so every event check, the unit count,
 * the "everyone is dead" game over and the chapter's records see a death), but
 * InitUnitsupports does not wipe its supports, and its character id goes on
 * the fallen list. ChapterChangeUnitCleanup (bmio.c), which runs before the
 * post-chapter save menu, clears US_DEAD for every unit on the list and then
 * does its retail reset (full HP, no status, hidden and not deployed), so the
 * unit is back for the next chapter with its stats, items and supports.
 *
 * Not a retreat (the retail UnitKill runs): enemy and green units; phantoms;
 * the arena (KillUnitOnArenaDeathMaybe is not hooked); scripted deaths (events'
 * kill commands, eventinfo.c KillPlayerUnitByPid, SET_HP 0, map-anim event battles,
 * which set US_DEAD themselves); the lords (Eirika, Ephraim); the chapter's
 * protected character (ROMChapterData protectCharacterIndex); and any unit
 * whose death ends the game: its defeat quote sets EVFLAG_GAMEOVER (the quote
 * runs in the battle, before the kill, so the flag being set already counts).
 * The game still ends as in the original.
 *
 * The fallen list is game state: it is saved per save block in the save mark
 * sidecar (reforged_savemark.c, <sav>.reforged) when the game writes a game or
 * suspend save, and comes back when that block is loaded, so a suspended
 * chapter keeps it. The .sav itself is unchanged.
 *
 * FE8PC_RFO_TEST_FALL=<character id> (a test switch, like FE8PC_RFO_CHECK):
 * on the first suspend resume of the run, that blue unit falls through
 * KillUnitOnCombatDeath (HP 0, killer: the first red unit), with or without
 * the option, so a scripted run can show the retail death and the casual
 * retreat from one save. Unset: nothing.
 */

#if PORT_PLAYED_BUILD

#include <stdio.h>
#include <stdlib.h>

#include "bmunit.h"
#include "bmmind.h"
#include "chapterdata.h"
#include "eventinfo.h"
#include "constants/characters.h"
#include "constants/classes.h"
#include "constants/event-flags.h"

#include "port_hw.h"
#include "port_reforged.h"

#define CASUAL_MAX_FALLEN 64

static u8 sFallen[CASUAL_MAX_FALLEN]; /* character ids, in the order they fell */
static u8 sFallenCount;
static u8 sTestFallDone;

static const char *UnitName(struct Unit *unit)
{
    static char buf[16];

    snprintf(buf, sizeof(buf), "pid 0x%02X", unit->pCharacterData->number);
    return buf;
}

/* A unit whose fall the original game treats as more than a death: the lords,
   the chapter's protected character, a game over. */
static int IsExempt(struct Unit *unit)
{
    int pid = unit->pCharacterData->number;
    const struct ROMChapterData *chapter;
    struct DefeatTalkEnt *ent;

    if (pid == CHARACTER_EIRIKA || pid == CHARACTER_EPHRAIM)
        return 1;
    chapter = GetROMChapterStruct(gPlaySt.chapterIndex);
    if (chapter != NULL && chapter->protectCharacterIndex == pid)
        return 1;
    if (CheckFlag(EVFLAG_GAMEOVER))
        return 1;
    ent = GetDefeatTalkEntry(pid);
    if (ent != NULL && ent->flag == EVFLAG_GAMEOVER)
        return 1;
    return 0;
}

int Reforged_CasualRetreat(struct Unit *unit)
{
    int pid, i;

    if (!Reforged_GetOption(RFO_CASUAL_MODE))
        return 0;
    if (unit == NULL || unit->pCharacterData == NULL || UNIT_FACTION(unit) != FACTION_BLUE)
        return 0;
    if (UNIT_IS_PHANTOM(unit) || IsExempt(unit))
        return 0;

    /* UnitKill's blue branch without InitUnitsupports */
    unit->state |= US_DEAD | US_HIDDEN;

    pid = unit->pCharacterData->number;
    for (i = 0; i < sFallenCount && sFallen[i] != pid; i++)
        ;
    if (i == sFallenCount && sFallenCount < CASUAL_MAX_FALLEN)
        sFallen[sFallenCount++] = (u8)pid;
    PortLog("[reforged] casual mode: %s retreats (chapter %d, turn %d); it returns next chapter\n", UnitName(unit),
        gPlaySt.chapterIndex, gPlaySt.chapterTurnNumber);
    return 1;
}

void Reforged_CasualReviveFallen(void)
{
    int i, j;

    for (i = 0; i < sFallenCount; i++)
    {
        for (j = 1; j < 0x40; j++)
        {
            struct Unit *unit = GetUnit(j);

            if (unit == NULL || unit->pCharacterData == NULL || unit->pCharacterData->number != sFallen[i])
                continue;
            if (unit->state & US_DEAD)
            {
                unit->state &= ~US_DEAD;
                PortLog("[reforged] casual mode: %s returns\n", UnitName(unit));
            }
            break;
        }
    }
    sFallenCount = 0;
}

int Reforged_CasualGetFallen(u8 *out, int max)
{
    int i;

    for (i = 0; i < sFallenCount && i < max; i++)
        out[i] = sFallen[i];
    return i;
}

void Reforged_CasualSetFallen(const u8 *pids, int count)
{
    int i;

    sFallenCount = 0;
    for (i = 0; i < count && sFallenCount < CASUAL_MAX_FALLEN; i++)
        if (pids[i] != 0)
            sFallen[sFallenCount++] = pids[i];
}

void Reforged_CasualTestFall(void)
{
    const char *env = getenv("FE8PC_RFO_TEST_FALL");
    struct Unit *unit = NULL, *killer = NULL;
    int pid, i;

    if (env == NULL || *env == 0 || sTestFallDone)
        return;
    sTestFallDone = 1;
    pid = (int)strtol(env, NULL, 0);

    for (i = 1; i < 0x40; i++)
    {
        struct Unit *u = GetUnit(i);

        if (u != NULL && u->pCharacterData != NULL && u->pCharacterData->number == pid && !(u->state & US_DEAD))
        {
            unit = u;
            break;
        }
    }
    for (i = 0x81; i < 0xC0; i++)
    {
        struct Unit *u = GetUnit(i);

        if (u != NULL && u->pCharacterData != NULL && !(u->state & US_DEAD))
        {
            killer = u;
            break;
        }
    }
    if (unit == NULL)
    {
        PortLog("[reforged] FE8PC_RFO_TEST_FALL=%s: no such living blue unit: nothing done\n", env);
        return;
    }
    if (killer == NULL) /* no red unit left: the first other living blue unit stands in */
        for (i = 1; i < 0x40 && killer == NULL; i++)
        {
            struct Unit *u = GetUnit(i);

            if (u != NULL && u != unit && u->pCharacterData != NULL && !(u->state & US_DEAD))
                killer = u;
        }
    if (killer == NULL)
    {
        PortLog("[reforged] FE8PC_RFO_TEST_FALL=%s: no other living unit to be the killer: nothing done\n", env);
        return;
    }
    PortLog("[reforged] FE8PC_RFO_TEST_FALL: %s falls (HP %d -> 0) to pid 0x%02X; supports %d %d %d %d %d %d %d\n",
        UnitName(unit), unit->curHP, killer->pCharacterData->number, unit->supports[0], unit->supports[1],
        unit->supports[2], unit->supports[3], unit->supports[4], unit->supports[5], unit->supports[6]);
    unit->curHP = 0;
    KillUnitOnCombatDeath(unit, killer);
    PortLog("[reforged] FE8PC_RFO_TEST_FALL: after the fall: state 0x%08lX, supports %d %d %d %d %d %d %d\n",
        (unsigned long)unit->state, unit->supports[0], unit->supports[1], unit->supports[2], unit->supports[3],
        unit->supports[4], unit->supports[5], unit->supports[6]);
}

#endif /* PORT_PLAYED_BUILD */
