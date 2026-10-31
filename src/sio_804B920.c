#include "global.h"

#include "hardware.h"
#include "bmlib.h"

#include "sio.h"

//! FE8U = 0x0804B920
void PauseLinkArenaPalAnims(void)
{
    gUnk_Sio_0203DDDC = 1;
    return;
}

//! FE8U = 0x0804B92C
void ResumeLinkArenaPalAnims(void)
{
    gUnk_Sio_0203DDDC = 0;
    return;
}

//! FE8U = 0x0804B938
void SioDisableWinAndBlend(void)
{
    SetWinEnable(0, 0, 0);
    SetBlendConfig(0, 0, 0, 0);
    return;
}

//! FE8U = 0x0804B964
void LinkArenaBoxTransition_OnHBlank(void)
{
    u16 vcount = REG_VCOUNT + 1;

    if (vcount > DISPLAY_HEIGHT)
    {
        gSioDisplayedWinBuf = gSioFrontWinBuf;
        vcount = 0;
    }

    REG_WIN1H = (gSioDisplayedWinBuf[vcount * 2 + 0] << 8) + gSioDisplayedWinBuf[vcount * 2 + 1];

    return;
}

//! FE8U = 0x0804B9A4
void LinkArenaBoxTransition_SwapBuffers(void)
{
    s16 * swap = gSioFrontWinBuf;
    gSioFrontWinBuf = gSioBackWinBuf;
    gSioBackWinBuf = swap;

    return;
}

//! FE8U = 0x0804B9BC
void LinkArenaBoxTransition_EndHBlank(void)
{
    SetWinEnable(0, 0, 0);
    SetPrimaryHBlankHandler(NULL);
    return;
}

//! FE8U = 0x0804B9E4
void LinkArenaBoxTransition_RasterRotatedBox(s16 * a, int b, int c, int d, int e, int f, int g, int h, int i, s16 j, u16 k)
{
    int x1;
    int y1;
    int x2;
    int y2;
    int x3;
    int y3;
    int x4;
    int y4;

    b -= (DISPLAY_WIDTH / 2);
    c -= (DISPLAY_HEIGHT / 2);

    d -= (DISPLAY_WIDTH / 2);
    e -= (DISPLAY_HEIGHT / 2);

    f -= (DISPLAY_WIDTH / 2);
    g -= (DISPLAY_HEIGHT / 2);

    h -= (DISPLAY_WIDTH / 2);
    i -= (DISPLAY_HEIGHT / 2);

    x1 = (b * SIN(j)) + (c * COS(j));
    y1 = (b * COS(j)) - (c * SIN(j));

    x2 = (d * SIN(j)) + (e * COS(j));
    y2 = (d * COS(j)) - (e * SIN(j));

    x3 = (f * SIN(j)) + (g * COS(j));
    y3 = (f * COS(j)) - (g * SIN(j));

    x4 = (h * SIN(j)) + (i * COS(j));
    y4 = (h * COS(j)) - (i * SIN(j));

    x1 = ((k * (x1 >> 0xc)) >> 0x8) + (DISPLAY_WIDTH / 2);
    y1 = ((k * (y1 >> 0xc)) >> 0x8) + (DISPLAY_HEIGHT / 2);

    x2 = ((k * (x2 >> 0xc)) >> 0x8) + (DISPLAY_WIDTH / 2);
    y2 = ((k * (y2 >> 0xc)) >> 0x8) + (DISPLAY_HEIGHT / 2);

    x3 = ((k * (x3 >> 0xc)) >> 0x8) + (DISPLAY_WIDTH / 2);
    y3 = ((k * (y3 >> 0xc)) >> 0x8) + (DISPLAY_HEIGHT / 2);

    x4 = ((k * (x4 >> 0xc)) >> 0x8) + (DISPLAY_WIDTH / 2);
    y4 = ((k * (y4 >> 0xc)) >> 0x8) + (DISPLAY_HEIGHT / 2);

    AddEdgeToWinBuf(a, x1, y1, x2, y2);
    AddEdgeToWinBuf(a, x2, y2, x3, y3);
    AddEdgeToWinBuf(a, x3, y3, x4, y4);
    AddEdgeToWinBuf(a, x4, y4, x1, y1);

    return;
}

//! FE8U = 0x0804BB54
void LinkArenaBoxTransition_Init(struct SioProc85AA7B4 * proc)
{
    gSioFrontWinBuf = gSioWinBufFrontData;

    gSioBackWinBuf = gSioWinBufFrontData - 320;
    gSioDisplayedWinBuf = gSioWinBufFrontData;

    InitWinBuf(gSioFrontWinBuf);
    InitWinBuf(gSioBackWinBuf);

    proc->unk_4c = 0;

    SetPrimaryHBlankHandler(LinkArenaBoxTransition_OnHBlank);

    return;
}

//! FE8U = 0x0804BBA0
void LinkArenaBoxTransition_OpenLoop(struct SioProc85AA7B4 * proc)
{
    int a;
    int b;

    if (proc->unk_4c == proc->unk_64)
    {
        SetPrimaryHBlankHandler(NULL);
        Proc_Break(proc);
        return;
    }

    a = Interpolate(INTERPOLATE_LINEAR, 0xa0, 0xc0, proc->unk_4c, proc->unk_64);
    b = Interpolate(INTERPOLATE_SQUARE, 0x10, 0x110, proc->unk_4c, proc->unk_64);

    InitWinBuf(gSioBackWinBuf);
    LinkArenaBoxTransition_RasterRotatedBox(gSioBackWinBuf, 0, 0, DISPLAY_WIDTH, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, DISPLAY_HEIGHT, a, b);
    LinkArenaBoxTransition_SwapBuffers();

    proc->unk_4c++;

    return;
}

//! FE8U = 0x0804BC3C
void LinkArenaBoxTransition_OpenEnd(void)
{
    SetDispEnable(1, 1, 1, 1, 1);
    SetWinEnable(0, 0, 0);

    SetWin1Box(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    return;
}

// clang-format off

struct ProcCmd CONST_DATA ProcScr_LinkArenaBoxTransitionOpen[] =
{
    PROC_YIELD,

    PROC_CALL(LinkArenaBoxTransition_Init),
    PROC_REPEAT(LinkArenaBoxTransition_OpenLoop),
    PROC_SLEEP(1),

    PROC_CALL(LinkArenaBoxTransition_OpenEnd),
    PROC_SLEEP(1),

    PROC_END,
};

// clang-format on

//! FE8U = 0x0804BC84
void StartLinkArenaBoxTransitionOpen(u16 a, ProcPtr parent)
{
    struct SioProc85AA7B4 * proc = Proc_StartBlocking(ProcScr_LinkArenaBoxTransitionOpen, parent);
    proc->unk_64 = a;

    SetDispEnable(1, 1, 1, 1, 1);
    SetWinEnable(0, 1, 0);

    SetWin1Box(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    SetWin1Layers(1, 1, 1, 1, 1);
    SetWOutLayers(0, 0, 0, 0, 0);

    gLCDControlBuffer.wincnt.win1_enableBlend = 1;

    return;
}

//! FE8U = 0x0804BD2C
void StartLinkArenaBoxTransitionOpenDefault(ProcPtr parent)
{
    StartLinkArenaBoxTransitionOpen(0x40, parent);
    return;
}

//! FE8U = 0x0804BD3C
void LinkArenaBoxTransition_CloseLoop(struct SioProc85AA7B4 * proc)
{
    int a;
    int b;

    if (proc->unk_4c == proc->unk_64)
    {
        SetPrimaryHBlankHandler(NULL);
        Proc_Break(proc);
        return;
    }

    a = Interpolate(INTERPOLATE_RSQUARE, 0xc0, 0xa0, proc->unk_4c, proc->unk_64);
    b = Interpolate(INTERPOLATE_RCUBIC, 0x110, 0x10, proc->unk_4c, proc->unk_64);

    InitWinBuf(gSioBackWinBuf);
    LinkArenaBoxTransition_RasterRotatedBox(gSioBackWinBuf, 0, 0, DISPLAY_WIDTH, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, DISPLAY_HEIGHT, a, b);
    LinkArenaBoxTransition_SwapBuffers();

    proc->unk_4c++;

    return;
}

//! FE8U = 0x0804BDD8
void LinkArenaBoxTransition_CloseEnd(void)
{
    SetDispEnable(0, 0, 0, 0, 0);
    SetWinEnable(0, 0, 0);

    SetWin1Box(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    return;
}

// clang-format off

struct ProcCmd CONST_DATA ProcScr_LinkArenaBoxTransitionClose[] =
{
    PROC_YIELD,

    PROC_CALL(LinkArenaBoxTransition_Init),
    PROC_REPEAT(LinkArenaBoxTransition_CloseLoop),
    PROC_SLEEP(1),

    PROC_CALL(LinkArenaBoxTransition_CloseEnd),
    PROC_SLEEP(1),

    PROC_END,
};

// clang-format on

//! FE8U = 0x0804BDFC
void StartLinkArenaBoxTransitionClose(u16 a, ProcPtr parent)
{
    struct SioProc85AA7B4 * proc = Proc_StartBlocking(ProcScr_LinkArenaBoxTransitionClose, parent);
    proc->unk_64 = a;

    SetWinEnable(0, 1, 0);

    SetWin1Box(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    SetWin1Layers(1, 1, 1, 1, 1);
    SetWOutLayers(0, 0, 0, 0, 0);

    gLCDControlBuffer.wincnt.win1_enableBlend = 1;

    return;
}

//! FE8U = 0x0804BE88
void StartLinkArenaBoxTransitionCloseDefault(ProcPtr parent)
{
    StartLinkArenaBoxTransitionClose(0x40, parent);
    return;
}

//! FE8U = 0x0804BE98
void LinkArenaBoxTransition_WaitForOpen(ProcPtr proc)
{
    if (Proc_Find(ProcScr_LinkArenaBoxTransitionOpen) == NULL)
    {
        Proc_Break(proc);
    }

    return;
}

//! FE8U = 0x0804BEB8
void LinkArenaBoxTransition_WaitForClose(ProcPtr proc)
{
    if (Proc_Find(ProcScr_LinkArenaBoxTransitionClose) == NULL)
    {
        Proc_Break(proc);
    }

    return;
}
