#include "global.h"
#include "mu.h"
#include "proc.h"
#include "hardware.h"
#include "bmunit.h"
#include "bmbattle.h"
#include "ap.h"
#include "ctc.h"
#include "bmio.h"
#include "rng.h"
#include "bmlib.h"
#include "mapanim.h"
#include "bm.h"
#include "bmudisp.h"
#include "bmmap.h"

#include "constants/songs.h"

struct MapAnimfxConf CONST_DATA MapAnimfxConf_DemonKingResurrectionCircle[] =
{
    {
        Img_DemonKingResurrectionCircle1,
        Pal_DemonKingResurrectionCircle1,
        Tsa_DemonKingResurrectionCircle1,
    },
    {
        Img_DemonKingResurrectionCircle2,
        Pal_DemonKingResurrectionCircle2,
        Tsa_DemonKingResurrectionCircle2,
    },
    {
        Img_DemonKingResurrectionCircle3,
        Pal_DemonKingResurrectionCircle3,
        Tsa_DemonKingResurrectionCircle3,
    },
    {
        Img_DemonKingResurrectionCircle4,
        Pal_DemonKingResurrectionCircle4,
        Tsa_DemonKingResurrectionCircle4,
    },
    {
        Img_DemonKingResurrectionCircle5,
        Pal_DemonKingResurrectionCircle5,
        Tsa_DemonKingResurrectionCircle5,
    },
    {
        Img_DemonKingResurrectionCircle6,
        Pal_DemonKingResurrectionCircle6,
        Tsa_DemonKingResurrectionCircle6,
    },
    {
        Img_DemonKingResurrectionCircle7,
        Pal_DemonKingResurrectionCircle7,
        Tsa_DemonKingResurrectionCircle7,
    },
    {
        Img_DemonKingResurrectionCircle8,
        Pal_DemonKingResurrectionCircle8,
        Tsa_DemonKingResurrectionCircle8,
    },
    {
        Img_DemonKingResurrectionCircle9,
        Pal_DemonKingResurrectionCircle9,
        Tsa_DemonKingResurrectionCircle9,
    },
    {
        Img_DemonKingResurrectionCircle10,
        Pal_DemonKingResurrectionCircle10,
        Tsa_DemonKingResurrectionCircle10,
    },
    {
        Img_DemonKingResurrectionCircle11,
        Pal_DemonKingResurrectionCircle11,
        Tsa_DemonKingResurrectionCircle11,
    },
    {
        Img_DemonKingResurrectionCircle12,
        Pal_DemonKingResurrectionCircle12,
        Tsa_DemonKingResurrectionCircle12,
    },
    {
        Img_DemonKingResurrectionCircle13,
        Pal_DemonKingResurrectionCircle13,
        Tsa_DemonKingResurrectionCircle13,
    },
    {
        Img_DemonKingResurrectionCircle14,
        Pal_DemonKingResurrectionCircle14,
        Tsa_DemonKingResurrectionCircle14,
    },
    {
        Img_DemonKingResurrectionCircle15,
        Pal_DemonKingResurrectionCircle15,
        Tsa_DemonKingResurrectionCircle15,
    },
    {
        Img_DemonKingResurrectionCircle16,
        Pal_DemonKingResurrectionCircle16,
        Tsa_DemonKingResurrectionCircle16,
    },
    {
        Img_DemonKingResurrectionCircle17,
        Pal_DemonKingResurrectionCircle17,
        Tsa_DemonKingResurrectionCircle17,
    },
    {
        Img_DemonKingResurrectionCircle18,
        Pal_DemonKingResurrectionCircle18,
        Tsa_DemonKingResurrectionCircle18,
    },
    {
        Img_DemonKingResurrectionCircle19,
        Pal_DemonKingResurrectionCircle19,
        Tsa_DemonKingResurrectionCircle19,
    },
    {
        Img_DemonKingResurrectionCircle20,
        Pal_DemonKingResurrectionCircle20,
        Tsa_DemonKingResurrectionCircle20,
    },
};

struct MapAnimfxConf CONST_DATA MapAnimfxConf_DemonKingResurrectionLightning[] =
{
    {
        Img_DemonKingResurrectionLightning1,
        Pal_DemonKingResurrectionLightning1,
        Tsa_DemonKingResurrectionLightning1,
    },
    {
        Img_DemonKingResurrectionLightning2,
        Pal_DemonKingResurrectionLightning2,
        Tsa_DemonKingResurrectionLightning2,
    },
    {
        Img_DemonKingResurrectionLightning3,
        Pal_DemonKingResurrectionLightning3,
        Tsa_DemonKingResurrectionLightning3,
    },
    {
        Img_DemonKingResurrectionLightning4,
        Pal_DemonKingResurrectionLightning4,
        Tsa_DemonKingResurrectionLightning4,
    },
    {
        Img_DemonKingResurrectionLightning5,
        Pal_DemonKingResurrectionLightning5,
        Tsa_DemonKingResurrectionLightning5,
    },
    {
        Img_DemonKingResurrectionLightning6,
        Pal_DemonKingResurrectionLightning6,
        Tsa_DemonKingResurrectionLightning6,
    },
    {
        Img_DemonKingResurrectionLightning7,
        Pal_DemonKingResurrectionLightning7,
        Tsa_DemonKingResurrectionLightning7,
    },
    {
        Img_DemonKingResurrectionLightning8,
        Pal_DemonKingResurrectionLightning8,
        Tsa_DemonKingResurrectionLightning8,
    },
    {
        Img_DemonKingResurrectionLightning9,
        Pal_DemonKingResurrectionLightning9,
        Tsa_DemonKingResurrectionLightning9,
    },
    {
        Img_DemonKingResurrectionLightning10,
        Pal_DemonKingResurrectionLightning10,
        Tsa_DemonKingResurrectionLightning10,
    },
    {
        Img_DemonKingResurrectionLightning11,
        Pal_DemonKingResurrectionLightning11,
        Tsa_DemonKingResurrectionLightning11,
    },
    {
        Img_DemonKingResurrectionLightning12,
        Pal_DemonKingResurrectionLightning12,
        Tsa_DemonKingResurrectionLightning12,
    },
    {
        Img_DemonKingResurrectionLightning13,
        Pal_DemonKingResurrectionLightning13,
        Tsa_DemonKingResurrectionLightning13,
    },
    {
        Img_DemonKingResurrectionLightning14,
        Pal_DemonKingResurrectionLightning14,
        Tsa_DemonKingResurrectionLightning14,
    },
    {
        Img_DemonKingResurrectionLightning15,
        Pal_DemonKingResurrectionLightning15,
        Tsa_DemonKingResurrectionLightning15,
    },
    {
        Img_DemonKingResurrectionLightning16,
        Pal_DemonKingResurrectionLightning16,
        Tsa_DemonKingResurrectionLightning16,
    },
    {
        Img_DemonKingResurrectionLightning17,
        Pal_DemonKingResurrectionLightning17,
        Tsa_DemonKingResurrectionLightning17,
    },
    {
        Img_DemonKingResurrectionLightning18,
        Pal_DemonKingResurrectionLightning18,
        Tsa_DemonKingResurrectionLightning18,
    },
    {
        Img_DemonKingResurrectionLightning19,
        Pal_DemonKingResurrectionLightning19,
        Tsa_DemonKingResurrectionLightning19,
    },
    {
        Img_DemonKingResurrectionLightning20,
        Pal_DemonKingResurrectionLightning20,
        Tsa_DemonKingResurrectionLightning20,
    },
    {
        Img_DemonKingResurrectionLightning21,
        Pal_DemonKingResurrectionLightning21,
        Tsa_DemonKingResurrectionLightning21,
    },
    {
        Img_DemonKingResurrectionLightning22,
        Pal_DemonKingResurrectionLightning22,
        Tsa_DemonKingResurrectionLightning22,
    },
    {
        Img_DemonKingResurrectionLightning23,
        Pal_DemonKingResurrectionLightning23,
        Tsa_DemonKingResurrectionLightning23,
    },
    {
        Img_DemonKingResurrectionLightning24,
        Pal_DemonKingResurrectionLightning24,
        Tsa_DemonKingResurrectionLightning24,
    },
};

struct MapAnimfxConf CONST_DATA MapAnimfxConf_DemonKingResurrectionEndFx[] =
{
    {
        Img_DemonKingResurrectionEndFx1,
        Pal_DemonKingResurrectionEndFx1,
        Tsa_DemonKingResurrectionEndFx1,
    },
    {
        Img_DemonKingResurrectionEndFx2,
        Pal_DemonKingResurrectionEndFx2,
        Tsa_DemonKingResurrectionEndFx2,
    },
    {
        Img_DemonKingResurrectionEndFx3,
        Pal_DemonKingResurrectionEndFx3,
        Tsa_DemonKingResurrectionEndFx3,
    },
    {
        Img_DemonKingResurrectionEndFx4,
        Pal_DemonKingResurrectionEndFx4,
        Tsa_DemonKingResurrectionEndFx4,
    },
    {
        Img_DemonKingResurrectionEndFx5,
        Pal_DemonKingResurrectionEndFx5,
        Tsa_DemonKingResurrectionEndFx5,
    },
    {
        Img_DemonKingResurrectionEndFx6,
        Pal_DemonKingResurrectionEndFx6,
        Tsa_DemonKingResurrectionEndFx6,
    },
    {
        Img_DemonKingResurrectionEndFx7,
        Pal_DemonKingResurrectionEndFx7,
        Tsa_DemonKingResurrectionEndFx7,
    },
    {
        Img_DemonKingResurrectionEndFx8,
        Pal_DemonKingResurrectionEndFx8,
        Tsa_DemonKingResurrectionEndFx8,
    },
    {
        Img_DemonKingResurrectionEndFx9,
        Pal_DemonKingResurrectionEndFx9,
        Tsa_DemonKingResurrectionEndFx9,
    },
    {
        Img_DemonKingResurrectionEndFx10,
        Pal_DemonKingResurrectionEndFx10,
        Tsa_DemonKingResurrectionEndFx10,
    },
    {
        Img_DemonKingResurrectionEndFx11,
        Pal_DemonKingResurrectionEndFx11,
        Tsa_DemonKingResurrectionEndFx11,
    },
    {
        Img_DemonKingResurrectionEndFx12,
        Pal_DemonKingResurrectionEndFx12,
        Tsa_DemonKingResurrectionEndFx12,
    },
};

CONST_DATA struct ProcCmd ProcScr_089A434C[] =
{
    PROC_SLEEP(1),

    PROC_CALL(DemonKingResurrectionFx_Init),
    PROC_REPEAT(DemonKingResurrectionFx_PlayAnim1),

    PROC_SLEEP(30),

    PROC_REPEAT(DemonKingResurrectionFx_PlayAnim2),
    PROC_REPEAT(DemonKingResurrectionFx_InitFlash),
    PROC_REPEAT(DemonKingResurrectionFx_FadeInFlash),

    PROC_CALL(DemonKingResurrectionFx_ClearBg2),

    PROC_END
};

void StartDemonKingResurrectionFx(ProcPtr proc)
{
    if (proc)
        Proc_StartBlocking(ProcScr_089A434C, proc);
    else
        Proc_Start(ProcScr_089A434C, PROC_TREE_3);
}

void DemonKingResurrectionFx_Init(struct MAEffectProc * proc)
{
    gLCDControlBuffer.bg0cnt.priority = 0;
    gLCDControlBuffer.bg1cnt.priority = 0;
    gLCDControlBuffer.bg2cnt.priority = 0;
    gLCDControlBuffer.bg3cnt.priority = 2;

    SetDispEnable(0, 0, 1, 0, 0);

    gLCDControlBuffer.wincnt.win0_enableBlend = 0;
    gLCDControlBuffer.wincnt.win1_enableBlend = 0;

    SetBlendTargetA(1, 0, 0, 0, 0);
    SetBlendBackdropA(0);

    SetBlendTargetB(0, 0, 1, 0, 0);
    SetBlendBackdropB(1);

    SetBlendAlpha(16, 16);

    BG_SetPosition(BG_0, 0, 0);
    BG_SetPosition(BG_2, 0, 0);

    proc->frame = 0;
    proc->timer = 0;
    proc->unk44 = 0;
}

// clang-format off

const int gDemonKingResurrectionFxChrTable[] =
{
    0x80, 0x1E0,
};

const u8 gDemonKingResurrectionFxPalTable[] =
{
    4, 5,
};

// clang-format on

void DemonKingResurrectionFx_PlayAnim1(struct MAEffectProc * proc)
{
    if (proc->timer == 0)
    {
        if (proc->frame == 0)
        {
            PlaySeSpacial(SONG_140, proc->xDisplay);
        }
        else if (proc->frame > 19)
        {
            proc->frame = 0;
            proc->timer = 0;
            proc->unk44 = 1;

            SetDispEnable(0, 0, 0, 0, 0);

            Proc_Break(proc);
            return;
        }

        Decompress(
            MapAnimfxConf_DemonKingResurrectionCircle[proc->frame].img,
            (void*) VRAM + gDemonKingResurrectionFxChrTable[proc->unk44] * 0x20);

        Decompress(
            MapAnimfxConf_DemonKingResurrectionCircle[proc->frame].tsa,
            gGenericBuffer);

        AddAttr2dBitMap(
            gBG2TilemapBuffer,
            (u16 *)gGenericBuffer,
            0, 0,
            gDemonKingResurrectionFxChrTable[proc->unk44] | (gDemonKingResurrectionFxPalTable[proc->unk44] << 12));

        BG_EnableSyncByMask(BG2_SYNC_BIT);

        ApplyPalette(MapAnimfxConf_DemonKingResurrectionCircle[proc->frame].pal, gDemonKingResurrectionFxPalTable[proc->unk44]);
        EnablePaletteSync();

        proc->frame++;
        proc->timer = 3;
        proc->unk44 ^= 1;
    }

    proc->timer--;
}

// clang-format off

const u8 gDemonKingResurrectionFxAnim2Durations[] =
{
    4, 5, 6, 7, 7, 7, 4,
    5, 5, 6, 6, 4, 4, 5,
    5, 3, 4, 3, 4, 3, 4,
    2, 2, 2, 0, 0,
};

// clang-format on

void DemonKingResurrectionFx_PlayAnim2(struct MAEffectProc * proc)
{
    if (proc->timer == 0)
    {
        Decompress(
            MapAnimfxConf_DemonKingResurrectionLightning[proc->frame].img,
            (void*) VRAM + gDemonKingResurrectionFxChrTable[proc->unk44]*0x20);

        Decompress(
            MapAnimfxConf_DemonKingResurrectionLightning[proc->frame].tsa,
            gGenericBuffer);

        AddAttr2dBitMap(
            gBG2TilemapBuffer,
            (u16 *)gGenericBuffer,
            0, 0,
            gDemonKingResurrectionFxChrTable[proc->unk44] | (gDemonKingResurrectionFxPalTable[proc->unk44] << 12));

        BG_EnableSyncByMask(BG2_SYNC_BIT);

        ApplyPalette(MapAnimfxConf_DemonKingResurrectionLightning[proc->frame].pal, gDemonKingResurrectionFxPalTable[proc->unk44]);
        EnablePaletteSync();

        if (proc->frame == 0)
        {
            SetDispEnable(0, 0, 1, 0, 0);
        }
        else if (proc->frame > 22)
        {
            Proc_Break(proc);
        }

        proc->timer = gDemonKingResurrectionFxAnim2Durations[proc->frame];
        proc->frame++;
        proc->unk44 ^= 1;
    }

    proc->timer--;
}

void DemonKingResurrectionFx_InitFlash(struct MAEffectProc * proc)
{
    if (proc->timer == 0)
    {
        proc->frame = 0;

        CpuFastFill(-1, (void*) VRAM + 0x20 * 0x2FF, 0x20);

        TileMap_FillRect(gBG0TilemapBuffer, 30, 20, TILEREF(0x2FF, 3));
        BG_EnableSyncByMask(BG0_SYNC_BIT);

        PAL_BG_COLOR(3, 15) = 0;
        EnablePaletteSync();

        SetDispEnable(1, 0, 1, 0, 0);

        Proc_Break(proc);
    }
    proc->timer--;
}

void DemonKingResurrectionFx_FadeInFlash(struct MAEffectProc * proc)
{
    u16 brightness = proc->frame * 4;

    if (proc->frame < 8)
    {
        PAL_BG_COLOR(3, 15) = RGB(brightness, brightness, brightness);
    }
    else
    {
        PAL_BG_COLOR(3, 15) = RGB(31, 31, 31);
        Proc_Break(proc);
    }

    EnablePaletteSync();
    proc->frame++;
}

void DemonKingResurrectionFx_ClearBg2(void)
{
    BG_Fill(gBG2TilemapBuffer, 0);
    BG_EnableSyncByMask(BG2_SYNC_BIT);
}

CONST_DATA struct ProcCmd ProcScr_089A4394[] =
{
    PROC_SLEEP(1),

    PROC_CALL(DemonKingResurrectionEndFx_Init),
    PROC_REPEAT(DemonKingResurrectionEndFx_FadeOutFlash),
    PROC_REPEAT(DemonKingResurrectionEndFx_PlayAnim),
    PROC_CALL(DemonKingResurrectionEndFx_ClearBgs),

    PROC_SLEEP(60),

    PROC_CALL(MapSpellAnim_CommonEnd),

    PROC_END
};

void StartDemonKingResurrectionEndFx(ProcPtr proc)
{
    if (proc)
        Proc_StartBlocking(ProcScr_089A4394, proc);
    else
        Proc_Start(ProcScr_089A4394, PROC_TREE_3);
}

void DemonKingResurrectionEndFx_Init(struct MAEffectProc * proc)
{
    gLCDControlBuffer.bg0cnt.priority = 0;
    gLCDControlBuffer.bg1cnt.priority = 0;
    gLCDControlBuffer.bg2cnt.priority = 0;
    gLCDControlBuffer.bg3cnt.priority = 2;

    SetDispEnable(1, 0, 1, 1, 1);

    gLCDControlBuffer.wincnt.win0_enableBlend = 0;
    gLCDControlBuffer.wincnt.win1_enableBlend = 0;

    SetBlendTargetA(1, 0, 1, 0, 0);
    SetBlendBackdropA(0);

    SetBlendTargetB(0, 0, 0, 1, 1);
    SetBlendBackdropB(1);

    SetBlendAlpha(16, 16);

    BG_SetPosition(BG_0, 0, 0);
    BG_SetPosition(BG_2, 0, 0);

    PAL_BG_COLOR(3, 15) = RGB(31, 31, 31);
    EnablePaletteSync();

    proc->frame = 0;
    proc->timer = 0;
    proc->unk44 = 0;

    proc->unk48 = 119;
}

void DemonKingResurrectionEndFx_FadeOutFlash(struct MAEffectProc * proc)
{
    int brightness = proc->unk48 * 32 / 120;
    PAL_BG_COLOR(3, 15) = RGB(brightness, brightness, brightness);

    EnablePaletteSync();

    proc->unk48--;

    if (proc->unk48 < 31)
        Proc_Break(proc);
}

void DemonKingResurrectionEndFx_PlayAnim(struct MAEffectProc * proc)
{
    if (proc->timer == 0)
    {
        if (proc->frame != 0)
        {
            if (proc->frame > 11)
            {
                proc->frame = proc->timer;
                Proc_Break(proc);
                return;
            }
        }
        else
        {
            SetDispEnable(0, 0, 1, 1, 1);
        }

        Decompress(
            MapAnimfxConf_DemonKingResurrectionEndFx[proc->frame].img,
            (void*) VRAM + gDemonKingResurrectionFxChrTable[proc->unk44]*0x20);

        Decompress(
            MapAnimfxConf_DemonKingResurrectionEndFx[proc->frame].tsa,
            gGenericBuffer);

        AddAttr2dBitMap(
            gBG2TilemapBuffer,
            (u16*) gGenericBuffer,
            0, 0,
            gDemonKingResurrectionFxChrTable[proc->unk44] | (gDemonKingResurrectionFxPalTable[proc->unk44] << 12));

        BG_EnableSyncByMask(BG2_SYNC_BIT);

        if (proc->unk48 < 0)
        {
            ApplyPalette(MapAnimfxConf_DemonKingResurrectionEndFx[proc->frame].pal, gDemonKingResurrectionFxPalTable[proc->unk44]);
            EnablePaletteSync();
        }

        proc->frame_idx = proc->frame;
        proc->frame++;

        proc->timer = 4;

        proc->unk4C = proc->unk44;
        proc->unk44 ^= 1;
    }

    proc->timer--;

    if (proc->unk48 >= 0)
    {
        int i, addedBrightness = proc->unk48 * 32 / 240;

        const u16 * const in  = MapAnimfxConf_DemonKingResurrectionEndFx[proc->frame_idx].pal;
        u16 *       const out = &gPaletteBuffer[0x10 * gDemonKingResurrectionFxPalTable[proc->unk4C]];

        for (i = 1; i < 16; ++i)
        {
            u32 r = RED_VALUE(in[i]);
            u32 g = GREEN_VALUE(in[i]);
            u32 b = BLUE_VALUE(in[i]);

            r = r + addedBrightness > 31 ? 31 : r + addedBrightness;
            g = g + addedBrightness > 31 ? 31 : g + addedBrightness;
            b = b + addedBrightness > 31 ? 31 : b + addedBrightness;

            out[i] = RGB(r, g, b);
        }

        EnablePaletteSync();

        proc->unk48--;
    }
}

void DemonKingResurrectionEndFx_ClearBgs(void)
{
    BG_Fill(gBG0TilemapBuffer, 0);
    BG_Fill(gBG2TilemapBuffer, 0);

    BG_EnableSyncByMask(BG0_SYNC_BIT + BG2_SYNC_BIT);
}

void StartBraceletSealBreakFx(ProcPtr proc)
{
    if (proc)
        Proc_StartBlocking(ProcScr_089A4434, proc);
    else
        Proc_Start(ProcScr_089A4434, PROC_TREE_3);
}

void BraceletSealBreakFx_OnEnd(void)
{
    SetSecondaryHBlankHandler(NULL);
    Proc_EndEach(ProcScr_089A448C);
}

void BraceletSealBreakFx_Init(struct MAEffectProc * proc)
{
    struct Proc8080050 * vsync;

    gLCDControlBuffer.bg0cnt.priority = 0;
    gLCDControlBuffer.bg1cnt.priority = 0;
    gLCDControlBuffer.bg2cnt.priority = 0;
    gLCDControlBuffer.bg3cnt.priority = 2;

    SetDispEnable(0, 0, 1, 1, 1);

    gLCDControlBuffer.wincnt.win0_enableBlend = 0;
    gLCDControlBuffer.wincnt.win1_enableBlend = 0;

    SetBlendTargetA(0, 0, 1, 0, 0);
    SetBlendBackdropA(0);

    SetBlendTargetB(0, 0, 0, 1, 1);
    SetBlendBackdropB(1);

    SetBlendAlpha(16, 16);

    BG_SetPosition(BG_2, 0, 0);

    proc->frame = 0;
    proc->timer = 0;
    proc->unk44 = 0;

    gBraceletSealBreakFxStretchEnabled = 0;

    vsync = Proc_Start(ProcScr_089A448C, PROC_TREE_VSYNC);

    vsync->unk29 = 0;
    vsync->unk2A = 0;

    SetSecondaryHBlankHandler(BraceletSealBreakFx_OnHBlank);
}

// clang-format off

struct MapAnimfxConf CONST_DATA MapAnimfxConf_BraceletSealBreakFx[] =
{
    {
        .img = Img_BraceletSealBreakFx1,
        .pal = Pal_BraceletSealBreakFx1,
        .tsa = Tsa_BraceletSealBreakFx1,
    },
    {
        .img = Img_BraceletSealBreakFx2,
        .pal = Pal_BraceletSealBreakFx2,
        .tsa = Tsa_BraceletSealBreakFx2,
    },
    {
        .img = Img_BraceletSealBreakFx3,
        .pal = Pal_BraceletSealBreakFx3,
        .tsa = Tsa_BraceletSealBreakFx3,
    },
    {
        .img = Img_BraceletSealBreakFx4,
        .pal = Pal_BraceletSealBreakFx4,
        .tsa = Tsa_BraceletSealBreakFx4,
    },
    {
        .img = Img_BraceletSealBreakFx5,
        .pal = Pal_BraceletSealBreakFx5,
        .tsa = Tsa_BraceletSealBreakFx5,
    },
    {
        .img = Img_BraceletSealBreakFx6,
        .pal = Pal_BraceletSealBreakFx6,
        .tsa = Tsa_BraceletSealBreakFx6,
    },
    {
        .img = Img_BraceletSealBreakFx7,
        .pal = Pal_BraceletSealBreakFx7,
        .tsa = Tsa_BraceletSealBreakFx7,
    },
    {
        .img = Img_BraceletSealBreakFx8,
        .pal = Pal_BraceletSealBreakFx8,
        .tsa = Tsa_BraceletSealBreakFx8,
    },
};

const u32 gBraceletSealBreakFxChrTable[] =
{
    0x160, 0x260,
};

const u8 gBraceletSealBreakFxPalTable[] =
{
    4, 5,
};

const struct Unk082058B4 gBraceletSealBreakFxSteps[] =
{
    { 0, 5, 0, },
    { 1, 4, 0, },
    { 2, 4, 0, },
    { 3, 3, 0, },
    { 4, 3, 0, },
    { 5, 2, 1, },
    { 6, 2, 1, },
    { 7, 1, 0, },
};

// clang-format on

void BraceletSealBreakFx_PlayAnim(struct MAEffectProc* proc)
{
    if (proc->timer == 0)
    {
        struct Proc8080050* vsync;
        u8 v0 = gBraceletSealBreakFxSteps[proc->frame].unk00;

        Decompress(
            MapAnimfxConf_BraceletSealBreakFx[v0].img,
            (void*) VRAM + gBraceletSealBreakFxChrTable[proc->unk44] * 0x20);

        Decompress(
            MapAnimfxConf_BraceletSealBreakFx[v0].tsa,
            gGenericBuffer);

        AddAttr2dBitMap(
            gBG2TilemapBuffer,
            (u16*) gGenericBuffer,
            0, 0,
            gBraceletSealBreakFxChrTable[proc->unk44] | (gBraceletSealBreakFxPalTable[proc->unk44] << 12));

        BG_EnableSyncByMask(BG2_SYNC_BIT);

        ApplyPalette(
            MapAnimfxConf_BraceletSealBreakFx[v0].pal,
            gBraceletSealBreakFxPalTable[proc->unk44]);

        EnablePaletteSync();

        vsync = (void*) Proc_Find(ProcScr_089A448C);

        vsync->unk29 = 1;
        vsync->unk2A = gBraceletSealBreakFxSteps[proc->frame].unk02;

        if (proc->frame == 0)
            PlaySeSpacial(SONG_13F, proc->xDisplay);

        proc->timer = gBraceletSealBreakFxSteps[proc->frame].unk01;

        if (proc->frame > 6)
        {
            vsync->unk29 = 1;
            vsync->unk2A = 0;

            proc->frame = 0;

            Proc_Break(proc);
        }
        else
        {
            proc->frame++;
            proc->unk44 ^= 1;
        }
    }

    proc->timer--;
}

//! FE8U = 0x08080288
void BraceletSealBreakFx_FadeToWhite(struct MAEffectProc * proc)
{
    int i;

    if (proc->timer == 0)
    {
        u16 amount = proc->frame * 4;

        if (proc->frame <= 7)
        {
            for (i = 1; i < 16; i++)
            {
                u16 r;
                u16 g;
                u16 b;
                u16 color = PAL_BG_COLOR(gBraceletSealBreakFxPalTable[proc->unk44], i);

                r = amount + RED_VALUE(color);

                if (r > 31)
                {
                    r = 31;
                }

                g = amount + GREEN_VALUE(color);

                if (g > 31)
                {
                    g = 31;
                }

                b = amount + BLUE_VALUE(color);

                if (b > 31)
                {
                    b = 31;
                }

                PAL_BG_COLOR(gBraceletSealBreakFxPalTable[proc->unk44], i) = RGB(r, g, b);
            }
        }
        else
        {
            SetBlendTargetA(0, 0, 0, 1, 1);
            SetBlendConfig(BLEND_EFFECT_BRIGHTEN, 16, 16, 16);
            SetDispEnable(0, 0, 0, 1, 1);

            proc->frame = 0;
            Proc_Break(proc);

            return;
        }

        EnablePaletteSync();
        proc->frame++;
    }
    else
    {
        proc->timer--;
    }

    return;
}

//! FE8U = 0x0808038C
void BraceletSealBreakFx_FadeFromWhite(struct MAEffectProc * proc)
{
    if (proc->frame < 130)
    {
        int bldY = (proc->frame * 16) / 130;
        SetBlendConfig(BLEND_EFFECT_BRIGHTEN, 16, 16, 16 - bldY);
    }
    else
    {
        SetBlendConfig(BLEND_EFFECT_BRIGHTEN, 16, 16, 0);
        Proc_Break(proc);
    }

    proc->frame++;

    return;
}

//! FE8U = 0x080803D8
void BraceletSealBreakFx_ClearBgs(void)
{
    BG_Fill(gBG0TilemapBuffer, 0);
    BG_Fill(gBG1TilemapBuffer, 0);
    BG_Fill(gBG2TilemapBuffer, 0);

    BG_EnableSyncByMask(BG0_SYNC_BIT | BG1_SYNC_BIT | BG2_SYNC_BIT);

    return;
}

// clang-format off

struct ProcCmd CONST_DATA ProcScr_089A4434[] =
{
    PROC_SET_END_CB(BraceletSealBreakFx_OnEnd),
    PROC_SLEEP(1),

    PROC_CALL(BraceletSealBreakFx_Init),
    PROC_REPEAT(BraceletSealBreakFx_PlayAnim),
    PROC_REPEAT(BraceletSealBreakFx_FadeToWhite),

    PROC_SLEEP(30),

    PROC_REPEAT(BraceletSealBreakFx_FadeFromWhite),
    PROC_CALL(BraceletSealBreakFx_ClearBgs),

    PROC_SLEEP(60),

    PROC_CALL(MapSpellAnim_CommonEnd),

    PROC_END,
};

// clang-format on

//! FE8U = 0x08080408
void BraceletSealBreakFx_OnHBlank(void)
{
    u16 vcount;

    if (gBraceletSealBreakFxStretchEnabled == 0)
    {
        return;
    }

    vcount = REG_VCOUNT + 1;

    if (vcount == 228)
    {
        vcount = 0;
    }

    if (vcount < DISPLAY_HEIGHT)
    {
        REG_BG2VOFS = ((vcount >> 1) - vcount) & 0x1FF;
    }

    return;
}

struct Proc89A448C
{
    /* 00 */ PROC_HEADER;
    /* 29 */ s8 unk_29;
    /* 2A */ s8 unk_2a;
};

//! FE8U = 0x0808044C
void BraceletSealBreakFxVSync_Loop(struct Proc89A448C * proc)
{
    if (proc->unk_29 != 0)
    {
        gBraceletSealBreakFxStretchEnabled = proc->unk_2a;
        proc->unk_29 = 0;
    }

    return;
}

// clang-format off

struct ProcCmd CONST_DATA ProcScr_089A448C[] =
{
    PROC_YIELD,
    PROC_REPEAT(BraceletSealBreakFxVSync_Loop),

    PROC_END,
};

// clang-format on

//! FE8U = 0x08080474
void StartDemonKingSealFx(ProcPtr parent)
{

    if (parent != NULL)
    {
        Proc_StartBlocking(ProcScr_DemonKingSealFx, parent);
    }
    else
    {
        Proc_Start(ProcScr_DemonKingSealFx, PROC_TREE_3);
    }

    return;
}

// clang-format off

struct MapAnimfxConf CONST_DATA MapAnimfxConf_DemonKingSealFx[] =
{
    {
        .img = Img_DemonKingSealFx1,
        .pal = Pal_DemonKingSealFx1,
        .tsa = Tsa_DemonKingSealFx1,
    },
    {
        .img = Img_DemonKingSealFx2,
        .pal = Pal_DemonKingSealFx2,
        .tsa = Tsa_DemonKingSealFx2,
    },
    {
        .img = Img_DemonKingSealFx3,
        .pal = Pal_DemonKingSealFx3,
        .tsa = Tsa_DemonKingSealFx3,
    },
    {
        .img = Img_DemonKingSealFx4,
        .pal = Pal_DemonKingSealFx4,
        .tsa = Tsa_DemonKingSealFx4,
    },
    {
        .img = Img_DemonKingSealFx5,
        .pal = Pal_DemonKingSealFx5,
        .tsa = Tsa_DemonKingSealFx5,
    },
    {
        .img = Img_DemonKingSealFx6,
        .pal = Pal_DemonKingSealFx6,
        .tsa = Tsa_DemonKingSealFx6,
    },
    {
        .img = Img_DemonKingSealFx7,
        .pal = Pal_DemonKingSealFx7,
        .tsa = Tsa_DemonKingSealFx7,
    },
    {
        .img = Img_DemonKingSealFx8,
        .pal = Pal_DemonKingSealFx8,
        .tsa = Tsa_DemonKingSealFx8,
    },
    {
        .img = Img_DemonKingSealFx9,
        .pal = Pal_DemonKingSealFx9,
        .tsa = Tsa_DemonKingSealFx9,
    },
    {
        .img = Img_DemonKingSealFx10,
        .pal = Pal_DemonKingSealFx10,
        .tsa = Tsa_DemonKingSealFx10,
    },
    {
        .img = Img_DemonKingSealFx11,
        .pal = Pal_DemonKingSealFx11,
        .tsa = Tsa_DemonKingSealFx11,
    },
    {
        .img = Img_DemonKingSealFx12,
        .pal = Pal_DemonKingSealFx12,
        .tsa = Tsa_DemonKingSealFx12,
    },
    {
        .img = Img_DemonKingSealFx13,
        .pal = Pal_DemonKingSealFx13,
        .tsa = Tsa_DemonKingSealFx13,
    },
    {
        .img = Img_DemonKingSealFx14,
        .pal = Pal_DemonKingSealFx14,
        .tsa = Tsa_DemonKingSealFx14,
    },
    {
        .img = Img_DemonKingSealFx15,
        .pal = Pal_DemonKingSealFx15,
        .tsa = Tsa_DemonKingSealFx15,
    },
    {
        .img = Img_DemonKingSealFx16,
        .pal = Pal_DemonKingSealFx16,
        .tsa = Tsa_DemonKingSealFx16,
    },
    {
        .img = Img_DemonKingSealFx17,
        .pal = Pal_DemonKingSealFx17,
        .tsa = Tsa_DemonKingSealFx17,
    },
    {
        .img = Img_DemonKingSealFx18,
        .pal = Pal_DemonKingSealFx18,
        .tsa = Tsa_DemonKingSealFx18,
    },
    {
        .img = Img_DemonKingSealFx19,
        .pal = Pal_DemonKingSealFx19,
        .tsa = Tsa_DemonKingSealFx19,
    },
    {
        .img = Img_DemonKingSealFx20,
        .pal = Pal_DemonKingSealFx20,
        .tsa = Tsa_DemonKingSealFx20,
    },
    {
        .img = Img_DemonKingSealFx21,
        .pal = Pal_DemonKingSealFx21,
        .tsa = Tsa_DemonKingSealFx21,
    },
    {
        .img = Img_DemonKingSealFx22,
        .pal = Pal_DemonKingSealFx22,
        .tsa = Tsa_DemonKingSealFx22,
    },
    {
        .img = Img_DemonKingSealFx23,
        .pal = Pal_DemonKingSealFx23,
        .tsa = Tsa_DemonKingSealFx23,
    },
    {
        .img = Img_DemonKingSealFx24,
        .pal = Pal_DemonKingSealFx24,
        .tsa = Tsa_DemonKingSealFx24,
    },
    {
        .img = Img_DemonKingSealFx25,
        .pal = Pal_DemonKingSealFx25,
        .tsa = Tsa_DemonKingSealFx25,
    },
    {
        .img = Img_DemonKingSealFx26,
        .pal = Pal_DemonKingSealFx26,
        .tsa = Tsa_DemonKingSealFx26,
    },
};

const int gDemonKingSealFxChrTable[] =
{
    0x160, 0x260,
};

const u8 gDemonKingSealFxPalTable[] =
{
    4, 5,
};

// clang-format on

//! FE8U = 0x08080498
void DemonKingSealFx_PutFrame(int frame, int unk44)
{
    Decompress(MapAnimfxConf_DemonKingSealFx[frame].img, (void *)(VRAM + gDemonKingSealFxChrTable[unk44] * 0x20));
    Decompress(MapAnimfxConf_DemonKingSealFx[frame].tsa, gGenericBuffer);
    AddAttr2dBitMap(
        gBG2TilemapBuffer, (u16 *)gGenericBuffer, 0, 0, gDemonKingSealFxChrTable[unk44] | gDemonKingSealFxPalTable[unk44] << 12);

    BG_EnableSyncByMask(BG2_SYNC_BIT);

    ApplyPalette(MapAnimfxConf_DemonKingSealFx[frame].pal, gDemonKingSealFxPalTable[unk44]);
    EnablePaletteSync();

    return;
}

//! FE8U = 0x08080530
void DemonKingSealFx_PutFrameNoPal(int frame, int unk44)
{
    Decompress(MapAnimfxConf_DemonKingSealFx[frame].img, (void *)(VRAM + gDemonKingSealFxChrTable[unk44] * 0x20));
    Decompress(MapAnimfxConf_DemonKingSealFx[frame].tsa, gGenericBuffer);
    AddAttr2dBitMap(
        gBG2TilemapBuffer, (u16 *)gGenericBuffer, 0, 0, gDemonKingSealFxChrTable[unk44] | gDemonKingSealFxPalTable[unk44] << 12);

    BG_EnableSyncByMask(BG2_SYNC_BIT);

    return;
}

//! FE8U = 0x080805AC
void DemonKingSealFx_PutLightenedFramePal(int idxA, int idxB, int val)
{
    s32 i;

    const u16 * src = MapAnimfxConf_DemonKingSealFx[idxA].pal;
    u16 * dst = PAL_BG(gDemonKingSealFxPalTable[idxB]);

    for (i = 0; i < 16; i++)
    {
        u16 r;
        u16 g;
        u16 b;
        u16 color = src[i];

        r = RED_VALUE(color);
        r += (31 - r) * val / 32;

        g = GREEN_VALUE(color);
        g += (31 - g) * val / 32;

        b = BLUE_VALUE(color);
        b += (31 - b) * val / 32;

        dst[i] = RGB(r, g, b);
    }

    EnablePaletteSync();

    return;
}

struct MAEffectSummonProc
{
    /* 00 */ PROC_HEADER;
    /* 29 */ s8 unk_29;
    /* 2A */ s16 unk_2a;
    /* 2C */ s16 unk_2c;
    /* 2E */ s16 unk_2e;
    /* 30 */ s16 unk_30;
    /* 32 */ s16 unk_32;
    /* 34 */ s16 unk_34;
    /* 36 */ s16 unk_36;
    /* 38 */ u8 unk_38;
    /* 3A */ STRUCT_PAD(0x3a, 0x40);
    /* 40 */ u16 unk_40;
    /* 42 */ u16 unk_42;
    /* 44 */ u16 unk_44;
    /* 46 */ u16 unk_46;
    /* 48 */ s16 unk_48;
    /* 4A */ s16 unk_4a;
    /* 4C */ STRUCT_PAD(0x4c, 0x50);
    /* 50 */ struct MAEffectSummonProc * unk_50;
};

//! FE8U = 0x08080654
void DemonKingSealFx_OnEnd(struct MAEffectProc * proc)
{
    Proc_End((void *)proc->img);
    return;
}

//! FE8U = 0x08080660
void DemonKingSealFx_Init(struct MAEffectProc * proc)
{
    gLCDControlBuffer.bg0cnt.priority = 0;
    gLCDControlBuffer.bg1cnt.priority = 0;
    gLCDControlBuffer.bg2cnt.priority = 0;
    gLCDControlBuffer.bg3cnt.priority = 2;

    SetDispEnable(0, 0, 1, 1, 1);

    gLCDControlBuffer.wincnt.win0_enableBlend = 0;
    gLCDControlBuffer.wincnt.win1_enableBlend = 0;

    SetBlendTargetA(0, 0, 1, 0, 0);
    SetBlendBackdropA(0);

    SetBlendTargetB(0, 0, 0, 1, 1);
    SetBlendBackdropB(1);

    SetBlendAlpha(16, 16);

    BG_SetPosition(BG_2, 8, 8);

    proc->img = Proc_Start(ProcScr_DemonKingSealShake, proc);
    proc->frame = 0;
    proc->timer = 0;
    proc->unk44 = 0;
    proc->unk46 = 0;

    return;
}

// clang-format off

const u8 gDemonKingSealFxIntroFrames[] =
{
    0, 1, 2, 3,
};

// clang-format on

//! FE8U = 0x08080730
void DemonKingSealFx_PlayIntro(struct MAEffectProc * proc)
{
    if (proc->timer == 0)
    {
        DemonKingSealFx_PutFrame(gDemonKingSealFxIntroFrames[proc->frame], proc->unk44);

        if (proc->frame == 0)
        {
            PlaySeSpacial(SONG_141, proc->xDisplay);
        }

        proc->frame++;
        proc->timer = 5;

        if (proc->frame > 3)
        {
            proc->frame = 0;
            proc->unk46 = 0;

            proc->unk48 = 0;
            proc->frame_idx = 0;

            proc->xDisplay = gBmSt.camera.x;
            proc->yDisplay = gBmSt.camera.y;

            Proc_Break(proc);
        }

        proc->unk44 ^= 1;
    }

    proc->timer--;

    return;
}

// clang-format off

const u8 gDemonKingSealFxShakeFrames[] =
{
    4, 5,
};

// clang-format on

//! FE8U = 0x080807C8
void DemonKingSealFx_PlayShakeLoop(struct MAEffectSummonProc * proc)
{
    if (proc->unk_46 == 5)
    {
        proc->unk_50->unk_29 = 1;
        proc->unk_50->unk_2a = 0;

        proc->unk_50->unk_34 = 2;
    }
    else if (proc->unk_46 == 8)
    {
        proc->unk_50->unk_29 = 1;
        proc->unk_50->unk_2a = 1;

        proc->unk_50->unk_34 = 3;
    }
    else if (proc->unk_46 == 0xb)
    {
        proc->unk_50->unk_29 = 1;
        proc->unk_50->unk_2a = 2;

        proc->unk_50->unk_34 = 4;
    }

    if (proc->unk_42 == 0)
    {
        DemonKingSealFx_PutFrame(gDemonKingSealFxShakeFrames[proc->unk_40], proc->unk_44);

        proc->unk_40++;
        proc->unk_42 = 4;

        if (proc->unk_46 > 21)
        {
            if (proc->unk_40 > 1)
            {
                proc->unk_40 = 0;
                proc->unk_46 = 0;
                Proc_Break(proc);
            }
        }
        else if (proc->unk_40 > 1)
        {
            proc->unk_40 = 0;
            proc->unk_46++;
        }

        proc->unk_44 ^= 1;
    }

    proc->unk_42--;

    return;
}

// clang-format off

const u8 gDemonKingSealFxMainFrames[] =
{
     6,  7,  8,  9,
    10, 11, 12, 13,
    14, 15, 16, 17,
};

// clang-format on

//! FE8U = 0x08080890
void DemonKingSealFx_PlayMain(struct MAEffectSummonProc * proc)
{
    if (proc->unk_42 == 0)
    {
        DemonKingSealFx_PutFrame(gDemonKingSealFxMainFrames[proc->unk_40], proc->unk_44);

        proc->unk_42 = 3;

        if (proc->unk_40 > 10)
        {
            proc->unk_40 = 0;
            proc->unk_46 = 0;

            proc->unk_42--;

            Proc_Break(proc);

            return;
        }

        proc->unk_40++;
        proc->unk_44 ^= 1;
    }

    proc->unk_42--;

    return;
}

//! FE8U = 0x08080900
void DemonKingSealFx_FadeToWhite(struct MAEffectSummonProc * proc)
{
    int i;

    if (proc->unk_42 == 0)
    {
        u16 amount = proc->unk_40 << 2;

        if (proc->unk_40 < 8)
        {
            for (i = 1; i < 16; i++)
            {
                u16 r;
                u16 g;
                u16 b;

                u16 color = PAL_BG_COLOR(gDemonKingSealFxPalTable[proc->unk_44], i);

                r = amount + RED_VALUE(color);

                if (r > 31)
                {
                    r = 31;
                }

                g = amount + GREEN_VALUE(color);

                if (g > 31)
                {
                    g = 31;
                }

                b = amount + BLUE_VALUE(color);

                if (b > 31)
                {
                    b = 31;
                }

                PAL_BG_COLOR(gDemonKingSealFxPalTable[proc->unk_44], i) = RGB(r, g, b);
            }

            EnablePaletteSync();

            proc->unk_40++;
        }
        else
        {
            proc->unk_40 = 0;
            proc->unk_46 = 0;
            proc->unk_48 = 0;
            proc->unk_4a = -1;

            Proc_Break(proc);

            return;
        }
    }
    else
    {
        proc->unk_42--;
    }

    return;
}

// clang-format off

const u8 gDemonKingSealFxFadeFrames[] =
{
    18, 19, 20, 21,
};

// clang-format on

//! FE8U = 0x080809D8
void DemonKingSealFx_PlayFadeLoop(struct MAEffectSummonProc * proc)
{
    s16 tmp = 0x95;

    if (proc->unk_46 == 2)
    {
        proc->unk_50->unk_29 = 1;
        proc->unk_50->unk_2a = 1;

        proc->unk_50->unk_34 = 3;
    }
    else if (proc->unk_46 == 3)
    {
        proc->unk_50->unk_29 = 1;
        proc->unk_50->unk_2a = 1;

        proc->unk_50->unk_34 = 4;
    }
    else if (proc->unk_46 == 4)
    {
        proc->unk_50->unk_29 = 1;
        proc->unk_50->unk_2a = 0;

        proc->unk_50->unk_34 = 3;
    }
    else if (proc->unk_46 == 5)
    {
        proc->unk_50->unk_29 = 1;
        proc->unk_50->unk_2a = 0;

        proc->unk_50->unk_34 = 4;
    }
    else if (proc->unk_46 == 6)
    {
        proc->unk_50->unk_29 = 0;
        proc->unk_50->unk_2a = 0;
    }

    if (proc->unk_42 == 0)
    {
        DemonKingSealFx_PutFrameNoPal(gDemonKingSealFxFadeFrames[proc->unk_40], proc->unk_44);

        proc->unk_4a = proc->unk_40;
        proc->unk_40++;

        proc->unk_42 = 4;

        if (proc->unk_46 >= 11)
        {
            if (proc->unk_40 > 3)
            {
                proc->unk_40 = 0;
                proc->unk_46 = 0;

                Proc_Break(proc);
            }
        }
        else if (proc->unk_40 > 3)
        {
            proc->unk_40 = 0;
            proc->unk_46++;
        }

        proc->unk_44 ^= 1;
    }

    proc->unk_42--;

    if (proc->unk_48 <= tmp)
    {
        proc->unk_48++;
    }

    if (proc->unk_4a >= 0)
    {
        DemonKingSealFx_PutLightenedFramePal(gDemonKingSealFxFadeFrames[proc->unk_4a], proc->unk_44, 0x20 - ((proc->unk_48 * 0x20) / 0x96));
    }

    return;
}

const u8 gDemonKingSealFxOutroFrames[] =
{
    22, 23, 24, 25,
};

//! FE8U = 0x08080B18
void DemonKingSealFx_PlayOutro(struct MAEffectSummonProc * proc)
{
    if (proc->unk_42 == 0)
    {
        DemonKingSealFx_PutFrame(gDemonKingSealFxOutroFrames[proc->unk_40], proc->unk_44);

        proc->unk_40++;
        proc->unk_42 = 5;

        if (proc->unk_40 > 3)
        {
            proc->unk_40 = 0;
            proc->unk_46 = 0;
            Proc_Break(proc);
        }

        proc->unk_44 ^= 1;
    }

    proc->unk_42--;

    return;
}

//! FE8U = 0x08080B84
void DemonKingSealFx_Clear(void)
{
    BG_SetPosition(BG_2, 0, 0);
    BG_Fill(gBG2TilemapBuffer, 0);

    BG_EnableSyncByMask(BG0_SYNC_BIT | BG1_SYNC_BIT | BG2_SYNC_BIT);

    return;
}

// clang-format off

struct ProcCmd CONST_DATA ProcScr_DemonKingSealFx[] =
{
    PROC_SET_END_CB(DemonKingSealFx_OnEnd),
    PROC_SLEEP(1),

    PROC_CALL(DemonKingSealFx_Init),
    PROC_REPEAT(DemonKingSealFx_PlayIntro),
    PROC_REPEAT(DemonKingSealFx_PlayShakeLoop),
    PROC_REPEAT(DemonKingSealFx_PlayMain),
    PROC_REPEAT(DemonKingSealFx_FadeToWhite),
    PROC_REPEAT(DemonKingSealFx_PlayFadeLoop),
    PROC_REPEAT(DemonKingSealFx_PlayOutro),
    PROC_CALL(DemonKingSealFx_Clear),

    PROC_SLEEP(60),

    PROC_CALL(MapSpellAnim_CommonEnd),

    PROC_END,
};

// clang-format on

//! FE8U = 0x08080BA8
void DemonKingSealShake_Init(struct MAEffectSummonProc * proc)
{
    proc->unk_29 = 0;
    proc->unk_2a = 0;

    proc->unk_2c = gBmSt.camera.x;
    proc->unk_2e = gBmSt.camera.y;

    proc->unk_30 = 0;
    proc->unk_32 = 0;

    proc->unk_34 = 4;
    proc->unk_36 = 0;

    proc->unk_38 = 0;

    return;
}

// clang-format off

struct Pair
{
    s8 x;
    s8 y;
#ifdef PC_PORT
} AGBCC_STRUCT;
#else
};
#endif

const struct Pair gDemonKingSealShakeOffsetsWeak[] =
{
    { -1, +0, },
    { +0, +0, },
    { +1, +0, },
    { +0, +0, },
    { +0, -1, },
    { +0, +0, },
    { +0, +1, },
    { +0, +0, },
};

const struct Pair gDemonKingSealShakeOffsetsMedium[] =
{
    { -1, +0, },
    { +1, +0, },
    { +0, -1, },
    { +0, +1, },
    { -1, +0, },
    { +1, +0, },
    { +0, -1, },
    { +0, +1, },
};

const struct Pair gDemonKingSealShakeOffsetsStrong[] =
{
    { -2, +0, },
    { +1, +0, },
    { +0, -2, },
    { +0, +1, },
    { -1, +0, },
    { +2, +0, },
    { +0, -1, },
    { +0, +2, },
};

// clang-format on

//! FE8U = 0x08080BD8
void DemonKingSealShake_Loop(struct MAEffectSummonProc * proc)
{
    int tmp = 8;

    if (proc->unk_29 == 0)
    {
        gBmSt.camera.x = proc->unk_2c;
        gBmSt.camera.y = proc->unk_2e;

        BG_SetPosition(BG_2, 8, 8);

        return;
    }

    if (proc->unk_36 > 0)
    {
        proc->unk_36--;
        return;
    }

    proc->unk_36 = proc->unk_34;

    switch (proc->unk_2a)
    {
        case 0:
            proc->unk_30 = gDemonKingSealShakeOffsetsWeak[proc->unk_38].x;
            proc->unk_32 = gDemonKingSealShakeOffsetsWeak[proc->unk_38].y;

            break;

        case 1:
            proc->unk_30 = gDemonKingSealShakeOffsetsMedium[proc->unk_38].x;
            proc->unk_32 = gDemonKingSealShakeOffsetsMedium[proc->unk_38].y;

            break;

        case 2:
            proc->unk_30 = gDemonKingSealShakeOffsetsStrong[proc->unk_38].x;
            proc->unk_32 = gDemonKingSealShakeOffsetsStrong[proc->unk_38].y;

            break;

        default:
            proc->unk_30 = proc->unk_2a * gDemonKingSealShakeOffsetsMedium[proc->unk_38].x;
            proc->unk_32 = proc->unk_2a * gDemonKingSealShakeOffsetsMedium[proc->unk_38].y;

            break;
    }

    gBmSt.camera.x = proc->unk_2c + proc->unk_30;
    gBmSt.camera.y = proc->unk_2e + proc->unk_32;

    BG_SetPosition(BG_2, proc->unk_30 + 8, proc->unk_32 + 8);

    proc->unk_38++;

    if (proc->unk_38 >= tmp)
    {
        proc->unk_38 = 0;
    }

    return;
}

// clang-format off

struct ProcCmd CONST_DATA ProcScr_DemonKingSealShake[] =
{
    PROC_CALL(DemonKingSealShake_Init),
    PROC_YIELD,

    PROC_REPEAT(DemonKingSealShake_Loop),

    PROC_END,
};

// clang-format on

//! FE8U = 0x0807AD08
void New6C_SummonGfx(ProcPtr parent, int xBase, int yBase)
{
    struct MAEffectProc * proc = Proc_StartBlocking(ProcScr_SummonGfx, parent);

    proc->xDisplay = ((xBase - (gBmSt.camera.x >> 4)) * 2 + 1) * 8;
    proc->yDisplay = ((yBase - (gBmSt.camera.y >> 4)) * 2 + 1) * 8;

    return;
}

//! FE8U = 0x08080D6C
void SummonGfx_Init(struct MAEffectProc * proc)
{
    SetDefaultMapAnimScreenConf();

    BG_SetPosition(BG_2, 0, 0);

    Decompress(Img_SummonFx, (void *)(0x06002C00 + GetBackgroundTileDataOffset(BG_2)));
    ApplyPalette(Pal_SummonFx, 4);
    SetBlendAlpha(16, 16);

    proc->frame = 0;
    proc->timer = 0;

    EnablePaletteSync();

    return;
}

// clang-format off

u8 * CONST_DATA TsaSet_SummonFx[] =
{
    Tsa_089E7E0C,
    Tsa_089E7E2C,
    Tsa_089E7E50,
    Tsa_089E7E78,
    Tsa_089E7EA0,
    Tsa_089E7ECC,
    Tsa_089E7EFC,
    Tsa_089E7F2C,
    Tsa_089E7F5C,
    Tsa_089E7F8C,
    Tsa_089E7FBC,
    Tsa_089E7FFC,
    Tsa_089E8040,
    Tsa_089E8090,
    Tsa_089E80EC,
    Tsa_089E8148,
    Tsa_089E81A4,
    Tsa_089E8200,
};

// clang-format on

//! FE8U = 0x08080DCC
void SummonGfx_Loop(struct MAEffectProc * proc)
{
    s32 exit;

    if (proc->timer == 0)
    {
        if (proc->frame == 0)
        {
            PlaySeSpacial(SONG_3BF, proc->xDisplay);
            exit = 0;
        }
        else if (proc->frame == 12)
        {
            RefreshEntityBmMaps();
            RefreshUnitSprites();
            exit = 0;
        }
        else if (proc->frame > 17)
        {
            Proc_Break(proc);
            exit = 1;
        }
        else
        {
            exit = 0;
        }

        if (exit)
        {
            return;
        }

        Decompress(TsaSet_SummonFx[proc->frame], gGenericBuffer);

        AddAttr2dBitMap(
            gBG2TilemapBuffer, (u16 *)gGenericBuffer, proc->xDisplay / 8 - 4, proc->yDisplay / 8 - 6, 0x00004160);

        BG_EnableSyncByMask(BG2_SYNC_BIT);

        proc->frame++;
        proc->timer = 4;
    }

    proc->timer--;

    return;
}

//! FE8U = 0x08080E84
void SummonGfx_ClearBg2(void)
{
    BG_Fill(gBG2TilemapBuffer, 0);
    BG_EnableSyncByMask(BG2_SYNC_BIT);
    return;
}

// clang-format off

struct ProcCmd CONST_DATA ProcScr_SummonGfx[] =
{
    PROC_SLEEP(1),

    PROC_CALL(SummonGfx_Init),
    PROC_REPEAT(SummonGfx_Loop),
    PROC_CALL(SummonGfx_ClearBg2),

    PROC_CALL(MapSpellAnim_CommonEnd),

    PROC_END,
};

// clang-format on

//! FE8U = 0x08080E9C
void StartSummonUnitFx(ProcPtr parent, struct Unit * unit)
{
    struct MAEffectProc * proc = Proc_StartBlocking(ProcScr_SummonUnitFx, parent);

    proc->unit = unit;
    proc->xDisplay = (((unit->xPos - (gBmSt.camera.x >> 4)) * 2) + 1) * 8;
    proc->yDisplay = (((unit->yPos - (gBmSt.camera.y >> 4)) * 2) + 1) * 8;

    return;
}

//! FE8U = 0x08080EE4
void SummonUnitFx_Init(struct MAEffectProc * proc)
{
    SetDefaultMapAnimScreenConf();

    BG_SetPosition(BG_2, 0, 0);

    Decompress(Img_SummonFx, (void *)(0x06002C00 + GetBackgroundTileDataOffset(BG_2)));
    ApplyPalette(Pal_SummonFx, 4);
    SetBlendAlpha(16, 16);

    proc->frame = 0;
    proc->timer = 0;

    EnablePaletteSync();

    return;
}

//! FE8U = 0x08080F44
void SummonUnitFx_Loop(struct MAEffectProc * proc)
{
    s32 exit;

    if (proc->timer == 0)
    {
        if (proc->frame == 0)
        {
            PlaySeSpacial(SONG_3BF, proc->xDisplay);
            exit = 0;
        }
        else if (proc->frame == 12)
        {
            proc->unit->state &= ~US_HIDDEN;
            RefreshEntityBmMaps();
            RefreshUnitSprites();
            exit = 0;
        }
        else if (proc->frame > 17)
        {
            Proc_Break(proc);
            exit = 1;
        }
        else
        {
            exit = 0;
        }

        if (exit)
        {
            return;
        }

        Decompress(TsaSet_SummonFx[proc->frame], gGenericBuffer);

        AddAttr2dBitMap(
            gBG2TilemapBuffer, (u16 *)gGenericBuffer, proc->xDisplay / 8 - 4, proc->yDisplay / 8 - 6, 0x00004160);

        BG_EnableSyncByMask(BG2_SYNC_BIT);

        proc->frame++;
        proc->timer = 4;
    }

    proc->timer--;

    return;
}

//! FE8U = 0x08081008
void SummonUnitFx_ClearBg2(void)
{
    BG_Fill(gBG2TilemapBuffer, 0);
    BG_EnableSyncByMask(BG2_SYNC_BIT);
    return;
}

// clang-format off

struct ProcCmd CONST_DATA ProcScr_SummonUnitFx[] =
{
    PROC_SLEEP(1),

    PROC_CALL(SummonUnitFx_Init),
    PROC_REPEAT(SummonUnitFx_Loop),
    PROC_CALL(SummonUnitFx_ClearBg2),

    PROC_CALL(MapSpellAnim_CommonEnd),

    PROC_END,
};

// clang-format on

//! FE8U = 0x08081020
void StartGlowingCross(ProcPtr parent, struct Unit * unit)
{
    struct MAEffectProc * proc = Proc_Start(ProcScr_GlowingCross, parent);

    proc->unit = unit;
    proc->xDisplay = (((unit->xPos - (gBmSt.camera.x >> 4)) * 2) + 1) * 8;
    proc->yDisplay = (((unit->yPos - (gBmSt.camera.y >> 4)) * 2) + 1) * 8;

    return;
}

//! FE8U = 0x08081068
void RemoveGlowingCrossDirectly(void)
{
    Proc_EndEach(ProcScr_GlowingCross);
    return;
}

//! FE8U = 0x08081078
void GlowingCross_Init(struct MAEffectProc * proc)
{
    SetDefaultMapAnimScreenConf();

    gLCDControlBuffer.bg0cnt.priority = 0;
    gLCDControlBuffer.bg1cnt.priority = 1;
    gLCDControlBuffer.bg2cnt.priority = 3;
    gLCDControlBuffer.bg3cnt.priority = 3;

    BG_SetPosition(BG_2, 0, 0);

    Decompress(Img_SummonFx, (void *)(0x06002C00 + GetBackgroundTileDataOffset(BG_2)));
    ApplyPalette(Pal_SummonFx, 4);
    SetBlendAlpha(16, 16);

    proc->frame = 0;
    proc->timer = 0;

    EnablePaletteSync();

    return;
}

//! FE8U = 0x08081100
void GlowingCross_Loop(struct MAEffectProc * proc)
{
    s32 exit;

    if (proc->timer == 0)
    {
        if (proc->frame == 0)
        {
            PlaySeSpacial(SONG_3BF, proc->xDisplay);
            exit = FALSE;
        }
        else if (proc->frame > 9)
        {
            Proc_Break(proc);
            exit = TRUE;
        }
        else
        {
            exit = FALSE;
        }

        if (exit)
        {
            return;
        }

        Decompress(TsaSet_SummonFx[proc->frame], gGenericBuffer);
        AddAttr2dBitMap(
            gBG2TilemapBuffer, (u16 *)gGenericBuffer, proc->xDisplay / 8 - 4, proc->yDisplay / 8 - 6, 0x00004160);
        BG_EnableSyncByMask(BG2_SYNC_BIT);

        proc->frame++;
        proc->timer = 4;
    }

    proc->timer--;

    return;
}

//! FE8U = 0x080811AC
void GlowingCross_OnEnd(void)
{
    DeleteAllPaletteAnimator();
    SetWinEnable(0, 0, 0);
    return;
}


// clang-format off

struct ProcCmd CONST_DATA ProcScr_GlowingCross[] =
{
    PROC_SLEEP(1),
    PROC_SET_END_CB(GlowingCross_OnEnd),

    PROC_CALL(GlowingCross_Init),
    PROC_REPEAT(GlowingCross_Loop),

    PROC_END,
};

// clang-format on

//! FE8U = 0x080811D0
void RemoveGlowingCrossDirectlyWithAnim(ProcPtr parent, int timer)
{
    struct MAEffectProc * proc = Proc_StartBlocking(ProcScr_GlowCrossExit, parent);
    proc->timer = timer;
    return;
}

//! FE8U = 0x080811EC
void GlowCrossExit_Init(struct MAEffectProc * proc)
{
    SetBlendAlpha(16, 16);
    proc->frame = 0;
    return;
}

//! FE8U = 0x08081208
void GlowCrossExit_Loop(struct MAEffectProc * proc)
{
    proc->frame++;

    if (proc->frame < proc->timer)
    {
        SetBlendAlpha(16 - (proc->frame * 16) / proc->timer, 16);
    }
    else
    {
        SetBlendAlpha(0, 16);
        Proc_Break(proc);
    }

    return;
}

//! FE8U = 0x0808125C
void nullsub_58(void)
{
    return;
}

// clang-format off

struct ProcCmd CONST_DATA ProcScr_GlowCrossExit[] =
{
    PROC_SLEEP(1),

    PROC_CALL(GlowCrossExit_Init),
    PROC_REPEAT(GlowCrossExit_Loop),
    PROC_CALL(nullsub_58),

    PROC_CALL(MapSpellAnim_CommonEnd),

    PROC_END,
};

const u8 gRouteSplitMenuItemName[] =
{
    0, 0, 0, 0,
    0x50, 0, 0, 0,
    0x8A, 0, 0, 0,
};

// clang-format on
