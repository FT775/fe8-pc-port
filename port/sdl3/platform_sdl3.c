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
 * Keys, by scancode so the keyboard layout does not matter (the user's layout):
 *   A: Z   B: X   L: Q   R: E   Start: Enter   Select: Backspace
 *   D-pad: the arrow keys
 *   Test builds only (FE8PC_TESTBUILD=1): either Shift is also Select.
 * Gamepads (any SDL3 gamepad, hot-plugged):
 *   A and B: the face buttons labelled A and B (cross and circle on
 *   PlayStation pads), so a Nintendo pad matches the GBA's own layout;
 *   unlabelled pads use south for A and east for B.
 *   L and R: the shoulder buttons or the triggers. Start: start. Select: back.
 *   D-pad: the D-pad or the left stick (past half way).
 * Opposite directions held together read as neither (mGBA's default), since
 * a real D-pad cannot press both.
 * F11 toggles fullscreen. Closing the window (or SIGINT/SIGTERM) quits.
 * Played builds: Space, or the right stick's click, held = fast-forward
 * (Reforged Options; above).
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

static const struct
{
    SDL_Scancode scancode;
    uint16_t key;
} sKeyMap[] = {
    { SDL_SCANCODE_Z,         PLATFORM_KEY_A },
    { SDL_SCANCODE_X,         PLATFORM_KEY_B },
    { SDL_SCANCODE_BACKSPACE, PLATFORM_KEY_SELECT },
    { SDL_SCANCODE_RETURN,    PLATFORM_KEY_START },
    { SDL_SCANCODE_KP_ENTER,  PLATFORM_KEY_START },
    { SDL_SCANCODE_RIGHT,     PLATFORM_KEY_RIGHT },
    { SDL_SCANCODE_LEFT,      PLATFORM_KEY_LEFT },
    { SDL_SCANCODE_UP,        PLATFORM_KEY_UP },
    { SDL_SCANCODE_DOWN,      PLATFORM_KEY_DOWN },
    { SDL_SCANCODE_E,         PLATFORM_KEY_R },
    { SDL_SCANCODE_Q,         PLATFORM_KEY_L },
#ifdef FE8PC_TESTBUILD
    /* Test builds only (FE8PC_TESTBUILD=1, port/Makefile; port-plan/DIVERGENCES.md TB-1):
       either Shift is a second Select. */
    { SDL_SCANCODE_LSHIFT,    PLATFORM_KEY_SELECT },
    { SDL_SCANCODE_RSHIFT,    PLATFORM_KEY_SELECT },
#endif
};

/* A test build says so in the window title. */
#ifdef FE8PC_TESTBUILD
#define WINDOW_TITLE "FE8 PC port (test build)"
#else
#define WINDOW_TITLE "FE8 PC port"
#endif

static const struct
{
    SDL_GamepadButton button;
    uint16_t key;
} sPadMap[] = {
    { SDL_GAMEPAD_BUTTON_BACK,           PLATFORM_KEY_SELECT },
    { SDL_GAMEPAD_BUTTON_START,          PLATFORM_KEY_START },
    { SDL_GAMEPAD_BUTTON_DPAD_RIGHT,     PLATFORM_KEY_RIGHT },
    { SDL_GAMEPAD_BUTTON_DPAD_LEFT,      PLATFORM_KEY_LEFT },
    { SDL_GAMEPAD_BUTTON_DPAD_UP,        PLATFORM_KEY_UP },
    { SDL_GAMEPAD_BUTTON_DPAD_DOWN,      PLATFORM_KEY_DOWN },
    { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PLATFORM_KEY_R },
    { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  PLATFORM_KEY_L },
};

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

static int ChooseScale(void)
{
    const char *s = getenv("FE8PC_SCALE");
    SDL_Rect usable;
    int scale;

    if (s != NULL)
    {
        scale = atoi(s);
        if (scale >= 1 && scale <= MAX_SCALE)
            return scale;
        Log("[sdl] FE8PC_SCALE=%s ignored (1 to %d)\n", s, MAX_SCALE);
    }
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

int Platform_PollEvents(void)
{
    SDL_Event ev;

#ifdef FE8PC_TESTBUILD
    PortSdlTest_Frame();
#endif
    while (SDL_PollEvent(&ev))
    {
        switch (ev.type)
        {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            sQuit = 1;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (ev.key.scancode == SDL_SCANCODE_F11 && !ev.key.repeat)
            {
                sFullscreen = !sFullscreen;
                SDL_SetWindowFullscreen(sWindow, sFullscreen);
            }
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
    return sQuit;
}

uint16_t Platform_ReadKeys(void)
{
    const bool *state = SDL_GetKeyboardState(NULL);
    uint16_t keys = 0;
    unsigned i, j;

    for (i = 0; i < SDL_arraysize(sKeyMap); i++)
        if (state[sKeyMap[i].scancode])
            keys |= sKeyMap[i].key;

    for (i = 0; i < MAX_PADS; i++)
    {
        SDL_Gamepad *pad = sPads[i].pad;
        int x, y;

        if (pad == NULL)
            continue;
        for (j = 0; j < SDL_arraysize(sPadMap); j++)
            if (SDL_GetGamepadButton(pad, sPadMap[j].button))
                keys |= sPadMap[j].key;
        if (SDL_GetGamepadButton(pad, sPads[i].buttonA))
            keys |= PLATFORM_KEY_A;
        if (SDL_GetGamepadButton(pad, sPads[i].buttonB))
            keys |= PLATFORM_KEY_B;
        if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > TRIGGER_THRESHOLD)
            keys |= PLATFORM_KEY_L;
        if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > TRIGGER_THRESHOLD)
            keys |= PLATFORM_KEY_R;
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
        int ffHeld = state[SDL_SCANCODE_SPACE];

        for (i = 0; i < MAX_PADS; i++)
            /* the right stick's click: the left stick moves the cursor (the
               D-pad above), so pressing it in could also tilt it */
            if (sPads[i].pad != NULL && SDL_GetGamepadButton(sPads[i].pad, SDL_GAMEPAD_BUTTON_RIGHT_STICK))
                ffHeld = 1;
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
 * (port/save/savefile_pc.c).
 */
static char sUserDir[4096];

const char *Platform_UserDir(void)
{
    static int sAsked;
    static const char *sDir;

    if (sAsked)
        return sDir;
    sAsked = 1;
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
