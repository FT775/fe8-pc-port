/**
 * Some unused functions and seems cropped.
 * maybe for debug and the source code is not compiled after test.
 */

#include "global.h"
#include "variables.h"
#include "functions.h"

#include "hardware.h"
#include "proc.h"
#include "event.h"
#include "bmunit.h"
#include "bmusemind.h"
#include "player_interface.h"
#include "bmtrick.h"
#include "bmreliance.h"
#include "scene.h"

struct Struct089ED67C {
    /* 00 */ u8 _pad00[0x0C];
    /* 0C */ s8 (*func)();
    /* 10 */ u8 _pad10[0x18 - 0x10];
};

CONST_DATA struct Struct089ED67C gUnknown_089ED67C[] = { 0 };
CONST_DATA struct Struct089ED67C gUnknown_089ED694[] = { 0 };
CONST_DATA struct Struct089ED67C gUnknown_089ED6AC[] = {
    {0},
    {0},
    {0},
    {0},
};

u8 PickRandomTrueCond_Table0()
{
    u8 buf[0x20];
    int i;
    int val = 0;
    int loc = 0;

    for (i = 0; gUnknown_089ED67C[i].func; i++, val++) {
        if (1 == gUnknown_089ED67C[i].func())
            buf[loc++] = val;
    }

    if (0 == loc)
        return -1;
    
    return buf[(GetGameClock() % 0x40) % loc];
}

u8 PickRandomTrueCond_Table1()
{
    u8 buf[0x20];
    int i;
    int val = 0;
    int loc = 0;

    for (i = 0; gUnknown_089ED694[i].func; i++, val++) {
        if (1 == gUnknown_089ED694[i].func())
            buf[loc++] = val;
    }

    if (0 == loc)
        return -1;
    
    return buf[(GetGameClock() % 0x40) % loc];
}

u8 PickRandomTrueCond_Table2()
{
    u8 buf[0x20];
    int i;
    int val = 0;
    int loc = 0;

    for (i = 0; gUnknown_089ED6AC[i].func; i++, val++) {
        if (1 == gUnknown_089ED6AC[i].func())
            buf[loc++] = val;
    }

    if (0 == loc)
        return -1;

    return buf[(GetGameClock() % 0x40) % loc];
}

u8 GetClockCycleIndex_Div1()
{
    int i, val = 0;
    for (i = 0; gUnknown_089ED6AC[i].func; i++, val++);

    return (GetGameClock() % 0x10) % val;
}

u8 GetClockCycleIndex_Div2()
{
    int i, val = 0;
    for (i = 0; gUnknown_089ED6AC[i].func; i++, val++);

    return ((GetGameClock() / 2) % 0x10) % val;
}

u8 GetClockCycleIndex_Div4()
{
    int i, val = 0;
    for (i = 0; gUnknown_089ED6AC[i].func; i++, val++);

    return ((GetGameClock() / 4) % 0x10) % val;
}

int sub_8084A48()
{
    int i, ret = 0;
    for (i = 1; i < 0x40; i++)
        GetUnit(i);

    if (2 == ret)
        return 1;
    else
        return 0;
}

int sub_8084A68()
{
    int i;
    for (i = 1; i < 0x40; i++)
        GetUnit(i);
    
    return 0;
}

int sub_8084A80() {return 1;}
int sub_8084A84() {return 0;}
int sub_8084A88() {return 0;}
int sub_8084A8C() {return 0;}
int sub_8084A90() {return 0;}
int sub_8084A94() {return 0;}
int sub_8084A98() {return 0;}
int sub_8084A9C() {return 0;}
int sub_8084AA0() {return 0;}
int sub_8084AA4() {return 0;}
int sub_8084AA8() {return 0;}
int sub_8084AAC() {return 0;}
int sub_8084AB0() {return 0;}
int sub_8084AB4() {return 0;}
int sub_8084AB8() {return 0;}
int sub_8084ABC() {return 0;}
int sub_8084AC0() {return 0;}
int sub_8084AC4() {return 0;}
int sub_8084AC8() {return 0;}
int sub_8084ACC() {return 0;}
int sub_8084AD0() {return 0;}
int sub_8084AD4() {return 0;}
int sub_8084AD8() {return 0;}

void PromoteChar100AndHide()
{
    EndPlayerPhaseSideWindows();
    ExecUnitDefaultPromotionAndHide(GetUnitFromCharId(0x100), 0);
}

int sub_8084AF4()
{
    int i;
    for (i = 1; i < 0x40; i++)
        GetUnit(i);
    
    return 0;
}

void RemoveAllLightRunes()
{
    struct Trap *cur;
    
    for (cur = GetTrap(0); TRAP_NONE != cur->type; cur++)
        if (TRAP_LIGHT_RUNE == cur->type) {
            RemoveLightRune(cur);
            cur--;
        }
}

void DebugSoftReset_01()
{
    SoftReset(0xFE);
}

int IsAButtonNotPressed()
{
    if (A_BUTTON & gKeyStatusPtr->newKeys)
        return 0;
    else
        return 1;
}

void DebugSoftReset_02()
{
    SoftReset(0xFE);
}

int IsAButtonPressed()
{
    if (A_BUTTON & gKeyStatusPtr->newKeys)
        return 1;
    else
        return 0;
}

void RemoveFog()
{
    UpdateMapViewWithFog(0);
}

void AddFiveThousandGold()
{
    SetPartyGoldAmount(GetPartyGoldAmount() + 5000);
}

void RemoveGold(int sub_val)
{
    if ((int)GetPartyGoldAmount() < sub_val)
        return;

    SetPartyGoldAmount(GetPartyGoldAmount() - sub_val);
}

void WeakenUnitStats(struct Unit *unit)
{
    if (unit->maxHP > 0)
        unit->maxHP -= 1;
    
    if (unit->pow > 0)
        unit->pow -= 1;
    
    if (unit->skl > 0)
        unit->skl -= 1;

    if (unit->spd > 0)
        unit->spd -= 1;

    if (unit->def > 0)
        unit->def -= 1;

    if (unit->res > 0)
        unit->res -= 1;

    if (unit->lck > 0)
        unit->lck -= 1;
}

void WeakenUnitStatsByPid(u8 pid)
{
    WeakenUnitStats(GetUnitFromCharId(pid));
}

s8 IsPidBlueDeployed(u8 pid)
{
    int i;
    struct Unit *unit;
    const struct CharacterData *chara;

    for (i = 1; i < FACTION_GREEN; i++) {
        unit = GetUnit(i);
        if (NULL == unit)
            continue;
        
        chara = unit->pCharacterData;
        if (NULL == chara)
            continue;

        if (0 == ((US_BIT16 | US_NOT_DEPLOYED | US_DEAD) & unit->state))
            if (chara->number == pid)
                return 1;
    }

    return 0;
}

s8 IsPidBlueDeployed_Pid0_01() {return IsPidBlueDeployed(0);}
s8 IsPidBlueDeployed_Pid0_02() {return IsPidBlueDeployed(0);}

s8 IsPidBlue(u8 pid)
{
    int i;
    struct Unit *unit;
    const struct CharacterData *chara;

    for (i = 1; i < FACTION_GREEN; i++) {
        unit = GetUnit(i);
        if (NULL == unit)
            continue;
        
        chara = unit->pCharacterData;
        if (NULL == chara)
            continue;

        if (0 == ((US_DEAD) & unit->state))
            if (chara->number == pid)
                return 1;
    }

    return 0;
}

s8 IsPidBlue_Pid0_01() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_02() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_03() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_04() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_05() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_06() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_07() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_08() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_09() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_10() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_11() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_12() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_13() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_14() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_15() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_16() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_17() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_18() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_19() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_20() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_21() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_22() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_23() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_24() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_25() {return IsPidBlue(0);}
s8 IsPidBlue_Pid0_26() {return IsPidBlue(0);}

s8 DoesPidHaveBlueUnit(u8 pid)
{
    int i;
    struct Unit *unit;
    const struct CharacterData *chara;

    for (i = 1; i < FACTION_GREEN; i++) {
        unit = GetUnit(i);
        if (NULL == unit)
            continue;
        
        chara = unit->pCharacterData;
        if (NULL == chara)
            continue;

        if (chara->number == pid)
            return 1;
    }

    return 0;
}

s8 DoesPidHaveBlueUnit_Pid0_01() {return DoesPidHaveBlueUnit(0);}
s8 DoesPidHaveBlueUnit_Pid0_02() {return DoesPidHaveBlueUnit(0);}
s8 DoesPidHaveBlueUnit_Pid0_03() {return DoesPidHaveBlueUnit(0);}

int IsAnyEnemyUnitAlive(void)
{
    int i;
    struct Unit *unit;
    for (i = FACTION_RED + 1; i < FACTION_PURPLE; i++) {
        unit = GetUnit(i);
        if (UNIT_IS_VALID(unit)) {
            if (0 == (US_DEAD & unit->state))
                return 1;
        }
    }
    return 0;
}

u16 CountLivingEnemyUnits()
{
    int i;
    u16 ret = 0;
    struct Unit *unit;
    for (i = FACTION_RED + 1; i < FACTION_PURPLE; i++) {
        unit = GetUnit(i);
        if (UNIT_IS_VALID(unit)) {
            if (0 == (US_DEAD & unit->state))
                ret++;
        }
    }
    return ret;
}

int AreAllEnemyUnitsDead()
{
    int dead = IsAnyEnemyUnitAlive();
    int ret = 0;
    u8 _dead = dead;
    ret = 0 == _dead ? 1 : 0;
    return ret;
}

s8 IsPidNonBlue(u8 pid)
{
    int i;
    struct Unit *unit;
    for (i = FACTION_GREEN + 1; i < FACTION_PURPLE; i++) {
        unit = GetUnit(i);
        if (UNIT_IS_VALID(unit)) {
            if (0 == (US_DEAD & unit->state))
                if (unit->pCharacterData->number == pid)
                    return 1;
        }
    }
    return 0;
}

int CountBlueUnitsYetToAct()
{
    int i;
    int ret = 0;
    struct Unit *unit;
    for (i = FACTION_BLUE + 1; i < FACTION_GREEN; i++) {
        unit = GetUnit(i);
        if (UNIT_IS_VALID(unit)) {
            if (0 == ((US_NOT_DEPLOYED | US_DEAD) & unit->state))
                if (0 == (US_UNSELECTABLE & unit->state))
                    ret++;
        }
    }
    return ret;
}

int HaveAllGreenUnitsEndedTurn()
{
    int i;
    struct Unit *unit;
    for (i = FACTION_GREEN + 1; i < FACTION_RED; i++) {
        unit = GetUnit(i);
        if (UNIT_IS_VALID(unit)) {
            if (0 == (US_UNSELECTABLE & unit->state))
                return 0;
        }
    }
    return 1;
}

s8 IsPidNonBlue_Pid0_01() {return IsPidNonBlue(0);}
s8 IsPidNonBlue_Pid0_02() {return IsPidNonBlue(0);}
s8 IsPidNonBlue_Pid0_03() {return IsPidNonBlue(0);}
s8 IsPidNonBlue_Pid0_04() {return IsPidNonBlue(0);}
s8 IsPidNonBlue_Pid0_05() {return IsPidNonBlue(0);}
s8 IsPidNonBlue_Pid0_06() {return IsPidNonBlue(0);}
s8 IsPidNonBlue_Pid0_07() {return IsPidNonBlue(0);}
s8 IsPidNonBlue_Pid0_08() {return IsPidNonBlue(0);}
s8 IsPidNonBlue_Pid0_09() {return IsPidNonBlue(0);}
s8 IsPidNonBlue_Pid0_10() {return IsPidNonBlue(0);}
s8 IsPidNonBlue_Pid0_11() {return IsPidNonBlue(0);}
s8 IsPidNonBlue_Pid0_12() {return IsPidNonBlue(0);}
s8 IsPidNonBlue_Pid0_13() {return IsPidNonBlue(0);}
s8 IsPidNonBlue_Pid0_14() {return IsPidNonBlue(0);}
s8 IsPidNonBlue_Pid0_15() {return IsPidNonBlue(0);}
s8 IsPidNonBlue_Pid0_16() {return IsPidNonBlue(0);}
s8 IsPidNonBlue_Pid0_17() {return IsPidNonBlue(0);}

s8 HaveCharactersMaxSupport_Chars0And0_01() {return HaveCharactersMaxSupport(0, 0);}
s8 HaveCharactersMaxSupport_Chars0And0_02() {return HaveCharactersMaxSupport(0, 0);}
s8 HaveCharactersMaxSupport_Chars0And0_03() {return HaveCharactersMaxSupport(0, 0);}
s8 HaveCharactersMaxSupport_Chars0And0_04() {return HaveCharactersMaxSupport(0, 0);}
s8 HaveCharactersMaxSupport_Chars0And0_05() {return HaveCharactersMaxSupport(0, 0);}
s8 HaveCharactersMaxSupport_Chars0And0_06() {return HaveCharactersMaxSupport(0, 0);}

int CountLivingUnitsWithPidInFaction(u8 pid, int faction)
{
    int i;
    struct Unit * unit;
    int ret = 0;

    for (i = faction + 1; i < faction + 0x40; i++) {
        unit = GetUnit(i);
        if (!UNIT_IS_VALID(unit))
            continue;

        if (US_DEAD & unit->state)
            continue;

        if (unit->pCharacterData->number == pid)
            ret++;
    }

    return ret;
}

int CountLivingUnitsInFaction(int faction)
{
    int i;
    struct Unit * unit;
    int ret = 0;

    for (i = faction + 1; i < faction + 0x40; i++) {
        unit = GetUnit(i);
        if (!UNIT_IS_VALID(unit))
            continue;

        if (0 == (US_DEAD & unit->state))
            ret++;
    }
    return ret;
}

int IsPastTurn20()
{
    return gPlaySt.chapterTurnNumber > 20;
}

int IsPastTurn25()
{
    return gPlaySt.chapterTurnNumber > 25;
}

int IsPastTurn30()
{
    return gPlaySt.chapterTurnNumber > 30;
}

int IsBluePhase()
{
    return FACTION_BLUE == gPlaySt.faction;
}

int IsRedPhase()
{
    return FACTION_RED == gPlaySt.faction;
}

int IsActiveUnitMale()
{
    return 0 == (CA_FEMALE & UNIT_CATTRIBUTES(gActiveUnit));
}

int HasAtLeast10000Gold()
{
    return (int)GetPartyGoldAmount() > 9999;
}

int HasAtLeast8000Gold()
{
    return (int)GetPartyGoldAmount() > 7999;
}

int HasAtLeast6000Gold()
{
    return (int)GetPartyGoldAmount() > 5999;
}

int HasAtLeast5000Gold()
{
    return (int)GetPartyGoldAmount() > 4999;
}

int IsTalkChoiceYes()
{
    return 1 == GetTalkChoiceResult();
}

int AreAtLeast50EnemyUnitsAlive()
{
    if (CountLivingEnemyUnits() >= 50)
        return 1;
    else
        return 0;
}

int sub_8085350()
{
    return 0;
}

int IsActiveUnitHpZero()
{
    return GetUnitCurrentHp(gActiveUnit) == 0;
}

int sub_8085370()
{
    return 0;
}
