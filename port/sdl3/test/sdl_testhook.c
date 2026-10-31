/*
 * Test builds only (FE8PC_TESTBUILD=1, port/Makefile; port-plan/DIVERGENCES.md
 * TB-1). port/Makefile.sdl and port/Makefile.mingw (SDL=1) build this file
 * into a test build (build/pc-sdl-test, build/win32-sdl-test) and never into
 * a release build, and platform_sdl3.c calls it only under #ifdef
 * FE8PC_TESTBUILD, so the release window build has none of it.
 *
 * Hooks for the automated checks of the window build (port/win32/README.md,
 * "Checks"). Those run with SDL's offscreen video driver and its disk or
 * dummy audio driver (SDL_VIDEO_DRIVER=offscreen, SDL_AUDIO_DRIVER=disk), so
 * no window shows and no sound plays, and with SDL's joystick drivers for
 * real devices off (port/check-all.mk, WIN_SDL_NOPADS), so a gamepad of the
 * machine's cannot press keys in a fixed run; SDL's virtual gamepads still
 * work. Each hook is off unless its variable is set:
 *
 *   FE8PC_TEST_KEYS=FILE        a Gate 0 key file (port/test/README.md, "Input
 *                               file"): tick t's mask is held for OnMain call
 *                               t (gPortOnMainCalls counts them), added to the
 *                               live keys, so the scripted runs of the Linux
 *                               and Windows windows get the same keys.
 *   FE8PC_TEST_FRAMEHASH=FILE   one line per presented frame: the frame number
 *                               (gPortFrameCount), the FNV-1a 32 of the frame
 *                               handed to SDL (240x160, R<<24|G<<16|B<<8|A
 *                               words) and of what SDL drew (the render
 *                               target read back before it is presented,
 *                               converted to SDL_PIXELFORMAT_RGBA8888 rows),
 *                               and the read-back size.
 *   FE8PC_TEST_FRAMESTATS=1     at the end, the time between presented frames:
 *                               mean, minimum, maximum, percentiles, and how
 *                               many came late (the pacer's target is
 *                               16.742706 ms, 59.7275 Hz).
 *   FE8PC_TEST_HIDDEN=1         the window is made hidden (SDL_WINDOW_HIDDEN):
 *                               the real video driver and renderer without a
 *                               window on the screen (frame times measured
 *                               with them).
 *   FE8PC_TEST_AUDIO_GAIN=G     the audio stream's gain; 0 is silence (the
 *                               real audio driver, WASAPI on Windows, without
 *                               a sound).
 *   FE8PC_TEST_INFO=1           log SDL's video and audio drivers, the
 *                               renderer, the window's pixel size, pixel
 *                               density and display scale, and on Windows the
 *                               DPI awareness and where SDL3.dll is loaded;
 *                               at the end, the window title (the save notes
 *                               of Platform_SetTitleNote).
 *   FE8PC_TEST_VPAD=TYPE[,FILE] an SDL virtual gamepad (SDL_AttachVirtualJoystick),
 *                               plugged in at start. TYPE: xbox (Xbox 360 pad
 *                               USB ids), ps4 (DualShock 4), switch (Switch
 *                               Pro Controller, whose A and B are east and
 *                               south) or standard (no ids). FILE: lines
 *                               "FRAME BUTTONS [LX LY LT RT]", from frame
 *                               FRAME on (gPortFrameCount) the pad holds the
 *                               buttons in the hex mask BUTTONS (bit n is
 *                               SDL_GamepadButton n) and the axes given, as
 *                               the joystick's raw axes (-32768 to 32767;
 *                               left out: the stick at 0, the triggers at
 *                               -32768, released). SDL's gamepad API reads
 *                               a stick at its raw value, and a trigger at
 *                               raw r as (r + 32768) * 32767 / 65535 rounded
 *                               down (the full range scaled to 0-32767), so
 *                               against the port's "more than 16384" raw 2
 *                               (16384) leaves L and R up and raw 3 (16385)
 *                               presses them (measured, Linux and Windows,
 *                               xbox and ps4 pads); "FRAME
 *                               unplug" and "FRAME plug" take it away and put
 *                               it back (hot-plug). # starts a comment.
 *   FE8PC_TEST_WINKEYS=FILE     Windows only, with the windows video driver
 *                               (a hidden window, FE8PC_TEST_HIDDEN=1): lines
 *                               "FRAME down|up KEY" post the key's
 *                               WM_KEYDOWN or WM_KEYUP (its virtual-key code
 *                               and its keyboard scan code) to the SDL window
 *                               at that frame, so the keys go through SDL's
 *                               own Windows keyboard code. KEY: z x q e
 *                               enter backspace up down left right lshift.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "port_hw.h"
#include "sdl_testhook.h"

#define FRAME_NS 16742706.298828125 /* port/hw/pacer.c */

/* ------------------------------------------------------------------------- */
/* FE8PC_TEST_KEYS                                                           */

static uint16_t *sKeyMasks;
static uint32_t sKeyTicks;

static void LoadKeys(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    uint32_t cap = 0, lineNo = 0;

    if (f == NULL)
    {
        PortLog("[sdltest] cannot read the key file '%s'\n", path);
        PortExit(1);
    }
    while (fgets(line, sizeof(line), f) != NULL)
    {
        char *p = strchr(line, '#'), *end;
        unsigned long mask, n = 1;

        lineNo++;
        if (p != NULL)
            *p = '\0';
        p = line + strspn(line, " \t\r\n");
        if (*p == '\0')
            continue;
        mask = strtoul(p, &end, 16);
        if (end == p || mask > 0x3FF)
        {
            PortLog("[sdltest] key file '%s' line %u: not a key mask\n", path, lineNo);
            PortExit(1);
        }
        if (*end == '*')
        {
            p = end + 1;
            n = strtoul(p, &end, 10);
            if (end == p)
            {
                PortLog("[sdltest] key file '%s' line %u: bad repeat count\n", path, lineNo);
                PortExit(1);
            }
        }
        while (n-- > 0)
        {
            if (sKeyTicks == cap)
            {
                cap = cap ? cap * 2 : 4096;
                sKeyMasks = realloc(sKeyMasks, cap * sizeof(*sKeyMasks));
                if (sKeyMasks == NULL)
                    PortExit(1);
            }
            sKeyMasks[sKeyTicks++] = (uint16_t)mask;
        }
    }
    fclose(f);
    PortLog("[sdltest] key file '%s': %u ticks\n", path, sKeyTicks);
}

uint16_t PortSdlTest_Keys(uint16_t keys)
{
    if (gPortOnMainCalls < sKeyTicks)
        keys |= sKeyMasks[gPortOnMainCalls];
    return keys;
}

/* ------------------------------------------------------------------------- */
/* FE8PC_TEST_FRAMEHASH, FE8PC_TEST_FRAMESTATS                               */

static FILE *sHashFile;
static int sStats;
static uint64_t sLastPresentNs;
static double *sIntervals; /* ms */
static uint32_t sIntervalCount, sIntervalCap;

static uint32_t Fnv(uint32_t h, const void *data, size_t n)
{
    const uint8_t *p = data;

    while (n-- != 0)
        h = (h ^ *p++) * 16777619u;
    return h;
}

void PortSdlTest_BeforePresent(SDL_Renderer *renderer, const uint32_t *rgba, int width, int height)
{
    SDL_Surface *got, *conv = NULL;
    uint32_t src, drawn = 2166136261u;
    int y, w = 0, h = 0;

    if (sHashFile == NULL)
        return;
    src = Fnv(2166136261u, rgba, (size_t)width * (size_t)height * sizeof(*rgba));
    got = SDL_RenderReadPixels(renderer, NULL);
    if (got != NULL)
        conv = SDL_ConvertSurface(got, SDL_PIXELFORMAT_RGBA8888);
    if (conv != NULL)
    {
        w = conv->w;
        h = conv->h;
        for (y = 0; y < h; y++)
            drawn = Fnv(drawn, (const uint8_t *)conv->pixels + (size_t)y * (size_t)conv->pitch, (size_t)w * 4);
    }
    else
        drawn = 0;
    fprintf(sHashFile, "%u %08x %08x %dx%d\n", gPortFrameCount, src, drawn, w, h);
    SDL_DestroySurface(conv);
    SDL_DestroySurface(got);
}

void PortSdlTest_AfterPresent(void)
{
    uint64_t now;

    if (!sStats)
        return;
    now = SDL_GetTicksNS();
    if (sLastPresentNs != 0)
    {
        if (sIntervalCount == sIntervalCap)
        {
            sIntervalCap = sIntervalCap ? sIntervalCap * 2 : 65536;
            sIntervals = realloc(sIntervals, sIntervalCap * sizeof(*sIntervals));
            if (sIntervals == NULL)
            {
                sStats = 0;
                return;
            }
        }
        sIntervals[sIntervalCount++] = (double)(now - sLastPresentNs) / 1e6;
    }
    sLastPresentNs = now;
}

static int CompareDouble(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;

    return x < y ? -1 : x > y;
}

static void ReportStats(void)
{
    double sum = 0, target = FRAME_NS / 1e6, p50, p90, p99, p999;
    uint32_t i, n = sIntervalCount, late2 = 0, late4 = 0, twoFrames = 0, early = 0;

    if (!sStats || n == 0)
        return;
    for (i = 0; i < n; i++)
    {
        sum += sIntervals[i];
        late2 += sIntervals[i] > target + 2.0;
        late4 += sIntervals[i] > target + 4.0;
        twoFrames += sIntervals[i] > 2 * target;
        early += sIntervals[i] < target - 2.0;
    }
    qsort(sIntervals, n, sizeof(*sIntervals), CompareDouble);
    p50 = sIntervals[n / 2];
    p90 = sIntervals[(uint32_t)(n * 0.90)];
    p99 = sIntervals[(uint32_t)(n * 0.99)];
    p999 = sIntervals[(uint32_t)(n * 0.999)];
    PortLog("[sdltest] frame times: %u intervals, mean %.4f ms (%.4f Hz; target %.4f ms, 59.7275 Hz), "
            "min %.3f, p50 %.3f, p90 %.3f, p99 %.3f, p99.9 %.3f, max %.3f ms; "
            "%u over target+2 ms, %u over target+4 ms, %u over 2 frames, %u under target-2 ms\n",
        n, sum / n, 1000.0 * n / sum, target, sIntervals[0], p50, p90, p99, p999, sIntervals[n - 1], late2, late4,
        twoFrames, early);
}

/* ------------------------------------------------------------------------- */
/* FE8PC_TEST_VPAD                                                           */

struct PadStep
{
    uint32_t frame;
    int kind; /* 0 state, 1 unplug, 2 plug */
    uint32_t buttons;
    int16_t axes[4];
};

static struct PadStep *sPadSteps;
static uint32_t sPadStepCount, sPadNext;
static const char *sPadType;
static SDL_JoystickID sPadId;
static SDL_Joystick *sPadJoy;
static int sPadLabelsLogged;

static void LoadPadScript(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    uint32_t cap = 0, lineNo = 0;

    if (f == NULL)
    {
        PortLog("[sdltest] cannot read the pad script '%s'\n", path);
        PortExit(1);
    }
    while (fgets(line, sizeof(line), f) != NULL)
    {
        struct PadStep s;
        char word[32];
        int ax[4] = { 0, 0, -32768, -32768 }, n;
        unsigned frame = 0, buttons = 0;
        char *p = strchr(line, '#');

        lineNo++;
        if (p != NULL)
            *p = '\0';
        memset(&s, 0, sizeof(s));
        if (sscanf(line, "%u %31s", &frame, word) < 2)
            continue;
        s.frame = frame;
        if (strcmp(word, "unplug") == 0)
            s.kind = 1;
        else if (strcmp(word, "plug") == 0)
            s.kind = 2;
        else if ((n = sscanf(line, "%u %x %d %d %d %d", &frame, &buttons, &ax[0], &ax[1], &ax[2], &ax[3])) >= 2)
        {
            s.buttons = buttons;
            for (n = 0; n < 4; n++)
                s.axes[n] = (int16_t)(ax[n] < -32768 ? -32768 : ax[n] > 32767 ? 32767 : ax[n]);
        }
        else
        {
            PortLog("[sdltest] pad script '%s' line %u: not understood\n", path, lineNo);
            PortExit(1);
        }
        if (sPadStepCount == cap)
        {
            cap = cap ? cap * 2 : 64;
            sPadSteps = realloc(sPadSteps, cap * sizeof(*sPadSteps));
            if (sPadSteps == NULL)
                PortExit(1);
        }
        sPadSteps[sPadStepCount++] = s;
    }
    fclose(f);
    PortLog("[sdltest] pad script '%s': %u steps\n", path, sPadStepCount);
}

static void PlugPad(void)
{
    SDL_VirtualJoystickDesc desc;
    char name[64];

    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    desc.button_mask = (1u << (SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1)) - 1;
    if (strcmp(sPadType, "xbox") == 0)
    {
        desc.vendor_id = 0x045E; /* Microsoft Xbox 360 Controller */
        desc.product_id = 0x028E;
    }
    else if (strcmp(sPadType, "ps4") == 0)
    {
        desc.vendor_id = 0x054C; /* Sony DualShock 4 */
        desc.product_id = 0x09CC;
    }
    else if (strcmp(sPadType, "switch") == 0)
    {
        desc.vendor_id = 0x057E; /* Nintendo Switch Pro Controller */
        desc.product_id = 0x2009;
    }
    SDL_snprintf(name, sizeof(name), "FE8 test pad (%s)", sPadType);
    desc.name = name;
    sPadId = SDL_AttachVirtualJoystick(&desc);
    if (sPadId == 0)
    {
        PortLog("[sdltest] SDL_AttachVirtualJoystick failed: %s\n", SDL_GetError());
        PortExit(1);
    }
    sPadJoy = SDL_OpenJoystick(sPadId);
    if (sPadJoy == NULL)
    {
        PortLog("[sdltest] SDL_OpenJoystick(virtual %u) failed: %s\n", (unsigned)sPadId, SDL_GetError());
        PortExit(1);
    }
    sPadLabelsLogged = 0;
    PortLog("[sdltest] frame %u: virtual pad %u plugged in (%s, %04X:%04X), is a gamepad: %s\n", gPortFrameCount,
        (unsigned)sPadId, sPadType, desc.vendor_id, desc.product_id, SDL_IsGamepad(sPadId) ? "yes" : "no");
}

static void UnplugPad(void)
{
    if (sPadJoy == NULL)
        return;
    SDL_CloseJoystick(sPadJoy);
    sPadJoy = NULL;
    if (!SDL_DetachVirtualJoystick(sPadId))
        PortLog("[sdltest] SDL_DetachVirtualJoystick failed: %s\n", SDL_GetError());
    PortLog("[sdltest] frame %u: virtual pad %u unplugged\n", gPortFrameCount, (unsigned)sPadId);
}

static const char *LabelName(SDL_GamepadButtonLabel l)
{
    switch (l)
    {
    case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
    case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
    case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
    case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "cross";
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "circle";
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "square";
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "triangle";
    default: return "none";
    }
}

/* ------------------------------------------------------------------------- */
/* FE8PC_TEST_WINKEYS                                                        */

#ifdef _WIN32
struct WinKeyStep
{
    uint32_t frame;
    int down;
    UINT vk, scan, extended;
};

static struct WinKeyStep *sWinKeys;
static uint32_t sWinKeyCount, sWinKeyNext;
static HWND sHwnd;

static void LoadWinKeys(const char *path, SDL_Window *window)
{
    static const struct
    {
        const char *name;
        UINT vk, scan, extended;
    } keys[] = {
        { "z", 'Z', 0x2C, 0 },         { "x", 'X', 0x2D, 0 },         { "q", 'Q', 0x10, 0 },
        { "e", 'E', 0x12, 0 },         { "enter", VK_RETURN, 0x1C, 0 }, { "backspace", VK_BACK, 0x0E, 0 },
        { "up", VK_UP, 0x48, 1 },      { "down", VK_DOWN, 0x50, 1 },  { "left", VK_LEFT, 0x4B, 1 },
        { "right", VK_RIGHT, 0x4D, 1 }, { "lshift", VK_SHIFT, 0x2A, 0 },
    };
    FILE *f = fopen(path, "r");
    char line[128], dir[16], name[32];
    uint32_t cap = 0;
    unsigned frame, i;

    sHwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (f == NULL || sHwnd == NULL)
    {
        PortLog("[sdltest] FE8PC_TEST_WINKEYS: %s\n", f == NULL ? "cannot read the file" : "no Win32 window");
        PortExit(1);
    }
    while (fgets(line, sizeof(line), f) != NULL)
    {
        if (line[0] == '#' || sscanf(line, "%u %15s %31s", &frame, dir, name) != 3)
            continue;
        for (i = 0; i < SDL_arraysize(keys) && strcmp(keys[i].name, name) != 0; i++)
            ;
        if (i == SDL_arraysize(keys) || (strcmp(dir, "down") != 0 && strcmp(dir, "up") != 0))
        {
            PortLog("[sdltest] FE8PC_TEST_WINKEYS: not understood: %s", line);
            PortExit(1);
        }
        if (sWinKeyCount == cap)
        {
            cap = cap ? cap * 2 : 64;
            sWinKeys = realloc(sWinKeys, cap * sizeof(*sWinKeys));
            if (sWinKeys == NULL)
                PortExit(1);
        }
        sWinKeys[sWinKeyCount].frame = frame;
        sWinKeys[sWinKeyCount].down = strcmp(dir, "down") == 0;
        sWinKeys[sWinKeyCount].vk = keys[i].vk;
        sWinKeys[sWinKeyCount].scan = keys[i].scan;
        sWinKeys[sWinKeyCount].extended = keys[i].extended;
        sWinKeyCount++;
    }
    fclose(f);
    PortLog("[sdltest] FE8PC_TEST_WINKEYS '%s': %u key messages for window %p\n", path, sWinKeyCount, (void *)sHwnd);
}

static void PostWinKeys(void)
{
    /* user32 by name, not imported: the exe may import only KERNEL32 and
       msvcrt (port/Makefile.mingw); SDL has loaded user32 by now. */
    typedef BOOL(WINAPI * PostMessageWFn)(HWND, UINT, WPARAM, LPARAM);
    static PostMessageWFn post;

    if (sWinKeyNext < sWinKeyCount && post == NULL)
    {
        HMODULE user32 = GetModuleHandleA("user32.dll");

        post = user32 != NULL ? (PostMessageWFn)(void *)GetProcAddress(user32, "PostMessageW") : NULL;
        if (post == NULL)
        {
            PortLog("[sdltest] FE8PC_TEST_WINKEYS: no PostMessageW\n");
            PortExit(1);
        }
    }
    while (sWinKeyNext < sWinKeyCount && sWinKeys[sWinKeyNext].frame <= gPortFrameCount)
    {
        const struct WinKeyStep *k = &sWinKeys[sWinKeyNext++];
        /* lParam: repeat count 1, the scan code, the extended-key bit; a key up
           also has the previous-state and transition bits. */
        LPARAM lp = (LPARAM)(1u | (k->scan << 16) | (k->extended << 24) | (k->down ? 0u : 0xC0000000u));

        if (!post(sHwnd, k->down ? WM_KEYDOWN : WM_KEYUP, (WPARAM)k->vk, lp))
            PortLog("[sdltest] PostMessage failed: %lu\n", (unsigned long)GetLastError());
        PortLog("[sdltest] frame %u: posted %s of virtual key 0x%02X (scan code 0x%02X%s)\n", gPortFrameCount,
            k->down ? "WM_KEYDOWN" : "WM_KEYUP", k->vk, k->scan, k->extended ? ", extended" : "");
    }
}
#endif

void PortSdlTest_Frame(void)
{
#ifdef _WIN32
    PostWinKeys();
#endif
    if (sPadType == NULL)
        return;
    if (sPadJoy != NULL && !sPadLabelsLogged && SDL_IsGamepad(sPadId))
    {
        SDL_GamepadType t = SDL_GetGamepadTypeForID(sPadId);

        sPadLabelsLogged = 1;
        PortLog("[sdltest] virtual pad %u: SDL gamepad type %s; south is labelled %s, east %s\n", (unsigned)sPadId,
            SDL_GetGamepadStringForType(t) != NULL ? SDL_GetGamepadStringForType(t) : "unknown",
            LabelName(SDL_GetGamepadButtonLabelForType(t, SDL_GAMEPAD_BUTTON_SOUTH)),
            LabelName(SDL_GetGamepadButtonLabelForType(t, SDL_GAMEPAD_BUTTON_EAST)));
    }
    while (sPadNext < sPadStepCount && sPadSteps[sPadNext].frame <= gPortFrameCount)
    {
        const struct PadStep *s = &sPadSteps[sPadNext++];
        int i;

        if (s->kind == 1)
            UnplugPad();
        else if (s->kind == 2 && sPadJoy == NULL)
            PlugPad();
        else if (s->kind == 0 && sPadJoy != NULL)
        {
            static const SDL_GamepadAxis axes[4] = {
                SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY,
                SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
            };

            for (i = 0; i <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT; i++)
                SDL_SetJoystickVirtualButton(sPadJoy, i, (s->buttons >> i) & 1);
            for (i = 0; i < 4; i++)
                SDL_SetJoystickVirtualAxis(sPadJoy, axes[i], s->axes[i]);
            PortLog("[sdltest] frame %u: virtual pad buttons 0x%04X, LX %d LY %d LT %d RT %d\n", gPortFrameCount,
                s->buttons, s->axes[0], s->axes[1], s->axes[2], s->axes[3]);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Start, audio, end                                                         */

SDL_WindowFlags PortSdlTest_WindowFlags(SDL_WindowFlags flags)
{
    const char *s = getenv("FE8PC_TEST_HIDDEN");

    if (s != NULL && atoi(s) != 0)
    {
        PortLog("[sdltest] FE8PC_TEST_HIDDEN: the window is made hidden\n");
        flags |= SDL_WINDOW_HIDDEN;
    }
    return flags;
}

static void LogInfo(SDL_Window *window, SDL_Renderer *renderer)
{
    char list[512];
    int i, n, pw = 0, ph = 0;

    list[0] = '\0';
    for (i = 0, n = SDL_GetNumVideoDrivers(); i < n; i++)
        SDL_snprintf(list + strlen(list), sizeof(list) - strlen(list), "%s%s", i ? " " : "", SDL_GetVideoDriver(i));
    PortLog("[sdltest] video drivers: %s; current %s\n", list, SDL_GetCurrentVideoDriver());
    list[0] = '\0';
    for (i = 0, n = SDL_GetNumAudioDrivers(); i < n; i++)
        SDL_snprintf(list + strlen(list), sizeof(list) - strlen(list), "%s%s", i ? " " : "", SDL_GetAudioDriver(i));
    PortLog("[sdltest] audio drivers: %s\n", list);
    list[0] = '\0';
    for (i = 0, n = SDL_GetNumRenderDrivers(); i < n; i++)
        SDL_snprintf(list + strlen(list), sizeof(list) - strlen(list), "%s%s", i ? " " : "", SDL_GetRenderDriver(i));
    PortLog("[sdltest] render drivers: %s; current %s\n", list, SDL_GetRendererName(renderer));
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    PortLog("[sdltest] window %d x %d pixels, pixel density %.2f, display scale %.2f, display content scale %.2f\n",
        pw, ph, SDL_GetWindowPixelDensity(window), SDL_GetWindowDisplayScale(window),
        SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window)));
#ifdef _WIN32
    {
        HMODULE sdl = GetModuleHandleA("SDL3.dll"), user32 = GetModuleHandleA("user32.dll");
        typedef void *(WINAPI *GetCtxFn)(void);
        typedef int(WINAPI *AwareFn)(void *);
        GetCtxFn getCtx = user32 ? (GetCtxFn)(void *)GetProcAddress(user32, "GetThreadDpiAwarenessContext") : NULL;
        AwareFn aware = user32 ? (AwareFn)(void *)GetProcAddress(user32, "GetAwarenessFromDpiAwarenessContext") : NULL;
        char path[MAX_PATH * 4] = "";

        if (sdl != NULL)
        {
            const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)sdl;
            const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)((const uint8_t *)sdl + dos->e_lfanew);

            GetModuleFileNameA(sdl, path, sizeof(path));
            PortLog("[sdltest] SDL3.dll at 0x%08X-0x%08X ('%s'); the GBA map is 0x02000000-0x0E00FFFF\n",
                (unsigned)(uintptr_t)sdl, (unsigned)((uintptr_t)sdl + nt->OptionalHeader.SizeOfImage), path);
        }
        if (getCtx != NULL && aware != NULL)
            PortLog("[sdltest] DPI awareness %d (0 unaware, 1 system, 2 per monitor)\n", aware(getCtx()));
        PortLog("[sdltest] active code page %u\n", (unsigned)GetACP());
    }
#endif
}

static SDL_Window *sInfoWindow; /* FE8PC_TEST_INFO: its title is logged at the end */

void PortSdlTest_AfterInit(SDL_Window *window, SDL_Renderer *renderer)
{
    const char *s;

    if ((s = getenv("FE8PC_TEST_INFO")) != NULL && atoi(s) != 0)
    {
        LogInfo(window, renderer);
        sInfoWindow = window;
    }
    if ((s = getenv("FE8PC_TEST_KEYS")) != NULL && s[0] != '\0')
        LoadKeys(s);
    if ((s = getenv("FE8PC_TEST_FRAMEHASH")) != NULL && s[0] != '\0')
    {
        sHashFile = fopen(s, "wb"); /* "\n" line ends on Windows too: the files compare byte for byte */
        if (sHashFile == NULL)
        {
            PortLog("[sdltest] cannot write '%s'\n", s);
            PortExit(1);
        }
        PortLog("[sdltest] frame hashes to '%s'\n", s);
    }
    if ((s = getenv("FE8PC_TEST_FRAMESTATS")) != NULL && atoi(s) != 0)
        sStats = 1;
#ifdef _WIN32
    if ((s = getenv("FE8PC_TEST_WINKEYS")) != NULL && s[0] != '\0')
        LoadWinKeys(s, window);
#endif
    if ((s = getenv("FE8PC_TEST_VPAD")) != NULL && s[0] != '\0')
    {
        static char type[32];
        const char *comma = strchr(s, ',');
        size_t n = comma != NULL ? (size_t)(comma - s) : strlen(s);

        if (n >= sizeof(type))
            n = sizeof(type) - 1;
        memcpy(type, s, n);
        type[n] = '\0';
        if (strcmp(type, "xbox") != 0 && strcmp(type, "ps4") != 0 && strcmp(type, "switch") != 0 &&
            strcmp(type, "standard") != 0)
        {
            PortLog("[sdltest] FE8PC_TEST_VPAD=%s: the type is xbox, ps4, switch or standard\n", s);
            PortExit(1);
        }
        sPadType = type;
        if (comma != NULL)
            LoadPadScript(comma + 1);
        PlugPad();
    }
}

void PortSdlTest_AudioOpened(SDL_AudioStream *stream)
{
    const char *s = getenv("FE8PC_TEST_AUDIO_GAIN");

    if (s != NULL && s[0] != '\0')
    {
        float gain = (float)atof(s);

        if (SDL_SetAudioStreamGain(stream, gain))
            PortLog("[sdltest] audio stream gain %.3f\n", (double)gain);
        else
            PortLog("[sdltest] SDL_SetAudioStreamGain failed: %s\n", SDL_GetError());
    }
}

void PortSdlTest_Shutdown(void)
{
    if (sInfoWindow != NULL)
        PortLog("[sdltest] window title at the end: '%s'\n", SDL_GetWindowTitle(sInfoWindow));
    ReportStats();
    UnplugPad();
    if (sHashFile != NULL)
    {
        fclose(sHashFile);
        sHashFile = NULL;
    }
}
