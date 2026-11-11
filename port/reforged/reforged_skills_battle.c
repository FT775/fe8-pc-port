#include "global.h"

#include <stdlib.h>
#include <string.h>

/*
 * Reforged Options, Game fixes: class skills, the battle side (RFO_FIX_SKILLS, RFO_FIX_ENEMY_SKILLS;
 * the user's request of 2026-11-06/07, design "Skills proposal (draft).md", "use your picks").
 * Who has which skill is reforged_skills.c's business (Reforged_SkillHas); this file only makes
 * the skills do something in src/bmbattle.c, through the same-line RFO_SKILL_* hooks of
 * include/bmbattle.h (nothing there on the GBA build).
 *
 * Two rules hold everywhere, so the retail game and its self-checks stay as they were:
 *   - Chance skills roll BattleRoll1RN(rate, FALSE), and only once Reforged_SkillHas has said yes.
 *     With skills off no random number is ever drawn (the RN sequence, suspend replay and Rewind
 *     are the original's), and in a simulation (forecast, AI) a chance skill never procs and draws
 *     nothing, the way FE8 already treats Great Shield or Silencer.
 *   - Passive skills (the Hit/Avoid/Crit mods, Wary Fighter, Vantage, Lifetaker, Poison Strike)
 *     run in simulations too, so the forecast, the AI and the real battle agree.
 *
 * Rates: attack skills (Luna, Sol) Skl/2 %, defence skills (Pavise, Aegis) Skl %, Miracle Lck %,
 * Armsthrift Lck x2 %, Despoil Lck %, each from the battle copy (gBattleActor/Target.unit), which
 * holds the stats as the getters gave them (rescue halving, Pair Up, Stat +2 included).
 */

#if PORT_PLAYED_BUILD

#include "bmunit.h"
#include "bmbattle.h"
#include "bmitem.h"
#include "bmmap.h"
#include "bmmind.h"
#include "bmphase.h"
#include "variables.h"

#include "port_reforged.h"
#include "reforged_skills.h"

#define DESPOIL_GOLD 300

/* Vantage swapped the opening of the battle BattleUnwind is generating (the Triangle Attack test
   needs to know: the "first hit" is then the defender's). Set on every unwind. */
static u8 sVantage;

/* This real battle is an event's scripted one (gActionData.scriptedBattleHits, which
   BattleUnwindScripted clears before returning): latched in Reforged_SkillBattleMods, before the
   unwind. Scripted battles stay exactly as written: no chance skill, no after-battle effect. */
static u8 sScripted;

/* Sol's heals in this battle (0: the actor, 1: the target), given after the battle
   (Reforged_SkillAfterUnwind): healing mid-battle would leave the battle animation's HP table
   behind the real HP, and a later hit could show a death that did not happen. */
static int sSolHeal[2];

/* Despoil procced in the last real battle: the gold is paid when the battle is applied
   (BattleApplyUnitUpdates), never while generating, because event battles and the Pair Up
   self-test generate the same battle more than once. The pair is kept so that a battle that was
   generated but never applied cannot pay out on some later, unrelated apply. */
static u8 sDespoil;
static u8 sDespoilActor, sDespoilTarget;

/* ---- Skill banners (v1.14.0, reforged_skillbanner.c): what procced on which hit ----
   Display data only: nothing in the game reads gRfoSkillProcPc or BATTLE_HIT_ATTR_18 (the battle
   animations' parser and the map animations test other bits; the map animations keep a u16 copy of
   the attributes, which drops bit 18). No random number is drawn for it. */
static int RealBattle(void)
{
    return (gBattleStats.config & BATTLE_CONFIG_REAL) && !(gBattleStats.config & BATTLE_CONFIG_SIMULATE) && !sScripted;
}

/* proc: RFO_PROC_*; side 0: the hit's striker has the skill, 1: its defender. The hit is the one
   being generated (gBattleHitIterator). */
static void RecordProc(int proc, int side)
{
    int i = (int)(gBattleHitIterator - gBattleHitArray);

    if (!RealBattle() || i < 0 || i >= BATTLE_HIT_MAX - 1)
        return;
    gRfoSkillProcPc[i][side] |= (unsigned short)(1 << proc);
    gBattleHitIterator->attributes |= BATTLE_HIT_ATTR_18;
}

/* test switch FE8PC_TEST_SKILL_PROCS (played builds; recordings of the banners): a list of skill
   names as shown ("Luna,Sol,Pavise,Aegis,Miracle"); each listed chance skill procs on every chance
   it gets in a real battle, for either side, with or without the skill, and draws no random
   number. Never in simulations or scripted battles. With "arena" in the list, only in arena battles
   (so a recorded route plays as before up to the arena). */
static int TestProc(int skill)
{
    const char *list = getenv("FE8PC_TEST_SKILL_PROCS");

    if (list == NULL || !RealBattle() || strstr(list, Reforged_SkillName(skill)) == NULL)
        return 0;
    return strstr(list, "arena") == NULL || (gBattleStats.config & BATTLE_CONFIG_ARENA) != 0;
}

/* Nihil: bu's foe in this battle has it, so bu's battle skills do nothing (both sides real units). */
int Reforged_SkillNegated(const struct BattleUnit *bu)
{
    const struct BattleUnit *foe;

    if (!Reforged_SkillsOn() || (bu != &gBattleActor && bu != &gBattleTarget))
        return 0;
    foe = bu == &gBattleActor ? &gBattleTarget : &gBattleActor;
    if (foe->unit.index == 0 || bu->unit.index == 0 || foe->unit.pCharacterData == NULL || foe->unit.pClassData == NULL)
        return 0;
    return Reforged_SkillHas(&foe->unit, SK_NIHIL);
}

static int Has(const struct BattleUnit *bu, int skill)
{
    /* Discipline is also asked for staff use, where the "foe" is the healed ally: never negated */
    if (skill != SK_DISCIPLINE && Reforged_SkillNegated(bu))
        return 0;
    return Reforged_SkillHas(&bu->unit, skill);
}

static int Arena(void)
{
    return (gBattleStats.config & BATTLE_CONFIG_ARENA) != 0;
}

static int EffLevel(const struct Unit *u)
{
    /* Underdog: a promoted unit counts 20 levels higher, as in the design */
    return u->level + ((UNIT_CATTRIBUTES(u) & CA_PROMOTED) ? 20 : 0);
}

static int Dist(int x1, int y1, int x2, int y2)
{
    return ABS(x1 - x2) + ABS(y1 - y2);
}

/* What the units around bu do to it. other is bu's foe in this battle: its index is skipped in the
   map scan and it is checked from its battle copy instead, because in a simulation the actor's map
   tile is still its old one (gBmMapUnit holds the old square, the copy the hypothetical one). */
struct AreaMods {
    u8 allyNear;  /* an ally within 2 squares (Tantivy) */
    u8 charm;     /* an ally with Charm within 2 squares */
    u8 hex;       /* an enemy with Hex adjacent */
    u8 anathema;  /* an enemy with Anathema within 3 squares */
};

static void AreaCheckUnit(struct AreaMods *m, const struct BattleUnit *bu, const struct Unit *u, int d)
{
    if (d == 0)
        return;

    if (AreUnitsAllied(u->index, bu->unit.index)) {
        if (d <= 2) {
            m->allyNear = 1;
            if (!m->charm && Reforged_SkillHas(u, SK_CHARM))
                m->charm = 1;
        }
    } else {
        if (d <= 1 && !m->hex && Reforged_SkillHas(u, SK_HEX))
            m->hex = 1;
        if (d <= 3 && !m->anathema && Reforged_SkillHas(u, SK_ANATHEMA))
            m->anathema = 1;
    }
}

static void AreaScan(struct AreaMods *m, const struct BattleUnit *bu, const struct BattleUnit *other)
{
    int x0 = bu->unit.xPos, y0 = bu->unit.yPos;
    int x, y, d;

    for (y = y0 - 3; y <= y0 + 3; ++y) {
        if (y < 0 || y >= gBmMapSize.y)
            continue;

        for (x = x0 - 3; x <= x0 + 3; ++x) {
            int idx;
            struct Unit *u;

            if (x < 0 || x >= gBmMapSize.x)
                continue;

            d = Dist(x, y, x0, y0);
            if (d > 3)
                continue;

            idx = gBmMapUnit[y][x];
            if (idx == 0 || idx == bu->unit.index || idx == other->unit.index)
                continue;

            u = GetUnit(idx);
            if (u == NULL || u->pCharacterData == NULL || (u->state & US_UNAVAILABLE))
                continue;

            AreaCheckUnit(m, bu, u, d);
        }
    }

    /* the foe's own Hex/Anathema do nothing to a unit with Nihil */
    if (!(Reforged_SkillHas(&bu->unit, SK_NIHIL) && !AreUnitsAllied(other->unit.index, bu->unit.index)))
        AreaCheckUnit(m, bu, &other->unit, Dist(other->unit.xPos, other->unit.yPos, x0, y0));
}

static void ClampLow(short *v)
{
    if (*v < 0)
        *v = 0;
}

/* One side's Hit/Avoid/Crit/crit-evade changes, before the effective rates are computed (so
   FE8's own clamps, Nullify-crit items and the Runesword rule still apply afterwards). */
static void BattleModsOne(struct BattleUnit *bu, struct BattleUnit *foe)
{
    struct AreaMods m;

    /* Nihil: FE8's own Critical +15 (CA_CRITBONUS, ComputeBattleUnitCritRate) is taken back */
    if ((UNIT_CATTRIBUTES(&bu->unit) & CA_CRITBONUS) && Reforged_SkillNegated(bu))
        bu->battleCritRate -= 15;

    /* Wrath: +20 Crit at half HP or less (curHP is still hpInitial here) */
    if (bu->unit.curHP * 2 <= bu->unit.maxHP && Has(bu, SK_WRATH))
        bu->battleCritRate += 20;

    /* Patience: +10 Hit/Avoid when it is not this unit's side's phase */
    if (UNIT_FACTION(&bu->unit) != gPlaySt.faction && Has(bu, SK_PATIENCE)) {
        bu->battleHitRate += 10;
        bu->battleAvoidRate += 10;
    }

    /* Underdog: +15 Hit/Avoid against a higher-level foe */
    if (EffLevel(&foe->unit) > EffLevel(&bu->unit) && Has(bu, SK_UNDERDOG)) {
        bu->battleHitRate += 15;
        bu->battleAvoidRate += 15;
    }

    /* Breakers: +30 Hit/Avoid against a foe holding that weapon type (equipped, even when it
       cannot counter at this range: weaponBefore, as weapon is cleared for no counter) */
    if (foe->weaponBefore) {
        int sk = SK_NONE;

        switch (foe->weaponType) {
        case ITYPE_SWORD: sk = SK_SWORDBREAKER; break;
        case ITYPE_LANCE: sk = SK_LANCEBREAKER; break;
        case ITYPE_AXE:   sk = SK_AXEBREAKER;   break;
        case ITYPE_BOW:   sk = SK_BOWBREAKER;   break;
        case ITYPE_ANIMA:
        case ITYPE_LIGHT:
        case ITYPE_DARK:  sk = SK_TOMEBREAKER;  break;
        }

        if (sk != SK_NONE && Has(bu, sk)) {
            bu->battleHitRate += 30;
            bu->battleAvoidRate += 30;
        }
    }

    /* Area skills: not in the arena (the copies' positions are not the arena's) */
    if (Arena())
        return;

    m.allyNear = m.charm = m.hex = m.anathema = 0;
    AreaScan(&m, bu, foe);

    if (m.charm) {
        bu->battleHitRate += 5;
        bu->battleAvoidRate += 5;
    }

    if (!m.allyNear && Has(bu, SK_TANTIVY)) {
        bu->battleHitRate += 10;
        bu->battleAvoidRate += 10;
    }

    if (m.hex)
        bu->battleAvoidRate -= 15;

    if (m.anathema) {
        bu->battleAvoidRate -= 10;
        bu->battleDodgeRate -= 10;
    }

    /* FE8 never lets Avoid go below 0 (ComputeBattleUnitAvoidRate); keep crit evade there too */
    ClampLow(&bu->battleAvoidRate);
    ClampLow(&bu->battleDodgeRate);
}

/* RFO_SKILL_BATTLE_MODS: BattleGenerate, after both ComputeBattleUnitStats. */
void Reforged_SkillBattleMods(void)
{
    sVantage = 0;
    sSolHeal[0] = sSolHeal[1] = 0;
    Reforged_SkillProcClear();
    sScripted = (gBattleStats.config & BATTLE_CONFIG_REAL) && gActionData.scriptedBattleHits != NULL;

    /* a new real battle: whatever an unapplied one rolled is gone (simulations, e.g. the AI
       thinking between a real generation and its apply, leave it alone) */
    if (!(gBattleStats.config & BATTLE_CONFIG_SIMULATE))
        sDespoil = 0;

    if (!Reforged_SkillsOn())
        return;

    /* obstacles (walls, snags) are index 0: ComputeBattleObstacleStats sets the rates there */
    if (gBattleActor.unit.index == 0 || gBattleTarget.unit.index == 0)
        return;

    BattleModsOne(&gBattleActor, &gBattleTarget);
    BattleModsOne(&gBattleTarget, &gBattleActor);
}

/* RFO_SKILL_NO_FOLLOWUP: BattleGetFollowUpOrder. Wary Fighter, either side: nobody doubles
   (a brave weapon still strikes twice, GetBattleUnitHitCount). The forecast's x2 follows. */
int Reforged_SkillWary(void)
{
    return Has(&gBattleActor, SK_WARY) || Has(&gBattleTarget, SK_WARY);
}

/* RFO_SKILL_VANTAGE: BattleUnwind, after BattleGetBattleUnitOrder. The attacked unit at half HP
   or less strikes first, if it can counter at all (weapon is 0 when it cannot). The battle
   animations pick the striking side per hit from BATTLE_HIT_INFO_RETALIATION, so they follow. */
void Reforged_SkillVantage(struct BattleUnit **attacker, struct BattleUnit **defender)
{
    sVantage = 0;
    Reforged_SkillProcClear(); /* every unwind, the arena's later rounds included (ClearBattleHits just ran) */

    if (Arena() || gBattleTarget.unit.index == 0 || gBattleTarget.weapon == 0)
        return;

    if (gBattleTarget.unit.curHP * 2 > gBattleTarget.unit.maxHP)
        return;

    if (!Has(&gBattleTarget, SK_VANTAGE))
        return;

    *attacker = &gBattleTarget;
    *defender = &gBattleActor;
    sVantage = 1;
    RecordProc(RFO_PROC_VANTAGE, 0); /* hit 0 (ClearBattleHits), the Vantage holder's */
}

/* RFO_VANTAGE_TA: BattleGenerateHitTriangleAttack tests "the opening hit" (INFO_BEGIN) to return
   early; under Vantage that hit is the defender's. Returns 1 where the test must flip: the
   defender's opening hit gets no Triangle Attack (it was not the one attacking), and the actor's
   own first hit keeps the chance it would have had. */
int Reforged_SkillVantageTAFlip(struct BattleUnit *attacker)
{
    (void)attacker;
    if (!sVantage)
        return 0;

    /* the test becomes "always return": the map animations create Triangle Attack's partners only
       when hit 0 carries it (mapanim_api.c) and battle quotes look at hit 0 too, so a battle that
       Vantage reordered has no Triangle Attack at all */
    return (gBattleHitIterator->info & BATTLE_HIT_INFO_BEGIN) != 0;
}

/* RFO_SKILL_LUNA: BattleGenerateHitAttributes, right after damage = attack - defense. The hit
   ignores half the foe's Def/Res (defense is already the right one); not with Great Shield or
   Pierce (Pierce already ignores all of it). Done on the damage, so the line stays as it was. */
void Reforged_SkillLuna(struct BattleUnit *attacker, int defense)
{
    if (defense <= 0)
        return;

    if (gBattleHitIterator->attributes & (BATTLE_HIT_ATTR_GREATSHLD | BATTLE_HIT_ATTR_PIERCE))
        return;

    if (sScripted)
        return;

    if (TestProc(SK_LUNA) || (Has(attacker, SK_LUNA) && BattleRoll1RN(attacker->unit.skl / 2, FALSE))) {
        gBattleStats.damage += defense / 2;
        if (defense / 2 > 0)
            RecordProc(RFO_PROC_LUNA, 0);
    }
}

/* RFO_SKILL_PAVISE: BattleGenerateHitAttributes, after the crit block, before the damage clamp.
   The defender halves the hit: Aegis against bows (ballista bolts too) and magic, Pavise against
   the rest (swords, lances, axes, physical monster attacks). Not against Great Shield (already 0)
   or Silencer (a kill is a kill). */
void Reforged_SkillPavise(struct BattleUnit *attacker, struct BattleUnit *defender)
{
    int sk;

    if (gBattleStats.damage <= 0)
        return;

    if (gBattleHitIterator->attributes & (BATTLE_HIT_ATTR_GREATSHLD | BATTLE_HIT_ATTR_SILENCER))
        return;

    if (attacker->weaponType == ITYPE_BOW || attacker->weaponType == ITYPE_BLLST
        || (attacker->weaponAttributes & (IA_MAGIC | IA_MAGICDAMAGE)))
        sk = SK_AEGIS;
    else
        sk = SK_PAVISE;

    if (sScripted)
        return;

    if (TestProc(sk) || (Has(defender, sk) && BattleRoll1RN(defender->unit.skl, FALSE))) {
        gBattleStats.damage /= 2;
        RecordProc(sk == SK_AEGIS ? RFO_PROC_AEGIS : RFO_PROC_PAVISE, 1);
    }
}

/* RFO_SKILL_MIRACLE: BattleGenerateHitEffects, before the damage is taken off the defender.
   A killing hit leaves it on 1 HP instead (only above 1 HP). gBattleStats.damage is what the
   hit records (hpChange), so the animations show the survival. Stops Silencer too. */
void Reforged_SkillMiracle(struct BattleUnit *defender)
{
    if (gBattleStats.damage < defender->unit.curHP || defender->unit.curHP <= 1)
        return;

    if (sScripted)
        return;

    if (TestProc(SK_MIRACLE) || (Has(defender, SK_MIRACLE) && BattleRoll1RN(defender->unit.lck, FALSE))) {
        gBattleStats.damage = defender->unit.curHP - 1;
        RecordProc(RFO_PROC_MIRACLE, 1);
    }
}

/* RFO_SKILL_SOL: BattleGenerateHitEffects, after the HP-drain block. Heals the damage dealt.
   BATTLE_HIT_ATTR_HPSTEAL is left as the weapon set it: the battle animations' HP table
   (banim-battleparse.c) takes an extra attacker entry for that flag that only the Nosferatu
   bar consumes, so flagging a physical hit would shift every later HP bar of the battle. Without
   the flag the heal shows as the HP after the battle. */
void Reforged_SkillSol(struct BattleUnit *attacker, struct BattleUnit *defender)
{
    (void)defender;
    if (gBattleStats.damage <= 0 || (gBattleHitIterator->attributes & BATTLE_HIT_ATTR_DEVIL))
        return;

    /* not in the arena: its rounds share the battle animation's HP gauge, and the arena's result is
       read from that gauge (banim-ekrbattle.c ekrBattleInRoundIdle), so a heal between rounds would
       leave it showing the wrong HP and could decide the wager wrongly */
    if (sScripted || attacker->unit.curHP <= 0 || Arena())
        return;

    if (!TestProc(SK_SOL) && (!Has(attacker, SK_SOL) || !BattleRoll1RN(attacker->unit.skl / 2, FALSE)))
        return;

    sSolHeal[attacker == &gBattleActor ? 0 : 1] += gBattleStats.damage; /* given after the battle */
    RecordProc(RFO_PROC_SOL, 0);
}

/* Sol's heals, after the battle, to a holder still standing. */
static void SolAfter(struct BattleUnit *bu, int heal)
{
    int hp;

    if (heal <= 0 || bu->unit.curHP <= 0)
        return;
    hp = bu->unit.curHP + heal;
    if (hp > bu->unit.maxHP)
        hp = bu->unit.maxHP;
    {
        static int sLogged;
        if (sLogged < 20)
            sLogged++, PortLog("[reforged] skills: Sol: unit 0x%02X heals %d -> %d HP%s\n", (u8)bu->unit.index,
                               bu->unit.curHP, hp, Arena() ? " (arena)" : "");
    }
    bu->unit.curHP = hp;
}

/* RFO_SKILL_ARMSTHRIFT: BattleGenerateHitEffects, the weapon's use in battle (not staves, which
   use Reforged_ItemAfterUse elsewhere). after is the weapon once used; the roll only happens
   when the use would cost something. Returns the weapon the attacker keeps. */
u16 Reforged_SkillArmsthrift(struct BattleUnit *attacker, int after)
{
    if (after == attacker->weapon)
        return after;

    if (sScripted || !Has(attacker, SK_ARMSTHRIFT))
        return after;

    if (BattleRoll1RN(attacker->unit.lck * 2, FALSE)) {
        RecordProc(RFO_PROC_ARMSTHRIFT, 0); /* recorded; no banner (Awakening shows none) */
        return attacker->weapon;
    }

    return after;
}

/* RFO_SKILL_LIFETAKER: BattleGenerateHit, the branch where the target falls. A kill on the
   unit's own attack (the actor) heals half its max HP; UpdateUnitFromBattle writes it back. */
void Reforged_SkillLifetaker(struct BattleUnit *attacker, struct BattleUnit *defender)
{
    int hp;

    if (attacker != &gBattleActor || attacker->unit.curHP <= 0 || defender->unit.curHP != 0)
        return;

    if (gBattleTarget.unit.index == 0 || Arena())
        return;

    if (!Has(attacker, SK_LIFETAKER))
        return;

    hp = attacker->unit.curHP + attacker->unit.maxHP / 2;
    if (hp > attacker->unit.maxHP)
        hp = attacker->unit.maxHP;
    attacker->unit.curHP = hp;
    RecordProc(RFO_PROC_LIFETAKER, 0); /* the killing hit (the iterator has not moved on yet) */
}

/* Poison Strike: the holder survived, landed a hit (not missed, not blocked by Dual Guard) and
   the foe is still standing: the foe loses 20% of its max HP, down to 1 at worst, so a kill and
   its experience never change. */
static void PoisonStrike(struct BattleUnit *bu, struct BattleUnit *foe, int buIsActor)
{
    const struct BattleHit *h;
    int hp, hit = 0;

    if (bu->unit.curHP <= 0 || foe->unit.curHP <= 1)
        return;

    if (!Has(bu, SK_POISONSTRIKE))
        return;

    for (h = gBattleHitArray; h < gBattleHitIterator; ++h) {
        int byActor = !(h->info & BATTLE_HIT_INFO_RETALIATION);

        if (byActor != buIsActor)
            continue;
        if (h->attributes & (BATTLE_HIT_ATTR_MISS | BATTLE_HIT_ATTR_17))
            continue;
        hit = 1;
        break;
    }

    if (!hit)
        return;

    hp = foe->unit.curHP - foe->unit.maxHP / 5;
    if (hp < 1)
        hp = 1;
    foe->unit.curHP = hp;
}

/* Reforged_SkillAfterUnwind (a PC block at the end of BattleGenerate). Sol's heals; Poison Strike (also in
   simulations) and the Despoil roll (real battles only). None of them in the arena (Sol does not even
   roll there: Reforged_SkillSol), nor after scripted battles, nor against walls and snags. */
void Reforged_SkillAfterUnwind(void)
{
    struct BattleUnit *killer = NULL;

    if (!Reforged_SkillsOn() || sScripted)
        return;

    SolAfter(&gBattleActor, sSolHeal[0]);
    SolAfter(&gBattleTarget, sSolHeal[1]);
    sSolHeal[0] = sSolHeal[1] = 0;

    if (Arena() || gBattleTarget.unit.index == 0 || gBattleActor.unit.index == 0)
        return;

    PoisonStrike(&gBattleActor, &gBattleTarget, 1);
    PoisonStrike(&gBattleTarget, &gBattleActor, 0);

    if (gBattleStats.config & BATTLE_CONFIG_SIMULATE)
        return;

    if (gBattleTarget.unit.curHP == 0 && gBattleActor.unit.curHP > 0) {
        killer = &gBattleActor;
    } else if (gBattleActor.unit.curHP == 0 && gBattleTarget.unit.curHP > 0) {
        killer = &gBattleTarget;
    }

    if (killer == NULL)
        return;

    /* a Devil weapon's backfire is not a kill by the other side */
    if (gBattleHitIterator > gBattleHitArray && ((gBattleHitIterator - 1)->attributes & BATTLE_HIT_ATTR_DEVIL))
        return;

    /* gold is the party's: player units only */
    if (UNIT_FACTION(&killer->unit) != FACTION_BLUE)
        return;

    if (!Has(killer, SK_DESPOIL))
        return;

    if (BattleRoll1RN(killer->unit.lck, FALSE)) {
        sDespoil = 1;
        sDespoilActor = gBattleActor.unit.index;
        sDespoilTarget = gBattleTarget.unit.index;
    }
}

/* RFO_SKILL_APPLY: BattleApplyUnitUpdates. Pays Despoil's gold once, for the battle that rolled it. */
void Reforged_SkillApply(void)
{
    if (!sDespoil)
        return;

    sDespoil = 0;

    if (gBattleActor.unit.index != sDespoilActor || gBattleTarget.unit.index != sDespoilTarget)
        return;

    AddPartyGoldAmount(DESPOIL_GOLD);
}

/* RFO_SKILL_WEXP_MUL: GetBattleUnitUpdatedWeaponExp. Discipline doubles the weapon EXP; that one
   function feeds the rank-up popup, the write-back, the arena and staves alike. */
int Reforged_SkillWexpMul(struct BattleUnit *bu)
{
    return Has(bu, SK_DISCIPLINE) ? 2 : 1;
}

/* ---- Astra (v1.12.0) ---- */

/* RFO_SKILL_ASTRA: BattleGenerateRoundHits, once the round's strike count is known. Skl/2 % that
   the round becomes 5 strikes (a brave weapon too: Astra replaces the round), each at half damage
   (Reforged_SkillAstraHalf). At most one Astra a battle: the battle animations hold 9 strikes
   (gAnimRoundData), and 5 + 2 + 2 is 9 (the PC's gBattleHitArray holds 10, BATTLE_HIT_MAX). The
   check reads the hits so far, so it needs no state (the arena unwinds without BattleGenerate).
   Never in simulations (BattleRoll1RN), never in scripted battles, no random number unless the
   attacker has Astra. */
int Reforged_SkillAstra(struct BattleUnit *attacker)
{
    struct BattleHit *h;
    static int sLogged;

    if (!Reforged_SkillsOn() || sScripted || gBattleActor.unit.index == 0 || gBattleTarget.unit.index == 0)
        return 0;
    if (attacker->weapon == 0 || GetItemType(attacker->weapon) == ITYPE_BLLST)
        return 0;
    for (h = gBattleHitArray; h < gBattleHitIterator; h++)
        if (h->attributes & BATTLE_HIT_ATTR_5)
            return 0;
    if (!Has(attacker, SK_ASTRA))
        return 0;
    /* test switch FE8PC_ASTRA_ALWAYS: it always triggers in a real battle (no random number drawn) */
    if (getenv("FE8PC_ASTRA_ALWAYS") != NULL ? (gBattleStats.config & BATTLE_CONFIG_SIMULATE) != 0
                                              : !BattleRoll1RN(attacker->unit.skl / 2, FALSE))
        return 0;
    if (sLogged < 20)
        sLogged++, PortLog("[reforged] skills: Astra: unit 0x%02X strikes 5 times at half damage\n", (u8)attacker->unit.index);
    /* on the round's first hit, directly: BattleGenerateRoundHits' u16 attrs would drop bit 18 (it is
       ORed into each hit, so the bit set here stays) */
    RecordProc(RFO_PROC_ASTRA, 0);
    return 1;
}

/* RFO_SKILL_ASTRA_HALF: BattleGenerateHitAttributes, after the crit and Pavise, before the clamp: an
   Astra strike does half (at least 1 when it did any); a Silencer kill stays a kill. */
void Reforged_SkillAstraHalf(void)
{
    int d = gBattleStats.damage;

    if (!(gBattleHitIterator->attributes & BATTLE_HIT_ATTR_5) || (gBattleHitIterator->attributes & BATTLE_HIT_ATTR_SILENCER))
        return;
    if (d > 0)
        gBattleStats.damage = d / 2 > 0 ? d / 2 : 1;
}

#endif /* PORT_PLAYED_BUILD */

