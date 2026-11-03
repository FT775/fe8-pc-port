#include "global.h"

/*
 * Reforged Options, the Game fixes screen: opened by A on "Game fixes >" in the options
 * screen (reforged_menu.c); a list screen (reforged_listmenu.h, drawn by
 * reforged_randomizer_menu.c's code). Played builds only; empty in every test build.
 * Six changes to the original game's rules (RFO_FIX_*; reforged_gamefixes.c), each OFF by
 * default. Like the Randomizer screen's, they are the settings for the next New Game: a game
 * keeps the fixes it was started with (its saves record them), whatever this screen says later.
 */

#if PORT_PLAYED_BUILD

#include "port_reforged.h"
#include "reforged_listmenu.h"

/* The help lines are at most 192 pixels (24 tiles). */
static const struct RandoRow sFixRows[] = {
    RR_HEAD("-- For the next New Game --"),
    RR_SWITCH(RFO_FIX_EARLY_EXP, "Early kill EXP", "To Ch8: a stronger foe never gives less."),
    RR_SWITCH(RFO_FIX_PROMO_LEVELS, "Promoted enemies", "Pre-promotion levels use base growths."),
    RR_SWITCH(RFO_FIX_AI_HEAL, "AI healing", "AI healers skip units at full HP."),
    RR_SWITCH(RFO_FIX_CANTO, "Canto after Trade", "Keep Canto after backing out of a trade."),
    RR_SWITCH(RFO_FIX_DEVIL, "Devil at high Luck", "Luck 32 and up: no backfire, not always."),
    RR_SWITCH(RFO_FIX_CH15_DROP, "Ch15 staff drop", "The Troubadour drops Sleep/Berserk."),
};

static const struct ReforgedListScreen sFixScreen = {
    "Reforged Options: Game fixes", sFixRows, (int)(sizeof(sFixRows) / sizeof(sFixRows[0])), -1, 0, NULL,
};

void *StartReforgedGameFixesScreen(void *parent)
{
    return StartReforgedListScreen(parent, &sFixScreen);
}

#endif /* PORT_PLAYED_BUILD */
