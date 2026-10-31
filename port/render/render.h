#ifndef GUARD_PORT_RENDER_RENDER_H
#define GUARD_PORT_RENDER_RENDER_H

/*
 * Software renderer, top level (port-plan/render-design-bg.md sections 4.3
 * and 9, render-design-obj.md section 9). It joins the BG half (bg_line.c,
 * bg_affine.c) and the OBJ half (obj_line.c, window.c, blend.c, out_rgba.c)
 * into the per-line step that port/hw/frame.c calls. Port-only code, not
 * compiled by agbcc.
 *
 * Calls from the frame step:
 *   RenderInit()        at startup and after a soft reset (main_pc.c).
 *   RenderBeginFrame()  at the start of line 0, before that line is drawn.
 *   RenderLine(line)    once per visible line (0-159), after the VCount and
 *                       VBlank IRQs of that line and before its HBlank IRQ.
 *   RenderEndFrame()    after line 159: hash, RGBA output, optional dumps.
 *
 * Environment (all optional):
 *   FE8PC_RENDER=0         skip drawing (logic-only runs; the frame stays black).
 *   FE8PC_BLEND=mgba8      mGBA's 8-bit blend math, for checkpoint compares
 *                          against mGBA only. Default: GBA hardware 5-bit math.
 *   FE8PC_RENDER_LAYERS=m  draw only the BGs in mask m (bg_line.c).
 *   FE8PC_DUMP_FRAMES=a-b  write frames a to b (gPortFrameCount) as PPM files
 *                          to FE8PC_DUMP_DIR (default "."): frameNNNNNN.ppm.
 */

#include <stdint.h>

#define RENDER_FRAME_WIDTH  240
#define RENDER_FRAME_HEIGHT 160

/* The finished frame in BGR555, bit 15 clear (the canonical output, D3). */
extern uint16_t gRenderFrame555[RENDER_FRAME_HEIGHT][RENDER_FRAME_WIDTH];
/* The same frame through the one D3 conversion, packed R<<24|G<<16|B<<8|A. */
extern uint32_t gRenderFrameRgba[RENDER_FRAME_HEIGHT * RENDER_FRAME_WIDTH];
/* FNV-1a 32 of gRenderFrame555 (RenderHashFrame555), set by RenderEndFrame. */
extern uint32_t gRenderFrameHash;
/* Frames completed since the last RenderInit. */
extern uint32_t gRenderFramesDone;
/* 0 when FE8PC_RENDER=0. */
extern int gRenderEnabled;

void RenderInit(void);
void RenderBeginFrame(void);
void RenderLine(int line);
void RenderEndFrame(void);

#endif /* GUARD_PORT_RENDER_RENDER_H */
