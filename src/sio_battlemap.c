#include "global.h"

#include "bmunit.h"
#include "bmudisp.h"
#include "mu.h"
#include "bmmap.h"
#include "chapterdata.h"
#include "bmtrick.h"
#include "bmlib.h"
#include "fontgrp.h"
#include "hardware.h"
#include "bm.h"
#include "bmio.h"
#include "m4a.h"
#include "soundwrapper.h"
#include "bmitem.h"
#include "icon.h"
#include "uimenu.h"
#include "bksel.h"
#include "bmbattle.h"
#include "uiutils.h"
#include "bmmind.h"
#include "popup.h"
#include "cp_common.h"
#include "cp_perform.h"
#include "ctc.h"
#include "scene.h"
#include "helpbox.h"
#include "mapanim.h"
#include "ekrbattle.h"
#include "event.h"
#include "eventcall.h"
#include "eventscript.h"
#include "EAstdlib.h"

#include "sio_core.h"
#include "sio.h"

#include "constants/msg.h"
#include "constants/songs.h"
#include "constants/terrains.h"

//! FE8U = 0x08049298
void LABattleMap_BackupUnitItems(struct Unit * unit)
{
    int i;

    for (i = 0; i < UNIT_ITEM_COUNT; i++)
    {
        gLinkArenaItemBackup[i] = unit->items[i];
    }

    return;
}

//! FE8U = 0x080492B8
void LABattleMap_RestoreUnitItems(struct Unit * unit)
{
    int i;

    for (i = 0; i < UNIT_ITEM_COUNT; i++)
    {
        unit->items[i] = gLinkArenaItemBackup[i];
    }

    return;
}

//! FE8U = 0x080492D8
void LABattleMap_RefreshMapAndSprites(void)
{
    LABattleMap_RefreshUnitMap();
    RefreshUnitSprites();
    return;
}

//! FE8U = 0x080492E8
void LABattleMap_SetNextPhase(int faction)
{
    int current = faction;
    int next = FACTION_ID_BLUE;

    while (1)
    {
        switch (current)
        {
            case FACTION_ID_BLUE:
                next = FACTION_ID_RED;
                break;

            case FACTION_ID_GREEN:
                next = FACTION_ID_PURPLE;
                break;

            case FACTION_ID_RED:
                next = FACTION_ID_GREEN;
                break;

            case FACTION_ID_PURPLE:
                next = FACTION_ID_BLUE;
                break;

            case 0xFF:
                next = 0xFF;
                break;
        }

        if (gSioBmSt.unk_0A[next] != 0 || next == 0xFF)
        {
            break;
        }

        current = next;
    }

    gSioBmSt.unk_01 = next;

    return;
}

//! FE8U = 0x0804933C
void LABattleMap_ClearCmdBuf(void)
{
    gLinkArenaCmdBuf[0] = 0;
    gLinkArenaCmdBuf[1] = 0;
    gLinkArenaCmdBuf[3] = 0;
    gLinkArenaCmdBuf[2] = 0;

    return;
}

//! FE8U = 0x08049350
u16 LABattleMap_SendCmd(u8 a, u8 b, u8 c, u8 d)
{
    LABattleMap_ClearCmdBuf();

    gLinkArenaCmdBuf[0] = a;
    gLinkArenaCmdBuf[1] = b;
    gLinkArenaCmdBuf[2] = c;
    gLinkArenaCmdBuf[3] = d;

    if (gLinkArenaSt.unk_00 == 2)
    {
        return SioEmitData(gLinkArenaCmdBuf, 4);
    }

    return 0;
}

//! FE8U = 0x080493A8
int LABattleMap_GetUnitSlot(u8 target)
{
    int i = 0;

    for (i = 0; i < 20; i++)
    {
        if (gLinkArenaUnitSlots[i] == target)
        {
            return i;
        }
    }

    // BUG -- no return if > 20
}

//! FE8U = 0x080493D0
void LABattleMap_StartUnitMoveOut(u8 a, int b, u8 * c, int * xOut, int * yOut)
{
    struct Unit * unit;

    // clang-format off

    u8 gUnknown_080D9FA0[8] =
    {
        MOVE_CMD_MOVE_UP,
        MOVE_CMD_HALT,

        MOVE_CMD_MOVE_LEFT,
        MOVE_CMD_HALT,

        MOVE_CMD_MOVE_DOWN,
        MOVE_CMD_HALT,

        MOVE_CMD_MOVE_RIGHT,
        MOVE_CMD_HALT,
    };

    s8 gUnknown_080D9FA8[8] =
    {
        +0, -1,
        -1, +0,
        +0, +1,
        +1, +0,
    };

    // clang-format on

    int var = LABattleMap_GetUnitSlot(a);
    int index = Div(var, 5) << 1;

    *c = var;

    unit = GetUnit(gLinkArenaUnitSlots[var]);

    if ((unit->state & US_BIT9) == 0)
    {
        gLinkArenaCombatantMus[b] = StartMu(unit);
        DisableMuCamera(gLinkArenaCombatantMus[b]);
        SetMuMoveScript(gLinkArenaCombatantMus[b], gUnknown_080D9FA0 + index);
    }

    unit->state |= US_HIDDEN;

    RefreshUnitSprites();

    *xOut = unit->xPos + gUnknown_080D9FA8[index + 0];
    *yOut = unit->yPos + gUnknown_080D9FA8[index + 1];

    if ((unit->state & US_BIT9) != 0)
    {
        unit->xPos = *xOut;
        unit->yPos = *yOut;

        *xOut = *xOut - gUnknown_080D9FA8[index + 0];
        *yOut = *yOut - gUnknown_080D9FA8[index + 1];

        gLinkArenaCombatantMus[b] = NULL;
    }

    return;
}

//! FE8U = 0x080494D4
void LABattleMap_ClearUnitSlots(void)
{
    int i = 0;

    for (i = 0; i < 20; i++)
    {
        gLinkArenaUnitSlots[i] = 0;
    }

    return;
}

// clang-format off

const u8 gLinkArenaSlotUnitOrder[] =
{
    4, 2, 0, 1, 3,
};

// clang-format on

//! FE8U = 0x080494F0
void LABattleMap_InitUnitSlots(void)
{
    int faction;
    int i;
    int j;

    for (i = 0; i < 4; i++)
    {
        int playerId = gSioSideToPlayerLut[gSioSt->selfId][i];

        if (!SioIsPlayerConnected(playerId))
        {
            continue;
        }

        faction = playerId * 0x40 + 1;

        for (j = 0; j < 5; j++)
        {
            int idx = i * 5 + j;
            int unitId = faction + gLinkArenaSlotUnitOrder[j];

            struct Unit * unit = GetUnit(unitId);

            if (unit->pCharacterData != NULL)
            {
                gLinkArenaUnitSlots[idx] = unitId;

                unit->xPos = gLinkArenaSlotPositions[idx].x;
                unit->yPos = gLinkArenaSlotPositions[idx].y;
            }
        }
    }

    return;
}

//! FE8U = 0x08049594
void LABattleMap_RefreshUnitMap(void)
{
    int i;

    BmMapFill(gBmMapUnit, 0);
    BmMapFill(gBmMapFog, 1);

    for (i = FACTION_BLUE + 1; i < FACTION_PURPLE + 6; i++)
    {
        struct Unit * unit = GetUnit(i);

        if (!UNIT_IS_VALID(unit))
        {
            continue;
        }

        if ((unit->state & US_HIDDEN) != 0)
        {
            continue;
        }

        gBmMapUnit[unit->yPos][unit->xPos] = i;
    }

    return;
}

//! FE8U = 0x080495F4
void LABattleMap_RefreshUnitSlots(void)
{
    int i;
    int j;

    for (i = 0; i < 4; i++)
    {
        int faction;

        int playerId = gSioSideToPlayerLut[gSioSt->selfId][i];

        if (!SioIsPlayerConnected(playerId))
        {
            continue;
        }

        faction = playerId * 0x40 + 1;

        for (j = 0; j < 5; j++)
        {
            int idx = i * 5 + j;
            int unitId = faction + gLinkArenaSlotUnitOrder[j];

            struct Unit * unit = GetUnit(unitId);

            if ((unit->pCharacterData == NULL) || ((unit->state & (US_HIDDEN | US_DEAD | US_BIT16)) != 0))
            {
                gLinkArenaUnitSlots[idx] = 0;
            }
            else
            {
                gLinkArenaUnitSlots[idx] = unitId;
            }
        }
    }

    return;
}

//! FE8U = 0x080496A4
void LABattleMap_InitChapter(void)
{
    CpuFill16(0, &gBmSt, sizeof(struct BmSt));

    gBmSt.gameStateBits |= BM_FLAG_LINKARENA;

    ClearTraps();

    gPlaySt.faction = FACTION_GREEN;
    gPlaySt.chapterIndex = 0x3a;
    gPlaySt.chapterTurnNumber = 0;

    gPlaySt.chapterVisionRange = GetROMChapterStruct(gPlaySt.chapterIndex)->initialFogLevel;
    gPlaySt.chapterWeatherId = GetROMChapterStruct(gPlaySt.chapterIndex)->initialWeather;

    InitChapterMap(0x3a);

    gPlaySt.time_chapter_started = GetGameClock();
    gPlaySt.unk48 = 0;

    return;
}

//! FE8U = 0x0804970C
void LABattleMap_InitMap(void)
{
    LABattleMap_InitChapter();
    LABattleMap_ClearUnitSlots();
    LABattleMap_InitUnitSlots();

    BmMapFill(gBmMapFog, gPlaySt.chapterVisionRange == 0);

    LABattleMap_RefreshUnitMap();

    RenderBmMap();

    return;
}

//! FE8U = 0x08049744
void LABattleMap_CountTeamUnits(void)
{
    int i;

    for (i = 0; i < 4; i++)
    {
        gSioBmSt.unk_0A[i] = 0;
    }

    for (i = 0; i < 20; i++)
    {
        u32 a = gLinkArenaUnitSlots[i];

        if (a != 0)
        {
            gSioBmSt.unk_0A[a >> 6]++;
        }
    }

    return;
}

//! FE8U = 0x08049788
void LoadLinkArenaFogPlaceholder(void)
{
    Decompress(Img_LinkArena_FogUnitPlaceholder, OBJ_CHR_ADDR(0x1F0));
    return;
}

//! FE8U = 0x080497A0
void LABattleMap_InitDisplay(void)
{
    SetupBackgrounds(NULL);

    sub_80156D4();

    ApplyUnitSpritePalettes();
    ForceSyncUnitSpriteSheet();

    LoadLinkArenaFogPlaceholder();
    InitSystemTextFont();

    gSioBmSt.unk_03 = 0xff;

    return;
}

//! FE8U = 0x080497CC
void SetLinkArenaGameConfig(void)
{
    gPlaySt.config.animationType = 0;

    gPlaySt.config.autoCursor = 1;
    gPlaySt.config.textSpeed = 2;
    gPlaySt.config.gameSpeed = 0;

    gPlaySt.config.disableBgm = 0;
    gPlaySt.config.disableSoundEffects = 0;
    gPlaySt.config.windowColor = 0;

    gPlaySt.config.battleForecastType = 0;

    gPlaySt.config.unitColor = 1;

    return;
}

// clang-format off

u8 CONST_DATA gLinkArenaPrecedingFaction[] =
{
    FACTION_ID_PURPLE,
    FACTION_ID_RED,
    FACTION_ID_BLUE,
    FACTION_ID_GREEN,
};

// clang-format on

//! FE8U = 0x08049828
void LABattleMap_Init(void)
{
    int i;
    struct Unit * unit;

    SetupBackgrounds(NULL);
    ClearSioBG();

    LABattleMap_InitMap();
    LABattleMap_CountTeamUnits();

    gSioBmSt.unk_09 = 0;
    gLinkArenaSt.unk_0B = 0;

    LABattleMap_SetNextPhase(gLinkArenaPrecedingFaction[gSioBmSt.unk_00]);

    gSioBmSt.unk_0E = 1;
    gSioBmSt.unk_02 = 0;
    gSioBmSt.unk_03 = 1;

    for (i = 0; i < 4; i++)
    {
        gSioBmSt.currentScore[i] = 0;
    }

    unit = GetUnit(gLinkArenaUnitSlots[3]);

    gLinkArenaCursorPos.x = unit->xPos * 16;
    gLinkArenaCursorPos.y = unit->yPos * 16;

    SetCursorMapPosition(unit->xPos, unit->yPos);

    gBmSt.camera.x = 0;
    gBmSt.camera.y = 0;

    ReadGameSaveCoreGfx();

    ApplyUnitSpritePalettes();
    ResetUnitSprites();
    RefreshUnitSprites();

    LoadLinkArenaFogPlaceholder();
    StartLinkArenaFogPlaceholders();

    Proc_Start(gProc_MapTask, PROC_TREE_4);
    BMapVSync_Start();
    SetLinkArenaGameConfig();

    gPlaySt.chapterStateBits &= ~PLAY_FLAG_HARD;

    return;
}

//! FE8U = 0x080498F4
void LABattleMap_StartSelectedUnitMu(void)
{
    struct Unit * unit = GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_04]);

    gLinkArenaCombatantMus[0] = StartMu(unit);

    SetMuScreenPosition(gLinkArenaCombatantMus[0], unit->xPos * 16, (unit->yPos - 1) * 16);

    DisableMuCamera(gLinkArenaCombatantMus[0]);
    SetMuFacing(gLinkArenaCombatantMus[0], 3);

    return;
}

//! FE8U = 0x08049940
void LABattleMap_WaitLButton(ProcPtr proc)
{
    if ((gKeyStatusPtr->newKeys & L_BUTTON) != 0)
    {
        Proc_Break(proc);
    }

    return;
}

//! FE8U = 0x08049964
void LABattleMap_StartPhaseProc(ProcPtr parent)
{
    switch (gLinkArenaSt.unk_00)
    {
        case 1:
            if (gPlaySt.faction == FACTION_BLUE)
            {
                Proc_StartBlocking(ProcScr_LABattleMap_LocalPhase, parent);
            }
            else
            {
                Proc_StartBlocking(ProcScr_LABattleMap_CpuPhase, parent);
            }

            break;

        case 2:
            if (gPlaySt.faction == gSioSt->selfId)
            {
                Proc_StartBlocking(ProcScr_LABattleMap_LocalPhase, parent);
            }
            else
            {
                Proc_StartBlocking(ProcScr_LABattleMap_RemotePhase, parent);
            }

            break;
    }

    Proc_Break(parent);

    return;
}

//! FE8U = 0x080499D0
void LABattleMap_BeginPhase(ProcPtr proc)
{
    int i = 0;

    if (gLinkArenaSt.unk_00 == 1)
    {
        if (gLinkArenaSt.unk_0B == 1)
        {
            Proc_Goto(proc, 3);
            return;
        }
    }
    else if (gLinkArenaSt.unk_0B == 2)
    {
        Proc_Goto(proc, 3);
        return;
    }

    if (gSioBmSt.unk_01 == 0xFF)
    {
        Proc_Goto(proc, 2);
        return;
    }

    gPlaySt.faction = gSioBmSt.unk_01;

    for (; gLinkArenaUnitSlots[i] == 0; i++)
    {
    }

    gSioBmSt.unk_02 = i;
    gSioBmSt.unk_03 = i + 1;

    LoadObjUIGfx();

    for (i = 0; i < 4; i++)
    {
        gSioBmSt.unk_2c[i].newScore = 0;
        gSioBmSt.unk_2c[i].unitId = 0;
    }

    SetupDebugFontForOBJ(-1, 9);

    return;
}

//! FE8U = 0x08049A60
int LinkArena_GetPlayerRanking(void)
{
    int i;

    u32 ret = 4;

    u32 score = gSioBmSt.currentScore[gSioSt->selfId];

    if (gLinkArenaSt.unk_ec.unk_0_1)
    {
        for (i = 0; i < 4; i++)
        {
            if (SioIsPlayerConnected(i) && (gSioSt->selfId == gSioBmSt.unk_0F[i]))
            {
                return i;
            }
        }

        ret = 3;
    }
    else
    {
        for (i = 0; i < 4; i++)
        {
            if (!SioIsPlayerConnected(i))
            {
                ret--;
                continue;
            }

            if (gSioSt->selfId == i)
            {
                ret--;
                continue;
            }

            if (score > gSioBmSt.currentScore[i])
            {
                ret--;
                continue;
            }
        }
    }

    return ret;
}

//! FE8U = 0x08049B04
void LABattleMap_End(void)
{
    Proc_EndEach(gProc_MapTask);

    EndLinkArenaFogPlaceholders();

    BMapVSync_End();
    Sound_FadeOutBGM(1);

    return;
}

// clang-format off

u8 CONST_DATA gLinkArenaCursorMoveLut[] =
{
    0x0E, 0x0E, 0x13, 0x01,
    0x0D, 0x0D, 0x00, 0x02,
    0x0C, 0x0C, 0x01, 0x03,
    0x0B, 0x0B, 0x02, 0x04,
    0x0A, 0x0A, 0x03, 0x05,
    0x06, 0x04, 0x13, 0x13,
    0x07, 0x05, 0x12, 0x12,
    0x08, 0x06, 0x11, 0x11,
    0x09, 0x07, 0x10, 0x10,
    0x0A, 0x08, 0x0F, 0x0F,
    0x04, 0x04, 0x0B, 0x09,
    0x03, 0x03, 0x0C, 0x0A,
    0x02, 0x02, 0x0D, 0x0B,
    0x01, 0x01, 0x0E, 0x0C,
    0x00, 0x00, 0x0F, 0x0D,
    0x0E, 0x10, 0x09, 0x09,
    0x0F, 0x11, 0x08, 0x08,
    0x10, 0x12, 0x07, 0x07,
    0x11, 0x13, 0x06, 0x06,
    0x12, 0x00, 0x05, 0x05,
};

// clang-format on

//! FE8U = 0x08049B24
void LABattleMap_MoveSelectCursor(u16 keys, s8 flag)
{
    u8 r2;
    int r4;
    int r5;

    r2 = gSioBmSt.unk_02;
    r5 = r2;
    gSioBmSt.unk_03 = gSioBmSt.unk_02;

    if ((keys & DPAD_ANY) == 0)
    {
        return;
    }

    r4 = r2 << 2;

    if ((keys & DPAD_UP) != 0)
    {
        r2 = gLinkArenaCursorMoveLut[r4 + 0];
    }
    else if ((keys & DPAD_DOWN) != 0)
    {
        r2 = gLinkArenaCursorMoveLut[r4 + 1];
    }
    else if ((keys & DPAD_LEFT) != 0)
    {
        r2 = gLinkArenaCursorMoveLut[r4 + 2];
    }
    else if ((keys & DPAD_RIGHT) != 0)
    {
        r2 = gLinkArenaCursorMoveLut[r4 + 3];
    }

    r5 = r2 - r5;

    if ((gSioBmSt.unk_03 == 0) && ((keys & DPAD_LEFT) != 0))
    {
        r5 = -1;
    }

    if ((gSioBmSt.unk_03 == 19) && ((keys & DPAD_DOWN) != 0))
    {
        r5 = +1;
    }

    while (1)
    {
        if (gLinkArenaUnitSlots[r2] != 0)
        {
            if (flag == 0 || (gLinkArenaUnitSlots[r2] >> 6) != gSioSt->selfId)
            {
                goto _end; // FIXME: Goto appears to be required for match
            }
        }

        if (r5 < 0)
        {
            r2--;

            if (r2 == 0xFF)
            {
                r2 = 19;
            }
        }
        else
        {
            r2++;
            r2 = r2 % 20;
        }
    }

_end:
    gSioBmSt.unk_02 = r2;

    return;
}

//! FE8U = 0x08049C18
void LABattleMap_UpdateCursorMu(void)
{
    struct Unit * unitA;
    struct Unit * unitB;

    if (gSioBmSt.unk_02 == gSioBmSt.unk_03)
    {
        return;
    }

    unitA = GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_03]);
    unitB = GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_02]);

    if (unitA != NULL)
    {
        EndAllMus();
        ShowUnitSprite(unitA);
    }

    if (unitB == NULL)
    {
        return;
    }

    if ((unitB->state & US_UNSELECTABLE) != 0)
    {
        return;
    }

    if ((gLinkArenaUnitSlots[gSioBmSt.unk_02] >> 6) != gSioSt->selfId)
    {
        return;
    }

    DisableMuCamera(StartMu(unitB));
    HideUnitSprite(unitB);

    return;
}

//! FE8U = 0x08049C94
bool UnitHasUsableWeapon(struct Unit * unit)
{
    int i;

    for (i = 0; i < UNIT_ITEM_COUNT; i++)
    {
        u16 item = unit->items[i];

        if ((GetItemAttributes(item) & IA_WEAPON) == 0)
        {
            continue;
        }

        if (CanUnitUseWeapon(unit, item) == 1)
        {
            return true;
        }
    }

    return false;
}

//! FE8U = 0x08049CD4
void LABattleMap_CheckNoDamageStall(ProcPtr proc)
{
    if (gSioBmSt.unk_09 >= gLinkArenaSt.unk_A0 * 3)
    {
        EndLinkArenaPointsBox();
        Proc_Goto(proc, 3);
    }

    Proc_Break(proc);

    return;
}

//! FE8U = 0x08049D0C
void LABattleMap_AwaitSioSendQueue(ProcPtr proc)
{
    if (SioGetPendingSendCount() < 8)
    {
        Proc_Break(proc);
    }

    return;
}

extern u8 gUnknown_080D9FB5[];

//! FE8U = 0x08049D24
void LABattleMap_SelectAttacker_Loop(struct SioBattleMapProc * proc)
{
    int x;
    int y;

    // clang-format off

    u8 gUnknown_080D9FB5[2] =
    {
        MOVE_CMD_MOVE_UP,
        MOVE_CMD_HALT,
    };

    // clang-format on

    u8 previous = gSioBmSt.unk_02;

    LABattleMap_UpdateCursorMu();
    LABattleMap_MoveSelectCursor(gKeyStatusPtr->repeatedKeys, 0);

    gActiveUnitId = gLinkArenaUnitSlots[gSioBmSt.unk_02];
    gActiveUnit = GetUnit(gActiveUnitId);

    if ((gKeyStatusPtr->newKeys & A_BUTTON) != 0)
    {
        if (((gActiveUnitId >> 6) == gSioSt->selfId) && (UnitHasUsableWeapon(gActiveUnit) == 1))
        {
            PlaySoundEffect(SONG_69);
            EndAllMus();

            gLinkArenaCombatantMus[0] = StartMu(gActiveUnit);
            DisableMuCamera(gLinkArenaCombatantMus[0]);
            SetMuMoveScript(gLinkArenaCombatantMus[0], gUnknown_080D9FB5);

            proc->unk_2c = gActiveUnit->xPos;
            proc->unk_30 = gActiveUnit->yPos - 1;
            gActiveUnit->state |= US_HIDDEN;

            LABattleMap_RefreshMapAndSprites();

            gSioBmSt.unk_04 = gSioBmSt.unk_02;

            LABattleMap_MoveSelectCursor(0x40, 1);
            LABattleMap_SendCmd(1, gActiveUnitId, 0, 0);

            Proc_Goto(proc, 5);
            return;
        }

        PlaySoundEffect(SONG_6C);
    }

    if ((gKeyStatusPtr->newKeys & R_BUTTON) != 0)
    {
        if ((gActiveUnit->state & US_BIT9) == 0)
        {
            EndAllMus();
            Proc_Goto(proc, 4);
            return;
        }
    }

    if ((gKeyStatusPtr->newKeys & START_BUTTON) != 0)
    {
        EndLinkArenaPointsBox();

        if (!gPlaySt.config.disableSoundEffects)
        {
            // Interestingly this does not seem to use the normal PlaySoundEffect macro
            m4aSongNumStart(SONG_68);
            Proc_Goto(proc, 2);
        }

        return;
    }

    x = gActiveUnit->xPos * 16;
    y = gActiveUnit->yPos * 16;

    SetCursorMapPosition(gActiveUnit->xPos, gActiveUnit->yPos);

    if (GetGameClock() - 1 == gLinkArenaCursorLastClock)
    {
        x = (x + gLinkArenaCursorPos.x) >> 1;
        y = (y + gLinkArenaCursorPos.y) >> 1;
    }

    gLinkArenaCursorPos.x = x;
    gLinkArenaCursorPos.y = y;

    gLinkArenaCursorLastClock = GetGameClock();

    PutMapCursor(x, y, 0);

    if (previous != gSioBmSt.unk_02)
    {
        PlaySoundEffect(SONG_65);
    }

    return;
}

//! FE8U = 0x08049F38
void LABattleMap_StartPointsBox(ProcPtr unused)
{
    StartLinkArenaPointsBox();
    return;
}

//! FE8U = 0x08049F44
void LABattleMap_SelectTarget_Loop(struct SioBattleMapProc * proc)
{
    int x;
    int y;

    u8 previous = gSioBmSt.unk_02;

    LABattleMap_MoveSelectCursor(gKeyStatusPtr->repeatedKeys, 1);

    gActiveUnitId = gLinkArenaUnitSlots[gSioBmSt.unk_02];

    gActiveUnit = GetUnit(gActiveUnitId);
    x = gActiveUnit->xPos * 16;
    y = gActiveUnit->yPos * 16;

    SetCursorMapPosition(gActiveUnit->xPos, gActiveUnit->yPos);

    if (GetGameClock() - 1 == gLinkArenaCursorLastClock)
    {
        x = (x + gLinkArenaCursorPos.x) >> 1;
        y = (y + gLinkArenaCursorPos.y) >> 1;
    }

    gLinkArenaCursorPos.x = x;
    gLinkArenaCursorPos.y = y;

    gLinkArenaCursorLastClock = GetGameClock();

    PutMapCursor(x, y, 0);

    if ((gKeyStatusPtr->newKeys & A_BUTTON) != 0)
    {
        PlaySoundEffect(SONG_69);

        LABattleMap_StartUnitMoveOut(
            gLinkArenaUnitSlots[gSioBmSt.unk_02], 1, &gSioBmSt.unk_05, &proc->unk_34, &proc->unk_38);
        LABattleMap_SendCmd(3, gLinkArenaUnitSlots[gSioBmSt.unk_05], gActiveUnitId, 0);
        EndLinkArenaPointsBox();

        Proc_Goto(proc, 7);

        return;
    }

    if ((gKeyStatusPtr->newKeys & B_BUTTON) != 0)
    {
        PlaySoundEffect(SONG_SE_SYS_WINDOW_CANSEL1);

        EndMu(gLinkArenaCombatantMus[0]);
        GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_04])->state &= ~US_HIDDEN;

        LABattleMap_RefreshMapAndSprites();

        gSioBmSt.unk_02 = gSioBmSt.unk_04;
        gSioBmSt.unk_03 = gSioBmSt.unk_04 + 1;

        LABattleMap_SendCmd(2, gActiveUnitId, gLinkArenaUnitSlots[gSioBmSt.unk_04], 0);

        Proc_Goto(proc, 1);

        return;
    }

    if ((gKeyStatusPtr->newKeys & R_BUTTON) != 0)
    {
        if ((gActiveUnit->state & US_BIT9) == 0)
        {
            EndAllMus();
            Proc_Goto(proc, 6);
            return;
        }
    }

    if (previous != gSioBmSt.unk_02)
    {
        PlaySoundEffect(SONG_65);
    }

    return;
}

//! FE8U = 0x0804A108
void LABattleMap_StartAttackItemMenu(struct SioProc85AA1AC * proc)
{
    ResetTextFont();

    gSioBmSt.unk_06 = 0xff;

    gActiveUnit = GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_04]);
    LABattleMap_BackupUnitItems(gActiveUnit);

    proc->unk_64 = GetGameLock();
    LoadIconPalettes(4);

    StartOrphanMenu(&MenuDef_SioAttackItem);

    return;
}

//! FE8U = 0x0804A158
void LABattleMap_AttackItemMenu_Loop(struct SioProc85AA1AC * proc)
{
    if (proc->unk_64 != GetGameLock())
    {
        return;
    }

    if (gSioBmSt.unk_06 == 0)
    {
        LABattleMap_RestoreUnitItems(gActiveUnit);
        Proc_End(proc);
        SetDispEnable(1, 1, 1, 1, 1);
    }
    else
    {
        Proc_Break(proc);
        SetDispEnable(0, 0, 1, 1, 1);
    }

    return;
}

//! FE8U = 0x0804A1D0
void LABattleMap_DetermineAttackRange(struct SioProc85AA1AC * proc)
{
    u16 item = gActiveUnit->items[gSioBmSt.unk_07];

    proc->unk_64 = GetGameLock();

    if ((GetItemMinRange(item) == 1) && (GetItemMaxRange(item) == 1))
    {
        gSioBmSt.unk_06 = 1;
        return;
    }

    if ((GetItemMinRange(item) == 2) && (GetItemMaxRange(item) == 2))
    {
        gSioBmSt.unk_06 = 2;
        return;
    }

    if ((GetItemMinRange(item) == 2) && (GetItemMaxRange(item) == 3))
    {
        gSioBmSt.unk_06 = 2;
        return;
    }

    item = GetUnitEquippedWeapon(GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_05]));

    if (item == 0)
    {
        gSioBmSt.unk_06 = 1;
        return;
    }

    if (GetItemMinRange(item) >= 2)
    {
        gSioBmSt.unk_06 = 2;
        return;
    }

    gSioBmSt.unk_06 = 1;
    LoadIconPalettes(4);

    return;
}

//! FE8U = 0x0804A298
void LABattleMap_GenerateForecast(struct SioProc85AA1AC * proc)
{
    int tmp = 0;
    int local_24 = +1;

    struct Unit * unitA = GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_04]);
    struct Unit * unitB = GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_05]);

    if (proc->unk_64 == GetGameLock())
    {
        int y = unitB->yPos + 1;

        if (gBmMapTerrain[y][unitB->xPos] != TERRAIN_FLOOR_REGULAR)
        {
            local_24 = -1;
        }

        if (gSioBmSt.unk_06 == 0)
        {
            LABattleMap_RestoreUnitItems(gActiveUnit);
            Proc_Goto(proc, 0);
        }
        else
        {
            EquipUnitItemSlot(gActiveUnit, gSioBmSt.unk_07);

            if ((unitB->state & US_BIT9) == 0)
            {
                NewBattleForecast(proc);
                tmp = (gSioBmSt.unk_06 == 2) ? 1 : 0;
                BattleGenerateSimulation(unitA, unitB, unitB->xPos + tmp, unitB->yPos + local_24, 0);
                UpdateBattleForecastContents();
                LABattleMap_RestoreUnitItems(gActiveUnit);
                Proc_Break(proc);
            }
            else
            {
                LABattleMap_RestoreUnitItems(gActiveUnit);
                Proc_Goto(proc, 1);
            }
        }
    }
    return;
}

//! FE8U = 0x0804A3A8
void LABattleMap_ForecastConfirm_Loop(ProcPtr proc)
{
    SetDispEnable(1, 1, 1, 1, 1);

    if ((gKeyStatusPtr->newKeys & A_BUTTON) != 0)
    {
        PlaySoundEffect(SONG_SE_SYS_WINDOW_SELECT1);
        CloseBattleForecast();

        Proc_Break(proc);

        return;
    }

    if ((gKeyStatusPtr->newKeys & B_BUTTON) != 0)
    {
        PlaySoundEffect(SONG_SE_SYS_WINDOW_CANSEL1);
        CloseBattleForecast();

        Proc_Goto(proc, 0);
    }

    return;
}

// clang-format off

struct ProcCmd CONST_DATA ProcScr_LABattleMapForecast[] =
{
PROC_LABEL(0),
    PROC_WHILE_EXISTS(gProcScr_BKSEL),
    PROC_SLEEP(1),

    PROC_CALL(LABattleMap_StartAttackItemMenu),

    PROC_REPEAT(LABattleMap_AttackItemMenu_Loop),

    PROC_CALL(LABattleMap_DetermineAttackRange),
    PROC_REPEAT(LABattleMap_GenerateForecast),
    PROC_REPEAT(LABattleMap_ForecastConfirm_Loop),

PROC_LABEL(1),
    PROC_END,
};

// clang-format on

//! FE8U = 0x0804A430
void LABattleMap_StartForecast(ProcPtr parent)
{
    Proc_StartBlocking(ProcScr_LABattleMapForecast, parent);
    Proc_Break(parent);
    return;
}

//! FE8U = 0x0804A44C
void LABattleMap_HandleWeaponSelectResult(struct SioBattleMapProc * proc)
{
    struct Unit * unit = GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_05]);

    ClearBg0Bg1();

    if (gSioBmSt.unk_06 == 0)
    {
        if ((unit->state & US_BIT9) == 0)
        {
            EndMu(gLinkArenaCombatantMus[1]);
        }
        else
        {
            unit->xPos = proc->unk_34;
            unit->yPos = proc->unk_38;
        }

        unit->state &= ~US_HIDDEN;

        RefreshUnitSprites();

        gSioBmSt.unk_02 = gSioBmSt.unk_05;
        gSioBmSt.unk_03 = gSioBmSt.unk_05 + 1;
        LABattleMap_SendCmd(4, 0, gLinkArenaUnitSlots[gSioBmSt.unk_05], 0);
        LABattleMap_StartPointsBox(proc);
        Proc_Goto(proc, 5);
    }
    else
    {
        if ((unit->state & US_BIT9) != 0)
        {
            gLinkArenaCombatantMus[1] = (void *)StartMu(unit);
            proc->unk_34 = unit->xPos;
            proc->unk_38 = unit->yPos;
            unit->state &= ~US_BIT9;
        }

        LABattleMap_SendCmd(5, 0, gSioBmSt.unk_06, gSioBmSt.unk_07);
    }

    return;
}

//! FE8U = 0x0804A51C
void LABattleMap_StartCombatantWarpFx(struct SioBattleMapProc * proc)
{
    struct Unit * unitA = GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_04]);
    struct Unit * unitB = GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_05]);

    unitA->xPos = proc->unk_2c;
    unitA->yPos = proc->unk_30;

    unitB->xPos = proc->unk_34;
    unitB->yPos = proc->unk_38;

    StartSioWarpFx(unitA, gLinkArenaCombatantMus[0], 6, 5, 1, 1, proc);
    StartSioWarpFx(unitB, gLinkArenaCombatantMus[1], 8, 5, 0, 0, proc);

    return;
}

extern u8 gLinkArenaCmdBuf[];

extern u8 gUnknown_080D9FB7[];

//! FE8U = 0x0804A5A4
void LABattleMap_PrepareCombatantSprites(void)
{
    struct Unit * unitB;
    struct Unit * unitA = GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_04]);

    // clang-format off

    u8 gUnknown_080D9FB7[2] =
    {
        MOVE_CMD_MOVE_RIGHT,
        MOVE_CMD_HALT,
    };

    // clang-format on

    EndMu(gLinkArenaCombatantMus[1]);

    unitB = GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_05]);
    unitB->state &= ~US_HIDDEN;

    if (gLinkArenaCmdBuf[2] == 1)
    {
        SetMuMoveScript(gLinkArenaCombatantMus[0], gUnknown_080D9FB7);
        unitA->xPos = 7;
    }

    LABattleMap_RefreshMapAndSprites();

    return;
}

extern struct ProcCmd ProcScr_LinkArenaCombatAction[];

//! FE8U = 0x0804A614
void LABattleMap_StartCombat(ProcPtr proc)
{
    struct Unit * unitA;
    struct Unit * unitB;

    if (MuExistsActive() == 1)
    {
        return;
    }

    unitA = GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_04]);
    unitB = GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_05]);

    HideUnitSprite(unitA);

    gActionData.unitActionType = 2;
    gActionData.targetIndex = gLinkArenaUnitSlots[gSioBmSt.unk_05];

    EquipUnitItemSlot(unitA, gLinkArenaCmdBuf[3]);
    BattleGenerateReal(unitA, unitB);

    gBmSt.gameStateBits |= BM_FLAG_LINKARENA;

    Proc_StartBlocking(ProcScr_LinkArenaCombatAction, proc);
    Proc_Break(proc);

    return;
}

//! FE8U = 0x0804A6A4
void LABattleMap_ApplyCombatResults(ProcPtr proc)
{
    u8 unitIdA = gLinkArenaUnitSlots[gSioBmSt.unk_04];
    u8 unitIdB = gLinkArenaUnitSlots[gSioBmSt.unk_05];

    struct Unit * unitA = GetUnit(unitIdA);
    struct Unit * unitB = GetUnit(unitIdB);

    LoadLinkArenaFogPlaceholder();

    gSioBmSt.unk_2c[unitIdA >> 6].newScore = gBattleActor.expGain;
    gSioBmSt.unk_2c[unitIdA >> 6].unitId = unitIdA;
    unitA->exp = 0;

    gSioBmSt.unk_2c[unitIdB >> 6].newScore = gBattleTarget.expGain;
    gSioBmSt.unk_2c[unitIdB >> 6].unitId = unitIdB;
    unitB->exp = 0;

    SioSetUniqueWeapons(unitA);
    SioSetUniqueWeapons(unitB);

    SetUnitStatus(unitA, 0);
    SetUnitStatus(unitB, 0);

    EndAllMus();

    if (GetUnitCurrentHp(unitA) != 0)
    {
        ShowUnitSprite(unitA);
        unitA->state &= ~US_HIDDEN;
    }

    LABattleMap_RefreshMapAndSprites();
    BG_SetPosition(2, 0, 0);

    if ((GetUnitCurrentHp(unitA) == gBattleActor.hpInitial) && (GetUnitCurrentHp(unitB) == gBattleTarget.hpInitial))
    {
        gSioBmSt.unk_09++;
    }
    else
    {
        gSioBmSt.unk_09 = 0;
    }

    Proc_Break(proc);

    return;
}

//! FE8U = 0x0804A7C0
void LABattleMap_WarpCombatantsToRoster(ProcPtr proc)
{
    u8 unitIdA = gLinkArenaUnitSlots[gSioBmSt.unk_04];
    u8 unitIdB = gLinkArenaUnitSlots[gSioBmSt.unk_05];

    struct Unit * unitA = GetUnit(unitIdA);
    struct Unit * unitB = GetUnit(unitIdB);

    s8 flag = 0;

    int indexA = LABattleMap_GetUnitSlot(unitIdA);
    int indexB = LABattleMap_GetUnitSlot(unitIdB);

    gLinkArenaCombatantMus[1] = NULL;
    gLinkArenaCombatantMus[0] = NULL;

    if (((unitA->state & (US_DEAD | US_BIT16)) != 0) || (unitA->pCharacterData == NULL))
    {
        gSioBmSt.unk_0A[unitIdA >> 6]--;
    }
    else
    {
        gLinkArenaCombatantMus[0] = (void *)StartMu(unitA);
        DisableMuCamera(gLinkArenaCombatantMus[0]);

        unitA->state |= US_HIDDEN;

        flag = 1;

        StartSioWarpFx(
            unitA, gLinkArenaCombatantMus[0], gLinkArenaSlotPositions[indexA].x, gLinkArenaSlotPositions[indexA].y, 2, flag, proc);
    }

    if (((unitB->state & (US_DEAD | US_BIT16)) != 0) || (unitB->pCharacterData == NULL))
    {
        gSioBmSt.unk_0A[unitIdB >> 6]--;
    }
    else
    {
        gLinkArenaCombatantMus[1] = (void *)StartMu(unitB);
        DisableMuCamera(gLinkArenaCombatantMus[1]);

        unitB->state |= US_HIDDEN;

        if (!flag)
        {
            flag = 1;
        }
        else
        {
            flag = 0;
        }

        StartSioWarpFx(
            unitB, gLinkArenaCombatantMus[1], gLinkArenaSlotPositions[indexB].x, gLinkArenaSlotPositions[indexB].y, 2, flag, proc);
    }

    LABattleMap_RefreshMapAndSprites();

    Proc_Break(proc);

    return;
}

//! FE8U = 0x0804A914
void LABattleMap_EliminateTeamsWithoutAttackers(void)
{
    int i;

    for (i = 0; i < 4; i++)
    {
        int j;
        int countA;
        int countB;

        if (SioIsPlayerConnected(i) == 0)
        {
            continue;
        }

        countA = 0;
        countB = 0;

        for (j = 0; j < 5; j++)
        {
            struct Unit * unit;

            if (gLinkArenaUnitSlots[i + j * 5] == 0)
            {
                continue;
            }

            countB++;

            unit = GetUnit(gLinkArenaUnitSlots[i + j * 5]);

            if ((unit->state & (US_DEAD | US_BIT16)) != 0)
            {
                continue;
            }

            if (UnitHasUsableWeapon(unit) == 1)
            {
                countA++;
            }
        }

        if ((countA == 0) && (countB != 0))
        {
            gSioBmSt.unk_0A[i] = 0;
        }
    }

    return;
}

//! FE8U = 0x0804A9A4
void LABattleMap_RecordEliminationAndAdvance(ProcPtr proc)
{
    u8 r4_;

    u8 r6 = gLinkArenaUnitSlots[gSioBmSt.unk_04];
    u8 r7 = gLinkArenaUnitSlots[gSioBmSt.unk_05];

    struct Unit * r4 = GetUnit(r6);
    struct Unit * r2 = GetUnit(r7);

    if ((r4->state & (US_DEAD | US_BIT16)) == 0)
    {
        r4->state &= ~US_HIDDEN;
    }

    if ((r2->state & (US_DEAD | US_BIT16)) == 0)
    {
        r2->state &= ~US_HIDDEN;
    }

    if (gSioBmSt.unk_0A[r6 >> 6] == 0)
    {
        r4_ = r6 >> 6;
    }
    else if (gSioBmSt.unk_0A[r7 >> 6] == 0)
    {
        r4_ = r7 >> 6;
    }
    else
    {
        goto _end;
    }

    gSioBmSt.unk_0F[gLinkArenaSt.unk_A0 - gSioBmSt.unk_0E] = r4_;
    gSioBmSt.unk_0E++;

    if (gSioBmSt.unk_0E == gLinkArenaSt.unk_A0)
    {

        if (gSioBmSt.unk_0A[r6 >> 6] != 0)
        {
            r4_ = r6 >> 6;
        }
        else
        {
            r4_ = r7 >> 6;
        }

        gSioBmSt.unk_0F[0] = r4_;

        LABattleMap_SetNextPhase(0xff);

        Proc_Break(proc);

        return;
    }

_end:
    LABattleMap_SetNextPhase(gPlaySt.faction);
    Proc_Break(proc);

    return;
}

//! FE8U = 0x0804AA88
void LABattleMap_RefreshAfterCombat(void)
{
    EndAllMus();
    EndAllMus();

    LABattleMap_RefreshUnitMap();
    LABattleMap_RefreshUnitSlots();

    RefreshUnitSprites();

    return;
}

//! FE8U = 0x0804AAA4
void LABattleMap_SendBuzzOnBPress(void)
{
    if ((gKeyStatusPtr->newKeys & B_BUTTON) != 0)
    {
        gSioMsgBuf.kind = SIO_MSG_84;
        gSioMsgBuf.sender = gSioSt->selfId;
        gSioMsgBuf.param = 0;
        SioSend(&gSioMsgBuf, sizeof(gSioMsgBuf));
    }

    return;
}

//! FE8U = 0x0804AADC
bool LABattleMap_IsAttackOrEndMsg(void * data)
{
    u8 * cast = data;

    switch (cast[0])
    {
        case 1:
        case 6:
        case 7:
            return true;
    }

    return false;
}

CONST_DATA struct PopupInstruction PopupScr_LinkArenaTeamSurrendered[] = {
    POPUP_STR(gUnknown_03001850),
    POPUP_SPACE(3),
    POPUP_MSG(MSG_757), // The team surrendered.
    POPUP_END
};

CONST_DATA struct PopupInstruction PopupScr_LinkArenaNoDamage[] = {
    POPUP_MSG(MSG_758), // No one can do damage this turn
    POPUP_END
};

//! FE8U = 0x0804AAFC
void LABattleMap_ReceiveAttackerSelection(struct SioProc85AA4CC * proc)
{
    u8 buf[4];

    u16 got = SioReceiveData(gLinkArenaCmdBuf, buf, LABattleMap_IsAttackOrEndMsg);

    if (got != 0)
    {
        switch (gLinkArenaCmdBuf[0])
        {
            case 1:
                LABattleMap_StartUnitMoveOut(gLinkArenaCmdBuf[1], 0, &gSioBmSt.unk_04, &proc->unk_2c, &proc->unk_30);
                Proc_Goto(proc, 1);

                break;

            case 6:
                EndLinkArenaPointsBox();
                SioStrCpy(gUnk_Sio_0203DAC5[buf[0]], gUnknown_03001850);
                NewPopup_Simple(PopupScr_LinkArenaTeamSurrendered, 0x60, 0, 0);

                Proc_Goto(proc, 3);

                break;

            case 7:
                EndLinkArenaPointsBox();
                NewPopup_Simple(PopupScr_LinkArenaNoDamage, 0x60, 0, 0);

                Proc_Goto(proc, 4);

                break;
        }
    }

    LABattleMap_SendBuzzOnBPress();

    return;
}

//! FE8U = 0x0804ABB4
bool LABattleMap_IsTargetOrCancelMsg(void * data)
{
    u8 * cast = data;

    switch (cast[0])
    {
        case 2:
        case 3:
            return true;
    }

    return false;
}

//! FE8U = 0x0804ABCC
void LABattleMap_ReceiveTargetSelection(struct SioProc85AA4CC * proc)
{
    struct Unit * unit;
    u8 buf[4];

    u16 got = SioReceiveData(gLinkArenaCmdBuf, buf, LABattleMap_IsTargetOrCancelMsg);

    if (got != 0)
    {
        switch (gLinkArenaCmdBuf[0])
        {
            case 2:
                unit = GetUnit(gLinkArenaCmdBuf[2]);

                if ((unit->state & US_BIT9) == 0)
                {
                    EndMu(gLinkArenaCombatantMus[0]);
                }
                else
                {
                    unit->xPos = proc->unk_2c;
                    unit->yPos = proc->unk_30;
                }

                unit->state &= 0xfffffffe;

                RefreshUnitSprites();

                Proc_Goto(proc, 0);

                break;

            case 3:
                LABattleMap_StartUnitMoveOut(gLinkArenaCmdBuf[1], 1, &gSioBmSt.unk_05, &proc->unk_34, &proc->unk_38);
                Proc_Goto(proc, 2);
                break;
        }
    }

    LABattleMap_SendBuzzOnBPress();

    return;
}

//! FE8U = 0x0804AC68
void LABattleMap_StartMuAndSavePosition(struct Unit * unit, int idx, int * xOut, int * yOut)
{
    gLinkArenaCombatantMus[idx] = StartMu(unit);

    *xOut = unit->xPos;
    *yOut = unit->yPos;

    unit->state &= ~US_BIT9;

    return;
}

//! FE8U = 0x0804ACAC
bool LABattleMap_IsForecastMsg(void * data)
{
    u8 * cast = data;

    switch (cast[0])
    {
        case 4:
        case 5:
            return true;
    }

    return false;
}

//! FE8U = 0x0804ACC4
void LABattleMap_ReceiveForecastResponse(struct SioProc85AA4CC * proc)
{
    struct Unit * unitA;
    struct Unit * unitB;
    u8 buf[4];

    u16 got = SioReceiveData(gLinkArenaCmdBuf, buf, LABattleMap_IsForecastMsg);

    if (got != 0)
    {
        switch (gLinkArenaCmdBuf[0])
        {
            case 4:
                unitA = GetUnit(gLinkArenaCmdBuf[2]);

                if ((unitA->state & US_BIT9) == 0)
                {
                    EndMu(gLinkArenaCombatantMus[1]);
                }
                else
                {
                    unitA->xPos = proc->unk_34;
                    unitA->yPos = proc->unk_38;
                }

                unitA->state &= ~US_HIDDEN;

                RefreshUnitSprites();
                Proc_Goto(proc, 1);

                break;

            case 5:
                unitA = GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_04]);
                unitB = GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_05]);

                if ((unitA->state & US_BIT9) != 0)
                {
                    LABattleMap_StartMuAndSavePosition(unitA, 0, &proc->unk_2c, &proc->unk_30);
                }

                if ((unitB->state & US_BIT9) != 0)
                {
                    LABattleMap_StartMuAndSavePosition(unitB, 1, &proc->unk_34, &proc->unk_38);
                }

                Proc_Break(proc);

                break;
        }
    }

    LABattleMap_SendBuzzOnBPress();

    return;
}

//! FE8U = 0x0804ADA0
int LABattleMap_CpuGetUnitBestAttack(struct Unit * unit)
{
    int i;

    u16 bestItem = 0;
    u32 bestMight = 0;

    for (i = 0; i < UNIT_ITEM_COUNT; i++)
    {
        u16 item = unit->items[i];

        if (item == 0)
        {
            break;
        }

        if (!CanUnitUseWeapon(unit, item))
        {
            continue;
        }

        if (GetItemMight(item) <= bestMight)
        {
            continue;
        }

        bestItem = item;
        bestMight = GetItemMight(item);
    }

    if (bestItem == 0)
    {
        return 0;
    }

    return bestMight + GetUnitPower(unit);
}

//! FE8U = 0x0804AE08
int LABattleMap_GetTeamCombatScore(int playerId)
{
    int i;

    int count = 0;
    int score = 0;

    for (i = playerId; i < playerId + 5; i++)
    {
        struct Unit * unit = GetUnit(i);

        if ((unit->state & (US_DEAD | US_BIT16)) != 0)
        {
            continue;
        }

        if (unit->pCharacterData == NULL)
        {
            continue;
        }

        count++;

        score += LABattleMap_CpuGetUnitBestAttack(unit);
        score += GetUnitCurrentHp(unit);
    }

    score += gSioBmSt.currentScore[playerId >> 6];

    score = Div(score, count);

    return score;
}

//! FE8U = 0x0804AE7C
bool LABattleMap_CpuCheckSurrenderKey(struct SioBattleMapProc * proc, int b)
{
    if ((gKeyStatusPtr->heldKeys & START_BUTTON) != 0)
    {
        EndLinkArenaPointsBox();
        proc->unk_58 = b;

        if (!gPlaySt.config.disableSoundEffects)
        {
            // Another non-usage of the PlaySoundEffects macro
            m4aSongNumStart(SONG_68);
            Proc_Goto(proc, 3);
        }

        return true;
    }

    return false;
}

//! FE8U = 0x0804AEC4
void LABattleMap_CpuPickWeakestTeam(ProcPtr proc)
{
    int i;

    int bestScore = -1;

    if (LABattleMap_CpuCheckSurrenderKey(proc, 0) == true)
    {
        return;
    }

    for (i = 0; i < 4; i++)
    {
        u32 score;

        if (!SioIsPlayerConnected(i))
        {
            continue;
        }

        if (gSioBmSt.unk_0A[i] == 0)
        {
            continue;
        }

        if (gPlaySt.faction == i)
        {
            continue;
        }

        score = LABattleMap_GetTeamCombatScore(i * 0x40 + 1);

        if (bestScore <= score)
        {
            continue;
        }

        bestScore = score;
        gSioBmSt.unk_02 = i;
    }

    Proc_Break(proc);

    return;
}

//! FE8U = 0x0804AF2C
int LABattleMap_CpuGetRangeVsTarget(int unused, struct Unit * unit)
{
    u16 weapon = GetUnitEquippedWeapon(unit);

    if (weapon == 0)
    {
        return 1;
    }

    if (GetItemMaxRange(weapon) == 1)
    {
        return 1;
    }

    if (GetItemMinRange(weapon) > 1)
    {
        return 2;
    }

    return 1;
}

//! FE8U = 0x0804AF5C
void LABattleMap_CpuPickBestAttack(struct SioBattleMapProc * proc)
{
    struct AiCombatSimulationSt sim;
    int i;
    int bestSlot;
    int slot;
    int allegiance;

    int bestScore = 0;
    u8 selectedUnitId = 0;
    u8 targetUnitId = 0;

    if (LABattleMap_CpuCheckSurrenderKey(proc, 1) == true)
    {
        return;
    }

    gAiState.combatWeightTableId = 0xe;
    allegiance = gPlaySt.faction * 0x40;

    for (i = allegiance + 1; i < allegiance + 6; i++)
    {
        gActiveUnitId = i;
        gActiveUnit = GetUnit(gActiveUnitId);

        if ((gActiveUnit->state & (US_DEAD | US_BIT16)) != 0)
        {
            continue;
        }

        if (gActiveUnit->pCharacterData == NULL)
        {
            continue;
        }

        for (slot = 0; slot < UNIT_ITEM_COUNT; slot++)
        {
            int targetFaction;
            int j;
            int flags;

            u16 item = gActiveUnit->items[slot];

            if (item == 0)
            {
                continue;
            }

            if (!CanUnitUseWeapon(gActiveUnit, item))
            {
                continue;
            }

            bestSlot = slot;
            flags = 0;

            if (GetItemMinRange(item) > 2)
            {
                continue;
            }

            if ((GetItemAttributes(item) & IA_UNCOUNTERABLE) != 0)
            {
                continue;
            }

            if (GetItemMinRange(item) == 1)
            {
                flags |= 2;
            }

            if (GetItemMaxRange(item) > 1)
            {
                flags |= 1;
            }

            sim.itemSlot = bestSlot;

            targetFaction = gSioBmSt.unk_02 * 0x40;

            for (j = targetFaction + 1; j < targetFaction + 6; j++)
            {
                struct AiCombatSimulationSt * simp = &sim;
                int flags2;
                u8 * r7 = gLinkArenaCmdBuf;
                struct Unit * unit = GetUnit(j);

                if ((unit->state & (US_DEAD | US_BIT16)) != 0)
                {
                    continue;
                }

                if (unit->pCharacterData == NULL)
                {
                    continue;
                }

                flags2 = flags & 2; // permuter
                simp->targetId = j;

                if (((u8)flags2) != 0)
                {
                    simp->xMove = unit->xPos + 1;
                    simp->yMove = unit->yPos;

                    AiSimulateBattleAgainstTargetAtPosition(&sim);

                    if (bestScore <= sim.score)
                    {
                        bestScore = sim.score;
                        selectedUnitId = gActiveUnitId;
                        targetUnitId = j;

                        if (flags == 3)
                        {
                            r7[2] = LABattleMap_CpuGetRangeVsTarget(3, unit);
                        }
                        else
                        {
                            r7[2] = 1;
                        }
                        r7[3] = bestSlot;
                    }
                }

                if ((flags & 1) != 0)
                {
                    simp->xMove = unit->xPos + 1;
                    simp->yMove = unit->yPos - 1;

                    AiSimulateBattleAgainstTargetAtPosition(&sim);

                    if (bestScore <= sim.score)
                    {
                        bestScore = sim.score;
                        selectedUnitId = gActiveUnitId;
                        targetUnitId = j;

                        if (flags == 3)
                        {
                            r7[2] = LABattleMap_CpuGetRangeVsTarget(3, unit);
                        }
                        else
                        {
                            r7[2] = 2;
                        }
                        r7[3] = bestSlot;
                    }
                }
            }
        }
    }

    LABattleMap_StartUnitMoveOut(selectedUnitId, 0, &gSioBmSt.unk_04, &proc->unk_2c, &proc->unk_30);
    gLinkArenaCmdBuf[1] = targetUnitId;

    Proc_Break(proc);

    return;
}

//! FE8U = 0x0804B190
void LABattleMap_CpuStartTargetCursor(ProcPtr proc)
{
    struct Unit * unit = GetUnit(gLinkArenaCmdBuf[1]);

    StartAiTargetCursor(unit->xPos * 16, unit->yPos * 16, 2, proc);

    return;
}

//! FE8U = 0x0804B1C0
void LABattleMap_CpuPrepareCombatants(struct SioProc85AA4CC * proc)
{
    struct Unit * unitA;
    struct Unit * unitB;

    LABattleMap_StartUnitMoveOut(gLinkArenaCmdBuf[1], 1, &gSioBmSt.unk_05, &proc->unk_34, &proc->unk_38);

    unitA = GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_04]);
    unitB = GetUnit(gLinkArenaUnitSlots[gSioBmSt.unk_05]);

    if ((unitA->state & US_BIT9) != 0)
    {
        LABattleMap_StartMuAndSavePosition(unitA, 0, &proc->unk_2c, &proc->unk_30);
    }

    if ((unitB->state & US_BIT9) != 0)
    {
        LABattleMap_StartMuAndSavePosition(unitB, 1, &proc->unk_34, &proc->unk_38);
    }

    return;
}

//! FE8U = 0x0804B250
void LABattleMap_CpuWaitCombatantMu(ProcPtr proc)
{
    if (LABattleMap_CpuCheckSurrenderKey(proc, 2) == 1)
    {
        return;
    }

    if (MuExistsActive() != 0)
    {
        return;
    }

    Proc_Break(proc);

    return;
}

// clang-format off

CONST_DATA u8 gLut_LinkArenaFogPlaceholder_YOffset[] =
{
    0, 0, 0, 0, 0, 0, 0,
    1, 1,
    2, 2, 2,
    3, 3, 3, 3,
    4, 4, 4, 4, 4, 4, 4,
    3, 3,
    2, 2, 2,
    1, 1, 1, 1
};

// clang-format on

//! FE8U = 0x0804B278
void LinkArenaFogSprite_Loop(void)
{
    int i;
    int j;

    int yOffset = (gLut_LinkArenaFogPlaceholder_YOffset[GetGameClock() & 0x1f] + 4) >> 1;

    for (i = 0; i < 4; i++)
    {
        if (!SioIsPlayerConnected(gSioSideToPlayerLut[gSioSt->selfId][i]))
        {
            continue;
        }

        for (j = 0; j < 5; j++)
        {
            struct Unit * unit = GetUnit(gLinkArenaUnitSlots[i * 5 + j]);

            if (!UNIT_IS_VALID(unit))
            {
                continue;
            }

            if (!(unit->state & US_BIT9))
            {
                continue;
            }

            CallARM_PushToSecondaryOAM(
                unit->xPos * 16, unit->yPos * 16 - yOffset, gObject_16x8,
                OAM2_PAL(GetUnitDisplayedSpritePalette(unit)) + OAM2_CHR(0x1F0) + OAM2_LAYER(2));
            CallARM_PushToSecondaryOAM(
                unit->xPos * 16, (unit->yPos * 16 - yOffset) + 8, gObject_16x8,
                OAM2_PAL(GetUnitDisplayedSpritePalette(unit)) + OAM2_CHR(0x1F2) + OAM2_LAYER(2));
        }
    }

    return;
}

// clang-format off

struct ProcCmd CONST_DATA ProcScr_DrawLinkArenaFogPlaceholders[] =
{
    PROC_15,
    PROC_MARK(PROC_MARK_DISP),
    PROC_YIELD,

    PROC_REPEAT(LinkArenaFogSprite_Loop),

#ifdef BUGFIX
    PROC_END,
#endif
};

// clang-format on

//! FE8U = 0x0804B38C
void StartLinkArenaFogPlaceholders(void)
{
    Proc_Start(ProcScr_DrawLinkArenaFogPlaceholders, PROC_TREE_4);
    return;
}

//! FE8U = 0x0804B3A0
void EndLinkArenaFogPlaceholders(void)
{
    Proc_EndEach(ProcScr_DrawLinkArenaFogPlaceholders);
    return;
}

//! FE8U = 0x0804B3B0
void LABattleMap_StartStatScreen(ProcPtr proc)
{
    SetStatScreenConfig(
        STATSCREEN_CONFIG_NONDEAD | STATSCREEN_CONFIG_NONBENCHED | STATSCREEN_CONFIG_NONUNK9 |
        STATSCREEN_CONFIG_NONROOFED | STATSCREEN_CONFIG_NONUNK16);
    StartStatScreen(gActiveUnit, proc);
    return;
}

//! FE8U = 0x0804B3D0
void LABattleMap_HandleSurrenderPrompt(ProcPtr proc)
{
    if (gSioBmSt.unk_08 == 0)
    {
        Proc_Goto(proc, 0);
        return;
    }

    EndAllMus();
    LABattleMap_SendCmd(6, gPlaySt.faction, 0, 0);

    return;
}

//! FE8U = 0x0804B408
void LABattleMap_HandleNoDamagePrompt(ProcPtr proc)
{
    if (gSioBmSt.unk_08 == 0)
    {
        gSioBmSt.unk_09 = 0;
        Proc_Goto(proc, 0);
        return;
    }

    EndAllMus();
    LABattleMap_SendCmd(7, gPlaySt.faction, 0, 0);

    return;
}

//! FE8U = 0x0804B43C
void LABattleMap_CpuHandleSurrenderPrompt(struct SioBattleMapProc * proc)
{
    if (gSioBmSt.unk_08 == 0)
    {
        Proc_Goto(proc, proc->unk_58);
        return;
    }

    EndAllMus();
    EndAllMus();

    gLinkArenaSt.unk_0B = 1;

    LABattleMap_SetNextPhase(0xff);

    Proc_Goto(proc, 5);

    return;
}

//! FE8U = 0x0804B480
void LABattleMap_RecordSurrenderRanking(struct SioBattleMapProc * proc)
{
    int i;
    int r6 = 0;

    if (gLinkArenaSt.unk_00 == 1)
    {
        gLinkArenaSt.unk_0B = 1;
        LABattleMap_SetNextPhase(0xff);
        Proc_Goto(proc, 8);
        return;
    }

    gSioBmSt.unk_0F[gLinkArenaSt.unk_A0 - gSioBmSt.unk_0E] = gPlaySt.faction;
    gSioBmSt.unk_0E++;

    gSioBmSt.currentScore[gPlaySt.faction] = 0;

    if (gSioBmSt.unk_0E == gLinkArenaSt.unk_A0)
    {
        for (i = 0; i < gLinkArenaSt.unk_A0; i++)
        {
            if (gSioBmSt.unk_0A[i] != 0)
            {
                r6 = i;
            }
        }

        gSioBmSt.unk_0F[0] = r6;

        LABattleMap_SetNextPhase(0xff);
        Proc_Goto(proc, 8);

        return;
    }

    LABattleMap_SetNextPhase(gPlaySt.faction);
    Proc_Goto(proc, 8);

    return;
}

//! FE8U = 0x0804B518
void LABattleMap_SetQuitEndState(ProcPtr proc)
{
    if (gLinkArenaSt.unk_00 == 1)
    {
        gLinkArenaSt.unk_0B = 1;
        LABattleMap_SetNextPhase(0xff);

        Proc_Goto(proc, 8);

        return;
    }

    gLinkArenaSt.unk_0B = 2;
    LABattleMap_SetNextPhase(0xff);

    Proc_Goto(proc, 8);

    return;
}

//! FE8U = 0x0804B554
void LABattleMap_RecordRemoteSurrenderRanking(struct SioBattleMapProc * proc)
{
    int i;
    int r6 = 0;

    gSioBmSt.unk_0F[gLinkArenaSt.unk_A0 - gSioBmSt.unk_0E] = gPlaySt.faction;
    gSioBmSt.unk_0E++;

    gSioBmSt.currentScore[gPlaySt.faction] = 0;

    if (gSioBmSt.unk_0E == gLinkArenaSt.unk_A0)
    {
        for (i = 0; i < gLinkArenaSt.unk_A0; i++)
        {
            if (gSioBmSt.unk_0A[i] != 0)
            {
                r6 = i;
            }
        }

        gSioBmSt.unk_0F[0] = r6;

        LABattleMap_SetNextPhase(0xff);
        Proc_Goto(proc, 5);

        return;
    }

    LABattleMap_SetNextPhase(gPlaySt.faction);
    Proc_Goto(proc, 5);

    return;
}

//! FE8U = 0x0804B5E0
void LABattleMap_RemoteSetQuitEndState(ProcPtr proc)
{
    gLinkArenaSt.unk_0B = 2;
    LABattleMap_SetNextPhase(0xff);

    Proc_Goto(proc, 5);

    return;
}

//! FE8U = 0x0804B604
void LAUnitDeaths_Init(struct SioBattleMapProc * proc)
{
    proc->unk_58 = 0;

    proc->unk_5c = gPlaySt.faction * 0x40;
    gSioBmSt.unk_0A[gPlaySt.faction] = 0;

    return;
}

//! FE8U = 0x0804B624
void LAUnitDeaths_FindNextAndStart(struct SioBattleMapProc * proc)
{
    struct Unit * unit;
    struct MuProc * mu;

    while (1)
    {
        if (proc->unk_58 == 5)
        {
            Proc_Goto(proc, 1);
            return;
        }

        unit = GetUnit(proc->unk_5c + proc->unk_58 + 1);

        if ((unit->state & (US_DEAD | US_BIT16)) != 0)
        {
            proc->unk_58++;
            continue;
        }

        if (unit->pCharacterData == NULL)
        {
            proc->unk_58++;
            continue;
        }

        break;
    }

    RefreshUnitSprites();
    HideUnitSprite(unit);

    mu = StartMu(unit);

    gWorkingMovementScript[0] = MOVE_CMD_MOVE_DOWN;
    gWorkingMovementScript[1] = MOVE_CMD_HALT;

    SetMuMoveScript(mu, gWorkingMovementScript);

    StartLinkArenaMUDeathFade(mu);

    proc->unk_54 = mu;
    proc->unk_58++;

    unit->state &= ~US_BIT9;
    unit->state |= (US_HIDDEN | US_DEAD);

    return;
}

//! FE8U = 0x0804B6AC
void LAUnitDeaths_EndMu(struct SioBattleMapProc * proc)
{
    EndMu(proc->unk_54);
    return;
}

//! FE8U = 0x0804B6B8
void LAUnitDeaths_OnEnd(void)
{
    LABattleMap_RefreshUnitMap();
    LABattleMap_RefreshUnitSlots();

    RefreshUnitSprites();

    return;
}

// clang-format off

struct ProcCmd CONST_DATA ProcScr_LASurrender_HandleUnitDeaths[] =
{
    PROC_CALL(LAUnitDeaths_Init),

PROC_LABEL(0),
    PROC_CALL(LAUnitDeaths_FindNextAndStart),
    PROC_SLEEP(32),
    PROC_CALL(LAUnitDeaths_EndMu),

    PROC_GOTO(0),

PROC_LABEL(1),
    PROC_CALL(LAUnitDeaths_OnEnd),

    PROC_END,
};

// clang-format on

//! FE8U = 0x0804B6CC
void LinkArena_StoreTalkChoice(void)
{
    if (GetTalkChoiceResult() == TALK_CHOICE_YES)
    {
        gSioBmSt.unk_08 = 1;
        return;
    }

    gSioBmSt.unk_08 = 0;

    return;
}

// clang-format off

CONST_DATA EventScr EventScr_LinkArenaSurrenderPrompt[] =
{
    STAL(1)
    EVBIT_T(7)
    EVBIT_MODIFY(0x4)
    TEXTSHOW(MSG_88C) // do you surrender?
    TEXTEND
    REMA
    ASMC(LinkArena_StoreTalkChoice)
    ENDA
};

// clang-format on

//! FE8U = 0x0804B6F4
void LABattleMap_StartSurrenderPrompt(void)
{
    CallEvent((u16 *)EventScr_LinkArenaSurrenderPrompt, EV_EXEC_CUTSCENE);
    return;
}

// clang-format off

CONST_DATA EventScr EventScr_LinkArenaNoDamagePrompt[] =
{
    STAL(1)
    EVBIT_T(7)
    EVBIT_MODIFY(0x4)
    TEXTSHOW(MSG_88D) // You can do no damage. Would you like to quit?
    TEXTEND
    REMA
    ASMC(LinkArena_StoreTalkChoice)
    ENDA
};

// clang-format on

//! FE8U = 0x0804B708
void LABattleMap_StartNoDamagePrompt(void)
{
    CallEvent((u16 *)EventScr_LinkArenaNoDamagePrompt, EV_EXEC_CUTSCENE);
    return;
}

//! FE8U = 0x0804B71C
void LABattleMap_StartBonusPointsHelp(struct SioBattleMapProc * proc)
{
    int i;

    LoadHelpBoxGfx(OBJ_CHR_ADDR(0x280), 6);
    StartHelpBoxExt_NoHelpSprite(64, 56, MSG_756); // "Each unit receives 30 extra pts."

    for (i = 0; i < 4; i++)
    {
        if (!SioIsPlayerConnected(i))
        {
            continue;
        }

        if (gSioBmSt.unk_0A[i] == 0)
        {
            continue;
        }

        proc->unk_58 = i;
    }

    proc->unk_5c = 0;

    return;
}

//! FE8U = 0x0804B76C
void LABattleMap_AwardBonusPoints_Loop(struct SioBattleMapProc * proc)
{
    struct Unit * unit;

    while (1)
    {
        if (proc->unk_5c > 4)
        {
            CloseHelpBox();
            Proc_Break(proc);
            return;
        }

        unit = GetUnit(proc->unk_58 * 0x40 + proc->unk_5c + 1);

        if ((unit->state & (US_DEAD | US_BIT16)) != 0)
        {
            proc->unk_5c++;
            continue;
        }

        if (unit->pCharacterData == NULL)
        {
            proc->unk_5c++;
            continue;
        }

        break;
    }

    gSioBmSt.unk_2c[proc->unk_58].newScore = 30;
    gSioBmSt.unk_2c[proc->unk_58].unitId = proc->unk_58 * 0x40 + proc->unk_5c + 1;

    StartSioPoints2(proc);

    proc->unk_5c++;

    return;
}

//! FE8U = 0x0804B7E4
void sub_804B7E4(ProcPtr proc)
{
    if (gLinkArenaSt.unk_00 == 1)
    {
        Proc_Goto(proc, 1);
    }

    return;
}

//! FE8U = 0x0804B800
void LinkArena_BeginBattleAnimations(void)
{
    BG_Fill(gBG2TilemapBuffer, 0);
    BG_EnableSyncByMask(BG2_SYNC_BIT);

    RenderBmMap();

    if (sub_8055BB4())
    {
        SetBanimLinkArenaFlag(1);
        BeginAnimsOnBattleAnimations();

        return;
    }

    EndAllMus();
    RenderBmMap();

    BeginBattleMapAnims();
    gBattleStats.config |= BATTLE_CONFIG_MAPANIMS;

    return;
}

//! FE8U = 0x0804B850
void LinkArena_PostCombatDeathFades(struct SioBattleMapProc * proc)
{
    struct MuProc * mu;

    if (gBattleActor.unit.curHP == 0)
    {
        mu = Proc_Find(ProcScr_Mu);
        StartLinkArenaMUDeathFade(mu);
        proc->unk_54 = mu;
    }

    if (gBattleTarget.unit.curHP == 0)
    {
        RefreshUnitSprites();

        HideUnitSprite(GetUnit(gBattleTarget.unit.index));

        mu = StartMu(&gBattleTarget.unit);

        gWorkingMovementScript[0] = GetFacingDirection(
            gBattleActor.unit.xPos, gBattleActor.unit.yPos, gBattleTarget.unit.xPos, gBattleTarget.unit.yPos);
        gWorkingMovementScript[1] = MOVE_CMD_HALT;

        SetMuMoveScript(mu, gWorkingMovementScript);
        StartLinkArenaMUDeathFade(mu);

        proc->unk_54 = mu;
    }

    return;
}

//! FE8U = 0x0804B8D0
void LinkArena_MarkDeadCombatants(void)
{
    struct Unit * unitA = GetUnit(gBattleActor.unit.index);
    struct Unit * unitB = GetUnit(gBattleTarget.unit.index);

    if (GetUnitCurrentHp(unitA) == 0)
    {
        unitA->state |= (US_HIDDEN | US_DEAD);
    }

    if (GetUnitCurrentHp(unitB) == 0)
    {
        unitB->state |= (US_HIDDEN | US_DEAD);
    }

    return;
}

struct ProcCmd CONST_DATA ProcScr_LABattleMap_LocalPhase[] = {
PROC_LABEL(0),
    PROC_REPEAT(LABattleMap_CheckNoDamageStall),
    PROC_CALL(LABattleMap_StartPointsBox),
PROC_LABEL(1),
    PROC_REPEAT(LABattleMap_AwaitSioSendQueue),
    PROC_REPEAT(LABattleMap_SelectAttacker_Loop),
PROC_LABEL(4),
    PROC_CALL(EndLinkArenaPointsBox),
    PROC_YIELD,
    PROC_CALL(LABattleMap_StartStatScreen),
    PROC_YIELD,
    PROC_CALL(LABattleMap_InitDisplay),
    PROC_CALL(RefreshBMapGraphics),
    PROC_GOTO(0),
PROC_LABEL(2),
    PROC_SLEEP(1),
    PROC_CALL(LABattleMap_StartSurrenderPrompt),
    PROC_YIELD,
    PROC_CALL(LABattleMap_HandleSurrenderPrompt),
    PROC_START_CHILD_BLOCKING(ProcScr_LASurrender_HandleUnitDeaths),
    PROC_YIELD,
    PROC_CALL(LABattleMap_RecordSurrenderRanking),
PROC_LABEL(3),
    PROC_SLEEP(1),
    PROC_CALL(LABattleMap_StartNoDamagePrompt),
    PROC_YIELD,
    PROC_CALL(LABattleMap_HandleNoDamagePrompt),
    PROC_CALL(LABattleMap_SetQuitEndState),
PROC_LABEL(5),
    PROC_REPEAT(LABattleMap_AwaitSioSendQueue),
    PROC_REPEAT(LABattleMap_SelectTarget_Loop),
PROC_LABEL(6),
    PROC_CALL(EndLinkArenaPointsBox),
    PROC_YIELD,
    PROC_CALL(LABattleMap_StartStatScreen),
    PROC_YIELD,
    PROC_CALL(LABattleMap_InitDisplay),
    PROC_CALL(RefreshBMapGraphics),
    PROC_CALL(LABattleMap_StartPointsBox),
    PROC_CALL(LABattleMap_StartSelectedUnitMu),
    PROC_GOTO(5),
PROC_LABEL(7),
    PROC_REPEAT(LABattleMap_StartForecast),
    PROC_CALL(EndLinkArenaPointsBox),
    PROC_CALL(LABattleMap_HandleWeaponSelectResult),
    PROC_CALL(LABattleMap_StartCombatantWarpFx),
    PROC_YIELD,
    PROC_CALL(LABattleMap_PrepareCombatantSprites),
    PROC_REPEAT(LABattleMap_StartCombat),
    PROC_REPEAT(LABattleMap_ApplyCombatResults),
    PROC_CALL(StartSioPoints1),
    PROC_YIELD,
    PROC_CALL(EndLinkArenaPointsBox),
    PROC_REPEAT(LABattleMap_WarpCombatantsToRoster),
    PROC_REPEAT(LABattleMap_RecordEliminationAndAdvance),
    PROC_CALL(LABattleMap_RefreshAfterCombat),
PROC_LABEL(8),
    PROC_END,
};

struct ProcCmd CONST_DATA ProcScr_LABattleMap_RemotePhase[] = {
PROC_LABEL(0),
    PROC_CALL(LABattleMap_StartPointsBox),
    PROC_REPEAT(LABattleMap_ReceiveAttackerSelection),
PROC_LABEL(1),
    PROC_REPEAT(LABattleMap_ReceiveTargetSelection),
    PROC_GOTO(2),
PROC_LABEL(3),
    PROC_START_CHILD_BLOCKING(ProcScr_LASurrender_HandleUnitDeaths),
    PROC_YIELD,
    PROC_WHILE_EXISTS(ProcScr_Popup),
    PROC_CALL(LABattleMap_RecordRemoteSurrenderRanking),
PROC_LABEL(4),
    PROC_WHILE_EXISTS(ProcScr_Popup),
    PROC_CALL(LABattleMap_RemoteSetQuitEndState),
PROC_LABEL(2),
    PROC_REPEAT(LABattleMap_ReceiveForecastResponse),
    PROC_CALL(EndLinkArenaPointsBox),
    PROC_CALL(LABattleMap_StartCombatantWarpFx),
    PROC_YIELD,
    PROC_CALL(LABattleMap_PrepareCombatantSprites),
    PROC_REPEAT(LABattleMap_StartCombat),
    PROC_REPEAT(LABattleMap_ApplyCombatResults),
    PROC_CALL(StartSioPoints1),
    PROC_YIELD,
    PROC_CALL(EndLinkArenaPointsBox),
    PROC_REPEAT(LABattleMap_WarpCombatantsToRoster),
    PROC_REPEAT(LABattleMap_RecordEliminationAndAdvance),
    PROC_CALL(LABattleMap_RefreshAfterCombat),
PROC_LABEL(5),
    PROC_END,
};

struct ProcCmd CONST_DATA ProcScr_LABattleMap_CpuPhase[] = {
    PROC_CALL(LABattleMap_StartPointsBox),
    PROC_YIELD,
PROC_LABEL(0),
    PROC_REPEAT(LABattleMap_CpuPickWeakestTeam),
PROC_LABEL(1),
    PROC_REPEAT(LABattleMap_CpuPickBestAttack),
    PROC_CALL(LABattleMap_CpuStartTargetCursor),
    PROC_YIELD,
    PROC_CALL(LABattleMap_CpuPrepareCombatants),
PROC_LABEL(2),
    PROC_REPEAT(LABattleMap_CpuWaitCombatantMu),
    PROC_CALL(EndLinkArenaPointsBox),
    PROC_GOTO(4),
PROC_LABEL(3),
    PROC_SLEEP(1),
    PROC_CALL(LABattleMap_StartSurrenderPrompt),
    PROC_YIELD,
    PROC_CALL(LABattleMap_CpuHandleSurrenderPrompt),
PROC_LABEL(4),
    PROC_CALL(LABattleMap_StartCombatantWarpFx),
    PROC_YIELD,
    PROC_CALL(LABattleMap_PrepareCombatantSprites),
    PROC_REPEAT(LABattleMap_StartCombat),
    PROC_REPEAT(LABattleMap_ApplyCombatResults),
    PROC_CALL(StartSioPoints1),
    PROC_YIELD,
    PROC_CALL(EndLinkArenaPointsBox),
    PROC_REPEAT(LABattleMap_WarpCombatantsToRoster),
    PROC_REPEAT(LABattleMap_RecordEliminationAndAdvance),
    PROC_CALL(LABattleMap_RefreshAfterCombat),
PROC_LABEL(5),
    PROC_END,
};
