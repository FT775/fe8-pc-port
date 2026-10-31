#include "global.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware.h"

#include "port_hw.h"
#include "render.h"
#include "../platform.h"
#include "../audio/audio_pc.h"

/*
 * Frame step (PLAN.md section 3.4). VBlankIntrWait advances the simulated
 * hardware by one frame and returns, so the game loop in main.c runs
 * unchanged. VCOUNT, DISPSTAT and the IRQ dispatch run per line so HBlank and
 * VCount handlers see the same sequence they would on hardware, and the
 * software renderer (port/render) draws each visible line in between.
 */

#define IRQ_COUNT 14
extern void *gIRQHandlers[IRQ_COUNT];

uint32_t gPortFrameCount;
uint32_t gPortFrameLimit = 60;
/* Nonzero while a game IRQ handler runs. Only read by the BIOS-area probe
   (nullprobe.c, FE8PC_NULLPROBE=1), for the open-bus latch rule. */
volatile uint32_t gPortIrqDepth;

#ifdef FE8PC_GATE0
void PortGate0IrqDue(int n); /* port/test/pc/gate0_hook.c */
/* Test harness only (P-25): REG_IF when the last OnMain began (the end of the
   frame step before it) and when this frame step began (after OnMain, which
   can take a latched IRQ). The raster-timing redraws start from these
   (gate0_hook.c, RrRedraw): the GBA-timing redraw from OnMain's start, since
   it makes the main loop's REG_IE stores again; the control redraw from the
   frame step's start. */
uint16_t gPortIfOnMainStart, gPortIfFrameStart;
/* The I/O halfwords that handlers taken inside OnMain (a latched IRQ, at the
   main loop's REG_IE store) changed, with their values from before the first
   such call of this OnMain (gPortOnMainIrqIoMask[i] nonzero for I/O halfword
   i). The GBA-timing redraw puts them back at its start: on the GBA that
   call came at the store's line, and the lines before it showed the values
   from before it. Cleared when OnMain begins. */
uint16_t gPortOnMainIrqIoPre[0x200];
uint8_t gPortOnMainIrqIoMask[0x200];
#endif

/*
 * P-25: the interrupt flags as on the GBA (GBATEK, "Interrupt Control"). A
 * request sets its IF bit whatever IE and IME hold, and the bit stays set
 * until the IRQ is taken. The CPU takes an IRQ whenever IME bit 0 is set and
 * IE & IF is not 0: at the request, or later at once when the game turns the
 * IE bit (or IME) on, in the middle of OnMain (SetPrimaryHBlankHandler,
 * SetInterrupt_LCDVCountMatch; src/hardware.c calls PortIrqTakePending after
 * those stores). IrqMain (src/crt0.s) takes the lowest set bit of IE & IF,
 * acknowledges it (writes the bit to IF) before it calls the handler, and the
 * CPU enters it again while bits stay pending.
 */
static void TakePending(int inOnMain)
{
    u16 pending;

    while ((REG_IME & 1) && (pending = (u16)(REG_IE & REG_IF & 0x3FFF)) != 0)
    {
        int n = 0;
        void (*handler)(void);

        while (!(pending & (1 << n)))
            n++;
        REG_IF &= (u16)~(1 << n); /* IrqMain's acknowledge, before the handler */
        handler = (void (*)(void))gIRQHandlers[n];
        if (handler != NULL)
        {
#ifdef FE8PC_GATE0
            static uint16_t sIoBefore[0x200];
            int i;

            if (inOnMain)
                memcpy(sIoBefore, (const void *)(uintptr_t)0x04000000u, sizeof(sIoBefore));
#endif
            gPortIrqDepth++;
            handler();
            gPortIrqDepth--;
#ifdef FE8PC_GATE0
            if (inOnMain)
                for (i = 0; i < 0x200; i++)
                    if (!gPortOnMainIrqIoMask[i] && ((volatile uint16_t *)(uintptr_t)0x04000000u)[i] != sIoBefore[i])
                    {
                        gPortOnMainIrqIoMask[i] = 1;
                        gPortOnMainIrqIoPre[i] = sIoBefore[i];
                    }
#endif
        }
    }
}

/* The REG_IE stores of src/hardware.c (OnMain, or a handler): see above. */
void PortIrqTakePending(void)
{
    TakePending(gPortIrqDepth == 0);
}

/* Request IRQ n: its IF bit is set, and IrqMain takes it if IME and IE allow. */
static void Irq(int n)
{
#ifdef FE8PC_GATE0
    /* Test harness only (the Gate 0 binary): during a raster-timing redraw the
       GBA's stores due before this IRQ are made first, so a REG_IE write that
       the GBA's main loop made mid-frame turns the IRQ off or on here as it
       did there (port/test/README.md, "Raster-timing frames"). A store that
       turns an IRQ on waits for the check of the GBA's next line event
       (gate0_hook.c). Does nothing outside a redraw. */
    PortGate0IrqDue(n);
    /* A REG_IE store of the GBA's main loop made just now takes an IRQ
       latched before it at once, before this request (P-25). */
    TakePending(0);
#endif
    REG_IF |= (u16)(1 << n);
    TakePending(0);
}

/*
 * F1 (render-design-bg.md 4.6): DISPSTAT bits 0-2 are read-only on the GBA
 * (GBATEK): VBlank on lines 160-226, HBlank during the HBlank step, VCount
 * while VCOUNT equals the compare value in bits 8-15. Bits 3-15 keep what the
 * game wrote. The VBlank flush writes the whole shadow DISPSTAT
 * (hardware.c:66), whose flag bits are 0, so the flags are set again at the
 * line start and after every IRQ handler. Without this, handlers that skip
 * while the VBlank flag is set (banim-efxlvup.c:263) ran at line 160 and moved
 * their effect up one line.
 */
static void SetDispstatFlags(int line, int hblank)
{
    u16 dispstat = REG_DISPSTAT & 0xFFF8;

    if (line >= 160 && line < 227)
        dispstat |= DISPSTAT_VBLANK;
    if (hblank)
        dispstat |= DISPSTAT_HBLANK;
    if (line == (dispstat >> 8))
        dispstat |= DISPSTAT_VCOUNT;
    REG_DISPSTAT = dispstat;
}

/*
 * F3 (render-design-bg.md 7.3 item 6, PLAN risk 11): FlushTiles has no bound
 * check against the 32-entry queue (hardware.c:977-999). Read-only check.
 */
static void CheckTileQueue(void)
{
    static uint32_t sHits;

    if (gFrameTmRegisterConfig.count > (int)ARRAY_COUNT(gFrameTmRegister) && sHits++ < 8)
        PortLog("[frame] frame %u: tile queue holds %d entries (limit %u)\n", gPortFrameCount,
            gFrameTmRegisterConfig.count, (unsigned)ARRAY_COUNT(gFrameTmRegister));
}

/* A line's start: VCOUNT, the flags and the VCount IRQ. Returns DISPSTAT as
   it was before that IRQ. */
static u16 LineStart(int line)
{
    u16 dispstat;

    REG_VCOUNT = (u16)line;
    SetDispstatFlags(line, 0);
#ifdef FE8PC_GATE0
    /* Test harness only, as in Irq: during a raster-timing redraw the GBA's
       stores due before this VCount IRQ check, which can be a REG_DISPSTAT
       store (its VCount IRQ enable or line, from OnVBlank's flush).
       PortGate0IrqDue does nothing outside a redraw; the SetDispstatFlags
       call after it runs on every line of this binary and sets what the
       line has already (idempotent). */
    PortGate0IrqDue(2);
    TakePending(0); /* P-25, as in Irq */
    /* P-25: the GBA's main loop can turn an IE bit on early in a line, before
       the line is drawn; the latched HBlank IRQ it takes at once is then the
       GBA's next line event (an HBlank event at this VCOUNT before the draw,
       which only a latched IRQ gives). The check lets that store be made
       here, so the redraw takes it at the same point. */
    PortGate0IrqDue(1);
    TakePending(0);
    SetDispstatFlags(line, 0);
#endif
    dispstat = REG_DISPSTAT;

    if ((dispstat & DISPSTAT_VCOUNT) && (dispstat & DISPSTAT_VCOUNT_INTR))
    {
        Irq(2);
        SetDispstatFlags(line, 0);
    }
    return dispstat;
}

/* A line's HBlank: the flag and the HBlank IRQ, whose writes reach line + 1. */
static void LineHBlank(int line)
{
    SetDispstatFlags(line, 1);
#ifdef FE8PC_GATE0
    /* Test harness only, as in Irq: the GBA's stores due before this HBlank
       IRQ check, which can be the REG_DISPSTAT store that turns the HBlank IRQ
       on (OnVBlank's flush, many lines after line 160 on the GBA after a slow
       VBlank proc tree). PortGate0IrqDue does nothing outside a redraw; the
       SetDispstatFlags call after it runs on every line, idempotent. */
    PortGate0IrqDue(1);
    TakePending(0); /* P-25, as in Irq */
    SetDispstatFlags(line, 1);
#endif
    if (REG_DISPSTAT & DISPSTAT_HBLANK_INTR)
        Irq(1);
    SetDispstatFlags(line, 0);
}

/*
 * One scanline, in mGBA's order (render-design-bg.md 4.1 and 4.3): VCount IRQ;
 * at line 160 the VBlank IRQ (OnVBlank and the flush); the line is drawn from
 * the values in effect now; then the HBlank IRQ, whose writes reach line + 1.
 */
static void Line(int line)
{
    u16 dispstat = LineStart(line);

    if (line == 160)
    {
        CheckTileQueue();
        PortAudio_PreVBlank(); /* tests: the per-VBlank sound record */
        if (dispstat & DISPSTAT_VBLANK_INTR)
        {
            Irq(0);
            SetDispstatFlags(line, 0);
        }
        /* OnVBlank has run the driver (m4aSoundVSync, m4aSoundMain): the FIFO
           model, PSG and DAC make this frame's 224 samples (port/audio,
           audio-design.md 1.4). Every frame step, even without the IRQ. */
        PortAudio_OnVBlank();
    }

    /* F2: the renderer (port/render/render.c). */
    if (line == 0)
        RenderBeginFrame();
    if (line < 160)
        RenderLine(line);
    if (line == 159)
        RenderEndFrame();

    LineHBlank(line);
}

#ifdef FE8PC_GATE0
/*
 * Test harness only: compiled into the Gate 0 test binary alone
 * (port/test/pc/gate0.mk defines FE8PC_GATE0 for this object; no shipped
 * build has these). Both run lines of the frame step for port/test/pc/
 * gate0_hook.c with the same line code as PortRunFrame (LineStart, Line,
 * LineHBlank above).
 *
 * PortLagFrameLines (the lag replay; port/test/README.md, "Lag replay"): the
 * line interrupts of one frame in which the GBA's main loop was still
 * running, a frame the PC never steps through. afterVBlank 0: lines 161-227
 * and 0-159 (VCount and HBlank IRQs, nothing drawn), then line 160 up to its
 * VBlank IRQ; the hook then runs that VBlank's OnVBlank itself, as the GBA
 * ran it. afterVBlank 1: the rest of line 160 (its HBlank IRQ). The order is
 * the frame step's, so the frame step that follows goes on from line 161 as
 * after any frame.
 *
 * The sound of that frame goes as in Line(160): PortAudio_PreVBlank (the
 * per-VBlank sound record) just before the VBlank IRQ, PortAudio_OnVBlank
 * (the FIFO model, PSG and DAC: the frame's 224 samples) after it. On the
 * GBA the FIFOs play through a lag frame like any other, so the lag replay
 * makes one sound frame per GBA VBlank (D4: the frame clock is master;
 * port/test/README.md, "Sound on any route"). Nothing the game reads changes.
 */
void PortLagFrameLines(int afterVBlank)
{
    int line;

    if (afterVBlank)
    {
        PortAudio_OnVBlank();
        LineHBlank(160);
        return;
    }
    for (line = 161; line < 228; line++)
    {
        LineStart(line);
        LineHBlank(line);
    }
    for (line = 0; line < 160; line++)
    {
        LineStart(line);
        LineHBlank(line);
    }
    LineStart(160);
    PortAudio_PreVBlank();
}

/*
 * PortRedrawFrameLines (the raster-timing redraw; port/test/README.md,
 * "Raster-timing frames"): lines 161-227 and 0-159 of a frame step once more,
 * drawn, exactly as PortRunFrame runs them (Line), without line 160 (no VBlank
 * IRQ, no sound, no frame count). The hook puts the machine back as it was
 * at the start of the frame step first, and back as it was after it then.
 */
void PortRedrawFrameLines(void)
{
    int line;

    for (line = 161; line < 228; line++)
        Line(line);
    for (line = 0; line < 160; line++)
        Line(line);
}
#endif

/*
 * Headless input for the spike (PLAN.md 3.7 headless backend, "keys from a
 * file", reduced to one rule): FE8PC_AUTOPRESS="mask,period,start[,hold]"
 * holds the active-high key mask (A 0x001, B 0x002, Select 0x004, Start 0x008,
 * ...) for `hold` frames (default 2) every `period` frames from frame `start`.
 */
static uint32_t sPressMask, sPressPeriod, sPressStart, sPressHold = 2;
static int sPressParsed;

/*
 * FE8PC_KEYSCRIPT="frame:mask,frame:mask,...": one-off presses, each mask
 * (as above) held 2 frames from its frame, for a short scripted run of any
 * build (a played build has no Gate 0 key file; port-plan/pending/
 * reforged-options-build.md). Up to 64 presses; held together with
 * FE8PC_AUTOPRESS. Unset: nothing changes.
 */
#define KEYSCRIPT_MAX 64
static uint32_t sScriptFrame[KEYSCRIPT_MAX], sScriptMask[KEYSCRIPT_MAX];
static int sScriptCount = -1;

static u16 KeyScriptHeld(void)
{
    u16 held = 0;
    int i;

    if (sScriptCount < 0)
    {
        const char *s = getenv("FE8PC_KEYSCRIPT");
        int n;

        sScriptCount = 0;
        while (s != NULL && *s != '\0' && sScriptCount < KEYSCRIPT_MAX &&
               sscanf(s, "%u:%i%n", &sScriptFrame[sScriptCount], (int *)&sScriptMask[sScriptCount], &n) >= 2)
        {
            sScriptCount++;
            s += n;
            if (*s == ',')
                s++;
        }
        if (sScriptCount > 0)
            PortLog("[keys] key script: %d presses, the last at frame %u\n", sScriptCount,
                sScriptFrame[sScriptCount - 1]);
    }
    for (i = 0; i < sScriptCount; i++)
        if (gPortFrameCount >= sScriptFrame[i] && gPortFrameCount - sScriptFrame[i] < 2)
            held |= (u16)sScriptMask[i];
    return held;
}
static u16 sAutoHeld; /* this frame step's FE8PC_AUTOPRESS keys */

/*
 * FE8PC_KEYLOG=1: log each change of the keys written to REG_KEYINPUT, and
 * each frame step in which the game's own UpdateKeyStatus (hardware.c:340,
 * called from OnMain) reported newly pressed keys, so a key press can be
 * followed from the platform to the game.
 */
static int sKeyLog = -1;
static u16 sLoggedHeld;

static int KeyLogOn(void)
{
    if (sKeyLog < 0)
    {
        const char *s = getenv("FE8PC_KEYLOG");
        sKeyLog = s != NULL && atoi(s) != 0;
    }
    return sKeyLog;
}

static void UpdateKeys(void)
{
    u16 held = 0;

    if (KeyLogOn() && gKeyStatusPtr->newKeys != 0)
        PortLog("[keys] frame %u: the game read newKeys 0x%03X (heldKeys 0x%03X)\n",
            gPortFrameCount, gKeyStatusPtr->newKeys, gKeyStatusPtr->heldKeys);

    if (!sPressParsed)
    {
        const char *s = getenv("FE8PC_AUTOPRESS");
        sPressParsed = 1;
        if (s != NULL && sscanf(s, "%i,%u,%u,%u", (int *)&sPressMask, &sPressPeriod, &sPressStart,
                             &sPressHold) >= 3 && sPressPeriod != 0)
            PortLog("[keys] auto-press 0x%03X every %u frames from frame %u, held %u frames\n",
                sPressMask, sPressPeriod, sPressStart, sPressHold);
        else
            sPressPeriod = 0;
    }
    if (sPressPeriod != 0 && gPortFrameCount >= sPressStart &&
        (gPortFrameCount - sPressStart) % sPressPeriod < sPressHold)
        held = (u16)sPressMask;
    held |= KeyScriptHeld();
    sAutoHeld = held;
    REG_KEYINPUT = (u16)(0x03FF & ~held); /* active low */
}

/*
 * Live keys (PLAN.md phase 4): the platform is polled once per frame step,
 * after the pacer's wait, so REG_KEYINPUT holds the newest keys when the
 * game's next OnMain reads them (timing-input.md: sample at the start of
 * OnMain). The game's own UpdateKeyStatus then applies the ignore mask and the
 * repeat timing; nothing of it is reimplemented here. The headless backend has
 * no live keys, so REG_KEYINPUT keeps the FE8PC_AUTOPRESS value.
 */
static void UpdateLiveKeys(void)
{
    u16 live, held;

    if (Platform_PollEvents())
    {
        PortLog("[frame] quit requested at frame %u\n", gPortFrameCount);
        PortExit(0);
    }
    live = Platform_ReadKeys() & PLATFORM_KEY_MASK;
    held = sAutoHeld | live;
    REG_KEYINPUT = (u16)(0x03FF & ~held); /* active low */
    if (KeyLogOn() && held != sLoggedHeld)
        PortLog("[keys] frame %u: REG_KEYINPUT 0x%03X (held 0x%03X, live 0x%03X)\n",
            gPortFrameCount, REG_KEYINPUT, held, live);
    sLoggedHeld = held;
}

void PortRunFrame(void)
{
    int line;

    UpdateKeys();
#ifdef FE8PC_GATE0
    gPortIfFrameStart = REG_IF;
#endif

    /* VBlank lines left over from the previous frame, then the visible lines,
       then line 160 with the VBlank IRQ. The game's next OnMain runs "right
       after OnVBlank", which is the split point the plan chose. */
    for (line = 161; line < 228; line++)
        Line(line);
    for (line = 0; line < 161; line++)
        Line(line);
#ifdef FE8PC_GATE0
    gPortIfOnMainStart = REG_IF;
    memset(gPortOnMainIrqIoMask, 0, sizeof(gPortOnMainIrqIoMask));
#endif

    /* The frame drawn on lines 0-159 is finished: show it (headless: no-op). */
    Platform_PresentFrame(gRenderFrameRgba, RENDER_FRAME_WIDTH, RENDER_FRAME_HEIGHT);

    gPortFrameCount++;

    /* The game's tick and this frame's IRQ handlers are done: a safe point to
       write the save file if SRAM changed (port/save/savefile_pc.c; nothing
       without a save file). */
    PortSave_Frame();
    if (gPortFrameLimit != 0 && gPortFrameCount >= gPortFrameLimit)
    {
        PortLog("[frame] frame limit %u reached\n", gPortFrameLimit);
        PortExit(0);
    }

    /* Hold the step to the GBA rate (off headless), then take the live keys. */
    PortPaceFrame();
    UpdateLiveKeys();
}

void VBlankIntrWait(void)
{
    int spins = 0;

    INTR_CHECK &= (u16)~INTR_FLAG_VBLANK;
    do
    {
        PortRunFrame();
        if (++spins == 4)
        {
            /* On hardware this would hang. Report it instead of spinning forever. */
            PortLog("[frame] VBlankIntrWait: no VBlank IRQ acknowledged after 4 frames "
                    "(IME=%u IE=0x%04X DISPSTAT=0x%04X)\n", REG_IME, REG_IE, REG_DISPSTAT);
            break;
        }
    } while (!(INTR_CHECK & INTR_FLAG_VBLANK));
}

#ifdef FE8PC_TESTBUILD
/*
 * Test builds only (FE8PC_TESTBUILD=1, port/Makefile; port-plan/DIVERGENCES.md TB-1):
 * the key sequence that opens the game's debug menu, Up Up Down Down Left
 * Right Left Right B A, one key per press. src/playerphase.c feeds it the newly
 * pressed keys once per OnMain while the player phase waits; it returns 1 when
 * they complete the sequence. A gap (a call missing in an OnMain: a menu, an
 * event, another phase) starts the sequence over. Kept in port code, so its
 * state is host memory, not a variable of the game files (those are placed at
 * GBA addresses, B-12), and a release build has no extra object.
 */
int PortTest_KonamiStep(u16 newKeys)
{
    static const u16 sequence[] = {
        DPAD_UP, DPAD_UP, DPAD_DOWN, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT, DPAD_LEFT, DPAD_RIGHT,
        B_BUTTON, A_BUTTON,
    };
    enum { LEN = sizeof(sequence) / sizeof(sequence[0]) };
    static u16 typed[LEN]; /* the latest presses, oldest first */
    static unsigned count;
    static uint32_t lastOnMain;
    unsigned i;

    if (gPortOnMainCalls != lastOnMain + 1)
        count = 0;
    lastOnMain = gPortOnMainCalls;

    newKeys &= 0x3FF;
    if (newKeys == 0)
        return 0;
    if (count == LEN)
    {
        for (i = 1; i < LEN; i++)
            typed[i - 1] = typed[i];
        count--;
    }
    typed[count++] = newKeys;
    if (count < LEN)
        return 0;
    for (i = 0; i < LEN; i++)
        if (typed[i] != sequence[i])
            return 0;
    count = 0;
    PortLog("[test] debug-menu key sequence typed (OnMain %u, frame %u)\n", gPortOnMainCalls,
        gPortFrameCount);
    return 1;
}
#endif
