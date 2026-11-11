#include "global.h"

/*
 * Reforged Options, Helpers: "Ruin floor select" (RFO_RUINS_SKIP, key skip_ruin_floors; v1.8.0 skipped
 * the cleared floors, v1.11.0 makes it a floor select at the owner's request of 2026-11-07: "start at
 * any floor you want that you have previously completed, so you CAN do the entire thing from the
 * bottom but gives you a 'level select' of sorts").
 *
 * The Lagdou Ruins are ten chapters (CHAPTER_R_01..R_10, 0x2E..0x37). Entering from the world map
 * always loads floor 1 (worldmap_main.c WorldMap_CallIntroEvent: the node's chapter is R_01); a
 * cleared floor goes on to the next (event MNC3) or leaves for the world map (MNCH); floor 10 ends
 * the run (the records screen, then MNCH). The game keeps no record of the floors reached.
 *
 * Here:
 *   - the best floor ever cleared is counted (RFO_RUINS_MOVE in src/eventscr.c Event2A_MoveToChapter:
 *     the move out of a cleared floor; floor 10 counts 10), kept per save block in the save mark's side
 *     file ("<block>_ruins = best=7", reforged_savemark.c), whether the option is on or not;
 *   - with the option on and a floor cleared, entering the Ruins (RFO_RUINS_PICK_CMD in the world map's
 *     label 14, before the intro event) shows a one-line menu "Start at floor < N >": Left/Right pick
 *     1 to the best floor cleared + 1 (at most 10; it starts there), A enters, B goes back to the map;
 *   - the pick is applied when the chapter is chosen (RFO_RUINS_ENTER, after the intro event):
 *     floor 1 is the original's run; floor N > 1 loads R_N with the dungeon record started as floor
 *     1's beginning would (InitDungeon(1)) and marks the run as started part-way;
 *   - a run started part-way is not a full run: when its floor 10 is cleared, the Ruins' best records
 *     (turns, units, time, EXP) and clear counts (Lyon needs three full runs) are left alone; only its
 *     kills are added to the total (RFO_RUINS_RECORDS, bmdifficulty.c UpdateDungeonRecordStats).
 *     The mark is kept per save block too ("partial"), and a run from floor 1 (InitRuinsDungeon,
 *     RFO_RUINS_FRESH) clears it.
 * The postgame recruits of floors 5 and 7 (Glen, Valter) need their floor played after the game is
 * beaten: picking that floor in the postgame does it. No random numbers are used. Off, or nothing
 * cleared yet: the original.
 *
 * Test switch: FE8PC_RUINS_FLOORS="7" sets the best floor cleared when the world map's Ruins entry
 * is reached; FE8PC_RUINS_PICK="5" answers the menu with floor 5 at once (scripted runs).
 */

#if PORT_PLAYED_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmdifficulty.h"
#include "eventscript.h"
#include "uimenu.h"
#include "uiutils.h"
#include "fontgrp.h"
#include "hardware.h"
#include "soundwrapper.h"
#include "proc.h"
#include "worldmap.h"
#include "constants/chapters.h"
#include "constants/songs.h"

#include "port_hw.h"
#include "port_reforged.h"

extern struct Font gFont_0201AFC0;

static int sBest;    /* the best floor ever cleared (0-10) */
static int sPartial; /* the run being played started past floor 1 */
static int sPick;    /* the floor picked in the menu (0: none pending) */
static int sMenuFloor, sMenuMax;
static ProcPtr sWorldMap;

static int Postgame(void)
{
    return (gPlaySt.chapterStateBits & PLAY_FLAG_COMPLETE) != 0;
}

/* src/events_script_utils.c InitRuinsDungeon (RFO_RUINS_FRESH): floor 1's beginning, a full run. */
void Reforged_RuinsFresh(void)
{
    sPartial = 0;
}

void Reforged_RuinsMove(int subcode)
{
    int ch = gPlaySt.chapterIndex, floor;

    if (ch < CHAPTER_R_01 || ch > CHAPTER_R_10 || (subcode != EVSUBCMD_MNCH && subcode != EVSUBCMD_MNC3))
        return;
    floor = ch - CHAPTER_R_01 + 1;
    if (floor > sBest)
        sBest = floor;
    PortLog("[reforged] ruins: floor %d cleared (%s): best floor cleared %d%s\n", floor,
        subcode == EVSUBCMD_MNC3 ? "going on" : "leaving", sBest, sPartial ? ", a run started part-way" : "");
    if (floor == 10)
        sPartial = 0; /* the run is over (its records were left alone: Reforged_RuinsPartialRecords) */
}

/* bmdifficulty.c UpdateDungeonRecordStats (RFO_RUINS_RECORDS), at the floor-10 clear: a run started
   part-way adds its kills to the total and nothing else. 1: done (the original's update skipped). */
int Reforged_RuinsPartialRecords(void)
{
    struct Dungeon *record;
    int val;

    if (gDungeonState.type != 1 || !sPartial)
        return 0;
    record = &gDungeonState.dungeon[1];
    val = record->enemiesDefeated + gDungeonState.current.enemiesDefeated;
    record->enemiesDefeated = val > 50000 ? 50000 : val;
    PortLog("[reforged] ruins: a run started part-way: the best records and clear counts are left alone\n");
    return 1;
}

/* ---- the menu ---- */

static int Floor_Draw(struct MenuProc *menu, struct MenuItemProc *item)
{
    char buf[40];

    snprintf(buf, sizeof(buf), "Start at floor %c %d %c", sMenuFloor > 1 ? '<' : ' ', sMenuFloor,
        sMenuFloor < sMenuMax ? '>' : ' ');
    ClearText(&item->text);
    Text_SetColor(&item->text, TEXT_COLOR_SYSTEM_WHITE);
    Text_DrawString(&item->text, buf);
    PutText(&item->text, BG_GetMapBuffer(menu->frontBg) + TILEMAP_INDEX(item->xTile, item->yTile));
    BG_EnableSyncByMask(BG0_SYNC_BIT);
    return 0;
}

static u8 Floor_Idle(struct MenuProc *menu, struct MenuItemProc *item)
{
    int keys = gKeyStatusPtr->repeatedKeys | gKeyStatusPtr->newKeys, was = sMenuFloor;

    if ((keys & DPAD_LEFT) && sMenuFloor > 1)
        sMenuFloor--;
    else if ((keys & DPAD_RIGHT) && sMenuFloor < sMenuMax)
        sMenuFloor++;
    if (sMenuFloor != was)
    {
        PlaySoundEffect(SONG_SE_SYS_CURSOR_LR1);
        Floor_Draw(menu, item);
    }
    return 0;
}

static u8 Floor_Selected(struct MenuProc *menu, struct MenuItemProc *item)
{
    (void)menu;
    (void)item;
    sPick = sMenuFloor;
    PortLog("[reforged] ruins: floor select: floor %d picked (1 to %d)\n", sPick, sMenuMax);
    return MENU_ACT_SKIPCURSOR | MENU_ACT_END | MENU_ACT_SND6A | MENU_ACT_CLEAR;
}

static u8 Floor_Available(const struct MenuItemDef *def, int number)
{
    (void)def;
    (void)number;
    return MENU_ENABLED;
}

static void Floor_Init(struct MenuProc *menu)
{
    (void)menu;
    BG_EnableSyncByMask(BG0_SYNC_BIT);
}

static void Floor_End(struct MenuProc *menu)
{
    (void)menu;
    ClearBg0Bg1();
}

static u8 Floor_Cancel(struct MenuProc *menu, struct MenuItemProc *item)
{
    (void)menu;
    (void)item;
    sPick = 0;
    Proc_Goto(sWorldMap, 3); /* back to the map, as the node menu's B (worldmap_path.c WMNodeMenu_OnCancel) */
    return MENU_ACT_SKIPCURSOR | MENU_ACT_END | MENU_ACT_SND6B | MENU_ACT_CLEAR;
}

static const struct MenuItemDef sFloorItems[] = {
    { "Start at floor", 0, 0, 0, 0x7A, Floor_Available, Floor_Draw, Floor_Selected, Floor_Idle, 0, 0 },
    { 0 },
};

static const struct MenuDef sFloorMenu = {
    { 7, 5, 16, 0 }, 0, sFloorItems, Floor_Init, Floor_End, 0, Floor_Cancel, 0, 0,
};

/* The world map's label 14 (RFO_RUINS_PICK_CMD, PROC_CALL_2): on the Lagdou Ruins with the option on
   and a floor cleared, the floor menu (blocking the world map; 0: stop this frame); else 1 (go on). */
s8 Reforged_RuinsPick(ProcPtr proc)
{
    const char *test = getenv("FE8PC_RUINS_FLOORS"), *pick = getenv("FE8PC_RUINS_PICK");
    int node = gGMData.units[0].location;

    sPick = 0;
    if (gWMNodeData[node].placementFlag != GMAP_NODE_PLACEMENT_DUNGEON)
        return 1;
    if (WMLoc_GetChapterId(node) != CHAPTER_R_01)
        return 1; /* the Tower of Valni */
    if (test != NULL)
        sBest = atoi(test) < 0 ? 0 : atoi(test) > 10 ? 10 : atoi(test);
    if (!Reforged_GetOption(RFO_RUINS_SKIP))
        return 1;
    if (gDungeonState.dungeon[1].clearCount > 0 && sBest < 10)
        sBest = 10; /* the game's own record: the Ruins were cleared (a save from before this count, or from the GBA) */
    if (sBest <= 0)
        return 1;
    sMenuMax = sBest + 1 > 10 ? 10 : sBest + 1;
    sMenuFloor = sMenuMax;
    if (pick != NULL)
    {
        sPick = atoi(pick) < 1 ? 1 : atoi(pick) > sMenuMax ? sMenuMax : atoi(pick);
        PortLog("[reforged] ruins: floor select (test): floor %d (1 to %d)\n", sPick, sMenuMax);
        return 1;
    }
    sWorldMap = proc;
    gGMData.sprite_disp = 0;
    InitTextFont(&gFont_0201AFC0, (void *)0x06001000, 0x80, 0);
    StartMenuAt(&sFloorMenu, sFloorMenu.rect, proc);
    return 0;
}

void Reforged_RuinsEnter(void)
{
    int pick = sPick;

    sPick = 0;
    if (gPlaySt.chapterIndex != CHAPTER_R_01 || pick <= 1)
        return; /* floor 1 (or no pick): the original's run; its beginning starts a full one */
    gPlaySt.chapterIndex = (u8)(CHAPTER_R_01 + pick - 1);
    InitDungeon(1); /* as floor 1's beginning event (InitRuinsDungeon) would: the Ruins' record */
    sPartial = 1;
    PortLog("[reforged] ruins: floor select: entering at floor %d (a run started part-way%s)\n", pick,
        Postgame() ? ", postgame" : "");
}

/* For the save mark: the best floor (0-10) plus 0x10 for a run started part-way. */
int Reforged_RuinsGet(void)
{
    return sBest | (sPartial ? 0x10 : 0);
}

void Reforged_RuinsSet(int value)
{
    sBest = value & 0x0F;
    if (sBest > 10)
        sBest = 0;
    sPartial = (value & 0x10) != 0;
}

#endif /* PORT_PLAYED_BUILD */
