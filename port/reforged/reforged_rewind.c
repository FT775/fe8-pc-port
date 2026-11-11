#include "global.h"

/*
 * Reforged Options: "Rewind" (RFO_REWIND, Helpers; the user's request of 2026-11-06:
 * "using backspace to utilize a turn-back that uses holding down the backspace
 * button"). Off by default; it changes play, so a save made with it on is marked.
 *
 * The game itself writes its suspend save just before every phase (bm.c
 * BmMain_SuspendBeforePhase), so at the start of each player phase the SRAM holds that
 * turn's beginning. Right after that write this keeps a copy of what the suspend save owns:
 * its two SRAM blocks (it alternates between them), their block infos, the header's last
 * suspend slot, and the save mark's suspend entries (reforged_savemark.c: checksum,
 * randomizer run, Casual mode's fallen units), up to REWIND_MAX turns of one visit to
 * a chapter. The game slots, the arena and the rest of the SRAM are never touched (a
 * save made meanwhile stays). The kept turns are forgotten when a game or a suspend is
 * read other than by a rewind (title, reset, another file), and when a turn begins that
 * is not later than the last kept one, or in another save file (a new visit).
 *
 * Holding the Rewind key (Controls; Backspace by default) for REWIND_HOLD frames on the
 * player phase's free map (playerphase.c MainIdle: no menu, no unit picked) puts a copy
 * back and resumes it through the game's own path (as its debug menu's "Continue
 * chapter" does: EndBMapMain, ReadSuspendSave, RestartGameAndGoto8). The copy is the
 * start of this turn, or, when nothing was done since then (the SRAM is still that
 * copy), the turn before; held again, one turn further back. Not with Nuzlocke (its
 * deaths stay), not in a tutorial chapter (the game writes no suspend there).
 *
 * FE8PC_REWIND_AT=<frame> (a test switch, DIVERGENCES PB-9): acts as if the key were held
 * from that frame for REWIND_HOLD + 600 frames.
 */

#if PORT_PLAYED_BUILD

#include <stdlib.h>
#include <string.h>

#include "proc.h"
#include "player_interface.h"
#include "bmio.h"
#include "bmsave.h"
#include "bmunit.h"
#include "gamecontrol.h"
#include "soundwrapper.h"
#include "constants/songs.h"

#include "port_hw.h"
#include "port_reforged.h"

#define REWIND_MAX   8
#define REWIND_HOLD  45 /* frame steps: three quarters of a second */

/* The suspend save's part of the SRAM. */
struct SuspendPart {
    struct SuspendSaveBlock blocks[2];
    struct SaveBlockInfo info[2]; /* SAVE_ID_SUSPEND, SAVE_ID_SUSPEND_ALT */
    u8 lastSlot;                  /* GlobalSaveInfo.last_suspend_slot */
};

struct RewindPoint {
    int valid;
    u8 chapter;
    u8 slot;    /* gPlaySt.gameSaveSlot: the save file */
    u16 turn;
    struct SuspendPart part;
    u8 marks[REFORGED_SAVEMARK_STATE_MAX];
};

static struct RewindPoint sPoints[REWIND_MAX];
static int sCount;                /* points held, oldest first */
static unsigned sHeldFrames;
static int sWaitRelease;          /* after a rewind, the key must be let go */
static int sPending;              /* the restart is under way (sProcScr_Rewind) */

static void ReadPart(struct SuspendPart *out)
{
    struct GlobalSaveInfo gi;

    memset(out, 0, sizeof(*out));
    memcpy(out->blocks, gSram->suspendSaveBlocks, sizeof(out->blocks));
    memcpy(out->info, &gSram->saveBlockInfo[SAVE_ID_SUSPEND], sizeof(out->info));
    out->lastSlot = ReadGlobalSaveInfo(&gi) ? gi.last_suspend_slot : 0;
}

static void WritePart(const struct SuspendPart *in)
{
    struct GlobalSaveInfo gi;

    WriteAndVerifySramFast(in->blocks, gSram->suspendSaveBlocks, sizeof(in->blocks));
    WriteAndVerifySramFast(in->info, &gSram->saveBlockInfo[SAVE_ID_SUSPEND], sizeof(in->info));
    if (ReadGlobalSaveInfo(&gi))
    {
        gi.last_suspend_slot = in->lastSlot;
        WriteGlobalSaveInfo(&gi); /* its checksum too */
    }
}

static int SameAsNow(const struct RewindPoint *p)
{
    static struct SuspendPart now;

    ReadPart(&now);
    return memcmp(&now, &p->part, sizeof(now)) == 0;
}

/* bmsave.c via the save mark (Reforged_SaveBlockRead): a game or a suspend was read. Unless
   it is a rewind's own restart, the kept turns belong to something else now. */
void Reforged_RewindForget(void)
{
    if (!sPending)
        sCount = 0;
}

/* bm.c, right after the phase change's suspend save: a player phase begins. */
void Reforged_RewindSnapshot(void)
{
    struct RewindPoint *p;

    if (gPlaySt.faction != FACTION_BLUE || (gPlaySt.chapterStateBits & PLAY_FLAG_TUTORIAL))
        return;
    if (sCount > 0 && SameAsNow(&sPoints[sCount - 1]))
        return; /* the same turn again (a rewind resumes at the phase change) */
    if (sCount > 0 && (sPoints[sCount - 1].chapter != gPlaySt.chapterIndex ||
                       sPoints[sCount - 1].slot != gPlaySt.gameSaveSlot ||
                       gPlaySt.chapterTurnNumber <= sPoints[sCount - 1].turn))
        sCount = 0; /* another chapter, file or visit: the old turns go */
    if (sCount == REWIND_MAX)
    {
        memmove(&sPoints[0], &sPoints[1], sizeof(sPoints[0]) * (REWIND_MAX - 1));
        sCount--;
    }
    p = &sPoints[sCount++];
    p->valid = 1;
    p->chapter = gPlaySt.chapterIndex;
    p->slot = gPlaySt.gameSaveSlot;
    p->turn = gPlaySt.chapterTurnNumber;
    ReadPart(&p->part);
    Reforged_SaveMarkStateGet(p->marks);
}

/* The platform, once per frame step: is the Rewind key held. */
void Reforged_RewindKey(int held)
{
    static int atRead;
    static unsigned at;

    if (!atRead)
    {
        const char *s = getenv("FE8PC_REWIND_AT");

        atRead = 1;
        at = s != NULL ? (unsigned)strtoul(s, NULL, 0) : 0;
    }
    if (at != 0 && gPortFrameCount >= at && gPortFrameCount < at + REWIND_HOLD + 600)
        held = 1;
    if (!held)
    {
        sHeldFrames = 0;
        sWaitRelease = 0;
        return;
    }
    if (sHeldFrames < 0xFFFF)
        sHeldFrames++;
}

/* The restart, a frame after the free map asked for it, from a proc of its own (not a child of
   the map's, which it ends): as the game's debug menu "Continue chapter" does, with the
   suspend slot found first (IsValidSuspendSave sets the offset ReadSuspendSave reads). */
static void Rewind_Restart(ProcPtr proc)
{
    EndBMapMain();
    ReadSuspendSave(SAVE_ID_SUSPEND); /* sPending still set: the kept turns stay (Reforged_RewindForget) */
    sPending = 0;
    RestartGameAndGoto8();
}

static const struct ProcCmd sProcScr_Rewind[] = {
    PROC_NAME("RFO_REWIND"),
    PROC_SLEEP(1),
    PROC_CALL(Rewind_Restart),
    PROC_END,
};

/* playerphase.c MainIdle: 1 when a rewind was started (the idle loop stops there). */
int Reforged_RewindIdle(void *proc)
{
    int target;
    const struct RewindPoint *p;

    Reforged_PairTest(proc); /* FE8PC_PAIR_TEST (reforged_pairup.c), a test switch */
    if (sPending)
        return 1; /* the restart runs next frame: nothing else meanwhile */
    if (sHeldFrames < REWIND_HOLD || sWaitRelease)
        return 0;
    sWaitRelease = 1;
    if (!Reforged_GetOption(RFO_REWIND) || Reforged_RandoNuzlocke())
        return 0;
    if (sCount > 0 && (sPoints[sCount - 1].chapter != gPlaySt.chapterIndex ||
                       sPoints[sCount - 1].slot != gPlaySt.gameSaveSlot))
        sCount = 0;
    target = sCount - 1;
    if (target >= 0 && SameAsNow(&sPoints[target]))
        target--; /* nothing done since this turn began: the turn before */
    if (target < 0)
    {
        PlaySoundEffect(SONG_SE_SYS_WINDOW_CANSEL1);
        PortLog("[reforged] rewind: no earlier turn kept in this chapter\n");
        return 0;
    }
    p = &sPoints[target];
    sCount = target + 1; /* the turns after it are gone */
    PortLog("[reforged] rewind: chapter %d, back from turn %d to the start of turn %d\n", gPlaySt.chapterIndex,
        gPlaySt.chapterTurnNumber, p->turn);
    PlaySoundEffect(SONG_SE_SYS_WINDOW_SELECT1);
    WritePart(&p->part);
    Reforged_SaveMarkStateSetSuspend(p->marks);
    if (!IsValidSuspendSave(SAVE_ID_SUSPEND))
    {
        PortLog("[reforged] rewind: the kept suspend save does not read back: not rewound\n");
        return 0;
    }
    EndPlayerPhaseSideWindows();
    sPending = 1;
    Proc_Start(sProcScr_Rewind, PROC_TREE_3);
    return 1;
}

#endif /* PORT_PLAYED_BUILD */
