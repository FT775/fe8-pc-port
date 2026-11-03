#include "global.h"

/*
 * Reforged Options randomizer: items (port/reforged/reforged_rando.h, "items").
 * Played builds only; empty in every test build. Ideas and algorithms after the
 * Universal FE Randomizer (UFER, MIT): no code is taken from it.
 *
 * What an item is: every id is put in one kind (a human weapon, a staff, a stat
 * booster, a promotion item, a consumable, a gem, an accessory, a key, a plot item,
 * a monster weapon, gold, a special item, or nothing) from the vanilla snapshot
 * (Rando_VanillaItem), cached once. Human weapons also carry flags: prf (a class or
 * character lock), legendary (S rank, and the sacred twins), basic (the iron set),
 * safe (no traits: the iron and steel sets, Javelin, Hand Axe), siege (3-10/3-15).
 * Monster weapons are the IA_LOCK_3 / ITYPE_MONSTER ones, the five rank-0 unlocked
 * dark tomes (Demon Surge, Shadowshot, Evil Eye, Crimson Eye, Stone) and the rank-0
 * Nightmare staff: by the code any unit can wield those, so they stay out of every
 * pool. Ballistas, dance rings, the Heaven Seal and the Juna Fruit are special:
 * never in a pool, never replaced.
 *
 * RandoItems_Apply (step 3 of the pipeline, on the restored gItemData): weapon
 * might / hit / weight / uses (UFER's coin flip, +/- nextInt(var + 1), clamped; a
 * vanilla value outside the clamp widens it, so Eclipse keeps its low hit and
 * Naglfar its 25 might) and weapon traits. The traits never add extended range or
 * magic damage (both need battle animations a class may not have). Effectiveness
 * points at the game's own lists (data_itemuse.c), so Delphi Shield's pointer
 * compare (bmitem.c IsItemEffectiveAgainst) still works; stat bonuses point at the
 * tables below. Item descriptions are text and stay vanilla.
 *
 * Rewards (RFO_RANDO_REWARDS): the fixed chests (eventinfo.c, the non-gold branch)
 * and the GIVEITEMTO gifts (eventscr.c Event37_GiveItem: villages, houses, story
 * gifts). Gold, keys, the Member Card, the braces, prf and legendary weapons,
 * monster and special items keep their place. Anything else: 50% a related item
 * (same kind; a weapon of the same type within one rank, no basics; a staff within
 * one rank), else any item of the general reward pool. Keyed on the chapter, the
 * item and the chest's tile / the gift's receiver (a fixed character), or the tile
 * the receiver stands on when the script gives to the active unit (villages, houses,
 * the Ch15 desert), so a different visitor or a reload finds the same item. Both
 * run only on a battle map, where the chapter is the map's own (no world map event
 * has a GIVEITEMTO; the lord split's GIVEITEMTOMAIN is gold). Chest
 * items also reach the unit through GIVEITEMTO (EventScr_OpenChest): the gift hook
 * leaves those alone, so a fixed chest is decided once, by its tile, and the Valni /
 * Lagdou random chests keep their own roll.
 *
 * Shops (RFO_RANDO_SHOPS): StartShopScreen's list (bmshop.c) is replaced by a
 * copy in a static buffer, same length, each entry an item of the same kind
 * (weapons: the same type half the time, else any type; within one rank), no
 * duplicates when it can be helped; a secret shop may also sell boosters,
 * promotion items and accessories. Keyed on the chapter, the vanilla list's
 * contents and the shop type, so a shop shows the same items on every visit and
 * after a reload. A world map node's shops use a fixed key in place of the chapter
 * (ShopChapterKey: there chapterIndex follows the last skirmish, dungeon or save
 * menu, so fighting a skirmish would reroll them); battle map and battle prep shops,
 * the prep screen's armory included, keep the chapter. StartShopScreen copies the
 * list into its proc at once and never reads the pointer again, so one buffer is
 * enough.
 *
 * Every hook returns its input unchanged when the run is not randomized or its
 * option is 0, and nothing here calls the game's RNG.
 */

#if PORT_PLAYED_BUILD

#include <string.h>

#include "bmitem.h"
#include "bmshop.h"
#include "bmunit.h"
#include "proc.h"
#include "event.h"
#include "eventinfo.h"
#include "eventscript.h"
#include "variables.h"
#include "worldmap.h"
#include "constants/items.h"
#include "constants/classes.h"

#include "port_hw.h"
#include "port_reforged.h"
#include "reforged_rando.h"

#define RANDO_ITEM_MAX 0xCD /* the last id in gItemData (ITEM_UNK_CD) */
#define RANDO_ITEM_SLOTS (RANDO_ITEM_MAX + 1)

/* The weapon stat clamps (UFER's defaults). */
#define W_MIGHT_VAR 3
#define W_MIGHT_MIN 0
#define W_MIGHT_MAX 23
#define W_HIT_VAR 20
#define W_HIT_MIN 55
#define W_HIT_MAX 100
#define W_WEIGHT_VAR 5
#define W_WEIGHT_MIN 2
#define W_WEIGHT_MAX 20
#define W_USES_VAR 20
#define W_USES_MIN 15
#define W_USES_MAX 60
#define W_USES_MIN_SIEGE 1

enum {
    KIND_NONE,       /* ITEM_NONE, unused and dummy ids */
    KIND_GOLD,       /* ITEM_GOLD and the money items */
    KIND_PLOT,       /* Member Card, Lunar Brace, Solar Brace */
    KIND_SPECIAL,    /* ballistas, dance rings, Heaven Seal, Juna Fruit */
    KIND_KEY,        /* Chest Key (and its bundle), Door Key, Lockpick */
    KIND_MONSTER,    /* monster weapons, dragon stones, monster tomes, Nightmare */
    KIND_WEAPON,     /* a weapon a human uses (types 0-7 but staff) */
    KIND_STAFF,
    KIND_BOOSTER,    /* the stat boosters and Metis's Tome */
    KIND_PROMO,      /* crests, Orion's Bolt, Elysian Whip, Guiding Ring, seals */
    KIND_CONSUMABLE, /* Vulnerary, Elixir, Pure Water, Antitoxin, Torch, Mine, Light Rune */
    KIND_GEM,
    KIND_ACCESSORY,  /* Delphi Shield, Hoplon Guard, Silver Card */
};

enum {
    IFLAG_PRF = 1 << 0,
    IFLAG_LEGEND = 1 << 1,
    IFLAG_BASIC = 1 << 2,
    IFLAG_SAFE = 1 << 3,
    IFLAG_SIEGE = 1 << 4,
    IFLAG_HEAL = 1 << 5,
};

static u8 sKind[RANDO_ITEM_SLOTS];
static u8 sFlags[RANDO_ITEM_SLOTS];
static u8 sLevel[RANDO_ITEM_SLOTS];
static int sClassified;

/* The stat-boost trait: +5 to one stat (never HP, as UFER). */
static const struct ItemStatBonuses sTraitBonuses[6] = {
    { 0, 5, 0, 0, 0, 0, 0, 0, 0 }, /* pow */
    { 0, 0, 5, 0, 0, 0, 0, 0, 0 }, /* skl */
    { 0, 0, 0, 5, 0, 0, 0, 0, 0 }, /* spd */
    { 0, 0, 0, 0, 5, 0, 0, 0, 0 }, /* def */
    { 0, 0, 0, 0, 0, 5, 0, 0, 0 }, /* res */
    { 0, 0, 0, 0, 0, 0, 5, 0, 0 }, /* lck */
};

/* The effectiveness trait: the game's own lists (UFER's seven). */
static const u8 *TraitEffList(int i)
{
    switch (i) {
    case 0: return ItemEffectiveness_Armor;
    case 1: return ItemEffectiveness_Horse;
    case 2: return ItemEffectiveness_ArmorAndHorse;
    case 3: return ItemEffectiveness_Flier;
    case 4: return ItemEffectiveness_Dragon;
    case 5: return ItemEffectiveness_Swordsman;
    default: return ItemEffectiveness_Monsters;
    }
}
#define TRAIT_EFF_COUNT 7

/* A second, independent value from a Rando_Hash result (a murmur3 finalizer). */
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

static int ChapterKey(void)
{
    return (u8)gPlaySt.chapterIndex;
}

/* A shop's chapter key. On the world map gPlaySt.chapterIndex is whatever the last
   skirmish, dungeon or save menu set (worldmap_main.c Worldmap_StartSkirmishAnim,
   WorldMap_CallIntroEvent, WorldMap_PrepareSaveMenu), so a node's shops
   (worldmap_basemenu.c, under the GMapBaseMenu proc that ProcScr_WorldMapMain starts
   blocking) take a fixed key past any chapter id instead. The prep screen's armory
   (prep_itemscreen.c StartPrepArmory, the fixed gPrepShopInventory) is offered only
   off the world map (the world map's item screen has Sell in its place), so it is a
   battle prep shop like the others: on a battle map ProcScr_WorldMapMain is gone and
   chapterIndex is the map's own (a skirmish's comes from its node). */
static int ShopChapterKey(void)
{
    if (Proc_Find(ProcScr_WorldMapMain))
        return 0x100;
    return ChapterKey();
}

int Rando_RankLevelOf(int wexp)
{
    if (wexp < WPN_EXP_E)
        return WPN_LEVEL_0;
    if (wexp < WPN_EXP_D)
        return WPN_LEVEL_E;
    if (wexp < WPN_EXP_C)
        return WPN_LEVEL_D;
    if (wexp < WPN_EXP_B)
        return WPN_LEVEL_C;
    if (wexp < WPN_EXP_A)
        return WPN_LEVEL_B;
    if (wexp < WPN_EXP_S)
        return WPN_LEVEL_A;
    return WPN_LEVEL_S;
}

/* The kind of an id by its number first (the lists the data alone cannot tell),
   then by its vanilla data. */
static int KindOf(int iid, const struct ItemData *v)
{
    if (iid == ITEM_NONE || iid > RANDO_ITEM_MAX || !v)
        return KIND_NONE;
    if (iid >= ITEM_VULNERARY_2 && iid <= ITEM_DANCE) /* the 60-use notes, the Dance item */
        return KIND_NONE;
    if (iid >= ITEM_UNK_BC)
        return KIND_NONE;

    switch (iid) {
    case ITEM_GOLD:
    case ITEM_1G: case ITEM_5G: case ITEM_10G: case ITEM_50G: case ITEM_100G:
    case ITEM_3000G: case ITEM_5000G: case ITEM_150G: case ITEM_200G:
        return KIND_GOLD;

    case ITEM_MEMBERCARD:
    case ITEM_LUNARBRACE:
    case ITEM_SOLARBRACE:
        return KIND_PLOT;

    case ITEM_BALLISTA_REGULAR: case ITEM_BALLISTA_LONG: case ITEM_BALLISTA_KILLER:
    case ITEM_FILLAS_MIGHT: case ITEM_NINISS_GRACE: case ITEM_THORS_IRE: case ITEM_SETS_LITANY:
    case ITEM_HEAVENSEAL: /* lords only: it would skip the Ch16 promotion */
    case ITEM_JUNAFRUIT:
        return KIND_SPECIAL;

    case ITEM_CHESTKEY: case ITEM_CHESTKEY_BUNDLE: case ITEM_DOORKEY: case ITEM_LOCKPICK:
        return KIND_KEY;

    case ITEM_MONSTER_DEMONSURG: case ITEM_MONSTER_SHADOWSHT: case ITEM_MONSTER_EVILEYE:
    case ITEM_MONSTER_CRIMSNEYE: case ITEM_MONSTER_STONE: case ITEM_NIGHTMARE:
        return KIND_MONSTER;

    case ITEM_BOOSTER_HP: case ITEM_BOOSTER_POW: case ITEM_BOOSTER_SKL: case ITEM_BOOSTER_SPD:
    case ITEM_BOOSTER_LCK: case ITEM_BOOSTER_DEF: case ITEM_BOOSTER_RES: case ITEM_BOOSTER_MOV:
    case ITEM_BOOSTER_CON: case ITEM_METISSTOME:
        return KIND_BOOSTER;

    case ITEM_HEROCREST: case ITEM_KNIGHTCREST: case ITEM_ORIONSBOLT: case ITEM_ELYSIANWHIP:
    case ITEM_GUIDINGRING: case ITEM_MASTERSEAL: case ITEM_OCEANSEAL:
        return KIND_PROMO;

    case ITEM_VULNERARY: case ITEM_ELIXIR: case ITEM_PUREWATER: case ITEM_ANTITOXIN:
    case ITEM_TORCH: case ITEM_MINE: case ITEM_LIGHTRUNE:
        return KIND_CONSUMABLE;

    case ITEM_WHITEGEM: case ITEM_BLUEGEM: case ITEM_REDGEM: case ITEM_BLACKGEM: case ITEM_GOLDGEM:
        return KIND_GEM;

    case ITEM_DELPHISHIELD: case ITEM_HOPLON_SHIELD: case ITEM_SILVERCARD:
        return KIND_ACCESSORY;
    }

    if (v->weaponType == ITYPE_MONSTER || (v->attributes & IA_LOCK_3))
        return KIND_MONSTER;
    if (v->weaponType == ITYPE_STAFF && (v->attributes & IA_STAFF))
        return KIND_STAFF;
    if (v->weaponType <= ITYPE_DARK && v->weaponType != ITYPE_STAFF && (v->attributes & IA_WEAPON))
        return KIND_WEAPON;
    return KIND_NONE;
}

static int FlagsOf(int iid, int kind, const struct ItemData *v)
{
    int flags = 0;

    if (kind == KIND_WEAPON) {
        if (v->attributes & (IA_LOCK_ANY & ~IA_LOCK_3))
            flags |= IFLAG_PRF;
        if (iid == ITEM_SWORD_SIEGLINDE || iid == ITEM_LANCE_SIEGMUND
            || Rando_RankLevelOf(v->weaponRank) == WPN_LEVEL_S)
            flags |= IFLAG_LEGEND;
        if ((v->encodedRange & 0xF) >= 10)
            flags |= IFLAG_SIEGE;
        switch (iid) {
        case ITEM_SWORD_IRON: case ITEM_LANCE_IRON: case ITEM_AXE_IRON: case ITEM_BOW_IRON:
        case ITEM_ANIMA_FIRE: case ITEM_LIGHT_LIGHTNING: case ITEM_DARK_FLUX:
            flags |= IFLAG_BASIC | IFLAG_SAFE;
            break;
        case ITEM_SWORD_STEEL: case ITEM_LANCE_STEEL: case ITEM_AXE_STEEL: case ITEM_BOW_STEEL:
        case ITEM_ANIMA_THUNDER: case ITEM_LANCE_JAVELIN: case ITEM_AXE_HANDAXE:
            flags |= IFLAG_SAFE;
            break;
        }
    } else if (kind == KIND_STAFF) {
        if (Rando_RankLevelOf(v->weaponRank) == WPN_LEVEL_S)
            flags |= IFLAG_LEGEND; /* Latona */
        switch (iid) {
        case ITEM_STAFF_HEAL: case ITEM_STAFF_MEND: case ITEM_STAFF_RECOVER:
        case ITEM_STAFF_PHYSIC: case ITEM_STAFF_FORTIFY:
            flags |= IFLAG_HEAL;
            break;
        }
    }
    return flags;
}

static void Classify(void)
{
    int iid;

    if (sClassified)
        return;
    if (!Rando_VanillaItem(ITEM_SWORD_IRON))
        return; /* no snapshot yet: everything reads as nothing, nothing is cached */

    for (iid = 0; iid < RANDO_ITEM_SLOTS; iid++) {
        const struct ItemData *v = Rando_VanillaItem(iid);
        int kind = KindOf(iid, v);

        sKind[iid] = kind;
        sFlags[iid] = (kind == KIND_NONE) ? 0 : FlagsOf(iid, kind, v);
        sLevel[iid] = (kind == KIND_WEAPON || kind == KIND_STAFF) ? Rando_RankLevelOf(v->weaponRank) : 0;
    }
    sClassified = 1;
}

static int Kind(int iid)
{
    Classify();
    if (iid <= 0 || iid > RANDO_ITEM_MAX)
        return KIND_NONE;
    return sKind[iid];
}

static int Flags(int iid)
{
    Classify();
    if (iid <= 0 || iid > RANDO_ITEM_MAX)
        return 0;
    return sFlags[iid];
}

int Rando_ItemIsWeapon(int iid)
{
    return Kind(iid) == KIND_WEAPON;
}

int Rando_ItemIsStaff(int iid)
{
    return Kind(iid) == KIND_STAFF;
}

int Rando_ItemIsPrf(int iid)
{
    return (Flags(iid) & IFLAG_PRF) != 0;
}

int Rando_ItemIsLegendary(int iid)
{
    return (Flags(iid) & IFLAG_LEGEND) != 0;
}

int Rando_ItemRankLevel(int iid)
{
    int kind = Kind(iid);

    if (kind != KIND_WEAPON && kind != KIND_STAFF)
        return 0;
    return sLevel[iid];
}

/* An item a reward or a shop may hand out (and so may also replace). */
static int IsPoolable(int iid)
{
    switch (Kind(iid)) {
    case KIND_WEAPON:
    case KIND_STAFF:
    case KIND_BOOSTER:
    case KIND_PROMO:
    case KIND_CONSUMABLE:
    case KIND_GEM:
    case KIND_ACCESSORY:
        return (Flags(iid) & (IFLAG_PRF | IFLAG_LEGEND)) == 0;
    }
    return 0;
}

static int AbsDiff(int a, int b)
{
    return a > b ? a - b : b - a;
}

static int VanillaType(int iid)
{
    const struct ItemData *v = Rando_VanillaItem(iid);

    return v ? v->weaponType : -1;
}

/* ------------------------------------------------------------------ weapon pools */

int Rando_PickWeapon(int type, int wantLevel, int maxLevel, u32 r)
{
    u8 cands[RANDO_ITEM_SLOTS];
    int count = 0, best = 99, iid;

    if (maxLevel < WPN_LEVEL_E)
        maxLevel = WPN_LEVEL_E;
    if (type < ITYPE_SWORD || type > ITYPE_DARK)
        return 0;

    Classify();
    for (iid = 1; iid <= RANDO_ITEM_MAX; iid++) {
        int lvl, d;

        if (type == ITYPE_STAFF) {
            if (sKind[iid] != KIND_STAFF || !(sFlags[iid] & IFLAG_HEAL))
                continue;
        } else {
            if (sKind[iid] != KIND_WEAPON || (sFlags[iid] & (IFLAG_PRF | IFLAG_LEGEND | IFLAG_SIEGE)))
                continue;
            if (VanillaType(iid) != type)
                continue;
        }
        lvl = sLevel[iid];
        if (lvl < WPN_LEVEL_E || lvl > maxLevel)
            continue;

        d = AbsDiff(lvl, wantLevel);
        if (d < best) {
            best = d;
            count = 0;
        }
        if (d == best)
            cands[count++] = iid;
    }
    if (count == 0)
        return 0;
    return cands[Rando_HashInt(r, count)];
}

int Rando_MonsterWeapon(int jid, u32 r)
{
    static const u8 sBael[] = { ITEM_MONSTER_SHARPCLAW, ITEM_MONSTER_VENINCLW };
    static const u8 sElderBael[] = { ITEM_MONSTER_SHARPCLAW, ITEM_MONSTER_VENINCLW, ITEM_MONSTER_LTHLTALON };
    static const u8 sGorgon[] = { ITEM_MONSTER_DEMONSURG, ITEM_MONSTER_STONE };
    static const u8 sDemonKing[] = { ITEM_DEMONLIGHT, ITEM_RAVAGER };

    /* The weapons each class gets in vanilla (monstergen_data.c gMonsterItemsByClassIndex
       and the chapter unit definitions). Bonewalkers, Wights, Cyclopes, Tarvos, Maelduins
       and the gargoyles use normal swords, lances, axes and bows: 0. */
    switch (jid) {
    case CLASS_REVENANT: return ITEM_MONSTER_ROTTENCLW;
    case CLASS_ENTOUMBED: return ITEM_MONSTER_FETIDCLW;
    case CLASS_BAEL: return sBael[Rando_HashInt(r, 2)];
    case CLASS_ELDER_BAEL:
    case CLASS_ELDER_BAEL_2: return sElderBael[Rando_HashInt(r, 3)];
    case CLASS_MAUTHEDOOG: return ITEM_MONSTER_FIREFANG;
    case CLASS_GWYLLGI: return ITEM_MONSTER_HELLFANG;
    case CLASS_MOGALL: return ITEM_MONSTER_EVILEYE;
    case CLASS_ARCH_MOGALL: return ITEM_MONSTER_CRIMSNEYE;
    case CLASS_GORGON: return sGorgon[Rando_HashInt(r, 2)];
    case CLASS_DRACO_ZOMBIE: return ITEM_MONSTER_WRETCHAIR;
    case CLASS_DEMON_KING: return sDemonKing[Rando_HashInt(r, 2)];
    case CLASS_MANAKETE_MYRRH: return ITEM_DIVINESTONE;
    case CLASS_MANAKETE: return ITEM_DEMONSTONE;          /* no vanilla unit: the plain stone */
    case CLASS_MANAKETE_2: return ITEM_MONSTER_WRETCHAIR; /* Morva's */
    }
    return 0;
}

/* ------------------------------------------------------------ weapon stats, traits */

/* UFER's variance: a coin flip for the sign, then +/- nextInt(var + 1), clamped
   (a vanilla value outside [lo, hi] widens the clamp to keep it reachable). */
static int Vary(struct RandoRng *rng, int value, int var, int lo, int hi)
{
    int up = Rando_RngInt(rng, 2);
    int d = Rando_RngInt(rng, var + 1);
    int n = up ? value + d : value - d;

    if (lo > value)
        lo = value;
    if (hi < value)
        hi = value;
    if (n < lo)
        n = lo;
    if (n > hi)
        n = hi;
    return n;
}

enum {
    TRAIT_STAT,
    TRAIT_EFFECTIVE,
    TRAIT_BRAVE,
    TRAIT_UNBREAKABLE,
    TRAIT_REVERSE,
    TRAIT_CRIT,
    TRAIT_POISON,
    TRAIT_DEVIL,
    TRAIT_COUNT
};

/* Adds one trait to it; returns 1 when one was added. */
static int AddTrait(struct RandoRng *rng, struct ItemData *it)
{
    u8 eligible[TRAIT_COUNT];
    int n = 0, pick;

    if (!it->pStatBonuses)
        eligible[n++] = TRAIT_STAT;
    if (!it->pEffectiveness)
        eligible[n++] = TRAIT_EFFECTIVE;
    if (!(it->attributes & IA_BRAVE))
        eligible[n++] = TRAIT_BRAVE;
    if (!(it->attributes & IA_UNBREAKABLE) && it->maxUses > 0)
        eligible[n++] = TRAIT_UNBREAKABLE;
    if (!(it->attributes & IA_REVERTTRIANGLE) && it->weaponType != ITYPE_BOW)
        eligible[n++] = TRAIT_REVERSE;
    if (it->crit <= 10)
        eligible[n++] = TRAIT_CRIT;
    if (it->weaponEffectId == WPN_EFFECT_NONE) {
        eligible[n++] = TRAIT_POISON;
        eligible[n++] = TRAIT_DEVIL;
    }
    if (n == 0)
        return 0;

    pick = eligible[Rando_RngInt(rng, n)];
    switch (pick) {
    case TRAIT_STAT:
        it->pStatBonuses = &sTraitBonuses[Rando_RngInt(rng, 6)];
        break;
    case TRAIT_EFFECTIVE:
        it->pEffectiveness = TraitEffList(Rando_RngInt(rng, TRAIT_EFF_COUNT));
        break;
    case TRAIT_BRAVE:
        it->attributes |= IA_BRAVE;
        break;
    case TRAIT_UNBREAKABLE: {
        /* An unbreakable item's price is costPerUse itself (bmitem.c GetItemCost). */
        u32 cost = (u32)it->costPerUse * 64;

        it->attributes |= IA_UNBREAKABLE;
        it->costPerUse = cost > 0xFFFF ? 0xFFFF : cost;
        break;
    }
    case TRAIT_REVERSE:
        it->attributes |= IA_REVERTTRIANGLE;
        break;
    case TRAIT_CRIT:
        it->crit += 5 * (4 + Rando_RngInt(rng, 7));
        break;
    case TRAIT_POISON:
        it->weaponEffectId = WPN_EFFECT_POISON;
        break;
    case TRAIT_DEVIL: {
        int might = it->might * 3 / 2;

        if (might < it->might + 5)
            might = it->might + 5;
        if (might > W_MIGHT_MAX)
            might = it->might > W_MIGHT_MAX ? it->might : W_MIGHT_MAX;
        it->might = might;
        it->weaponEffectId = WPN_EFFECT_DEVIL;
        it->weaponExp = 8;
        break;
    }
    }
    return 1;
}

void RandoItems_Apply(void)
{
    struct RandoRng rng;
    int iid;
    int chance = Rando_Opt(RFO_RANDO_W_EFFECTS);

    Classify();
    if (!sClassified)
        return;

    /* One stream per stat (UFER's salts: weapons + 0 might, 1 hit, 2 weight, 3 uses),
       the items in id order. */
    if (Rando_Opt(RFO_RANDO_W_MIGHT)) {
        Rando_RngSeed(&rng, RANDO_SALT_WEAPONS + 0);
        for (iid = 1; iid <= RANDO_ITEM_MAX; iid++) {
            struct ItemData *it = Rando_Item(iid);

            if (sKind[iid] != KIND_WEAPON || !it)
                continue;
            if (it->weaponEffectId == WPN_EFFECT_HPHALVE || it->might == 0xFF)
                continue; /* Eclipse: its might is not a might */
            it->might = Vary(&rng, it->might, W_MIGHT_VAR, W_MIGHT_MIN, W_MIGHT_MAX);
        }
    }
    if (Rando_Opt(RFO_RANDO_W_HIT)) {
        Rando_RngSeed(&rng, RANDO_SALT_WEAPONS + 1);
        for (iid = 1; iid <= RANDO_ITEM_MAX; iid++) {
            struct ItemData *it = Rando_Item(iid);

            if (sKind[iid] != KIND_WEAPON || !it)
                continue;
            it->hit = Vary(&rng, it->hit, W_HIT_VAR, W_HIT_MIN, W_HIT_MAX);
        }
    }
    if (Rando_Opt(RFO_RANDO_W_WEIGHT)) {
        Rando_RngSeed(&rng, RANDO_SALT_WEAPONS + 2);
        for (iid = 1; iid <= RANDO_ITEM_MAX; iid++) {
            struct ItemData *it = Rando_Item(iid);

            if (sKind[iid] != KIND_WEAPON || !it)
                continue;
            it->weight = Vary(&rng, it->weight, W_WEIGHT_VAR, W_WEIGHT_MIN, W_WEIGHT_MAX);
        }
    }
    if (Rando_Opt(RFO_RANDO_W_USES)) {
        Rando_RngSeed(&rng, RANDO_SALT_WEAPONS + 3);
        for (iid = 1; iid <= RANDO_ITEM_MAX; iid++) {
            struct ItemData *it = Rando_Item(iid);

            if (sKind[iid] != KIND_WEAPON || !it)
                continue;
            if (it->maxUses == 0 || (it->attributes & IA_UNBREAKABLE))
                continue;
            it->maxUses = Vary(&rng, it->maxUses, W_USES_VAR,
                (sFlags[iid] & IFLAG_SIEGE) ? W_USES_MIN_SIEGE : W_USES_MIN, W_USES_MAX);
        }
    }

    /* Traits: one roll per weapon (not the safe basics), then one eligible trait. */
    if (chance > 0) {
        Rando_RngSeed(&rng, RANDO_SALT_WEAPONS + 4);
        for (iid = 1; iid <= RANDO_ITEM_MAX; iid++) {
            struct ItemData *it = Rando_Item(iid);

            if (sKind[iid] != KIND_WEAPON || (sFlags[iid] & IFLAG_SAFE) || !it)
                continue;
            if (Rando_RngInt(&rng, 100) >= chance)
                continue;
            AddTrait(&rng, it);
        }
    }
    /* No log line here: Rando_Start logs the run's config, and the self-test applies
       hundreds of configs. */
}

/* ------------------------------------------------------------------------ rewards */

/* The candidates in id order; returns how many. */
static int RelatedItems(int item, u8 *out)
{
    int kind = Kind(item), lvl = sLevel[item], type = VanillaType(item);
    int n = 0, iid;

    for (iid = 1; iid <= RANDO_ITEM_MAX; iid++) {
        if (iid == item || !IsPoolable(iid) || sKind[iid] != kind)
            continue;
        if (kind == KIND_WEAPON) {
            if (VanillaType(iid) != type || (sFlags[iid] & IFLAG_BASIC))
                continue;
            if (AbsDiff(sLevel[iid], lvl) > 1 || sLevel[iid] > WPN_LEVEL_A)
                continue;
        } else if (kind == KIND_STAFF) {
            if (AbsDiff(sLevel[iid], lvl) > 1)
                continue;
        }
        out[n++] = iid;
    }
    return n;
}

static int GeneralRewards(int item, u8 *out)
{
    int n = 0, iid;

    for (iid = 1; iid <= RANDO_ITEM_MAX; iid++) {
        if (iid == item || !IsPoolable(iid) || (sFlags[iid] & IFLAG_BASIC))
            continue;
        if (sLevel[iid] > WPN_LEVEL_A)
            continue;
        out[n++] = iid;
    }
    return n;
}

/* A reward's replacement from its key h (the item itself when it keeps its place). */
static int RewardFor(int item, u32 h)
{
    u8 cands[RANDO_ITEM_SLOTS];
    int n = 0;

    if (!IsPoolable(item))
        return item;

    if (Rando_HashInt(Mix(h, 0), 2) == 0)
        n = RelatedItems(item, cands);
    if (n == 0)
        n = GeneralRewards(item, cands);
    if (n == 0)
        return item;
    return cands[Rando_HashInt(Mix(h, 1), n)];
}

int Reforged_RandoChestItem(int item, int x, int y)
{
    int iid = item & 0xFF;
    u32 h;

    if (!gRandoRunOn || !Rando_Opt(RFO_RANDO_REWARDS))
        return item;

    h = Rando_Hash(RANDO_SALT_REWARDS, ChapterKey(), iid, ((x & 0xFF) << 8) | (y & 0xFF));
    return (item & ~0xFF) | RewardFor(iid, h);
}

/* The map event engine whose current command is the GIVEITEMTO being run, or NULL
   (only one map event engine runs at a time: CallEvent queues the others). */
static const struct EventEngineProc *GiftEngine(void)
{
    const struct EventEngineProc *proc = Proc_Find(ProcScr_StdEventEngine);

    if (!proc || !proc->pEventCurrent)
        return NULL;
    if ((proc->pEventCurrent[0] >> 8) != EV_CMD_GIVEITEM || EVT_SUB_CMD(proc->pEventCurrent) != EVSUBCMD_GIVEITEMTO)
        return NULL;
    return proc;
}

int Reforged_RandoGiftItem(int item, struct Unit *target)
{
    /* Slot 3 holds an item id; GotItem's MakeNewItem gives it full uses (it reads only
       the low byte), so any high bits are kept as they are. */
    const struct EventEngineProc *proc;
    int iid = item & 0xFF;
    u32 where, h;

    if (!gRandoRunOn || !Rando_Opt(RFO_RANDO_REWARDS))
        return item;

    where = (target && target->pCharacterData) ? target->pCharacterData->number : 0;
    proc = GiftEngine();
    if (proc) {
        s16 param = (s16)proc->pEventCurrent[1];

        /* Every chest item reaches the unit through EventScr_OpenChest's GIVEITEMTO: a
           fixed chest was already decided by Reforged_RandoChestItem, and the Valni and
           Lagdou random chests keep their own (vanilla) roll. */
        if (proc->pEventStart == (const u16 *)EventScr_OpenChest || proc->pEventIdk == (const u16 *)EventScr_OpenChest)
            return item;

        /* A village, a house or the Ch15 desert gives to whoever stands there: key on the
           tile, not on the visitor, so another visitor (or a reload) finds the same item. */
        if (target && (param == CHAR_EVT_ACTIVE_UNIT || param == CHAR_EVT_POSITION_AT_SLOTB))
            where = 0x10000 | ((target->xPos & 0xFF) << 8) | (target->yPos & 0xFF);
    }

    h = Rando_Hash(RANDO_SALT_REWARDS + 1, ChapterKey(), iid, where);
    return (item & ~0xFF) | RewardFor(iid, h);
}

/* -------------------------------------------------------------------------- shops */

/* StartShopScreen copies SHOP_ITEMS_MAX_AMT + 1 entries whatever the terminator, so the
   buffer is that long and zero past the list. */
static u16 sShopBuf[SHOP_ITEMS_MAX_AMT + 4];

static int InList(const u8 *list, int n, int iid)
{
    int i;

    for (i = 0; i < n; i++)
        if (list[i] == iid)
            return 1;
    return 0;
}

static int IsRareShopKind(int kind)
{
    return kind == KIND_BOOSTER || kind == KIND_PROMO || kind == KIND_ACCESSORY;
}

/* Fills out with the candidates for a shop entry; returns how many. */
static int ShopCandidates(int item, int anyType, int rare, const u8 *used, int nused, u8 *out)
{
    int kind = Kind(item), lvl = sLevel[item], type = VanillaType(item);
    int n = 0, iid;

    for (iid = 1; iid <= RANDO_ITEM_MAX; iid++) {
        if (!IsPoolable(iid) || (used && InList(used, nused, iid)))
            continue;
        if (Rando_VanillaItem(iid)->costPerUse == 0)
            continue; /* no price (venin weapons, the ruins weapons, Metis's Tome): free in a shop */
        if (rare) {
            if (!IsRareShopKind(sKind[iid]))
                continue;
        } else {
            if (sKind[iid] != kind)
                continue;
            if (kind == KIND_WEAPON || kind == KIND_STAFF) {
                if (AbsDiff(sLevel[iid], lvl) > 1)
                    continue;
                if (kind == KIND_WEAPON && !anyType && VanillaType(iid) != type)
                    continue;
            }
        }
        out[n++] = iid;
    }
    return n;
}

static u32 ShopListHash(const u16 *list, int n)
{
    u32 h = 2166136261u;
    int i;

    for (i = 0; i < n; i++) {
        h ^= list[i];
        h *= 16777619u;
    }
    return h;
}

const u16 *Reforged_RandoShopItems(const u16 *inventory, int shopType)
{
    u8 used[SHOP_ITEMS_MAX_AMT];
    u8 cands[RANDO_ITEM_SLOTS];
    u32 base;
    int n, i;

    if (!inventory || !gRandoRunOn || !Rando_Opt(RFO_RANDO_SHOPS))
        return inventory;
    if (shopType != SHOP_TYPE_ARMORY && shopType != SHOP_TYPE_VENDOR && shopType != SHOP_TYPE_SECRET_SHOP)
        return inventory;
    Classify();
    if (!sClassified)
        return inventory;

    for (n = 0; n < SHOP_ITEMS_MAX_AMT && inventory[n] != 0; n++)
        ;
    base = Rando_Hash(RANDO_SALT_SHOPS, ShopChapterKey(), ShopListHash(inventory, n), shopType);

    memset(sShopBuf, 0, sizeof(sShopBuf));
    for (i = 0; i < n; i++) {
        int item = inventory[i] & 0xFF;
        int out = item;
        u32 e = Mix(base, i);

        if (IsPoolable(item)) {
            int kind = sKind[item];
            int anyType = kind == KIND_WEAPON && Rando_HashInt(Mix(e, 0), 2) != 0;
            int rare = shopType == SHOP_TYPE_SECRET_SHOP && kind != KIND_WEAPON && kind != KIND_STAFF
                && Rando_HashInt(Mix(e, 1), 2) != 0;
            int c = ShopCandidates(item, anyType, rare, used, i, cands);

            if (c == 0) /* everything fitting is already on the list: allow a repeat */
                c = ShopCandidates(item, anyType, rare, NULL, 0, cands);
            if (c > 0)
                out = cands[Rando_HashInt(Mix(e, 2), c)];
        }
        used[i] = out;
        sShopBuf[i] = out;
    }
    return sShopBuf;
}

/* ---------------------------------------------------------------------- self-test */

static int sFails;

static void CheckFail(const char *what, int iid, int value)
{
    sFails++;
    PortLog("[rando] check: items: %s (item 0x%02X, value %d)\n", what, iid, value);
}

static int IsTraitBonus(const struct ItemStatBonuses *p)
{
    int i;

    for (i = 0; i < 6; i++)
        if (p == &sTraitBonuses[i])
            return 1;
    return 0;
}

static int IsTraitEff(const u8 *p)
{
    int i;

    for (i = 0; i < TRAIT_EFF_COUNT; i++)
        if (p == TraitEffList(i))
            return 1;
    return 0;
}

static int InRange(int value, int vanilla, int lo, int hi)
{
    if (lo > vanilla)
        lo = vanilla;
    if (hi < vanilla)
        hi = vanilla;
    return value >= lo && value <= hi;
}

static void CheckTables(void)
{
    const u32 traitBits = IA_BRAVE | IA_UNBREAKABLE | IA_REVERTTRIANGLE | IA_LOCK_ANY;
    int iid;

    for (iid = 1; iid <= RANDO_ITEM_MAX; iid++) {
        const struct ItemData *v = Rando_VanillaItem(iid);
        const struct ItemData *it = Rando_Item(iid);

        if (!v || !it) {
            CheckFail("no table entry", iid, 0);
            continue;
        }
        if (it->number != v->number || it->weaponType != v->weaponType
            || it->encodedRange != v->encodedRange || it->weaponRank != v->weaponRank)
            CheckFail("id, type, range or rank changed", iid, it->weaponType);

        if (sKind[iid] != KIND_WEAPON) {
            /* Only weapons change here (another step may change lock bits). */
            if (it->might != v->might || it->hit != v->hit || it->weight != v->weight
                || it->maxUses != v->maxUses || it->crit != v->crit || it->costPerUse != v->costPerUse
                || it->pStatBonuses != v->pStatBonuses || it->pEffectiveness != v->pEffectiveness
                || it->weaponEffectId != v->weaponEffectId || it->weaponExp != v->weaponExp
                || (it->attributes & ~IA_LOCK_ANY) != (v->attributes & ~IA_LOCK_ANY))
                CheckFail("a non-weapon changed", iid, 0);
            continue;
        }

        if (GetItemReachBits(iid) == REACH_NONE)
            CheckFail("weapon range has no reach", iid, it->encodedRange);
        if (!(v->weaponEffectId == WPN_EFFECT_HPHALVE || v->might == 0xFF)
            && !InRange(it->might, v->might, W_MIGHT_MIN, W_MIGHT_MAX))
            CheckFail("might out of range", iid, it->might);
        if (!InRange(it->hit, v->hit, W_HIT_MIN, W_HIT_MAX))
            CheckFail("hit out of range", iid, it->hit);
        if (!InRange(it->weight, v->weight, W_WEIGHT_MIN, W_WEIGHT_MAX))
            CheckFail("weight out of range", iid, it->weight);
        if (v->maxUses > 0 && !(it->attributes & IA_UNBREAKABLE) && (it->maxUses < 1 || it->maxUses > 63))
            CheckFail("uses out of range", iid, it->maxUses);
        if (v->maxUses == 0 && it->maxUses != 0)
            CheckFail("uses given to an unbreakable weapon", iid, it->maxUses);
        if (it->crit > (v->crit > 10 ? v->crit : 60))
            CheckFail("crit out of range", iid, it->crit);
        if (it->pStatBonuses != v->pStatBonuses && (v->pStatBonuses || !IsTraitBonus(it->pStatBonuses)))
            CheckFail("stat bonus pointer not from a known list", iid, 0);
        if (it->pEffectiveness != v->pEffectiveness && (v->pEffectiveness || !IsTraitEff(it->pEffectiveness)))
            CheckFail("effectiveness pointer not from a known list", iid, 0);
        if (it->weaponEffectId != v->weaponEffectId
            && (v->weaponEffectId != WPN_EFFECT_NONE
                || (it->weaponEffectId != WPN_EFFECT_POISON && it->weaponEffectId != WPN_EFFECT_DEVIL)))
            CheckFail("weapon effect changed badly", iid, it->weaponEffectId);
        if ((it->attributes & ~traitBits) != (v->attributes & ~traitBits))
            CheckFail("attributes changed beyond the traits", iid, (int)it->attributes);
        if ((it->attributes & IA_REVERTTRIANGLE) && !(v->attributes & IA_REVERTTRIANGLE)
            && it->weaponType == ITYPE_BOW)
            CheckFail("reverse triangle on a bow", iid, 0);
        if ((sFlags[iid] & IFLAG_SAFE)
            && (it->pStatBonuses != v->pStatBonuses || it->pEffectiveness != v->pEffectiveness
                || it->crit != v->crit || it->weaponEffectId != v->weaponEffectId
                || (it->attributes & (IA_BRAVE | IA_UNBREAKABLE | IA_REVERTTRIANGLE))
                    != (v->attributes & (IA_BRAVE | IA_UNBREAKABLE | IA_REVERTTRIANGLE))))
            CheckFail("a trait on a safe basic weapon", iid, 0);
    }
}

static void CheckPickers(void)
{
    int type, want, max, k, jid;

    for (type = ITYPE_SWORD; type <= ITYPE_DARK; type++) {
        for (max = 0; max <= WPN_LEVEL_S; max++) {
            int exists = 0, iid, eff = max < WPN_LEVEL_E ? WPN_LEVEL_E : max;

            for (iid = 1; iid <= RANDO_ITEM_MAX; iid++) {
                if (type == ITYPE_STAFF ? (sKind[iid] == KIND_STAFF && (sFlags[iid] & IFLAG_HEAL))
                                        : (sKind[iid] == KIND_WEAPON && VanillaType(iid) == type
                                           && !(sFlags[iid] & (IFLAG_PRF | IFLAG_LEGEND | IFLAG_SIEGE))))
                    if (sLevel[iid] >= WPN_LEVEL_E && sLevel[iid] <= eff)
                        exists = 1;
            }
            for (want = 0; want <= WPN_LEVEL_S; want++) {
                for (k = 0; k < 4; k++) {
                    u32 r = Rando_Hash(RANDO_SALT_WEAPONS + 9, type, want * 8 + max, k);
                    int got = Rando_PickWeapon(type, want, max, r);

                    if (got == 0) {
                        if (exists)
                            CheckFail("PickWeapon found nothing where a weapon exists", type, want * 10 + max);
                        continue;
                    }
                    if (VanillaType(got) != type || Rando_ItemIsPrf(got) || Rando_ItemIsLegendary(got)
                        || Kind(got) == KIND_MONSTER || Rando_ItemRankLevel(got) > eff
                        || (type == ITYPE_STAFF ? !Rando_ItemIsStaff(got) : !Rando_ItemIsWeapon(got)))
                        CheckFail("PickWeapon gave a bad weapon", got, type * 100 + want * 10 + max);
                }
            }
        }
    }

    for (jid = 1; jid <= 0x7F; jid++) {
        const struct ClassData *c = Rando_VanillaClass(jid);
        int got = Rando_MonsterWeapon(jid, Rando_Hash(RANDO_SALT_WEAPONS + 9, jid, 0, 0));

        if (got && Kind(got) != KIND_MONSTER)
            CheckFail("MonsterWeapon gave a non-monster weapon", got, jid);
        if (c && (c->attributes & CA_LOCK_3) && jid != CLASS_GORGONEGG && jid != CLASS_GORGONEGG2 && !got)
            CheckFail("MonsterWeapon gave nothing to a monster class", 0, jid);
        if (got && c && (Rando_VanillaItem(got)->attributes & IA_LOCK_3) && !(c->attributes & CA_LOCK_3))
            CheckFail("MonsterWeapon gave a locked weapon the class cannot use", got, jid);
    }
}

static int RewardOk(int in, int out)
{
    if (!IsPoolable(in))
        return out == in;
    return IsPoolable(out) && sLevel[out] <= WPN_LEVEL_A;
}

static void CheckRewardsAndShops(void)
{
    static const u8 sCoords[3][2] = { { 3, 4 }, { 17, 2 }, { 0, 25 } };
    u16 list[SHOP_ITEMS_MAX_AMT + 1];
    u16 first[SHOP_ITEMS_MAX_AMT + 1];
    struct RandoConfig saved = gRandoRun;
    int savedOn = gRandoRunOn;
    int iid, k, start, shopType, len, i;

    /* Off: every hook gives its input back. */
    gRandoRunOn = 0;
    for (iid = 1; iid <= RANDO_ITEM_MAX; iid++) {
        if (Reforged_RandoChestItem(iid, 3, 4) != iid || Reforged_RandoGiftItem(iid, NULL) != iid)
            CheckFail("a reward changed with the run off", iid, 0);
    }
    list[0] = ITEM_SWORD_IRON;
    list[1] = 0;
    if (Reforged_RandoShopItems(list, SHOP_TYPE_ARMORY) != list)
        CheckFail("a shop changed with the run off", ITEM_SWORD_IRON, 0);

    /* A fake run with rewards and shops on. */
    gRandoRunOn = 1;
    gRandoRun.v[RFO_RANDO_ENABLE - RFO_RANDO_FIRST] = 1;
    gRandoRun.v[RFO_RANDO_REWARDS - RFO_RANDO_FIRST] = 1;
    gRandoRun.v[RFO_RANDO_SHOPS - RFO_RANDO_FIRST] = 1;

    if (Reforged_RandoShopItems(NULL, SHOP_TYPE_ARMORY) != NULL)
        CheckFail("the default shop list was not passed through", 0, 0);

    for (iid = 1; iid <= RANDO_ITEM_MAX; iid++) {
        for (k = 0; k < 3; k++) {
            int out = Reforged_RandoChestItem(iid, sCoords[k][0], sCoords[k][1]);

            if (!RewardOk(iid, out))
                CheckFail("a chest gave a bad item", iid, out);
            if (out != Reforged_RandoChestItem(iid, sCoords[k][0], sCoords[k][1]))
                CheckFail("a chest is not deterministic", iid, out);
        }
        {
            int out = Reforged_RandoGiftItem(iid, NULL);

            if (!RewardOk(iid, out))
                CheckFail("a gift gave a bad item", iid, out);
        }
    }

    /* Shop lists: runs of consecutive ids (exempt ones included, they must stay). */
    for (shopType = SHOP_TYPE_ARMORY; shopType <= SHOP_TYPE_SECRET_SHOP; shopType++) {
        for (start = 1; start <= RANDO_ITEM_MAX; start += 5) {
            const u16 *got;

            len = 0;
            for (iid = start; iid <= RANDO_ITEM_MAX && len < 12; iid++)
                if (Kind(iid) != KIND_NONE)
                    list[len++] = iid;
            list[len] = 0;
            if (len == 0)
                continue;

            got = Reforged_RandoShopItems(list, shopType);
            for (i = 0; i <= len; i++)
                first[i] = got[i];
            got = Reforged_RandoShopItems(list, shopType);

            for (i = 0; i < len; i++) {
                int in = list[i], out = first[i];

                if (got[i] != out)
                    CheckFail("a shop is not deterministic", in, out);
                if (!IsPoolable(in)) {
                    if (out != in)
                        CheckFail("a shop replaced an exempt item", in, out);
                    continue;
                }
                if (!IsPoolable(out))
                    CheckFail("a shop sells a bad item", in, out);
                else if (out != in && Rando_VanillaItem(out)->costPerUse == 0)
                    CheckFail("a shop sells an item with no price", in, out);
                else if (Kind(out) != Kind(in)
                         && !(shopType == SHOP_TYPE_SECRET_SHOP && IsRareShopKind(Kind(out))))
                    CheckFail("a shop item changed kind", in, out);
                else if ((Kind(in) == KIND_WEAPON || Kind(in) == KIND_STAFF)
                         && AbsDiff(sLevel[out], sLevel[in]) > 1)
                    CheckFail("a shop item's rank is off by more than one", in, out);
            }
            if (first[len] != 0 || got[SHOP_ITEMS_MAX_AMT] != 0)
                CheckFail("a shop list is not terminated", start, len);
        }
    }

    gRandoRun = saved;
    gRandoRunOn = savedOn;
}

int RandoItems_SelfTest(void)
{
    sFails = 0;
    Classify();
    if (!sClassified) {
        PortLog("[rando] check: items: no vanilla snapshot\n");
        return 1;
    }
    CheckTables();
    CheckPickers();
    CheckRewardsAndShops();
    return sFails;
}

#endif /* PORT_PLAYED_BUILD */
