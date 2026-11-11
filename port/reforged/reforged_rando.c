#include "global.h"

/*
 * Reforged Options, the randomizer's core (port/reforged/reforged_rando.h has the
 * map of its files; modelled on the Universal FE Randomizer, UFER, MIT: the ideas
 * and algorithms, no code). Played builds only, empty in every test build.
 *
 * The run. A new game (src/bmsave.c WriteNewGameSave, Reforged_RandoNewGame) copies
 * the Randomizer screen's settings and seed into the run (gRandoRun); the save mark
 * (reforged_savemark.c) keeps the run's copy with every save block written, and a
 * save block's read (ReadGameSave / ReadSuspendSave, before InitUnits:
 * Reforged_RandoWillRead) makes that block's copy the run again, or none. So a run
 * keeps its own randomization whatever the menu says later. A block's run of another
 * algorithm version is not applied (vanilla tables) but carried (gRandoCarried): the
 * save mark writes it back with the blocks saved meanwhile, so the run is not lost.
 *
 * The tables. gCharacterData, gClassData and gItemData are kept as they are at the
 * first boot (Reforged_RandoBoot, src/main.c), once per process: a soft reset
 * (A+B+Start+Select, which goes back into AgbMain) boots again with the tables of the
 * run being played, so that boot puts the kept ones back and ends the run (and the
 * carried one) instead of taking them again. Every start (Rando_Start) puts them back first,
 * then, for a randomized run, rebuilds them from the run alone, in UFER's order:
 * player classes (reforged_rando_classes.c), bases, weapons (reforged_rando_items.c),
 * class movement / constitution / affinity, the enemy growth buff, growths (so
 * "transfer personal growths" happens before the growths are randomized); then
 * recruitment (reforged_rando_recruit.c: the fills take their characters' data as
 * the steps before left it) and the battle palettes (reforged_rando_palettes.c,
 * after every class change).
 *
 * Every choice is a pure function of the run (seed, algorithm version, values) and of
 * the thing chosen for (a character or class id): each step has its own salt, each
 * character or class its own PRNG stream or hash, never the game's RNG (whose state is
 * saved in suspends and drives battles). Loading a save later rebuilds the same tables.
 *
 * Limits kept (the save's bit widths and the engine's): personal + class bases within
 * 0 (HP 1) and the class cap, HP <= 60, other stats <= 31, luck 0-30; growths 0-255
 * (characters, u8) and 0-127 (classes, s8); class movement 4-8, <= 15.
 *
 * FE8PC_RANDO_CHECK=1 (a test switch, like FE8PC_RFO_CHECK): at the first boot, after
 * the snapshot, many seeds times several configs are applied and the tables checked
 * (limits, determinism, the restore, a refused run carried, a boot again; plus the
 * classes' and items' own checks), the totals logged ("[rando] self-test: N configs,
 * F failures"); =exit: the process then ends with 1 on a failure, 0 when clean.
 *
 * FE8PC_RANDO_SCAN=first[:count] (a test switch: seeds and what they give, logged; Scan below).
 *
 * FE8PC_RANDO_FORCE=<seed> (a test switch, for scripted runs on chapter saves that have
 * no save mark; hex with 0x, or decimal): a save block's read ignores the save mark and
 * a new game ignores the menu's seed; both apply the Randomizer screen's settings with
 * that seed, the randomizer on ("[rando] FE8PC_RANDO_FORCE: ..."). The settings file is
 * not changed. Unset: one getenv, nothing else.
 */

#if PORT_PLAYED_BUILD

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmunit.h"
#include "bmitem.h"
#include "constants/classes.h"
#include "constants/characters.h"

#include "port_hw.h"
#include "port_reforged.h"
#include "reforged_rando.h"

/* The tables' sizes (src/data_characters.c, data_classes.c, data_items.c; the arrays
   are declared without a size, so counted: 256 characters [pid - 1], 127 classes
   [jid - 1] for jids 0x01-0x7F, 206 items [iid] for 0x00-0xCD). */
#define RANDO_CHARS   256
#define RANDO_CLASSES 127
#define RANDO_ITEMS   206

#define STAT_HP_MAX   60 /* the engine's fixed HP cap (and the save's 6 bits) */
#define STAT_MAX      31 /* the save's 5 bits */
#define STAT_LCK_MAX  30 /* the engine's luck cap */

struct RandoConfig gRandoRun;
int gRandoRunOn;
struct RandoConfig gRandoCarried;
int gRandoCarriedOn;

static struct CharacterData sVanillaChars[RANDO_CHARS];
static struct ClassData sVanillaClasses[RANDO_CLASSES];
static struct ItemData sVanillaItems[RANDO_ITEMS];
static int sSnapshotTaken;
static int sBooted; /* Reforged_RandoBoot ran once: later boots are soft resets */
static int sQuiet; /* the self-test: no per-start log line */

static void TakeSnapshot(void)
{
    if (sSnapshotTaken)
        return;
    memcpy(sVanillaChars, gCharacterData, sizeof(sVanillaChars));
    memcpy(sVanillaClasses, gClassData, sizeof(sVanillaClasses));
    memcpy(sVanillaItems, gItemData, sizeof(sVanillaItems));
    sSnapshotTaken = 1;
}

static void RestoreSnapshot(void)
{
    TakeSnapshot();
    memcpy(gCharacterData, sVanillaChars, sizeof(sVanillaChars));
    memcpy(gClassData, sVanillaClasses, sizeof(sVanillaClasses));
    memcpy(gItemData, sVanillaItems, sizeof(sVanillaItems));
}

/* ------------------------------------------------------------------ table access */

const struct CharacterData *Rando_VanillaChar(int pid)
{
    if (pid < 1 || pid > RANDO_CHARS)
        return NULL;
    TakeSnapshot();
    return &sVanillaChars[pid - 1];
}

const struct ClassData *Rando_VanillaClass(int jid)
{
    if (jid < 1 || jid > RANDO_CLASSES)
        return NULL;
    TakeSnapshot();
    return &sVanillaClasses[jid - 1];
}

const struct ItemData *Rando_VanillaItem(int iid)
{
    if (iid < 0 || iid >= RANDO_ITEMS)
        return NULL;
    TakeSnapshot();
    return &sVanillaItems[iid];
}

struct CharacterData *Rando_Char(int pid)
{
    return pid < 1 || pid > RANDO_CHARS ? NULL : &gCharacterData[pid - 1];
}

struct ClassData *Rando_Class(int jid)
{
    return jid < 1 || jid > RANDO_CLASSES ? NULL : &gClassData[jid - 1];
}

struct ItemData *Rando_Item(int iid)
{
    return iid < 0 || iid >= RANDO_ITEMS ? NULL : &gItemData[iid];
}

int Rando_IsPlayable(int pid)
{
    return (pid >= 0x01 && pid <= 0x22 && pid != 0x1B) || (pid >= 0x24 && pid <= 0x2C);
}

/* ------------------------------------------------------------------ the run's options */

int Rando_Opt(int id)
{
    if (!gRandoRunOn || id < RFO_RANDO_FIRST || id > RFO_RANDO_LAST)
        return 0;
    return gRandoRun.v[id - RFO_RANDO_FIRST];
}

void Rando_ConfigFromSettings(struct RandoConfig *cfg)
{
    int i;

    memset(cfg, 0, sizeof(*cfg));
    cfg->seed = Reforged_GetRandoSeed();
    for (i = 0; i < RFO_RANDO_COUNT; i++)
        cfg->v[i] = (u8)Reforged_GetOption(RFO_RANDO_FIRST + i);
    /* "FE7/FE6 classes" is the version (RANDO_VERSION_THREE): an older build keeps that, not a new key; New
       Game then makes the ROM set the version (RomsAtNewGame) */
    cfg->v[RFO_RANDO_FE7 - RFO_RANDO_FIRST] = cfg->v[RFO_RANDO_FE7 - RFO_RANDO_FIRST] != 0;
    cfg->version = cfg->v[RFO_RANDO_FE7 - RFO_RANDO_FIRST] ? RANDO_VERSION : RANDO_VERSION_PLAIN;
}

/* Every class the randomizer may name from a ROM is loaded (reforged_fe7.c): FE7's Nomads, lords, Archsage,
   Dark Druid and Bard; FE6's Thief F, Troopers, King, Lion Lord, Master Lord and Divine Dragon. A ROM whose
   classes did not all build counts as not loaded (its loader logged why). */
int Rando_RomsLoaded(void)
{
    int roms = 0, jid, fe7 = 1, fe6 = 1;

    for (jid = 0x80; jid <= REFORGED_FOREIGN_LAST_JID; jid++)
        if (!Reforged_Fe7ClassAvailable(jid))
        {
            if (jid <= REFORGED_FE7_LAST_JID)
                fe7 = 0;
            else
                fe6 = 0;
        }
    if (fe7)
        roms |= RANDO_ROM_FE7;
    if (fe6)
        roms |= RANDO_ROM_FE6;
    return roms;
}

/* ------------------------------------------------------------------ hashes and PRNG */

static u32 Fmix32(u32 h)
{
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

u32 Rando_Hash(u32 salt, u32 a, u32 b, u32 c)
{
    u32 h = 0x243F6A88u;

    h = Fmix32(h ^ gRandoRun.seed);
    h = Fmix32(h + 0x9E3779B9u + (gRandoRun.version >= 2 ? 1u : gRandoRun.version)); /* RANDO_VERSION's note */
    h = Fmix32(h ^ (salt * 0x85EBCA77u + 0x165667B1u));
    h = Fmix32(h + a * 0xC2B2AE3Du + 0x27D4EB2Fu);
    h = Fmix32(h ^ (b * 0x165667B1u + 0x9E3779B9u));
    h = Fmix32(h + c * 0xD3A2646Du + 0xFD7046C5u);
    return h;
}

static u32 SplitMix32(u32 *x)
{
    u32 z = (*x += 0x9E3779B9u);

    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}

/* A stream of its own for (salt, key): a character's or class's choices never depend
   on what was chosen for another one. */
static void RngSeedFor(struct RandoRng *r, u32 salt, u32 key)
{
    u32 x = Rando_Hash(salt, key, 0x52A6D, 0x1D);
    int i;

    for (i = 0; i < 4; i++)
        r->s[i] = SplitMix32(&x);
    if ((r->s[0] | r->s[1] | r->s[2] | r->s[3]) == 0)
        r->s[0] = 1; /* xoshiro's one bad state */
}

void Rando_RngSeed(struct RandoRng *r, u32 salt)
{
    RngSeedFor(r, salt, 0);
}

static u32 Rotl32(u32 x, int k)
{
    return (x << k) | (x >> (32 - k));
}

u32 Rando_RngNext(struct RandoRng *r)
{
    u32 result = Rotl32(r->s[1] * 5, 7) * 9;
    u32 t = r->s[1] << 9;

    r->s[2] ^= r->s[0];
    r->s[3] ^= r->s[1];
    r->s[1] ^= r->s[2];
    r->s[0] ^= r->s[3];
    r->s[2] ^= t;
    r->s[3] = Rotl32(r->s[3], 11);
    return result;
}

int Rando_HashInt(u32 r, int n)
{
    if (n <= 0)
        return 0;
    return (int)(((unsigned long long)r * (unsigned)n) >> 32); /* multiply-shift */
}

int Rando_RngInt(struct RandoRng *r, int n)
{
    if (n <= 0)
        return 0;
    return Rando_HashInt(Rando_RngNext(r), n);
}

static int Clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int Min(int a, int b)
{
    return a < b ? a : b;
}

/* ------------------------------------------------------------------ growths */

/* UFER's stat order for growths: HP, STR, SKL, SPD, LCK, DEF, RES. */
static void GrowthFields(struct CharacterData *c, u8 *g[7])
{
    g[0] = &c->growthHP;
    g[1] = &c->growthPow;
    g[2] = &c->growthSkl;
    g[3] = &c->growthSpd;
    g[4] = &c->growthLck;
    g[5] = &c->growthDef;
    g[6] = &c->growthRes;
}

static void GrowthsRedistribute(struct RandoRng *r, int v[7], int min, int max, int var, int adjustHP)
{
    int total = 0, i, guard;

    for (i = 0; i < 7; i++)
        total += v[i];
    if (Rando_RngInt(r, 2) == 0)
        total += Rando_RngInt(r, var + 1);
    else
        total -= Rando_RngInt(r, var + 1);
    for (i = 0; i < 7; i++)
        v[i] = min;
    total -= 7 * min;
    if (7 * (max - min) <= total)
    {
        for (i = 0; i < 7; i++)
            v[i] = max; /* the budget does not fit: everything at max */
        return;
    }
    /* chunks of 5 to random stats, HP the fall-through pick (2/8, or 4/10 with
       "adjust HP"); bounded: a stream that keeps hitting full stats ends in order */
    for (guard = 0; total > 0 && guard < 4096; guard++)
    {
        int pick = Rando_RngInt(r, adjustHP ? 10 : 8);
        int stat = pick >= 1 && pick <= 6 ? pick : 0;
        int add = Min(5, Min(total, max - v[stat]));

        if (add <= 0)
            continue;
        v[stat] += add;
        total -= add;
    }
    for (i = 0; i < 7 && total > 0; i++)
    {
        int add = Min(total, max - v[i]);

        v[i] += add;
        total -= add;
    }
}

static void GrowthsDelta(struct RandoRng *r, int v[7], int min, int max, int d, int adjustHP)
{
    int i;

    for (i = 0; i < 7; i++)
    {
        int up = Rando_RngInt(r, 2) == 0;

        if (i == 0 && adjustHP)
            up = 1; /* "adjust HP growths": HP never goes down */
        if (up && v[i] < max)
            v[i] += Rando_RngInt(r, Min(d + 1, max - v[i] + 1));
        else if (v[i] > min)
            v[i] -= Rando_RngInt(r, Min(d + 1, v[i] - min + 1));
        v[i] = Clamp(v[i], min, max);
    }
}

static void GrowthsFull(struct RandoRng *r, int v[7], int min, int max, int adjustHP)
{
    int range = max - min, i;

    for (i = 0; i < 7; i++)
        v[i] = Rando_RngInt(r, range + 1) + min;
    if (adjustHP && v[0] < range / 2 + min)
        v[0] = Min(v[0] + range / 2, max);
}

static void ApplyGrowths(void)
{
    int mode = Rando_Opt(RFO_RANDO_GROWTHS);
    int var = Rando_Opt(RFO_RANDO_GROWTH_VAR);
    int min = Rando_Opt(RFO_RANDO_GROWTH_MIN);
    int max = Rando_Opt(RFO_RANDO_GROWTH_MAX);
    int adjustHP = Rando_Opt(RFO_RANDO_GROWTH_HP) != 0;
    int pid, i;

    if (mode == 0)
        return;
    if (max < min)
        max = min;
    for (pid = 1; pid <= RANDO_CHARS; pid++)
    {
        struct CharacterData *c = Rando_Char(pid);
        struct RandoRng r;
        u8 *g[7];
        int v[7];

        if (!Rando_IsPlayable(pid) || c == NULL)
            continue;
        GrowthFields(c, g);
        for (i = 0; i < 7; i++)
            v[i] = *g[i];
        RngSeedFor(&r, RANDO_SALT_GROWTHS, (u32)pid);
        if (mode == 1)
            GrowthsRedistribute(&r, v, min, max, var, adjustHP);
        else if (mode == 2)
            GrowthsDelta(&r, v, min, max, var, adjustHP);
        else
            GrowthsFull(&r, v, min, max, adjustHP);
        for (i = 0; i < 7; i++)
            *g[i] = (u8)Clamp(Clamp(v[i], min, max), 0, 255);
    }
}

/* ------------------------------------------------------------------ bases */

/* UFER's stat order for bases: HP, STR, SKL, SPD, DEF, RES, LCK. Personal bases are
   offsets from the class's, kept so that class + personal is within [0 (HP 1), cap];
   luck (the character's alone) 0-30. */
struct BaseStat {
    s8 *personal;
    int lo, hi;
};

static void BaseFields(struct CharacterData *c, const struct ClassData *k, struct BaseStat b[7])
{
    int cls[6], cap[6], i;

    b[0].personal = &c->baseHP;
    b[1].personal = &c->basePow;
    b[2].personal = &c->baseSkl;
    b[3].personal = &c->baseSpd;
    b[4].personal = &c->baseDef;
    b[5].personal = &c->baseRes;
    b[6].personal = &c->baseLck;
    cls[0] = k->baseHP;  cap[0] = Min(k->maxHP, STAT_HP_MAX);
    cls[1] = k->basePow; cap[1] = Min(k->maxPow, STAT_MAX);
    cls[2] = k->baseSkl; cap[2] = Min(k->maxSkl, STAT_MAX);
    cls[3] = k->baseSpd; cap[3] = Min(k->maxSpd, STAT_MAX);
    cls[4] = k->baseDef; cap[4] = Min(k->maxDef, STAT_MAX);
    cls[5] = k->baseRes; cap[5] = Min(k->maxRes, STAT_MAX);
    for (i = 0; i < 6; i++)
    {
        b[i].lo = (i == 0 ? 1 : 0) - cls[i];
        b[i].hi = cap[i] - cls[i];
        if (b[i].hi < b[i].lo)
            b[i].hi = b[i].lo; /* a class base over its own cap: keep the floor */
    }
    b[6].lo = 0;
    b[6].hi = STAT_LCK_MAX;
}

static void BasesRedistribute(struct RandoRng *r, struct BaseStat b[7], int var)
{
    /* nextInt(9): 0, 1, 5 HP; 2 STR; 3 SKL; 4 SPD; 6 DEF; 7 RES; 8 LCK */
    static const u8 sPick[9] = { 0, 0, 1, 2, 3, 0, 4, 5, 6 };
    int v[7], total = 0, i, guard;

    for (i = 0; i < 7; i++)
        total += *b[i].personal;
    if (Rando_RngInt(r, 2) == 0)
        total += Rando_RngInt(r, var + 1);
    else
        total -= Rando_RngInt(r, var + 1);
    for (i = 0; i < 6; i++)
        v[i] = Clamp(0, b[i].lo, b[i].hi);
    v[6] = Clamp(Rando_RngInt(r, 4) + Rando_RngInt(r, 4), b[6].lo, b[6].hi);
    total -= v[6];
    if (total < 0)
        total = 0;
    /* UFER's loop can hang with every stat at its bound: bounded, and it stops when
       nothing has room */
    for (guard = 0; total > 0 && guard < 2048; guard++)
    {
        int stat = sPick[Rando_RngInt(r, 9)];
        int amt = Min(Rando_RngInt(r, 3) + 1, total);
        int room = b[stat].hi - v[stat];

        if (room <= 0)
        {
            for (i = 0; i < 7 && v[i] >= b[i].hi; i++)
                ;
            if (i == 7)
                break;
            continue;
        }
        amt = Min(amt, room);
        v[stat] += amt;
        total -= amt;
    }
    for (i = 0; i < 7; i++)
        *b[i].personal = (s8)Clamp(v[i], b[i].lo, b[i].hi);
}

static void BasesDelta(struct RandoRng *r, struct BaseStat b[7], int d)
{
    int i, v;

    for (i = 0; i < 7; i++)
    {
        v = *b[i].personal;
        if (Rando_RngInt(r, 2) == 0)
            v += Rando_RngInt(r, d + 1);
        else
            v -= Rando_RngInt(r, d + 1);
        *b[i].personal = (s8)Clamp(v, b[i].lo, b[i].hi);
    }
}

static void ApplyBases(void)
{
    int mode = Rando_Opt(RFO_RANDO_BASES);
    int var = Rando_Opt(RFO_RANDO_BASE_VAR);
    int pid;

    if (mode == 0)
        return;
    for (pid = 1; pid <= RANDO_CHARS; pid++)
    {
        struct CharacterData *c = Rando_Char(pid);
        const struct ClassData *k;
        struct BaseStat b[7];
        struct RandoRng r;

        if (!Rando_IsPlayable(pid) || c == NULL)
            continue;
        k = Rando_Class(c->defaultClass); /* the class this run gives (after step 1) */
        if (k == NULL)
            continue;
        BaseFields(c, k, b);
        RngSeedFor(&r, RANDO_SALT_BASES, (u32)pid);
        if (mode == 1)
            BasesRedistribute(&r, b, var);
        else
            BasesDelta(&r, b, var);
    }
}

/* ------------------------------------------------------------------ MOV / CON / affinity */

/* A class units really are (battle animation, not a ballista, civilian, phantom or egg)
   with movement: the ones "Class movement" changes. Not the Demon King either (the
   final boss alone has it; its map is made for its 2 MOV, and UFER never changes
   Fomortiis). */
static int MovClass(int jid)
{
    const struct ClassData *k = Rando_VanillaClass(jid);

    if (k == NULL || k->baseMov <= 0 || k->pBattleAnimDef == NULL)
        return 0;
    if (jid == CLASS_PHANTOM || jid == CLASS_GORGONEGG || jid == CLASS_GORGONEGG2 || jid == CLASS_DEMON_KING)
        return 0;
    if (jid >= CLASS_BLST_REGULAR_USED && jid <= CLASS_CIVILIAN_F3)
        return 0; /* the ballista states 0x67-0x6C and the civilians 0x6D-0x72 */
    return 1;
}

static int VanillaPromoted(int jid)
{
    const struct ClassData *k = Rando_VanillaClass(jid);

    return k != NULL && (k->attributes & CA_PROMOTED) != 0;
}

/* The highest movement (this run's) among the unpromoted classes promoting into the
   promoted class jid: gPromoJidLut, the unpromoted ClassData.promotion, and the
   promoted class's own back pointer. 0 when none. */
static int BaseClassesMov(int jid)
{
    const struct ClassData *k = Rando_VanillaClass(jid);
    int best = 0, u, mov;

    for (u = 1; u <= RANDO_CLASSES; u++)
    {
        const struct ClassData *uk = Rando_VanillaClass(u);

        if (VanillaPromoted(u))
            continue;
        if (uk->promotion != jid && gPromoJidLut[u][0] != jid && gPromoJidLut[u][1] != jid)
            continue;
        mov = Rando_Class(u)->baseMov;
        if (mov > best)
            best = mov;
    }
    if (k != NULL && k->promotion != 0 && k->promotion != jid && !VanillaPromoted(k->promotion) &&
        Rando_Class(k->promotion) != NULL)
    {
        mov = Rando_Class(k->promotion)->baseMov;
        if (mov > best)
            best = mov;
    }
    return best;
}

static void ApplyMov(void)
{
    int pass, jid, lo;

    if (!Rando_Opt(RFO_RANDO_MOV))
        return;
    /* unpromoted first: a promoted class never moves less than its base classes */
    for (pass = 0; pass < 2; pass++)
        for (jid = 1; jid <= RANDO_CLASSES; jid++)
        {
            u32 h;

            if (!MovClass(jid) || VanillaPromoted(jid) != pass)
                continue;
            h = Rando_Hash(RANDO_SALT_MOV, (u32)jid, 0, 0);
            lo = 4;
            if (pass == 1 && BaseClassesMov(jid) > lo)
                lo = Min(BaseClassesMov(jid), 8);
            Rando_Class(jid)->baseMov = (s8)Clamp(lo + Rando_HashInt(h, 8 - lo + 1), 0, 15);
        }
}

static void ApplyCon(void)
{
    int pid;

    if (!Rando_Opt(RFO_RANDO_CON))
        return;
    for (pid = 1; pid <= RANDO_CHARS; pid++)
    {
        struct CharacterData *c = Rando_Char(pid);
        const struct ClassData *k;
        struct RandoRng r;
        int total, d;

        if (!Rando_IsPlayable(pid) || c == NULL || (k = Rando_Class(c->defaultClass)) == NULL)
            continue;
        RngSeedFor(&r, RANDO_SALT_CON, (u32)pid);
        total = k->baseCon + c->baseCon;
        d = Rando_RngInt(&r, 3); /* UFER: nextInt(variance), exclusive */
        total += Rando_RngInt(&r, 2) == 0 ? d : -d;
        if (total < 3)
            total = 3;
        if (total > k->maxCon)
            total = k->maxCon;
        c->baseCon = (s8)(total - k->baseCon);
    }
}

static void ApplyAffinity(void)
{
    int pid;

    if (!Rando_Opt(RFO_RANDO_AFFINITY))
        return;
    for (pid = 1; pid <= RANDO_CHARS; pid++)
        if (Rando_IsPlayable(pid))
            Rando_Char(pid)->affinity =
                (u8)(UNIT_AFFIN_FIRE + Rando_HashInt(Rando_Hash(RANDO_SALT_AFFINITY, (u32)pid, 0, 0),
                                                      UNIT_AFFIN_ANIMA - UNIT_AFFIN_FIRE + 1));
}

/* ------------------------------------------------------------------ enemy growth buff */

/* Class growths drive the generic enemies' autolevel; player level-ups use the
   characters' growths, so they are not touched. Capped at 127 (s8). */
static void ApplyEnemyGrowth(void)
{
    int mode = Rando_Opt(RFO_RANDO_ENEMY_GROWTH);
    int amt = Rando_Opt(RFO_RANDO_ENEMY_GROWTH_AMT);
    int jid, i;

    if (mode == 0 || amt == 0)
        return;
    for (jid = 1; jid <= RANDO_CLASSES; jid++)
    {
        struct ClassData *k = Rando_Class(jid);
        s8 *g[7];

        g[0] = &k->growthHP;
        g[1] = &k->growthPow;
        g[2] = &k->growthSkl;
        g[3] = &k->growthSpd;
        g[4] = &k->growthDef;
        g[5] = &k->growthRes;
        g[6] = &k->growthLck;
        for (i = 0; i < 7; i++)
        {
            int v = *g[i];

            if (v < 0)
                continue;
            v = mode == 1 ? v + amt : v * (100 + amt) / 100;
            *g[i] = (s8)Clamp(v, 0, 127);
        }
    }
}

/* ------------------------------------------------------------------ the start */

/* "key=value" for every option of cfg not at its default. */
static void DescribeConfig(const struct RandoConfig *cfg, char *buf, size_t size)
{
    size_t len = 0;
    int i;

    buf[0] = 0;
    for (i = 0; i < RFO_RANDO_COUNT && len < size; i++)
    {
        int id = RFO_RANDO_FIRST + i;
        const char *key = Reforged_OptionKey(id);
        int n;

        if (id == RFO_RANDO_ENABLE || cfg->v[i] == Reforged_OptionDefault(id))
            continue;
        n = snprintf(buf + len, size - len, " %s=%d", key != NULL ? key : "?", cfg->v[i]);
        if (n < 0)
            break;
        len += (size_t)n;
    }
    if (len >= size && size > 4)
        memcpy(buf + size - 4, "...", 4);
}

void Rando_Start(const struct RandoConfig *cfg)
{
    char desc[1024];

    gRandoCarriedOn = 0; /* whatever started before is no longer being played */
    memset(&gRandoCarried, 0, sizeof(gRandoCarried));
    RestoreSnapshot();
    gRandoRunOn = 0;
    memset(&gRandoRun, 0, sizeof(gRandoRun));
    if (cfg != NULL && cfg->v[RFO_RANDO_ENABLE - RFO_RANDO_FIRST] != 0)
    {
        if (cfg->version < RANDO_VERSION_MIN || cfg->version > RANDO_VERSION)
        {
            /* carried: the blocks saved while it is played keep it (reforged_savemark.c) */
            gRandoCarried = *cfg;
            gRandoCarriedOn = 1;
            PortLog("[rando] the run was randomized by version %d of the randomizer, this build has version %d: "
                    "not applied, the tables stay vanilla; its randomizer entry (seed 0x%08X) is kept with the "
                    "blocks saved from now on\n", cfg->version, RANDO_VERSION, (unsigned)cfg->seed);
        }
        else
        {
            gRandoRun = *cfg;
            gRandoRunOn = 1;
            /* the version decides "FE7/FE6 classes" (RANDO_VERSION_FE7, RANDO_VERSION_THREE), whatever the
               entry's randomizer_fe7 says (an older build that carried the run drops that key) */
            gRandoRun.v[RFO_RANDO_FE7 - RFO_RANDO_FIRST] = (u8)(Rando_VersionRoms(cfg->version) != 0 ? 1 : 0);
        }
    }

    /* Every step runs at every start: with no run each changes nothing but clears its
       own per-run state (the classes' player-class map and unit key). The facts the
       classes and items files cache come from the snapshot, which never changes. */
    RandoPromotions_Apply(); /* "Random promotions": before the classes read them */
    RandoClasses_Apply(); /* player classes (ranks, bases, growth transfer), the boss map */
    ApplyBases();
    RandoItems_Apply();   /* weapons */
    ApplyMov();
    ApplyCon();
    ApplyAffinity();
    ApplyEnemyGrowth();
    ApplyGrowths();
    RandoRecruit_Apply();  /* the fills take this run's data of their characters */
    RandoPalettes_Apply(); /* after every class change */
    RandoClasses_ApplyForeign(); /* versions 11-14: the players' foreign rolls, after everything they must not move */
    Reforged_Promo3Tables(); /* v1.23.0, "Third promotion" (a game without the randomizer): the twins' prf locks */

    if (gRandoRunOn && !sQuiet)
    {
        DescribeConfig(&gRandoRun, desc, sizeof(desc));
        PortLog("[rando] tables randomized: seed 0x%08X, version %d,%s\n", (unsigned)cfg->seed, cfg->version,
            desc[0] ? desc : " every option at its default");
    }
}

/* ------------------------------------------------------------------ entry points */

/* FE8PC_RANDO_FORCE (the top of this file): 1 and the menu's settings with its seed,
   the randomizer on, in cfg; 0 when unset (or not a number, logged). */
static int ForcedConfig(struct RandoConfig *cfg, const char *what)
{
    const char *s = getenv("FE8PC_RANDO_FORCE");
    const char *digits, *p;
    unsigned long seed;
    int hex, ok;

    if (s == NULL)
        return 0;
    hex = s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
    digits = hex ? s + 2 : s;
    ok = digits[0] != 0;
    for (p = digits; *p != 0 && ok; p++)
        ok = (*p >= '0' && *p <= '9') || (hex && ((*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')));
    seed = ok ? strtoul(digits, NULL, hex ? 16 : 10) : 0;
    if (!ok)
    {
        PortLog("[rando] FE8PC_RANDO_FORCE='%s' is not a seed (hex with 0x, or decimal): ignored\n", s);
        return 0;
    }
    Rando_ConfigFromSettings(cfg);
    cfg->seed = (u32)seed;
    cfg->v[RFO_RANDO_ENABLE - RFO_RANDO_FIRST] = 1;
    PortLog("[rando] FE8PC_RANDO_FORCE: %s: the Randomizer screen's settings with seed 0x%08X, the randomizer "
            "on (a test switch; the settings file is not changed)\n", what, (unsigned)cfg->seed);
    return 1;
}

/* "FE7/FE6 classes" (RANDO_VERSION_THREE) at New Game: the run includes the foreign ROMs whose classes are
   loaded now (Rando_RomsLoaded), recorded as its version (since v1.22.0 15 + the set: 16 FE7, 17 FE6, 18 both,
   Rando_VersionForRoms; v1.21.0 recorded 11-14); with the row off, or neither ROM loaded, it is version 15
   (RANDO_VERSION_PLAIN). A ROM appearing later never changes
   the run's picks; one that disappears later leaves FE8 choices and saved units' stand-ins. */
static void RomsAtNewGame(struct RandoConfig *cfg, const char *what)
{
    int roms;

    if (!cfg->v[RFO_RANDO_ENABLE - RFO_RANDO_FIRST])
        return;
    if (cfg->version != RANDO_VERSION)
    {
        PortLog("[rando] %s: randomizer version %d (three promotion choices; \"FE7/FE6 classes\" off)\n", what,
            cfg->version);
        return;
    }
    roms = Rando_RomsLoaded();
    cfg->version = (u8)Rando_VersionForRoms(roms);
    cfg->v[RFO_RANDO_FE7 - RFO_RANDO_FIRST] = (u8)(roms != 0);
    if (roms != 0)
        PortLog("[rando] %s: \"FE7/FE6 classes\" on with %s loaded: randomizer version %d (three promotion "
                "choices; this run may use the %s classes)\n", what,
            roms == (RANDO_ROM_FE7 | RANDO_ROM_FE6) ? "the FE7 and FE6 ROMs" : roms == RANDO_ROM_FE7 ? "the FE7 ROM" : "the FE6 ROM",
            cfg->version, roms == (RANDO_ROM_FE7 | RANDO_ROM_FE6) ? "FE7 and FE6" : roms == RANDO_ROM_FE7 ? "FE7" : "FE6");
    else
        PortLog("[rando] %s: \"FE7/FE6 classes\" is on but neither foreign ROM is loaded: randomizer version %d "
                "(three promotion choices, no foreign classes)\n", what, cfg->version);
}

void Reforged_RandoNewGame(void)
{
    struct RandoConfig cfg;
    int on;

    Reforged_GameFixesNewGame(); /* the Game fixes screen's choices: also fixed for this run */
    if (ForcedConfig(&cfg, "new game"))
    {
        RomsAtNewGame(&cfg, "new game");
        Rando_Start(&cfg);
        RandoNuzlocke_NewGame();
        PortLog("[rando] new game: randomized run, seed 0x%08X\n", (unsigned)gRandoRun.seed);
        return;
    }
    Rando_ConfigFromSettings(&cfg);
    on = cfg.v[RFO_RANDO_ENABLE - RFO_RANDO_FIRST] != 0;
    if (on && cfg.seed == 0)
    {
        unsigned seed = Reforged_NewRandoSeed();

        Reforged_SetRandoSeed(seed);
        Reforged_SaveSettings(); /* the menu then shows the seed this run has */
        cfg.seed = seed;
        PortLog("[rando] new game: seed 0x%08X picked\n", seed);
    }
    RomsAtNewGame(&cfg, "new game");
    Rando_Start(on ? &cfg : NULL);
    RandoNuzlocke_NewGame();
    if (gRandoRunOn)
        PortLog("[rando] new game: randomized run, seed 0x%08X\n", (unsigned)gRandoRun.seed);
    else
        PortLog("[rando] new game: not randomized (vanilla tables)\n");
}

void Reforged_RandoWillRead(int block)
{
    const struct RandoConfig *cfg = NULL;
    struct RandoConfig forced;
    char what[32];

    snprintf(what, sizeof(what), "save block %d", block);
    if (ForcedConfig(&forced, what))
    {
        RomsAtNewGame(&forced, what); /* the version a new game would record now (the ROMs loaded) */
        cfg = &forced; /* the save mark ignored */
    }
    else if (block >= 0 && block <= 4)
        cfg = Rando_SaveMarkBlock(block);
    Rando_Start(cfg);
    if (gRandoRunOn)
        PortLog("[rando] save block %d: randomized run, seed 0x%08X, tables rebuilt\n", block,
            (unsigned)gRandoRun.seed);
    else if (gRandoCarriedOn)
        PortLog("[rando] save block %d: its randomizer entry (version %d) was not applied: vanilla tables; the "
                "entry is carried, saves keep it\n", block, gRandoCarried.version);
    else if (cfg != NULL)
        PortLog("[rando] save block %d: its randomizer entry was not applied: vanilla tables\n", block);
    else
        PortLog("[rando] save block %d: not randomized: vanilla tables\n", block);
}

int Reforged_RandoActive(void)
{
    return gRandoRunOn;
}

unsigned Reforged_RandoActiveSeed(void)
{
    return gRandoRunOn ? (unsigned)gRandoRun.seed : 0;
}

/* ------------------------------------------------------------------ self-test */

static int sCheckLogged;

static int CheckFail(const char *fmt, ...)
{
    char buf[512];
    va_list ap;

    if (sCheckLogged++ < 60)
    {
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        PortLog("[rando] check: %s\n", buf);
    }
    else if (sCheckLogged == 61)
        PortLog("[rando] check: (more failures, not logged)\n");
    return 1;
}

static int RangeKnown(int encodedRange)
{
    switch (encodedRange)
    {
    case 0x11: case 0x12: case 0x13: case 0x14: case 0x22: case 0x23: case 0x33: case 0x3A: case 0x3F:
        return 1;
    }
    return 0;
}

/* The live tables against the limits; cfg: the run applied (NULL: vanilla). */
static int CheckTables(const struct RandoConfig *cfg, const char *what)
{
#define CFG(id) (cfg != NULL ? cfg->v[(id) - RFO_RANDO_FIRST] : 0)
    int fails = 0, pid, jid, iid, i;
    int gmin = CFG(RFO_RANDO_GROWTH_MIN), gmax = CFG(RFO_RANDO_GROWTH_MAX);

    if (gmax < gmin)
        gmax = gmin;
    for (pid = 1; pid <= RANDO_CHARS; pid++)
    {
        const struct CharacterData *c = Rando_Char(pid);
        const struct ClassData *k;
        int cls[6], cap[6], per[6], hard[6];
        u8 g[7];

        if (c->number != Rando_VanillaChar(pid)->number)
            fails += CheckFail("%s: pid 0x%02X: number changed", what, pid);
        if (!Rando_IsPlayable(pid))
            continue;
        if (c->defaultClass > 0x7F && c->defaultClass <= REFORGED_FOREIGN_LAST_JID &&
            RandoClasses_PlayerFe8Class(pid) != 0 && Reforged_Fe7ClassAvailable(c->defaultClass))
            k = GetClassData(c->defaultClass); /* versions 11-14: a foreign class of the run (its limits) */
        else if (c->defaultClass < 1 || c->defaultClass > 0x7F || (k = Rando_Class(c->defaultClass)) == NULL ||
            k->pBattleAnimDef == NULL)
        {
            fails += CheckFail("%s: pid 0x%02X: default class 0x%02X is not a unit class", what, pid,
                c->defaultClass);
            continue;
        }
        cls[0] = k->baseHP;  cap[0] = k->maxHP;  per[0] = c->baseHP;  hard[0] = STAT_HP_MAX;
        cls[1] = k->basePow; cap[1] = k->maxPow; per[1] = c->basePow; hard[1] = STAT_MAX;
        cls[2] = k->baseSkl; cap[2] = k->maxSkl; per[2] = c->baseSkl; hard[2] = STAT_MAX;
        cls[3] = k->baseSpd; cap[3] = k->maxSpd; per[3] = c->baseSpd; hard[3] = STAT_MAX;
        cls[4] = k->baseDef; cap[4] = k->maxDef; per[4] = c->baseDef; hard[4] = STAT_MAX;
        cls[5] = k->baseRes; cap[5] = k->maxRes; per[5] = c->baseRes; hard[5] = STAT_MAX;
        for (i = 0; i < 6; i++)
        {
            int total = cls[i] + per[i];

            if (total < (i == 0 ? 1 : 0) || total > hard[i])
                fails += CheckFail("%s: pid 0x%02X: base stat %d is %d (class %d + personal %d)", what, pid, i,
                    total, cls[i], per[i]);
            else if (CFG(RFO_RANDO_BASES) && total > cap[i] && cls[i] <= cap[i])
                fails += CheckFail("%s: pid 0x%02X: base stat %d is %d, over the class cap %d", what, pid, i,
                    total, cap[i]);
        }
        if (c->baseLck < 0 || c->baseLck > STAT_LCK_MAX)
            fails += CheckFail("%s: pid 0x%02X: luck %d", what, pid, c->baseLck);
        if (k->baseCon + c->baseCon < 1 || k->baseCon + c->baseCon > STAT_MAX)
            fails += CheckFail("%s: pid 0x%02X: con %d", what, pid, k->baseCon + c->baseCon);
        else if (CFG(RFO_RANDO_CON) && (k->baseCon + c->baseCon > k->maxCon ||
                                        (k->baseCon + c->baseCon < 3 && k->maxCon >= 3)))
            fails += CheckFail("%s: pid 0x%02X: con %d outside 3-%d", what, pid, k->baseCon + c->baseCon,
                k->maxCon);
        if (c->affinity > UNIT_AFFIN_ANIMA || (CFG(RFO_RANDO_AFFINITY) && c->affinity < UNIT_AFFIN_FIRE))
            fails += CheckFail("%s: pid 0x%02X: affinity %d", what, pid, c->affinity);
        if (CFG(RFO_RANDO_GROWTHS))
        {
            g[0] = c->growthHP; g[1] = c->growthPow; g[2] = c->growthSkl; g[3] = c->growthSpd;
            g[4] = c->growthDef; g[5] = c->growthRes; g[6] = c->growthLck;
            for (i = 0; i < 7; i++)
                if (g[i] < gmin || g[i] > gmax)
                    fails += CheckFail("%s: pid 0x%02X: growth %d is %d, outside %d-%d", what, pid, i, g[i],
                        gmin, gmax);
        }
    }
    for (jid = 1; jid <= RANDO_CLASSES; jid++)
    {
        const struct ClassData *k = Rando_Class(jid);
        const s8 *g[7];

        if (k->number != Rando_VanillaClass(jid)->number)
            fails += CheckFail("%s: class 0x%02X: number changed", what, jid);
        if (k->baseMov < 0 || k->baseMov > 15)
            fails += CheckFail("%s: class 0x%02X: mov %d", what, jid, k->baseMov);
        if (CFG(RFO_RANDO_MOV) && MovClass(jid))
        {
            int lo = 4;

            if (VanillaPromoted(jid) && BaseClassesMov(jid) > lo)
                lo = Min(BaseClassesMov(jid), 8);
            if (k->baseMov < lo || k->baseMov > 8)
                fails += CheckFail("%s: class 0x%02X: mov %d outside %d-8", what, jid, k->baseMov, lo);
        }
        g[0] = &k->growthHP; g[1] = &k->growthPow; g[2] = &k->growthSkl; g[3] = &k->growthSpd;
        g[4] = &k->growthDef; g[5] = &k->growthRes; g[6] = &k->growthLck;
        for (i = 0; i < 7; i++)
            if (*g[i] < 0)
                fails += CheckFail("%s: class 0x%02X: growth %d is %d", what, jid, i, *g[i]);
    }
    for (iid = 0; iid < RANDO_ITEMS; iid++)
    {
        const struct ItemData *it = Rando_Item(iid), *v = Rando_VanillaItem(iid);

        if (it->number != v->number)
            fails += CheckFail("%s: item 0x%02X: number changed", what, iid);
        if (it->maxUses > 63 || (v->maxUses > 0 && it->maxUses < 1))
            fails += CheckFail("%s: item 0x%02X: uses %d", what, iid, it->maxUses);
        if (v->weaponType > ITYPE_DARK)
            continue;
        if (it->weaponType > ITYPE_DARK)
            fails += CheckFail("%s: item 0x%02X: weapon type %d", what, iid, it->weaponType);
        if (it->might > (v->might > 31 ? v->might : 31) || it->weight > (v->weight > 30 ? v->weight : 30) ||
            it->crit > (v->crit > 100 ? v->crit : 100) || it->hit > (v->hit > 100 ? v->hit : 100))
            fails += CheckFail("%s: item 0x%02X: might %d hit %d weight %d crit %d", what, iid, it->might, it->hit,
                it->weight, it->crit);
        if (RangeKnown(v->encodedRange) ? !RangeKnown(it->encodedRange) : it->encodedRange != v->encodedRange)
            fails += CheckFail("%s: item 0x%02X: range 0x%02X", what, iid, it->encodedRange);
    }
    return fails;
#undef CFG
}

/* The documented defaults of the RFO_RANDO_* values (port_reforged.h), set here so the
   test does not depend on the settings file. */
static void TestBase(struct RandoConfig *cfg, u32 seed)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->seed = seed;
    cfg->version = RANDO_VERSION_THREE; /* the FE7 classes are the version (RANDO_VERSION_FE7). Versions 11-14 (the
                                           Keep colours sets of v1.21.0's saves, built at the start); each config is
                                           also run as 15-18 (the same tables, SelfTest) */
#define SET(id, val) (cfg->v[(id) - RFO_RANDO_FIRST] = (u8)(val))
    SET(RFO_RANDO_ENABLE, 1);
    SET(RFO_RANDO_GROWTH_VAR, 30);
    SET(RFO_RANDO_GROWTH_MIN, 5);
    SET(RFO_RANDO_GROWTH_MAX, 80);
    SET(RFO_RANDO_BASE_VAR, 5);
    SET(RFO_RANDO_GENDER, 1);
    SET(RFO_RANDO_PALETTES, 1);
    SET(RFO_RANDO_ENEMY_GROWTH_AMT, 10);
    SET(RFO_RANDO_BOSS_BUFF_AMT, 5);
}

/* Test config `kind` (0 when there is none). */
static int TestConfig(int kind, u32 seed, struct RandoConfig *cfg)
{
    TestBase(cfg, seed);
    switch (kind)
    {
    case 0: /* everything on */
    case 1: /* everything on, the other modes */
        SET(RFO_RANDO_GROWTHS, kind == 0 ? 1 : 2);
        SET(RFO_RANDO_GROWTH_HP, kind);
        SET(RFO_RANDO_BASES, kind == 0 ? 1 : 2);
        SET(RFO_RANDO_BASE_VAR, kind == 0 ? 5 : 10);
        SET(RFO_RANDO_MOV, 1);
        SET(RFO_RANDO_CON, 1);
        SET(RFO_RANDO_AFFINITY, 1);
        SET(RFO_RANDO_CLASSES, 1);
        SET(RFO_RANDO_LORDS, 1);
        SET(RFO_RANDO_THIEVES, 1);
        SET(RFO_RANDO_SPECIAL, 1);
        SET(RFO_RANDO_MONSTERS, 1);
        SET(RFO_RANDO_EVENLY, kind == 0);
        SET(RFO_RANDO_GENDER, kind == 0);
        SET(RFO_RANDO_CLASS_GROWTHS, kind == 0);
        SET(RFO_RANDO_PROMOTIONS, 1);
        SET(RFO_RANDO_PROMO_MONSTERS, kind);
        SET(RFO_RANDO_FE7, 1);
        cfg->version = RANDO_VERSION_THREE + 3; /* "FE7 classes" on: the version says so (14; 18 too, SelfTest) */
        SET(RFO_RANDO_MUSIC, 1);
        SET(RFO_RANDO_FLOOR_MONSTERS, 1);
        SET(RFO_RANDO_CLASS_BASES, kind);
        SET(RFO_RANDO_BOSSES, 1);
        SET(RFO_RANDO_ENEMIES, 1);
        SET(RFO_RANDO_ENEMY_GROWTH, kind == 0 ? 1 : 2);
        SET(RFO_RANDO_ENEMY_GROWTH_AMT, kind == 0 ? 10 : 100);
        SET(RFO_RANDO_BOSS_BUFF, kind == 0 ? 1 : 2);
        SET(RFO_RANDO_BOSS_BUFF_AMT, kind == 0 ? 5 : 20);
        SET(RFO_RANDO_ENEMY_WEAPONS, kind == 0 ? 25 : 100);
        SET(RFO_RANDO_W_MIGHT, 1);
        SET(RFO_RANDO_W_HIT, 1);
        SET(RFO_RANDO_W_WEIGHT, 1);
        SET(RFO_RANDO_W_USES, 1);
        SET(RFO_RANDO_W_EFFECTS, kind == 0 ? 25 : 100);
        SET(RFO_RANDO_REWARDS, 1);
        SET(RFO_RANDO_DROPS, kind == 0 ? 10 : 100);
        SET(RFO_RANDO_SHOPS, 1);
        SET(RFO_RANDO_RECRUIT, 1);
        SET(RFO_RANDO_RECRUIT_CLASS, kind);
        SET(RFO_RANDO_RECRUIT_STATS, kind);
        SET(RFO_RANDO_RECRUIT_GENDER, kind);
        return 1;
    case 2: /* classes with lords, thieves, the dancer and monsters */
        SET(RFO_RANDO_CLASSES, 1);
        SET(RFO_RANDO_LORDS, 1);
        SET(RFO_RANDO_THIEVES, 1);
        SET(RFO_RANDO_SPECIAL, 1);
        SET(RFO_RANDO_MONSTERS, 1);
        return 1;
    case 3: /* growths: redistribute */
        SET(RFO_RANDO_GROWTHS, 1);
        return 1;
    case 4: /* redistribute, adjust HP, the widest range */
        SET(RFO_RANDO_GROWTHS, 1);
        SET(RFO_RANDO_GROWTH_HP, 1);
        SET(RFO_RANDO_GROWTH_VAR, 100);
        SET(RFO_RANDO_GROWTH_MIN, 0);
        SET(RFO_RANDO_GROWTH_MAX, 255);
        return 1;
    case 5: /* delta */
        SET(RFO_RANDO_GROWTHS, 2);
        SET(RFO_RANDO_GROWTH_HP, 1);
        SET(RFO_RANDO_GROWTH_VAR, 100);
        return 1;
    case 6: /* full random */
        SET(RFO_RANDO_GROWTHS, 3);
        SET(RFO_RANDO_GROWTH_HP, 1);
        return 1;
    case 7: /* full random, min over max */
        SET(RFO_RANDO_GROWTHS, 3);
        SET(RFO_RANDO_GROWTH_MIN, 100);
        SET(RFO_RANDO_GROWTH_MAX, 0);
        return 1;
    case 8: /* bases: redistribute */
    case 9: /* bases: delta */
        SET(RFO_RANDO_BASES, kind == 8 ? 1 : 2);
        SET(RFO_RANDO_BASE_VAR, 10);
        return 1;
    case 10: /* bosses and enemies */
        SET(RFO_RANDO_BOSSES, 1);
        SET(RFO_RANDO_ENEMIES, 1);
        SET(RFO_RANDO_ENEMY_GROWTH, 1);
        SET(RFO_RANDO_ENEMY_GROWTH_AMT, 100);
        SET(RFO_RANDO_BOSS_BUFF, 1);
        SET(RFO_RANDO_BOSS_BUFF_AMT, 20);
        SET(RFO_RANDO_ENEMY_WEAPONS, 100);
        SET(RFO_RANDO_DROPS, 100);
        return 1;
    case 11: /* weapons, rewards, shops */
        SET(RFO_RANDO_W_MIGHT, 1);
        SET(RFO_RANDO_W_HIT, 1);
        SET(RFO_RANDO_W_WEIGHT, 1);
        SET(RFO_RANDO_W_USES, 1);
        SET(RFO_RANDO_W_EFFECTS, 100);
        SET(RFO_RANDO_REWARDS, 1);
        SET(RFO_RANDO_SHOPS, 1);
        return 1;
    case 12: /* movement, constitution, affinity, the scaling enemy buff */
        SET(RFO_RANDO_MOV, 1);
        SET(RFO_RANDO_CON, 1);
        SET(RFO_RANDO_AFFINITY, 1);
        SET(RFO_RANDO_ENEMY_GROWTH, 2);
        SET(RFO_RANDO_ENEMY_GROWTH_AMT, 50);
        return 1;
    case 13: /* on, every feature off: the tables must stay vanilla */
        return 1;
    case 14: /* recruitment alone (vanilla classes: trainees, the dancer, thieves) */
        SET(RFO_RANDO_RECRUIT, 1);
        SET(RFO_RANDO_THIEVES, 1);
        SET(RFO_RANDO_SPECIAL, 1);
        return 1;
    case 15: /* recruitment, the slot's class and stats, cross-gender, with growths */
        SET(RFO_RANDO_RECRUIT, 1);
        SET(RFO_RANDO_RECRUIT_CLASS, 1);
        SET(RFO_RANDO_RECRUIT_STATS, 1);
        SET(RFO_RANDO_RECRUIT_GENDER, 1);
        SET(RFO_RANDO_GROWTHS, 3);
        return 1;
    case 16: /* recruitment with player classes, autolevel, own class */
        SET(RFO_RANDO_RECRUIT, 1);
        SET(RFO_RANDO_CLASSES, 1);
        SET(RFO_RANDO_BASES, 1);
        return 1;
    }
    return 0;
#undef SET
}

#define TEST_SEEDS 100

static struct CharacterData sTestChars[RANDO_CHARS];
static struct ClassData sTestClasses[RANDO_CLASSES];
static struct ItemData sTestItems[RANDO_ITEMS];
static struct CharacterData sTestChars2[RANDO_CHARS]; /* a second set (versions 15-18 against 11-14) */
static struct ClassData sTestClasses2[RANDO_CLASSES];
static struct ItemData sTestItems2[RANDO_ITEMS];

static int TablesEqual(const struct CharacterData *c, const struct ClassData *k, const struct ItemData *it)
{
    return memcmp(gCharacterData, c, sizeof(sTestChars)) == 0 && memcmp(gClassData, k, sizeof(sTestClasses)) == 0 &&
           memcmp(gItemData, it, sizeof(sTestItems)) == 0;
}

/* Versions 11-14 change the characters only where they must (reforged_rando_classes.c RandoClasses_ApplyForeign):
   the twins' prf locks, and the entry of a player whose foreign roll fired. The saved characters chars (a run of
   another version) with those changes taken from the live run, then TablesEqual; *rolled: the players taken. */
static int TablesEqualThree(struct CharacterData *chars, const struct ClassData *k, const struct ItemData *it, int *rolled)
{
    int pid;

    *rolled = 0;
    chars[CHARACTER_EIRIKA - 1].attributes |= CA_LOCK_4;
    chars[CHARACTER_EPHRAIM - 1].attributes |= CA_LOCK_5;
    for (pid = 1; pid <= RANDO_CHARS; pid++)
        if (RandoClasses_PlayerFe8Class(pid) != 0)
        {
            if (chars[pid - 1].defaultClass != RandoClasses_PlayerFe8Class(pid))
                return 0;
            chars[pid - 1] = gCharacterData[pid - 1];
            (*rolled)++;
        }
    return TablesEqual(chars, k, it);
}

static int SelfTest(void)
{
    struct RandoConfig cfg, other;
    char what[64];
    int kind, s, fails = 0, configs = 0, f, allRuns = 0;
    u32 x = 0x0BADC0DEu;

    sQuiet = 1;
    RandoRecruit_SetQuiet(1);
    RandoClasses_SetQuiet(1);
    sCheckLogged = 0;
    Rando_Start(NULL);
    fails += CheckTables(NULL, "vanilla");
    for (s = 0; s < TEST_SEEDS; s++)
    {
        u32 seed = s == 0 ? 1u : s == 1 ? 0xFFFFFFFFu : SplitMix32(&x);

        if (seed == 0)
            seed = 0x5EED;
        for (kind = 0; TestConfig(kind, seed, &cfg); kind++)
        {
            snprintf(what, sizeof(what), "seed 0x%08X config %d", (unsigned)seed, kind);
            configs++;
            f = 0;
            Rando_Start(&cfg);
            if (!gRandoRunOn)
                f += CheckFail("%s: the run did not start", what);
            f += CheckTables(&cfg, what);
            f += RandoClasses_SelfTest();
            f += RandoItems_SelfTest();
            f += RandoPalettes_SelfTest();
            f += RandoMusic_SelfTest();
            f += RandoRecruit_SelfTest();
            if (cfg.version >= RANDO_VERSION_THREE && cfg.version < RANDO_VERSION_ALLCOLOURS)
            {
                /* the same config as version 15-18 (11 + r -> 15 + r): the same tables (only the colours differ),
                   and its palettes (built on demand) */
                struct RandoConfig all = cfg;

                all.version = (u8)(cfg.version - RANDO_VERSION_THREE + RANDO_VERSION_ALLCOLOURS);
                memcpy(sTestChars2, gCharacterData, sizeof(sTestChars2));
                memcpy(sTestClasses2, gClassData, sizeof(sTestClasses2));
                memcpy(sTestItems2, gItemData, sizeof(sTestItems2));
                Rando_Start(&all);
                if (!gRandoRunOn || gRandoRun.version != all.version)
                    f += CheckFail("%s: version %d: the run did not start", what, all.version);
                else if (!TablesEqual(sTestChars2, sTestClasses2, sTestItems2))
                    f += CheckFail("%s: version %d: the tables differ from version %d's", what, all.version,
                        cfg.version);
                f += RandoPalettes_SelfTest();
                allRuns++;
                Rando_Start(&cfg);
            }
            if (kind == 13)
            {
                int rolled;

                /* version 11-14's twins' locks aside (TablesEqualThree) */
                memcpy(sTestChars, sVanillaChars, sizeof(sTestChars));
                if (!TablesEqualThree(sTestChars, sVanillaClasses, sVanillaItems, &rolled) || rolled != 0)
                    f += CheckFail("%s: every feature off, but the tables changed", what);
            }
            /* the same run again, after another one: the same tables */
            memcpy(sTestChars, gCharacterData, sizeof(sTestChars));
            memcpy(sTestClasses, gClassData, sizeof(sTestClasses));
            memcpy(sTestItems, gItemData, sizeof(sTestItems));
            TestConfig(kind == 0 ? 1 : 0, seed ^ 0x9E3779B9u, &other);
            Rando_Start(&other);
            Rando_Start(&cfg);
            if (!TablesEqual(sTestChars, sTestClasses, sTestItems))
                f += CheckFail("%s: not deterministic (a second apply differs)", what);
            Rando_Start(NULL);
            if (gRandoRunOn || !TablesEqual(sVanillaChars, sVanillaClasses, sVanillaItems))
                f += CheckFail("%s: the restore left the tables changed", what);
            f += RandoPalettes_SelfTest(); /* no run: the vanilla sets */
            f += RandoRecruit_SelfTest();  /* no run: no fills */
            fails += f;
        }
    }
    /* versions 9 and 10's FE7 extras (RANDO_VERSION_FE7): for a sample of seeds, the tables of a version 8
       run and of the same run as version 9 (no FE7 classes) and 10 (FE7 classes) are the same; the version
       alone decides "FE7 classes" (a version 9 entry that says randomizer_fe7=1 is without them, a version
       10 entry without the key, as an older build writes it back, with them); and the classes' picks
       (RandoClasses_Fe7SelfTest: the units, the promotion rows) are version 8's but where an FE7 roll
       fired in version 10; with the FE7 classes not loaded, all of them */
    {
        static const u8 kinds[3] = { 0, 1, 10 };
        int fired = 0, promoRows = 0, runs = 0, k;

        x = 0x0BADC0DEu;
        for (s = 0; s < 24; s++)
        {
            u32 seed = s == 0 ? 1u : SplitMix32(&x);

            for (k = 0; k < 3; k++)
            {
                struct RandoConfig v8, v9, lost;

                TestConfig(kinds[k], seed, &cfg);
                cfg.version = RANDO_VERSION_FE7;
                cfg.v[RFO_RANDO_FE7 - RFO_RANDO_FIRST] = 1;
                cfg.v[RFO_RANDO_PROMOTIONS - RFO_RANDO_FIRST] = 1;
                v8 = cfg;
                v8.version = RANDO_VERSION_NOFE7; /* the option means nothing there */
                v9 = cfg;
                v9.version = RANDO_VERSION_FE7 - 1; /* randomizer_fe7=1, but version 9: without */
                lost = cfg;
                lost.v[RFO_RANDO_FE7 - RFO_RANDO_FIRST] = 0; /* the key dropped: version 10 still has them */
                snprintf(what, sizeof(what), "FE7 extras: seed 0x%08X config %d", (unsigned)seed, kinds[k]);
                Rando_Start(&v8);
                if (Rando_Fe7Run() || Rando_Opt(RFO_RANDO_FE7))
                    fails += CheckFail("%s: version 8 with randomizer_fe7=1 includes the FE7 classes", what);
                memcpy(sTestChars, gCharacterData, sizeof(sTestChars));
                memcpy(sTestClasses, gClassData, sizeof(sTestClasses));
                memcpy(sTestItems, gItemData, sizeof(sTestItems));
                Rando_Start(&cfg);
                if (!Rando_Fe7Run() || Rando_Opt(RFO_RANDO_FE7) != 1)
                    fails += CheckFail("%s: version 10 does not include the FE7 classes", what);
                if (!TablesEqual(sTestChars, sTestClasses, sTestItems))
                    fails += CheckFail("%s: version 10 (FE7 classes): the tables differ from version 8's", what);
                Rando_Start(&v9);
                if (Rando_Fe7Run() || Rando_Opt(RFO_RANDO_FE7))
                    fails += CheckFail("%s: version 9 with randomizer_fe7=1 includes the FE7 classes", what);
                if (!TablesEqual(sTestChars, sTestClasses, sTestItems))
                    fails += CheckFail("%s: version 9 (no FE7 classes): the tables differ from version 8's", what);
                Rando_Start(&lost);
                if (!Rando_Fe7Run() || Rando_Opt(RFO_RANDO_FE7) != 1)
                    fails += CheckFail("%s: version 10 without its randomizer_fe7 key lost the FE7 classes", what);
                f = RandoClasses_Fe7SelfTest(&cfg, &fired, &promoRows);
                if (f)
                    CheckFail("%s: %d unit or promotion picks wrong (above)", what, f);
                fails += f;
                runs++;
                Rando_Start(NULL);
            }
        }
        PortLog("[rando] self-test: FE7 extras: %d runs as versions 8, 9 and 10 (the version decides FE7 classes): "
                "version 8's tables and picks kept; %d FE7 unit rolls fired, %d promotion rows got the third choice "
                "(FE7 classes %s)\n", runs, fired, promoRows,
            Reforged_Fe7ClassAvailable(REFORGED_FE7_JID_ARCHSAGE) ? "loaded" : "not loaded: none applied");
    }
    /* versions 11-14 (RANDO_VERSION_THREE): for a sample of seeds and configs (Random promotions on and off), the
       tables of the run as version 9 and 10 against 11 and 14: the same but the twins' locks and the players whose
       foreign roll fired (TablesEqualThree); the version decides the ROM set (14 keeps it without its
       randomizer_fe7 key, 11 with randomizer_fe7=1 has none); and the classes' picks (RandoClasses_ThreeSelfTest:
       the units, the players, the promotion rows and lists) */
    {
        static const u8 kinds[5] = { 0, 1, 2, 16, 10 }; /* 16: classes with the dancer kept (her Bard roll) */
        static const char *const sNames[0x10] = { "Nomad", "Nomad F", "Blade Lord", "Blade Lord (Eirika)",
            "Armor Lord", "Armor Lord (Ephraim)", "Archsage", "Dark Druid", "Bard", "Thief F", "Trooper", "Trooper F",
            "King", "Lion Lord", "Master Lord", "Divine Dragon" };
        struct RandoThreeStats st, st18;
        char seen[512];
        int k, rolledAll = 0, len = 0, j;

        memset(&st, 0, sizeof(st));
        memset(&st18, 0, sizeof(st18));
        x = 0x7E1AF00Du;
        for (s = 0; s < 16; s++)
        {
            u32 seed = s == 0 ? 1u : SplitMix32(&x);

            for (k = 0; k < 5; k++)
            {
                struct RandoConfig v9, v10, v11, v14, lost, v15, v18;
                int rolled;

                TestConfig(kinds[k], seed, &cfg);
                cfg.v[RFO_RANDO_FE7 - RFO_RANDO_FIRST] = 1;
                if (kinds[k] == 2)
                    cfg.v[RFO_RANDO_RECRUIT - RFO_RANDO_FIRST] = 1; /* recruitment before the foreign rolls */
                v9 = v10 = v11 = v14 = cfg;
                v9.version = RANDO_VERSION_FE7 - 1;
                v10.version = RANDO_VERSION_FE7;
                v11.version = RANDO_VERSION_THREE;
                v14.version = RANDO_VERSION_THREE + 3;
                lost = v14;
                v15 = v11;
                v15.version = RANDO_VERSION_ALLCOLOURS;
                v18 = v14;
                v18.version = RANDO_VERSION;
                v18.v[RFO_RANDO_FE7 - RFO_RANDO_FIRST] = 0; /* (18 keeps both ROMs without the key too) */
                lost.v[RFO_RANDO_FE7 - RFO_RANDO_FIRST] = 0; /* the key dropped: version 14 keeps both ROMs */
                snprintf(what, sizeof(what), "three choices: seed 0x%08X config %d", (unsigned)seed, kinds[k]);
                Rando_Start(&v9);
                memcpy(sTestChars, gCharacterData, sizeof(sTestChars));
                memcpy(sTestClasses, gClassData, sizeof(sTestClasses));
                memcpy(sTestItems, gItemData, sizeof(sTestItems));
                Rando_Start(&v11);
                if (!Rando_ThreeRun() || Rando_ThreeRoms() != 0 || Rando_Opt(RFO_RANDO_FE7) != 0)
                    fails += CheckFail("%s: version 11 with randomizer_fe7=1 includes a foreign ROM", what);
                if (!TablesEqualThree(sTestChars, sTestClasses, sTestItems, &rolled) || rolled != 0)
                    fails += CheckFail("%s: version 11: the tables differ from version 9's", what);
                f = CheckTables(&v11, what) + RandoClasses_SelfTest() + RandoRecruit_SelfTest() + RandoPalettes_SelfTest();
                memcpy(sTestChars2, gCharacterData, sizeof(sTestChars2));
                memcpy(sTestClasses2, gClassData, sizeof(sTestClasses2));
                memcpy(sTestItems2, gItemData, sizeof(sTestItems2));
                Rando_Start(&v15); /* version 15 makes version 11's picks */
                if (!Rando_ThreeRun() || Rando_ThreeRoms() != 0 || !TablesEqual(sTestChars2, sTestClasses2, sTestItems2))
                    fails += CheckFail("%s: version 15: the tables or ROMs differ from version 11's", what);
                f += RandoPalettes_SelfTest();
                Rando_Start(&v10);
                memcpy(sTestChars, gCharacterData, sizeof(sTestChars));
                Rando_Start(&lost);
                if (Rando_ThreeRoms() != (RANDO_ROM_FE7 | RANDO_ROM_FE6) || Rando_Opt(RFO_RANDO_FE7) != 1)
                    fails += CheckFail("%s: version 14 without its randomizer_fe7 key lost its ROMs", what);
                if (!TablesEqualThree(sTestChars, sTestClasses, sTestItems, &rolled))
                    fails += CheckFail("%s: version 14: the tables differ from version 10's but where a foreign roll fired",
                        what);
                rolledAll += rolled;
                f += CheckTables(&v14, what) + RandoClasses_SelfTest() + RandoRecruit_SelfTest() + RandoPalettes_SelfTest();
                if (f)
                    CheckFail("%s: %d table checks failed (above)", what, f);
                fails += f;
                f = RandoClasses_ThreeSelfTest(&v10, &st);
                if (f)
                    CheckFail("%s: %d unit, player or promotion picks wrong (above)", what, f);
                fails += f;
                /* version 18 makes version 14's picks: the same tables, ROM set and classes' picks */
                Rando_Start(&lost); /* (RandoClasses_ThreeSelfTest starts other runs) */
                memcpy(sTestChars2, gCharacterData, sizeof(sTestChars2));
                memcpy(sTestClasses2, gClassData, sizeof(sTestClasses2));
                memcpy(sTestItems2, gItemData, sizeof(sTestItems2));
                Rando_Start(&v18);
                if (Rando_ThreeRoms() != (RANDO_ROM_FE7 | RANDO_ROM_FE6) || !TablesEqual(sTestChars2, sTestClasses2, sTestItems2))
                    fails += CheckFail("%s: version 18: the tables or ROMs differ from version 14's", what);
                f = RandoPalettes_SelfTest() + RandoClasses_ThreeSelfTestAt(&v10, &st18,
                    RANDO_VERSION_ALLCOLOURS - RANDO_VERSION_THREE);
                if (f)
                    CheckFail("%s: version 18: %d picks or palettes wrong (above)", what, f);
                fails += f;
                allRuns++;
                Rando_Start(NULL);
            }
        }
        seen[0] = '\0';
        for (j = 0; j < 0x10; j++)
            if (st.byClass[j] > 0 && len < (int)sizeof(seen) - 40)
                len += snprintf(seen + len, sizeof(seen) - len, "%s%s %d", len ? ", " : "", sNames[j], st.byClass[j]);
        PortLog("[rando] self-test: three choices: %d runs as versions 9-14: version 10's tables and picks kept but "
                "where a foreign roll fired; %d player rolls (%d in the tables), %d unit rolls, %d boss rolls fired; %d "
                "promotion lists of three (fills: %d foreign, %d random, %d FE8), %d version 9/10 lists under three; "
                "foreign classes seen: %s (FE7 classes %s, FE6 classes %s)\n",
            st.runs, st.players, rolledAll, st.units, st.bosses, st.lists, st.promoForeign, st.promoRandom, st.promoFill,
            st.oldShort, seen[0] ? seen : "none", (Rando_RomsLoaded() & RANDO_ROM_FE7) ? "loaded" : "not loaded",
            (Rando_RomsLoaded() & RANDO_ROM_FE6) ? "loaded" : "not loaded");
        PortLog("[rando] self-test: stand-ins: %d player rolls save as their class of the run; %d unit and boss rolls "
                "as their class of the run, %d as a class of its tier and their weapons; %d foreign promotion choices "
                "as a class of the row's tier, the unit's sex and their weapons\n", st.standPlayers, st.standUnitsSame,
            st.standUnitsOther, st.standPromo);
    }
    /* a run of another algorithm version is refused, and carried until the next start */
    TestConfig(0, 1, &cfg);
    cfg.version = RANDO_VERSION + 1;
    Rando_Start(&cfg);
    if (gRandoRunOn || !TablesEqual(sVanillaChars, sVanillaClasses, sVanillaItems))
        fails += CheckFail("version %d: applied, should be refused", cfg.version);
    if (!gRandoCarriedOn || memcmp(&gRandoCarried, &cfg, sizeof(cfg)) != 0)
        fails += CheckFail("version %d: refused but not carried", cfg.version);
    Rando_Start(NULL);
    if (gRandoCarriedOn)
        fails += CheckFail("version %d: still carried after another start", cfg.version);
    /* a version 1 run (v1.1) is still applied, with the same tables as version 2's
       (the hash mixes in 1 for both; only the map check of the units differs) */
    {
        struct RandoConfig v1;

        TestConfig(0, 1, &cfg);
        cfg.version = RANDO_VERSION_FE7; /* (versions 11-14 add their changes: their own test below) */
        v1 = cfg;
        v1.version = 1;
        Rando_Start(&cfg);
        memcpy(sTestChars, gCharacterData, sizeof(sTestChars));
        memcpy(sTestClasses, gClassData, sizeof(sTestClasses));
        memcpy(sTestItems, gItemData, sizeof(sTestItems));
        Rando_Start(&v1);
        if (!gRandoRunOn || gRandoCarriedOn || gRandoRun.version != 1)
            fails += CheckFail("version 1: not applied");
        else if (!TablesEqual(sTestChars, sTestClasses, sTestItems))
            fails += CheckFail("version 1: its tables differ from version %d's", RANDO_VERSION);
        Rando_Start(NULL);
    }
    /* a soft reset boots again with a randomized run's tables: the boot puts the
       vanilla ones back and ends the run, and never takes them as the snapshot */
    configs++;
    memcpy(sTestChars, sVanillaChars, sizeof(sTestChars));
    memcpy(sTestClasses, sVanillaClasses, sizeof(sTestClasses));
    memcpy(sTestItems, sVanillaItems, sizeof(sTestItems));
    TestConfig(0, 0x5EED, &cfg);
    Rando_Start(&cfg);
    if (TablesEqual(sVanillaChars, sVanillaClasses, sVanillaItems))
        fails += CheckFail("boot again: the test run left the tables vanilla (nothing to check)");
    other = cfg;
    other.version = RANDO_VERSION + 1;
    Reforged_RandoBoot();
    if (gRandoRunOn || gRandoCarriedOn || !TablesEqual(sVanillaChars, sVanillaClasses, sVanillaItems))
        fails += CheckFail("boot again: the run or the randomized tables were kept");
    if (memcmp(sVanillaChars, sTestChars, sizeof(sTestChars)) != 0 ||
        memcmp(sVanillaClasses, sTestClasses, sizeof(sTestClasses)) != 0 ||
        memcmp(sVanillaItems, sTestItems, sizeof(sTestItems)) != 0)
        fails += CheckFail("boot again: the snapshot was taken again (from the randomized tables)");
    Rando_Start(&other); /* a carried run, then a boot again */
    Reforged_RandoBoot();
    if (gRandoCarriedOn)
        fails += CheckFail("boot again: the carried run was kept");
    Rando_Start(NULL);
    sQuiet = 0;
    RandoRecruit_SetQuiet(0);
    RandoClasses_SetQuiet(0);
    fails += RandoPalettes_AllSelfTest(); /* version 15: every character on every class */
    PortLog("[rando] self-test: versions 15-18: %d runs with version 11-14's tables (and picks)\n", allRuns);
    PortLog("[rando] self-test: %d configs, %d failures\n", configs, fails);
    return fails;
}

/* FE8PC_RANDO_SCAN=first[:count] (a test switch, the first boot only; for finding seeds to test with): for each seed
   from first (count of them, default 100), the Randomizer screen's settings with that seed and the randomizer on, as
   New Game would record them now (the ROMs loaded), applied and logged: "[rando] scan: seed S vV: ..." with each
   player whose foreign roll fired (pid:class before->foreign class) and, for each class in FE8PC_RANDO_SCAN_ROWS
   (comma-separated ids), its three promotion choices for a man and a woman (row/M=a,b,c), and for each "chapter:pid"
   in FE8PC_RANDO_SCAN_UNITS the class that unit gets as a red definition there (uPID@CH=class; no map: the map checks
   pass). The vanilla tables are put
   back after; the settings file is not changed. */
static void Scan(const char *spec)
{
    const char *rows = getenv("FE8PC_RANDO_SCAN_ROWS"), *units = getenv("FE8PC_RANDO_SCAN_UNITS");
    char *end;
    int savedCh = gPlaySt.chapterIndex;
    unsigned long first = strtoul(spec, &end, 0), count = *end == ':' ? strtoul(end + 1, NULL, 0) : 100, n;

    sQuiet = 1;
    RandoClasses_SetQuiet(1);
    RandoRecruit_SetQuiet(1);
    for (n = 0; n < count; n++)
    {
        struct RandoConfig cfg;
        char line[1024];
        const char *r;
        int len = 0, pid;

        Rando_ConfigFromSettings(&cfg);
        cfg.seed = (u32)(first + n);
        cfg.v[RFO_RANDO_ENABLE - RFO_RANDO_FIRST] = 1;
        if (cfg.version == RANDO_VERSION)
        {
            int roms = Rando_RomsLoaded();

            cfg.version = (u8)Rando_VersionForRoms(roms);
            cfg.v[RFO_RANDO_FE7 - RFO_RANDO_FIRST] = (u8)(roms != 0);
        }
        Rando_Start(&cfg);
        line[0] = '\0';
        for (pid = 1; pid <= 0xFF && len < (int)sizeof(line) - 40; pid++)
            if (RandoClasses_PlayerFe8Class(pid) != 0)
                len += snprintf(line + len, sizeof(line) - len, " %02X:%02X->%02X", pid, RandoClasses_PlayerFe8Class(pid),
                    RandoClasses_PlayerClass(pid));
        for (r = rows; r != NULL && *r != '\0' && len < (int)sizeof(line) - 60;)
        {
            int jid = (int)strtol(r, &end, 0), f;

            if (end == r)
                break;
            for (f = 0; f < 2; f++)
            {
                u8 l[3] = { 0, 0, 0 };

                Rando_PromoList(jid, f, l);
                len += snprintf(line + len, sizeof(line) - len, " %02X/%c=%02X,%02X,%02X", jid, f ? 'F' : 'M', l[0], l[1],
                    l[2]);
            }
            r = *end == ',' ? end + 1 : end;
        }
        for (r = units; r != NULL && *r != '\0' && len < (int)sizeof(line) - 30;)
        {
            /* "ch:pid": that boss (or unit) as a chapter's definition gives it, red, its default class */
            struct UnitDefinition def, buf;
            const struct UnitDefinition *got;
            int ch = (int)strtol(r, &end, 0), upid;

            if (end == r || *end != ':')
                break;
            upid = (int)strtol(end + 1, &end, 0);
            memset(&def, 0, sizeof(def));
            def.charIndex = (u8)upid;
            def.allegiance = FACTION_ID_RED;
            def.level = 15;
            def.xPosition = 2;
            def.yPosition = 3;
            gPlaySt.chapterIndex = (u8)ch;
            got = Reforged_RandoUnitDef(&def, &buf);
            len += snprintf(line + len, sizeof(line) - len, " u%02X@%d=%02X", upid, ch,
                got != NULL && got->classIndex ? got->classIndex : (Rando_VanillaChar(upid) ? Rando_VanillaChar(upid)->defaultClass : 0));
            r = *end == ',' ? end + 1 : end;
        }
        PortLog("[rando] scan: seed 0x%08X v%d:%s\n", (unsigned)cfg.seed, cfg.version, line);
    }
    Rando_Start(NULL);
    gPlaySt.chapterIndex = (u8)savedCh;
    sQuiet = 0;
    RandoClasses_SetQuiet(0);
    RandoRecruit_SetQuiet(0);
}

void Reforged_RandoBoot(void)
{
    const char *check;

    if (sBooted)
    {
        /* a soft reset: the live tables are the run's; never take them as vanilla */
        Rando_Start(NULL);
        PortLog("[rando] boot again (soft reset): vanilla tables put back, no run\n");
        return;
    }
    sBooted = 1;
    sSnapshotTaken = 0; /* the first boot: the tables as the game starts */
    TakeSnapshot();
    gRandoRunOn = 0;
    memset(&gRandoRun, 0, sizeof(gRandoRun));
    gRandoCarriedOn = 0;
    memset(&gRandoCarried, 0, sizeof(gRandoCarried));
    PortLog("[rando] vanilla tables kept (%d characters, %d classes, %d items), algorithm version %d\n",
        RANDO_CHARS, RANDO_CLASSES, RANDO_ITEMS, RANDO_VERSION);
    RandoPalettes_Dump();
    check = getenv("FE8PC_RANDO_CHECK"); /* the first boot only */
    if (check != NULL)
    {
        int fails = SelfTest();

        if (strcmp(check, "exit") == 0)
        {
            fflush(stdout);
            PortExit(fails ? 1 : 0);
        }
    }
    check = getenv("FE8PC_PROMO3_CHECK"); /* v1.23.0, "Third promotion": the first boot only */
    if (check != NULL)
    {
        int fails = RandoClasses_Promo3SelfTest();

        if (strcmp(check, "exit") == 0)
        {
            fflush(stdout);
            PortExit(fails ? 1 : 0);
        }
    }
    check = getenv("FE8PC_RANDO_SCAN");
    if (check != NULL)
        Scan(check);
}

#endif /* PORT_PLAYED_BUILD */
