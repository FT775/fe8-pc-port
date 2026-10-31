#ifndef GUARD_PORT_RENDER_OBJ_LINE_H
#define GUARD_PORT_RENDER_OBJ_LINE_H

/*
 * Software renderer, sprite and effects half (port-plan/render-design-obj.md).
 * OBJ sprites, windows, the blend unit, forced blank, the backdrop and the
 * BGR555 to RGBA8888 output step (D3). Port-only host code, not compiled by
 * agbcc.
 *
 * This half reads memory only through struct RenderMem, so the same code runs
 * on the live D1-a images in the game (RenderMemLive) and on small synthetic
 * images in port/render/test_obj.c. It needs only <stdint.h>; it does not
 * include global.h or bg_line.h.
 *
 * Per visible line (0-159), after the BG half has filled its struct
 * RenderBgLine for that line, the integrator calls:
 *
 *     RenderObjFxLine(line, &fx, &mem, &bg, row);
 *
 * with fx copied from the shared snapshot (RENDER_FX_REGS_FROM) or NULL to
 * latch it here, bg copied from the BG line (RENDER_COMPOSE_BG_FROM) or NULL
 * for no BGs, and row = the 240-pixel BGR555 row of the frame buffer.
 * At the end of the frame: RenderFrameToRgba8888, RenderHashFrame555 and
 * RenderObjTakePeakCycles.
 */

#include <stddef.h>
#include <stdint.h>

#define RENDER_WIDTH 240
#define RENDER_HEIGHT 160

/* A BG pixel with colour index 0. Bit 15 is free in BGR555 (bg_line.h agrees). */
#ifndef RENDER_TRANSPARENT
#define RENDER_TRANSPARENT 0x8000
#endif

#define RENDER_WHITE 0x7FFF

/* Byte offsets into the GBA images. */
#define RENDER_OBJ_VRAM_OFFSET 0x10000 /* OBJ tiles in modes 0-2 */
#define RENDER_OBJ_VRAM_SIZE   0x8000
#define RENDER_OBJ_PLTT_OFFSET 0x200   /* OBJ palette, PLTT entry 256 */

/* Layer ids, also the bit numbers of the window masks and the BLDCNT targets. */
enum
{
    RENDER_LAYER_BG0,
    RENDER_LAYER_BG1,
    RENDER_LAYER_BG2,
    RENDER_LAYER_BG3,
    RENDER_LAYER_OBJ,
    RENDER_LAYER_BD, /* backdrop */
    RENDER_LAYER_NONE
};

#define RENDER_WIN_OBJ 0x10 /* window mask bit 4: OBJ shown */
#define RENDER_WIN_FX  0x20 /* window mask bit 5: effects on */
#define RENDER_WIN_ALL 0x3F

/* Blend arithmetic (design 7.6). */
enum
{
    RENDER_BLEND_HW,    /* 5-bit hardware math (GBATEK), the shipped default */
    RENDER_BLEND_MGBA8  /* desktop mGBA's 8-bit math, top 5 bits kept: checkpoint compares */
};

/*
 * Design 7.4: mGBA alpha-blends a semi-transparent OBJ even where the window
 * turns effects off. 1 follows mGBA. Flip it if hardware evidence turns up.
 */
#define RENDER_SEMI_OBJ_IGNORES_WIN_FX 1

/* GBA OBJ cycles per line (GBATEK "LCD OBJ - Overview"). */
#define RENDER_OBJ_CYCLES        1210
#define RENDER_OBJ_CYCLES_HBFREE 954 /* DISPCNT bit 5 */

/* Tripwires (design section 10). Each logs its first hit; none changes output. */
enum
{
    RENDER_TRIP_NONE,
    RENDER_TRIP_OBJ_8BPP,     /* T1 */
    RENDER_TRIP_OBJ_MODE3,    /* T2 */
    RENDER_TRIP_OBJ_SHAPE3,   /* T3 */
    RENDER_TRIP_MOSAIC,       /* T4 */
    RENDER_TRIP_DISPCNT,      /* T5: mode not 0 or 1, or bits 3-5 set */
    RENDER_TRIP_WINDOW,       /* T6: left > right or right > 240 on an enabled window */
    RENDER_TRIP_OBJ_WRAP,     /* T7: 2D column wrap, or OBJ tile address past 32 KB */
    RENDER_TRIP_OBJ_CYCLES,   /* T8: a sprite on the line was dropped by the cycle budget */
    RENDER_TRIP_EV,           /* T9: EVA, EVB or EVY above 16 */
    RENDER_TRIP_COUNT
};

/* The images this half reads. Each pointer is the start of the GBA region. */
struct RenderMem
{
    const uint8_t *io;   /* 0x04000000, at least 0x56 bytes */
    const uint8_t *pltt; /* 0x05000000, 1 KB */
    const uint8_t *vram; /* 0x06000000, 96 KB (OBJ tiles at +0x10000) */
    const uint8_t *oam;  /* 0x07000000, 1 KB */
};

/* The registers this half uses. Field names match struct RenderLineRegs. */
struct RenderFxRegs
{
    uint16_t dispcnt;
    uint16_t win0h;
    uint16_t win1h;
    uint16_t win0v;
    uint16_t win1v;
    uint16_t winin;
    uint16_t winout;
    uint16_t mosaic;
    uint16_t bldcnt;
    uint16_t bldalpha;
    uint16_t bldy;
};

/* Copy the fields above from a struct RenderLineRegs (bg_line.h). */
#define RENDER_FX_REGS_FROM(dst, src) \
    do \
    { \
        (dst)->dispcnt = (src)->dispcnt; \
        (dst)->win0h = (src)->win0h; \
        (dst)->win1h = (src)->win1h; \
        (dst)->win0v = (src)->win0v; \
        (dst)->win1v = (src)->win1v; \
        (dst)->winin = (src)->winin; \
        (dst)->winout = (src)->winout; \
        (dst)->mosaic = (src)->mosaic; \
        (dst)->bldcnt = (src)->bldcnt; \
        (dst)->bldalpha = (src)->bldalpha; \
        (dst)->bldy = (src)->bldy; \
    } while (0)

/* The four BG lines, as the compositor reads them. */
struct RenderComposeBg
{
    const uint16_t *color[4]; /* 240 BGR555 pixels, or RENDER_TRANSPARENT; may be NULL when off */
    uint8_t on[4];            /* drawn on this line */
    uint8_t prio[4];          /* BGnCNT bits 0-1 on this line */
};

/* Fill a struct RenderComposeBg from a struct RenderBgLine (bg_line.h). No copy of pixels. */
#define RENDER_COMPOSE_BG_FROM(dst, src) \
    do \
    { \
        int n_; \
        for (n_ = 0; n_ < 4; n_++) \
        { \
            (dst)->color[n_] = (src)->color[n_]; \
            (dst)->on[n_] = (src)->on[n_]; \
            (dst)->prio[n_] = (src)->prio[n_]; \
        } \
    } while (0)

/* One line of OBJ output (design 4.6). */
struct RenderObjLine
{
    uint16_t color[RENDER_WIDTH]; /* winning colour pixel, BGR555 */
    uint8_t prio[RENDER_WIDTH];   /* its priority, 0-3 */
    uint8_t has[RENDER_WIDTH];    /* 1 where a colour sprite pixel is opaque */
    uint8_t semi[RENDER_WIDTH];   /* 1 where the winner is semi-transparent (mode 1) */
    uint8_t win[RENDER_WIDTH];    /* 1 where an OBJ-window (mode 2) sprite is opaque */
    int cycles;                   /* OBJ cycles charged on this line */
};

/* Settings (tests and debug switches may change them). */
extern int gRenderBlendMode;         /* RENDER_BLEND_HW or RENDER_BLEND_MGBA8 */
extern int gRenderSemiIgnoresWinFx;  /* starts as RENDER_SEMI_OBJ_IGNORES_WIN_FX */
extern int gRenderObjCycleLimit;     /* 1: enforce the per-line OBJ cycle budget (mGBA does) */

/* Tripwire hook: called on the first hit of each id. NULL prints to stderr. */
extern void (*gRenderFxTripHook)(int id, int line, uint32_t detail);

/* obj_line.c */
void RenderMemLive(struct RenderMem *mem);
void RenderLatchFxRegs(const uint8_t *io, struct RenderFxRegs *fx);
void RenderObjLine(int line, uint16_t dispcnt, const struct RenderMem *mem, struct RenderObjLine *out);
void RenderObjFxLine(int line, const struct RenderFxRegs *fx, const struct RenderMem *mem,
    const struct RenderComposeBg *bg, uint16_t *dst);
uint32_t RenderObjTakePeakCycles(void);
void RenderFxTrip(int id, int line, uint32_t detail);
uint32_t RenderFxTripCount(int id);
void RenderFxTripReset(void);

/* window.c */
void RenderWindowLine(int line, const struct RenderFxRegs *fx, const uint8_t *objWin, uint8_t *mask);

/* blend.c */
int RenderClampEv(int v);
uint16_t RenderBlendAlpha(uint16_t a, uint16_t b, int eva, int evb);
uint16_t RenderBrighten(uint16_t c, int evy);
uint16_t RenderDarken(uint16_t c, int evy);
uint16_t RenderBlendAlphaMgba8(uint16_t a, uint16_t b, int eva, int evb);
uint16_t RenderBrightenMgba8(uint16_t c, int evy);
uint16_t RenderDarkenMgba8(uint16_t c, int evy);
void RenderComposeLine(int line, const struct RenderFxRegs *fx, const struct RenderComposeBg *bg,
    uint16_t backdrop, const struct RenderObjLine *obj, const uint8_t *mask, uint16_t *dst);

/* out_rgba.c */
unsigned RenderExpand5(unsigned v);
uint32_t RenderBgr555ToRgba8888(uint16_t c);
void RenderOutInit(void);
void RenderFrameToRgba8888(const uint16_t *src555, uint32_t *dst);
uint32_t RenderHashFrame555(const uint16_t *frame555);
int RenderDumpPpm555(const char *path, const uint16_t *frame555);

#endif /* GUARD_PORT_RENDER_OBJ_LINE_H */
