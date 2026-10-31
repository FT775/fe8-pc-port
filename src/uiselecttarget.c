#include "global.h"

#include "functions.h"
#include "m4a.h"
#include "hardware.h"
#include "soundwrapper.h"
#include "uiutils.h"
#include "bmio.h"
#include "face.h"
#include "bm.h"

#include "uiselecttarget.h"
#include "constants/songs.h"

#if defined(PC_PORT) && PORT_PLAYED_BUILD
#include "port_reforged.h" /* PC played builds: Reforged Options, danger zone on Select */
#endif

static struct Vec2 EWRAM_DATA sSelectTargetRoot = {};

static struct SelectTarget EWRAM_DATA sSelectTargetList[MAX_TARGET_LIST_COUNT] = {0};

static int EWRAM_DATA sSelectTargetCount = 0;

struct ProcCmd CONST_DATA gProcScr_TargetSelection[] =
{
PROC_LABEL(0),
    PROC_REPEAT(TargetSelection_Loop),
    PROC_SLEEP(1),

    PROC_CALL(RefreshBMapGraphics),
    PROC_GOTO(0),

    PROC_END,
};

struct Unk_085B658C
{
    s8 x, y;
#ifdef PC_PORT
} AGBCC_STRUCT;
#else
};
#endif

struct Unk_085B658C CONST_DATA gNearTargetLinkOrder[] =
{
    {  0,  0 },
    {  0, -2 },
    {  0, -1 },
    { +1, -1 },
    { +1,  0 },
    { +2,  0 },
    { +1, +1 },
    {  0, +1 },
    {  0, +2 },
    { -1, +1 },
    { -1,  0 },
    { -2,  0 },
    { -1, -1 },
};

void InitTargets(int xRoot, int yRoot) {
    sSelectTargetRoot.x = xRoot;
    sSelectTargetRoot.y = yRoot;
    sSelectTargetCount = 0;
}

void AddTarget(int x, int y, int unitId, int tId) {
    sSelectTargetList[sSelectTargetCount].x = x;
    sSelectTargetList[sSelectTargetCount].y = y;
    sSelectTargetList[sSelectTargetCount].uid = unitId;
    sSelectTargetList[sSelectTargetCount].extra = tId;

    sSelectTargetCount++;
}

inline int GetSelectTargetCount(void)
{
    return sSelectTargetCount;
}

inline struct SelectTarget* GetTarget(int index)
{
    return &sSelectTargetList[index];
}

void LinkTargets(void)
{
    int i, last;

    for (i = 0; i < GetSelectTargetCount(); i++)
    {
        GetTarget(i)->prev = GetTarget(i - 1);
        GetTarget(i)->next = GetTarget(i + 1);
    }

    last = GetSelectTargetCount() - 1;

    GetTarget(0)->prev = GetTarget(last);
    GetTarget(last)->next = GetTarget(0);
}

void TargetSelection_GetRealCursorPosition(struct SelectTargetProc* proc, int* xPos, int* yPos) {
    *xPos = proc->currentTarget->x * 16;
    *yPos = proc->currentTarget->y * 16;
}

void TargetSelection_Loop(struct SelectTargetProc* proc) {
    int x, y;
    int r5;

    if ((TARGETSELECTION_FLAG_FROZEN & proc->flags) != 0) {
        TargetSelection_GetRealCursorPosition(proc, &x, &y);
        PutMapCursor(x, y, 4);
        return;
    }

#if defined(PC_PORT) && PORT_PLAYED_BUILD
    /* PC played builds only, Reforged Options "Danger zone on Select" (off by
       default; port/reforged/reforged_dangerzone.c): Select shows or hides the
       enemies' danger zone; A, B, R or L first restore the screen exactly. */
    Reforged_DangerZoneTargetSelect();
#endif

    TargetSelection_HandleMoveInput(proc);

    r5 = TargetSelection_HandleSelectInput(proc);

    if ((TARGETSELECTION_ACTION_END & r5) != 0) {
        EndTargetSelection(proc);
    }

    if ((TARGETSELECTION_ACTION_SE_6A & r5) != 0) {
        PlaySoundEffect(SONG_SE_SYS_WINDOW_SELECT1);
    }

    if ((TARGETSELECTION_ACTION_SE_6B & r5) != 0) {
        PlaySoundEffect(SONG_SE_SYS_WINDOW_CANSEL1);
    }

    if ((TARGETSELECTION_ACTION_CLEARBGS & r5) != 0) {
        ClearBg0Bg1();
    }

    if ((TARGETSELECTION_ACTION_ENDFACE & r5) != 0) {
        EndFaceById(0);
    }

    if ((TARGETSELECTION_ACTION_ENDFAST & r5) == 0) {
        TargetSelection_GetRealCursorPosition(proc, &x, &y);
        if (EnsureCameraOntoPosition(proc, x >> 4, y >> 4) != 1) {
            PutMapCursor(x, y, 2);
        }
    }

    return;
}

ProcPtr NewTargetSelection(const struct SelectInfo* selectInfo) {
    struct SelectTargetProc* proc;

    LockGame();
    proc = Proc_Start(gProcScr_TargetSelection, PROC_TREE_3);
#if defined(PC_PORT) && PORT_PLAYED_BUILD
    Reforged_DangerZoneReset(); /* PC played builds: the target display is shown */
#endif

    proc->flags = TARGETSELECTION_FLAG_GAMELOCK;
    proc->selectRoutines = selectInfo;
    proc->currentTarget = GetLinkedTargets();
    proc->onAPress = 0;

    if (proc->selectRoutines->onInit) {
        proc->selectRoutines->onInit(proc);
    }

    if (proc->selectRoutines->onUnk08) {
        proc->selectRoutines->onUnk08(proc);
    }

    if (proc->selectRoutines->onSwitchIn) {
        proc->selectRoutines->onSwitchIn(proc, proc->currentTarget);
    }

    gKeyStatusPtr->newKeys = 0;

    return proc;
}

ProcPtr NewTargetSelection_Specialized(const struct SelectInfo* selectInfo, u8(*onSelect)(ProcPtr, struct SelectTarget*)) {
    struct SelectTargetProc* proc = NewTargetSelection(selectInfo);

    proc->onAPress = onSelect;

#ifdef PC_PORT
    // Retail has no return statement. agbcc stores onSelect through r1
    // (adds r1, r0, #0; str r4, [r1, #0x38] at 0x0804FAAC) and leaves
    // NewTargetSelection's result in r0, so the GBA returns proc. The callers
    // (src/bmitemuse.c) pass it to StartSubtitleHelp as the parent proc.
    // See port-plan/DIVERGENCES.md D-31.
    return proc;
#else
#if 0
    return proc; // BUG
#endif
#endif
}

ProcPtr EndTargetSelection(struct SelectTargetProc* proc) {
    if (proc->selectRoutines->onEnd) {
        proc->selectRoutines->onEnd(proc);
    }

    if ((TARGETSELECTION_FLAG_GAMELOCK & proc->flags) != 0) {
        UnlockGame();
    }

    Proc_End(proc);

    return proc->proc_parent;
}

void TargetSelection_HandleMoveInput(struct SelectTargetProc* proc) {
    struct SelectTarget* current = proc->currentTarget;

    if ((DPAD_LEFT | DPAD_UP) & gKeyStatusPtr->repeatedKeys) {
        if (current->next != 0) {
            proc->currentTarget = current->next;
        }
    }

    if ((DPAD_RIGHT | DPAD_DOWN) & gKeyStatusPtr->repeatedKeys) {
        if (proc->currentTarget->prev) {
            proc->currentTarget = proc->currentTarget->prev;
        }
    }

    if (proc->currentTarget == current) {
        return;
    }

    if (proc->selectRoutines->onSwitchOut) {
        proc->selectRoutines->onSwitchOut(proc, current);
    }

    if (proc->selectRoutines->onSwitchIn) {
        proc->selectRoutines->onSwitchIn(proc, proc->currentTarget);
    }

    PlaySoundEffect(SONG_SE_SYS_CURSOR_LR1);
}

int TargetSelection_HandleSelectInput(struct SelectTargetProc* proc) {
    int ret = 0;

    if (A_BUTTON & gKeyStatusPtr->newKeys) {
        if (proc->onAPress) {
            ret = proc->onAPress(proc, proc->currentTarget);
        } else {
            if (proc->selectRoutines->onSelect) {
                ret = proc->selectRoutines->onSelect(proc, proc->currentTarget);
            }
        }
    } else if (B_BUTTON & gKeyStatusPtr->newKeys) {
        if (proc->selectRoutines->onCancel) {
            ret = proc->selectRoutines->onCancel(proc, proc->currentTarget);
        }
    } else if (R_BUTTON & gKeyStatusPtr->newKeys) {
        if (proc->selectRoutines->onHelp) {
            ret = proc->selectRoutines->onHelp(proc, proc->currentTarget);
        }
    }

    return ret;
}

void FreezeTargetSelection(void) {
    struct SelectTargetProc * proc = Proc_Find(gProcScr_TargetSelection);
    if (proc) {
        proc->flags |= TARGETSELECTION_FLAG_FROZEN;
    }
}

void ResumeTargetSelection(void) {
    struct SelectTargetProc * proc = Proc_Find(gProcScr_TargetSelection);
    if (proc) {
        proc->flags &= ~TARGETSELECTION_FLAG_FROZEN;
    }
}

int GetFurthestTargetDistance(void)
{
    int i, result = 0;
    struct SelectTarget * it = sSelectTargetList;

    for (i = 0; i < GetSelectTargetCount(); i++, it++)
    {
        // RECT_DISTANCE
        int distance = ABS(sSelectTargetRoot.x - it->x) + ABS(sSelectTargetRoot.y - it->y);

        if (result < distance)
            result = distance;
    }

    return result;
}

struct SelectTarget* GetLinkedTargetsNear(void) {
    int i, j;

    struct SelectTarget * first = NULL;
    struct SelectTarget * last = NULL;

    for (i = 0; i < (int) ARRAY_COUNT(gNearTargetLinkOrder); i++)
    {
        struct SelectTarget * it;

        int x = sSelectTargetRoot.x + gNearTargetLinkOrder[i].x;
        int y = sSelectTargetRoot.y + gNearTargetLinkOrder[i].y;

        for (j = 0, it = sSelectTargetList; j < sSelectTargetCount; j++, it++)
        {
            if (x == it->x && y == it->y)
            {
                it->next = last;

                if (last != NULL)
                    last->prev = it;

                if (first == NULL)
                    first = it;

                last = it;
            }
        }
    }

    first->next = last;
    last->prev = first;

    return first;
}

struct SelectTarget* GetLinkedTargetsFar(void) {
    LinkTargets();
    return sSelectTargetList;
}

struct SelectTarget* GetLinkedTargets(void) {
    if (GetFurthestTargetDistance() > 2) {
        return GetLinkedTargetsFar();
    }
    return GetLinkedTargetsNear();
}
