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
   bump it (and keep the old behaviour for runs of the old version, or refuse them). */
#define RANDO_VERSION 1

struct RandoConfig {
    u32 seed;
    u8 version;              /* RANDO_VERSION when the run started */
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
/* The menu's settings (Reforged_GetOption, Reforged_GetRandoSeed) as a config. */
void Rando_ConfigFromSettings(struct RandoConfig *cfg);
/* Restores the vanilla tables and clears the run and the carried run, then (cfg != NULL and
   cfg enabled) applies cfg and makes it the run, or carries it when its version is not
   RANDO_VERSION; cfg == NULL: vanilla, no run. */
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

/* --------------------------------------------------- palettes (reforged_rando_palettes.c) */

/* The last step of the pipeline: gAnimCharaPalConfig / gAnimCharaPalIt put back, then
   ("Keep colours") each playable whose class or identity changed gets its colours on the
   classes of its new line. */
void RandoPalettes_Apply(void);
int RandoPalettes_SelfTest(void);
int RandoPalettes_Built(void); /* the number of sets built by the last apply */
void RandoPalettes_Dump(void); /* FE8PC_RANDO_PALDUMP */

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

#endif /* GUARD_REFORGED_RANDO_H */
