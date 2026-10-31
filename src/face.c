
#include "global.h"

#include "ctc.h"
#include "proc.h"
#include "hardware.h"
#include "rng.h"
#include "bm.h"
#include "bmlib.h"
#include "prepscreen.h"
#include "constants/faces.h"

#include "face.h"

struct FaceVramEntry EWRAM_DATA sFaceConfig[4] = { 0 };

struct FaceVramEntry CONST_DATA gDefaultFaceConfig[FACE_SLOT_COUNT] =
{
    [0] =
    {
        .tileOffset = 0x20 * 0x300,
        .paletteId  = 6,
    },

    [1] =
    {
        .tileOffset = 0x20 * 0x280,
        .paletteId  = 7,
    },

    [2] =
    {
        .tileOffset = 0x20 * 0x200,
        .paletteId  = 8,
    },

    [3] =
    {
        .tileOffset = 0x20 * 0x380,
        .paletteId  = 9,
    },
};

// face without shoulders (64x96)
u16 CONST_DATA gSprite_Face64x96[] =
{
    4,
    OAM0_SHAPE_64x32,              OAM1_SIZE_64x32 + OAM1_X(-32), OAM2_CHR(0x00),
    OAM0_SHAPE_64x32 + OAM0_Y(32), OAM1_SIZE_64x32 + OAM1_X(-32), OAM2_CHR(0x08),
    OAM0_SHAPE_32x16 + OAM0_Y(64), OAM1_SIZE_32x16 + OAM1_X(-32), OAM2_CHR(0x10),
    OAM0_SHAPE_32x16 + OAM0_Y(64), OAM1_SIZE_32x16,               OAM2_CHR(0x50),
};

// face without shoulders (64x96), flipped
u16 CONST_DATA gSprite_Face64x96_Flipped[] =
{
    4,
    OAM0_SHAPE_64x32,              OAM1_SIZE_64x32 + OAM1_HFLIP + OAM1_X(-32), OAM2_CHR(0x00),
    OAM0_SHAPE_64x32 + OAM0_Y(32), OAM1_SIZE_64x32 + OAM1_HFLIP + OAM1_X(-32), OAM2_CHR(0x08),
    OAM0_SHAPE_32x16 + OAM0_Y(64), OAM1_SIZE_32x16 + OAM1_HFLIP + OAM1_X(-32), OAM2_CHR(0x50),
    OAM0_SHAPE_32x16 + OAM0_Y(64), OAM1_SIZE_32x16 + OAM1_HFLIP,               OAM2_CHR(0x10),
};

// full face (96x96)
u16 CONST_DATA gSprite_Face96x96[] =
{
    6,
    OAM0_SHAPE_64x32,              OAM1_SIZE_64x32 + OAM1_X(-32), OAM2_CHR(0x00),
    OAM0_SHAPE_64x32 + OAM0_Y(32), OAM1_SIZE_64x32 + OAM1_X(-32), OAM2_CHR(0x08),
    OAM0_SHAPE_32x16 + OAM0_Y(64), OAM1_SIZE_32x16 + OAM1_X(-32), OAM2_CHR(0x10),
    OAM0_SHAPE_32x16 + OAM0_Y(64), OAM1_SIZE_32x16,               OAM2_CHR(0x50),
    OAM0_SHAPE_16x32 + OAM0_Y(48), OAM1_SIZE_16x32 + OAM1_X(-48), OAM2_CHR(0x14),
    OAM0_SHAPE_16x32 + OAM0_Y(48), OAM1_SIZE_16x32 + OAM1_X(+32), OAM2_CHR(0x16),
};

// full face (96x96), flipped
u16 CONST_DATA gSprite_Face96x96_Flipped[] =
{
    6,
    OAM0_SHAPE_64x32,              OAM1_SIZE_64x32 + OAM1_HFLIP + OAM1_X(-32), OAM2_CHR(0x00),
    OAM0_SHAPE_64x32 + OAM0_Y(32), OAM1_SIZE_64x32 + OAM1_HFLIP + OAM1_X(-32), OAM2_CHR(0x08),
    OAM0_SHAPE_32x16 + OAM0_Y(64), OAM1_SIZE_32x16 + OAM1_HFLIP + OAM1_X(-32), OAM2_CHR(0x50),
    OAM0_SHAPE_32x16 + OAM0_Y(64), OAM1_SIZE_32x16 + OAM1_HFLIP,               OAM2_CHR(0x10),
    OAM0_SHAPE_16x32 + OAM0_Y(48), OAM1_SIZE_16x32 + OAM1_HFLIP + OAM1_X(-48), OAM2_CHR(0x16),
    OAM0_SHAPE_16x32 + OAM0_Y(48), OAM1_SIZE_16x32 + OAM1_HFLIP + OAM1_X(+32), OAM2_CHR(0x14),
};

// smaller face (80x72)
u16 CONST_DATA gSprite_Face80x72[] =
{
    8,
    OAM0_SHAPE_64x32,              OAM1_SIZE_64x32 + OAM1_X(-32), OAM2_CHR(0x00),
    OAM0_SHAPE_64x32 + OAM0_Y(32), OAM1_SIZE_64x32 + OAM1_X(-32), OAM2_CHR(0x08),
    OAM0_SHAPE_32x8  + OAM0_Y(64), OAM1_SIZE_32x8  + OAM1_X(-32), OAM2_CHR(0x10),
    OAM0_SHAPE_32x8  + OAM0_Y(64), OAM1_SIZE_32x8,                OAM2_CHR(0x50),
    OAM0_SHAPE_8x16  + OAM0_Y(48), OAM1_SIZE_8x16  + OAM1_X(-40), OAM2_CHR(0x15),
    OAM0_SHAPE_8x16  + OAM0_Y(48), OAM1_SIZE_8x16  + OAM1_X(+32), OAM2_CHR(0x16),
    OAM0_SHAPE_8x8   + OAM0_Y(64), OAM1_SIZE_8x8   + OAM1_X(-40), OAM2_CHR(0x55),
    OAM0_SHAPE_8x8   + OAM0_Y(64), OAM1_SIZE_8x8   + OAM1_X(+32), OAM2_CHR(0x56),
};

// smaller face (80x72), flipped
u16 CONST_DATA gSprite_Face80x72_Flipped[] =
{
    8,
    OAM0_SHAPE_64x32,              OAM1_SIZE_64x32 + OAM1_HFLIP + OAM1_X(-32), OAM2_CHR(0x00),
    OAM0_SHAPE_64x32 + OAM0_Y(32), OAM1_SIZE_64x32 + OAM1_HFLIP + OAM1_X(-32), OAM2_CHR(0x08),
    OAM0_SHAPE_32x8  + OAM0_Y(64), OAM1_SIZE_32x8  + OAM1_HFLIP + OAM1_X(-32), OAM2_CHR(0x50),
    OAM0_SHAPE_32x8  + OAM0_Y(64), OAM1_SIZE_32x8  + OAM1_HFLIP,               OAM2_CHR(0x10),
    OAM0_SHAPE_8x16  + OAM0_Y(48), OAM1_SIZE_8x16  + OAM1_HFLIP + OAM1_X(-40), OAM2_CHR(0x16),
    OAM0_SHAPE_8x16  + OAM0_Y(48), OAM1_SIZE_8x16  + OAM1_HFLIP + OAM1_X(+32), OAM2_CHR(0x15),
    OAM0_SHAPE_8x8   + OAM0_Y(64), OAM1_SIZE_8x8   + OAM1_HFLIP + OAM1_X(-40), OAM2_CHR(0x56),
    OAM0_SHAPE_8x8   + OAM0_Y(64), OAM1_SIZE_8x8   + OAM1_HFLIP + OAM1_X(+32), OAM2_CHR(0x55),
};

// cut face (96x72)
u16 CONST_DATA gSprite_Face96x72[] =
{
    8,
    OAM0_SHAPE_64x32,              OAM1_SIZE_64x32 + OAM1_X(-32), OAM2_CHR(0x00),
    OAM0_SHAPE_64x32 + OAM0_Y(32), OAM1_SIZE_64x32 + OAM1_X(-32), OAM2_CHR(0x08),
    OAM0_SHAPE_32x8  + OAM0_Y(64), OAM1_SIZE_32x8  + OAM1_X(-32), OAM2_CHR(0x10),
    OAM0_SHAPE_32x8  + OAM0_Y(64), OAM1_SIZE_32x8,                OAM2_CHR(0x50),
    OAM0_SHAPE_16x16 + OAM0_Y(48), OAM1_SIZE_16x16 + OAM1_X(-48), OAM2_CHR(0x14),
    OAM0_SHAPE_16x16 + OAM0_Y(48), OAM1_SIZE_16x16 + OAM1_X(+32), OAM2_CHR(0x16),
    OAM0_SHAPE_16x8  + OAM0_Y(64), OAM1_SIZE_16x8  + OAM1_X(-48), OAM2_CHR(0x54),
    OAM0_SHAPE_16x8  + OAM0_Y(64), OAM1_SIZE_16x8  + OAM1_X(+32), OAM2_CHR(0x56),
};

// cut face (96x72), flipped
u16 CONST_DATA gSprite_Face96x72_Flipped[] =
{
    8,
    OAM0_SHAPE_64x32,              OAM1_SIZE_64x32 + OAM1_HFLIP + OAM1_X(-32), OAM2_CHR(0x00),
    OAM0_SHAPE_64x32 + OAM0_Y(32), OAM1_SIZE_64x32 + OAM1_HFLIP + OAM1_X(-32), OAM2_CHR(0x08),
    OAM0_SHAPE_32x8  + OAM0_Y(64), OAM1_SIZE_32x8  + OAM1_HFLIP + OAM1_X(-32), OAM2_CHR(0x50),
    OAM0_SHAPE_32x8  + OAM0_Y(64), OAM1_SIZE_32x8  + OAM1_HFLIP,               OAM2_CHR(0x10),
    OAM0_SHAPE_16x16 + OAM0_Y(48), OAM1_SIZE_16x16 + OAM1_HFLIP + OAM1_X(-48), OAM2_CHR(0x16),
    OAM0_SHAPE_16x16 + OAM0_Y(48), OAM1_SIZE_16x16 + OAM1_HFLIP + OAM1_X(+32), OAM2_CHR(0x14),
    OAM0_SHAPE_16x8  + OAM0_Y(64), OAM1_SIZE_16x8  + OAM1_HFLIP + OAM1_X(-48), OAM2_CHR(0x56),
    OAM0_SHAPE_16x8  + OAM0_Y(64), OAM1_SIZE_16x8  + OAM1_HFLIP + OAM1_X(+32), OAM2_CHR(0x54),
};

// face proc
struct ProcCmd CONST_DATA gProcScr_E_FACE[] =
{
    PROC_NAME("E_FACE"),

    PROC_WHILE_EXISTS(ProcScr_CamMove),
    PROC_SLEEP(0),

    PROC_CALL(Face_OnInit),

    PROC_CALL(Face_OnIdle),
    PROC_REPEAT(Face_OnIdle),

    PROC_END,
};

// other face proc. This one waits an extra frame before displaying anything
struct ProcCmd CONST_DATA gProcScr_E_FACE_ExtraFrame[] =
{
    PROC_NAME("E_FACE"),

    PROC_WHILE_EXISTS(ProcScr_CamMove),
    PROC_SLEEP(1),

    PROC_CALL(Face_OnInit),

    PROC_CALL(Face_OnIdle),
    PROC_REPEAT(Face_OnIdle),

    PROC_END,
};

// unk
u8 CONST_DATA gFaceTm_Chibi[] =
{
    4, 4,
    0x00, 0x01, 0x02, 0x03,
    0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B,
    0x0C, 0x0D, 0x0E, 0x0F,
};

struct ProcCmd CONST_DATA gProcScr_FaceChibiSpr[] =
{
    PROC_REPEAT(FaceChibiSpr_OnIdle),
    PROC_END,
};

u16 CONST_DATA gSprite_FaceChibi[] =
{
    2,
    OAM0_SHAPE_32x16,              OAM1_SIZE_32x16, OAM2_CHR(0),
    OAM0_SHAPE_32x16 + OAM0_Y(16), OAM1_SIZE_32x16, OAM2_CHR(4),
};

u16 CONST_DATA gSprite_FaceChibi_Flipped[] =
{
    2,
    OAM0_SHAPE_32x16,              OAM1_SIZE_32x16 + OAM1_HFLIP, OAM2_CHR(0),
    OAM0_SHAPE_32x16 + OAM0_Y(16), OAM1_SIZE_32x16 + OAM1_HFLIP, OAM2_CHR(4),
};

struct ProcCmd CONST_DATA gProcScr_BgFaceBlink[] =
{
    PROC_CALL(BgFaceBlink_Init),

PROC_LABEL(0),
    PROC_REPEAT(BgFaceBlink_Delay),
    PROC_REPEAT(BgFaceBlink_PutFace),

    PROC_GOTO(0),

    PROC_END,
};

struct ProcCmd CONST_DATA gProcScr_FaceEndIn8Frames[] =
{
    PROC_SLEEP(8),
    PROC_CALL(EndFacePtr),

    PROC_END,
};

struct ProcCmd CONST_DATA gProcScr_FaceMouth[] =
{
    PROC_CALL(FaceMouth_Init),
    PROC_REPEAT(FaceMouth_Loop),

    PROC_END,
};

struct ProcCmd CONST_DATA gProcScr_FaceBlink[] =
{
PROC_LABEL(0),
    PROC_CALL(FaceBlink_Init),

PROC_LABEL(0),
    PROC_REPEAT(FaceBlink_Delay),

PROC_LABEL(1),
    PROC_REPEAT(FaceBlink_PreSwitch),

PROC_LABEL(2),
    PROC_CALL(FaceBlink_InitDisplayFrame0),
    PROC_REPEAT(FaceBlink_DisplayFrame0),
    PROC_REPEAT(FaceBlink_Delay),

PROC_LABEL(3),
    PROC_CALL(FaceBlink_InitDisplayFrame1),
    PROC_REPEAT(FaceBlink_DisplayFrame1),
    PROC_REPEAT(FaceBlink_Delay),

PROC_LABEL(4),
    PROC_CALL(FaceBlink_InitDisplayFrameFlip),
    PROC_REPEAT(FaceBlink_DisplayFrameFlip),

PROC_LABEL(97),
    PROC_REPEAT(FaceBlink_DisplayFrame0),

    PROC_END,
};

struct ProcCmd CONST_DATA gProcScr_FaceChange[] =
{
    PROC_SLEEP(0),
    PROC_CALL(FaceChange_LockChildProcs),

    PROC_SLEEP(2),
    PROC_CALL(FaceChange_LoadGfx),

    PROC_SLEEP(0),
    PROC_CALL(FaceChange_UnlockChildProcs),

    PROC_END,
};

//! FE8U = 0x08005514
const struct FaceData* GetPortraitData(int fid) {
    return portrait_data + fid - 1;
}

//! FE8U = 0x08005528
void ResetFaces(void) {
    int i;

    for (i = 0; i < FACE_SLOT_COUNT; i++) {
        EndFaceById(i);
    }

    SetupFaceGfxData(0);

    return;
}

//! FE8U = 0x08005544
void SetupFaceGfxData(struct FaceVramEntry* config) {
    int i;

    if (config == 0) {
        config = gDefaultFaceConfig;
    }

    for (i = 0; i < FACE_SLOT_COUNT; i++) {
        sFaceConfig[i].tileOffset = config[i].tileOffset;
        sFaceConfig[i].paletteId = config[i].paletteId;
    }

    return;
}

//! FE8U = 0x08005570
int FindFreeFaceSlot(void) {
    int i;

    for (i = 0; i < FACE_SLOT_COUNT; i++) {
        if (gFaces[i] == NULL) {
            return i;
        }
    }

    return -1;
}

//! FE8U = 0x08005594
void Face_OnInit(struct FaceProc* proc) {
    Decompress(proc->pFaceInfo->img, (void *)(sFaceConfig[proc->faceSlot].tileOffset + 0x06010000));
    return;
}

//! FE8U = 0x080055BC
void Face_OnIdle(struct FaceProc* proc) {
    int oam0;

    if (GetFaceDisplayBits(proc) & FACE_DISP_HIDDEN) {
        return;
    }

    if (GetFaceDisplayBits(proc) & FACE_DISP_BLEND) {
        oam0 = OAM0_BLEND;
    } else {
        oam0 = 0;
    }

    oam0 += OAM0_Y(proc->yPos);

    PutSpriteExt(
        proc->spriteLayer,
        0x1FF & proc->xPos,
        oam0,
        proc->sprite,
        proc->oam2
    );

    return;
}

//! FE8U = 0x08005610
struct FaceProc* StartFaceAuto(int fid, int x, int y, int disp) {

    int slot = FindFreeFaceSlot();

    if (slot < 0) {
        return NULL;
    }

    return StartFace(slot, fid, x, y, disp);
}

//! FE8U = 0x0800563C
struct FaceProc* StartFace(int slot, int fid, int x, int y, int disp) {
    struct FaceProc* proc;
    const struct FaceData* info;

    if (gFaces[slot] != NULL) {
        return NULL;
    }

    proc = Proc_Start(gProcScr_E_FACE, PROC_TREE_5);

    gFaces[slot] = proc;

    info = GetPortraitData(fid);

    if (disp & FACE_DISP_BIT_13) {
        CpuFastFill(0, PAL_OBJ(0) + PAL_OFFSET(sFaceConfig[slot].paletteId), 0x20);
        EnablePaletteSync();
    } else {
        ApplyPalette(info->pal, sFaceConfig[slot].paletteId + 0x10);
    }

    proc->pFaceInfo = info;

    proc->faceSlot = slot;
    proc->faceId = fid;

    proc->spriteLayer = 5;

    proc->xPos = x;
    proc->yPos = y;

    if (disp & FACE_DISP_BIT_12) {
        proc->unk_44 = NULL;
        proc->pBlinkProc = NULL;
    } else {
        proc->unk_44 = Proc_Start(gProcScr_FaceMouth, proc);
        proc->pBlinkProc = Proc_Start(gProcScr_FaceBlink, proc);
    }

    proc->displayBits = ~disp;

    SetFaceDisplayBits(proc, disp);

    return proc;
}

//! FE8U = 0x08005738
void EndFace(struct FaceProc* proc) {
#ifdef PC_PORT
    /* D-3: with proc NULL the GBA's ldrb of 0x40 (0x08005740) reads open-bus lane 0 of the
       BIOS latch (PORT_NULL_READ by the rule): 4 post-SWI (0xE3A02004), 2 post-IRQ
       (0xE55EC002). The GBA then stores NULL to gFaces[faceSlot] (0x08005748): slot 4 is one
       past the array, 0x03004990, which is gpBgVerticalScrollSt (the host places both symbols
       at their GBA addresses, D1-a, so this is the same store); slot 2 is gFaces[2].
       Measured (mGBA, the 39 route files and w_i10, port-plan/pending/folded/r10-faces.md): 35,240
       calls (EndFaceById, EndFacePtr); slot 4 at 34,985 (no HBlank IRQ), slot 2 at 208 with
       the HBlank IRQ on, as the rule gives, and slot 2 at 47 without it (a VBlank IRQ in a
       lag frame came after the last SWI, which the rule does not model: these get slot 4).
       gFaces[2] and gpBgVerticalScrollSt were NULL at every slot-2 call, so no route
       changes (integration 11, D-3's round-10 amendment, was working ID D-R10-FACES-4; before, always slot 4).
       Proc_End(NULL) (0x08002D6C) does nothing. See DIVERGENCES.md D-3. */
    if (proc == NULL) {
        *(struct FaceProc**)((uintptr_t)gFaces + (u32)PORT_NULL_READ(proc, faceSlot) * sizeof(gFaces[0])) = NULL;
        return;
    }
#endif
    gFaces[proc->faceSlot] = NULL;
    Proc_End(proc);

    return;
}

//! FE8U = 0x08005758
void EndFaceById(int slot) {
    EndFace(gFaces[slot]);

    return;
}

//! FE8U = 0x08005770
int SetFaceDisplayBits(struct FaceProc* proc, int disp) {

    if (proc == 0) {
        return 0;
    }

    proc->displayBits = disp;

    FaceRefreshSprite(proc);

    return proc->displayBits;
}

//! FE8U = 0x0800578C
int SetFaceDisplayBitsById(int slot, int disp) {
    return SetFaceDisplayBits(gFaces[slot], disp);
}

//! FE8U = 0x080057A4
int GetFaceDisplayBits(struct FaceProc* proc) {
#ifdef PC_PORT
    /*
     * A face slot with no face (port-plan/DIVERGENCES.md, D-70, was working ID D-R10-ENDING-1): the
     * GBA's ldr r0, [r0, #0x30] (0x080057A4) with proc NULL loads the BIOS open-bus latch
     * word, which PORT_NULL_READ gives by the latch rule. Measured (mGBA, route_bend, the
     * ending's CG text scene MSG 0xC22, which has no face): 0xE55EC002 at 378 of the 379
     * reads from GetFaceDisplayBitsById (CgTextInterpreter_Loop_Main and CgText_OnEnd, the
     * CG text's HBlank IRQ on); at CgText_InitFadeOut 0xE3A02004 on route_bend and route_b22
     * (a SWI just before) or 0xE55EC002 (route_b21, all 379 of 379: an HBlank IRQ after
     * CgText_ClearSpriteText's BIOS copy). Every
     * such caller hands the value to SetFaceDisplayBitsById on the same empty slot, which
     * returns 0 for a NULL face (0x08005774), so it is dropped for any latch.
     */
    return PORT_NULL_READ(proc, displayBits);
#else
    return proc->displayBits;
#endif
}

//! FE8U = 0x080057A8
int GetFaceDisplayBitsById(int slot) {
    return GetFaceDisplayBits(gFaces[slot]);
}

//! FE8U = 0x080057C0
void FaceRefreshSprite(struct FaceProc* proc) {
    int oam2Layer;

    switch (proc->displayBits & 0x807) {
        case 0:
            proc->sprite = gSprite_Face64x96;
            break;

        case 1:
            proc->sprite = gSprite_Face64x96_Flipped;
            break;

        case 2:
            proc->sprite = gSprite_Face96x96;
            break;

        case 3:
            proc->sprite = gSprite_Face96x96_Flipped;
            break;

        case 4:
            proc->sprite = gSprite_Face80x72;
            break;

        case 5:
            proc->sprite = gSprite_Face80x72_Flipped;
            break;

        case 0x800:
            proc->sprite = gSprite_Face96x72;
            break;

        case 0x801:
            proc->sprite = gSprite_Face96x72_Flipped;
            break;
    }

    switch (proc->displayBits & FACE_DISP_HLAYER_MASK) {
        case FACE_DISP_HLAYER(FACE_HLAYER_0):
            oam2Layer = OAM2_LAYER(0);
            break;

        case FACE_DISP_HLAYER(FACE_HLAYER_1):
            oam2Layer = OAM2_LAYER(1);
            break;

        case FACE_DISP_HLAYER(FACE_HLAYER_3):
            oam2Layer = OAM2_LAYER(3);
            break;

        default:
            oam2Layer = OAM2_LAYER(2);
            break;
    }

    proc->oam2 = (sFaceConfig[proc->faceSlot].tileOffset / CHR_SIZE) + ((sFaceConfig[proc->faceSlot].paletteId & 0xF) * 0x1000) + oam2Layer;

    return;
}

//! FE8U = 0x08005894
void PutFaceTm(u16 * tm, u8* data, int tileref, s8 isFlipped) {
    int ix;
    int iy;

    int width = *data++;
    int height = *data++;

    u8* it = data;

    if (isFlipped == 0) {

        for (iy = 0; iy < height; iy++) {
            for (ix = 0; ix < width; ix++) {

                if (*it == 0xFF) {
                    it++;
                    continue;
                }

                tm[TILEMAP_INDEX(ix, iy)] = *it++ + tileref;
            }
        }
    } else {
        for (iy = 0; iy < height; iy++) {
            for (ix = width - 1; ix >= 0; ix--) {

                if (*it == 0xFF) {
                    it++;
                    continue;
                }

                tm[TILEMAP_INDEX(ix, iy)] = *it++ + tileref + 0x400;
            }
        }
    }

    return;
}

//! FE8U = 0x08005924
void UnpackFaceChibiGraphics(int fid, int chr, int pal) {
    if (fid >= FID_FACTION_CHIBI) {
        RegisterDataMove(GetFactionFaceImg(fid), (void *)(((chr * CHR_SIZE + VRAM) & 0x1FFFF) + VRAM), 0x200);
        ApplyFactionFacePal(fid, pal);
    } else {
        const struct FaceData* info = GetPortraitData(fid);

        Decompress(info->imgChibi, (void *)(chr * CHR_SIZE + VRAM));
        ApplyPalette(info->pal, pal);
    }
    return;
}

//! FE8U = 0x08005988
void PutFaceChibi(int fid, u16 * tm, int chr, int pal, s8 isFlipped) {
    UnpackFaceChibiGraphics(fid, chr, pal);

    chr &= 0x3FF;
    PutFaceTm(tm, gFaceTm_Chibi, TILEREF(chr, pal), isFlipped);

    return;
}

//! FE8U = 0x080059CC
void UnpackFaceChibiSprGraphics(int fid, int chr, int pal) {
    u8 buffer[0x200];

    chr += 0x800;

    if (fid >= FID_FACTION_CHIBI) {
        RegisterDataMove(GetFactionFaceImg(fid) + 0x00, (void *)(((chr + 0x00) * 0x20 & 0x1FFFF) + VRAM), 0x80);
        RegisterDataMove(GetFactionFaceImg(fid) + 0x80, (void *)(((chr + 0x20) * 0x20 & 0x1FFFF) + VRAM), 0x80);
        RegisterDataMove(GetFactionFaceImg(fid) + 0x100, (void *)(((chr + 0x04) * 0x20 & 0x1FFFF) + VRAM), 0x80);
        RegisterDataMove(GetFactionFaceImg(fid) + 0x180, (void *)(((chr + 0x24) * 0x20 & 0x1FFFF) + VRAM), 0x80);

        ApplyFactionFacePal(fid, pal + 0x10);
    } else {
        const struct FaceData* info = GetPortraitData(fid);

        Decompress(info->imgChibi, buffer);

        CpuFastCopy(buffer + 0x00, (void *)((chr + 0x00) * 0x20 + VRAM), 0x80);
        CpuFastCopy(buffer + 0x80, (void *)((chr + 0x20) * 0x20 + VRAM), 0x80);
        CpuFastCopy(buffer + 0x100, (void *)((chr + 0x04) * 0x20 + VRAM), 0x80);
        CpuFastCopy(buffer + 0x180, (void *)((chr + 0x24) * 0x20 + VRAM), 0x80);

        ApplyPalette(info->pal, pal + 0x10);
    }

    return;
}

//! FE8U = 0x08005AD4
void FaceChibiSpr_OnIdle(struct FaceProc* proc) {
    PutSprite(
        5,
        proc->xPos - gLCDControlBuffer.bgoffset[0].x,
        proc->yPos - gLCDControlBuffer.bgoffset[0].y,
        proc->sprite,
        proc->oam2
    );

    return;
}

//! FE8U = 0x08005B04
void StartFaceChibiSpr(int x, int y, int fid, int chr, int pal, s8 isFlipped, ProcPtr parent) {
    struct FaceProc* proc;

    UnpackFaceChibiSprGraphics(fid, chr, pal);

    proc = Proc_Start(gProcScr_FaceChibiSpr, parent);

    proc->xPos = x;
    proc->yPos = y;

    proc->oam2 = chr + ((pal & 0xF) * 0x1000);

    if (isFlipped) {
       proc->sprite = gSprite_FaceChibi_Flipped;
    } else {
       proc->sprite = gSprite_FaceChibi;
    }

    return;
}

//! FE8U = 0x08005B68
void EndFaceChibiSpr(void) {
    Proc_EndEach(gProcScr_FaceChibiSpr);

    return;
}

//! FE8U = 0x08005B78
void PutFace80x72_Standard(u16 * tm, int tileref, const struct FaceData* info) {
    int x = info->xMouth - 1;
    int y = info->yMouth;

    CallARM_FillTileRect(tm, gTSA_Face80x72_Standard, (u16)tileref);

    tm[TILEMAP_INDEX(x, y) + 0x00 + 0] = tileref + 0x00 + 0x1C;
    tm[TILEMAP_INDEX(x, y) + 0x00 + 1] = tileref + 0x00 + 0x1D;
    tm[TILEMAP_INDEX(x, y) + 0x00 + 2] = tileref + 0x00 + 0x1E;
    tm[TILEMAP_INDEX(x, y) + 0x00 + 3] = tileref + 0x00 + 0x1F;

    tm[TILEMAP_INDEX(x, y) + 0x20 + 0] = tileref + 0x20 + 0x1C;
    tm[TILEMAP_INDEX(x, y) + 0x20 + 1] = tileref + 0x20 + 0x1D;
    tm[TILEMAP_INDEX(x, y) + 0x20 + 2] = tileref + 0x20 + 0x1E;
    tm[TILEMAP_INDEX(x, y) + 0x20 + 3] = tileref + 0x20 + 0x1F;

    return;
}

//! FE8U = 0x08005BCC
void PutFace80x72_Raised(u16 * tm, int tileref, const struct FaceData* info) {
    int x = info->xMouth - 1;
    int y = info->yMouth - 1;

    CallARM_FillTileRect(tm, gTSA_Face80x72_Raised, (u16)tileref);

    tm[TILEMAP_INDEX(x, y) + 0x00 + 0] = tileref + 0x00 + 0x1C;
    tm[TILEMAP_INDEX(x, y) + 0x00 + 1] = tileref + 0x00 + 0x1D;
    tm[TILEMAP_INDEX(x, y) + 0x00 + 2] = tileref + 0x00 + 0x1E;
    tm[TILEMAP_INDEX(x, y) + 0x00 + 3] = tileref + 0x00 + 0x1F;

    tm[TILEMAP_INDEX(x, y) + 0x20 + 0] = tileref + 0x20 + 0x1C;
    tm[TILEMAP_INDEX(x, y) + 0x20 + 1] = tileref + 0x20 + 0x1D;
    tm[TILEMAP_INDEX(x, y) + 0x20 + 2] = tileref + 0x20 + 0x1E;
    tm[TILEMAP_INDEX(x, y) + 0x20 + 3] = tileref + 0x20 + 0x1F;

    return;
}

//! FE8U = 0x08005C24
s8 ShouldFaceBeRaised(int fid) {

    switch (fid) {
        case FID_LUTE:
        case FID_AMELIA:
        case FID_EWAN:
        case FID_DOZLA:
        case FID_MYRRH:
            return 1;

        default:
            return 0;
    }
}

//! FE8U = 0x08005CA4
void PutFace80x72_Core(u16 * tm, int fid, int chr, int pal) {
    const struct FaceData* info;

    if (fid == 0) {
        return;
    }

    info = GetPortraitData(fid);

    ApplyPalette(info->pal, pal);

    if (info->img != 0) {
        int i;

        Decompress(info->img, (void *)(chr * 0x20 + VRAM));
        ApplyPalette(info->pal, pal);

        if (ShouldFaceBeRaised(fid) != 0) {
            PutFace80x72_Raised(tm, (pal << 12) + (0x3FF & chr), info);
        } else {
            PutFace80x72_Standard(tm, (pal << 12) + (0x3FF & chr), info);
        }

        for (i = 0; i < 5; i++) {
            tm[i * 0x20 + 0] = 0;
            tm[i * 0x20 + 9] = 0;
        }

    } else {
        Decompress(info->imgCard, (void*)(chr * CHR_SIZE + VRAM));
        PutAppliedBitmap(tm, (pal << 12) + (0x3FF & chr), 10, 9);
    }

    return;
}

//! FE8U = 0x08005D64
void BgFaceBlink_Init(struct FaceBlinkProc* proc) {
    proc->pFaceProc = NULL;
    proc->unk_38 = 120;
    proc->unk_32 = 0;

    return;
}

//! FE8U = 0x08005D70
void BgFaceBlink_Delay(struct FaceBlinkProc* proc) {

    proc->unk_38--;

    if (proc->unk_38 >= 0) {
        return;
    }

    proc->unk_38 = FaceBlinkProc_GenBlinkInterval(proc);
    proc->unk_34 = 0;

    Proc_Break(proc);

    return;
}

//! FE8U = 0x08005D98
void BgFaceBlink_PutFace(struct FaceBlinkProc* proc) {
    const struct FaceData* info;
    u16* ptrA;
    u16* ptrB;
    int offset;

    int tileref = (proc->palId << 12) + (0x3FF & proc->tileId);

    info = GetPortraitData(proc->faceId);

    offset = 0;

    switch (proc->unk_34) {
        case 3:
            offset = 88;
            break;

        case 0:
        case 6:
            offset = 24;
            break;

        case 9:

            PutFace80x72_Standard(proc->unk_3c, (proc->palId << 12) + (0x3FF & proc->tileId), info);

            BG_EnableSync(GetBackgroundFromBufferPointer(proc->unk_3c));

            Proc_Break(proc);

            return;

        case 1:
        case 2:
        case 4:
        case 5:
        case 7:
        case 8:
            proc->unk_34++;
            return;
    }

    info = GetPortraitData(proc->faceId);

    ptrA = ((info->yEyes << 5) + proc->unk_3c) + info->xEyes;

    ptrB = ptrA - 1;

    *(ptrB + 0x00 + 0) = tileref + offset + 0x00 + 0;
    *(ptrB + 0x00 + 1) = tileref + offset + 0x00 + 1;
    *(ptrB + 0x00 + 2) = tileref + offset + 0x00 + 2;
    *(ptrB + 0x00 + 3) = tileref + offset + 0x00 + 3;

    *(ptrA + 0x20 - 1) = tileref + offset + 0x20 + 0;
    *(ptrA + 0x20 + 0) = tileref + offset + 0x20 + 1;
    *(ptrA + 0x20 + 1) = tileref + offset + 0x20 + 2;
    *(ptrA + 0x20 + 2) = tileref + offset + 0x20 + 3;

    BG_EnableSync(GetBackgroundFromBufferPointer(ptrB));

    proc->unk_34++;

    return;
}

//! FE8U = 0x08005E98
void PutFace80x72(ProcPtr proc, u16 * bgOut, int fid, int tileId, int palId)
{
    Proc_EndEach(gProcScr_BgFaceBlink);
    PutFace80x72_Core(bgOut, fid, tileId, palId);
    GetPortraitData(fid);
    return;
}

//! FE8U = 0x08005ECC
void EndFacePtr(struct DeleteFaceProc* proc) {
    EndFace(proc->target);
    return;
}

//! FE8U = 0x08005ED8
void EndFaceIn8Frames(struct FaceProc* target) {
    struct DeleteFaceProc* proc = Proc_Start(gProcScr_FaceEndIn8Frames, PROC_TREE_3);

    proc->target = target;

    return;
}

//! FE8U = 0x08005EF0
void StartFaceFadeIn(struct FaceProc* proc) {
    const struct FaceData* info = GetPortraitData(proc->faceId);

    SetBlackPal(sFaceConfig[proc->faceSlot].paletteId + 0x10);
    StartPalFade(info->pal, sFaceConfig[proc->faceSlot].paletteId + 0x10, 12, proc);

    return;
}

//! FE8U = 0x08005F38
void StartFaceFadeOut(struct FaceProc* proc) {
#ifdef PC_PORT
    /*
     * A talk slot with no face (port-plan/DIVERGENCES.md, D-80, was working ID D-R10-FACES-1):
     * TalkInterpret's [ClearFace] on the slot it emptied just before, in MSG_AB4,
     * Ephraim's Chapter 10 opening ("[OpenRight][ClearFace]" twice; the only such text
     * in the game). With proc NULL the GBA loads faceId (ldrh 0x3E, 0x08005F3C) and
     * faceSlot (ldrb 0x40, 0x08005F48) from the BIOS latch: PORT_NULL_READ by the rule.
     * Measured (mGBA, w_i10: route_i10 to tick 4,328, then A every 21 ticks; tick
     * 5,528, IE 0x2001, no HBlank IRQ): 0xE3A0 and 4. sFaceConfig[4] is past the array:
     * sMsgString's bytes 4-5 (0x0202A6B0), the decoded text's own codes, 0x100B there,
     * so the GBA runs StartPalFadeToBlack(0x101B, 12, NULL), whose PalFadeSt and
     * palette lie on EWRAM's mirrors (src/bmlib.c, D-81, was working ID D-R10-FACES-2), and
     * EndFaceIn8Frames(NULL), whose EndFace(NULL) 8 frames later is D-3. faceId only
     * feeds GetPortraitData, whose result is not used, so it is not read here.
     */
    if (proc == NULL) {
        struct FaceVramEntry* config = (struct FaceVramEntry*)((uintptr_t)sFaceConfig
            + (u32)PORT_NULL_READ(proc, faceSlot) * sizeof(struct FaceVramEntry));

        /* openBus: the opcode after the ldrh at 0x08005F4E, movs r1, #12 (not used: EWRAM) */
        StartPalFadeToBlack(PORT_GBA_READ(config, paletteId, PORT_THUMB_OPEN_BUS(0x210C)) + 0x10, 12, proc);
        EndFaceIn8Frames(proc);
        return;
    }
#endif

    // unused
    const struct FaceData* info = GetPortraitData(proc->faceId);

    StartPalFadeToBlack(sFaceConfig[proc->faceSlot].paletteId + 0x10, 12, proc);

    EndFaceIn8Frames(proc);

    return;
}

//! FE8U = 0x08005F6C
u8* GetFactionFaceImg(int fid) {

    // generic minimug image lut
    u8* imgTable[] = {
        Img_FactionMiniCard3,
        Img_FactionMiniCard2,
        Img_FactionMiniCard0,
        Img_FactionMiniCard0,
        Img_FactionMiniCard3,
        Img_FactionMiniCard1,
        Img_FactionMiniCard1,
        Img_FactionMiniCard3,
    };

    fid = fid - FID_FACTION_CHIBI;

    return imgTable[fid];
}

//! FE8U = 0x08005F9C
void ApplyFactionFacePal(int fid, int pal) {

    // generic minimug palette lut
    u16* palTable[] = {
        Pal_FactionMiniCard_Blue,
        Pal_FactionMiniCard_Orange,
        Pal_FactionMiniCard_Green,
        Pal_FactionMiniCard_Orange,
        Pal_FactionMiniCard_Orange,
        Pal_FactionMiniCard_Green,
        Pal_FactionMiniCard_Orange,
        Pal_FactionMiniCard_Blue,
    };

    fid = fid - FID_FACTION_CHIBI;

    ApplyPalette(palTable[fid], pal);

    return;
}

//! FE8U = 0x08005FD4
void FaceMouth_Init(struct FaceBlinkProc* proc) {
    proc->pFaceProc = (struct FaceProc*)(proc->proc_parent);
    proc->unk_32 = 0;

    return;
}

//! FE8U = 0x08005FE0
void FaceMouth_Loop(struct FaceBlinkProc* proc) {
    int oam1;
    int oam0;

    if (!(GetFaceDisplayBits(proc->pFaceProc) & (FACE_DISP_TALK_1 | FACE_DISP_TALK_2))) {
        int offsetA = (GetFaceDisplayBits(proc->pFaceProc) & FACE_DISP_SMILE) ? 0 : 24;
        offsetA += 16;

        Register2dChrMove(
            proc->pFaceProc->pFaceInfo->imgMouth + offsetA * 0x20,
            (void*)(((proc->pFaceProc->oam2 + 28) & 0x3FF) * 0x20 + 0x06010000),
            4,
            2
        );

    } else {
        proc->unk_32--;

        if (proc->unk_32 < 0) {
            int offsetB = (GetFaceDisplayBits(proc->pFaceProc) & FACE_DISP_SMILE) ? 0 : 24;

            proc->unk_32 = ((AdvanceGetLCGRNValue() >> 16) & 7) + 1;
            proc->blinkControl = (proc->blinkControl + 1) & 3;

            switch (proc->blinkControl) {
                case 1:
                case 3:
                    offsetB += 8;
                    break;

                case 2:
                    offsetB += 16;
                    break;

                case 0:
                default:
                    offsetB += 0;
                    break;
            }

            Register2dChrMove(
                proc->pFaceProc->pFaceInfo->imgMouth + offsetB * 0x20,
                (void*)(((proc->pFaceProc->oam2 + 28) & 0x3FF) * 0x20 + 0x06010000),
                4,
                2
            );

        }
    }

    oam1 = 4 - proc->pFaceProc->pFaceInfo->xMouth;

    oam1 = (GetFaceDisplayBits(proc->pFaceProc) & FACE_DISP_FLIPPED) ? oam1 : -oam1;

    oam1 = OAM1_X((oam1 * 8 + proc->pFaceProc->xPos) - 16);

    if (GetFaceDisplayBits(proc->pFaceProc) & FACE_DISP_FLIPPED) {
        oam1 = oam1 + OAM1_HFLIP;
    }

    if (GetFaceDisplayBits(proc->pFaceProc) & FACE_DISP_BLEND) {
        oam0 = OAM0_BLEND;
    } else {
        oam0 = 0;
    }

    oam0 += (proc->pFaceProc->yPos + (proc->pFaceProc->pFaceInfo->yMouth * 8)) & 0xFF;

    PutSpriteExt(
        proc->pFaceProc->spriteLayer,
        oam1,
        oam0,
        gObject_32x16,
        proc->pFaceProc->oam2 + 28
    );

    return;
}

//! FE8U = 0x08006134
void PutFaceEyeSprite(struct FaceBlinkProc* proc, int unk) {
    int oam1;
    int oam0;

    s8 flag = 0;

    switch (unk) {
        case 0:
            unk = 88;
            break;

        case 1:
            unk = 24;
            break;

        case 0x80:
            unk = 88;
            flag = 1;
            break;

        case 0x81:
            unk = 24;
            flag = 1;
            break;

        default:
            return;
    }

    oam1 = 4 - proc->pFaceProc->pFaceInfo->xEyes;

    oam1 = (GetFaceDisplayBits(proc->pFaceProc) & FACE_DISP_FLIPPED) ? oam1 : -oam1;

    oam1 = ((oam1 * 8 + proc->pFaceProc->xPos) - 16) & 0x1FF;

    if (GetFaceDisplayBits(proc->pFaceProc) & 1) {
        oam1 = oam1 + 0x1000;
    }

    if (GetFaceDisplayBits(proc->pFaceProc) & FACE_DISP_BLEND) {
        oam0 = OAM0_BLEND;
    } else {
        oam0 = 0;
    }

    oam0 += (proc->pFaceProc->yPos + (proc->pFaceProc->pFaceInfo->yEyes * 8)) & 0xff;

    if (flag) {
        if (!(GetFaceDisplayBits(proc->pFaceProc) & FACE_DISP_FLIPPED)) {
            oam1 = oam1 + 16;
        }

        PutSpriteExt(
            proc->pFaceProc->spriteLayer,
            oam1,
            oam0,
            gObject_16x16,
            proc->pFaceProc->oam2 + unk + 2
        );
    }
    else {
        PutSpriteExt(
            proc->pFaceProc->spriteLayer,
            oam1,
            oam0,
            gObject_32x16,
            proc->pFaceProc->oam2 + unk
        );
    }

    return;
}

//! FE8U = 0x0800623C
void FaceBlink_Init(struct FaceBlinkProc* proc) {

    proc->pFaceProc = (struct FaceProc*)(proc->proc_parent);
    proc->blinkControl = ((struct FaceProc*)(proc->proc_parent))->pFaceInfo->blinkKind;
    proc->unk_38 = FaceBlinkProc_GenBlinkInterval(proc);
    proc->unk_32 = 0;

    if (proc->blinkControl == 6) {
        proc->blinkControl = 5;
        proc->unk_38 = INT32_MAX;
        proc->unk_32 = 2;
        proc->unk_34 = 6;

        Proc_Goto(proc, 97);
    }

    return;
}

//! FE8U = 0x08006280
void FaceBlink_Delay(struct FaceBlinkProc* proc) {
    int unk;

    proc->unk_38--;

    unk = proc->unk_32;

    if (unk != 0) {
        Proc_Goto(proc, (s16)unk);
        return;
    }

    if (proc->unk_38 < 0) {
        proc->unk_38 = FaceBlinkProc_GenBlinkInterval(proc);
        proc->unk_34 = unk;

        Proc_Goto(proc, 1);
    }

    return;
}

//! FE8U = 0x080062B8
void FaceBlink_PreSwitch(struct FaceBlinkProc* proc) {
    int r5 = 2;

    switch (proc->unk_34) {
        case 3:
        case 4:
        case 5:
            r5 = 0;
            break;

        case 0:
        case 1:
        case 2:
        case 6:
        case 7:
        case 8:
            r5 = 1;
            break;

        case 10:
            Proc_Goto(proc, 0);
            break;

    }

    PutFaceEyeSprite(proc, r5);

    proc->unk_34++;

    return;
}

//! FE8U = 0x08006324
void FaceBlink_InitDisplayFrame0(struct FaceBlinkProc* proc) {
    proc->unk_34 = 0;
    return;
}

//! FE8U = 0x0800632C
void FaceBlink_DisplayFrame0(struct FaceBlinkProc* proc) {

    if (proc->unk_34 < 6) {
        FaceBlink_PreSwitch(proc);
        return;
    }

    PutFaceEyeSprite(proc, 0);

    if (proc->unk_32 == 0) {
        Proc_Goto(proc, 1);
    }

    if (proc->unk_32 == 3) {
        Proc_Goto(proc, 3);
    }

    return;
}

//! FE8U = 0x08006370
void FaceBlink_InitDisplayFrame1(struct FaceBlinkProc* proc) {
    proc->unk_34 = 0;
    return;
}

//! FE8U = 0x08006378
void FaceBlink_DisplayFrame1(struct FaceBlinkProc* proc) {

    if (proc->unk_34 < 3) {
        FaceBlink_PreSwitch(proc);
        return;
    }

    PutFaceEyeSprite(proc, 1);

    if (proc->unk_32 == 0) {
        Proc_Goto(proc, 1);
    }

    if (proc->unk_32 == 2) {
        Proc_Goto(proc, 2);
    }

    return;
}

//! FE8U = 0x080063BC
void FaceBlink_InitDisplayFrameFlip(struct FaceBlinkProc* proc) {
    proc->unk_34 = 0;
    return;
}

//! FE8U = 0x080063C4
void FaceBlink_DisplayFrameFlip(struct FaceBlinkProc* proc) {
    int r5 = 2;

    switch (proc->unk_34) {
        case 3:
        case 4:
        case 5:
            r5 = 0;
            break;

        case 0:
        case 1:
        case 2:
        case 6:
        case 7:
        case 8:
            r5 = 1;
            break;

        case 10:
            Proc_Goto(proc, 0);
            proc->unk_32 = 0;
    }

    PutFaceEyeSprite(proc, 0x80 + r5);

    proc->unk_34++;

    return;
}

//! FE8U = 0x08006438
void SetFaceBlinkControl(struct FaceProc* proc, int blinkControl) {
    struct FaceBlinkProc* blinkProc;

#ifdef PC_PORT
    /*
     * A face slot with no face (port-plan/DIVERGENCES.md, D-71, was working ID D-R10-ENDING-2; D-4 below
     * is the face with no blink proc). With proc NULL the GBA loads pBlinkProc from 0x48
     * (ldr at 0x08006444): the BIOS latch word, an address 0xE....... where the GBA has no
     * memory. So the strh of blinkControl to it + 0x30 (0x08006446) and the str of the
     * interval to it + 0x38 (0x0800644E) are dropped, FaceBlinkProc_GenBlinkInterval's ldrh
     * of it + 0x30 (0x0800647C) is open bus, and blinkControl 0's pFaceInfo loads (0x08006440)
     * only feed those stores. What is left is the interval's one AdvanceGetLCGRNValue(), as
     * for D-4. Measured (mGBA, route_bend, the ending's CG text, 754 calls for slots 0 and 1):
     * pBlinkProc 0xE55EC002, stores to 0xE55EC032 and 0xE55EC03A (nothing changed at their
     * 28-bit mirrors either), ldrh 0x0409, gLCGRNValue one step on per call.
     */
    if (proc == NULL) {
        AdvanceGetLCGRNValue();
        return;
    }
#endif
    if (blinkControl == 0) {
        blinkControl = proc->pFaceInfo->blinkKind;
    }

    blinkProc = proc->pBlinkProc;
#ifdef PC_PORT
    /* GBA still advances the LCG once in FaceBlinkProc_GenBlinkInterval; see DIVERGENCES.md D-4. */
    if (blinkProc == NULL) {
        AdvanceGetLCGRNValue();
        return;
    }
#endif
    blinkProc->blinkControl = blinkControl;

    blinkProc->unk_38 = FaceBlinkProc_GenBlinkInterval(blinkProc);

    return;
}

//! FE8U = 0x08006458
void SetFaceBlinkControlById(int slot, int blinkControl) {
    SetFaceBlinkControl(gFaces[slot], blinkControl);
    return;
}

//! FE8U = 0x08006470
int FaceBlinkProc_GenBlinkInterval(struct FaceBlinkProc* proc) {
#ifdef PC_PORT
    unsigned rn = AdvanceGetLCGRNValue();
    int var = rn >> 16;
#else
    int var = AdvanceGetLCGRNValue() >> 16;
#endif

    switch (proc->blinkControl) {
        case 3:
            return (var >> 7) + 300;

        case 1:
            return (var >> 7) + 30;

        case 2:
            return (var >> 9) + 30;

        case 4:
            return 1;

        case 5:
            return INT32_MAX;
    }

#ifdef PC_PORT
    /*
     * Host-ABI value (port-plan/DIVERGENCES.md D-44). Retail falls off the
     * end for any blinkControl other than 1-5. agbcc returns r0, which still
     * holds AdvanceGetLCGRNValue()'s result, the new gLCGRNValue (0 to
     * 0x3FFFFFFF) (GBA 0x08006474 bl, 0x08006486 bhi to the 0x080064C8
     * return with r0 untouched). GCC returned rn >> 16. Retail reaches it
     * with 6 (FACE_BLINK_CLOSED): from FaceBlink_Init, which then stores
     * INT32_MAX instead, and from SetFaceBlinkControl(proc, 0) on a
     * closed-eye portrait ([EnableBlinking], 0x16, in talk or CgText text),
     * where nothing reads the value before FaceBlink_Init runs again (the
     * only way back to FaceBlink_Delay is Proc_Goto(proc, 0), and the first
     * label 0 comes before FaceBlink_Init). So with the retail portraits
     * (blinkKind 1 or 6) the value is never counted down; a portrait with
     * another blinkKind would get it as its next blink delay.
     */
    return (int)rn;
#endif
}

//! FE8U = 0x080064D4
void SetFaceEyeState(struct FaceProc* proc, int unk) {
#ifdef PC_PORT
    /*
     * A face slot with no face (port-plan/DIVERGENCES.md, D-82, was working ID D-R10-FACES-3):
     * TalkInterpret's [OpenEyes], [CloseEyes], [HalfCloseEyes] and [Wink] act on the
     * active talk slot (src/scene.c, 0x08007822). With proc NULL the GBA's ldr of
     * pBlinkProc (0x48, at 0x080064D4) gives the BIOS latch word, which always starts
     * with 0xE, so its strh to it + 0x32 (0x080064D6) goes to unused space and is
     * dropped: nothing changes, for every latch. No retail text puts these codes on an
     * empty slot (port-plan/pending/folded/r10-faces.md, the text sweep), and CgText_HandleFaceCode
     * (cgtext.c), the other caller, has no caller in the ROM. A live face whose
     * pBlinkProc is NULL stores to 0x32, in the BIOS area: H-1 drops it.
     */
    if (proc == NULL)
        return;
#endif
    proc->pBlinkProc->unk_32 = unk;
    return;
}

//! FE8U = 0x080064DC
void SetFaceEyeStateById(int slot, int unk) {
    SetFaceEyeState(gFaces[slot], unk);
    return;
}

//! FE8U = 0x080064F4
struct FaceProc* StartFace2(int slot, int fid, int x, int y, int disp) {
    struct FaceProc* proc;
    const struct FaceData* info;
    s16 oam2Layer;

    if (gFaces[slot] != NULL) {
        return NULL;
    }

    proc = Proc_Start(gProcScr_E_FACE_ExtraFrame, PROC_TREE_5);

    gFaces[slot] = proc;

    info = GetPortraitData(fid);

    if (disp & FACE_DISP_BIT_13) {
        CpuFastFill(0, PAL_OBJ(0) + PAL_OFFSET(sFaceConfig[slot].paletteId), 0x20);
        EnablePaletteSync();
    } else {
        ApplyPalette(info->pal, sFaceConfig[slot].paletteId + 0x10);
    }

    proc->pFaceInfo = info;

    proc->faceSlot = slot;
    proc->faceId = fid;

    proc->spriteLayer = 5;

    proc->xPos = x;
    proc->yPos = y;

    proc->unk_44 = NULL;
    proc->pBlinkProc = NULL;

    proc->displayBits = disp;

    FaceRefreshSprite(proc);

    switch (disp & FACE_DISP_HLAYER_MASK) {
        case FACE_DISP_HLAYER(FACE_HLAYER_0):
            oam2Layer = OAM2_LAYER(0);
            break;

        case FACE_DISP_HLAYER(FACE_HLAYER_1):
            oam2Layer = OAM2_LAYER(1);
            break;

        case FACE_DISP_HLAYER(FACE_HLAYER_3):
            oam2Layer = OAM2_LAYER(3);
            break;

        default:
            oam2Layer = OAM2_LAYER(2);
            break;
    }

    proc->oam2 = (sFaceConfig[slot].tileOffset / CHR_SIZE) + ((sFaceConfig[slot].paletteId & 0xF) * 0x1000) + oam2Layer;

    return proc;
}

//! FE8U = 0x08006618
void SetFacePosition(int slot, int x, int y) {

    gFaces[slot]->xPos = x;
    gFaces[slot]->yPos = y;

    return;
}

//! FE8U = 0x0800662C
void FaceChange_LockChildProcs(struct UnkFaceProc* proc) {

    if (proc->pFaceProc->pBlinkProc) {
        TryLockProc(proc->pFaceProc->pBlinkProc);
    }

    if (proc->pFaceProc->unk_44) {
        TryLockProc(proc->pFaceProc->unk_44);
    }

    return;
}

//! FE8U = 0x08006650
void FaceChange_LoadGfx(struct UnkFaceProc* proc) {
    struct FaceProc* faceProc;

    proc->pFaceInfo = GetPortraitData(proc->faceId);

    Decompress(proc->pFaceInfo->img, (void*)(sFaceConfig[proc->pFaceProc->faceSlot].tileOffset + 0x06010000));

    ApplyPalette(proc->pFaceInfo->pal, sFaceConfig[proc->pFaceProc->faceSlot].paletteId + 0x10);

    faceProc = proc->pFaceProc;
    faceProc->pFaceInfo = proc->pFaceInfo;
    faceProc->faceId = proc->faceId;

    return;
}

//! FE8U = 0x080066A8
void FaceChange_UnlockChildProcs(struct UnkFaceProc* proc) {
    if (proc->pFaceProc->pBlinkProc) {
        proc->pFaceProc->pBlinkProc->blinkControl = proc->pFaceInfo->blinkKind;

        Proc_Goto(proc->pFaceProc->pBlinkProc, 0);

        TryUnlockProc(proc->pFaceProc->pBlinkProc);
    }

    if (proc->pFaceProc->unk_44) {
        TryUnlockProc(proc->pFaceProc->unk_44);
    }

    return;
}

//! FE8U = 0x080066E0
void StartFaceChange(struct FaceProc* parent, int fid) {
    struct UnkFaceProc* proc = Proc_Start(gProcScr_FaceChange, parent);

    proc->pFaceProc = parent;
    proc->faceId = fid;

    return;
}

//! FE8U = 0x080066FC
void UnpackFaceImg(int offset, int fid) {
    Decompress(GetPortraitData(fid)->img, (void*)(offset * 0x20 + VRAM));
    return;
}

//! FE8U = 0x0800671C
void ApplyFacePal(int pal, int fid) {
    ApplyPalette(GetPortraitData(fid)->pal, pal);
    return;
}

// ????
u16 const gFaceTm_96x80[] =
{
      -1,   -1, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,   -1,   -1,
      -1,   -1, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,   -1,   -1,
      -1,   -1, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,   -1,   -1,
      -1,   -1, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,   -1,   -1,
      -1,   -1, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,   -1,   -1,
      -1,   -1, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,   -1,   -1,
    0x14, 0x15, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x16, 0x17,
    0x34, 0x35, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x36, 0x37,
    0x54, 0x55, 0x10, 0x11, 0x12, 0x13, 0x50, 0x51, 0x52, 0x53, 0x56, 0x57,
    0x74, 0x75, 0x30, 0x31, 0x32, 0x33, 0x70, 0x71, 0x72, 0x73, 0x76, 0x77,
};

//! FE8U = 0x08006738
void PutFaceOnBackGround(u16* a, u16 b, s8 c) {
    int i;
    int j;
    const u16* src = gFaceTm_96x80;
    u16* dst = a;

    if (c == 0) {
        for (i = 0; i < 10; i++) {
            const u16* nextSrc = src + 12;

            for (j = 0; j < 12; j++) {
                if (src[j] == 0xFFFF) {
                    dst[j] = 0;
                } else {
                    dst[j] = src[j] + b;
                }

            }

            src = nextSrc;
            dst += 0x20;
        }
    } else {
        for (i = 0; i < 10; i++) {
            for (j = 0; j < 12; j++) {
                if (src[11 - j] == 0xFFFF) {
                    dst[j] = 0;
                } else {
                    dst[j] = src[11 - j] + b + 0x400;
                }
            }

            src += 12;
            dst += 0x20;
        }
    }
}
