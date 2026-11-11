#include "global.h"

/*
 * Reforged Options, the Controls screen: opened by A on "Controls >" in the
 * options screen (reforged_menu.c). Played builds only; empty in every test build.
 * The bindings are port/hw/controls.c's (port/include/port_controls.h); the
 * settings file keeps them (reforged_settings.c, control_* keys), written when the
 * screen is left with a change. The window backend (port/sdl3/platform_sdl3.c)
 * reads them at once, so a new key works as soon as it is set.
 *
 * One row per action (the GBA buttons, fast-forward, fullscreen), four columns:
 * two keyboard keys and two gamepad buttons; the last row puts every binding back
 * to its default. A blocking child of the options screen's proc, drawn as the
 * Randomizer screen is (6 recycled row slots, the list scrolls with the cursor).
 *
 * Keys: Up/Down pick a row, Left/Right a column; A on a cell waits for the key or
 * button to bind there (the next one pressed on the keyboard for a key column, on
 * a gamepad for a pad column; Escape cancels, Delete clears the cell; after 10
 * seconds it gives up); Select clears the cell; A on "Reset to defaults" resets
 * them all; B goes back. While it waits, the game's own buttons do nothing here.
 *
 * Protection: a key or button is bound once (binding it again moves it, and the
 * cell it came from gets this cell's old binding: a swap). A, B, Start and the
 * four directions always keep at least one key and one gamepad button: a clear
 * or a swap that would take the last one away is refused (the help line says why).
 */

#if PORT_PLAYED_BUILD

#include <stdio.h>
#include <string.h>
#include <strings.h>

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
#include "port_controls.h"
#include "../platform.h"

enum {
    CTM_ROWS_SHOWN = 5,  /* below the column titles */
    CTM_HEAD_Y = 3,      /* tile row of the column titles */
    CTM_LIST_TOP = 5,    /* tile row of the first entry shown */
    CTM_LABEL_X = 2,
    CTM_LABEL_W = 11,    /* "Reset to defaults"; an action's label ("Fullscreen", 47 pixels)
                            ends before the first column, whose cells are put over the rest */
    CTM_COL_X0 = 10,     /* the four columns: 10 and 15 (keys, 5 tiles), 20 and 24 (pads, 4);
                            the hand (DisplayUiHand draws 14 pixels left of x) stands
                            after the longest label */
    CTM_RESET_X = 4,     /* "Reset to defaults": in from the frame, room for the hand */
    CTM_ROW_COUNT = CTL_COUNT + 1, /* the actions, then "Reset to defaults" */
    CTM_ROW_RESET = CTL_COUNT,
    CTM_CAPTURE_FRAMES = 600,
    /* Text tiles (InitText, from VRAM 0x6001000): title 18x2, help 24x2, column
       titles 18x2 in all, 5 row slots of an 11x2 label and 18x2 of cells: 416 tiles,
       up to 0x6004400, below the BG0 map at 0x6006000. */
};

struct ReforgedControlsProc {
    PROC_HEADER;
    /* 29 */ u8 cursor;   /* the row */
    /* 2A */ u8 column;   /* 0-3: key, key 2, pad, pad 2 */
    /* 2B */ u8 top;
    /* 2C */ u8 dirty;
    /* 2D */ u8 capturing;
    /* 2E */ u8 waitRelease; /* after a capture: until every key is up */
    /* 30 */ u16 timer;
};

static const char *const sColumnTitles[CTL_SLOTS] = { "Key", "Key 2", "Pad", "Pad 2" };
static const char *const sRowLabels[CTL_COUNT] = {
    "A", "B", "Select", "Start", "Right", "Left", "Up", "Down", "R", "L", "Fast fwd", "Fullscreen", "Rewind",
};

static struct Text sTitleText, sHelpText;
static struct Text sHeadText[CTL_SLOTS];
static struct Text sLabelText[CTM_ROWS_SHOWN];
static struct Text sCellText[CTM_ROWS_SHOWN][CTL_SLOTS];

static int RowY(int row, int top)
{
    if (row < top || row >= top + CTM_ROWS_SHOWN)
        return -1;
    return CTM_LIST_TOP + (row - top) * 2;
}

static int ColW(int column)
{
    return CTL_SLOT_IS_PAD(column) ? 4 : 5;
}

static int ColX(int column)
{
    int x = CTM_COL_X0, c;

    for (c = 0; c < column; c++)
        x += ColW(c);
    return x;
}

/* (the shared rules, port/hw/controls.c) */
#define Essential(action) Controls_IsEssential(action)

/* A short name for a cell: the pad names in capitals, long key names cut down. */
static void ShortName(int slot, const char *name, char *out, int size)
{
    static const char *const keyShort[][2] = {
        { "Backspace", "BkSp" }, { "Return", "Enter" }, { "Keypad Enter", "KP Ent" }, { "Escape", "Esc" },
        { "Delete", "Del" }, { "Insert", "Ins" }, { "PageUp", "PgUp" }, { "PageDown", "PgDn" },
        { "Left Shift", "LShift" }, { "Right Shift", "RShift" }, { "Left Ctrl", "LCtrl" }, { "Right Ctrl", "RCtrl" },
        { "Left Alt", "LAlt" }, { "Right Alt", "RAlt" }, { "CapsLock", "Caps" }, { "Space", "Space" },
    };
    static const char *const padShort[][2] = {
        { "a", "A" }, { "b", "B" }, { "west", "West" }, { "north", "North" }, { "back", "Back" },
        { "start", "Start" }, { "guide", "Guide" }, { "l", "L" }, { "r", "R" }, { "zl", "ZL" }, { "zr", "ZR" },
        { "dpup", "D-Up" }, { "dpdown", "D-Dn" }, { "dpleft", "D-Lt" }, { "dpright", "D-Rt" },
        { "lstick", "LStk" }, { "rstick", "RStk" },
    };
    unsigned i;
    int len;

    if (name[0] == 0)
    {
        snprintf(out, size, "-");
        return;
    }
    snprintf(out, size, "%s", name);
    if (CTL_SLOT_IS_PAD(slot))
    {
        for (i = 0; i < sizeof(padShort) / sizeof(padShort[0]); i++)
            if (strcmp(name, padShort[i][0]) == 0)
                snprintf(out, size, "%s", padShort[i][1]);
    }
    else
    {
        for (i = 0; i < sizeof(keyShort) / sizeof(keyShort[0]); i++)
            if (strcmp(name, keyShort[i][0]) == 0)
                snprintf(out, size, "%s", keyShort[i][1]);
        if (strncmp(out, "Keypad ", 7) == 0)
            snprintf(out, size, "KP %s", name + 7);
    }
    /* the font is proportional: cut until it fits the column (a space each side) */
    for (len = (int)strlen(out); len > 1 && GetStringTextLen(out) > ColW(slot) * 8 - 4; len--)
        out[len - 1] = 0;
}

static void DrawRow(struct ReforgedControlsProc *proc, int row)
{
    int y = RowY(row, proc->top), slot = row - proc->top, s;
    struct Text *label;

    if (y < 0)
        return;
    label = &sLabelText[slot];
    ClearText(label);
    if (row == CTM_ROW_RESET)
    {
        Text_InsertDrawString(label, 0, TEXT_COLOR_SYSTEM_GOLD, "Reset to defaults");
        PutText(label, TILEMAP_LOCATED(gBG0TilemapBuffer, CTM_RESET_X, y));
        BG_EnableSyncByMask(BG0_SYNC_BIT);
        return;
    }
    Text_InsertDrawString(label, 0, TEXT_COLOR_SYSTEM_WHITE, sRowLabels[row]);
    PutText(label, TILEMAP_LOCATED(gBG0TilemapBuffer, CTM_LABEL_X, y));
    for (s = 0; s < CTL_SLOTS; s++)
    {
        struct Text *t = &sCellText[slot][s];
        char buf[CTL_NAME_MAX];
        int color = TEXT_COLOR_SYSTEM_BLUE;

        ShortName(s, Controls_Get(row, s), buf, sizeof(buf));
        if (Controls_Get(row, s)[0] == 0)
            color = TEXT_COLOR_SYSTEM_GRAY;
        if (proc->capturing && row == proc->cursor && s == proc->column)
        {
            snprintf(buf, sizeof(buf), "...");
            color = TEXT_COLOR_SYSTEM_GOLD;
        }
        ClearText(t);
        Text_InsertDrawString(t, 2, color, buf);
        PutText(t, TILEMAP_LOCATED(gBG0TilemapBuffer, ColX(s), y));
    }
    BG_EnableSyncByMask(BG0_SYNC_BIT);
}

static void DrawList(struct ReforgedControlsProc *proc)
{
    int row, x, y;

    for (y = CTM_LIST_TOP; y < CTM_LIST_TOP + CTM_ROWS_SHOWN * 2; y++)
        for (x = 0; x < 30; x++)
            if (x >= 2 && x <= 27)
                gBG0TilemapBuffer[TILEMAP_INDEX(x, y)] = 0;
    for (row = proc->top; row < CTM_ROW_COUNT && row < proc->top + CTM_ROWS_SHOWN; row++)
        DrawRow(proc, row);
    BG_EnableSyncByMask(BG0_SYNC_BIT);
}

static void DrawHelp(const char *str)
{
    ClearText(&sHelpText);
    Text_InsertDrawString(&sHelpText, 0, TEXT_COLOR_SYSTEM_WHITE, str);
    PutText(&sHelpText, TILEMAP_LOCATED(gBG0TilemapBuffer, 3, 17));
    BG_EnableSyncByMask(BG0_SYNC_BIT);
}

static void DrawRowHelp(struct ReforgedControlsProc *proc)
{
    if (proc->cursor == CTM_ROW_RESET)
        DrawHelp("A: every key and button back to default.");
    else
        DrawHelp("A: bind. Select: clear. B: back.");
}

static void Controls_Init(struct ReforgedControlsProc *proc)
{
    int i, s;

    /* as the Randomizer screen: the options screen's set-up stays, its text and
       frames are replaced, the palettes loaded again, the mural started again */
    LoadObjUIGfx();
    LoadUiFrameGraphics();
    ResetTextFont();
    ResetText();
    InitSystemTextFont();
    EndMuralBackground();
    StartMuralBackground(proc->proc_parent, NULL, 0xe);

    BG_Fill(gBG0TilemapBuffer, 0);
    BG_Fill(gBG1TilemapBuffer, 0);

    proc->cursor = 0;
    proc->column = 0;
    proc->top = 0;
    proc->dirty = 0;
    proc->capturing = 0;
    proc->waitRelease = 0;
    proc->timer = 0;

    DrawUiFrame2(1, 2, 28, 14, 0);
    DrawUiFrame2(1, 16, 28, 4, 0);

    InitText(&sTitleText, 18);
    PutDrawText(&sTitleText, TILEMAP_LOCATED(gBG0TilemapBuffer, 2, 0), TEXT_COLOR_SYSTEM_WHITE, 0, 18,
        "Reforged Options: Controls");
    for (s = 0; s < CTL_SLOTS; s++)
    {
        InitText(&sHeadText[s], ColW(s));
        Text_InsertDrawString(&sHeadText[s], 2, TEXT_COLOR_SYSTEM_GOLD, sColumnTitles[s]);
        PutText(&sHeadText[s], TILEMAP_LOCATED(gBG0TilemapBuffer, ColX(s), CTM_HEAD_Y));
    }
    for (i = 0; i < CTM_ROWS_SHOWN; i++)
    {
        InitText(&sLabelText[i], CTM_LABEL_W);
        for (s = 0; s < CTL_SLOTS; s++)
            InitText(&sCellText[i][s], ColW(s));
    }
    DrawList(proc);
    InitText(&sHelpText, 24);
    DrawRowHelp(proc);
    BG_EnableSyncByMask(BG0_SYNC_BIT | BG1_SYNC_BIT);
}

static void EndCapture(struct ReforgedControlsProc *proc)
{
    Platform_CaptureEnd();
    proc->capturing = 0;
    proc->waitRelease = 1;
    DrawRow(proc, proc->cursor);
}

/* Binds name to the cursor's cell, unless that takes an essential action's last
   binding of its kind away; the help line says what happened. */
static void BindCell(struct ReforgedControlsProc *proc, const char *name)
{
    static char msg[64];
    int row = proc->cursor, slot = proc->column, a, s;
    const char *old = Controls_Get(row, slot);

    if (name[0] == 0)
    {
        if (Essential(row) && Controls_OthersOfKind(row, slot, NULL) == 0)
        {
            snprintf(msg, sizeof(msg), "%s needs a %s.", sRowLabels[row], CTL_SLOT_IS_PAD(slot) ? "button" : "key");
            DrawHelp(msg);
            PlaySoundEffect(SONG_SE_SYS_WINDOW_CANSEL1);
            return;
        }
    }
    else if (old[0] == 0)
    {
        /* the swap would leave the cell the name comes from empty */
        for (a = 0; a < CTL_COUNT; a++)
            for (s = 0; s < CTL_SLOTS; s++)
                if ((a != row || s != slot) && CTL_SLOT_IS_PAD(s) == CTL_SLOT_IS_PAD(slot) &&
                    strcasecmp(Controls_Get(a, s), name) == 0 && Essential(a) && a != row &&
                    Controls_OthersOfKind(a, s, name) == 0) /* (a second copy of the name does not count) */
                {
                    snprintf(msg, sizeof(msg), "That is %s's only %s.", sRowLabels[a],
                        CTL_SLOT_IS_PAD(slot) ? "button" : "key");
                    DrawHelp(msg);
                    PlaySoundEffect(SONG_SE_SYS_WINDOW_CANSEL1);
                    return;
                }
    }
    Controls_Set(row, slot, name);
    proc->dirty = 1;
    PlaySoundEffect(SONG_SE_SYS_WINDOW_SELECT1);
    DrawList(proc); /* a swap may change another row */
    DrawRowHelp(proc);
}

static void Controls_Capture(struct ReforgedControlsProc *proc)
{
    char name[CTL_NAME_MAX];
    int pad = 0;

    if (++proc->timer >= CTM_CAPTURE_FRAMES)
    {
        EndCapture(proc);
        DrawRowHelp(proc);
        PlaySoundEffect(SONG_SE_SYS_WINDOW_CANSEL1);
        return;
    }
    if (!Platform_CaptureInput(&pad, name, sizeof(name)))
        return;
    if (!pad && strcmp(name, "Escape") == 0)
    {
        EndCapture(proc);
        DrawRowHelp(proc);
        PlaySoundEffect(SONG_SE_SYS_WINDOW_CANSEL1);
        return;
    }
    if (!pad && strcmp(name, "Delete") == 0)
    {
        EndCapture(proc);
        BindCell(proc, "");
        return;
    }
    if (pad != CTL_SLOT_IS_PAD(proc->column))
    {
        DrawHelp(pad ? "This column takes a key. Esc: cancel." : "This column takes a pad button.");
        Platform_CaptureBegin(); /* keep waiting */
        return;
    }
    EndCapture(proc);
    BindCell(proc, name);
}

static void Controls_Loop(struct ReforgedControlsProc *proc)
{
    int prevRow = proc->cursor, prevCol = proc->column, y;

    if (proc->capturing)
    {
        Controls_Capture(proc);
    }
    else if (proc->waitRelease)
    {
        /* the key just bound may be a GBA button too: nothing until it is up */
        if (gKeyStatusPtr->heldKeys == 0)
            proc->waitRelease = 0;
    }
    else
    {
        if (gKeyStatusPtr->repeatedKeys & DPAD_UP)
            proc->cursor = proc->cursor > 0 ? proc->cursor - 1 : CTM_ROW_COUNT - 1;
        else if (gKeyStatusPtr->repeatedKeys & DPAD_DOWN)
            proc->cursor = proc->cursor < CTM_ROW_COUNT - 1 ? proc->cursor + 1 : 0;
        else if ((gKeyStatusPtr->repeatedKeys & DPAD_LEFT) && proc->column > 0)
            proc->column--;
        else if ((gKeyStatusPtr->repeatedKeys & DPAD_RIGHT) && proc->column < CTL_SLOTS - 1)
            proc->column++;

        if (prevRow != proc->cursor || prevCol != proc->column)
        {
            int top = proc->top;

            PlaySoundEffect(prevRow != proc->cursor ? SONG_SE_SYS_CURSOR_UD1 : SONG_SE_SYS_CURSOR_LR1);
            if (proc->cursor < top)
                top = proc->cursor;
            else if (proc->cursor >= top + CTM_ROWS_SHOWN)
                top = proc->cursor - (CTM_ROWS_SHOWN - 1);
            if (top != proc->top)
            {
                proc->top = top;
                DrawList(proc);
            }
            if (prevRow != proc->cursor)
                DrawRowHelp(proc);
        }

        if (gKeyStatusPtr->newKeys & B_BUTTON)
        {
            PlaySoundEffect(SONG_SE_SYS_WINDOW_CANSEL1);
            if (proc->dirty)
                Reforged_SaveSettings();
            Proc_Break(proc);
            return;
        }
        if (proc->cursor == CTM_ROW_RESET)
        {
            if (gKeyStatusPtr->newKeys & A_BUTTON)
            {
                Controls_ResetDefaults();
                proc->dirty = 1;
                PlaySoundEffect(SONG_SE_SYS_WINDOW_SELECT1);
                DrawList(proc);
                DrawHelp("Every key and button is back to default.");
            }
        }
        else if (gKeyStatusPtr->newKeys & A_BUTTON)
        {
            proc->capturing = 1;
            proc->timer = 0;
            Platform_CaptureBegin();
            PlaySoundEffect(SONG_SE_SYS_WINDOW_SELECT1);
            DrawRow(proc, proc->cursor);
            DrawHelp(CTL_SLOT_IS_PAD(proc->column) ? "Press a pad button. Esc: cancel. Del: clear."
                                                   : "Press a key. Esc: cancel. Del: clear.");
        }
        else if (gKeyStatusPtr->newKeys & SELECT_BUTTON)
            BindCell(proc, "");
    }

    y = RowY(proc->cursor, proc->top);
    if (y >= 0)
        DisplayUiHand(proc->cursor == CTM_ROW_RESET ? CTM_RESET_X * 8 : ColX(proc->column) * 8, y * 8);
}

static void Controls_End(struct ReforgedControlsProc *proc)
{
    Reforged_MenuRedraw(proc->proc_parent);
}

static const struct ProcCmd sProcScr_ReforgedControls[] = {
    PROC_NAME("PC_reforged_controls"),

    PROC_CALL_ARG(NewFadeOut, 8),
    PROC_WHILE(FadeOutExists),

    PROC_CALL(Controls_Init),

    PROC_CALL_ARG(NewFadeIn, 8),
    PROC_WHILE(FadeInExists),

    PROC_REPEAT(Controls_Loop),

    PROC_CALL_ARG(NewFadeOut, 8),
    PROC_WHILE(FadeOutExists),

    PROC_CALL(Controls_End),

    PROC_CALL_ARG(NewFadeIn, 8),
    PROC_WHILE(FadeInExists),

    PROC_END,
};

void *StartReforgedControlsScreen(void *parent)
{
    return Proc_StartBlocking(sProcScr_ReforgedControls, parent);
}

#endif /* PORT_PLAYED_BUILD */
