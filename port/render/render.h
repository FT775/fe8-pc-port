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
 *   FE8PC_DUMP_WIDE=dir    with Widescreen on (below), also the same frames'
 *                          RENDER_WIDE_W x 160 pictures, as PPM files in dir
 *                          (same names; needs FE8PC_DUMP_FRAMES for the range).
 *                          The 240x160 dumps are never changed by it.
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

/* Widescreen (v1.25.0; the Display screen's row, port/reforged/reforged_display_menu.c, played builds).
   With it on, RenderEndFrame also puts each finished frame, centred, into a RENDER_WIDE_W x 160 picture
   with black sides (RENDER_WIDE_MARGIN pixels each), which the window shows (port/hw/frame.c) in place of
   the 240x160 one. The 240x160 frame, its hash and FE8PC_DUMP_FRAMES are the same with it on or off; with
   it off the wide picture is not touched. 284 x 160 is 16:9 within 0.2%. Session data, never game state. */
#define RENDER_WIDE_W      284
#define RENDER_WIDE_MARGIN ((RENDER_WIDE_W - RENDER_FRAME_WIDTH) / 2)
extern uint16_t gRenderWide555[RENDER_FRAME_HEIGHT][RENDER_WIDE_W];
extern uint32_t gRenderWideRgba[RENDER_FRAME_HEIGHT * RENDER_WIDE_W];
extern int gRenderWideOn; /* RenderSetWide's; 0 at start, kept by RenderInit (a soft reset) */
void RenderSetWide(int on);
/* v1.26.0 (milestone M1, played builds: port/Makefile's PORT_RFO_WIDE for render.o and wide.o): on the
   battle map the sides show more of the map (port/render/wide.c, from port/reforged/reforged_wide.c's
   gPortWide), and the game's 240 strip sits at x = gRenderWideOff (0 .. 2 x RENDER_WIDE_MARGIN; the
   margin itself on every other screen, whose sides stay black). gRenderWideMap: 1 when the last finished
   picture's sides show the map. Test switches (with FE8PC_DUMP_WIDE): FE8PC_DUMP_WIDE's directory also gets
   wide.log, one line per dumped frame: "frame off map reason"; FE8PC_WIDE_LOG=1 logs each change between
   the map sides and black sides with its reason. v1.28.0: wide.log's lines go on with "objs mus fogs hps
   flags" (RenderWideLogExtra, port/render/wide.c). */
extern int gRenderWideOff;
extern int gRenderWideMap;
struct RenderLineRegs;
struct RenderFxRegs;
void RenderWideReset(void);
void RenderWideBeginFrame(void);
void RenderWideLine(int line, const struct RenderLineRegs *regs, const struct RenderFxRegs *fx);
int RenderWideLineDone(int line);
int RenderWideFrameOff(int *map, int *reason);
void RenderWideLogExtra(char *buf, int size);

#endif /* GUARD_PORT_RENDER_RENDER_H */
