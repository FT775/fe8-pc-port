#include "global.h"

#include "face.h"
#include "hardware.h"
#include "worldmap.h"

//! FE8U = 0x080B820C
void WorldMapFaceHolder_Init(struct WMFaceHolderProc * proc)
{
    int i;
    for (i = 0; i < 2; i++)
    {
        proc->faceWrapper[i].faceProc = NULL;
        proc->faceWrapper[i].unk_0a = 0;
        proc->faceWrapper[i].y = 0;
        proc->faceWrapper[i].x = 0;
        proc->faceWrapper[i].faceId |= 0xFFFF;
    }
}

struct ProcCmd CONST_DATA ProcScr_WorldMapFaceHolder[] = {
    PROC_MARK(8),
    PROC_CALL(WorldMapFaceHolder_Init),
    PROC_BLOCK,

    PROC_END,
};

//! FE8U = 0x080B823C
void WMFaceCtrl_Init(struct WMFaceCtrlProc * proc)
{
    proc->timer = 0;
    proc->faceHolderProc = Proc_Start(ProcScr_WorldMapFaceHolder, proc);
    proc->increment = 0;
    proc->blendAmt = 0;
    proc->unk_33 = 0;
    proc->unk_34 = 0;
}

//! FE8U = 0x080B826C
int GetWMFaceTileXAndScroll(int xIn, int *xOut) {
    int a = xIn - 0x30;
    int b;

    if (a < 0)
        b = xIn - 0x29;
    else
        b = a;

    xIn = b;
    xIn = xIn >> 3;
    *xOut = xIn * 8 - a;

    return xIn;
}

//! FE8U = 0x080B828C
int GetWMFaceBg(void) {
    return (gGMData.state.bits.state_3) ? BG_2 : BG_1;
}

//! FE8U = 0x080B82A8
int GetWMFaceVramOffset(void) {
    return (gGMData.state.bits.state_3) ? 0x5000 : 0x1800;
}

//! FE8U = 0x080B82C8
void PutWMFaceOnBg(int bg, u32 offset, int xIn, int yIn, u8 flip)
{
    int bgX;
    u16* r0;
    int x;
    int y;
    int y_;

    x = GetWMFaceTileXAndScroll(xIn, &bgX);
    y = yIn / 8;
    y_ = (y * 8) - yIn;

    r0 = BG_GetMapBuffer(bg) + y * 0x20 + x;

    PutFaceOnBackGround(
        r0,
        (offset >> 5) + 0x4000,
        flip
    );

    BG_EnableSyncByMask(1 << (bg));

    BG_SetPosition(bg, bgX, y_);
}

//! FE8U = 0x080B8350
void HandleWMFaceFade(struct WMFaceCtrlProc * proc)
{
    int i;

    SetBlendConfig(1, proc->blendAmt >> 1, 0x10 - (proc->blendAmt >> 1), 0);

    proc->blendAmt += proc->increment;

    if (proc->blendAmt == 0)
    {
        for (i = 0; i < 2; i++)
        {
            if ((proc->faceHolderProc->faceWrapper[i].faceProc != NULL) && ((proc->faceHolderProc->faceWrapper[i].unk_0a == -1)))
            {
                EndFaceById(i);

                proc->faceHolderProc->faceWrapper[i].unk_0a = 0;
                proc->faceHolderProc->faceWrapper[i].faceProc = NULL;
            }
        }

        proc->increment = 0;
    }

    if (proc->blendAmt == 0x20)
    {
        for (i = 0; i < 2; i++)
        {
            struct FaceProc * faceProc = proc->faceHolderProc->faceWrapper[i].faceProc;

            if (faceProc != NULL && (proc->faceHolderProc->faceWrapper[i].unk_0a == 1))
            {
                SetFaceDisplayBits(faceProc, GetFaceDisplayBits(faceProc) & ~(FACE_DISP_BLEND));
                proc->faceHolderProc->faceWrapper[i].unk_0a = 0;
            }
        }

        proc->increment = 0;
    }
}

//! FE8U = 0x080B843C
void WMFaceCtrl_LoopExt(struct WMFaceCtrlProc * proc)
{
    int i;
    int iVar10;
    struct WMFaceHolderProc * internalProc = proc->faceHolderProc;
    int bg = GetWMFaceBg();

    for (i = 0; i < 2; i++)
    {
        iVar10 = 0;

        if (internalProc->faceWrapper[i].faceProc != NULL)
        {
            struct FaceProc * faceProc = internalProc->faceWrapper[i].faceProc;
            int x = internalProc->faceWrapper[i].x;
            u16 * yptrUnsigned = &internalProc->faceWrapper[i].y;

            if (((*yptrUnsigned & 0x800) != 0) && ((*yptrUnsigned & 0xff) < 0x10))
            {
                if ((*yptrUnsigned & 0x100) != 0)
                {
                    faceProc->xPos = x + (((0x10 - (*yptrUnsigned & 0xff)) * 0x20 * (0x10 - (*yptrUnsigned & 0xff))) / 256);
                    iVar10 = 1;
                }

                if ((*yptrUnsigned & 0x200) != 0)
                {
                    faceProc->xPos = x - (((0x10 - (*yptrUnsigned & 0xff)) * 0x20 * (0x10 - (*yptrUnsigned & 0xff))) / 256);
                    iVar10 = 1;
                }

                (*yptrUnsigned)++;

                if ((*yptrUnsigned & 0xff) > 0xf)
                {
                    iVar10 = 2;

                    SetFaceDisplayBits(faceProc, GetFaceDisplayBits(faceProc) &~ 0x4000);
                    *yptrUnsigned &= ~0x800;
                }
            }

            if (((*yptrUnsigned & 0x1000) != 0) && ((*yptrUnsigned & 0xff) < 0x10))
            {
                if ((*yptrUnsigned & 0xff) == 0)
                    SetFaceDisplayBits(faceProc, GetFaceDisplayBits(faceProc) | 0x4000);

                if ((*yptrUnsigned & 0x100) != 0)
                {
                    faceProc->xPos = ({ x - 0x20; }) + (((0x10 - (*yptrUnsigned & 0xff)) * 0x20 * (0x10 - (*yptrUnsigned & 0xff))) / 256);
                    iVar10 = 1;
                }

                if ((*yptrUnsigned & 0x200) != 0)
                {
                    faceProc->xPos = ({ x + 0x20; }) - (((0x10 - (*yptrUnsigned & 0xff)) * 0x20 * (0x10 - (*yptrUnsigned & 0xff))) / 256);
                    iVar10 = 1;
                }

                (*yptrUnsigned)++;

                if ((*yptrUnsigned & 0xff) > 0xf)
                    iVar10 = 2;
            }

            switch (iVar10) {
            case 1:
                BG_Fill(BG_GetMapBuffer(bg), 0);
                PutWMFaceOnBg(
                    GetWMFaceBg(),
                    GetWMFaceVramOffset(),
                    faceProc->xPos,
                    0x1c,
                    (*yptrUnsigned >> 10) & 1
                );
                BG_EnableSyncByMask(1 << (bg));

                break;

            case 2:
                BG_Fill(BG_GetMapBuffer(bg), 0);
                BG_EnableSyncByMask(1 << (bg));

                break;
            }
        }
    }
}

//! FE8U = 0x080B8630
void WMFaceCtrl_Loop(struct WMFaceCtrlProc * proc)
{
    u8 gUnknown_08205F28[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 1, 1, 1, 1, 1, 1, 2, 2,
        2, 2, 2, 2, 3, 3, 3, 3, 3,
        3, 3, 3, 3, 3, 2, 2, 2, 2,
        2, 2, 1, 1, 1, 1, 1, 1, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0,

        -1,
    };

    proc->timer++;

    if (gUnknown_08205F28[proc->timer] == (u8)-1)
        proc->timer = 0;

    WMFaceCtrl_LoopExt(proc);

    if (proc->increment != 0)
        HandleWMFaceFade(proc);
}

//! FE8U = 0x080B867C
void nullsub_57(void) {
    return;
}

struct ProcCmd CONST_DATA ProcScr_WorldMapFaceCtrl[] = {
    PROC_MARK(8),
    PROC_SET_END_CB(nullsub_57),

    PROC_CALL(WMFaceCtrl_Init),
    PROC_REPEAT(WMFaceCtrl_Loop),

    PROC_END,
};

//! FE8U = 0x080B8680
ProcPtr StartWMFaceCtrl(ProcPtr parent) {
    return Proc_Start(ProcScr_WorldMapFaceCtrl, parent);
}

//! FE8U = 0x080B8694
void EndWMFaceCtrl(void)
{
    Proc_EndEach(ProcScr_WorldMapFaceCtrl);
    return;
}

//! FE8U = 0x080B86A4
int GetWMFaceBlendAmt(void)
{
    struct WMFaceCtrlProc * proc;

    Proc_Find(ProcScr_WorldMapFaceCtrl);

    proc = Proc_Find(ProcScr_WorldMapFaceCtrl);

    if (proc) {
        return proc->blendAmt >> 1;
    } else {
        return 0x10;
    }
}

//! FE8U = 0x080B86CC
void WmDrawFace(int faceSlot, int faceId, u16 config)
{
    int bg;
    u32 offset;

    struct WMFaceCtrlProc * proc = Proc_Find(ProcScr_WorldMapFaceCtrl);

    struct WMFaceWrapper * pWrapper = proc->faceHolderProc->faceWrapper + faceSlot;

    if (gGMData.state.bits.state_3)
    {
        SetBlendTargetA(0, 0, 1, 0, 0);
        SetBlendTargetB(0, 1, 0, 1, 1);
    }
    else
    {
        SetBlendTargetA(0, 1, 0, 0, 0);
        SetBlendTargetB(0, 0, 1, 1, 1);
    }

    bg = GetWMFaceBg();
    offset = GetWMFaceVramOffset();

    if (pWrapper->faceProc == NULL)
    {
        int x;
        int disp;
        struct FaceProc * faceProc;

        pWrapper->faceId = faceId;

        pWrapper->x = config & 0x00ff;
        pWrapper->y = (config & 0xff00) + 0x800;

        UnpackFaceImg(offset / CHR_SIZE, faceId);
        ApplyFacePal(4, faceId);

        BG_Fill(BG_GetMapBuffer(bg), 0);

        x = pWrapper->x;

        if (config & 0x400)
            disp = (FACE_DISP_HIDDEN | FACE_DISP_BLEND | FACE_DISP_KIND(FACE_96x80) | FACE_DISP_HLAYER(FACE_HLAYER_1) | FACE_DISP_FLIPPED);
        else
            disp = (FACE_DISP_HIDDEN | FACE_DISP_BLEND | FACE_DISP_KIND(FACE_96x80) | FACE_DISP_HLAYER(FACE_HLAYER_1));

        if (config & 0x8000)
            disp = disp | FACE_DISP_BIT_13;

        faceProc = StartFace2(faceSlot, faceId, x, 28, disp);

        pWrapper->faceProc = faceProc;

        if ((config & 0x6000) == 0x6000)
            faceProc->spriteLayer = 6;
        else if (config & 0x4000)
            faceProc->spriteLayer = 5;
        else if (config & 0x2000)
            faceProc->spriteLayer = 4;
        else
            faceProc->spriteLayer = 3;

        SetFaceBlinkControlById(faceSlot, 5);

        pWrapper->unk_0a = +1;
        proc->increment = +2;

        if (proc->blendAmt == 0x20)
        {
            proc->blendAmt = 0;
            SetBlendConfig(1, 0, 0x10, 0);
        }
    }
}

//! FE8U = 0x080B8844
void WmClearFace(int faceSlot, u16 config)
{
    int bg;
    u32 offset;

    struct WMFaceCtrlProc * proc = Proc_Find(ProcScr_WorldMapFaceCtrl);

    struct WMFaceWrapper* pWrapper = proc->faceHolderProc->faceWrapper + faceSlot;

    if (gGMData.state.bits.state_3) {
        SetBlendTargetA(0, 0, 1, 0, 0);
        SetBlendTargetB(0, 1, 0, 1, 1);
    } else {
        SetBlendTargetA(0, 1, 0, 0, 0);
        SetBlendTargetB(0, 0, 1, 1, 1);
    }

    bg = GetWMFaceBg();
    offset = GetWMFaceVramOffset();

    if (pWrapper->faceProc != NULL && (pWrapper->y & 0x1000) == 0) {
        int tmp;

        UnpackFaceImg(offset / CHR_SIZE, (s16)pWrapper->faceId);
        ApplyFacePal(4, (s16)pWrapper->faceId);

        BG_Fill(BG_GetMapBuffer(bg), 0);

        tmp = 0xFFFFFF00 & (u16)pWrapper->y;
        // Seems to be required to match...
        tmp &= (u16)pWrapper->y;

        if (config & 0x300) {
            tmp &= 0x0000FCFF;
            tmp |= (config & 0x300);
        } else {
            tmp &= 0x0000FCFF;
        }

        tmp &= 0xff00;

        pWrapper->y = tmp + 0x1000;

        pWrapper->unk_0a = -1;
        proc->increment = -2;

        if (proc->blendAmt == 0) {
            proc->blendAmt = 0x20;
            SetBlendConfig(1, 0x10, 0, 0);
        }
    }
}

//! FE8U = 0x080B895C
void HideWmFace(void)
{
    int i;

    struct WMFaceCtrlProc * proc = Proc_Find(ProcScr_WorldMapFaceCtrl);

    SetBlendTargetA(0, 0, 0, 0, 0);
    SetBlendTargetB(0, 0, 0, 1, 0);

#ifdef PC_PORT
    /*
     * No world-map face controller (port-plan/DIVERGENCES.md, D-83, was working ID D-R10-WMFACE-1): at a
     * world-map skirmish's start (WorldMap_HideAllForSkirmish, WorldMainProc label 16) after a walk that ran no
     * world-map event, or after A on the lord's own node while monsters are there (WorldMap_TrySelectNode,
     * src/worldmap_main.c, also label 16: the wmface reviewer's second trigger, which no route
     * runs; the guard covers both, being in HideWmFace), Proc_Find returns NULL. The GBA's loop
     * then loads faceHolderProc from 0x2C
     * (ldr at 0x080B8994): the BIOS latch word, 0xE......., where the GBA has no memory. So
     * each slot's faceProc and y loads (0x080B899A, 0x080B89A0) are open bus, the opcodes at the
     * load + 4: a "face" at 0xD027D027, not NULL, and y 0x0176, not hidden. GetFaceDisplayBits,
     * SetFaceDisplayBits and FaceRefreshSprite then load from that face (open bus) and store to it
     * (no memory: dropped); their one load from RAM, sFaceConfig[0x18] (0x0202A74C, the faceSlot
     * byte from open bus), only feeds the dropped oam2 store. The y and unk_0a stores go to
     * 0xE......., the increment store to the BIOS (dropped), and the blendAmt load (0x31) is latch
     * lane 1, never 0 (0x20 post-SWI, 0xC0 post-IRQ, 0xF0 in an IRQ or before the first SWI), so
     * SetBlendConfig is not called. What is left is the two SetBlendTarget calls above, for every
     * latch word. Measured (mGBA, route_sk4/sk10/sk15/sk17 at ticks 1,478/1,472/1,414/1,365):
     * latch 0xE55EC002, face 0xD027D027, y 0x0176, blendAmt 0xC0 in both passes; nothing in RAM
     * changed across the loop but the stacks.
     */
    if (proc == NULL)
        return;
#endif

    for (i = 0; i < 2; i++)
    {
        struct WMFaceWrapper * pWrapper = &proc->faceHolderProc->faceWrapper[i];

        struct FaceProc * faceProc = pWrapper->faceProc;

        if (faceProc != NULL && (pWrapper->y & 0x1000) == 0)
        {
            SetFaceDisplayBits(faceProc, GetFaceDisplayBits(faceProc) | FACE_DISP_HIDDEN);

            pWrapper->y &= ~0xff;
            pWrapper->y += 0x1000;

            pWrapper->unk_0a = -1;
            proc->increment = -2;

            if (proc->blendAmt == 0)
            {
                proc->blendAmt = 0x20;
                SetBlendConfig(0, 0x10, 0, 0);
            }
        }
    }
}
