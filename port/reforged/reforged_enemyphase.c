#include "global.h"

/*
 * Reforged Options: "Enemy phase" (RFO_ENEMY_SPEED, the user's request of 2026-11-06:
 * "pressing start to skip the entire enemy phase plus making their turns/text pretty
 * fast by default"). Display-only, like fast-forward (reforged_fastforward.c), whose
 * clock it drives: the game runs the same frame steps with the same keys, only faster.
 *
 *   - While the enemy (or NPC) phase runs on the battle map, the game goes at the
 *     option's speed: Normal, 2x or 4x (the default). Its sound follows "Fast-forward
 *     sound" (muted, or sped up at 2x-4x).
 *   - Start pressed during that phase skips the rest of it: 8x until the player phase.
 *     No key is pressed for the player: a battle quote still waits for A, and the game
 *     reads the same keys as without the option (the save checks replay routes that
 *     press Start in enemy phases). Start also does what it does in the original there
 *     (it hurries the AI's target cursor).
 *
 * An enemy turn is running while its AI phase proc (bm.c BmMain_StartPhase:
 * gProcScr_CpPhase, for the red and the green side) exists; a chapter's opening scene,
 * which also runs before the first player phase, is not one. A skip lasts until the
 * player phase (gPlaySt.faction back to blue), the phase changes and their events
 * included. Played window builds only (fast-forward's platform).
 */

#if PORT_PLAYED_BUILD

#include "proc.h"
#include "hardware.h"
#include "bmunit.h"
#include "cp_common.h"
#include "event.h"

#include "port_hw.h"
#include "port_reforged.h"

extern struct ProcCmd CONST_DATA gProc_BMapMain[];

#define SKIP_SPEED 8

static int sSkip;
static unsigned sSkipped;
static u8 sSkipChapter; /* a chapter change (MNC3 on an enemy turn) ends the skip */

static int OnMap(void)
{
    return Proc_Find(gProc_BMapMain) != NULL;
}

static int EnemyTurnRunning(void)
{
    return gPlaySt.faction != FACTION_BLUE && OnMap() && Proc_Find(gProcScr_CpPhase) != NULL;
}

int Reforged_EnemyPhaseSpeed(void)
{
    int n;

    if (sSkip && (gPlaySt.faction == FACTION_BLUE || !OnMap() || gPlaySt.chapterIndex != sSkipChapter))
    {
        PortLog("[reforged] enemy phase skipped (%u frame steps)\n", sSkipped);
        sSkip = 0;
    }
    /* Story and event scenes run at the normal speed, with sound, a skip or not: a chapter's
       end or a defeat on an enemy turn (cp_perform.c RunPotentialWaitEvents), death and boss
       quotes, the game over, and the scenes between two phases (no AI phase proc then: the
       NPC phase's turn events). A skip goes on after them. */
    if (EventEngineExists() || !EnemyTurnRunning())
        return 1;
    if (sSkip)
    {
        sSkipped++;
        return SKIP_SPEED;
    }
    if (gKeyStatusPtr->newKeys & START_BUTTON)
    {
        sSkip = 1;
        sSkipped = 0;
        sSkipChapter = gPlaySt.chapterIndex;
        return SKIP_SPEED;
    }
    n = Reforged_GetOption(RFO_ENEMY_SPEED);
    return n == 2 || n == 4 ? n : 1;
}


#endif /* PORT_PLAYED_BUILD */
