#ifndef GUARD_BMARENA_H
#define GUARD_BMARENA_H

#include "global.h"
#include "bmunit.h"

struct ArenaData
{
    /* 00 */ struct Unit * playerUnit;
    /* 04 */ struct Unit * opponentUnit;
    /* 08 */ short matchupGoldValue;
    /* 0A */ u8 result;
    /* 0B */ u8 unk0B;
    /* 0C */ u8 range;
    /* 0D */ u8 playerWpnType;
    /* 0E */ u8 opponentWpnType;
    /* 0F */ u8 playerClassId;
    /* 10 */ u8 opponentClassId;
    /* 11 */ u8 playerLevel;
    /* 12 */ u8 opponentLevel;
    /* 13 */ s8 playerIsMagic;
    /* 14 */ s8 opponentIsMagic;
    /* 16 */ u16 playerPowerWeight;
    /* 18 */ u16 opponentPowerWeight;
    /* 1A */ u16 playerWeapon;
    /* 1C */ u16 opponentWeapon;
};

/* PC played builds: Reforged Options "Arena odds" (port/reforged/reforged_arena.c). RFO_ARENA_ODDS_EARLY:
   src/uiarena.c ArenaUi_WagerGoldDialogue, the opponent's window before the wager question.
   RFO_ARENA_DETAILS: DrawArenaOpponentDetailsText, the window with both sides' odds instead. Elsewhere:
   nothing. RFO_ARENA_TEST_END: ArenaUi_OnEnd, the test switch FE8PC_TEST_ARENA_CLASS's fighter gets
   its own class back. RFO_ARENA_REFUSES: ArenaIsUnitAllowed, "Armed dancers" (v1.18.0): a Dancer or the FE7 Bard
   is refused, as in the original (a Dancer has no weapon rank there): the arena always plays battle animations,
   theirs have no attack (the Dancer's dance hangs a fight in which she strikes twice in a row), and the map
   animation it would fall back to has no map behind it in the arena (a black screen). */
#if defined(PC_PORT) && PORT_PLAYED_BUILD
int Reforged_ArenaOddsOn(void);
int Reforged_ArenaRefuses(struct Unit *unit);
#define RFO_ARENA_REFUSES(unit) if (Reforged_ArenaRefuses(unit)) return 0;
int Reforged_ArenaDrawDetails(void);
void Reforged_ArenaTestEnd(void);
#define RFO_ARENA_ODDS_EARLY(proc) if (Reforged_ArenaOddsOn()) DrawArenaOpponentDetailsText(proc);
#define RFO_ARENA_DETAILS() if (Reforged_ArenaDrawDetails()) return;
#define RFO_ARENA_TEST_END() Reforged_ArenaTestEnd();
#else
#define RFO_ARENA_ODDS_EARLY(proc)
#define RFO_ARENA_DETAILS()
#define RFO_ARENA_TEST_END()
#define RFO_ARENA_REFUSES(unit)
#endif

void ArenaBeginInternal(struct Unit * unit);
void ArenaBegin(struct Unit * unit);
void ArenaResume(struct Unit * unit);
int GetUnitBestWRankType(struct Unit *);
int GetClassBestWRankType(const struct ClassData *);
int ArenaGenerateOpposingClassId(int);
s8 IsWeaponMagic(int);
int ArenaGetOpposingLevel(int);
int ArenaGetPowerRanking(struct Unit *, s8);
void ArenaGenerateOpponentUnit(void);
void ArenaGenerateBaseWeapons(void);
u16 ArenaGetUpgradedWeapon(u16 item);
s8 ArenaAdjustOpponentDamage(void);
s8 ArenaAdjustOpponentPowerRanking(void);
void ArenaGenerateMatchupGoldValue(void);
int ArenaGetMatchupGoldValue(void);
int ArenaGetResult(void);
void ArenaSetResult(int result);
void ArenaContinueBattle(void);
s8 ArenaIsUnitAllowed(struct Unit *);
void ArenaSetFallbackWeaponForUnit(struct Unit * unit, u16 * pItem);
void ArenaSetFallbackWeaponsMaybe(void);

extern struct ArenaData gArenaState;

#endif // GUARD_BMARENA_H
