#ifndef GUARD_PORT_PORT_STATE_H
#define GUARD_PORT_PORT_STATE_H

/*
 * Save states (Reforged Options, Helpers: "Save states", v1.24.0; port/reforged/reforged_savestate.c).
 * Played builds only. A state is a photo of the whole game at one program point: the GBA memory the
 * port keeps (EWRAM, IWRAM, I/O, palette, VRAM, OAM; never SRAM) and every byte of the host .data and
 * .bss of the game's and the port's own objects (between the marker objects port/statemark/begin.c and
 * end.c, first and last on the link line; the C runtime's and the libraries' data lie outside them and
 * are never touched). Everything there is restored, except what a file names with PORT_STATE_SESSION:
 * host globals that are not game state (settings, files, SDL, audio device, input, timing, logs, heap
 * pointers, environment switches). The new process keeps its own values for those.
 *
 * PORT_STATE_SESSION(var), at file scope after var's definition, names var (its whole sizeof) as session
 * data: a constructor registers its address before main(). PORT_STATE_SESSION_NOW(ptr, size): the same,
 * called at run time, for a function's static that is set before any state can be loaded (boot). Both
 * are empty in every test build.
 *
 * The program point: frame.c VBlankIntrWait with gBmSt.main_loop_ended set (the end of OnMain,
 * MainUpdate_8055C68 or InBattleMainRoutine, each of which returns right after it; the Gate 0 harness's
 * sample point, port/test/README.md "Save states"), before the frame step.
 */

#include <stdint.h>

/* PORT_RFO_STATE: port/Makefile gives it to every port object (port/reforged included) of a played build,
   on PORT_PLAYED_BUILD's condition (no FE8PC_TESTBUILD, no FE8PC_DEBUG); port.h's PORT_PLAYED_BUILD itself
   cannot tell a port-layer object of the Gate 0 binary, whose FE8PC_DEBUG only the game's flags carry. */
#if defined(PORT_RFO_STATE)
#define PORT_STATE_ON 1
#else
#define PORT_STATE_ON 0
#endif

#if PORT_STATE_ON
void PortState_Exclude(const void *p, unsigned long size, const char *name);
#define PORT_STATE_SESSION(var)                                                                         \
    static void __attribute__((constructor)) PortStateSession_##var(void)                               \
    {                                                                                                   \
        PortState_Exclude(&(var), sizeof(var), #var);                                                   \
    }
#define PORT_STATE_SESSION_NOW(ptr, size) PortState_Exclude((ptr), (size), #ptr)

/* frame.c VBlankIntrWait: the program point (main_loop_ended set). Does what was asked (a key, a test
   switch); nothing otherwise. */
void PortState_Point(void);
/* frame.c, just before the frame is shown: the confirmation toast on the window's picture. render.c,
   before a frame is dumped: the same on the 555 picture, only with FE8PC_TEST_STATE_TOAST_DUMP=1.
   ToastRgba's picture is stride pixels wide with the game's 240 strip at x0 (Widescreen: render.h
   RENDER_WIDE_W, RENDER_WIDE_MARGIN; else 240, 0); the toast stays inside that strip. */
void PortState_ToastRgba(uint32_t *rgba, int stride, int x0);
void PortState_ToastDump555(uint16_t *frame555);
/* The platform: Ctrl+S (save 1) or Ctrl+L (load 2) was pressed; acted on at the next program point.
   Ignored with the row off. */
void PortState_Request(int what);
/* The platform, v1.24.1: Ctrl+S (1) or Ctrl+L (2) pressed with the row off: the toast "Save states are off"
   only (display; nothing saved or loaded). Ignored with the row on. */
void PortState_OffHint(int what);
/* 1 when the row is on (the platform then keeps Ctrl+S / Ctrl+L away from the GBA buttons). */
int PortState_KeysOn(void);
/* render.c: the folder frame dumps go to after a load (FE8PC_TEST_STATE_DUMP_DIR), or NULL. */
const char *PortState_DumpDir(void);
#else
#define PORT_STATE_SESSION(var)
#define PORT_STATE_SESSION_NOW(ptr, size) ((void)0)
#endif

#endif /* GUARD_PORT_PORT_STATE_H */
