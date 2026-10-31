#include "global.h"
#include "classchg.h"
#include "proc.h"
#include "constants/characters.h"
#include "constants/classes.h"
#include "bmio.h"
#include "bmmap.h"
#include "bmmind.h"
#include "mu.h"

// TODO: Implicit declaration
#ifdef PC_PORT
u16 GetUnitEquippedWeapon(struct Unit *); /* PC: the definition's return type (host ABI; port/tools/decl_sweep.py) */
#else
u32 GetUnitEquippedWeapon(struct Unit *);
#endif

CONST_DATA struct ProcCmd ProcScr_PromoHandler[] = {
    PROC_SLEEP(3),

PROC_LABEL(0),
    PROC_CALL(PromoHandler_SetInitStat),

PROC_LABEL(1),
    PROC_REPEAT(PromoHandlerIdle),

PROC_LABEL(7),
    PROC_END,
};

CONST_DATA u32 gUnknown_08B12704[] = {
    0x00011e05,
    0x28050008,
    0x50050003,
    0x00080001,
    0x00032805,
    0x00000000,
};

struct TraineeData {
    u8 charId;
    u8 promotionLevel;
    u8 class;
#ifdef PC_PORT
} AGBCC_STRUCT;
#else
};
#endif

u32 PromoHandler_SetupAndStartUI(struct ProcPromoHandler *proc)
{
    struct Unit *unit;
    u8 classNumber;
    u32 terrain;

#ifdef PC_PORT
    /*
     * D-36 (NULL-probe N-24/N-25): the retail loop below runs i = 0 to 6 over this 3-entry array.
     * On the GBA, agbcc puts the array at sp+0 (4-byte entries) under a 16-byte frame, so
     * trainees[3..6] are the words above it:
     *   [3] sp+12, the spill slot of i. charId is read before i = 3 is stored there, so it is 2 (the
     *       stored i = 2); promotionLevel and class are read after, so they are 0.
     *   [4..6] sp+16/20/24, the caller's r8, r9 and r10, saved by the prologue. Nothing that can be on
     *       the path touches them (crt0, AgbMain, ExecMainUpdate, the main callbacks that run proc tree 3:
     *       OnMain, MainUpdateEkrBattle and MainUpdate_8055C68, Proc_Run, RunProcessRecursive,
     *       RunProcessScript, PromoHandlerIdle, the _call_via thunks), so they still hold their value
     *       from cartridge entry, 0. Measured in mGBA on route ch5: r8-r10 = 0 at every ExecMainUpdate
     *       entry (135,506) and every Proc_Run entry from those main callbacks (590,937).
     * The GBA's values are written out here, so the PC reads what the GBA reads (mGBA, routes ch4 and ch5:
     * these four in all 6 calls). Class 0 has no promotion (gPromoJidLut[0] is {0, 0}), so these
     * entries never start a promotion screen (on the routes [3] finds Seth, a Paladin; [4..6] no unit).
     * Not measured: the r8-r10 values the real BIOS hands over at power-on (0 in mGBA; SoftReset
     * clears them, GBATEK).
     */
    struct TraineeData trainees[7] = {
        { CHARACTER_ROSS,   10, CLASS_JOURNEYMAN },
        { CHARACTER_AMELIA, 10, CLASS_RECRUIT },
        { CHARACTER_EWAN,   10, CLASS_PUPIL },
        { 2, 0, 0 },
        { 0, 0, 0 },
        { 0, 0, 0 },
        { 0, 0, 0 },
    };
#else
    struct TraineeData trainees[3] = {
        { CHARACTER_ROSS,   10, CLASS_JOURNEYMAN },
        { CHARACTER_AMELIA, 10, CLASS_RECRUIT },
        { CHARACTER_EWAN,   10, CLASS_PUPIL },
    };
#endif

    switch (gPlaySt.chapterModeIndex) {
        case CHAPTER_MODE_EIRIKA:
        default:
            terrain = TERRAIN_PLAINS;
            break;

        case CHAPTER_MODE_EPHRAIM:
            terrain = TERRAIN_DESERT;
            break;
    }

    if (proc->bmtype == PROMO_HANDLER_TYPE_TRANINEE) {
        u8 i;
        u8 flag;
        proc->bmtype = PROMO_HANDLER_TYPE_TRANINEE;
        proc->sel_en = 1;
        flag = 0;

#if BUGFIX
        for (i = 0; i < 3; i++) {
#else
        for (i = 0; i < 7; i++) {
#endif
            unit = GetUnitFromCharId(trainees[i].charId);
            if (!unit)
                flag = true;

#ifdef PC_PORT
            /*
             * D-36 (NULL-probe N-24/N-25): no such unit (a trainee not recruited yet, or charId 0
             * above). The GBA goes on and reads state, level and pClassData through NULL (BIOS open
             * bus) and ->number through the latch word (open bus), but those values can only set
             * flag, which is already set: the iteration ends with nothing changed, whatever the
             * latch. See DIVERGENCES.md D-36.
             */
            if (!unit) {
                flag = 0;
                continue;
            }
#endif

            if (unit->state & (US_BIT16 | US_DEAD))
                flag = true;

            if (unit->level < trainees[i].promotionLevel)
                flag = true;

            classNumber = unit->pClassData->number;
            if (classNumber != trainees[i].class)
                flag = true;

            if (!gPromoJidLut[classNumber][0] && !gPromoJidLut[classNumber][1])
                flag = true;

            if (!flag) {
                if (gPromoJidLut[classNumber][0] && !gPromoJidLut[classNumber][1]) {
                    proc->jid = gPromoJidLut[classNumber][0];
                    proc->sel_en = 0;
                }

                if (!gPromoJidLut[classNumber][0] && gPromoJidLut[classNumber][1]) {
                    proc->jid = gPromoJidLut[classNumber][1];
                    proc->sel_en = 0;
                }

                if (trainees[i].charId != terrain)
                    MakePromotionScreen(proc, trainees[i].charId, terrain);
                else
                    MakePromotionScreen(proc, trainees[i].charId, TERRAIN_ROAD);

                return PROMO_HANDLER_STAT_IDLE;
            }
            flag = 0;
        }
        return PROMO_HANDLER_STAT_END;
    } else if (proc->bmtype == PROMO_HANDLER_TYPE_BM) {
        proc->bmtype = PROMO_HANDLER_TYPE_BM;
        proc->sel_en = 1;
        unit = GetUnitFromCharId(proc->pid);
        classNumber = unit->pClassData->number;

        /* If no class to promote, end the handler proc */
        if (!gPromoJidLut[classNumber][0] && !gPromoJidLut[classNumber][1])
            return PROMO_HANDLER_STAT_END;

        if (gPromoJidLut[classNumber][0] && !gPromoJidLut[classNumber][1]) {
            proc->jid = gPromoJidLut[classNumber][0];
            proc->sel_en = 0;
        }

        if (!gPromoJidLut[classNumber][0] && gPromoJidLut[classNumber][1]) {
            proc->jid = gPromoJidLut[classNumber][1];
            proc->sel_en = 0;
        }

        MakePromotionScreen(proc, proc->pid, TERRAIN_PLAINS);
        return PROMO_HANDLER_STAT_IDLE;
    } else if (proc->bmtype == PROMO_HANDLER_TYPE_PREP) {
        proc->bmtype = PROMO_HANDLER_TYPE_PREP;
        proc->sel_en = 1;
        unit = GetUnitFromCharId(proc->pid);
        classNumber = unit->pClassData->number;
        if (!gPromoJidLut[classNumber][0] && !gPromoJidLut[classNumber][1]) {
            BMapDispResume();
            RefreshBMapGraphics();
            return PROMO_HANDLER_STAT_END;
        }
        if (gPromoJidLut[classNumber][0] && !gPromoJidLut[classNumber][1]) {
            proc->jid = gPromoJidLut[classNumber][0];
            proc->sel_en = 0;
        }
        if (!gPromoJidLut[classNumber][0] && gPromoJidLut[classNumber][1]) {
            proc->jid = gPromoJidLut[classNumber][1];
            proc->sel_en = 0;
        }
        MakePromotionScreen(proc, proc->pid, TERRAIN_PLAINS);
        return PROMO_HANDLER_STAT_IDLE;
    }
    else
        return PROMO_HANDLER_STAT_END;
}

u8 PromoHandler_SetInitStat(struct ProcPromoHandler *proc)
{
    proc->stat = PROMO_HANDLER_STAT_INIT;
    return 0;
}

void PromoHandlerIdle(struct ProcPromoHandler *proc)
{
    switch (proc->stat) {
    case PROMO_HANDLER_STAT_IDLE:
    default:
        return;

    case PROMO_HANDLER_STAT_INIT:
        proc->stat = PromoHandler_SetupAndStartUI(proc);
        break;

    case PROMO_HANDLER_STAT_END:
        Proc_Break(proc);
        break;
    }
}

void StartTraineePromoScreen(ProcPtr parent)
{
    struct BattleUnit *actor, *target;
    struct ProcPromoHandler *proc = Proc_StartBlocking(ProcScr_PromoHandler, parent);
    proc->bmtype = PROMO_HANDLER_TYPE_TRANINEE;
    proc->u32 = 0;
    proc->unit = NULL;
    proc->item_slot = -1;
    actor = &gBattleActor;
    target = &gBattleTarget;
    target->weaponBefore = 0;
    actor->weaponBefore = 0;
    target->weapon = 0;
    actor->weapon = 0;
    target->statusOut = -1;
}

void StartPrepScreenPromotion(struct ProcPrepItemUse *proc)
{
    struct BattleUnit *actor, *target;
    struct ProcPromoHandler *new_proc;
    struct ProcPrepItemUse *parent;

    u32 weapon;
    u32 slot = proc->slot;
    if (slot != -1) {
        struct BattleUnit *actor, *target;
        actor = &gBattleActor;
        target = &gBattleTarget;
        target->weaponBefore = proc->unit->items[slot];
        actor->weaponBefore = proc->unit->items[slot];
    }

    weapon = GetUnitEquippedWeapon(proc->unit);
    actor = &gBattleActor;
    target = &gBattleTarget;
    target->weapon = weapon;
    actor->weapon = weapon;
    target->statusOut = -1;

    new_proc = Proc_StartBlocking(ProcScr_PromoHandler, proc);
    new_proc->bmtype = PROMO_HANDLER_TYPE_BM;
    new_proc->u32 = 0;

    parent = new_proc->proc_parent;
    new_proc->pid = parent->unit->pCharacterData->number;
    new_proc->unit = parent->unit;
    new_proc->item_slot = parent->slot;
}

void StartBmPromotion(ProcPtr proc)
{
    struct Unit *unit;
    struct ProcPromoHandler *new_proc = Proc_StartBlocking(ProcScr_PromoHandler, proc);
    new_proc->bmtype = PROMO_HANDLER_TYPE_PREP;
    new_proc->u32 = 0;
    unit = GetUnit(gActionData.subjectIndex);
    new_proc->pid = unit->pCharacterData->number;
    new_proc->unit = GetUnit(gActionData.subjectIndex);
    new_proc->item_slot = gActionData.itemSlotIndex;
    BMapDispSuspend();
    EndAllMus();
}
