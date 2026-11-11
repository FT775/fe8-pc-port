#include "global.h"

/*
 * Reforged Options, Helpers: "Faster supports" (RFO_FAST_SUPPORTS, v1.9.0; the owner's answer of
 * 2026-11-07: faster supports, gained from proximity and from Pair Up).
 *
 * In the original (src/bmreliance.c ProcessTurnSupportExp), from turn 2 of a chapter, at the start of
 * each player phase, every pair of your units who can support each other gains its support growth
 * (1 to 3 points; C needs 80, B 160, A 240) only when they stand next to each other (or one carries
 * the other). With the option at x2 or x3:
 *   - nearby counts too: within SUPPORT_NEAR squares (RFO_SUPPORT_FAR, the distance test's default
 *     case; a carried unit still only counts for its carrier);
 *   - each gain is made 2 or 3 times (RFO_SUPPORT_EXTRA, after the original's gain), capped as the
 *     original caps it (the level's maximum). The extra gains are not added to the chapter's support
 *     total (gPlaySt.chapterTotalSupportGain, a chapter statistic), as Pair Up's extra is not.
 * Pair Up's own extra gain (reforged_pairup.c Reforged_PairSupportExtra) is made the same number of
 * times, so a pair still gains twice as fast as two units side by side.
 * Support conversations still need the two side by side. No random numbers are used. Off: the original.
 */

#if PORT_PLAYED_BUILD

#include "bmunit.h"
#include "bmreliance.h"

#include "port_hw.h"
#include "port_reforged.h"

#define SUPPORT_NEAR 3

static int sLogged; /* the first gains are logged */

/* 1 (off), 2 or 3 */
int Reforged_SupportMultiplier(void)
{
    int v = Reforged_GetOption(RFO_FAST_SUPPORTS);

    return v >= 1 && v <= 2 ? v + 1 : 1;
}

int Reforged_SupportNear(const struct Unit *unit, const struct Unit *other)
{
    if (Reforged_SupportMultiplier() == 1)
        return 0;
    if ((unit->state & US_RESCUED) || (other->state & US_RESCUED))
        return 0;
    return RECT_DISTANCE(unit->xPos, unit->yPos, other->xPos, other->yPos) <= SUPPORT_NEAR;
}

void Reforged_SupportExtra(struct Unit *unit, int num, struct Unit *other)
{
    int n = Reforged_SupportMultiplier(), total;

    if (n == 1 || GetUnitTotalSupportLevel(other) >= MAX_SIMULTANEOUS_SUPPORT_COUNT)
        return;
    total = gPlaySt.chapterTotalSupportGain;
    while (--n > 0)
        UnitGainSupportExp(unit, num);
    gPlaySt.chapterTotalSupportGain = total;
    if (sLogged < 24)
        sLogged++, PortLog("[reforged] faster supports: pid 0x%02X with pid 0x%02X (%d squares): %d support points\n",
            unit->pCharacterData->number, other->pCharacterData->number,
            RECT_DISTANCE(unit->xPos, unit->yPos, other->xPos, other->yPos), unit->supports[num]);
}

#endif /* PORT_PLAYED_BUILD */
