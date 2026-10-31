#include "global.h"

#include "agb_sram.h"

/*
 * Replaces src/agb_sram.c. The GBA copies ReadSramFast_Core and
 * VerifySramFast_Core into RAM buffers and calls the copies. The host calls
 * the C functions directly. Same signatures and results; no REG_WAITCNT
 * writes (PLAN.md 3.2). Under D1-a SRAM sits at 0x0E000000, so a mismatch
 * address is never 0.
 */

const char AgbLibSramVersion[] = "SRAM_F_V103";

void ReadSramFast_Core(const u8 *src, u8 *dest, u32 size)
{
    while (--size != (u32)-1)
        *dest++ = *src++;
}

void WriteSramFast(const u8 *src, u8 *dest, u32 size)
{
    while (--size != (u32)-1)
        *dest++ = *src++;
}

u32 VerifySramFast_Core(const u8 *src, u8 *dest, u32 size)
{
    while (--size != (u32)-1)
    {
        if (*dest++ != *src++)
            return (u32)(uintptr_t)(dest - 1);
    }
    return 0;
}

void SetSramFastFunc(void)
{
    ReadSramFast = (void *)ReadSramFast_Core;
    VerifySramFast = (void *)VerifySramFast_Core;
}

u32 WriteAndVerifySramFast(void const *src, void *dest, u32 size)
{
    u8 i;
    u32 errorAddr = 0;

    for (i = 0; i < 3; i++)
    {
        WriteSramFast(src, dest, size);
        errorAddr = VerifySramFast(src, dest, size);
        if (errorAddr == 0)
            break;
    }
    return errorAddr;
}
