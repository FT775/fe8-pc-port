#ifndef GUARD_BMBATTLE_H
#define GUARD_BMBATTLE_H

#include "constants/terrains.h"
#include "bmunit.h"

/* PC played builds: the battle's hit array holds 9 strikes and the end mark, for the Astra skill
   (port/reforged/reforged_skills_battle.c: 5 + 2 + 2 at most); everywhere else the original's 7 (6 + 1). */
#if defined(PC_PORT) && PORT_PLAYED_BUILD
#define RFO_BATTLE_HIT_MAX 10
/* the host link places GBA symbols at their GBA addresses by name (port/tools/gen_host_link.py), and
   7 entries on from gBattleHitArray's slot come gBattleHitIterator and two more variables: the bigger
   array gets a name of its own, in host memory */
#define gBattleHitArray gBattleHitArrayPc
#else
#define RFO_BATTLE_HIT_MAX 7
#endif

enum {
    // Battle-related magic constants

    // Maximum amount of hits that can happen in a battle (-1, because of the terminating "hit")
    BATTLE_HIT_MAX = RFO_BATTLE_HIT_MAX,

    // Followup attack (double attack) attack speed difference threshold
    BATTLE_FOLLOWUP_SPEED_THRESHOLD = 4,

    // Maximum battle damage
    BATTLE_MAX_DAMAGE = 127,
};

struct BattleUnit {
    /* 00 */ struct Unit unit;

    /* 48 */ u16 weapon;
    /* 4A */ u16 weaponBefore;
    /* 4C */ u32 weaponAttributes;
    /* 50 */ u8 weaponType;
    /* 51 */ u8 weaponSlotIndex;

    /* 52 */ s8 canCounter;

    /* 53 */ s8 wTriangleHitBonus;
    /* 54 */ s8 wTriangleDmgBonus;

    /* 55 */ u8 terrainId;
    /* 56 */ s8 terrainDefense;
    /* 57 */ s8 terrainAvoid;
    /* 58 */ s8 terrainResistance;
    /* 59 */ /* pad */

    /* 5A */ short battleAttack;
    /* 5C */ short battleDefense;
    /* 5E */ short battleSpeed;
    /* 60 */ short battleHitRate;
    /* 62 */ short battleAvoidRate;
    /* 64 */ short battleEffectiveHitRate;
    /* 66 */ short battleCritRate;
    /* 68 */ short battleDodgeRate;
    /* 6A */ short battleEffectiveCritRate;
    /* 6C */ short battleSilencerRate;

    /* 6E */ s8 expGain;
    /* 6F */ s8 statusOut;
    /* 70 */ s8 levelPrevious;
    /* 71 */ s8 expPrevious;

    /* 72 */ s8 hpInitial;

    /* 73 */ s8 changeHP;
    /* 74 */ s8 changePow;
    /* 75 */ s8 changeSkl;
    /* 76 */ s8 changeSpd;
    /* 77 */ s8 changeDef;
    /* 78 */ s8 changeRes;
    /* 79 */ s8 changeLck;
    /* 7A */ s8 changeCon;

    /* 7B */ s8 wexpMultiplier;
    /* 7C */ s8 nonZeroDamage;
    /* 7D */ s8 weaponBroke;

    /* 7E */ s8 hasItemEffectTarget;
    /* 7F */ /* pad */
};

struct BattleStats {
    /* 00 */ u16 config;

    /* 02 */ u8 range;

    /* 04 */ short damage;
    /* 06 */ short attack;
    /* 08 */ short defense;
    /* 0A */ short hitRate;
    /* 0C */ short critRate;
    /* 0E */ short silencerRate;

    /* 10 */ struct Unit* taUnitA;
    /* 14 */ struct Unit* taUnitB;
};

enum {
    // For use with BattleStats:config

    BATTLE_CONFIG_REAL = (1 << 0),
    BATTLE_CONFIG_SIMULATE = (1 << 1),
    BATTLE_CONFIG_BIT2 = (1 << 2),
    BATTLE_CONFIG_BALLISTA = (1 << 3),
    BATTLE_CONFIG_PROMOTION = (1 << 4),
    BATTLE_CONFIG_ARENA = (1 << 5),
    BATTLE_CONFIG_REFRESH = (1 << 6),
    BATTLE_CONFIG_MAPANIMS = (1 << 7),
    BATTLE_CONFIG_PROMOTION_PREP = (1 << 8),
    BATTLE_CONFIG_DANCERING = (1 << 9),
};

struct BattleHit {
    /* 00:18 */ unsigned attributes : 19;
    /* 19:23 */ unsigned info       : 5;
    /* 24:31 */ signed   hpChange   : 8;
};

enum {
    BATTLE_HIT_ATTR_CRIT      = (1 << 0),
    BATTLE_HIT_ATTR_MISS      = (1 << 1),
    BATTLE_HIT_ATTR_FOLLOWUP  = (1 << 2),
    BATTLE_HIT_ATTR_RETALIATE = (1 << 3),
    BATTLE_HIT_ATTR_BRAVE     = (1 << 4),
    BATTLE_HIT_ATTR_5         = (1 << 5), // unused?
    BATTLE_HIT_ATTR_POISON    = (1 << 6),
    BATTLE_HIT_ATTR_DEVIL     = (1 << 7),
    BATTLE_HIT_ATTR_HPSTEAL   = (1 << 8),
    BATTLE_HIT_ATTR_HPHALVE   = (1 << 9),
    BATTLE_HIT_ATTR_TATTACK   = (1 << 10), // triangle attack!
    BATTLE_HIT_ATTR_SILENCER  = (1 << 11),
    BATTLE_HIT_ATTR_12        = (1 << 12), // unused?
    BATTLE_HIT_ATTR_PETRIFY   = (1 << 13),
    BATTLE_HIT_ATTR_SURESHOT  = (1 << 14),
    BATTLE_HIT_ATTR_GREATSHLD = (1 << 15),
    BATTLE_HIT_ATTR_PIERCE    = (1 << 16),
    BATTLE_HIT_ATTR_17        = (1 << 17), // unused?
    BATTLE_HIT_ATTR_18        = (1 << 18), // unused?
};

enum {
    BATTLE_HIT_INFO_BEGIN        = (1 << 0),
    BATTLE_HIT_INFO_FINISHES     = (1 << 1),
    BATTLE_HIT_INFO_KILLS_TARGET = (1 << 2),
    BATTLE_HIT_INFO_RETALIATION  = (1 << 3),
    BATTLE_HIT_INFO_END          = (1 << 4),
};

enum {
    // Special item slot identifiers understood by SetBattleUnitWeapon and other functions

    BU_ISLOT_AUTO           = -1,
    BU_ISLOT_5              = UNIT_ITEM_COUNT + 0,
    BU_ISLOT_ARENA_PLAYER   = UNIT_ITEM_COUNT + 1,
    BU_ISLOT_ARENA_OPPONENT = UNIT_ITEM_COUNT + 2,
    BU_ISLOT_BALLISTA       = UNIT_ITEM_COUNT + 3,
};

// Public objects

extern struct BattleStats gBattleStats;

extern struct BattleUnit gBattleActor;
extern struct BattleUnit gBattleTarget;

extern struct BattleHit gBattleHitArray[BATTLE_HIT_MAX];
extern struct BattleHit* gBattleHitIterator;

// Public functions

void BattleApplyGameStateUpdates(void);
void BattleGenerateSimulation(struct Unit* actor, struct Unit* target, int x, int y, int actorWpnSlot);
void BattleGenerateReal(struct Unit* actor, struct Unit* target);
void BattleGenerateBallistaSimulation(struct Unit* actor, struct Unit* target, int x, int y);
void BattleGenerateBallistaReal(struct Unit* actor, struct Unit* target);
void BattleGenerate(struct Unit* actor, struct Unit* target);
void BattleGenerateUiStats(struct Unit* unit, s8 itemSlot);
void InitBattleUnit(struct BattleUnit* bUnit, struct Unit* unit);
void InitBattleUnitWithoutBonuses(struct BattleUnit* bu, struct Unit* unit);
void SetBattleUnitTerrainBonuses(struct BattleUnit* bu, int terrain);
void SetBattleUnitTerrainBonusesAuto(struct BattleUnit* bu);
void SetBattleUnitWeapon(struct BattleUnit* bu, int itemSlot);
void SetBattleUnitWeaponBallista(struct BattleUnit* bu);
void sub_802A958(void); // unused?
void ClearBattleHits(void);
void BattleUnwind(void);
void BattleGetBattleUnitOrder(struct BattleUnit** outAttacker, struct BattleUnit** outDefender);
s8 BattleGetFollowUpOrder(struct BattleUnit** outAttacker, struct BattleUnit** outDefender);
void BattleApplyExpGains(void);
int GetAutoleveledStatIncrease(int growth, int levelCount);
s8 CanBattleUnitGainLevels(struct BattleUnit* bu);
void CheckBattleUnitLevelUp(struct BattleUnit* bu);
void ApplyUnitDefaultPromotion(struct Unit* unit);
void ApplyUnitPromotion(struct Unit* unit, u8 classId);
void GenerateBattleUnitStatGainsComparatively(struct BattleUnit* bu, struct Unit* unit);
void CheckBattleUnitStatCaps(struct Unit* unit, struct BattleUnit* bu);
void BattleApplyUnitUpdates(void);
s8 sub_802C0B0(void); // unused?
s8 HasBattleUnitGainedWeaponLevel(struct BattleUnit* bu);
void UpdateUnitFromBattle(struct Unit* unit, struct BattleUnit* bu);
void UpdateUnitDuringBattle(struct Unit* unit, struct BattleUnit* bu);
void BattleApplyBallistaUpdates(void);
void NullSomeStuff(void);
void BattleUnitTargetSetEquippedWeapon(struct BattleUnit* bu);
void BattleUnitTargetCheckCanCounter(struct BattleUnit* bu);
void BattleApplyWeaponTriangleEffect(struct BattleUnit* actor, struct BattleUnit* target);
void BattleInitTargetCanCounter(void);
void InitObstacleBattleUnit(void);
void UpdateObstacleFromBattle(struct BattleUnit* bu);
void BeginBattleAnimations(void);
int GetSoloAnimPreconfType(struct Unit* unit);
int GetBattleAnimPreconfType(void);
void BattleInitItemEffect(struct Unit* actor, int itemSlot);
void BattleInitItemEffectTarget(struct Unit* unit);
void BattleApplyMiscAction(struct Proc* proc);
void BattleApplyItemEffect(struct Proc* proc);
int GetOffensiveStaffAccuracy(struct Unit* actor, struct Unit* target);
void BattleGenerateArena(struct Unit* unit);
s8 BattleIsTriangleAttack(void);
s8 DidBattleUnitBreakWeapon(struct BattleUnit* bu);
void SetScriptedBattle(struct BattleHit* hits);
void UnitLevelUp(struct Unit* unit);
void BattleHitAdvance(void);
void BattleHitTerminate(void);

void UpdateActorFromBattle(void);

void BattleGenerateSimulationInternal(struct Unit* actor, struct Unit* target, int x, int y, int actorWpnSlot);
void BattleGenerateRealInternal(struct Unit* actor, struct Unit* target);

s8 BattleRoll1RN(u16 threshold, s8 simResult);
s8 BattleRoll2RN(u16 threshold, s8 simResult);

void ComputeBattleUnitStats(struct BattleUnit* attacker, struct BattleUnit* defender);
void ComputeBattleUnitEffectiveStats(struct BattleUnit* attacker, struct BattleUnit* defender);
void ComputeBattleUnitSupportBonuses(struct BattleUnit* attacker, struct BattleUnit* defender);
void ComputeBattleUnitDefense(struct BattleUnit* attacker, struct BattleUnit* defender);
void ComputeBattleUnitBaseDefense(struct BattleUnit* bu);
void ComputeBattleUnitAttack(struct BattleUnit* attacker, struct BattleUnit* defender);
void ComputeBattleUnitSpeed(struct BattleUnit* bu);
void ComputeBattleUnitHitRate(struct BattleUnit* bu);
void ComputeBattleUnitAvoidRate(struct BattleUnit* bu);
void ComputeBattleUnitCritRate(struct BattleUnit* bu);
void ComputeBattleUnitDodgeRate(struct BattleUnit* bu);
void ComputeBattleUnitEffectiveHitRate(struct BattleUnit* attacker, struct BattleUnit* defender);
void ComputeBattleUnitEffectiveCritRate(struct BattleUnit* attacker, struct BattleUnit* defender);
void ComputeBattleUnitSilencerRate(struct BattleUnit* attacker, struct BattleUnit* defender);
void ComputeBattleUnitWeaponRankBonuses(struct BattleUnit* bu);
void ComputeBattleUnitStatusBonuses(struct BattleUnit* bu);
void ComputeBattleUnitSpecialWeaponStats(struct BattleUnit* attacker, struct BattleUnit* defender);

s8 BattleGenerateRoundHits(struct BattleUnit* attacker, struct BattleUnit* defender);
int GetBattleUnitHitCount(struct BattleUnit* attacker);
int BattleCheckBraveEffect(struct BattleUnit* bu);

s8 BattleCheckTriangleAttack(struct BattleUnit* attacker, struct BattleUnit* defender);
void BattleUpdateBattleStats(struct BattleUnit* attacker, struct BattleUnit* defender);
void BattleCheckSureShot(struct BattleUnit* attacker);
void BattleCheckPierce(struct BattleUnit* attacker, struct BattleUnit* defender);
void BattleCheckGreatShield(struct BattleUnit* attacker, struct BattleUnit* defender);
s8 BattleCheckSilencer(struct BattleUnit* attacker, struct BattleUnit* defender);
void BattleCheckPetrify(struct BattleUnit* attacker, struct BattleUnit* defender);
void BattleGenerateHitAttributes(struct BattleUnit* attacker, struct BattleUnit* defender);
void BattleGenerateHitTriangleAttack(struct BattleUnit* attacker, struct BattleUnit* defender);
void BattleGenerateHitEffects(struct BattleUnit* attacker, struct BattleUnit* defender);
s8 BattleGenerateHit(struct BattleUnit* attacker, struct BattleUnit* defender);

int GetStatIncrease(int growth);

int GetBattleUnitUpdatedWeaponExp(struct BattleUnit* bu);

int GetUnitExpLevel(struct Unit* unit);
int GetUnitRoundExp(struct Unit* actor, struct Unit* target);
int GetUnitPowerLevel(struct Unit* unit);
int GetUnitClassKillExpBonus(struct Unit* actor, struct Unit* target);
int GetUnitExpMultiplier(struct Unit* actor, struct Unit* target);
int GetUnitKillExpBonus(struct Unit* actor, struct Unit* target);
void ModifyUnitSpecialExp(struct Unit* actor, struct Unit* target, int* exp);
int GetBattleUnitExpGain(struct BattleUnit* actor, struct BattleUnit* target);
void BattleApplyItemExpGains(void);
int GetBattleUnitStaffExp(struct BattleUnit* bu);
void BattleApplyMiscActionExpGains(void);

void BattleApplyReaverEffect(struct BattleUnit* attacker, struct BattleUnit* defender);

void ComputeBattleObstacleStats(void);

void BattlePrintDebugUnitInfo(struct BattleUnit* actor, struct BattleUnit* target);
void BattlePrintDebugHitInfo(void);

void BattleGenerateHitScriptedDamage(struct BattleUnit* bu);
void BattleUnwindScripted(void);

#define BUNIT_IS_OBSTACLE(aBu) (((aBu)->terrainId == TERRAIN_WALL_DAMAGED) || ((aBu)->terrainId == TERRAIN_SNAG))

/*
 * PC played builds only: Reforged Options' "Game fixes" (port/reforged/reforged_gamefixes.c), each OFF by
 * default (then the value is the original's). In every other build each is its value, on the same line.
 *   RFO_EARLY_KILL_EXP  src/bmbattle.c GetUnitKillExpBonus, before the route split, a target stronger
 *                             than the actor: its kill term is never below an equal-strength kill's
 *                             (ceil(P(actor) / 2); P is the class and level power, plus 20 x the base
 *                             class's relative power for a promoted unit).
 *   RFO_DEVIL_CHANCE     src/bmbattle.c BattleGenerateHitEffects: the Devil effect's backfire chance,
 *                             31 - Luck, stops at 0 instead of wrapping to 100% (Luck 32 and up).
 */
#if defined(PC_PORT) && PORT_PLAYED_BUILD
int Reforged_FixEarlyKillExp(int local, struct Unit *actor);
int Reforged_FixDevilChance(int threshold);
#define RFO_EARLY_KILL_EXP(orig, actor) Reforged_FixEarlyKillExp((orig), (actor))
#define RFO_DEVIL_CHANCE(t) Reforged_FixDevilChance(t)
#else
#define RFO_EARLY_KILL_EXP(orig, actor) (orig)
#define RFO_DEVIL_CHANCE(t) (t)
#endif

/* Pair Up (port/reforged/reforged_pairup.c): src/bmbattle.c, Dual Guard. A blocked hit carries
   BATTLE_HIT_ATTR_GREATSHLD (the shield flash) and BATTLE_HIT_ATTR_17 (no effects, no gauge). */
#if defined(PC_PORT) && PORT_PLAYED_BUILD
int Reforged_PairDualGuard(struct BattleUnit *attacker, struct BattleUnit *defender);
void Reforged_PairGaugeFill(struct BattleUnit *attacker, struct BattleUnit *defender);
#define RFO_DUALGUARD_ATTR | BATTLE_HIT_ATTR_17
#define RFO_PAIR_GAUGE_FILL(attacker, defender) Reforged_PairGaugeFill((attacker), (defender));
#else
#define RFO_DUALGUARD_ATTR
#define RFO_PAIR_GAUGE_FILL(attacker, defender)
#endif

/*
 * Class skills, the battle side (port/reforged/reforged_skills_battle.c; game fixes RFO_FIX_SKILLS and
 * RFO_FIX_ENEMY_SKILLS, port/reforged/reforged_skills.h). Same-line hooks in src/bmbattle.c; in every
 * other build each expands to nothing (RFO_SKILL_ARMSTHRIFT: to its weapon).
 *   RFO_SKILL_BATTLE_MODS()      BattleGenerate, after both ComputeBattleUnitStats: Wrath, Patience,
 *                                Underdog, Breakers, Charm, Hex, Tantivy, Anathema (Hit/Avoid/Crit)
 *   RFO_SKILL_NO_FOLLOWUP()      BattleGetFollowUpOrder: Wary Fighter, nobody doubles
 *   RFO_SKILL_ASTRA(a, n, attrs) BattleGenerateRoundHits: Astra, the round becomes 5 strikes (BATTLE_HIT_ATTR_5)
 *   RFO_SKILL_ASTRA_HALF()       BattleGenerateHitAttributes: an Astra strike does half damage
 *   RFO_SKILL_ASTRA_ANIM         src/banim-battleparse.c: an Astra strike plays the critical-hit animation
 *   RFO_ASTRA_HITS(hits)         ParseBattleHitToBanimCmd: the Astra strikes of one side become one animation
 *                                round with one HP drop (port/reforged/reforged_astra_anim.c); GBA: (hits)
 *   RFO_ASTRA_COPIES()           MainUpdateEkrBattle, after BattleAIS_ExecCommands: the skill banners
 *                                (port/reforged/reforged_skillbanner.c, Reforged_SkillBannerTick) and the faded copies of the
 *                                attacker during that merged round
 *   RFO_SKILL_NIHIL_OFF(bu)      BattleCheckSureShot / Pierce / GreatShield: the foe's Nihil, FE8's own skill off
 *   RFO_SKILL_NIHIL_SILENCER(bu) ComputeBattleUnitSilencerRate: the foe's Nihil, no Silencer
 *   RFO_SKILL_VANTAGE(pa, pd)    BattleUnwind: Vantage, the attacked unit strikes first
 *   RFO_VANTAGE_TA(attacker)     BattleGenerateHitTriangleAttack: the opening-hit test under Vantage
 *   RFO_SKILL_LUNA(a, defense)   BattleGenerateHitAttributes, after damage = attack - defense
 *   RFO_SKILL_PAVISE(a, d)       BattleGenerateHitAttributes, before the damage clamp: Pavise, Aegis
 *   RFO_SKILL_MIRACLE(d)         BattleGenerateHitEffects, before the defender loses HP
 *   RFO_SKILL_SOL(a, d)          BattleGenerateHitEffects, after the HP-drain block
 *   RFO_SKILL_ARMSTHRIFT(a, w)   BattleGenerateHitEffects, the weapon's use (inside its PC block)
 *   RFO_SKILL_LIFETAKER(a, d)    BattleGenerateHit, the target falls
 *   Reforged_SkillAfterUnwind()  BattleGenerate, after the unwind (a PC block): Poison Strike, Despoil
 *   RFO_SKILL_APPLY()            BattleApplyUnitUpdates: Despoil's gold
 *   RFO_SKILL_WEXP_MUL(bu)       GetBattleUnitUpdatedWeaponExp: Discipline, weapon EXP x2
 */
#if defined(PC_PORT) && PORT_PLAYED_BUILD
void Reforged_SkillBattleMods(void);
int Reforged_SkillWary(void);
void Reforged_SkillVantage(struct BattleUnit **attacker, struct BattleUnit **defender);
int Reforged_SkillVantageTAFlip(struct BattleUnit *attacker);
void Reforged_SkillLuna(struct BattleUnit *attacker, int defense);
void Reforged_SkillPavise(struct BattleUnit *attacker, struct BattleUnit *defender);
void Reforged_SkillMiracle(struct BattleUnit *defender);
void Reforged_SkillSol(struct BattleUnit *attacker, struct BattleUnit *defender);
u16 Reforged_SkillArmsthrift(struct BattleUnit *attacker, int after);
void Reforged_SkillLifetaker(struct BattleUnit *attacker, struct BattleUnit *defender);
void Reforged_SkillAfterUnwind(void);
void Reforged_SkillApply(void);
int Reforged_SkillWexpMul(struct BattleUnit *bu);
#define RFO_SKILL_BATTLE_MODS() Reforged_SkillBattleMods();
#define RFO_SKILL_NO_FOLLOWUP() if (Reforged_SkillWary()) return FALSE;
int Reforged_SkillAstra(struct BattleUnit *attacker);
void Reforged_SkillAstraHalf(void);
int Reforged_SkillNegated(const struct BattleUnit *bu);
#define RFO_SKILL_ASTRA(attacker, count, attrs) if (Reforged_SkillAstra(attacker)) { (count) = 5; (attrs) |= BATTLE_HIT_ATTR_5; }
#define RFO_SKILL_ASTRA_HALF() Reforged_SkillAstraHalf();
#define RFO_SKILL_ASTRA_ANIM | BATTLE_HIT_ATTR_5
struct BattleHit *Reforged_AstraMergeHits(struct BattleHit *hits);
void Reforged_AstraCopiesDraw(void);
#define RFO_ASTRA_HITS(hits) Reforged_AstraMergeHits(hits)
void Reforged_SkillBannerTick(void);
#define RFO_ASTRA_COPIES() Reforged_AstraCopiesDraw(); Reforged_SkillBannerTick();
#define RFO_SKILL_NIHIL_OFF(bu) if (Reforged_SkillNegated(bu)) return;
#define RFO_SKILL_NIHIL_SILENCER(bu) if (Reforged_SkillNegated(bu)) { (bu)->battleSilencerRate = 0; return; }
#define RFO_SKILL_VANTAGE(pa, pd) Reforged_SkillVantage((pa), (pd));
#define RFO_VANTAGE_TA(attacker) ^ Reforged_SkillVantageTAFlip(attacker)
#define RFO_SKILL_LUNA(attacker, defense) Reforged_SkillLuna((attacker), (defense));
#define RFO_SKILL_PAVISE(attacker, defender) Reforged_SkillPavise((attacker), (defender));
#define RFO_SKILL_MIRACLE(defender) Reforged_SkillMiracle(defender);
#define RFO_SKILL_SOL(attacker, defender) Reforged_SkillSol((attacker), (defender));
#define RFO_SKILL_ARMSTHRIFT(attacker, after) Reforged_SkillArmsthrift((attacker), (after))
#define RFO_SKILL_LIFETAKER(attacker, defender) Reforged_SkillLifetaker((attacker), (defender));
#define RFO_SKILL_APPLY() Reforged_SkillApply();
#define RFO_SKILL_WEXP_MUL(bu) * Reforged_SkillWexpMul(bu)
#else
#define RFO_SKILL_BATTLE_MODS()
#define RFO_SKILL_NO_FOLLOWUP()
#define RFO_SKILL_ASTRA(attacker, count, attrs)
#define RFO_SKILL_ASTRA_HALF()
#define RFO_SKILL_ASTRA_ANIM
#define RFO_ASTRA_HITS(hits) (hits)
#define RFO_ASTRA_COPIES()
#define RFO_SKILL_NIHIL_OFF(bu)
#define RFO_SKILL_NIHIL_SILENCER(bu)
#define RFO_SKILL_VANTAGE(pa, pd)
#define RFO_VANTAGE_TA(attacker)
#define RFO_SKILL_LUNA(attacker, defense)
#define RFO_SKILL_PAVISE(attacker, defender)
#define RFO_SKILL_MIRACLE(defender)
#define RFO_SKILL_SOL(attacker, defender)
#define RFO_SKILL_ARMSTHRIFT(attacker, after) (after)
#define RFO_SKILL_LIFETAKER(attacker, defender)
#define RFO_SKILL_APPLY()
#define RFO_SKILL_WEXP_MUL(bu)
#endif

#endif // GUARD_BMBATTLE_H
