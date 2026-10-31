#ifndef GUARD_PORT_RENDER_BG_LINE_H
#define GUARD_PORT_RENDER_BG_LINE_H

/*
 * Software renderer, background half (port-plan/render-design-bg.md).
 * Text BGs in modes 0 and 1, affine BG2 in mode 1, drawn one scanline at a
 * time from live I/O, BG VRAM and PLTT. Port-only code, not compiled by agbcc.
 *
 * Per-frame and per-line calls (design section 4.3):
 *   RenderBgInit()        at startup, and again after a soft reset.
 *   RenderBgBeginFrame()  at the start of line 0, before that line is drawn,
 *                         even when DISPCNT forced blank is on (affine latch).
 *   RenderSnapshotRegs()  once per visible line, then, unless forced blank,
 *   RenderBgLine()        once per visible line (0-159).
 *
 * struct RenderLineRegs and struct RenderBgLine are the interface from design
 * section 1.2. They are shared with the OBJ half.
 */

#include "gba/types.h"
#include "gba/defines.h"

#define RENDER_WIDTH  240
#define RENDER_HEIGHT 160

/* A BG pixel with colour index 0. Bit 15 is free in BGR555. */
#ifndef RENDER_TRANSPARENT
#define RENDER_TRANSPARENT 0x8000
#endif

/* Live BG VRAM and PLTT at their GBA addresses (D1-a). */
#define RENDER_VRAM8(off)  (((const u8 *)(uintptr_t)VRAM)[(off)])
#define RENDER_VRAM16(off) (((const u16 *)(uintptr_t)VRAM)[(off) >> 1])
#define RENDER_PLTT(i)     (((const u16 *)(uintptr_t)PLTT)[(i)])

/* One snapshot of I/O 0x00-0x55 per line. All fields are raw register values. */
struct RenderLineRegs
{
    u16 dispcnt;
    u16 bgcnt[4];
    u16 bghofs[4];
    u16 bgvofs[4];
    u16 bg2pa;
    u16 bg2pb;
    u16 bg2pc;
    u16 bg2pd;
    u32 bg2x; /* 28-bit signed 19.8 fixed point in bits 0-27 */
    u32 bg2y;
    u16 win0h;
    u16 win1h;
    u16 win0v;
    u16 win1v;
    u16 winin;
    u16 winout;
    u16 mosaic;
    u16 bldcnt;
    u16 bldalpha;
    u16 bldy;
};

/* What the BG half hands the OBJ half for one line. */
struct RenderBgLine
{
    u16 color[4][RENDER_WIDTH]; /* BGR555 masked to 15 bits, or RENDER_TRANSPARENT */
    u8 on[4];                   /* drawn on this line: DISPCNT bit 8+n and the mode allows it */
    u8 prio[4];                 /* BGnCNT bits 0-1 on this line */
    u16 backdrop;               /* PLTT[0] on this line, masked to 15 bits */
};

/* One-time warnings (design sections 5.1, 5.4 and 6.3). */
enum
{
    RENDER_LOG_MODE_UNUSED,     /* DISPCNT mode 2-7: no BGs drawn */
    RENDER_LOG_DISPCNT_CHANGE,  /* DISPCNT changed between lines 1 and 159 */
    RENDER_LOG_TEXT_8BPP,       /* text BG with BGnCNT bit 7 set */
    RENDER_LOG_TILE_PAST_VRAM,  /* text tile data at or past 0x10000: transparent */
    RENDER_LOG_MOSAIC,          /* MOSAIC non-zero while a drawn BG has bit 6 set */
    RENDER_LOG_AFFINE_RELOAD,   /* BG2X/BG2Y changed during the visible lines */
    RENDER_LOG_COUNT
};

/* bg_line.c */
void RenderBgInit(void);
void RenderBgBeginFrame(void);
void RenderSnapshotRegs(struct RenderLineRegs *regs);
s32 RenderSext28(u32 value);
void RenderBgLine(int line, const struct RenderLineRegs *regs, struct RenderBgLine *out);
int RenderFetch8bpp(u32 charBase, int tile, int px, int py);
void RenderSetLayerMask(u8 mask);
u8 RenderGetLayerMask(void);
void RenderLogOnce(int id, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
u32 RenderLogHits(int id);
/* 1: draw text BGs the way the oracle, mGBA 0.10.5, cuts them at window
   segments (FE8PC_MGBA_BGCLIP=1, Gate 0 compares only; bg_line.c). */
extern int gRenderMgbaBgClip;

/* bg_affine.c */
void RenderAffineInit(void);
void RenderAffineBeginFrame(void);
void RenderAffineCheckReload(const struct RenderLineRegs *regs);
void RenderAffineStep(const struct RenderLineRegs *regs);
void RenderAffineBgLine(const struct RenderLineRegs *regs, u16 *out);
void RenderAffineGetRef(s32 *refX, s32 *refY);

#endif /* GUARD_PORT_RENDER_BG_LINE_H */
