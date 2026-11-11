#include "global.h"

/*
 * "Keep game Options" (RFO_KEEP_GAME_OPTIONS, the user's request of 2026-11-04: "set
 * these as the default settings for every game"). The game's own Options screen (Animation,
 * Game Speed, Text Speed and the other ten rows) belongs to each save: a New Game starts
 * at the original's defaults (bmio.c InitPlayConfig). With the option on, the values the
 * player last chose are remembered (the settings file's game_options line, kept by
 * reforged_settings.c) and each New Game starts with them.
 *
 * Remembered: when the Options screen closes (uiconfig.c Config_HandleExit), and when the
 * option is switched on in the in-chapter Reforged screen (a game is loaded there; on the
 * title screen gPlaySt holds no game's choices). Applied: right after InitPlayConfig in
 * the New Game (bmsave.c), before the new game is first written, through the game's own
 * SetGameOption, so each value is one the Options screen itself can set. Off, nothing is
 * read or written: the game is the original. A display-only option: it never marks a save.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fontgrp.h"
#include "uiconfig.h"

#include "port_hw.h"
#include "port_reforged.h"

#if PORT_PLAYED_BUILD

/* The choices the Options screen offers for the option (its selectors with a label). */
static int ChoiceCount(int option)
{
    int n = 0;

    while (n < 4 && gGameOptions[option].selectors[n].optionTextId != 0)
        n++;
    return n;
}

static void LogValues(const char *what, const unsigned char *v)
{
    char line[320];
    int i, at = 0;

    for (i = 0; i < RFO_GAME_OPTION_COUNT && at < (int)sizeof(line) - 24; i++)
        at += snprintf(line + at, sizeof(line) - at, " %s:%d", Reforged_GameOptionKey(i), v[i]);
    line[at] = '\0';
    PortLog("[reforged] keep game Options: %s%s\n", what, line);
}

int Reforged_GameOptionsCapture(void)
{
    unsigned char v[RFO_GAME_OPTION_COUNT], old[RFO_GAME_OPTION_COUNT];
    int i;

    if (!Reforged_GetOption(RFO_KEEP_GAME_OPTIONS))
        return 0;
    for (i = 0; i < RFO_GAME_OPTION_COUNT; i++)
        v[i] = GetGameOption(gGameOptionsUiOrder[i]);
    if (Reforged_GameOptionsGet(old) && memcmp(v, old, sizeof(v)) == 0)
        return 0;
    Reforged_GameOptionsSet(v);
    LogValues("remembered", v);
    return Reforged_SaveSettings() != 0 ? -1 : 0;
}

/* FE8PC_RFO_CHECK (a test switch, DIVERGENCES PB-9): at the first New Game with the option on,
   remember changed values, write the settings file, read it back, and compare; the game's
   Options and the remembered values are then put back as they were. */
static void SelfCheck(void)
{
    static int done;
    unsigned char before[RFO_GAME_OPTION_COUNT], want[RFO_GAME_OPTION_COUNT], got[RFO_GAME_OPTION_COUNT];
    int had, i, fails = 0;

    if (done || getenv("FE8PC_RFO_CHECK") == NULL)
        return;
    done = 1;
    had = Reforged_GameOptionsGet(before); /* always 1: Apply runs this only with values */
    for (i = 0; i < RFO_GAME_OPTION_COUNT; i++)
        want[i] = GetGameOption(gGameOptionsUiOrder[i]);
    /* a different choice on every row the screen offers more than one of */
    for (i = 0; i < RFO_GAME_OPTION_COUNT; i++)
    {
        int n = ChoiceCount(gGameOptionsUiOrder[i]);

        if (n > 1)
            SetGameOption(gGameOptionsUiOrder[i], (u8)((want[i] + 1) % n));
        got[i] = GetGameOption(gGameOptionsUiOrder[i]);
    }
    if (Reforged_GameOptionsCapture() != 0)
        fails++; /* not written: the file is not read back (it holds the old values) */
    else
    {
        Reforged_LoadSettings(); /* the file as written */
    }
    if (!fails)
    {
        unsigned char back[RFO_GAME_OPTION_COUNT];

        if (!Reforged_GameOptionsGet(back) || memcmp(back, got, sizeof(back)) != 0)
            fails++;
    }
    for (i = 0; i < RFO_GAME_OPTION_COUNT; i++)
        SetGameOption(gGameOptionsUiOrder[i], want[i]);
    if (had)
        Reforged_GameOptionsSet(before);
    Reforged_SaveSettings();
    PortLog("[reforged] check: keep game Options: remembered, written and read back: %s\n",
        fails ? "FAILED" : "ok");
}

void Reforged_GameOptionsApply(void)
{
    unsigned char v[RFO_GAME_OPTION_COUNT];
    int i;

    if (!Reforged_GetOption(RFO_KEEP_GAME_OPTIONS) || !Reforged_GameOptionsGet(v))
        return;
    for (i = 0; i < RFO_GAME_OPTION_COUNT; i++)
    {
        int option = gGameOptionsUiOrder[i];

        if (v[i] < ChoiceCount(option))
            SetGameOption((u8)option, v[i]);
        v[i] = GetGameOption((u8)option); /* the log shows what the game now holds */
    }
    LogValues("the new game starts with", v);
    SelfCheck();
}

#endif /* PORT_PLAYED_BUILD */
