#include "global.h"

/*
 * Reforged Options, the Game fixes screen: opened by A on "Game fixes >" in the options
 * screen (reforged_menu.c); a list screen (reforged_listmenu.h, drawn by
 * reforged_randomizer_menu.c's code). Played builds only; empty in every test build.
 * Changes to the original game's rules (RFO_FIX_*; reforged_gamefixes.c), each OFF by
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
    RR_SWITCH(RFO_FIX_PAIR_UP, "Pair Up", "Rescue pairs units: the partner boosts\nstats and blocks hits (Dual Guard)."),
    /* Skills (reforged_skills.c): modes, not switches (0 off, 1 class, 2 class + personal;
       enemies 0 off, 1 promoted enemies and bosses, 2 all) */
    { RR_CHOICE, RFO_FIX_SKILLS, 3, 0, 0, 0, "Skills", "Class: class skills. Personal: also\neach character's own skill.",
      { "Off", "Class", "Personal" }, { 0, 1, 2 } },
    { RR_CHOICE, RFO_FIX_ENEMY_SKILLS, 3, 0, 0, 0, "Enemy skills", "Enemy and NPC skills. Promoted: only\npromoted units, bosses. All: all of them.",
      { "Off", "Promoted", "All" }, { 0, 1, 2 } },
    RR_SWITCH(RFO_FIX_SMART_AI, "Smarter enemies", "Regular enemies skip hopeless attacks:\nno 0-damage hits, no likely deaths."),
    RR_SWITCH(RFO_FIX_MYRRH, "Myrrh buff", "Myrrh's Dragonstone never breaks,\nand her Speed is +2."),
    RR_SWITCH(RFO_FIX_JOIN_RANKS, "Late joiners' ranks", "Joining from Ch9: weapon ranks +1,\nfrom Ch13: +2 (at most A)."),
    RR_SWITCH(RFO_FIX_ARMED_DANCERS, "Armed dancers", "Dancers can fight: Tethys uses light\ntomes (rank E), a Bard fire tomes."),
    RR_SWITCH(RFO_FIX_PROMO3, "Third promotion", "Every promotion offers 3 classes\n(FE7/FE6 ones with their ROMs)."),
};

static const struct ReforgedListScreen sFixScreen = {
    "Reforged Options: Game fixes", sFixRows, (int)(sizeof(sFixRows) / sizeof(sFixRows[0])), -1, 0, NULL,
};

void *StartReforgedGameFixesScreen(void *parent)
{
    return StartReforgedListScreen(parent, &sFixScreen);
}

#endif /* PORT_PLAYED_BUILD */
