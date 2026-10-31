#ifndef GUARD_PORT_BIOS_MATH_H
#define GUARD_PORT_BIOS_MATH_H

/*
 * Port-only view of the BIOS arithmetic in port/bios/bios_math.c. The game
 * calls Div, DivRem, DivArm, Sqrt, ArcTan2, BgAffineSet and ObjAffineSet
 * through include/gba/syscall.h. These declarations expose the same routines
 * at the register level, with 32-bit inputs as the SWI receives them, so that
 * port/test/bios can check them against hardware test vectors. Nothing in
 * src/ uses this header.
 */

#include <stdint.h>

/* The registers SWI 6 (Div) and SWI 7 (DivArm) return. */
struct PortBiosDivRegs
{
    int32_t r0;  /* quotient, rounded toward zero */
    int32_t r1;  /* remainder, with the sign of the numerator */
    uint32_t r3; /* magnitude of the quotient */
};

/*
 * SWI 6 for num / denom. Returns 1 and fills *out when the BIOS returns.
 * Returns 0 when the BIOS never returns: denom 0 with num other than 0, 1 or
 * -1 (the BIOS loops forever). *out is not written then.
 */
int PortBiosDivRegs(int32_t num, int32_t denom, struct PortBiosDivRegs *out);

/* SWI 9 (ArcTan) for r0 = tan. Returns r0; *r1 and *r3 get the BIOS's r1 and r3. */
int32_t PortBiosArcTan(int32_t tan, int32_t *r1, int32_t *r3);

/* SWI 10 (ArcTan2) for r0 = x, r1 = y. Returns r0; *r1 gets the BIOS's r1. */
int32_t PortBiosArcTan2(int32_t x, int32_t y, int32_t *r1);

/* The affine sine table: entry i = trunc(sin(2 * pi * i / 256) * 0x4000). */
extern const int16_t gPortBiosSineTable[256];

#endif /* GUARD_PORT_BIOS_MATH_H */
