#include "global.h"

#include "bg_line.h"

/*
 * Software renderer, background half: affine BG2 in mode 1
 * (port-plan/render-design-bg.md section 6).
 *
 * Reference-point model (design 6.3), equivalent to mGBA 0.10.3 for FE8:
 * 1. Latch at the start of line 0 (RenderAffineBeginFrame): the raw BG2X/BG2Y
 *    values, sign-extended from 28 bits.
 * 2. Reload check at the start of lines 1-159 (RenderAffineCheckReload): a
 *    coordinate whose live raw value differs from the last latch is latched
 *    again, without a step. X and Y are checked on their own, because a write
 *    to one does not reset the other.
 * 3. Step at the end of lines 0-159 (RenderAffineStep): when the mode is not 0
 *    and DISPCNT bit 10 is set, add PB to X and PD to Y.
 * Maps are one byte per cell; tiles are always 8bpp with no palette bank.
 */

static s32 sAffineRefX, sAffineRefY; /* internal reference point, 19.8 fixed point */
static u32 sAffineRawX, sAffineRawY; /* raw BG2X/BG2Y at the last latch */

void RenderAffineInit(void)
{
    sAffineRefX = 0;
    sAffineRefY = 0;
    sAffineRawX = 0;
    sAffineRawY = 0;
}

void RenderAffineBeginFrame(void)
{
    sAffineRawX = REG_BG2X;
    sAffineRawY = REG_BG2Y;
    sAffineRefX = RenderSext28(sAffineRawX);
    sAffineRefY = RenderSext28(sAffineRawY);
}

void RenderAffineCheckReload(const struct RenderLineRegs *regs)
{
    if (regs->bg2x != sAffineRawX)
    {
        RenderLogOnce(RENDER_LOG_AFFINE_RELOAD, "BG2X changed during the visible lines (0x%08X -> 0x%08X): "
            "reference point reloaded", sAffineRawX, regs->bg2x);
        sAffineRawX = regs->bg2x;
        sAffineRefX = RenderSext28(regs->bg2x);
    }
    if (regs->bg2y != sAffineRawY)
    {
        RenderLogOnce(RENDER_LOG_AFFINE_RELOAD, "BG2Y changed during the visible lines (0x%08X -> 0x%08X): "
            "reference point reloaded", sAffineRawY, regs->bg2y);
        sAffineRawY = regs->bg2y;
        sAffineRefY = RenderSext28(regs->bg2y);
    }
}

void RenderAffineStep(const struct RenderLineRegs *regs)
{
    if ((regs->dispcnt & 7) != 0 && (regs->dispcnt & DISPCNT_BG2_ON))
    {
        sAffineRefX = (s32)((u32)sAffineRefX + (u32)(s32)(s16)regs->bg2pb);
        sAffineRefY = (s32)((u32)sAffineRefY + (u32)(s32)(s16)regs->bg2pd);
    }
}

void RenderAffineGetRef(s32 *refX, s32 *refY)
{
    if (refX != NULL)
        *refX = sAffineRefX;
    if (refY != NULL)
        *refY = sAffineRefY;
}

/* Affine BG2 for the current line (design 6.4). Pixel x samples the texture at
   ref + (PA, PC) * x, in 19.8 fixed point. */
void RenderAffineBgLine(const struct RenderLineRegs *regs, u16 *out)
{
    u16 cnt = regs->bgcnt[2];
    u32 charBase = ((cnt >> 2) & 3) * 0x4000u;
    u32 mapBase = ((cnt >> 8) & 0x1F) * 0x800u;
    s32 dim = 128 << (cnt >> 14);
    int wrap = (cnt & BGCNT_WRAP) != 0;
    u32 fx = (u32)sAffineRefX;
    u32 fy = (u32)sAffineRefY;
    u32 pa = (u32)(s32)(s16)regs->bg2pa;
    u32 pc = (u32)(s32)(s16)regs->bg2pc;
    int x;

    for (x = 0; x < RENDER_WIDTH; x++, fx += pa, fy += pc)
    {
        s32 px = (s32)fx >> 8; /* arithmetic shift */
        s32 py = (s32)fy >> 8;
        int tile, index;

        if (wrap)
        {
            px &= dim - 1;
            py &= dim - 1;
        }
        else if ((u32)px >= (u32)dim || (u32)py >= (u32)dim)
        {
            out[x] = RENDER_TRANSPARENT;
            continue;
        }
        tile = RENDER_VRAM8(mapBase + (u32)(py >> 3) * (u32)(dim >> 3) + (u32)(px >> 3));
        index = RenderFetch8bpp(charBase, tile, px & 7, py & 7);
        out[x] = index != 0 ? (RENDER_PLTT(index) & 0x7FFF) : RENDER_TRANSPARENT;
    }
}
