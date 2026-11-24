#include "global.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_hw.h"
#include "port_state.h"
#include "bg_line.h"
#include "obj_line.h"
#include "render.h"

/*
 * Software renderer, top level: the per-line order the two halves agreed on
 * (port-plan/render-design-bg.md 1.2 and 4.3, render-design-obj.md 2 and 9).
 *
 * Per visible line: one register snapshot (RenderSnapshotRegs); the BG half
 * (RenderBgLine) unless DISPCNT forced blank is on; then the OBJ half, which
 * draws sprites, applies windows and blending, and writes the 240-pixel
 * BGR555 row (RenderObjFxLine). In forced blank the OBJ half writes a white
 * row and the BG half does not run, so the affine reference point does not
 * step on that line, as in mGBA 0.10 (design open question 11).
 *
 * The frame hash and the RGBA conversion come from out_rgba.c; there is no
 * second copy of either here.
 */

uint16_t gRenderFrame555[RENDER_FRAME_HEIGHT][RENDER_FRAME_WIDTH];
uint32_t gRenderFrameRgba[RENDER_FRAME_HEIGHT * RENDER_FRAME_WIDTH];
uint32_t gRenderFrameHash;
uint32_t gRenderFramesDone;
int gRenderEnabled = 1;
/* Widescreen (render.h): the pillarboxed picture, and whether it is made. */
uint16_t gRenderWide555[RENDER_FRAME_HEIGHT][RENDER_WIDE_W];
uint32_t gRenderWideRgba[RENDER_FRAME_HEIGHT * RENDER_WIDE_W];
int gRenderWideOn;
int gRenderWideOff = RENDER_WIDE_MARGIN; /* v1.26.0: the shown picture's x of the 240 strip (render.h) */
int gRenderWideMap;
static const char *sWideDumpDir; /* FE8PC_DUMP_WIDE, or NULL */
static FILE *sWideLog;           /* its wide.log (frame, off, map sides, reason) */

static struct RenderMem sMem;
static struct RenderBgLine sBgLine; /* 2 KB: kept off the stack */
static int sDumpOn;
static uint32_t sDumpFirst, sDumpLast;
static const char *sDumpDir = ".";

static void TripLog(int id, int line, uint32_t detail)
{
    PortLog("[render] tripwire T%d first hit: frame %u, line %d, detail 0x%X\n", id, gPortFrameCount,
        line, (unsigned)detail);
}

void RenderInit(void)
{
    const char *s;

    gRenderEnabled = 1;
    s = getenv("FE8PC_RENDER");
    if (s != NULL && strcmp(s, "0") == 0)
        gRenderEnabled = 0;

    gRenderBlendMode = RENDER_BLEND_HW;
    s = getenv("FE8PC_BLEND");
    if (s != NULL && strcmp(s, "mgba8") == 0)
        gRenderBlendMode = RENDER_BLEND_MGBA8;
    else if (s != NULL && strcmp(s, "hw") != 0)
        PortLog("[render] FE8PC_BLEND=%s not known (hw or mgba8): using hw\n", s);
    s = getenv("FE8PC_MGBA_BGCLIP");
    gRenderMgbaBgClip = s != NULL && strcmp(s, "1") == 0;

    sDumpOn = 0;
    s = getenv("FE8PC_DUMP_FRAMES");
    if (s != NULL)
    {
        unsigned a, b;
        int n = sscanf(s, "%u-%u", &a, &b);

        if (n >= 1)
        {
            sDumpFirst = a;
            sDumpLast = n == 2 ? b : a;
            sDumpOn = 1;
        }
        if (getenv("FE8PC_DUMP_DIR") != NULL)
            sDumpDir = getenv("FE8PC_DUMP_DIR");
    }
    s = getenv("FE8PC_DUMP_WIDE");
    sWideDumpDir = s != NULL && s[0] != '\0' ? s : NULL;

    gRenderFxTripHook = TripLog;
    RenderFxTripReset();
    RenderBgInit();
    RenderOutInit();
    RenderMemLive(&sMem);
    memset(gRenderFrame555, 0, sizeof(gRenderFrame555));
    memset(gRenderFrameRgba, 0, sizeof(gRenderFrameRgba));
    gRenderFrameHash = RenderHashFrame555(&gRenderFrame555[0][0]);
    gRenderFramesDone = 0;

    PortLog("[render] renderer %s, blend math %s%s%s\n", gRenderEnabled ? "on" : "off (FE8PC_RENDER=0)",
        gRenderBlendMode == RENDER_BLEND_MGBA8 ? "mGBA 8-bit (checkpoint compares only)" : "GBA hardware 5-bit",
        gRenderMgbaBgClip ? ", text BGs cut at window segments as mGBA does (checkpoint compares only)" : "",
        sDumpOn ? ", dumping frames" : "");
    if (sWideDumpDir != NULL)
        PortLog("[render] FE8PC_DUMP_WIDE: widescreen frames to %s (with Widescreen on, FE8PC_DUMP_FRAMES' range)\n",
            sWideDumpDir);
}

/* Widescreen (render.h): on or off from the next finished frame. Turning it on blacks the picture first, so
   the sides are black from the first frame shown. */
void RenderSetWide(int on)
{
    on = on != 0;
    if (on && !gRenderWideOn)
    {
        memset(gRenderWide555, 0, sizeof(gRenderWide555));
        RenderPixelsToRgba8888(&gRenderWide555[0][0], gRenderWideRgba, RENDER_FRAME_HEIGHT * RENDER_WIDE_W);
    }
    if (on != gRenderWideOn)
        PortLog("[render] widescreen %s (%dx%d)\n", on ? "on" : "off", on ? RENDER_WIDE_W : RENDER_FRAME_WIDTH,
            RENDER_FRAME_HEIGHT);
#if PORT_RFO_WIDE
    if (on != gRenderWideOn)
        RenderWideReset(); /* black sides until the next frame decides (wide.c) */
#endif
    gRenderWideOn = on;
}

/* The finished 240x160 frame copied into the wide picture at x = off. The sides are black (BGR555 0),
   except on the lines whose sides port/render/wide.c drew from the battle map (v1.26.0, played builds;
   off is then the frame's own, 0 .. 2 x margin). v1.25.0 (and every test build): off = the margin, black. */
static void RenderWideFrame(void)
{
    int off = RENDER_WIDE_MARGIN;
    int y;

#if PORT_RFO_WIDE
    off = RenderWideFrameOff(&gRenderWideMap, NULL);
#endif
    gRenderWideOff = off;
    for (y = 0; y < RENDER_FRAME_HEIGHT; y++)
    {
        uint16_t *row = gRenderWide555[y];

#if PORT_RFO_WIDE
        if (!RenderWideLineDone(y))
#endif
        {
            memset(row, 0, off * sizeof(row[0]));
            memset(row + off + RENDER_FRAME_WIDTH, 0, (RENDER_WIDE_W - off - RENDER_FRAME_WIDTH) * sizeof(row[0]));
        }
        memcpy(row + off, gRenderFrame555[y], sizeof(gRenderFrame555[y]));
    }
    RenderPixelsToRgba8888(&gRenderWide555[0][0], gRenderWideRgba, RENDER_FRAME_HEIGHT * RENDER_WIDE_W);
}

/* Line-0 work: the affine latch (BG design 6.3). Runs even in forced blank. */
void RenderBeginFrame(void)
{
    if (!gRenderEnabled)
        return;
    RenderBgBeginFrame();
#if PORT_RFO_WIDE
    if (gRenderWideOn)
        RenderWideBeginFrame(); /* v1.26.0: whether this frame's sides show the battle map (wide.c) */
#endif
}

void RenderLine(int line)
{
    struct RenderLineRegs regs;
    struct RenderFxRegs fx;
    struct RenderComposeBg bg;
    uint16_t *row;

    if (!gRenderEnabled || line < 0 || line >= RENDER_FRAME_HEIGHT)
        return;
    row = gRenderFrame555[line];

    RenderSnapshotRegs(&regs);
    RENDER_FX_REGS_FROM(&fx, &regs);
    if (regs.dispcnt & DISPCNT_FORCED_BLANK)
    {
        RenderObjFxLine(line, &fx, &sMem, NULL, row); /* white row */
        return;
    }
    RenderBgLine(line, &regs, &sBgLine);
    RENDER_COMPOSE_BG_FROM(&bg, &sBgLine);
    RenderObjFxLine(line, &fx, &sMem, &bg, row);
#if PORT_RFO_WIDE
    if (gRenderWideOn)
        RenderWideLine(line, &regs, &fx); /* v1.26.0: the sides of this line, same registers (wide.c) */
#endif
}

void RenderEndFrame(void)
{
    if (!gRenderEnabled)
        return;
    gRenderFrameHash = RenderHashFrame555(&gRenderFrame555[0][0]);
    RenderFrameToRgba8888(&gRenderFrame555[0][0], gRenderFrameRgba);
    gRenderFramesDone++;
    if (gRenderWideOn)
        RenderWideFrame(); /* (before the dump below may draw a test toast into the 240 frame) */

    if (sDumpOn && gPortFrameCount >= sDumpFirst && gPortFrameCount <= sDumpLast)
    {
        char path[512];
        const char *dir = sDumpDir;

#if PORT_STATE_ON
        /* save states' test switches: after a load, frames go to FE8PC_TEST_STATE_DUMP_DIR (a load puts
           the frame counter back, so they would replace the ones before); the toast only with
           FE8PC_TEST_STATE_TOAST_DUMP=1 (the hash above is taken first either way) */
        if (PortState_DumpDir() != NULL)
            dir = PortState_DumpDir();
        PortState_ToastDump555(&gRenderFrame555[0][0]);
#endif
        snprintf(path, sizeof(path), "%s/frame%06u.ppm", dir, gPortFrameCount);
        if (RenderDumpPpm555(path, &gRenderFrame555[0][0]) != 0)
            PortLog("[render] cannot write %s\n", path);
        if (gRenderWideOn && sWideDumpDir != NULL) /* FE8PC_DUMP_WIDE: the wide picture, same name */
        {
            snprintf(path, sizeof(path), "%s/frame%06u.ppm", sWideDumpDir, gPortFrameCount);
            if (RenderDumpPpm555Size(path, &gRenderWide555[0][0], RENDER_WIDE_W, RENDER_FRAME_HEIGHT) != 0)
                PortLog("[render] cannot write %s\n", path);
            /* v1.26.0: wide.log, "frame off map reason" per dumped frame (port/tools/widecheck.sh) */
            if (sWideLog == NULL)
            {
                snprintf(path, sizeof(path), "%s/wide.log", sWideDumpDir);
                sWideLog = fopen(path, "a");
            }
            if (sWideLog != NULL)
            {
                int reason = 0;
                char extra[64] = "";

#if PORT_RFO_WIDE
                RenderWideFrameOff(NULL, &reason);
                RenderWideLogExtra(extra, (int)sizeof(extra)); /* v1.28.0: " objs mus fogs hps flags" */
#endif
                fprintf(sWideLog, "%u %d %d %d%s\n", gPortFrameCount, gRenderWideOff, gRenderWideMap, reason, extra);
                fflush(sWideLog);
            }
        }
    }
}

/* Save states (port/include/port_state.h): the frame buffers are drawn whole every frame, and the rest
   is the renderer's settings (FE8PC_RENDER, FE8PC_DUMP_FRAMES / _DIR); none of it is game state. */
PORT_STATE_SESSION(gRenderFrame555)
PORT_STATE_SESSION(gRenderFrameRgba)
PORT_STATE_SESSION(gRenderFrameHash)
PORT_STATE_SESSION(gRenderFramesDone)
PORT_STATE_SESSION(gRenderEnabled)
PORT_STATE_SESSION(sDumpOn)
PORT_STATE_SESSION(sDumpFirst)
PORT_STATE_SESSION(sDumpLast)
PORT_STATE_SESSION(sDumpDir)
PORT_STATE_SESSION(gRenderWide555) /* widescreen: rebuilt every frame; the switch is a setting */
PORT_STATE_SESSION(gRenderWideRgba)
PORT_STATE_SESSION(gRenderWideOn)
PORT_STATE_SESSION(sWideDumpDir)
PORT_STATE_SESSION(gRenderWideOff)
PORT_STATE_SESSION(gRenderWideMap)
PORT_STATE_SESSION(sWideLog)
