#ifndef GUARD_PORT_PLATFORM_H
#define GUARD_PORT_PLATFORM_H

/*
 * Platform interface (port-plan/PLAN.md section 3.7). Library-neutral: plain
 * C types only, so no SDL (or other library) type ever reaches the shim or
 * the game. The shim (port/hw) calls it; each backend implements all of it:
 *
 *   port/hw/platform_headless.c  no window, no audio device, no live keys.
 *                                Built by port/Makefile (fe8pc, the gates).
 *   port/sdl3/platform_sdl3.c    SDL3 window, keyboard and gamepad. Built by
 *                                port/Makefile.sdl (fe8pc-sdl). The only code
 *                                that includes SDL headers.
 *
 * Call order from the shim (port/hw/main_pc.c, port/hw/frame.c):
 *   Platform_Init once, after the GBA memory map is reserved.
 *   Per frame step: Platform_PresentFrame with the finished frame, then the
 *   pacer (port/hw/pacer.c, Platform_NowNs and Platform_SleepUntilNs), then
 *   Platform_PollEvents and Platform_ReadKeys, whose mask goes to the
 *   REG_KEYINPUT image just before the game's next OnMain reads it.
 *   Platform_Shutdown at exit.
 *
 * Presenting never drives game logic: the frame clock is the pacer's
 * (PLAN.md phase 4, decision D4).
 */

#include <stddef.h>
#include <stdint.h>

#define PLATFORM_GBA_WIDTH  240
#define PLATFORM_GBA_HEIGHT 160

/* GBA keys, active high, as include/gba/io_reg.h numbers them. REG_KEYINPUT
   holds the complement (active low). */
#define PLATFORM_KEY_A      0x001
#define PLATFORM_KEY_B      0x002
#define PLATFORM_KEY_SELECT 0x004
#define PLATFORM_KEY_START  0x008
#define PLATFORM_KEY_RIGHT  0x010
#define PLATFORM_KEY_LEFT   0x020
#define PLATFORM_KEY_UP     0x040
#define PLATFORM_KEY_DOWN   0x080
#define PLATFORM_KEY_R      0x100
#define PLATFORM_KEY_L      0x200
#define PLATFORM_KEY_MASK   0x3FF

/* Start the backend and open its window, if it has one. 0 on success. */
int Platform_Init(void);
void Platform_Shutdown(void);

/* Nonzero when a person is at the controls: a window and live input. The shim
   then paces at the GBA rate and has no frame limit unless one is asked for.
   A static property of the backend; valid before Platform_Init. */
int Platform_IsInteractive(void);

/* Show one finished frame: width * height pixels, row-major, each packed
   R << 24 | G << 16 | B << 8 | A (port/render/out_rgba.c, decision D3).
   Scaled by a whole number with nearest-neighbour sampling, never filtered. */
void Platform_PresentFrame(const uint32_t *rgba, int width, int height);

/* Pump OS events. Called once per frame step. Returns nonzero once the user
   has asked to quit (window closed, SIGINT or SIGTERM). */
int Platform_PollEvents(void);

/* The GBA keys held now (PLATFORM_KEY_*, active high), from the keyboard and
   any gamepad. Opposite directions held together read as neither, as a real
   D-pad cannot press both (mGBA's default). Valid after Platform_PollEvents. */
uint16_t Platform_ReadKeys(void);

/* Audio (phase 3; port-plan/audio-design.md sections 1.5 and 3). Samples are
   s16, interleaved L,R; counts are stereo frames. port/audio/audio_out.c is
   the only caller. The device starts paused. None of these may block: the
   frame step never waits on audio (D4). The headless backend has no device
   (the frames go to FE8PC_AUDIO_DUMP files instead, port/audio/fifo_pc.c). */
int Platform_OpenAudio(uint32_t rateHz, uint32_t channels); /* 0: no device */
void Platform_QueueAudio(const int16_t *lr, uint32_t frames);
uint32_t Platform_AudioQueued(void);                        /* frames waiting */
void Platform_SetAudioRatio(double ratio);
void Platform_PauseAudio(int pause);

/* Monotonic clock in nanoseconds (any epoch), and a sleep until a time on it. */
uint64_t Platform_NowNs(void);
void Platform_SleepUntilNs(uint64_t deadlineNs);

/* Files (phases 1 and 5). Platform_LoadFile returns the whole file, or NULL;
   release it with Platform_FreeFile. Platform_SaveFileAtomic writes the data
   to tmpPath (a file of the caller's, in path's folder), flushes it to the
   disk and renames it over path (replacing an existing file); 0 on success.
   port/save/savefile_pc.c keeps the player's .sav with them. Platform_UserDir
   names a per-user directory, ending in a path separator: where the window
   build's .sav goes when the ROM's folder cannot be written, and where it
   looks for one in any case (the window backend: the folder SDL's pref path
   names, $XDG_DATA_HOME/fe8pc/ or ~/.local/share/fe8pc/ on Linux,
   %APPDATA%\fe8pc\ on Windows). It only names it; the folder may not be
   there. Platform_MakeUserDir makes it (0, or -1 logged) and is called only
   when the save file goes there, so a run that saves next to the ROM leaves
   no empty folder behind. NULL when there is none (the backend logs why);
   never the current folder in its place. */
void *Platform_LoadFile(const char *path, size_t *size);
void Platform_FreeFile(void *data);
int Platform_SaveFileAtomic(const char *path, const char *tmpPath, const void *data, size_t size);
const char *Platform_UserDir(void);
int Platform_MakeUserDir(void);

/* A short note shown after the window title for the rest of the run, or
   until the next call (NULL: none), for what the player must see: where the
   save file is when it is not next to the ROM, that there are two save
   files, that saving failed, or that another copy of the game has the save
   file. May be called before Platform_Init. The
   headless backend has no title; its log has the same lines. */
void Platform_SetTitleNote(const char *note);

#endif /* GUARD_PORT_PLATFORM_H */
