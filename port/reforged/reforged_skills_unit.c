#include "global.h"

/*
 * Reforged Options, Game fixes: class skills, the unit side (the user's request of 2026-11-06/07;
 * design "Skills proposal (draft).md", "use your picks"). The shared rules (who has which skill,
 * the options RFO_FIX_SKILLS / RFO_FIX_ENEMY_SKILLS) are in reforged_skills.c; this file only
 * turns a skill the unit has into numbers at the game's own hooks (include/bmunit.h RFO_SKILL_*):
 *
 *   - Str/Mag/Skl/Spd/Lck/Def/Res +2 and HP +5: added in the stat getters (src/bmunit.c), so the
 *     stat screen, battles, the forecast, the AI and every HP clamp see the same totals.
 *   - Celerity (Mov +1): in UNIT_MOV and the path arrow's open-coded movement
 *     (src/bmpatharrowdisp.c), so the blue range, the arrow and the AI's reach agree.
 *   - Renewal: +20 (% of max HP) to the start-of-turn terrain heal (src/bmtarget.c), on any
 *     terrain, stacking with forts; the game's own heal list and animation do the rest.
 *   - Live to Serve: a heal staff used on another unit heals the healer by what the targets
 *     actually gained (src/bmusemind.c ExecStandardHeal and ExecFortify).
 *
 * The getters are also called on the battle's unit copies (gBattleActor.unit, gBattleTarget.unit,
 * e.g. mapanim_api.c's GetUnitMaxHp(&bu->unit)), which InitBattleUnit fills with the totals: those
 * get nothing here, or the bonus would count twice.
 */

#if PORT_PLAYED_BUILD

#include "bmunit.h"
#include "variables.h"
#include "bmmap.h"
#include "mu.h"
#include "bmbattle.h"
#include "bmmind.h"
#include "uiselecttarget.h"

#include "port_reforged.h"
#include "reforged_skills.h"

#define SKILL_STAT_BONUS 2
#define SKILL_HP_BONUS   5
#define SKILL_MOV_BONUS  1
#define SKILL_RENEWAL    20 /* percent of max HP, as the terrain table's heal amounts */

/* A unit whose getters may take a skill bonus: skills on in this run, and not a battle copy. */
static int Eligible(const struct Unit *unit)
{
    return unit != NULL && Reforged_SkillsOn() && !Reforged_SkillIsBattleCopy(unit);
}

int Reforged_SkillStat(const struct Unit *unit, int stat)
{
    int has, myrrh = stat == 2 ? Reforged_MyrrhSpeed(unit) : 0; /* Game fix "Myrrh buff": Speed +2 */

    if (!Eligible(unit))
        return myrrh;

    switch (stat) {
    case 0: /* Pow is Str or Mag: one stat in FE8; holding both still gives +2 */
        has = Reforged_SkillHas(unit, SK_STR2) || Reforged_SkillHas(unit, SK_MAG2);
        break;
    case 1: has = Reforged_SkillHas(unit, SK_SKL2); break;
    case 2: has = Reforged_SkillHas(unit, SK_SPD2); break;
    case 3: has = Reforged_SkillHas(unit, SK_LCK2); break;
    case 4: has = Reforged_SkillHas(unit, SK_DEF2); break;
    case 5: has = Reforged_SkillHas(unit, SK_RES2); break;
    default: has = 0; break;
    }
    return myrrh + (has ? SKILL_STAT_BONUS : 0);
}

int Reforged_SkillHp(const struct Unit *unit)
{
    return Eligible(unit) && Reforged_SkillHas(unit, SK_HP5) ? SKILL_HP_BONUS : 0;
}

int Reforged_SkillMov(const struct Unit *unit)
{
    /* InitBattleUnit stores the full UNIT_MOV in the copy's movBonus: never add again there. */
    return Eligible(unit) && Reforged_SkillHas(unit, SK_CELERITY) ? SKILL_MOV_BONUS : 0;
}

int Reforged_SkillRenewal(const struct Unit *unit)
{
    return Eligible(unit) && Reforged_SkillHas(unit, SK_RENEWAL) ? SKILL_RENEWAL : 0;
}

/* ---- Live to Serve ---- */

/* The healer, when it has Live to Serve; NULL otherwise. */
static struct Unit *LtsHealer(void)
{
    struct Unit *healer = GetUnit(gActionData.subjectIndex);

    if (healer == NULL || healer->pCharacterData == NULL || !Reforged_SkillsOn())
        return NULL;
    return Reforged_SkillHas(healer, SK_LIVETOSERVE) ? healer : NULL;
}

/* Heal the healer by gain. The action's end writes gBattleActor back to the unit
   (UpdateActorFromBattle -> UpdateUnitFromBattle: unit->curHP = bu->unit.curHP), so the copy
   must hold the new HP too or the heal is undone. */
static void LtsGive(struct Unit *healer, int gain)
{
    int hp;

    if (gain <= 0)
        return;
    if (gBattleActor.unit.index == healer->index)
    {
        /* from the copy's HP, which the write-back uses: Fortify may have healed the live unit
           itself in its loop, which the game then undoes from this copy */
        hp = gBattleActor.unit.curHP + gain;
        if (hp > GetUnitMaxHp(healer))
            hp = GetUnitMaxHp(healer);
        gBattleActor.unit.curHP = hp;
        healer->curHP = hp;
        return;
    }
    AddUnitHp(healer, gain);
}

/* ExecStandardHeal (Heal, Mend, Recover, Physic), after gBattleTarget.unit.curHP took the
   target's new HP: the gain is what the target really got (capped at its max HP). */
void Reforged_SkillLtsHeal(void)
{
    struct Unit *healer;

    if (gActionData.subjectIndex == gActionData.targetIndex) /* never for healing oneself */
        return;
    healer = LtsHealer();
    if (healer == NULL)
        return;
    LtsGive(healer, gBattleTarget.unit.curHP - gBattleTarget.hpInitial);
}

/* ExecFortify: the target list's HP before the heal loop, then the total gained after it. */
static int sLtsFortifyBefore;

static int LtsFortifyTargetsHp(void)
{
    int i, n = GetSelectTargetCount(), sum = 0;

    for (i = 0; i < n; i++) {
        struct Unit *u = GetUnit(GetTarget(i)->uid);

        if (u != NULL && u->index != gActionData.subjectIndex) /* the healer's own share: no */
            sum += u->curHP;
    }
    return sum;
}

void Reforged_SkillLtsFortifyBegin(void)
{
    sLtsFortifyBefore = LtsFortifyTargetsHp();
}

void Reforged_SkillLtsFortifyEnd(void)
{
    struct Unit *healer = LtsHealer();

    if (healer != NULL)
        LtsGive(healer, LtsFortifyTargetsHp() - sLtsFortifyBefore);
    sLtsFortifyBefore = 0;
}

/* ---- Pass in fog (v1.12.0) ----
   src/bmidoten.c UnitApplyWorkingMovementScript (RFO_SKILL_PASS_BUMP): the unit has just stepped (in
   the walk-through, not on the map yet) onto a square where an enemy it cannot see stands
   (gBmMapHidden HIDDEN_BIT_UNIT). The original stops it one step before: *pit is the step's command,
   and the caller writes BUMP there and HALT after it; gActionData.xMove/yMove is the square before.
   1: bump (the original); 0: walk on. A unit with Pass walks on through, unless the hidden enemy
   is on its last square: then it bumps, on the last square before that holds no unit and no hidden
   unit (it may have passed through others), the script cut back to there. */
static void StepBack(int cmd, int *x, int *y)
{
    switch (cmd)
    {
    case MOVE_CMD_MOVE_UP: (*y)++; break;
    case MOVE_CMD_MOVE_DOWN: (*y)--; break;
    case MOVE_CMD_MOVE_LEFT: (*x)++; break;
    case MOVE_CMD_MOVE_RIGHT: (*x)--; break;
    }
}

int Reforged_SkillPassBump(struct Unit *unit, unsigned char **pit)
{
    unsigned char *it = *pit;
    int x = gActionData.xMove, y = gActionData.yMove;

    if (!Reforged_SkillPassUnit(unit))
        return 1;
    if (it[1] != MOVE_CMD_HALT)
        return 0; /* not the last square: through */
    /* back from the square before, past any square someone is on */
    while (it > gWorkingMovementScript &&
           ((gBmMapUnit[y][x] != 0 && gBmMapUnit[y][x] != unit->index) || (gBmMapHidden[y][x] & HIDDEN_BIT_UNIT)))
    {
        it--;
        StepBack(*it, &x, &y);
    }
    *pit = it;
    gActionData.xMove = x;
    gActionData.yMove = y;
    return 1;
}

#endif /* PORT_PLAYED_BUILD */
