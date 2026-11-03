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
 * Eirika and Ephraim have no set of their own (the lord classes' generic palettes
 * are theirs): randomized, they get the new class's generic colours.
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
#define BANIM_COUNT  201 /* src/banim_data.c */
#define CBAPT_COUNT  108 /* src/banim_pal_chara.c */
#define SYNTH_BASE   0x80
#define SYNTH_MAX    126 /* gAnimCharaPalIt is a u8: SYNTH_BASE + k + 1 <= 0xFF */
#define PALS         5   /* a character set: player, enemy, NPC, 4th, independent */
#define SET_BYTES    (PALS * 32)
#define GEN_BYTES    (4 * 32)
#define LZ_BYTES     (4 + (SET_BYTES / 8) * 9)

struct SynthSet {
    u8 raw[SET_BYTES];
    u8 lz[LZ_BYTES];
};

static u8 sGen[JID_MAX + 1][GEN_BYTES]; /* each class's generic palettes */
static u16 sPersonal[JID_MAX + 1];      /* index masks (bit i: colour i) */
static u16 sTeam[JID_MAX + 1];
static u8 sHasGen[JID_MAX + 1];
static u8 sHasRoles[JID_MAX + 1];
static int sInfoBuilt;

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
    const struct ClassData *k = Rando_VanillaClass(jid);
    int anim;

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

static int Ramps(u16 mask, const u8 *ref, struct Ramp *out)
{
    u8 run[16];
    int count = 0, n = 0, i, j, k;

    for (i = 1; i <= 16; i++)
    {
        if (i < 16 && (mask & (1 << i)))
        {
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
static void MapKind(u16 srcMask, const u8 *srcSet, const u8 *srcGen, u16 dstMask, const u8 *dstGen, s8 map[16])
{
    struct Ramp sr[16], tr[16];
    int sn = Ramps(srcMask, srcSet, sr), tn = Ramps(dstMask, dstGen, tr), i, j, r;
    int sl[16][4], tl[4];

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

/* The character set for vanilla set `srcIdx` (of class srcJid) moved onto class
   dstJid, in out; 0 when nothing maps (then the class's generic colours stay). */
static int BuildSet(int srcIdx, int srcJid, int dstJid, u8 out[SET_BYTES])
{
    u8 set[SET_BYTES];
    s8 map[16];
    u16 per, team;
    int p, t, any = 0;

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
static int Family(int jid, u8 out[4])
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
        n += Rando_ClassPromotions(jid, out + n);
    return n;
}

void RandoPalettes_Apply(void)
{
    int pid;

    KeepVanilla();
    memcpy(gAnimCharaPalConfig, sVanillaConfig, sizeof(sVanillaConfig));
    memcpy(gAnimCharaPalIt, sVanillaIt, sizeof(sVanillaIt));
    sSynthCount = 0;
    if (!gRandoRunOn || !Rando_Opt(RFO_RANDO_PALETTES))
        return;
    BuildInfo();
    for (pid = 1; pid <= 0x100; pid++)
    {
        const struct CharacterData *c = Rando_Char(pid);
        const struct CharacterData *vc = Rando_VanillaChar(pid);
        int src = Rando_RecruitSource(pid);
        u8 fam[4];
        int n, i, slot = 0;

        if (!Rando_IsPlayable(pid) || c == NULL || vc == NULL || src < 1 || src > 0x100)
            continue;
        if (src == pid && c->defaultClass == vc->defaultClass)
            continue; /* the character's own class: its vanilla sets */
        n = Family(c->defaultClass, fam);
        memset(gAnimCharaPalConfig[pid - 1], 0, 7);
        memset(gAnimCharaPalIt[pid - 1], 0, 7);
        for (i = 0; i < n && slot < 7; i++)
        {
            int s, it = 0, own = 0;

            for (s = 0; s < 7 && !own; s++)
                if (sVanillaConfig[src - 1][s] == fam[i])
                {
                    own = 1; /* the source's own set for this class (0: generic, its look) */
                    it = sVanillaIt[src - 1][s];
                }
            if (!own && sSynthCount < SYNTH_MAX && (s = SourceSlot(src, fam[i])) >= 0 &&
                BuildSet(sVanillaIt[src - 1][s] - 1, sVanillaConfig[src - 1][s], fam[i], sSynth[sSynthCount].raw))
            {
                PackLiteral(sSynth[sSynthCount].raw, sSynth[sSynthCount].lz);
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
