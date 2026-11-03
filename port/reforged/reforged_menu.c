#include "global.h"

/*
 * Reforged Options screen (port-plan/reforged-options-plan.md section 2): the
 * played builds' PC-only Extras entry (savemenu.c, savedraw.c, under
 * PC_PORT && PORT_PLAYED_BUILD). Empty in every test build.
 *
 * A blocking child of the save menu's proc, started by
 * SaveMenuStartExtraMiscScreen as the Sound Room is (soundroom.c
 * StartSoundRoomScreen); the save menu has faded out before, and fades its
 * Extras list back in after this proc ends (PL_SAVEMENU_EXEC_EXTRA_MISC_OPTION,
 * SaveMenuPostExtraMiscScreen).
 *
 * Modelled on the game's own Options screen (src/uiconfig.c: Config_Init,
 * Config_Loop_KeyHandler) and the Sound Room's set-up (SoundRoomUi_Init):
 * frames from DrawUiFrame2 on BG1, text on BG0 in the system font, the mural
 * background, the UI hand. The text is PC-only: plain C strings drawn by
 * PutDrawText, whose Text_DrawString takes the English font's ASCII path
 * (fontgrp.c Text_DrawStringASCII); no message id is added.
 *
 * The rows are the options (stored by port/reforged/reforged_settings.c; each
 * OFF by default but the fast-forward block's and the original-game bug
 * fixes'), then the submenus. Each option
 * acts through its own file in port/reforged (danger zone, items, fast-forward,
 * casual, growths, HP bars); at its default its hooks do nothing. "Cheats >"
 * opens the Cheats screen (reforged_cheats_menu.c), "Randomizer >" the
 * Randomizer screen (reforged_randomizer_menu.c: the next New Game's seeded
 * randomizer settings), "Game fixes >" the Game fixes screen
 * (reforged_gamefixes_menu.c: optional changes to the original's rules), "Controls >" the Controls screen
 * (reforged_controls_menu.c: keys and gamepad buttons), "Display >" the Display
 * screen and "Option sets >" the Option sets screen (reforged_display_menu.c:
 * window size and scaling; sets of options saved and loaded). 15 rows.
 * Keys: Up/Down move, Left/Right or A switch a row, B goes back (and writes
 * the settings file if a value changed). The frame shows 6 rows; with more,
 * the list scrolls one row at a time with the cursor (proc->top).
 */

#if PORT_PLAYED_BUILD

#include "proc.h"
#include "hardware.h"
#include "fontgrp.h"
#include "uiutils.h"
#include "bm.h"
#include "sysutil.h"
#include "soundwrapper.h"
#include "functions.h"
#include "constants/songs.h"

#include "port_reforged.h"

/* 1 shows the "Option sets >" row, last on the screen (the Option sets screen,
   reforged_display_menu.c); 0 hides it (as the first release did, port-rfo-polish,
   U6, before the sets were built). */
#define RFO_SHOW_OPTION_SETS 1

enum {
    /* rows 0 to RFO_MAP_HPBARS are this screen's options, then the submenus; the
       cheat ids after RFO_MAP_HPBARS are the Cheats screen's (merge, port-rfo-int3;
       the growth-rates and map HP bars rows, port-rfo-int5; the original-game
       bug fixes row, RFO_BUG_FIXES, before HP bars, port-retailfix) */
    RFO_ROW_SWITCHES = RFO_MAP_HPBARS + 1,
    RFO_ROW_COUNT = RFO_ROW_SWITCHES + 5 + RFO_SHOW_OPTION_SETS,
    RFO_ROWS_SHOWN = 6, /* the list frame holds 6 rows; more scroll (proc->top) */
    /* Text tiles (InitText, from VRAM 0x6001000): title 12x2, help 24x2, each
       option row's label 14x2 and values 10x2 (48 tiles), but "HP bars &
       warnings", whose four values take 13x2 (RFO_HPB_VALUE_W, port-rfo-int6);
       a submenu row's label 9x2 (RFO_SUBMENU_W: 18 tiles; "Option sets >" is 56
       pixels): 9 option rows and 6 submenu rows, 618 tiles, up to 0x6005D40,
       below the BG0 map at 0x6006000 (22 tiles left: one more submenu row
       fits, not an option row). */
    RFO_SUBMENU_W = 9,
    RFO_ROW_CHEATS = RFO_ROW_SWITCHES, /* "Cheats >" (the Cheats block) */
    RFO_ROW_RANDOMIZER = RFO_ROW_SWITCHES + 1, /* "Randomizer >" (the Randomizer block) */
    RFO_ROW_GAME_FIXES = RFO_ROW_SWITCHES + 2, /* "Game fixes >" (reforged_gamefixes_menu.c) */
    RFO_ROW_CONTROLS = RFO_ROW_SWITCHES + 3, /* "Controls >" (reforged_controls_menu.c) */
    RFO_ROW_DISPLAY = RFO_ROW_SWITCHES + 4, /* "Display >" (reforged_display_menu.c) */
    RFO_ROW_OPTION_SETS = RFO_ROW_SWITCHES + 5, /* "Option sets >" (reforged_display_menu.c) */

    RFO_LIST_TOP = 3,   /* tile row of the first entry */
    RFO_LABEL_X = 4,    /* tile column of the labels */
    RFO_VALUE_X = 18,   /* tile column of a row's first value */
    RFO_VALUE_W = 10,   /* tiles of a row's values */
    /* map HP bars block: "Off Bars Warnings Both" is 102 pixels in the system
       font, more than 10 tiles; that row's values start 3 columns left (its
       label, 83 pixels from column 4, ends in column 14) and take 13 tiles, to
       column 27, the frame's last inner column */
    RFO_HPB_VALUE_X = 15,
    RFO_HPB_VALUE_W = 13,
};

struct ReforgedMenuProc {
    PROC_HEADER;
    /* 29 */ u8 cursor;
    /* 2A */ u8 dirty;
    /* 2B */ u8 top; /* the first row shown */
};

static const char *const sRowLabels[RFO_ROW_COUNT] = {
    "Danger zone on Select",
    "Fast-forward",
    "Unbreakable weapons",
    "Casual mode",
    /* fast-forward block */
    "Fast-forward speed",
    "Fast-forward sound",
    /* growth-rates block */
    "Growth rates on Select",
    /* original-game bug fixes block */
    "Original-game bug fixes",
    /* map HP bars block */
    "HP bars & warnings",
    /* the submenus stay last */
    "Cheats >",
    "Randomizer >",
    "Game fixes >",
    "Controls >",
    "Display >",
#if RFO_SHOW_OPTION_SETS
    "Option sets >",
#endif
};

static const char *const sRowHelp[RFO_ROW_COUNT] = {
    "Select shows the enemy range.",
    "Hold Space or right stick: go faster.",
    "Off, your units only, or everyone.",
    "Fallen units come back.",
    /* fast-forward block */
    "How fast, while the key is held.",
    "Muted, or faster (8x: muted).",
    /* growth-rates block */
    "Select on a unit's stats: growths.",
    /* original-game bug fixes block */
    "Fix the original game's freezes.",
    /* map HP bars block */
    "Bars: hurt units. Warnings: ! on threats.",
    "Gold, experience, growths.",
    "Seeded shuffles for the next New Game.",
    "Rule fixes for the next New Game.",
    "Choose your keys and gamepad buttons.",
    "Window size, scaling, fullscreen.",
#if RFO_SHOW_OPTION_SETS
    "Save or load sets of options.",
#endif
};

static struct Text sTitleText;
static struct Text sHelpText;
static struct Text sRowText[RFO_ROW_COUNT];
static struct Text sValueText[RFO_ROW_SWITCHES];

/* Each row's values, left to right: a switch shows On, Off (as the game's
   Options screen does); "Unbreakable weapons" shows Off, Yours, All; "HP bars
   & warnings" Off, Bars, Warnings, Both. */
struct RowValue { const char *text; u8 value; u8 x; /* pixels from RFO_VALUE_X */ };

static const struct RowValue sSwitchValues[] = { { "On", 1, 8 }, { "Off", 0, 40 } };
static const struct RowValue sUnbreakValues[] = { { "Off", 0, 0 }, { "Yours", 1, 24 }, { "All", 2, 64 } };
/* fast-forward block */
static const struct RowValue sFfSpeedValues[] = { { "2x", 2, 0 }, { "3x", 3, 20 }, { "4x", 4, 40 }, { "8x", 8, 60 } };
static const struct RowValue sFfSoundValues[] = { { "Mute", 0, 0 }, { "Sped up", 1, 32 } };
/* map HP bars block (widths 13, 20, 38, 19 pixels; 104 in the row's 13 tiles) */
static const struct RowValue sHpBarsValues[] = {
    { "Off", RFO_HPBARS_OFF, 0 }, { "Bars", RFO_HPBARS_BARS, 17 },
    { "Warnings", RFO_HPBARS_WARNINGS, 42 }, { "Both", RFO_HPBARS_BOTH, 85 } };

static const struct RowValue *RowValues(int row, int *count)
{
    if (row == RFO_UNBREAKABLE)
    {
        *count = 3;
        return sUnbreakValues;
    }
    if (row == RFO_FF_SPEED) /* fast-forward block */
    {
        *count = 4;
        return sFfSpeedValues;
    }
    if (row == RFO_FF_SOUND)
    {
        *count = 2;
        return sFfSoundValues;
    }
    if (row == RFO_MAP_HPBARS) /* map HP bars block */
    {
        *count = 4;
        return sHpBarsValues;
    }
    *count = 2;
    return sSwitchValues;
}

/* A row's values: the tile column they start at and their width in tiles. */
static int ValueX(int row)
{
    return row == RFO_MAP_HPBARS ? RFO_HPB_VALUE_X : RFO_VALUE_X;
}

static int ValueW(int row)
{
    return row == RFO_MAP_HPBARS ? RFO_HPB_VALUE_W : RFO_VALUE_W;
}

/* The tile row of a list row, or -1 when it is scrolled out of the frame. */
static int RowY(int row, int top)
{
    if (row < top || row >= top + RFO_ROWS_SHOWN)
        return -1;
    return RFO_LIST_TOP + (row - top) * 2;
}

static void DrawValue(int row, int top)
{
    int cur = Reforged_GetOption(row), n, i;
    const struct RowValue *v = RowValues(row, &n);

    ClearText(&sValueText[row]);
    for (i = 0; i < n; i++)
        Text_InsertDrawString(&sValueText[row], v[i].x,
            v[i].value == cur ? TEXT_COLOR_SYSTEM_BLUE : TEXT_COLOR_SYSTEM_GRAY, v[i].text);
    if (RowY(row, top) >= 0)
        PutText(&sValueText[row], TILEMAP_LOCATED(gBG0TilemapBuffer, ValueX(row), RowY(row, top)));
    BG_EnableSyncByMask(BG0_SYNC_BIT);
}

/* The rows shown from `top`: the list's text tiles put again (more rows than
   the frame holds scroll, one row at a time, with the cursor). */
static void DrawList(int top)
{
    int row, x, y;

    for (y = RFO_LIST_TOP; y < RFO_LIST_TOP + RFO_ROWS_SHOWN * 2; y++)
        for (x = 0; x < 30; x++)
            gBG0TilemapBuffer[TILEMAP_INDEX(x, y)] = 0;
    for (row = top; row < RFO_ROW_COUNT && row < top + RFO_ROWS_SHOWN; row++)
    {
        PutText(&sRowText[row], TILEMAP_LOCATED(gBG0TilemapBuffer, RFO_LABEL_X, RowY(row, top)));
        if (row < RFO_ROW_SWITCHES)
            DrawValue(row, top);
    }
    BG_EnableSyncByMask(BG0_SYNC_BIT);
}

static void DrawHelp(int row)
{
    ClearText(&sHelpText);
    Text_InsertDrawString(&sHelpText, 0, TEXT_COLOR_SYSTEM_WHITE, sRowHelp[row]);
    PutText(&sHelpText, TILEMAP_LOCATED(gBG0TilemapBuffer, 3, 17));
    BG_EnableSyncByMask(BG0_SYNC_BIT);
}

static void ReforgedMenu_Init(struct ReforgedMenuProc *proc)
{
    int i;

    SetupBackgrounds(NULL);

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

    proc->cursor = 0;
    proc->dirty = 0;
    proc->top = 0;

    DrawUiFrame2(1, 2, 28, 14, 0); /* list: tile rows 2-15 */
    DrawUiFrame2(1, 16, 28, 4, 0); /* help line: rows 16-19 */

    InitText(&sTitleText, 12);
    PutDrawText(&sTitleText, TILEMAP_LOCATED(gBG0TilemapBuffer, 2, 0), TEXT_COLOR_SYSTEM_WHITE, 0, 12,
        "Reforged Options");

    for (i = 0; i < RFO_ROW_COUNT; i++)
    {
        InitText(&sRowText[i], i < RFO_ROW_SWITCHES ? 14 : RFO_SUBMENU_W);
        ClearText(&sRowText[i]);
        Text_InsertDrawString(&sRowText[i], 0, TEXT_COLOR_SYSTEM_WHITE, sRowLabels[i]);
    }

    for (i = 0; i < RFO_ROW_SWITCHES; i++)
        InitText(&sValueText[i], ValueW(i));
    DrawList(proc->top);

    InitText(&sHelpText, 24);
    DrawHelp(0);

    StartMuralBackground(proc, NULL, 0xe);

    BG_EnableSyncByMask(BG0_SYNC_BIT | BG1_SYNC_BIT | BG2_SYNC_BIT | BG3_SYNC_BIT);
}

static void ReforgedMenu_Loop(struct ReforgedMenuProc *proc)
{
    int prev = proc->cursor;
    int row;

    if (gKeyStatusPtr->repeatedKeys & DPAD_UP)
    {
        if (proc->cursor != 0)
            proc->cursor--;
        else if (gKeyStatusPtr->newKeys & DPAD_UP)
            proc->cursor = RFO_ROW_COUNT - 1;
    }
    else if (gKeyStatusPtr->repeatedKeys & DPAD_DOWN)
    {
        if (proc->cursor < RFO_ROW_COUNT - 1)
            proc->cursor++;
        else if (gKeyStatusPtr->newKeys & DPAD_DOWN)
            proc->cursor = 0;
    }

    if (prev != proc->cursor)
    {
        PlaySoundEffect(SONG_SE_SYS_CURSOR_UD1);
        DrawHelp(proc->cursor);
        /* scroll the list so the cursor's row is shown */
        if (proc->cursor < proc->top || proc->cursor >= proc->top + RFO_ROWS_SHOWN)
        {
            proc->top = proc->cursor < proc->top ? proc->cursor : proc->cursor - (RFO_ROWS_SHOWN - 1);
            DrawList(proc->top);
        }
    }

    row = proc->cursor;

    if (gKeyStatusPtr->newKeys & B_BUTTON)
    {
        PlaySoundEffect(SONG_SE_SYS_WINDOW_CANSEL1);
        if (proc->dirty)
            Reforged_SaveSettings();
        Proc_Break(proc);
        return;
    }

    /* --- Cheats submenu: A opens it (reforged_cheats_menu.c), from the start --- */
    if (row == RFO_ROW_CHEATS && (gKeyStatusPtr->newKeys & A_BUTTON))
    {
        PlaySoundEffect(SONG_SE_SYS_WINDOW_SELECT1);
        StartReforgedCheatsScreen(proc);
        return;
    }
    /* --- end of the Cheats block --- */

    /* --- Randomizer submenu: A opens it (reforged_randomizer_menu.c), as Cheats --- */
    if (row == RFO_ROW_RANDOMIZER && (gKeyStatusPtr->newKeys & A_BUTTON))
    {
        PlaySoundEffect(SONG_SE_SYS_WINDOW_SELECT1);
        StartReforgedRandomizerScreen(proc);
        return;
    }
    /* --- end of the Randomizer block --- */

    /* Game fixes, Controls, Display and Option sets submenus: A opens them, as Cheats */
    if (row >= RFO_ROW_GAME_FIXES && (gKeyStatusPtr->newKeys & A_BUTTON))
    {
        PlaySoundEffect(SONG_SE_SYS_WINDOW_SELECT1);
        if (row == RFO_ROW_GAME_FIXES)
            StartReforgedGameFixesScreen(proc);
        else if (row == RFO_ROW_CONTROLS)
            StartReforgedControlsScreen(proc);
        else if (row == RFO_ROW_DISPLAY)
            StartReforgedDisplayScreen(proc);
#if RFO_SHOW_OPTION_SETS
        else if (row == RFO_ROW_OPTION_SETS)
            StartReforgedOptionSetsScreen(proc);
#endif
        return;
    }

    if (row < RFO_ROW_SWITCHES)
    {
        int want = -1, n, i, cur = Reforged_GetOption(row);
        const struct RowValue *v = RowValues(row, &n);

        for (i = 0; i < n - 1 && v[i].value != cur; i++)
            ;
        if ((gKeyStatusPtr->newKeys & DPAD_LEFT) && i > 0)
            want = v[i - 1].value;
        else if ((gKeyStatusPtr->newKeys & DPAD_RIGHT) && i < n - 1)
            want = v[i + 1].value;
        else if (gKeyStatusPtr->newKeys & A_BUTTON)
            want = v[(i + 1) % n].value;

        if (want >= 0 && want != Reforged_GetOption(row))
        {
            Reforged_SetOption(row, want);
            proc->dirty = 1;
            PlaySoundEffect(SONG_SE_SYS_CURSOR_LR1);
            DrawValue(row, proc->top);
        }
    }

    /* left of the label, as the Options screen puts its hand (uiconfig.c) */
    DisplayUiHand(RFO_LABEL_X * 8 - 8, RowY(row, proc->top) * 8);
}

static void ReforgedMenu_End(struct ReforgedMenuProc *proc)
{
    EndMuralBackground();
    EndAllProcChildren(proc);
}

static const struct ProcCmd sProcScr_ReforgedMenu[] = {
    PROC_NAME("PC_reforged"),
    PROC_YIELD,

    PROC_CALL(ReforgedMenu_Init),

    PROC_CALL_ARG(NewFadeIn, 8),
    PROC_WHILE(FadeInExists),

    PROC_REPEAT(ReforgedMenu_Loop),

    PROC_CALL_ARG(NewFadeOut, 8),
    PROC_WHILE(FadeOutExists),

    PROC_CALL(ReforgedMenu_End),
    PROC_YIELD,

    PROC_END,
};

void *StartReforgedOptionsScreen(void *parent)
{
    return Proc_StartBlocking(sProcScr_ReforgedMenu, parent);
}

/* --- Cheats submenu (port-rfo-cheats) --- */
/* Called by the Cheats and Randomizer screens (blocking children of this proc)
   behind their fade out: draws this screen again as it was (the child reused
   BG0/BG1 and the text tiles), with the cursor kept on the submenu's row. */
void Reforged_MenuRedraw(void *menuProc)
{
    struct ReforgedMenuProc *proc = menuProc;
    u8 cursor = proc->cursor, dirty = proc->dirty, top = proc->top;

    EndMuralBackground();
    ReforgedMenu_Init(proc);
    proc->cursor = cursor;
    proc->dirty = dirty;
    proc->top = top; /* merge (port-rfo-int3): the list scrolls (fast-forward's rows) */
    DrawList(top);
    DrawHelp(cursor);
}
/* --- end of the Cheats block --- */

/* --- The Extras entry's label (port-rfo-label; decision U7, 2026-10-30) ---
 *
 * The Extras list draws each entry's name as OBJ tiles from the save menu's
 * label sheet (Img_GameMainMenuObjs, ROM 0x08A2812C: LZ77, 6 KB, 32 tiles
 * wide, decompressed to OBJ chr 0x200 by SaveMenu_LoadExtraMenuGraphics),
 * through SpriteArray_SaveMenuExtraOptionLabels[bit] (src/savemenu_data.c) in OBJ palette 4.
 * The Reforged entry uses bit 3, whose sprite (gSprite_08A20898: four 32x16
 * OBJs, 128x16 pixels, chr 0x20E-0x21D and 0x22E-0x23D) points at a label
 * the GBA never shows: "Battle History". These lines put "Reforged Options"
 * in those 32 tiles while the list is on screen, composed from the same
 * sheet's own letters (read from the ROM data at run time, so no picture is
 * stored here), and put the GBA's tiles back when the list goes away.
 *
 * Every letter is a cut of one of the sheet's labels (strip = the 16-pixel
 * row of the sheet: 0 "Transfer Data" / "Battle History", 1 "Map" / "Support
 * Conversation", 2 "Sound Room" / "Link Arena"; x and y in pixels of that
 * strip). The sheet has no "g" and no capital "O": the "g" is the "o" of
 * "Room" with the tail of the "y" of "History", the "O" the left curve of the
 * "C" of "Conversation" with the bowl of the "D" of "Data". Where cuts meet,
 * a letter's body colours (1-4) win over the shadow (5), the shadow over the
 * outline (6), as the sheet's own letters overlap. The text (99 pixels) is
 * centred in the 128, as the GBA's labels are.
 */
#include <string.h>

struct LabelPiece {
    u8 strip, x0, x1, y0, y1, dx;
};

struct LabelGlyph {
    u8 adv, count;
    struct LabelPiece piece[2];
};

static const struct LabelGlyph sLabelGlyphs[] = {
    { 9, 1, { { 2, 59, 67, 0, 15, 0 } } },                           /* R  "Room" */
    { 6, 1, { { 0, 164, 170, 0, 15, 0 } } },                         /* e  "Battle" */
    { 4, 2, { { 0, 50, 53, 0, 15, 0 }, { 0, 54, 54, 0, 4, 4 } } },   /* f  "Transfer" */
    { 6, 1, { { 2, 68, 73, 0, 15, 0 } } },                           /* o  "Room" */
    { 6, 2, { { 0, 59, 59, 11, 13, 0 }, { 0, 60, 64, 0, 15, 1 } } }, /* r  "Transfer" */
    { 7, 2, { { 2, 68, 74, 0, 15, 0 }, { 0, 207, 212, 11, 15, 2 } } }, /* g  "Room" o + "History" y */
    { 6, 1, { { 0, 164, 170, 0, 15, 0 } } },                         /* e  "Battle" */
    { 8, 1, { { 2, 47, 55, 0, 15, 0 } } },                           /* d  "Sound" */
    { 4, 0, { { 0 } } },                                             /* space */
    { 10, 2, { { 1, 165, 168, 0, 15, 0 }, { 0, 71, 76, 0, 15, 4 } } }, /* O  "Conversation" C + "Data" D */
    { 6, 1, { { 1, 139, 144, 0, 15, 0 } } },                         /* p  "Support" */
    { 5, 1, { { 0, 82, 86, 0, 15, 0 } } },                           /* t  "Data" */
    { 4, 1, { { 1, 216, 219, 0, 15, 0 } } },                         /* i  "Conversation" */
    { 6, 1, { { 2, 68, 73, 0, 15, 0 } } },                           /* o  "Room" */
    { 7, 1, { { 1, 225, 231, 0, 15, 0 } } },                         /* n  "Conversation" */
    { 5, 1, { { 1, 201, 205, 0, 15, 0 } } },                         /* s  "Conversation" */
};

#define LABEL_W      128
#define LABEL_CHR    0x20E      /* gSprite_08A20898's first tile; the second row is +0x20 */
#define LABEL_ROW_SZ (16 * 32)  /* 16 tiles of 32 bytes */

static u32 sLabelSheet[0x1800 / 4]; /* Img_GameMainMenuObjs, decompressed */
static u8 sLabelTiles[2 * LABEL_ROW_SZ]; /* the composed label, as the two VRAM rows */
static u8 sLabelUndo[2 * LABEL_ROW_SZ];  /* what the GBA has there */
static u8 sLabelMade, sLabelShown;

static int LabelSheetPixel(int strip, int x, int y)
{
    const u8 *s = (const u8 *)sLabelSheet;
    int b = s[((strip * 2 + y / 8) * 32 + x / 8) * 32 + (y % 8) * 4 + (x % 8) / 2];

    return (x & 1) ? (b >> 4) : (b & 15);
}

static int LabelRank(int c)
{
    return c == 0 ? 0 : c == 6 ? 1 : c == 5 ? 2 : 3;
}

static void MakeLabel(void)
{
    static u8 canvas[16][160];
    int i, j, x, y, cx = 0, left = 160, right = -1, off;

    LZ77UnCompWram(Img_GameMainMenuObjs, sLabelSheet);
    memset(canvas, 0, sizeof(canvas));

    for (i = 0; i < (int)ARRAY_COUNT(sLabelGlyphs); i++) {
        for (j = 0; j < sLabelGlyphs[i].count; j++) {
            const struct LabelPiece *p = &sLabelGlyphs[i].piece[j];

            for (y = p->y0; y <= p->y1; y++)
                for (x = p->x0; x <= p->x1; x++) {
                    int c = LabelSheetPixel(p->strip, x, y);
                    int dx = cx + p->dx + x - p->x0;

                    if (c != 0 && LabelRank(c) > LabelRank(canvas[y][dx]))
                        canvas[y][dx] = c;
                }
        }
        cx += sLabelGlyphs[i].adv;
    }

    for (x = 0; x < 160; x++)
        for (y = 0; y < 16; y++)
            if (canvas[y][x] != 0) {
                if (x < left)
                    left = x;
                right = x;
            }
    off = (LABEL_W - (right - left + 1)) / 2;

    memset(sLabelTiles, 0, sizeof(sLabelTiles));
    for (y = 0; y < 16; y++)
        for (x = left; x <= right; x++) {
            int lx = x - left + off;
            u8 *b = &sLabelTiles[(y / 8) * LABEL_ROW_SZ + (lx / 8) * 32 + (y % 8) * 4 + (lx % 8) / 2];

            *b |= (lx & 1) ? (canvas[y][x] << 4) : canvas[y][x];
        }
    sLabelMade = 1;
}

/*
 * Called by SaveDraw_DrawExtraMenuOptions each frame (shown: the Extras list
 * is on screen this frame) and with 0 where the save menu ends its drawing
 * proc (SaveMenuStartExtraMiscScreen behind its fade out, PostSaveMenuHandler).
 * Showing keeps what the GBA has in the 32 tiles (bytes the game writes there
 * meanwhile, as when it decompresses the sheet again, are taken as the GBA's);
 * hiding gives it back, except bytes the game has written since. So whenever
 * the list is gone, VRAM holds what the GBA game has there.
 */
void Reforged_ExtrasLabel(int shown)
{
    u8 *vram[2] = {
        (u8 *)OBJ_VRAM0 + LABEL_CHR * 32,
        (u8 *)OBJ_VRAM0 + (LABEL_CHR + 0x20) * 32,
    };
    int r, i;

    if (shown) {
        if (!sLabelMade)
            MakeLabel();
        for (r = 0; r < 2; r++)
            for (i = 0; i < LABEL_ROW_SZ; i++) {
                u8 *v = &vram[r][i], want = sLabelTiles[r * LABEL_ROW_SZ + i];

                if (!sLabelShown || *v != want) {
                    sLabelUndo[r * LABEL_ROW_SZ + i] = *v;
                    *v = want;
                }
            }
        sLabelShown = 1;
    } else if (sLabelShown) {
        for (r = 0; r < 2; r++)
            for (i = 0; i < LABEL_ROW_SZ; i++)
                if (vram[r][i] == sLabelTiles[r * LABEL_ROW_SZ + i])
                    vram[r][i] = sLabelUndo[r * LABEL_ROW_SZ + i];
        sLabelShown = 0;
    }
}
/* --- end of the Extras label block --- */

#endif /* PORT_PLAYED_BUILD */
