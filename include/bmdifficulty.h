#ifndef GUARD_BMDIFFICULTY_H
#define GUARD_BMDIFFICULTY_H

#include "global.h"
#include "fontgrp.h"
#include "bmunit.h"

#define BGPAL_BMDIFFICULTY_UNK_0 0
#define OBPAL_BMDIFFICULTY_UNK_5 5

struct Dungeon {
    /* 00 */ u32 expEarned:16;

    /* 02 */ u32 unitsUsed:8;

    /* 03 */ u32 turnCount:9;
    /* 04 */ u32 mapTime:18;

    /* 06 */ u32 enemiesDefeated:16;

    /* 08 */ u32 clearCount:7;
    /* 09 */ u32 postgameEnemiesDefeated:10;
    /* 0A */ u32 postgameClearCount:4;
    /* 0B */ u32 unk_0B_0:4; // ??
    /* 0B */ u32 unk_0B_4:4; // ??
#ifdef PC_PORT
} __attribute__((packed)) AGBCC_STRUCT;
#else
};
#endif

struct Struct030017A0 {
    /* 00 */ struct Dungeon current;
    /* 0C */ struct Dungeon dungeon[2];
    /* 24 */ u8 type; // tower = 0, ruins = 1
};

extern struct Struct030017A0 gDungeonState;

struct Struct020038C8 {
    struct Text text[5][8];
#ifdef PC_PORT
} AGBCC_STRUCT;
#else
};
#endif

extern struct Struct020038C8 gDungeonRecordValueTexts[2];

enum {
    DUNGEONRECORD_LABEL_MONSTERS,
    DUNGEONRECORD_LABEL_EXP,
    DUNGEONRECORD_LABEL_UNITS,
    DUNGEONRECORD_LABEL_TURNS,
    DUNGEONRECORD_LABEL_TIME,
};

struct BMDifficultyProc {
    PROC_HEADER;

    /* 2C */ int labelIndex;
    /* 30 */ int unk_30;
    /* 34 */ int unk_34;
    /* 38 */ int unk_38;
    /* 3C */ int unk_3c;
};

extern int gDungeonRecordSavedClock;

struct Struct080D7FD0 {
    s8 x;
    s8 y;
    u16 _pad;
    u8 numDigits;
#ifdef PC_PORT
} AGBCC_STRUCT;
#else
};
#endif

struct Outer080D7FD0 {
    struct Struct080D7FD0 current[4];
    s8 x;
    s8 y;
    struct Struct080D7FD0 record[4];
    s8 x2;
    s8 y2;
#ifdef PC_PORT
} AGBCC_STRUCT;
#else
};
#endif
extern const struct Outer080D7FD0 gDungeonRecordValueLayout;

int GetCurrentPromotedLevelBonus(void);
s8 CanUnitSeize(struct Unit * unit);
void DungeonRecordUi_InitText(void);
void InitDungeon(u8 type);
/* PC played builds: Reforged Options "Skip ruin floors" (port/reforged/reforged_ruins.c). RFO_RUINS_FRESH:
   src/events_script_utils.c InitRuinsDungeon, floor 1's beginning (a run of the Ruins from floor 1).
   RFO_RUINS_RECORDS: src/bmdifficulty.c UpdateDungeonRecordStats, a run started part-way only adds
   its kills (the best records and clear counts stay). Elsewhere: nothing. */
#if defined(PC_PORT) && PORT_PLAYED_BUILD
void Reforged_RuinsFresh(void);
int Reforged_RuinsPartialRecords(void);
#define RFO_RUINS_FRESH() Reforged_RuinsFresh();
#define RFO_RUINS_RECORDS() if (Reforged_RuinsPartialRecords()) return;
#else
#define RFO_RUINS_FRESH()
#define RFO_RUINS_RECORDS()
#endif
void UnlockPostgameAllyByEnemyCount(void);
void UnlockPostgameAllyByClearCount(void);
void StartRetreatProcessing(void);
s8 PrepScreenProc_AddPostgameUnits(ProcPtr proc);
s8 TryAddPostgameUnit(ProcPtr, const struct UnitDefinition*);
void SaveDungeonState(struct Dungeon* savedDungeon);
void LoadDungeonState(struct Dungeon* savedDungeon);
void SaveDungeonRecords(struct Dungeon* savedDungeon);
void LoadDungeonRecords(struct Dungeon* savedDungeon);
int UpdateDungeonMapTime(struct Dungeon* dungeon);
void UpdateDungeonStats(struct Dungeon*);
void UpdateDungeonRecordStats(void);
void UpdateDungeonEnemiesDefeated(void);
void StartDungeonRecordProcFromMenu(ProcPtr proc);
void RecordDisplayAfterTowerCleared(ProcPtr proc);
void PushGlobalTimer(void);
void PopGlobalTimer(void);
void DungeonRecordUi_StartBgm(void);
void SetupDungeonRecordUi(ProcPtr proc);
void DrawDungeonRecordUiLabels(struct Text * th);
struct Text * DrawNumberText(struct Text * th, u16 number, u8 places, s8 x, s8 y, u8 colorId);
struct Text * DrawNumberText_WithReset(struct Text * th, u16 number, u8 numTiles, s8 x, s8 y, u8 colorId);
void DungeonRecordUi_ClearTexts(struct Text * th, u8 count);
struct Text * DrawTimeText(struct Text* th, int time, s8 xBase, s8 yBase, u8 colorId);
struct Text * DrawTimeText_WithReset(struct Text* th, int time, s8 xBase, s8 yBase, u8 colorId, s8 drawPunctuation);
void DrawDungeonRecordUiText(ProcPtr proc);
void DungeonRecordUi_UpdateRunningTime(void);
void DungeonRecordUi_KeyListenerUpdatesTime(ProcPtr proc);
void DungeonRecordUi_KeyListener(ProcPtr proc);
void EndDungeonRecordUi(void);
void DungeonRecordUi_CopyTextToObjVram(struct Text * th);
void DungeonRecordUi_UpdateValue_Init(struct BMDifficultyProc* proc);
void DungeonRecordUi_UpdateValue_Loop(struct BMDifficultyProc* proc);
struct BMDifficultyProc * DungeonRecordUi_SpawnUpdateValueProc(int label, int value, ProcPtr parent);
u32 GetCurrentDungeonValueByUiLabel(u32 label);
u32 GetRecordDungeonValueByUiLabel(u32 label);
s8 DungeonRecordUi_IsNewRecordForLabel(u32 label);
void DungeonRecordUi_ClearCountAnim_Init(struct BMDifficultyProc* proc);
void DungeonRecordUi_ClearCountAnim_ClearTexts(ProcPtr proc);
void DungeonRecordUi_ClearCountAnim_Loop(struct BMDifficultyProc* proc);
void DungeonRecordUi_UpdateEnemiesDefeatedCount_Init(struct BMDifficultyProc* proc);
void DungeonRecordUi_UpdateEnemiesDefeatedCount(struct BMDifficultyProc* proc);
void DungeonRecordUi_UpdateEnemiesDefeatedCount_End(struct BMDifficultyProc* proc);
void DungeonRecordUi_SetLabelToExp(struct BMDifficultyProc* proc);
void DungeonRecordUi_SpawnUpdateValueIfNewRecord(struct BMDifficultyProc* proc);
void DungeonRecordUi_GotoNextLabel(struct BMDifficultyProc* proc);

extern struct Struct02003BE8 gUnknown_02003BE8;
extern u16 gDungeonRecordNumberPathTimes[];
extern int gDungeonRecordNumberPathPoints[];
extern u16 gDungeonRecordBgBuffer[];
extern struct Font gDungeonRecordFont;
extern struct Text gDungeonRecordClearCountTexts[8];

extern struct ProcCmd CONST_DATA sProcScr_DisplayDungeonRecord_FromMenu[];
extern struct ProcCmd CONST_DATA sProcScr_DisplayDungeonRecord_AfterDungeonClear[];
extern const u16 CONST_DATA Sprite_DungeonRecordNumber[];
extern u16 CONST_DATA gDungeonRecordNewRecordPathTimes[];
extern int CONST_DATA gDungeonRecordNewRecordPathPoints[];
extern struct ProcCmd CONST_DATA sProcScr_DungeonRecord_UpdateNewRecordValues[];

#endif  // GUARD_BMDIFFICULTY_H
