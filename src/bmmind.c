#include "global.h"

#include "proc.h"
#include "rng.h"
#include "bmunit.h"
#include "bmitem.h"
#include "bmmap.h"
#include "mu.h"
#include "uiselecttarget.h"
#include "bmbattle.h"
#include "bmreliance.h"
#include "m4a.h"
#include "soundwrapper.h"
#include "bmusemind.h"
#include "bmtrap.h"
#include "bmarch.h"
#include "bmtarget.h"
#include "bmudisp.h"
#include "bm.h"
#include "bmsave.h"
#include "bmlib.h"
#include "popup.h"
#include "eventinfo.h"
#include "mapanim.h"

#include "bmmind.h"

#include "constants/items.h"
#include "constants/terrains.h"
#include "constants/songs.h"

#if defined(PC_PORT) && PORT_PLAYED_BUILD
#include "port_reforged.h" /* PC played builds: Reforged Options, casual mode and saves marked */
#endif

EWRAM_DATA struct ActionData gActionData = { 0 };

struct ProcCmd CONST_DATA sProcScr_AfterDropAction[] = {
    PROC_SLEEP(0),

    PROC_WHILE(MuExistsActive),

    PROC_CALL_2(AfterDrop_CheckTrapAfterDropMaybe),
    PROC_CALL(UpdateMapAndUnit),

    PROC_END,
};

struct ProcCmd CONST_DATA sProcScr_DeathDropAnim[] = {
    PROC_REPEAT(DeathDropSpriteAnim_Loop),
    PROC_CALL(DeathDropSpriteAnim_ExecAnyTrap),

    PROC_SLEEP(0),

    PROC_CALL(DeathDropSpriteAnim_End),

    PROC_END,
};

struct ProcCmd CONST_DATA sProcScr_CombatAction[] = {
    PROC_CALL(BeginBattleAnimations),
    PROC_SLEEP(1),
    PROC_CALL(BattleApplyGameStateUpdates),
    PROC_WHILE(DoesBMXFADEExist),
    PROC_CALL(BATTLE_GOTO1_IfNobodyIsDead),

    PROC_CALL(BATTLE_PostCombatDeathFades),

    PROC_SLEEP(32),

    PROC_CALL(BATTLE_DeleteLinkedMOVEUNIT),

PROC_LABEL(1),
    PROC_CALL_2(BATTLE_HandleItemDrop),
    PROC_CALL(BATTLE_HandleCombatDeaths),
    PROC_SLEEP(0),

    PROC_END,
};

struct ProcCmd CONST_DATA sProcScr_ArenaAction[] = {
    PROC_SLEEP(0),
    PROC_CALL(ArenaAction_MaybeSkipDeathFades),

    PROC_CALL(BATTLE_PostCombatDeathFades),

    PROC_SLEEP(32),

    PROC_CALL(BATTLE_DeleteLinkedMOVEUNIT),

PROC_LABEL(1),
    PROC_CALL(BATTLE_HandleArenaDeathsMaybe),
    PROC_SLEEP(0),

    PROC_END,
};

//! FE8U = 0x08031FEC
void StoreRNStateToActionStruct(void) {
    StoreRNState(gActionData._u00);
    return;
}

//! FE8U = 0x08031FFC
void LoadRNStateFromActionStruct(void) {
    LoadRNState(gActionData._u00);
    return;
}

//! FE8U = 0x0803200C
u32 ApplyUnitAction(ProcPtr proc) {
    gActiveUnit = GetUnit(gActionData.subjectIndex);

    if (gActionData.unitActionType == UNIT_ACTION_COMBAT) {
        int itemIdx = GetItemIndex(
            GetUnit(gActionData.subjectIndex)->items[gActionData.itemSlotIndex]
        );

        if (itemIdx == ITEM_NIGHTMARE) {
            ActionStaffDoorChestUseItem(proc);
            return 0;
        }
    }

    switch (gActionData.unitActionType) {
        case UNIT_ACTION_WAIT:
        case UNIT_ACTION_TRAPPED:
            gActiveUnit->state |= US_HAS_MOVED;
            return 1;

        case UNIT_ACTION_RESCUE:
            return ActionRescue(proc);

        case UNIT_ACTION_DROP:
            return ActionDrop(proc);

        case UNIT_ACTION_VISIT:
        case UNIT_ACTION_SEIZE:
            return ActionVisitAndSeize(proc);

        case UNIT_ACTION_COMBAT:
            return ActionCombat(proc);

        case UNIT_ACTION_DANCE:
            return ActionDance(proc);

        case UNIT_ACTION_TALK:
            return ActionTalk(proc);

        case UNIT_ACTION_SUPPORT:
            return ActionSupport(proc);

        case UNIT_ACTION_STEAL:
            return ActionSteal(proc);

        case UNIT_ACTION_SUMMON:
            return ActionSummon(proc);

        case UNIT_ACTION_SUMMON_DK:
            return ActionSummonDK(proc);

        case UNIT_ACTION_ARENA:
            return ActionArena(proc);

        case UNIT_ACTION_STAFF:
        case UNIT_ACTION_DOOR:
        case UNIT_ACTION_CHEST:
        case UNIT_ACTION_USE_ITEM:
            ActionStaffDoorChestUseItem(proc);
            return 0;

        case UNIT_ACTION_PICK:
            ActionPick(proc);
            return 0;

        default:
            return 1;
    }
}

//! FE8U = 0x08032164
s8 ActionRescue(ProcPtr proc) {
    struct Unit* subject = GetUnit(gActionData.subjectIndex);
    struct Unit* target = GetUnit(gActionData.targetIndex);

    TryRemoveUnitFromBallista(target);

    Make6CKOIDO(
        target,
        GetSomeFacingDirection(subject->xPos, subject->yPos, target->xPos, target->yPos),
        0,
        proc
    );

    UnitRescue(subject, target);
    HideUnitSprite(target);

    return 0;
}

//! FE8U = 0x080321B8
int AfterDrop_CheckTrapAfterDropMaybe(struct AfterDropActionProc* proc) {
    return ExecTrapAfterDropAction(proc, proc->unit);
}

//! FE8U = 0x080321C8
int UpdateMapAndUnit(void) {
    RefreshEntityBmMaps();
    RenderBmMap();
    RefreshUnitSprites();
    ForceSyncUnitSpriteSheet();

    // return; // BUG?
}

//! FE8U = 0x080321E0
s8 ActionDrop(ProcPtr proc) {
    struct AfterDropActionProc* child;

    struct Unit* target = GetUnit(gActionData.targetIndex);

    if (gBmMapHidden[gActionData.yOther][gActionData.xOther] & HIDDEN_BIT_UNIT) {
        gWorkingMovementScript[0] = MOVE_CMD_BUMP;
        gWorkingMovementScript[1] = MOVE_CMD_HALT;
        SetAutoMuMoveScript(gWorkingMovementScript);
        return 0;
    }

    UnitFinalizeMovement(GetUnit(gActionData.subjectIndex));

    Make6CKOIDO(
        target,
        GetSomeFacingDirection(gActionData.xOther, gActionData.yOther, target->xPos, target->yPos),
        2,
        proc
    );

    UnitDrop(
        GetUnit(gActionData.subjectIndex),
        gActionData.xOther,
        gActionData.yOther
    );

    child = Proc_StartBlocking(sProcScr_AfterDropAction, proc);
    child->unit = target;

    return 0;
}

//! FE8U = 0x08032270
s8 ActionVisitAndSeize(ProcPtr proc) {
    int x = GetUnit(gActionData.subjectIndex)->xPos;
    int y = GetUnit(gActionData.subjectIndex)->yPos;

    StartAvailableTileEvent(x, y);

    return 0;
}

//! FE8U = 0x0803229C
s8 ActionCombat(ProcPtr proc) {
    struct Unit* target = GetUnit(gActionData.targetIndex);

    int itemIdx = GetItemIndex(
        GetUnit(gActionData.subjectIndex)->items[gActionData.itemSlotIndex]
    );

    gBattleActor.hasItemEffectTarget = 0;

    if (itemIdx == ITEM_NIGHTMARE) {
        int i;
        int targetCount;

        MakeTargetListForFuckingNightmare(GetUnit(gActionData.subjectIndex));
        targetCount = GetSelectTargetCount();

        for (i = 0; i < targetCount; i++) {
            SetUnitStatus(
                GetUnit(GetTarget(i)->uid),
                UNIT_STATUS_SLEEP
            );
        }
    }

    if (target == 0) {
        InitObstacleBattleUnit();
    }

    if (gActionData.itemSlotIndex == BU_ISLOT_BALLISTA) {
        BattleGenerateBallistaReal(GetUnit(gActionData.subjectIndex), target);
    } else {
        BattleGenerateReal(GetUnit(gActionData.subjectIndex), target);
    }

    Proc_StartBlocking(sProcScr_CombatAction, proc);

    return 0;
}

//! FE8U = 0x08032344
s8 ActionArena(ProcPtr proc) {
    Proc_StartBlocking(sProcScr_ArenaAction, proc);
    return 0;
}

//! FE8U = 0x08032358
s8 ActionDance(ProcPtr proc) {
    GetUnit(gActionData.targetIndex)->state &= ~( US_UNSELECTABLE | US_HAS_MOVED | US_HAS_MOVED_AI );

    BattleInitItemEffect(GetUnit(gActionData.subjectIndex), -1);
    BattleInitItemEffectTarget(GetUnit(gActionData.targetIndex));

    gBattleStats.config = BATTLE_CONFIG_REFRESH;

    BattleApplyMiscAction(proc);
    BeginBattleAnimations();

    return 0;
}

//! FE8U = 0x080323A8
s8 ActionTalk(ProcPtr proc) {
    StartCharacterEvent(
        GetUnit(gActionData.subjectIndex)->pCharacterData->number,
        GetUnit(gActionData.targetIndex)->pCharacterData->number
    );

    return 0;
}

//! FE8U = 0x080323D4
s8 ActionSupport(ProcPtr proc) {
    int subjectExp;
    int targetExp;

    struct Unit* target = GetUnit(gActionData.targetIndex);

    int targetSupportNum = GetUnitSupporterNum(gActiveUnit, target->pCharacterData->number);
    int subjectSupportNum = GetUnitSupporterNum(target, gActiveUnit->pCharacterData->number);

    CanUnitSupportNow(target, subjectSupportNum);

    UnitGainSupportLevel(gActiveUnit, targetSupportNum);
    UnitGainSupportLevel(target, subjectSupportNum);

    StartSupportTalk(
        gActiveUnit->pCharacterData->number,
        target->pCharacterData->number,
        GetUnitSupportLevel(gActiveUnit, targetSupportNum)
    );

    subjectExp = gActiveUnit->supports[targetSupportNum];
    targetExp = target->supports[subjectSupportNum];

    if (subjectExp != targetExp) {
        if (subjectExp > targetExp) {
            target->supports[subjectSupportNum] = subjectExp;
        }

        if (subjectExp < targetExp) {
            gActiveUnit->supports[targetSupportNum] = targetExp;
        }
    }

    return 0;
}

//! FE8U = 0x0803247C
s8 ActionSteal(ProcPtr proc) {
    int item;

    struct Unit* target = GetUnit(gActionData.targetIndex);

    if (target->state & US_DROP_ITEM) {
        if (gActionData.itemSlotIndex == (GetUnitItemCount(target) - 1)) {
            target->state &= ~US_DROP_ITEM;
        }
    }

    item = GetUnit(gActionData.targetIndex)->items[gActionData.itemSlotIndex];

    UnitRemoveItem(GetUnit(gActionData.targetIndex), gActionData.itemSlotIndex);

    switch (ITEM_INDEX(item)) {
        case ITEM_1G:
        case ITEM_5G:
        case ITEM_10G:
        case ITEM_50G:
        case ITEM_100G:
        case ITEM_150G:
        case ITEM_200G:
        case ITEM_3000G:
        case ITEM_5000G:
            SetPartyGoldAmount(GetPartyGoldAmount() + GetItemCost(item));
            break;

        default:
            UnitAddItem(GetUnit(gActionData.subjectIndex), item);
            break;
    }

    BattleInitItemEffect(GetUnit(gActionData.subjectIndex), -1);
    gBattleTarget.terrainId = TERRAIN_PLAINS;
    InitBattleUnit(&gBattleTarget, GetUnit(gActionData.targetIndex));
    gBattleTarget.weapon = item;
    BattleApplyMiscAction(proc);

    EndAllMus();
    BeginMapAnimForSteal();

    return 0;
}

//! FE8U = 0x08032554
s8 ActionSummon(ProcPtr proc) {
    InitBattleUnit(&gBattleActor, gActiveUnit);

    BattleApplyMiscAction(proc);
    EndAllMus();
    BeginMapAnimForSummon();

    return 0;
}

//! FE8U = 0x08032580
s8 ActionSummonDK(ProcPtr proc) {
    InitBattleUnit(&gBattleActor, gActiveUnit);

    BattleApplyMiscAction(proc);
    EndAllMus();
    BeginMapAnimForSummonDK();

    return 0;
}

//! FE8U = 0x080325AC
void DeathDropSpriteAnim_Loop(struct DeathDropAnimProc* proc) {
    int x = Interpolate(INTERPOLATE_LINEAR, proc->xFrom, proc->xTo, proc->clock, proc->clockEnd);
    int y = Interpolate(INTERPOLATE_LINEAR, proc->yFrom, proc->yTo, proc->clock, proc->clockEnd);

    y += proc->yOffset;

    proc->yOffset += proc->ySpeed;
    proc->ySpeed += proc->yAccel;

    PutUnitSprite(
        7,
        x - gBmSt.camera.x,
        y - gBmSt.camera.y,
        proc->unit
    );

    ++proc->clock;

    if (proc->clock == proc->clockEnd) {
        Proc_Break(proc);
    }

    return;
}

//! FE8U = 0x08032658
void DeathDropSpriteAnim_ExecAnyTrap(struct DeathDropAnimProc * proc)
{
    ExecTrapAfterDeathDrop(proc, proc->unit);
}

//! FE8U = 0x08032664
void DeathDropSpriteAnim_End(void) {
    RefreshEntityBmMaps();
    RefreshUnitSprites();

    return;
}

//! FE8U = 0x08032674
void DropRescueOnDeath(ProcPtr proc, struct Unit* unit) {
    struct DeathDropAnimProc* child;

#ifdef PC_PORT
    /*
     * D-51 (port-plan/DIVERGENCES.md): after a battle with a cracked wall
     * or a snag, BATTLE_HandleCombatDeaths passes GetUnit(0) = NULL. The GBA's
     * GetUnitCurrentHp(NULL) reads curHP and maxHP from the BIOS latch (lanes
     * 3 and 2): -29 post-SWI (measured), -27 post-IRQ, -30 inside an IRQ,
     * never 0, so it returns here; its one store goes to the BIOS area,
     * which ignores it. Returning at once is the same for every latch.
     */
    if (unit == NULL)
        return;
#endif

    if (GetUnitCurrentHp(unit) != 0) {
        return;
    }

    if (!(unit->state & US_RESCUING)) {
        return;
    }

    child = Proc_StartBlocking(sProcScr_DeathDropAnim, proc);

    child->unit = GetUnit(unit->rescue);

    UnitGetDeathDropLocation(unit, &child->xDrop, &child->yDrop);
    UnitDrop(unit, child->xDrop, child->yDrop);

    child->xFrom = unit->xPos * 16;
    child->yFrom = unit->yPos * 16;
    child->xTo = child->xDrop * 16;
    child->yTo = child->yDrop * 16;
    child->yOffset = 0;
    child->ySpeed = -5;
    child->yAccel = 1;
    child->clock = 0;
    child->clockEnd = 11;

    UseUnitSprite(GetUnitSMSId(child->unit));
    ForceSyncUnitSpriteSheet();

    PlaySoundEffect(SONG_AC);

    return;
}

//! FE8U = 0x08032728
void KillUnitOnCombatDeath(struct Unit* unitA, struct Unit* unitB) {
#ifdef PC_PORT
    /* D-51: unitA = GetUnit(0) = NULL after an obstacle battle; the GBA
       returns at the HP test for every latch, as in DropRescueOnDeath above.
       unitB NULL with unitA dead (an attacker killed by its own Devil Axe on a
       wall or snag, P-27) is guarded at the record below. */
    if (unitA == NULL)
        return;
#endif

    if (GetUnitCurrentHp(unitA) != 0) {
        return;
    }

#ifdef PC_PORT
    /* D-73 (was working ID D-R10-PATHS-1; P-27): a Devil Axe backfire (BattleGenerateHitEffects,
       bmbattle.c) kills its wielder in a battle against a cracked wall or a
       snag, so unitB is GetUnit(0) = NULL. The GBA loads unitB->pCharacterData
       from the BIOS latch (ldr at 0x0803273A), then ->number at that word + 4
       (ldrb at 0x0803273C): above 0x0FFFFFFF for every latch, so the CPU's open
       bus, the opcode at 0x08032740 (0xF071), lane 0 or 2: 0x71. mGBA at the
       real tick (round 10 "paths", pe14 5,603: latch 0xE3A02004 read, then 0x71)
       records killer 0x71 for the dead unit, as here (PORT_NULL_READ_VIA,
       port.h). With a real unit, the retail reads. */
    PidStatsRecordDefeatInfo(unitA->pCharacterData->number,
        PORT_NULL_READ_VIA(unitB, pCharacterData, number, PORT_THUMB_OPEN_BUS(0xF071)),
        DEFEAT_CAUSE_COMBAT);
#else
    PidStatsRecordDefeatInfo(unitA->pCharacterData->number, unitB->pCharacterData->number, DEFEAT_CAUSE_COMBAT);
#endif // PC_PORT

#if defined(PC_PORT) && PORT_PLAYED_BUILD
    /* Reforged Options, casual mode (port/reforged/reforged_casual.c): with
       the option on, a blue unit that is not exempt retreats (dead for this
       chapter, supports kept) and returns next chapter; otherwise UnitKill. */
    if (!Reforged_CasualRetreat(unitA))
        UnitKill(unitA);
#else
    UnitKill(unitA);
#endif

    return;
}

//! FE8U = 0x08032750
void KillUnitOnArenaDeathMaybe(struct Unit* unit) {
    if (GetUnitCurrentHp(unit) != 0) {
        return;
    }

    RANDO_HOOK_DEATH(unit) UnitKill(unit);

    PidStatsRecordDefeatInfo(unit->pCharacterData->number, 0, DEFEAT_CAUSE_ARENA);

    return;
}

//! FE8U = 0x08032774
void BATTLE_GOTO1_IfNobodyIsDead(ProcPtr proc) {
    if (!(gBattleStats.config & BATTLE_CONFIG_MAPANIMS)) {
        if (gBattleActor.unit.curHP == 0) {
            return;
        }

        if (gBattleTarget.unit.curHP == 0) {
            return;
        }
    }

    Proc_Goto(proc, 1);

    return;
}

//! FE8U = 0x080327B4
bool DidUnitDie(struct Unit* unit) {
    if (GetUnitCurrentHp(unit) != 0) {
        return false;
    }

    return true;
}

//! FE8U = 0x080327B4
void BATTLE_PostCombatDeathFades(struct CombatActionProc* proc) {
    struct MuProc* muProc;

    proc->unk_54 = NULL;

    if (DidUnitDie(&gBattleActor.unit)) {
        muProc = Proc_Find(ProcScr_Mu);
        MU_StartDeathFade(muProc);
        proc->unk_54 = muProc;

        TryRemoveUnitFromBallista(&gBattleActor.unit);
    }

    if (DidUnitDie(&gBattleTarget.unit)) {
        struct Unit* target = GetUnit(gBattleTarget.unit.index);
        target->state |= US_HIDDEN;

        TryRemoveUnitFromBallista(target);

        RefreshUnitSprites();
        muProc = StartMu(&gBattleTarget.unit);

        gWorkingMovementScript[0] = GetFacingDirection(gBattleActor.unit.xPos, gBattleActor.unit.yPos, gBattleTarget.unit.xPos, gBattleTarget.unit.yPos);
        gWorkingMovementScript[1] = MOVE_CMD_HALT;

        SetMuMoveScript(muProc, gWorkingMovementScript);
        MU_StartDeathFade(muProc);

        proc->unk_54 = muProc;
    }

    return;
}

//! FE8U = 0x08032860
void BATTLE_DeleteLinkedMOVEUNIT(struct CombatActionProc* proc) {
    EndMu(proc->unk_54);
    return;
}

//! FE8U = 0x0803286C
void BATTLE_HandleCombatDeaths(struct CombatActionProc* proc) {
    struct Unit* unitA = GetUnit(proc->unitIdA);
    struct Unit* unitB = GetUnit(proc->unitIdB);

    DropRescueOnDeath(proc, unitA);
    DropRescueOnDeath(proc, unitB);

    KillUnitOnCombatDeath(unitA, unitB);
    KillUnitOnCombatDeath(unitB, unitA);

    return;
}

//! FE8U = 0x080328B0
void StartMapSongBgmIfChanged(void) {
    int bgmIdx = RANDO_MAP_SONG(GetCurrentMapMusicIndex());

    if (GetCurrentBgmSong() != bgmIdx) {
        StartBgmExt(bgmIdx, 6, NULL);
    }

    return;
}

//! FE8U = 0x080328D0
bool BATTLE_HandleItemDrop(struct CombatActionProc* proc) {
    struct Unit* unitA = NULL;
    struct Unit* unitB;

    proc->unitIdA = gBattleActor.unit.index;
    proc->unitIdB = gBattleTarget.unit.index;

    if (gBattleActor.unit.curHP == 0) {
        unitA = GetUnit(gBattleActor.unit.index);
        unitB = GetUnit(gBattleTarget.unit.index);
    }

    if (gBattleTarget.unit.curHP == 0) {
        unitA = GetUnit(gBattleTarget.unit.index);
        unitB = GetUnit(gBattleActor.unit.index);
    }

    if (unitA == NULL) {
        return true;
    }

    if (!(unitA->state & US_DROP_ITEM)) {
        return true;
    }

    if (unitA->items[0] == 0) {
        return true;
    }

#ifdef PC_PORT
    /* D-79, integration 11 "nullunit2" (round 10 paths review; port-plan/pending/folded/r10-nullunit2.md):
       a Devil Axe backfire kills its wielder in a battle against a cracked wall or a snag, so
       unitB is GetUnit(0) = NULL (P-27's case, KillUnitOnCombatDeath below). When the dead
       wielder has US_DROP_ITEM and an item, the GBA's ldrsb at 0x08032940 loads index (+0x0B)
       through NULL: lane 3 of the BIOS latch, 0xE3 post-SWI (0xE5, 0xE2, 0xE1 for the other
       latches), & 0xC0 = 0xC0, not FACTION_BLUE, so TRUE (no popup) for every latch. Not reached
       in story play (no Devil Axe enemy attacks walls; blue units lose US_DROP_ITEM), only in
       the edited start pd14 (port/test/inputs/paths/pd14.keys). PORT_UNIT_FACTION (port.h) gives
       the rule's lane; with a live unit, the retail read. */
    if (PORT_UNIT_FACTION(unitB) != FACTION_BLUE) {
#else
    if (UNIT_FACTION(unitB) != FACTION_BLUE) {
#endif // PC_PORT
        return true;
    }

    NewPopup_GeneralItemGot(
        unitB,
        GetUnitLastItem(unitA),
        proc
    );

    return false;
}

//! FE8U = 0x08032974
void ArenaAction_MaybeSkipDeathFades(ProcPtr proc) {
    gBattleTarget.unit.maxHP = 1;
    gBattleTarget.unit.curHP = 1;

    if (gBattleActor.unit.curHP != 0) {
        Proc_Goto(proc, 1);
    }

    return;
}

//! FE8U = 0x080329A0
void BATTLE_HandleArenaDeathsMaybe(ProcPtr proc) {
    KillUnitOnArenaDeathMaybe(gActiveUnit);
    DropRescueOnDeath(proc, gActiveUnit);

    return;
}

struct BattleHit * StoreScriptBattleHits(struct BattleHit * r0)
{
    CpuFastCopy(r0, gActionData.script_hits, 0x1C);
    return gActionData.script_hits;
}
