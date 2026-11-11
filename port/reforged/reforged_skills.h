#ifndef REFORGED_SKILLS_H
#define REFORGED_SKILLS_H

/*
 * Reforged Options, Game fixes: class skills (the user's request of 2026-11-06/07; the design is
 * "Skills proposal (draft).md" in the Windows folder, decided 2026-11-07 "use your picks", with
 * enemy skills for promoted enemies and bosses: "promoted/boss enemies having skills is fine").
 *
 *   RFO_FIX_SKILLS        0 off [0], 1 class skills, 2 class + personal skills
 *   RFO_FIX_ENEMY_SKILLS  0 off [0], 1 promoted enemies and bosses, 2 every enemy/NPC
 *
 * Both are game fixes: fixed at New Game for the run (Reforged_RunFix holds the value 0-2).
 * Skills belong to classes (two per class at most: the "kept" one and the promotion one) and,
 * with 2, to characters (one personal skill; some only once promoted). Nothing is saved per unit, except each player character's kept skill (the save's side file).
 * FE8's own skills (Canto, Steal, ...) are listed (SK_* below SK_FIRST_NEW) for the screens only;
 * the game still decides them from class attributes as before.
 *
 * Files: reforged_skills.c (the tables, Reforged_SkillHas, names, the screens' texts),
 * reforged_skills_battle.c (the battle hooks: include/bmbattle.h RFO_SKILL_*),
 * reforged_skills_unit.c (stats, HP, movement, Renewal, Live to Serve: include/bmunit.h RFO_SKILL_*).
 */

struct Unit;
struct BattleUnit;

enum ReforgedSkill {
    SK_NONE = 0,
    /* FE8's own (shown on the screens; behaviour unchanged) */
    SK_CANTO, SK_STEAL, SK_LOCKTOUCH, SK_CRIT15, SK_SILENCER, SK_SURESTRIKE, SK_PIERCE,
    SK_GREATSHIELD, SK_SLAYER, SK_SUMMON, SK_DANCE,
    SK_FIRST_NEW,
    /* new */
    SK_CHARM = SK_FIRST_NEW, /* allies within 2 squares: +5 Hit, +5 Avoid */
    SK_SAVIOR,         /* no Skl/Spd halving while rescuing */
    SK_WARY,           /* Wary Fighter: no follow-up attacks for either side (brave still twice) */
    SK_ARMSTHRIFT,     /* Lck*2 %: a hit does not use the weapon */
    SK_VANTAGE,        /* at <= 50% HP, strike first when attacked */
    SK_PATIENCE,       /* +10 Hit, +10 Avoid on the other side's phase */
    SK_WRATH,          /* +20 Crit at <= 50% HP */
    SK_DESPOIL,        /* a kill: Lck % for 300 gold (player units) */
    SK_MIRACLE,        /* Lck %: survive a killing hit on 1 HP (above 1 HP) */
    SK_LIVETOSERVE,    /* healing an ally heals you the same */
    SK_STR2, SK_MAG2, SK_SKL2, SK_SPD2, SK_LCK2, SK_DEF2, SK_RES2, /* +2 (Str and Mag: the same stat) */
    SK_HP5,            /* max HP +5 */
    SK_HEX,            /* adjacent enemies: -15 Avoid */
    SK_TANTIVY,        /* no ally within 2 squares: +10 Hit, +10 Avoid */
    SK_UNDERDOG,       /* against a higher-level foe (promoted = +20 levels): +15 Hit, +15 Avoid */
    SK_DISCIPLINE,     /* weapon EXP x2 */
    SK_SOL,            /* Skl/2 %: heal the damage dealt */
    SK_LUNA,           /* Skl/2 %: the hit ignores half the foe's Def/Res */
    SK_PAVISE,         /* Skl %: halve damage from swords, lances, axes, physical monster attacks */
    SK_AEGIS,          /* Skl %: halve damage from bows and magic */
    SK_LIFETAKER,      /* a kill on your own attack: recover 50% max HP */
    SK_POISONSTRIKE,   /* after combat in which you hit: the foe loses 20% max HP (cannot kill) */
    SK_RENEWAL,        /* start of your turn: recover 20% of max HP (on top of a fort's heal) */
    SK_CELERITY,       /* Mov +1 */
    SK_ANATHEMA,       /* enemies within 3 squares: -10 Avoid, -10 crit evade */
    SK_SWORDBREAKER, SK_LANCEBREAKER, SK_AXEBREAKER, SK_BOWBREAKER, SK_TOMEBREAKER, /* +30 Hit/Avoid vs that type */
    /* v1.12.0 (the owner's go-ahead of 2026-11-07; appended: the save side file names kept skills,
       and also takes their numbers) */
    SK_ASTRA,          /* Skl/2 %: the attack's strikes become 5 at half damage (one Astra a battle) */
    SK_NIHIL,          /* the foe's battle skills do nothing (bosses, with Enemy skills on) */
    SK_PASS,           /* move through squares held by enemies (not stop on them) */
    SK_COUNT
};

#if PORT_PLAYED_BUILD
/* The unit has the skill in this run (options, class, personal, enemy rule). 0 for NULL or a unit
   with no class data, and always 0 with skills off. */
int Reforged_SkillHas(const struct Unit *unit, int skill);
/* The skills a unit of class jid (and character pid, 0: none) would have, FE8's own included:
   up to max ids into out; returns the count (a personal skill that waits for promotion counts when jid is promoted).
   For the screens and the promotion preview. */
int Reforged_SkillList(int jid, int pid, int faction, unsigned char *out, int max);
/* A skill's short name ("Luna") and one-line help ("Skl/2 %: ignores half Def/Res"). */
const char *Reforged_SkillName(int skill);
const char *Reforged_SkillHelp(int skill);
/* The unit struct is one of the battle's copies (gBattleActor.unit, gBattleTarget.unit): getters
   on those must not add skill bonuses twice (InitBattleUnit copies the totals). */
int Reforged_SkillIsBattleCopy(const struct Unit *unit);
/* Skills are on in this run for anybody (RFO_FIX_SKILLS or RFO_FIX_ENEMY_SKILLS). */
int Reforged_SkillsOn(void);
/* Pass (include/bmunit.h RFO_SKILL_PASS*): the unit with index uid / the unit has Pass. */
int Reforged_SkillPass(int uid);
int Reforged_SkillPassUnit(const struct Unit *unit);
int Reforged_SkillPassCheck(void); /* the game fixes self-check: Pass in the flood */

/* Skill banners (reforged_skillbanner.c, v1.14.0): which skill activated on which hit of the last
   real battle (never a simulation, never a scripted battle), one bit per RFO_PROC_* in
   gRfoSkillProcPc[hit][0] (the hit's striker's skills) or [1] (its defender's). The hook in
   reforged_skills_battle.c also sets BATTLE_HIT_ATTR_18 on the hit: the display trusts the table
   only for a hit carrying that bit (ClearBattleHits clears it, so a hit array written any other
   way, a staff, an event battle, never shows an old record). gRfoAstraSrcPc[round]: the battle
   animation's round -> its first hit (reforged_astra_anim.c; Astra's merged rounds). */
enum {
    RFO_PROC_LUNA, RFO_PROC_SOL, RFO_PROC_PAVISE, RFO_PROC_AEGIS, RFO_PROC_MIRACLE, RFO_PROC_ASTRA,
    RFO_PROC_VANTAGE, RFO_PROC_LIFETAKER, RFO_PROC_ARMSTHRIFT, RFO_PROC_COUNT
};
extern unsigned short gRfoSkillProcPc[][2];
extern unsigned char gRfoAstraSrcPc[];
void Reforged_SkillProcClear(void);
void Reforged_SkillBannerBattleStart(void); /* each battle animation parse (Reforged_AstraMergeHits) */
#endif

/* --- Added by the screens part (reforged_skills.c) --- */
#if PORT_PLAYED_BUILD
/* Virtual text ids 0x7F50 + slot (reforged_items.c Reforged_VirtualMsg routes them here): slots
   0-2 the promotion screen's choices, 3 the stat screen's class help. 1 when buf was filled. */
int Reforged_SkillVirtualMsg(int slot, char *buf);
/* The promotion screen (src/classchg-sel.c RFO_SKILL_PROMO_DESC): with skills on for the unit,
   composes choice slot's "Gains / Keeps / Loses" text for class jid and returns its virtual id;
   else desc (the class's own description). */
int Reforged_SkillPromoDesc(struct Unit *unit, int slot, int jid, int desc);
/* The stat screen's R help on the class name (src/statscreen.c RFO_SKILL_CLASS_HELP): with
   skills on for the unit, its skills and short effects (virtual id 0x7F53); else desc. */
int Reforged_SkillClassHelp(struct Unit *unit, int desc);
#endif
/* --- end of the screens part --- */

/* --- Kept skills and the Skills page (v1.6.0, the owner's answers of 2026-11-07: "Own page";
   "Keep the old skill") --- */
#if PORT_PLAYED_BUILD
/* A promoted player unit keeps the skill its previous class gave it (the "kept" slot), not the
   new class's fixed one; the new class adds its promotion skill. Recorded per character (pid)
   when the unit promotes (Reforged_SkillPromoting, called from the RANDO_HOOK_PROMOTING path in
   bmbattle.c ApplyUnitPromotion/ApplyUnitDefaultPromotion), kept with each save in its side file
   (reforged_savemark.c), cleared at New Game. 0: none recorded (the class pair is used). */
void Reforged_SkillPromoting(struct Unit *unit);
int Reforged_SkillKept(int pid);
void Reforged_SkillKeptSet(int pid, int skill);
void Reforged_SkillKeptReset(void);
/* The unit's own skills as Reforged_SkillHas decides them (kept skill and personal included),
   FE8's own first: up to max ids; returns the count. */
int Reforged_SkillUnitList(const struct Unit *unit, unsigned char *out, int max);
/* What promoting the unit into class jid would do to its skills: the ids it would gain, keep and
   lose (each up to 8); the counts are returned through ng/nk/nl. */
void Reforged_SkillPromoChange(const struct Unit *unit, int jid, unsigned char *gains, int *ng,
    unsigned char *keeps, int *nk, unsigned char *loses, int *nl);
/* The unit's promotion choices as the promotion screen offers them (gPromoJidLut, Match gender,
   trainees' third choice): up to 3 class ids; returns the count (0: none). */
int Reforged_SkillPromoChoices(const struct Unit *unit, int *out);
#endif
#if PORT_PLAYED_BUILD
/* The Skills page (reforged_skills_page.c): its line helps (virtual ids 0x7F54 + index, routed
   through Reforged_SkillVirtualMsg), and the names/helps as class jid shows them (Str or Mag +2). */
int Reforged_SkillPageMsg(int index, char *buf);
const char *Reforged_SkillShownName(int skill, int jid);
const char *Reforged_SkillShownHelp(int skill, int jid);
#endif
/* --- end of kept skills --- */

#endif /* REFORGED_SKILLS_H */
