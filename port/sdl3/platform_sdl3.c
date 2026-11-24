#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <errno.h>
#include <sys/stat.h>
#endif

#include <SDL3/SDL.h>

#include "../platform.h"
#include "port_controls.h"
#ifdef FE8PC_TESTBUILD
#include "test/sdl_testhook.h"
#endif

/*
 * Fast-forward (Reforged Options; port/reforged/reforged_fastforward.c): the
 * played window builds only, PORT_RFO_FASTFORWARD, which port/Makefile gives
 * this object on the same condition as that file's PORT_PLAYED_BUILD (no
 * FE8PC_TESTBUILD, no FE8PC_DEBUG). A test build has none of it, and the
 * headless builds (the Gate 0 binary among them) do not use this file. Hold Space, or press in the right stick: the clock the pacer sees
 * runs N times as fast (Platform_NowNs, Platform_SleepUntilNs), every N-th
 * frame is presented, and the stream plays N times as fast (muted unless
 * "Fast-forward sound" is "Sped up"). Space and the stick click are not GBA
 * keys: they never reach REG_KEYINPUT.
 */
#ifdef PORT_RFO_FASTFORWARD
#define PLATFORM_FAST_FORWARD 1
#include "port_reforged.h"
#endif

/*
 * Mouse controls (Reforged Options, the Controls screen's "Mouse" row, v1.29.0; port/reforged/reforged_mouse.c):
 * the played window builds only (the fast-forward block's condition). With the row on, each mouse event is
 * turned into a pixel of the picture shown (SDL_ConvertEventToRenderCoordinates: the renderer's logical
 * presentation, so Pixel / Fill / Stretch, fullscreen, any window size and the display's scaling need nothing
 * of ours) and handed to Reforged_MouseEvent; Platform_ReadKeys ORs in Reforged_MouseKeys, the keys the mouse
 * presses (the same per-frame key path as the keyboard's). With the row off the events are dropped.
 * A button pressed in the frame step the window got the focus, or the next one, is dropped (clicking the
 * window to focus it is not A). The side buttons (X1, X2) do nothing. The wheel's notches are counted
 * whole (a smooth wheel's fractions add up).
 * FE8PC_TEST_MOUSE=FILE (a test switch, port/tools/mousecheck.sh): mouse events put into SDL's queue as if
 * the mouse made them, one line each: "POLL move X Y", "POLL down|up|click left|middle|right|x1|x2 X Y",
 * "POLL wheel DY [DX]", "POLL focus", and to test the picture's mapping "POLL size W H" (the window's size)
 * and "POLL fullscreen 0|1"; POLL counts frame steps (Platform_PollEvents calls; poll P comes after the
 * picture dumped as frame P was drawn, the mouse log's "frame P + 1"), X Y are window pixels, or picture pixels with FE8PC_TEST_MOUSE_PICTURE=1 (turned into
 * window pixels by SDL_RenderCoordinatesToWindow at that poll). # starts a comment.
 */
#ifdef PORT_RFO_FASTFORWARD
#define PLATFORM_MOUSE 1
#endif

/*
 * Save states (Reforged Options, Helpers: "Save states", v1.24.0; port/reforged/reforged_savestate.c):
 * the played builds only (PORT_RFO_STATE). With the row on, Ctrl+S asks for a save and Ctrl+L for a
 * load (either Ctrl; on the press of S or L, or of Ctrl with S or L held). With the row off (v1.24.1) the
 * same press only shows the hint "Save states are off" (PortState_OffHint), once per press; the keys then
 * reach their bindings exactly as before. While a Ctrl is held (the row on), S and
 * L reach no GBA button, fast-forward or rewind binding, and a key that made the combination stays
 * away from them until it is let go. After a load (Platform_ReleaseKeys) every key and gamepad input
 * held then is ignored until it is released. Platform_AudioJoin: the sound of the state just loaded
 * follows without a click. FE8PC_TEST_HOLD_KEYS (a test switch, PB-9): keys held as if pressed on the
 * keyboard, "STEP:KEY[+KEY...]:STEPS[,...]" (SDL key names; LCtrl / RCtrl for Left Ctrl / Right Ctrl),
 * counted in frame steps since the start (Platform_PollEvents calls, which a load does not put back, so a
 * key can be held across a load); before any load a step is the frame of that number.
 */
#include "port_state.h"
#if PORT_STATE_ON
#define PLATFORM_SAVE_STATES 1
#endif

#ifdef _WIN32
/* port/win32/posix_win32.c */
int PortWin32RoamingDir(char *out, size_t outSize);
#endif

/*
 * SDL3 backend of the platform interface (port-plan/PLAN.md section 3.7),
 * built by port/Makefile.sdl into fe8pc-sdl (Linux) and by port/Makefile.mingw
 * SDL=1 into fe8pc-sdl.exe (Windows, with SDL3.dll beside it), the same file
 * on both. The only port code that includes SDL headers (and its test hooks,
 * port/sdl3/test, in test builds only). Tested with SDL 3.4.2 (Ubuntu
 * libsdl3-dev:i386; on Windows the official SDL3-devel-3.4.2-mingw i686 DLL).
 * On Windows SDL picks its own drivers (windows video, Direct3D 11 renderer,
 * WASAPI audio), the per-user folder is %APPDATA%\fe8pc\, and the log goes
 * where port/win32/gui/gui_win32.c sends it (fe8pc-sdl.log next to the exe,
 * or the console with --console).
 *
 * Picture: one streaming 240x160 texture, scaled by a whole number with
 * nearest-neighbour sampling (SDL_SCALEMODE_NEAREST; not SDL 3.4's PIXELART
 * mode, which smooths edges) and letterboxed when the window is not an exact
 * multiple (SDL_LOGICAL_PRESENTATION_INTEGER_SCALE). No filter, no colour
 * change: the pixels are the renderer's D3 output. VSync is off, so
 * presenting never holds up the frame step; the pacer sets the rate.
 *
 * Keys, by scancode so the keyboard layout does not matter (the user's layout),
 * as bound in port/hw/controls.c (port/include/port_controls.h; the played
 * builds' Controls screen and settings file change them). The defaults:
 *   A: Z   B: X   L: Q   R: E   Start: Enter   Select: Backspace
 *   D-pad: the arrow keys
 *   Test builds only (FE8PC_TESTBUILD=1): either Shift is also Select.
 * Gamepads (any SDL3 gamepad, hot-plugged), the defaults:
 *   A and B: the face buttons labelled A and B (cross and circle on
 *   PlayStation pads), so a Nintendo pad matches the GBA's own layout;
 *   unlabelled pads use south for A and east for B.
 *   L and R: the shoulder buttons or the triggers. Start: start. Select: back.
 *   D-pad: the D-pad; the left stick (past half way) always moves too.
 * Opposite directions held together read as neither (mGBA's default), since
 * a real D-pad cannot press both.
 * F11 toggles fullscreen (bindable). Closing the window (or SIGINT/SIGTERM) quits.
 * Played builds: Space, or the right stick's click, held = fast-forward
 * (Reforged Options; above; bindable).
 *
 * Saves: the window build keeps the player's save file by default, the ROM
 * path with .sav for its extension (baserom.gba -> baserom.sav), the raw
 * 32 KiB SRAM image mGBA uses; --save PATH, FE8PC_SAVE=PATH, --no-save or
 * FE8PC_SAVE=none change that (port/save/savefile_pc.c). When the ROM's
 * folder cannot be written, the default file goes to the per-user folder
 * (Platform_UserDir, the folder SDL's pref path names, made only then) and
 * the window title says where; a save there as well as next to the ROM is
 * named in the title ("two save files: using X, other at Y"); a save that
 * fails, a read-only save file, or
 * a save file another program changed puts "SAVE FAILED" in the title; a
 * second copy of the game on the same save file says "save in use by another
 * copy: not saving" there and never writes it (Platform_SetTitleNote).
 *
 * Environment:
 *   FE8PC_SCALE=n   window scale, 1 to 10. Default 3, lowered if the display
 *                   is too small.
 *   FE8PC_SAVE=PATH the save file (see above); none: no save file.
 *   SDL_VIDEO_DRIVER, SDL_RENDER_DRIVER: SDL's own overrides (for example
 *                   x11 or wayland, and software or opengl).
 */

#ifdef _WIN32
/*
 * SDL3.dll is built with MinGW's default struct layout (-mms-bitfields,
 * -malign-double), the port's objects with the Linux i386 one
 * (port/Makefile.mingw). Some SDL structs differ between the two (SDL_PathInfo,
 * SDL_InitState, the sensor events' sensor_timestamp, and the trailing
 * padding of most event structs inside the 128-byte SDL_Event; port/win32/
 * README.md, "SDL's structs"). The members this file and its test hooks share
 * with SDL do not differ: these checks hold under both layouts (both were
 * compiled and compared). Another SDL struct needs the same check before the
 * Windows build uses it.
 */
SDL_COMPILE_TIME_ASSERT(win32_event_size, sizeof(SDL_Event) == 128);
SDL_COMPILE_TIME_ASSERT(win32_key_scancode, offsetof(SDL_KeyboardEvent, scancode) == 24);
SDL_COMPILE_TIME_ASSERT(win32_key_repeat, offsetof(SDL_KeyboardEvent, repeat) == 37);
SDL_COMPILE_TIME_ASSERT(win32_gdevice_which, offsetof(SDL_GamepadDeviceEvent, which) == 16);
SDL_COMPILE_TIME_ASSERT(win32_audio_spec, sizeof(SDL_AudioSpec) == 12 && offsetof(SDL_AudioSpec, freq) == 8);
SDL_COMPILE_TIME_ASSERT(win32_rect, sizeof(SDL_Rect) == 16 && offsetof(SDL_Rect, h) == 12);
SDL_COMPILE_TIME_ASSERT(win32_surface, offsetof(SDL_Surface, w) == 8 && offsetof(SDL_Surface, pitch) == 16 &&
                                           offsetof(SDL_Surface, pixels) == 20);
SDL_COMPILE_TIME_ASSERT(win32_vjoy_desc, sizeof(SDL_VirtualJoystickDesc) == 84 &&
                                             offsetof(SDL_VirtualJoystickDesc, name) == 36);
/* v1.29.0, mouse controls: the mouse events' members this file reads and the test switch writes */
SDL_COMPILE_TIME_ASSERT(win32_mouse_motion, offsetof(SDL_MouseMotionEvent, windowID) == 16 &&
                                                offsetof(SDL_MouseMotionEvent, which) == 20 &&
                                                offsetof(SDL_MouseMotionEvent, x) == 28 &&
                                                offsetof(SDL_MouseMotionEvent, y) == 32);
SDL_COMPILE_TIME_ASSERT(win32_mouse_button, offsetof(SDL_MouseButtonEvent, windowID) == 16 &&
                                                offsetof(SDL_MouseButtonEvent, button) == 24 &&
                                                offsetof(SDL_MouseButtonEvent, down) == 25 &&
                                                offsetof(SDL_MouseButtonEvent, x) == 28 &&
                                                offsetof(SDL_MouseButtonEvent, y) == 32);
SDL_COMPILE_TIME_ASSERT(win32_mouse_wheel, offsetof(SDL_MouseWheelEvent, windowID) == 16 &&
                                               offsetof(SDL_MouseWheelEvent, x) == 24 &&
                                               offsetof(SDL_MouseWheelEvent, y) == 28 &&
                                               offsetof(SDL_MouseWheelEvent, direction) == 32 &&
                                               offsetof(SDL_MouseWheelEvent, mouse_x) == 36 &&
                                               offsetof(SDL_MouseWheelEvent, mouse_y) == 40);
SDL_COMPILE_TIME_ASSERT(win32_window_event, offsetof(SDL_WindowEvent, windowID) == 16);
#endif

#define GBA_W PLATFORM_GBA_WIDTH
#define GBA_H PLATFORM_GBA_HEIGHT
#define DEFAULT_SCALE 3
#define MAX_SCALE 10
#define MAX_PADS 4
#define STICK_THRESHOLD 16384   /* half way, of 32767 */
#define TRIGGER_THRESHOLD 16384

static SDL_Window *sWindow;
static SDL_Renderer *sRenderer;
static SDL_Texture *sTexture;
static SDL_AudioStream *sAudio;
static int sQuit;
static int sFullscreen;
/* The picture's width (Platform_SetWide: GBA_W, or the Widescreen row's), the window's scale (the last one
   chosen or set), and the presentation and texture filter Platform_SetScaling chose, which a new width keeps. */
static int sPresentW = GBA_W;
static int sScale = DEFAULT_SCALE;
static SDL_RendererLogicalPresentation sPresentation = SDL_LOGICAL_PRESENTATION_INTEGER_SCALE;
static SDL_ScaleMode sTexScale = SDL_SCALEMODE_NEAREST;

struct Pad
{
    SDL_Gamepad *pad;
    SDL_JoystickID id;
    SDL_GamepadButton buttonA, buttonB;
};
static struct Pad sPads[MAX_PADS];

#ifdef FE8PC_TESTBUILD
/* Test builds only (FE8PC_TESTBUILD=1, port/Makefile; port-plan/DIVERGENCES.md TB-1):
   either Shift is a second Select, whatever the bindings. */
static const struct
{
    SDL_Scancode scancode;
    uint16_t key;
} sKeyMap[] = {
    { SDL_SCANCODE_LSHIFT,    PLATFORM_KEY_SELECT },
    { SDL_SCANCODE_RSHIFT,    PLATFORM_KEY_SELECT },
};
#endif

/* A test build says so in the window title. */
#ifdef FE8PC_TESTBUILD
#define WINDOW_TITLE "FE8 PC port (test build)"
#else
#define WINDOW_TITLE "FE8 PC port"
#endif

/* The gamepad binding names (port/include/port_controls.h): a button, or one
   of these codes. */
enum { PADB_NONE = -1, PADB_A = -2, PADB_B = -3, PADB_ZL = -4, PADB_ZR = -5 };

static const struct
{
    const char *name;
    int code;
} sPadNames[] = {
    { "a", PADB_A },                                  { "b", PADB_B },
    { "west", SDL_GAMEPAD_BUTTON_WEST },              { "north", SDL_GAMEPAD_BUTTON_NORTH },
    { "back", SDL_GAMEPAD_BUTTON_BACK },              { "start", SDL_GAMEPAD_BUTTON_START },
    { "guide", SDL_GAMEPAD_BUTTON_GUIDE },
    { "l", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER },        { "r", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
    { "zl", PADB_ZL },                                { "zr", PADB_ZR },
    { "dpup", SDL_GAMEPAD_BUTTON_DPAD_UP },           { "dpdown", SDL_GAMEPAD_BUTTON_DPAD_DOWN },
    { "dpleft", SDL_GAMEPAD_BUTTON_DPAD_LEFT },       { "dpright", SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
    { "lstick", SDL_GAMEPAD_BUTTON_LEFT_STICK },      { "rstick", SDL_GAMEPAD_BUTTON_RIGHT_STICK },
};

/* The bindings resolved (Controls_Generation tells when to again). */
static struct
{
    unsigned generation;
    SDL_Scancode key[CTL_COUNT][2];
    int pad[CTL_COUNT][2];
} sBound;

static int PadCode(const char *name)
{
    unsigned i;

    for (i = 0; i < SDL_arraysize(sPadNames); i++)
        if (SDL_strcasecmp(name, sPadNames[i].name) == 0)
            return sPadNames[i].code;
    return PADB_NONE;
}

static void ResolveBindings(void)
{
    int a, k;

    if (sBound.generation == Controls_Generation())
        return;
    for (a = 0; a < CTL_COUNT; a++)
        for (k = 0; k < 2; k++)
        {
            const char *key = Controls_Get(a, CTL_KEY1 + k), *pad = Controls_Get(a, CTL_PAD1 + k);

            sBound.key[a][k] = key[0] != '\0' ? SDL_GetScancodeFromName(key) : SDL_SCANCODE_UNKNOWN;
            sBound.pad[a][k] = pad[0] != '\0' ? PadCode(pad) : PADB_NONE;
        }
    sBound.generation = Controls_Generation();
}

/* The settings file separates a row's bindings with '|' (reforged_settings.c):
   SDL's "Keypad |" and "Keypad ||" cannot be written there, so they are never
   binding names. */
static int KeyNameUsable(const char *name)
{
    return name != NULL && name[0] != '\0' && SDL_strchr(name, '|') == NULL;
}

int Platform_BindingValid(int pad, const char *name)
{
    if (!KeyNameUsable(name))
        return 0;
    return pad ? PadCode(name) != PADB_NONE : SDL_GetScancodeFromName(name) != SDL_SCANCODE_UNKNOWN;
}

void Platform_CanonicalName(int pad, const char *name, char *out, size_t size)
{
    unsigned i;

    SDL_snprintf(out, size, "%s", name != NULL ? name : "");
    if (!Platform_BindingValid(pad, name))
        return;
    if (!pad)
    {
        SDL_snprintf(out, size, "%s", SDL_GetScancodeName(SDL_GetScancodeFromName(name)));
        return;
    }
    for (i = 0; i < SDL_arraysize(sPadNames); i++)
        if (SDL_strcasecmp(name, sPadNames[i].name) == 0)
            SDL_snprintf(out, size, "%s", sPadNames[i].name);
}

static void Log(const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(1);
static void Log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

/* Platform_SetTitleNote's note, shown after the title ("" for none). */
static char sTitleNote[4200];

static void ApplyTitle(void)
{
    char title[sizeof(sTitleNote) + 64];

    if (sWindow == NULL)
        return; /* Platform_Init applies it */
    if (sTitleNote[0] != '\0')
        SDL_snprintf(title, sizeof(title), "%s - %s", WINDOW_TITLE, sTitleNote);
    else
        SDL_snprintf(title, sizeof(title), "%s", WINDOW_TITLE);
    if (!SDL_SetWindowTitle(sWindow, title))
        Log("[sdl] SDL_SetWindowTitle failed: %s\n", SDL_GetError());
}

void Platform_SetTitleNote(const char *note)
{
    SDL_snprintf(sTitleNote, sizeof(sTitleNote), "%s", note != NULL ? note : "");
    ApplyTitle();
}

/* FE8PC_SCALE when it is a usable scale (1 to MAX_SCALE), else 0. */
static int EnvScale(void)
{
    const char *s = getenv("FE8PC_SCALE");
    int scale = s != NULL ? atoi(s) : 0;

    return scale >= 1 && scale <= MAX_SCALE ? scale : 0;
}

static int ChooseScale(void)
{
    const char *s = getenv("FE8PC_SCALE");
    SDL_Rect usable;
    int scale;

    if (EnvScale() > 0)
        return EnvScale();
    if (s != NULL)
        Log("[sdl] FE8PC_SCALE=%s ignored (1 to %d)\n", s, MAX_SCALE);
    scale = DEFAULT_SCALE;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable))
        while (scale > 1 && (sPresentW * scale > usable.w || GBA_H * scale > usable.h))
            scale--;
    return scale;
}

/* Face buttons by their printed label, so A is the button marked A. */
static void MapFaceButtons(struct Pad *p)
{
    static const SDL_GamepadButton faces[] = {
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST,
        SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
    };
    unsigned i;

    p->buttonA = SDL_GAMEPAD_BUTTON_INVALID;
    p->buttonB = SDL_GAMEPAD_BUTTON_INVALID;
    for (i = 0; i < SDL_arraysize(faces); i++)
    {
        SDL_GamepadButtonLabel label = SDL_GetGamepadButtonLabel(p->pad, faces[i]);

        if (label == SDL_GAMEPAD_BUTTON_LABEL_A || label == SDL_GAMEPAD_BUTTON_LABEL_CROSS)
            p->buttonA = faces[i];
        else if (label == SDL_GAMEPAD_BUTTON_LABEL_B || label == SDL_GAMEPAD_BUTTON_LABEL_CIRCLE)
            p->buttonB = faces[i];
    }
    if (p->buttonA == SDL_GAMEPAD_BUTTON_INVALID || p->buttonB == SDL_GAMEPAD_BUTTON_INVALID)
    {
        p->buttonA = SDL_GAMEPAD_BUTTON_SOUTH;
        p->buttonB = SDL_GAMEPAD_BUTTON_EAST;
    }
}

static void OpenPad(SDL_JoystickID id)
{
    unsigned i;

    for (i = 0; i < MAX_PADS; i++)
        if (sPads[i].pad != NULL && sPads[i].id == id)
            return;
    for (i = 0; i < MAX_PADS; i++)
    {
        if (sPads[i].pad != NULL)
            continue;
        sPads[i].pad = SDL_OpenGamepad(id);
        if (sPads[i].pad == NULL)
        {
            Log("[sdl] gamepad %u: cannot open: %s\n", (unsigned)id, SDL_GetError());
            return;
        }
        sPads[i].id = id;
        MapFaceButtons(&sPads[i]);
        Log("[sdl] gamepad %u connected: %s\n", (unsigned)id, SDL_GetGamepadName(sPads[i].pad));
        return;
    }
}

static void ClosePad(SDL_JoystickID id)
{
    unsigned i;

    for (i = 0; i < MAX_PADS; i++)
    {
        if (sPads[i].pad != NULL && sPads[i].id == id)
        {
            SDL_CloseGamepad(sPads[i].pad);
            sPads[i].pad = NULL;
            Log("[sdl] gamepad %u disconnected\n", (unsigned)id);
        }
    }
}

int Platform_Init(void)
{
    int scale, version;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;

    SDL_SetAppMetadata("FE8 PC port", NULL, "fe8pc");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        Log("[sdl] SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    scale = ChooseScale();
    sScale = scale;
#ifdef FE8PC_TESTBUILD
    flags = PortSdlTest_WindowFlags(flags);
#endif
    if (!SDL_CreateWindowAndRenderer(WINDOW_TITLE, sPresentW * scale, GBA_H * scale,
            flags, &sWindow, &sRenderer))
    {
        Log("[sdl] cannot open the window: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    SDL_SetWindowMinimumSize(sWindow, sPresentW, GBA_H);
    if (sTitleNote[0] != '\0')
        ApplyTitle(); /* a note set before the window existed (the save file, port/save/savefile_pc.c) */
    /* Whole-number scaling with black bars, whatever size the window takes. */
    if (!SDL_SetRenderLogicalPresentation(sRenderer, sPresentW, GBA_H, sPresentation))
        Log("[sdl] SDL_SetRenderLogicalPresentation failed: %s\n", SDL_GetError());
    SDL_SetRenderVSync(sRenderer, 0);
    sTexture = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
        sPresentW, GBA_H);
    if (sTexture == NULL)
    {
        Log("[sdl] SDL_CreateTexture failed: %s\n", SDL_GetError());
        Platform_Shutdown();
        return -1;
    }
    SDL_SetTextureScaleMode(sTexture, sTexScale);
    SDL_SetTextureBlendMode(sTexture, SDL_BLENDMODE_NONE);

    version = SDL_GetVersion();
    Log("[sdl] SDL %d.%d.%d, video driver %s, renderer %s, window %dx%d (scale %d)\n",
        SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version),
        SDL_GetCurrentVideoDriver(), SDL_GetRendererName(sRenderer), sPresentW * scale, GBA_H * scale,
        scale);
#ifdef FE8PC_TESTBUILD
    PortSdlTest_AfterInit(sWindow, sRenderer);
#endif
    return 0;
}

void Platform_Shutdown(void)
{
    unsigned i;

    if (sWindow == NULL)
        return;
#ifdef FE8PC_TESTBUILD
    PortSdlTest_Shutdown();
#endif
    if (sAudio != NULL)
    {
        SDL_DestroyAudioStream(sAudio); /* also closes its device */
        sAudio = NULL;
    }
    for (i = 0; i < MAX_PADS; i++)
        if (sPads[i].pad != NULL)
            SDL_CloseGamepad(sPads[i].pad);
    memset(sPads, 0, sizeof(sPads));
    if (sTexture != NULL)
        SDL_DestroyTexture(sTexture);
    if (sRenderer != NULL)
        SDL_DestroyRenderer(sRenderer);
    SDL_DestroyWindow(sWindow);
    sTexture = NULL;
    sRenderer = NULL;
    sWindow = NULL;
    SDL_Quit();
}

int Platform_IsInteractive(void)
{
    return 1;
}

void Platform_PresentFrame(const uint32_t *rgba, int width, int height)
{
    static int sWarned;

    if (sRenderer == NULL)
        return;
#ifdef PLATFORM_FAST_FORWARD
    if (!Reforged_FastForwardPresent())
        return; /* fast-forward: only every N-th frame is shown */
#endif
    if (width != sPresentW || height != GBA_H)
    {
        if (!sWarned++)
            Log("[sdl] frame of %dx%d ignored (expected %dx%d)\n", width, height, sPresentW, GBA_H);
        return;
    }
    if (!SDL_UpdateTexture(sTexture, NULL, rgba, sPresentW * (int)sizeof(uint32_t)))
    {
        if (!sWarned++)
            Log("[sdl] SDL_UpdateTexture failed: %s\n", SDL_GetError());
        return;
    }
    SDL_SetRenderDrawColor(sRenderer, 0, 0, 0, 255);
    SDL_RenderClear(sRenderer);
    SDL_RenderTexture(sRenderer, sTexture, NULL, NULL);
#ifdef FE8PC_TESTBUILD
    PortSdlTest_BeforePresent(sRenderer, rgba, width, height);
#endif
    SDL_RenderPresent(sRenderer);
#ifdef FE8PC_TESTBUILD
    PortSdlTest_AfterPresent();
#endif
}

/* A gamepad binding (sPadNames' code) held on pad p. */
static int PadDown(const struct Pad *p, int code)
{
    switch (code)
    {
    case PADB_NONE:
        return 0;
    case PADB_A:
        return SDL_GetGamepadButton(p->pad, p->buttonA);
    case PADB_B:
        return SDL_GetGamepadButton(p->pad, p->buttonB);
    case PADB_ZL:
        return SDL_GetGamepadAxis(p->pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > TRIGGER_THRESHOLD;
    case PADB_ZR:
        return SDL_GetGamepadAxis(p->pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > TRIGGER_THRESHOLD;
    default:
        return SDL_GetGamepadButton(p->pad, (SDL_GamepadButton)code);
    }
}

#ifdef PLATFORM_SAVE_STATES
static int sPadWait; /* save states: after a load, the gamepads count again once nothing on them is held */
#endif

/* An action's keys or buttons (on any pad) held now. */
static int ActionHeld(const bool *state, int action)
{
    unsigned i;
    int k;

    for (k = 0; k < 2; k++)
        if (sBound.key[action][k] != SDL_SCANCODE_UNKNOWN && state[sBound.key[action][k]])
            return 1;
#ifdef PLATFORM_SAVE_STATES
    if (sPadWait)
        return 0;
#endif
    for (i = 0; i < MAX_PADS; i++)
        if (sPads[i].pad != NULL)
            for (k = 0; k < 2; k++)
                if (PadDown(&sPads[i], sBound.pad[action][k]))
                    return 1;
    return 0;
}

/* Rebinding (the Controls screen): while a capture is on, the fullscreen
   binding does not toggle, so it can be bound like any key. */
static bool sCapPrevKeys[SDL_SCANCODE_COUNT];
static uint32_t sCapPrevPad[MAX_PADS];
static int sCapturing;
static int sFullscreenPadHeld;
static unsigned sPolls;         /* Platform_PollEvents calls */
static unsigned sCapturePolled; /* sPolls at the last capture call */
static unsigned sFullscreenGeneration;

/* A capture counts while the screen keeps asking: one not asked for 2 frames is
   over (its screen was closed, a soft reset). */
static int CaptureOn(void)
{
    return sCapturing && sPolls - sCapturePolled <= 2;
}

static uint32_t PadMask(const struct Pad *p)
{
    uint32_t mask = 0;
    unsigned j;

    for (j = 0; j < SDL_arraysize(sPadNames); j++)
        if (PadDown(p, sPadNames[j].code))
            mask |= 1u << j;
    return mask;
}

static void CaptureSnapshot(void)
{
    int n = 0, i;
    const bool *state = SDL_GetKeyboardState(&n);

    SDL_memset(sCapPrevKeys, 0, sizeof(sCapPrevKeys));
    for (i = 0; i < n && i < SDL_SCANCODE_COUNT; i++)
        sCapPrevKeys[i] = state[i];
    for (i = 0; i < MAX_PADS; i++)
        sCapPrevPad[i] = sPads[i].pad != NULL ? PadMask(&sPads[i]) : 0;
}

void Platform_CaptureBegin(void)
{
    CaptureSnapshot();
    sCapturing = 1;
    sCapturePolled = sPolls;
}

void Platform_CaptureEnd(void)
{
    sCapturing = 0;
}

int Platform_CaptureInput(int *pad, char *name, size_t size)
{
    int n = 0, i, found = 0;
    const bool *state = SDL_GetKeyboardState(&n);

    sCapturePolled = sPolls;
    for (i = 1; i < n && i < SDL_SCANCODE_COUNT && !found; i++)
        if (state[i] && !sCapPrevKeys[i])
        {
            const char *keyName = SDL_GetScancodeName((SDL_Scancode)i);

            /* (a key whose name is another's, or has no usable name, is not bound) */
            if (KeyNameUsable(keyName) && SDL_GetScancodeFromName(keyName) == (SDL_Scancode)i)
            {
                *pad = 0;
                SDL_snprintf(name, size, "%s", keyName);
                found = 1;
            }
        }
    for (i = 0; i < MAX_PADS && !found; i++)
    {
        uint32_t now = sPads[i].pad != NULL ? PadMask(&sPads[i]) : 0, fresh = now & ~sCapPrevPad[i];
        unsigned j;

        for (j = 0; j < SDL_arraysize(sPadNames) && !found; j++)
            if (fresh & (1u << j))
            {
                *pad = 1;
                SDL_snprintf(name, size, "%s", sPadNames[j].name);
                found = 1;
            }
    }
    CaptureSnapshot();
    if (found)
        sCapturing = 0;
    return found;
}

static int sPendingScale; /* a window size asked for while fullscreen: set on leaving it */

static int IsFullscreen(void)
{
    return sWindow != NULL && (SDL_GetWindowFlags(sWindow) & SDL_WINDOW_FULLSCREEN) != 0;
}

static void ResizeWindow(int scale)
{
    SDL_Rect usable;

    if (scale > MAX_SCALE)
        scale = MAX_SCALE;
    if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(sWindow), &usable))
        while (scale > 1 && (sPresentW * scale > usable.w || GBA_H * scale > usable.h))
            scale--;
    if (SDL_GetWindowFlags(sWindow) & SDL_WINDOW_MAXIMIZED)
        SDL_RestoreWindow(sWindow);
    sScale = scale;
    SDL_SetWindowSize(sWindow, sPresentW * scale, GBA_H * scale);
    SDL_SetWindowPosition(sWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    Log("[sdl] window %dx%d (scale %d)\n", sPresentW * scale, GBA_H * scale, scale);
}

static void ToggleFullscreen(void)
{
    /* (the window's own state: SDL can refuse, or the system change it) */
    sFullscreen = !IsFullscreen();
    if (!SDL_SetWindowFullscreen(sWindow, sFullscreen))
    {
        Log("[sdl] fullscreen %s failed: %s\n", sFullscreen ? "on" : "off", SDL_GetError());
        sFullscreen = IsFullscreen();
        return;
    }
    if (!sFullscreen && sPendingScale > 0)
    {
        SDL_SyncWindow(sWindow);
        ResizeWindow(sPendingScale);
        sPendingScale = 0;
    }
}

/* Display (Reforged Options' Display screen, port/reforged/reforged_display_menu.c). */
void Platform_SetWindowScale(int scale)
{
    if (sWindow == NULL)
        return;
    if (EnvScale() > 0)
    {
        Log("[sdl] window size %dx ignored: FE8PC_SCALE is set\n", scale);
        return;
    }
    if (scale <= 0)
        scale = ChooseScale(); /* Auto */
    if (IsFullscreen())
    {
        sPendingScale = scale; /* the size the window gets when it leaves fullscreen */
        return;
    }
    ResizeWindow(scale);
}

void Platform_SetScaling(int mode, int smooth)
{
    SDL_RendererLogicalPresentation p = mode == 2 ? SDL_LOGICAL_PRESENTATION_STRETCH
                                      : mode == 1 ? SDL_LOGICAL_PRESENTATION_LETTERBOX
                                                  : SDL_LOGICAL_PRESENTATION_INTEGER_SCALE;

    if (sRenderer == NULL || sTexture == NULL)
        return;
    sPresentation = p;
    sTexScale = smooth && mode != 0 ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST;
    if (!SDL_SetRenderLogicalPresentation(sRenderer, sPresentW, GBA_H, p))
        Log("[sdl] SDL_SetRenderLogicalPresentation failed: %s\n", SDL_GetError());
    SDL_SetTextureScaleMode(sTexture, sTexScale);
}

/* Widescreen (the Display screen; port/platform.h). A new streaming texture of the new width, the logical
   size and minimum size to match, and the window at its scale in the new shape. Before Platform_Init only
   the width is kept (Platform_Init uses it). */
void Platform_SetWide(int width)
{
    SDL_Texture *tex;

    if (width < GBA_W || width > PLATFORM_MAX_WIDTH)
        width = GBA_W;
    if (width == sPresentW)
        return;
    if (sRenderer == NULL || sWindow == NULL)
    {
        sPresentW = width;
        return;
    }
    tex = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, width, GBA_H);
    if (tex == NULL)
    {
        Log("[sdl] SDL_CreateTexture (%dx%d) failed: %s; the picture stays %d wide\n", width, GBA_H,
            SDL_GetError(), sPresentW);
        return;
    }
    SDL_SetTextureScaleMode(tex, sTexScale);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
    if (sTexture != NULL)
        SDL_DestroyTexture(sTexture);
    sTexture = tex;
    sPresentW = width;
    if (!SDL_SetRenderLogicalPresentation(sRenderer, sPresentW, GBA_H, sPresentation))
        Log("[sdl] SDL_SetRenderLogicalPresentation failed: %s\n", SDL_GetError());
    SDL_SetWindowMinimumSize(sWindow, sPresentW, GBA_H);
    Log("[sdl] picture %dx%d\n", sPresentW, GBA_H);
    if (IsFullscreen())
    {
        if (sPendingScale <= 0)
            sPendingScale = sScale; /* the new shape when it leaves fullscreen */
        return;
    }
    ResizeWindow(EnvScale() > 0 ? EnvScale() : sScale);
}

void Platform_SetFullscreen(int on)
{
    if (sWindow == NULL || (on != 0) == IsFullscreen())
        return;
    ToggleFullscreen();
}

#ifdef PLATFORM_MOUSE
/* Mouse controls (above): the focus rule, the wheel's fractions, the test switch's events. */
static unsigned sFocusPoll;
static int sFocusSeen;
static float sWheelX, sWheelY;
#define TEST_MOUSE_MAX 4096
enum { TM_MOVE, TM_DOWN, TM_UP, TM_CLICK, TM_WHEEL, TM_FOCUS, TM_SIZE, TM_FULLSCREEN };
static struct TestMouse
{
    unsigned poll;
    int kind, button;
    float x, y;
} *sTestMouse;
static int sTestMouseCount = -1, sTestMouseNext, sTestMousePicture;
static float sTestMouseX, sTestMouseY; /* the pointer's window position, for the wheel's events */

static void MouseEvent(const SDL_Event *in)
{
    SDL_Event ev = *in;
    float fx = 0, fy = 0;
    int kind, button = 0, steps = 0, i;

    if (in->type == SDL_EVENT_WINDOW_FOCUS_GAINED)
    {
        sFocusPoll = sPolls;
        sFocusSeen = 1;
        return;
    }
    if (!Reforged_MouseOn() || sRenderer == NULL)
        return;
    switch (in->type)
    {
    case SDL_EVENT_MOUSE_MOTION:
        kind = REFORGED_MOUSE_MOTION;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        button = in->button.button == SDL_BUTTON_LEFT ? 1 : in->button.button == SDL_BUTTON_MIDDLE ? 2
               : in->button.button == SDL_BUTTON_RIGHT ? 3 : 0;
        if (button == 0)
            return; /* X1, X2: nothing */
        if (sFocusSeen && sPolls - sFocusPoll <= 1)
        {
            Log("[sdl] mouse: the click that focused the window ignored\n");
            return;
        }
        kind = REFORGED_MOUSE_DOWN;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
    {
        float x = in->wheel.x, y = in->wheel.y;

        if (in->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
            x = -x, y = -y;
        sWheelX += x;
        sWheelY += y;
        kind = REFORGED_MOUSE_WHEEL;
        break;
    }
    default:
        return;
    }
    if (!SDL_ConvertEventToRenderCoordinates(sRenderer, &ev))
        return;
    if (kind == REFORGED_MOUSE_MOTION)
        fx = ev.motion.x, fy = ev.motion.y;
    else if (kind == REFORGED_MOUSE_DOWN)
        fx = ev.button.x, fy = ev.button.y;
    else
        fx = ev.wheel.mouse_x, fy = ev.wheel.mouse_y;
    if (kind != REFORGED_MOUSE_WHEEL)
    {
        Reforged_MouseEvent(kind, button, (int)SDL_floorf(fx), (int)SDL_floorf(fy), 0, sPresentW);
        return;
    }
    for (i = 0; i < 2; i++)
    {
        float *acc = i == 0 ? &sWheelY : &sWheelX;

        steps = (int)*acc; /* whole notches (toward 0) */
        *acc -= (float)steps;
        if (steps != 0)
            Reforged_MouseEvent(i == 0 ? REFORGED_MOUSE_WHEEL : REFORGED_MOUSE_WHEEL_X, 0, (int)SDL_floorf(fx),
                (int)SDL_floorf(fy), steps, sPresentW);
    }
}

/* FE8PC_TEST_MOUSE: read once (above). */
static void ReadTestMouse(void)
{
    const char *path = getenv("FE8PC_TEST_MOUSE"), *pic = getenv("FE8PC_TEST_MOUSE_PICTURE");
    FILE *f;
    char line[256];

    sTestMouseCount = 0;
    sTestMousePicture = pic != NULL && atoi(pic) != 0;
    if (path == NULL || (f = fopen(path, "r")) == NULL)
    {
        if (path != NULL)
            Log("[sdl] FE8PC_TEST_MOUSE '%s' not read\n", path);
        return;
    }
    sTestMouse = SDL_calloc(TEST_MOUSE_MAX, sizeof(*sTestMouse));
    while (sTestMouse != NULL && fgets(line, sizeof(line), f) != NULL && sTestMouseCount < TEST_MOUSE_MAX)
    {
        struct TestMouse *t = &sTestMouse[sTestMouseCount];
        char word[16] = "", b[16] = "", *hash = strchr(line, '#');
        unsigned poll;
        float x = 0, y = 0;
        int n;

        if (hash != NULL)
            *hash = '\0';
        n = sscanf(line, "%u %15s", &poll, word);
        if (n < 2)
            continue;
        SDL_memset(t, 0, sizeof(*t));
        t->poll = poll;
        if (strcmp(word, "move") == 0 && sscanf(line, "%*u %*s %f %f", &x, &y) == 2)
            t->kind = TM_MOVE;
        else if ((strcmp(word, "down") == 0 || strcmp(word, "up") == 0 || strcmp(word, "click") == 0) &&
                 sscanf(line, "%*u %*s %15s %f %f", b, &x, &y) == 3)
        {
            t->kind = word[0] == 'd' ? TM_DOWN : word[0] == 'u' ? TM_UP : TM_CLICK;
            t->button = strcmp(b, "left") == 0 ? SDL_BUTTON_LEFT : strcmp(b, "middle") == 0 ? SDL_BUTTON_MIDDLE
                      : strcmp(b, "right") == 0 ? SDL_BUTTON_RIGHT : strcmp(b, "x1") == 0 ? SDL_BUTTON_X1
                      : strcmp(b, "x2") == 0 ? SDL_BUTTON_X2 : 0;
            if (t->button == 0)
                continue;
        }
        else if (strcmp(word, "wheel") == 0 && sscanf(line, "%*u %*s %f %f", &y, &x) >= 1)
            t->kind = TM_WHEEL; /* (DY, then DX) */
        else if (strcmp(word, "focus") == 0)
            t->kind = TM_FOCUS;
        else if (strcmp(word, "size") == 0 && sscanf(line, "%*u %*s %f %f", &x, &y) == 2)
            t->kind = TM_SIZE;
        else if (strcmp(word, "fullscreen") == 0 && sscanf(line, "%*u %*s %f", &x) == 1)
            t->kind = TM_FULLSCREEN;
        else
        {
            Log("[sdl] FE8PC_TEST_MOUSE: line not understood: %s", line);
            continue;
        }
        t->x = x, t->y = y;
        sTestMouseCount++;
    }
    fclose(f);
    Log("[sdl] FE8PC_TEST_MOUSE: %d events from %s%s\n", sTestMouseCount, path,
        sTestMousePicture ? " (picture pixels)" : "");
}

/* At the start of each poll: this poll's test events into SDL's queue. */
static void TestMouseFrame(void)
{
    if (sTestMouseCount < 0)
        ReadTestMouse();
    while (sTestMouseNext < sTestMouseCount && sTestMouse[sTestMouseNext].poll <= sPolls)
    {
        const struct TestMouse *t = &sTestMouse[sTestMouseNext++];
        SDL_Event ev;
        float wx = t->x, wy = t->y;
        SDL_WindowID id = sWindow != NULL ? SDL_GetWindowID(sWindow) : 0;
        int k;

        if (t->poll < sPolls)
            continue; /* (a poll already gone) */
        if (t->kind == TM_SIZE)
        {
            if (sWindow != NULL)
            {
                SDL_SetWindowSize(sWindow, (int)t->x, (int)t->y);
                SDL_SyncWindow(sWindow);
                SDL_GetWindowSize(sWindow, &k, NULL);
                Log("[sdl] FE8PC_TEST_MOUSE: window size %dx%d asked (now %d wide)\n", (int)t->x, (int)t->y, k);
            }
            continue;
        }
        if (t->kind == TM_FULLSCREEN)
        {
            Platform_SetFullscreen(t->x != 0);
            if (sWindow != NULL)
            {
                int h = 0;

                SDL_SyncWindow(sWindow);
                SDL_GetWindowSize(sWindow, &k, &h);
                Log("[sdl] FE8PC_TEST_MOUSE: fullscreen %s, window %dx%d\n", IsFullscreen() ? "on" : "off", k, h);
            }
            continue;
        }
        if (sTestMousePicture && sRenderer != NULL && t->kind != TM_WHEEL && t->kind != TM_FOCUS)
            SDL_RenderCoordinatesToWindow(sRenderer, t->x + 0.5f, t->y + 0.5f, &wx, &wy);
        SDL_zero(ev);
        switch (t->kind)
        {
        case TM_MOVE:
            ev.type = SDL_EVENT_MOUSE_MOTION;
            ev.motion.windowID = id;
            ev.motion.which = 1;
            ev.motion.x = wx, ev.motion.y = wy;
            sTestMouseX = wx, sTestMouseY = wy;
            SDL_PushEvent(&ev);
            break;
        case TM_DOWN:
        case TM_UP:
        case TM_CLICK:
            for (k = 0; k < 2; k++)
            {
                if ((k == 0 && t->kind == TM_UP) || (k == 1 && t->kind == TM_DOWN))
                    continue;
                SDL_zero(ev);
                ev.type = k == 0 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
                ev.button.windowID = id;
                ev.button.which = 1;
                ev.button.button = (Uint8)t->button;
                ev.button.down = k == 0;
                ev.button.clicks = 1;
                ev.button.x = wx, ev.button.y = wy;
                SDL_PushEvent(&ev);
            }
            sTestMouseX = wx, sTestMouseY = wy;
            break;
        case TM_WHEEL:
            ev.type = SDL_EVENT_MOUSE_WHEEL;
            ev.wheel.windowID = id;
            ev.wheel.which = 1;
            ev.wheel.x = t->x, ev.wheel.y = t->y;
            ev.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
            ev.wheel.mouse_x = sTestMouseX, ev.wheel.mouse_y = sTestMouseY;
            SDL_PushEvent(&ev);
            break;
        case TM_FOCUS:
            ev.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
            ev.window.windowID = id;
            SDL_PushEvent(&ev);
            break;
        default:
            break;
        }
    }
}
#endif

int Platform_PollEvents(void)
{
    SDL_Event ev;
    unsigned i;
    int held = 0, k;

#ifdef FE8PC_TESTBUILD
    PortSdlTest_Frame();
#endif
#ifdef PLATFORM_MOUSE
    TestMouseFrame();
#endif
    ResolveBindings();
    while (SDL_PollEvent(&ev))
    {
        switch (ev.type)
        {
#ifdef PLATFORM_MOUSE
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            MouseEvent(&ev); /* mouse controls (above); nothing with the row off */
            break;
#endif
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            sQuit = 1;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (!ev.key.repeat && !CaptureOn() && ev.key.scancode != SDL_SCANCODE_UNKNOWN &&
                (ev.key.scancode == sBound.key[CTL_FULLSCREEN][0] || ev.key.scancode == sBound.key[CTL_FULLSCREEN][1]))
                ToggleFullscreen();
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            OpenPad(ev.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            ClosePad(ev.gdevice.which);
            break;
        default:
            break;
        }
    }
    /* a gamepad's fullscreen binding: on the press, not while held; a button
       already down when the bindings change (just bound to it) is not a press */
    sPolls++;
    for (i = 0; i < MAX_PADS; i++)
        if (sPads[i].pad != NULL)
            for (k = 0; k < 2; k++)
                held |= PadDown(&sPads[i], sBound.pad[CTL_FULLSCREEN][k]);
    if (held && !sFullscreenPadHeld && !CaptureOn() && sFullscreenGeneration == sBound.generation)
        ToggleFullscreen();
    sFullscreenPadHeld = held;
    sFullscreenGeneration = sBound.generation;
    return sQuit;
}

#ifdef PLATFORM_SAVE_STATES
/* Save states: the keyboard as the game sees it this frame step (the real one, the test switch's keys,
   minus what is held back), and the keys held back until they are let go. */
static bool sStateKeys[SDL_SCANCODE_COUNT];
static bool sKeyHeldBack[SDL_SCANCODE_COUNT];
static int sComboWas[2]; /* Ctrl+S, Ctrl+L held at the last frame step */
static int sHintWas[2];  /* the same, for the row-off hint (v1.24.1) */
#define HOLD_MAX 16
static struct HoldKey
{
    uint32_t from, to;
    SDL_Scancode sc[4];
    int n, done;
} sHold[HOLD_MAX];
static int sHoldCount = -1;

static SDL_Scancode HoldKeyCode(const char *name)
{
    if (SDL_strcasecmp(name, "LCtrl") == 0)
        return SDL_SCANCODE_LCTRL;
    if (SDL_strcasecmp(name, "RCtrl") == 0)
        return SDL_SCANCODE_RCTRL;
    return SDL_GetScancodeFromName(name);
}

/* FE8PC_TEST_HOLD_KEYS=FRAME:KEY[+KEY...]:FRAMES[,...] (a test switch): read once. */
static void ReadHoldKeys(void)
{
    const char *s = getenv("FE8PC_TEST_HOLD_KEYS");

    sHoldCount = 0;
    while (s != NULL && *s != '\0' && sHoldCount < HOLD_MAX)
    {
        struct HoldKey *h = &sHold[sHoldCount];
        char *end;
        unsigned long from = SDL_strtoul(s, &end, 10), len;

        if (end == s || *end != ':')
            break;
        s = end + 1;
        h->n = 0;
        while (*s != '\0' && *s != ':')
        {
            char name[32];
            size_t k = 0;

            while (*s != '\0' && *s != ':' && *s != '+' && k + 1 < sizeof(name))
                name[k++] = *s++;
            name[k] = '\0';
            if (*s == '+')
                s++;
            if (h->n < 4 && HoldKeyCode(name) != SDL_SCANCODE_UNKNOWN)
                h->sc[h->n++] = HoldKeyCode(name);
            else
                Log("[sdl] FE8PC_TEST_HOLD_KEYS: key '%s' unknown (or more than 4)\n", name);
        }
        if (*s != ':')
            break;
        len = SDL_strtoul(s + 1, &end, 10);
        s = *end == ',' ? end + 1 : end;
        h->from = (uint32_t)from;
        h->to = (uint32_t)(from + (len ? len : 1));
        h->done = 0;
        if (h->n > 0)
            sHoldCount++;
    }
    if (sHoldCount > 0)
        Log("[sdl] FE8PC_TEST_HOLD_KEYS: %d key windows\n", sHoldCount);
}

/* Any gamepad input a binding or the left stick reads. */
static int AnyPadInput(void)
{
    unsigned i;
    int a, k;

    for (i = 0; i < MAX_PADS; i++)
    {
        if (sPads[i].pad == NULL)
            continue;
        for (a = 0; a < CTL_COUNT; a++)
            for (k = 0; k < 2; k++)
                if (PadDown(&sPads[i], sBound.pad[a][k]))
                    return 1;
        if (SDL_abs(SDL_GetGamepadAxis(sPads[i].pad, SDL_GAMEPAD_AXIS_LEFTX)) > STICK_THRESHOLD ||
            SDL_abs(SDL_GetGamepadAxis(sPads[i].pad, SDL_GAMEPAD_AXIS_LEFTY)) > STICK_THRESHOLD)
            return 1;
    }
    return 0;
}

/* Once per frame step, before the bindings read the keyboard: Ctrl+S / Ctrl+L (the row on), and the
   keyboard the bindings then see. */
static const bool *StateKeys(const bool *real)
{
    static const SDL_Scancode comboKey[2] = { SDL_SCANCODE_S, SDL_SCANCODE_L };
    int i, ctrl;

    if (sHoldCount < 0)
        ReadHoldKeys();
    SDL_memcpy(sStateKeys, real, sizeof(sStateKeys));
    for (i = 0; i < sHoldCount; i++)
    {
        struct HoldKey *h = &sHold[i];
        int k;

        if (h->done)
            continue;
        if (sPolls >= h->to)
        {
            h->done = 1;
            continue;
        }
        if (sPolls >= h->from)
            for (k = 0; k < h->n; k++)
                sStateKeys[h->sc[k]] = true;
    }
    if (sPadWait && !AnyPadInput())
        sPadWait = 0;
    for (i = 0; i < SDL_SCANCODE_COUNT; i++)
        if (sKeyHeldBack[i] && !sStateKeys[i])
            sKeyHeldBack[i] = false;
    ctrl = sStateKeys[SDL_SCANCODE_LCTRL] || sStateKeys[SDL_SCANCODE_RCTRL];
    if (PortState_KeysOn())
    {
        for (i = 0; i < 2; i++)
        {
            int combo = ctrl && sStateKeys[comboKey[i]];

            if (combo && !sComboWas[i])
                PortState_Request(i + 1); /* 1 save, 2 load */
            if (combo)
                sKeyHeldBack[comboKey[i]] = true;
            sComboWas[i] = combo;
        }
        if (ctrl)
            sStateKeys[SDL_SCANCODE_S] = sStateKeys[SDL_SCANCODE_L] = false;
    }
    else
        sComboWas[0] = sComboWas[1] = 0;
    /* v1.24.1: the row off, a press of Ctrl+S or Ctrl+L shows a hint (display only; the keys are left alone) */
    for (i = 0; i < 2; i++)
    {
        int combo = ctrl && sStateKeys[comboKey[i]];

        if (combo && !sHintWas[i] && !PortState_KeysOn())
            PortState_OffHint(i + 1);
        sHintWas[i] = combo;
    }
    for (i = 0; i < SDL_SCANCODE_COUNT; i++)
        if (sKeyHeldBack[i])
            sStateKeys[i] = false;
    return sStateKeys;
}

/* After a load: what is held now reaches the game only once let go and pressed again. */
void Platform_ReleaseKeys(void)
{
    int i;

    for (i = 0; i < SDL_SCANCODE_COUNT; i++)
        if (sStateKeys[i] || SDL_GetKeyboardState(NULL)[i])
            sKeyHeldBack[i] = true;
    sPadWait = 1;
#ifdef PLATFORM_MOUSE
    Reforged_MouseReset(); /* mouse controls: what the mouse was doing is dropped */
#endif
}
#endif

uint16_t Platform_ReadKeys(void)
{
    const bool *state = SDL_GetKeyboardState(NULL);
    uint16_t keys = 0;
    unsigned i;
    int a;

    ResolveBindings();
#ifdef PLATFORM_SAVE_STATES
    state = StateKeys(state);
#endif
    for (a = 0; a < CTL_GBA_KEYS; a++)
        if (ActionHeld(state, a))
            keys |= (uint16_t)(PLATFORM_KEY_A << a);
#ifdef FE8PC_TESTBUILD
    for (i = 0; i < SDL_arraysize(sKeyMap); i++)
        if (state[sKeyMap[i].scancode])
            keys |= sKeyMap[i].key;
#endif

    for (i = 0; i < MAX_PADS; i++)
    {
        SDL_Gamepad *pad = sPads[i].pad;
        int x, y;

        if (pad == NULL)
            continue;
#ifdef PLATFORM_SAVE_STATES
        if (sPadWait)
            continue;
#endif
        x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
        y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
        if (x > STICK_THRESHOLD)
            keys |= PLATFORM_KEY_RIGHT;
        else if (x < -STICK_THRESHOLD)
            keys |= PLATFORM_KEY_LEFT;
        if (y > STICK_THRESHOLD)
            keys |= PLATFORM_KEY_DOWN;
        else if (y < -STICK_THRESHOLD)
            keys |= PLATFORM_KEY_UP;
    }
#ifdef FE8PC_TESTBUILD
    keys = PortSdlTest_Keys(keys);
#endif
#ifdef PLATFORM_MOUSE
    /* Mouse controls (above): the keys the mouse presses this frame step; any key or button held cancels
       what it was doing (0 with the row off) */
    keys |= Reforged_MouseKeys(keys);
#endif
#ifdef PLATFORM_FAST_FORWARD
    {
        /* Once per frame step (frame.c UpdateLiveKeys, after the pacer). */
        /* (the right stick's click by default: the left stick moves the cursor,
           so pressing it in could also tilt it) */
        int ffHeld = ActionHeld(state, CTL_FAST_FORWARD);

        Reforged_RewindKey(ActionHeld(state, CTL_REWIND)); /* "Rewind": the key held */
        int bytes = sAudio != NULL ? SDL_GetAudioStreamQueued(sAudio) : 0;

        unsigned silence;

        if (Reforged_FastForwardFrame(ffHeld, bytes > 0 ? (unsigned)bytes / (2 * sizeof(int16_t)) : 0,
                SDL_GetTicksNS()) && sAudio != NULL)
            SDL_SetAudioStreamFrequencyRatio(sAudio, (float)Reforged_FastForwardAudioRatio(0));
        if (Reforged_FastForwardTakeFlush(&silence) && sAudio != NULL)
        {
            /* a muted fast-forward released: its queued (muted) samples dropped, the queue
               refilled with silence to its normal level */
            static const int16_t zeros[2 * 256];

            SDL_ClearAudioStream(sAudio);
            while (silence > 0)
            {
                unsigned n = silence < 256 ? silence : 256;

                SDL_PutAudioStreamData(sAudio, zeros, (int)(n * 2 * sizeof(int16_t)));
                silence -= n;
            }
        }
    }
#endif

    /* A D-pad cannot hold both opposite directions (mGBA's default). */
    if ((keys & (PLATFORM_KEY_RIGHT | PLATFORM_KEY_LEFT)) == (PLATFORM_KEY_RIGHT | PLATFORM_KEY_LEFT))
        keys &= (uint16_t)~(PLATFORM_KEY_RIGHT | PLATFORM_KEY_LEFT);
    if ((keys & (PLATFORM_KEY_UP | PLATFORM_KEY_DOWN)) == (PLATFORM_KEY_UP | PLATFORM_KEY_DOWN))
        keys &= (uint16_t)~(PLATFORM_KEY_UP | PLATFORM_KEY_DOWN);
    return keys;
}

/*
 * Audio (port-plan/audio-design.md 1.5 and 3). One SDL audio stream on the
 * default playback device, fed s16 stereo at the GBA's own rate, 13,379 Hz.
 * SDL converts to the device's rate on its own thread; nothing here filters,
 * mixes or changes a sample. The device opens paused; port/audio/audio_out.c
 * prefills the queue and resumes it, and sets the frequency ratio (D4).
 */
#ifdef PLATFORM_FAST_FORWARD
static void SDLCALL StreamShortCheck(void *userdata, SDL_AudioStream *stream, int additional, int total)
{
    (void)userdata;
    (void)stream;
    Reforged_FastForwardStreamShort(additional, total); /* the audio thread: counters only */
}
#endif

int Platform_OpenAudio(uint32_t rateHz, uint32_t channels)
{
    SDL_AudioSpec spec, devSpec;
    int devFrames = 0;
    const char *name;

    if (sAudio != NULL)
        return 1;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        Log("[sdl] no audio: SDL_InitSubSystem(AUDIO) failed: %s\n", SDL_GetError());
        return 0;
    }
    spec.format = SDL_AUDIO_S16;
    spec.channels = (int)channels;
    spec.freq = (int)rateHz;
    sAudio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (sAudio == NULL)
    {
        Log("[sdl] no audio: cannot open the playback device: %s\n", SDL_GetError());
        return 0;
    }
    name = SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(sAudio));
    if (!SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(sAudio), &devSpec, &devFrames))
        SDL_zero(devSpec);
    Log("[sdl] audio driver %s, device \"%s\" (%d Hz, %d channels, %d-frame buffer); stream in: %d Hz, "
        "%d channels, s16\n", SDL_GetCurrentAudioDriver(), name != NULL ? name : "?", devSpec.freq,
        devSpec.channels, devFrames, spec.freq, spec.channels);
#ifdef FE8PC_TESTBUILD
    PortSdlTest_AudioOpened(sAudio);
#endif
#ifdef PLATFORM_FAST_FORWARD
    /* FE8PC_FF_CHECK (test hook): count each time the device asks the stream
       for more than it holds (it then plays silence for the rest). */
    if (getenv("FE8PC_FF_CHECK") != NULL)
        SDL_SetAudioStreamGetCallback(sAudio, StreamShortCheck, NULL);
#endif
    return 1;
}

#ifdef PLATFORM_SAVE_STATES
/* Save states: the stream's last stereo frame put in, and the frames still to fade in after a load. A
   load changes the sound at once (the state's own music and effects go on from where they were then),
   so the stream gets a 64-frame ramp from its last frame down to silence (about 5 ms), then the loaded
   state's sound fades in over 256 frames (about 19 ms): no step in the wave, so no click. */
#define AUDIO_JOIN_RAMP 64
#define AUDIO_FADE_IN 256
static int16_t sLastLr[2];
static int sFadeIn;

void Platform_AudioJoin(void)
{
    int16_t ramp[2 * AUDIO_JOIN_RAMP];
    int i;

    if (sAudio == NULL)
        return;
    for (i = 0; i < AUDIO_JOIN_RAMP; i++)
    {
        int g = AUDIO_JOIN_RAMP - 1 - i;

        ramp[2 * i] = (int16_t)(sLastLr[0] * g / AUDIO_JOIN_RAMP);
        ramp[2 * i + 1] = (int16_t)(sLastLr[1] * g / AUDIO_JOIN_RAMP);
    }
    SDL_PutAudioStreamData(sAudio, ramp, (int)sizeof(ramp));
    sLastLr[0] = sLastLr[1] = 0;
    sFadeIn = AUDIO_FADE_IN;
}
#endif

void Platform_QueueAudio(const int16_t *lr, uint32_t frames)
{
#ifdef PLATFORM_FAST_FORWARD
    /* Fast-forward's mute ramp: a scaled copy, in chunks. */
    int16_t buf[2 * 256];

    while (sAudio != NULL && frames > 256)
    {
        Platform_QueueAudio(lr, 256);
        lr += 2 * 256;
        frames -= 256;
    }
    if (sAudio != NULL && Reforged_FastForwardGain(lr, buf, frames))
        lr = buf;
#endif
#if defined(PLATFORM_SAVE_STATES) && defined(PLATFORM_FAST_FORWARD)
    /* save states (Platform_AudioJoin): the first frames after a load fade in */
    if (sAudio != NULL && frames > 0)
    {
        if (sFadeIn > 0)
        {
            uint32_t i;

            if (lr != buf)
            {
                SDL_memcpy(buf, lr, frames * 2 * sizeof(int16_t));
                lr = buf;
            }
            for (i = 0; i < frames && sFadeIn > 0; i++, sFadeIn--)
            {
                int g = AUDIO_FADE_IN - sFadeIn;

                buf[2 * i] = (int16_t)(buf[2 * i] * g / AUDIO_FADE_IN);
                buf[2 * i + 1] = (int16_t)(buf[2 * i + 1] * g / AUDIO_FADE_IN);
            }
        }
        sLastLr[0] = lr[2 * (frames - 1)];
        sLastLr[1] = lr[2 * (frames - 1) + 1];
    }
#endif
    if (sAudio != NULL && !SDL_PutAudioStreamData(sAudio, lr, (int)(frames * 2 * sizeof(int16_t))))
    {
        static int sWarned;

        if (!sWarned++)
            Log("[sdl] SDL_PutAudioStreamData failed: %s\n", SDL_GetError());
    }
}

/* Stereo frames put in and not yet taken by the device (SDL counts the
   stream's input bytes, in the input format). */
uint32_t Platform_AudioQueued(void)
{
    int bytes;

    if (sAudio == NULL)
        return 0;
    bytes = SDL_GetAudioStreamQueued(sAudio);
#ifdef PLATFORM_FAST_FORWARD
    /* fast-forward: N times the frames for the same real time */
    return bytes > 0 ? Reforged_FastForwardQueued((uint32_t)bytes / (2 * sizeof(int16_t))) : 0;
#endif
    return bytes > 0 ? (uint32_t)bytes / (2 * sizeof(int16_t)) : 0;
}

void Platform_SetAudioRatio(double ratio)
{
#ifdef PLATFORM_FAST_FORWARD
    ratio = Reforged_FastForwardAudioRatio(ratio); /* times N while fast-forwarding */
#endif
    if (sAudio != NULL)
        SDL_SetAudioStreamFrequencyRatio(sAudio, (float)ratio);
}

void Platform_PauseAudio(int pause)
{
    if (sAudio == NULL)
        return;
    if (pause)
        SDL_PauseAudioStreamDevice(sAudio);
    else
        SDL_ResumeAudioStreamDevice(sAudio);
}

uint64_t Platform_NowNs(void)
{
#ifdef PLATFORM_FAST_FORWARD
    /* The pacer's clock: real time, but N times as fast while fast-forwarding
       (the same as SDL_GetTicksNS until the first press). */
    return Reforged_FastForwardClock(SDL_GetTicksNS());
#else
    return SDL_GetTicksNS();
#endif
}

void Platform_SleepUntilNs(uint64_t deadlineNs)
{
    uint64_t now = SDL_GetTicksNS();

#ifdef PLATFORM_FAST_FORWARD
    deadlineNs = Reforged_FastForwardRealTime(deadlineNs); /* Platform_NowNs's clock back to real time */
#endif
    /* SDL_DelayPrecise sleeps, then spins for the last stretch. */
    if (deadlineNs > now)
        SDL_DelayPrecise(deadlineNs - now);
}

void *Platform_LoadFile(const char *path, size_t *size)
{
    return SDL_LoadFile(path, size);
}

void Platform_FreeFile(void *data)
{
    SDL_free(data);
}

int Platform_SaveFileAtomic(const char *path, const char *tmp, const void *data, size_t size)
{
    SDL_IOStream *io;
    bool ok;

    io = SDL_IOFromFile(tmp, "wb");
    if (io == NULL)
        return -1;
    ok = SDL_WriteIO(io, data, size) == size;
    /* To the disk before the rename (SDL 3.2+: fsync, or FlushFileBuffers on
       Windows), so a power cut leaves the old file or the new one. */
    ok = ok && SDL_FlushIO(io);
    ok = SDL_CloseIO(io) && ok;
    if (!ok || !SDL_RenamePath(tmp, path))
    {
        SDL_RemovePath(tmp);
        return -1;
    }
    return 0;
}

/*
 * The per-user folder: the one SDL_GetPrefPath(NULL, "fe8pc") names, by SDL
 * 3.4.2's rules, but not made here, since SDL_GetPrefPath always makes it
 * and a run that saves next to the ROM would leave an empty folder behind.
 * Windows: the shell's Roaming application data folder (SHGetFolderPath,
 * CSIDL_APPDATA; what %APPDATA% names) and fe8pc\. Elsewhere:
 * $XDG_DATA_HOME/fe8pc/, or $HOME/.local/share/fe8pc/ when XDG_DATA_HOME is
 * not set. Named once; NULL, logged, when there is none (no Roaming folder,
 * no HOME), never the current folder in its place. Platform_MakeUserDir makes
 * it as SDL_GetPrefPath does (mode 0700 on Linux, each missing folder of the
 * path; CreateDirectory on Windows) when the save file goes there
 * (port/save/savefile_pc.c). FE8PC_USER_DIR, when set, names it instead (a test switch).
 */
static char sUserDir[4096];
static int sAsked;
static const char *sDir;

const char *Platform_UserDir(void)
{
    if (sAsked)
        return sDir;
    sAsked = 1;
    {
        /* FE8PC_USER_DIR (a test switch, as in the headless build): that folder in place of the
           per-user one, so a scripted run (port/test/tools/savechk.sh) has its own settings file
           and never reads or writes the player's. Logged. */
        const char *env = getenv("FE8PC_USER_DIR");
        size_t n = env != NULL ? strlen(env) : 0;

        if (n > 0 && n + 2 <= sizeof(sUserDir))
        {
            SDL_memcpy(sUserDir, env, n);
            if (sUserDir[n - 1] != '/' && sUserDir[n - 1] != '\\')
#ifdef _WIN32
                sUserDir[n++] = '\\';
#else
                sUserDir[n++] = '/';
#endif
            sUserDir[n] = '\0';
            Log("[sdl] per-user folder from FE8PC_USER_DIR: %s\n", sUserDir);
            sDir = sUserDir;
            return sDir;
        }
    }
#ifdef _WIN32
    {
        char roaming[sizeof(sUserDir) - 8];

        if (PortWin32RoamingDir(roaming, sizeof(roaming)) != 0)
        {
            Log("[sdl] no per-user folder: the Roaming application data folder is not known\n");
            return NULL;
        }
        SDL_snprintf(sUserDir, sizeof(sUserDir), "%s\\fe8pc\\", roaming);
    }
#else
    {
        const char *base = getenv("XDG_DATA_HOME"), *append = "/";
        size_t n;

        if (base == NULL || base[0] == '\0')
        {
            base = getenv("HOME");
            append = "/.local/share/";
        }
        if (base == NULL || base[0] == '\0')
        {
            Log("[sdl] no per-user folder: neither XDG_DATA_HOME nor HOME is set\n");
            return NULL;
        }
        n = strlen(base);
        if (base[n - 1] == '/')
            append++;
        if (n + strlen(append) + sizeof("fe8pc/") > sizeof(sUserDir))
        {
            Log("[sdl] no per-user folder: the path is too long\n");
            return NULL;
        }
        SDL_snprintf(sUserDir, sizeof(sUserDir), "%s%sfe8pc/", base, append);
    }
#endif
    sDir = sUserDir;
    return sDir;
}

int Platform_MakeUserDir(void)
{
    const char *dir = Platform_UserDir();

    if (dir == NULL)
        return -1;
#ifdef _WIN32
    if (!SDL_CreateDirectory(dir))
    {
        Log("[sdl] cannot make the per-user folder '%s': %s\n", dir, SDL_GetError());
        return -1;
    }
#else
    {
        char path[sizeof(sUserDir)];
        char *p;

        SDL_strlcpy(path, dir, sizeof(path));
        for (p = path + 1; *p != '\0'; p++)
        {
            if (*p != '/')
                continue;
            *p = '\0';
            if (mkdir(path, 0700) != 0 && errno != EEXIST)
            {
                Log("[sdl] cannot make the per-user folder '%s': making '%s': %s\n", dir, path, strerror(errno));
                return -1;
            }
            *p = '/';
        }
    }
#endif
    return 0;
}

#ifdef PLATFORM_SAVE_STATES
/* Save states (port/include/port_state.h): nothing of this file is game state. A load keeps this
   process's window, renderer, audio stream, gamepads, bindings, title note, per-user folder and input. */
PORT_STATE_SESSION(sWindow)
PORT_STATE_SESSION(sRenderer)
PORT_STATE_SESSION(sTexture)
PORT_STATE_SESSION(sAudio)
PORT_STATE_SESSION(sQuit)
PORT_STATE_SESSION(sFullscreen)
PORT_STATE_SESSION(sPresentW)
PORT_STATE_SESSION(sScale)
PORT_STATE_SESSION(sPresentation)
PORT_STATE_SESSION(sTexScale)
PORT_STATE_SESSION(sPads)
PORT_STATE_SESSION(sBound)
PORT_STATE_SESSION(sTitleNote)
PORT_STATE_SESSION(sPadWait)
PORT_STATE_SESSION(sCapPrevKeys)
PORT_STATE_SESSION(sCapPrevPad)
PORT_STATE_SESSION(sCapturing)
PORT_STATE_SESSION(sFullscreenPadHeld)
PORT_STATE_SESSION(sPolls)
PORT_STATE_SESSION(sCapturePolled)
PORT_STATE_SESSION(sFullscreenGeneration)
PORT_STATE_SESSION(sPendingScale)
PORT_STATE_SESSION(sStateKeys)
PORT_STATE_SESSION(sKeyHeldBack)
PORT_STATE_SESSION(sComboWas)
PORT_STATE_SESSION(sHold)
PORT_STATE_SESSION(sHoldCount)
PORT_STATE_SESSION(sLastLr)
PORT_STATE_SESSION(sFadeIn)
PORT_STATE_SESSION(sUserDir)
PORT_STATE_SESSION(sAsked)
PORT_STATE_SESSION(sDir)
#ifdef PLATFORM_MOUSE
PORT_STATE_SESSION(sFocusPoll)
PORT_STATE_SESSION(sFocusSeen)
PORT_STATE_SESSION(sWheelX)
PORT_STATE_SESSION(sWheelY)
PORT_STATE_SESSION(sTestMouse)
PORT_STATE_SESSION(sTestMouseCount)
PORT_STATE_SESSION(sTestMouseNext)
PORT_STATE_SESSION(sTestMousePicture)
PORT_STATE_SESSION(sTestMouseX)
PORT_STATE_SESSION(sTestMouseY)
#endif
#endif
