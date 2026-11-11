#include "global.h"

/*
 * Reforged Options, the Cheats screen (port-plan/reforged-next-briefs.md item 6):
 * opened by A on "Cheats >" in the options screen (reforged_menu.c), from the
 * start: no unlock and no Konami code (the user's decision of 2026-10-29).
 * Played builds only; empty in every test build.
 *
 * A blocking child of the options screen's proc, as the options screen is of
 * the save menu's. It fades out, draws its own frames and text on BG0/BG1
 * (the mural on BG3 is started again for its palette), and on its way out
 * (behind a fade out) has the options screen draw itself again
 * (Reforged_MenuRedraw), then fades back in.
 *
 * Rows (every cheat OFF by default; the rules are in reforged_cheats.c):
 *   Infinite gold   On / Off
 *   Growths         Off / Fixed / +25% / +50%
 *   EXP gained      x1 / x2 / x3
 * Up/Down move, Left/Right or A change a row, B goes back (and writes the
 * settings file if a value changed).
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

enum {
    CHT_ROW_COUNT = 3,

    CHT_LIST_TOP = 3,  /* tile row of the first entry */
    CHT_LABEL_X = 4,   /* tile column of the labels */
    CHT_VALUE_X = 14,  /* tile column of a row's first value */
    CHT_VALUE_W = 14,  /* tiles of a row's values */
    CHT_VALUE_GAP = 7, /* pixels between two values */
};

struct ReforgedCheatsProc {
    PROC_HEADER;
    /* 29 */ u8 cursor;
    /* 2A */ u8 dirty;
};

/* Each row: its option, label, help line and values (left to right; the value
   stored for each is its position). */
struct CheatRow {
    u8 option;
    const char *label;
    const char *help;
    u8 count;
    const char *values[4];
    u8 order[4]; /* the stored value of each shown value */
};

static const struct CheatRow sCheatRows[CHT_ROW_COUNT] = {
    { RFO_CHEAT_GOLD, "Infinite gold", "Gold stays at 999,999; gems become items.",
      2, { "On", "Off" }, { 1, 0 } },
    { RFO_CHEAT_GROWTHS, "Growths", "Fixed: average gains. Or more growth.",
      4, { "Off", "Fixed", "+25%", "+50%" }, { 0, 1, 2, 3 } },
    { RFO_CHEAT_EXP, "EXP gained", "More EXP, still one level a battle.",
      3, { "x1", "x2", "x3" }, { 0, 1, 2 } },
};

static struct Text sCheatTitleText;
static struct Text sCheatHelpText;
static struct Text sCheatRowText[CHT_ROW_COUNT];
static struct Text sCheatValueText[CHT_ROW_COUNT];

static void DrawCheatValue(int row)
{
    const struct CheatRow *r = &sCheatRows[row];
    int cur = Reforged_GetOption(r->option), i, x = 0;

    ClearText(&sCheatValueText[row]);
    for (i = 0; i < r->count; i++)
    {
        Text_InsertDrawString(&sCheatValueText[row], x,
            r->order[i] == cur ? TEXT_COLOR_SYSTEM_BLUE : TEXT_COLOR_SYSTEM_GRAY, r->values[i]);
        x += GetStringTextLen(r->values[i]) + CHT_VALUE_GAP;
    }
    PutText(&sCheatValueText[row], TILEMAP_LOCATED(gBG0TilemapBuffer, CHT_VALUE_X, CHT_LIST_TOP + row * 2));
    BG_EnableSyncByMask(BG0_SYNC_BIT);
}

static void DrawCheatHelp(int row)
{
    ClearText(&sCheatHelpText);
    Text_InsertDrawString(&sCheatHelpText, 0, TEXT_COLOR_SYSTEM_WHITE, sCheatRows[row].help);
    PutText(&sCheatHelpText, TILEMAP_LOCATED(gBG0TilemapBuffer, 3, 17));
    BG_EnableSyncByMask(BG0_SYNC_BIT);
}

static void CheatsMenu_Init(struct ReforgedCheatsProc *proc)
{
    int i;

    /* the options screen's set-up stays (backgrounds, BG priorities); its
       text and frames are replaced. The fade out left every palette black,
       and the fade in starts from the palettes as they are then, so each
       graphic's palette is loaded again: the frames, the hand, the text and
       the mural (started again, as the options screen's child, so it lives
       on after this screen). */
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
    proc->dirty = 0;

    DrawUiFrame2(1, 2, 28, 14, 0); /* list: tile rows 2-15 */
    DrawUiFrame2(1, 16, 28, 4, 0); /* help line: rows 16-19 */

    InitText(&sCheatTitleText, 16);
    PutDrawText(&sCheatTitleText, TILEMAP_LOCATED(gBG0TilemapBuffer, 2, 0), TEXT_COLOR_SYSTEM_WHITE, 0, 16,
        "Reforged Options: Cheats");

    for (i = 0; i < CHT_ROW_COUNT; i++)
    {
        InitText(&sCheatRowText[i], CHT_VALUE_X - CHT_LABEL_X);
        PutDrawText(&sCheatRowText[i], TILEMAP_LOCATED(gBG0TilemapBuffer, CHT_LABEL_X, CHT_LIST_TOP + i * 2),
            TEXT_COLOR_SYSTEM_WHITE, 0, CHT_VALUE_X - CHT_LABEL_X, sCheatRows[i].label);

        InitText(&sCheatValueText[i], CHT_VALUE_W);
        DrawCheatValue(i);
    }

    InitText(&sCheatHelpText, 24);
    DrawCheatHelp(0);

    BG_EnableSyncByMask(BG0_SYNC_BIT | BG1_SYNC_BIT);
}

static void CheatsMenu_Loop(struct ReforgedCheatsProc *proc)
{
    const struct CheatRow *r;
    int prev = proc->cursor, row, i, cur, want = -1;

    if (gKeyStatusPtr->repeatedKeys & DPAD_UP)
    {
        if (proc->cursor != 0)
            proc->cursor--;
        else if (gKeyStatusPtr->newKeys & DPAD_UP)
            proc->cursor = CHT_ROW_COUNT - 1;
    }
    else if (gKeyStatusPtr->repeatedKeys & DPAD_DOWN)
    {
        if (proc->cursor < CHT_ROW_COUNT - 1)
            proc->cursor++;
        else if (gKeyStatusPtr->newKeys & DPAD_DOWN)
            proc->cursor = 0;
    }

    if (prev != proc->cursor)
    {
        PlaySoundEffect(SONG_SE_SYS_CURSOR_UD1);
        DrawCheatHelp(proc->cursor);
    }

    row = proc->cursor;
    r = &sCheatRows[row];

    if (gKeyStatusPtr->newKeys & B_BUTTON)
    {
        PlaySoundEffect(SONG_SE_SYS_WINDOW_CANSEL1);
        if (proc->dirty)
            Reforged_SaveSettings();
        Proc_Break(proc);
        return;
    }

    cur = Reforged_GetOption(r->option);
    for (i = 0; i < r->count - 1 && r->order[i] != cur; i++)
        ;
    if ((gKeyStatusPtr->newKeys & DPAD_LEFT) && i > 0)
        want = r->order[i - 1];
    else if ((gKeyStatusPtr->newKeys & DPAD_RIGHT) && i < r->count - 1)
        want = r->order[i + 1];
    else if (gKeyStatusPtr->newKeys & A_BUTTON)
        want = r->order[(i + 1) % r->count];

    if (want >= 0 && want != cur)
    {
        Reforged_SetOption(r->option, want);
        proc->dirty = 1;
        PlaySoundEffect(SONG_SE_SYS_CURSOR_LR1);
        DrawCheatValue(row);
    }

    DisplayUiHand(CHT_LABEL_X * 8 - 8, (CHT_LIST_TOP + row * 2) * 8);
}

static void CheatsMenu_End(struct ReforgedCheatsProc *proc)
{
    Reforged_MenuRedraw(proc->proc_parent);
}

static const struct ProcCmd sProcScr_ReforgedCheats[] = {
    PROC_NAME("PC_reforged_cheats"),

    PROC_CALL_ARG(NewFadeOut, 8),
    PROC_WHILE(FadeOutExists),

    PROC_CALL(CheatsMenu_Init),

    PROC_CALL_ARG(NewFadeIn, 8),
    PROC_WHILE(FadeInExists),

    PROC_REPEAT(CheatsMenu_Loop),

    PROC_CALL_ARG(NewFadeOut, 8),
    PROC_WHILE(FadeOutExists),

    PROC_CALL(CheatsMenu_End),

    PROC_CALL_ARG(NewFadeIn, 8),
    PROC_WHILE(FadeInExists),

    PROC_END,
};

void *StartReforgedCheatsScreen(void *parent)
{
    return Proc_StartBlocking(sProcScr_ReforgedCheats, parent);
}

#endif /* PORT_PLAYED_BUILD */
