#ifndef GUARD_BM_RELIANCE_H
#define GUARD_BM_RELIANCE_H

#include "global.h"

enum
{
    // Config

    MAX_SIMULTANEOUS_SUPPORT_COUNT = 5,
    SUPPORT_BONUSES_MAX_DISTANCE = 3,
};

enum
{
    SUPPORT_LEVEL_NONE,
    SUPPORT_LEVEL_C,
    SUPPORT_LEVEL_B,
    SUPPORT_LEVEL_A,
};

enum
{
    SUPPORT_EXP_C = 81,
    SUPPORT_EXP_B = 161,
    SUPPORT_EXP_A = 241,
};

struct SupportData
{
    /* 00 */ u8 characters[UNIT_SUPPORT_MAX_COUNT];
    /* 07 */ u8 supportExpBase[UNIT_SUPPORT_MAX_COUNT];
    /* 0E */ u8 supportExpGrowth[UNIT_SUPPORT_MAX_COUNT];
    /* 15 */ u8 supportCount;
#ifdef PC_PORT
} AGBCC_STRUCT;
#else
};
#endif

struct SupportBonuses
{
    /* 00 */ u8 affinity;

    /* 01 */ u8 bonusAttack;
    /* 02 */ u8 bonusDefense;
    /* 03 */ u8 bonusHit;
    /* 04 */ u8 bonusAvoid;
    /* 05 */ u8 bonusCrit;
    /* 06 */ u8 bonusDodge;
#ifdef PC_PORT
} AGBCC_STRUCT;
#else
};
#endif

extern struct SupportData CONST_DATA gSupportData[];
extern CONST_DATA struct SupportBonuses gAffinityBonuses[];

int GetUnitSupporterCount(struct Unit* unit);
u8 GetUnitSupporterCharacter(struct Unit* unit, int num);
struct Unit* GetUnitSupporterUnit(struct Unit* unit, int num);
int GetUnitSupportLevel(struct Unit* unit, int num);
int GetUnitTotalSupportLevel(struct Unit* unit);
void UnitGainSupportExp(struct Unit* unit, int num);
void UnitGainSupportLevel(struct Unit* unit, int num);
s8 CanUnitSupportNow(struct Unit* unit, int num);
int GetUnitSupporterInitialExp(struct Unit* unit, int num);
int GetUnitSupporterNum(struct Unit* unit, u8 charId);
void InitUnitsupports(struct Unit* unit);
void ProcessTurnSupportExp(void);
int GetUnitSupportBonuses(struct Unit* unit, struct SupportBonuses* bonuses);
int GetUnitAffinityIcon(struct Unit* unit);
int GetCharacterAffinityIcon(int characterId);
int GetSupportLevelUiChar(int level);
char* GetAffinityName(int affinity); // unused?
s8 HaveCharactersMaxSupport(u8 charA, u8 charB);
void SwapUnitStats(struct Unit* unitA, struct Unit* unitB);

extern CONST_DATA struct SupportData SupportData_Eirika;
extern CONST_DATA struct SupportData SupportData_Ephraim;
extern CONST_DATA struct SupportData SupportData_Seth;
extern CONST_DATA struct SupportData SupportData_Franz;
extern CONST_DATA struct SupportData SupportData_Gilliam;
extern CONST_DATA struct SupportData SupportData_Moulder;
extern CONST_DATA struct SupportData SupportData_Vanessa;
extern CONST_DATA struct SupportData SupportData_Garcia;
extern CONST_DATA struct SupportData SupportData_Ross;
extern CONST_DATA struct SupportData SupportData_Colm;
extern CONST_DATA struct SupportData SupportData_Neimi;
extern CONST_DATA struct SupportData SupportData_Artur;
extern CONST_DATA struct SupportData SupportData_Lute;
extern CONST_DATA struct SupportData SupportData_Natasha;
extern CONST_DATA struct SupportData SupportData_Joshua;
extern CONST_DATA struct SupportData SupportData_Kyle;
extern CONST_DATA struct SupportData SupportData_Forde;
extern CONST_DATA struct SupportData SupportData_Tana;
extern CONST_DATA struct SupportData SupportData_Innes;
extern CONST_DATA struct SupportData SupportData_Gerik;
extern CONST_DATA struct SupportData SupportData_Tethys;
extern CONST_DATA struct SupportData SupportData_Marisa;
extern CONST_DATA struct SupportData SupportData_Saleh;
extern CONST_DATA struct SupportData SupportData_Ewan;
extern CONST_DATA struct SupportData SupportData_LArachel;
extern CONST_DATA struct SupportData SupportData_Dozla;
extern CONST_DATA struct SupportData SupportData_Cormag;
extern CONST_DATA struct SupportData SupportData_Amelia;
extern CONST_DATA struct SupportData SupportData_Rennac;
extern CONST_DATA struct SupportData SupportData_Duessel;
extern CONST_DATA struct SupportData SupportData_Myrrh;
extern CONST_DATA struct SupportData SupportData_Knoll;
extern CONST_DATA struct SupportData SupportData_Syrene;

/* Pair Up (port/reforged/reforged_pairup.c): src/bmreliance.c, a pair's partner counts as next to the
   lead for support bonuses; a pair gains support points twice as fast. Nothing on the GBA. */
#if defined(PC_PORT) && PORT_PLAYED_BUILD
int Reforged_PairPartnerOf(const struct Unit *unit, const struct Unit *other);
void Reforged_PairSupportExtra(struct Unit *unit, int num, struct Unit *other);
#define RFO_PAIR_SUPPORT_NEAR(unit, other) && !Reforged_PairPartnerOf((unit), (other))
#define RFO_PAIR_SUPPORT_EXTRA(unit, num, other) Reforged_PairSupportExtra((unit), (num), (other));
#else
#define RFO_PAIR_SUPPORT_NEAR(unit, other)
#define RFO_PAIR_SUPPORT_EXTRA(unit, num, other)
#endif

/* "Faster supports" (port/reforged/reforged_supports.c): src/bmreliance.c ProcessTurnSupportExp, units
   within 3 squares gain support points too (RFO_SUPPORT_FAR, the distance test's default case) and
   each gain is made 2 or 3 times (RFO_SUPPORT_EXTRA). Nothing on the GBA. */
#if defined(PC_PORT) && PORT_PLAYED_BUILD
int Reforged_SupportNear(const struct Unit *unit, const struct Unit *other);
void Reforged_SupportExtra(struct Unit *unit, int num, struct Unit *other);
int Reforged_SupportMultiplier(void);
#define RFO_SUPPORT_FAR(unit, other) if (Reforged_SupportNear((unit), (other))) break;
#define RFO_SUPPORT_EXTRA(unit, num, other) Reforged_SupportExtra((unit), (num), (other));
#else
#define RFO_SUPPORT_FAR(unit, other)
#define RFO_SUPPORT_EXTRA(unit, num, other)
#endif

#endif // GUARD_BM_RELIANCE_H