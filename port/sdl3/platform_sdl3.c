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
        while (scale > 1 && (GBA_W * scale > usable.w || GBA_H * scale > usable.h))
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
#ifdef FE8PC_TESTBUILD
    flags = PortSdlTest_WindowFlags(flags);
#endif
    if (!SDL_CreateWindowAndRenderer(WINDOW_TITLE, GBA_W * scale, GBA_H * scale,
            flags, &sWindow, &sRenderer))
    {
        Log("[sdl] cannot open the window: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    SDL_SetWindowMinimumSize(sWindow, GBA_W, GBA_H);
    if (sTitleNote[0] != '\0')
        ApplyTitle(); /* a note set before the window existed (the save file, port/save/savefile_pc.c) */
    /* Whole-number scaling with black bars, whatever size the window takes. */
    if (!SDL_SetRenderLogicalPresentation(sRenderer, GBA_W, GBA_H,
            SDL_LOGICAL_PRESENTATION_INTEGER_SCALE))
        Log("[sdl] SDL_SetRenderLogicalPresentation failed: %s\n", SDL_GetError());
    SDL_SetRenderVSync(sRenderer, 0);
    sTexture = SDL_CreateTexture(sRenderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
        GBA_W, GBA_H);
    if (sTexture == NULL)
    {
        Log("[sdl] SDL_CreateTexture failed: %s\n", SDL_GetError());
        Platform_Shutdown();
        return -1;
    }
    SDL_SetTextureScaleMode(sTexture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(sTexture, SDL_BLENDMODE_NONE);

    version = SDL_GetVersion();
    Log("[sdl] SDL %d.%d.%d, video driver %s, renderer %s, window %dx%d (scale %d)\n",
        SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version),
        SDL_GetCurrentVideoDriver(), SDL_GetRendererName(sRenderer), GBA_W * scale, GBA_H * scale,
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
    if (width != GBA_W || height != GBA_H)
    {
        if (!sWarned++)
            Log("[sdl] frame of %dx%d ignored (expected %dx%d)\n", width, height, GBA_W, GBA_H);
        return;
    }
    if (!SDL_UpdateTexture(sTexture, NULL, rgba, GBA_W * (int)sizeof(uint32_t)))
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

/* An action's keys or buttons (on any pad) held now. */
static int ActionHeld(const bool *state, int action)
{
    unsigned i;
    int k;

    for (k = 0; k < 2; k++)
        if (sBound.key[action][k] != SDL_SCANCODE_UNKNOWN && state[sBound.key[action][k]])
            return 1;
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
        while (scale > 1 && (GBA_W * scale > usable.w || GBA_H * scale > usable.h))
            scale--;
    if (SDL_GetWindowFlags(sWindow) & SDL_WINDOW_MAXIMIZED)
        SDL_RestoreWindow(sWindow);
    SDL_SetWindowSize(sWindow, GBA_W * scale, GBA_H * scale);
    SDL_SetWindowPosition(sWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    Log("[sdl] window %dx%d (scale %d)\n", GBA_W * scale, GBA_H * scale, scale);
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
    if (!SDL_SetRenderLogicalPresentation(sRenderer, GBA_W, GBA_H, p))
        Log("[sdl] SDL_SetRenderLogicalPresentation failed: %s\n", SDL_GetError());
    SDL_SetTextureScaleMode(sTexture, smooth && mode != 0 ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
}

void Platform_SetFullscreen(int on)
{
    if (sWindow == NULL || (on != 0) == IsFullscreen())
        return;
    ToggleFullscreen();
}

int Platform_PollEvents(void)
{
    SDL_Event ev;
    unsigned i;
    int held = 0, k;

#ifdef FE8PC_TESTBUILD
    PortSdlTest_Frame();
#endif
    ResolveBindings();
    while (SDL_PollEvent(&ev))
    {
        switch (ev.type)
        {
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

uint16_t Platform_ReadKeys(void)
{
    const bool *state = SDL_GetKeyboardState(NULL);
    uint16_t keys = 0;
    unsigned i;
    int a;

    ResolveBindings();
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
#ifdef PLATFORM_FAST_FORWARD
    {
        /* Once per frame step (frame.c UpdateLiveKeys, after the pacer). */
        /* (the right stick's click by default: the left stick moves the cursor,
           so pressing it in could also tilt it) */
        int ffHeld = ActionHeld(state, CTL_FAST_FORWARD);
        int bytes = sAudio != NULL ? SDL_GetAudioStreamQueued(sAudio) : 0;

        if (Reforged_FastForwardFrame(ffHeld, bytes > 0 ? (unsigned)bytes / (2 * sizeof(int16_t)) : 0,
                SDL_GetTicksNS()) && sAudio != NULL)
            SDL_SetAudioStreamFrequencyRatio(sAudio, (float)Reforged_FastForwardAudioRatio(0));
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

const char *Platform_UserDir(void)
{
    static int sAsked;
    static const char *sDir;

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
