#include "global.h"

/*
 * Reforged Options: "Helpers >" (the user's requests of 2026-11-06), a list screen
 * (reforged_listmenu.h) after Display on the options screen; the in-chapter Reforged
 * screen lists the same rows. Display-only ones first:
 *   - "Enemy phase" (RFO_ENEMY_SPEED, reforged_enemyphase.c): enemy turns at 2x / 4x,
 *     Start skips the rest of one.
 *   - "True hit" (RFO_TRUE_HIT, reforged_items.c): the forecast's real chance to hit.
 *   - "Skill banners" (RFO_SKILL_BANNERS, reforged_skillbanner.c): a skill's name when it activates.
 *   - "Myrrh look" (RFO_MYRRH_LOOK, reforged_fe7.c, v1.20.0): Myrrh's dragon form drawn as FE6's Divine
 *     Dragon; grey, with its own help, without the player's FE6 ROM (reforged_randomizer_menu.c).
 *   - "Keep game Options" (RFO_KEEP_GAME_OPTIONS, reforged_gameoptions.c).
 * and two that change play (each marks a save, like Casual mode; they act at once):
 *   - "Convoy anywhere" (RFO_CONVOY_ANYWHERE): every unit's action menu offers Supply
 *     wherever the chapter has the convoy (bmmenu.c SupplyUsability; the original: the
 *     lord and the units next to the lord).
 *   - "Pass skirmishes" (RFO_SKIRMISH_PASS): on the world map the army walks through a
 *     node held by monsters instead of stopping there for a skirmish
 *     (worldmap_main.c WorldMap_MuWalkLoop). Ending the move on such a node, A on the
 *     lord's own node, or monsters walking onto it still start the fight.
 */

#if PORT_PLAYED_BUILD

#include "port_reforged.h"
#include "reforged_listmenu.h"

int Reforged_ConvoyAnywhere(void)
{
    return Reforged_GetOption(RFO_CONVOY_ANYWHERE) != 0;
}

int Reforged_SkirmishPass(void)
{
    return Reforged_GetOption(RFO_SKIRMISH_PASS) != 0;
}

/* The help lines are at most 192 pixels (24 tiles). */
static const struct RandoRow sHelperRows[] = {
    { RR_CHOICE, RFO_ENEMY_SPEED, 3, 0, 0, 0, "Enemy phase", "How fast enemy turns go.\nStart during them: skip to your turn.",
      { "Normal", "2x", "4x" }, { 1, 2, 4 }, NULL, NULL, 0 },
    RR_SWITCH(RFO_TRUE_HIT, "True hit", "Show the real chance to hit: the game\nrolls twice, so 70 is 82 and 30 is 18."),
    RR_SWITCH(RFO_ARENA_ODDS, "Arena odds", "Arena: see the foe and both sides' hit,\ndamage and crit before the wager."),
    RR_SWITCH(RFO_SKILL_BANNERS, "Skill banners", "Show a skill's name in battle when it\nactivates (Luna, Sol, Pavise...)."),
    RR_SWITCH(RFO_MYRRH_LOOK, "Myrrh look", "Myrrh's dragon form in battle: FE6's\nDivine Dragon instead of FE8's."),
    RR_SWITCH(RFO_CONVOY_ANYWHERE, "Convoy anywhere", "Every unit can use Supply, not only\nthe lord and the units next to the lord."),
    RR_SWITCH(RFO_SKIRMISH_PASS, "Pass skirmishes", "World map: walk past monsters. Stop on\ntheir square, or wait, to fight them."),
    RR_SWITCH(RFO_RUINS_SKIP, "Ruin floor select", "Lagdou Ruins: pick a floor to start on,\nup to one past the best you cleared."),
    { RR_CHOICE, RFO_FAST_SUPPORTS, 3, 0, 0, 0, "Faster supports", "Support points x2 or x3, and units\nwithin 3 squares count, not only next to.",
      { "Off", "x2", "x3" }, { 0, 1, 2 }, NULL, NULL, 0 },
    RR_SWITCH(RFO_REWIND, "Rewind", "Hold Backspace on the map: back to the\nstart of the turn (again: one more)."),
    RR_SWITCH(RFO_KEEP_GAME_OPTIONS, "Keep game Options", "New games start with the Options you\nlast set in a game (speeds, animation)."),
};

static const struct ReforgedListScreen sHelperScreen = {
    "Reforged Options: Helpers", sHelperRows, (int)(sizeof(sHelperRows) / sizeof(sHelperRows[0])),
    -1, 0, NULL, NULL,
};

void *StartReforgedHelpersScreen(void *parent)
{
    return StartReforgedListScreen(parent, &sHelperScreen);
}

#endif /* PORT_PLAYED_BUILD */
