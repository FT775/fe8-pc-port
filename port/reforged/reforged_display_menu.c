#include "global.h"

/*
 * Reforged Options, the Display screen: opened by A on "Display >" in the options
 * screen (reforged_menu.c); a list screen (reforged_listmenu.h, drawn by
 * reforged_randomizer_menu.c's code). Played builds only; empty in every test build.
 * Window size, scaling, smoothing, fullscreen and widescreen (RFO_WINDOW_SCALE ...
 * RFO_WIDESCREEN, display-only: they never mark a save); v1.31.0: the update notice
 * (RFO_UPDATE_NOTICE, reforged_update.c). A change is applied to the
 * window at once (port/platform.h Platform_SetWindowScale, Platform_SetScaling,
 * Platform_SetFullscreen, Platform_SetWide; widescreen also port/render/render.h
 * RenderSetWide) and kept in the settings file; the saved values are
 * applied when the settings are read at start (Reforged_ApplyDisplay).
 */

#if PORT_PLAYED_BUILD

#include <stdio.h>

#include "port_reforged.h"
#include "reforged_listmenu.h"
#include "../platform.h"
#include "../render/render.h"

void PortHq_SetRow(int on); /* port/audio/audio_pc.h (v1.32.0) */

void Reforged_ApplyDisplay(int option)
{
    /* Widescreen (v1.25.0) first, so the window size below is taken in its shape: the renderer's picture
       and the window's width change together (a frame of the other width would be dropped). */
    if (option < 0 || option == RFO_WIDESCREEN)
    {
        int wide = Reforged_GetOption(RFO_WIDESCREEN) != 0;

        RenderSetWide(wide);
        Platform_SetWide(wide ? RENDER_WIDE_W : PLATFORM_GBA_WIDTH);
    }
    if (option < 0 || option == RFO_WINDOW_SCALE)
        Platform_SetWindowScale(Reforged_GetOption(RFO_WINDOW_SCALE));
    if (option < 0 || option == RFO_SCALE_MODE || option == RFO_SCALE_SMOOTH)
        Platform_SetScaling(Reforged_GetOption(RFO_SCALE_MODE), Reforged_GetOption(RFO_SCALE_SMOOTH));
    if (option < 0 || option == RFO_FULLSCREEN)
        Platform_SetFullscreen(Reforged_GetOption(RFO_FULLSCREEN));
    if (option < 0 || option == RFO_UPDATE_NOTICE)
        Reforged_UpdateCheck(); /* v1.31.0: once per run, the row on (reforged_update.c) */
    if (option < 0 || option == RFO_AUDIO_HQ)
        PortHq_SetRow(Reforged_GetOption(RFO_AUDIO_HQ)); /* v1.32.0: "Enhanced sound" (port/audio/hq_pc.c) */
}

/* The help lines are at most 192 pixels (24 tiles). */
static const struct RandoRow sDisplayRows[] = {
    RR_NUM(RFO_WINDOW_SCALE, RN_SCALE, 0, 1, "Window size", "Auto: 3x, or less if the screen is small."),
    { RR_CHOICE, RFO_SCALE_MODE, 3, 0, 0, 0, "Scaling", "Pixel: sharp, black bars. Fill/Stretch.",
      { "Pixel", "Fill", "Stretch" }, { 0, 1, 2 }, NULL, NULL, 0 },
    RR_SWITCH(RFO_SCALE_SMOOTH, "Smoothing", "Soften the pixels when filling or stretching."),
    RR_SWITCH(RFO_FULLSCREEN, "Fullscreen", "Start fullscreen. F11 still switches."),
    RR_SWITCH(RFO_WIDESCREEN, "Widescreen", "A 16:9 window, the game in the middle."),
    RR_SWITCH(RFO_UPDATE_NOTICE, "Updates", "Title screen: a newer version; Select installs."),
    { RR_CHOICE, RFO_AUDIO_HQ, 2, 0, 0, 0, "Sound", "Enhanced: smoother music, no GBA hiss.",
      { "Original", "Enhanced" }, { 0, 1 }, NULL, NULL, 0 },
};

static const struct ReforgedListScreen sDisplayScreen = {
    "Reforged Options: Display", sDisplayRows, (int)(sizeof(sDisplayRows) / sizeof(sDisplayRows[0])),
    -1, 0, Reforged_ApplyDisplay,
};

void *StartReforgedDisplayScreen(void *parent)
{
    return StartReforgedListScreen(parent, &sDisplayScreen);
}

/*
 * The Option sets screen ("Option sets >"): three sets of options (everything on
 * the options, Cheats and Randomizer screens, and the seed; not the controls or the
 * Display options), each saved to and loaded from option-set-N.txt in the per-user
 * folder (reforged_settings.c). Save writes the options as they are now; Load puts
 * them all back (anything the set file does not name at its default) and writes the
 * settings file. Each row shows whether its set is there.
 */
static char sSetMsg[48];

static const char *SaveSet(int n)
{
    snprintf(sSetMsg, sizeof(sSetMsg), Reforged_SaveOptionSet(n) ? "Saved as set %d." : "Set %d: could not save.", n);
    return sSetMsg;
}

static const char *LoadSet(int n)
{
    snprintf(sSetMsg, sizeof(sSetMsg), Reforged_LoadOptionSet(n) ? "Set %d loaded." : "Set %d is empty.", n);
    return sSetMsg;
}

static const char *SetState(int n)
{
    return Reforged_OptionSetExists(n) ? "Saved" : "Empty";
}

static const struct RandoRow sSetRows[] = {
    RR_HEAD("-- Save --"),
    RR_ACT(SaveSet, SetState, 1, "Save to set 1", "Save every option (not keys, not Display)."),
    RR_ACT(SaveSet, SetState, 2, "Save to set 2", "Save every option (not keys, not Display)."),
    RR_ACT(SaveSet, SetState, 3, "Save to set 3", "Save every option (not keys, not Display)."),
    RR_HEAD("-- Load --"),
    RR_ACT(LoadSet, SetState, 1, "Load set 1", "Put set 1's options back."),
    RR_ACT(LoadSet, SetState, 2, "Load set 2", "Put set 2's options back."),
    RR_ACT(LoadSet, SetState, 3, "Load set 3", "Put set 3's options back."),
};

static const struct ReforgedListScreen sSetScreen = {
    "Reforged Options: Option sets", sSetRows, (int)(sizeof(sSetRows) / sizeof(sSetRows[0])), -1, 0, NULL,
};

void *StartReforgedOptionSetsScreen(void *parent)
{
    return StartReforgedListScreen(parent, &sSetScreen);
}

#endif /* PORT_PLAYED_BUILD */
