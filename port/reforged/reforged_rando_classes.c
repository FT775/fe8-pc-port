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
 */

#if PORT_PLAYED_BUILD

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

#include "port_hw.h"
#include "port_reforged.h"
#include "reforged_rando.h"

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
    u32 a, b, c;    /* the unit's content key (chapter, pid, class; tile; level) */
};

static struct UnitKey sLast;

static int sQuiet; /* the self-test: the hooks it calls do not log */

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

static u32 Facts(int jid)
{
    return ValidJid(jid) ? sFacts[jid] : 0;
}

static int Has(int jid, u32 flags)
{
    return (Facts(jid) & flags) != 0;
}

/* The class uses weapon type t (vanilla base rank; the randomizer never changes it). */
static int ClassUses(int jid, int t)
{
    const struct ClassData *k = Rando_VanillaClass(jid);

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

/* The default promotion of an unpromoted class: ClassData.promotion when it is a
   promoted class (it keeps the sex: Shaman F -> Druid F), else gPromoJidLut's first. */
static int PromotionOf(int jid)
{
    const struct ClassData *k = Rando_VanillaClass(jid);

    if (k == NULL || VanillaPromoted(jid))
        return 0;
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
        c = i == 0 ? k->promotion : gPromoJidLut[jid][i - 1];
        if (ValidJid(c) && VanillaPromoted(c) && (n == 0 || (c != out[0] && (n == 1 || c != out[1]))))
            out[n++] = (u8)c;
    }
    return n;
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
    for (u = 1; u <= JID_MAX; u++)
        if (IsPromotionOf(u, jid))
            return u;
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

/* The class a playable pid's definition gives in this run. */
static int PlayerDefClass(int pid, int defClass)
{
    const struct CharacterData *vc = Rando_VanillaChar(pid);
    int now = RandoClasses_PlayerClass(pid);

    if (vc == NULL || now == vc->defaultClass)
        return defClass ? defClass : now; /* not randomized */
    if (defClass == 0 || defClass == vc->defaultClass)
        return now;
    if (!ValidJid(defClass) || InFamily(now, defClass))
        return defClass; /* already the new class's line (LOAD3 of a promoted unit) */
    if (Has(defClass, F_SPECIAL) || !Has(defClass, F_REAL))
        return defClass; /* a cutscene's special class */
    if (VanillaPromoted(defClass) && !VanillaPromoted(now))
    {
        int p = PromotionOf(now);

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

/* The boss pid's class in this run for its vanilla class orig (one class all game). */
static int BossClassFor(int pid, int orig)
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
    pick = cands[Rando_HashInt(Rando_Hash(RANDO_SALT_BOSSES, (u32)pid, (u32)orig, 0), n)];
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
    const struct ClassData *kn = Rando_Class(newJ), *ko = Rando_Class(oldJ);
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

/* ------------------------------------------------------------------ the unit hooks */

/* The first word of a unit's content key: the chapter it loads in, pid, class. */
static u32 UnitKeyA(int chapter, int pid, int orig)
{
    return ((u32)(chapter & 0xFF) << 16) | ((u32)(pid & 0xFF) << 8) | (u32)(orig & 0xFF);
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
    if (local.genMonster || orig == CLASS_GORGONEGG || orig == CLASS_GORGONEGG2 || orig == CLASS_PHANTOM)
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
        cls = BossClassFor(pid, orig);
        if (cls != orig && !TerrainOk(cls, orig, &local))
        {
            PortLog("[rando] boss 0x%02X (chapter %d): class 0x%02X cannot stand or walk where it must: "
                "class 0x%02X kept\n", pid, Chapter(), cls, orig);
            cls = orig;
        }
    }
    else if (local.allegiance == FACTION_ID_RED && Rando_Opt(RFO_RANDO_ENEMIES) && !IsBossPid(pid) &&
             !DoNotChange(pid) && !LootsVillages(&local))
    {
        sLast.kind = UK_ENEMY;
        cls = EnemyClassFor(orig, sLast.a, sLast.b, sLast.c);
        if (cls != orig && !TerrainOk(cls, orig, &local))
            cls = orig;
    }
    sLast.newClass = cls;
    if (cls == produced || !ValidJid(cls))
    {
        sLast.newClass = produced;
        return uDef;
    }
    local.classIndex = (u8)cls;
    *buf = local;
    if (sLast.kind == UK_BOSS)
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
        cls = BossClassFor(pid, orig);
    else if (Rando_Opt(RFO_RANDO_ENEMIES) && !DoNotChange(pid) && !LootsVillages(uDef))
        cls = EnemyClassFor(orig, UnitKeyA(chapterId, pid, orig),
            ((u32)uDef->xPosition << 8) | (u32)uDef->yPosition, uDef->level);
    else
        return jid;
    return (cls == orig || !ValidJid(cls)) ? jid : cls;
}

/* ---- inventories */

static int IsOwnMonsterWeapon(int jid, int iid)
{
    int i;

    for (i = 0; i < 3; i++)
        if (Rando_MonsterWeapon(jid, sPickAll[i]) == iid)
            return 1;
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
        r = ReplacementFor(unit, item, isPlayer, Mix(h, (u32)i));
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
        unit->items[count] = (u16)MakeNewItem(DropItem(h));
}

void Reforged_RandoUnitLoaded(struct Unit *unit, const struct UnitDefinition *uDef)
{
    struct UnitKey k = sLast;
    int pid, jid, changed, isPlayer;

    memset(&sLast, 0, sizeof(sLast));
    (void)uDef;
    Reforged_RandoUnitJoined(unit); /* Nuzlocke: a unit in the graveyard that joins again dies */
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
    return p;
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
        b = BossClassFor(pid, orig);
        if (b != BossClassFor(pid, orig))
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
    sQuiet = 0;
    if (sFails > 40)
        PortLog("[rando] check: classes: %d failures in all\n", sFails);
    return sFails;
}

#endif /* PORT_PLAYED_BUILD */
