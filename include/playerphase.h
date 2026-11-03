#ifndef GUARD_PLAYERPHASE_H
#define GUARD_PLAYERPHASE_H

struct MoveLimitViewProc
{
    /* 00 */ PROC_HEADER;

    /* 29 */ STRUCT_PAD(0x29, 0x4A);
    /* 4A */ s16 flags;
    /* 4C */ s16 unk_4C;
};

enum
{
    PLAYER_SELECT_NOUNIT     = 0,
    PLAYER_SELECT_TURNENDED  = 1,
    PLAYER_SELECT_CONTROL    = 2,
    PLAYER_SELECT_NOCONTROL  = 3,
    PLAYER_SELECT_4          = 4,
};

enum
{
    LIMITVIEW_BLUE   = (1 << 0),
    LIMITVIEW_RED    = (1 << 1),
    LIMITVIEW_GREEN  = (1 << 2),
    // (1 << 3)
    LIMITVIEW_UNK    = (1 << 4),
};

void PlayerPhase_Suspend(void);
void HandlePlayerCursorMovement(void);
// bool CanShowUnitStatScreen(struct Unit * unit);
void PlayerPhase_MainIdle(ProcPtr proc);
void DisplayUnitEffectRange(struct Unit * unit);
void PlayerPhase_InitUnitMovementSelect(void);
void DisplayActiveUnitEffectRange(ProcPtr proc);
void PlayerPhase_DisplayDangerZone(void);
void PlayerPhase_RangeDisplayIdle(ProcPtr proc);
void PlayerPhase_CancelAction(ProcPtr proc);
void PlayerPhase_BackToMove(ProcPtr proc);
s8 PlayerPhase_PrepareAction(ProcPtr proc);
bool TryMakeCantoUnit(ProcPtr proc);
bool RunPotentialWaitEvents(void);
bool EnsureCameraOntoActiveUnitPosition(ProcPtr proc);
void PlayerPhase_FinishAction(ProcPtr proc);
void PlayerPhase_CommitActiveUnitMove_unused(void);
void PlayerPhase_OpenUnitActionMenu(ProcPtr proc);
void PlayerPhase_ApplyUnitMovement(ProcPtr proc);
int GetPlayerSelectKind(struct Unit * unit);
bool CanMoveActiveUnitTo(int x, int y);
void PlayerPhase_DisplayUnitMovement(void);
void PlayerPhase_WaitForUnitMovement(ProcPtr proc);
void PlayerPhase_ResumeRangeDisplay(ProcPtr proc);
void PlayerPhase_ReReadGameSaveGfx(void);
void MakeMoveunitForActiveUnit(void);
void ClearActiveUnit(struct Unit * unit);
void PlayPhaseForcePressAButtonInRangeDisp(void);
void PlayerPhase_RangeDisplayIdle_ForceAPress(ProcPtr proc);
void PlayPhaseForceActiveUnitCanto(void);
void MoveLimitViewChange_OnInit(struct MoveLimitViewProc * proc);
void MoveLimitViewChange_OnLoop(struct MoveLimitViewProc * proc);
void MoveLimitView_OnInit(ProcPtr proc);
void MoveLimitView_OnLoop(struct MoveLimitViewProc * proc);
void MoveLimitView_OnEnd(struct MoveLimitViewProc * proc);
void DisplayMoveRangeGraphics(int flags);
void HideMoveRangeGraphics(void);
bool TrySetCursorOn(int unitId);
void TrySwitchViewedUnit(int x, int y);
void PlayerPhase_HandleAutoEnd(ProcPtr proc);

extern const struct MenuDef gMapMenuDef;
extern const struct MenuDef gUnitActionMenuDef;

extern u16 Pal_LimitViewBlue[];
extern u16 Pal_LimitViewRed[];
extern u16 Pal_LimitViewGreen[];

extern u8 Img_LimitViewSquareFull[];

extern struct ProcCmd gProcScr_PlayerPhaseSelectUnit[];
extern struct ProcCmd gProcScr_PlayerPhase[];

/*
 * PC played builds only: Reforged Options' "Game fixes" (port/reforged/reforged_gamefixes.c), OFF by default.
 * RFO_CANTO_MOVECOUNT, src/playerphase.c PlayerPhase_ApplyUnitMovement: the move count read again after a
 * Trade or Supply screen left with nothing moved; an Attack or Staff range shown before has erased the
 * movement map there (0xFF), so the original loses Canto. A read that is not a real move keeps the count the
 * first pass stored. Everywhere else, and in every other build, the map's value (the original expression).
 */
#if defined(PC_PORT) && PORT_PLAYED_BUILD
int Reforged_FixCantoMoveCount(int mapValue, int keptMoveCount, int movementMax);
#define RFO_CANTO_MOVECOUNT(mapValue) Reforged_FixCantoMoveCount((mapValue), gActionData.moveCount, MAP_MOVEMENT_MAX)
#else
#define RFO_CANTO_MOVECOUNT(mapValue) mapValue
#endif

/*
 * PC played builds only: the map menu with a "Reforged" entry after Options (port/reforged/
 * reforged_field_menu.c: Reforged Options during a chapter; the run's own settings shown, not
 * changed). RFO_MAP_MENU_DEF, on src/playerphase.c's two StartOrphanMenuAdjusted lines, gives a
 * host copy of the ROM's gMapMenuDef with that entry (the PC build reads the menu data from the ROM,
 * src/menu_def.c is not built). Everywhere else the menu itself.
 */
#if defined(PC_PORT) && PORT_PLAYED_BUILD
const struct MenuDef *Reforged_MapMenuDef(const struct MenuDef *def);
#define RFO_MAP_MENU_DEF(def) Reforged_MapMenuDef(def)
#else
#define RFO_MAP_MENU_DEF(def) (def)
#endif

#endif  // GUARD_PLAYERPHASE_H
