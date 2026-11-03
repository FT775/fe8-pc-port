#include "global.h"

/*
 * Reforged Options randomizer: "Nuzlocke" (RFO_RANDO_NUZLOCKE; port/reforged/
 * reforged_rando.h has the map of the randomizer's files). Played builds only; empty
 * in every test build.
 *
 * The rules a run with it keeps (on top of the game's own permadeath):
 * - Casual mode's retreat is off for the run (reforged_casual.c asks
 *   Reforged_RandoNuzlocke), whatever the options screen says: a fallen unit dies.
 * - A death is final through any reset. The moment a blue unit dies in a battle (the
 *   map, the enemy phase, the arena) or to a hazard (casual mode's two hooks, and
 *   src/bmmind.c KillUnitOnArenaDeathMaybe: RANDO_HOOK_DEATH), its character id goes
 *   in the run's graveyard, which the save mark (reforged_savemark.c, the .sav's
 *   sidecar) writes at once, before the game can write a save. Whenever a save block
 *   of the run is loaded (a suspend, "Restart chapter", a chapter save), every unit in
 *   the graveyard that is alive in it dies again (UnitKill, as the game's own death),
 *   so neither a reset nor an older save brings it back; and a unit that joins again
 *   (an older save from before it joined: loaded blue, src/bmunit.c LoadUnit's hook,
 *   or turned blue, UnitChangeFaction: RANDO_HOOK_JOINED) dies as it joins. A carried
 *   unit is put down first, a carrier freed (the game's death paths do the same).
 *
 * The run is told apart by the seed and the save's playthrough id (gPlaySt), and a
 * new game starts with an empty graveyard. The phantoms, and non-blue units, are never
 * recorded. Units that die in events (scripted deaths) are dead in the saves already.
 */

#if PORT_PLAYED_BUILD

#include "bmunit.h"
#include "chapterdata.h"
#include "eventinfo.h"
#include "constants/characters.h"
#include "constants/classes.h"
#include "constants/event-flags.h"

#include "port_hw.h"
#include "port_reforged.h"
#include "reforged_rando.h"

int Reforged_RandoNuzlocke(void)
{
    return gRandoRunOn && Rando_Opt(RFO_RANDO_NUZLOCKE) != 0;
}

/* A death that ends the game (the lords, the chapter's protected character, a defeat
   quote that sets the game-over flag): the game over is the end of that attempt, so
   it is not recorded (casual mode's rule, reforged_casual.c). */
static int EndsTheGame(struct Unit *unit)
{
    int pid = unit->pCharacterData->number;
    const struct ROMChapterData *chapter = GetROMChapterStruct(gPlaySt.chapterIndex);
    struct DefeatTalkEnt *ent;

    if (pid == CHARACTER_EIRIKA || pid == CHARACTER_EPHRAIM || CheckFlag(EVFLAG_GAMEOVER))
        return 1;
    if (chapter != NULL && chapter->protectCharacterIndex == pid)
        return 1;
    ent = GetDefeatTalkEntry(pid);
    return ent != NULL && ent->flag == EVFLAG_GAMEOVER;
}

void Reforged_RandoUnitDied(struct Unit *unit)
{
    int pid;

    if (!Reforged_RandoNuzlocke() || unit == NULL || unit->pCharacterData == NULL ||
        UNIT_FACTION(unit) != FACTION_BLUE || UNIT_IS_PHANTOM(unit) || EndsTheGame(unit))
        return;
    pid = unit->pCharacterData->number;
    PortLog("[rando] nuzlocke: pid 0x%02X died (chapter %d, turn %d): final\n", pid, gPlaySt.chapterIndex,
        gPlaySt.chapterTurnNumber);
    Reforged_GraveAdd(gRandoRun.seed, gPlaySt.playthroughIdentifier, pid);
}

static int InGraveyard(int pid)
{
    u8 dead[64];
    int n, i;

    n = Reforged_GraveGet(gRandoRun.seed, gPlaySt.playthroughIdentifier, dead, sizeof(dead));
    for (i = 0; i < n; i++)
        if (dead[i] == pid)
            return 1;
    return 0;
}

/* The game's own death, with the rescue links undone first (the game's death paths
   drop a carried unit, DropRescueOnDeath): a unit it carries is put down on its
   square, a unit carrying it is freed. */
static void KillAgain(struct Unit *unit, const char *where)
{
    struct Unit *other = unit->rescue ? GetUnit(unit->rescue) : NULL;

    if (other != NULL && other->pCharacterData != NULL)
    {
        if (unit->state & US_RESCUING)
        {
            other->state &= ~(US_RESCUED | US_HIDDEN | US_UNSELECTABLE);
            other->xPos = unit->xPos;
            other->yPos = unit->yPos;
        }
        else
            other->state &= ~US_RESCUING;
        other->rescue = 0;
    }
    unit->state &= ~(US_RESCUING | US_RESCUED);
    unit->rescue = 0;
    UnitKill(unit);
    PortLog("[rando] nuzlocke: pid 0x%02X is in the graveyard: dead again (%s)\n", unit->pCharacterData->number,
        where);
}

void Reforged_RandoBlockLoaded(void)
{
    int j;

    if (!Reforged_RandoNuzlocke())
        return;
    for (j = 1; j < 0x40; j++)
    {
        struct Unit *unit = GetUnit(j);

        if (unit != NULL && unit->pCharacterData != NULL && !(unit->state & US_DEAD) && !UNIT_IS_PHANTOM(unit) &&
            InGraveyard(unit->pCharacterData->number))
            KillAgain(unit, "the save loaded");
    }
}

/* A unit that joins the army (loaded blue, or changed to blue: a talk, an event) when
   its character is in the graveyard: an older save from before it joined. */
void Reforged_RandoUnitJoined(struct Unit *unit)
{
    if (!Reforged_RandoNuzlocke() || unit == NULL || unit->pCharacterData == NULL ||
        UNIT_FACTION(unit) != FACTION_BLUE || (unit->state & US_DEAD) || UNIT_IS_PHANTOM(unit))
        return;
    if (InGraveyard(unit->pCharacterData->number))
        KillAgain(unit, "it joins again");
}

/* A new game: its graveyard starts empty (reforged_rando.c Reforged_RandoNewGame). */
void RandoNuzlocke_NewGame(void)
{
    if (Reforged_RandoNuzlocke())
        Reforged_GraveClear(gRandoRun.seed, gPlaySt.playthroughIdentifier);
}

#endif /* PORT_PLAYED_BUILD */
