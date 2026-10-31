#include "global.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port_hw.h"
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
}

/* Line-0 work: the affine latch (BG design 6.3). Runs even in forced blank. */
void RenderBeginFrame(void)
{
    if (!gRenderEnabled)
        return;
    RenderBgBeginFrame();
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
}

void RenderEndFrame(void)
{
    if (!gRenderEnabled)
        return;
    gRenderFrameHash = RenderHashFrame555(&gRenderFrame555[0][0]);
    RenderFrameToRgba8888(&gRenderFrame555[0][0], gRenderFrameRgba);
    gRenderFramesDone++;

    if (sDumpOn && gPortFrameCount >= sDumpFirst && gPortFrameCount <= sDumpLast)
    {
        char path[512];

        snprintf(path, sizeof(path), "%s/frame%06u.ppm", sDumpDir, gPortFrameCount);
        if (RenderDumpPpm555(path, &gRenderFrame555[0][0]) != 0)
            PortLog("[render] cannot write %s\n", path);
    }
}
