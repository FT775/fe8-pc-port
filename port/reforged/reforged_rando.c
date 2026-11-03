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
    cfg->version = RANDO_VERSION;
    for (i = 0; i < RFO_RANDO_COUNT; i++)
        cfg->v[i] = (u8)Reforged_GetOption(RFO_RANDO_FIRST + i);
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
    h = Fmix32(h + 0x9E3779B9u + gRandoRun.version);
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
        if (cfg->version != RANDO_VERSION)
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
        }
    }

    /* Every step runs at every start: with no run each changes nothing but clears its
       own per-run state (the classes' player-class map and unit key). The facts the
       classes and items files cache come from the snapshot, which never changes. */
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

    if (gRandoRunOn && !sQuiet)
    {
        DescribeConfig(cfg, desc, sizeof(desc));
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

void Reforged_RandoNewGame(void)
{
    struct RandoConfig cfg;
    int on;

    Reforged_GameFixesNewGame(); /* the Game fixes screen's choices: also fixed for this run */
    if (ForcedConfig(&cfg, "new game"))
    {
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
        cfg = &forced; /* the save mark ignored */
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
        if (c->defaultClass < 1 || c->defaultClass > 0x7F || (k = Rando_Class(c->defaultClass)) == NULL ||
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
    cfg->version = RANDO_VERSION;
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

static int TablesEqual(const struct CharacterData *c, const struct ClassData *k, const struct ItemData *it)
{
    return memcmp(gCharacterData, c, sizeof(sTestChars)) == 0 && memcmp(gClassData, k, sizeof(sTestClasses)) == 0 &&
           memcmp(gItemData, it, sizeof(sTestItems)) == 0;
}

static int SelfTest(void)
{
    struct RandoConfig cfg, other;
    char what[64];
    int kind, s, fails = 0, configs = 0, f;
    u32 x = 0x0BADC0DEu;

    sQuiet = 1;
    RandoRecruit_SetQuiet(1);
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
            f += RandoRecruit_SelfTest();
            if (kind == 13 && !TablesEqual(sVanillaChars, sVanillaClasses, sVanillaItems))
                f += CheckFail("%s: every feature off, but the tables changed", what);
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
    PortLog("[rando] self-test: %d configs, %d failures\n", configs, fails);
    return fails;
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
}

#endif /* PORT_PLAYED_BUILD */
