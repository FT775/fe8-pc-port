#include "global.h"

/*
 * Reforged Options randomizer: classes (port/reforged/reforged_rando.h, "classes").
 * Played builds only; empty in every test build. Ideas and algorithms after the
 * Universal FE Randomizer (UFER, MIT): no code is taken from it.
 *
 * Class facts. Every class id 0x01-0x7F gets its facts once, from the vanilla
 * snapshot: tier (CA_PROMOTED), flier (the fliers' movement table, or a flier
 * bit), monster (0x52-0x66 and the 0x7C/0x7D duplicates), thief (Thief, Rogue,
 * Assassin M/F), lord, trainee, the weapon types it uses, whether it can attack
 * (a weapon rank, or a monster weapon), in melee, at range, whether it can promote
 * (ClassData.promotion or gPromoJidLut), its M/F twin. Special classes are never
 * a target and never randomized away from: manaketes, gorgon eggs, Bard, Dancer,
 * Necromancer, Fleet, Phantom, Draco Zombie, Demon King, the ballista states,
 * civilians and the other NPC classes up to 0x7D, and any class with no battle
 * animation or a relative power of 0.
 *
 * Player classes (RandoClasses_Apply, step 1 of the pipeline). For each playable
 * pid in id order, one PRNG stream (RANDO_SALT_CLASSES): a class of the vanilla
 * class's tier (a trainee counts as unpromoted; an unpromoted target must be able
 * to promote), lords only with "Include lords" (Eirika then keeps swords, Ephraim
 * lances, both promotable, and their prf weapons through CA_LOCK_4 / CA_LOCK_5 on
 * the character), thieves only with "Include thieves", monsters only with "Monster
 * classes", Tethys only with "Include dancer", Myrrh never. Eirika, Ephraim, Seth,
 * Artur, Garcia, Gilliam and Neimi must be able to attack; Seth and Gilliam in
 * melee, Artur and Neimi at range (the scripted FIGHTs they start: prologue Seth vs.
 * Valter, Ch1 tutorial Gilliam, Ch3 Neimi from two squares, Ch4 Artur). "Assign
 * evenly" takes the class (and its twin) out of the tier's pool until it empties.
 * "Match gender" then swaps to the M/F twin of the character's sex. The character's
 * weapon ranks move to the new class's types (UFER's transferWeaponRanks: the
 * highest N, assigned at random, the last one lowered a step for each further
 * type, dark at least D), the bases keep the final stats ("retain final") or the
 * personal ones, and "Class growths: transfer" moves the personal growth offsets.
 *
 * Units (LoadUnit hooks). Reforged_RandoUnitDef picks the class of the unit being
 * loaded: a playable pid gets its class of the run (the promoted form when the
 * definition gives a promoted class), a red boss its boss class (one per pid and
 * vanilla class, forced to change, same tier, an attacker, fliers stay fliers,
 * human or monster kept, matched to the boss's sex), a red generic its enemy class
 * (keyed on the chapter, pid, class, tile and level; thieves, village looters and
 * generated monsters keep theirs; a non-flier that rolls a flier rolls once more).
 * A boss or enemy class change falls back to the vanilla class when the unit's tile
 * or a REDA waypoint is impassable for the new class but not for the old one.
 * Reforged_RandoUnitLoaded then fixes what a class change breaks (ranks, the
 * inventory as UFER's validateCharacterInventory: replaced weapons keep their
 * slot, so the drop item stays last), and applies the enemy options: boss stat
 * buffs, better enemy weapons, enemy drops. Everything is keyed on the run and
 * the unit's content, never on the game's RNG or the load order.
 *
 * Other hooks: the Ch16 PROM promotes a randomized lord into a promotion of its
 * own class, and is skipped for one already promoted (by its brace); a scripted
 * battle's forced weapon the unit cannot wield becomes its equipped weapon; the
 * lord braces work for the randomized lords, Master Seal and the matching crests
 * for the monster classes; the world map's skirmish icons show the leaders' classes
 * of the run (Reforged_RandoSkirmishLeaderClass, the same choice and key as the
 * load). Reforged_RandoMoveGuard bounds the REDA path walk (bmidoten.c
 * GenerateBestMovementScript) in randomized runs. Reforged_RandoStatGain keeps
 * UnitAutolevelCore's whole-autolevel gains from wrapping an s8 stat (buffed
 * enemy growths).
 *
 * Version 10's FE7 extras ("FE7 classes", reforged_rando.h RANDO_VERSION_FE7; version 9 is the same run
 * without them, the version records which): FE7's
 * Archsage and Dark Druid (reforged_fe7.c, ids past 0x7F, never in the candidate lists
 * above) as a rare third promotion choice of a magic class's row (Fe7PromoRows,
 * Reforged_RandoFe7Promo) and as a rare late boss or regular enemy (Fe7EnemyClass),
 * each its own roll after the version 8 pick; RandoClasses_Fe7SelfTest checks that
 * every version 8 pick stays.
 *
 * Versions 11-14 (reforged_rando.h RANDO_VERSION_THREE; the owner's decisions of 2026-11-09): every
 * promotion screen offers three different classes (Rando_PromoList: version 10's choices, then the
 * run's fill), and with "FE7/FE6 classes" every loaded foreign class can come up (the promotion fill,
 * the players' foreign rolls RandoClasses_ApplyForeign, late bosses and enemies ThreeEnemyClass), each
 * its own roll after every version 10 choice; RandoClasses_ThreeSelfTest checks that every version 10
 * pick stays.
 *
 * v1.23.0, the Game fix "Third promotion" (RFO_FIX_PROMO3, off by default; the owner's table of 2026-11-09): in a game
 * without the randomizer, every promotion screen offers three classes through the same list (Reforged_PromoList and
 * the hooks above): the game's own choices, then the owner's table (Promo3ListEx, below), an FE8 stand-in for a
 * class whose ROM is not loaded. Randomized games never use it; with the fix off nothing changes. A class the
 * row adds gets the randomizer's after-care when taken (Promo3Care in Reforged_RandoPromoted: gains, ranks, a basic
 * weapon for one lost), and a seal used on a tile such a class could not stand on shows the game's own screen
 * (P3TileBars).
 */

#if PORT_PLAYED_BUILD

#include <stdio.h>
#include <string.h>

#include "proc.h"
#include "bmunit.h"
#include "bmitem.h"
#include "bmmap.h"
#include "bmidoten.h"
#include "mu.h"
#include "muctrl.h"
#include "variables.h"
#include "constants/chapters.h"
#include "constants/characters.h"
#include "constants/classes.h"
#include "constants/items.h"
#include "constants/terrains.h"
#include "bmlib.h"
#include "chapterdata.h"
#include "ekrbattle.h"
#include "bmbattle.h"
#include "bmarch.h"
#include "bmcontainer.h"
#include "classchg.h"
#include "bmmind.h"

#include "port_hw.h"
#include "port_reforged.h"
#include "reforged_rando.h"
#include "reforged_skills.h"

#define JID_MAX   0x7F
#define PID_MAX   0xFF
#define STAT_HP_MAX 60
#define STAT_MAX  31
#define STAT_LCK_MAX 30
#define STORY_LAST 23 /* UFER's FE8 chapter count: prologue 0 .. the final chapter 23 */

enum {
    F_REAL      = 1 << 0,  /* battle animation, relative power > 0 */
    F_SPECIAL   = 1 << 1,  /* never a target, never randomized away from */
    F_LORD      = 1 << 2,
    F_TRAINEE   = 1 << 3,
    F_THIEF     = 1 << 4,
    F_MONSTER   = 1 << 5,
    F_PROMOTED  = 1 << 6,
    F_FLIER     = 1 << 7,
    F_ATTACK    = 1 << 8,
    F_MELEE     = 1 << 9,
    F_RANGE     = 1 << 10,
    F_STAFF     = 1 << 11,
    F_PROMOTES  = 1 << 12, /* unpromoted, with a promotion */
    F_MALE      = 1 << 13, /* the M side of a twin pair */
    F_FEMALE    = 1 << 14, /* the F side */
    F_MONSTERWPN= 1 << 15, /* fights with monster weapons (Rando_MonsterWeapon) */
    F_SUMMON    = 1 << 16,
};

static u32 sFacts[JID_MAX + 1];
static u8 sTwin[JID_MAX + 1];
static int sFactsBuilt;

/* The class each playable pid has in the run (0: its vanilla class). */
static u8 sPlayerClass[PID_MAX + 1];

/* What Reforged_RandoUnitDef decided for the unit LoadUnit is building; read by
   Reforged_RandoUnitLoaded in the same LoadUnit. */
enum { UK_NONE, UK_PLAYER, UK_BOSS, UK_ENEMY, UK_OTHER };

struct UnitKey {
    int valid;
    int kind;
    int pid;
    int origClass;  /* the class the vanilla tables give this definition */
    int newClass;
    int hadItems;   /* the definition lists items */
    int floor;      /* Tower & Ruins: a generated monster made another (its drop is kept) */
    u32 a, b, c;    /* the unit's content key (chapter, pid, class; tile; level) */
};

static struct UnitKey sLast;

static int sQuiet; /* the self-test: the hooks it calls do not log */
static int sQuietApply; /* reforged_rando.c's self-test: the pipeline's steps do not log (RandoClasses_SetQuiet) */

void RandoClasses_SetQuiet(int quiet)
{
    sQuietApply = quiet;
}

/* Three hash values reaching every index of a 1-3 entry choice in Rando_HashInt. */
static const u32 sPickAll[3] = { 0x00000000u, 0x60000000u, 0xB0000000u };

static const u8 sLevelExp[7] = { WPN_EXP_0, WPN_EXP_E, WPN_EXP_D, WPN_EXP_C, WPN_EXP_B, WPN_EXP_A, WPN_EXP_S };

static const u8 sTwins[][2] = {
    { CLASS_CAVALIER, CLASS_CAVALIER_F },         { CLASS_PALADIN, CLASS_PALADIN_F },
    { CLASS_ARMOR_KNIGHT, CLASS_ARMOR_KNIGHT_F }, { CLASS_GENERAL, CLASS_GENERAL_F },
    { CLASS_MERCENARY, CLASS_MERCENARY_F },       { CLASS_HERO, CLASS_HERO_F },
    { CLASS_MYRMIDON, CLASS_MYRMIDON_F },         { CLASS_SWORDMASTER, CLASS_SWORDMASTER_F },
    { CLASS_ASSASSIN, CLASS_ASSASSIN_F },         { CLASS_ARCHER, CLASS_ARCHER_F },
    { CLASS_SNIPER, CLASS_SNIPER_F },             { CLASS_RANGER, CLASS_RANGER_F },
    { CLASS_WYVERN_RIDER, CLASS_WYVERN_RIDER_F }, { CLASS_WYVERN_LORD, CLASS_WYVERN_LORD_F },
    { CLASS_WYVERN_KNIGHT, CLASS_WYVERN_KNIGHT_F }, { CLASS_MAGE, CLASS_MAGE_F },
    { CLASS_SAGE, CLASS_SAGE_F },                 { CLASS_MAGE_KNIGHT, CLASS_MAGE_KNIGHT_F },
    { CLASS_BISHOP, CLASS_BISHOP_F },             { CLASS_SHAMAN, CLASS_SHAMAN_F },
    { CLASS_DRUID, CLASS_DRUID_F },               { CLASS_SUMMONER, CLASS_SUMMONER_F },
    { CLASS_GREAT_KNIGHT, CLASS_GREAT_KNIGHT_F }, { CLASS_PRIEST, CLASS_CLERIC },
};

/* ------------------------------------------------------------------ small helpers */

/* A second, independent value from a hash. */
static u32 Mix(u32 h, u32 k)
{
    u32 x = h + 0x9E3779B9u * (k + 1);

    x ^= x >> 16;
    x *= 0x85EBCA6Bu;
    x ^= x >> 13;
    x *= 0xC2B2AE35u;
    x ^= x >> 16;
    return x;
}

static int Min(int a, int b)
{
    return a < b ? a : b;
}

static int Max(int a, int b)
{
    return a > b ? a : b;
}

static int Clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int ValidJid(int jid)
{
    return jid >= 1 && jid <= JID_MAX;
}

/* The foreign classes (reforged_fe7.c): FE7's 0x80-0x88, FE6's 0x89-0x8F. */
#define FJ_NOMAD        0x80
#define FJ_NOMAD_F      0x81
#define FJ_BLADE_LORD   0x82
#define FJ_ARMOR_LORD   0x84 /* FE7's Great Lord, shown "Armor Lord" (for Knights) */
#define FJ_ARCHSAGE     REFORGED_FE7_JID_ARCHSAGE
#define FJ_DARK_DRUID   REFORGED_FE7_JID_DARK_DRUID
#define FJ_BARD         REFORGED_FE7_JID_BARD
#define FJ_THIEF_F      REFORGED_FE6_JID_THIEF_F
#define FJ_TROOPER      REFORGED_FE6_JID_TROOPER
#define FJ_TROOPER_F    REFORGED_FE6_JID_TROOPER_F
#define FJ_KING         REFORGED_FE6_JID_KING
#define FJ_LION_LORD    REFORGED_FE6_JID_LION_LORD
#define FJ_MASTER_LORD  REFORGED_FE6_JID_MASTER_LORD
#define FJ_DIVINE       REFORGED_FE6_JID_DIVINE_DRAGON

static int IsForeignJid(int jid)
{
    return jid >= 0x80 && jid <= REFORGED_FOREIGN_LAST_JID;
}

/* The ROM a foreign class comes from (RANDO_ROM_*). */
static int ForeignRom(int jid)
{
    return jid <= REFORGED_FE7_LAST_JID ? RANDO_ROM_FE7 : RANDO_ROM_FE6;
}

int Rando_ThreeRun(void)
{
    return gRandoRunOn && gRandoRun.version >= RANDO_VERSION_THREE;
}

int Rando_ThreeRoms(void)
{
    return Rando_ThreeRun() ? Rando_VersionRoms(gRandoRun.version) : 0;
}

/* Version 10's FE7 classes (Archsage, Dark Druid) have the facts of their FE8 stand-ins (Sage, Druid: a
   promoted attacker with staves) in every run; in a version 11-14 run every foreign class has its save
   stand-in's (Reforged_Fe7Fallback, the male side), so a unit in one gets its inventory and ranks seen to; so has
   (v1.23.0) the foreign class a "Third promotion" game's row added, while its promotion's after-care runs
   (sP3Foreign, set only inside Reforged_RandoPromoted in a game without the randomizer). */
static int sP3Foreign;

static int Fe7Stand(int jid)
{
    if (jid == REFORGED_FE7_JID_ARCHSAGE)
        return CLASS_SAGE;
    if (jid == REFORGED_FE7_JID_DARK_DRUID)
        return CLASS_DRUID;
    if (IsForeignJid(jid) && (Rando_ThreeRun() || sP3Foreign))
        return Reforged_Fe7Fallback(jid, 0);
    return 0;
}

static int sFe7Hide; /* the self-test: the ROMs (RANDO_ROM_*) whose foreign classes are taken as not loaded */

/* The FE7 class jid is loaded (reforged_fe7.c), as the randomizer may use it now. */
static int Fe7Loaded(int jid)
{
    return !(sFe7Hide & ForeignRom(jid)) && Reforged_Fe7ClassAvailable(jid);
}

/* Versions 11-14: the foreign class jid may come up in this run now (its ROM in the run's version, the class
   loaded). */
static int ForeignOk(int jid)
{
    return IsForeignJid(jid) && (Rando_ThreeRoms() & ForeignRom(jid)) != 0 && Fe7Loaded(jid);
}

/* The sword lords of the run's version, in the order a unit of that sex gets them: FE7's Blade Lord (Lyn's art) first
   for a woman, FE6's Master Lord (Roy's) first for a man, then the other; only the lords whose ROM the version names,
   whatever is loaded now (a ROM removed later must give the stand-in or the FE8 choice, never the other ROM's lord).
   Returns how many (0-2). */
static int RunSwordLords(int female, int order[2])
{
    int roms = Rando_ThreeRoms(), n = 0, first = female ? FJ_BLADE_LORD : FJ_MASTER_LORD,
        second = female ? FJ_MASTER_LORD : FJ_BLADE_LORD;

    if (roms & ForeignRom(first))
        order[n++] = first;
    if (roms & ForeignRom(second))
        order[n++] = second;
    return n;
}

/* A class's data for the map checks: an FE8 class's of this run, or a loaded foreign class's (NULL: neither). */
static const struct ClassData *MapClass(int jid)
{
    if (IsForeignJid(jid))
        return Fe7Loaded(jid) ? GetClassData(jid) : NULL;
    return Rando_Class(jid);
}

/* The class's battle animation list has an entry for weapon kind t (ekrcmd.c GetBattleAnimationId's type
   entries, 0x100 + t). */
static int AnimHasType(const struct ClassData *k, int t)
{
    const struct BattleAnimDef *d;
    int j;

    if (k == NULL || k->pBattleAnimDef == NULL)
        return 0;
    for (d = k->pBattleAnimDef, j = 0; d->wtype != 0 && j < 64; d++, j++)
        if (d->wtype == 0x100 + t && d->index != 0)
            return 1;
    return 0;
}

static u32 Facts(int jid)
{
    return ValidJid(jid) ? sFacts[jid] : Fe7Stand(jid) ? sFacts[Fe7Stand(jid)] : 0;
}

static int Has(int jid, u32 flags)
{
    return (Facts(jid) & flags) != 0;
}

/* The class uses weapon type t (vanilla base rank; the randomizer never changes it; an FE7 class of
   version 10, its own when it is loaded; in a version 11-14 run a foreign class only with a battle animation
   for that kind, so a weapon it is given is one it can be shown fighting with). */
static int ClassUses(int jid, int t)
{
    const struct ClassData *k = Rando_VanillaClass(jid);

    if (k == NULL && Fe7Stand(jid) && Fe7Loaded(jid))
    {
        k = GetClassData(jid);
        if (Rando_ThreeRun() && t >= 0 && t < 8 && !AnimHasType(k, t))
            return 0;
    }

    return k != NULL && t >= 0 && t < 8 && k->baseRanks[t] != 0;
}

static int IsAttackType(int t)
{
    return t >= ITYPE_SWORD && t <= ITYPE_DARK && t != ITYPE_STAFF;
}

static u8 Chapter(void)
{
    return (u8)gPlaySt.chapterIndex;
}

/* ------------------------------------------------------------------ class facts */

static int IsSpecialJid(int jid)
{
    switch (jid)
    {
    case CLASS_MANAKETE: case CLASS_MANAKETE_2: case CLASS_MANAKETE_MYRRH:
    case CLASS_GORGONEGG: case CLASS_GORGONEGG2:
    case CLASS_BARD: case CLASS_DANCER: case CLASS_NECROMANCER: case CLASS_FLEET: case CLASS_PHANTOM:
    case CLASS_DRACO_ZOMBIE: case CLASS_DEMON_KING:
        return 1;
    }
    /* ballista states, civilians, NPC classes, the tent, the Creature Campaign duplicates */
    return jid >= CLASS_BLST_REGULAR_USED && jid <= CLASS_ELDER_BAEL_2;
}

static int IsTraineeJid(int jid)
{
    switch (jid)
    {
    case CLASS_JOURNEYMAN: case CLASS_PUPIL: case CLASS_RECRUIT:
    case CLASS_JOURNEYMAN_T1: case CLASS_PUPIL_T1: case CLASS_RECRUIT_T1:
    case CLASS_JOURNEYMAN_T2: case CLASS_PUPIL_T2: case CLASS_RECRUIT_T2:
        return 1;
    }
    return 0;
}

static int IsThiefJid(int jid)
{
    return jid == CLASS_THIEF || jid == CLASS_ROGUE || jid == CLASS_ASSASSIN || jid == CLASS_ASSASSIN_F;
}

static int VanillaPromoted(int jid)
{
    const struct ClassData *k = Rando_VanillaClass(jid);

    return k != NULL && (k->attributes & CA_PROMOTED) != 0;
}

/* "Random promotions" is on in this run: gPromoJidLut holds the run's promotions (the
   vanilla ClassData.promotion no longer counts). */
static int RandomPromotions(void)
{
    return gRandoRunOn && Rando_Opt(RFO_RANDO_PROMOTIONS);
}

static void BuildFacts(void);
extern const int gPromoJidLutCount; /* src/classchg-data.c */

/* With Random promotions and Match gender: a promoted class of a male/female pair, made
   the given sex's (the run's table holds one side of each pair: RandoPromotions_Apply). */
static int SexTwin(int jid, int female)
{
    if (!RandomPromotions() || !Rando_Opt(RFO_RANDO_GENDER) || !ValidJid(jid))
        return jid;
    BuildFacts();
    if (sTwin[jid] != 0 && ((female && Has(jid, F_MALE)) || (!female && Has(jid, F_FEMALE))))
        return sTwin[jid];
    return jid;
}

/* The default promotion of an unpromoted class: ClassData.promotion when it is a
   promoted class (it keeps the sex: Shaman F -> Druid F), else gPromoJidLut's first;
   with Random promotions, gPromoJidLut's first. */
static int PromotionOf(int jid)
{
    const struct ClassData *k = Rando_VanillaClass(jid);

    if (k == NULL || VanillaPromoted(jid))
        return 0;
    if (RandomPromotions())
        return ValidJid(gPromoJidLut[jid][0]) && VanillaPromoted(gPromoJidLut[jid][0]) ? gPromoJidLut[jid][0] : 0;
    if (ValidJid(k->promotion) && VanillaPromoted(k->promotion))
        return k->promotion;
    if (ValidJid(gPromoJidLut[jid][0]) && VanillaPromoted(gPromoJidLut[jid][0]))
        return gPromoJidLut[jid][0];
    return 0;
}

/* jid is one of the promotions of the unpromoted class base. */
static int IsPromotionOf(int base, int jid)
{
    const struct ClassData *k = Rando_VanillaClass(base);

    if (k == NULL || !ValidJid(jid) || VanillaPromoted(base))
        return 0;
    if (RandomPromotions())
        return jid == gPromoJidLut[base][0] || jid == gPromoJidLut[base][1] ||
               (sTwin[jid] != 0 && (sTwin[jid] == gPromoJidLut[base][0] || sTwin[jid] == gPromoJidLut[base][1]));
    return jid == gPromoJidLut[base][0] || jid == gPromoJidLut[base][1] || jid == k->promotion;
}

static void BuildFacts(void)
{
    const struct ClassData *flyRef;
    int jid, t, i;

    if (sFactsBuilt)
        return;
    flyRef = Rando_VanillaClass(CLASS_PEGASUS_KNIGHT);
    if (flyRef == NULL)
        return;
    memset(sFacts, 0, sizeof(sFacts));
    memset(sTwin, 0, sizeof(sTwin));
    for (jid = 1; jid <= JID_MAX; jid++)
    {
        const struct ClassData *k = Rando_VanillaClass(jid);
        u32 f = 0;

        if (k == NULL)
            continue;
        if (k->pBattleAnimDef != NULL && k->classRelativePower > 0)
            f |= F_REAL;
        if (!(f & F_REAL) || IsSpecialJid(jid))
            f |= F_SPECIAL;
        if (jid >= CLASS_EPHRAIM_LORD && jid <= CLASS_EIRIKA_MASTER_LORD)
            f |= F_LORD;
        if (IsTraineeJid(jid))
            f |= F_TRAINEE;
        if (IsThiefJid(jid))
            f |= F_THIEF;
        if (Rando_ClassIsMonster(jid))
            f |= F_MONSTER;
        if (k->attributes & CA_PROMOTED)
            f |= F_PROMOTED;
        if (Rando_ClassIsFlier(jid))
            f |= F_FLIER;
        if (k->attributes & CA_SUMMON)
            f |= F_SUMMON;
        if (k->baseRanks[ITYPE_STAFF])
            f |= F_STAFF;
        for (t = 0; t < 8; t++)
        {
            if (!k->baseRanks[t] || !IsAttackType(t))
                continue;
            f |= F_ATTACK;
            if (t == ITYPE_SWORD || t == ITYPE_LANCE || t == ITYPE_AXE)
                f |= F_MELEE;
            else
                f |= F_RANGE;
        }
        if (Rando_MonsterWeapon(jid, 0) != 0)
        {
            f |= F_MONSTERWPN | F_ATTACK;
            for (i = 0; i < 3; i++)
            {
                const struct ItemData *it = Rando_VanillaItem(Rando_MonsterWeapon(jid, sPickAll[i]));

                if (it == NULL)
                    continue;
                if ((it->encodedRange >> 4) <= 1)
                    f |= F_MELEE;
                if ((it->encodedRange & 0xF) >= 2)
                    f |= F_RANGE;
            }
        }
        if (!(f & F_PROMOTED) && PromotionOf(jid) != 0)
            f |= F_PROMOTES;
        sFacts[jid] = f;
    }
    for (i = 0; i < (int)(sizeof(sTwins) / sizeof(sTwins[0])); i++)
    {
        sTwin[sTwins[i][0]] = sTwins[i][1];
        sTwin[sTwins[i][1]] = sTwins[i][0];
        sFacts[sTwins[i][0]] |= F_MALE;
        sFacts[sTwins[i][1]] |= F_FEMALE;
    }
    sFactsBuilt = 1;
}

int Rando_ClassIsPromoted(int jid)
{
    return VanillaPromoted(jid);
}

int Rando_ClassIsFlier(int jid)
{
    const struct ClassData *k = Rando_VanillaClass(jid);
    const struct ClassData *ref = Rando_VanillaClass(CLASS_PEGASUS_KNIGHT);

    if (k == NULL || ref == NULL)
        return 0;
    return (k->attributes & CA_FLYER) != 0 ||
           (k->pMovCostTable[0] != NULL && k->pMovCostTable[0] == ref->pMovCostTable[0]);
}

int Rando_ClassIsMonster(int jid)
{
    return (jid >= CLASS_REVENANT && jid <= CLASS_DEMON_KING) || jid == CLASS_CYCLOPS_2 || jid == CLASS_ELDER_BAEL_2;
}

int Rando_ClassTwin(int jid)
{
    BuildFacts();
    return ValidJid(jid) ? sTwin[jid] : 0;
}

int Rando_ClassPromotions(int jid, u8 out[3])
{
    const struct ClassData *k = Rando_VanillaClass(jid);
    int n = 0, i, c;

    if (k == NULL || VanillaPromoted(jid))
        return 0;
    for (i = 0; i < 3; i++)
    {
        c = i == 0 ? (RandomPromotions() ? 0 : k->promotion) : gPromoJidLut[jid][i - 1];
        if (ValidJid(c) && VanillaPromoted(c) && (n == 0 || (c != out[0] && (n == 1 || c != out[1]))))
            out[n++] = (u8)c;
    }
    return n;
}

/* Rando_ClassPromotions, as a unit of that sex gets them (SexTwin; duplicates dropped). */
int Rando_ClassPromotionsFor(int jid, int female, u8 out[3])
{
    u8 raw[3];
    int n = Rando_ClassPromotions(jid, raw), i, j, m = 0;

    for (i = 0; i < n; i++)
    {
        int c = SexTwin(raw[i], female);

        for (j = 0; j < m && out[j] != c; j++)
            ;
        if (j == m)
            out[m++] = (u8)c;
    }
    return m;
}

int Rando_ClassBase(int jid)
{
    const struct ClassData *k = Rando_VanillaClass(jid);
    int u;

    if (k == NULL || !VanillaPromoted(jid))
        return 0;
    /* the promoted class's back pointer first (Paladin -> Cavalier), then any
       unpromoted class that promotes into it */
    if (ValidJid(k->promotion) && !VanillaPromoted(k->promotion) && IsPromotionOf(k->promotion, jid))
        return k->promotion;
    if (RandomPromotions())
    {
        /* the run's lines: never a trainee's, a special or (for another class) a lord's
           base, nor one across the monster line or of the other sex; a base whose pair
           holds jid itself before one holding its other-sex twin */
        int pass;

        BuildFacts();
        for (pass = 0; pass < 2; pass++)
            for (u = 1; u <= JID_MAX; u++)
            {
                if (Has(u, F_TRAINEE) || Has(u, F_SPECIAL) || (Has(u, F_LORD) && !Has(jid, F_LORD)) ||
                    Has(u, F_MONSTER) != Has(jid, F_MONSTER) || VanillaPromoted(u) || u >= gPromoJidLutCount ||
                    (Has(jid, F_MALE) && Has(u, F_FEMALE)) || (Has(jid, F_FEMALE) && Has(u, F_MALE)))
                    continue;
                if (pass == 0 ? (gPromoJidLut[u][0] == jid || gPromoJidLut[u][1] == jid) : IsPromotionOf(u, jid))
                    return u;
            }
    }
    else
        for (u = 1; u <= JID_MAX; u++)
            if (IsPromotionOf(u, jid))
                return u;
    if (RandomPromotions() && ValidJid(k->promotion) && !VanillaPromoted(k->promotion) &&
        Rando_VanillaClass(k->promotion) != NULL)
        return k->promotion; /* no random line leads here: the vanilla one */
    return 0;
}

/* jid belongs to the line of the class base: the class, its promotions, or (base
   promoted) the class it comes from. */
static int InFamily(int base, int jid)
{
    if (jid == base)
        return 1;
    if (!VanillaPromoted(base))
        return IsPromotionOf(base, jid);
    return Rando_ClassBase(base) == jid;
}

/* ------------------------------------------------------------------ player classes */

static int IsLordPid(int pid)
{
    return pid == CHARACTER_EIRIKA || pid == CHARACTER_EPHRAIM;
}

/* Characters the randomizer never changes, whatever the options (UFER's doNotChange). */
static int DoNotChange(int pid)
{
    switch (pid)
    {
    case CHARACTER_MYRRH: case CHARACTER_LYON_CC: case CHARACTER_LYON: case CHARACTER_LYON_FINAL:
    case CHARACTER_MORVA: case CHARACTER_FOMORTIIS: case CHARACTER_DARA: case CHARACTER_KLIMT:
        return 1;
    }
    return 0;
}

/* The playable pid keeps its vanilla class in this run (whatever is picked). */
static int PlayerSkipped(int pid)
{
    if (!Rando_IsPlayable(pid) || DoNotChange(pid))
        return 1;
    if (IsLordPid(pid) && !Rando_Opt(RFO_RANDO_LORDS))
        return 1;
    if ((pid == CHARACTER_COLM || pid == CHARACTER_RENNAC) && !Rando_Opt(RFO_RANDO_THIEVES))
        return 1;
    if (pid == CHARACTER_TETHYS && !Rando_Opt(RFO_RANDO_SPECIAL))
        return 1;
    return 0;
}

/* A class any player may get with the run's options (lords: their own lord class
   is added by PlayerFits). */
static int PlayerTarget(int jid)
{
    u32 f = Facts(jid);

    if (!(f & F_REAL) || (f & (F_SPECIAL | F_TRAINEE | F_LORD)))
        return 0;
    if ((f & F_THIEF) && !Rando_Opt(RFO_RANDO_THIEVES))
        return 0;
    if ((f & F_MONSTER) && !Rando_Opt(RFO_RANDO_MONSTERS))
        return 0;
    if (!(f & F_PROMOTED) && !(f & F_PROMOTES))
        return 0; /* Soldier: a player could never promote */
    return 1;
}

static int OwnLordClass(int pid, int jid)
{
    return (pid == CHARACTER_EIRIKA && jid == CLASS_EIRIKA_LORD) ||
           (pid == CHARACTER_EPHRAIM && jid == CLASS_EPHRAIM_LORD);
}

/* jid is a valid class for the playable pid of the given tier. */
static int PlayerFits(int pid, int jid, int promotedTier)
{
    u32 f = Facts(jid);

    if (!ValidJid(jid) || ((f & F_PROMOTED) != 0) != (promotedTier != 0))
        return 0;
    if (!PlayerTarget(jid) && !(OwnLordClass(pid, jid) && Rando_Opt(RFO_RANDO_LORDS)))
        return 0;
    switch (pid)
    {
    case CHARACTER_EIRIKA:
        return ClassUses(jid, ITYPE_SWORD) && (f & F_PROMOTES) && (f & F_ATTACK);
    case CHARACTER_EPHRAIM:
        return ClassUses(jid, ITYPE_LANCE) && (f & F_PROMOTES) && (f & F_ATTACK);
    case CHARACTER_SETH:
    case CHARACTER_GILLIAM: /* Ch1 tutorial FIGHT, adjacent: a staff class would heal */
        return (f & F_ATTACK) && (f & F_MELEE);
    case CHARACTER_ARTUR:
    case CHARACTER_NEIMI:   /* Ch3 FIGHT from two squares */
        return (f & F_ATTACK) && (f & F_RANGE);
    case CHARACTER_GARCIA:
        return (f & F_ATTACK) != 0;
    }
    return 1;
}

/* The M/F twin of jid matching the character's sex, when it exists and fits. */
static int GenderTwin(int pid, int jid, int promotedTier)
{
    const struct CharacterData *vc = Rando_VanillaChar(pid);
    int female = vc != NULL && (vc->attributes & CA_FEMALE) != 0;
    int twin = ValidJid(jid) ? sTwin[jid] : 0;

    if (twin == 0)
        return jid;
    if ((female && Has(jid, F_MALE)) || (!female && Has(jid, F_FEMALE)))
        if (PlayerFits(pid, twin, promotedTier))
            return twin;
    return jid;
}

struct ClassPool {
    u8 jid[JID_MAX + 1];
    int count;
};

static void PoolFill(struct ClassPool *p, int promotedTier)
{
    int jid;

    p->count = 0;
    for (jid = 1; jid <= JID_MAX; jid++)
        if (PlayerTarget(jid) && Has(jid, F_PROMOTED) == (promotedTier != 0))
            p->jid[p->count++] = (u8)jid;
}

static void PoolRemove(struct ClassPool *p, int jid)
{
    int i, n = 0;

    for (i = 0; i < p->count; i++)
        if (p->jid[i] != jid)
            p->jid[n++] = p->jid[i];
    p->count = n;
}

/* The candidates of pool p that fit pid (plus a lord's own class); returns how many. */
static int PoolCandidates(const struct ClassPool *p, int pid, int promotedTier, u8 *out)
{
    int i, n = 0;

    for (i = 0; i < p->count; i++)
        if (PlayerFits(pid, p->jid[i], promotedTier))
            out[n++] = p->jid[i];
    if (Rando_Opt(RFO_RANDO_LORDS) && IsLordPid(pid))
    {
        int own = pid == CHARACTER_EIRIKA ? CLASS_EIRIKA_LORD : CLASS_EPHRAIM_LORD;

        if (PlayerFits(pid, own, promotedTier))
            out[n++] = (u8)own;
    }
    return n;
}

static int LowerRank(int wexp)
{
    int lvl = Rando_RankLevelOf(wexp);

    return sLevelExp[Max(WPN_LEVEL_E, lvl - 1)];
}

/* UFER transferWeaponRanks: the character's effective vanilla ranks (class base,
   overridden by a nonzero character rank), the highest N kept (N = the new class's
   weapon types), assigned at random to those types; the last one is reused, a step
   lower each time. Dark at least D. out: the new character baseRanks. */
static void TransferRanksFrom(struct RandoRng *rng, const u8 eff[8], const struct ClassData *kn, u8 out[8])
{
    int have[8], types[8], n = 0, need = 0, i, j, v;

    memset(out, 0, 8);
    for (i = 0; i < 8; i++)
    {
        v = eff[i];
        if (v)
            have[n++] = v;
        if (kn->baseRanks[i])
            types[need++] = i;
    }
    if (need == 0)
        return;
    if (n == 0)
    {
        for (j = 0; j < need; j++)
            out[types[j]] = kn->baseRanks[types[j]];
    }
    else
    {
        /* ascending, then drop the lowest until they fit */
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && have[j - 1] > have[j]; j--)
            {
                v = have[j];
                have[j] = have[j - 1];
                have[j - 1] = v;
            }
        while (n > need)
        {
            for (i = 1; i < n; i++)
                have[i - 1] = have[i];
            n--;
        }
        for (j = 0; j < need; j++)
        {
            if (n > 1)
            {
                int k = Rando_RngInt(rng, n);

                v = have[k];
                for (i = k + 1; i < n; i++)
                    have[i - 1] = have[i];
                n--;
            }
            else
            {
                v = have[0];
                have[0] = LowerRank(have[0]);
            }
            out[types[j]] = (u8)v;
        }
    }
    for (j = 0; j < need; j++)
    {
        int t = types[j];

        if (out[t] < WPN_EXP_E)
            out[t] = WPN_EXP_E;
        if (t == ITYPE_DARK && out[t] < WPN_EXP_D)
            out[t] = WPN_EXP_D;
    }
}

static void TransferRanks(struct RandoRng *rng, const struct CharacterData *vc, const struct ClassData *ko,
    const struct ClassData *kn, u8 out[8])
{
    u8 eff[8];
    int i;

    for (i = 0; i < 8; i++)
        eff[i] = vc->baseRanks[i] ? vc->baseRanks[i] : ko->baseRanks[i];
    TransferRanksFrom(rng, eff, kn, out);
}

void Rando_TransferRanks(struct RandoRng *rng, const u8 eff[8], int newJ, u8 out[8])
{
    const struct ClassData *kn = Rando_VanillaClass(newJ);

    memset(out, 0, 8);
    if (kn != NULL)
        TransferRanksFrom(rng, eff, kn, out);
}

int Rando_PlayerFits(int pid, int jid)
{
    BuildFacts();
    return PlayerFits(pid, jid, Has(jid, F_PROMOTED));
}

void RandoClasses_SetPlayerClass(int pid, int jid)
{
    if (pid >= 1 && pid <= PID_MAX && ValidJid(jid))
        sPlayerClass[pid] = (u8)jid;
}

/* Writes the new class into the character (pid's live entry, restored from vanilla). */
static void ApplyPlayerClass(struct RandoRng *rng, int pid, int oldJ, int newJ)
{
    struct CharacterData *c = Rando_Char(pid);
    const struct CharacterData *vc = Rando_VanillaChar(pid);
    const struct ClassData *ko = Rando_VanillaClass(oldJ);
    const struct ClassData *kn = Rando_VanillaClass(newJ);
    s8 *per[6];
    int oldB[6], newB[6], cap[6], i;
    u8 ranks[8];

    if (c == NULL || vc == NULL || ko == NULL || kn == NULL)
        return;

    TransferRanks(rng, vc, ko, kn, ranks);
    memcpy(c->baseRanks, ranks, sizeof(ranks));

    /* bases: HP, Pow, Skl, Spd, Def, Res (luck is the character's alone) */
    per[0] = &c->baseHP;  oldB[0] = ko->baseHP;  newB[0] = kn->baseHP;  cap[0] = Min(kn->maxHP, STAT_HP_MAX);
    per[1] = &c->basePow; oldB[1] = ko->basePow; newB[1] = kn->basePow; cap[1] = Min(kn->maxPow, STAT_MAX);
    per[2] = &c->baseSkl; oldB[2] = ko->baseSkl; newB[2] = kn->baseSkl; cap[2] = Min(kn->maxSkl, STAT_MAX);
    per[3] = &c->baseSpd; oldB[3] = ko->baseSpd; newB[3] = kn->baseSpd; cap[3] = Min(kn->maxSpd, STAT_MAX);
    per[4] = &c->baseDef; oldB[4] = ko->baseDef; newB[4] = kn->baseDef; cap[4] = Min(kn->maxDef, STAT_MAX);
    per[5] = &c->baseRes; oldB[5] = ko->baseRes; newB[5] = kn->baseRes; cap[5] = Min(kn->maxRes, STAT_MAX);
    for (i = 0; i < 6; i++)
    {
        int v = *per[i];
        int lo = (i == 0 ? 1 : 0) - newB[i];
        int hi = cap[i] - newB[i];

        if (Rando_Opt(RFO_RANDO_CLASS_BASES) == 0)
            v += oldB[i] - newB[i]; /* retain final */
        if (hi < lo)
            hi = lo;
        *per[i] = (s8)Clamp(v, lo, hi);
    }

    if (Rando_Opt(RFO_RANDO_CLASS_GROWTHS) == 1)
    {
        u8 *g[7];
        int go[7], gn[7];

        g[0] = &c->growthHP;  go[0] = ko->growthHP;  gn[0] = kn->growthHP;
        g[1] = &c->growthPow; go[1] = ko->growthPow; gn[1] = kn->growthPow;
        g[2] = &c->growthSkl; go[2] = ko->growthSkl; gn[2] = kn->growthSkl;
        g[3] = &c->growthSpd; go[3] = ko->growthSpd; gn[3] = kn->growthSpd;
        g[4] = &c->growthDef; go[4] = ko->growthDef; gn[4] = kn->growthDef;
        g[5] = &c->growthRes; go[5] = ko->growthRes; gn[5] = kn->growthRes;
        g[6] = &c->growthLck; go[6] = ko->growthLck; gn[6] = kn->growthLck;
        for (i = 0; i < 7; i++)
            *g[i] = (u8)Clamp(gn[i] + ((int)*g[i] - go[i]), 0, 255);
    }

    if (pid == CHARACTER_EIRIKA)
        c->attributes |= CA_LOCK_4; /* Rapier, Sieglinde */
    if (pid == CHARACTER_EPHRAIM)
        c->attributes |= CA_LOCK_5; /* Reginleif, Siegmund */

    c->defaultClass = (u8)newJ;
    sPlayerClass[pid] = (u8)newJ;
}

void RandoClasses_Apply(void)
{
    struct ClassPool pool[2];
    struct RandoRng rng;
    u8 cands[JID_MAX + 1];
    int pid;

    memset(sPlayerClass, 0, sizeof(sPlayerClass));
    memset(&sLast, 0, sizeof(sLast));
    BuildFacts();
    if (!gRandoRunOn || !sFactsBuilt || !Rando_Opt(RFO_RANDO_CLASSES))
        return;

    PoolFill(&pool[0], 0);
    PoolFill(&pool[1], 1);
    Rando_RngSeed(&rng, RANDO_SALT_CLASSES);
    for (pid = 1; pid <= PID_MAX; pid++)
    {
        const struct CharacterData *vc;
        int oldJ, tier, n, pick;

        if (PlayerSkipped(pid))
            continue;
        vc = Rando_VanillaChar(pid);
        oldJ = vc->defaultClass;
        if (!ValidJid(oldJ))
            continue;
        tier = VanillaPromoted(oldJ) ? 1 : 0;

        n = PoolCandidates(&pool[tier], pid, tier, cands);
        if (n == 0 && Rando_Opt(RFO_RANDO_EVENLY))
        {
            PoolFill(&pool[tier], tier); /* the even pool ran dry for this character: refill */
            n = PoolCandidates(&pool[tier], pid, tier, cands);
        }
        if (n == 0)
            continue; /* nothing fits: the vanilla class */
        pick = cands[Rando_RngInt(&rng, n)];
        if (Rando_Opt(RFO_RANDO_EVENLY))
        {
            PoolRemove(&pool[tier], pick);
            if (sTwin[pick])
                PoolRemove(&pool[tier], sTwin[pick]);
        }
        if (Rando_Opt(RFO_RANDO_GENDER))
            pick = GenderTwin(pid, pick, tier);
        if (pick == oldJ)
            continue;
        ApplyPlayerClass(&rng, pid, oldJ, pick);
    }
}

int RandoClasses_PlayerClass(int pid)
{
    const struct CharacterData *vc = Rando_VanillaChar(pid);

    if (vc == NULL)
        return 0;
    if (!gRandoRunOn || pid > PID_MAX || sPlayerClass[pid] == 0)
        return vc->defaultClass; /* set by the classes step or by recruitment, each run */
    return sPlayerClass[pid];
}

/* ---- versions 11-14: the players' foreign rolls ("FE7/FE6 classes", RANDO_VERSION_THREE) */

static u8 sPlayerFe8[PID_MAX + 1]; /* the class of the run before its foreign roll fired (0: none fired) */

int RandoClasses_PlayerFe8Class(int pid)
{
    return pid >= 1 && pid <= PID_MAX ? sPlayerFe8[pid] : 0;
}

/* The foreign counterpart of a player's class of the run, for a unit of that sex (0: none). Each is the class whose
   save stand-in (reforged_fe7.c Reforged_Fe7Fallback, which keys on the unit's sex) is that class, so a run without
   the ROM plays the same unit; a counterpart whose stand-in for this unit is another class gives none (with Match
   gender Off a woman can be in a man's class: a male General's Armor Lord would save as General F, a male Sage's
   Archsage as Sage F, a male Myrmidon's Lion Lord as Myrmidon F). Archer -> Nomad (FE7, by the class's sex: either
   Nomad's stand-in is the Archer of its own sex, whoever holds it), Thief -> Thief F (FE6; with Match gender a woman only), Myrmidon -> Lion Lord (FE6;
   with Match gender a man only: Roy's art), Swordmaster -> Blade Lord (FE7, Lyn's art: a woman's first) or Master
   Lord (FE6, Roy's: a man's first; the other when the run's version names only that one's ROM, RunSwordLords), General -> Armor Lord (FE7), Ranger -> Nomad Trooper (FE6, by the class's sex, as the Nomad), Sage ->
   Archsage and Druid -> Dark Druid (FE7), Dancer -> Bard (FE7), Myrrh's Manakete -> Divine Dragon (FE6). */
static int PlayerForeignForSex(int cls, int female)
{
    int gender = Rando_Opt(RFO_RANDO_GENDER);

    switch (cls)
    {
    case CLASS_ARCHER: case CLASS_ARCHER_F:
        return cls == CLASS_ARCHER_F ? FJ_NOMAD_F : FJ_NOMAD;
    case CLASS_THIEF:
        return !gender || female ? FJ_THIEF_F : 0;
    case CLASS_MYRMIDON: case CLASS_MYRMIDON_F:
        return !gender || !female ? FJ_LION_LORD : 0;
    case CLASS_SWORDMASTER: case CLASS_SWORDMASTER_F:
    {
        /* the run's version decides (RunSwordLords); a lord that is not loaded now leaves the stand-in */
        int order[2];

        return RunSwordLords(female, order) > 0 ? order[0] : 0;
    }
    case CLASS_GENERAL: case CLASS_GENERAL_F:
        return FJ_ARMOR_LORD;
    case CLASS_RANGER: case CLASS_RANGER_F:
        return cls == CLASS_RANGER_F ? FJ_TROOPER_F : FJ_TROOPER;
    case CLASS_SAGE: case CLASS_SAGE_F:
        return FJ_ARCHSAGE;
    case CLASS_DRUID: case CLASS_DRUID_F:
        return FJ_DARK_DRUID;
    case CLASS_DANCER:
        return FJ_BARD;
    case CLASS_MANAKETE_MYRRH:
        return FJ_DIVINE;
    }
    return 0;
}

static int PlayerForeignFor(int cls, int female)
{
    int target = PlayerForeignForSex(cls, female);

    return target != 0 && Reforged_Fe7Fallback(target, female) == cls ? target : 0; /* the stand-in rule, exact */
}

/* Every weapon kind class cls uses, target uses too (a unit keeps every rank it has). */
static int UsesAllOf(int target, int cls)
{
    int t;

    for (t = 0; t < 8; t++)
        if (ClassUses(cls, t) && !ClassUses(target, t))
            return 0;
    return 1;
}

/* Makes the foreign class target pid's class of the run in place of cls: the character's default class (the unit
   hooks load it in that class), its ranks for kinds the class lacks dropped (none: UsesAllOf), and with Class bases
   Final its personal bases moved by the class's difference (retain final, clamped as ApplyPlayerClass). */
static void ApplyForeignPlayer(int pid, int cls, int target)
{
    struct CharacterData *c = Rando_Char(pid);
    const struct ClassData *ko = GetClassData(cls), *kn = GetClassData(target);
    s8 *per[6];
    int oldB[6], newB[6], cap[6], i;

    if (c == NULL || ko == NULL || kn == NULL)
        return;
    per[0] = &c->baseHP;  oldB[0] = ko->baseHP;  newB[0] = kn->baseHP;  cap[0] = Min(kn->maxHP, STAT_HP_MAX);
    per[1] = &c->basePow; oldB[1] = ko->basePow; newB[1] = kn->basePow; cap[1] = Min(kn->maxPow, STAT_MAX);
    per[2] = &c->baseSkl; oldB[2] = ko->baseSkl; newB[2] = kn->baseSkl; cap[2] = Min(kn->maxSkl, STAT_MAX);
    per[3] = &c->baseSpd; oldB[3] = ko->baseSpd; newB[3] = kn->baseSpd; cap[3] = Min(kn->maxSpd, STAT_MAX);
    per[4] = &c->baseDef; oldB[4] = ko->baseDef; newB[4] = kn->baseDef; cap[4] = Min(kn->maxDef, STAT_MAX);
    per[5] = &c->baseRes; oldB[5] = ko->baseRes; newB[5] = kn->baseRes; cap[5] = Min(kn->maxRes, STAT_MAX);
    for (i = 0; i < 6; i++)
    {
        int v = *per[i];
        int lo = (i == 0 ? 1 : 0) - newB[i];
        int hi = cap[i] - newB[i];

        if (Rando_Opt(RFO_RANDO_CLASS_BASES) == 0)
            v += oldB[i] - newB[i]; /* retain final */
        if (hi < lo)
            hi = lo;
        *per[i] = (s8)Clamp(v, lo, hi);
    }
    {
        /* the body keeps its size (Con: the final value, between 3 and the class's cap) */
        int total = c->baseCon + ko->baseCon, hi = Min(kn->maxCon, STAT_MAX);

        if (hi < 3)
            hi = 3;
        c->baseCon = (s8)(Clamp(total, 3, hi) - kn->baseCon);
    }
    for (i = 0; i < 8; i++)
        if (c->baseRanks[i] && !ClassUses(target, i))
            c->baseRanks[i] = 0;
    c->defaultClass = (u8)target;
    sPlayerClass[pid] = (u8)target;
    sPlayerFe8[pid] = (u8)cls;
}

/* The last step of the pipeline (reforged_rando.c Rando_Start, after the palettes, so nothing a version 10 seed
   gives moves). Any version 11-14 run: the twins keep their prf weapons whatever class they promote into (CA_LOCK_4,
   CA_LOCK_5 on the character: a third choice may be no lord class). With Player classes and the run's foreign ROMs:
   each playable pid but the twins whose class of the run (after Recruitment) has a foreign counterpart (PlayerForeignFor:
   one whose save stand-in for that unit is that class) that uses every weapon kind it does rolls on its own salt (keyed on the pid and that class), 1 in
   RANDO_THREE_PLAYER_ODDS, the Bard and the Divine Dragon 1 in RANDO_THREE_SPECIAL_ODDS; a class that is not loaded
   now leaves the class of the run (its stand-in). */
void RandoClasses_ApplyForeign(void)
{
    struct CharacterData *c;
    int pid;

    memset(sPlayerFe8, 0, sizeof(sPlayerFe8));
    if (!Rando_ThreeRun())
        return;
    if ((c = Rando_Char(CHARACTER_EIRIKA)) != NULL)
        c->attributes |= CA_LOCK_4;
    if ((c = Rando_Char(CHARACTER_EPHRAIM)) != NULL)
        c->attributes |= CA_LOCK_5;
    if (!Rando_ThreeRoms() || !Rando_Opt(RFO_RANDO_CLASSES))
        return;
    BuildFacts();
    for (pid = 1; pid <= PID_MAX; pid++)
    {
        int cls, target, odds, female;

        c = Rando_Char(pid);
        if (!Rando_IsPlayable(pid) || c == NULL || IsLordPid(pid))
            continue; /* the twins meet their foreign lords at promotion (Rando_PromoList), never as their class */
        cls = RandoClasses_PlayerClass(pid);
        female = (c->attributes & CA_FEMALE) != 0;
        target = ValidJid(cls) ? PlayerForeignFor(cls, female) : 0;
        if (target == 0 || !(Rando_ThreeRoms() & ForeignRom(target)))
            continue;
        odds = (target == FJ_BARD || target == FJ_DIVINE) ? RANDO_THREE_SPECIAL_ODDS : RANDO_THREE_PLAYER_ODDS;
        if (Rando_HashInt(Rando_Hash(RANDO_SALT_THREE + 3, (u32)pid, (u32)cls, 0), odds) != 0)
            continue;
        if (!Fe7Loaded(target) || !UsesAllOf(target, cls))
        {
            if (!sQuiet && !sQuietApply)
                PortLog("[rando] player 0x%02X: foreign class 0x%02X rolled for class 0x%02X, but it is %s: class 0x%02X "
                    "kept\n", pid, target, cls, Fe7Loaded(target) ? "missing a weapon kind" : "not loaded", cls);
            continue;
        }
        ApplyForeignPlayer(pid, cls, target);
        if (!sQuiet && !sQuietApply)
            PortLog("[rando] player 0x%02X: foreign class 0x%02X instead of 0x%02X\n", pid, target, cls);
    }
}

/* The class a playable pid's definition gives in this run. */
static int PlayerDefClass(int pid, int defClass)
{
    const struct CharacterData *vc = Rando_VanillaChar(pid);
    int now = RandoClasses_PlayerClass(pid);

    if (vc == NULL || now == vc->defaultClass)
        return defClass ? defClass : now; /* not randomized */
    if (defClass == 0 || defClass == vc->defaultClass)
        return now;
    if (IsForeignJid(now))
    {
        /* versions 11-14: a player in a foreign class (RandoClasses_ApplyForeign); a promoted definition of an
           unpromoted one gives its first promotion (Nomad: Ranger, Lion Lord: Master Lord, Thief F: Assassin F) */
        const struct ClassData *k = GetClassData(now);

        if (ValidJid(defClass) && VanillaPromoted(defClass) && k != NULL && !(k->attributes & CA_PROMOTED) &&
            now < gPromoJidLutCount && gPromoJidLut[now][0] != 0)
            return gPromoJidLut[now][0];
        return now;
    }
    if (!ValidJid(defClass) || InFamily(now, defClass))
        return defClass; /* already the new class's line (LOAD3 of a promoted unit) */
    if (Has(defClass, F_SPECIAL) || !Has(defClass, F_REAL))
        return defClass; /* a cutscene's special class */
    if (VanillaPromoted(defClass) && !VanillaPromoted(now))
    {
        int p = PromotionOf(now);
        const struct CharacterData *lc = Rando_Char(pid);

        if (p)
            p = SexTwin(p, ((lc != NULL ? lc->attributes : vc->attributes) & CA_FEMALE) != 0);
        return p ? p : now;
    }
    if (!VanillaPromoted(defClass) && VanillaPromoted(now))
    {
        int b = Rando_ClassBase(now);

        return b ? b : now;
    }
    return now;
}

/* ------------------------------------------------------------------ bosses and enemies */

static int IsBossPid(int pid)
{
    const struct CharacterData *vc = Rando_VanillaChar(pid);

    return vc != NULL && (vc->attributes & CA_BOSS) != 0 && !Rando_IsPlayable(pid);
}

/* A class a boss or enemy may be randomized away from (or into). */
static int EnemySource(int jid)
{
    u32 f = Facts(jid);

    return (f & F_REAL) && !(f & (F_SPECIAL | F_LORD | F_TRAINEE | F_THIEF | F_SUMMON));
}

static int EnemyTarget(int jid, int orig)
{
    u32 f = Facts(jid), o = Facts(orig);

    if (!EnemySource(jid) || !(f & F_ATTACK) || jid == orig || (ValidJid(orig) && jid == sTwin[orig]))
        return 0;
    return ((f & F_PROMOTED) == (o & F_PROMOTED)) && ((f & F_MONSTER) == (o & F_MONSTER));
}

static int EnemyCandidates(int orig, int fliersOnly, u8 *out)
{
    int jid, n = 0;

    for (jid = 1; jid <= JID_MAX; jid++)
        if (EnemyTarget(jid, orig) && (!fliersOnly || Has(jid, F_FLIER)))
            out[n++] = (u8)jid;
    return n;
}

/* The boss pid's class in this run for its vanilla class orig (one class all game);
   try_ > 0: version 4's further picks when the first does not fit the map. */
static int BossClassFor(int pid, int orig, int try_)
{
    const struct CharacterData *vc = Rando_VanillaChar(pid);
    u8 cands[JID_MAX + 1];
    int n, pick, twin, female;

    if (!Rando_Opt(RFO_RANDO_BOSSES) || vc == NULL || DoNotChange(pid) || !EnemySource(orig))
        return orig;
    n = EnemyCandidates(orig, Has(orig, F_FLIER), cands);
    if (Has(orig, F_FLIER) == 0)
    {
        int i, m = 0;

        for (i = 0; i < n; i++) /* bosses: non-fliers stay non-fliers */
            if (!Has(cands[i], F_FLIER))
                cands[m++] = cands[i];
        n = m;
    }
    if (n == 0)
        return orig;
    pick = cands[Rando_HashInt(Rando_Hash(RANDO_SALT_BOSSES, (u32)pid, (u32)orig, (u32)try_), n)];
    /* UFER's loose gender rule for bosses: the twin of the boss's sex */
    female = (vc->attributes & CA_FEMALE) != 0;
    twin = sTwin[pick];
    if (twin && ((female && Has(pick, F_MALE)) || (!female && Has(pick, F_FEMALE))) && EnemyTarget(twin, orig) &&
        Has(twin, F_FLIER) == Has(pick, F_FLIER))
        pick = twin;
    return pick;
}

/* A generic enemy's class, keyed on its content. */
static int EnemyClassFor(int orig, u32 a, u32 b, u32 c)
{
    u8 cands[JID_MAX + 1];
    u32 h;
    int n, pick;

    if (!EnemySource(orig))
        return orig;
    n = EnemyCandidates(orig, Has(orig, F_FLIER), cands);
    if (n == 0)
        return orig;
    h = Rando_Hash(RANDO_SALT_MINIONS, a, b, c);
    pick = cands[Rando_HashInt(h, n)];
    if (!Has(orig, F_FLIER) && Has(pick, F_FLIER))
        pick = cands[Rando_HashInt(Mix(h, 1), n)]; /* a non-flier rolling a flier rolls once more */
    return pick;
}

/* AI B scripts that pillage villages (cp_data.c gAi2ScriptTable 0x04, 0x05, 0x11). */
static int LootsVillages(const struct UnitDefinition *d)
{
    int ai = d->ai[UDEF_AIIDX_AI_B];

    return ai == 0x04 || ai == 0x05 || ai == 0x11;
}

/* Every square the unit stands on or walks to (its tile, each REDA waypoint) that the
   old class can enter must be enterable by the new one (gBmMapTerrain, the class's
   standard movement table, as MuCtr_GenerateMoveScript uses; -1 is impassable). */
static int SquareOk(const s8 *newT, const s8 *oldT, int x, int y)
{
    int terrain;

    if (x < 0 || y < 0 || x >= gBmMapSize.x || y >= gBmMapSize.y || gBmMapTerrain[y] == NULL)
        return 1;
    terrain = gBmMapTerrain[y][x];
    if (terrain >= TERRAIN_COUNT)
        return 1;
    return oldT[terrain] < 0 || newT[terrain] >= 0;
}

static int TerrainOk(int newJ, int oldJ, const struct UnitDefinition *d)
{
    const struct ClassData *kn = MapClass(newJ), *ko = MapClass(oldJ);
    const struct REDA *redas = (const struct REDA *)d->redas;
    const s8 *newT, *oldT;
    int i;

    if (gBmMapTerrain == NULL || gBmMapSize.x <= 0 || gBmMapSize.y <= 0 || kn == NULL || ko == NULL)
        return 1; /* no map loaded: nothing to check against */
    newT = kn->pMovCostTable[0];
    oldT = ko->pMovCostTable[0];
    if (newT == NULL || oldT == NULL)
        return newT != NULL || oldT == NULL;
    if (!SquareOk(newT, oldT, d->xPosition, d->yPosition))
        return 0;
    for (i = 0; redas != NULL && i < (int)d->redaCount; i++)
        if (!SquareOk(newT, oldT, redas[i].x, redas[i].y))
            return 0;
    return 1;
}

/* Randomizer version 2 (RANDO_VERSION_REACH, the user's request of 2026-11-03: units
   randomized onto mountains and the like must not turn into duds). TerrainOk only asks
   whether the new class can stand on the unit's squares. A class that can stand there can
   still be a dud: an archer on a mountain ringed by peaks never leaves, and one on a
   mountain range or a horseman in sand crawls a square a turn. ReachOk asks how far the
   unit gets: from where it ends up (its last REDA waypoint, or its square), the squares
   each class can walk to in REACH_TURNS turns with its own terrain costs (the standard
   table; -1 or more than a turn's points is impassable; other units ignored) and the same
   REACH_MOVE points a turn for both, so a slower class is not refused for being slow, only
   for the terrain. The new class must reach at least REACH_PERCENT as many squares.
   The terrain is the chapter's own map as it is unpacked (OrigTerrain), never the live
   one: a reinforcement measured after the player opened a door or broke a wall would
   otherwise get another class from the same seed (review rv-v113 "reach"). */
char *getenv(const char *name);

#define REACH_MAX (64 * 64)
#define REACH_PERCENT 50
#define REACH_PERCENT_V4 75 /* RANDO_VERSION_MAPFIT */
#define REACH_TRIES 8
#define REACH_TURNS 3
#define REACH_MOVE 5
#define REACH_BUCKETS (REACH_TURNS * 32)

static u8 sReachBest[REACH_MAX];          /* (turn << 5) | points used; 0xFF: not reached */
static u16 sReachNode[REACH_MAX * 4 + 1]; /* bucket lists: the square, */
static u16 sReachNext[REACH_MAX * 4 + 1]; /* the next entry (0: none; entries count from 1) */
static u16 sReachHead[REACH_BUCKETS];

/* The chapter's map as GetChapterMapPointer gives it (bmmap.c UnpackChapterMap: w, h, then
   the tiles), unpacked here once per map: gBmMapBuffer is an overlay other screens reuse,
   and gBmMapBaseTiles carries the map changes. */
#define ORIG_MAP_WORDS (1 + 128 * 128)
static u16 sOrigMap[ORIG_MAP_WORDS];
static const void *sOrigMapSrc;

/* 1 when sOrigMap holds the current chapter's map, its size gBmMapSize. */
static int OrigMapReady(void)
{
    const void *src = GetChapterMapPointer(gPlaySt.chapterIndex);

    if (src == NULL || gTilesetTerrainLookup == NULL)
        return 0;
    if (src != sOrigMapSrc)
    {
        Decompress(src, sOrigMap);
        sOrigMapSrc = src;
    }
    return ((u8 *)sOrigMap)[0] == gBmMapSize.x && ((u8 *)sOrigMap)[1] == gBmMapSize.y;
}

static int OrigTerrain(int x, int y)
{
    return gTilesetTerrainLookup[sOrigMap[1 + y * gBmMapSize.x + x] >> 2];
}

/* The squares the cost table t walks to from (x0, y0) in REACH_TURNS turns of REACH_MOVE
   points (the start counts): a Dijkstra on (turn, points used), which a bucket queue keeps
   in order (every step costs at least 1). -1 when the map does not fit. */
static int ReachArea(const s8 *t, int x0, int y0)
{
    static const s8 dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
    int w = gBmMapSize.x, h = gBmMapSize.y, used = 0, n = 0, b, k;

    if (w <= 0 || h <= 0 || w * h > REACH_MAX)
        return -1;
    if (x0 < 0 || y0 < 0 || x0 >= w || y0 >= h)
        return 0;
    memset(sReachBest, 0xFF, (size_t)(w * h));
    memset(sReachHead, 0, sizeof(sReachHead));
    sReachBest[y0 * w + x0] = 0;
    sReachNode[++used] = (u16)(y0 * w + x0);
    sReachNext[used] = 0;
    sReachHead[0] = (u16)used;
    for (b = 0; b < REACH_BUCKETS; b++)
    {
        while (sReachHead[b] != 0)
        {
            int e = sReachHead[b], at = sReachNode[e], x = at % w, y = at / w;

            sReachHead[b] = sReachNext[e];
            if (sReachBest[at] != b)
                continue; /* a later, better entry settled it */
            n++;
            for (k = 0; k < 4; k++)
            {
                int nx = x + dx[k], ny = y + dy[k], terrain, c, turn = b >> 5, pts = b & 31, v;

                if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                    continue;
                terrain = OrigTerrain(nx, ny);
                if (terrain >= TERRAIN_COUNT)
                    continue;
                c = t[terrain];
                if (c < 0 || c > REACH_MOVE)
                    continue;
                if (c == 0)
                    c = 1;
                if (pts + c <= REACH_MOVE)
                    v = (turn << 5) | (pts + c);
                else
                    v = ((turn + 1) << 5) | c;
                if ((v >> 5) >= REACH_TURNS || v >= sReachBest[ny * w + nx] || used >= REACH_MAX * 4)
                    continue;
                sReachBest[ny * w + nx] = (u8)v;
                sReachNode[++used] = (u16)(ny * w + nx);
                sReachNext[used] = sReachHead[v];
                sReachHead[v] = (u16)used;
            }
        }
    }
    return n;
}

/* Where the unit ends up: its last REDA waypoint, or its square. */
static void UnitHome(const struct UnitDefinition *d, int *x, int *y)
{
    const struct REDA *redas = (const struct REDA *)d->redas;

    *x = d->xPosition;
    *y = d->yPosition;
    if (redas != NULL && d->redaCount > 0)
    {
        *x = redas[d->redaCount - 1].x;
        *y = redas[d->redaCount - 1].y;
    }
}

/* 1 when newJ gets to at least REACH_PERCENT as many squares as oldJ (above); *got / *of:
   the two counts. */
static int ReachOk(int newJ, int oldJ, const struct UnitDefinition *d, int *got, int *of)
{
    const struct ClassData *kn = MapClass(newJ), *ko = MapClass(oldJ);
    int x, y, nOld, nNew;

    *got = *of = 0;
    if (gBmMapSize.x <= 0 || gBmMapSize.y <= 0 || kn == NULL || ko == NULL || kn->pMovCostTable[0] == NULL ||
        ko->pMovCostTable[0] == NULL || !OrigMapReady())
        return 1;
    UnitHome(d, &x, &y);
    nOld = ReachArea(ko->pMovCostTable[0], x, y);
    nNew = ReachArea(kn->pMovCostTable[0], x, y);
    if (nOld <= 0 || nNew < 0)
        return 1;
    *got = nNew;
    *of = nOld;
    return nNew * 100 >= nOld * (gRandoRun.version >= RANDO_VERSION_MAPFIT ? REACH_PERCENT_V4 : REACH_PERCENT);
}

/* Version 4 (RANDO_VERSION_MAPFIT, the user's report of 2026-11-04: Chapter 2's brigand
   boss made a Mage on its mountain, 69% of the reach): the new class crosses the terrain
   of the square the unit ends up on (UnitHome) at no more cost than the old class, so a
   unit set on a mountain, in a forest or in sand keeps a class that moves there as well.
   *terrain, *costNew, *costOld: what was compared. */
static int HomeTerrainOk(int newJ, int oldJ, const struct UnitDefinition *d, int *terrain, int *costNew,
    int *costOld)
{
    const struct ClassData *kn = MapClass(newJ), *ko = MapClass(oldJ);
    int x, y, cn, co;

    *terrain = *costNew = *costOld = 0;
    if (gBmMapSize.x <= 0 || gBmMapSize.y <= 0 || kn == NULL || ko == NULL || kn->pMovCostTable[0] == NULL ||
        ko->pMovCostTable[0] == NULL || !OrigMapReady())
        return 1;
    UnitHome(d, &x, &y);
    if (x < 0 || y < 0 || x >= gBmMapSize.x || y >= gBmMapSize.y)
        return 1;
    *terrain = OrigTerrain(x, y);
    if (*terrain >= TERRAIN_COUNT)
        return 1;
    cn = kn->pMovCostTable[0][*terrain];
    co = ko->pMovCostTable[0][*terrain];
    *costNew = cn;
    *costOld = co;
    if (co <= 0)
        return 1; /* the old class cannot cross it either (it stands there by script) */
    return cn > 0 && cn <= co;
}

/* A new class for the unit is kept when it can stand and walk where it must (TerrainOk)
   and, in a version 2 run, can leave its square (ReachOk); in a version 4 run it also
   crosses its own square's terrain as well as the old class (HomeTerrainOk). */
static int ClassFitsMap(int cls, int orig, const struct UnitDefinition *d, const char *who, int pid)
{
    int got, of;

    if (!TerrainOk(cls, orig, d))
        return 0;
    if (gRandoRun.version < RANDO_VERSION_REACH)
        return 1;
    {
        static int logAll = -1;
        int ok;

        if (logAll < 0)
            logAll = getenv("FE8PC_REACH_LOG") != NULL; /* test switch: every measurement */
        ok = ReachOk(cls, orig, d, &got, &of);
        if (ok && gRandoRun.version >= RANDO_VERSION_MAPFIT)
        {
            int terrain, cn, co;

            if (!HomeTerrainOk(cls, orig, d, &terrain, &cn, &co))
            {
                if (!sQuiet)
                    PortLog("[rando] %s 0x%02X (chapter %d) at %d,%d: class 0x%02X would be slow on its square "
                        "(terrain 0x%02X costs it %d, class 0x%02X %d): not used\n", who, pid, Chapter(),
                        d->xPosition, d->yPosition, cls, terrain, cn, orig, co);
                return 0;
            }
        }
        if (logAll)
            PortLog("[rando] reach %s 0x%02X (chapter %d) at %d,%d: class 0x%02X reaches %d of %d (class 0x%02X)%s\n",
                who, pid, Chapter(), d->xPosition, d->yPosition, cls, got, of, orig,
                OrigMapReady() ? "" : " (no map)");
        if (ok)
            return 1;
    }
    {
        PortLog("[rando] %s 0x%02X (chapter %d) at %d,%d: class 0x%02X would be stuck (reaches %d squares in "
            "%d turns, class 0x%02X %d): not used\n", who, pid, Chapter(), d->xPosition, d->yPosition, cls,
            got, REACH_TURNS, orig, of);
        return 0;
    }
}

/* ------------------------------------------------------------------ the unit hooks */

/* The first word of a unit's content key: the chapter it loads in, pid, class. */
static u32 UnitKeyA(int chapter, int pid, int orig)
{
    return ((u32)(chapter & 0xFF) << 16) | ((u32)(pid & 0xFF) << 8) | (u32)(orig & 0xFF);
}

static int FloorMonsterFor(int orig, u32 h); /* "Tower & Ruins" (below) */
static int StoryChapter(int ch);           /* the enemy options (below) */

/* Version 10's FE7 bosses and elites ("FE7 classes", RANDO_VERSION_FE7): from story chapter 15 on
   (StoryChapter: both routes' Chapter 15 is 16, the final chapter 23; the Tower and the Ruins hold
   only monsters, the skirmishes are not late), a boss or regular enemy whose class of the run (the
   version 8 pick, after its map checks) is a Sage, Bishop (Archsage) or Druid (Dark Druid), either
   sex, rolls once more on its own salt: bosses 1 in RANDO_FE7_BOSS_ODDS (keyed on the boss and that
   class, so a boss is the same in every late chapter), enemies 1 in RANDO_FE7_ELITE_ODDS (keyed on
   the unit as its class is). The FE7 class moves as its stand-in (the Sage's and Druid's magic
   movement tables, reforged_fe7.c), which must pass the map checks against the vanilla class. The
   FE7 class not loaded now: the version 8 class, so a run without the ROM plays version 8's units. */
#define FE7_FIRST_STORY 16

static int Fe7Target(int cls)
{
    switch (cls)
    {
    case CLASS_SAGE: case CLASS_SAGE_F: case CLASS_BISHOP: case CLASS_BISHOP_F:
        return REFORGED_FE7_JID_ARCHSAGE;
    case CLASS_DRUID: case CLASS_DRUID_F:
        return REFORGED_FE7_JID_DARK_DRUID;
    }
    return 0;
}

/* The FE7 class for the unit sLast describes (a boss or a regular enemy whose class of the run is
   cls), or 0. */
static int Fe7EnemyClass(const struct UnitDefinition *d, int pid, int orig, int cls)
{
    int target = Fe7Target(cls), boss = sLast.kind == UK_BOSS, stand;
    u32 h;

    if (target == 0 || !Rando_Fe7Run() || StoryChapter(Chapter()) < FE7_FIRST_STORY)
        return 0;
    if (boss)
    {
        if (!Rando_Opt(RFO_RANDO_BOSSES) || DoNotChange(pid))
            return 0;
        h = Rando_Hash(RANDO_SALT_FE7 + 1, (u32)pid, (u32)cls, 0);
    }
    else
        h = Rando_Hash(RANDO_SALT_FE7 + 2, sLast.a, sLast.b, sLast.c);
    if (Rando_HashInt(h, boss ? RANDO_FE7_BOSS_ODDS : RANDO_FE7_ELITE_ODDS) != 0)
        return 0;
    if (!Fe7Loaded(target))
    {
        if (!sQuiet)
            PortLog("[rando] %s 0x%02X (chapter %d): FE7 class 0x%02X rolled, but the FE7 classes are not loaded: "
                "class 0x%02X kept\n", boss ? "boss" : "enemy", pid, Chapter(), target, cls);
        return 0;
    }
    stand = Fe7Stand(target);
    if (stand != cls && !ClassFitsMap(stand, orig, d, boss ? "boss" : "enemy", pid))
        return 0;
    if (!sQuiet)
        PortLog("[rando] %s 0x%02X (chapter %d) at %d,%d: FE7 class 0x%02X instead of 0x%02X\n", boss ? "boss" : "enemy",
            pid, Chapter(), d->xPosition, d->yPosition, target, cls);
    return target;
}

/* Versions 11-14's late bosses and enemies ("FE7/FE6 classes", RANDO_VERSION_THREE): from story chapter 15 on, as
   version 10's FE7 roll (and only where it did not fire), a boss or regular enemy whose class of the run (after its
   map checks) has a foreign counterpart rolls on its own salt: a boss in a promoted human sword class (no flier)
   becomes FE6's King and a General boss FE7's Armor Lord, 1 in RANDO_THREE_BOSS_ODDS (keyed on the boss and that
   class, as version 10's); a regular Archer becomes FE7's Nomad and a Sniper or Ranger FE6's Nomad Trooper (the
   class's sex), 1 in RANDO_THREE_ENEMY_ODDS; an enemy Thief (village looters too) FE6's Thief F 1 in
   RANDO_THREE_THIEF_ODDS, with Match gender only one whose character is female (FE6's art is a woman's). Both the
   foreign class (as it plays, with its own movement: FE7's Nomad moves as a horseman, not as its stand-in Archer)
   and its stand-in (reforged_fe7.c Reforged_Fe7Fallback: what a run without the ROM plays) must pass the map
   checks against the vanilla class; its ROM must be in the run's version and loaded now, else the class of the
   run. */
static int ThreeEnemyClass(const struct UnitDefinition *d, int pid, int orig, int cls)
{
    const struct CharacterData *vc = Rando_VanillaChar(pid);
    int boss = sLast.kind == UK_BOSS, target = 0, odds = RANDO_THREE_ENEMY_ODDS, female, stand;
    u32 h;

    if (!Rando_ThreeRoms() || StoryChapter(Chapter()) < FE7_FIRST_STORY || !ValidJid(cls) || d->genMonster)
        return 0;
    female = vc != NULL && (vc->attributes & CA_FEMALE) != 0;
    if (boss)
    {
        if (!Rando_Opt(RFO_RANDO_BOSSES) || DoNotChange(pid))
            return 0;
        if (cls == CLASS_GENERAL || cls == CLASS_GENERAL_F)
            target = FJ_ARMOR_LORD;
        else if (Has(cls, F_PROMOTED) && !Has(cls, F_MONSTER | F_FLIER | F_SPECIAL) && ClassUses(cls, ITYPE_SWORD))
            target = FJ_KING;
        odds = RANDO_THREE_BOSS_ODDS;
        h = Rando_Hash(RANDO_SALT_THREE + 4, (u32)pid, (u32)cls, 0);
    }
    else
    {
        if (!Rando_Opt(RFO_RANDO_ENEMIES) || DoNotChange(pid) || IsBossPid(pid) || d->allegiance != FACTION_ID_RED)
            return 0;
        switch (cls)
        {
        case CLASS_ARCHER:   target = FJ_NOMAD;     break;
        case CLASS_ARCHER_F: target = FJ_NOMAD_F;   break;
        case CLASS_SNIPER: case CLASS_RANGER:     target = FJ_TROOPER;   break;
        case CLASS_SNIPER_F: case CLASS_RANGER_F: target = FJ_TROOPER_F; break;
        case CLASS_THIEF:
            if (!Rando_Opt(RFO_RANDO_GENDER) || female)
                target = FJ_THIEF_F;
            odds = RANDO_THREE_THIEF_ODDS;
            break;
        }
        h = Rando_Hash(RANDO_SALT_THREE + 5, sLast.a, sLast.b, sLast.c);
    }
    if (target == 0 || !(Rando_ThreeRoms() & ForeignRom(target)) || Rando_HashInt(h, odds) != 0)
        return 0;
    if (!Fe7Loaded(target))
    {
        if (!sQuiet)
            PortLog("[rando] %s 0x%02X (chapter %d): foreign class 0x%02X rolled, but it is not loaded: class 0x%02X "
                "kept\n", boss ? "boss" : "enemy", pid, Chapter(), target, cls);
        return 0;
    }
    stand = Reforged_Fe7Fallback(target, female);
    if (stand != cls && !ClassFitsMap(stand, orig, d, boss ? "boss" : "enemy", pid))
        return 0;
    if (!ClassFitsMap(target, orig, d, boss ? "boss" : "enemy", pid))
        return 0;
    if (!sQuiet)
        PortLog("[rando] %s 0x%02X (chapter %d) at %d,%d: foreign class 0x%02X instead of 0x%02X\n",
            boss ? "boss" : "enemy", pid, Chapter(), d->xPosition, d->yPosition, target, cls);
    return target;
}

const struct UnitDefinition *Reforged_RandoUnitDef(const struct UnitDefinition *uDef, struct UnitDefinition *buf)
{
    struct UnitDefinition local;
    const struct CharacterData *vc;
    const struct CharacterData *lc;
    int pid, produced, orig, cls;

    memset(&sLast, 0, sizeof(sLast));
    if (!gRandoRunOn || uDef == NULL || buf == NULL)
        return uDef;
    BuildFacts();
    if (!sFactsBuilt)
        return uDef;
    local = *uDef; /* uDef may already be buf (a generated monster) */
    pid = local.charIndex;
    vc = Rando_VanillaChar(pid);
    lc = Rando_Char(pid);
    if (vc == NULL || lc == NULL)
        return uDef;
    produced = local.classIndex ? local.classIndex : lc->defaultClass;
    orig = local.classIndex ? local.classIndex : vc->defaultClass;

    sLast.valid = 1;
    sLast.kind = UK_OTHER;
    sLast.pid = pid;
    sLast.origClass = orig;
    sLast.hadItems = local.items[0] != 0;
    sLast.a = UnitKeyA(Chapter(), pid, orig);
    sLast.b = ((u32)local.xPosition << 8) | (u32)local.yPosition;
    sLast.c = local.level;

    cls = produced;
    if (local.genMonster && local.allegiance == FACTION_ID_RED && Rando_Opt(RFO_RANDO_FLOOR_MONSTERS))
    {
        /* "Tower & Ruins": another monster of the same tier (FloorMonsterFor); up to
           REACH_TRIES picks keyed on the unit, the first that fits the map, else its own */
        int try_;

        sLast.kind = UK_ENEMY;
        sLast.floor = 1;
        for (try_ = 0; try_ < REACH_TRIES; try_++)
        {
            int c = FloorMonsterFor(produced, Rando_Hash(RANDO_SALT_FLOORS, sLast.a, sLast.b, sLast.c ^ ((u32)try_ << 24)));

            if (c == 0)
                break;
            if (c == produced || ClassFitsMap(c, produced, &local, "enemy", pid))
            {
                cls = c;
                break;
            }
        }
        sLast.origClass = produced;
        if (cls != produced && CanClassWieldWeaponType((u8)cls, ITYPE_BOW) == TRUE &&
            CanClassWieldWeaponType((u8)produced, ITYPE_BOW) != TRUE)
        {
            /* a bow monster's AI, as LoadUnit gives a generated one (bmunit.c) */
            local.ai[2] = (u8)((local.ai[2] & (1 | 2 | 4)) | (8 | 32));
        }
        else if (cls != produced && CanClassWieldWeaponType((u8)produced, ITYPE_BOW) == TRUE &&
                 CanClassWieldWeaponType((u8)cls, ITYPE_BOW) != TRUE)
            local.ai[2] = (u8)(local.ai[2] & (1 | 2 | 4)); /* not a bow monster: its bits off again */
    }
    else if (local.genMonster || orig == CLASS_GORGONEGG || orig == CLASS_GORGONEGG2 || orig == CLASS_PHANTOM)
        ; /* generated monsters stay as generated; eggs and phantoms are special */
    else if (Rando_IsPlayable(pid))
    {
        sLast.kind = UK_PLAYER;
        cls = PlayerDefClass(pid, local.classIndex);
        if (cls != orig && !TerrainOk(cls, orig, &local))
            PortLog("[rando] unit 0x%02X (chapter %d): class 0x%02X cannot reach every square of its definition "
                "(kept: a player's class is fixed for the run)\n", pid, Chapter(), cls);
    }
    else if (local.allegiance == FACTION_ID_RED && IsBossPid(pid))
    {
        sLast.kind = UK_BOSS;
        cls = BossClassFor(pid, orig, 0);
        if (cls != orig && !ClassFitsMap(cls, orig, &local, "boss", pid))
        {
            /* versions 1-3: the original class. Version 4: up to REACH_TRIES - 1 more picks
               (the same order in every chapter), the first that fits; else the original. */
            int try_, next = orig;

            for (try_ = 1; gRandoRun.version >= RANDO_VERSION_MAPFIT && try_ < REACH_TRIES; try_++)
            {
                int c = BossClassFor(pid, orig, try_);

                if (c != orig && ClassFitsMap(c, orig, &local, "boss", pid))
                {
                    next = c;
                    break;
                }
            }
            if (next == orig)
                PortLog("[rando] boss 0x%02X (chapter %d): class 0x%02X cannot stand or walk where it must: "
                    "class 0x%02X kept\n", pid, Chapter(), cls, orig);
            cls = next;
        }
    }
    else if (local.allegiance == FACTION_ID_RED && Rando_Opt(RFO_RANDO_ENEMIES) && !IsBossPid(pid) &&
             !DoNotChange(pid) && !LootsVillages(&local))
    {
        sLast.kind = UK_ENEMY;
        cls = EnemyClassFor(orig, sLast.a, sLast.b, sLast.c);
        if (cls != orig && !ClassFitsMap(cls, orig, &local, "enemy", pid))
        {
            /* version 1: the original class. Version 2: up to REACH_TRIES - 1 more picks,
               keyed on the same unit (its level word salted with the try), the first that
               fits; the original class when none does. */
            int try_, next = orig;

            for (try_ = 1; gRandoRun.version >= RANDO_VERSION_REACH && try_ < REACH_TRIES; try_++)
            {
                int c = EnemyClassFor(orig, sLast.a, sLast.b, sLast.c ^ ((u32)try_ << 24));

                if (c != orig && ClassFitsMap(c, orig, &local, "enemy", pid))
                {
                    next = c;
                    break;
                }
            }
            cls = next;
        }
    }
    {
        int fe7 = 0;

        if (sLast.kind == UK_BOSS || (sLast.kind == UK_ENEMY && !sLast.floor))
        {
            fe7 = Fe7EnemyClass(&local, pid, orig, cls); /* version 10's extra roll, after the pick */
            if (fe7 != 0)
                cls = fe7;
        }
        /* versions 11-14: the foreign roll, where version 10's did not fire (a looting thief, kind UK_OTHER, too:
           it then has its inventory seen to as an enemy's) */
        if (fe7 == 0 && Rando_ThreeRoms() &&
            (sLast.kind == UK_BOSS || (sLast.kind == UK_ENEMY && !sLast.floor) ||
             (sLast.kind == UK_OTHER && cls == CLASS_THIEF && local.allegiance == FACTION_ID_RED)))
        {
            int x = ThreeEnemyClass(&local, pid, orig, cls);

            if (x != 0)
            {
                cls = x;
                if (sLast.kind == UK_OTHER)
                    sLast.kind = UK_ENEMY;
            }
        }
    }
    sLast.newClass = cls;
    if (cls == produced || !(ValidJid(cls) || Fe7Stand(cls)))
    {
        sLast.newClass = produced;
        return uDef;
    }
    local.classIndex = (u8)cls;
    *buf = local;
    if (sLast.kind == UK_BOSS && !sQuiet)
        PortLog("[rando] boss 0x%02X (chapter %d): class 0x%02X -> 0x%02X\n", pid, Chapter(), orig, cls);
    return buf;
}

/* The world map's skirmish icon (eventinfo.c GetChapterSkirmishLeaderClasses): the
   class Reforged_RandoUnitDef gives the group's leader, jid the vanilla value. The
   skirmish loads with gPlaySt.chapterIndex = WMLoc_GetChapterId(node), the chapterId
   the icon code passes, and the group's definitions keep their tile and level on
   the way (Event2C_LoadUnits, GetUnitDefinitionFromEventScr), so the key is the
   same. Not checked: the terrain fallback (the map is not loaded), so a leader that
   falls back to its vanilla class shows the rolled one; generated monsters keep
   the vanilla value. */
int Reforged_RandoSkirmishLeaderClass(int chapterId, const struct UnitDefinition *uDef, int jid)
{
    const struct CharacterData *vc;
    int pid, orig, cls;

    if (!gRandoRunOn || uDef == NULL || uDef->genMonster || uDef->allegiance != FACTION_ID_RED)
        return jid;
    BuildFacts();
    pid = uDef->charIndex;
    vc = Rando_VanillaChar(pid);
    if (!sFactsBuilt || vc == NULL || Rando_IsPlayable(pid))
        return jid;
    orig = uDef->classIndex ? uDef->classIndex : vc->defaultClass;
    if (orig == CLASS_GORGONEGG || orig == CLASS_GORGONEGG2 || orig == CLASS_PHANTOM)
        return jid;
    if (IsBossPid(pid))
        cls = BossClassFor(pid, orig, 0);
    else if (Rando_Opt(RFO_RANDO_ENEMIES) && !DoNotChange(pid) && !LootsVillages(uDef))
        cls = EnemyClassFor(orig, UnitKeyA(chapterId, pid, orig),
            ((u32)uDef->xPosition << 8) | (u32)uDef->yPosition, uDef->level);
    else
        return jid;
    return (cls == orig || !ValidJid(cls)) ? jid : cls;
}

/* ---- random promotions (RFO_RANDO_PROMOTIONS, the user's request of 2026-11-06: "random
   promotion options does sound fun") */

/* Every class with promotions (not a lord, a trainee or a special class) gets two promoted
   classes (not a lord's, a trainee's, a special one, never the Draco Zombie or the Demon
   King), two different ones (never the two sides of one male/female pair: the table holds
   the base class's sex's side, or the male side for a class with no twin, and Match gender
   gives the unit its own at promotion), picked by the run's seed for that class: of the same lean
   (ClassLean: magic to magic, weapons to weapons); "Monster promos" Separate (0): a monster
   class promotes into monster classes and the others into the others; Mixed (1, the user's
   request of 2026-11-07): either into either. With "Match gender" a male or female class
   never gets the other sex's twin. gPromoJidLut (the promotion screen, the crests) and the
   class's own promotion field are set; both are put back at every start (vanilla copy
   below, gClassData by the snapshot). Lords and trainees keep theirs. */
extern const int gPromoJidLutCount; /* src/classchg-data.c */
static u8 sVanillaPromo[JID_MAX + 1][2];
static int sPromoKept;

/* What a class fights with, from its vanilla weapon ranks: LEAN_PHYSICAL (swords, lances,
   axes, bows), LEAN_MAGIC (anima, light, dark, staves) or 0 (both or neither). FE8's player
   classes are all one or the other. A monster: magic for the Mogalls and the Gorgon (their
   eyes and Demon Surge hit Resistance), physical for the rest. "Random promotions" keeps it
   (the user's request of 2026-11-07: "magic users get magic promotions and the same for
   strength"). */
#define LEAN_PHYSICAL 1
#define LEAN_MAGIC    2
static int ClassLean(int jid)
{
    const struct ClassData *k = Rando_VanillaClass(jid);
    int phys, mag;

    if (k == NULL)
        return 0;
    if (Rando_ClassIsMonster(jid))
        return (jid == CLASS_MOGALL || jid == CLASS_ARCH_MOGALL || jid == CLASS_GORGON) ? LEAN_MAGIC : LEAN_PHYSICAL;
    phys = k->baseRanks[ITYPE_SWORD] || k->baseRanks[ITYPE_LANCE] || k->baseRanks[ITYPE_AXE] || k->baseRanks[ITYPE_BOW];
    mag = k->baseRanks[ITYPE_STAFF] || k->baseRanks[ITYPE_ANIMA] || k->baseRanks[ITYPE_LIGHT] || k->baseRanks[ITYPE_DARK];
    if (phys == mag)
        return 0;
    return phys ? LEAN_PHYSICAL : LEAN_MAGIC;
}

/* A class a random promotion may give: promoted, with a battle animation, not a lord's,
   a trainee's or a special one, never the Draco Zombie or the Demon King. */
static int PromoTarget(int c)
{
    u32 g = Facts(c);

    return (g & F_PROMOTED) && (g & F_REAL) && !(g & (F_SPECIAL | F_LORD | F_TRAINEE | F_SUMMON)) &&
           c != CLASS_DRACO_ZOMBIE && c != CLASS_DEMON_KING && c != CLASS_GORGONEGG && c != CLASS_GORGONEGG2 &&
           c != CLASS_PHANTOM;
}

/* The vanilla promotion choices of a class (gPromoJidLut before Random promotions). */
int Rando_VanillaPromo(int jid, int i)
{
    if (jid < 0 || jid > JID_MAX || jid >= gPromoJidLutCount || i < 0 || i > 1)
        return 0;
    return sPromoKept ? sVanillaPromo[jid][i] : gPromoJidLut[jid][i];
}

/* Match gender at promotion: a choice with a twin of the other sex becomes the unit's own
   sex's (a Pegasus Knight, which has no twin, offered Paladin gets Paladin F), by the
   unit's live character data (recruitment's Cross-gender writes the sex there). The
   promotion screen (classchg-sel.c, classchg-menuselect.c), the default promotion
   (bmbattle.c) and event promotions (Reforged_RandoPromoteJid) ask. */
int Reforged_RandoPromoChoice(struct Unit *unit, int jid)
{
    if (unit == NULL || unit->pCharacterData == NULL)
        return jid;
    return SexTwin(jid, (unit->pCharacterData->attributes & CA_FEMALE) != 0);
}

/* ---- version 10's FE7 extras ("FE7 classes", RANDO_VERSION_FE7 in reforged_rando.h) */

/* The run's rare third promotion choice per class (an FE7 class id; 0: none), rolled at every start
   after the version 8 rows, never in them. */
static u8 sFe7Promo[JID_MAX + 1];
static u8 sFe7PromoLogged[JID_MAX + 1]; /* logged once per start */

int Rando_Fe7Run(void)
{
    return gRandoRunOn && Rando_VersionFe7(gRandoRun.version); /* the version decides (RANDO_VERSION_FE7) */
}

/* A row that may get the FE7 extra: a human magic class (ClassLean) that promotes, not a lord's,
   trainee's or special one (a row Random promotions rewrites: Fe7PromoRows also wants a vanilla
   promotion). Dark users get Dark Druid, the others (anima, light, staves only) Archsage. */
static int Fe7PromoFor(int jid)
{
    u32 f = Facts(jid);
    const struct ClassData *k = Rando_VanillaClass(jid);

    if (k == NULL || !(f & F_PROMOTES) || (f & (F_LORD | F_TRAINEE | F_SPECIAL | F_MONSTER)) ||
        ClassLean(jid) != LEAN_MAGIC)
        return 0;
    return k->baseRanks[ITYPE_DARK] ? REFORGED_FE7_JID_DARK_DRUID : REFORGED_FE7_JID_ARCHSAGE;
}

/* port_reforged.h: the third choice of class jid's promotions in this run (src/classchg-sel.c,
   classchg-menuselect.c and reforged_skills.c ask, through Reforged_Fe7ThirdPromo). Either sex gets
   it (FE7's art is Athos's and Nergal's; a woman's save stand-in is Sage F / Druid F, as Blade Lord
   is a man's Swordmaster). */
int Reforged_RandoFe7Promo(int jid)
{
    if (!Rando_Fe7Run() || !ValidJid(jid) || sFe7Promo[jid] == 0 || !Fe7Loaded(sFe7Promo[jid]))
        return 0;
    if (!sQuiet && !sFe7PromoLogged[jid])
    {
        sFe7PromoLogged[jid] = 1;
        PortLog("[rando] promotion: class 0x%02X's choices 0x%02X, 0x%02X and the FE7 extra 0x%02X\n", jid,
            gPromoJidLut[jid][0], gPromoJidLut[jid][1], sFe7Promo[jid]);
    }
    return sFe7Promo[jid];
}

/* After the version 8 rows: each eligible row's own roll (RANDO_SALT_FE7, the class id), 1 in
   RANDO_FE7_PROMO_ODDS. The ROM is not asked: the run decides, Reforged_RandoFe7Promo shows. */
static void Fe7PromoRows(int rows)
{
    int jid;

    for (jid = 1; jid < rows; jid++)
    {
        int fe7 = Fe7PromoFor(jid);

        if (fe7 != 0 && ValidJid(sVanillaPromo[jid][0]) &&
            Rando_HashInt(Rando_Hash(RANDO_SALT_FE7, (u32)jid, 0, 0), RANDO_FE7_PROMO_ODDS) == 0)
            sFe7Promo[jid] = (u8)fe7;
    }
}

/* ---- versions 11-14: three choices on every promotion screen (RANDO_VERSION_THREE) */

static u8 sThreeRow[JID_MAX + 1]; /* Random promotions rewrote the row */
static u8 sThreeRnd[JID_MAX + 1]; /* the row's third random class (the side of the two: the base class's sex) */
static u8 sThreeFor[JID_MAX + 1]; /* the row's foreign roll fired */

/* With Random promotions, after a row's two (which it never changes): its third random class, drawn from the same
   candidates (the same rules: lean, monsters, Match gender) but neither of the two nor a twin of one, on its own
   salt; and its foreign roll, 1 in RANDO_THREE_PROMO_ODDS (a magic row 1 in RANDO_THREE_MAGIC_ODDS: with version
   10's 1 in 8 before it, 1 in 4 in all where the FE7 ROM is in the run). */
static void ThreeRowRolls(int jid, const u8 *cands, int m, int a, int b)
{
    u8 rest[JID_MAX + 1];
    int i, n = 0;

    sThreeRow[jid] = 1;
    for (i = 0; i < m; i++)
    {
        int c = cands[i];

        if (c == a || c == b || (sTwin[c] != 0 && (sTwin[c] == a || sTwin[c] == b)))
            continue;
        rest[n++] = (u8)c;
    }
    sThreeRnd[jid] = n > 0 ? rest[Rando_HashInt(Rando_Hash(RANDO_SALT_THREE + 1, (u32)jid, 0, 0), n)] : 0;
    sThreeFor[jid] = (u8)(Rando_HashInt(Rando_Hash(RANDO_SALT_THREE, (u32)jid, 0, 0),
        ClassLean(jid) == LEAN_MAGIC ? RANDO_THREE_MAGIC_ODDS : RANDO_THREE_PROMO_ODDS) == 0);
}

void RandoPromotions_Apply(void)
{
    int rows = gPromoJidLutCount < JID_MAX + 1 ? gPromoJidLutCount : JID_MAX + 1, jid;
    int mixed = Rando_Opt(RFO_RANDO_PROMO_MONSTERS) != 0;

    if (!sPromoKept)
    {
        memcpy(sVanillaPromo, gPromoJidLut, (size_t)rows * 2);
        sPromoKept = 1;
    }
    memcpy(gPromoJidLut, sVanillaPromo, (size_t)rows * 2);
    memset(sFe7Promo, 0, sizeof(sFe7Promo));
    memset(sFe7PromoLogged, 0, sizeof(sFe7PromoLogged));
    memset(sThreeRow, 0, sizeof(sThreeRow));
    memset(sThreeRnd, 0, sizeof(sThreeRnd));
    memset(sThreeFor, 0, sizeof(sThreeFor));
    if (!gRandoRunOn || !Rando_Opt(RFO_RANDO_PROMOTIONS))
        return;
    BuildFacts();
    if (!sFactsBuilt)
        return;
    for (jid = 1; jid < rows; jid++)
    {
        u8 cands[JID_MAX + 1];
        u32 f = Facts(jid);
        int c, m = 0, a, b, lean = ClassLean(jid);
        struct ClassData *k;

        if (!(f & F_PROMOTES) || (f & (F_LORD | F_TRAINEE | F_SPECIAL)) || !ValidJid(sVanillaPromo[jid][0]))
            continue;
        for (c = 1; c <= JID_MAX; c++)
        {
            u32 g = Facts(c);

            if (!PromoTarget(c))
                continue;
            if (sTwin[c] != 0 && !((f & F_FEMALE) ? (g & F_FEMALE) : (g & F_MALE)))
                continue; /* one side of each male/female pair: the base class's sex, else the male one */
            if (!mixed && ((f & F_MONSTER) != 0) != ((g & F_MONSTER) != 0))
                continue; /* Separate: monsters into monsters, the others into the others */
            if (Rando_Opt(RFO_RANDO_GENDER) && (((f & F_FEMALE) && (g & F_MALE)) || ((f & F_MALE) && (g & F_FEMALE))))
                continue;
            if (lean != 0 && ClassLean(c) != lean)
                continue; /* a magic class promotes into magic classes, a weapon class into weapon classes */
            cands[m++] = (u8)c;
        }
        if (m < 2)
            continue;
        a = Rando_HashInt(Rando_Hash(RANDO_SALT_PROMOTIONS, (u32)jid, 0, 0), m);
        b = Rando_HashInt(Rando_Hash(RANDO_SALT_PROMOTIONS, (u32)jid, 1, 0), m - 1);
        if (b >= a)
            b++; /* never the same class twice */
        gPromoJidLut[jid][0] = cands[a];
        gPromoJidLut[jid][1] = cands[b];
        k = (struct ClassData *)Rando_Class(jid);
        if (k != NULL)
            k->promotion = cands[a];
        if (Rando_ThreeRun())
            ThreeRowRolls(jid, cands, m, cands[a], cands[b]); /* after the two, on their own salts */
    }
    if (Rando_Fe7Run())
        Fe7PromoRows(rows);
}

static int Shown(const u8 *shown, int n, int c)
{
    int i;

    for (i = 0; i < n; i++)
        if (shown[i] == c)
            return 1;
    return 0;
}

/* A class of either tier, as its table says (a foreign one: its live data). */
static int TierPromoted(int c)
{
    const struct ClassData *k;

    if (ValidJid(c))
        return VanillaPromoted(c);
    k = IsForeignJid(c) ? GetClassData(c) : NULL;
    return k != NULL && (k->attributes & CA_PROMOTED) != 0;
}

/* The sword lord for a promotion list: the first of the run's version's (RunSwordLords) that the list does not show
   yet; 0 when that one is not loaded now (a ROM removed later: the FE8 fill takes the slot, never the other ROM's
   lord). */
static int SwordLord(int female, const u8 *shown, int n)
{
    int order[2], k = RunSwordLords(female, order), i;

    for (i = 0; i < k; i++)
        if (!Shown(shown, n, order[i]))
            return ForeignOk(order[i]) ? order[i] : 0;
    return 0;
}

/* Random promotions' foreign third for a row it rewrote (its roll fired), matched to the base class as the random
   choices are (weapon kind, tier, the unit's sex): a magic row version 10's Archsage or Dark Druid (Fe7PromoFor);
   a sword user a sword lord (SwordLord); a lance or axe user that does not fly (armours, infantry) Armor Lord; a bow
   user the Nomad Trooper of its sex. Monsters none. */
static int ThreeRandomForeign(int jid, int female, const u8 *shown, int n)
{
    int c = 0;

    if (!ValidJid(jid) || Has(jid, F_MONSTER))
        return 0;
    if (ClassLean(jid) == LEAN_MAGIC)
        c = Fe7PromoFor(jid);
    else if (ClassUses(jid, ITYPE_SWORD))
        return SwordLord(female, shown, n);
    else if ((ClassUses(jid, ITYPE_LANCE) || ClassUses(jid, ITYPE_AXE)) && !Has(jid, F_FLIER))
        c = FJ_ARMOR_LORD;
    else if (ClassUses(jid, ITYPE_BOW))
        c = female ? FJ_TROOPER_F : FJ_TROOPER;
    return c != 0 && ForeignOk(c) && !Shown(shown, n, c) ? c : 0;
}

/* A row Random promotions does not rewrite (it is off, or a lord's, trainee's or foreign class's row): the row's
   natural foreign promotion, every time its ROM is in the run: Mercenary and Myrmidon a sword lord, Fighter,
   Brigand and Pirate Armor Lord (FE7's axe lord), Archer the Nomad Trooper (the unit's sex), Mage, Monk, Priest,
   Cleric and Troubadour Archsage, Shaman Dark Druid; Eirika's Lord FE6's Master Lord (her sword; FE7's Blade Lord is
   already her row's second when loaded), Lion Lord FE7's Blade Lord, the FE7 Nomads the Nomad Trooper. */
static int ThreeNaturalForeign(int jid, int female, const u8 *shown, int n)
{
    int c = 0;

    switch (jid)
    {
    case CLASS_MERCENARY: case CLASS_MERCENARY_F: case CLASS_MYRMIDON: case CLASS_MYRMIDON_F:
        return SwordLord(female, shown, n);
    case CLASS_FIGHTER: case CLASS_BRIGAND: case CLASS_PIRATE:
        c = FJ_ARMOR_LORD;
        break;
    case CLASS_ARCHER: case CLASS_ARCHER_F: case FJ_NOMAD: case FJ_NOMAD_F:
        c = female ? FJ_TROOPER_F : FJ_TROOPER;
        break;
    case CLASS_MAGE: case CLASS_MAGE_F: case CLASS_MONK: case CLASS_PRIEST: case CLASS_CLERIC: case CLASS_TROUBADOUR:
        c = FJ_ARCHSAGE;
        break;
    case CLASS_SHAMAN: case CLASS_SHAMAN_F:
        c = FJ_DARK_DRUID;
        break;
    case CLASS_EIRIKA_LORD:
        c = FJ_MASTER_LORD;
        break;
    case FJ_LION_LORD:
        c = FJ_BLADE_LORD;
        break;
    }
    return c != 0 && ForeignOk(c) && !Shown(shown, n, c) ? c : 0;
}

/* FE8 classes with no twin that FE8 only ever draws as one sex (the FE8 fill keeps to the unit's). */
static int SexLocked(int c, int female)
{
    switch (c)
    {
    case CLASS_FALCON_KNIGHT: case CLASS_VALKYRIE: case CLASS_PEGASUS_KNIGHT: case CLASS_TROUBADOUR:
        return !female;
    case CLASS_WARRIOR: case CLASS_BERSERKER: case CLASS_ROGUE: case CLASS_FIGHTER: case CLASS_BRIGAND:
    case CLASS_PIRATE: case CLASS_MONK:
        return female;
    }
    return 0;
}

/* An unpromoted class a trainee's first promotion may be filled with. */
static int FillBaseTarget(int c)
{
    u32 f = Facts(c);

    return (f & F_REAL) && (f & F_PROMOTES) && !(f & (F_SPECIAL | F_TRAINEE | F_LORD | F_SUMMON | F_PROMOTED));
}

/* The FE8 fill: a class of the row's tier (its first choice's: a trainee's first promotion is to an unpromoted
   class) that a random promotion could give (PromoTarget; the trainee's: FillBaseTarget), human for a human and a
   monster for a monster, of the base class's lean (magic or weapons), the unit's sex (a twin's side, SexLocked), not
   shown nor a twin of one shown, sharing the most weapon kinds with the base class and adding the fewest others (a
   foreign row: its own kinds; a Thief's fill is a Swordmaster before a General, a Nomad's a Sniper before a Warrior);
   among those, the seed's pick for the row, the sex and the slot (RANDO_SALT_THREE + 2). When no class passes, the
   lean rule goes, then the monster one (a Mogall: Arch Mogall and Gorgon are the only promoted magic monsters). */
static int ThreeFill(int jid, int female, const u8 *shown, int n)
{
    u8 cands[JID_MAX + 1];
    int base = ValidJid(jid) ? jid : Reforged_Fe7Fallback(jid, female), first, promoted, lean, monster;
    int m = 0, best = 0, c, t, relax;

    first = jid <= JID_MAX ? Rando_VanillaPromo(jid, 0) : gPromoJidLut[jid][0];
    if (first == 0 && jid <= JID_MAX)
        first = Rando_VanillaPromo(jid, 1);
    promoted = first == 0 || TierPromoted(first);
    lean = ClassLean(base);
    monster = Has(base, F_MONSTER);
    /* relax 0: every rule; 1: either lean (a Mogall's magic line has two promoted classes); 2: monster or not */
    for (relax = 0; relax < 3 && m == 0; relax++)
        for (c = 1; c <= JID_MAX; c++)
        {
            int score = 0;

            if (promoted ? !PromoTarget(c) : !FillBaseTarget(c))
                continue;
            if ((relax < 2 && Has(c, F_MONSTER) != monster) || (relax < 1 && lean != 0 && ClassLean(c) != lean))
                continue;
            if (sTwin[c] != 0 && (female ? !Has(c, F_FEMALE) : !Has(c, F_MALE)))
                continue;
            if (SexLocked(c, female) || Shown(shown, n, c) || (sTwin[c] != 0 && Shown(shown, n, sTwin[c])))
                continue;
            for (t = 0; t < 8; t++)
                if (ClassUses(c, t))
                    score += ClassUses(jid, t) ? 4 : -1; /* each shared kind, less each kind the base never used */
            if (m == 0 || score > best)
            {
                best = score;
                m = 0;
            }
            if (score == best)
                cands[m++] = (u8)c;
        }
    return m > 0 ? cands[Rando_HashInt(Rando_Hash(RANDO_SALT_THREE + 2, (u32)jid, (u32)female, (u32)n), m)] : 0;
}

/* A choice added to a list: never 0, a repeat, or (strict: the fill) the twin of one already there. */
static int AddChoice(u8 *out, int *n, int c, int strict)
{
    int i;

    if (c == 0 || *n >= 3)
        return 0;
    for (i = 0; i < *n; i++)
        if (out[i] == c || (strict && ValidJid(c) && sTwin[c] != 0 && out[i] == sTwin[c]))
            return 0;
    out[(*n)++] = (u8)c;
    return 1;
}

/* A trainee's third (src/classchg-sel.c, once both routes are cleared: Check3rdTraineeEnabled), or 0. */
static int TraineeThird(int jid)
{
    switch (jid)
    {
    case CLASS_JOURNEYMAN: return CLASS_JOURNEYMAN_T1;
    case CLASS_PUPIL:      return CLASS_PUPIL_T1;
    case CLASS_RECRUIT:    return CLASS_RECRUIT_T1;
    }
    return 0;
}

/* Version 10's list for class jid and the unit's sex, as its screen showed it (src/classchg-sel.c): the table's two
   (Match gender's swap), then the trainee's third (unlocked) or reforged_fe7.c's third (a Myrmidon's Blade Lord, a
   Knight's Armor Lord, version 10's FE7 extra); repeats dropped. */
static int V10List(int jid, int female, int trainee3rd, u8 out[3])
{
    int n = 0, i, t = TraineeThird(jid);

    for (i = 0; i < 2; i++)
        AddChoice(out, &n, SexTwin(gPromoJidLut[jid][i], female), 0);
    if (t != 0 && trainee3rd)
        AddChoice(out, &n, t, 0);
    else
        AddChoice(out, &n, Reforged_Fe7ThirdPromo(jid), 0);
    return n;
}

#define SRC_V10    0
#define SRC_FOREIGN 1
#define SRC_RANDOM 2
#define SRC_FILL   3

/* Rando_PromoList with the trainee's unlock given; src[i]: where out[i] came from (SRC_*). */
static int PromoListEx(int jid, int female, int trainee3rd, u8 out[3], u8 src[3])
{
    int n, c;

    if (!Rando_ThreeRun() || jid < 1 || jid >= gPromoJidLutCount || jid > REFORGED_FOREIGN_LAST_JID ||
        (gPromoJidLut[jid][0] == 0 && gPromoJidLut[jid][1] == 0))
        return 0;
    BuildFacts();
    if (!sFactsBuilt)
        return 0;
    female = female != 0;
    n = V10List(jid, female, trainee3rd, out);
    src[0] = src[1] = src[2] = SRC_V10;
    if (n < 3)
    {
        if (ValidJid(jid) && sThreeRow[jid])
            c = sThreeFor[jid] ? ThreeRandomForeign(jid, female, out, n) : 0;
        else
            c = ThreeNaturalForeign(jid, female, out, n);
        if (AddChoice(out, &n, c, 1))
            src[n - 1] = SRC_FOREIGN;
    }
    if (n < 3 && ValidJid(jid) && sThreeRow[jid] && AddChoice(out, &n, SexTwin(sThreeRnd[jid], female), 1))
        src[n - 1] = SRC_RANDOM;
    while (n < 3)
    {
        c = ThreeFill(jid, female, out, n);
        if (!AddChoice(out, &n, c, 1))
            break;
        src[n - 1] = SRC_FILL;
    }
    return n;
}

int Rando_PromoList(int jid, int female, u8 out[3])
{
    u8 src[3];

    return PromoListEx(jid, female, TraineeThird(jid) != 0 && Check3rdTraineeEnabled(), out, src);
}

static int UnitIsFemale(const struct Unit *unit)
{
    return unit->pCharacterData != NULL && (unit->pCharacterData->attributes & CA_FEMALE) != 0;
}

static int Promo3ListEx(int jid, int female, u8 out[3], u8 *src, u8 *wantOut);
static int P3TileBars(const struct ClassData *old, const u8 l[3], const u8 src[3], int terrain);
static int P3UnitTerrain(const struct Unit *unit);

/* port_reforged.h: the unit's three choices in a version 11-14 run, or (v1.23.0) in a game without the randomizer
   with "Third promotion" on (3), else 0. */
int Reforged_PromoList(const struct Unit *unit, u8 out[3])
{
    if (unit == NULL || unit->pClassData == NULL || unit->pCharacterData == NULL)
        return 0;
    if (Rando_ThreeRun())
        return Rando_PromoList(unit->pClassData->number, UnitIsFemale(unit), out) == 3 ? 3 : 0;
    if (Reforged_Promo3Active())
    {
        u8 src[3];

        if (Promo3ListEx(unit->pClassData->number, UnitIsFemale(unit), out, src, NULL) != 3)
            return 0;
        /* a seal used on the map, on a tile an added class could not stand on: the game's own screen */
        return P3TileBars(unit->pClassData, out, src, P3UnitTerrain(unit)) != 0 ? 0 : 3;
    }
    return 0;
}

/* include/bmunit.h RFO_PROMO_SLOT: the promotion screen's choice `slot` (0-2): the run's list's, else jid (what the
   screen always showed). */
int Reforged_PromoSlot(struct Unit *unit, int slot, int jid)
{
    u8 l[3];

    if (slot < 0 || slot > 2 || Reforged_PromoList(unit, l) != 3)
        return jid;
    return l[slot];
}

/* include/bmunit.h RFO_PROMO_THREE (src/classchg-handler.c): 1 when the unit's promotion opens the three-choice
   screen (a version 11-14 run, or v1.23.0's "Third promotion": even for a row with one class), logged. */
int Reforged_PromoThree(struct Unit *unit)
{
    static const char *const sSrc[4] = { "", " (fill: foreign)", " (fill: random)", " (fill: FE8)" };
    u8 l[3], src[3];
    int jid;

    if (Reforged_PromoList(unit, l) != 3)
    {
        int bar;

        if (unit != NULL && unit->pClassData != NULL && unit->pCharacterData != NULL && Reforged_Promo3Active() &&
            Promo3ListEx(unit->pClassData->number, UnitIsFemale(unit), l, src, NULL) == 3 &&
            (bar = P3TileBars(unit->pClassData, l, src, P3UnitTerrain(unit))) != 0)
            PortLog("[reforged] third promotion: unit 0x%02X, class 0x%02X, on terrain 0x%02X where class 0x%02X could "
                "not stand: the game's own choices\n", unit->pCharacterData->number, unit->pClassData->number,
                P3UnitTerrain(unit), bar);
        return 0;
    }
    jid = unit->pClassData->number;
    if (!Rando_ThreeRun())
    {
        /* v1.23.0, "Third promotion" (a game without the randomizer) */
        static const char *const sP3[4] = { "", " (table)", " (FE8 stand-in)", " (fill)" };

        Promo3ListEx(jid, UnitIsFemale(unit), l, src, NULL);
        PortLog("[reforged] third promotion: unit 0x%02X, class 0x%02X (%s): choices 0x%02X%s, 0x%02X%s, 0x%02X%s\n",
            unit->pCharacterData->number, jid, UnitIsFemale(unit) ? "F" : "M", l[0], sP3[src[0] & 3], l[1],
            sP3[src[1] & 3], l[2], sP3[src[2] & 3]);
        return 1;
    }
    PromoListEx(jid, UnitIsFemale(unit), TraineeThird(jid) != 0 && Check3rdTraineeEnabled(), l, src);
    PortLog("[rando] promotion: unit 0x%02X, class 0x%02X (%s): choices 0x%02X%s, 0x%02X%s, 0x%02X%s\n",
        unit->pCharacterData->number, jid, UnitIsFemale(unit) ? "F" : "M", l[0], sSrc[src[0]], l[1], sSrc[src[1]],
        l[2], sSrc[src[2]]);
    return 1;
}

/* ---- v1.23.0: the Game fix "Third promotion" (RFO_FIX_PROMO3; the owner's table of 2026-11-09)

   In a game without the randomizer (gRandoRunOn and gRandoCarriedOn 0) started with the row On, every promotion
   screen offers three classes: the game's own choices first, unchanged, in their order (the class's gPromoJidLut row,
   which at boot holds the twins' FE7 lords and the FE7 Nomads' FE6 Trooper when those classes are loaded, and
   reforged_fe7.c's third: a Myrmidon's Blade Lord, a Knight's Armor Lord), then the classes of the owner's table
   (Promo3Wanted) in its order, repeats dropped, up to three. A table class that needs a ROM that is not loaded (or
   an FE8 class of the other sex only: SexLocked) gets an FE8 stand-in in its place (Promo3Fallback): its save
   stand-in (Reforged_Fe7Fallback, the class it saves as without the ROM) when that is not on the screen and shares a
   weapon kind with the unit's class, else the FE8 class of the row's tier that fits best (Promo3Fill: the unit's sex,
   human or monster as the class, a weapon kind of the unit's class, the most of the missing class's kinds and the
   fewest others, the class's movement, then the lowest id). The stand-ins are chosen after every available table
   class is in its place, so none takes a table class's slot. No random numbers: the same in every game. A trainee's
   first promotion stays the game's (its third only once both routes are cleared, as in the original); special
   classes (the Gorgon Egg) and classes with no promotion keep theirs. The randomizer's games never use this (a
   version 11-18 game already has its own three; an older one keeps what it had). */

#define P3_W(t) (1 << (t))

/* The fix is on in the game being played (a game without the randomizer). */
int Reforged_Promo3Active(void)
{
    return !gRandoRunOn && !gRandoCarriedOn && Reforged_RunFix(RFO_FIX_PROMO3) != 0;
}

/* The weapon kinds of an FE8 class (bits 1 << ITYPE_*, sword to dark): its vanilla base ranks. */
static int P3MaskFe8(int c)
{
    const struct ClassData *k = ValidJid(c) ? Rando_VanillaClass(c) : NULL;
    int m = 0, t;

    for (t = 0; k != NULL && t < 8; t++)
        if (k->baseRanks[t] != 0)
            m |= P3_W(t);
    return m;
}

/* A foreign class's weapon kinds without its ROM: its save stand-in's plus what the class adds (Blade Lord the bow,
   Armor Lord sword, lance and axe, Archsage dark, Dark Druid light), as reforged_fe7.c builds them (the self-test
   checks these against every loaded class). */
static int P3MaskNoRom(int c)
{
    int m = P3MaskFe8(Reforged_Fe7Fallback(c, 0));

    switch (c)
    {
    case FJ_BLADE_LORD: case FJ_BLADE_LORD + 1:
        m |= P3_W(ITYPE_BOW);
        break;
    case FJ_ARMOR_LORD: case FJ_ARMOR_LORD + 1:
        m |= P3_W(ITYPE_SWORD) | P3_W(ITYPE_LANCE) | P3_W(ITYPE_AXE);
        break;
    case FJ_ARCHSAGE:
        m |= P3_W(ITYPE_DARK);
        break;
    case FJ_DARK_DRUID:
        m |= P3_W(ITYPE_LIGHT);
        break;
    }
    return m;
}

/* Class c's weapon kinds: an FE8 class's; a loaded foreign class's own base ranks; else P3MaskNoRom. */
static int P3Mask(int c)
{
    const struct ClassData *k;
    int m = 0, t;

    if (!IsForeignJid(c))
        return P3MaskFe8(c);
    if (!Fe7Loaded(c))
        return P3MaskNoRom(c);
    k = GetClassData(c);
    for (t = 0; k != NULL && t < 8; t++)
        if (k->baseRanks[t] != 0)
            m |= P3_W(t);
    return m;
}

static int P3Count(int m)
{
    int n = 0;

    for (; m != 0; m &= m - 1)
        n++;
    return n;
}

/* 2 a flier, 1 mounted, 0 on foot (a foreign class: its own attributes when loaded, else its stand-in's). */
static int P3Move(int c)
{
    const struct ClassData *k;

    if (IsForeignJid(c) && !Fe7Loaded(c))
        c = Reforged_Fe7Fallback(c, 0);
    if (ValidJid(c) && Has(c, F_FLIER))
        return 2;
    k = ValidJid(c) ? Rando_VanillaClass(c) : GetClassData(c);
    return k != NULL && (k->attributes & CA_MOUNTED) ? 1 : 0;
}

/* The class's tier (CA_PROMOTED; a foreign class that is not loaded: its stand-in's). */
static int P3Promoted(int c)
{
    if (IsForeignJid(c) && !Fe7Loaded(c))
        c = Reforged_Fe7Fallback(c, 0);
    return TierPromoted(c);
}

/* Class c's other-sex twin: an FE8 class's (sTwin), the FE6 Trooper's and the FE7 Nomad's; 0 for none. */
static int P3Twin(int c)
{
    switch (c)
    {
    case FJ_TROOPER: return FJ_TROOPER_F;
    case FJ_TROOPER_F: return FJ_TROOPER;
    case FJ_NOMAD: return FJ_NOMAD_F;
    case FJ_NOMAD_F: return FJ_NOMAD;
    }
    return ValidJid(c) ? sTwin[c] : 0;
}

/* An FE8 class of either sex made the unit's (its twin); a class with no twin is itself. */
static int P3OfSex(int c, int female)
{
    if (!ValidJid(c) || sTwin[c] == 0)
        return c;
    return (female ? Has(c, F_MALE) : Has(c, F_FEMALE)) ? sTwin[c] : c;
}

/* The game's own choices for class jid, as the screen shows them with the fix off (the row, then reforged_fe7.c's
   third), dropping a foreign class that is not loaded (the self-test's hidden ROMs: a real game never has one). */
static int P3Vanilla(int jid, u8 out[3])
{
    int n = 0, i, c;

    for (i = 0; i < 3; i++)
    {
        c = i < 2 ? gPromoJidLut[jid][i] : Reforged_Fe7ThirdPromo(jid);
        if (c != 0 && (!IsForeignJid(c) || Fe7Loaded(c)))
            AddChoice(out, &n, c, 0);
    }
    return n;
}

/* The owner's table: the classes a class's screen adds after its own (the unit's sex where the table says so).
   Returns how many (0-2). */
static int Promo3Wanted(int jid, int female, int want[2])
{
    int n = 0;

    switch (jid)
    {
    case CLASS_MYRMIDON: case CLASS_MYRMIDON_F:
        want[n++] = FJ_BLADE_LORD;
        break;
    case CLASS_MERCENARY: case CLASS_MERCENARY_F:
        want[n++] = FJ_MASTER_LORD;
        break;
    case CLASS_ARMOR_KNIGHT: case CLASS_ARMOR_KNIGHT_F:
    case CLASS_FIGHTER: case CLASS_BRIGAND: case CLASS_PIRATE:
        want[n++] = FJ_ARMOR_LORD;
        break;
    case CLASS_CAVALIER: case CLASS_CAVALIER_F: case CLASS_ARCHER: case CLASS_ARCHER_F:
        want[n++] = female ? FJ_TROOPER_F : FJ_TROOPER;
        break;
    case FJ_NOMAD: case FJ_NOMAD_F:
        want[n++] = female ? FJ_TROOPER_F : FJ_TROOPER;
        want[n++] = P3OfSex(CLASS_PALADIN, female);
        break;
    case CLASS_MAGE: case CLASS_MAGE_F: case CLASS_MONK: case CLASS_PRIEST: case CLASS_CLERIC: case CLASS_TROUBADOUR:
        want[n++] = FJ_ARCHSAGE;
        break;
    case CLASS_SHAMAN: case CLASS_SHAMAN_F:
        want[n++] = FJ_DARK_DRUID;
        break;
    case CLASS_THIEF: case FJ_THIEF_F:
        want[n++] = P3OfSex(CLASS_SWORDMASTER, female);
        break;
    case CLASS_PEGASUS_KNIGHT:
        want[n++] = P3OfSex(CLASS_WYVERN_LORD, female);
        break;
    case CLASS_WYVERN_RIDER: case CLASS_WYVERN_RIDER_F:
        want[n++] = female ? CLASS_FALCON_KNIGHT : CLASS_GREAT_KNIGHT;
        break;
    case CLASS_EIRIKA_LORD:
        want[n++] = FJ_BLADE_LORD + 1; /* Eirika's Blade Lord (her locks; her row's second when FE7 is loaded) */
        want[n++] = FJ_MASTER_LORD;
        break;
    case CLASS_EPHRAIM_LORD:
        want[n++] = FJ_ARMOR_LORD + 1; /* Ephraim's Armor Lord (his locks; his row's second when FE7 is loaded) */
        want[n++] = P3OfSex(CLASS_GENERAL, female);
        break;
    case FJ_LION_LORD:
        want[n++] = female ? CLASS_EIRIKA_MASTER_LORD : CLASS_EPHRAIM_MASTER_LORD; /* FE8's Great Lord */
        want[n++] = FJ_BLADE_LORD;
        break;
    /* the trainees' second promotion (their third tier, once both routes are cleared): the missing class of their
       line's own pair (Cavalier: Great Knight; Fighter: Warrior; Mage: Mage Knight) */
    case CLASS_RECRUIT_T1:
        want[n++] = P3OfSex(CLASS_GREAT_KNIGHT, female);
        break;
    case CLASS_JOURNEYMAN_T1:
        want[n++] = CLASS_WARRIOR;
        break;
    case CLASS_PUPIL_T1:
        want[n++] = P3OfSex(CLASS_MAGE_KNIGHT, female);
        break;
    }
    return n;
}

/* Table class c can be offered as it is: a loaded foreign class, or an FE8 class of the unit's sex. */
static int P3Available(int c, int female)
{
    if (IsForeignJid(c))
        return Fe7Loaded(c);
    return ValidJid(c) && !SexLocked(c, female);
}

/* The FE8 fill: the class of the row's tier that fits best in place of class want (0: a plain fill, the base class's
   own weapons): a class a promotion may give (PromoTarget), human for a human and a monster for a monster, the
   unit's sex (a twin's side, SexLocked), not shown nor a twin of one shown; by, in order: a weapon kind of the base
   class (the unit keeps a usable rank), 4 for each kind of the wanted or the base class less 1 for each other kind,
   the base class's movement, the lowest id. When no class passes, the lean rule (magic or weapons) goes, then the
   monster one, as in ThreeFill. */
static int Promo3Fill(int jid, int want, int female, const u8 *shown, int n)
{
    int baseStand = ValidJid(jid) ? jid : Reforged_Fe7Fallback(jid, female);
    int b = P3Mask(jid), pref = b | (want != 0 ? P3Mask(want) : 0), move = P3Move(jid);
    int first = gPromoJidLut[jid][0] != 0 ? gPromoJidLut[jid][0] : gPromoJidLut[jid][1];
    int promoted = first == 0 || P3Promoted(first), monster = Has(baseStand, F_MONSTER), lean = ClassLean(baseStand);
    int best = 0, bk1 = 0, bk2 = 0, bk3 = 0, c, relax;

    for (relax = 0; relax < 3 && best == 0; relax++)
        for (c = 1; c <= JID_MAX; c++)
        {
            int m, k1, k2, k3;

            if (promoted ? !PromoTarget(c) : !FillBaseTarget(c))
                continue;
            if ((relax < 2 && Has(c, F_MONSTER) != monster) || (relax < 1 && lean != 0 && ClassLean(c) != lean))
                continue;
            if (sTwin[c] != 0 && (female ? !Has(c, F_FEMALE) : !Has(c, F_MALE)))
                continue;
            if (SexLocked(c, female) || Shown(shown, n, c) || (sTwin[c] != 0 && Shown(shown, n, sTwin[c])))
                continue;
            m = P3Mask(c);
            k1 = (m & b) != 0;
            k2 = 4 * P3Count(m & pref) - P3Count(m & ~pref);
            k3 = P3Move(c) == move;
            if (best == 0 || k1 > bk1 || (k1 == bk1 && (k2 > bk2 || (k2 == bk2 && k3 > bk3))))
            {
                best = c;
                bk1 = k1;
                bk2 = k2;
                bk3 = k3;
            }
        }
    return best;
}

/* The FE8 class in place of table class want (not available): its save stand-in for the unit's sex when that is
   not shown (nor its twin), of the unit's sex and the wanted class's tier, and shares a weapon kind with the base
   class; else Promo3Fill. */
static int Promo3Fallback(int jid, int want, int female, const u8 *shown, int n)
{
    int s = IsForeignJid(want) ? P3OfSex(Reforged_Fe7Fallback(want, female), female) : 0;

    if (ValidJid(s) && !Shown(shown, n, s) && !(sTwin[s] != 0 && Shown(shown, n, sTwin[s])) &&
        !SexLocked(s, female) && (P3Mask(s) & P3Mask(jid)) != 0 && P3Promoted(s) == P3Promoted(want))
        return s;
    return Promo3Fill(jid, want, female, shown, n);
}

#define P3SRC_GAME     0 /* the game's own choice */
#define P3SRC_TABLE    1 /* the owner's table */
#define P3SRC_STANDIN  2 /* an FE8 class in place of a table class */
#define P3SRC_FILL     3 /* an FE8 fill (no table class left) */

/* The three classes for class jid and the unit's sex (3), or 0 (the screen keeps its own: a trainee's first
   promotion, a special class, no promotion, a class that is not loaded); src[i] (P3SRC_*) and wantOut[i] (the table
   class a stand-in replaces) may be NULL. */
static int Promo3ListEx(int jid, int female, u8 out[3], u8 *src, u8 *wantOut)
{
    u8 s[3] = { 0, 0, 0 }, w[3] = { 0, 0, 0 }, list[3] = { 0, 0, 0 };
    int want[2], slot[3], nw, n, i, j, ns = 0;

    if (jid < 1 || jid >= gPromoJidLutCount || jid > REFORGED_FOREIGN_LAST_JID ||
        (gPromoJidLut[jid][0] == 0 && gPromoJidLut[jid][1] == 0) || TraineeThird(jid) != 0)
        return 0;
    BuildFacts();
    if (!sFactsBuilt)
        return 0;
    if (IsForeignJid(jid) ? !Fe7Loaded(jid) : (Facts(jid) & F_SPECIAL) != 0)
        return 0;
    female = female != 0;
    n = P3Vanilla(jid, list);
    nw = Promo3Wanted(jid, female, want);
    /* the table's classes in their places; an open slot (0) for each one that is not available */
    for (i = 0; i < nw && n < 3; i++)
    {
        if (Shown(list, n, want[i]) || (P3Twin(want[i]) != 0 && Shown(list, n, P3Twin(want[i]))))
            continue; /* (a woman in the male Nomad class: his row's Trooper is her table's) */
        if (P3Available(want[i], female))
        {
            if (AddChoice(list, &n, want[i], 1))
                s[n - 1] = P3SRC_TABLE;
            continue;
        }
        w[n] = (u8)want[i];
        s[n] = P3SRC_STANDIN;
        slot[ns++] = n;
        list[n++] = 0;
    }
    /* the stand-ins, in order, each seeing every class placed so far */
    for (j = 0; j < ns; j++)
    {
        u8 seen[3];
        int k = 0;

        for (i = 0; i < n; i++)
            if (list[i] != 0)
                seen[k++] = list[i];
        list[slot[j]] = (u8)Promo3Fallback(jid, w[slot[j]], female, seen, k);
    }
    /* close any slot no class fills, then fill to three */
    for (i = j = 0; i < n; i++)
        if (list[i] != 0)
        {
            list[j] = list[i];
            s[j] = s[i];
            w[j] = w[i];
            j++;
        }
    n = j;
    while (n < 3)
    {
        int c = Promo3Fill(jid, 0, female, list, n);

        if (!AddChoice(list, &n, c, 1))
            break;
        s[n - 1] = P3SRC_FILL;
        w[n - 1] = 0;
    }
    if (n != 3)
        return 0;
    for (i = 0; i < 3; i++)
    {
        out[i] = list[i];
        if (src != NULL)
            src[i] = s[i];
        if (wantOut != NULL)
            wantOut[i] = w[i];
    }
    return 3;
}

/* The tile rule. A promotion never moves the unit, so a class the row adds that cannot enter the tile the unit
   stands on (a Wyvern Rider made a Great Knight over a peak, a Pirate an Armor Lord at sea) would leave it stuck
   there. P3TileBars: the first class of the list the row added (not the game's own) that cannot enter `terrain`
   while the unit's present class can (standard movement costs; below 0 is "cannot cross"), else 0; terrain < 0:
   no tile to check. */
static int P3TileBars(const struct ClassData *old, const u8 l[3], const u8 src[3], int terrain)
{
    int i;

    if (terrain < 0 || terrain >= TERRAIN_COUNT || old == NULL || old->pMovCostTable[0] == NULL ||
        old->pMovCostTable[0][terrain] <= 0)
        return 0;
    for (i = 0; i < 3; i++)
    {
        const struct ClassData *k = src[i] != P3SRC_GAME ? GetClassData(l[i]) : NULL;

        if (k != NULL && k->pMovCostTable[0] != NULL && k->pMovCostTable[0][terrain] < 0)
            return l[i];
    }
    return 0;
}

/* The terrain under a unit that is using an item on the map (the acting unit, hidden while it acts, its move
   already made: src/bmusemind.c's StartBmPromotion), else -1 (the preparations, an event: no tile to check). */
static int P3UnitTerrain(const struct Unit *unit)
{
    int x = unit->xPos, y = unit->yPos;

    if ((gPlaySt.chapterStateBits & PLAY_FLAG_PREPSCREEN) || gActionData.unitActionType != UNIT_ACTION_USE_ITEM ||
        unit->index == 0 || gActionData.subjectIndex != (u8)unit->index || !(unit->state & US_HIDDEN))
        return -1;
    if (gBmMapTerrain == NULL || x < 0 || y < 0 || x >= gBmMapSize.x || y >= gBmMapSize.y || gBmMapTerrain[y] == NULL)
        return -1;
    return gBmMapTerrain[y][x];
}

/* After a promotion in a "Third promotion" game (Reforged_RandoPromoted): 1 when the class the unit took is a
   class the row added to its old class's screen (the table's, a stand-in or a fill; an FE8 class or a loaded class
   of FE7 or FE6; not the game's own, which stay the original's in every way). Such a class gets the randomizer's
   after-care: promotion gains when the game's data has none (Wyvern Lord and Druid for a woman: nobody promotes
   into them in the original), no rank left in a kind the class does not use (a Cavalier made a Trooper keeps no
   lance rank: the Trooper has no lances and no lance animation), and one basic weapon when it holds a weapon it could
   use before and can no longer use, and none it can. */
static int Promo3Care(const struct Unit *unit, int from)
{
    u8 l[3], src[3];
    int jid = unit->pClassData->number, i;

    if (!Reforged_Promo3Active() || !(ValidJid(jid) || (IsForeignJid(jid) && Fe7Loaded(jid))) ||
        Promo3ListEx(from, UnitIsFemale(unit), l, src, NULL) != 3)
        return 0;
    for (i = 0; i < 3; i++)
        if (l[i] == jid)
            return src[i] != P3SRC_GAME;
    return 0;
}

/* The twins keep their prf weapons whatever class they promote into (CA_LOCK_4, the Rapier and Sieglinde, for
   Eirika; CA_LOCK_5, Reginleif and Siegmund, for Ephraim; on the character, as a randomizer version 11-18 game does:
   a third may be no lord class) while the fix is on; in any other game without the randomizer, the characters' own
   bits. Called after the randomizer's start (reforged_rando.c Rando_Start, which puts the vanilla tables back) and
   when a save block's game fixes are read (reforged_savemark.c Reforged_SaveBlockRead). A randomized game's tables
   are the randomizer's. */
void Reforged_Promo3Tables(void)
{
    static const struct { u8 pid; u32 bit; } sLocks[2] = {
        { CHARACTER_EIRIKA, CA_LOCK_4 }, { CHARACTER_EPHRAIM, CA_LOCK_5 },
    };
    int i;

    if (gRandoRunOn)
        return;
    for (i = 0; i < 2; i++)
    {
        const struct CharacterData *vc = Rando_VanillaChar(sLocks[i].pid); /* (takes the snapshot first) */
        struct CharacterData *c = Rando_Char(sLocks[i].pid);

        if (vc == NULL || c == NULL)
            continue;
        c->attributes = (c->attributes & ~sLocks[i].bit) |
                        (Reforged_Promo3Active() ? sLocks[i].bit : (vc->attributes & sLocks[i].bit));
    }
}

/* ---- the self-test (FE8PC_PROMO3_CHECK, reforged_rando.c at the first boot) */

static int sP3Fails;

static void P3Fail(const char *what, int jid, int female, int roms, int x)
{
    if (sP3Fails++ < 40)
        PortLog("[promo3] self-test FAIL: %s (class 0x%02X %s, ROMs %s%s, value 0x%X)\n", what, jid, female ? "F" : "M",
            (roms & RANDO_ROM_FE7) ? "FE7" : "", (roms & RANDO_ROM_FE6) ? "+FE6" : (roms ? "" : "none"), x);
}

static void P3CareCheck(int jid, int female, int x, int roms, int *sims, int *fsims);

static const char *P3Name(int c, char *buf, int size)
{
    const struct ClassData *k = c != 0 ? GetClassData(c) : NULL;
    const char *s = NULL;

    if (IsForeignJid(c) && !Fe7Loaded(c))
        k = NULL;
    if (k != NULL)
        s = GetStringFromIndex(k->nameTextId);
    snprintf(buf, size, "%s(0x%02X)", s != NULL ? s : "?", c);
    return buf;
}

/* Every class with a promotion row x sex x ROM set (none, FE7, FE6, both: the loaded ROMs, the others taken as
   absent through sFe7Hide), with the fix on in a game without the randomizer: three different classes, the game's
   own first, the table's class wherever its ROM is loaded (else an FE8 stand-in: never a class of a missing ROM),
   the row's tier, a weapon kind of the unit's class (or, when none of the unit's sex and tier has one, logged), the
   unit's sex, every foreign choice with a battle animation for each kind it has a rank in and a save stand-in of the
   unit's sex, tier and a kind it uses; the masks used without a ROM equal the loaded classes'; the fix off (and in
   a randomized game) the screen's own choices; every class the row adds, taken by a unit of the class (P3CareCheck:
   a real ApplyUnitPromotion): some stat gained, no rank left in a kind the class does not use, a weapon it can use;
   the tile rule (P3TileBars). Logs the table for each ROM set. Returns the failures. */
int RandoClasses_Promo3SelfTest(void)
{
    static const int sSets[4] = { 0, RANDO_ROM_FE7, RANDO_ROM_FE6, RANDO_ROM_FE7 | RANDO_ROM_FE6 };
    int loaded = Rando_RomsLoaded(), keepHide = sFe7Hide, keepFix = Reforged_RunFix(RFO_FIX_PROMO3);
    int keepOn = gRandoRunOn, keepCarried = gRandoCarriedOn, set, lists = 0, foreign = 0, stand = 0, sets = 0, c;
    int sims = 0, fsims = 0, descs = 0;

    BuildFacts();
    sP3Fails = 0;
    gRandoRunOn = 0;
    gRandoCarriedOn = 0;
    Reforged_SetRunFix(RFO_FIX_PROMO3, 1);
    /* the masks without a ROM are the loaded classes' */
    for (c = 0x80; c <= REFORGED_FOREIGN_LAST_JID; c++)
        if (Fe7Loaded(c) && gPromoJidLut[c][0] == 0 && P3Mask(c) != P3MaskNoRom(c) &&
            (c == FJ_BLADE_LORD || c == FJ_BLADE_LORD + 1 || c == FJ_ARMOR_LORD || c == FJ_ARMOR_LORD + 1 ||
             c == FJ_ARCHSAGE || c == FJ_DARK_DRUID || c == FJ_TROOPER || c == FJ_TROOPER_F || c == FJ_MASTER_LORD))
            P3Fail("a foreign class's weapons differ from those used without its ROM", c, 0, loaded,
                P3Mask(c) * 256 + P3MaskNoRom(c));
    /* review round 3: the new class descriptions ("Equip: ...") are shown only in a game with the row on; a game
       with the row off and a randomized game keep v1.22.0's texts (the FE7 ROM's, the FE6 classes' three lines, the
       game's own for Great Lord, the third-tier Pupil and the promoted monsters) */
    {
        static const u8 sFix[3] = { CLASS_EPHRAIM_MASTER_LORD, CLASS_EIRIKA_MASTER_LORD, CLASS_PUPIL_T2 };
        char on[256], off[256];
        int i, state;

        for (state = 0; state < 3; state++) /* the row on; the row off; the row on in a randomized game */
        {
            Reforged_SetRunFix(RFO_FIX_PROMO3, state != 1);
            gRandoRunOn = state == 2;
            for (i = 0; i < 3; i++)
            {
                int d = Reforged_SkillPromoDesc(NULL, 0, sFix[i], 0x123);

                if (state == 0 ? (d == 0x123 || d < 0x7F50) : d != 0x123)
                    P3Fail("a promotion screen text that does not follow the row", sFix[i], 0, loaded, state * 0x10000 + d);
            }
        }
        gRandoRunOn = 0;
        for (c = 0x80; c <= REFORGED_FOREIGN_LAST_JID; c++)
        {
            if (!Reforged_Fe7ClassAvailable(c))
                continue;
            on[0] = off[0] = 0;
            Reforged_SetRunFix(RFO_FIX_PROMO3, 1);
            Reforged_Fe7VirtualMsg((c - 0x80) * 2 + 1, on);
            Reforged_SetRunFix(RFO_FIX_PROMO3, 0);
            Reforged_Fe7VirtualMsg((c - 0x80) * 2 + 1, off);
            if (on[0] == 0 || off[0] == 0 || strcmp(on, off) == 0 ||
                (strstr(on, "Equip:") == NULL && strstr(on, "cannot attack") == NULL) || strstr(off, "Equip:") != NULL)
                P3Fail("a foreign class's description does not follow the row", c, 0, loaded, 0);
            Reforged_SetRunFix(RFO_FIX_PROMO3, 1);
            gRandoRunOn = 1;
            on[0] = 0;
            Reforged_Fe7VirtualMsg((c - 0x80) * 2 + 1, on);
            gRandoRunOn = 0;
            if (strcmp(on, off) != 0)
                P3Fail("a foreign class's description changes in a randomized game", c, 0, loaded, 0);
            descs++;
        }
        Reforged_SetRunFix(RFO_FIX_PROMO3, 1);
        if (loaded != 0 && descs == 0)
            P3Fail("no foreign class's description was checked", 0, 0, loaded, 0);
    }
    for (set = 0; set < 4; set++)
    {
        int roms = sSets[set], jid, female;

        if ((roms & loaded) != roms)
            continue;
        sets++;
        sFe7Hide = (RANDO_ROM_FE7 | RANDO_ROM_FE6) & ~roms;
        PortLog("[promo3] self-test: ROMs %s:\n", roms == 0 ? "none" : roms == RANDO_ROM_FE7 ? "FE7" :
            roms == RANDO_ROM_FE6 ? "FE6" : "FE7+FE6");
        for (jid = 1; jid <= REFORGED_FOREIGN_LAST_JID && jid < gPromoJidLutCount; jid++)
            for (female = 0; female < 2; female++)
            {
                u8 l[3], v[3], src[3], wnt[3];
                int n = Promo3ListEx(jid, female, l, src, wnt), nv, i, j, want[2], nw, tier, b, first, special;
                char nm[4][48];

                special = IsForeignJid(jid) ? !Fe7Loaded(jid) : (Facts(jid) & F_SPECIAL) != 0;
                if (gPromoJidLut[jid][0] == 0 && gPromoJidLut[jid][1] == 0)
                {
                    if (n != 0)
                        P3Fail("a list for a class with no promotion", jid, female, roms, n);
                    continue;
                }
                if (TraineeThird(jid) != 0 || special)
                {
                    if (n != 0)
                        P3Fail("a list for a trainee's first promotion or a special class", jid, female, roms, n);
                    continue;
                }
                if (n != 3)
                {
                    P3Fail("a promotable class without three choices", jid, female, roms, n);
                    continue;
                }
                lists++;
                nv = P3Vanilla(jid, v);
                nw = Promo3Wanted(jid, female, want);
                first = gPromoJidLut[jid][0] != 0 ? gPromoJidLut[jid][0] : gPromoJidLut[jid][1];
                tier = P3Promoted(first);
                b = P3Mask(jid);
                for (i = 0; i < 3; i++)
                {
                    int x = l[i];

                    for (j = 0; j < i; j++)
                        if (l[j] == x || (src[i] != P3SRC_GAME && P3Twin(x) != 0 && P3Twin(x) == l[j]))
                            P3Fail("a repeat (or a twin) in a list", jid, female, roms, x);
                    if (i < nv && (x != v[i] || src[i] != P3SRC_GAME))
                        P3Fail("the list does not start with the game's own choices", jid, female, roms, x);
                    if (IsForeignJid(x) && !Fe7Loaded(x))
                        P3Fail("a class of a ROM that is not loaded", jid, female, roms, x);
                    if (src[i] == P3SRC_GAME)
                        continue;
                    if (P3Promoted(x) != tier)
                        P3Fail("a class of another tier", jid, female, roms, x);
                    if ((x == FJ_TROOPER && female) || (x == FJ_TROOPER_F && !female) || SexLocked(x, female) ||
                        (ValidJid(x) && sTwin[x] != 0 && (female ? Has(x, F_MALE) : Has(x, F_FEMALE))))
                        P3Fail("the other sex's class", jid, female, roms, x);
                    if ((P3Mask(x) & b) == 0 && b != 0 && src[i] == P3SRC_TABLE)
                        PortLog("[promo3] self-test: class 0x%02X %s: the table's 0x%02X has none of its kinds\n", jid,
                            female ? "F" : "M", x);
                    else if ((P3Mask(x) & b) == 0 && b != 0)
                    {
                        /* allowed only when no class of the unit's sex and tier left fits (logged) */
                        u8 seen[3];
                        int k = 0, alt;

                        for (j = 0; j < 3; j++)
                            if (j != i)
                                seen[k++] = l[j];
                        alt = Promo3Fill(jid, 0, female, seen, k);
                        if (alt != 0 && (P3Mask(alt) & b) != 0)
                            P3Fail("a choice with none of the unit's weapon kinds", jid, female, roms, x);
                        else
                            PortLog("[promo3] self-test: class 0x%02X %s: 0x%02X has none of its kinds (no class of "
                                "that sex and tier left has one)\n", jid, female ? "F" : "M", x);
                    }
                    if (src[i] == P3SRC_TABLE)
                    {
                        int ok = 0;

                        for (j = 0; j < nw; j++)
                            ok |= want[j] == x;
                        if (!ok)
                            P3Fail("a table class the table does not name", jid, female, roms, x);
                    }
                    if (src[i] == P3SRC_STANDIN && (IsForeignJid(x) || P3Available(wnt[i], female)))
                        P3Fail("a stand-in for an available class (or a foreign stand-in)", jid, female, roms, x);
                    if (IsForeignJid(x))
                    {
                        const struct ClassData *k = GetClassData(x);
                        int s = Reforged_Fe7Fallback(x, female), t;

                        foreign++;
                        for (t = 0; t < 8; t++)
                            if (k->baseRanks[t] != 0 && t != ITYPE_STAFF && !AnimHasType(k, t))
                                P3Fail("a foreign choice without a battle animation for a kind it has", jid, female,
                                    roms, x * 16 + t);
                        if (!ValidJid(s) || P3Promoted(s) != tier || (P3Mask(s) & P3Mask(x)) == 0 ||
                            (sTwin[s] != 0 && (female ? Has(s, F_MALE) : Has(s, F_FEMALE))))
                            P3Fail("a foreign choice's save stand-in is no class of the unit's sex, tier and weapons",
                                jid, female, roms, x * 256 + s);
                    }
                    if (src[i] == P3SRC_STANDIN)
                        stand++;
                    P3CareCheck(jid, female, x, roms, &sims, &fsims);
                }
                /* every table class whose ROM is loaded (and of the unit's sex) is there, unless three came first */
                for (j = 0; j < nw; j++)
                    if (P3Available(want[j], female))
                    {
                        int there = 0;

                        for (i = 0; i < 3; i++)
                            there |= l[i] == want[j] || (P3Twin(want[j]) != 0 && l[i] == P3Twin(want[j]));
                        if (!there && nv + j < 3)
                            P3Fail("a table class with its ROM loaded is missing", jid, female, roms, want[j]);
                    }
                {
                    static const char *const sTag[4] = { "", " [table]", " [stand-in]", " [fill]" };

                    PortLog("[promo3]   0x%02X %s: 0x%02X%s, 0x%02X%s, 0x%02X%s  (%s; %s, %s, %s)\n", jid,
                        female ? "F" : "M", l[0], sTag[src[0]], l[1], sTag[src[1]], l[2], sTag[src[2]],
                        P3Name(jid, nm[0], 48), P3Name(l[0], nm[1], 48), P3Name(l[1], nm[2], 48),
                        P3Name(l[2], nm[3], 48));
                }
            }
    }
    sFe7Hide = keepHide;
    /* the fix off, and in a randomized game: the screen's own (no list) */
    {
        u8 l[3];
        struct Unit u;

        memset(&u, 0, sizeof(u));
        u.pCharacterData = GetCharacterData(CHARACTER_FRANZ);
        u.pClassData = GetClassData(CLASS_CAVALIER);
        Reforged_SetRunFix(RFO_FIX_PROMO3, 0);
        if (Reforged_PromoList(&u, l) != 0 || Reforged_Promo3Active())
            P3Fail("the fix off gives a list", CLASS_CAVALIER, 0, loaded, 0);
        Reforged_SetRunFix(RFO_FIX_PROMO3, 1);
        if (Reforged_PromoList(&u, l) != 3)
            P3Fail("the fix on gives no list", CLASS_CAVALIER, 0, loaded, 0);
        gRandoCarriedOn = 1;
        if (Reforged_PromoList(&u, l) != 0)
            P3Fail("a carried randomized game gives a list", CLASS_CAVALIER, 0, loaded, 0);
        gRandoCarriedOn = 0;
    }
    /* the twins keep their prf weapons in any class with the fix on (Reforged_Promo3Tables), not with it off */
    {
        static const struct { u8 pid, cls, item; } sPrf[4] = {
            { CHARACTER_EIRIKA, CLASS_SWORDMASTER_F, ITEM_SWORD_SIEGLINDE }, { CHARACTER_EIRIKA, CLASS_SWORDMASTER_F, ITEM_SWORD_RAPIER },
            { CHARACTER_EPHRAIM, CLASS_GENERAL, ITEM_LANCE_SIEGMUND }, { CHARACTER_EPHRAIM, CLASS_GENERAL, ITEM_LANCE_REGINLEIF },
        };
        struct Unit u;
        int k, on;

        for (on = 1; on >= 0; on--)
        {
            Reforged_SetRunFix(RFO_FIX_PROMO3, on);
            Reforged_Promo3Tables();
            for (k = 0; k < 4; k++)
            {
                memset(&u, 0, sizeof(u));
                u.pCharacterData = GetCharacterData(sPrf[k].pid);
                u.pClassData = GetClassData(sPrf[k].cls);
                u.ranks[ITYPE_SWORD] = u.ranks[ITYPE_LANCE] = WPN_EXP_S;
                if ((CanUnitUseWeapon(&u, MakeNewItem(sPrf[k].item)) != 0) != on)
                    P3Fail(on ? "a twin cannot use a prf weapon in another class with the fix on"
                              : "a twin uses a prf weapon in another class with the fix off", sPrf[k].cls, sPrf[k].pid == CHARACTER_EIRIKA,
                        loaded, sPrf[k].item);
            }
        }
        if (GetCharacterData(CHARACTER_EIRIKA)->attributes != Rando_VanillaChar(CHARACTER_EIRIKA)->attributes ||
            GetCharacterData(CHARACTER_EPHRAIM)->attributes != Rando_VanillaChar(CHARACTER_EPHRAIM)->attributes)
            P3Fail("the twins' characters are not the vanilla ones with the fix off", 0, 0, loaded, 0);
    }
    /* the tile rule: a class the row adds that could not stand where the unit is bars the list there (a flier made
       a Great Knight over a peak, a Pirate an armoured class at sea); a flier's flying third and plain ground do
       not; a unit that is not using an item on the map has no tile */
    {
        static const struct { u8 jid, female, terrain, bars; } sTile[] = {
            { CLASS_WYVERN_RIDER, 0, TERRAIN_PEAK, 1 }, { CLASS_WYVERN_RIDER, 0, TERRAIN_SEA, 1 },
            { CLASS_WYVERN_RIDER, 0, TERRAIN_PLAINS, 0 }, { CLASS_WYVERN_RIDER_F, 1, TERRAIN_PEAK, 0 },
            { CLASS_PEGASUS_KNIGHT, 1, TERRAIN_PEAK, 0 }, { CLASS_PIRATE, 0, TERRAIN_SEA, 1 },
            { CLASS_PIRATE, 0, TERRAIN_PLAINS, 0 }, { CLASS_BRIGAND, 0, TERRAIN_PEAK, 1 },
            { CLASS_CAVALIER, 0, TERRAIN_PEAK, 0 }, { CLASS_MYRMIDON, 0, TERRAIN_PLAINS, 0 },
            /* the armoured third (Armor Lord or General) on a mountain or in a river */
            { CLASS_FIGHTER, 0, TERRAIN_MOUNTAIN, 1 }, { CLASS_FIGHTER, 0, TERRAIN_PLAINS, 0 },
            { CLASS_BRIGAND, 0, TERRAIN_MOUNTAIN, 1 }, { CLASS_BRIGAND, 0, TERRAIN_RIVER, 1 },
            { CLASS_PIRATE, 0, TERRAIN_MOUNTAIN, 1 }, { CLASS_PIRATE, 0, TERRAIN_RIVER, 1 },
            { CLASS_WYVERN_RIDER, 0, TERRAIN_RIVER, 1 }, { CLASS_JOURNEYMAN_T1, 1, TERRAIN_MOUNTAIN, 1 },
            { CLASS_JOURNEYMAN_T1, 0, TERRAIN_MOUNTAIN, 0 },
        };
        struct Unit u;
        u8 l[3], src[3];
        int k;

        Reforged_SetRunFix(RFO_FIX_PROMO3, 1);
        for (k = 0; k < (int)(sizeof(sTile) / sizeof(sTile[0])); k++)
        {
            int bar;

            if (Promo3ListEx(sTile[k].jid, sTile[k].female, l, src, NULL) != 3)
            {
                P3Fail("the tile rule: no list", sTile[k].jid, sTile[k].female, loaded, 0);
                continue;
            }
            bar = P3TileBars(GetClassData(sTile[k].jid), l, src, sTile[k].terrain);
            if ((bar != 0) != sTile[k].bars || (bar != 0 && bar != l[2]) ||
                (sTile[k].jid == CLASS_WYVERN_RIDER && sTile[k].bars && bar != CLASS_GREAT_KNIGHT))
                P3Fail("the tile rule gives the wrong answer", sTile[k].jid, sTile[k].female, loaded,
                    sTile[k].terrain * 256 + bar);
        }
        if (P3TileBars(GetClassData(CLASS_WYVERN_RIDER), l, src, -1) != 0)
            P3Fail("the tile rule bars a list with no tile", CLASS_WYVERN_RIDER, 0, loaded, 0);
        /* Ephraim's Lord: his General (and Armor Lord) cannot be on a mountain or in a river; plains are fine */
        if (Promo3ListEx(CLASS_EPHRAIM_LORD, 0, l, src, NULL) != 3 ||
            P3TileBars(GetClassData(CLASS_EPHRAIM_LORD), l, src, TERRAIN_MOUNTAIN) == 0 ||
            P3TileBars(GetClassData(CLASS_EPHRAIM_LORD), l, src, TERRAIN_RIVER) == 0 ||
            P3TileBars(GetClassData(CLASS_EPHRAIM_LORD), l, src, TERRAIN_PLAINS) != 0)
            P3Fail("the tile rule gives the wrong answer for Ephraim's Lord", CLASS_EPHRAIM_LORD, 0, loaded, 0);
        memset(&u, 0, sizeof(u));
        u.pCharacterData = GetCharacterData(CHARACTER_FRANZ);
        u.pClassData = GetClassData(CLASS_WYVERN_RIDER);
        if (P3UnitTerrain(&u) != -1 || Reforged_PromoList(&u, l) != 3)
            P3Fail("a unit that is not using an item on the map has a tile", CLASS_WYVERN_RIDER, 0, loaded, 0);
    }
    gRandoRunOn = keepOn;
    gRandoCarriedOn = keepCarried;
    Reforged_SetRunFix(RFO_FIX_PROMO3, keepFix);
    Reforged_Promo3Tables();
    PortLog("[promo3] self-test: %d ROM sets (FE7 classes %s, FE6 classes %s), %d lists of three, %d foreign choices, "
        "%d FE8 stand-ins, %d promotions tried, %d foreign promotions tried, %d failures\n", sets,
        (loaded & RANDO_ROM_FE7) ? "loaded" : "not loaded", (loaded & RANDO_ROM_FE6) ? "loaded" : "not loaded", lists,
        foreign, stand, sims, fsims, sP3Fails);
    return sP3Fails;
}

/* ---- Tower & Ruins (RFO_RANDO_FLOOR_MONSTERS, the user's request of 2026-11-06: "balanced
   to avoid facing a dracolich in the tower") */

/* A generated monster (the Tower of Valni, the Lagdou Ruins, the world map's skirmishes)
   becomes another monster of the same tier: an unpromoted one stays unpromoted, a promoted
   one promoted, never a Draco Zombie, the Demon King or a manakete. 0: not one of these. */
static int FloorMonsterFor(int orig, u32 h)
{
    static const u8 sLow[] = { CLASS_REVENANT, CLASS_BONEWALKER, CLASS_BONEWALKER_BOW, CLASS_BAEL,
                               CLASS_MAUTHEDOOG, CLASS_MOGALL, CLASS_GARGOYLE, CLASS_TARVOS };
    static const u8 sHigh[] = { CLASS_ENTOUMBED, CLASS_WIGHT, CLASS_WIGHT_BOW, CLASS_ELDER_BAEL, CLASS_GWYLLGI,
                                CLASS_ARCH_MOGALL, CLASS_DEATHGOYLE, CLASS_MAELDUIN, CLASS_CYCLOPS, CLASS_GORGON };
    int i;

    for (i = 0; i < (int)sizeof(sLow); i++)
        if (sLow[i] == orig)
            return sLow[Rando_HashInt(h, (int)sizeof(sLow))];
    for (i = 0; i < (int)sizeof(sHigh); i++)
        if (sHigh[i] == orig)
            return sHigh[Rando_HashInt(h, (int)sizeof(sHigh))];
    return 0;
}

/* ---- inventories */

/* The class's battle animations cover the item (ekrcmd.c GetBattleAnimationId: an entry
   for the item itself, or for its weapon type). */
static int AnimFits(int jid, int iid)
{
    const struct ClassData *k = Rando_Class(jid);
    const struct ItemData *it = Rando_VanillaItem(iid);
    const struct BattleAnimDef *d;

    if (k == NULL || it == NULL || k->pBattleAnimDef == NULL)
        return 0;
    for (d = k->pBattleAnimDef; d->wtype != 0; d++)
        if (d->wtype == iid || d->wtype - 0x100 == it->weaponType)
            return 1;
    return 0;
}

/* Version 4: the weapons of the monster class's kind its battle animations cover. */
static int MonsterKinFor(int jid, u8 *out)
{
    u8 kin[RANDO_KIN_MAX];
    int n = Rando_MonsterKin(jid, kin), i, m = 0;

    for (i = 0; i < n; i++)
        if (AnimFits(jid, kin[i]))
            out[m++] = kin[i];
    return m;
}

static int IsOwnMonsterWeapon(int jid, int iid)
{
    int i;

    for (i = 0; i < 3; i++)
        if (Rando_MonsterWeapon(jid, sPickAll[i]) == iid)
            return 1;
    if (gRandoRunOn && gRandoRun.version >= RANDO_VERSION_MAPFIT)
    {
        u8 kin[RANDO_KIN_MAX];
        int n = MonsterKinFor(jid, kin);

        for (i = 0; i < n; i++)
            if (kin[i] == iid)
                return 1;
    }
    return 0;
}

/* The unit's class can really fight with / heal with this item (rank, lock, and a
   type its class uses, so the battle animation exists). */
static int ItemFits(struct Unit *unit, int item)
{
    int jid = unit->pClassData->number;
    int iid = ITEM_INDEX(item);
    int attrs = GetItemAttributes(item);
    int type = GetItemType(item);

    if (attrs & IA_WEAPON)
    {
        if (Has(jid, F_MONSTERWPN))
            return IsOwnMonsterWeapon(jid, iid);
        if (!Rando_ItemIsWeapon(iid) || type < 0 || type >= 8 || !ClassUses(jid, type))
            return 0; /* a monster or special weapon on a human, or a type the class lacks */
        return CanUnitUseWeapon(unit, item);
    }
    if (attrs & IA_STAFF)
        return ClassUses(jid, ITYPE_STAFF) && CanUnitUseStaff(unit, item);
    return 0;
}

static int HasFitting(struct Unit *unit, int wantAttrs)
{
    int i;

    for (i = 0; i < UNIT_ITEM_COUNT && unit->items[i]; i++)
        if ((GetItemAttributes(unit->items[i]) & wantAttrs) && ItemFits(unit, unit->items[i]))
            return 1;
    return 0;
}

static int HasItem(struct Unit *unit, int iid)
{
    int i;

    for (i = 0; i < UNIT_ITEM_COUNT && unit->items[i]; i++)
        if (ITEM_INDEX(unit->items[i]) == iid)
            return 1;
    return 0;
}

/* HasItem, but for slot skip (the slot being replaced, which still holds its old item). */
static int HasItemElsewhere(struct Unit *unit, int iid, int skip)
{
    int i;

    for (i = 0; i < UNIT_ITEM_COUNT && unit->items[i]; i++)
        if (i != skip && ITEM_INDEX(unit->items[i]) == iid)
            return 1;
    return 0;
}

/* Adds an item; with US_DROP_ITEM set it goes before the last item, which stays the drop. */
static int AddItemKeepDrop(struct Unit *unit, int item)
{
    int count = GetUnitItemCount(unit);

    if (count >= UNIT_ITEM_COUNT || item == 0)
        return 0;
    if ((unit->state & US_DROP_ITEM) && count > 0)
    {
        unit->items[count] = unit->items[count - 1];
        unit->items[count - 1] = (u16)item;
    }
    else
        unit->items[count] = (u16)item;
    return 1;
}

/* A type the unit may be given a weapon of: its class uses it (a player also needs a
   rank, the character's ranks being the class's types already). */
static int TypeUsable(struct Unit *unit, int t, int isPlayer)
{
    return ClassUses(unit->pClassData->number, t) && (!isPlayer || unit->ranks[t] > 0);
}

/* A weapon (attack != 0) or staff of a type the unit can use, rank level near
   want; non-players get the rank it needs. 0 when there is none. */
static int PickFor(struct Unit *unit, int attack, int preferType, int want, int isPlayer, u32 h)
{
    u8 types[8];
    int n = 0, t, i, start, r;

    for (t = 0; t < 8; t++)
        if ((attack ? IsAttackType(t) : t == ITYPE_STAFF) && TypeUsable(unit, t, isPlayer))
            types[n++] = (u8)t;
    if (n == 0)
        return 0;
    start = Rando_HashInt(Mix(h, 1), n);
    for (i = 0; i < n; i++)
        if (types[i] == preferType)
            start = i;
    for (i = 0; i < n; i++)
    {
        int maxL, w;

        t = types[(start + i) % n];
        maxL = Rando_RankLevelOf(unit->ranks[t]);
        w = want >= WPN_LEVEL_E ? want : Max(maxL, WPN_LEVEL_E);
        if (!isPlayer && maxL < w)
            maxL = w;
        if (t == ITYPE_DARK && maxL < WPN_LEVEL_D)
            maxL = WPN_LEVEL_D; /* no E dark tome: dark users get D */
        if (maxL < WPN_LEVEL_E)
            maxL = WPN_LEVEL_E;
        r = Rando_PickWeapon(t, w, maxL, Mix(h, 2 + i));
        if (r == 0)
            continue;
        if (unit->ranks[t] < GetItemRequiredExp(r) && (!isPlayer || t == ITYPE_DARK))
            unit->ranks[t] = (u8)GetItemRequiredExp(r);
        if (unit->ranks[t] < GetItemRequiredExp(r))
            continue;
        return r;
    }
    return 0;
}

/* The replacement of an item the unit cannot use: same category (weapon / staff)
   first, the other one when the class has none of it; 0 to keep the item. */
static int ReplacementFor(struct Unit *unit, int item, int isPlayer, u32 h)
{
    int jid = unit->pClassData->number;
    int iid = ITEM_INDEX(item);
    int isWeapon = (GetItemAttributes(item) & IA_WEAPON) != 0;
    int want = Rando_ItemRankLevel(iid);
    int r;

    if (Has(jid, F_MONSTERWPN))
        return Rando_MonsterWeapon(jid, h);
    r = PickFor(unit, isWeapon, GetItemType(item), want, isPlayer, h);
    if (r == 0)
        r = PickFor(unit, !isWeapon, -1, want, isPlayer, Mix(h, 7));
    return r;
}

/* The tutorials' forced attacks use the first weapon only (src/events/ch1-tutorials.h
   DISABLEWEAPONS(EVENT_MENUOVERRIDE_WEAPON2), B ignored): Seth and Gilliam attack an
   adjacent enemy, Neimi and Artur one at range 2. Their first item must be a weapon they
   can use at that range, or tutorial mode cannot go on. A fitting weapon further down
   moves to the front; with none, the first slot gets one. */
static void FrontWeaponForRange(struct Unit *unit, int range, u32 h)
{
    int jid = unit->pClassData->number;
    int i, t, w, tmp;

    for (i = 0; i < UNIT_ITEM_COUNT && unit->items[i]; i++)
    {
        int item = unit->items[i];

        if ((GetItemAttributes(item) & IA_WEAPON) && ItemFits(unit, item)
            && GetItemMinRange(item) <= range && GetItemMaxRange(item) >= range)
        {
            if (i != 0)
            {
                tmp = unit->items[0];
                unit->items[0] = (u16)item;
                unit->items[i] = (u16)tmp;
            }
            return;
        }
    }
    for (t = 0; t < 8; t++)
    {
        int type = (t + Rando_HashInt(h, 8)) % 8;

        if (type == ITYPE_STAFF || !ClassUses(jid, type))
            continue;
        w = PickFor(unit, 1, type, WPN_LEVEL_E, 1, Mix(h, (u32)type));
        if (w && GetItemMinRange(w) <= range && GetItemMaxRange(w) >= range)
        {
            if (GetUnitItemCount(unit) < UNIT_ITEM_COUNT)
                for (i = GetUnitItemCount(unit); i > 0; i--)
                    unit->items[i] = unit->items[i - 1];
            unit->items[0] = (u16)MakeNewItem(w);
            return;
        }
    }
}

/* Version 4: the monster weapon a converted slot gets: the class's own pick (h) when the
   unit has none of it, else the first of its kind it lacks; 0 when it has them all. */
static int NextMonsterWeapon(struct Unit *unit, int jid, u32 h)
{
    u8 kin[RANDO_KIN_MAX];
    int main = Rando_MonsterWeapon(jid, h), n, i;

    if (main != 0 && !HasItem(unit, main))
        return main;
    n = MonsterKinFor(jid, kin);
    for (i = 0; i < n; i++)
        if (!HasItem(unit, kin[i]))
            return kin[i];
    return 0;
}

#define NODUPE_TRIES 12 /* version 5 */

static void RemoveSlot(struct Unit *unit, int i)
{
    for (; i < UNIT_ITEM_COUNT - 1; i++)
        unit->items[i] = unit->items[i + 1];
    unit->items[UNIT_ITEM_COUNT - 1] = 0;
}

/* After a promotion (bmbattle.c ApplyUnitPromotion, ApplyUnitDefaultPromotion; the hook
   RANDO_HOOK_PROMOTED). With "Random promotions" a unit can land in a class that uses none
   of what it holds (a Mercenary made a Sniper, a Fighter a Cyclops with Monster promos
   Mixed): it gets one basic weapon (or staff) the new class can use, in a free slot, or,
   when its slots are full, in place of the first item it can no longer use, which goes to
   the convoy (no room there either: no new weapon); everything else stays. The promotion scene then shows the after side with a weapon the new class has an
   animation for (GetBattleAnimationId finds none for a type the class lacks). */
/* The class a unit promotes from (RANDO_HOOK_PROMOTING, before ApplyUnitPromotion's stat
   ups), for Reforged_RandoPromoted. */
static struct Unit *sPromoUnit;
static int sPromoFrom;
static u8 sPromoRanks[8]; /* v1.23.0: its weapon ranks then ("Third promotion": P3KeepRanks) */
static u16 sPromoItems[UNIT_ITEM_COUNT]; /* review round 4: its items then, and which of them it could use */
static u8 sPromoUsable;

void Reforged_RandoPromoting(struct Unit *unit)
{
    Reforged_SkillPromoting(unit); /* Skills: the old class's skill passes on (reforged_skills.c) */
    sPromoUnit = unit;
    sPromoFrom = (unit != NULL && unit->pClassData != NULL) ? unit->pClassData->number : 0;
    sPromoUsable = 0;
    if (unit != NULL)
    {
        int i, status = unit->statusIndex;

        memcpy(sPromoRanks, unit->ranks, sizeof(sPromoRanks));
        unit->statusIndex = 0; /* (a Silence wears off: a silenced mage still "could use" its tome) */
        for (i = 0; i < UNIT_ITEM_COUNT; i++)
        {
            sPromoItems[i] = unit->items[i];
            if (unit->items[i] && CanUnitUseWeapon(unit, unit->items[i]))
                sPromoUsable |= (u8)(1 << i);
        }
        unit->statusIndex = status;
    }
}

/* "Third promotion": the unit could use this item before the promotion (Reforged_RandoPromoting's
   snapshot; without one, as if it could). */
static int P3WasUsable(int item, const u8 *ranksBefore)
{
    int i;

    if (ranksBefore != sPromoRanks)
        return 1;
    for (i = 0; i < UNIT_ITEM_COUNT; i++)
        if (sPromoItems[i] == item && (sPromoUsable & (1 << i)))
            return 1;
    return 0;
}

/* A class with no promotion gains in the game's data (the monsters: a player never
   promotes into one in the original) gives those of the unit's own vanilla promotion (a
   Fighter made an Entombed gains what a Hero would), else (a monster's own line has none)
   +3 HP, +2 Str/Mag, Skl, Def, Res, +1 Spd; each capped at the new class's maximum, so a
   monster promotion is not a weaker one. */
static void PromotionGainsOf(struct Unit *unit, int from, int own)
{
    const struct ClassData *k = unit->pClassData, *ref;
    int hp, g[6] = { 3, 2, 2, 1, 2, 2 }; /* the generic gains: about a Hero's or a Paladin's */

    if (k->promotionHp || k->promotionPow || k->promotionSkl || k->promotionSpd || k->promotionDef || k->promotionRes)
        return;
    ref = ValidJid(Rando_VanillaPromo(from, 0)) ? Rando_VanillaClass(Rando_VanillaPromo(from, 0)) : NULL;
    if (own != 0)
    {
        /* v1.23.0, "Third promotion": the class's other-sex twin's first (Wyvern Lord's for a woman's) */
        const struct ClassData *t = ValidJid(own) ? Rando_VanillaClass(own) : NULL;

        if (t != NULL && (t->promotionHp || t->promotionPow || t->promotionSkl || t->promotionSpd ||
                          t->promotionDef || t->promotionRes))
            ref = t;
    }
    if (ref != NULL && (ref->promotionHp || ref->promotionPow || ref->promotionSkl || ref->promotionSpd ||
                        ref->promotionDef || ref->promotionRes))
    {
        g[0] = ref->promotionHp; g[1] = ref->promotionPow; g[2] = ref->promotionSkl;
        g[3] = ref->promotionSpd; g[4] = ref->promotionDef; g[5] = ref->promotionRes;
    }
    else
        ref = NULL; /* a monster's own line has none either: the generic gains (g's start) */
    hp = unit->maxHP;
#define GAIN(stat, max, n) unit->stat = (u8)(unit->stat + (n) > k->max ? (unit->stat > k->max ? unit->stat : k->max) : unit->stat + (n))
    GAIN(maxHP, maxHP, g[0]);
    GAIN(pow, maxPow, g[1]);
    GAIN(skl, maxSkl, g[2]);
    GAIN(spd, maxSpd, g[3]);
    GAIN(def, maxDef, g[4]);
    GAIN(res, maxRes, g[5]);
#undef GAIN
    unit->curHP = (u8)(unit->curHP + (unit->maxHP - hp));
    if (unit->curHP > GetUnitMaxHp(unit))
        unit->curHP = GetUnitMaxHp(unit);
    if (!sQuiet)
        PortLog("[%s] promotion: class 0x%02X has no promotion gains: %s used\n", own != 0 ? "reforged" : "rando",
            k->number, ref == NULL ? "the generic ones" : (own != 0 && ref->number == own) ?
            "its other-sex twin's" : "the unit's usual promotion's");
}

static void PromotionGains(struct Unit *unit, int from)
{
    PromotionGainsOf(unit, from, 0);
}

static void RandoPromoted(struct Unit *unit, int from, const u8 *ranksBefore);

/* "Third promotion": a class the row added never costs the unit rank in a weapon kind the class uses. The game's
   ApplyUnitPromotion (src/bmbattle.c) takes the old class's base rank off and adds the new class's; every promotion
   of the original has a new base at least as high, but an added class can have a lower one (a Fighter, axe D, made
   a General, axe E; a Priest, staff D, made a Druid, staff E; an Archer, bow D, made a Warrior, bow E), which took
   30 points off a trained unit and with them the use of the Killer Axe or the Restore staff it held; a rank under
   the old class's base (a test unit's only) wrapped round to S. Here every kind the new class uses gets the higher
   of the rank before the promotion, the game's result (worked out without the wrap) and the class's base rank.
   before: the unit's ranks when the promotion began (Reforged_RandoPromoting). */
static void P3KeepRanks(struct Unit *unit, int from, const u8 *before)
{
    const struct ClassData *was = GetClassData(from), *k = unit->pClassData;
    int jid = k->number, t;

    for (t = 0; t < 8; t++)
    {
        int r;

        if (!ClassUses(jid, t))
            continue;
        r = before[t] - (was != NULL ? was->baseRanks[t] : 0) + k->baseRanks[t];
        if (r > WPN_EXP_S)
            r = WPN_EXP_S;
        if (r < before[t])
            r = before[t];
        if (r < k->baseRanks[t])
            r = k->baseRanks[t];
        unit->ranks[t] = (u8)r;
    }
}

/* "Third promotion": the one weapon a unit gets when an added class took the use of its own (RandoPromoted): the
   plain weapon of a kind the new class uses and the unit has a rank in (the kind by the unit's and the class's fixed
   hash, as before): Iron Sword, Iron Lance, Iron Axe, Iron Bow, Fire, Lightning or Flux, never the randomizer's
   pool of every E-rank weapon (which holds the Slim weapons, the Javelin and the monster-slayers Shadowkiller,
   Bright Lance, Fiendcleaver and Beacon Bow). Flux needs rank D: a dark user gets that rank, as from PickFor. */
static const u8 sP3Basic[8] = {
    [ITYPE_SWORD] = ITEM_SWORD_IRON, [ITYPE_LANCE] = ITEM_LANCE_IRON, [ITYPE_AXE] = ITEM_AXE_IRON,
    [ITYPE_BOW] = ITEM_BOW_IRON, [ITYPE_ANIMA] = ITEM_ANIMA_FIRE, [ITYPE_LIGHT] = ITEM_LIGHT_LIGHTNING,
    [ITYPE_DARK] = ITEM_DARK_FLUX,
};

static int P3BasicWeapon(struct Unit *unit, u32 h)
{
    u8 types[8];
    int n = 0, t, i, start;

    for (t = 0; t < 8; t++)
        if (IsAttackType(t) && TypeUsable(unit, t, 1))
            types[n++] = (u8)t;
    if (n == 0)
        return 0;
    start = Rando_HashInt(Mix(h, 1), n);
    for (i = 0; i < n; i++)
    {
        int w;

        t = types[(start + i) % n];
        w = sP3Basic[t];
        if (w == 0)
            continue;
        if (unit->ranks[t] < GetItemRequiredExp(w))
        {
            if (t != ITYPE_DARK)
                continue;
            unit->ranks[t] = (u8)GetItemRequiredExp(w);
        }
        return w;
    }
    return 0;
}

void Reforged_RandoPromoted(struct Unit *unit)
{
    int from = (unit == sPromoUnit) ? sPromoFrom : 0;
    const u8 *before = (unit != NULL && unit == sPromoUnit) ? sPromoRanks : NULL;

    sPromoUnit = NULL;
    RandoPromoted(unit, from, before);
    sP3Foreign = 0;
}

static void RandoPromoted(struct Unit *unit, int from, const u8 *ranksBefore)
{
    int jid, i, w = 0, staff, p3 = 0;
    u32 h;

    if (unit == NULL || unit->pClassData == NULL || unit->pCharacterData == NULL)
        return;
    if (!gRandoRunOn)
    {
        /* v1.23.0, "Third promotion": a class the row added gets the same care (Promo3Care); a class of FE7 or
           FE6 through its save stand-in's facts and its own ranks, as in a version 11-18 run (Fe7Stand) */
        if (!Promo3Care(unit, from))
            return;
        p3 = 1;
        sP3Foreign = IsForeignJid(unit->pClassData->number);
    }
    else if (!Rando_Opt(RFO_RANDO_PROMOTIONS))
    {
        /* versions 11-14: a class the run's fill added (not version 10's list) gets the same care */
        u8 l[3];
        int i, n;

        if (!Rando_ThreeRun() || from < 1 || from >= gPromoJidLutCount || from > REFORGED_FOREIGN_LAST_JID)
            return;
        n = V10List(from, UnitIsFemale(unit), TraineeThird(from) != 0 && Check3rdTraineeEnabled(), l);
        for (i = 0; i < n && l[i] != unit->pClassData->number; i++)
            ;
        if (i < n || (ValidJid(from) && Rando_VanillaClass(from) != NULL &&
                      Rando_VanillaClass(from)->promotion == unit->pClassData->number))
            return;
    }
    if (!ValidJid(unit->pClassData->number) && !Fe7Stand(unit->pClassData->number))
        return; /* an FE7 lord or Nomad (reforged_fe7.c): not in the randomizer's tables, it keeps its own ranks
                   (version 10's Archsage and Dark Druid go on: a Cleric made an Archsage gets a tome) */
    BuildFacts();
    if (!sFactsBuilt)
        return;
    jid = unit->pClassData->number;
    if (p3)
        PromotionGainsOf(unit, from, ValidJid(jid) && sTwin[jid] != 0 ? sTwin[jid] : jid);
    else if (ValidJid(from))
        PromotionGains(unit, from);
    if (p3 && ranksBefore != NULL)
        P3KeepRanks(unit, from, ranksBefore); /* no rank lost in a kind the added class uses */
    for (i = 0; i < 8; i++)
        if (!ClassUses(jid, i))
            unit->ranks[i] = 0; /* a Fighter made a Sniper no longer swings axes (EnemyRanks does the same) */
    /* (a "Third promotion" game has no seed: the same weapon for the same unit and class in every game) */
    h = p3 ? Mix(Mix(0x50334D33u, (u32)unit->pCharacterData->number), (u32)jid)
           : Rando_Hash(RANDO_SALT_INVENTORY, (u32)unit->pCharacterData->number, (u32)jid, 0x50524F4Du);
    staff = 0;
    if (p3)
    {
        /* only for a weapon lost: it holds one of a kind its old class used that it could use before the
           promotion and can no longer use, and none it can (an Archer made a Swordmaster gets a sword; a Cleric
           made a Sage gets no tome, as the original's Bishop gets none; a weapon it could never use, traded to
           it or above its rank, earns nothing: review round 4, an Archer with a C bow rank holding only a Silver
           Bow made a Warrior gets no Iron weapon) */
        const struct ClassData *was = GetClassData(from);
        int lost = 0;

        for (i = 0; i < UNIT_ITEM_COUNT && unit->items[i]; i++)
        {
            int type = GetItemType(unit->items[i]);

            lost |= (GetItemAttributes(unit->items[i]) & IA_WEAPON) && !ItemFits(unit, unit->items[i]) &&
                    was != NULL && type >= 0 && type < 8 && was->baseRanks[type] != 0 &&
                    P3WasUsable(unit->items[i], ranksBefore);
        }
        if (lost && Has(jid, F_ATTACK) && !HasFitting(unit, IA_WEAPON))
            w = Has(jid, F_MONSTERWPN) ? Rando_MonsterWeapon(jid, Mix(h, 21)) : P3BasicWeapon(unit, Mix(h, 21));
    }
    else if (Has(jid, F_ATTACK) && !HasFitting(unit, IA_WEAPON))
        w = Has(jid, F_MONSTERWPN) ? Rando_MonsterWeapon(jid, Mix(h, 21)) : PickFor(unit, 1, -1, WPN_LEVEL_E, 1, Mix(h, 21));
    else if (!Has(jid, F_ATTACK) && Has(jid, F_STAFF) && !HasFitting(unit, IA_STAFF))
    {
        w = PickFor(unit, 0, ITYPE_STAFF, WPN_LEVEL_E, 1, Mix(h, 20));
        staff = 1;
    }
    if (w != 0 && !AddItemKeepDrop(unit, MakeNewItem(w)))
    {
        /* full: the first item it can no longer use goes to the convoy, if there is room */
        for (i = 0; i < UNIT_ITEM_COUNT && unit->items[i]; i++)
            if ((GetItemAttributes(unit->items[i]) & (IA_WEAPON | IA_STAFF)) && !ItemFits(unit, unit->items[i]))
                break;
        if (i < UNIT_ITEM_COUNT && unit->items[i] && GetConvoyItemCount() < CONVOY_ITEM_COUNT &&
            AddItemToConvoy(unit->items[i]) >= 0)
            unit->items[i] = (u16)MakeNewItem(w);
        else
            w = 0; /* no room anywhere: it keeps what it has (trade or the convoy later) */
    }
    for (i = 0; i < UNIT_ITEM_COUNT && unit->items[i]; i++)
        if ((GetItemAttributes(unit->items[i]) & IA_WEAPON) && ItemFits(unit, unit->items[i]))
        {
            if (i != 0 && !(unit->state & US_DROP_ITEM && i == GetUnitItemCount(unit) - 1))
                EquipUnitItemSlot(unit, i); /* to the top: the weapon it fights with */
            break;
        }
    if ((unit->state & US_IN_BALLISTA) && !(UNIT_CATTRIBUTES(unit) & CA_BALLISTAE))
        TryRemoveUnitFromBallista(unit); /* out of an Archer's class into one with no ballista */
    if (!sQuiet)
        PortLog("[%s] promotion: unit 0x%02X is now class 0x%02X; new item 0x%02X%s\n", p3 ? "reforged" : "rando",
            unit->pCharacterData->number, jid, w, w == 0 ? " (none needed)" : staff ? " (a staff)" : " (a weapon)");
    if (gBattleActor.weapon != 0 && !ItemFits(unit, gBattleActor.weapon))
        gBattleActor.weapon = (u16)GetUnitEquippedWeapon(unit);
}

/* The self-test (RandoClasses_Promo3SelfTest): class x, which the row adds to class jid's screen, taken by a unit
   of the unit's sex in class jid a little above the class's bases, holding a basic weapon of the class's first
   kind, through the game's own ApplyUnitPromotion: a foreign class has promotion gains of its own; an FE8 class
   leaves the unit with some stat raised (Wyvern Lord and Druid for a woman have no gains in the game's data), no
   rank in a kind it does not use, and, when it attacks and the old class did, a weapon it can use. A class of FE7
   or FE6 the row adds is taken the same way by a trained unit (ranks 89 above the class's bases, holding a basic
   weapon of a kind the new class lacks when there is one: a Cavalier with a C lance rank and an Iron Lance made a
   Trooper): no rank left in a kind the class has none in, no weapon held that it can still use in such a kind, and
   a weapon it can use. */
static void P3CareCheck(int jid, int female, int x, int roms, int *sims, int *fsims)
{
    const struct ClassData *old = GetClassData(jid), *k = GetClassData(x);
    struct Unit u;
    int t, before, after, kind = -1, keepQuiet = sQuiet, fgn = !ValidJid(x);
    u16 keepWeapon = gBattleActor.weapon, held;
    u8 rb[8];

    if (old == NULL || k == NULL)
        return;
    if (fgn && !(k->promotionHp || k->promotionPow || k->promotionSkl || k->promotionSpd || k->promotionDef ||
                 k->promotionRes))
    {
        P3Fail("a foreign choice with no promotion gains", jid, female, roms, x);
        return;
    }
    memset(&u, 0, sizeof(u));
    u.pCharacterData = GetCharacterData(female ? CHARACTER_VANESSA : CHARACTER_FRANZ);
    u.pClassData = old;
    u.index = 0x40; /* (not a player unit: the skills' promotion hook leaves it alone) */
    u.level = 10;
    u.maxHP = u.curHP = (s8)(old->baseHP + 5);
    u.pow = (s8)(old->basePow + 3);
    u.skl = (s8)(old->baseSkl + 3);
    u.spd = (s8)(old->baseSpd + 3);
    u.def = (s8)(old->baseDef + 3);
    u.res = (s8)(old->baseRes + 3);
    for (t = 0; t < 8; t++)
        if (old->baseRanks[t] != 0)
        {
            u.ranks[t] = (u8)(old->baseRanks[t] < 250 ? old->baseRanks[t] + 2 : old->baseRanks[t]);
            if (fgn)
                u.ranks[t] = (u8)(old->baseRanks[t] + 89 < WPN_EXP_S ? old->baseRanks[t] + 89 : WPN_EXP_S);
            if (kind < 0 && IsAttackType(t))
                kind = t;
        }
    for (t = 0; fgn && t < 8; t++)
        if (old->baseRanks[t] != 0 && IsAttackType(t) && k->baseRanks[t] == 0)
            kind = t; /* a kind the foreign class lacks */
    if (kind >= 0)
    {
        int w = Rando_PickWeapon(kind, WPN_LEVEL_E, kind == ITYPE_DARK ? WPN_LEVEL_D : WPN_LEVEL_E, 0);

        if (w != 0)
            u.items[0] = (u16)MakeNewItem(w);
    }
    before = u.maxHP + u.pow + u.skl + u.spd + u.def + u.res;
    memcpy(rb, u.ranks, sizeof(rb));
    held = u.items[0];
    sQuiet = 1;
    ApplyUnitPromotion(&u, (u8)x);
    sQuiet = keepQuiet;
    gBattleActor.weapon = keepWeapon;
    after = u.maxHP + u.pow + u.skl + u.spd + u.def + u.res;
    /* review round 3: no rank lowered in a kind the added class uses (a Fighter made a General kept axe D less 30);
       a weapon handed out is the kind's plain one (never a Slim weapon or a monster-slayer from the randomizer's
       pool); a weapon it held of a kind the class uses is still usable */
    for (t = 0; t < 8; t++)
        if (k->baseRanks[t] != 0 && (u.ranks[t] < rb[t] || u.ranks[t] < k->baseRanks[t]))
            P3Fail("an added class lowers the rank in a kind it uses", jid, female, roms, x * 16 + t);
    for (t = 0; t < UNIT_ITEM_COUNT && u.items[t]; t++)
    {
        int iid = ITEM_INDEX(u.items[t]), type = GetItemType(u.items[t]);

        if (u.items[t] == held || Has(x, F_MONSTERWPN))
            continue;
        if (type < 0 || type >= 8 || sP3Basic[type] == 0 || sP3Basic[type] != iid)
            P3Fail("a weapon given that is not its kind's plain one", jid, female, roms, x * 256 + iid);
    }
    if (held != 0 && GetItemType(held) >= 0 && GetItemType(held) < 8 && k->baseRanks[GetItemType(held)] != 0 &&
        !Has(x, F_MONSTERWPN) && !CanUnitUseWeapon(&u, held))
        P3Fail("an added class takes the use of a held weapon of a kind it uses", jid, female, roms, x);
    {
        /* a rank under the old class's base (a test unit's): no wrap round to S, the class's base rank at least
           (a second promotion of a copy, not counted) */
        struct Unit lo;

        memset(&lo, 0, sizeof(lo));
        lo.pCharacterData = u.pCharacterData;
        lo.pClassData = old;
        lo.index = 0x40;
        lo.level = 10;
        lo.maxHP = lo.curHP = (s8)(old->baseHP + 5);
        for (t = 0; t < 8; t++)
            if (old->baseRanks[t] > 1)
                lo.ranks[t] = 1;
            else
                lo.ranks[t] = old->baseRanks[t];
        sQuiet = 1;
        ApplyUnitPromotion(&lo, (u8)x);
        sQuiet = keepQuiet;
        gBattleActor.weapon = keepWeapon;
        for (t = 0; t < 8; t++)
            if (k->baseRanks[t] != 0 && (lo.ranks[t] < k->baseRanks[t] ||
                                         (old->baseRanks[t] > 1 && lo.ranks[t] > k->baseRanks[t])))
                P3Fail("a rank under the old class's base wraps (or ends under the new class's base)", jid, female,
                    roms, x * 16 + t);
    }
    if (kind >= 0)
    {
        /* review round 4: a unit holding only a weapon of the old class's kind that it could not use before (above
           its rank: an Archer with a D bow rank and a Silver Bow) is given nothing, whatever the class (a copy) */
        struct Unit hi;
        int w = Rando_PickWeapon(kind, WPN_LEVEL_A, WPN_LEVEL_A, 0);

        memset(&hi, 0, sizeof(hi));
        hi.pCharacterData = u.pCharacterData;
        hi.pClassData = old;
        hi.index = 0x40;
        hi.level = 10;
        hi.maxHP = hi.curHP = (s8)(old->baseHP + 5);
        for (t = 0; t < 8; t++)
            hi.ranks[t] = old->baseRanks[t];
        if (w != 0)
            hi.items[0] = (u16)MakeNewItem(w);
        if (w != 0 && !CanUnitUseWeapon(&hi, hi.items[0]))
        {
            held = hi.items[0];
            sQuiet = 1;
            ApplyUnitPromotion(&hi, (u8)x);
            sQuiet = keepQuiet;
            gBattleActor.weapon = keepWeapon;
            if (hi.items[0] != held || hi.items[1] != 0)
                P3Fail("a weapon given for one the unit could never use", jid, female, roms, x * 256 + ITEM_INDEX(hi.items[1]));
        }
    }
    if (fgn)
    {
        int can = 0, attacks = 0;

        (*fsims)++;
        if (u.pClassData != k)
            P3Fail("a promotion that does not give the class", jid, female, roms, x);
        if (after <= before)
            P3Fail("an added foreign class raises no stat", jid, female, roms, x);
        for (t = 0; t < 8; t++)
        {
            attacks |= k->baseRanks[t] != 0 && IsAttackType(t);
            if (u.ranks[t] != 0 && k->baseRanks[t] == 0)
                P3Fail("an added foreign class leaves a rank in a kind it does not use", jid, female, roms, x * 16 + t);
        }
        for (t = 0; t < UNIT_ITEM_COUNT && u.items[t]; t++)
            if ((GetItemAttributes(u.items[t]) & IA_WEAPON) && CanUnitUseWeapon(&u, u.items[t]))
            {
                int type = GetItemType(u.items[t]);

                can = 1;
                if (type < 0 || type >= 8 || k->baseRanks[type] == 0)
                    P3Fail("an added foreign class still uses a weapon of a kind it does not have", jid, female, roms,
                        x * 256 + ITEM_INDEX(u.items[t]));
            }
        if (u.items[0] != 0 && attacks && !can)
            P3Fail("an added foreign class leaves the unit no weapon it can use", jid, female, roms, x);
        if (sP3Foreign)
            P3Fail("the foreign after-care's switch is left on", jid, female, roms, x);
        return;
    }
    (*sims)++;
    if (u.pClassData != k)
        P3Fail("a promotion that does not give the class", jid, female, roms, x);
    if (after <= before)
        P3Fail("an added FE8 class raises no stat", jid, female, roms, x);
    for (t = 0; t < 8; t++)
        if (u.ranks[t] != 0 && !ClassUses(x, t))
            P3Fail("an added FE8 class leaves a rank in a kind it does not use", jid, female, roms, x * 16 + t);
    if (u.items[0] != 0 && Has(x, F_ATTACK) && !HasFitting(&u, IA_WEAPON))
        P3Fail("an added FE8 class leaves the unit no weapon it can use", jid, female, roms, x);
}

/* The promotion scenes (banim-ekrbattleintro.c, the promotion screen's preview in
   classchg-sel.c) look a class's battle animation up by the weapon shown; for a weapon type
   the class has no animation for, GetBattleAnimationId gives -1 and banim_data[-1] is read.
   With Random promotions that happens (a Fighter's axe on a Sniper, a Cleric with no weapon
   on a Sage). Then: the class's "no weapon" animation, else its first one, as the promotion
   screen's own first preview does (LoadClassBattleSprite). A found animation is kept. */
int Reforged_BanimFallback(const void *animDef, int idx, u32 *out)
{
    const struct BattleAnimDef *d = animDef;
    int j, first = -1, firstJ = 0;

    if (idx >= 0 || d == NULL)
        return idx;
    for (j = 0; d[j].wtype != 0 && j < 64; j++)
    {
        if (d[j].index == 0)
            continue;
        if (d[j].wtype == SPECIAL_BANIM_WTYPE)
        {
            if (out != NULL)
                *out = (u32)j;
            return d[j].index - 1;
        }
        if (first < 0)
        {
            first = d[j].index - 1;
            firstJ = j;
        }
    }
    if (first >= 0 && out != NULL)
        *out = (u32)firstJ;
    if (!sQuiet)
        PortLog("[rando] battle animation: no animation for the weapon shown: animation %d instead\n", first);
    return first;
}

/* A class change's inventory (UFER validateCharacterInventory, with slots kept). */
static void RepairInventory(struct Unit *unit, const struct UnitKey *k, int isPlayer)
{
    int jid = unit->pClassData->number;
    const struct ClassData *ko = Rando_VanillaClass(k->origClass);
    u32 h = Rando_Hash(RANDO_SALT_INVENTORY, k->a, k->b, k->c);
    int unarmed = Chapter() == CHAPTER_L_PROLOGUE && k->pid == CHARACTER_EIRIKA; /* Seth hands her the Rapier */
    int i;

    for (i = 0; i < UNIT_ITEM_COUNT && unit->items[i]; i++)
    {
        int item = unit->items[i], r;

        if (!(GetItemAttributes(item) & (IA_WEAPON | IA_STAFF)) || ItemFits(unit, item))
            continue; /* keys, vulneraries, promotion items, the Member Card stay */
        if (k->floor && (unit->state & US_DROP_ITEM) && i == GetUnitItemCount(unit) - 1)
            continue; /* Tower & Ruins: a monster's drop (a Runesword, a Killer Bow) stays the prize */
        if (gRandoRun.version >= RANDO_VERSION_MAPFIT && Has(jid, F_MONSTERWPN))
        {
            /* version 4: each weapon becomes one the unit does not have yet, its class's
               own pick first, then the others of its kind; past those the slot goes (a
               copy is no use), unless it is the item the unit drops */
            r = NextMonsterWeapon(unit, jid, Mix(h, (u32)i));
            if (r == 0)
            {
                int last = GetUnitItemCount(unit) - 1;

                if ((unit->state & US_DROP_ITEM) && i == last)
                    r = Rando_MonsterWeapon(jid, Mix(h, (u32)i));
                else
                {
                    RemoveSlot(unit, i);
                    i--;
                    continue;
                }
            }
        }
        else
        {
            r = ReplacementFor(unit, item, isPlayer, Mix(h, (u32)i));
            /* version 5: not a second copy of an item the unit holds (with Unbreakable
               weapons a copy is no use at all); up to NODUPE_TRIES - 1 more picks keyed
               on the same slot, the first it lacks; when the class has nothing else to
               give (a Shaman's only starting tome is Flux), the slot goes, as a monster's
               extra weapons do, unless it is the item the unit drops */
            if (gRandoRun.version >= RANDO_VERSION_NODUPES && r != 0 && HasItemElsewhere(unit, r, i))
            {
                int try_, other = 0;

                for (try_ = 1; try_ < NODUPE_TRIES && other == 0; try_++)
                {
                    int c = ReplacementFor(unit, item, isPlayer, Mix(Mix(h, (u32)i), 0x100u + (u32)try_));

                    if (c != 0 && !HasItemElsewhere(unit, c, i))
                        other = c;
                }
                if (other != 0)
                    r = other;
                else if (!((unit->state & US_DROP_ITEM) && i == GetUnitItemCount(unit) - 1))
                {
                    RemoveSlot(unit, i);
                    i--;
                    continue;
                }
            }
        }
        if (r != 0)
            unit->items[i] = (u16)MakeNewItem(r);
    }

    /* a former thief: the Lockpick (useless now) becomes a Chest Key */
    if (ko != NULL && (ko->attributes & CA_STEAL) && !(UNIT_CATTRIBUTES(unit) & CA_THIEF))
        for (i = 0; i < UNIT_ITEM_COUNT && unit->items[i]; i++)
            if (ITEM_INDEX(unit->items[i]) == ITEM_LOCKPICK)
                unit->items[i] = (u16)MakeNewItem(ITEM_CHESTKEY);

    if (!unarmed && k->hadItems)
    {
        /* a healer with no staff gets a healing staff, an attacker with no weapon a basic one */
        if (Has(jid, F_STAFF) && !HasFitting(unit, IA_STAFF))
        {
            int s = PickFor(unit, 0, ITYPE_STAFF, WPN_LEVEL_E, isPlayer, Mix(h, 20));

            if (s)
                AddItemKeepDrop(unit, MakeNewItem(s));
        }
        if (Has(jid, F_ATTACK) && !HasFitting(unit, IA_WEAPON))
        {
            int w = Has(jid, F_MONSTERWPN) ? Rando_MonsterWeapon(jid, Mix(h, 21))
                                            : PickFor(unit, 1, -1, WPN_LEVEL_E, isPlayer, Mix(h, 21));

            if (w)
                AddItemKeepDrop(unit, MakeNewItem(w));
        }
    }

    /* a new thief (a player) gets a Lockpick */
    if (isPlayer && (UNIT_CATTRIBUTES(unit) & CA_THIEF) && !HasItem(unit, ITEM_LOCKPICK))
        AddItemKeepDrop(unit, MakeNewItem(ITEM_LOCKPICK));

    if (!unarmed && (k->pid == CHARACTER_SETH || k->pid == CHARACTER_GILLIAM))
        FrontWeaponForRange(unit, 1, Mix(h, 30));
    else if (!unarmed && (k->pid == CHARACTER_NEIMI || k->pid == CHARACTER_ARTUR))
        FrontWeaponForRange(unit, 2, Mix(h, 31));
}

/* A boss's ranks after a class change: every type the new class uses gets the boss's
   highest vanilla rank (UFER transferBossWeaponLevels), the others none. */
static void BossRanks(struct Unit *unit, const struct UnitKey *k)
{
    const struct CharacterData *vc = Rando_VanillaChar(k->pid);
    const struct ClassData *ko = Rando_VanillaClass(k->origClass);
    const struct ClassData *kn = unit->pClassData;
    int best = 0, t;

    for (t = 0; t < 8 && vc != NULL && ko != NULL; t++)
        best = Max(best, vc->baseRanks[t] ? vc->baseRanks[t] : ko->baseRanks[t]);
    for (t = 0; t < 8; t++)
    {
        if (!kn->baseRanks[t])
            unit->ranks[t] = 0;
        else
        {
            int v = Max(Max(best, kn->baseRanks[t]), unit->ranks[t]);

            if (t == ITYPE_DARK && v < WPN_EXP_D)
                v = WPN_EXP_D;
            unit->ranks[t] = (u8)Min(v, WPN_EXP_S);
        }
    }
}

/* An enemy's ranks after a class change: the new class's types only. */
static void EnemyRanks(struct Unit *unit)
{
    const struct ClassData *kn = unit->pClassData;
    int t;

    for (t = 0; t < 8; t++)
        unit->ranks[t] = kn->baseRanks[t] ? (u8)Max(unit->ranks[t], kn->baseRanks[t]) : 0;
}

/* ---- enemy options */

/* The story position of a chapter: prologue 0 .. the final chapter 23 (both routes'
   chapters 9-21x side by side); the dungeons count as the end, the rest as mid-game. */
static int StoryChapter(int ch)
{
    static const u8 sEirika[14] = {
        CHAPTER_E_9, CHAPTER_E_10, CHAPTER_E_11, CHAPTER_E_12, CHAPTER_E_13, CHAPTER_E_14, CHAPTER_E_15,
        CHAPTER_E_16, CHAPTER_E_17, CHAPTER_E_18, CHAPTER_E_19, CHAPTER_E_20, CHAPTER_E_21, CHAPTER_E_21X,
    };
    static const u8 sEphraim[14] = {
        CHAPTER_I_9, CHAPTER_I_10, CHAPTER_I_11, CHAPTER_I_12, CHAPTER_I_13, CHAPTER_I_14, CHAPTER_I_15,
        CHAPTER_I_16, CHAPTER_I_17, CHAPTER_I_18, CHAPTER_I_19, CHAPTER_I_20, CHAPTER_I_21, CHAPTER_I_21X,
    };
    int i;

    if (ch >= CHAPTER_L_PROLOGUE && ch <= CHAPTER_L_4)
        return ch;
    if (ch == CHAPTER_L_5X)
        return 5;
    if (ch >= CHAPTER_L_5 && ch <= CHAPTER_L_8)
        return ch; /* Ch5 6 .. Ch8 9 */
    for (i = 0; i < 14; i++)
        if (sEirika[i] == ch || sEphraim[i] == ch)
            return 10 + i;
    if (ch >= CHAPTER_T_01 && ch <= CHAPTER_R_10)
        return STORY_LAST;
    return STORY_LAST / 2;
}

/* UFER's boss buff: min(max, ceil(max * t)), t = story / 23 (linear) or eased. */
static int BossBuffAmount(int mode, int amt, int story)
{
    long num, den;

    if (amt <= 0 || story <= 0)
        return 0;
    if (mode == 1)
    {
        num = (long)amt * story;
        den = STORY_LAST;
    }
    else
    {
        long s2 = (long)story * story, r2 = (long)(STORY_LAST - story) * (STORY_LAST - story);

        num = (long)amt * s2;
        den = s2 + r2;
    }
    return Min(amt, (int)((num + den - 1) / den));
}

/* UFER's minion rule: bases + class bases kept in [0, class cap]. A generic character's
   personal bases are offsets tuned for its vanilla class (some are negative), so after a
   class change a stat can fall below 0 (a level 1 Soldier with Skl -1, stored as 31). */
static void ClampStats(struct Unit *unit)
{
    const struct ClassData *k = unit->pClassData;
    s8 *v[7];
    int cap[7], i;

    v[0] = &unit->maxHP; cap[0] = Min(k->maxHP, STAT_HP_MAX);
    v[1] = &unit->pow;   cap[1] = Min(k->maxPow, STAT_MAX);
    v[2] = &unit->skl;   cap[2] = Min(k->maxSkl, STAT_MAX);
    v[3] = &unit->spd;   cap[3] = Min(k->maxSpd, STAT_MAX);
    v[4] = &unit->def;   cap[4] = Min(k->maxDef, STAT_MAX);
    v[5] = &unit->res;   cap[5] = Min(k->maxRes, STAT_MAX);
    v[6] = &unit->lck;   cap[6] = STAT_LCK_MAX;
    for (i = 0; i < 7; i++)
        *v[i] = (s8)Clamp(*v[i], i == 0 ? 1 : 0, cap[i]);
}

static void BossBuff(struct Unit *unit)
{
    const struct ClassData *k = unit->pClassData;
    int buff = BossBuffAmount(Rando_Opt(RFO_RANDO_BOSS_BUFF), Rando_Opt(RFO_RANDO_BOSS_BUFF_AMT), StoryChapter(Chapter()));
    s8 *v[7];
    int cap[7], i;

    if (buff <= 0)
        return;
    v[0] = &unit->maxHP; cap[0] = Min(k->maxHP, STAT_HP_MAX);
    v[1] = &unit->pow;   cap[1] = Min(k->maxPow, STAT_MAX);
    v[2] = &unit->skl;   cap[2] = Min(k->maxSkl, STAT_MAX);
    v[3] = &unit->spd;   cap[3] = Min(k->maxSpd, STAT_MAX);
    v[4] = &unit->def;   cap[4] = Min(k->maxDef, STAT_MAX);
    v[5] = &unit->res;   cap[5] = Min(k->maxRes, STAT_MAX);
    v[6] = &unit->lck;   cap[6] = STAT_LCK_MAX;
    for (i = 0; i < 7; i++)
        if (*v[i] < cap[i])
            *v[i] = (s8)Min(*v[i] + buff, cap[i]);
}

/* UFER's improved enemy weapons: each weapon below A becomes one of the next rank. */
static void BetterWeapons(struct Unit *unit, const struct UnitKey *k)
{
    u32 h = Rando_Hash(RANDO_SALT_ENEMY + 1, k->a, k->b, k->c);
    int i;

    if (Rando_HashInt(h, 100) >= Rando_Opt(RFO_RANDO_ENEMY_WEAPONS))
        return;
    for (i = 0; i < UNIT_ITEM_COUNT && unit->items[i]; i++)
    {
        int item = unit->items[i], iid = ITEM_INDEX(item), type, lvl, next;

        if (!Rando_ItemIsWeapon(iid) || Rando_ItemIsPrf(iid) || Rando_ItemIsLegendary(iid))
            continue;
        if (GetItemMaxRange(item) >= 10)
            continue; /* siege tomes keep their place */
        type = GetItemType(item);
        lvl = Rando_ItemRankLevel(iid);
        if (lvl < WPN_LEVEL_E || lvl >= WPN_LEVEL_A || !ClassUses(unit->pClassData->number, type))
            continue;
        next = Rando_PickWeapon(type, lvl + 1, lvl + 1, Mix(h, 10 + (u32)i));
        /* version 5: not an upgrade the unit already holds; another of the rank, else the
           slot keeps its weapon */
        if (gRandoRun.version >= RANDO_VERSION_NODUPES && next != 0 && HasItemElsewhere(unit, next, i))
        {
            int try_, other = 0;

            for (try_ = 1; try_ < NODUPE_TRIES && other == 0; try_++)
            {
                int c = Rando_PickWeapon(type, lvl + 1, lvl + 1, Mix(Mix(h, 10 + (u32)i), 0x100u + (u32)try_));

                if (c != 0 && !HasItemElsewhere(unit, c, i))
                    other = c;
            }
            next = other;
        }
        if (next == 0 || Rando_ItemRankLevel(next) <= lvl)
            continue;
        unit->items[i] = (u16)MakeNewItem(next);
        if (unit->ranks[type] < GetItemRequiredExp(next))
            unit->ranks[type] = (u8)GetItemRequiredExp(next);
    }
}

/* Drop tiers (UFER: common / uncommon / rare by the quarter of the story). */
static const u8 sDropCommon[] = { ITEM_VULNERARY, ITEM_ANTITOXIN, ITEM_PUREWATER, ITEM_TORCH, ITEM_REDGEM };
static const u8 sDropUncommon[] = {
    ITEM_ELIXIR, ITEM_BLUEGEM, ITEM_HEROCREST, ITEM_KNIGHTCREST, ITEM_ORIONSBOLT, ITEM_ELYSIANWHIP,
    ITEM_GUIDINGRING, ITEM_OCEANSEAL,
};
static const u8 sDropRare[] = {
    ITEM_BOOSTER_HP, ITEM_BOOSTER_POW, ITEM_BOOSTER_SKL, ITEM_BOOSTER_SPD, ITEM_BOOSTER_LCK, ITEM_BOOSTER_DEF,
    ITEM_BOOSTER_RES, ITEM_BOOSTER_MOV, ITEM_BOOSTER_CON, ITEM_MASTERSEAL, ITEM_WHITEGEM,
};
static const u8 sDropWeights[4][2] = { { 90, 9 }, { 70, 20 }, { 60, 30 }, { 50, 30 } }; /* common, uncommon; rare the rest */

static int DropItem(u32 h)
{
    int q = Clamp(StoryChapter(Chapter()) * 4 / (STORY_LAST + 1), 0, 3);
    int roll = Rando_HashInt(Mix(h, 2), 100);
    const u8 *list;
    int n;

    if (roll < sDropWeights[q][0])
    {
        list = sDropCommon;
        n = sizeof(sDropCommon);
    }
    else if (roll < sDropWeights[q][0] + sDropWeights[q][1])
    {
        list = sDropUncommon;
        n = sizeof(sDropUncommon);
    }
    else
    {
        list = sDropRare;
        n = sizeof(sDropRare);
    }
    return list[Rando_HashInt(Mix(h, 3), n)];
}

static void Drops(struct Unit *unit, const struct UnitKey *k)
{
    u32 h = Rando_Hash(RANDO_SALT_DROPS, k->a, k->b, k->c);
    int count;

    if (Rando_HashInt(h, 100) >= Rando_Opt(RFO_RANDO_DROPS))
        return;
    unit->state |= US_DROP_ITEM; /* the last item drops */
    if (Rando_HashInt(Mix(h, 1), 4) == 0)
        return; /* 1 in 4: what it holds */
    count = GetUnitItemCount(unit);
    if (count < UNIT_ITEM_COUNT)
        unit->items[count] = (u16)MakeNewItem(Rando_SealItem(DropItem(h)));
}

void Reforged_RandoUnitLoaded(struct Unit *unit, const struct UnitDefinition *uDef)
{
    struct UnitKey k = sLast;
    int pid, jid, changed, isPlayer;

    memset(&sLast, 0, sizeof(sLast));
    (void)uDef;
    Reforged_RandoUnitJoined(unit); /* Nuzlocke: a unit in the graveyard that joins again dies */
    RandoItems_SealUnit(unit);      /* "Master Seals only": the unit's own promotion items */
    if (!gRandoRunOn || unit == NULL || unit->pCharacterData == NULL || unit->pClassData == NULL || !k.valid)
        return;
    pid = unit->pCharacterData->number;
    jid = unit->pClassData->number;
    if (pid != k.pid || UNIT_IS_GORGON_EGG(unit) || UNIT_IS_PHANTOM(unit))
        return;
    isPlayer = Rando_IsPlayable(pid);
    changed = jid != k.origClass && (k.kind == UK_PLAYER || k.kind == UK_BOSS || k.kind == UK_ENEMY);

    if (changed)
    {
        if (k.kind == UK_BOSS)
            BossRanks(unit, &k);
        else if (k.kind == UK_ENEMY)
            EnemyRanks(unit);
        if (k.kind == UK_BOSS || k.kind == UK_ENEMY)
            ClampStats(unit);
        RepairInventory(unit, &k, isPlayer);
    }

    if (UNIT_FACTION(unit) == FACTION_BLUE || isPlayer)
        return;
    if (Rando_Opt(RFO_RANDO_BOSS_BUFF) && IsBossPid(pid) && pid != CHARACTER_VALTER_PROLOGUE)
        BossBuff(unit); /* UFER doNotBuff: the prologue's Valter (Seth must survive him) */
    if (Rando_Opt(RFO_RANDO_ENEMY_WEAPONS))
        BetterWeapons(unit, &k);
    if (Rando_Opt(RFO_RANDO_DROPS) && UNIT_FACTION(unit) == FACTION_RED && !IsBossPid(pid) &&
        !(unit->state & US_DROP_ITEM))
        Drops(unit, &k);
}

/* ------------------------------------------------------------------ promotions, events */

/* The unit's class differs from its character's vanilla class in this run. */
static int ClassRandomized(struct Unit *unit)
{
    const struct CharacterData *vc = Rando_VanillaChar(unit->pCharacterData->number);
    int pid = unit->pCharacterData->number;

    if (vc == NULL)
        return 0;
    if (Rando_IsPlayable(pid))
        return RandoClasses_PlayerClass(pid) != vc->defaultClass;
    return !InFamily(vc->defaultClass, unit->pClassData->number);
}

int Reforged_RandoPromoteJid(struct Unit *unit, int jid)
{
    int cur, p;

    /* the FE7 classes (reforged_fe7.c): a unit already in a promoted FE7 class is not promoted again
       (Chapter 16's Lunar Brace event, PROM Eirika -> Master Lord, when she took Blade Lord), in every
       run, randomized or not */
    if (unit != NULL && unit->pClassData != NULL && !ValidJid(unit->pClassData->number) &&
        (unit->pClassData->attributes & CA_PROMOTED))
    {
        PortLog("[reforged] promotion event: pid 0x%02X is already in promoted class 0x%02X: skipped\n",
            unit->pCharacterData != NULL ? unit->pCharacterData->number : 0, unit->pClassData->number);
        return 0; /* Event40_PromoteUnit ends the command */
    }
    /* versions 11-14 (and v1.23.0's "Third promotion"): a twin who already took a promoted class (the brace's screen
       offers three, a third may be no lord class) is not promoted again */
    if ((Rando_ThreeRun() || Reforged_Promo3Active()) && unit != NULL && unit->pCharacterData != NULL && unit->pClassData != NULL &&
        IsLordPid(unit->pCharacterData->number) && (unit->pClassData->attributes & CA_PROMOTED))
    {
        if (!sQuiet)
            PortLog("[%s] promotion event: unit 0x%02X is already promoted (class 0x%02X): skipped\n",
                Rando_ThreeRun() ? "rando" : "reforged", unit->pCharacterData->number, unit->pClassData->number);
        return 0;
    }
    if (!gRandoRunOn || unit == NULL || unit->pCharacterData == NULL || unit->pClassData == NULL ||
        !Rando_IsPlayable(unit->pCharacterData->number) || !ClassRandomized(unit))
        return jid;
    BuildFacts();
    cur = unit->pClassData->number;
    if (VanillaPromoted(cur))
    {
        if (!sQuiet)
            PortLog("[rando] promotion event: unit 0x%02X is already promoted (class 0x%02X): skipped\n",
                unit->pCharacterData->number, cur);
        return 0; /* Event40_PromoteUnit ends the command (a second promotion would reset the level) */
    }
    if (IsPromotionOf(cur, jid) && VanillaPromoted(jid))
        return jid;
    p = PromotionOf(cur);
    if (p == 0)
        return jid;
    if (!sQuiet)
        PortLog("[rando] promotion event: unit 0x%02X, class 0x%02X -> 0x%02X (event asked 0x%02X)\n",
            unit->pCharacterData->number, cur, p, jid);
    return Reforged_RandoPromoChoice(unit, p);
}

int Reforged_RandoEventBattleItem(struct Unit *unit, int item)
{
    int w;

    if (!gRandoRunOn || unit == NULL || item == 0 || unit->pCharacterData == NULL || unit->pClassData == NULL)
        return item;
    if (!(GetItemAttributes(item) & IA_WEAPON) || !ClassRandomized(unit))
        return item;
    BuildFacts();
    if (ItemFits(unit, item))
        return item;
    /* StartEventBattle reads item once before this hook (item != 0 set the battle up as
       an item effect), so 0 would leave it half built: the unit's own weapon instead
       takes the forced weapon's path, which builds the real battle. */
    w = GetUnitEquippedWeapon(unit);
    if (w == 0)
        return item;
    if (!sQuiet)
        PortLog("[rando] scripted battle: unit 0x%02X fights with item 0x%02X instead of 0x%02X\n",
            unit->pCharacterData->number, ITEM_INDEX(w), ITEM_INDEX(item));
    return ITEM_INDEX(w);
}

/* reforged_fe7.c Reforged_Fe7EventPromoMenu (Chapter 16's PROM): 1 when a twin's event promotion opens the
   three-choice screen (a version 11-14 run, or v1.23.0's "Third promotion"; the twin in an unpromoted class with
   three choices). */
int Reforged_RandoEventPromoMenu(struct Unit *unit)
{
    u8 l[3];

    return (Rando_ThreeRun() || Reforged_Promo3Active()) && unit != NULL && unit->pCharacterData != NULL && unit->pClassData != NULL &&
           IsLordPid(unit->pCharacterData->number) && !(unit->pClassData->attributes & CA_PROMOTED) &&
           Reforged_PromoList(unit, l) == 3;
}

/* The vanilla crest of a monster class's line (UFER's additional classes). */
static int MonsterCrest(int jid)
{
    switch (jid)
    {
    case CLASS_REVENANT: case CLASS_BONEWALKER: case CLASS_MAUTHEDOOG:
        return ITEM_HEROCREST;
    case CLASS_BAEL: case CLASS_TARVOS:
        return ITEM_KNIGHTCREST;
    case CLASS_BONEWALKER_BOW:
        return ITEM_ORIONSBOLT;
    case CLASS_GARGOYLE:
        return ITEM_ELYSIANWHIP;
    case CLASS_MOGALL:
        return ITEM_GUIDINGRING;
    }
    return 0;
}

int Reforged_RandoPromoItem(struct Unit *unit, int item)
{
    int iid, pid, jid;

    if (!gRandoRunOn || unit == NULL || unit->pCharacterData == NULL || unit->pClassData == NULL)
        return -1;
    BuildFacts();
    iid = GetItemIndex(item);
    pid = unit->pCharacterData->number;
    jid = unit->pClassData->number;
    if (((iid == ITEM_LUNARBRACE && pid == CHARACTER_EIRIKA) || (iid == ITEM_SOLARBRACE && pid == CHARACTER_EPHRAIM)) &&
        RandoClasses_PlayerClass(pid) != Rando_VanillaChar(pid)->defaultClass)
        return (!Has(jid, F_PROMOTED) && Has(jid, F_PROMOTES)) ? 1 : 0; /* the braces skip the level check */
    if (Rando_Opt(RFO_RANDO_MONSTERS) && Has(jid, F_MONSTER) && !Has(jid, F_PROMOTED) && Has(jid, F_PROMOTES) &&
        (iid == ITEM_MASTERSEAL || (iid != 0 && iid == MonsterCrest(jid))))
        return unit->level >= 10 ? 1 : 0;
    return -1;
}

/* bmidoten.c GenerateBestMovementScript walks back from the target along falling
   costs; for a target its class cannot reach (every cost 0xFF) it wanders at random,
   possibly off the map, writing past gWorkingMovementScript (0x40) and
   RevertMovementScript's buffer. In a randomized run the walk stops when it leaves
   the map or fills the buffer, and the script is dropped (the unit does not walk).
   A walk of 63 steps that has reached the origin (cost 0) is whole: its 63 commands
   and the end mark fill the buffer exactly, so the loop may test that square. */
int Reforged_RandoMoveGuard(int x, int y, u8 **output, u8 *outputStart)
{
    int in, out, len;

    if (!gRandoRunOn || output == NULL || outputStart == NULL)
        return 1;
    len = (int)(*output - outputStart);
    in = gBmMapSize.x > 0 && gBmMapSize.y > 0 && x >= 0 && y >= 0 && x < gBmMapSize.x && y < gBmMapSize.y;
    out = gBmMapSize.x > 0 && gBmMapSize.y > 0 && !in;
    if (!out && len < MOVE_CMD_MAX_COUNT - 1)
        return 1;
    if (in && len == MOVE_CMD_MAX_COUNT - 1 && gWorkingBmMap != NULL && gWorkingBmMap[y] != NULL &&
        gWorkingBmMap[y][x] == 0)
        return 1; /* at the origin: the loop ends on this square */
    if (!sQuiet)
        PortLog("[rando] movement script: no path to the target (%s at %d,%d after %d steps): the unit stays\n",
            out ? "left the map" : "too long", x, y, len);
    *output = outputStart;
    return 0;
}

/* bmunit.c UnitAutolevelCore adds a whole autolevel at once to an s8 stat: with the
   enemy growth buff (class growths up to 127) a high-level unit (a hard-mode arena
   opponent, level up to 2.4x the player's) would wrap past 127 to a negative stat.
   The gain is added in int and stops at 127 (UnitCheckStatCaps then lowers the stat
   to the class cap). Any run: only a gain that would wrap changes. */
int Reforged_RandoStatGain(int stat, int gain)
{
    if (stat + gain > 127)
        return 127 - stat;
    if (stat + gain < -128)
        return -128 - stat;
    return gain;
}

/* ------------------------------------------------------------------ self-test */

static int sFails;

static void Fail(const char *what, int id, int value)
{
    if (sFails++ < 40)
        PortLog("[rando] check: classes: %s (0x%02X, value 0x%02X)\n", what, id, value);
}

static void CheckPlayers(void)
{
    int pid, t, i;

    for (pid = 1; pid <= PID_MAX; pid++)
    {
        const struct CharacterData *c = Rando_Char(pid), *vc = Rando_VanillaChar(pid);
        const struct ClassData *kn;
        int jid, old, tier, total, lo, cap, cls[6], per[6], caps[6], hard;

        if (!Rando_IsPlayable(pid) || c == NULL || vc == NULL)
            continue;
        jid = c->defaultClass;
        old = vc->defaultClass;
        if (jid != RandoClasses_PlayerClass(pid))
            Fail("a playable's default class is not its class of the run", pid, jid);
        if (IsForeignJid(jid))
        {
            /* versions 11-14: the foreign counterpart of its class of the run (RandoClasses_ApplyForeign) */
            int fe8 = sPlayerFe8[pid], female = (c->attributes & CA_FEMALE) != 0;

            kn = GetClassData(jid);
            if (fe8 == 0 || !ForeignOk(jid) || PlayerForeignFor(fe8, female) != jid || !UsesAllOf(jid, fe8) || kn == NULL)
                Fail("a playable's foreign class is no counterpart of its class of the run", pid, jid * 256 + fe8);
            if (Reforged_Fe7Fallback(jid, female) != fe8)
                Fail("a playable's foreign class saves as another class than its class of the run (its stand-in)", pid,
                    jid * 256 + fe8);
            for (t = 0; t < 8 && kn != NULL; t++)
                if (c->baseRanks[t] && !ClassUses(jid, t))
                    Fail("a foreign class: a rank in a type it cannot use", pid, t);
            continue;
        }
        if (Rando_RecruitSource(pid) != pid)
            continue; /* another character's identity: reforged_rando_recruit.c checks it */
        if (!Rando_Opt(RFO_RANDO_CLASSES) || jid == old)
        {
            if (!Rando_Opt(RFO_RANDO_CLASSES) && jid != old)
                Fail("a class changed with player classes off", pid, jid);
            continue;
        }
        if (PlayerSkipped(pid))
            Fail("a kept character changed class", pid, jid);
        tier = VanillaPromoted(old) ? 1 : 0;
        if (VanillaPromoted(jid) != tier)
            Fail("a playable changed tier", pid, jid);
        if (!PlayerFits(pid, jid, tier))
            Fail("a playable's class breaks the rules (attack, melee, range, lords, pools)", pid, jid);
        if (Rando_Opt(RFO_RANDO_GENDER) && sTwin[jid])
        {
            int female = (vc->attributes & CA_FEMALE) != 0;

            if ((female && Has(jid, F_MALE)) || (!female && Has(jid, F_FEMALE)))
                if (PlayerFits(pid, sTwin[jid], tier))
                    Fail("a playable's class has the other sex", pid, jid);
        }
        if (pid == CHARACTER_EIRIKA && !(c->attributes & CA_LOCK_4))
            Fail("Eirika lost her prf weapons", pid, jid);
        if (pid == CHARACTER_EPHRAIM && !(c->attributes & CA_LOCK_5))
            Fail("Ephraim lost his prf weapons", pid, jid);
        kn = Rando_Class(jid);
        for (t = 0; t < 8; t++)
        {
            if (c->baseRanks[t] && !kn->baseRanks[t])
                Fail("a rank in a type the class does not use", pid, t);
            if (t == ITYPE_DARK && c->baseRanks[t] && c->baseRanks[t] < WPN_EXP_D)
                Fail("a dark rank under D", pid, c->baseRanks[t]);
        }
        cls[0] = kn->baseHP;  per[0] = c->baseHP;  caps[0] = kn->maxHP;
        cls[1] = kn->basePow; per[1] = c->basePow; caps[1] = kn->maxPow;
        cls[2] = kn->baseSkl; per[2] = c->baseSkl; caps[2] = kn->maxSkl;
        cls[3] = kn->baseSpd; per[3] = c->baseSpd; caps[3] = kn->maxSpd;
        cls[4] = kn->baseDef; per[4] = c->baseDef; caps[4] = kn->maxDef;
        cls[5] = kn->baseRes; per[5] = c->baseRes; caps[5] = kn->maxRes;
        for (i = 0; i < 6; i++)
        {
            total = cls[i] + per[i];
            lo = i == 0 ? 1 : 0;
            hard = i == 0 ? STAT_HP_MAX : STAT_MAX;
            cap = Min(caps[i], hard);
            if (total < lo || total > hard || (cls[i] <= cap && total > cap))
                Fail("a base out of its limits", pid, i * 0x10 + Clamp(total, 0, 15));
        }
    }
}

static void CheckBossesAndEnemies(void)
{
    u8 cands[JID_MAX + 1];
    int pid, orig, k;

    for (pid = 1; pid <= PID_MAX && Rando_Opt(RFO_RANDO_BOSSES); pid++)
    {
        int b;

        if (!IsBossPid(pid))
            continue;
        orig = Rando_VanillaChar(pid)->defaultClass;
        b = BossClassFor(pid, orig, 0);
        if (b != BossClassFor(pid, orig, 0))
            Fail("a boss class is not deterministic", pid, b);
        if (b == orig)
        {
            if (EnemySource(orig) && !DoNotChange(pid) && EnemyCandidates(orig, Has(orig, F_FLIER), cands) > 0 &&
                !Has(orig, F_FLIER))
            {
                int i, m = 0, n = EnemyCandidates(orig, 0, cands);

                for (i = 0; i < n; i++)
                    m += !Has(cands[i], F_FLIER);
                if (m > 0)
                    Fail("a boss kept its class though others fit", pid, orig);
            }
            continue;
        }
        if (DoNotChange(pid) || !EnemySource(orig))
            Fail("a kept boss changed class", pid, b);
        if (!EnemyTarget(b, orig) || Has(b, F_FLIER) != Has(orig, F_FLIER))
            Fail("a boss class breaks the rules (tier, attack, flier, monster)", pid, b);
    }
    for (orig = 1; orig <= JID_MAX && Rando_Opt(RFO_RANDO_ENEMIES); orig++)
    {
        if (!EnemySource(orig))
            continue;
        for (k = 0; k < 4; k++)
        {
            u32 a = ((u32)(k * 7) << 16) | (0x80u << 8) | (u32)orig;
            int e = EnemyClassFor(orig, a, (u32)(k * 3) << 8, (u32)k + 1);

            if (e != EnemyClassFor(orig, a, (u32)(k * 3) << 8, (u32)k + 1))
                Fail("an enemy class is not deterministic", orig, e);
            if (e == orig)
            {
                if (EnemyCandidates(orig, Has(orig, F_FLIER), cands) > 0)
                    Fail("an enemy kept its class though others fit", orig, e);
                continue;
            }
            if (!EnemyTarget(e, orig) || (Has(orig, F_FLIER) && !Has(e, F_FLIER)))
                Fail("an enemy class breaks the rules (tier, attack, flier, monster)", orig, e);
        }
    }
}

static void CheckHooks(void)
{
    struct UnitDefinition def, buf;
    struct Unit unit;
    u8 script[MOVE_CMD_MAX_COUNT];
    u8 *out;
    int savedOn = gRandoRunOn;

    memset(&def, 0, sizeof(def));
    memset(&unit, 0, sizeof(unit));
    def.charIndex = CHARACTER_SETH;
    def.allegiance = FACTION_ID_BLUE;
    def.level = 1;
    unit.pCharacterData = Rando_Char(CHARACTER_EIRIKA);
    unit.pClassData = Rando_Class(Rando_Char(CHARACTER_EIRIKA)->defaultClass);
    unit.level = 1;

    /* off: every hook gives its input back */
    gRandoRunOn = 0;
    if (Reforged_RandoUnitDef(&def, &buf) != &def)
        Fail("the unit hook changed a definition with the run off", def.charIndex, 0);
    if (Reforged_RandoPromoteJid(&unit, CLASS_EIRIKA_MASTER_LORD) != CLASS_EIRIKA_MASTER_LORD)
        Fail("the promotion hook changed a class with the run off", CHARACTER_EIRIKA, 0);
    if (Reforged_RandoPromoItem(&unit, ITEM_LUNARBRACE) != -1)
        Fail("the promotion item hook answered with the run off", CHARACTER_EIRIKA, 0);
    if (Reforged_RandoEventBattleItem(&unit, ITEM_LIGHT_LIGHTNING) != ITEM_LIGHT_LIGHTNING)
        Fail("the scripted battle hook changed an item with the run off", CHARACTER_EIRIKA, 0);
    out = script + MOVE_CMD_MAX_COUNT - 1;
    if (!Reforged_RandoMoveGuard(0, 0, &out, script))
        Fail("the movement guard acted with the run off", 0, 0);
    gRandoRunOn = savedOn;

    if (!gRandoRunOn)
        return;
    /* on, over a 2x2 cost map (origin 0,0): the guard stops a walk that fills the
       buffer or leaves the map, and drops it; a full walk that reached the origin
       is kept */
    {
        static u8 row0[2] = { 0, 1 }, row1[2] = { 1, 2 };
        static u8 *rows[2] = { row0, row1 };
        u8 **savedMap = gWorkingBmMap;
        short savedW = gBmMapSize.x, savedH = gBmMapSize.y;

        gWorkingBmMap = rows;
        gBmMapSize.x = 2;
        gBmMapSize.y = 2;
        out = script + MOVE_CMD_MAX_COUNT - 1;
        if (Reforged_RandoMoveGuard(1, 1, &out, script) || out != script)
            Fail("the movement guard let a walk overrun its buffer", 0, 0);
        out = script + MOVE_CMD_MAX_COUNT - 1;
        if (!Reforged_RandoMoveGuard(0, 0, &out, script) || out != script + MOVE_CMD_MAX_COUNT - 1)
            Fail("the movement guard dropped a 63-step walk that reached the origin", 0, 0);
        out = script + 3;
        if (!Reforged_RandoMoveGuard(1, 1, &out, script) || out != script + 3)
            Fail("the movement guard stopped a short walk", 0, 0);
        out = script + 3;
        if (Reforged_RandoMoveGuard(2, 0, &out, script) || out != script)
            Fail("the movement guard let a walk leave the map", 0, 0);
        gWorkingBmMap = savedMap;
        gBmMapSize.x = savedW;
        gBmMapSize.y = savedH;
    }
    /* a skirmish icon shows the class the load gives (no map: no terrain fallback) */
    {
        const struct UnitDefinition *got;
        short savedW = gBmMapSize.x;
        int icon;

        memset(&def, 0, sizeof(def));
        def.charIndex = 0x80;
        def.classIndex = CLASS_FIGHTER;
        def.allegiance = FACTION_ID_RED;
        def.level = 5;
        def.xPosition = 3;
        def.yPosition = 4;
        gBmMapSize.x = 0;
        got = Reforged_RandoUnitDef(&def, &buf);
        icon = Reforged_RandoSkirmishLeaderClass(Chapter(), &def, def.classIndex);
        if (got != NULL && icon != got->classIndex)
            Fail("a skirmish icon differs from the leader's class", def.charIndex, icon);
        memset(&sLast, 0, sizeof(sLast));
        gBmMapSize.x = savedW;
    }
    /* version 4: a unit made a monster gets different weapons of its kind, never two of one;
       its other items stay. Version 3: every weapon the class's own pick (copies), as before. */
    {
        static int logged;
        struct UnitKey k;
        int savedV = gRandoRun.version, jid, i, j;

        for (jid = 1; jid <= JID_MAX; jid++)
        {
            u8 kin[RANDO_KIN_MAX];
            int nk, weapons = 0, ver;

            if (!ValidJid(jid) || !Has(jid, F_MONSTERWPN) || IsSpecialJid(jid))
                continue; /* special classes (manaketes, the Demon King) are never given */
            nk = MonsterKinFor(jid, kin);
            if (!logged)
            {
                char line[64];
                int at = 0;

                for (i = 0; i < nk; i++)
                    at += snprintf(line + at, sizeof(line) - at, " 0x%02X", kin[i]);
                line[at] = '\0';
                PortLog("[rando] check: monster class 0x%02X: its weapons%s\n", jid, nk ? line : " none");
            }
            if (nk == 0 || !AnimFits(jid, Rando_MonsterWeapon(jid, 0)))
                Fail("a monster class has no weapon its battle animations cover", jid, nk);
            for (ver = RANDO_VERSION_MAPFIT - 1; ver <= RANDO_VERSION_MAPFIT; ver++)
            {
                memset(&unit, 0, sizeof(unit));
                memset(&k, 0, sizeof(k));
                unit.pCharacterData = Rando_Char(0x80);
                unit.pClassData = Rando_Class(jid);
                unit.level = 5;
                unit.items[0] = (u16)MakeNewItem(ITEM_LANCE_IRON);
                unit.items[1] = (u16)MakeNewItem(ITEM_LANCE_JAVELIN);
                unit.items[2] = (u16)MakeNewItem(ITEM_VULNERARY);
                unit.items[3] = (u16)MakeNewItem(ITEM_LANCE_STEEL);
                k.valid = 1;
                k.pid = 0x80;
                k.origClass = CLASS_SOLDIER;
                k.hadItems = 1;
                k.a = (u32)jid;
                gRandoRun.version = (u8)ver;
                RepairInventory(&unit, &k, 0);
                weapons = 0;
                for (i = 0; i < UNIT_ITEM_COUNT && unit.items[i]; i++)
                {
                    if (!(GetItemAttributes(unit.items[i]) & IA_WEAPON))
                        continue;
                    weapons++;
                    if (!IsOwnMonsterWeapon(jid, ITEM_INDEX(unit.items[i])))
                        Fail("a monster got a weapon not of its kind", jid, ITEM_INDEX(unit.items[i]));
                    for (j = 0; j < i && ver >= RANDO_VERSION_MAPFIT; j++)
                        if (ITEM_INDEX(unit.items[j]) == ITEM_INDEX(unit.items[i]))
                            Fail("version 4: a monster got two of one weapon", jid, ITEM_INDEX(unit.items[i]));
                }
                if (!HasItem(&unit, ITEM_VULNERARY))
                    Fail("a monster's Vulnerary was lost", jid, ver);
                if (weapons != (ver >= RANDO_VERSION_MAPFIT ? Min(3, nk) : 3))
                    Fail("a monster got the wrong number of weapons", jid, ver * 100 + weapons);
            }
        }
        logged = 1;
        gRandoRun.version = (u8)savedV;
        memset(&unit, 0, sizeof(unit));
        unit.pCharacterData = Rando_Char(CHARACTER_EIRIKA);
        unit.pClassData = Rando_Class(Rando_Char(CHARACTER_EIRIKA)->defaultClass);
        unit.level = 1;
    }
    /* version 5: a class change's replacement weapons are not copies of one another when the
       class has others to give (version 4: as before). Every class a unit can be given, a player
       and an enemy each, three weapons of a type it cannot use: copies counted per version. */
    {
        static int logged;
        struct UnitKey k;
        int savedV = gRandoRun.version, jid, i, j, ver, who;
        int dups[2] = { 0, 0 }, units = 0;

        for (jid = 1; jid <= JID_MAX; jid++)
        {
            const struct ClassData *kc;
            int bowUser, t;

            if (!ValidJid(jid) || IsSpecialJid(jid) || Has(jid, F_MONSTERWPN) || !Has(jid, F_ATTACK))
                continue;
            kc = Rando_Class(jid);
            if (kc == NULL)
                continue;
            bowUser = kc->baseRanks[ITYPE_BOW] != 0;
            for (who = 0; who < 2; who++)
                for (ver = RANDO_VERSION_NODUPES - 1; ver <= RANDO_VERSION_NODUPES; ver++)
                {
                    int copies = 0;

                    memset(&unit, 0, sizeof(unit));
                    memset(&k, 0, sizeof(k));
                    unit.pCharacterData = Rando_Char(who ? 0x80 : CHARACTER_GILLIAM);
                    unit.pClassData = kc;
                    unit.level = 5;
                    for (t = 0; t < 8; t++)
                        unit.ranks[t] = kc->baseRanks[t];
                    unit.items[0] = (u16)MakeNewItem(bowUser ? ITEM_AXE_IRON : ITEM_BOW_IRON);
                    unit.items[1] = (u16)MakeNewItem(bowUser ? ITEM_AXE_STEEL : ITEM_BOW_STEEL);
                    unit.items[2] = (u16)MakeNewItem(ITEM_VULNERARY);
                    unit.items[3] = (u16)MakeNewItem(bowUser ? ITEM_AXE_HANDAXE : ITEM_BOW_SHORTBOW);
                    k.valid = 1;
                    k.pid = who ? 0x80 : CHARACTER_GILLIAM;
                    k.origClass = bowUser ? CLASS_FIGHTER : CLASS_ARCHER;
                    k.hadItems = 1;
                    k.a = (u32)jid * 7u + (u32)who;
                    gRandoRun.version = (u8)ver;
                    RepairInventory(&unit, &k, !who);
                    for (i = 0; i < UNIT_ITEM_COUNT && unit.items[i]; i++)
                        for (j = 0; j < i; j++)
                            if (ITEM_INDEX(unit.items[j]) == ITEM_INDEX(unit.items[i]) &&
                                (GetItemAttributes(unit.items[i]) & (IA_WEAPON | IA_STAFF)))
                                copies++;
                    if (!HasItem(&unit, ITEM_VULNERARY))
                        Fail("a class change lost the Vulnerary", jid, ver);
                    dups[ver - (RANDO_VERSION_NODUPES - 1)] += copies;
                    units += ver == RANDO_VERSION_NODUPES;
                }
        }
        if (!logged)
            PortLog("[rando] check: class-change copies: %d in version %d, %d in version %d (%d units)\n",
                dups[0], RANDO_VERSION_NODUPES - 1, dups[1], RANDO_VERSION_NODUPES, units);
        logged = 1;
        if (dups[1] != 0)
            Fail("version 5 still gives copies of one weapon", dups[1], dups[0]);
        gRandoRun.version = (u8)savedV;
        memset(&unit, 0, sizeof(unit));
        unit.pCharacterData = Rando_Char(CHARACTER_EIRIKA);
        unit.pClassData = Rando_Class(Rando_Char(CHARACTER_EIRIKA)->defaultClass);
        unit.level = 1;
    }
    /* autolevel gains stop at 127 and are otherwise unchanged */
    if (Reforged_RandoStatGain(100, 20) != 20 || Reforged_RandoStatGain(100, 40) != 27 ||
        Reforged_RandoStatGain(127, 5) != 0)
        Fail("an autolevel gain wraps or is changed", 0, Reforged_RandoStatGain(100, 40));
    /* a randomized Eirika may use her brace in her class */
    if (RandoClasses_PlayerClass(CHARACTER_EIRIKA) != Rando_VanillaChar(CHARACTER_EIRIKA)->defaultClass &&
        Reforged_RandoPromoItem(&unit, ITEM_LUNARBRACE) != 1)
        Fail("a randomized Eirika cannot use the Lunar Brace", CHARACTER_EIRIKA, unit.pClassData->number);
    if (RandoClasses_PlayerClass(CHARACTER_EIRIKA) != Rando_VanillaChar(CHARACTER_EIRIKA)->defaultClass)
    {
        int p = Reforged_RandoPromoteJid(&unit, CLASS_EIRIKA_MASTER_LORD);

        if (!IsPromotionOf(unit.pClassData->number, p))
            Fail("the Ch16 promotion of a randomized Eirika is not a promotion of her class", CHARACTER_EIRIKA, p);
        if (PromotionOf(unit.pClassData->number) != 0)
        {
            const struct ClassData *was = unit.pClassData;

            /* already promoted (her brace): the PROM is skipped (0) */
            unit.pClassData = Rando_Class(PromotionOf(was->number));
            p = Reforged_RandoPromoteJid(&unit, CLASS_EIRIKA_MASTER_LORD);
            if (p != 0)
                Fail("the Ch16 promotion of an already promoted randomized Eirika is not skipped", CHARACTER_EIRIKA, p);
            unit.pClassData = was;
        }
    }
}

/* "Random promotions" on: every class that promotes (not a lord, trainee or monster) has two
   different promoted player classes, its promotion field the first; lords keep theirs; and
   off again, the vanilla table. "Tower & Ruins": a monster stays a monster of its tier. */
static void CheckPromotionsAndFloors(void)
{
    struct RandoConfig saved = gRandoRun;
    int savedOn = gRandoRunOn, rows = gPromoJidLutCount < JID_MAX + 1 ? gPromoJidLutCount : JID_MAX + 1, jid, k, mixed;
    u8 field[JID_MAX + 1];

    for (jid = 1; jid <= JID_MAX; jid++)
        field[jid] = Rando_Class(jid) ? Rando_Class(jid)->promotion : 0;
    gRandoRunOn = 1;
    gRandoRun.v[RFO_RANDO_PROMOTIONS - RFO_RANDO_FIRST] = 1;
    for (mixed = 0; mixed < 2; mixed++)
    {
        int crossed = 0;

        gRandoRun.v[RFO_RANDO_PROMO_MONSTERS - RFO_RANDO_FIRST] = (u8)mixed;
        RandoPromotions_Apply();
        for (jid = 1; jid < rows; jid++)
        {
            u32 f = Facts(jid);
            int a = gPromoJidLut[jid][0], b = gPromoJidLut[jid][1];

            if (f & (F_LORD | F_TRAINEE | F_SPECIAL))
            {
                if (a != sVanillaPromo[jid][0] || b != sVanillaPromo[jid][1])
                    Fail("random promotions changed a lord's, trainee's or special class's", jid, a);
                continue;
            }
            if (!(f & F_PROMOTES) || !ValidJid(sVanillaPromo[jid][0]))
                continue;
            if (a == b || !PromoTarget(a) || !PromoTarget(b) || (sTwin[a] != 0 && sTwin[a] == b))
                Fail("random promotions gave a bad pair", jid, a * 256 + b);
            if ((sTwin[a] && !((f & F_FEMALE) ? Has(a, F_FEMALE) : Has(a, F_MALE))) ||
                (sTwin[b] && !((f & F_FEMALE) ? Has(b, F_FEMALE) : Has(b, F_MALE))))
                Fail("random promotions: a pair's side is not the base class's sex (or male)", jid, a * 256 + b);
            if (ClassLean(jid) != 0 && (ClassLean(a) != ClassLean(jid) || ClassLean(b) != ClassLean(jid)))
                Fail("random promotions mixed magic and weapon classes", jid, a * 256 + b);
            if (((Facts(a) & F_MONSTER) != 0) != ((f & F_MONSTER) != 0) ||
                ((Facts(b) & F_MONSTER) != 0) != ((f & F_MONSTER) != 0))
            {
                crossed++;
                if (!mixed)
                    Fail("random promotions (Separate) crossed monsters and the others", jid, a * 256 + b);
            }
            if (Rando_Class(jid) && Rando_Class(jid)->promotion != a)
                Fail("random promotions left the class's promotion field", jid, Rando_Class(jid)->promotion);
        }
        if (mixed && crossed == 0)
            Fail("random promotions (Mixed) never crossed monsters and the others", 0, 0);
    }
    gRandoRun = saved;
    gRandoRunOn = savedOn;
    RandoPromotions_Apply();
    for (jid = 1; jid <= JID_MAX; jid++)
        if (Rando_Class(jid))
            ((struct ClassData *)Rando_Class(jid))->promotion = field[jid];
    if (!Rando_Opt(RFO_RANDO_PROMOTIONS) && memcmp(gPromoJidLut, sVanillaPromo, (size_t)rows * 2) != 0)
        Fail("random promotions off: the table is not the vanilla one", 0, 0);

    for (jid = 1; jid <= JID_MAX; jid++)
    {
        int tierOrig = (Facts(jid) & F_PROMOTED) != 0, any = FloorMonsterFor(jid, 0) != 0;

        for (k = 0; k < 32 && any; k++)
        {
            int c = FloorMonsterFor(jid, Rando_Hash(RANDO_SALT_FLOORS, (u32)jid, (u32)k, 0));

            if (c == 0 || !(Facts(c) & F_MONSTER) || ((Facts(c) & F_PROMOTED) != 0) != tierOrig ||
                c == CLASS_DRACO_ZOMBIE || c == CLASS_DEMON_KING)
                Fail("Tower & Ruins gave a monster of another tier", jid, c);
        }
        if (any && !(Facts(jid) & F_MONSTER))
            Fail("Tower & Ruins changes a class that is no monster", jid, 0);
    }
}

/* Version 10's FE7 extras against version 8 (reforged_rando.h RandoClasses_Fe7SelfTest): cfg10 (a version 10
   run, "FE7 classes", with Random promotions on) applied as version 8, as version 10, as version 10
   with the FE7 classes taken as not loaded, and as version 9; each time the class Reforged_RandoUnitDef gives a sample of
   unit definitions (no map loaded: the map checks pass), keyed as the game keys them (an early and three
   late chapters, every class a regular enemy may have with four tiles and levels, every boss), and the
   promotion rows. Versions 8 and 9: no FE7 extra anywhere. Version 10: the same rows; the same units but
   where an FE7 roll fired (an FE7 class, from a version 8 Sage, Bishop or Druid, in a late chapter); the
   FE7 rows only on human magic classes. Not loaded, and version 9: exactly version 8's. The failures; *fired and
   *promoRows count the rolls that fired. */
#define FE7_SAMPLE_MAX 4096
static u8 sFe7Pick[4][FE7_SAMPLE_MAX];
static u8 sFe7PickCh[FE7_SAMPLE_MAX];

static int Fe7Sample(u8 *out, u8 *chOut)
{
    static const u8 sChapters[4] = { CHAPTER_L_5, CHAPTER_E_15, CHAPTER_I_17, CHAPTER_E_21 };
    struct UnitDefinition def, buf;
    int c, orig, k, pid, n = 0;

    for (c = 0; c < 4; c++)
    {
        gPlaySt.chapterIndex = sChapters[c];
        for (orig = 1; orig <= JID_MAX && Rando_Opt(RFO_RANDO_ENEMIES); orig++)
        {
            if (!EnemySource(orig))
                continue;
            for (k = 0; k < 4 && n < FE7_SAMPLE_MAX; k++)
            {
                const struct UnitDefinition *got;

                memset(&def, 0, sizeof(def));
                def.charIndex = 0x80;
                def.classIndex = (u8)orig;
                def.allegiance = FACTION_ID_RED;
                def.level = (u8)(5 + k * 4);
                def.xPosition = (u8)(k * 3 + 1);
                def.yPosition = (u8)(orig & 15);
                got = Reforged_RandoUnitDef(&def, &buf);
                chOut[n] = sChapters[c];
                out[n++] = got != NULL ? got->classIndex : 0;
                memset(&sLast, 0, sizeof(sLast));
            }
        }
        for (pid = 1; pid <= PID_MAX && n < FE7_SAMPLE_MAX; pid++)
        {
            const struct UnitDefinition *got;

            if (!IsBossPid(pid))
                continue;
            memset(&def, 0, sizeof(def));
            def.charIndex = (u8)pid;
            def.allegiance = FACTION_ID_RED;
            def.level = 15;
            def.xPosition = 2;
            def.yPosition = 3;
            got = Reforged_RandoUnitDef(&def, &buf);
            chOut[n] = sChapters[c];
            out[n++] = got != NULL ? (got->classIndex ? got->classIndex : Rando_VanillaChar(pid)->defaultClass) : 0;
            memset(&sLast, 0, sizeof(sLast));
        }
    }
    return n;
}

int RandoClasses_Fe7SelfTest(const struct RandoConfig *cfg10, int *fired, int *promoRows)
{
    struct RandoConfig v8 = *cfg10, v9 = *cfg10;
    int savedCh = gPlaySt.chapterIndex, savedQuiet = sQuiet, rows, n[4], pass, i, jid;
    short savedW = gBmMapSize.x;
    static u8 lut[JID_MAX + 1][2];

    sFails = 0;
    sQuiet = 1;
    gBmMapSize.x = 0;
    rows = gPromoJidLutCount < JID_MAX + 1 ? gPromoJidLutCount : JID_MAX + 1;
    v8.version = RANDO_VERSION_NOFE7;
    v9.version = RANDO_VERSION_FE7 - 1;
    for (pass = 0; pass < 4; pass++)
    {
        sFe7Hide = pass == 2 ? RANDO_ROM_FE7 | RANDO_ROM_FE6 : 0;
        Rando_Start(pass == 0 ? &v8 : pass == 3 ? &v9 : cfg10);
        BuildFacts();
        n[pass] = Fe7Sample(sFe7Pick[pass], sFe7PickCh);
        if (pass == 0)
            memcpy(lut, gPromoJidLut, (size_t)rows * 2);
        else if (memcmp(lut, gPromoJidLut, (size_t)rows * 2) != 0)
            Fail("FE7 extras: version 9 or 10 changed version 8's promotion rows", pass, 0);
        for (jid = 1; jid < rows; jid++)
        {
            int x = Reforged_RandoFe7Promo(jid);

            if (pass != 1 && x != 0)
                Fail("FE7 extras: a third choice in version 8 or 9 or with the FE7 classes not loaded", jid, x);
            if (pass == 1 && sFe7Promo[jid] != 0)
            {
                (*promoRows)++;
                if (sFe7Promo[jid] != Fe7PromoFor(jid))
                    Fail("FE7 extras: a third choice on a class that is not a human magic one", jid, sFe7Promo[jid]);
                if (x != 0 && x != sFe7Promo[jid])
                    Fail("FE7 extras: the third choice shown is not the run's", jid, x);
            }
        }
    }
    sFe7Hide = 0;
    if (n[0] != n[1] || n[0] != n[2] || n[0] != n[3])
        Fail("FE7 extras: the sample's size differs", n[0], n[1]);
    for (i = 0; i < n[0] && i < n[1] && i < n[2] && i < n[3]; i++)
    {
        int a = sFe7Pick[0][i], b = sFe7Pick[1][i];

        if (sFe7Pick[2][i] != a)
            Fail("FE7 extras: not loaded, but a pick differs from version 8's", i, sFe7Pick[2][i]);
        if (sFe7Pick[3][i] != a)
            Fail("FE7 extras: version 9, but a pick differs from version 8's", i, sFe7Pick[3][i]);
        if (b == a)
            continue;
        if (!Fe7Stand(b) || Fe7Target(a) != b || StoryChapter(sFe7PickCh[i]) < FE7_FIRST_STORY ||
            !Reforged_Fe7ClassAvailable(b))
            Fail("FE7 extras: a pick differs from version 8's but is no FE7 roll", i, a * 256 + b);
        else
            (*fired)++;
    }
    gPlaySt.chapterIndex = (u8)savedCh;
    gBmMapSize.x = savedW;
    sQuiet = savedQuiet;
    return sFails;
}

/* Versions 11-14 against version 10 (reforged_rando.h RandoClasses_ThreeSelfTest). Passes (cfg's seed and options,
   its version set): 0 version 10, 1 version 9, 2 version 11 (no ROM), 3 version 12 (FE7), 4 version 13 (FE6), 5
   version 14 (both), 6 version 10 with the foreign classes taken as not loaded, 7 version 14 likewise, 8 version 14
   with only the FE6 classes taken as not loaded, 9 version 14 with only the FE7 ones (a ROM removed later). Each pass:
   the promotion rows (all as pass 0's), the players' classes and the unit sample (Fe7Sample plus enemy thieves).
   Pass 2 must be pass 1's, 4 pass 1's but where an FE6 roll fired, 3 and 5 pass 0's but where a roll of their ROMs
   fired, 7 pass 6's, 8 pass 0's and 9 pass 6's likewise; and 8 and 9 pass 5's everywhere but where pass 5 has a
   class of the hidden ROM: a player or unit then has its class of the run (the stand-in), a list an FE8 class (no
   foreign class takes its slot). A fired roll: the foreign counterpart of the class it replaces (ThreeTargetOk), its ROM in the
   version and loaded, in a late chapter (units); the unit then gets an inventory it has animations for
   (ThreeInventoryOk). Every list of a version 11-14 run: three different classes, version 10's first, the fill no
   twin of another, its foreign classes in the version and loaded, its sex, its tier; versions 9 and 10: no list (the
   screen's own, which oldShort counts when it has fewer than three). */
#define THREE_PASSES 10
static u8 sThreePick[THREE_PASSES][FE7_SAMPLE_MAX];
static u8 sThreeList[REFORGED_FOREIGN_LAST_JID + 1][2][2][4]; /* pass 5's lists ([3]: 1 when asked) */
static u8 sThreeCh[FE7_SAMPLE_MAX];
static u8 sThreePlayer[THREE_PASSES][PID_MAX + 1];
static u8 sThreeLut[JID_MAX + 1][2];

/* the unit sample, and four enemy thieves per chapter (thieves are no enemy source, Fe7Sample skips them) */
static int ThreeSample(u8 *out, u8 *chOut)
{
    static const u8 sChapters[4] = { CHAPTER_L_5, CHAPTER_E_15, CHAPTER_I_17, CHAPTER_E_21 };
    struct UnitDefinition def, buf;
    int n = Fe7Sample(out, chOut), c, k;

    for (c = 0; c < 4; c++)
        for (k = 0; k < 4 && n < FE7_SAMPLE_MAX; k++)
        {
            const struct UnitDefinition *got;

            gPlaySt.chapterIndex = sChapters[c];
            memset(&def, 0, sizeof(def));
            def.charIndex = 0x80;
            def.classIndex = CLASS_THIEF;
            def.allegiance = FACTION_ID_RED;
            def.level = (u8)(3 + k * 5);
            def.xPosition = (u8)(k * 2 + 3);
            def.yPosition = (u8)(9 + k);
            def.ai[UDEF_AIIDX_AI_B] = (u8)(k == 0 ? 0x04 : 0); /* one looter */
            got = Reforged_RandoUnitDef(&def, &buf);
            chOut[n] = sChapters[c];
            out[n++] = got != NULL ? got->classIndex : 0;
            memset(&sLast, 0, sizeof(sLast));
        }
    return n;
}

/* x (a foreign class) may replace the boss or enemy class a in a version 11-14 run (ThreeEnemyClass) */
static int ThreeTargetOk(int a, int x)
{
    switch (x)
    {
    case FJ_KING:      return Has(a, F_PROMOTED) && !Has(a, F_MONSTER | F_FLIER | F_SPECIAL) && ClassUses(a, ITYPE_SWORD);
    case FJ_ARMOR_LORD: return a == CLASS_GENERAL || a == CLASS_GENERAL_F;
    case FJ_NOMAD:     return a == CLASS_ARCHER;
    case FJ_NOMAD_F:   return a == CLASS_ARCHER_F;
    case FJ_TROOPER:   return a == CLASS_SNIPER || a == CLASS_RANGER;
    case FJ_TROOPER_F: return a == CLASS_SNIPER_F || a == CLASS_RANGER_F;
    case FJ_THIEF_F:   return a == CLASS_THIEF;
    }
    return 0;
}

/* A unit of class a (holding a weapon of each attack kind a uses) made class x: after RepairInventory every weapon
   or staff it holds is of a kind x has a battle animation for, and it holds a weapon when x attacks. */
static int ThreeInventoryOk(int a, int x, int pid, int isPlayer)
{
    struct Unit u;
    struct UnitKey k;
    const struct ClassData *kx = GetClassData(x);
    int t, i, n = 0, armed = 0;

    if (kx == NULL || Rando_Char(pid) == NULL)
        return 0;
    memset(&u, 0, sizeof(u));
    memset(&k, 0, sizeof(k));
    u.pCharacterData = Rando_Char(pid);
    u.pClassData = kx;
    for (t = 0; t < 8; t++)
        u.ranks[t] = kx->baseRanks[t];
    for (t = 0; t < 8 && n < UNIT_ITEM_COUNT - 1; t++)
        if (ClassUses(a, t))
        {
            int w = Rando_PickWeapon(t, WPN_LEVEL_D, WPN_LEVEL_B, (u32)(pid * 31 + t));

            if (w != 0)
                u.items[n++] = (u16)MakeNewItem(w);
        }
    k.valid = 1;
    k.kind = isPlayer ? UK_PLAYER : UK_ENEMY;
    k.pid = pid;
    k.origClass = a;
    k.newClass = x;
    k.hadItems = 1;
    k.a = UnitKeyA(Chapter(), pid, a);
    RepairInventory(&u, &k, isPlayer);
    for (i = 0; i < UNIT_ITEM_COUNT && u.items[i]; i++)
    {
        int attrs = GetItemAttributes(u.items[i]), type = GetItemType(u.items[i]);

        if (!(attrs & (IA_WEAPON | IA_STAFF)))
            continue;
        if (type < 0 || type >= 8 || !AnimHasType(kx, type))
            return 0;
        armed |= (attrs & IA_WEAPON) != 0;
    }
    for (t = 0; t < 8; t++)
        if (ClassUses(x, t) && IsAttackType(t))
            return armed;
    return 1;
}

static void ThreeCount(struct RandoThreeStats *st, int x)
{
    if (IsForeignJid(x))
        st->byClass[x - 0x80]++;
}

/* the rows a player's unit can be in (its class of the run, or a trainee's or lord's own, or a foreign base class) */
static int ThreeRowAsked(int jid)
{
    if (ValidJid(jid))
        return (PlayerTarget(jid) && !Has(jid, F_PROMOTED)) || Has(jid, F_LORD | F_TRAINEE);
    return (jid == FJ_NOMAD || jid == FJ_NOMAD_F || jid == FJ_THIEF_F || jid == FJ_LION_LORD) && Fe7Loaded(jid);
}

/* Foreign class x's save stand-in for a unit of that sex (reforged_fe7.c Reforged_Fe7Fallback) is an FE8 class of
   the given tier (0/1) that uses a weapon kind x uses (the save keeps those ranks), of the unit's sex when it has a
   twin and the stand-in goes by the unit's sex (a Trooper F is a Ranger F, a Nomad an Archer, whoever holds it: the
   class's own sex, as the class it replaced). */
static int StandInSane(int x, int female, int tier)
{
    int stand = Reforged_Fe7Fallback(x, female), t;

    if (!ValidJid(stand) || IsForeignJid(stand) || TierPromoted(stand) != tier)
        return 0;
    if (sTwin[stand] != 0 && Reforged_Fe7Fallback(x, 0) != Reforged_Fe7Fallback(x, 1) &&
        (female ? !Has(stand, F_FEMALE) : !Has(stand, F_MALE)))
        return 0;
    for (t = 0; t < 8; t++)
        if (ClassUses(stand, t) && ClassUses(x, t))
            return 1;
    return 0;
}

static void ThreeCheckLists(int pass, const u8 *sVer, int hide, struct RandoThreeStats *st)
{
    int jid, female, tr;

    for (jid = 1; jid <= REFORGED_FOREIGN_LAST_JID && jid < gPromoJidLutCount; jid++)
    {
        if (!ThreeRowAsked(jid) || (gPromoJidLut[jid][0] == 0 && gPromoJidLut[jid][1] == 0))
            continue;
        for (female = 0; female < 2; female++)
            for (tr = 0; tr < (TraineeThird(jid) ? 2 : 1); tr++)
            {
                u8 l[3], v[3], src[3];
                int n = PromoListEx(jid, female, tr, l, src), nv, i, j, first, tier;

                if (sVer[pass] < RANDO_VERSION_THREE)
                {
                    if (n != 0)
                        Fail("three choices: a list in version 9 or 10", jid, n);
                    if (tr == 0 && V10List(jid, female, 0, v) < 3)
                        st->oldShort++;
                    continue;
                }
                if (n != 3)
                {
                    Fail("three choices: a list without three classes", jid * 2 + female, n);
                    continue;
                }
                nv = V10List(jid, female, tr, v);
                first = jid <= JID_MAX ? Rando_VanillaPromo(jid, 0) : gPromoJidLut[jid][0];
                tier = first == 0 || TierPromoted(first);
                for (i = 0; i < 3; i++)
                {
                    int x = l[i];

                    for (j = 0; j < i; j++)
                        if (l[j] == x || (src[i] != SRC_V10 && ValidJid(x) && sTwin[x] != 0 && sTwin[x] == l[j]))
                            Fail("three choices: a repeat (or a twin) in a list", jid * 2 + female, x);
                    if (i < nv && x != v[i])
                        Fail("three choices: the list does not start with version 10's", jid * 2 + female, x);
                    if (src[i] == SRC_V10)
                        continue;
                    if (x == FJ_KING || x == FJ_BARD || x == FJ_DIVINE || x == FJ_NOMAD || x == FJ_NOMAD_F)
                        Fail("three choices: a class no promotion may give", jid * 2 + female, x);
                    if (IsForeignJid(x) && !ForeignOk(x))
                        Fail("three choices: a foreign class outside the run's ROMs (or not loaded)", jid * 2 + female, x);
                    if ((x == FJ_TROOPER && female) || (x == FJ_TROOPER_F && !female) ||
                        (ValidJid(x) && sTwin[x] != 0 && Rando_Opt(RFO_RANDO_GENDER) &&
                         (female ? Has(x, F_MALE) : Has(x, F_FEMALE)) && src[i] == SRC_FILL))
                        Fail("three choices: the other sex's class", jid * 2 + female, x);
                    if (TierPromoted(x) != tier)
                        Fail("three choices: a class of another tier", jid * 2 + female, x);
                    if (IsForeignJid(x))
                    {
                        /* a unit promoted into it: a build or run without the ROM loads the stand-in */
                        if (!StandInSane(x, female, tier))
                            Fail("three choices: a foreign choice's save stand-in is no class of the unit's tier, sex "
                                "and weapons", jid * 2 + female, x * 256 + Reforged_Fe7Fallback(x, female));
                        else
                            st->standPromo++;
                    }
                    if (tr == 0 && female == 0)
                    {
                        if (src[i] == SRC_FOREIGN)
                            st->promoForeign++;
                        else if (src[i] == SRC_RANDOM)
                            st->promoRandom++;
                        else
                            st->promoFill++;
                    }
                    ThreeCount(st, x);
                }
                if (pass == 5)
                {
                    memcpy(sThreeList[jid][female][tr], l, 3);
                    sThreeList[jid][female][tr][3] = 1;
                }
                else if (hide != 0 && sThreeList[jid][female][tr][3])
                {
                    /* a version 14 run with one ROM hidden: pass 5's foreign classes of the other ROM where they
                       were, and FE8 classes for the rest (version 10's are checked above; a fill drawn after a
                       hidden class may be another FE8 class, its hash counting what the list shows) */
                    for (i = 0; i < 3; i++)
                    {
                        int y = sThreeList[jid][female][tr][i];

                        if (IsForeignJid(y) && !(ForeignRom(y) & hide) ? l[i] != y : l[i] != y && IsForeignJid(l[i]))
                            Fail("three choices: one ROM removed, a list differs from both ROMs' but where that ROM's "
                                "class was", jid * 2 + female, y * 256 + l[i]);
                    }
                }
                st->lists++;
            }
    }
}

int RandoClasses_ThreeSelfTest(const struct RandoConfig *cfg, struct RandoThreeStats *st)
{
    return RandoClasses_ThreeSelfTestAt(cfg, st, 0);
}

/* The same with versions 11-14 run as 11-14 + add (4: versions 15-18, which make the same picks) */
int RandoClasses_ThreeSelfTestAt(const struct RandoConfig *cfg, struct RandoThreeStats *st, int add)
{
    static const u8 sVer[THREE_PASSES] = { RANDO_VERSION_FE7, RANDO_VERSION_FE7 - 1, RANDO_VERSION_THREE,
        RANDO_VERSION_THREE + 1, RANDO_VERSION_THREE + 2, RANDO_VERSION_THREE + 3, RANDO_VERSION_FE7, RANDO_VERSION_THREE + 3,
        RANDO_VERSION_THREE + 3, RANDO_VERSION_THREE + 3 };
    static const s8 sBase[THREE_PASSES] = { -1, -1, 1, 0, 1, 0, -1, 6, 0, 6 };
    static const u8 sHide[THREE_PASSES] = { 0, 0, 0, 0, 0, 0, RANDO_ROM_FE7 | RANDO_ROM_FE6, RANDO_ROM_FE7 | RANDO_ROM_FE6,
        RANDO_ROM_FE6, RANDO_ROM_FE7 };
    struct RandoConfig run = *cfg;
    int savedCh = gPlaySt.chapterIndex, savedQuiet = sQuiet, rows, n[THREE_PASSES], pass, i, pid;
    short savedW = gBmMapSize.x;

    sFails = 0;
    sQuiet = 1;
    gBmMapSize.x = 0;
    rows = gPromoJidLutCount < JID_MAX + 1 ? gPromoJidLutCount : JID_MAX + 1;
    for (pass = 0; pass < THREE_PASSES; pass++)
    {
        run.version = (u8)(sVer[pass] >= RANDO_VERSION_THREE ? sVer[pass] + add : sVer[pass]);
        sFe7Hide = sHide[pass];
        if (pass == 0)
            memset(sThreeList, 0, sizeof(sThreeList));
        Rando_Start(&run);
        BuildFacts();
        if (Rando_ThreeRun() != (sVer[pass] >= RANDO_VERSION_THREE) ||
            Rando_ThreeRoms() != (sVer[pass] >= RANDO_VERSION_THREE ? sVer[pass] - RANDO_VERSION_THREE : 0))
            Fail("three choices: the version's ROM set", pass, run.version);
        n[pass] = ThreeSample(sThreePick[pass], sThreeCh);
        for (pid = 0; pid <= PID_MAX; pid++)
            sThreePlayer[pass][pid] = (u8)(Rando_IsPlayable(pid) ? RandoClasses_PlayerClass(pid) : 0);
        if (pass == 0)
            memcpy(sThreeLut, gPromoJidLut, (size_t)rows * 2);
        else if (memcmp(sThreeLut, gPromoJidLut, (size_t)rows * 2) != 0)
            Fail("three choices: a version changed version 10's promotion rows", pass, 0);
        ThreeCheckLists(pass, sVer, pass >= 8 ? sHide[pass] : 0, st);
        if (Rando_ThreeRun())
        {
            /* every player counterpart, for either sex and this config's Match gender, saves as the class it
               replaces (PlayerForeignFor: the stand-in rule) */
            int a, f;

            for (a = 1; a <= JID_MAX; a++)
                for (f = 0; f < 2; f++)
                {
                    int x = PlayerForeignFor(a, f);

                    if (x != 0 && Reforged_Fe7Fallback(x, f) != a)
                        Fail("three choices: a player counterpart's stand-in is not the class it replaces", a * 2 + f,
                            x * 256 + Reforged_Fe7Fallback(x, f));
                }
        }
        if (sBase[pass] < 0)
            continue;
        /* players: the base pass's class, or its foreign counterpart (a fired roll) */
        for (pid = 1; pid <= PID_MAX; pid++)
        {
            int a = sThreePlayer[sBase[pass]][pid], x = sThreePlayer[pass][pid];
            const struct CharacterData *c = Rando_Char(pid);

            if (a == x)
                continue;
            if (!IsForeignJid(x) || !ForeignOk(x) || c == NULL || sPlayerFe8[pid] != a ||
                PlayerForeignFor(a, (c->attributes & CA_FEMALE) != 0) != x || !UsesAllOf(x, a))
                Fail("three choices: a player's class differs from version 10's but is no foreign roll", pid, a * 256 + x);
            else
            {
                st->players++;
                ThreeCount(st, x);
                if (Reforged_Fe7Fallback(x, (c->attributes & CA_FEMALE) != 0) != a)
                    Fail("three choices: a player's foreign class saves as another class than its class of the run",
                        pid, a * 256 + x);
                else
                    st->standPlayers++;
                if (!ThreeInventoryOk(a, x, pid, 1))
                    Fail("three choices: a player's foreign class gets a weapon it has no animation for", pid, x);
            }
        }
        /* passes 8 and 9 (one ROM hidden): pass 5's players and units, the class of the run where pass 5 has the
           hidden ROM's class */
        if (pass >= 8)
        {
            for (pid = 1; pid <= PID_MAX; pid++)
            {
                int y = sThreePlayer[5][pid], x = sThreePlayer[pass][pid];

                if (x != y && !(IsForeignJid(y) && (ForeignRom(y) & sHide[pass]) && x == sThreePlayer[sBase[pass]][pid]))
                    Fail("three choices: one ROM removed, a player's class differs from both ROMs' but where that ROM's "
                        "class was", pid, y * 256 + x);
            }
            for (i = 0; i < n[pass] && i < n[5]; i++)
            {
                int y = sThreePick[5][i], x = sThreePick[pass][i];

                if (x != y && !(IsForeignJid(y) && (ForeignRom(y) & sHide[pass]) && x == sThreePick[sBase[pass]][i]))
                    Fail("three choices: one ROM removed, a unit differs from both ROMs' but where that ROM's class was",
                        i, y * 256 + x);
            }
        }
        /* units */
        if (n[pass] != n[sBase[pass]])
            Fail("three choices: the sample's size differs", pass, n[pass]);
        for (i = 0; i < n[pass] && i < n[sBase[pass]]; i++)
        {
            int a = sThreePick[sBase[pass]][i], x = sThreePick[pass][i];

            if (a == x)
                continue;
            gPlaySt.chapterIndex = sThreeCh[i];
            if (!IsForeignJid(x) || !ForeignOk(x) || !ThreeTargetOk(a, x) || StoryChapter(sThreeCh[i]) < FE7_FIRST_STORY)
                Fail("three choices: a unit differs from version 10's but is no foreign roll", i, a * 256 + x);
            else
            {
                if (x == FJ_KING || x == FJ_ARMOR_LORD)
                    st->bosses++;
                else
                    st->units++;
                ThreeCount(st, x);
                {
                    /* its stand-in (either sex: the sample does not keep the pid) is its class of the run, else a
                       class of that tier and the foreign class's weapons (ThreeEnemyClass map-checked it) */
                    int f, same = 1, ok = 1;

                    for (f = 0; f < 2; f++)
                        if (Reforged_Fe7Fallback(x, f) != a)
                        {
                            same = 0;
                            ok &= StandInSane(x, f, TierPromoted(a));
                        }
                    if (!ok)
                        Fail("three choices: a foreign unit's save stand-in is neither its class of the run nor a class "
                            "of its tier and weapons", i, a * 256 + x);
                    else if (same)
                        st->standUnitsSame++;
                    else
                        st->standUnitsOther++;
                }
                if (!ThreeInventoryOk(a, x, 0x80, 0))
                    Fail("three choices: a foreign unit gets a weapon it has no animation for", i, x);
            }
        }
    }
    sFe7Hide = 0;
    st->runs++;
    gPlaySt.chapterIndex = (u8)savedCh;
    gBmMapSize.x = savedW;
    sQuiet = savedQuiet;
    return sFails;
}

int RandoClasses_SelfTest(void)
{
    sFails = 0;
    BuildFacts();
    if (!sFactsBuilt)
    {
        PortLog("[rando] check: classes: no vanilla snapshot\n");
        return 1;
    }
    if (gRandoRunOn)
    {
        CheckPlayers();
        CheckBossesAndEnemies();
    }
    sQuiet = 1;
    CheckHooks();
    CheckPromotionsAndFloors();
    sQuiet = 0;
    if (sFails > 40)
        PortLog("[rando] check: classes: %d failures in all\n", sFails);
    return sFails;
}

#endif /* PORT_PLAYED_BUILD */
