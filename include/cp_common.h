#ifndef GUARD_CP_COMMON_H
#define GUARD_CP_COMMON_H

#include "global.h"

struct AiState
{
    /* 00 */ u8 units[116];
    /* 74 */ u8 * unitIt;
    /* 78 */ u8 orderState;
    /* 79 */ u8 decideState;
    /* 7A */ s8 dangerMapFilled; // bool
    /* 7B */ u8 flags;
    /* 7C */ u8 unk7C;
    /* 7D */ u8 combatWeightTableId;
    /* 7E */ u8 unk7E;
    /* 7F */ u8 unk7F;
    /* 80 */ u32 specialItemFlags;
    /* 84 */ u8 unk84;
    /* 85 */ u8 bestBlueMov;
    /* 86 */ u8 cmd_result[8];
};

struct AiDecision
{
    /* 00 */ u8 actionId;

    /* 01 */ u8 unitId;
    /* 02 */ u8 xMove;
    /* 03 */ u8 yMove;
    /* 04 */ u8 unk04;
    /* 05 */ u8 unk05;
    /* 06 */ u8 targetId;
    /* 07 */ u8 itemSlot;
    /* 08 */ u8 xTarget;
    /* 09 */ u8 yTarget;

    /* 0A */ s8 actionPerformed;
#ifdef PC_PORT
} AGBCC_STRUCT;
#else
};
#endif

enum
{
    // gAiState.flags
    AI_FLAGS_NONE = 0,

    AI_FLAG_0 = (1 << 0),
    AI_FLAG_STAY = (1 << 1),
    AI_FLAG_BERSERKED = (1 << 2),
    AI_FLAG_3 = (1 << 3),
};

enum
{
    // Unit::aiFlags
    AI_UNIT_FLAG_0 = (1 << 0),
    AI_UNIT_FLAG_1 = (1 << 1),
    AI_UNIT_FLAG_2 = (1 << 2),
    AI_UNIT_FLAG_3 = (1 << 3),
    AI_UNIT_FLAG_4 = (1 << 4),
    AI_UNIT_FLAG_5 = (1 << 5),
    AI_UNIT_FLAG_6 = (1 << 6),
};

enum
{
    // "ai3"

    // 0~2 healing ai theshold select
    AI_UNIT_CONFIG_HEALTHRESHOLD_SHIFT = 0,
    AI_UNIT_CONFIG_HEALTHRESHOLD_BITS = 3,
    AI_UNIT_CONFIG_HEALTHRESHOLD_MASK = ((1 << AI_UNIT_CONFIG_HEALTHRESHOLD_BITS) - 1) << AI_UNIT_CONFIG_HEALTHRESHOLD_SHIFT,

    // 3~7 combat target decide weight set select
    AI_UNIT_CONFIG_COMBATWEIGHT_SHIFT = 3,
    AI_UNIT_CONFIG_COMBATWEIGHT_BITS = 5,
    AI_UNIT_CONFIG_COMBATWEIGHT_MASK = ((1 << AI_UNIT_CONFIG_COMBATWEIGHT_BITS) - 1) << AI_UNIT_CONFIG_COMBATWEIGHT_SHIFT,

    // "ai4"

    // 8+

    // 13
    AI_UNIT_CONFIG_FLAG_STAY = 1 << 13,
};

enum
{
    AI_A_00 = 0x00,
    AI_A_01 = 0x01,
    AI_A_02 = 0x02,
    AI_A_03 = 0x03,
    AI_A_04 = 0x04,
    AI_A_05 = 0x05,
    AI_A_06 = 0x06,
    AI_A_07 = 0x07,
    AI_A_08 = 0x08,
    AI_A_09 = 0x09,
    AI_A_0A = 0x0A,
    AI_A_0B = 0x0B,
    AI_A_0C = 0x0C,
    AI_A_0D = 0x0D,
    AI_A_0E = 0x0E,
    AI_A_0F = 0x0F,
    AI_A_10 = 0x10,
    AI_A_11 = 0x11,
    AI_A_12 = 0x12,
    AI_A_13 = 0x13,
    AI_A_14 = 0x14,

    AI_A_INVALID
};

enum
{
    AI_B_00 = 0x00,
    AI_B_01 = 0x01,
    AI_B_02 = 0x02,
    AI_B_03 = 0x03,
    AI_B_04 = 0x04,
    AI_B_05 = 0x05,
    AI_B_06 = 0x06,
    AI_B_07 = 0x07,
    AI_B_08 = 0x08,
    AI_B_09 = 0x09,
    AI_B_0A = 0x0A,
    AI_B_0B = 0x0B,
    AI_B_0C = 0x0C,
    AI_B_0D = 0x0D,
    AI_B_0E = 0x0E,
    AI_B_0F = 0x0F,
    AI_B_10 = 0x10,
    AI_B_11 = 0x11,
    AI_B_12 = 0x12,

    AI_B_INVALID
};

enum
{
    AI_ACTION_NONE = 0, // move only
    AI_ACTION_COMBAT = 1,
    AI_ACTION_ESCAPE = 2,
    AI_ACTION_STEAL = 3,
    AI_ACTION_PILLAGE = 4,
    AI_ACTION_STAFF = 5,
    AI_ACTION_USEITEM = 6,
    AI_ACTION_REFRESH = 7,
    AI_ACTION_TALK = 8,
    AI_ACTION_RIDEBALLISTA = 9,
    AI_ACTION_EXITBALLISTA = 10,
    AI_ACTION_DKNIGHTMARE = 11,
    AI_ACTION_DKSUMMON = 12,
    AI_ACTION_PICK = 13,
};

enum
{
    AI_COMPARE_GT,
    AI_COMPARE_GE,
    AI_COMPARE_EQ,
    AI_COMPARE_LE,
    AI_COMPARE_LT,
    AI_COMPARE_NE,
};

struct AiCombatSimulationSt
{
    /* 00 */ u8 xMove;
    /* 01 */ u8 yMove;
    /* 02 */ u8 targetId;
    /* 04 */ u16 itemSlot;
    /* 08 */ u32 score;
};

extern struct AiState gAiState;
extern struct AiDecision gAiDecision;

extern void(*AiDecideMainFunc)(void);

extern struct ProcCmd CONST_DATA gProcScr_CpPhase[];
extern struct ProcCmd CONST_DATA gProcScr_BerserkCpPhase[];
extern struct ProcCmd CONST_DATA gProcScr_CpOrder[];
extern struct ProcCmd CONST_DATA gProcScr_BerserkCpOrder[];
extern struct ProcCmd CONST_DATA gProcScr_CpDecide[];
extern struct ProcCmd CONST_DATA gProcScr_CpPerform[];

void AiClearDecision(void);
void AiSetDecision(s16 xMove, s16 yMove, u8 actionId, u8 targetId, u8 itemSlot, u8 xTarget, u8 yTarget);
void AiUpdateDecision(u8 actionId, u8 targetId, u8 itemSlot, u8 xTarget, u8 yTarget);
void AiDecideMain(void);

s8 AiAttemptOffensiveAction(s8 (* isEnemy)(struct Unit * unit));
s8 AiAttemptCombatWithinMovement(s8 (* isEnemy)(struct Unit * unit));
void AiFillReversedAttackRangeMap(struct Unit *, u16);
void AiFloodMovementAndRange(struct Unit * unit, u16 move, u16 item);
s8 AiAttemptBallistaCombat(s8 (* isEnemy)(struct Unit * unit), struct AiCombatSimulationSt*);
u8 AiAttemptStealAction_GetMovementAt(int x, int y);
s8 AiAttemptStealActionWithinMovement(void);
s8 AiSimulateBestBattleAgainstTarget(struct AiCombatSimulationSt *);
s8 AiSimulateBestBallistaBattleAgainstTarget(struct AiCombatSimulationSt *, u16);
u32 AiGetCombatPositionScore(int, int, struct AiCombatSimulationSt *);
s8 AiIsBadFight(struct AiCombatSimulationSt * st);
s8 AiSimulateBattleAgainstTargetAtPosition(struct AiCombatSimulationSt *);
int AiGetDamageDealtCombatScoreComponent(void);
int AiGetOpponentLowHpScoreComponent(void);
int AiGetFriendZoneCombatScoreComponent(void);
int AiGetTargetClassCombatScoreComponent(void);
int AiGetTurnCombatScoreComponent(void);
int AiGetDamageTakenScoreComponent(void);
int AiGetDangerScoreComponent(void);
int AiGetLowHpScoreComponent(void);
void AiComputeCombatScore(struct AiCombatSimulationSt * st);
int AiGetInRangeCombatPositionScoreComponent(int, int, struct Unit*);
int AiGetTerrainCombatPositionScoreComponent(int, int);
int AiGetFriendZoneCombatPositionScoreComponent(int, int);
void AiInitDangerMap(void);
// ??? AiFillDangerMap(???);
s8 AiCheckDangerAt(int, int, u8);
s8 AiTryGetNearestHealPoint(struct Vec2* out);
void UpdateAllPhaseHealingAIStatus(void);
s8 AiUpdateGetUnitIsHealing(struct Unit* unit);
s8 AiTryHealSelf(void);
s8 AiTryMoveTowardsEscape(void);
// ??? GetEscapePointStructThingMaybe(???);
s8 AiCanEquip(void);
s8 AiEquipGetFlags(u16*);
void AiEquipGetDanger(int, int, u16*, u16*, u16*);
// ??? AiEquipBestMatch(???);
void AiEquipBestConsideringDanger(u16, u16, u16, u16*);
// ??? AiUpdateUnitFlagsFromStaff(???);
// ??? AiIsWithinFlyingDistance(???);
// ??? StoreItemAndGetUnitAttack(???);
void AiTryDanceOrStealAfterMove(void);
void AiTryActionAfterMove(void);
// ??? AiTryDoDanceAdjacent(???);
// ??? AiTryDoStealAdjacent(???);
// ??? AiTryCombatAt(???);
// ??? AiIsUnitAtPositionDifferentAllegiance(???);
s8 AiFunc_CountEnemiesInRange(const void*);
// ??? AiFunc_AttackNearConfiguredPosition_unused(???);
s8 AiFunc_GetCounter(const void*);
s8 AiFunc_RestartDecisionState(const void*);
// ??? AiFunc_RestartDecisionState_unused(???);
// ??? AiTryMoveToSpecificPosition(???);
s8 AiCountEnemyInRangeOrTryMoveToSpecificPosition(const void*);
s8 AiFunc_IsInArea(const void*);
// ??? AiFunc_DecideTalk(???);
s8 AiFunc_FindLeaderCombatTarget(const void*);
// ??? AiIsUnitEnemyWithStoredCharId(???);
// ??? AiFunc_FindCombatTargetByCharId(???);
// ??? AiIsUnitEnemyCmdResult(???);
// ??? AiIsUnitEnemyOnTheWayToCmdResult(???);
s8 AiFunc_AttackCmdResult(const void*);
s8 AiFunc_TryStaffByChance(const void*);
s8 AiFunc_TrySpecialItemsByChance(const void*);
s8 AiBallistaRideExit(const void * input);
s8 AiFunc_MoveTowardsInputPosition(const void*);
// ??? AiFunc_EndDecision(???);
// ??? GetAiStaffFuncIndex(???);
s8 AiTryDoStaff(s8 (*isEnemy)(struct Unit* unit)); // AiTryDoStaff
// ??? GetAiSafestAccessibleAdjacentPosition(???);
// ??? AiStaffHealMendRecover(???);
// ??? AiStaffPhysicRescue(???);
// ??? AiStaffFortify(???);
// ??? AiStaffWarp(???);
// ??? AiStaffRestore(???);
// ??? AiIsUnitArmed(???);
// ??? GetAiSilenceEffectivenessScore(???);
// ??? AiStaffSilence(???);
// ??? AiStaffSleepBerserk(???);
// ??? AiStaffBarrier(???);
s8 AiGetGoodWarpPosition(struct Vec2*);
// ??? GetSpecialItemFuncIndex(???);
s8 AiTryDoSpecialItems(void);
// ??? AiSpecialItemDoorKey(???);
// ??? AiSpecialItemLockpick(???);
// ??? AiSpecialItemAntitoxin(???);
// ??? AiGetRangeAt(???);
// ??? AiFindGoodDoorKeyUsePosition(???);
// ??? AiFindGoodChestKeyUsePosition(???);
// ??? AiFindGoodLockpickUsePosition(???);
// ??? AiTryRoguePick(???);
// ??? AiSetMovCostTableWithPassableWalls(???);
// ??? AiSetMovCostTableWithPassableTerrain(???);
// ??? AiSetMovCostTableWithTwoPassableTerrains(???);
void InitAiMoveMapForUnit(struct Unit*);
// ??? AiMapFloodRange(???);
// ??? AiMapFlood_NoWall(???);
// ??? AiMapFlood_NoWall_Pass(???);
void GenerateExtendedMovementMapOnRangeNeglectWall(int x, int y, const s8* cost); // AiMapRangeFillMovementWithPassableWalls
void AiMapFloodRangeFrom_NoWall(int, int, struct Unit*);
// ??? AiMapFloodRange_NoDoors(???);
// ??? AiMapFloodRange_NoDoors_Pass(???);
// ??? AiMapFloodRange_NoSnagWalls(???);
void AiMapFloodRange_NoSnagWalls_Pass(struct Unit* unit);
void AiMapFloodRange_UpToMove(struct Unit* unit);
void AiUpdateNoMoveFlag(struct Unit* unit);
void AiMapFloodRangeFrom(int, int, struct Unit*);
// ??? AiDetermineNightmareEffectiveness(???);
// ??? AiTryUseNightmareStaff(???);
// ??? AiDecideNightmareStaff(???);
// ??? AiTryDKSummon(???);
// ??? AiDecideDKSummon(???);

/*
 * PC played builds only: Reforged Options' "Game fixes" (port/reforged/reforged_gamefixes.c), OFF by default.
 * RFO_AI_HEAL_WANTS, src/cp_staff.c AiStaffHealMendRecover / AiStaffPhysicRescue and src/cp_utility.c
 * AiCountAlliedUnitsSeekingHealingInRange (Fortify): a unit at full HP is no healing target. The original
 * flags every player unit with a usable tome as "seeking healing" (AI_UNIT_FLAG_0) and never checks its HP.
 * Everywhere else, and in every other build, the condition itself.
 */
#if defined(PC_PORT) && PORT_PLAYED_BUILD
int Reforged_FixAiHealFullHp(struct Unit *unit, int wants);
#define RFO_AI_HEAL_WANTS(unit, cond) Reforged_FixAiHealFullHp((unit), (cond))
#else
#define RFO_AI_HEAL_WANTS(unit, cond) (cond)
#endif

/*
 * PC played builds only: Game fixes, "Smarter enemies" (RFO_FIX_SMART_AI, port/reforged/reforged_gamefixes.c),
 * OFF by default. src/cp_battle.c, after the AI's battle simulation against a target:
 *   RFO_AI_BAD_FIGHT()        AiIsBadFight (FE8: always 0): 1 skips the target, for a regular enemy (red, not a
 *                             boss, not berserk) whose attack would do no real damage while the target hits
 *                             back, or that would likely die (50%+) without likely killing (50%+).
 *   RFO_AI_SCORE_RISK(score)  AiComputeCombatScore: the score loses the attacker's chance to die / 5 (0-20).
 *   RFO_AI_AVOID_MOVE(px, py, maxDanger)  src/cp_utility.c AiTryMoveTowards: a unit that skipped targets this
 *                             turn does not end its move inside a skipped target's reach (when it can help it).
 *   RFO_AI_ESCAPE_MARK()      src/cp_0803E2F4.c AiTryMoveTowardsEscape: that move heads for an escape point (never steered).
 * No random numbers are used. In every other build: 0 and nothing.
 */
#if defined(PC_PORT) && PORT_PLAYED_BUILD
int Reforged_FixAiBadFight(void);
int Reforged_FixAiRiskPenalty(void);
void Reforged_FixAiAvoidMove(s16 *xOut, s16 *yOut, int maxDanger);
void Reforged_FixAiEscapeMark(void);
#define RFO_AI_ESCAPE_MARK() Reforged_FixAiEscapeMark();
#define RFO_AI_BAD_FIGHT() Reforged_FixAiBadFight()
#define RFO_AI_SCORE_RISK(score) (score) -= Reforged_FixAiRiskPenalty();
#define RFO_AI_AVOID_MOVE(px, py, maxDanger) Reforged_FixAiAvoidMove((px), (py), (maxDanger));
#else
#define RFO_AI_BAD_FIGHT() 0
#define RFO_AI_SCORE_RISK(score)
#define RFO_AI_AVOID_MOVE(px, py, maxDanger)
#define RFO_AI_ESCAPE_MARK()
#endif

#endif // GUARD_CP_COMMON_H
