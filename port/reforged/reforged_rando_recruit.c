#include "global.h"

/*
 * Reforged Options randomizer: recruitment, "Recruitment" (RFO_RANDO_RECRUIT_*;
 * port/reforged/reforged_rando.h has the map of the randomizer's files). After the
 * Universal FE Randomizer's (UFER, MIT; ideas only, no code). Played builds only;
 * empty in every test build.
 *
 * Who joins where is shuffled: each "slot" (a playable character id, with its join
 * point, its events, its level and its supports) is filled by another character of
 * the pool, whose identity it takes. The slot keeps its pid, so every event script,
 * talk and check that names it still works; what changes is who it is:
 *
 * - its name: every text the game decodes (src/msg.c GetStringFromIndex,
 *   RANDO_HOOK_TEXT) has each slot's name replaced by its fill's, as whole words,
 *   all at once (no chains), so the dialogue, menus and the unit's own name agree;
 * - its face: every portrait read (src/face.c GetPortraitData, RANDO_FACE) of a
 *   slot's portrait shows its fill's (talks, the stat screen, menus);
 * - its stats, from the fill (after this run's classes, bases and growths):
 *   growths, affinity, constitution, description, sex; class: the fill's own class
 *   ("Recruit class: Own"; a trainee takes its first class, then the tier is matched
 *   to the slot's: promoted with the fill's promotion or demoted to its base) or the
 *   slot's ("Slot"); bases: the fill's final stats autoleveled from the fill's level
 *   to the slot's with the fill's growths (+-10 levels for a promotion, the promotion
 *   bonuses added or taken off: "Autolevel") or the slot's final stats ("Slot"),
 *   kept within 0 (HP 1) and the class caps; weapon ranks moved to the new class;
 * - its battle colours (reforged_rando_palettes.c: the fill's sets, Rando_RecruitSource).
 *
 * The pool: the playable characters 0x02-0x22 but Ephraim, Myrrh and the summon
 * (Eirika and Ephraim never: the story's lords, and Myrrh as the classes step), the
 * thieves only with "Include thieves", Tethys only with "Include dancer". Slots
 * whose scripts need something are filled first, with fills whose class gives it:
 * Seth and Gilliam fight in melee, Artur and Neimi at range, Garcia attacks (the
 * scripted FIGHTs, as the classes step), Vanessa, Cormag, Syrene and Tana fly (they
 * appear where only fliers stand); with no such fill left the slot takes a fill in
 * the slot's own class of the run. Without "Cross-gender" men fill men's slots and
 * women women's (the pool is shuffled within each sex). One PRNG stream for the
 * whole shuffle (RANDO_SALT_RECRUIT); the classes step's choices and the fills'
 * data come first, so a seed always gives the same shuffle.
 */

#if PORT_PLAYED_BUILD

#include <stdio.h>
#include <string.h>

#include "bmunit.h"
#include "bmitem.h"
#include "functions.h"
#include "variables.h"
#include "constants/characters.h"
#include "constants/classes.h"

#include "port_hw.h"
#include "port_reforged.h"
#include "reforged_rando.h"

#define PID_MAX     0xFF
#define FACE_MAX    0x400
#define NAME_MAX    24
#define STAT_HP_MAX 60
#define STAT_MAX    31
#define STAT_LCK_MAX 30
#define RANDO_SALT_RECRUIT 911 /* UFER's */

/* src/msg.c (EWRAM_DATA, not static): the cached decoded message's id. */
extern int sActiveMsg;

static u8 sSource[PID_MAX + 1];   /* slot pid -> fill pid; 0: itself */
static u16 sFaceMap[FACE_MAX];    /* portrait id -> portrait id shown; 0: itself */
static char sNameFrom[0x24][NAME_MAX];
static char sNameTo[0x24][NAME_MAX];
static u8 sNameLen[0x24];
static int sNames;
static int sQuiet;

static int Min(int a, int b)
{
    return a < b ? a : b;
}

static int Clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

int Rando_RecruitSource(int pid)
{
    if (!gRandoRunOn || pid < 1 || pid > PID_MAX || sSource[pid] == 0)
        return pid;
    return sSource[pid];
}

static int InPool(int pid)
{
    if (pid < CHARACTER_SETH || pid > CHARACTER_TANA)
        return 0;
    if (pid == CHARACTER_EPHRAIM || pid == CHARACTER_MYRRH || pid == 0x1B)
        return 0;
    if ((pid == CHARACTER_COLM || pid == CHARACTER_RENNAC) && !Rando_Opt(RFO_RANDO_THIEVES))
        return 0;
    if (pid == CHARACTER_TETHYS && !Rando_Opt(RFO_RANDO_SPECIAL))
        return 0;
    return 1;
}

static int IsFemale(int pid)
{
    const struct CharacterData *vc = Rando_VanillaChar(pid);

    return vc != NULL && (vc->attributes & CA_FEMALE) != 0;
}

/* What a slot's scripts need of its class: 0 nothing, 1 the class rules (attack,
   melee, range: Rando_PlayerFits), 2 flying. */
static int SlotNeed(int pid)
{
    switch (pid)
    {
    case CHARACTER_SETH: case CHARACTER_GILLIAM: case CHARACTER_ARTUR: case CHARACTER_NEIMI: case CHARACTER_GARCIA:
        return 1;
    case CHARACTER_VANESSA: case CHARACTER_CORMAG: case CHARACTER_SYRENE: case CHARACTER_TANA:
        return 2;
    }
    return 0;
}

static int IsTrainee(int jid)
{
    return jid == CLASS_JOURNEYMAN || jid == CLASS_PUPIL || jid == CLASS_RECRUIT;
}

/* A class a unit can be (a battle animation; not an egg). */
static int UnitClass(int jid)
{
    const struct ClassData *k = Rando_VanillaClass(jid);

    return k != NULL && k->pBattleAnimDef != NULL && jid != CLASS_GORGONEGG && jid != CLASS_GORGONEGG2;
}

/* Each pid's class before the shuffle (the classes step's): the shuffle sets the
   slots' classes as it goes, so it never reads them back from the classes file. */
static u8 sClassBefore[0x24];

/* The class fill F has in slot S ("Own"), its tier matched to the slot's. */
static int OwnClassFor(int S, int F, u32 h)
{
    int c = sClassBefore[F];
    int slotTier = Rando_ClassIsPromoted(sClassBefore[S]);
    u8 promos[3];
    int n;

    if (F == S)
        return c;
    if (IsTrainee(c))
    {
        int a = gPromoJidLut[c][0], b = gPromoJidLut[c][1];

        c = (b != 0 && Rando_HashInt(h, 2)) ? b : a; /* Fighter / Pirate, Mage / Shaman, Knight F / Cavalier F */
    }
    if (slotTier && !Rando_ClassIsPromoted(c) &&
        (n = Rando_ClassPromotionsFor(c, Rando_VanillaChar(F) != NULL && (Rando_VanillaChar(F)->attributes & CA_FEMALE), promos)) > 0)
        c = promos[Rando_HashInt(h >> 8, n)];
    else if (!slotTier && Rando_ClassIsPromoted(c) && UnitClass(Rando_ClassBase(c)))
        c = Rando_ClassBase(c); /* not the Gorgon's egg */
    return c;
}

static int Fits(int S, int jid)
{
    switch (SlotNeed(S))
    {
    case 1:
        return Rando_PlayerFits(S, jid);
    case 2:
        return Rando_ClassIsFlier(jid);
    }
    return 1;
}

/* ------------------------------------------------------------------ the fill */

struct Stats {
    int v[6]; /* HP, Pow, Skl, Spd, Def, Res: final (class + personal) */
};

static void Finals(const struct CharacterData *c, const struct ClassData *k, struct Stats *s)
{
    s->v[0] = k->baseHP + c->baseHP;
    s->v[1] = k->basePow + c->basePow;
    s->v[2] = k->baseSkl + c->baseSkl;
    s->v[3] = k->baseSpd + c->baseSpd;
    s->v[4] = k->baseDef + c->baseDef;
    s->v[5] = k->baseRes + c->baseRes;
}

static int Eff(int level, int jid)
{
    return level + (Rando_ClassIsPromoted(jid) ? 10 : 0);
}

/* Writes fill F (its data as this run made it: live[]) into slot S, in class newJ. */
static void FillSlot(struct RandoRng *rng, const struct CharacterData *live, int S, int F, int newJ)
{
    struct CharacterData *c = Rando_Char(S);
    const struct CharacterData *s = &live[S - 1], *f = &live[F - 1];
    const struct ClassData *kF = Rando_Class(f->defaultClass), *kN = Rando_Class(newJ);
    const u8 g[6] = { f->growthHP, f->growthPow, f->growthSkl, f->growthSpd, f->growthDef, f->growthRes };
    const u8 pb[6] = { kN->promotionHp, kN->promotionPow, kN->promotionSkl, kN->promotionSpd, kN->promotionDef,
                       kN->promotionRes };
    const u8 pbOld[6] = { kF->promotionHp, kF->promotionPow, kF->promotionSkl, kF->promotionSpd, kF->promotionDef,
                          kF->promotionRes };
    const s8 nb[6] = { kN->baseHP, kN->basePow, kN->baseSkl, kN->baseSpd, kN->baseDef, kN->baseRes };
    const s8 nc[6] = { kN->maxHP, kN->maxPow, kN->maxSkl, kN->maxSpd, kN->maxDef, kN->maxRes };
    s8 *per[6];
    struct Stats st;
    int i, delta, promoted, demoted, con;
    u8 eff[8], ranks[8];

    promoted = Rando_ClassIsPromoted(newJ) && !Rando_ClassIsPromoted(f->defaultClass);
    demoted = !Rando_ClassIsPromoted(newJ) && Rando_ClassIsPromoted(f->defaultClass);
    if (Rando_Opt(RFO_RANDO_RECRUIT_STATS) == 1)
    {
        Finals(s, Rando_Class(s->defaultClass), &st); /* the slot's stats */
        c->baseLck = s->baseLck;
    }
    else
    {
        Finals(f, kF, &st);
        delta = Eff(s->baseLevel, newJ) - Eff(f->baseLevel, f->defaultClass);
        for (i = 0; i < 6; i++)
        {
            int grow = (g[i] * delta + (delta >= 0 ? 50 : -50)) / 100;

            st.v[i] += grow;
            if (promoted)
                st.v[i] += pb[i];
            if (demoted)
                st.v[i] -= pbOld[i];
        }
        c->baseLck = (s8)Clamp(f->baseLck + (f->growthLck * delta + (delta >= 0 ? 50 : -50)) / 100, 0, STAT_LCK_MAX);
    }
    per[0] = &c->baseHP; per[1] = &c->basePow; per[2] = &c->baseSkl;
    per[3] = &c->baseSpd; per[4] = &c->baseDef; per[5] = &c->baseRes;
    for (i = 0; i < 6; i++)
    {
        int lo = i == 0 ? 1 : 0;
        int hi = Min(nc[i], i == 0 ? STAT_HP_MAX : STAT_MAX);

        if (hi < nb[i])
            hi = nb[i]; /* a class base over its own cap: keep the base */
        *per[i] = (s8)(Clamp(st.v[i], lo, hi) - nb[i]);
    }
    con = kF->baseCon + f->baseCon;
    c->baseCon = (s8)(Clamp(con, 1, Min(kN->maxCon, STAT_MAX)) - kN->baseCon);

    c->growthHP = f->growthHP;   c->growthPow = f->growthPow; c->growthSkl = f->growthSkl;
    c->growthSpd = f->growthSpd; c->growthDef = f->growthDef; c->growthRes = f->growthRes;
    c->growthLck = f->growthLck;
    c->affinity = f->affinity;
    c->descTextId = f->descTextId;
    c->attributes = (s->attributes & ~CA_FEMALE) | (f->attributes & CA_FEMALE);
    if (newJ == f->defaultClass)
        memcpy(c->baseRanks, f->baseRanks, sizeof(c->baseRanks));
    else
    {
        for (i = 0; i < 8; i++)
            eff[i] = f->baseRanks[i] ? f->baseRanks[i] : kF->baseRanks[i];
        Rando_TransferRanks(rng, eff, newJ, ranks);
        memcpy(c->baseRanks, ranks, sizeof(ranks));
    }
    c->defaultClass = (u8)newJ;
    RandoClasses_SetPlayerClass(S, newJ);
}

/* A message's text, decoded (no name replacement), into buf. */
static void DecodeName(int msg, char *buf)
{
    static char tmp[0x555];
    int i;

    buf[0] = 0;
    if (msg <= 0)
        return;
    CallARM_DecompText((const char *)gMsgTable[msg], tmp);
    for (i = 0; i < NAME_MAX - 1 && (u8)tmp[i] >= 0x20; i++)
        buf[i] = tmp[i];
    buf[i] = 0;
}

void RandoRecruit_Apply(void)
{
    static struct CharacterData live[0x100];
    struct RandoRng rng;
    u8 slots[0x24], fills[0x24], used[0x24];
    int nSlots = 0, pass, i, j, pid;

    memset(sSource, 0, sizeof(sSource));
    memset(sFaceMap, 0, sizeof(sFaceMap));
    sNames = 0;
    sActiveMsg = -1; /* the cached message may hold another run's names */
    if (!gRandoRunOn || !Rando_Opt(RFO_RANDO_RECRUIT))
        return;
    memcpy(live, gCharacterData, sizeof(live));
    for (pid = 1; pid < 0x24; pid++)
        sClassBefore[pid] = (u8)RandoClasses_PlayerClass(pid);
    for (pid = 1; pid < 0x24; pid++)
        if (InPool(pid))
        {
            slots[nSlots] = (u8)pid;
            fills[nSlots] = (u8)pid;
            used[nSlots] = 0;
            nSlots++;
        }
    Rando_RngSeed(&rng, RANDO_SALT_RECRUIT);
    /* pass 0: the slots with needs, pass 1: the rest; each in pid order */
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < nSlots; i++)
        {
            int S = slots[i], need = SlotNeed(S);
            u8 cand[0x24];
            int n = 0, k, F, cls, forced = 0;

            if ((need != 0) != (pass == 0))
                continue;
            for (k = 0; k < nSlots; k++)
                if (!used[k] && (Rando_Opt(RFO_RANDO_RECRUIT_GENDER) || IsFemale(fills[k]) == IsFemale(S)) &&
                    (Rando_Opt(RFO_RANDO_RECRUIT_CLASS) == 1 ||
                     Fits(S, OwnClassFor(S, fills[k], Rando_Hash(RANDO_SALT_RECRUIT, (u32)S, fills[k], 2)))))
                    cand[n++] = (u8)k;
            if (n == 0)
            {
                forced = 1; /* no fill gives the class: the slot's own class */
                for (k = 0; k < nSlots; k++)
                    if (!used[k] && (Rando_Opt(RFO_RANDO_RECRUIT_GENDER) || IsFemale(fills[k]) == IsFemale(S)))
                        cand[n++] = (u8)k;
            }
            if (n == 0)
                for (k = 0; k < nSlots; k++)
                    if (!used[k])
                        cand[n++] = (u8)k;
            if (n == 0)
                continue;
            k = cand[Rando_RngInt(&rng, n)];
            used[k] = 1;
            F = fills[k];
            sSource[S] = (u8)F;
            if (forced || Rando_Opt(RFO_RANDO_RECRUIT_CLASS) == 1)
                cls = sClassBefore[S];
            else
                cls = OwnClassFor(S, F, Rando_Hash(RANDO_SALT_RECRUIT, (u32)S, (u32)F, 2));
            if (F != S)
                FillSlot(&rng, live, S, F, cls);
        }
    /* faces and names: slot -> fill, all at once */
    for (i = 0; i < nSlots; i++)
    {
        int S = slots[i], F = sSource[S];
        const struct CharacterData *vs = Rando_VanillaChar(S), *vf = Rando_VanillaChar(F);

        if (F == 0 || F == S || vs == NULL || vf == NULL)
            continue;
        if (vs->portraitId > 0 && vs->portraitId < FACE_MAX)
            sFaceMap[vs->portraitId] = vf->portraitId;
        DecodeName(vs->nameTextId, sNameFrom[sNames]);
        DecodeName(vf->nameTextId, sNameTo[sNames]);
        sNameLen[sNames] = (u8)strlen(sNameFrom[sNames]);
        if (sNameLen[sNames] > 0)
            sNames++;
    }
    /* longest names first, so a name inside a longer one never wins */
    for (i = 1; i < sNames; i++)
        for (j = i; j > 0 && sNameLen[j - 1] < sNameLen[j]; j--)
        {
            char t[NAME_MAX];
            u8 l;

            memcpy(t, sNameFrom[j], NAME_MAX); memcpy(sNameFrom[j], sNameFrom[j - 1], NAME_MAX); memcpy(sNameFrom[j - 1], t, NAME_MAX);
            memcpy(t, sNameTo[j], NAME_MAX); memcpy(sNameTo[j], sNameTo[j - 1], NAME_MAX); memcpy(sNameTo[j - 1], t, NAME_MAX);
            l = sNameLen[j]; sNameLen[j] = sNameLen[j - 1]; sNameLen[j - 1] = l;
        }
    if (!sQuiet)
    {
        char line[512];
        int len = 0;

        line[0] = 0;
        for (i = 0; i < nSlots && len < (int)sizeof(line) - 16; i++)
            if (sSource[slots[i]] != 0 && sSource[slots[i]] != slots[i])
                len += snprintf(line + len, sizeof(line) - len, " %02X<-%02X", slots[i], sSource[slots[i]]);
        PortLog("[rando] recruitment (slot<-fill):%s\n", line);
    }
}

/* ------------------------------------------------------------------ the hooks */

int Reforged_RandoFace(int fid)
{
    if (!gRandoRunOn || fid <= 0 || fid >= FACE_MAX || sFaceMap[fid] == 0)
        return fid;
    return sFaceMap[fid];
}

static int IsLetter(u8 ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

void Reforged_RandoText(char *buf, int size)
{
    static char out[0x600];
    int i = 0, o = 0, n, prevLetter = 0, limit;

    if (!gRandoRunOn || sNames == 0 || buf == NULL || size <= 1)
        return;
    /* a name is replaced only when the whole rest of the text still fits after it,
       so the text is never cut (a longer name near the end of a full buffer stays
       the slot's name) */
    limit = (size < (int)sizeof(out) ? size : (int)sizeof(out)) - 1;
    while (buf[i] != 0 && o < limit)
    {
        u8 ch = (u8)buf[i];

        if (ch == 0x10) /* [LoadFace] and its face id */
        {
            int k;

            for (k = 0; k < 3 && buf[i] != 0; k++)
                out[o++] = buf[i++];
            prevLetter = 0;
            continue;
        }
        if (ch == 0x80 && buf[i + 1] != 0) /* a two-byte code */
        {
            out[o++] = buf[i++];
            out[o++] = buf[i++];
            prevLetter = 0;
            continue;
        }
        if (!prevLetter && IsLetter(ch))
        {
            for (n = 0; n < sNames; n++)
                if (strncmp(buf + i, sNameFrom[n], sNameLen[n]) == 0 && !IsLetter((u8)buf[i + sNameLen[n]]))
                    break;
            if (n < sNames && o + (int)strlen(sNameTo[n]) + (int)strlen(buf + i + sNameLen[n]) <= limit)
            {
                int l = (int)strlen(sNameTo[n]);

                memcpy(out + o, sNameTo[n], l);
                o += l;
                i += sNameLen[n];
                prevLetter = 1;
                continue;
            }
        }
        prevLetter = IsLetter(ch);
        out[o++] = buf[i++];
    }
    while (buf[i] != 0 && o < limit)
        out[o++] = buf[i++];
    memcpy(buf, out, o);
    buf[o] = 0;
}

/* ------------------------------------------------------------------ self-test */

int RandoRecruit_SelfTest(void)
{
    u8 seen[PID_MAX + 1];
    int fails = 0, pid;

    if (!gRandoRunOn || !Rando_Opt(RFO_RANDO_RECRUIT))
    {
        for (pid = 1; pid <= PID_MAX; pid++)
            if (sSource[pid] != 0)
            {
                PortLog("[rando] check: recruit: a fill with recruitment off (0x%02X)\n", pid);
                return 1;
            }
        return sNames != 0;
    }
    memset(seen, 0, sizeof(seen));
    for (pid = 1; pid <= PID_MAX; pid++)
    {
        int F = sSource[pid], i;
        const struct CharacterData *c = Rando_Char(pid);
        const struct ClassData *k;

        if (F == 0)
            continue;
        if (!InPool(pid) || !InPool(F) || seen[F]++)
        {
            PortLog("[rando] check: recruit: slot 0x%02X fill 0x%02X: not a permutation of the pool\n", pid, F);
            fails++;
            continue;
        }
        if (!Rando_Opt(RFO_RANDO_RECRUIT_GENDER) && IsFemale(pid) != IsFemale(F))
        {
            PortLog("[rando] check: recruit: slot 0x%02X fill 0x%02X: crosses sex\n", pid, F);
            fails++;
        }
        if (RandoClasses_PlayerFe8Class(pid) != 0 && c->defaultClass == RandoClasses_PlayerClass(pid) &&
            (k = GetClassData(c->defaultClass)) != NULL && k->pBattleAnimDef != NULL)
            ; /* versions 11-14: a foreign class of the run (reforged_rando_classes.c checks it) */
        else if (c->defaultClass != RandoClasses_PlayerClass(pid) || (k = Rando_Class(c->defaultClass)) == NULL ||
            k->pBattleAnimDef == NULL)
        {
            PortLog("[rando] check: recruit: slot 0x%02X: class 0x%02X\n", pid, c->defaultClass);
            fails++;
            continue;
        }
        if (SlotNeed(pid) == 1 && !Fits(pid, RandoClasses_PlayerFe8Class(pid) ? RandoClasses_PlayerFe8Class(pid) : c->defaultClass))
        {
            PortLog("[rando] check: recruit: slot 0x%02X: class 0x%02X misses the slot's need\n", pid,
                c->defaultClass);
            fails++;
        }
        {
            int tot[6] = { k->baseHP + c->baseHP, k->basePow + c->basePow, k->baseSkl + c->baseSkl,
                           k->baseSpd + c->baseSpd, k->baseDef + c->baseDef, k->baseRes + c->baseRes };

            for (i = 0; i < 6; i++)
                if (tot[i] < (i == 0 ? 1 : 0) || tot[i] > (i == 0 ? STAT_HP_MAX : STAT_MAX))
                {
                    PortLog("[rando] check: recruit: slot 0x%02X: stat %d is %d\n", pid, i, tot[i]);
                    fails++;
                }
        }
        if (c->baseLck < 0 || c->baseLck > STAT_LCK_MAX || k->baseCon + c->baseCon < 1)
        {
            PortLog("[rando] check: recruit: slot 0x%02X: luck %d con %d\n", pid, c->baseLck, k->baseCon + c->baseCon);
            fails++;
        }
        for (i = 0; i < 8; i++)
            if (c->baseRanks[i] && !k->baseRanks[i])
            {
                PortLog("[rando] check: recruit: slot 0x%02X: a rank in type %d its class does not use\n", pid, i);
                fails++;
            }
    }
    /* the text hook: a slot's name becomes its fill's, a longer word is left alone */
    if (sNames > 0)
    {
        char buf[0x80];

        snprintf(buf, sizeof(buf), "%s, %sx %s.", sNameFrom[0], sNameFrom[0], sNameFrom[sNames - 1]);
        Reforged_RandoText(buf, sizeof(buf));
        if (strncmp(buf, sNameTo[0], strlen(sNameTo[0])) != 0 || strstr(buf, sNameTo[sNames - 1]) == NULL)
        {
            PortLog("[rando] check: recruit: text replacement gave '%s'\n", buf);
            fails++;
        }
    }
    /* a full message buffer of names: the text stays whole and ends inside it */
    if (sNames > 0)
    {
        static char big[0x555];
        size_t len = 0, at;

        while (len + NAME_MAX + 2 < sizeof(big))
            for (at = 0; at < (size_t)sNames && len + NAME_MAX + 2 < sizeof(big); at++)
                len += (size_t)snprintf(big + len, sizeof(big) - len, "%s ", sNameFrom[at]);
        big[len++] = 0x10;
        big[len] = 0;
        Reforged_RandoText(big, sizeof(big));
        len = strlen(big);
        if (len > sizeof(big) - 1 || (u8)big[len - 1] != 0x10)
        {
            PortLog("[rando] check: recruit: a full message lost its end (%u bytes)\n", (unsigned)len);
            fails++;
        }
    }
    return fails;
}

void RandoRecruit_SetQuiet(int quiet)
{
    sQuiet = quiet;
}

#endif /* PORT_PLAYED_BUILD */
