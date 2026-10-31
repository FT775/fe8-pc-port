#include "global.h"
#include "types.h"
#include "bmunit.h"
#include "bmphase.h"
#include "bm.h"
#include "prepscreen.h"
#include "variables.h"
#include "functions.h"

int GetLastStatScreenUid(void);
void ProcFun_ResetCursorPosition(ProcPtr proc);
void ADJUSTFROMXI_MoveCameraOnSomeUnit(ProcPtr proc);

struct ProcCmd CONST_DATA gProcScr_ResetCursorPosition[] = {
    PROC_CALL(ProcFun_ResetCursorPosition),
    PROC_YIELD,
    PROC_END
};

struct ProcCmd CONST_DATA gProcScr_ADJUSTSFROMXI[] = {
    PROC_NAME("ADJUSTSFROMXI"),
    PROC_CALL(ADJUSTFROMXI_MoveCameraOnSomeUnit),
    PROC_SLEEP(1),
    PROC_END
};


void GetPlayerStartCursorPosition(int *px, int *py)
{
    struct Unit *unit;
    if (1 == gPlaySt.chapterTurnNumber) {
        unit = GetUnitFromCharId(GetPlayerLeaderPid());
#ifdef PC_PORT
        /*
         * D-51 (port-plan/DIVERGENCES.md): after a debug-menu jump to an
         * Ephraim chapter the leader is not in the army, so GetUnitFromCharId
         * gives NULL and the GBA reads xPos and yPos from the BIOS latch
         * (lanes 0 and 1). PORT_NULL_READ takes the rule's latch: 04 20
         * post-SWI. In Ephraim Ch21's jump the GBA had three lag VBlanks after
         * the last SWI and read 02 C0 (post-IRQ): an open latch choice, since
         * the PC does not model lag (DIVERGENCES, "Open-bus latch rule").
         */
        gPlaySt.xCursor = PORT_NULL_READ(unit, xPos);
        gPlaySt.yCursor = PORT_NULL_READ(unit, yPos);
#else
        gPlaySt.xCursor = unit->xPos;
        gPlaySt.yCursor = unit->yPos;
#endif
    }

    if (1 != gPlaySt.config.autoCursor) {
        unit = GetUnitFromCharId(GetPlayerLeaderPid());
#ifdef PC_PORT
        *px = PORT_NULL_READ(unit, xPos);
        *py = PORT_NULL_READ(unit, yPos);
#else
        *px = unit->xPos;
        *py = unit->yPos;
#endif
    } else {
        *px = gPlaySt.xCursor;
        *py = gPlaySt.yCursor;
    }
}

void GetEnemyStartCursorPosition(int *px, int *py)
{
    int i;

    for (i = gPlaySt.faction + 1; i < gPlaySt.faction + 0x40; i++) {
        struct Unit *unit = GetUnit(i);
        if (!UNIT_IS_VALID(unit))
            continue;
        
        if (unit->state & (US_HIDDEN | US_BIT9))
            continue;
        
        *px = unit->xPos;
        *py = unit->yPos;

        if (CA_BOSS & UNIT_CATTRIBUTES(unit))
            break;
    }
}

void ProcFun_ResetCursorPosition(ProcPtr proc)
{
    int x, y;
    
    x = -1;
    y = -1;

    if (0 == GetPhaseAbleUnitCount(gPlaySt.faction)) {
        Proc_End(proc);
        return;
    }

    switch (gPlaySt.faction) {
    case FACTION_BLUE:
        GetPlayerStartCursorPosition(&x, &y);
        break;
    
    case FACTION_GREEN:
    case FACTION_RED:
        GetEnemyStartCursorPosition(&x, &y);
        break;
    
    default:
        break;
    }

    if ((x >= 0) && (y >= 0)) {
        EnsureCameraOntoPosition(proc, x, y);
        SetCursorMapPosition(x, y);
    }
}

void ADJUSTFROMXI_MoveCameraOnSomeUnit(ProcPtr proc)
{
    int x, y;
    struct Unit *unit = GetUnit(GetLastStatScreenUid());

    if (NULL == unit)
        return;

    x = unit->xPos;
    y = unit->yPos;
    EnsureCameraOntoPosition(proc, x, y);
    SetCursorMapPosition(x, y);
}
