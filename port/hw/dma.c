#include "global.h"

#include "port_hw.h"

/*
 * Immediate DMA. port.h routes the DmaSet macro here. Transfers with start
 * timing "now" run at once, like hardware. VBlank, HBlank and special timing
 * (sound FIFO) are not modelled in the spike; they are logged once.
 */
void PortDmaSet(int dmaNum, u32 src, u32 dest, u32 control)
{
    static unsigned sDeferredLogged;
    vu32 *regs = (vu32 *)(uintptr_t)(REG_ADDR_DMA0 + 12 * dmaNum);
    u16 cnt = (u16)(control >> 16);
    u32 count = control & 0xFFFF;
    int unit, dstCtl, srcCtl, dstStep, srcStep;
    u32 i;

    regs[0] = src;
    regs[1] = dest;
    regs[2] = control;

    if (!(cnt & DMA_ENABLE))
        return;
    if ((cnt & DMA_START_MASK) != DMA_START_NOW)
    {
        if (sDeferredLogged++ < 4)
            PortLog("[dma] channel %d timed start 0x%04X ignored (not modelled)\n", dmaNum, cnt);
        return;
    }

    if (count == 0)
        count = dmaNum == 3 ? 0x10000 : 0x4000;
    unit = (cnt & DMA_32BIT) ? 4 : 2;
    dstCtl = (cnt >> 5) & 3;
    srcCtl = (cnt >> 7) & 3;
    dstStep = (dstCtl == 0 || dstCtl == 3) ? unit : (dstCtl == 1 ? -unit : 0);
    srcStep = srcCtl == 0 ? unit : (srcCtl == 1 ? -unit : 0);
    src &= ~(u32)(unit - 1);
    dest &= ~(u32)(unit - 1);

    for (i = 0; i < count; i++)
    {
        if (unit == 4)
            *(vu32 *)(uintptr_t)dest = *(vu32 *)(uintptr_t)src;
        else
            *(vu16 *)(uintptr_t)dest = *(vu16 *)(uintptr_t)src;
        dest += dstStep;
        src += srcStep;
    }

    if (!(cnt & DMA_REPEAT))
        regs[2] = control & 0x7FFFFFFF;
}
