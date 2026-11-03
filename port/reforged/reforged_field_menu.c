#include "global.h"

/*
 * Reforged Options during a chapter: the map menu's "Reforged" entry, after Options (include/
 * playerphase.h RFO_MAP_MENU_DEF: a host copy of the ROM's map menu with the entry). Played builds
 * only; empty in every test build.
 *
 * The rule: what may change in the middle of a run can be changed here (the options screen's
 * own rows, the cheats and the Display options, each acting at once); what a run is started
 * with (the randomizer and its seed, Nuzlocke, the game fixes: reforged_rando.c, reforged_gamefixes.c)
 * is shown under "This run" and cannot be changed, here or anywhere, until the next New Game.
 *
 * A list screen (reforged_listmenu.h, drawn by reforged_randomizer_menu.c's code) run the way the
 * game's own Options run from the map menu (src/uiconfig.c ProcScr_Config_Field): the game locked,
 * a fade to black, the map's display suspended and the options screen's backgrounds set up; on
 * the way out (the list's ended callback, behind its fade) the mural ended and the map's display
 * resumed and redrawn, then the game unlocked. B leaves (and writes the settings file if a value
 * changed).
 */

#if PORT_PLAYED_BUILD

#include <stdio.h>
#include <string.h>

#include "proc.h"
#include "hardware.h"
#include "fontgrp.h"
#include "uiutils.h"
#include "uimenu.h"
#include "bm.h"
#include "bmio.h"
#include "bmlib.h"
#include "bmmenu.h"
#include "playerphase.h"
#include "sysutil.h"
#include "functions.h"

#include "port_reforged.h"
#include "reforged_listmenu.h"

/* --- This run (read-only rows: action rows with no action, their value shown) --- */

static const char *OnOff(int on)
{
    return on ? "On" : "Off";
}

static const char *RunRandomizer(int arg)
{
    (void)arg;
    return OnOff(Reforged_RandoActive());
}

static const char *RunSeed(int arg)
{
    static char buf[12];

    (void)arg;
    if (!Reforged_RandoActive())
        return "-";
    snprintf(buf, sizeof(buf), "%08X", Reforged_RandoActiveSeed());
    return buf;
}

static const char *RunNuzlocke(int arg)
{
    (void)arg;
    return OnOff(Reforged_RandoNuzlocke());
}

static const char *RunFix(int id)
{
    return OnOff(Reforged_RunFix(id));
}

#define RUN_HELP "Set at New Game: fixed for this run."
#define RR_RUN(fn, arg, label) RR_ACT(NULL, fn, arg, label, RUN_HELP)

/* The help lines are at most 192 pixels (24 tiles). */
static const struct RandoRow sFieldRows[] = {
    RR_HEAD("-- Options --"),
    RR_SWITCH(RFO_DANGER_ZONE_SELECT, "Danger zone", "Select shows the enemy range."),
    RR_SWITCH(RFO_FAST_FORWARD, "Fast-forward", "Hold Space or right stick: go faster."),
    { RR_CHOICE, RFO_FF_SPEED, 4, 0, 0, 0, "Fast-forward speed", "How fast, while the key is held.",
      { "2x", "3x", "4x", "8x" }, { 2, 3, 4, 8 }, NULL, NULL, 0 },
    { RR_CHOICE, RFO_FF_SOUND, 2, 0, 0, 0, "Fast-forward sound", "Muted, or faster (8x: muted).",
      { "Mute", "Sped up" }, { 0, 1 }, NULL, NULL, 0 },
    { RR_CHOICE, RFO_MAP_HPBARS, 4, 0, 0, 0, "HP bars & warnings", "Bars: hurt units. Warnings: ! on threats.",
      { "Off", "Bars", "Warnings", "Both" }, { 0, 1, 2, 3 }, NULL, NULL, 0 },
    RR_SWITCH(RFO_GROWTH_SELECT, "Growth rates", "Select on a unit's stats: growths."),
    { RR_CHOICE, RFO_UNBREAKABLE, 3, 0, 0, 0, "Unbreakable", "Off, your units only, or everyone.",
      { "Off", "Yours", "All" }, { 0, 1, 2 }, NULL, NULL, 0 },
    RR_SWITCH(RFO_CASUAL_MODE, "Casual mode", "Fallen units return (not with Nuzlocke)."),
    RR_SWITCH(RFO_BUG_FIXES, "Freeze fixes", "Fix the original game's freezes."),
    RR_HEAD("-- Cheats --"),
    RR_SWITCH(RFO_CHEAT_GOLD, "Infinite gold", "Gold stays at 999,999 (refilled)."),
    { RR_CHOICE, RFO_CHEAT_GROWTHS, 4, 0, 0, 0, "Growths", "Fixed: average gains. Or more growth.",
      { "Off", "Fixed", "+25%", "+50%" }, { 0, 1, 2, 3 }, NULL, NULL, 0 },
    { RR_CHOICE, RFO_CHEAT_EXP, 3, 0, 0, 0, "EXP gained", "More EXP, still one level a battle.",
      { "x1", "x2", "x3" }, { 0, 1, 2 }, NULL, NULL, 0 },
    RR_HEAD("-- Display --"),
    RR_NUM(RFO_WINDOW_SCALE, RN_SCALE, 0, 1, "Window size", "Auto: 3x, or less if the screen is small."),
    { RR_CHOICE, RFO_SCALE_MODE, 3, 0, 0, 0, "Scaling", "Pixel: sharp, black bars. Fill/Stretch.",
      { "Pixel", "Fill", "Stretch" }, { 0, 1, 2 }, NULL, NULL, 0 },
    RR_SWITCH(RFO_SCALE_SMOOTH, "Smoothing", "Soften the pixels when filling or stretching."),
    RR_SWITCH(RFO_FULLSCREEN, "Fullscreen", "Start fullscreen. F11 still switches."),
    RR_HEAD("-- This run --"),
    RR_RUN(RunRandomizer, 0, "Randomizer"),
    RR_RUN(RunSeed, 0, "Seed"),
    RR_RUN(RunNuzlocke, 0, "Nuzlocke"),
    RR_RUN(RunFix, RFO_FIX_EARLY_EXP, "Early kill EXP"),
    RR_RUN(RunFix, RFO_FIX_PROMO_LEVELS, "Promoted enemies"),
    RR_RUN(RunFix, RFO_FIX_AI_HEAL, "AI healing"),
    RR_RUN(RunFix, RFO_FIX_CANTO, "Canto after Trade"),
    RR_RUN(RunFix, RFO_FIX_DEVIL, "Devil at high Luck"),
    RR_RUN(RunFix, RFO_FIX_CH15_DROP, "Ch15 staff drop"),
};

/* Behind the list's fade out: the map back as the game's Options leave it. */
static void FieldEnded(void *parent)
{
    (void)parent;
    EndMuralBackground();
    BMapDispResume();
    RefreshBMapGraphics();
}

static const struct ReforgedListScreen sFieldScreen = {
    "Reforged Options", sFieldRows, (int)(sizeof(sFieldRows) / sizeof(sFieldRows[0])), -1, 0,
    Reforged_ApplyDisplay, FieldEnded,
};

/* The options screen's backgrounds (reforged_menu.c ReforgedMenu_Init): the list screen
   draws its frames and text over them and starts the mural as this proc's child. */
static void Field_Setup(ProcPtr proc)
{
    /* the game's Options screen's layout from the map (src/uiconfig.c Config_Init; the same as
       SetupBackgrounds' default): the mural goes on BG3 at 0x8000 */
    u16 bgConfig[12] = {
        0x0000, 0x6000, 0,
        0x0000, 0x6800, 0,
        0x0000, 0x7000, 0,
        0x8000, 0x7800, 0,
    };

    (void)proc;
    SetupBackgrounds(bgConfig);
    ResetTextFont();
    ResetText();
    LoadObjUIGfx();
    LoadUiFrameGraphics();
    InitSystemTextFont();
    SetDispEnable(1, 1, 1, 1, 1);
    gLCDControlBuffer.bg0cnt.priority = 0;
    gLCDControlBuffer.bg1cnt.priority = 1;
    gLCDControlBuffer.bg2cnt.priority = 2;
    gLCDControlBuffer.bg3cnt.priority = 3;
    SetWinEnable(0, 0, 0);
    BG_Fill(gBG0TilemapBuffer, 0);
    BG_Fill(gBG1TilemapBuffer, 0);
    BG_Fill(gBG2TilemapBuffer, 0);
    BG_Fill(gBG3TilemapBuffer, 0);
    BG_EnableSyncByMask(BG0_SYNC_BIT | BG1_SYNC_BIT | BG2_SYNC_BIT | BG3_SYNC_BIT);
}

static void Field_StartList(ProcPtr proc)
{
    StartReforgedListScreen(proc, &sFieldScreen);
}

static const struct ProcCmd sProcScr_ReforgedField[] = {
    PROC_NAME("PC_reforged_field"),

    PROC_CALL(LockGame),
    PROC_CALL_ARG(NewFadeOut, 16),
    PROC_WHILE(FadeOutExists),
    PROC_CALL(BMapDispSuspend),
    PROC_YIELD,

    PROC_CALL(Field_Setup),
    PROC_CALL(Field_StartList), /* blocking: its own fades; it hands the map back (FieldEnded) */
    PROC_YIELD,

    PROC_CALL(UnlockGame),
    PROC_END,
};

static u8 MapMenu_ReforgedCommand(struct MenuProc *menu, struct MenuItemProc *menuItem)
{
    (void)menu;
    (void)menuItem;
    Proc_Start(sProcScr_ReforgedField, PROC_TREE_3);
    return MENU_ACT_SKIPCURSOR | MENU_ACT_END | MENU_ACT_SND6A | MENU_ACT_CLEAR;
}

/* Not in the tutorial (its events steer the map menu), nor while an event hides Options
   (src/eventscr.c's map-menu overrides name Options, 0x71, never this entry). */
static u8 MapMenu_IsReforgedAvailable(const struct MenuItemDef *def, int number)
{
    static const struct MenuItemDef options = { NULL, 0, 0, 0, 0x71, NULL, 0, 0, 0, 0, 0 };

    (void)def;
    if (gPlaySt.chapterStateBits & PLAY_FLAG_TUTORIAL)
        return MENU_NOTSHOWN;
    if (OverriddenMenuAvailability(&options, number) == MENU_NOTSHOWN)
        return MENU_NOTSHOWN;
    return MENU_ENABLED;
}

/* The ROM's map menu with "Reforged" after Options. The name is drawn as given (no message id);
   help (R) is Options' own line. overrideId 0x79: none of the map menu's (0x6E-0x78), so no
   tutorial override ever matches it. */
#define MAP_MENU_MAX 16
static struct MenuItemDef sMapMenuItems[MAP_MENU_MAX];
static struct MenuDef sMapMenuDef;

const struct MenuDef *Reforged_MapMenuDef(const struct MenuDef *def)
{
    static const struct MenuItemDef reforged = {
        "Reforged", 0, 0x6E1, 0, 0x79, MapMenu_IsReforgedAvailable, 0, MapMenu_ReforgedCommand, 0, 0, 0
    };
    static const struct MenuDef *built;
    int i, n = 0, added = 0, shown = 0;

    if (built == def)
        goto place;
    for (i = 0; def->menuItems[i].isAvailable != NULL && n < MAP_MENU_MAX - 2; i++)
    {
        sMapMenuItems[n++] = def->menuItems[i];
        if (def->menuItems[i].onSelected == MapMenu_OptionsCommand)
        {
            sMapMenuItems[n++] = reforged;
            added = 1;
        }
    }
    if (!added)
        sMapMenuItems[n++] = reforged;
    memset(&sMapMenuItems[n], 0, sizeof(sMapMenuItems[n])); /* MenuItemsEnd */
    sMapMenuDef = *def;
    sMapMenuDef.menuItems = sMapMenuItems;
    built = def;
place:
    /* The ROM's menu starts at tile row 2 with 2 rows an item: 8 items fill the screen to row 19
       (a dungeon map shows all 8 of its own). Counted as StartMenuCore counts them (both tests have
       no side effects); with this entry's 9th the menu starts at row 0, so it still fits. */
    for (i = 0; sMapMenuItems[i].isAvailable != NULL; i++)
    {
        int availability = OverriddenMenuAvailability(&sMapMenuItems[i], i);

        if (!availability)
            availability = sMapMenuItems[i].isAvailable(&sMapMenuItems[i], i);
        shown += availability != MENU_NOTSHOWN;
    }
    sMapMenuDef.rect.y = shown > 8 ? 0 : def->rect.y;
    return &sMapMenuDef;
}

#endif /* PORT_PLAYED_BUILD */
