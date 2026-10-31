#include "global.h"

/*
 * Replaces src/ramfunc.c. The GBA copies the ARM routines of arm.s into IWRAM
 * (0x03003750) and calls them through pointers. The host binds the same
 * pointers straight to the C versions in arm_pc.c (PLAN.md 3.2). The pointer
 * variables keep their GBA addresses (gen_host_link.py binds them).
 */

extern void (*DrawGlyphRAMFunc)(int, int, int, int);
extern void (*DecodeStringRAMFunc)(const char *, char *);
extern void (*PutOamHiRAMFunc)(int, int, const u16 *, int);
extern void (*PutOamLoRAMFunc)(int, int, const u16 *, int);
extern void (*MapFloodCoreStepRAMFunc)(int, int, int);
extern void (*MapFloodCoreRAMFunc)(void);

void DrawGlyph();
void DecodeString();
void PutOamHi(int x, int y, const u16 *list, int oam2);
void PutOamLo(int x, int y, const u16 *list, int oam2);
void MapFloodCoreStep();
void MapFloodCore();

void StoreRoutinesToIRAM(void)
{
    DrawGlyphRAMFunc = (void *)DrawGlyph;
    DecodeStringRAMFunc = (void *)DecodeString;
    PutOamHiRAMFunc = PutOamHi;
    PutOamLoRAMFunc = PutOamLo;
    MapFloodCoreStepRAMFunc = (void *)MapFloodCoreStep;
    MapFloodCoreRAMFunc = (void *)MapFloodCore;
}

void DrawGlyphRam(int a, int b, int c, int d)
{
    DrawGlyphRAMFunc(a, b, c, d);
}

void CallARM_DecompText(const char *a, char *b)
{
    DecodeStringRAMFunc(a, b);
}

void CallARM_PushToSecondaryOAM(int a, int b, const u16 *c, int d)
{
    PutOamHiRAMFunc(a, b, c, d);
}

void CallARM_PushToPrimaryOAM(int a, int b, const u16 *c, int d)
{
    PutOamLoRAMFunc(a, b, c, d);
}

void CallARM_Func5(int a, int b, int c)
{
    MapFloodCoreStepRAMFunc(a, b, c);
}

void CallARM_FillMovementMap(void)
{
    MapFloodCoreRAMFunc();
}
