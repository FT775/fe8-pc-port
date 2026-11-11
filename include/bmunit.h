#ifndef GUARD_BM_UNIT_H
#define GUARD_BM_UNIT_H

#include "global.h"

struct SupportData;
struct BattleAnimDef;

// Config
enum { UNIT_LEVEL_MAX = 20 };
enum { UNIT_ITEM_COUNT = 5 };
enum { UNIT_DEFINITION_ITEM_COUNT = 4 };

// Meaningful constants
enum { UNIT_EXP_DISABLED = 0xFF };

struct CharacterData
{
    /* 00 */ u16 nameTextId;
    /* 02 */ u16 descTextId;
    /* 04 */ u8 number;
    /* 05 */ u8 defaultClass;
    /* 06 */ u16 portraitId;
    /* 08 */ u8 miniPortrait;
    /* 09 */ u8 affinity;
    /* 0A */ u8 sort_order;

    /* 0B */ s8 baseLevel;
    /* 0C */ s8 baseHP;
    /* 0D */ s8 basePow;
    /* 0E */ s8 baseSkl;
    /* 0F */ s8 baseSpd;
    /* 10 */ s8 baseDef;
    /* 11 */ s8 baseRes;
    /* 12 */ s8 baseLck;
    /* 13 */ s8 baseCon;

    /* 14 */ u8 baseRanks[8];

    /* 1C */ u8 growthHP;
    /* 1D */ u8 growthPow;
    /* 1E */ u8 growthSkl;
    /* 1F */ u8 growthSpd;
    /* 20 */ u8 growthDef;
    /* 21 */ u8 growthRes;
    /* 22 */ u8 growthLck;

    /* 23 */ u8 _u23;
    /* 24 */ u8 _u24;
    /* 25 */ u8 _u25[2]; // Unique animation IDs in FE7
    /* 27 */ u8 _u27;

    /* 28 */ u32 attributes;

    /* 2C */ const struct SupportData* pSupportData;
    /* 30 */ u8 visit_group;

    /* 31 */ u8 _pad_[0x34 - 0x31];
};

struct ClassData
{
    /* 00 */ u16 nameTextId;
    /* 02 */ u16 descTextId;
    /* 04 */ u8 number;
    /* 05 */ u8 promotion;
    /* 06 */ u8 SMSId;
    /* 07 */ u8 slowWalking;
    /* 08 */ u16 defaultPortraitId;
    /* 0A */ u8 sort_order;

    /* 0B */ s8 baseHP;
    /* 0C */ s8 basePow;
    /* 0D */ s8 baseSkl;
    /* 0E */ s8 baseSpd;
    /* 0F */ s8 baseDef;
    /* 10 */ s8 baseRes;
    /* 11 */ s8 baseCon;
    /* 12 */ s8 baseMov;

    /* 13 */ s8 maxHP;
    /* 14 */ s8 maxPow;
    /* 15 */ s8 maxSkl;
    /* 16 */ s8 maxSpd;
    /* 17 */ s8 maxDef;
    /* 18 */ s8 maxRes;
    /* 19 */ s8 maxCon;

    /* 1A */ s8 classRelativePower;

    /* 1B */ s8 growthHP;
    /* 1C */ s8 growthPow;
    /* 1D */ s8 growthSkl;
    /* 1E */ s8 growthSpd;
    /* 1F */ s8 growthDef;
    /* 20 */ s8 growthRes;
    /* 21 */ s8 growthLck;

    /* 22 */ u8 promotionHp;
    /* 23 */ u8 promotionPow;
    /* 24 */ u8 promotionSkl;
    /* 25 */ u8 promotionSpd;
    /* 26 */ u8 promotionDef;
    /* 27 */ u8 promotionRes;

    /* 28 */ u32 attributes;

    /* 2C */ u8 baseRanks[8];

    /* 34 */ const struct BattleAnimDef * pBattleAnimDef;
    /* 38 */ const s8 * pMovCostTable[3]; // standard, rain, snow

    /* 44 */ const s8 * pTerrainAvoidLookup;
    /* 48 */ const s8 * pTerrainDefenseLookup;
    /* 4C */ const s8 * pTerrainResistanceLookup;

    /* 50 */ const void * _pU50;
};

enum {
    // ClassData::slowWalking
    UNIT_WALKSPEED_FAST,
    UNIT_WALKSPEED_SLOW,
};

struct Unit
{
    /* 00 */ const struct CharacterData* pCharacterData;
    /* 04 */ const struct ClassData* pClassData;

    /* 08 */ s8 level;
    /* 09 */ u8 exp;

    /* 0A */ u8 aiFlags;

    /* 0B */ s8 index;

    /* 0C */ u32 state;

    /* 10 */ s8 xPos;
    /* 11 */ s8 yPos;

    /* 12 */ s8 maxHP;
    /* 13 */ s8 curHP;
    /* 14 */ s8 pow;
    /* 15 */ s8 skl;
    /* 16 */ s8 spd;
    /* 17 */ s8 def;
    /* 18 */ s8 res;
    /* 19 */ s8 lck;

    /* 1A */ s8 conBonus;
    /* 1B */ u8 rescue;
    /* 1C */ u8 ballistaIndex;
    /* 1D */ s8 movBonus;

    /* 1E */ u16 items[UNIT_ITEM_COUNT];
    /* 28 */ u8 ranks[8];

    /* 30 */ u8 statusIndex : 4;
    /* 30 */ u8 statusDuration : 4;

    /* 31 */ u8 torchDuration : 4;
    /* 31 */ u8 barrierDuration : 4;

    /* 32 */ u8 supports[UNIT_SUPPORT_MAX_COUNT];
    /* 39 */ s8 supportBits;

    /* pad */
    /* 3A */ u8 _u3A;
    /* 3B */ u8 _u3B;

    /* 3C */ struct SMSHandle * pMapSpriteHandle;

    /* 40 */ u16 ai_config; // a bitmask
    /* 42 */ u8 ai1;        // enum to gAi1ScriptTable
    /* 43 */ u8 ai_a_pc;
    /* 44 */ u8 ai2;        // enum to gAi2ScriptTable
    /* 45 */ u8 ai_b_pc;
    /* 46 */ u8 ai_counter;

    /* pad */
    /* 47 */ u8 _u47;
};

enum udef_ai_index {
    UDEF_AIIDX_AI_A,
    UDEF_AIIDX_AI_B,
    UDEF_AIIDX_AI_CONF_L,
    UDEF_AIIDX_AI_CONF_H,

    UDEF_AIIDX_MAX
};

struct UnitDefinition
{
    /* 00 */ u8  charIndex;
    /* 01 */ u8  classIndex;
    /* 02 */ u8  leaderCharIndex;

    /* 03 */ u8  autolevel  : 1;
    /* 03 */ u8  allegiance : 2;
    /* 03 */ u8  level      : 5;

    /* 04 */ u16 xPosition  : 6; /* 04:0 to 04:5 */
    /* 04 */ u16 yPosition  : 6; /* 04:6 to 05:3 */
    /* 05 */ u16 genMonster : 1; /* 05:4 */
    /* 05 */ u16 itemDrop   : 1; /* 05:5 */
    /* 05 */ u16 sumFlag    : 1; /* 05:6 */
    /* 05 */ u16 unk_05_7   : 1; /* 05:7 */
    /* 05 */ u16 extraData  : 8;
    /* 07 */ u16 redaCount  : 8;

    /* 08 */ const void * redas;

    /* 0C */ u8 items[UNIT_DEFINITION_ITEM_COUNT];

    /* 10 */ u8 ai[UDEF_AIIDX_MAX];
} BITPACKED;

enum
{
    // Unit state constant masks

    US_NONE         = 0,

    US_HIDDEN       = (1 << 0),
    US_UNSELECTABLE = (1 << 1),
    US_DEAD         = (1 << 2),
    US_NOT_DEPLOYED = (1 << 3),
    US_RESCUING     = (1 << 4),
    US_RESCUED      = (1 << 5),
    US_HAS_MOVED    = (1 << 6), // Bad name?
    US_CANTOING     = US_HAS_MOVED, // Alias
    US_UNDER_A_ROOF = (1 << 7),
    US_BIT8 = (1 << 8), // has been seen?
    US_BIT9 = (1 << 9), // hidden by fog?
    US_HAS_MOVED_AI = (1 << 10),
    US_IN_BALLISTA  = (1 << 11),
    US_DROP_ITEM    = (1 << 12),
    US_GROWTH_BOOST = (1 << 13),
    US_SOLOANIM_1   = (1 << 14),
    US_SOLOANIM_2   = (1 << 15),
    US_BIT16        = (1 << 16),
    US_BIT17        = (1 << 17),
    US_BIT18        = (1 << 18),
    US_BIT19        = (1 << 19),
    US_BIT20        = (1 << 20),
    US_BIT21        = (1 << 21),
    US_BIT22        = (1 << 22),
    US_BIT23        = (1 << 23),
    // = (1 << 24),
    US_BIT25 = (1 << 25),
    US_BIT26 = (1 << 26),
    US_BIT27 = (1 << 27),
    // = (1 << 28),
    // = (1 << 29),
    // = (1 << 30),
    // = (1 << 31),

    // Helpers
    US_UNAVAILABLE = (US_DEAD | US_NOT_DEPLOYED | US_BIT16),
    US_SOLOANIM = (US_SOLOANIM_1 | US_SOLOANIM_2),
};

enum
{
    // Unit status identifiers

    UNIT_STATUS_NONE = 0,

    UNIT_STATUS_POISON = 1,
    UNIT_STATUS_SLEEP = 2,
    UNIT_STATUS_SILENCED = 3,
    UNIT_STATUS_BERSERK = 4,

    UNIT_STATUS_ATTACK = 5,
    UNIT_STATUS_DEFENSE = 6,
    UNIT_STATUS_CRIT = 7,
    UNIT_STATUS_AVOID = 8,

    UNIT_STATUS_SICK = 9,
    UNIT_STATUS_RECOVER = 10,

    UNIT_STATUS_PETRIFY = 11,
    UNIT_STATUS_12 = 12,
    UNIT_STATUS_13 = 13,
};

enum {
    FACTION_BLUE   = 0x00, // player units
    FACTION_GREEN  = 0x40, // ally npc units
    FACTION_RED    = 0x80, // enemy units
    FACTION_PURPLE = 0xC0, // link arena 4th team
};

enum
{
    FACTION_ID_BLUE   = 0,
    FACTION_ID_GREEN  = 1,
    FACTION_ID_RED    = 2,
    FACTION_ID_PURPLE = 3,
};

enum
{
    // Character/Class attributes

    CA_NONE = 0,

    CA_MOUNTEDAID = (1 << 0),
    CA_CANTO = (1 << 1),
    CA_STEAL = (1 << 2),
    CA_THIEF = (1 << 3),
    CA_DANCE = (1 << 4),
    CA_PLAY = (1 << 5),
    CA_CRITBONUS = (1 << 6),
    CA_BALLISTAE = (1 << 7),
    CA_PROMOTED = (1 << 8),
    CA_SUPPLY = (1 << 9),
    CA_MOUNTED = (1 << 10),
    CA_WYVERN = (1 << 11),
    CA_PEGASUS = (1 << 12),
    CA_LORD = (1 << 13),
    CA_FEMALE = (1 << 14),
    CA_BOSS = (1 << 15),
    CA_LOCK_1 = (1 << 16),
    CA_LOCK_2 = (1 << 17),
    CA_LOCK_3 = (1 << 18), // Dragons or Monster depending of game
    CA_MAXLEVEL10 = (1 << 19),
    CA_UNSELECTABLE = (1 << 20),
    CA_TRIANGLEATTACK_PEGASI = (1 << 21),
    CA_TRIANGLEATTACK_ARMORS = (1 << 22),
    CA_BIT_23 = (1 << 23),
    CA_NEGATE_LETHALITY = (1 << 24),
    CA_ASSASSIN = (1 << 25),
    CA_MAGICSEAL = (1 << 26),
    CA_SUMMON = (1 << 27),
    CA_LOCK_4 = (1 << 28),
    CA_LOCK_5 = (1 << 29),
    CA_LOCK_6 = (1 << 30),
    CA_LOCK_7 = (1 << 31),

    // Helpers
    CA_REFRESHER = CA_DANCE | CA_PLAY,
    CA_FLYER = CA_WYVERN | CA_PEGASUS,
    CA_TRIANGLEATTACK_ANY = CA_TRIANGLEATTACK_ARMORS | CA_TRIANGLEATTACK_PEGASI,
};

enum
{
    // To check result of GetUnit[Item]UseBits

    UNIT_USEBIT_WEAPON = (1 << 0),
    UNIT_USEBIT_STAFF  = (1 << 1),
};

enum unit_affinity_index {
    UNIT_AFFIN_FIRE = 1,
    UNIT_AFFIN_THUNDER,
    UNIT_AFFIN_WIND,
    UNIT_AFFIN_ICE,
    UNIT_AFFIN_DARK,
    UNIT_AFFIN_LIGHT,
    UNIT_AFFIN_ANIMA,
};

// TODO: MOVE ELSEWHERE
extern CONST_DATA struct ClassData gClassData[]; // gClassData
extern CONST_DATA struct CharacterData gCharacterData[]; // gCharacterData
extern struct UnitDefinition gUnitDef1;
extern struct UnitDefinition gUnitDef2;
extern struct UnitDefinition gUnitDefEggHatching;
extern struct UnitDefinition gUnitDefSumDK[];
extern struct Unit gUnitArrayBlue[62];
extern struct Unit gUnitArrayRed[50];
extern struct Unit gUnitArrayGreen[20];
extern CONST_DATA struct Unit *gUnitLookup[0x100];

void InitUnits(void);
void ClearUnit(struct Unit* unit);
void CopyUnit(struct Unit* from, struct Unit* to);
struct Unit* GetFreeUnit(int faction);
struct Unit* GetFreeBlueUnit(const struct UnitDefinition* uDef);
int GetUnitFogViewRange(struct Unit* unit);
void SetUnitStatus(struct Unit* unit, int statusId);
void SetUnitStatusExt(struct Unit* unit, int status, int duration);
int GetUnitSMSId(struct Unit* unit);
bool UnitAddItem(struct Unit* unit, int item);
void UnitClearInventory(struct Unit* unit);
void UnitRemoveInvalidItems(struct Unit* unit);
int GetUnitItemCount(struct Unit* unit);
bool UnitHasItem(struct Unit* unit, int item);
int LoadUnits(const struct UnitDefinition* uDef);
void UnitHalvePowDefRes(struct Unit* unit);
bool CanClassWieldWeaponType(u8 classId, u8 wpnType);
struct Unit* LoadUnit(const struct UnitDefinition* uDef);
void UnitInitFromDefinition(struct Unit* unit, const struct UnitDefinition* uDef);
void UnitLoadItemsFromDefinition(struct Unit* unit, const struct UnitDefinition* uDef);
void UnitLoadStatsFromChracter(struct Unit* unit, const struct CharacterData* character);
void FixROMUnitStructPtr(struct Unit* unit);
void UnitLoadSupports(struct Unit* unit);
void UnitAutolevelWExp(struct Unit* unit, const struct UnitDefinition* uDef);
void UnitAutolevelCore(struct Unit* unit, u8 classId, int levelCount);
void UnitAutolevelPenalty(struct Unit* unit, u8 classId, int levelCount);
void UnitApplyBonusLevels(struct Unit* unit, int levelCount);
void UnitAutolevel(struct Unit* unit);
void UnitAutolevelRealistic(struct Unit* unit);
void UnitCheckStatCaps(struct Unit* unit);
struct Unit* GetUnitFromCharId(int charId);
struct Unit* GetUnitFromCharIdAndFaction(int charId, int faction);
bool CanUnitRescue(struct Unit* actor, struct Unit* target);
void UnitRescue(struct Unit* actor, struct Unit* target);
void UnitDrop(struct Unit* actor, int xTarget, int yTarget);
s8 UnitGive(struct Unit* actor, struct Unit* target);
void UnitKill(struct Unit* unit);
void UnitChangeFaction(struct Unit* unit, int faction);
void UnitFinalizeMovement(struct Unit* unit);
void UnitGetDeathDropLocation(struct Unit* unit, int* xOut, int* yOut);
void UnitBeginAction(struct Unit* unit);
void UnitBeginCantoAction(struct Unit* unit);
void MoveActiveUnit(int x, int y);
void ClearActiveFactionGrayedStates(void);
void TickActiveFactionTurn(void);
void SetAllUnitNotBackSprite(void); // TODO: better name
void UnitUpdateUsedItem(struct Unit* unit, int itemSlot);
int GetUnitAid(struct Unit* unit);
int GetUnitMagBy2Range(struct Unit* unit);
bool UnitHasMagicRank(struct Unit* unit);
void DeployUnitAtPosition(struct Unit* unit, int x, int y);
int GetUnitKeyItemSlotForTerrain(struct Unit* unit, int terrain);
int GetUnitAidIconId(u32 attributes);
int GetUnitWeaponUsabilityBits(struct Unit* unit);
int GetCombinedEnemyWeaponUsabilityBits(void);
bool CanUnitMove(void);
bool IsPositionMagicSealed(int x, int y);
bool IsUnitMagicSealed(struct Unit* unit);
int GetUnitLastItem(struct Unit* unit);
const s8* GetUnitMovementCost(struct Unit* unit);
int GetClassSMSId(int classId);
void UpdatePrevDeployStates(void);
void LoadUnitPrepScreenPositions(void);
void ClearTemporaryUnits(void);
bool IsUnitSlotAvailable(int faction);
void HideBlueUnits(void);
void ClearRedAndGreenUnits(void);
u16 CountAvailableBlueUnits(void);
u16 CountRedUnits(void);
u16 CountGreenUnits(void);
void ClearCutsceneUnits(void);
void RefreshAllies(void);
int GetUnitCurrentHp(struct Unit* unit);
int GetUnitMaxHp(struct Unit* unit);
int GetUnitPower(struct Unit* unit);
int GetUnitSkill(struct Unit* unit);
int GetUnitSpeed(struct Unit* unit);
int GetUnitDefense(struct Unit* unit);
int GetUnitResistance(struct Unit* unit);
int GetUnitLuck(struct Unit* unit);
int GetUnitPortraitId(struct Unit* unit);
int GetUnitMiniPortraitId(struct Unit* unit);
int GetUnitLeaderCharId(struct Unit* unit);
void SetUnitLeaderCharId(struct Unit* unit, int charId);
void SetUnitHp(struct Unit* unit, int value);
void AddUnitHp(struct Unit* unit, int amount);
char* GetUnitRescueName(struct Unit* unit);
char* GetUnitStatusName(struct Unit* unit);
struct Unit* GetUnit(int id);
const struct ClassData* GetClassData(int classId);
const struct CharacterData* GetCharacterData(int charId);
void UnitRemoveItem(struct Unit* unit, int slot);

#define UNIT_IS_VALID(aUnit) ((aUnit) && (aUnit)->pCharacterData)
#define UNIT_CHAR_ID(aUnit) ((aUnit)->pCharacterData->number)
#define UNIT_CLASS_ID(aUnit) ((aUnit)->pClassData->number)
#define UNIT_FACTION(aUnit) ((aUnit)->index & 0xC0)

#define UNIT_CATTRIBUTES(aUnit) ((aUnit)->pCharacterData->attributes | (aUnit)->pClassData->attributes)

#define UNIT_NAME_ID(aUnit) ((aUnit)->pCharacterData->nameTextId)

#define UNIT_MHP_MAX(aUnit) (UNIT_FACTION(unit) == FACTION_RED ? 120 : 60)
#define UNIT_POW_MAX(aUnit) ((aUnit)->pClassData->maxPow)
#define UNIT_SKL_MAX(aUnit) ((aUnit)->pClassData->maxSkl)
#define UNIT_SPD_MAX(aUnit) ((aUnit)->pClassData->maxSpd)
#define UNIT_DEF_MAX(aUnit) ((aUnit)->pClassData->maxDef)
#define UNIT_RES_MAX(aUnit) ((aUnit)->pClassData->maxRes)
#define UNIT_LCK_MAX(aUnit) (30)
#define UNIT_CON_MAX(aUnit) ((aUnit)->pClassData->maxCon)
#define UNIT_MOV_MAX(aUnit) (15)

#define UNIT_CON_BASE(aUnit) ((aUnit)->pClassData->baseCon + (aUnit)->pCharacterData->baseCon)
#define UNIT_MOV_BASE(aUnit) ((aUnit)->pClassData->baseMov)

#define UNIT_CON(aUnit) (UNIT_CON_BASE(aUnit) + (aUnit)->conBonus)
#define UNIT_MOV(aUnit) ((aUnit)->movBonus + UNIT_MOV_BASE(aUnit) RFO_SKILL_MOV(aUnit))

#define UNIT_IS_GORGON_EGG(aUnit) (((aUnit)->pClassData->number == CLASS_GORGONEGG) || ((aUnit)->pClassData->number == CLASS_GORGONEGG2))
#define UNIT_IS_PHANTOM(aUnit) ((aUnit)->pClassData->number == CLASS_PHANTOM)

#define UNIT_ARENA_LEVEL(aUnit) (((aUnit)->state >> 17) & 0x7)

#define UNIT_SUPPORT_DATA(aUnit) ((aUnit)->pCharacterData->pSupportData)

// NOTE: if this ends up being only used in [Get|Set]UnitLeaderCharId, having this as a macro may end up being unnecessary
#define UNIT_LEADER_CHARACTER(aUnit) ((aUnit)->supports[UNIT_SUPPORT_MAX_COUNT-1])

/*
 * PC played builds only (PORT_PLAYED_BUILD): the Reforged Options randomizer
 * (port/reforged/reforged_rando*.c, port/include/port_reforged.h). Each hook sits
 * on an existing line of the game code (appended, or around a value), so the
 * test and GBA builds keep their line counts and stay byte for byte the same:
 * there every macro below is empty, or the value itself. With the randomizer
 * off for the run being played, every function returns its input unchanged.
 *   RANDO_HOOK_UNITDEF       src/bmunit.c LoadUnit, after the genMonster block:
 *                            may point uDef at buf (a copy with another class
 *                            or items); uDef may already be &buf.
 *   RANDO_HOOK_UNITLOADED    src/bmunit.c LoadUnit, before UnitCheckStatCaps:
 *                            inventory and rank fixes, enemy buffs, drops.
 *   RANDO_HOOK_WILLREAD      src/bmsave.c ReadGameSave / ReadSuspendSave, before
 *                            InitUnits: the tables of that save block's run.
 *   RANDO_HOOK_CHEST         src/eventinfo.c, a chest with a set item.
 *   RANDO_GIFT_ITEM          src/eventscr.c Event37_GiveItem (GIVEITEMTO).
 *   RANDO_PROMOTE_JID        src/eventscr.c Event40_PromoteUnit (PROM): the class,
 *                            resolved before the command starts its proc.
 *   RANDO_PROMOTE_SKIP       there, before the proc: a jid of 0 (never a PROM
 *                            class in vanilla) ends the command with no promotion
 *                            (a randomized unit that is already promoted).
 *   RFO_PROMOTE_MENU         there, after RANDO_PROMOTE_SKIP: the FE7 classes
 *                            (port/reforged/reforged_fe7.c, v1.16.1): Chapter 16's
 *                            PROM of a twin still in their Lord class, with their
 *                            FE7 lord loaded, opens the brace's class choice
 *                            instead (the command yields until it ends).
 *   RFO_PROMO_NO_CANCEL      src/classchg-menuselect.c ClassChgMenuSelOnPressB: B
 *                            does nothing on that choice (as for a trainee).
 *   RFO_PROMO_SLOT           src/classchg-sel.c Make6C_PromotionMenuSelect,
 *                            src/classchg-menuselect.c ClassChgMenuItem_OnSelect: the
 *                            promotion screen's choice 0-2, in a randomizer version
 *                            11-14 run the run's three (reforged_rando_classes.c
 *                            Rando_PromoList), or (v1.23.0) in a game without the
 *                            randomizer with the Game fix "Third promotion" the
 *                            owner's three (Promo3ListEx), else the class given.
 *   RFO_PROMO_THREE          src/classchg-handler.c PromoHandler_SetupAndStartUI: in
 *                            such a game every promotion opens the choice screen
 *                            (sel_en), a row with one class too.
 *   RANDO_SKIRMISH_LEADER_CLASS src/eventinfo.c GetChapterSkirmishLeaderClasses:
 *                            the world map's skirmish icons show the leader's
 *                            class of the run.
 *   RANDO_STAT_GAIN          src/bmunit.c UnitAutolevelCore: a stat increase that
 *                            would wrap the s8 stat past 127 stops at 127
 *                            (UnitCheckStatCaps then lowers it to the cap); this
 *                            one applies in every played run, and only changes
 *                            what would have wrapped.
 *   RANDO_HOOK_EVTBATTLE_ITEM src/eventscr3.c StartEventBattle: a forced weapon
 *                            the unit cannot use is dropped (0).
 *   RANDO_HOOK_PROMO_ITEM    src/bmitemuse.c CanUnitUsePromotionItem.
 *   RANDO_HOOK_SHOP          src/bmshop.c StartShopScreen: the list shown.
 *   RANDO_HOOK_MOVESCRIPT    src/bmidoten.c GenerateBestMovementScript, the
 *                            walk's loop test (before the map read): in a
 *                            randomized run a walk that leaves the map or
 *                            fills the move script (a REDA target the class
 *                            cannot reach) stops and its script is dropped.
 *   RANDO_CHARA_PAL          src/banim-ekrmain.c, src/banim-ekrmainmini.c: a
 *                            character battle palette by its index (orig: the
 *                            game's own read of it). Indices 0x80 and up are the
 *                            randomizer's palettes (a character's colours on a
 *                            new class), which gAnimCharaPalIt then points to.
 *   RANDO_CHARA_ENSURE       src/banim-ekrbattleintro.c, src/banim-ekrmain.c (a
 *                            triangle attack's partners), src/classchg-sel.c: before
 *                            a unit's set for its class is looked up, randomizer
 *                            version 15 ("Keep colours" on every class) puts the
 *                            set it shows in the unit's row (built when first
 *                            needed); nothing in any other game.
 *   RANDO_HOOK_TEXT          src/msg.c GetStringFromIndex, after the decode: with
 *                            "Recruitment", each slot's name becomes its fill's.
 *   RANDO_FACE               src/face.c GetPortraitData: a slot's portrait shows
 *                            its fill's.
 *   RANDO_HOOK_DEATH         src/bmmind.c KillUnitOnArenaDeathMaybe: "Nuzlocke"
 *                            records the death (the other deaths: casual mode's
 *                            hooks call it).
 *   RANDO_HOOK_JOINED        src/bmunit.c UnitChangeFaction: "Nuzlocke", a unit
 *                            in the run's graveyard that turns blue dies again.
 */
#if defined(PC_PORT) && PORT_PLAYED_BUILD
const struct UnitDefinition *Reforged_RandoUnitDef(const struct UnitDefinition *uDef, struct UnitDefinition *buf);
void Reforged_RandoUnitLoaded(struct Unit *unit, const struct UnitDefinition *uDef);
void Reforged_RandoWillRead(int block);
int Reforged_RandoChestItem(int item, int x, int y);
int Reforged_RandoGiftItem(int item, struct Unit *target);
int Reforged_RandoPromoteJid(struct Unit *unit, int jid); /* 0: no promotion */
int Reforged_RandoSkirmishLeaderClass(int chapterId, const struct UnitDefinition *uDef, int jid);
int Reforged_RandoStatGain(int stat, int gain);
int Reforged_RandoEventBattleItem(struct Unit *unit, int item);
int Reforged_RandoPromoItem(struct Unit *unit, int item); /* 0 or 1, or -1: the game's own check */
const u16 *Reforged_RandoShopItems(const u16 *inventory, int shopType);
int Reforged_RandoMoveGuard(int x, int y, u8 **output, u8 *outputStart); /* 0: stop the walk */
char *Reforged_RandoCharaPal(int idx);
#define RANDO_CHARA_PAL(orig, idx) Reforged_RandoCharaPal(idx)
int Reforged_RandoCharaPalFits(int idx, int bid, int jid);
#define RANDO_UNIQUE_PAL_FITS(pal, bid, jid) if (!Reforged_RandoCharaPalFits((pal), (bid), (jid))) (pal) = -1;
void Reforged_RandoCharaEnsure(int valid, int pid0, int jid, int bid, const void *unit);
#define RANDO_CHARA_ENSURE(valid, pid0, jid, bid, unit) Reforged_RandoCharaEnsure((valid), (pid0), (jid), (bid), (unit));
void Reforged_RandoText(char *buf, int size);
int Reforged_RandoFace(int fid);
#define RANDO_HOOK_TEXT(buf, size) Reforged_RandoText((char *)(buf), (size));
int Reforged_RandoMapSong(int song);
int Reforged_RandoBattleSong(int song);
#define RANDO_MAP_SONG(song) Reforged_RandoMapSong(song)
int Reforged_RandoMapSongPeek(int song);
#define RANDO_MAP_SONG_PEEK(song) Reforged_RandoMapSongPeek(song)
#define RANDO_BATTLE_SONG(song) Reforged_RandoBattleSong(song)
#define RANDO_FACE(fid) Reforged_RandoFace(fid)
void Reforged_RandoUnitDied(struct Unit *unit);
#define RANDO_HOOK_DEATH(unit) Reforged_RandoUnitDied(unit);
void Reforged_RandoUnitJoined(struct Unit *unit);
#define RANDO_HOOK_JOINED(unit) Reforged_RandoUnitJoined(unit);
void Reforged_RandoPromoted(struct Unit *unit);
void Reforged_RandoPromoting(struct Unit *unit);
#define RANDO_HOOK_PROMOTING(unit) Reforged_RandoPromoting(unit);
int Reforged_RandoPromoChoice(struct Unit *unit, int jid);
int Reforged_BanimFallback(const void *animDef, int idx, u32 *out);
#define RANDO_BANIM_FALLBACK(animDef, idx, out) Reforged_BanimFallback((animDef), (s16)(idx), (out))
#define RANDO_PROMO_CHOICE(unit, jid) Reforged_RandoPromoChoice((unit), (jid))
int Reforged_PromoSlot(struct Unit *unit, int slot, int jid);
int Reforged_PromoThree(struct Unit *unit);
#define RFO_PROMO_SLOT(unit, slot, jid) Reforged_PromoSlot((unit), (slot), (jid))
#define RFO_PROMO_THREE(proc, unit) if (Reforged_PromoThree(unit)) (proc)->sel_en = 1;
#define RANDO_HOOK_PROMOTED(unit) Reforged_RandoPromoted(unit);
#define RANDO_HOOK_UNITDEF(uDef, buf) (uDef) = Reforged_RandoUnitDef((uDef), (buf));
#define RANDO_HOOK_UNITLOADED(unit, uDef) Reforged_RandoUnitLoaded((unit), (uDef));
#define RANDO_HOOK_WILLREAD(block) Reforged_RandoWillRead(block);
#define RANDO_HOOK_CHEST(info) (info).givenItem = Reforged_RandoChestItem((info).givenItem, (info).xPos, (info).yPos);
#define RANDO_GIFT_ITEM(item, target) Reforged_RandoGiftItem((item), (target))
#define RANDO_PROMOTE_JID(unit, jid) Reforged_RandoPromoteJid((unit), (jid))
#define RANDO_PROMOTE_SKIP(jid) if ((jid) == 0) return EVC_ADVANCE_CONTINUE;
int Reforged_Fe7EventPromoMenu(void *evproc, struct Unit *unit, int jid, int item); /* 1: the menu runs */
int Reforged_Fe7PromoNoCancel(void *handler);
#define RFO_PROMOTE_MENU(evproc, pid, jid, item) if (Reforged_Fe7EventPromoMenu((evproc), GetUnitStructFromEventParameter(pid), (jid), (item))) return EVC_ADVANCE_YIELD;
#define RFO_PROMO_NO_CANCEL(handler) || Reforged_Fe7PromoNoCancel(handler)
#define RANDO_SKIRMISH_LEADER_CLASS(chapterId, uDef, jid) Reforged_RandoSkirmishLeaderClass((chapterId), (const struct UnitDefinition *)(uDef), (jid))
#define RANDO_STAT_GAIN(stat, gain) Reforged_RandoStatGain((stat), (gain))
#define RANDO_HOOK_EVTBATTLE_ITEM(unit, item) (item) =Reforged_RandoEventBattleItem((unit), (item));
#define RANDO_HOOK_PROMO_ITEM(unit, item) { int rr_ = Reforged_RandoPromoItem((unit), (item)); if (rr_ >= 0) return rr_; }
#define RANDO_HOOK_SHOP(inventory, shopType) (inventory) = Reforged_RandoShopItems((inventory), (shopType));
#define RANDO_HOOK_MOVESCRIPT(x, y, output, outputStart) Reforged_RandoMoveGuard((x), (y), &(output), (outputStart)) &&
#else
#define RANDO_CHARA_PAL(orig, idx) (orig)
#define RANDO_UNIQUE_PAL_FITS(pal, bid, jid)
#define RANDO_CHARA_ENSURE(valid, pid0, jid, bid, unit)
#define RANDO_HOOK_TEXT(buf, size)
#define RANDO_MAP_SONG(song) (song)
#define RANDO_MAP_SONG_PEEK(song) (song)
#define RANDO_BATTLE_SONG(song) (song)
#define RANDO_FACE(fid) (fid)
#define RANDO_HOOK_DEATH(unit)
#define RANDO_HOOK_JOINED(unit)
#define RANDO_HOOK_PROMOTED(unit)
#define RANDO_HOOK_PROMOTING(unit)
#define RANDO_PROMO_CHOICE(unit, jid) (jid)
#define RFO_PROMO_SLOT(unit, slot, jid) (jid)
#define RFO_PROMO_THREE(proc, unit)
#define RANDO_BANIM_FALLBACK(animDef, idx, out) (idx)
#define RANDO_HOOK_UNITDEF(uDef, buf)
#define RANDO_HOOK_UNITLOADED(unit, uDef)
#define RANDO_HOOK_WILLREAD(block)
#define RANDO_HOOK_CHEST(info)
#define RANDO_GIFT_ITEM(item, target) (item)
#define RANDO_PROMOTE_JID(unit, jid) (jid)
#define RANDO_PROMOTE_SKIP(jid)
#define RFO_PROMOTE_MENU(evproc, pid, jid, item)
#define RFO_PROMO_NO_CANCEL(handler)
#define RANDO_SKIRMISH_LEADER_CLASS(chapterId, uDef, jid) (jid)
#define RANDO_STAT_GAIN(stat, gain) (gain)
#define RANDO_HOOK_EVTBATTLE_ITEM(unit, item)
#define RANDO_HOOK_PROMO_ITEM(unit, item)
#define RANDO_HOOK_SHOP(inventory, shopType)
#define RANDO_HOOK_MOVESCRIPT(x, y, output, outputStart)
#endif

/*
 * PC played builds only: Reforged Options' "Game fixes" (port/reforged/reforged_gamefixes.c), each OFF by
 * default (then the original's code runs). In every other build the first is the original statement and the
 * second is empty, on the same line.
 *   RFO_PROMO_AUTOLEVEL      src/bmunit.c UnitAutolevel, the promoted unit's extra levels: rolled with the
 *                            base class's growths (pClassData is that class for the one call, then put back).
 *                            UnitAutolevelCore never reads its classId, so `call` running after the swap
 *                            (and reading the base class's .promotion there) changes nothing else.
 *   RFO_JOIN_RANKS(unit)     src/bmunit.c LoadUnit and UnitChangeFaction: "Late joiners' ranks" (a floor, so a
 *                            second call changes nothing)
 *   RFO_STAFF_DROP_ORDER     src/bmunit.c LoadUnit, after RANDO_HOOK_UNITLOADED: Chapter 15's dropping
 *                            Troubadour carries Mend first, so its status staff is the drop.
 */
#if defined(PC_PORT) && PORT_PLAYED_BUILD
const struct ClassData *Reforged_FixPromoAutolevelClass(struct Unit *unit);
void Reforged_FixStaffDropOrder(struct Unit *unit);
#define RFO_PROMO_AUTOLEVEL(unit, call) do { const struct ClassData *rfoJob_ = (unit)->pClassData; \
    (unit)->pClassData = Reforged_FixPromoAutolevelClass(unit); call; (unit)->pClassData = rfoJob_; } while (0)
#define RFO_STAFF_DROP_ORDER(unit) Reforged_FixStaffDropOrder(unit);
void Reforged_FixJoinRanks(struct Unit *unit);
#define RFO_JOIN_RANKS(unit) Reforged_FixJoinRanks(unit);
#else
#define RFO_PROMO_AUTOLEVEL(unit, call) call
#define RFO_STAFF_DROP_ORDER(unit)
#define RFO_JOIN_RANKS(unit)
#endif

/*
 * Pair Up (port/reforged/reforged_pairup.c; a game fix, RFO_FIX_PAIR_UP). Same-line hooks: each
 * expands to the original code (or nothing) on the GBA and in builds that are not played.
 *   RFO_RESCUE_HALVES(unit)        src/bmunit.c GetUnitSkill/Speed, src/statscreen.c DisplayPage0 and
 *                                  the blinking arrows: a pair's lead keeps its full Skill and Speed.
 *   RFO_PAIR_BONUS(unit, stat)     src/bmunit.c, the six stat getters: + the partner's boost.
 *   RFO_PAIR_ANY_RESCUE(a, t)      src/bmunit.c CanUnitRescue: no Aid >= Con between player units.
 *   RFO_PAIR_RESET(unit)           src/bmunit.c UnitRescue: a new pair's gauge starts empty.
 */
#if defined(PC_PORT) && PORT_PLAYED_BUILD
int Reforged_RescueHalves(const struct Unit *unit);
int Reforged_PairBonus(const struct Unit *unit, int stat);
int Reforged_PairAnyRescue(const struct Unit *actor, const struct Unit *target);
void Reforged_PairReset(struct Unit *unit);
#define RFO_RESCUE_HALVES(unit) Reforged_RescueHalves(unit)
#define RFO_IF_RESCUE_HALVES(unit) if (Reforged_RescueHalves(unit))
/* src/statscreen.c DisplayPage0, the Cond line with no status: a pair's lead shows its Dual Guard
   gauge ("Guard" and the percentage where a status shows its turns). */
const char *Reforged_PairStatusName(const struct Unit *unit, const char *name);
int Reforged_PairStatusX(const struct Unit *unit, int x);
void Reforged_PairStatusGauge(const struct Unit *unit);
#define RFO_PAIR_STATUS_NAME(unit, name) Reforged_PairStatusName((unit), (name))
#define RFO_PAIR_STATUS_X(unit, x) Reforged_PairStatusX((unit), (x))
#define RFO_PAIR_STATUS_GAUGE(unit) Reforged_PairStatusGauge(unit);
#define RFO_PAIR_BONUS(unit, stat) + Reforged_PairBonus((unit), (stat))
#define RFO_PAIR_ANY_RESCUE(actor, target) || Reforged_PairAnyRescue((actor), (target))
#define RFO_PAIR_RESET(unit) Reforged_PairReset(unit);
#else
#define RFO_RESCUE_HALVES(unit) ((unit)->state & US_RESCUING)
#define RFO_IF_RESCUE_HALVES(unit)
#define RFO_PAIR_STATUS_NAME(unit, name) name
#define RFO_PAIR_STATUS_X(unit, x) x
#define RFO_PAIR_STATUS_GAUGE(unit)
#define RFO_PAIR_BONUS(unit, stat)
#define RFO_PAIR_ANY_RESCUE(actor, target)
#define RFO_PAIR_RESET(unit)
#endif

/*
 * Class skills, the unit side (port/reforged/reforged_skills_unit.c; game fixes RFO_FIX_SKILLS and
 * RFO_FIX_ENEMY_SKILLS, port/reforged/reforged_skills.h). Same-line hooks: each expands to
 * nothing on the GBA and in builds that are not played. The stat/HP/Mov ones add nothing on the
 * battle's unit copies (InitBattleUnit copies the getters' totals into them).
 *   RFO_SKILL_PASS(uid)            src/bmidoten.c GenerateMovementMap: Pass, no unit blocks the flood
 *   RFO_SKILL_PASS_ADJ(unit)       src/bmunit.c CanUnitMove: an enemy square next to a Pass unit is open
 *   RFO_SKILL_PASS_BUMP(unit, pit) src/bmidoten.c UnitApplyWorkingMovementScript: a Pass unit walks past a
 *                                  hidden (fog) enemy; one on its last square still stops it, on a free one
 *   RFO_SKILL_STAT(unit, stat)     src/bmunit.c, the six stat getters (Skl/Spd: both branches, a
 *                                  rescuer's halved stat too): + 2 for Str/Mag/Skl/Spd/Lck/Def/Res +2.
 *                                  stat: 0 Pow, 1 Skl, 2 Spd, 3 Lck, 4 Def, 5 Res (RFO_PAIR_BONUS's).
 *   RFO_SKILL_HP(unit)             src/bmunit.c GetUnitMaxHp: + 5 for HP +5 (every HP clamp uses it).
 *   RFO_SKILL_MOV(unit)            UNIT_MOV above and src/bmpatharrowdisp.c PathArrowDisp_Init (which
 *                                  open-codes it): + 1 for Celerity, so range, path and AI agree.
 *   RFO_SKILL_RENEWAL(unit)        src/bmtarget.c MakeTerrainHealTargetList, added to the terrain's
 *                                  heal percentage in the test and the amount: + 20 for Renewal.
 *   RFO_SKILL_LTS_HEAL()           src/bmusemind.c ExecStandardHeal: Live to Serve heals the healer.
 *   RFO_SKILL_LTS_FORTIFY_BEGIN()  src/bmusemind.c ExecFortify, before and after the heal loop: the
 *   RFO_SKILL_LTS_FORTIFY_END()    healer gets the total its targets gained.
 */
#if defined(PC_PORT) && PORT_PLAYED_BUILD
int Reforged_SkillStat(const struct Unit *unit, int stat);
int Reforged_SkillHp(const struct Unit *unit);
int Reforged_SkillMov(const struct Unit *unit);
int Reforged_SkillRenewal(const struct Unit *unit);
void Reforged_SkillLtsHeal(void);
void Reforged_SkillLtsFortifyBegin(void);
void Reforged_SkillLtsFortifyEnd(void);
int Reforged_SkillPass(int uid);
int Reforged_SkillPassUnit(const struct Unit *unit);
int Reforged_SkillPassBump(struct Unit *unit, unsigned char **pit);
#define RFO_SKILL_PASS(uid) || Reforged_SkillPass(uid)
#define RFO_SKILL_PASS_ADJ(unit) && !Reforged_SkillPassUnit(unit)
#define RFO_SKILL_PASS_BUMP(unit, pit) && Reforged_SkillPassBump((unit), (pit))
#define RFO_SKILL_STAT(unit, stat) + Reforged_SkillStat((unit), (stat))
#define RFO_SKILL_HP(unit) + Reforged_SkillHp(unit)
#define RFO_SKILL_MOV(unit) + Reforged_SkillMov(unit)
#define RFO_SKILL_RENEWAL(unit) + Reforged_SkillRenewal(unit)
#define RFO_SKILL_LTS_HEAL() Reforged_SkillLtsHeal();
#define RFO_SKILL_LTS_FORTIFY_BEGIN() Reforged_SkillLtsFortifyBegin();
#define RFO_SKILL_LTS_FORTIFY_END() Reforged_SkillLtsFortifyEnd();
#else
#define RFO_SKILL_PASS(uid)
#define RFO_SKILL_PASS_ADJ(unit)
#define RFO_SKILL_PASS_BUMP(unit, pit)
#define RFO_SKILL_STAT(unit, stat)
#define RFO_SKILL_HP(unit)
#define RFO_SKILL_MOV(unit)
#define RFO_SKILL_RENEWAL(unit)
#define RFO_SKILL_LTS_HEAL()
#define RFO_SKILL_LTS_FORTIFY_BEGIN()
#define RFO_SKILL_LTS_FORTIFY_END()
#endif

#endif // GUARD_BM_UNIT_H
