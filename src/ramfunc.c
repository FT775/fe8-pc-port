#include "global.h"

extern u8 RAMFuncArea[];  // buffer to copy the code to

// pointers to the loaded functions
extern void (*DrawGlyphRAMFunc)(int, int, int, int);
extern void (*DecodeStringRAMFunc)(const char *, char *);
extern void (*PutOamHiRAMFunc)(int, int, const u16 *, int);
extern void (*PutOamLoRAMFunc)(int, int, const u16 *, int);
extern void (*MapFloodCoreStepRAMFunc)(int, int, int);
extern void (*MapFloodCoreRAMFunc)(void);

// arm.s symbols
extern const u8 ARMCodeToCopy_Start[];
extern const u8 DrawGlyph[];
extern const u8 DecodeString[];
extern const u8 PutOamHi[];
extern const u8 PutOamLo[];
extern const u8 MapFloodCoreStep[];
extern const u8 MapFloodCore[];
extern const u8 ARMCodeToCopy_End[];

void StoreRoutinesToIRAM(void)
{
    size_t armCodeSize = (ARMCodeToCopy_End - ARMCodeToCopy_Start);

    // Copy all of the ARM code into RAM
    CpuCopy16(ARMCodeToCopy_Start, RAMFuncArea, armCodeSize + (armCodeSize >> 31));

    // Set pointers to each of the functions
    DrawGlyphRAMFunc = (void *)(RAMFuncArea + (DrawGlyph    - ARMCodeToCopy_Start));
    DecodeStringRAMFunc = (void *)(RAMFuncArea + (DecodeString - ARMCodeToCopy_Start));
    PutOamHiRAMFunc = (void *)(RAMFuncArea + (PutOamHi - ARMCodeToCopy_Start));
    PutOamLoRAMFunc = (void *)(RAMFuncArea + (PutOamLo   - ARMCodeToCopy_Start));
    MapFloodCoreStepRAMFunc = (void *)(RAMFuncArea + (MapFloodCoreStep              - ARMCodeToCopy_Start));
    MapFloodCoreRAMFunc = (void *)(RAMFuncArea + (MapFloodCore    - ARMCodeToCopy_Start));
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

void CallARM_FillMovementMap()
{
    MapFloodCoreRAMFunc();
}
