#include "global.h"

#include "face.h"
#include "bmlib.h"
#include "ctc.h"
#include "hardware.h"
#include "fontgrp.h"
#include "soundwrapper.h"
#include "m4a.h"
#include "bmio.h"
#include "prepscreen.h"

#include "sio_core.h"
#include "sio.h"

#include "constants/songs.h"

/**
 * Battle result screen for the Link Arena.
 * Not to be confused with "sio_result.c", which refers to the
 * "Battle Data" UI.
 */

// clang-format off

u16 const * CONST_DATA SpriteArray_LAPostBattlePlayerNumbers[] =
{
    Sprite_LAPostBattle_P1,
    Sprite_LAPostBattle_P2,
    Sprite_LAPostBattle_P3,
    Sprite_LAPostBattle_P4,
};

u16 const * CONST_DATA SpriteArray_LAPostBattlePlacements[] =
{
    Sprite_LAPostBattle_1st,
    Sprite_LAPostBattle_2nd,
    Sprite_LAPostBattle_3rd,
    Sprite_LAPostBattle_4th,
};

s16 const gSioPostBattleRowYLut[5][4] =
{
    { -1, -1, -1, -1, },

    { -1, -1, -1, -1, },

    { 7, 13, -1, -1, },

    { 5, 10, 15, -1, },

    { 2, 7, 12, 17, },
};

u8 const gSioPlayerMaskByTeamCount[3] = { 3, 7, 15 };

// clang-format on

//! FE8U = 0x08045208
void SioPostBattleSprites_Init(struct SioPostBattleSpritesProc * proc)
{
    UnpackFaceChibiSprGraphics(proc->fid, proc->oam2 * 8 + 0x180, proc->oam2 + 10);
    proc->timer = 0;
    proc->y = -38;
    return;
}

//! FE8U = 0x08045234
void SioPostBattleSprites_Loop_DrawSlideIn(struct SioPostBattleSpritesProc * proc)
{
    struct SioPostBattleProc * unk2C = proc->unk_2c;

    if (unk2C->unk_64 == 0)
    {
        if (proc->timer > 32)
        {
            Proc_Break(proc);
        }
    }

    if (unk2C->unk_64 != 0)
    {
        proc->y++;
    }

    proc->delayMaybe--;

    if (proc->delayMaybe < 0)
    {
        proc->delayMaybe = 0;
    }

    if (proc->delayMaybe == 0)
    {
        if (proc->timer <= 32)
        {
            proc->x = Interpolate(INTERPOLATE_RSQUARE, -80, 0, proc->timer, 32);
        }

        proc->timer++;

        PutSprite(4, proc->x, proc->y - 16, SpriteArray_LAPostBattlePlacements[proc->ranking], 0);
        PutSprite(4, proc->x + 72, proc->y - 6, SpriteArray_LAPostBattlePlayerNumbers[proc->oam2], 0);
        PutSprite(4, proc->x + 96, proc->y + 8, Sprite_080D9DE4, 0);
        PutSprite(4, proc->x + 64, proc->y + 8, Sprite_080D9E06, proc->ranking * 4 + 0x50);
        PutSprite(4, -proc->x + 112, proc->y - 8, Sprite_LinkArena_NameBanner, OAM2_PAL(proc->oam2) + 0x400);
        PutSprite(4, -proc->x + 124, proc->y, Sprite_080D9DF2, proc->oam2 == 3 ? 0x40 : proc->oam2 * 9);
        PutSprite(
            5, -proc->x + 208, proc->y - 8, Sprite_LAPostBattle_ChibiFace, OAM2_PAL(proc->oam2 + 10) + 0x180 + (proc->oam2 << 3));
    }

    return;
}

//! FE8U = 0x0804538C
void SioPostBattleSprites_Loop_DrawStatic(struct SioPostBattleSpritesProc * proc)
{
    PutSprite(4, proc->x, proc->y - 16, SpriteArray_LAPostBattlePlacements[proc->ranking], 0);
    PutSprite(4, proc->x + 72, proc->y - 6, SpriteArray_LAPostBattlePlayerNumbers[proc->oam2], 0);
    PutSprite(4, proc->x + 96, proc->y + 8, Sprite_080D9DE4, 0);
    PutSprite(4, proc->x + 64, proc->y + 8, Sprite_080D9E06, proc->ranking * 4 + 0x50);
    PutSprite(4, -proc->x + 112, proc->y - 8, Sprite_LinkArena_NameBanner, OAM2_PAL(proc->oam2) + 0x400);
    PutSprite(4, -proc->x + 124, proc->y, Sprite_080D9DF2, proc->oam2 == 3 ? 0x40 : proc->oam2 * 9);
    PutSprite(5, -proc->x + 208, proc->y - 8, Sprite_LAPostBattle_ChibiFace, OAM2_PAL(proc->oam2 + 10) + 0x180 + (proc->oam2 << 3));

    return;
}

// clang-format off

struct ProcCmd CONST_DATA ProcScr_LinkArenaPostBattle_DrawSprites[] =
{
    PROC_YIELD,

    PROC_CALL(SioPostBattleSprites_Init),

    PROC_REPEAT(SioPostBattleSprites_Loop_DrawSlideIn),
    PROC_REPEAT(SioPostBattleSprites_Loop_DrawStatic),

    PROC_END,
};

// clang-format on

//! FE8U = 0x08045494
ProcPtr StartDrawLinkArenaRankSprites(struct SioPostBattleProc * parent, int delayMaybe, u16 fid, u8 oam2, u8 ranking)
{
    struct SioPostBattleSpritesProc * proc = Proc_Start(ProcScr_LinkArenaPostBattle_DrawSprites, parent);

    proc->unk_2c = parent;
    proc->delayMaybe = delayMaybe;
    proc->fid = fid;
    proc->ranking = ranking;
    proc->oam2 = oam2;

    return proc;
}

//! FE8U = 0x080454E4
void SioPostBattle_DrawRankingRowBg(struct SioPostBattleProc * proc)
{
    int i;

    int unk_40 = proc->unk_40;
    u16 * tm = gBG2TilemapBuffer;

    if (proc->playerId == proc->unk_44[(proc->unk_41 - 1)].playerId)
    {
        CallARM_FillTileRect(
            tm + TILEMAP_INDEX(0, gSioPostBattleRowYLut[unk_40][(proc->unk_41 - 1)]), Tsa_SioPostBattleOwnRow, 0x2078);
    }
    else
    {
        for (i = 0; i < 0x60; i++)
        {
            tm[TILEMAP_INDEX(i, gSioPostBattleRowYLut[unk_40][(proc->unk_41 - 1)])] = 0x1034;
        }
    }

    return;
}

//! FE8U = 0x0804556C
void SioPostBattle_DrawNamesAndPoints(struct SioPostBattleProc * proc)
{
    int i;

    int unk_40 = proc->unk_40;
    SetTextFont(&Font_Sio_02000C60);

    for (i = 0; i < unk_40; i++)
    {
        char * str = gUnk_Sio_0203DAC5[i];

        int len = GetStringTextLen(str);
        len = (72 - len) / 2;

        if (i < 3)
        {
            Text_InsertDrawString(Texts_0203DB14, 72 * i + len, 0, str);
        }
        else
        {
            Text_InsertDrawString(gUnk_Sio_0203DB1C, len, 0, str);
        }

        SioDrawNumber(gUnk_Sio_0203DB1C, i * 32 + 152, 2, proc->unk_44[i].points);
    }

    return;
}

extern struct ProcCmd ProcScr_SioPostBattle_PlayMusic[];

//! FE8U = 0x08045610
ProcPtr SioPostBattle_StartMusicProc(struct SioPostBattleProc * parent)
{
    struct SioPostBattleMusicProc * proc = Proc_Start(ProcScr_SioPostBattle_PlayMusic, parent);

    if (parent->playerId == parent->unk_44[0].playerId)
    {
        proc->isPlayerWinner = 1;
    }
    else
    {
        proc->isPlayerWinner = 0;
    }

    return proc;
}

//! FE8U = 0x08045640
void SioPostBattle_Init(struct SioPostBattleProc * proc)
{
    int i;

    ClearSioBG();
    InitSioBG();

    Decompress(Img_TacticianSelObj, (void *)(0x06014800));
    Decompress(Img_LinkArenaPlayerBanners, (void *)(0x06016000));
    Decompress(Img_LinkArenaPlacementRanks, (void *)(0x06016800));

    ApplyPalettes(Pal_TacticianSelObj, 0x13, 4);
    ApplyPalette(Pal_LinkArenaPlacementRanks, 0x17);

    Decompress(Img_LinkArenaActiveBannerFx, (void *)(0x06000F00));
    ApplyPalette(Pal_LinkArenaActiveBannerFx, 2);

    Decompress(Img_LinkArenaPostBattleBg, (void *)(VRAM + GetBackgroundTileDataOffset(3)));
    ApplyPalettes(Pal_LinkArenaPostBattleBg, 10, 4);
    Decompress(Tsa_LinkArenaPostBattleBg, gGenericBuffer);
    CallARM_FillTileRect(gBG3TilemapBuffer, gGenericBuffer, 0xa000);

    InitSpriteTextFont(&Font_Sio_02000C60, (void *)(0x06012000), 0xe);
    ApplyPalette(Pal_Text, 0x1E);
    SetTextFontGlyphs(TEXT_GLYPHS_SYSTEM);
    ResetTextFont();

    for (i = 0; i < 2; i++)
    {
        InitSpriteText(&Texts_0203DB14[i]);
        SpriteText_DrawBackgroundExt(&Texts_0203DB14[i], 0);
    }

    proc->unk_40 = gSioSt->unk_007;
    proc->unk_41 = gSioSt->unk_007;
    proc->playerId = gSioSt->selfId;

    CpuFill16(0, proc->unk_44, sizeof(proc->unk_44));
    GetLinkArenaFinalRanking(proc->unk_44);

    SioPostBattle_DrawNamesAndPoints(proc);

    proc->unk_64 = 176;

    BG_SetPosition(2, 0, 176);

    SetWinEnable(0, 0, 0);

    SetBlendAlpha(8, 8);
    SetBlendTargetA(0, 0, 1, 0, 0);
    SetBlendTargetB(0, 0, 0, 1, 0);

    SioPostBattle_StartMusicProc(proc);

    BG_EnableSyncByMask(BG3_SYNC_BIT);

    return;
}

//! FE8U = 0x080457F8
void SioPostBattle_Loop_Main(struct SioPostBattleProc * proc)
{
    int fid;

    int unk_40 = proc->unk_40;

    proc->unk_64--;
    BG_SetPosition(2, 0, proc->unk_64);

    UpdateLinkArenaActiveBannerBgGlow();

    if (proc->unk_41 != 0)
    {
        if ((proc->unk_64 >> 3) == (gSioPostBattleRowYLut[unk_40][(proc->unk_41 - 1)] + 4))
        {
            SioPostBattle_DrawRankingRowBg(proc);
            BG_EnableSyncByMask(BG2_SYNC_BIT);

            fid = gSioBmSt.unk_24[proc->unk_44[(proc->unk_41 - 1)].playerId];
            proc->unk_2c[(proc->unk_41 - 1)] =
                StartDrawLinkArenaRankSprites(proc, 40, fid, proc->unk_44[(proc->unk_41 - 1)].playerId, (proc->unk_41 - 1));

            proc->unk_41--;
        }
    }

    if (proc->unk_64 == 0)
    {
        Proc_Break(proc);
    }

    return;
}

//! FE8U = 0x0804589C
void SioPostBattle_AwaitAPress(ProcPtr proc)
{
    UpdateLinkArenaActiveBannerBgGlow();

    if ((gKeyStatusPtr->newKeys & A_BUTTON) != 0)
    {
        m4aMPlayFadeOut(gMPlayTable[gSongTable[0x81].ms].info, 1);
        Proc_Break(proc);
    }

    return;
}

// clang-format off

struct ProcCmd CONST_DATA ProcScr_SioPostBattle[] =
{
    PROC_CALL(SioPostBattle_Init),

    PROC_CALL(FadeInBlackSpeed20),
    PROC_YIELD,

    PROC_CALL(ResumeLinkArenaPalAnims),

    PROC_REPEAT(SioPostBattle_Loop_Main),
    PROC_REPEAT(SioPostBattle_AwaitAPress),

    PROC_CALL(PauseLinkArenaPalAnims),

    PROC_CALL(FadeWithCallBack_Speed20),
    PROC_YIELD,

    PROC_CALL(BMapVSync_Start),

    PROC_END,
};

// clang-format on

//! FE8U = 0x080458E8
void SioPostBattleMusic_PlayFanfare(struct SioPostBattleMusicProc * proc)
{
    if (proc->isPlayerWinner != 0)
    {
        StartBgmExt(SONG_COLOSSEUM_VICTORY, 0, 0);
    }
    else
    {
        StartBgmExt(SONG_COLOSSEUM_DEFEAT, 0, 0);
    }

    PlaySoundEffect(SONG_81);

    return;
}

//! FE8U = 0x08045920
void SioPostBattleMusic_PlayStandardBgm(void)
{
    StartBgmExt(SONG_COLOSSEUM_DEFEAT, 0, 0);
    return;
}

// clang-format off

struct ProcCmd CONST_DATA ProcScr_SioPostBattle_PlayMusic[] =
{
    PROC_SLEEP(16),

    PROC_CALL(SioPostBattleMusic_PlayFanfare),

    PROC_SLEEP(210),

    PROC_CALL(SioPostBattleMusic_PlayStandardBgm),

    PROC_END,
};

struct ProcCmd CONST_DATA ProcScr_SIOPRA[] = {
    PROC_NAME("SIOPRA"),
    PROC_YIELD,
    PROC_CALL(StartLinkArenaTeamList),
    PROC_YIELD,
    PROC_CALL(SioTerm_EndIfNoTeamChosen),
    PROC_CALL(SioBat_LoadAllTeamUnits),
    PROC_CALL(Sio_EndBackgroundSlide),
    PROC_CALL(EndLinkArenaTitleBanner),
    PROC_CALL(ClearLinkArenaUiBlend),
    PROC_YIELD,
    PROC_CALL(New6C_SIOMAIN2),
    PROC_YIELD,
    PROC_REPEAT(SioBat_BranchAfterMain2),
    PROC_CALL(PauseLinkArenaPalAnims),
    PROC_CALL(FadeWithCallBack_Speed20),
    PROC_YIELD,
    PROC_START_CHILD_BLOCKING(ProcScr_SioPostBattle),
    PROC_YIELD,
    PROC_CALL(SetSioSaveConfigB3),
PROC_LABEL(4),
PROC_LABEL(1),
    PROC_CALL(PauseLinkArenaPalAnims),
    PROC_CALL(FadeWithCallBack_Speed20),
    PROC_YIELD,
    PROC_END,
};

/* Apparently these should belongs to sio_bat? */
const struct LinkArenaRuleInfo gLinkArenaRuleData[] = {
    { 0x776, 14, 17, 0x77B, 0x77C },
    { 0x777, 16, 22, 0x779, 0x77A },
    { 0x778, 14, 17, 0x77B, 0x77C },
};

CONST_DATA struct FaceVramEntry FaceConfig_SioBat[] = {
    { 0x7000, 1 },
    { 0x7000, 1 },
    { 0x7000, 1 },
    { 0x7000, 1 },
};

CONST_DATA struct MultiArenaSaveTeam * gpSioSaveTeamBuf = (void *)gGenericBuffer;

struct ProcCmd CONST_DATA ProcScr_SIOBAT[] = {
    PROC_NAME("SIOBAT"),
PROC_LABEL(0),
    PROC_CALL(StartLinkArenaTeamList),
    PROC_YIELD,
    PROC_CALL(SioTerm_EndIfNoTeamChosen),
    PROC_CALL(Sio_EndBackgroundSlide),
    PROC_CALL(EndLinkArenaTitleBanner),
    PROC_CALL(ClearLinkArenaUiBlend),
    PROC_CALL(SioBat_InitSetupScreen),
    PROC_CALL(FadeInBlackSpeed20),
    PROC_YIELD,
    PROC_CALL(ResumeLinkArenaPalAnims),
    PROC_CALL(SioBat_StartSioProcs),
PROC_LABEL(3),
    PROC_REPEAT(SioBat_SetupLoop),
    PROC_CALL(SioBat_EnableLinkErrorCheck),
    PROC_REPEAT(SioBat_WaitSetupAck),
    PROC_CALL(SioSyncPlayers_Init),
    PROC_REPEAT(SioSyncPlayers_Loop),
    PROC_CALL(SioBat_DecideFirstMover),
    PROC_REPEAT(SioBat_ReceiveFirstMover),
    PROC_CALL(SioSyncPlayers_Init),
    PROC_REPEAT(SioSyncPlayers_Loop),
    PROC_REPEAT(SioBat_FirstMoverRouletteLoop),
    PROC_SLEEP(10),
    PROC_CALL(SioBat_PlayFirstMoverSound),
    PROC_SLEEP(80),
    PROC_CALL(PauseLinkArenaPalAnims),
    PROC_CALL(FadeWithCallBack_Speed20),
    PROC_YIELD,
    PROC_CALL(EndLinkArenaVersusSpriteDraw),
    PROC_CALL(EndMuralBackground),
    PROC_CALL(SioBat_InitRulesScreen),
    PROC_CALL(FadeInBlackSpeed20),
    PROC_YIELD,
    PROC_CALL(ResumeLinkArenaPalAnims),
    PROC_SLEEP(180),
    PROC_CALL(SioSyncPlayers_Init),
    PROC_REPEAT(SioSyncPlayers_Loop),
    PROC_CALL(PauseLinkArenaPalAnims),
    PROC_CALL(FadeWithCallBack_Speed20),
    PROC_YIELD,
    PROC_END_EACH(ProcScr_RuleSettingSpriteDraw_Static),
    PROC_CALL(Sio_EndBackgroundSlide),
    PROC_CALL(EndLinkArenaTitleBanner),
    PROC_CALL(ClearLinkArenaUiBlend),
    PROC_CALL(SioBat_InitNowLoadingScreen),
    PROC_CALL(FadeInBlackSpeed20),
    PROC_YIELD,
    PROC_CALL(ResumeLinkArenaPalAnims),
    PROC_CALL(SioBat_InitTeamTransfer),
    PROC_REPEAT(SioBat_TeamTransferLoop),
    PROC_REPEAT(SioBat_WaitTeamTransferDone),
    PROC_CALL(SioSyncPlayers_Init),
    PROC_REPEAT(SioSyncPlayers_Loop),
    PROC_CALL(PauseLinkArenaPalAnims),
    PROC_CALL(FadeWithCallBack_Speed20),
    PROC_YIELD,
    PROC_CALL(EndLinkArenaVersusSpriteDraw),
    PROC_CALL(EndMuralBackground),
    PROC_CALL(Sio_EndBackgroundSlide),
    PROC_SLEEP(1),
    PROC_CALL(New6C_SIOMAIN2),
    PROC_YIELD,
    PROC_REPEAT(SioBat_BranchAfterMain2),
    PROC_CALL(PauseLinkArenaPalAnims),
    PROC_CALL(FadeWithCallBack_Speed20),
    PROC_YIELD,
    PROC_CALL(SioBat_DisableLinkErrorCheck),
    PROC_CALL(SioBat_EndHandler),
    PROC_START_CHILD_BLOCKING(ProcScr_SioPostBattle),
    PROC_YIELD,
    PROC_CALL(SioBat_ReleaseIrq),
    PROC_CALL(SioBat_RegisterHighScore),
    PROC_YIELD,
    PROC_CALL(SetSioSaveConfigB3),
    PROC_CALL(Sio_EndBackgroundSlide),
    PROC_GOTO(1),
PROC_LABEL(2),
    PROC_CALL(PauseLinkArenaPalAnims),
    PROC_CALL(FadeWithCallBack_Speed20),
    PROC_YIELD,
    PROC_CALL(ResetFaces),
    PROC_CALL(EndLinkArenaVersusSpriteDraw),
    PROC_CALL(EndMuralBackground),
    PROC_GOTO(0),
PROC_LABEL(4),
    PROC_CALL(SioSyncPlayers_Init),
    PROC_REPEAT(SioSyncPlayers_Loop),
    PROC_SLEEP(1),
    PROC_CALL(PauseLinkArenaPalAnims),
    PROC_CALL(FadeWithCallBack_Speed20),
    PROC_YIELD,
    PROC_CALL(SioBat_DisableLinkErrorCheck),
    PROC_CALL(SioBat_EndHandler),
    PROC_CALL(SioBat_ReleaseIrq),
PROC_LABEL(1),
    PROC_CALL(SioInit_SW),
    PROC_END,
};

struct ProcCmd CONST_DATA ProcScr_SIOTERM[] = {
    PROC_NAME("SIOTERM"),
PROC_LABEL(0),
    PROC_CALL(StartLinkArenaTeamList),
    PROC_YIELD,
    PROC_CALL(SioTerm_EndIfNoTeamChosen),
PROC_LABEL(3),
    PROC_CALL(SioTerm_Init),
    PROC_CALL(FadeInBlackSpeed20),
    PROC_YIELD,
    PROC_CALL(ResumeLinkArenaPalAnims),
    PROC_REPEAT(SioTerm_SaveSelectLoop),
    PROC_CALL(PauseLinkArenaPalAnims),
    PROC_CALL(FadeWithCallBack_Speed20),
    PROC_YIELD,
    PROC_CALL(ClearSioAllBGs),
    PROC_CALL(SioTerm_LoadSelectedSave),
    PROC_CALL(Sio_EndBackgroundSlide),
    PROC_CALL(EndLinkArenaTitleBanner),
    PROC_CALL(ClearLinkArenaUiBlend),
    PROC_CALL(EndLinkArenaButtonSpriteDraw),
    PROC_CALL(EndMuralBackground),
    PROC_CALL(BMapVSync_End),
    PROC_YIELD,
    PROC_CALL(StartPrepAtMenuWithConfig),
PROC_LABEL(5),
    PROC_REPEAT(SioTerm_WaitAtMenu),
    PROC_CALL(BMapVSync_Start),
    PROC_CALL(SioTerm_ReinitIfNoSaveSelected),
    PROC_CALL(PauseLinkArenaPalAnims),
    PROC_CALL(FadeWithCallBack_Speed20),
    PROC_YIELD,
    PROC_CALL(StartNameSelect),
    PROC_YIELD,
    PROC_CALL(SioTerm_ReturnToPrepIfRequested),
    PROC_YIELD,
    PROC_GOTO(0),
PROC_LABEL(2),
    PROC_CALL(PauseLinkArenaPalAnims),
    PROC_CALL(FadeWithCallBack_Speed20),
    PROC_YIELD,
    PROC_GOTO(3),
PROC_LABEL(4),
    PROC_CALL(PauseLinkArenaPalAnims),
    PROC_CALL(FadeWithCallBack_Speed20),
    PROC_YIELD,
    PROC_CALL(SioTerm_ResetBg1Position),
    PROC_CALL(EndLinkArenaButtonSpriteDraw),
    PROC_CALL(EndMuralBackground),
    PROC_GOTO(0),
PROC_LABEL(1),
    PROC_END,
};
