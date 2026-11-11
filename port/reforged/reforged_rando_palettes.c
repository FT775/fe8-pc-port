#include "global.h"

/*
 * Reforged Options randomizer: battle palettes, "Keep colours" (RFO_RANDO_PALETTES;
 * port/reforged/reforged_rando.h has the map of the randomizer's files). Played
 * builds only; empty in every test build.
 *
 * A character's own battle colours are a palette set per (character, class):
 * gAnimCharaPalConfig[pid - 1][slot] names the class, gAnimCharaPalIt[pid - 1][slot]
 * the set (an index + 1 in character_battle_animation_palette_table). A character in
 * a class it has no set for shows the class's generic colours. After a class change
 * (player classes, or a recruit's new slot), this file gives the character a set for
 * each class of its new line (the class, its promotions, or the class it promotes
 * from), built from the character's own vanilla set:
 *
 * - A class the character already has a vanilla set for keeps that set.
 * - Otherwise the set is the new class's generic one with the character's colours
 *   moved onto it. A class palette is 16 colours in ramps (light to dark runs of
 *   2-4); the characters' vanilla sets change some ramps of their class's generic
 *   palette. Which ones is learned from the data, per class, at the first build:
 *   "team" colours (a character's player and enemy palettes differ there: the
 *   clothes that turn red on the enemy side) and "personal" ones (changed from the
 *   generic palette but the same on both sides: hair, and the outfit's own
 *   colours). A class with no vanilla sets borrows its M/F twin's roles when both
 *   generic palettes have the same team colours; a class with neither (monsters,
 *   the Soldier, a few female classes) keeps its generic colours.
 * - Each of the new class's ramps of a kind takes the character's ramp of the same
 *   kind whose part looks most alike in the two classes' generic palettes (the
 *   generic sprites colour the same parts alike: hair with hair, armour with
 *   armour), light to dark onto light to dark in the character's colours. The
 *   same index map is applied to all five palettes of the set (player, enemy,
 *   NPC, the fourth and the "independent" one), so the enemy version of a
 *   character keeps its enemy-side colours.
 *
 * The built sets live here (raw, and as an uncompressed LZ77 stream, which is what
 * the game's readers decompress); gAnimCharaPalIt points at them with indices
 * 0x81 and up (the vanilla table has 108). Every read of a set by its index goes
 * through Reforged_RandoCharaPal (include/bmunit.h RANDO_CHARA_PAL, in
 * src/banim-ekrmain.c and src/banim-ekrmainmini.c). gAnimCharaPalConfig and
 * gAnimCharaPalIt are put back from their vanilla copy at every start
 * (RandoPalettes_Apply), like the other tables.
 *
 * Monsters (randomizer version 7, the user's request of 2026-11-04): no character has a
 * set for a monster class, so there are no roles to learn; the monster's battle colours
 * are the same on both sides. Instead its body (the ramp most of its sprite is drawn in,
 * counted from the sheets in graphics/banim: Mogall's purple, the skeletons' bones, the
 * dogs' fur; sMonsterBody) takes the character's signature colour, the most saturated
 * of its personal ramps (mostly its hair): each body colour keeps its own lightness and
 * takes that hue and saturation, so the shading stays. Eyes, teeth, weapons and the
 * outline keep the monster's own colours.
 *
 * Eirika and Ephraim have no set of their own (the lord classes' generic palettes
 * are theirs): randomized, they get the new class's generic colours (versions 1-14).
 *
 * Every class (randomizer version 15, RANDO_VERSION_ALLCOLOURS, the owner's request of
 * 2026-11-09: "make all classes get custom palettes per character's color combinations"):
 * the apply builds nothing; a playable slot filled by another character takes that
 * character's vanilla rows, and every other set is built when a battle first shows it
 * (Reforged_RandoCharaEnsure, through include/bmunit.h RANDO_CHARA_ENSURE before each
 * lookup: the battle intro, a triangle attack's partners, the promotion screen) into a
 * 126-slot cache (CacheSet: the slot used longest ago is reused and its row entries
 * dropped; a fight shows at most four sets, so the u8 index 0x81-0xFE never overflows,
 * whatever the number of characters and classes). Each class, FE8's 0x01-0x7F and the
 * loaded foreign 0x80-0x8F (reforged_fe7.c; their generic palette is their first
 * animation's, the faction rows as the loader uses them), has roles (BuildAll):
 * - learned as above (and the twins' borrowing), unchanged;
 * - a monster (sMonsterBody, now with the manakete dragon class 0x3B and FE6's Divine Dragon):
 *   the version 7 body rule, with a signature for everyone (SignatureLoose);
 * - else derived (DeriveRoles), justified by how FE8's own data is laid out: team =
 *   the colours the class's faction rows paint by FE8's scheme (blue player, red enemy,
 *   green NPC: what the characters' sets learn as team); personal = the other colours its
 *   rows change (the generic rows recolour a generic unit's hair per faction, and the
 *   characters' sets give that part their own colour), the hair of a class drawn as one
 *   character (sHair: lords, King, Thief F, Bard, Recruit, Pupil, the twins' lords, whose
 *   rows keep that character's hair), and each other coloured, non-skin ramp that looks
 *   more like a personal ramp than a fixed one of the nearest FE8 reference class
 *   (sRefs: the one of a short list with the nearest generic palette, PalDist). Ramps of
 *   a derived palette are also cut where the hue jumps (sHueSplit), so skin and hair or
 *   gold and cloth are never one ramp.
 * The set is BuildSet's mapping onto those roles; the hair of an sHair class takes the
 * character's hair (its most colourful own ramp, where its class's generic hair is when
 * it can). Eirika and Ephraim bring their lord class's generic palette with its hair as
 * their personal colours; the class's faction colours stay (unless it has nothing else),
 * and their own lord classes keep their look, as Myrrh's class does for Myrrh (the
 * "Myrrh look" swaps it). A class drawn with the same palette as one the character has a
 * set for uses that set (the trainees' two forms). A foreign class fighting with a
 * borrowed FE8 animation (Armor Lord's lance) shows the character's colours of that FE8
 * class. Bosses and NPCs with a vanilla set get theirs; generic units have none.
 * Versions 1-14 never reach any of this (AllOn), so their sets stay byte for byte.
 * Test switches: FE8PC_TEST_KEEP_COLOURS=1 (version 15 in any game), FE8PC_TEST_PAL_SRC=a[:b]
 * (player units show character a's colours, the others b's).
 *
 * FE8PC_RANDO_PALDUMP=<file> (a test switch): at the first boot, every class's
 * generic palette and every vanilla character set, as hex, for offline checks.
 */

#if PORT_PLAYED_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmunit.h"
#include "ekrbattle.h"
#include "banim_data.h"
#include "variables.h"
#include "constants/characters.h"
#include "constants/classes.h"

#include "port_hw.h"
#include "port_reforged.h"
#include "reforged_rando.h"

#define JID_MAX      0x7F
#define CLASS_SLOTS  (REFORGED_FOREIGN_LAST_JID + 1) /* FE8's 0x01-0x7F and the foreign 0x80-0x8F */
#define BANIM_COUNT  201 /* src/banim_data.c */
#define BANIM_SLOTS  0xF8  /* banim_data grown for the foreign animations (src/banim_data.c, reforged_fe7.c: 0xD0-0xF7) */
#define FOREIGN_POOL 0xD0  /* the first foreign animation slot (reforged_fe7.c POOL_FIRST) */
#define CBAPT_COUNT  108 /* src/banim_pal_chara.c */
#define SYNTH_BASE   0x80
#define SYNTH_MAX    126 /* gAnimCharaPalIt is a u8: SYNTH_BASE + k + 1 <= 0xFF */
#define PALS         5   /* a character set: player, enemy, NPC, 4th, independent */
#define SET_BYTES    (PALS * 32)
#define GEN_BYTES    (4 * 32)
#define LZ_BYTES     (4 + (SET_BYTES / 8) * 9)

typedef char SynthFitsAByte[SYNTH_BASE + SYNTH_MAX + 1 <= 0xFF ? 1 : -1];

struct SynthSet {
    u8 raw[SET_BYTES];
    u8 lz[LZ_BYTES];
    u8 monster; /* a version 7 monster set: only for its monster's own sprites */
    u8 fixed;   /* built by the apply (versions 1-14): never reused */
    /* version 15 (built when a battle needs it, AllColours): whose colours (pid), on which class's palette
       (jid; 0: a free slot), and when last used (the slot reused is the least recently used) */
    u8 src, jid;
    u32 tick;
};

static u8 sGen[CLASS_SLOTS][GEN_BYTES]; /* each class's generic palettes */
static u16 sPersonal[CLASS_SLOTS];      /* index masks (bit i: colour i) */
static u16 sTeam[CLASS_SLOTS];
static u8 sHasGen[CLASS_SLOTS];
static u8 sHasRoles[CLASS_SLOTS];
static int sInfoBuilt;

/* version 15 (AllColours): every class's roles, the learned ones or derived (DeriveRoles) */
enum { ROLES_NONE, ROLES_LEARNED, ROLES_TWIN, ROLES_DERIVED, ROLES_MONSTER };
static u16 sAPersonal[CLASS_SLOTS], sATeam[CLASS_SLOTS];
static u16 sAHair[CLASS_SLOTS]; /* the ramp Eirika's and Ephraim's hair goes on (TwinHairMask) */
static u8 sAKind[CLASS_SLOTS], sARef[CLASS_SLOTS];
/* the self-test's view of the last BuildAllSet (role sets): the mapping and the colours it took */
static s8 sLastMap[16];
static u8 sLastSet[SET_BYTES];
static int sLastRoles;
/* the row self-test (EnsureSelfTest): version 15 on, and the colour source of every unit */
static int sTestAllOn, sTestSrc;
static int sAllBuilt;
static u32 sTick;
static u8 sBuildFailed[0x100][(CLASS_SLOTS + 7) / 8]; /* (src, jid) found to give nothing: not tried again */

static u8 sVanillaConfig[0x100][7];
static u8 sVanillaIt[0x100][7];
static int sVanillaKept;

static struct SynthSet sSynth[SYNTH_MAX];
static int sSynthCount;

/* ------------------------------------------------------------------ data access */

static u32 Load32le(const void *p)
{
    const u8 *b = p;

    return (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
}

/* Decompresses an LZ77 stream into dst when it is one and fits; its size, or 0. */
static int Unpack(const void *src, u8 *dst, int max)
{
    u32 size;

    if (src == NULL || ((const u8 *)src)[0] != 0x10)
        return 0;
    size = Load32le(src) >> 8;
    if (size == 0 || size > (u32)max)
        return 0;
    LZ77UnCompWram(src, dst);
    return (int)size;
}

static u16 Color(const u8 *pal, int i)
{
    return (u16)(pal[2 * i] | (pal[2 * i + 1] << 8));
}

static void SetColor(u8 *pal, int i, u16 c)
{
    pal[2 * i] = (u8)c;
    pal[2 * i + 1] = (u8)(c >> 8);
}

static int Luma(u16 c)
{
    return 299 * (c & 31) + 587 * ((c >> 5) & 31) + 114 * ((c >> 10) & 31);
}

static int Dist(u16 a, u16 b)
{
    int d = 0, s;

    for (s = 0; s < 15; s += 5)
    {
        int x = (int)((a >> s) & 31) - (int)((b >> s) & 31);

        d += x < 0 ? -x : x;
    }
    return d;
}

/* The banim whose palette a class's units use (src/banim-ekrmain.c GetBanimPalette:
   the archers take another one), from its first animation; -1 when none. */
static int ClassPalBanim(int jid)
{
    const struct ClassData *k;
    int anim;

    if (jid > JID_MAX)
    {
        /* a foreign class (reforged_fe7.c), when loaded: its first animation, in the foreign pool */
        if (jid >= CLASS_SLOTS || !Reforged_Fe7ClassAvailable(jid) || (k = GetClassData(jid)) == NULL ||
            k->pBattleAnimDef == NULL || k->pBattleAnimDef[0].wtype == 0)
            return -1;
        anim = k->pBattleAnimDef[0].index - 1;
        return anim >= 0 && anim < BANIM_SLOTS && banim_data[anim].pal != NULL ? anim : -1;
    }
    k = Rando_VanillaClass(jid);
    if (k == NULL || k->pBattleAnimDef == NULL || k->pBattleAnimDef[0].wtype == 0)
        return -1;
    switch (jid)
    {
    case CLASS_ARCHER:   return 0x25;
    case CLASS_ARCHER_F: return 0x27;
    case CLASS_SNIPER:   return 0x29;
    case CLASS_SNIPER_F: return 0x2B;
    }
    anim = k->pBattleAnimDef[0].index - 1;
    return anim >= 0 && anim < BANIM_COUNT ? anim : -1;
}

static void KeepVanilla(void)
{
    if (sVanillaKept)
        return;
    memcpy(sVanillaConfig, gAnimCharaPalConfig, sizeof(sVanillaConfig));
    memcpy(sVanillaIt, gAnimCharaPalIt, sizeof(sVanillaIt));
    sVanillaKept = 1;
}

/* A vanilla set's colours (5 palettes; a shorter set repeats its first palette). */
static int VanillaSet(int idx, u8 out[SET_BYTES])
{
    static u8 buf[0x400];
    int n, p;

    if (idx < 0 || idx >= CBAPT_COUNT)
        return 0;
    n = Unpack(character_battle_animation_palette_table[idx].pal, buf, sizeof(buf));
    if (n < 64)
        return 0;
    for (p = 0; p < PALS; p++)
        memcpy(out + p * 32, buf + (p * 32 + 32 <= n ? p * 32 : 0), 32);
    return 1;
}

/* A character set's personal and team masks against its class's generic palette. */
static void SetMasks(const u8 *set, const u8 *gen, u16 *personal, u16 *team)
{
    int i;

    *personal = *team = 0;
    for (i = 1; i < 16; i++)
    {
        int changed = Color(set, i) != Color(gen, i);
        int teamed = Color(set, i) != Color(set + 32, i);

        if (teamed)
            *team |= (u16)(1 << i);
        else if (changed)
            *personal |= (u16)(1 << i);
    }
}

static u16 GenTeamMask(int jid)
{
    u16 m = 0;
    int i;

    for (i = 1; i < 16; i++)
        if (Color(sGen[jid], i) != Color(sGen[jid] + 32, i))
            m |= (u16)(1 << i);
    return m;
}

static void BuildInfo(void)
{
    static u8 buf[GEN_BYTES + 0x100];
    u8 set[SET_BYTES];
    int jid, pid, s;

    if (sInfoBuilt)
        return;
    KeepVanilla();
    memset(sHasGen, 0, sizeof(sHasGen));
    memset(sHasRoles, 0, sizeof(sHasRoles));
    memset(sPersonal, 0, sizeof(sPersonal));
    memset(sTeam, 0, sizeof(sTeam));
    for (jid = 1; jid <= JID_MAX; jid++)
    {
        int b = ClassPalBanim(jid);

        if (b >= 0 && Unpack(banim_data[b].pal, buf, sizeof(buf)) >= GEN_BYTES)
        {
            memcpy(sGen[jid], buf, GEN_BYTES);
            sHasGen[jid] = 1;
        }
    }
    for (pid = 1; pid <= 0x100; pid++)
        for (s = 0; s < 7; s++)
        {
            int j = sVanillaConfig[pid - 1][s];
            u16 per, team;

            if (j < 1 || j > JID_MAX || !sHasGen[j] || sVanillaIt[pid - 1][s] == 0 ||
                !VanillaSet(sVanillaIt[pid - 1][s] - 1, set))
                continue;
            SetMasks(set, sGen[j], &per, &team);
            sPersonal[j] |= per;
            sTeam[j] |= team;
        }
    for (jid = 1; jid <= JID_MAX; jid++)
    {
        sPersonal[jid] &= (u16)~sTeam[jid];
        sHasRoles[jid] = (sPersonal[jid] | sTeam[jid]) != 0;
    }
    for (jid = 1; jid <= JID_MAX; jid++)
    {
        int twin = Rando_ClassTwin(jid);

        if (sHasRoles[jid] || !sHasGen[jid] || twin == 0 || !sHasRoles[twin] || !sHasGen[twin] ||
            GenTeamMask(jid) != GenTeamMask(twin))
            continue;
        sPersonal[jid] = sPersonal[twin];
        sTeam[jid] = sTeam[twin];
        sHasRoles[jid] = 2; /* borrowed */
    }
    sInfoBuilt = 1;
}

/* ------------------------------------------------------------------ ramps */

struct Ramp {
    u8 idx[16];
    int n;
};

/* The indices of mask as ramps: runs of neighbouring indices, a run longer than 4
   cut where two neighbours differ most (ref: the palette the indices are in). */
static int SplitRun(const u8 *idx, int n, const u8 *ref, struct Ramp *out, int count, int max)
{
    int i, cut = 1, best = -1;

    if (count >= max)
        return count;
    if (n <= 4)
    {
        memcpy(out[count].idx, idx, n);
        out[count].n = n;
        return count + 1;
    }
    for (i = 1; i < n; i++)
    {
        int d = Dist(Color(ref, idx[i - 1]), Color(ref, idx[i]));

        if (i >= 2 && n - i >= 2 && d > best)
        {
            best = d;
            cut = i;
        }
    }
    count = SplitRun(idx, cut, ref, out, count, max);
    return SplitRun(idx + cut, n - cut, ref, out, count, max);
}

static void ToHsl(u16 c, double *h, double *s, double *l);

/* Version 15, a derived class's palette (sHueSplit): two neighbours belong to different ramps when their
   hues are far apart (skin next to hair, gold next to cloth). Versions 1-14 cut runs by length only. */
static int sHueSplit;

static int HueBreak(const u8 *ref, int a, int b)
{
    double ha, sa, la, hb, sb, lb, dh;

    ToHsl(Color(ref, a), &ha, &sa, &la);
    ToHsl(Color(ref, b), &hb, &sb, &lb);
    dh = ha > hb ? ha - hb : hb - ha;
    if (dh > 3)
        dh = 6 - dh;
    return sa > 0.2 && sb > 0.2 && la > 0.1 && lb > 0.1 && la < 0.92 && lb < 0.92 && dh > 0.75;
}

static int Ramps(u16 mask, const u8 *ref, struct Ramp *out)
{
    u8 run[16];
    int count = 0, n = 0, i, j, k;

    for (i = 1; i <= 16; i++)
    {
        if (i < 16 && (mask & (1 << i)))
        {
            if (sHueSplit && n > 0 && HueBreak(ref, run[n - 1], i))
            {
                count = SplitRun(run, n, ref, out, count, 16);
                n = 0;
            }
            run[n++] = (u8)i;
            continue;
        }
        if (n > 0)
            count = SplitRun(run, n, ref, out, count, 16);
        n = 0;
    }
    /* longest first, then by index (stable) */
    for (i = 1; i < count; i++)
        for (j = i; j > 0 && out[j - 1].n < out[j].n; j--)
        {
            struct Ramp t = out[j];

            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    /* each ramp light to dark */
    for (k = 0; k < count; k++)
        for (i = 1; i < out[k].n; i++)
            for (j = i; j > 0 && Luma(Color(ref, out[k].idx[j - 1])) < Luma(Color(ref, out[k].idx[j])); j--)
            {
                u8 t = out[k].idx[j];

                out[k].idx[j] = out[k].idx[j - 1];
                out[k].idx[j - 1] = t;
            }
    return count;
}

/* A ramp's mean colour in a palette, as chromaticity (r, g, b per 1000 of their sum)
   and brightness. */
static void RampLook(const struct Ramp *rp, const u8 *pal, int look[4])
{
    int i, r = 0, g = 0, b = 0, sum;

    for (i = 0; i < rp->n; i++)
    {
        u16 c = Color(pal, rp->idx[i]);

        r += c & 31;
        g += (c >> 5) & 31;
        b += (c >> 10) & 31;
    }
    sum = r + g + b + 1;
    look[0] = r * 1000 / sum;
    look[1] = g * 1000 / sum;
    look[2] = b * 1000 / sum;
    look[3] = sum * 10 / (rp->n > 0 ? rp->n : 1);
}

static int LookDist(const int a[4], const int b[4])
{
    int d = 0, i;

    for (i = 0; i < 3; i++)
        d += a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
    return d * 2 + (a[3] > b[3] ? a[3] - b[3] : b[3] - a[3]);
}

/* map[t] = the source index whose colour goes to target index t. Each target ramp
   takes the source ramp whose part looks most alike in the two classes' generic
   palettes (hair with hair, armour with armour: generic sprites colour the same
   parts alike), then light to dark onto light to dark in the character's colours. */
static void MapKindEx(u16 srcMask, const u8 *srcSet, const u8 *srcGen, int srcSplit, u16 dstMask, const u8 *dstGen,
    int dstSplit, s8 map[16])
{
    struct Ramp sr[16], tr[16];
    int sn, tn, i, j, r;
    int sl[16][4], tl[4];

    sHueSplit = srcSplit;
    sn = Ramps(srcMask, srcSet, sr);
    sHueSplit = dstSplit;
    tn = Ramps(dstMask, dstGen, tr);
    sHueSplit = 0;

    if (sn == 0)
        return;
    for (j = 0; j < sn; j++)
        RampLook(&sr[j], srcGen, sl[j]);
    for (i = 0; i < tn; i++)
    {
        const struct Ramp *s = &sr[0];
        const struct Ramp *t = &tr[i];
        int best = -1;

        RampLook(t, dstGen, tl);
        for (j = 0; j < sn; j++)
        {
            int d = LookDist(sl[j], tl);

            if (best < 0 || d < best)
            {
                best = d;
                s = &sr[j];
            }
        }

        for (r = 0; r < t->n; r++)
        {
            int at = t->n == 1 ? s->n / 2 : (r * (s->n - 1) + (t->n - 1) / 2) / (t->n - 1);

            map[t->idx[r]] = (s8)s->idx[at];
        }
    }
}

static void MapKind(u16 srcMask, const u8 *srcSet, const u8 *srcGen, u16 dstMask, const u8 *dstGen, s8 map[16])
{
    MapKindEx(srcMask, srcSet, srcGen, 0, dstMask, dstGen, 0, map);
}

/* The character set for vanilla set `srcIdx` (of class srcJid) moved onto class
   dstJid, in out; 0 when nothing maps (then the class's generic colours stay). */
static int BuildSet(int srcIdx, int srcJid, int dstJid, u8 out[SET_BYTES])
{
    u8 set[SET_BYTES];
    s8 map[16];
    u16 per, team;
    int p, t, any = 0;

    if (dstJid < 1 || dstJid > JID_MAX) /* versions 1-14: a foreign class keeps its generic colours (v1.21.0
                                           read past the arrays here, see the CHANGELOG of v1.22.0) */
        return 0;
    if (!sHasGen[srcJid] || !sHasGen[dstJid] || !sHasRoles[dstJid] || !VanillaSet(srcIdx, set))
        return 0;
    SetMasks(set, sGen[srcJid], &per, &team);
    memset(map, -1, sizeof(map));
    MapKind(per ? per : team, set, sGen[srcJid], sPersonal[dstJid], sGen[dstJid], map);
    MapKind(team ? team : per, set, sGen[srcJid], sTeam[dstJid], sGen[dstJid], map);
    for (p = 0; p < PALS; p++)
    {
        memcpy(out + p * 32, sGen[dstJid] + (p < 4 ? p : 0) * 32, 32);
        for (t = 1; t < 16; t++)
            if (map[t] >= 0)
            {
                SetColor(out + p * 32, t, Color(set + p * 32, map[t]));
                any = 1;
            }
    }
    return any;
}

/* ------------------------------------------------------------------ monsters */

/* Each monster banim's body ramp (palette indices), from the pixel counts of its sheets
   (graphics/banim/banim_<abbr>_sheet_*.4bpp): the most used ramp but the outline (15) and
   white (1). Revenants' flesh, the skeletons' bones, the spiders' legs and back, the
   Cyclopes' skin, the Mauthe Doogs' fur, the Gwyllgis' mane, the Tarvos's skin, the
   Mogalls' body, the Gorgons' hair, the gargoyles' stone. */
static const struct { const char *abbr; u16 mask; } sMonsterBody[] = {
    { "zom_at1", 0x003C }, /* 2-5 */
    { "sks_sw1", 0x7800 }, /* 11-14 */
    { "ska_ar1", 0x7800 },
    { "bae_at1", 0x7800 }, /* 11-14 */
    { "cyc_ax1", 0x003C }, /* 2-5 */
    { "mdg_at1", 0x0E00 }, /* 9-11 */
    { "cer_at1", 0x0E00 }, /* 9-11 */
    { "mcd_ax1", 0x7800 }, /* 11-14 */
    { "bgl_mg1", 0x0070 }, /* 4-6 */
    { "gog_mg1", 0x00E0 }, /* 5-7 */
    { "gar_sp1", 0x7800 }, /* 11-14 */
    /* version 15 (v1.22.0; only for its every-class sets): the manaketes' dragon (fifd_mg1: its scales,
       11-14, the most drawn after the outline) and FE6's Divine Dragon (godd_mg1, read from the player's FE6
       ROM: its white-gold body, 2-6; 9-14 are its faction colours) */
    { "fifd_mg1", 0x7800 },
    { "godd_mg1", 0x007C },
};
#define MONSTER_BODY_V7 11 /* versions 7-14 know the first 11 */

static u16 MonsterBodyMaskOf(int jid, int count)
{
    int b = ClassPalBanim(jid), i;

    if (b < 0)
        return 0;
    for (i = 0; i < count; i++)
        if (strncmp(banim_data[b].abbr, sMonsterBody[i].abbr, sizeof(banim_data[b].abbr)) == 0)
            return sMonsterBody[i].mask;
    return 0;
}

static u16 MonsterBodyMask(int jid)
{
    return MonsterBodyMaskOf(jid, MONSTER_BODY_V7);
}

/* version 15: every monster body known */
static u16 MonsterBodyMaskAll(int jid)
{
    return MonsterBodyMaskOf(jid, (int)(sizeof(sMonsterBody) / sizeof(sMonsterBody[0])));
}

/* GBA colour <-> hue (0-6), saturation and lightness (0-1). */
static void ToHsl(u16 c, double *h, double *s, double *l)
{
    double r = (c & 31) / 31.0, g = ((c >> 5) & 31) / 31.0, b = ((c >> 10) & 31) / 31.0;
    double mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    double d = mx - mn;

    *l = (mx + mn) / 2;
    *s = d == 0 ? 0 : d / (1 - (2 * *l - 1 < 0 ? 1 - 2 * *l : 2 * *l - 1));
    if (d == 0)
        *h = 0;
    else if (mx == r)
        *h = (g - b) / d + (g < b ? 6 : 0);
    else if (mx == g)
        *h = (b - r) / d + 2;
    else
        *h = (r - g) / d + 4;
}

static double HueTo(double p, double q, double t)
{
    while (t < 0)
        t += 6;
    while (t >= 6)
        t -= 6;
    if (t < 1)
        return p + (q - p) * t;
    if (t < 3)
        return q;
    if (t < 4)
        return p + (q - p) * (4 - t);
    return p;
}

static u16 FromHsl(double h, double s, double l)
{
    double q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
    int r = (int)(HueTo(p, q, h + 2) * 31 + 0.5), g = (int)(HueTo(p, q, h) * 31 + 0.5),
        b = (int)(HueTo(p, q, h - 2) * 31 + 0.5);

    r = r < 0 ? 0 : r > 31 ? 31 : r;
    g = g < 0 ? 0 : g > 31 ? 31 : g;
    b = b < 0 ? 0 : b > 31 ? 31 : b;
    return (u16)(r | (g << 5) | (b << 10));
}

/* A skin tone: an orange hue, light, not strongly saturated (a character's set often changes
   the skin from the class's, so it is among the personal ramps, but it is no hair colour). */
static int SkinLike(double h, double s, double l)
{
    return h >= 0.15 && h <= 0.85 && l > 0.5 && s < 0.8;
}

/* The character's signature colour: of its personal ramps (else its team ones), the one
   with the most saturation over its colours, skin tones left out; 0 when it has none with
   any colour. */
static struct Ramp sSigRamp; /* the ramp the last SignatureOf took (version 15: a character's hair) */

static int SignatureOf(const u8 *set, u16 per, u16 team, double *hue, double *sat)
{
    struct Ramp rp[16];
    int n, k, i, best = -1;
    double bestScore = 0;

    n = Ramps(per ? per : team, set, rp);
    for (k = 0; k < n; k++)
    {
        double sum = 0, h, s, l;

        int skin = 0;

        for (i = 0; i < rp[k].n; i++)
        {
            ToHsl(Color(set, rp[k].idx[i]), &h, &s, &l);
            sum += s * (l > 0.08 && l < 0.92 ? 1 : 0.2);
            skin += SkinLike(h, s, l);
        }
        if (skin * 2 > rp[k].n)
            continue;
        if (sum > bestScore)
        {
            bestScore = sum;
            best = k;
        }
    }
    if (best < 0 || bestScore / rp[best].n < 0.15)
        return 0;
    sSigRamp = rp[best];
    {
        int r = 0, g = 0, b = 0;
        double l;

        for (i = 0; i < rp[best].n; i++)
        {
            u16 c = Color(set, rp[best].idx[i]);

            r += c & 31;
            g += (c >> 5) & 31;
            b += (c >> 10) & 31;
        }
        ToHsl((u16)(r / rp[best].n | ((g / rp[best].n) << 5) | ((b / rp[best].n) << 10)), hue, sat, &l);
    }
    return 1;
}

/* Version 15: a signature for every character with colours: SignatureOf, else its team ramps', else its most
   saturated colour that is no skin tone (TintBody makes it bright either way). */
static int SignatureLoose(const u8 *set, u16 per, u16 team, double *hue, double *sat)
{
    int i, best = -1;
    double bs = -1, h, s, l;

    if (SignatureOf(set, per, team, hue, sat) || (per != 0 && team != 0 && SignatureOf(set, team, 0, hue, sat)))
        return 1;
    for (i = 1; i < 15; i++)
        if ((per | team) & (1 << i))
        {
            ToHsl(Color(set, i), &h, &s, &l);
            if (!SkinLike(h, s, l) && l > 0.08 && l < 0.92 && s > bs)
            {
                bs = s;
                best = i;
            }
        }
    if (best < 0)
        return 0;
    ToHsl(Color(set, best), hue, sat, &l);
    return 1;
}

static int Signature(const u8 *set, const u8 *gen, double *hue, double *sat)
{
    u16 per, team;

    SetMasks(set, gen, &per, &team);
    return SignatureOf(set, per, team, hue, sat);
}

/* The body colours: each colour of mask in the five palettes of out keeps its own lightness and takes the
   signature's hue and (clamped bright) saturation. */
static void TintBody(u8 *out, u16 mask, double hue, double sat)
{
    int p, i;

    /* bright, the user's choice (2026-11-04: "earthy tones get boring quick"): at least
       0.65, so a brown-haired character still gives a vivid body; at most 0.9 */
    sat = sat < 0.65 ? 0.65 : sat > 0.9 ? 0.9 : sat;
    for (p = 0; p < PALS; p++)
        for (i = 1; i < 16; i++)
            if (mask & (1 << i))
            {
                double h, s, l;

                ToHsl(Color(out + p * 32, i), &h, &s, &l);
                SetColor(out + p * 32, i, FromHsl(hue, sat * (0.7 + 0.3 * (1 - (l > 0.5 ? l - 0.5 : 0.5 - l) * 2)), l));
            }
}

/* Version 7: the set for vanilla set srcIdx (of class srcJid) on monster class dstJid: the
   monster's generic palettes with its body in the character's signature colour; 0 when
   dstJid is no monster this knows, or the character has no colour to give. */
static int BuildMonsterSet(int srcIdx, int srcJid, int dstJid, u8 out[SET_BYTES])
{
    u8 set[SET_BYTES];
    u16 mask = dstJid >= 1 && dstJid <= JID_MAX ? MonsterBodyMask(dstJid) : 0; /* (a foreign class: none) */
    double hue, sat;
    int p;

    if (mask == 0 || !sHasGen[srcJid] || !sHasGen[dstJid] || sHasRoles[dstJid] || !VanillaSet(srcIdx, set) ||
        !Signature(set, sGen[srcJid], &hue, &sat))
        return 0;
    for (p = 0; p < PALS; p++)
        memcpy(out + p * 32, sGen[dstJid] + (p < 4 ? p : 0) * 32, 32);
    TintBody(out, mask, hue, sat);
    return 1;
}

/* The uncompressed LZ77 stream of a set: the header, then each 8 bytes after a
   flag byte of 0 (8 literals). */
static void PackLiteral(const u8 *raw, u8 *lz)
{
    int i, o = 4;

    lz[0] = 0x10;
    lz[1] = (u8)(SET_BYTES & 0xFF);
    lz[2] = (u8)((SET_BYTES >> 8) & 0xFF);
    lz[3] = (u8)((SET_BYTES >> 16) & 0xFF);
    for (i = 0; i < SET_BYTES; i += 8)
    {
        lz[o++] = 0;
        memcpy(lz + o, raw + i, 8);
        o += 8;
    }
}

/* ------------------------------------------------------------------ version 15: every class */

static int Promoted(int jid)
{
    const struct ClassData *k;

    if (jid >= 1 && jid <= JID_MAX)
        return Rando_ClassIsPromoted(jid);
    if (jid >= CLASS_SLOTS || !Reforged_Fe7ClassAvailable(jid) || (k = GetClassData(jid)) == NULL)
        return 0;
    return (k->attributes & CA_PROMOTED) != 0;
}

/* The FE8 classes whose learned roles a class with none of its own is matched against (the first is no
   preference: the one whose generic palette is nearest wins, DeriveRoles); a class not listed tries its M/F
   twin, then every class with learned roles. */
static const struct { u8 jid; u8 ref[3]; } sRefs[] = {
    /* the foreign classes (reforged_fe7.c) */
    { 0x80, { CLASS_RANGER, CLASS_ARCHER, 0 } },                        /* Nomad */
    { 0x81, { CLASS_RANGER_F, CLASS_ARCHER_F, 0 } },                    /* Nomad F */
    { 0x82, { CLASS_SWORDMASTER_F, CLASS_MYRMIDON_F, CLASS_SWORDMASTER } }, /* Blade Lord (Lyn's body) */
    { 0x83, { CLASS_SWORDMASTER_F, CLASS_MYRMIDON_F, CLASS_SWORDMASTER } }, /* Blade Lord (Eirika's) */
    { 0x84, { CLASS_GENERAL, CLASS_GREAT_KNIGHT, CLASS_PALADIN } },     /* Armor Lord (Hector's body) */
    { 0x85, { CLASS_GENERAL, CLASS_GREAT_KNIGHT, CLASS_PALADIN } },     /* Armor Lord (Ephraim's) */
    { 0x86, { CLASS_SAGE, CLASS_BISHOP, 0 } },                          /* Archsage */
    { 0x87, { CLASS_DRUID, CLASS_SUMMONER, 0 } },                       /* Dark Druid */
    { 0x88, { CLASS_DANCER, CLASS_MONK, CLASS_PRIEST } },               /* Bard */
    { 0x89, { CLASS_THIEF, CLASS_ASSASSIN_F, CLASS_MYRMIDON_F } },      /* Thief F */
    { 0x8A, { CLASS_RANGER, CLASS_PALADIN, 0 } },                       /* Trooper */
    { 0x8B, { CLASS_RANGER_F, CLASS_PALADIN_F, 0 } },                   /* Trooper F */
    { 0x8C, { CLASS_GENERAL, CLASS_SWORDMASTER, CLASS_GREAT_KNIGHT } }, /* King */
    { 0x8D, { CLASS_MYRMIDON, CLASS_MERCENARY, CLASS_SWORDMASTER } },   /* Lion Lord */
    { 0x8E, { CLASS_SWORDMASTER, CLASS_HERO, CLASS_PALADIN } },         /* Master Lord */
    /* FE8 classes no character has a set for */
    { CLASS_EPHRAIM_LORD, { CLASS_MERCENARY, CLASS_MYRMIDON, CLASS_CAVALIER } },
    { CLASS_EIRIKA_LORD, { CLASS_MYRMIDON_F, CLASS_SWORDMASTER_F, CLASS_PEGASUS_KNIGHT } },
    { CLASS_EPHRAIM_MASTER_LORD, { CLASS_PALADIN, CLASS_GREAT_KNIGHT, CLASS_HERO } },
    { CLASS_EIRIKA_MASTER_LORD, { CLASS_SWORDMASTER_F, CLASS_FALCON_KNIGHT, CLASS_PALADIN_F } },
    { CLASS_SOLDIER, { CLASS_ARMOR_KNIGHT, CLASS_FIGHTER, CLASS_CAVALIER } },
    { CLASS_JOURNEYMAN, { CLASS_FIGHTER, CLASS_PIRATE, CLASS_BRIGAND } },
    { CLASS_JOURNEYMAN_T1, { CLASS_FIGHTER, CLASS_PIRATE, CLASS_BRIGAND } },
    { CLASS_JOURNEYMAN_T2, { CLASS_FIGHTER, CLASS_PIRATE, CLASS_BRIGAND } },
    { CLASS_RECRUIT, { CLASS_CAVALIER_F, CLASS_ARMOR_KNIGHT_F, CLASS_CAVALIER } },
    { CLASS_RECRUIT_T1, { CLASS_CAVALIER_F, CLASS_ARMOR_KNIGHT_F, CLASS_CAVALIER } },
    { CLASS_RECRUIT_T2, { CLASS_CAVALIER_F, CLASS_ARMOR_KNIGHT_F, CLASS_CAVALIER } },
    { CLASS_PUPIL, { CLASS_MAGE, CLASS_SHAMAN, 0 } },
    { CLASS_PUPIL_T1, { CLASS_MAGE, CLASS_SHAMAN, 0 } },
    { CLASS_PUPIL_T2, { CLASS_MAGE, CLASS_SHAMAN, 0 } },
};

/* How far apart two classes' generic palettes are: for each colour of a (1-14), the nearest of b's. */
static int PalDist(int a, int b)
{
    int i, j, d = 0;

    for (i = 1; i < 15; i++)
    {
        int best = 1 << 20;

        for (j = 1; j < 15; j++)
        {
            int x = Dist(Color(sGen[a], i), Color(sGen[b], j));

            if (x < best)
                best = x;
        }
        d += best;
    }
    return d;
}

/* The indices of dst (1-14) whose nearest colour in ref's generic palette is one of refMask's. */
static u16 NearestMask(u16 refMask, int ref, int dst)
{
    u16 m = 0;
    int i, j;

    for (i = 1; i < 15; i++)
    {
        int best = -1, bj = 0;

        for (j = 1; j < 15; j++)
        {
            int x = Dist(Color(sGen[dst], i), Color(sGen[ref], j));

            if (best < 0 || x < best)
            {
                best = x;
                bj = j;
            }
        }
        if (refMask & (1 << bj))
            m |= (u16)(1 << i);
    }
    return m;
}

/* The nearest ramp of list (n) to look l: its distance (a large one when the list is empty). */
static int NearestRamp(const struct Ramp *list, int n, const u8 *pal, const int l[4])
{
    int k, best = 1 << 20, look[4];

    for (k = 0; k < n; k++)
    {
        int d;

        RampLook(&list[k], pal, look);
        d = LookDist(look, l);
        if (d < best)
            best = d;
    }
    return best;
}

/* The classes drawn as one character (the lords, the King): their generic rows keep that character's hair in
   every faction, and the FE8 classes nearest them wear helmets or hoods, so neither tells the hair from the
   cloth: their hair ramps, read from the palettes (FE8's lords: graphics/banim; the foreign ones from the
   player's ROMs). */
static const struct { u8 jid; u16 hair; } sHair[] = {
    { CLASS_EPHRAIM_LORD, 0x01C0 },        /* 6-8, blue */
    { CLASS_EIRIKA_LORD, 0x01C0 },         /* 6-8, blue */
    { CLASS_EPHRAIM_MASTER_LORD, 0x00E0 }, /* 5-7 */
    { CLASS_EIRIKA_MASTER_LORD, 0x01E0 },  /* 5-8 */
    { 0x82, 0x00E0 }, { 0x83, 0x00E0 },    /* Blade Lord: Lyn's green, 5-7 */
    { 0x84, 0x00E0 }, { 0x85, 0x00E0 },    /* Armor Lord: Hector's blue, 5-7 */
    { 0x8C, 0x0080 },                      /* King: Zephiel's green, 7 */
    { 0x8D, 0x00E0 }, { 0x8E, 0x00E0 },    /* Lion / Master Lord: Roy's red, 5-7 */
    /* drawn as one look too: FE6's Thief F (Cath's orange, 5-7), the Bard (Nils's cyan, 5-7), the Recruit
       (Amelia's blonde, 5-7) and the Pupil (Ewan's orange, 5-7) */
    { 0x89, 0x00E0 }, { 0x88, 0x00E0 },
    { CLASS_RECRUIT, 0x00E0 }, { CLASS_RECRUIT_T1, 0x00E0 }, { CLASS_RECRUIT_T2, 0x00E0 },
    { CLASS_PUPIL, 0x00E0 }, { CLASS_PUPIL_T1, 0x00E0 }, { CLASS_PUPIL_T2, 0x00E0 },
};

static u16 HairOf(int jid)
{
    int i;

    for (i = 0; i < (int)(sizeof(sHair) / sizeof(sHair[0])); i++)
        if (sHair[i].jid == jid)
            return sHair[i].hair;
    return 0;
}

static void Rgb(u16 c, int *r, int *g, int *b)
{
    *r = c & 31;
    *g = (c >> 5) & 31;
    *b = (c >> 10) & 31;
}

/* A palette's team colours by FE8's faction scheme: blue (or cyan) on the player's row, red (or purple) on the
   enemy's, green on the NPCs'. The generic rows also change a generic unit's hair between factions (FE8's
   Myrmidon: brown, white, red, yellow), which no faction scheme matches. */
static u16 FactionTeamMask(int jid)
{
    u16 m = 0;
    int i;

    for (i = 1; i < 15; i++)
    {
        int r0, g0, b0, r1, g1, b1, r2, g2, b2;

        Rgb(Color(sGen[jid], i), &r0, &g0, &b0);
        Rgb(Color(sGen[jid] + 32, i), &r1, &g1, &b1);
        Rgb(Color(sGen[jid] + 64, i), &r2, &g2, &b2);
        if (b0 > r0 + 3 && r1 > g1 + 3 && g2 > r2 && g2 >= b2)
            m |= (u16)(1 << i);
    }
    return m;
}

/* The colours a palette changes between its faction rows. */
static u16 VariesMask(int jid)
{
    u16 m = 0;
    int i, r;

    for (i = 1; i < 15; i++)
        for (r = 1; r < 4; r++)
            if ((Color(sGen[jid], i) & 0x7FFF) != (Color(sGen[jid] + r * 32, i) & 0x7FFF))
                m |= (u16)(1 << i);
    return m;
}

/* The faction test is strict per colour, and a team ramp's palest shade often fails it (Lion Lord's cape: its
   highlight is a pale yellow on the enemy row). The team colours grow over their neighbours that the rows also
   change and that continue the same ramp in the player's row (coloured, no hue break, no skin tone). */
static u16 GrowTeam(int jid, u16 team)
{
    u16 varies = VariesMask(jid);
    int grown = 1, i;

    while (grown)
    {
        grown = 0;
        for (i = 1; i < 15; i++)
        {
            double h, sa, l;
            int nb, r, g, b, mx, mn;

            if ((team & (1 << i)) || !(varies & (1 << i)))
                continue;
            ToHsl(Color(sGen[jid], i), &h, &sa, &l);
            Rgb(Color(sGen[jid], i), &r, &g, &b);
            mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
            mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
            if (SkinLike(h, sa, l) || mx - mn < 6)
                continue; /* (a grey next to the team colours, Druid F's robe, is no part of them) */
            for (nb = i - 1; nb <= i + 1; nb += 2)
                if (nb >= 1 && nb < 15 && (team & (1 << nb)) && !HueBreak(sGen[jid], nb, i))
                {
                    team |= (u16)(1 << i);
                    grown = 1;
                    break;
                }
        }
    }
    return team;
}

/* Roles for class dst, which no character has a set for (the twins' lords, the Soldier, the trainees, a few
   others, the foreign classes):
   - team: the colours its rows change by FE8's faction scheme (FactionTeamMask; the parts that turn red on
     the enemy side, as in the characters' sets), else the colours nearest ref's team colours;
   - personal: the other colours its rows change between factions (a generic unit's hair: what the characters'
     sets give their own colours on FE8's classes), the hair of a class drawn as one character (sHair), and
     each remaining coloured ramp (not grey, not skin) that looks more like one of ref's personal ramps than
     any of ref's fixed ones (the nearest of its reference classes: FE8's character-learned roles). */
static void DeriveRoles(int dst, int ref, u16 *personal, u16 *team)
{
    struct Ramp g[16], rp[16], ro[16];
    int n, np, no, k, i, look[4];

    *team = FactionTeamMask(dst);
    if (*team == 0)
        *team = NearestMask(sTeam[ref], ref, dst);
    else
        *team = GrowTeam(dst, *team);
    *personal = (u16)(VariesMask(dst) & ~*team & 0x7FFE);
    for (i = 1; i < 15; i++) /* a skin tone some rows change (FE6's Trooper: the enemy's paler face) stays skin */
        if (*personal & (1 << i))
        {
            double h, sa, l;

            ToHsl(Color(sGen[dst], i), &h, &sa, &l);
            if (SkinLike(h, sa, l))
                *personal &= (u16)~(1 << i);
        }
    *personal |= HairOf(dst);
    *team &= (u16)~*personal;
    sHueSplit = 1;
    n = Ramps((u16)(0x7FFE & ~*team & ~*personal), sGen[dst], g);
    sHueSplit = 0;
    np = Ramps(sPersonal[ref], sGen[ref], rp);
    no = Ramps((u16)(0x7FFE & ~sPersonal[ref] & ~sTeam[ref]), sGen[ref], ro);
    for (k = 0; k < n; k++)
    {
        double h, sa, l, chroma = 0;
        int skin = 0;

        for (i = 0; i < g[k].n; i++)
        {
            int r, gg, bb, mx, mn;

            Rgb(Color(sGen[dst], g[k].idx[i]), &r, &gg, &bb);
            mx = r > gg ? (r > bb ? r : bb) : (gg > bb ? gg : bb);
            mn = r < gg ? (r < bb ? r : bb) : (gg < bb ? gg : bb);
            chroma += (mx - mn) / 31.0 / g[k].n; /* (HSL's saturation calls a pale yellow fully saturated) */
            ToHsl(Color(sGen[dst], g[k].idx[i]), &h, &sa, &l);
            skin += SkinLike(h, sa, l);
        }
        if (chroma < 0.3 || skin > 0 || g[k].n < 2)
            continue;
        RampLook(&g[k], sGen[dst], look);
        if (np > 0 && NearestRamp(rp, np, sGen[ref], look) < NearestRamp(ro, no, sGen[ref], look))
            for (i = 0; i < g[k].n; i++)
                *personal |= (u16)(1 << g[k].idx[i]);
    }
    if (dst == 0x8A || dst == 0x8B)
        *personal &= (u16)~0x7018; /* FE6's Trooper rows change the horse's coat by faction (12-14) and the
                                      rider's skin (3-4: the enemy's is paler), generic touches: no character
                                      brings a horse colour, and skin stays the class's */
}

/* 1 when colour i of class jid is skin: a skin tone that its faction rows keep (hair and cloth change between
   them; a gold trim of the team colours is no skin either). */
static int SkinIndex(int jid, int i)
{
    double h, sa, l;

    if (i < 1 || i > 14 || ((VariesMask(jid) | sATeam[jid] | HairOf(jid)) >> i) & 1)
        return 0; /* (a class drawn as one character keeps its hair in every row: sHair says where it is) */
    ToHsl(Color(sGen[jid], i), &h, &sa, &l);
    return SkinLike(h, sa, l) && h <= 0.65; /* a yellower tone is blonde or light brown hair (the Dancer's) */
}

/* 1 when most of a ramp's colours are skin (SkinIndex) in class jid. */
static int RampIsSkin(const struct Ramp *rp, int jid)
{
    int i, skin = 0;

    for (i = 0; i < rp->n; i++)
        skin += SkinIndex(jid, rp->idx[i]);
    return skin * 2 > rp->n;
}

/* Where Eirika's and Ephraim's hair goes on class jid (a class not drawn as one character: sHair has those):
   one ramp of its personal colours, never the outline (15) or skin (SkinIndex). Ramps of two or more colours
   first, coloured ones before greys; among those a ramp that is the hair of a generic unit (its rows change
   it strongly between factions, and not by FE8's faction scheme), then one at FE8's usual hair indices
   (6-7), then the longest. A class whose personal colours hold no hair (a helmet hides it) gets its longest
   coloured personal ramp: the Paladin's barding, which the characters' own sets recolour, not the horse's
   coat or mane. 0: none (no personal colours). */
static u16 TwinHairMask(int jid)
{
    struct Ramp rp[16];
    u16 per = (u16)(sAPersonal[jid] & 0x7FFE), hair = 0, out = 0;
    int n, k, i, r, best = -1, bestHair = 0;
    double bestScore = 0;

    if (HairOf(jid) != 0)
        return HairOf(jid);
    for (i = 1; i < 15; i++)
    {
        int strong = 0;

        if (SkinIndex(jid, i))
            per &= (u16)~(1 << i);
        for (r = 1; r < 4; r++)
            strong |= Dist(Color(sGen[jid], i), Color(sGen[jid] + r * 32, i)) >= 8;
        if (strong)
            hair |= (u16)(1 << i);
    }
    hair &= (u16)~FactionTeamMask(jid);
    sHueSplit = 1;
    n = Ramps(per, sGen[jid], rp);
    sHueSplit = 0;
    for (k = 0; k < n; k++)
    {
        double chroma = 0, score;
        int inHair = 0, at67 = 0;

        if (RampIsSkin(&rp[k], jid))
            continue;
        for (i = 0; i < rp[k].n; i++)
        {
            int cr, cg, cb, mx, mn;

            Rgb(Color(sGen[jid], rp[k].idx[i]), &cr, &cg, &cb);
            mx = cr > cg ? (cr > cb ? cr : cb) : (cg > cb ? cg : cb);
            mn = cr < cg ? (cr < cb ? cr : cb) : (cg < cb ? cg : cb);
            chroma += (mx - mn) / 31.0 / rp[k].n;
            inHair += (hair >> rp[k].idx[i]) & 1;
            at67 |= rp[k].idx[i] == 6 || rp[k].idx[i] == 7;
        }
        /* a ramp of two or more colours first, then a coloured one (a grey: steel, a white robe), then ... */
        score = (rp[k].n >= 2 ? 100000 : 0) + (chroma >= 0.15 ? 10000 : 0) + (inHair * 2 > rp[k].n ? 1000 : 0) +
                (at67 ? 100 : 0) + rp[k].n * 10 + chroma;
        if (best < 0 || score > bestScore)
        {
            best = k;
            bestScore = score;
            bestHair = inHair * 2 > rp[k].n;
        }
    }
    for (i = 0; best >= 0 && i < rp[best].n; i++)
        out |= (u16)(1 << rp[best].idx[i]);
    if (bestHair) /* a generic unit's hair: only its own colours (the Archer's 5-7, not the pale 8 next to it),
                     when two or more */
    {
        u16 h2 = (u16)(out & hair);

        if ((h2 & (h2 - 1)) != 0)
            out = h2;
    }
    return out;
}

/* The colours Eirika and Ephraim may change on class jid: its hair (TwinHairMask), else (a class with no
   personal colours at all) its team colours, as before. */
static u16 TwinTarget(int jid)
{
    if (sAHair[jid] != 0)
        return sAHair[jid];
    return sAPersonal[jid] == 0 ? (u16)(sATeam[jid] & 0x7FFE) : 0;
}

/* Every class's generic palette (the foreign ones too, when loaded) and roles: the learned or borrowed ones
   (BuildInfo, version 7-14's), else derived (DeriveRoles) from the nearest of its reference classes (sRefs),
   else the monster rule (a monster body known: sMonsterBody). */
static void BuildAll(void)
{
    static u8 buf[GEN_BYTES + 0x100];
    int jid, i, k;

    if (sAllBuilt)
        return;
    BuildInfo();
    for (jid = JID_MAX + 1; jid < CLASS_SLOTS; jid++)
    {
        int b = ClassPalBanim(jid);

        sHasGen[jid] = 0;
        if (b >= 0 && Unpack(banim_data[b].pal, buf, sizeof(buf)) >= GEN_BYTES)
        {
            memcpy(sGen[jid], buf, GEN_BYTES);
            sHasGen[jid] = 1;
        }
    }
    for (jid = 1; jid < CLASS_SLOTS; jid++)
    {
        int best = -1, bestD = 0;

        sAKind[jid] = ROLES_NONE;
        sARef[jid] = 0;
        sAPersonal[jid] = sATeam[jid] = 0;
        if (!sHasGen[jid])
            continue;
        if (jid <= JID_MAX && sHasRoles[jid])
        {
            sAKind[jid] = sHasRoles[jid] == 2 ? ROLES_TWIN : ROLES_LEARNED;
            sAPersonal[jid] = sPersonal[jid];
            sATeam[jid] = sTeam[jid];
            continue;
        }
        if (MonsterBodyMaskAll(jid) != 0)
        {
            sAKind[jid] = ROLES_MONSTER;
            continue;
        }
        for (i = 0; i < (int)(sizeof(sRefs) / sizeof(sRefs[0])); i++)
            if (sRefs[i].jid == jid)
                for (k = 0; k < 3; k++)
                {
                    int r = sRefs[i].ref[k], d;

                    if (r == 0 || !sHasGen[r] || !sHasRoles[r])
                        continue;
                    d = PalDist(jid, r);
                    if (best < 0 || d < bestD)
                    {
                        best = r;
                        bestD = d;
                    }
                }
        if (best < 0 && jid <= JID_MAX && Rando_ClassTwin(jid) != 0 && sHasRoles[Rando_ClassTwin(jid)])
            best = Rando_ClassTwin(jid);
        if (best < 0) /* no reference: the nearest class with learned roles */
            for (k = 1; k <= JID_MAX; k++)
            {
                int d;

                if (sHasRoles[k] != 1 || !sHasGen[k])
                    continue;
                d = PalDist(jid, k);
                if (best < 0 || d < bestD)
                {
                    best = k;
                    bestD = d;
                }
            }
        if (best < 0)
            continue;
        DeriveRoles(jid, best, &sAPersonal[jid], &sATeam[jid]);
        if ((sAPersonal[jid] | sATeam[jid]) == 0)
            continue;
        sAKind[jid] = ROLES_DERIVED;
        sARef[jid] = (u8)best;
    }
    for (jid = 1; jid < CLASS_SLOTS; jid++)
        sAHair[jid] = sAKind[jid] == ROLES_LEARNED || sAKind[jid] == ROLES_TWIN || sAKind[jid] == ROLES_DERIVED
                          ? TwinHairMask(jid)
                          : 0;
    sAllBuilt = 1;
}

/* Eirika's and Ephraim's own classes (their colours are those classes' generic ones), the lord of the tier
   asked for; 0 for anyone else. */
static int TwinLord(int pid, int promoted)
{
    if (pid == CHARACTER_EIRIKA)
        return promoted ? CLASS_EIRIKA_MASTER_LORD : CLASS_EIRIKA_LORD;
    if (pid == CHARACTER_EPHRAIM)
        return promoted ? CLASS_EPHRAIM_MASTER_LORD : CLASS_EPHRAIM_LORD;
    return 0;
}

static int TwinOwnClass(int pid, int jid)
{
    return TwinLord(pid, 0) != 0 && (jid == TwinLord(pid, 0) || jid == TwinLord(pid, 1));
}

/* SourceSlot for any class (the foreign ones' tier from their own data). */
static int SourceSlotAll(int pid, int dstJid)
{
    const struct CharacterData *vc = Rando_VanillaChar(pid);
    int s, best = -1, score = -1;

    for (s = 0; s < 7; s++)
    {
        int j = sVanillaConfig[pid - 1][s], sc;

        if (j < 1 || j > JID_MAX || sVanillaIt[pid - 1][s] == 0 || !sHasGen[j])
            continue;
        sc = 0;
        if (Rando_ClassIsPromoted(j) == Promoted(dstJid))
            sc += 2;
        if (vc != NULL && j == vc->defaultClass)
            sc += 1;
        if (sc > score)
        {
            score = sc;
            best = s;
        }
    }
    return best;
}

/* The colours character src brings to class dst: a set (five palettes), the class it is drawn for and its
   personal and team masks. Eirika and Ephraim bring their lord class's generic palettes, with that class's
   roles; anyone else the vanilla set SourceSlotAll picks. 0: none (no set of its own). */
static int SourceColours(int src, int dst, u8 set[SET_BYTES], int *srcJid, u16 *per, u16 *team)
{
    int tw = TwinLord(src, Promoted(dst)), s, p;

    if (tw != 0)
    {
        if (!sHasGen[tw] || sAKind[tw] == ROLES_NONE || sAKind[tw] == ROLES_MONSTER)
            return 0;
        for (p = 0; p < PALS; p++)
            memcpy(set + p * 32, sGen[tw] + (p < 4 ? p : 0) * 32, 32);
        *srcJid = tw;
        *per = HairOf(tw); /* their signature: the hair */
        *team = 0;
        return 1;
    }
    if (src < 1 || src > 0x100 || (s = SourceSlotAll(src, dst)) < 0)
        return 0;
    *srcJid = sVanillaConfig[src - 1][s];
    if (!VanillaSet(sVanillaIt[src - 1][s] - 1, set))
        return 0;
    SetMasks(set, sGen[*srcJid], per, team);
    return (*per | *team) != 0;
}

/* Version 15's set of character src on class dst, in out: the role mapping (BuildSet's) onto dst's learned
   or derived roles, else the monster rule on dst's body (its signature colour). 1: a set, 2: a monster set
   (only for its own creature's sprites), 0: none. */
static int BuildAllSet(int src, int dst, u8 out[SET_BYTES])
{
    u8 set[SET_BYTES];
    s8 map[16];
    u16 per, team, mask, only0 = 0;
    int srcJid, p, t, any = 0;
    double hue, sat;

    BuildAll();
    sLastRoles = 0;
    if (dst < 1 || dst >= CLASS_SLOTS || !sHasGen[dst] || !SourceColours(src, dst, set, &srcJid, &per, &team))
        return 0;
    if (sAKind[dst] == ROLES_LEARNED || sAKind[dst] == ROLES_TWIN || sAKind[dst] == ROLES_DERIVED)
    {
        memset(map, -1, sizeof(map));
        {
        int ss = sAKind[srcJid] == ROLES_DERIVED && TwinLord(src, 0) != 0, ds = sAKind[dst] == ROLES_DERIVED;
        /* the outline (15) stays the class's: a character's set changing it (a shade) makes no colour of it */
        u16 dPer = (u16)(sAPersonal[dst] & 0x7FFE), dTeam = (u16)(sATeam[dst] & 0x7FFE);

        if (TwinLord(src, 0) != 0) /* Eirika and Ephraim: their hair on the class's hair only (TwinTarget; the
                                      class's faction colours stay, their lords' have none, unless the class has
                                      no personal colours at all) */
            MapKindEx(per, set, sGen[srcJid], ss, TwinTarget(dst), sGen[dst], ds, map);
        else
        {
            MapKindEx(per ? per : team, set, sGen[srcJid], ss, dPer, sGen[dst], ds, map);
            MapKindEx(team ? team : per, set, sGen[srcJid], ss, dTeam, sGen[dst], ds, map);
        }
        }
        /* a class drawn as one character (sHair): its hair takes the character's hair, the ramp of its
           signature colour (generic palettes colour hair differently, so the look match can miss it) */
        if (HairOf(dst) != 0)
        {
            struct Ramp hr[16], sr;
            double h, sa;
            int nh, k, r, ok = 0;

            /* the character's hair: Eirika's and Ephraim's (sHair), else the most colourful ramp (cut by hue,
               no skin) of its personal colours where its class's generic rows change between factions (a
               generic unit's hair, VariesMask), else of all its personal colours, else its signature ramp */
            int nr = 0, best = -1, pass;
            double bestC = 0;

            for (pass = 0; pass < 2 && best < 0; pass++)
            {
            u16 from = pass == 0 && TwinLord(src, 0) == 0 ? (u16)(per & VariesMask(srcJid)) : (u16)(per ? per : team);

            sHueSplit = 1;
            nr = from != 0 ? Ramps(from, set, hr) : 0;
            sHueSplit = 0;
            for (k = 0; k < nr; k++)
            {
                double c = 0;
                int skin = 0;

                for (r = 0; r < hr[k].n; r++)
                {
                    int cr, cg, cb, mx, mn;

                    Rgb(Color(set, hr[k].idx[r]), &cr, &cg, &cb);
                    mx = cr > cg ? (cr > cb ? cr : cb) : (cg > cb ? cg : cb);
                    mn = cr < cg ? (cr < cb ? cr : cb) : (cg < cb ? cg : cb);
                    c += (mx - mn) / 31.0 / hr[k].n;
                    ToHsl(Color(set, hr[k].idx[r]), &h, &sa, &sat);
                    skin += SkinLike(h, sa, sat);
                }
                if (skin * 2 > hr[k].n || (hr[k].n < 2 && TwinLord(src, 0) == 0))
                    continue;
                if (best < 0 || c > bestC)
                {
                    best = k;
                    bestC = c;
                }
            }
            }
            if (best >= 0)
            {
                ok = 1;
                sr = hr[best];
            }
            else if (SignatureOf(set, per, team, &h, &sa))
            {
                ok = 1;
                sr = sSigRamp;
            }
            sHueSplit = 1;
            nh = ok ? Ramps(HairOf(dst), sGen[dst], hr) : 0;
            sHueSplit = 0;
            for (k = 0; k < nh; k++)
                for (r = 0; r < hr[k].n; r++)
                    map[hr[k].idx[r]] = (s8)sr.idx[hr[k].n == 1 ? sr.n / 2 : (r * (sr.n - 1) + (hr[k].n - 1) / 2) / (hr[k].n - 1)];
        }
        /* skin stays skin: a skin colour of the class takes only the character's skin tone (a ramp matched by
           look can hold the character's hair or cloth there) */
        for (t = 1; t < 15; t++)
            if (map[t] >= 0 && SkinIndex(dst, t))
            {
                double h, sa, l;

                ToHsl(Color(set, map[t]), &h, &sa, &l);
                if (!SkinLike(h, sa, l))
                    map[t] = -1;
            }
        map[15] = -1;
        /* Eirika's and Ephraim's hair on colours the class's rows give by faction (the Paladin's barding, a
           class with team colours only): the player's row only, the other factions' rows keep theirs */
        if (TwinLord(src, 0) != 0)
            only0 = (u16)(TwinTarget(dst) & (FactionTeamMask(dst) | sATeam[dst]));
        for (p = 0; p < PALS; p++)
        {
            memcpy(out + p * 32, sGen[dst] + (p < 4 ? p : 0) * 32, 32);
            for (t = 1; t < 16; t++)
                if (map[t] >= 0 && (p == 0 || !((only0 >> t) & 1)))
                {
                    SetColor(out + p * 32, t, Color(set + p * 32, map[t]));
                    any = 1;
                }
        }
        memcpy(sLastMap, map, sizeof(sLastMap));
        memcpy(sLastSet, set, sizeof(sLastSet));
        sLastRoles = 1;
        if (any)
            return 1;
    }
    sLastRoles = 0;
    mask = MonsterBodyMaskAll(dst);
    if (sAKind[dst] == ROLES_MONSTER && mask != 0 && SignatureLoose(set, per, team, &hue, &sat))
    {
        for (p = 0; p < PALS; p++)
            memcpy(out + p * 32, sGen[dst] + (p < 4 ? p : 0) * 32, 32);
        TintBody(out, mask, hue, sat);
        return 2;
    }
    return 0;
}

/* Version 15 is on: a randomized game of version 15 or later with Keep colours, or the test switch
   FE8PC_TEST_KEEP_COLOURS (any game, for looking at the colours with the class test switches). */
static int AllOn(void)
{
    static int sForced = -1;

    if (sForced < 0)
        sForced = getenv("FE8PC_TEST_KEEP_COLOURS") != NULL;
    return sTestAllOn || sForced || (gRandoRunOn && Rando_Opt(RFO_RANDO_PALETTES) && gRandoRun.version >= RANDO_VERSION_ALLCOLOURS);
}

/* A slot no longer holds its set: every row entry pointing at it goes. */
static void ForgetSlot(int k)
{
    int pid, s, it = SYNTH_BASE + k + 1;

    for (pid = 0; pid < 0x100; pid++)
        for (s = 0; s < 7; s++)
            if (gAnimCharaPalIt[pid][s] == it)
            {
                gAnimCharaPalConfig[pid][s] = 0;
                gAnimCharaPalIt[pid][s] = 0;
            }
    sSynth[k].jid = 0;
    sSynth[k].src = 0;
}

/* The set of character src on class jid in the battle cache: found, or built into a free slot, else into the
   least recently used one (its row entries dropped first). Only a few sets are shown at once (the two fighters
   and a triangle attack's two partners), so the 126 slots never take a set still on screen. The gAnimCharaPalIt
   value (SYNTH_BASE + slot + 1, at most 0xFE), or 0 (the class's generic colours). */
static int CacheSet(int src, int jid)
{
    int k, best = -1, r;

    for (k = 0; k < sSynthCount; k++)
        if (!sSynth[k].fixed && sSynth[k].jid == jid && sSynth[k].src == src)
        {
            sSynth[k].tick = ++sTick;
            return SYNTH_BASE + k + 1;
        }
    if (sBuildFailed[src - 1][jid / 8] & (1 << (jid % 8)))
        return 0;
    for (k = 0; k < sSynthCount && best < 0; k++)
        if (!sSynth[k].fixed && sSynth[k].jid == 0)
            best = k; /* a slot freed earlier */
    if (best < 0 && sSynthCount < SYNTH_MAX)
        best = sSynthCount;
    if (best < 0)
        for (k = 0; k < sSynthCount; k++)
            if (!sSynth[k].fixed && (best < 0 || sSynth[k].tick < sSynth[best].tick))
                best = k;
    if (best < 0)
        return 0;
    if (best < sSynthCount)
        ForgetSlot(best);
    r = BuildAllSet(src, jid, sSynth[best].raw);
    if (r == 0)
    {
        sBuildFailed[src - 1][jid / 8] |= (u8)(1 << (jid % 8));
        return 0;
    }
    if (best == sSynthCount)
        sSynthCount++;
    sSynth[best].monster = (u8)(r == 2);
    sSynth[best].fixed = 0;
    sSynth[best].src = (u8)src;
    sSynth[best].jid = (u8)jid;
    sSynth[best].tick = ++sTick;
    PackLiteral(sSynth[best].raw, sSynth[best].lz);
    return SYNTH_BASE + best + 1;
}

/* The character whose colours unit pid shows: its recruitment fill's (a playable slot of a randomized game),
   else its own; FE8PC_TEST_PAL_SRC=a[:b] (a test switch): character a's on the player's units, b's on the
   others (when given). */
static int ColourSource(int pid, const struct Unit *unit)
{
    const char *test = getenv("FE8PC_TEST_PAL_SRC");

    if (sTestSrc != 0)
        return sTestSrc;
    if (test != NULL)
    {
        char *end;
        long a = strtol(test, &end, 0), b = *end == ':' ? strtol(end + 1, NULL, 0) : 0;
        long v = unit != NULL && UNIT_FACTION(unit) == FACTION_BLUE ? a : b;

        if (v >= 1 && v <= 0x100)
            return (int)v;
    }
    if (gRandoRunOn && Rando_IsPlayable(pid))
    {
        int src = Rando_RecruitSource(pid);

        if (src >= 1 && src <= 0x100)
            return src;
    }
    return pid;
}

/* The slot of src's vanilla row whose class is drawn with class jid's palette (another class), or -1. */
static int SamePaletteSlot(int src, int jid)
{
    int s;

    for (s = 0; s < 7; s++)
    {
        int j = sVanillaConfig[src - 1][s];

        if (j >= 1 && j <= JID_MAX && j != jid && ClassPalBanim(j) >= 0 && ClassPalBanim(j) == ClassPalBanim(jid))
            return s;
    }
    return -1;
}

/* The gAnimCharaPalIt value character src shows on class jid in a fight drawn with banim bid: its own set for
   the class (vanilla; 0 there: the class's generic colours are its look), nothing on Eirika's and Ephraim's
   own lord classes and on Myrrh's class for Myrrh (her look; the "Myrrh look" option changes it), else the
   battle cache's. A foreign class fighting with a borrowed FE8 animation (Armor Lord's lance: the General's
   or Ephraim's Master Lord's) shows the colours of that FE8 class. */
static int WantedSet(int src, int jid, int bid)
{
    int s;

    if (jid > JID_MAX && bid >= 0 && bid < FOREIGN_POOL)
    {
        int lance = Reforged_Fe7LanceClass(jid);

        if (lance == jid)
            return 0;
        jid = lance;
    }
    for (s = 0; s < 7; s++)
        if (sVanillaConfig[src - 1][s] == jid)
            return sVanillaIt[src - 1][s];
    if ((s = SamePaletteSlot(src, jid)) >= 0) /* a class drawn with the same palette as one of its own (the trainees'
                                                 two forms): that set, or (0) that class's generic look, Ross's */
        return sVanillaIt[src - 1][s];
    if (TwinOwnClass(src, jid) || (src == CHARACTER_MYRRH && jid == CLASS_MANAKETE_MYRRH))
        return 0;
    if (!sHasGen[jid])
        return 0;
    return CacheSet(src, jid);
}

/* include/bmunit.h RANDO_CHARA_ENSURE (src/banim-ekrbattleintro.c, src/banim-ekrmain.c, src/classchg-sel.c):
   before the game looks up unit pid0 + 1's set for class jid (a fight drawn with banim bid), version 15 puts the
   set it should show in the unit's row (gAnimCharaPalConfig / gAnimCharaPalIt): an empty entry, else one of the
   battle cache's (the least recently used), else the last (a vanilla set it held comes back from the vanilla
   row when asked for again). Nothing at all when version 15 is off. */
void Reforged_RandoCharaEnsure(int valid, int pid0, int jid, int bid, const void *unit)
{
    int src, want, s, slot = -1, lru = -1;

    if (!valid || pid0 < 0 || pid0 > 0xFF || jid < 1 || jid >= CLASS_SLOTS || !AllOn())
        return;
    KeepVanilla();
    BuildAll();
    src = ColourSource(pid0 + 1, (const struct Unit *)unit);
    want = WantedSet(src, jid, bid);
    for (s = 0; s < 7; s++)
        if (gAnimCharaPalConfig[pid0][s] == jid)
        {
            if (gAnimCharaPalIt[pid0][s] != want)
            {
                gAnimCharaPalIt[pid0][s] = (u8)want;
                if (want == 0)
                    gAnimCharaPalConfig[pid0][s] = 0;
            }
            return;
        }
    if (want == 0)
        return;
    for (s = 0; s < 7 && slot < 0; s++)
        if (gAnimCharaPalConfig[pid0][s] == 0)
            slot = s;
    for (s = 0; s < 7; s++) /* the battle cache's entry used longest ago */
    {
        int it = gAnimCharaPalIt[pid0][s];

        if (it > SYNTH_BASE && it <= SYNTH_BASE + sSynthCount && !sSynth[it - SYNTH_BASE - 1].fixed &&
            (slot < 0 || gAnimCharaPalConfig[pid0][slot] != 0) &&
            (lru < 0 || sSynth[it - SYNTH_BASE - 1].tick < sSynth[gAnimCharaPalIt[pid0][lru] - SYNTH_BASE - 1].tick))
            lru = s;
    }
    if (slot < 0)
        slot = lru >= 0 ? lru : 6;
    gAnimCharaPalConfig[pid0][slot] = (u8)jid;
    gAnimCharaPalIt[pid0][slot] = (u8)want;
}

/* ------------------------------------------------------------------ the apply */

/* The vanilla set of pid best used as the colour source for class dstJid: one of the
   same tier, else the one of the character's default class, else the first; the slot
   (0-6), or -1 when pid has none. */
static int SourceSlot(int pid, int dstJid)
{
    const struct CharacterData *vc = Rando_VanillaChar(pid);
    int s, best = -1, score = -1;

    for (s = 0; s < 7; s++)
    {
        int j = sVanillaConfig[pid - 1][s], sc;

        if (j < 1 || j > JID_MAX || sVanillaIt[pid - 1][s] == 0 || !sHasGen[j])
            continue;
        sc = 0;
        if (Rando_ClassIsPromoted(j) == Rando_ClassIsPromoted(dstJid))
            sc += 2;
        if (vc != NULL && j == vc->defaultClass)
            sc += 1;
        if (sc > score)
        {
            score = sc;
            best = s;
        }
    }
    return best;
}

/* The classes a unit of class jid can be in: the class, its promotions, or the class
   it promotes from (a definition giving the unpromoted form). */
static int Family(int jid, int female, u8 out[4])
{
    int n = 0;

    out[n++] = (u8)jid;
    if (Rando_ClassIsPromoted(jid))
    {
        int b = Rando_ClassBase(jid);

        if (b != 0)
            out[n++] = (u8)b;
    }
    else
        n += Rando_ClassPromotionsFor(jid, female, out + n);
    return n;
}

void RandoPalettes_Apply(void)
{
    int pid;

    KeepVanilla();
    memcpy(gAnimCharaPalConfig, sVanillaConfig, sizeof(sVanillaConfig));
    memcpy(gAnimCharaPalIt, sVanillaIt, sizeof(sVanillaIt));
    sSynthCount = 0;
    memset(sBuildFailed, 0, sizeof(sBuildFailed));
    if (!gRandoRunOn || !Rando_Opt(RFO_RANDO_PALETTES))
        return;
    BuildInfo();
    if (gRandoRun.version >= RANDO_VERSION_ALLCOLOURS)
    {
        /* version 15: a playable slot filled by another character shows that character's own sets; every
           other class's set is built when a battle shows it (Reforged_RandoCharaEnsure) */
        for (pid = 1; pid <= 0x100; pid++)
        {
            int src = Rando_RecruitSource(pid);

            if (!Rando_IsPlayable(pid) || src < 1 || src > 0x100 || src == pid)
                continue;
            memcpy(gAnimCharaPalConfig[pid - 1], sVanillaConfig[src - 1], 7);
            memcpy(gAnimCharaPalIt[pid - 1], sVanillaIt[src - 1], 7);
        }
        if (getenv("FE8PC_RANDO_PALDUMP") != NULL)
            RandoPalettes_Dump();
        return;
    }
    for (pid = 1; pid <= 0x100; pid++)
    {
        const struct CharacterData *c = Rando_Char(pid);
        const struct CharacterData *vc = Rando_VanillaChar(pid);
        int src = Rando_RecruitSource(pid);
        u8 fam[4];
        int n, i, slot = 0;

        if (!Rando_IsPlayable(pid) || c == NULL || vc == NULL || src < 1 || src > 0x100)
            continue;
        if (src == pid && c->defaultClass == vc->defaultClass && !Rando_Opt(RFO_RANDO_PROMOTIONS))
            continue; /* the character's own class: its vanilla sets (with Random promotions its
                         family is rebuilt: its own sets are kept, the new promotions added) */
        n = Family(c->defaultClass, (c->attributes & CA_FEMALE) != 0, fam);
        memset(gAnimCharaPalConfig[pid - 1], 0, 7);
        memset(gAnimCharaPalIt[pid - 1], 0, 7);
        for (i = 0; i < n && slot < 7; i++)
        {
            int s, it = 0, own = 0;

            if (fam[i] < 1 || fam[i] > JID_MAX)
                continue; /* a foreign class (version 12-14): its generic colours, as PLAYING.md says */
            for (s = 0; s < 7 && !own; s++)
                if (sVanillaConfig[src - 1][s] == fam[i])
                {
                    own = 1; /* the source's own set for this class (0: generic, its look) */
                    it = sVanillaIt[src - 1][s];
                }
            if (!own && sSynthCount < SYNTH_MAX && (s = SourceSlot(src, fam[i])) >= 0 &&
                ((sSynth[sSynthCount].monster = 0,
                  BuildSet(sVanillaIt[src - 1][s] - 1, sVanillaConfig[src - 1][s], fam[i], sSynth[sSynthCount].raw)) ||
                 (gRandoRun.version >= RANDO_VERSION_MONSTERCOLOUR &&
                  (sSynth[sSynthCount].monster = 1,
                   BuildMonsterSet(sVanillaIt[src - 1][s] - 1, sVanillaConfig[src - 1][s], fam[i],
                       sSynth[sSynthCount].raw)))))
            {
                PackLiteral(sSynth[sSynthCount].raw, sSynth[sSynthCount].lz);
                sSynth[sSynthCount].fixed = 1;
                it = SYNTH_BASE + sSynthCount + 1;
                sSynthCount++;
            }
            if (it == 0)
                continue; /* the class's generic colours */
            gAnimCharaPalConfig[pid - 1][slot] = fam[i];
            gAnimCharaPalIt[pid - 1][slot] = (u8)it;
            slot++;
        }
    }
    if (getenv("FE8PC_RANDO_PALDUMP") != NULL && gRandoRunOn)
        RandoPalettes_Dump(); /* the run's sets, for offline checks */
}

/* The creature of a banim: its abbreviation up to the '_' ("mcd" of "mcd_ax1"). */
static int SameCreature(int a, int b)
{
    int i;

    if (a < 0 || b < 0 || a >= BANIM_SLOTS || b >= BANIM_SLOTS)
        return 0;
    for (i = 0; i < (int)sizeof(banim_data[a].abbr); i++)
    {
        if (banim_data[a].abbr[i] != banim_data[b].abbr[i])
            return 0;
        if (banim_data[a].abbr[i] == '_' || banim_data[a].abbr[i] == '\0')
            return 1;
    }
    return 1;
}

/* Battle intro (src/banim-ekrbattleintro.c RANDO_UNIQUE_PAL_FITS): a version 7 monster set
   colours only its monster's own sprites. A class can borrow another one for a weapon (a
   Maelduin's Hatchet is drawn with the Great Knight's grkm_ax1): that one keeps its own
   palette, as the original shows it. Every other set fits (0: drop it). */
int Reforged_RandoCharaPalFits(int idx, int bid, int jid)
{
    if (idx < SYNTH_BASE || idx >= SYNTH_BASE + sSynthCount || !sSynth[idx - SYNTH_BASE].monster)
        return 1;
    return SameCreature(bid, ClassPalBanim(jid));
}

char *Reforged_RandoCharaPal(int idx)
{
    static int sWarned;

    if (idx >= 0 && idx < CBAPT_COUNT)
        return character_battle_animation_palette_table[idx].pal;
    if (idx >= SYNTH_BASE && idx < SYNTH_BASE + sSynthCount)
        return (char *)sSynth[idx - SYNTH_BASE].lz;
    if (!sWarned)
    {
        sWarned = 1;
        PortLog("[rando] palette set %d is not built: the first set shown instead\n", idx);
    }
    return character_battle_animation_palette_table[0].pal;
}

/* ------------------------------------------------------------------ self-test, dump */

int RandoPalettes_SelfTest(void)
{
    static u8 buf[0x400];
    int fails = 0, pid, s;

    KeepVanilla();
    if ((!gRandoRunOn || !Rando_Opt(RFO_RANDO_PALETTES)) &&
        (sSynthCount != 0 || memcmp(gAnimCharaPalConfig, sVanillaConfig, sizeof(sVanillaConfig)) != 0 ||
         memcmp(gAnimCharaPalIt, sVanillaIt, sizeof(sVanillaIt)) != 0))
    {
        PortLog("[rando] check: palettes: not the vanilla sets with no run (or Keep colours off)\n");
        fails++;
    }
    for (pid = 1; pid <= 0x100; pid++)
        for (s = 0; s < 7; s++)
        {
            int j = gAnimCharaPalConfig[pid - 1][s], it = gAnimCharaPalIt[pid - 1][s];

            if (j == 0 || it == 0)
                continue; /* it 0: the class's generic colours (vanilla has some) */
            if ((it - 1 >= CBAPT_COUNT && it - 1 < SYNTH_BASE) || it - 1 >= SYNTH_BASE + sSynthCount)
            {
                PortLog("[rando] check: palettes: pid 0x%02X slot %d: set %d not valid\n", pid, s, it);
                fails++;
                continue;
            }
            if (Unpack(Reforged_RandoCharaPal(it - 1), buf, sizeof(buf)) < GEN_BYTES)
            {
                PortLog("[rando] check: palettes: pid 0x%02X slot %d: set %d does not unpack\n", pid, s, it);
                fails++;
            }
        }
    /* version 7's monster sets: every monster class in sMonsterBody gets one from a character
       with colours (Seth's own set), changed on its body ramp only, the same twice */
    {
        static const short sMonsterClasses[] = {
            CLASS_REVENANT, CLASS_ENTOUMBED, CLASS_BONEWALKER, CLASS_BONEWALKER_BOW, CLASS_WIGHT, CLASS_WIGHT_BOW,
            CLASS_BAEL, CLASS_ELDER_BAEL, CLASS_CYCLOPS, CLASS_MAUTHEDOOG, CLASS_GWYLLGI, CLASS_TARVOS,
            CLASS_MAELDUIN, CLASS_MOGALL, CLASS_ARCH_MOGALL, CLASS_GORGON, CLASS_GARGOYLE, CLASS_DEATHGOYLE,
        };
        u8 a[SET_BYTES], b[SET_BYTES];
        int k, slot, i, p;

        BuildInfo();
        slot = SourceSlot(CHARACTER_SETH, CLASS_PALADIN);
        for (k = 0; slot >= 0 && k < (int)(sizeof(sMonsterClasses) / sizeof(sMonsterClasses[0])); k++)
        {
            int jid = sMonsterClasses[k], src = sVanillaIt[CHARACTER_SETH - 1][slot] - 1;
            int srcJid = sVanillaConfig[CHARACTER_SETH - 1][slot];
            u16 mask = MonsterBodyMask(jid);

            if (mask == 0 || !BuildMonsterSet(src, srcJid, jid, a) || !BuildMonsterSet(src, srcJid, jid, b) ||
                memcmp(a, b, sizeof(a)) != 0)
            {
                PortLog("[rando] check: palettes: monster class 0x%02X has no set (or not the same twice)\n", jid);
                fails++;
                continue;
            }
            for (p = 0; p < PALS; p++)
                for (i = 0; i < 16; i++)
                    if (!(mask & (1 << i)) && Color(a + p * 32, i) != Color(sGen[jid] + (p < 4 ? p : 0) * 32, i))
                    {
                        PortLog("[rando] check: palettes: monster class 0x%02X colour %d changed off its body\n", jid, i);
                        fails++;
                        p = PALS;
                        break;
                    }
        }
    }
    /* a monster set stays on its monster (a Maelduin's Hatchet: the Great Knight banim 0x50) */
    if (!SameCreature(ClassPalBanim(CLASS_MAELDUIN), ClassPalBanim(CLASS_TARVOS)) ||
        SameCreature(ClassPalBanim(CLASS_MAELDUIN), 0x50))
    {
        PortLog("[rando] check: palettes: a monster set would colour another creature's sprite\n");
        fails++;
    }
    for (s = 0; s < sSynthCount; s++)
    {
        Unpack(sSynth[s].lz, buf, sizeof(buf));
        if (memcmp(buf, sSynth[s].raw, SET_BYTES) != 0)
        {
            PortLog("[rando] check: palettes: built set %d does not round-trip\n", s);
            fails++;
        }
    }
    return fails;
}

int RandoPalettes_Built(void)
{
    return sSynthCount;
}

/* The classes no unit is ever shown in with a character's colours: the ballistae and the tent (no battle
   animation of a unit), Draco Zombie and the Demon King (story bosses the randomizer never gives: "never
   Draco Zombie / Demon King"), the Gorgon eggs. */
static int NeverCharacterClass(int jid)
{
    return (jid >= CLASS_BLST_REGULAR_USED && jid <= CLASS_BLST_KILLER_EMPTY) || jid == CLASS_TENT ||
           jid == CLASS_DRACO_ZOMBIE || jid == CLASS_DEMON_KING || jid == CLASS_GORGONEGG || jid == CLASS_GORGONEGG2;
}

static void DumpRaw(FILE *f, const u8 *raw, int n)
{
    int i;

    fprintf(f, " %d ", n);
    for (i = 0; i < n; i++)
        fprintf(f, "%02X", raw[i]);
    fprintf(f, "\n");
}

static int sCheckLogged, sLooks[3]; /* sLooks: outline, skin, twin-hair failures */

/* A failed check's line (the first 60; every failure is counted). */
static int CheckFailAt(const char *what, int src, int jid, int idx)
{
    if (sCheckLogged++ < 60)
        PortLog("[rando] check: palettes: pid 0x%02X on class 0x%02X: %s (index %d)\n", src, jid, what, idx);
    return 1;
}

/* The looks of the set BuildAllSet just made (src on jid, out): the outline (index 15) stays the class's;
   a skin tone of the class takes only a skin tone; Eirika and Ephraim change only the class's hair
   (TwinTarget). */
static int CheckLooks(int src, int jid, const u8 *out)
{
    int fails = 0, p, i;
    u16 allowed = TwinTarget(jid);

    if (!sLastRoles)
        return 0; /* a monster set: its body tinted */
    for (p = 0; p < PALS; p++)
        if (Color(out + p * 32, 15) != Color(sGen[jid] + (p < 4 ? p : 0) * 32, 15))
        {
            fails += CheckFailAt("the outline changed", src, jid, 15), sLooks[0]++;
            break;
        }
    for (i = 1; i < 15; i++)
    {
        double h, sa, l;

        if (sLastMap[i] < 0 || !SkinIndex(jid, i))
            continue;
        ToHsl(Color(sLastSet, sLastMap[i]), &h, &sa, &l);
        if (!SkinLike(h, sa, l))
            fails += CheckFailAt("a skin tone takes a colour that is no skin tone", src, jid, i), sLooks[1]++;
    }
    if (TwinLord(src, 0) != 0)
        for (i = 1; i < 16; i++)
            for (p = 0; p < PALS; p++)
                if (Color(out + p * 32, i) != Color(sGen[jid] + (p < 4 ? p : 0) * 32, i) && !((allowed >> i) & 1))
                {
                    fails += CheckFailAt("Eirika's or Ephraim's hair off the class's hair ramp", src, jid, i), sLooks[2]++;
                    break;
                }
                else if (p > 0 && Color(out + p * 32, i) != Color(sGen[jid] + (p < 4 ? p : 0) * 32, i) &&
                         (((FactionTeamMask(jid) | sATeam[jid]) >> i) & 1))
                {
                    fails += CheckFailAt("Eirika's or Ephraim's hair on another faction's team colour", src, jid, i),
                        sLooks[2]++;
                    break;
                }
    return fails;
}

/* The unit row of pid0 (shown with src's colours) after Reforged_RandoCharaEnsure for class jid: one entry for
   jid, holding the set WantedSet gives (none when that is 0), no class twice, every cache entry pointing at a
   slot that holds src's set for that class, every vanilla entry one of src's vanilla row. */
static int sRowStrict = 1; /* 0: the row was given made-up vanilla entries */

static int RowCheck(int pid0, int src, int jid, int *checks)
{
    int want = jid != 0 ? WantedSet(src, jid, -1) : 0, s, t, n = 0, fails = 0; /* jid 0: the entries only */

    for (s = 0; s < 7; s++)
    {
        int c = gAnimCharaPalConfig[pid0][s], it = gAnimCharaPalIt[pid0][s];

        if (jid != 0 && c == jid && (want != 0 || it != 0))
        {
            n++;
            if (it != want)
                fails += CheckFailAt("rows: the entry is not the set wanted", src, jid, s);
        }
        for (t = s + 1; t < 7; t++)
            if (c != 0 && gAnimCharaPalConfig[pid0][t] == c)
                fails += CheckFailAt("rows: a class twice in the row", src, c, t);
        if (c != 0 && it > SYNTH_BASE)
        {
            int k = it - SYNTH_BASE - 1;

            if (k >= sSynthCount || sSynth[k].fixed || sSynth[k].jid != c || sSynth[k].src != src)
                fails += CheckFailAt("rows: a cache entry points at another set", src, c, s);
        }
        else if (c != 0 && it != 0 && sRowStrict)
        {
            int v, ok = 0;

            for (v = 0; v < 7; v++)
                ok |= sVanillaConfig[src - 1][v] == c && sVanillaIt[src - 1][v] == it;
            if (!ok)
                fails += CheckFailAt("rows: a vanilla entry that is not the character's", src, c, s);
        }
    }
    if (jid != 0 && n != (want != 0))
        fails += CheckFailAt("rows: the class's entry missing or twice", src, jid, n);
    (*checks)++;
    return fails;
}

/* Reforged_RandoCharaEnsure's row bookkeeping (RANDO_CHARA_ENSURE): a character's own row asked for every
   class three times (empty entries first, then the least recently used cache entries; its vanilla entries
   come back), the cache churned in between (ForgetSlot must clear the row entries of every reused slot), a
   row of seven non-cache entries (the last is taken, its set comes back when asked again), and a recruitment
   fill's row (the source's vanilla row copied, as RandoPalettes_Apply does). */
static int EnsureSelfTest(void)
{
    static u8 saveConfig[0x100][7], saveIt[0x100][7];
    int fails = 0, checks = 0, calls = 0, round, i, n = 0, src, pid, s, fill = 0x40;
    u8 list[CLASS_SLOTS];

    memcpy(saveConfig, gAnimCharaPalConfig, sizeof(saveConfig));
    memcpy(saveIt, gAnimCharaPalIt, sizeof(saveIt));
    for (i = 1; i < CLASS_SLOTS; i++)
        if (sHasGen[i] && !NeverCharacterClass(i))
            list[n++] = (u8)i;
    memcpy(gAnimCharaPalConfig, sVanillaConfig, sizeof(sVanillaConfig));
    memcpy(gAnimCharaPalIt, sVanillaIt, sizeof(sVanillaIt));
    sSynthCount = 0;
    memset(sBuildFailed, 0, sizeof(sBuildFailed));
    sTestAllOn = 1;
    src = sTestSrc = CHARACTER_SETH;
    for (round = 0; round < 3; round++)
    {
        for (i = 0; i < n; i++)
        {
            int jid = list[round == 1 ? n - 1 - i : i];

            Reforged_RandoCharaEnsure(1, src - 1, jid, -1, NULL);
            calls++;
            fails += RowCheck(src - 1, src, jid, &checks);
        }
        if (round == 0) /* churn the cache: other characters' sets fill every slot (Seth's are reused) */
            for (pid = 1; pid <= 0x40; pid++)
                for (i = 0; i < n; i += 3)
                    if (pid != src)
                        CacheSet(pid, list[i]);
        for (pid = 0; pid < 0x100; pid++) /* no entry of any row points at a slot holding another class's set */
            for (s = 0; s < 7; s++)
            {
                int it = gAnimCharaPalIt[pid][s];

                if (gAnimCharaPalConfig[pid][s] != 0 && it > SYNTH_BASE &&
                    (it - SYNTH_BASE - 1 >= sSynthCount || sSynth[it - SYNTH_BASE - 1].jid != gAnimCharaPalConfig[pid][s]))
                    fails += CheckFailAt("rows: an entry left pointing at a reused slot", pid + 1, gAnimCharaPalConfig[pid][s], s);
            }
        if (round == 0)
            fails += RowCheck(src - 1, src, 0, &checks); /* Seth's entries all point at his sets still */
    }
    /* seven entries none of the cache's: the last one is taken, and its class's set comes back */
    for (s = 0; s < 7; s++)
    {
        gAnimCharaPalConfig[src - 1][s] = list[s];
        gAnimCharaPalIt[src - 1][s] = (u8)WantedSet(src, list[s], -1);
        if (gAnimCharaPalIt[src - 1][s] > SYNTH_BASE)
            gAnimCharaPalIt[src - 1][s] = 1 + s; /* a vanilla index */
    }
    sRowStrict = 0;
    Reforged_RandoCharaEnsure(1, src - 1, list[10], -1, NULL);
    calls++;
    if (gAnimCharaPalConfig[src - 1][6] != list[10] && WantedSet(src, list[10], -1) != 0)
        fails += CheckFailAt("rows: a full row did not take its last entry", src, list[10], 6);
    fails += RowCheck(src - 1, src, list[10], &checks);
    Reforged_RandoCharaEnsure(1, src - 1, list[6], -1, NULL);
    calls++;
    fails += RowCheck(src - 1, src, list[6], &checks);
    sRowStrict = 1;
    /* a recruitment fill: slot `fill` shows Eirika (her vanilla row copied); her own classes keep their entries */
    src = sTestSrc = CHARACTER_EIRIKA;
    memcpy(gAnimCharaPalConfig[fill - 1], sVanillaConfig[src - 1], 7);
    memcpy(gAnimCharaPalIt[fill - 1], sVanillaIt[src - 1], 7);
    for (i = 0; i < n; i++)
    {
        Reforged_RandoCharaEnsure(1, fill - 1, list[i], -1, NULL);
        calls++;
        fails += RowCheck(fill - 1, src, list[i], &checks);
    }
    for (s = 0; s < 7; s++)
        if (sVanillaConfig[src - 1][s] != 0 && sVanillaIt[src - 1][s] != 0)
        {
            Reforged_RandoCharaEnsure(1, fill - 1, sVanillaConfig[src - 1][s], -1, NULL);
            calls++;
            fails += RowCheck(fill - 1, src, sVanillaConfig[src - 1][s], &checks);
        }
    sTestAllOn = sTestSrc = 0;
    if (!AllOn()) /* version 15 off: the row stays as it is */
    {
        u8 c[7], it[7];

        memset(gAnimCharaPalConfig[fill - 1], 0, 7);
        memset(gAnimCharaPalIt[fill - 1], 0, 7);
        memcpy(c, gAnimCharaPalConfig[fill - 1], 7);
        memcpy(it, gAnimCharaPalIt[fill - 1], 7);
        Reforged_RandoCharaEnsure(1, fill - 1, list[n / 2], -1, NULL);
        calls++;
        checks++;
        if (memcmp(c, gAnimCharaPalConfig[fill - 1], 7) != 0 || memcmp(it, gAnimCharaPalIt[fill - 1], 7) != 0)
            fails += CheckFailAt("rows: changed with version 15 off", fill, list[n / 2], 0);
    }
    memcpy(gAnimCharaPalConfig, saveConfig, sizeof(saveConfig));
    memcpy(gAnimCharaPalIt, saveIt, sizeof(saveIt));
    sSynthCount = 0;
    memset(sBuildFailed, 0, sizeof(sBuildFailed));
    PortLog("[rando] self-test: keep colours rows: %d Ensure calls over %d classes, %d row checks, %d failures\n", calls, n,
        checks, fails);
    return fails;
}

/* Version 15's check: every character with colours of its own (a vanilla set, or Eirika and Ephraim) on every
   class with a battle palette (FE8's and every loaded foreign one, but NeverCharacterClass's) has a set: its
   own, or one built through the battle cache (CacheSet, as a battle asks: all of them one after another, so
   the 126 slots are reused many times). Each built set must come back from its index (at most 0xFE) as built
   now, still be there after the next two (a fight's other sets) and differ from the class's generic palettes;
   the same set twice must be the same. FE8PC_RANDO_PALDUMP=<file>: every pair also to <file>.all (for the
   contact sheets). The live state is put back after (RandoPalettes_Apply). */
int RandoPalettes_AllSelfTest(void)
{
    static u8 buf[0x400];
    u8 again[SET_BYTES];
    const char *path = getenv("FE8PC_RANDO_PALDUMP");
    char allPath[1024];
    FILE *f = NULL;
    int fails = 0, chars = 0, classes = 0, foreign = 0, own = 0, built = 0, monster = 0, derived = 0, maxIdx = 0;
    int reused = 0, src, jid, k, prev[2] = { 0, 0 };
    u32 tick0;

    KeepVanilla();
    BuildAll();
    sCheckLogged = 0;
    memset(sLooks, 0, sizeof(sLooks));
    fails += EnsureSelfTest();
    sSynthCount = 0;
    memset(sBuildFailed, 0, sizeof(sBuildFailed));
    tick0 = sTick;
    if (path != NULL)
    {
        snprintf(allPath, sizeof(allPath), "%s.all", path);
        f = fopen(allPath, "w");
    }
    for (jid = 1; jid < CLASS_SLOTS; jid++)
        if (sHasGen[jid] && !NeverCharacterClass(jid))
        {
            classes++;
            foreign += jid > JID_MAX;
            derived += sAKind[jid] == ROLES_DERIVED;
            if (sAKind[jid] == ROLES_NONE)
            {
                PortLog("[rando] check: palettes: class 0x%02X has a battle palette but no roles\n", jid);
                fails++;
            }
            if (f != NULL)
            {
                fprintf(f, "ALLCLASS %d %d %d %04X %04X %s %04X\n", jid, sAKind[jid], sARef[jid], sAPersonal[jid],
                    sATeam[jid], banim_data[ClassPalBanim(jid)].abbr, TwinTarget(jid));
                fprintf(f, "ALLGEN %d", jid);
                DumpRaw(f, sGen[jid], GEN_BYTES);
            }
        }
    for (src = 1; src <= 0x100; src++)
    {
        u8 set[SET_BYTES];
        int sj;
        u16 per, team;

        if (!SourceColours(src, CLASS_PALADIN, set, &sj, &per, &team))
            continue; /* no colours of its own (the generic soldiers) */
        chars++;
        for (jid = 1; jid < CLASS_SLOTS; jid++)
        {
            int it, idx, p, differs = 0;

            if (!sHasGen[jid] || NeverCharacterClass(jid))
                continue;
            it = WantedSet(src, jid, -1);
            for (k = 0; k < 7 && it == 0; k++)
                if (sVanillaConfig[src - 1][k] == jid)
                    break;
            if (it != 0 && it <= CBAPT_COUNT)
            {
                own++; /* its vanilla set */
                continue;
            }
            if (it == 0)
            {
                if ((k < 7 && sVanillaConfig[src - 1][k] == jid) || TwinOwnClass(src, jid) || SamePaletteSlot(src, jid) >= 0 ||
                    (src == CHARACTER_MYRRH && jid == CLASS_MANAKETE_MYRRH))
                {
                    own++; /* its own look is the class's generic one */
                    continue;
                }
                PortLog("[rando] check: palettes: pid 0x%02X on class 0x%02X: no set (generic colours)\n", src, jid);
                fails++;
                continue;
            }
            idx = it - 1;
            if (idx < SYNTH_BASE || idx > 0xFE || idx >= SYNTH_BASE + sSynthCount)
            {
                PortLog("[rando] check: palettes: pid 0x%02X on class 0x%02X: index 0x%X out of range\n", src, jid, idx);
                fails++;
                continue;
            }
            maxIdx = idx > maxIdx ? idx : maxIdx;
            built++;
            monster += sSynth[idx - SYNTH_BASE].monster;
            if (Unpack(Reforged_RandoCharaPal(idx), buf, sizeof(buf)) != SET_BYTES ||
                BuildAllSet(src, jid, again) == 0 || memcmp(buf, again, SET_BYTES) != 0)
            {
                PortLog("[rando] check: palettes: pid 0x%02X on class 0x%02X: set %d is not the one built\n", src, jid, idx);
                fails++;
            }
            for (p = 0; p < 2; p++)
                for (k = 1; k < 16; k++)
                    differs |= Color(buf + p * 32, k) != Color(sGen[jid] + p * 32, k);
            if (!differs)
            {
                PortLog("[rando] check: palettes: pid 0x%02X on class 0x%02X: the class's generic colours\n", src, jid);
                fails++;
            }
            fails += CheckLooks(src, jid, again);
            for (p = 0; p < 2; p++)
            {
                int pk = prev[p] & 0xFF; /* prev: slot | src << 8 | jid << 16 */

                if (prev[p] != 0 && (sSynth[pk].src != ((prev[p] >> 8) & 0xFF) || sSynth[pk].jid != (prev[p] >> 16)))
                {
                    PortLog("[rando] check: palettes: pid 0x%02X on class 0x%02X: a set of the same fight was reused\n",
                        src, jid);
                    fails++;
                }
            }
            prev[0] = prev[1];
            prev[1] = (idx - SYNTH_BASE) | (sSynth[idx - SYNTH_BASE].src << 8) | (sSynth[idx - SYNTH_BASE].jid << 16);
            if (f != NULL)
            {
                fprintf(f, "ALL %d %d %d %d", src, jid, idx, sSynth[idx - SYNTH_BASE].monster);
                DumpRaw(f, buf, SET_BYTES);
            }
        }
    }
    reused = built - sSynthCount; /* every set built past the 126th reused a slot */
    (void)tick0;
    if (f != NULL)
        fclose(f);
    if (chars < 40 || classes < 90)
    {
        PortLog("[rando] check: palettes: only %d characters and %d classes checked\n", chars, classes);
        fails++;
    }
    PortLog("[rando] self-test: keep colours: %d characters x %d classes (%d foreign, %d with derived roles): %d own "
            "sets, %d built (%d monster), cache %d of %d slots, highest index 0x%02X, %d reuses, %d failures\n",
        chars, classes, foreign, derived, own, built, monster, sSynthCount, SYNTH_MAX, maxIdx, reused, fails);
    PortLog("[rando] self-test: keep colours looks: %d outline, %d skin, %d hair-off-hair failures\n", sLooks[0], sLooks[1],
        sLooks[2]);
    RandoPalettes_Apply(); /* the live state back */
    return fails;
}

static void DumpBytes(FILE *f, const void *src)
{
    static u8 buf[0x400];
    int size = Unpack(src, buf, sizeof(buf)), i;

    fprintf(f, " %d ", size);
    for (i = 0; i < size; i++)
        fprintf(f, "%02X", buf[i]);
    fprintf(f, "\n");
}

void RandoPalettes_Dump(void)
{
    const char *path = getenv("FE8PC_RANDO_PALDUMP");
    FILE *f;
    int jid, pid, i;

    if (path == NULL || (f = fopen(path, "w")) == NULL)
        return;
    KeepVanilla();
    BuildInfo();
    for (jid = 1; jid <= JID_MAX; jid++)
        if (sHasGen[jid])
            fprintf(f, "ROLES %d %d %d %04X %04X %04X %s\n", jid, ClassPalBanim(jid), sHasRoles[jid], sPersonal[jid],
                sTeam[jid], GenTeamMask(jid), banim_data[ClassPalBanim(jid)].abbr);
    for (jid = 1; jid <= JID_MAX; jid++)
    {
        int b = ClassPalBanim(jid);

        if (b < 0)
            continue;
        fprintf(f, "CLASS %d 0 %d", jid, b);
        DumpBytes(f, banim_data[b].pal);
    }
    for (pid = 1; pid <= 0x100; pid++)
        for (i = 0; i < 7; i++)
            if (gAnimCharaPalConfig[pid - 1][i] != 0 && gAnimCharaPalIt[pid - 1][i] != 0)
            {
                int idx = gAnimCharaPalIt[pid - 1][i] - 1;

                fprintf(f, "CHAR %d %d %d %d", pid, i, gAnimCharaPalConfig[pid - 1][i], idx);
                DumpBytes(f, Reforged_RandoCharaPal(idx));
            }
    fclose(f);
    PortLog("[rando] palettes dumped to %s\n", path);
}

#endif /* PORT_PLAYED_BUILD */
