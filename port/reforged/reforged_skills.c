#include "global.h"

/*
 * Reforged Options, Game fixes: "Skills" and "Enemy skills" (RFO_FIX_SKILLS, RFO_FIX_ENEMY_SKILLS;
 * the user's request of 2026-11-06/07 for class skills, the design "Skills proposal (draft).md"
 * decided 2026-11-07 "use your picks", and "promoted/boss enemies having skills is fine"). Both
 * off by default and fixed at New Game like the other game fixes (Reforged_RunFix: 0-2).
 *
 * This file is the core and the screens:
 *   - the class table (every class id: FE8's player classes of both sexes, the trainees' three
 *     tiers, the monsters; anything else has none): a "kept" skill and, for a promoted class,
 *     a promotion skill (design section 3);
 *   - the personal table (one skill per playable character; some only once promoted);
 *   - Reforged_SkillHas: who has what in this run (the options, the class, the character and,
 *     for anybody not blue, the Enemy skills rule; never personal skills off the blue side);
 *   - FE8's own skills (Canto, Steal, ...) listed from the class data as the game decides them,
 *     for the screens only: the game still runs them as before;
 *   - the names and one-line helps, and the two screens' texts: the promotion screen's
 *     "Gains / Keeps / Loses" per choice (src/classchg-sel.c RFO_SKILL_PROMO_DESC, the user's
 *     requirement of 2026-11-07: never promote, look and reload to find out) and the stat
 *     screen's R help on the class name (src/statscreen.c RFO_SKILL_CLASS_HELP). Both are
 *     virtual text ids 0x7F50+ (reforged_items.c routes them here; src/msg.c never caches them,
 *     include/bmitem.h RFO_VMSG_CACHEABLE).
 *
 * v1.12.0 (the owner's go-ahead of 2026-11-07: "all sound good, but add Astra to heroes/
 * swordsmasters", and Astra replacing Armsthrift on Heroes): Pass (Thief, Rogue, Assassin, and
 * Tethys), Astra (Hero: Astra + Sol, Astra fixed so a promoted Mercenary gives up Armsthrift;
 * Swordmaster: Vantage + Astra), Nihil (bosses, with Enemy skills on).
 */

#if PORT_PLAYED_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmunit.h"
#include "bmidoten.h"
#include "bmmap.h"
#include "bmbattle.h"
#include "bmitem.h"
#include "bmsave.h"
#include "variables.h"
#include "constants/classes.h"
#include "constants/characters.h"

#include "port_hw.h"
#include "port_reforged.h"
#include "reforged_skills.h"

#define SK_CLASS_IDS 0x80 /* class ids 0x00-0x7F (CLASS_PUPIL_T1 is the last) */

/* --- The class table: { kept, promotion }. A class FE8 already gives a combat skill (Sniper,
   General, Wyvern Knight, Swordmaster, Berserker, Assassin, Bishop, Summoner) has that as its
   promotion skill: it comes from the class data (ClassOwnSkills), so it is not repeated here. --- */
static const u8 sClassSkills[SK_CLASS_IDS][2] = {
    /* lords */
    [CLASS_EPHRAIM_LORD]        = { SK_CHARM, 0 },
    [CLASS_EIRIKA_LORD]         = { SK_CHARM, 0 },
    [CLASS_EPHRAIM_MASTER_LORD] = { SK_CHARM, SK_LUNA },
    [CLASS_EIRIKA_MASTER_LORD]  = { SK_CHARM, SK_SOL },
    /* cavalry */
    [CLASS_CAVALIER]            = { SK_SAVIOR, 0 },
    [CLASS_CAVALIER_F]          = { SK_SAVIOR, 0 },
    [CLASS_PALADIN]             = { SK_SAVIOR, SK_AEGIS },
    [CLASS_PALADIN_F]           = { SK_SAVIOR, SK_AEGIS },
    [CLASS_GREAT_KNIGHT]        = { SK_SAVIOR, SK_PAVISE },
    [CLASS_GREAT_KNIGHT_F]      = { SK_SAVIOR, SK_PAVISE },
    /* armour (General: + Great Shield) */
    [CLASS_ARMOR_KNIGHT]        = { SK_WARY, 0 },
    [CLASS_ARMOR_KNIGHT_F]      = { SK_WARY, 0 },
    [CLASS_GENERAL]             = { SK_WARY, 0 },
    [CLASS_GENERAL_F]           = { SK_WARY, 0 },
    /* thieves (Assassin: + Silencer) */
    [CLASS_THIEF]               = { SK_PASS, 0 },
    [CLASS_ROGUE]               = { SK_PASS, SK_POISONSTRIKE },
    [CLASS_ASSASSIN]            = { SK_PASS, 0 },
    [CLASS_ASSASSIN_F]          = { SK_PASS, 0 },
    /* swords (Swordmaster: + Critical +15) */
    [CLASS_MERCENARY]           = { SK_ARMSTHRIFT, 0 },
    [CLASS_MERCENARY_F]         = { SK_ARMSTHRIFT, 0 },
    [CLASS_HERO]                = { SK_ASTRA, SK_SOL }, /* Astra fixed: ClassFixedKept */
    [CLASS_HERO_F]              = { SK_ASTRA, SK_SOL },
    [CLASS_MYRMIDON]            = { SK_VANTAGE, 0 },
    [CLASS_MYRMIDON_F]          = { SK_VANTAGE, 0 },
    [CLASS_SWORDMASTER]         = { SK_VANTAGE, SK_ASTRA },
    [CLASS_SWORDMASTER_F]       = { SK_VANTAGE, SK_ASTRA },
    /* bows (Sniper: + Sure Strike) */
    [CLASS_ARCHER]              = { SK_PATIENCE, 0 },
    [CLASS_ARCHER_F]            = { SK_PATIENCE, 0 },
    [CLASS_SNIPER]              = { SK_PATIENCE, 0 },
    [CLASS_SNIPER_F]            = { SK_PATIENCE, 0 },
    [CLASS_RANGER]              = { SK_PATIENCE, SK_LIFETAKER },
    [CLASS_RANGER_F]            = { SK_PATIENCE, SK_LIFETAKER },
    /* wyverns (Wyvern Knight: + Pierce) */
    [CLASS_WYVERN_RIDER]        = { SK_STR2, 0 },
    [CLASS_WYVERN_RIDER_F]      = { SK_STR2, 0 },
    [CLASS_WYVERN_LORD]         = { SK_STR2, SK_SWORDBREAKER },
    [CLASS_WYVERN_LORD_F]       = { SK_STR2, SK_SWORDBREAKER },
    [CLASS_WYVERN_KNIGHT]       = { SK_STR2, 0 },
    [CLASS_WYVERN_KNIGHT_F]     = { SK_STR2, 0 },
    /* magic (Bishop: + Slayer; Summoner: + Summon) */
    [CLASS_MAGE]                = { SK_MAG2, 0 },
    [CLASS_MAGE_F]              = { SK_MAG2, 0 },
    [CLASS_SAGE]                = { SK_MAG2, SK_LUNA },
    [CLASS_SAGE_F]              = { SK_MAG2, SK_LUNA },
    [CLASS_MAGE_KNIGHT]         = { SK_MAG2, SK_CELERITY },
    [CLASS_MAGE_KNIGHT_F]       = { SK_MAG2, SK_CELERITY },
    [CLASS_BISHOP]              = { SK_LIVETOSERVE, 0 },
    [CLASS_BISHOP_F]            = { SK_LIVETOSERVE, 0 },
    [CLASS_SHAMAN]              = { SK_HEX, 0 },
    [CLASS_SHAMAN_F]            = { SK_HEX, 0 },
    [CLASS_DRUID]               = { SK_HEX, SK_ANATHEMA },
    [CLASS_DRUID_F]             = { SK_HEX, SK_ANATHEMA },
    [CLASS_SUMMONER]            = { SK_HEX, 0 },
    [CLASS_SUMMONER_F]          = { SK_HEX, 0 },
    [CLASS_MONK]                = { SK_MIRACLE, 0 },
    [CLASS_PRIEST]              = { SK_LIVETOSERVE, 0 },
    [CLASS_CLERIC]              = { SK_LIVETOSERVE, 0 },
    [CLASS_TROUBADOUR]          = { SK_SAVIOR, 0 },
    [CLASS_VALKYRIE]            = { SK_SAVIOR, SK_RENEWAL },
    /* axes (Berserker: + Critical +15) */
    [CLASS_FIGHTER]             = { SK_WRATH, 0 },
    [CLASS_WARRIOR]             = { SK_WRATH, SK_LUNA },
    [CLASS_BRIGAND]             = { SK_DESPOIL, 0 },
    [CLASS_PIRATE]              = { SK_DESPOIL, 0 },
    [CLASS_BERSERKER]           = { SK_DESPOIL, 0 },
    /* fliers */
    [CLASS_PEGASUS_KNIGHT]      = { SK_TANTIVY, 0 },
    [CLASS_FALCON_KNIGHT]       = { SK_TANTIVY, SK_BOWBREAKER },
    /* the trainees, all three tiers: Underdog; a promoted Pupil adds Luna (TraineePromo), a
       promoted Journeyman or Recruit has FE8's own Critical +15 */
    [CLASS_RECRUIT_T1]          = { SK_UNDERDOG, 0 },
    [CLASS_JOURNEYMAN_T2]       = { SK_UNDERDOG, 0 },
    [CLASS_PUPIL_T2]            = { SK_UNDERDOG, 0 },
    [CLASS_RECRUIT_T2]          = { SK_UNDERDOG, 0 },
    [CLASS_JOURNEYMAN]          = { SK_UNDERDOG, 0 },
    [CLASS_PUPIL]               = { SK_UNDERDOG, 0 },
    [CLASS_RECRUIT]             = { SK_UNDERDOG, 0 },
    [CLASS_JOURNEYMAN_T1]       = { SK_UNDERDOG, 0 },
    [CLASS_PUPIL_T1]            = { SK_UNDERDOG, 0 },
    /* monsters (the design's monster table; the randomizer can hand these out) */
    [CLASS_REVENANT]            = { SK_HP5, 0 },
    [CLASS_ENTOUMBED]           = { SK_HP5, SK_RENEWAL },
    [CLASS_BONEWALKER]          = { SK_ARMSTHRIFT, 0 },
    [CLASS_BONEWALKER_BOW]      = { SK_PATIENCE, 0 },
    [CLASS_WIGHT]               = { SK_ARMSTHRIFT, SK_PAVISE },
    /* the design's "Sure Strike" for the bow Wight: FE8 decides Sure Strike by class id in
       bmbattle.c (Sniper only), so listing it would promise what the battle never does; Luna,
       the nearest attack skill that is built, instead */
    [CLASS_WIGHT_BOW]           = { SK_PATIENCE, SK_LUNA },
    [CLASS_BAEL]                = { SK_POISONSTRIKE, 0 },
    [CLASS_ELDER_BAEL]          = { SK_POISONSTRIKE, SK_LIFETAKER },
    [CLASS_ELDER_BAEL_2]        = { SK_POISONSTRIKE, SK_LIFETAKER },
    [CLASS_CYCLOPS]             = { SK_WRATH, SK_WARY },
    [CLASS_CYCLOPS_2]           = { SK_WRATH, SK_WARY },
    [CLASS_MAUTHEDOOG]          = { SK_VANTAGE, 0 },
    [CLASS_GWYLLGI]             = { SK_VANTAGE, SK_SOL },
    /* Tarvos: the design's Canto is FE8's own (class data); Maelduin adds Luna */
    [CLASS_TARVOS]              = { SK_CELERITY, 0 }, /* the design's Canto: FE8's Tarvos has no CA_CANTO */
    [CLASS_MAELDUIN]            = { SK_CELERITY, SK_LUNA },
    [CLASS_MOGALL]              = { SK_HEX, 0 },
    [CLASS_ARCH_MOGALL]         = { SK_HEX, SK_ANATHEMA },
    [CLASS_GORGON]              = { SK_HEX, SK_ANATHEMA },
    [CLASS_GARGOYLE]            = { SK_TANTIVY, 0 },
    [CLASS_DEATHGOYLE]          = { SK_TANTIVY, SK_AEGIS },
};

/* --- The personal table (Skills = 2, blue units only). afterPromo: only once the unit's class
   is promoted (the design's star). --- */
static const struct { u8 pid, skill, afterPromo; } sPersonal[] = {
    { CHARACTER_EIRIKA,   SK_LANCEBREAKER, 1 },
    { CHARACTER_EPHRAIM,  SK_AXEBREAKER,   1 },
    { CHARACTER_SETH,     SK_CHARM,        0 },
    { CHARACTER_FRANZ,    SK_DISCIPLINE,   0 },
    { CHARACTER_GILLIAM,  SK_TOMEBREAKER,  1 },
    { CHARACTER_MOULDER,  SK_SAVIOR,       0 },
    { CHARACTER_VANESSA,  SK_BOWBREAKER,   1 },
    { CHARACTER_ROSS,     SK_WRATH,        0 },
    { CHARACTER_GARCIA,   SK_SWORDBREAKER, 1 },
    { CHARACTER_NEIMI,    SK_UNDERDOG,     0 },
    { CHARACTER_COLM,     SK_CELERITY,     1 },
    { CHARACTER_ARTUR,    SK_MIRACLE,      0 },
    { CHARACTER_LUTE,     SK_DISCIPLINE,   0 },
    { CHARACTER_NATASHA,  SK_RENEWAL,      1 },
    { CHARACTER_JOSHUA,   SK_DESPOIL,      0 },
    { CHARACTER_FORDE,    SK_PATIENCE,     0 },
    { CHARACTER_KYLE,     SK_DEF2,         0 },
    { CHARACTER_TANA,     SK_CHARM,        0 },
    { CHARACTER_AMELIA,   SK_UNDERDOG,     0 },
    { CHARACTER_INNES,    SK_TANTIVY,      0 },
    { CHARACTER_GERIK,    SK_STR2,         0 },
    { CHARACTER_TETHYS,   SK_PASS,         0 },
    { CHARACTER_MARISA,   SK_WRATH,        0 },
    { CHARACTER_LARACHEL, SK_MIRACLE,      0 },
    { CHARACTER_DOZLA,    SK_HP5,          0 },
    { CHARACTER_SALEH,    SK_MAG2,         0 },
    { CHARACTER_EWAN,     SK_DISCIPLINE,   0 },
    { CHARACTER_CORMAG,   SK_LIFETAKER,    1 },
    { CHARACTER_RENNAC,   SK_DESPOIL,      0 },
    { CHARACTER_DUESSEL,  SK_AEGIS,        0 },
    { CHARACTER_KNOLL,    SK_HEX,          0 },
    { CHARACTER_MYRRH,    SK_RENEWAL,      0 },
    { CHARACTER_SYRENE,   SK_SAVIOR,       0 },
};
#define SK_PERSONAL_COUNT ((int)(sizeof(sPersonal) / sizeof(sPersonal[0])))

/* --- Names and one-line helps. The stat screen's R help shows "Name: help" a line and a help
   box is at most 192 px wide (src/helpbox.c), so "Name: help" stays within about 33 characters. --- */
static const char *const sNames[SK_COUNT] = {
    [SK_NONE] = "",
    [SK_CANTO] = "Canto",
    [SK_STEAL] = "Steal",
    [SK_LOCKTOUCH] = "Locktouch",
    [SK_CRIT15] = "Critical +15", /* FE8 gives it no name (decided 2026-11-07) */
    [SK_SILENCER] = "Silencer",
    [SK_SURESTRIKE] = "Sure Strike",
    [SK_PIERCE] = "Pierce",
    [SK_GREATSHIELD] = "Great Shield",
    [SK_SLAYER] = "Slayer",
    [SK_SUMMON] = "Summon",
    [SK_DANCE] = "Dance",
    [SK_CHARM] = "Charm",
    [SK_SAVIOR] = "Savior",
    [SK_WARY] = "Wary Fighter",
    [SK_ARMSTHRIFT] = "Armsthrift",
    [SK_VANTAGE] = "Vantage",
    [SK_PATIENCE] = "Patience",
    [SK_WRATH] = "Wrath",
    [SK_DESPOIL] = "Despoil",
    [SK_MIRACLE] = "Miracle",
    [SK_LIVETOSERVE] = "Live to Serve",
    [SK_STR2] = "Str +2",
    [SK_MAG2] = "Mag +2",
    [SK_SKL2] = "Skl +2",
    [SK_SPD2] = "Spd +2",
    [SK_LCK2] = "Lck +2",
    [SK_DEF2] = "Def +2",
    [SK_RES2] = "Res +2",
    [SK_HP5] = "HP +5",
    [SK_HEX] = "Hex",
    [SK_TANTIVY] = "Tantivy",
    [SK_UNDERDOG] = "Underdog",
    [SK_DISCIPLINE] = "Discipline",
    [SK_SOL] = "Sol",
    [SK_LUNA] = "Luna",
    [SK_PAVISE] = "Pavise",
    [SK_AEGIS] = "Aegis",
    [SK_LIFETAKER] = "Lifetaker",
    [SK_POISONSTRIKE] = "Poison Strike",
    [SK_RENEWAL] = "Renewal",
    [SK_CELERITY] = "Celerity",
    [SK_ANATHEMA] = "Anathema",
    [SK_SWORDBREAKER] = "Swordbreaker",
    [SK_LANCEBREAKER] = "Lancebreaker",
    [SK_AXEBREAKER] = "Axebreaker",
    [SK_BOWBREAKER] = "Bowbreaker",
    [SK_TOMEBREAKER] = "Tomebreaker",
    [SK_ASTRA] = "Astra",
    [SK_NIHIL] = "Nihil",
    [SK_PASS] = "Pass",
};

static const char *const sHelps[SK_COUNT] = {
    [SK_NONE] = "",
    [SK_CANTO] = "Move after acting",
    [SK_STEAL] = "Take items (Spd)",
    [SK_LOCKTOUCH] = "Uses Lockpicks",
    [SK_CRIT15] = "+15 Crit",
    [SK_SILENCER] = "Crits may kill",
    [SK_SURESTRIKE] = "Level %: no miss",
    [SK_PIERCE] = "Level %: ignore Def",
    [SK_GREATSHIELD] = "Level %: no damage",
    [SK_SLAYER] = "Effective vs monsters",
    [SK_SUMMON] = "Calls a Phantom",
    [SK_DANCE] = "Refreshes an ally",
    [SK_CHARM] = "Allies near +5 Hit/Avo",
    [SK_SAVIOR] = "Rescue: no penalty",
    [SK_WARY] = "No one doubles",
    [SK_ARMSTHRIFT] = "Lck x2 %: no wear",
    [SK_VANTAGE] = "Half HP: strike first",
    [SK_PATIENCE] = "Foe phase +10 Hit/Avo",
    [SK_WRATH] = "Half HP: +20 Crit",
    [SK_DESPOIL] = "Kill: Lck % 300 gold",
    [SK_MIRACLE] = "Lck %: live on 1 HP",
    [SK_LIVETOSERVE] = "Healing heals you",
    [SK_STR2] = "Strength +2",
    [SK_MAG2] = "Magic +2",
    [SK_SKL2] = "Skill +2",
    [SK_SPD2] = "Speed +2",
    [SK_LCK2] = "Luck +2",
    [SK_DEF2] = "Defense +2",
    [SK_RES2] = "Resistance +2",
    [SK_HP5] = "Max HP +5",
    [SK_HEX] = "Adjacent foes -15 Avo",
    [SK_TANTIVY] = "Alone: +10 Hit/Avo",
    [SK_UNDERDOG] = "Higher Lv: +15 Hit/Avo",
    [SK_DISCIPLINE] = "Weapon EXP x2",
    [SK_SOL] = "Skl/2 %: heal damage",
    [SK_LUNA] = "Skl/2 %: halve Def",
    [SK_PAVISE] = "Skl %: halve melee",
    [SK_AEGIS] = "Skl %: halve bow/magic",
    [SK_LIFETAKER] = "Kill: heal half HP",
    [SK_POISONSTRIKE] = "Foe loses 20% HP",
    [SK_RENEWAL] = "Turn start +20% HP",
    [SK_CELERITY] = "Move +1",
    [SK_ANATHEMA] = "Foes in 3 -10 Avo/Ddg",
    [SK_SWORDBREAKER] = "+30 Hit/Avo: swords",
    [SK_LANCEBREAKER] = "+30 Hit/Avo: lances",
    [SK_AXEBREAKER] = "+30 Hit/Avo: axes",
    [SK_BOWBREAKER] = "+30 Hit/Avo: bows",
    [SK_TOMEBREAKER] = "+30 Hit/Avo: magic",
    [SK_ASTRA] = "Skl/2 %: 5 hits, half damage",
    [SK_NIHIL] = "Foe's battle skills do nothing",
    [SK_PASS] = "Move through enemy squares",
};

const char *Reforged_SkillName(int skill)
{
    return skill > SK_NONE && skill < SK_COUNT && sNames[skill] != NULL ? sNames[skill] : "";
}

const char *Reforged_SkillHelp(int skill)
{
    return skill > SK_NONE && skill < SK_COUNT && sHelps[skill] != NULL ? sHelps[skill] : "";
}

int Reforged_SkillsOn(void)
{
    return Reforged_RunFix(RFO_FIX_SKILLS) != 0 || Reforged_RunFix(RFO_FIX_ENEMY_SKILLS) != 0;
}

int Reforged_SkillIsBattleCopy(const struct Unit *unit)
{
    /* InitBattleUnit copies a unit's totals into these: a getter on them must not add a bonus
       again. Only these two (a stack copy, as BattleGenerateUiStats makes, is a real unit). */
    return unit == &gBattleActor.unit || unit == &gBattleTarget.unit;
}

/* --- Who has skills --- */

/* The class whose rows (sClassSkills) and guards apply to class jid: itself, or for an FE7 class
   (0x80-0x88, reforged_fe7.c; the table has no rows past 0x7F) its FE8 stand-in: Nomad an Archer,
   Blade Lord a Swordmaster, Eirika's Blade Lord her Master Lord (Charm), Great Lord a General,
   Ephraim's Great Lord his Master Lord, Archsage a Sage, Dark Druid a Druid, Bard a Dancer. FE8's own
   attributes (Canto) are still read from the real class. */
#define SK_JID(jid) Reforged_Fe7SkillClass(jid)

static const struct ClassData *ClassOf(int jid)
{
    return jid > 0 && SK_JID(jid) < SK_CLASS_IDS ? GetClassData(jid) : NULL;
}

/* Which kinds a unit of this faction, class and character has in this run: bit 0 class skills,
   bit 1 personal skills. Blue: Skills >= 1 gives class skills, 2 also personal. Anybody else
   (red, green): Enemy skills 2 gives class skills, 1 only to a promoted class or a boss; never
   personal skills (they belong to the player's characters). */
static int Gate(int faction, const struct ClassData *cd, const struct CharacterData *chd)
{
    int v;

    if (cd == NULL)
        return 0;
    if (faction == FACTION_BLUE)
    {
        v = Reforged_RunFix(RFO_FIX_SKILLS);
        return v >= 2 ? 3 : v >= 1 ? 1 : 0;
    }
    v = Reforged_RunFix(RFO_FIX_ENEMY_SKILLS);
    if (v >= 2)
        return 1;
    if (v == 1 && ((cd->attributes & CA_PROMOTED) || (cd->attributes & CA_BOSS) ||
                   (chd != NULL && (chd->attributes & CA_BOSS))))
        return 1;
    return 0;
}

/* The trainees' promoted tiers: a promoted Pupil gains Luna (a promoted Journeyman or Recruit
   gets FE8's own Critical +15 from its class data). The class data decides which tier is
   promoted, so the table needs no guess about FE8's trainee ids. */
static int TraineePromo(const struct ClassData *cd)
{
    switch (cd->number)
    {
    case CLASS_PUPIL:
    case CLASS_PUPIL_T2:
    case CLASS_PUPIL_T1:
        return (cd->attributes & CA_PROMOTED) ? SK_LUNA : 0;
    }
    return 0;
}

/* The class's two new skills (kept, promotion); 0 for none. */
static void ClassNew(const struct ClassData *cd, int *kept, int *promo)
{
    *kept = sClassSkills[SK_JID(cd->number)][0];
    *promo = sClassSkills[SK_JID(cd->number)][1];
    if (*promo == 0)
        *promo = TraineePromo(cd);
}

/* The character's personal skill for this class (0: none, or not yet promoted). */
static int PersonalSkill(int pid, const struct ClassData *cd)
{
    int k;

    for (k = 0; k < SK_PERSONAL_COUNT; k++)
        if (sPersonal[k].pid == pid)
            return sPersonal[k].afterPromo && !(cd->attributes & CA_PROMOTED) ? 0 : sPersonal[k].skill;
    return 0;
}

/* FE8's own skills of a class, as the game decides them (the class attributes; Sure Strike,
   Pierce, Great Shield and Slayer by class id: bmbattle.c BattleCheckSureShot, BattleCheckPierce,
   BattleCheckGreatShield, bmitem.c IsUnitEffectiveAgainst). */
static int ClassOwnSkills(const struct ClassData *cd, u8 *out, int max)
{
    int n = 0;
    u32 a = cd->attributes;

#define SK_ADD_OWN(cond, sk) do { if ((cond) && n < max) out[n++] = (sk); } while (0)
    SK_ADD_OWN(a & CA_CANTO, SK_CANTO);
    SK_ADD_OWN(a & CA_STEAL, SK_STEAL);
    SK_ADD_OWN(a & CA_THIEF, SK_LOCKTOUCH);
    SK_ADD_OWN(a & CA_CRITBONUS, SK_CRIT15);
    SK_ADD_OWN(a & CA_ASSASSIN, SK_SILENCER);
    SK_ADD_OWN(cd->number == CLASS_SNIPER || cd->number == CLASS_SNIPER_F, SK_SURESTRIKE);
    SK_ADD_OWN(cd->number == CLASS_WYVERN_KNIGHT || cd->number == CLASS_WYVERN_KNIGHT_F, SK_PIERCE);
    SK_ADD_OWN(cd->number == CLASS_GENERAL || cd->number == CLASS_GENERAL_F, SK_GREATSHIELD);
    SK_ADD_OWN(cd->number == CLASS_BISHOP || cd->number == CLASS_BISHOP_F, SK_SLAYER);
    SK_ADD_OWN(a & CA_SUMMON, SK_SUMMON);
    SK_ADD_OWN(a & (CA_DANCE | CA_PLAY), SK_DANCE);
#undef SK_ADD_OWN
    return n;
}

static int AddUnique(u8 *out, int n, int max, int sk)
{
    int i;

    if (sk == SK_MAG2)
        sk = SK_STR2; /* the same stat (pow): one entry, named Str or Mag by the class (ShownName) */

    if (sk <= SK_NONE || sk >= SK_COUNT || n >= max)
        return n;
    for (i = 0; i < n; i++)
        if (out[i] == sk)
            return n;
    out[n] = (u8)sk;
    return n + 1;
}

static int BossNihil(int faction, const struct ClassData *cd, const struct CharacterData *chd);

/* The list for a class, character and gate: the kept skill, the promotion skill, FE8's own,
   then the personal one. */
static int ListFor(const struct ClassData *cd, int pid, int gate, u8 *out, int max)
{
    u8 own[12];
    int n = 0, kept, promo, i, k;

    if (cd == NULL || gate == 0 || max <= 0)
        return 0;
    if (gate & 1)
    {
        ClassNew(cd, &kept, &promo);
        n = AddUnique(out, n, max, kept);
        n = AddUnique(out, n, max, promo);
        k = ClassOwnSkills(cd, own, (int)sizeof(own));
        for (i = 0; i < k; i++)
            n = AddUnique(out, n, max, own[i]);
    }
    if (gate & 2)
        n = AddUnique(out, n, max, PersonalSkill(pid, cd));
    return n;
}

int Reforged_SkillList(int jid, int pid, int faction, unsigned char *out, int max)
{
    const struct ClassData *cd = ClassOf(jid);
    const struct CharacterData *chd = pid > 0 ? GetCharacterData(pid) : NULL;
    int n = ListFor(cd, pid, Gate(faction, cd, chd), out, max);

    if (BossNihil(faction, cd, chd))
        n = AddUnique(out, n, max, SK_NIHIL);
    return n;
}

/* --- Kept skills (v1.6.0, the owner's choice of 2026-11-07 "Keep the old skill"): a promoted
   blue unit has the skill its previous class gave it in the class's "kept" slot, not the new
   class's own; the new class adds its promotion skill. Recorded per character when it promotes
   (Reforged_SkillPromoting), kept with each save block (reforged_savemark.c "<block>_kept"),
   cleared at New Game (reforged_gamefixes.c). Only blue units in a promoted class use it. --- */

#define SK_KEPT_PIDS 0x100 /* the player's characters are below 0x46; every u8 pid is safe */
static u8 sKept[SK_KEPT_PIDS];

int Reforged_SkillKept(int pid)
{
    int sk = pid > 0 && pid < SK_KEPT_PIDS ? sKept[pid] : 0;

    /* v1.6.0 to v1.11.0 gave thieves Spd +2 in Pass's place, and only thieves passed Spd +2 on:
       a thief promoted then keeps Pass now */
    return sk == SK_SPD2 ? SK_PASS : sk;
}

void Reforged_SkillKeptSet(int pid, int skill)
{
    if (pid > 0 && pid < SK_KEPT_PIDS)
        sKept[pid] = (u8)(skill >= SK_FIRST_NEW && skill < SK_COUNT ? skill : 0); /* only new skills are kept */
}

void Reforged_SkillKeptReset(void)
{
    memset(sKept, 0, sizeof(sKept));
}

/* The skill a unit of class cd (character pid) passes on when it promotes: an unpromoted
   class's kept slot; from a promoted class (a guard: FE8 never promotes one), the skill it
   already keeps, else that class's kept slot. 0: none. */
static int KeptFrom(const struct ClassData *cd, int pid)
{
    int kept, promo, had;

    ClassNew(cd, &kept, &promo);
    if (cd->attributes & CA_PROMOTED)
    {
        had = Reforged_SkillKept(pid);
        return had ? had : kept;
    }
    return kept;
}

/* bmbattle.c ApplyUnitPromotion / ApplyUnitDefaultPromotion (RANDO_HOOK_PROMOTING, through
   reforged_rando_classes.c Reforged_RandoPromoting): the unit is still in its old class. */
void Reforged_SkillPromoting(struct Unit *unit)
{
    const struct ClassData *cd;
    int pid, sk;

    if (unit == NULL || unit->pCharacterData == NULL || (cd = unit->pClassData) == NULL)
        return;
    if (!Reforged_RunFix(RFO_FIX_SKILLS) || UNIT_FACTION(unit) != FACTION_BLUE || SK_JID(cd->number) >= SK_CLASS_IDS)
        return;
    pid = unit->pCharacterData->number;
    sk = KeptFrom(cd, pid);
    Reforged_SkillKeptSet(pid, sk);
    PortLog("[reforged] skills: pid 0x%02X promotes from class 0x%02X and keeps %s\n", pid, cd->number,
        sk ? Reforged_SkillName(sk) : "nothing (the new class's own)");
}

/* A class whose first slot is its own whatever the unit kept (the owner's choice of 2026-11-07:
   Heroes get Astra, and a promoted Mercenary gives up Armsthrift for it). */
static int ClassFixedKept(const struct ClassData *cd)
{
    return cd != NULL && (cd->number == CLASS_HERO || cd->number == CLASS_HERO_F);
}

/* The class's two new skills for this unit: a blue unit in a promoted class has its kept skill
   (when one is recorded) in place of the class's kept slot, but for a fixed class. */
static void UnitClassNew(const struct Unit *unit, const struct ClassData *cd, int *kept, int *promo)
{
    int had;

    ClassNew(cd, kept, promo);
    if (UNIT_FACTION(unit) == FACTION_BLUE && (cd->attributes & CA_PROMOTED) && !ClassFixedKept(cd) &&
        (had = Reforged_SkillKept(unit->pCharacterData->number)) != 0)
        *kept = had;
}

/* Nihil: enemy and green bosses, with Enemy skills on (either mode). */
static int BossNihil(int faction, const struct ClassData *cd, const struct CharacterData *chd)
{
    /* test switch FE8PC_TEST_NIHIL_FOES: every enemy and green unit has Nihil (recordings) */
    if (faction != FACTION_BLUE && cd != NULL && getenv("FE8PC_TEST_NIHIL_FOES") != NULL)
        return 1;
    if (faction == FACTION_BLUE || !Reforged_RunFix(RFO_FIX_ENEMY_SKILLS) || cd == NULL)
        return 0;
    return (cd->attributes & CA_BOSS) || (chd != NULL && (chd->attributes & CA_BOSS));
}

/* A unit's list: FE8's own skills first, then the kept and promotion skills, then the personal
   one (gate as Gate's). */
static int UnitListFor(const struct ClassData *cd, int gate, int kept, int promo, int pid, u8 *out, int max)
{
    u8 own[12];
    int n = 0, i, k;

    if (cd == NULL || gate == 0 || max <= 0)
        return 0;
    if (gate & 1)
    {
        k = ClassOwnSkills(cd, own, (int)sizeof(own));
        for (i = 0; i < k; i++)
            n = AddUnique(out, n, max, own[i]);
        n = AddUnique(out, n, max, kept);
        n = AddUnique(out, n, max, promo);
    }
    if (gate & 2)
        n = AddUnique(out, n, max, PersonalSkill(pid, cd));
    return n;
}

int Reforged_SkillUnitList(const struct Unit *unit, unsigned char *out, int max)
{
    const struct ClassData *cd;
    int gate, kept = 0, promo = 0, n;

    if (unit == NULL || out == NULL || unit->pCharacterData == NULL || (cd = unit->pClassData) == NULL ||
        SK_JID(cd->number) >= SK_CLASS_IDS)
        return 0;
    gate = Gate(UNIT_FACTION(unit), cd, unit->pCharacterData);
    if (gate & 1)
        UnitClassNew(unit, cd, &kept, &promo);
    n = UnitListFor(cd, gate, kept, promo, unit->pCharacterData->number, out, max);
    if (BossNihil(UNIT_FACTION(unit), cd, unit->pCharacterData))
        n = AddUnique(out, n, max, SK_NIHIL);
    return n;
}

void Reforged_SkillPromoChange(const struct Unit *unit, int jid, unsigned char *gains, int *ng,
    unsigned char *keeps, int *nk, unsigned char *loses, int *nl)
{
    const struct ClassData *cur, *next;
    u8 now[16], after[16];
    int na, nb, i, j, gate, kept = 0, promo = 0, pid, faction, carried, g = 0, k = 0, l = 0;

    if (ng) *ng = 0;
    if (nk) *nk = 0;
    if (nl) *nl = 0;
    if (unit == NULL || unit->pCharacterData == NULL || (cur = unit->pClassData) == NULL ||
        SK_JID(cur->number) >= SK_CLASS_IDS || (next = ClassOf(jid)) == NULL)
        return;
    pid = unit->pCharacterData->number;
    faction = UNIT_FACTION(unit);
    na = Reforged_SkillUnitList(unit, now, (int)sizeof(now));
    /* after: what Reforged_SkillHas would give in class jid once Reforged_SkillPromoting has
       recorded the old class's skill (a blue unit into a promoted class) */
    gate = Gate(faction, next, unit->pCharacterData);
    if (gate & 1)
    {
        ClassNew(next, &kept, &promo);
        if (faction == FACTION_BLUE && (next->attributes & CA_PROMOTED) && !ClassFixedKept(next) &&
            (carried = KeptFrom(cur, pid)) != 0)
            kept = carried;
    }
    nb = UnitListFor(next, gate, kept, promo, pid, after, (int)sizeof(after));
    for (i = 0; i < nb; i++)
    {
        for (j = 0; j < na && now[j] != after[i]; j++)
            ;
        if (j < na)
        {
            if (keeps && k < 8)
                keeps[k++] = after[i];
        }
        else if (gains && g < 8)
            gains[g++] = after[i];
    }
    for (i = 0; i < na; i++)
    {
        for (j = 0; j < nb && after[j] != now[i]; j++)
            ;
        if (j == nb && loses && l < 8)
            loses[l++] = now[i];
    }
    if (ng) *ng = g;
    if (nk) *nk = k;
    if (nl) *nl = l;
}

/* The third trainee choice is offered once both lords' stories were cleared (src/classchg-sel.c
   Check3rdTraineeEnabled; read here without its first-run init, which writes the save). */
static int Trainee3rd(void)
{
    struct GlobalSaveInfo info;

    if (!ReadGlobalSaveInfo(&info))
        return 0;
    return (info.Eirk_mode_easy || info.Eirk_mode_norm || info.Eirk_mode_hard) &&
           (info.Ephy_mode_easy || info.Ephy_mode_norm || info.Ephy_mode_hard);
}

extern const int gPromoJidLutCount; /* src/classchg-data.c */

int Reforged_SkillPromoChoices(const struct Unit *unit, int *out)
{
    const struct ClassData *cd;
    int n = 0, j, c, third = 0;

    if (unit == NULL || out == NULL || unit->pCharacterData == NULL || (cd = unit->pClassData) == NULL)
        return 0;
    if ((cd->attributes & CA_PROMOTED) || cd->number >= gPromoJidLutCount || SK_JID(cd->number) >= SK_CLASS_IDS)
        return 0;
    {
        /* v1.21.0: a randomizer version 11-14 run's three choices (reforged_rando_classes.c, as the screen) */
        unsigned char three[3];

        if (Reforged_PromoList(unit, three) == 3)
        {
            for (j = 0; j < 3; j++)
                out[j] = three[j];
            return 3;
        }
    }
    for (j = 0; j < 2; j++)
    {
        /* as Make6C_PromotionMenuSelect: the table (the randomizer's when Random promotions is
           on) and Match gender's swap */
        c = Reforged_RandoPromoChoice((struct Unit *)unit, gPromoJidLut[cd->number][j]);
        if (ClassOf(c) != NULL && (n == 0 || out[0] != c))
            out[n++] = c;
    }
    if (n == 0)
        return 0;
    switch (cd->number)
    {
    case CLASS_JOURNEYMAN: third = CLASS_JOURNEYMAN_T1; break;
    case CLASS_PUPIL:      third = CLASS_PUPIL_T1;      break;
    case CLASS_RECRUIT:    third = CLASS_RECRUIT_T1;    break;
    }
    if (third != 0 && Trainee3rd())
        out[n++] = third;
    else if ((third = Reforged_Fe7ThirdPromo(cd->number)) != 0)
        out[n++] = third; /* the FE7 classes: a Myrmidon's Blade Lord, a Knight's Great Lord (src/classchg-sel.c) */
    return n;
}

int Reforged_SkillHas(const struct Unit *unit, int skill)
{
    const struct ClassData *cd;
    int gate, kept, promo, i, n;
    u8 list[16];

    if (unit == NULL || skill <= SK_NONE || skill >= SK_COUNT)
        return 0;
    cd = unit->pClassData;
    if (cd == NULL || unit->pCharacterData == NULL || SK_JID(cd->number) >= SK_CLASS_IDS)
        return 0;
    if (skill == SK_NIHIL)
        return BossNihil(UNIT_FACTION(unit), cd, unit->pCharacterData);
    gate = Gate(UNIT_FACTION(unit), cd, unit->pCharacterData);
    if (gate == 0)
        return 0;
    if (skill >= SK_FIRST_NEW)
    {
        /* the fast path (the stat getters call this often): the two tables (and the kept skill) only */
        if (gate & 1)
        {
            UnitClassNew(unit, cd, &kept, &promo);
            if (kept == skill || promo == skill)
                return 1;
        }
        return (gate & 2) && PersonalSkill(unit->pCharacterData->number, cd) == skill;
    }
    n = ListFor(cd, unit->pCharacterData->number, gate, list, (int)sizeof(list));
    for (i = 0; i < n; i++)
        if (list[i] == skill)
            return 1;
    return 0;
}

int Reforged_SkillPassUnit(const struct Unit *unit)
{
    return unit != NULL && Reforged_SkillHas(unit, SK_PASS);
}

/* GenerateMovementMap (include/bmunit.h RFO_SKILL_PASS): the moving unit has Pass, so no unit blocks
   its flood (allies never did; the squares it may end on are filtered as before). */
static int sPassForce; /* Reforged_SkillPassCheck: the next floods take Pass */

int Reforged_SkillPass(int uid)
{
    struct Unit *u;

    if (sPassForce)
        return 1;

    if (uid <= 0 || uid >= 0x100)
        return 0;
    u = GetUnit(uid);
    /* test switch FE8PC_TEST_PASS_BLUE: every player unit has Pass (recordings) */
    if (UNIT_IS_VALID(u) && UNIT_FACTION(u) == FACTION_BLUE && getenv("FE8PC_TEST_PASS_BLUE") != NULL)
    {
        static int sLogged;
        if (sLogged < 4000)
            sLogged++, PortLog("[reforged] skills test: Pass for unit 0x%02X's movement (frame %u)\n", (u8)u->index, gPortFrameCount);
        return 1;
    }
    if (!Reforged_SkillsOn())
        return 0;
    return UNIT_IS_VALID(u) && Reforged_SkillHas(u, SK_PASS);
}

/* The game fixes self-check (FE8PC_RFO_CHECK): a blue unit boxed in by enemies on its four sides
   reaches no other square without Pass and some with it (the flood itself, GenerateMovementMap).
   The map is put back. 1: passed (or no unit to try it on). */
int Reforged_SkillPassCheck(void)
{
    static const s8 adj[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
    u8 saved[4];
    int i, x, y, k, off = 0, on = 0, uid = 0;
    struct Unit *u = NULL;

    for (i = 1; i < 0x40 && uid == 0; i++)
    {
        u = GetUnit(i);
        if (UNIT_IS_VALID(u) && !(u->state & US_UNAVAILABLE) && u->xPos > 1 && u->yPos > 1 &&
            u->xPos < gBmMapSize.x - 2 && u->yPos < gBmMapSize.y - 2)
            uid = i;
    }
    if (uid == 0)
        return 1;
    for (k = 0; k < 4; k++)
    {
        saved[k] = gBmMapUnit[u->yPos + adj[k][1]][u->xPos + adj[k][0]];
        gBmMapUnit[u->yPos + adj[k][1]][u->xPos + adj[k][0]] = 0x81; /* an enemy's id: the flood checks the faction bit */
    }
    GenerateUnitMovementMap(u);
    for (y = 0; y < gBmMapSize.y; y++)
        for (x = 0; x < gBmMapSize.x; x++)
            off += gBmMapMovement[y][x] <= MAP_MOVEMENT_MAX;
    sPassForce = 1;
    GenerateUnitMovementMap(u);
    sPassForce = 0;
    for (y = 0; y < gBmMapSize.y; y++)
        for (x = 0; x < gBmMapSize.x; x++)
            on += gBmMapMovement[y][x] <= MAP_MOVEMENT_MAX;
    for (k = 0; k < 4; k++)
        gBmMapUnit[u->yPos + adj[k][1]][u->xPos + adj[k][0]] = saved[k];
    PortLog("[reforged] skills check: Pass: unit 0x%02X boxed in by enemies reaches %d square(s) without Pass, %d with\n",
        uid, off, on);
    return off == 1 && on > 1;
}

/* --- The screens --- */

/* A class that uses only staves and tomes (its starting ranks): Str +2 and Mag +2 are the same
   stat (pow), so such a class shows it as "Mag +2", any other as "Str +2". */
static int ClassIsMagic(const struct ClassData *cd)
{
    int i, magic = 0;

    if (cd == NULL)
        return 0;
    for (i = 0; i < 8; i++)
    {
        if (cd->baseRanks[i] == 0)
            continue;
        if (i < ITYPE_STAFF)
            return 0;
        magic = 1;
    }
    return magic;
}

static const char *ShownName(int skill, const struct ClassData *cd)
{
    if (skill == SK_STR2 || skill == SK_MAG2)
        return ClassIsMagic(cd) ? "Mag +2" : "Str +2";
    return Reforged_SkillName(skill);
}

static const char *ShownHelp(int skill, const struct ClassData *cd)
{
    if (skill == SK_STR2 || skill == SK_MAG2)
        return ClassIsMagic(cd) ? "Magic +2" : "Strength +2";
    return Reforged_SkillHelp(skill);
}

/* For the Skills page (reforged_skills_page.c): the name and help as class jid shows them. */
const char *Reforged_SkillShownName(int skill, int jid)
{
    return ShownName(skill, ClassOf(jid));
}

const char *Reforged_SkillShownHelp(int skill, int jid)
{
    return ShownHelp(skill, ClassOf(jid));
}

#define SK_LINE 38 /* characters per line: InitTalk's 2 lines are about 200 px */
#define SK_MSG_NL "\x01" /* the game's text line break */

static char sPromoText[3][160];
static char sClassHelp[256];

/* "Label" + the names, comma separated, into buf (size); its length. */
static int Segment(char *buf, int size, const char *label, const u8 *sk, int n, const struct ClassData *cd)
{
    int len, i;

    len = snprintf(buf, size, "%s", label);
    for (i = 0; i < n && len < size; i++)
        len += snprintf(buf + len, size - len, "%s%s", i ? ", " : "", ShownName(sk[i], cd));
    return len < size ? len : size - 1;
}

/* Two lines from up to three segments (gains, keeps, loses; empty ones skipped), joined by
   sep on a line: 1 when they fit SK_LINE characters a line. */
static int TwoLines(char *out, int size, char seg[3][96], const char *sep)
{
    const char *s[3];
    int n = 0, i, split, l1, l2, k;

    for (i = 0; i < 3; i++)
        if (seg[i][0])
            s[n++] = seg[i];
    if (n == 0)
        return 0;
    /* try each place for the line break: after segment split (1..n) */
    for (split = 1; split <= n; split++)
    {
        l1 = l2 = 0;
        for (k = 0; k < split; k++)
            l1 += (int)strlen(s[k]) + (k ? (int)strlen(sep) : 0);
        for (k = split; k < n; k++)
            l2 += (int)strlen(s[k]) + (k > split ? (int)strlen(sep) : 0);
        if (l1 <= SK_LINE && l2 <= SK_LINE)
        {
            int len = 0;

            out[0] = 0;
            for (k = 0; k < n; k++)
                len += snprintf(out + len, size - len, "%s%s", k == 0 ? "" : k == split ? SK_MSG_NL : sep, s[k]);
            return 1;
        }
    }
    return 0;
}

/* v1.23.0 (the owner: "make sure any gained skills + weapon skills are mentioned"): the promotion screen's text for
   the FE8 classes it can offer whose own description names no weapon, or not all of them, in a game with the Game
   fix "Third promotion" on (Reforged_Promo3Active; any other game shows the game's own text) (the two Great Lords;
   the third-tier Pupil, which says "Equip: Anima." and has light and dark too; the promoted monsters a random
   promotion can offer). This port's own words, two lines as FE8's; the game's text data is untouched and the
   stat screen's class help still shows it. Virtual ids 0x7F60 + row (Reforged_SkillVirtualMsg slots 16-31). */
#define SK_PROMO_FIX_SLOT 16
static const struct { u8 jid; const char *text; } sPromoDescFix[] = {
    { CLASS_EPHRAIM_MASTER_LORD, "Mounted lords of the finest qualities." SK_MSG_NL "Equip: Lances." },
    { CLASS_EIRIKA_MASTER_LORD, "Mounted lords of the finest qualities." SK_MSG_NL "Equip: Swords." },
    { CLASS_PUPIL_T2, "Mages in training with lofty" SK_MSG_NL "aspirations. Equip: Anima, light, dark." },
    { CLASS_ENTOUMBED, "Reanimated corpses, slow but strong." SK_MSG_NL "Equip: Claws." },
    { CLASS_WIGHT, "Skeletons driven by terrible magic." SK_MSG_NL "Equip: Swords, lances." },
    { CLASS_WIGHT_BOW, "Skeletons driven by terrible magic." SK_MSG_NL "Equip: Bows." },
    { CLASS_ELDER_BAEL, "Magic-infused giant spiders." SK_MSG_NL "Equip: Claws." },
    { CLASS_CYCLOPS, "One-eyed giants of great strength." SK_MSG_NL "Equip: Axes." },
    { CLASS_GWYLLGI, "Savage, three-headed hellhounds." SK_MSG_NL "Equip: Fangs." },
    { CLASS_MAELDUIN, "Immensely fast and strong creatures." SK_MSG_NL "Equip: Axes, bows." },
    { CLASS_ARCH_MOGALL, "Mogall of dread power." SK_MSG_NL "Equip: Dark." },
    { CLASS_GORGON, "Cursed creatures who turn all who" SK_MSG_NL "behold them into stone. Equip: Dark." },
    { CLASS_DEATHGOYLE, "Gargoyles empowered by magic." SK_MSG_NL "Equip: Lances." },
};

static int PromoDescFix(int jid, int desc)
{
    int i;

    if (!Reforged_Promo3Active())
        return desc; /* a game with the row off, a randomized game: the game's own text, as v1.22.0 */
    for (i = 0; i < (int)(sizeof(sPromoDescFix) / sizeof(sPromoDescFix[0])); i++)
        if (sPromoDescFix[i].jid == jid)
            return 0x7F50 + SK_PROMO_FIX_SLOT + i;
    return desc;
}

int Reforged_SkillPromoDesc(struct Unit *unit, int slot, int jid, int desc)
{
    const struct ClassData *cur, *next;
    u8 gain[8], keep[8], lose[8];
    int ng = 0, nk = 0, nl = 0, faction;
    char seg[3][96];
    char *out;

    desc = PromoDescFix(jid, desc);
    if (unit == NULL || slot < 0 || slot > 2 || unit->pCharacterData == NULL)
        return desc;
    cur = unit->pClassData;
    next = ClassOf(jid);
    if (cur == NULL || next == NULL)
        return desc;
    faction = UNIT_FACTION(unit);
    if (!Gate(faction, cur, unit->pCharacterData) && !Gate(faction, next, unit->pCharacterData))
        return desc; /* skills off for this unit: the class's own description */
    /* the unit's own skills now (its kept one included) against those it would have: the old
       class's skill passes on ("Keep the old skill"), so it is a "Keeps" */
    Reforged_SkillPromoChange(unit, jid, gain, &ng, keep, &nk, lose, &nl);
    out = sPromoText[slot];
    if (ng + nk + nl == 0)
    {
        strcpy(out, "No skills before or after.");
        return 0x7F50 + slot;
    }
    /* readable words first; then the short marks; then cut at the line length */
    seg[0][0] = seg[1][0] = seg[2][0] = 0;
    if (ng) Segment(seg[0], sizeof(seg[0]), "Gains: ", gain, ng, next);
    if (nk) Segment(seg[1], sizeof(seg[1]), "Keeps: ", keep, nk, next);
    if (nl) Segment(seg[2], sizeof(seg[2]), "Loses: ", lose, nl, cur);
    if (TwoLines(out, sizeof(sPromoText[slot]), seg, ". "))
        return 0x7F50 + slot;
    seg[0][0] = seg[1][0] = seg[2][0] = 0;
    if (ng) Segment(seg[0], sizeof(seg[0]), "+ ", gain, ng, next);
    if (nk) Segment(seg[1], sizeof(seg[1]), "= ", keep, nk, next);
    if (nl) Segment(seg[2], sizeof(seg[2]), "- ", lose, nl, cur);
    if (TwoLines(out, sizeof(sPromoText[slot]), seg, " / "))
        return 0x7F50 + slot;
    /* still too long (it would scroll): gains and keeps on line 1, losses on line 2, each cut */
    {
        char l1[96];

        snprintf(l1, sizeof(l1), "%s%s%s", seg[0], (seg[0][0] && seg[1][0]) ? " / " : "", seg[1]);
        l1[SK_LINE] = 0;
        seg[2][SK_LINE] = 0;
        if (l1[0] && seg[2][0])
            snprintf(out, sizeof(sPromoText[slot]), "%s" SK_MSG_NL "%s", l1, seg[2]);
        else
            snprintf(out, sizeof(sPromoText[slot]), "%s", l1[0] ? l1 : seg[2]);
    }
    return 0x7F50 + slot;
}

/* FE8's map-utility skills: on the stat screen they are named without an effect line. */
static int IsUtility(int skill)
{
    return skill == SK_CANTO || skill == SK_STEAL || skill == SK_LOCKTOUCH || skill == SK_SUMMON ||
           skill == SK_DANCE;
}

int Reforged_SkillClassHelp(struct Unit *unit, int desc)
{
    const struct ClassData *cd;
    u8 list[16], fx[16], util[16];
    int n, nf = 0, nu = 0, i, len = 0, lines = 0, shown;

    if (unit == NULL || unit->pClassData == NULL || unit->pCharacterData == NULL)
        return desc;
    cd = unit->pClassData;
    if (!Gate(UNIT_FACTION(unit), cd, unit->pCharacterData))
        return desc; /* skills off for this unit: the class's own description */
    n = Reforged_SkillUnitList(unit, list, (int)sizeof(list)); /* its kept skill included */
    if (n == 0)
    {
        strcpy(sClassHelp, "Skills: none.");
        return 0x7F53;
    }
    for (i = 0; i < n; i++)
    {
        if (IsUtility(list[i]))
            util[nu++] = list[i];
        else
            fx[nf++] = list[i];
    }
    /* at most 3 lines: "Name: effect" per skill, the utility skills by name on one line; when
       that is more than 3, the last line names the rest ("Also: ...") */
    shown = nf + (nu ? 1 : 0) <= 3 ? nf : 2;
    sClassHelp[0] = 0;
    for (i = 0; i < shown; i++)
    {
        len += snprintf(sClassHelp + len, sizeof(sClassHelp) - len, "%s%s: %s", lines ? SK_MSG_NL : "",
            ShownName(fx[i], cd), ShownHelp(fx[i], cd));
        lines++;
    }
    if (shown < nf || nu)
    {
        int first = 1;

        len += snprintf(sClassHelp + len, sizeof(sClassHelp) - len, "%s%s", lines ? SK_MSG_NL : "",
            shown < nf ? "Also: " : "");
        for (i = shown; i < nf; i++, first = 0)
            len += snprintf(sClassHelp + len, sizeof(sClassHelp) - len, "%s%s", first ? "" : ", ", ShownName(fx[i], cd));
        for (i = 0; i < nu; i++, first = 0)
            len += snprintf(sClassHelp + len, sizeof(sClassHelp) - len, "%s%s", first ? "" : ", ", ShownName(util[i], cd));
    }
    return 0x7F53;
}

int Reforged_SkillVirtualMsg(int slot, char *buf)
{
    if (slot >= 0 && slot <= 2)
    {
        strcpy(buf, sPromoText[slot]);
        return 1;
    }
    if (slot == 3)
    {
        strcpy(buf, sClassHelp);
        return 1;
    }
    if (slot >= 4 && slot <= 15)
        return Reforged_SkillPageMsg(slot - 4, buf); /* the Skills page's line helps (reforged_skills_page.c) */
    if (slot >= SK_PROMO_FIX_SLOT && slot < SK_PROMO_FIX_SLOT + (int)(sizeof(sPromoDescFix) / sizeof(sPromoDescFix[0])))
    {
        strcpy(buf, sPromoDescFix[slot - SK_PROMO_FIX_SLOT].text);
        return 1;
    }
    if (slot >= 0 && slot <= 31)
    {
        buf[0] = 0; /* a reserved id: never past gMsgTable's end */
        return 1;
    }
    return 0;
}

#endif /* PORT_PLAYED_BUILD */
