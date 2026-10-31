#include "global.h"

#include "worldmap.h"

struct AutoMuTarget
{
    /* 00 */ union
    {
        u8 node;
        struct Vec2 pos;
    } src;
    /* 04 */ union
    {
        u8 node;
        struct Vec2 pos;
    } dst;
#ifdef PC_PORT
} AGBCC_STRUCT;
#else
};
#endif

struct GMapAutoMuProc
{
    /* 00 */ PROC_HEADER;
    /* 29 */ u8 kind;
    /* 2A */ u8 flags;
    // 2B
    /* 2C */ u16 unitId;
    /* 2E */ u16 unk_2e;
    /* 30 */ struct AutoMuTarget target;
    /* 38 */ u8 unk_38;
    /* 39 */ u8 unk_39;
    /* 3A */ s16 speed;
    /* 3C */ s16 delay;
};

//! FE8U = 0x080C3124
void GmapAutoMu_OnEnd(struct GMapAutoMuProc * proc)
{
    if (GmMu_IsMoving(GM_MU, proc->unitId) != 0)
    {
        GmMu_FinishMovement(GM_MU, proc->unitId);
    }

    EndGmapUnitFade();

    switch (proc->kind)
    {
        case 0:
        default:
            gGMData.units[proc->unitId].location = proc->target.dst.node;
            break;

        case 1:
            GmMu_SetPosition(GM_MU, proc->unitId, proc->target.dst.pos.x, proc->target.dst.pos.y);
            break;
    }

    if ((proc->flags & 2) != 0)
    {
        HideGmUnit(proc->unitId);
    }

    return;
}

//! FE8U = 0x080C31A8
void GmapAutoMu_WaitInitialDelay(struct GMapAutoMuProc * proc)
{
    proc->delay--;

    if (proc->delay < 1)
    {
        Proc_Break(proc);
    }

    return;
}

//! FE8U = 0x080C31C4
void GmapAutoMu_80C31C4(struct GMapAutoMuProc * proc)
{
    if ((proc->flags & 1) != 0)
    {
        Proc_Goto(proc, 0);
    }

    return;
}

//! FE8U = 0x080C31E0
void GmapAutoMu_80C31E0(struct GMapAutoMuProc * proc)
{
    if ((proc->flags & 2) != 0)
    {
        Proc_Goto(proc, 4);
    }

    return;
}

//! FE8U = 0x080C31FC
void GmapAutoMu_80C31FC(struct GMapAutoMuProc * proc)
{
    switch (proc->kind)
    {
        case 0:
        default:
            Proc_Goto(proc, 1);
            break;

        case 1:
            Proc_Goto(proc, 2);
            break;
    }

    return;
}

//! FE8U = 0x080C3220
void GmapAutoMu_StartFadeIn(struct GMapAutoMuProc * proc)
{
    GmMu_StartFadeIn(GM_MU, proc->unitId, 30);
    ShowGmUnit(proc->unitId);

    return;
}

//! FE8U = 0x080C3244
void GmapAutoMu_StartFadeOut(struct GMapAutoMuProc * proc)
{
    GmMu_StartFadeOut(GM_MU, proc->unitId, 30);

    return;
}

//! FE8U = 0x080C3264
void GmapAutoMu_WaitForFadeEnd(struct GMapAutoMuProc * proc)
{
    if (!GmUnitFadeExists())
    {
        Proc_Break(proc);
    }

    return;
}

//! FE8U = 0x080C3280
void GmapAutoMu_80C3280(struct GMapAutoMuProc * proc)
{
    struct UnknownSub80BDEB4 input;

    input.unk_00 = proc->unitId;
    input.unk_01 = proc->unk_38;
    input.unk_06 = proc->target.src.node;
    input.unk_08 = proc->target.dst.node;
    input.unk_0c = proc->speed;
    input.unk_0a = 0;
    input.unk_02 = (proc->flags >> 2) & 1;
    input.unk_03 = 0xff;
    input.unk_04 = 4;

    GmMu_StartMoveBetweenNodes(GM_MU, &input);

    return;
}

//! FE8U = 0x080C32E4
void GmapAutoMu_80C32E4(struct GMapAutoMuProc * proc)
{
    struct UnknownSub80BDFA4 input;

    input.unk_00 = proc->unitId;
    input.unk_01 = proc->unk_39;
    input.unk_02 = proc->unk_38;
    input.unk_06 = proc->target.src.pos.x;
    input.unk_08 = proc->target.src.pos.y;
    input.unk_0a = proc->target.dst.pos.x;
    input.unk_0c = proc->target.dst.pos.y;
    input.unk_10 = proc->speed;
    input.unk_0e = 0;
    input.unk_03 = (proc->flags >> 2) & 1;
    input.unk_04 = 0xff;
    input.unk_05 = 4;

    GmMu_StartStraightMove(GM_MU, &input);

    return;
}

//! FE8U = 0x080C3350
void GmapAutoMu_80C3350(struct GMapAutoMuProc * proc)
{
    if (!GmMu_IsMoving(GM_MU, proc->unitId))
    {
        Proc_Break(proc);
    }

    return;
}

// clang-format off

struct ProcCmd CONST_DATA ProcScr_GmapAutoMu[] =
{
    PROC_NAME("Gmap Auto Mu"),
    PROC_MARK(PROC_MARK_WMSTUFF),

    PROC_SET_END_CB(GmapAutoMu_OnEnd),
    PROC_YIELD,

    PROC_REPEAT(GmapAutoMu_WaitInitialDelay),
    PROC_CALL(GmapAutoMu_80C31C4),

    PROC_CALL(GmapAutoMu_80C31FC),

PROC_LABEL(0),
    PROC_CALL(GmapAutoMu_StartFadeIn),
    PROC_REPEAT(GmapAutoMu_WaitForFadeEnd),

    PROC_CALL(GmapAutoMu_80C31FC),

    // fallthrough

PROC_LABEL(1),
    PROC_CALL(GmapAutoMu_80C3280),

    PROC_GOTO(3),

PROC_LABEL(2),
    PROC_CALL(GmapAutoMu_80C32E4),

    // fallthrough

PROC_LABEL(3),
    PROC_REPEAT(GmapAutoMu_80C3350),
    PROC_CALL(GmapAutoMu_80C31E0),

    PROC_GOTO(5),

PROC_LABEL(4),
    PROC_CALL(GmapAutoMu_StartFadeOut),
    PROC_REPEAT(GmapAutoMu_WaitForFadeEnd),

PROC_LABEL(5),
    PROC_END,
};

// clang-format on

//! FE8U = 0x080C3378
ProcPtr StartGmapAutoMu_Type0(struct Sub80C3378 * input, int flag, ProcPtr parent)
{
    struct GMapAutoMuProc * proc;

    if (parent != NULL)
    {
        proc = Proc_StartBlocking(ProcScr_GmapAutoMu, parent);
    }
    else
    {
        proc = Proc_Start(ProcScr_GmapAutoMu, PROC_TREE_3);
    }

    proc->kind = 0;
    proc->flags = flag;

    proc->unitId = input->wm_uid;
    proc->unk_38 = input->unk_01;

    proc->target.src.node = input->srcNode;
    proc->target.dst.node = input->dstNode;

    proc->delay = input->delay;
    proc->speed = input->speed;

    return proc;
}

//! FE8U = 0x080C33D4
ProcPtr StartGmapAutoMu_Type1(struct Sub80C33D4 * input, int flag, ProcPtr parent)
{
    struct GMapAutoMuProc * proc;

    if (parent != NULL)
    {
        proc = Proc_StartBlocking(ProcScr_GmapAutoMu, parent);
    }
    else
    {
        proc = Proc_Start(ProcScr_GmapAutoMu, PROC_TREE_3);
    }

    proc->kind = 1;
    proc->flags = flag;
    proc->unitId = input->wm_uid;
    proc->unk_39 = input->unk_01;
    proc->unk_38 = input->unk_02;

    proc->target.src.pos.x = input->x1;
    proc->target.src.pos.y = input->y1;
    proc->target.dst.pos.x = input->x2;
    proc->target.dst.pos.y = input->y2;

    proc->delay = input->delay;
    proc->speed = input->speed;

    return proc;
}

//! FE8U = 0x080C343C
void EndGmAutoMuFor(int index)
{
    struct GMapAutoMuProc * proc;
    struct ProcFindIterator procIter;

    if (index < 0)
    {
        Proc_EndEach(ProcScr_GmapAutoMu);
        return;
    }

    Proc_FindBegin(&procIter, ProcScr_GmapAutoMu);
    do
    {
        proc = Proc_FindNext(&procIter);

        if (proc->unitId == index)
        {
            Proc_End(proc);
            return;
        }
    } while (proc != NULL);

    return;
}

//! FE8U = 0x080C3484
bool IsGmAutoMuActiveFor(int index)
{
    struct GMapAutoMuProc * proc;
    struct ProcFindIterator procIter;

    if (index < 0)
    {
        return Proc_Find(ProcScr_GmapAutoMu) ? true : false;
    }

    Proc_FindBegin(&procIter, ProcScr_GmapAutoMu);
    do
    {
        proc = Proc_FindNext(&procIter);

#ifdef PC_PORT
        /* D-35 (NULL-probe N-23): when no Gmap Auto Mu proc matches, Proc_FindNext returns
           NULL and the GBA's ldrh of 0x2C (0x080C34B0) reads open-bus lanes 0-1. The only caller,
           EventAC_WmUnitMoveWait, runs in world-map events with the world map's HBlank IRQ on
           (WorldMapHblank), so the latch is post-IRQ 0xE55EC002 and the value is 0xC002
           (zero-extended; mGBA, route ch2 tick 37,699 on the skip path and 37,981 on the
           normal path). PORT_NULL_READ picks the latch from IME, IE and DISPSTAT at the read,
           so a read with the HBlank IRQ off gets the post-SWI 0x2004. The value is compared
           with index, which no retail event argument (-1, 0, 2) can equal for any latch; the
           loop test then ends the loop and the function returns false. See DIVERGENCES.md D-35. */
        if (PORT_NULL_READ(proc, unitId) == index)
#else
        if (proc->unitId == index)
#endif
        {
            return true;
        }
    } while (proc != NULL);

    return false;
}
