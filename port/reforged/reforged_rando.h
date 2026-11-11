#ifndef GUARD_REFORGED_RANDO_H
#define GUARD_REFORGED_RANDO_H

/*
 * Reforged Options randomizer: the interface between its files. Played builds only
 * (every file that includes this is wrapped in #if PORT_PLAYED_BUILD).
 *
 *   reforged_rando.c          core: the run's config, the seeded PRNG and hashes, the
 *                             vanilla snapshot and its restore, the apply pipeline (UFER's
 *                             order), growths, bases, MOV/CON/affinity, enemy growth buffs,
 *                             the new-game and save-load entry points, the self-test.
 *   reforged_rando_classes.c  classes: player, boss and enemy classes, weapon ranks, the
 *                             unit hooks (LoadUnit), inventories after a class change,
 *                             boss buffs, better enemy weapons, drops, promotions.
 *   reforged_rando_items.c    items: what an item is, the weapon pools, weapon stats and
 *                             traits, chest/gift rewards, shops.
 *   reforged_rando_recruit.c  recruitment: who fills each playable slot, the fill's data
 *                             written into the slot, names in texts, portraits.
 *   reforged_rando_palettes.c "Keep colours": characters' battle palettes on new classes.
 *   reforged_rando_nuzlocke.c "Nuzlocke": the run's graveyard (kept by the save mark).
 *   reforged_randomizer_menu.c the Randomizer screen (with reforged_settings.c's keys).
 *   reforged_savemark.c       <blk>_rando: the run's config saved with each save block.
 *
 * Rules that hold everywhere:
 * - Never call the game's RNG (NextRN*, Roll*RN, InitRN, Load/StoreRNState, the LCG): its
 *   state is saved in suspends and drives battles. Use Rando_Hash / struct RandoRng.
 * - Every decision is a pure function of the run's config (seed, version, values) and of
 *   stable content (ids, chapter index, coordinates), never of call order or addresses,
 *   so a save loaded later rebuilds the same tables and makes the same choices.
 * - The tables (gCharacterData, gClassData, gItemData) are restored from the vanilla
 *   snapshot before every apply; nothing may patch them outside the apply pipeline.
 * - Keep values inside the save's bit widths: stats <= 31, max HP <= 60 (63 in the save),
 *   item uses <= 63, class ids <= 0x7F, level <= 31.
 * - Port code here is compiled with the game's flags (gnu89): declarations at the top of
 *   a block, no "for (int i ...)".
 */

#include "global.h"
#include "bmunit.h"
#include "bmitem.h"
#include "port_reforged.h"

/* The algorithm's version, saved with each run. A change to any choice a seed makes must
   bump it (and keep the old behaviour for runs of the old version, or refuse them).
   1: v1.1. 2 (v1.1.3): an enemy's or boss's new class must also be able to leave its
   square (RANDO_VERSION_REACH, reforged_rando_classes.c ReachOk); a version 1 run keeps
   version 1's picks. Runs of RANDO_VERSION_MIN..RANDO_VERSION are applied, others carried.
   The hash (Rando_Hash) mixes in version 1 for every version, so a seed gives the same
   choices in all of them but where version 2 refuses a stuck class, version 3 a
   repeated chapter reward, version 4 a slow class or a copied monster weapon and version 5
   a copied replacement weapon, version 6 the "Dummy" items; version 7 adds colours to monsters;
   version 8 leaves out every item FE8 names "Dummy"; versions 9 and 10 are version 8 without
   and with the FE7 classes (only as extras: every version 8 pick stays; RANDO_VERSION_FE7);
   versions 11-14 add three choices on every promotion screen and the FE6 classes
   (RANDO_VERSION_THREE); versions 15-18 give a character's own colours on every class
   (RANDO_VERSION_ALLCOLOURS). RANDO_VERSION is defined below, after the version blocks. */
#define RANDO_VERSION_MIN 1
#define RANDO_VERSION_REACH 2
#define RANDO_VERSION_NOREPEAT 3 /* 3 (v1.1.4): no reward repeats within a chapter (reforged_rando_items.c) */
/* 4 (v1.1.5, reforged_rando_classes.c): an enemy's or boss's new class crosses the ground
   it stands on at least as easily as its old one and reaches 75% (not 50%) as far; bosses
   reroll like enemies; a unit made a monster gets that monster's different weapons, not
   copies of one. */
#define RANDO_VERSION_MAPFIT 4
/* 5 (v1.1.7, reforged_rando_classes.c RepairInventory, the user's report of 2026-11-04:
   Gilliam with two Slim Lances): a weapon or staff a class change replaces becomes one the
   unit does not already hold, when the class has another to give. */
#define RANDO_VERSION_NODUPES 5
/* 6 (v1.1.9, reforged_rando_items.c IsPoolable, the user's report of 2026-11-04: a village
   gave a "Dummy"): the Mine and the Light Rune, FE7 leftovers that FE8 names "Dummy" and
   never hands out, are never a reward, shop item or drop. */
#define RANDO_VERSION_NODUMMY 6
/* 7 (v1.2.0, reforged_rando_palettes.c, the user's request of 2026-11-04): with Keep colours, a
   character given a monster class gets the monster's body in its own signature colour. */
#define RANDO_VERSION_MONSTERCOLOUR 7
/* 8 (v1.2.1, reforged_rando_items.c IsDummyItem, the user's request of 2026-11-04): every
   item FE8 names "Dummy" (FE7's: Luce, Forblaze, Mani Katti, the dance rings, the Heaven
   Seal, besides version 6's Mine and Light Rune) is never a reward, shop item, drop or
   picked weapon. */
#define RANDO_VERSION_NOFE7 8
/* 9 and 10 (v1.17.0, reforged_rando_classes.c, the owner's decision of 2026-11-08): "FE7 classes"
   (RFO_RANDO_FE7, off by default). Archsage and Dark Druid are no third tier and replace nothing: with
   Random promotions, a human magic class's row gets, 1 time in RANDO_FE7_PROMO_ODDS, a third choice
   (Archsage for anima, light and staff classes, Dark Druid for dark ones); with Boss / Enemy classes, in
   story chapter 15 and later, a boss whose class of the run is a Sage, Bishop or Druid (either sex) becomes
   the FE7 one 1 time in RANDO_FE7_BOSS_ODDS and such a regular enemy 1 time in RANDO_FE7_ELITE_ODDS. Each
   is its own roll (RANDO_SALT_FE7 + 0 promotions, + 1 bosses, + 2 enemies) made after the version 8
   choice, which it may only add to (the third choice) or replace (that unit's class); the version 8
   candidate lists and hashes are untouched, so a seed's version 8 picks are the same in versions 9 and 10.
   Without the ROM loaded nothing is added (a saved FE7 unit falls back to its stand-in, reforged_fe7.c).

   Whether a run includes them is its VERSION, never an option value: New Game records version 10
   (RANDO_VERSION) with "FE7 classes" on and the player's FE7 ROM loaded, else version 9
   (RANDO_VERSION_PLAIN). The reason: an older build that carries a newer run (Rando_Start, the save
   mark) keeps only the entry's v= and seed= and the option keys it knows, so a new key such as
   randomizer_fe7 would be lost when it rewrites the side file, and the run's picks would change.
   Rando_Start sets the run's RFO_RANDO_FE7 value from the version (Rando_VersionFe7), so the side
   file's randomizer_fe7 only mirrors it, and the menu row only picks the version New Game writes.

   Versions 9 and 10 came as a pair (odd without the FE7 classes, even with them). The hash mixes in 1
   for every version from 2 on, so every version from 2 on hashes alike. */
#define RANDO_VERSION_FE7 10
#define RANDO_FE7_PROMO_ODDS 8
#define RANDO_FE7_BOSS_ODDS 4
#define RANDO_FE7_ELITE_ODDS 12

/* 11-14 (v1.21.0, reforged_rando_classes.c; the owner's decisions of 2026-11-09: the foreign classes are
   reached through the randomizer ("mix them in with the randomizers"), and "every promotion moment should
   have 3 choices, allows for more agency even with a 'bad roll' for promotions").
   - Every promotion screen of a randomized game offers exactly three different classes (Rando_PromoList):
     version 10's choices first (the table's two, Match gender's swap, version 10's own third: a Myrmidon's
     Blade Lord, a Knight's Armor Lord, a trainee's third, the FE7 extra), then the run's fill: with Random
     promotions, a rare foreign class that fits the row (1 in RANDO_THREE_PROMO_ODDS; magic rows 1 in
     RANDO_THREE_MAGIC_ODDS more after version 10's 1 in 8), else a third random class under the two random
     ones' rules; without (and on rows Random promotions keeps: lords, trainees, the foreign classes' own),
     the row's natural foreign promotion, else an FE8 class of the same weapons and sex.
   - "FE7/FE6 classes" (RFO_RANDO_FE7) on: any loaded foreign class (reforged_fe7.c: FE7's 0x80-0x88, FE6's
     0x89-0x8F) may come up, each by its own roll after every version 10 choice: the promotion fill above, a
     player's class of the run made its foreign counterpart (1 in RANDO_THREE_PLAYER_ODDS; the Bard for a
     Dancer and the Divine Dragon for Myrrh 1 in RANDO_THREE_SPECIAL_ODDS), late bosses (King, Armor Lord, 1 in
     RANDO_THREE_BOSS_ODDS) and enemies (Nomad, Trooper 1 in RANDO_THREE_ENEMY_ODDS, Thief F 1 in
     RANDO_THREE_THIEF_ODDS). Version 10's own FE7 extras stay where the FE7 ROM was loaded.
   Every other choice a seed makes is version 10's (9's without the FE7 ROM): the fill and the foreign rolls
   have their own salts (RANDO_SALT_THREE) and only add a choice or replace that one unit's class.

   Which foreign ROMs a run includes is its VERSION (version 10's reason: an older build that carries a
   newer run keeps only v=, seed= and the keys it knows): v1.21.0's New Game recorded 11 + the ROM set loaded
   then (bit 0 FE7, bit 1 FE6) with the row on, else 11: 11 none, 12 FE7, 13 FE6, 14 both (since v1.22.0 it
   records the next block, 15 + the set: Rando_VersionForRoms, RANDO_VERSION_ALLCOLOURS below).
   A run's own foreign picks (the rolls above) use a class only when the version names its ROM and the class is
   loaded now (a ROM removed later: the stand-ins and FE8 choices, never the other ROM's class); version 10's
   choices (reforged_fe7.c's thirds and table rows, the base FE7/FE6 class features) follow the ROMs loaded now,
   as in a game without the randomizer. From 11 on versions come in blocks of four (Rando_VersionRoms); a later
   change to the picks takes the next block (15-18) and its first number as its gate (gates are compared
   with >=). Versions 9 and 10 keep their meaning (Rando_VersionFe7). */
#define RANDO_VERSION_THREE 11
#define RANDO_ROM_FE7 1
#define RANDO_ROM_FE6 2
/* 15-18 (v1.22.0, reforged_rando_palettes.c, the owner's request of 2026-11-09: "make all classes get custom
   palettes per character's color combinations"): with Keep colours, every class a character is shown in gets
   that character's own colours (the FE7/FE6 classes, the classes with no colour data, Eirika and Ephraim in
   other classes, bosses in a new class), built when a battle needs them. Only how battles look changes: every
   choice a seed makes is version 11-14's (15 as 11, 16 as 12, 17 as 13, 18 as 14: the same ROM sets). A
   version 11-14 game keeps v1.21.0's colours (an older version looks and plays as it did). */
#define RANDO_VERSION_ALLCOLOURS 15
#define RANDO_VERSION (RANDO_VERSION_ALLCOLOURS + 3)
#define RANDO_VERSION_PLAIN RANDO_VERSION_ALLCOLOURS /* a new game without foreign classes */
#define Rando_VersionRoms(ver) ((ver) == RANDO_VERSION_FE7 ? RANDO_ROM_FE7 : \
    (ver) >= RANDO_VERSION_THREE ? (((ver) - RANDO_VERSION_THREE) & 3) : 0)
#define Rando_VersionForRoms(roms) (RANDO_VERSION_ALLCOLOURS + ((roms) & 3))
#define Rando_VersionFe7(ver) ((Rando_VersionRoms(ver) & RANDO_ROM_FE7) != 0)
#if RANDO_VERSION != Rando_VersionForRoms(RANDO_ROM_FE7 | RANDO_ROM_FE6)
#error "RANDO_VERSION must be the last version of the newest block (see RANDO_VERSION_THREE)"
#endif
#define RANDO_THREE_PROMO_ODDS 4
#define RANDO_THREE_MAGIC_ODDS 7
#define RANDO_THREE_PLAYER_ODDS 4
#define RANDO_THREE_SPECIAL_ODDS 8
#define RANDO_THREE_BOSS_ODDS 6
#define RANDO_THREE_ENEMY_ODDS 8
#define RANDO_THREE_THIEF_ODDS 4

/* "Master Seals only" (reforged_rando_items.c): the item, or a Master Seal for a promotion item. */
int Rando_SealItem(int item);
struct Unit;
void RandoItems_SealUnit(struct Unit *unit);

struct RandoConfig {
    u32 seed;
    u8 version;              /* the algorithm version the run started with (9 / 10: RANDO_VERSION_FE7;
                                11-14: RANDO_VERSION_THREE; 15-18: RANDO_VERSION_ALLCOLOURS) */
    u8 v[RFO_RANDO_COUNT];   /* the RFO_RANDO_* values, index id - RFO_RANDO_FIRST */
};

/* ---------------------------------------------------------------- core (reforged_rando.c) */

/* The run being played: gRandoRunOn is 1 when it is randomized (then gRandoRun holds it). */
extern struct RandoConfig gRandoRun;
extern int gRandoRunOn;
/* A run Rando_Start refused (another algorithm version): not applied (vanilla tables), but
   gRandoCarriedOn is 1 and gRandoCarried holds it until the next start, so the save mark
   writes it back with the blocks saved meanwhile instead of erasing it. */
extern struct RandoConfig gRandoCarried;
extern int gRandoCarriedOn;

/* The run's value of an RFO_RANDO_* option; 0 when the run is not randomized (even for
   RFO_RANDO_ENABLE). */
int Rando_Opt(int id);
/* The run includes the FE7 classes: randomized and its version says so (Rando_VersionFe7: 10, 12 or 14);
   whether the ROM is loaded now is asked separately (Reforged_Fe7ClassAvailable). */
int Rando_Fe7Run(void);
/* A version 11-14 run (RANDO_VERSION_THREE): three promotion choices; Rando_ThreeRoms: the foreign ROMs it
   includes (RANDO_ROM_*; 0 for any other run). */
int Rando_ThreeRun(void);
int Rando_ThreeRoms(void);
/* The menu's settings (Reforged_GetOption, Reforged_GetRandoSeed) as a config: version RANDO_VERSION
   with "FE7/FE6 classes" on, else RANDO_VERSION_PLAIN; New Game then records the ROMs loaded
   (Rando_RomsAtNewGame, reforged_rando.c). */
void Rando_ConfigFromSettings(struct RandoConfig *cfg);
/* The foreign ROMs whose classes are loaded now (RANDO_ROM_*: every class the randomizer may name). */
int Rando_RomsLoaded(void);
/* Restores the vanilla tables and clears the run and the carried run, then (cfg != NULL and
   cfg enabled) applies cfg and makes it the run (its RFO_RANDO_FE7 value set from its version,
   Rando_VersionFe7), or carries it unchanged when its version is not RANDO_VERSION_MIN..RANDO_VERSION;
   cfg == NULL: vanilla, no run. */
void Rando_Start(const struct RandoConfig *cfg);

/* Salts, one per step (UFER's, where it has one). */
enum {
    RANDO_SALT_GROWTHS = 124,
    RANDO_SALT_CLASSES = 878,   /* player classes; UFER 874 + 4 */
    RANDO_SALT_BOSSES = 879,
    RANDO_SALT_MINIONS = 880,
    RANDO_SALT_MOV = 881,
    RANDO_SALT_BASES = 9001,
    RANDO_SALT_CON = 9002,
    RANDO_SALT_AFFINITY = 9003,
    RANDO_SALT_WEAPONS = 30000, /* + 0 might, 1 hit, 2 weight, 3 uses, 4 traits */
    RANDO_SALT_SHOPS = 8480,
    RANDO_SALT_ENEMY = 252521,  /* + 0 growth buff, 1 better weapons, 2 boss buff */
    RANDO_SALT_REWARDS = 27682,
    RANDO_SALT_DROPS = 27683,
    RANDO_SALT_INVENTORY = 40000,
    RANDO_SALT_GEMSWAP = 50000, /* "Infinite gold": a gem's useful item (reforged_rando_items.c) */
    RANDO_SALT_PROMOTIONS = 60000, /* "Random promotions" (reforged_rando_classes.c) */
    RANDO_SALT_MUSIC = 60010,      /* "Random music" (reforged_rando_music.c), + 1 battle */
    RANDO_SALT_FLOORS = 60020,     /* "Tower & Ruins" (reforged_rando_classes.c) */
    RANDO_SALT_FE7 = 60030,        /* "FE7 classes" (reforged_rando_classes.c): + 0 promotions, 1 bosses, 2 enemies */
    RANDO_SALT_THREE = 60040,      /* versions 11-14 (reforged_rando_classes.c): + 0 a row's foreign third, 1 its
                                      random third, 2 the FE8 fill, 3 players, 4 bosses, 5 enemies */
};

/* A 32-bit hash of the run's seed, version, a salt and three values (stable, well mixed). */
u32 Rando_Hash(u32 salt, u32 a, u32 b, u32 c);
/* A small PRNG (xoshiro128** seeded by splitmix from the run's seed and the salt). */
struct RandoRng { u32 s[4]; };
void Rando_RngSeed(struct RandoRng *r, u32 salt);
u32 Rando_RngNext(struct RandoRng *r);
/* [0, n); 0 when n <= 0. */
int Rando_RngInt(struct RandoRng *r, int n);
/* r reduced to [0, n) (for a Rando_Hash value); 0 when n <= 0. */
int Rando_HashInt(u32 r, int n);

/* The vanilla entries (the snapshot taken at boot), whatever the live tables hold. */
const struct CharacterData *Rando_VanillaChar(int pid);
const struct ClassData *Rando_VanillaClass(int jid);
const struct ItemData *Rando_VanillaItem(int iid);
/* Writable live entries (the same arrays the game reads). */
struct CharacterData *Rando_Char(int pid);
struct ClassData *Rando_Class(int jid);
struct ItemData *Rando_Item(int iid);

/* A playable character whose stats the randomizer changes: 0x01-0x22 but 0x1B, and the
   postgame copies 0x24-0x2C (not Lyon 0x23). */
int Rando_IsPlayable(int pid);

/* reforged_savemark.c: the run saved with save block `block` (SAVE_ID_GAME0..SAVE_ID_SUSPEND_ALT,
   0-4), or NULL when that block's run was not randomized (or has no valid save mark). */
const struct RandoConfig *Rando_SaveMarkBlock(int block);

/* Self-tests (FE8PC_RANDO_CHECK; Reforged_RandoBoot runs them after each apply of its test
   configs): each checks its own file's invariants on the live tables and returns the number
   of failures, logging each one ("[rando] check: ..."). */
int RandoClasses_SelfTest(void);
/* Version 10's FE7 extras against version 8's picks (reforged_rando_classes.c): the run applied as
   version 8 and again as version 10 (cfg10, FE7 classes), every unit definition in a sample keyed as the
   game keys it: the same class unless the FE7 roll fired (then an FE7 class, from a version 8 Sage,
   Bishop or Druid), and with the FE7 classes not loaded always the same. *fired: the rolls that fired. */
int RandoClasses_Fe7SelfTest(const struct RandoConfig *cfg10, int *fired, int *promoRows);
/* Versions 11-14 against version 10 (reforged_rando_classes.c): cfg (version 10, FE7 classes) applied as
   version 10, 9 and as each of 11-14, with the foreign classes loaded and taken as not loaded: the
   promotion rows, the player classes and a sample of units the same but where a foreign roll fired (then
   its counterpart, its ROM in the version and loaded, sex and weapons matching), every promotion list
   three different classes starting with version 10's, version 9 and 10's lists unchanged, every foreign
   class that may come up with an animation for each weapon kind it is given. */
struct RandoThreeStats {
    int runs, players, units, bosses, promoForeign, promoRandom, promoFill, lists, oldShort;
    /* the save stand-ins (reforged_fe7.c Reforged_Fe7Fallback: what a build or run without the ROM loads) of the fired
       rolls and the lists' foreign classes: players (always their class of the run), units and bosses as their class
       of the run or as another class of its tier and the foreign class's weapons (the map-checked stand-in: a King's
       Swordmaster, a Sniper's Trooper's Ranger), promotion choices a class of the row's tier, the unit's sex and a
       weapon kind of the foreign class */
    int standPlayers, standUnitsSame, standUnitsOther, standPromo;
    int byClass[0x10]; /* foreign picks seen (players, units, promotion lists), by class - 0x80 */
};
int RandoClasses_ThreeSelfTest(const struct RandoConfig *cfg, struct RandoThreeStats *st);
int RandoClasses_ThreeSelfTestAt(const struct RandoConfig *cfg, struct RandoThreeStats *st, int add);
/* Version 11-14's player rolls (reforged_rando_classes.c): pid's class before its foreign roll (0: none). */
int RandoClasses_PlayerFe8Class(int pid);
int RandoItems_SelfTest(void);

/* ----------------------------------------------------- classes (reforged_rando_classes.c) */

/* Step 1 of the pipeline (after the restore): player classes. Rewrites, for each randomized
   playable pid, gCharacterData's defaultClass, baseRanks, the bases (retain final: personal +=
   old class base - new class base, clamped) and, with "Class growths: transfer", the growths;
   for the lords, the lock bits of their prf weapons. Also decides the boss class map. */
void RandoClasses_Apply(void);
/* The class a playable pid has in this run (its vanilla default class when unchanged). */
int RandoClasses_PlayerClass(int pid);
/* Class facts used by the other files. */
int Rando_ClassIsPromoted(int jid);
int Rando_ClassIsFlier(int jid);
int Rando_ClassIsMonster(int jid);
/* The unpromoted class a promoted one comes from (the first one found), or 0. */
int Rando_ClassBase(int jid);
/* The M/F twin of a class (Cavalier <-> Cavalier F), or 0. */
int Rando_ClassTwin(int jid);
/* The promoted classes an unpromoted class promotes into (ClassData.promotion and
   gPromoJidLut, no repeats): how many, in out. 0 for a promoted class. */
int Rando_ClassPromotions(int jid, u8 out[3]);
int Rando_ClassPromotionsFor(int jid, int female, u8 out[3]); /* as a unit of that sex gets them */

/* --------------------------------------------------------- items (reforged_rando_items.c) */

/* Step 3 of the pipeline: weapon might/hit/weight/uses and traits (gItemData). */
void RandoItems_Apply(void);
/* What an item is (vanilla data): */
int Rando_ItemIsWeapon(int iid);    /* weapon types 0-7 but staff, usable by humans (not a
                                        monster weapon, not a rank-0 dark monster tome) */
int Rando_ItemIsStaff(int iid);
int Rando_ItemIsPrf(int iid);       /* locked to a class or character (Rapier, ...) */
int Rando_ItemIsLegendary(int iid); /* the sacred twins and the other S-rank story weapons */
/* The vanilla rank of a weapon or staff as a level: 0 none/prf, 1 E, 2 D, 3 C, 4 B, 5 A, 6 S. */
int Rando_ItemRankLevel(int iid);
/* A rank level from a wexp value (WPN_EXP_*), as the game's GetWeaponLevelFromExp. */
int Rando_RankLevelOf(int wexp);
/* A weapon of `type` (ITYPE_SWORD..ITYPE_DARK; ITYPE_STAFF: a healing staff), never prf,
   legendary or monster: its rank level as close to wantLevel as can be and <= maxLevel
   (maxLevel < 1: E), r choosing among the closest; 0 when the type has none. */
int Rando_PickWeapon(int type, int wantLevel, int maxLevel, u32 r);
/* The weapon a monster class fights with (Revenant: Rotten Claw, Mogall: Evil Eye, ...),
   r choosing when there are several; 0 for a class that is not a monster. */
int Rando_MonsterWeapon(int jid, u32 r);
/* Every weapon of the monster class jid's kind (version 4's other weapons), in a fixed
   order, into out (at most RANDO_KIN_MAX); the count. */
#define RANDO_KIN_MAX 4
int Rando_MonsterKin(int jid, u8 *out);

/* --------------------------------------------------- palettes (reforged_rando_palettes.c) */

/* The last step of the pipeline: gAnimCharaPalConfig / gAnimCharaPalIt put back, then
   ("Keep colours") each playable whose class or identity changed gets its colours on the
   classes of its new line. */
void RandoPalettes_Apply(void);
int RandoPalettes_SelfTest(void);
int RandoPalettes_Built(void); /* the number of sets built by the last apply */
void RandoPalettes_Dump(void); /* FE8PC_RANDO_PALDUMP */
/* Version 15 (RANDO_VERSION_ALLCOLOURS): every character with colours of its own on every class with a battle
   palette (FE8's and the loaded foreign ones), built through the battle cache: the failures, and one summary
   line ("[rando] self-test: keep colours: ..."). */
int RandoPalettes_AllSelfTest(void);

/* -------------------------------------------------- recruitment (reforged_rando_recruit.c) */

/* The character whose identity (name, face, colours, stats) the playable pid has in
   this run: pid itself unless "Recruitment" shuffled it. */
int Rando_RecruitSource(int pid);
/* A step of the pipeline, after growths and before the palettes. */
void RandoRecruit_Apply(void);
int RandoRecruit_SelfTest(void);
void RandoRecruit_SetQuiet(int quiet); /* the self-test: no per-start log line */

/* ------------------------------------------------------ Nuzlocke (reforged_rando_nuzlocke.c) */
/* A new game of a Nuzlocke run starts with an empty graveyard. */
void RandoNuzlocke_NewGame(void);

/* classes helpers for recruitment (reforged_rando_classes.c) */
/* UFER transferWeaponRanks from effective ranks eff[8] to class newJ's types. */
void Rando_TransferRanks(struct RandoRng *rng, const u8 eff[8], int newJ, u8 out[8]);
/* jid obeys the class rules for the playable pid (attack, melee, range, lords). */
int Rando_PlayerFits(int pid, int jid);
/* Makes jid pid's class of the run (the unit hooks then load pid in it). */
void RandoClasses_SetPlayerClass(int pid, int jid);

/* "Random music" (reforged_rando_music.c) */
int Reforged_RandoMapSong(int song);
int Reforged_RandoBattleSong(int song);
int RandoMusic_SelfTest(void);
/* "Random promotions" (reforged_rando_classes.c): gPromoJidLut for the run, before the classes */
void RandoPromotions_Apply(void);
/* Versions 11-14: the players' foreign rolls, the last step of the pipeline (after the palettes). */
void RandoClasses_ApplyForeign(void);
void RandoClasses_SetQuiet(int quiet); /* the self-test: the pipeline's class steps do not log */
/* Versions 11-14: the three classes a unit of class jid and that sex is offered (out[3]); 0 when the run
   is not one of them (the screen then shows what it always did). */
int Rando_PromoList(int jid, int female, u8 out[3]);
/* v1.23.0's "Third promotion" self-test (FE8PC_PROMO3_CHECK, at the first boot): the failures. */
int RandoClasses_Promo3SelfTest(void);
int Rando_VanillaPromo(int jid, int i); /* the vanilla gPromoJidLut entry */

#endif /* GUARD_REFORGED_RANDO_H */
