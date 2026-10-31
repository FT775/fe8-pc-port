#include "global.h"

#include "fontgrp.h"
#include "hardware.h"
#include "ctc.h"
#include "bmlib.h"
#include "savemenu.h"

struct Proc8A21568 {
    /* 00 */ PROC_HEADER;

    /* 29 */ u8 pad[0x58-0x29];
    /* 58 */ int unk_58;
};

extern u16 Pal_ExtramenuUnk[]; // pal
extern u8 Img_ExtramenuUnkJpText1[]; // gfx
extern u8 Img_ExtramenuUnkJpText2[]; // gfx

//! FE8U = 0x080B0458
void ExtraMenuUnk_OnHBlank(void) {
    u16 vcount = (REG_VCOUNT + 1);

    if (vcount > 160) {
        vcount = 0;
    }

    if ((vcount & 1) == 0) {
        if (vcount < 100) {
            REG_BLDCNT = BLDCNT_TGT1_BG0 | BLDCNT_EFFECT_DARKEN;
            (*(vu16*)REG_ADDR_BLDY) = (100 - vcount) * 16 / 100;
        } else {
            REG_BLDCNT = BLDCNT_TGT1_BG2 | BLDCNT_EFFECT_BLEND | BLDCNT_TGT2_BG0;
            REG_BLDALPHA = BLDALPHA_BLEND(10, 16);
        }
    }

    return;
}

u16 CONST_DATA gBgConfig_ExtraMenuUnk[] = {
    0x0000, 0x6000, 0x0000,
    0x0000, 0x6800, 0x0000,
    0x8000, 0x7800, 0x0000,
    0x8000, 0x7800, 0x0000,
};

//! FE8U = 0x080B04BC
void ExtraMenuUnk_Init(struct Proc8A21568* proc) {
    proc->unk_58 = 4;

    SetupBackgrounds(gBgConfig_ExtraMenuUnk);

    gLCDControlBuffer.dispcnt.mode = 1;

    gLCDControlBuffer.bg2cnt.screenSize = 1;
    gLCDControlBuffer.bg2cnt.areaOverflowMode = 0;

    return;
}

//! FE8U = 0x080B04F8
void ExtraMenuUnk_LoadGfx(void) {
    ApplyPalettes(Pal_CommGameBgScreenInShop, 0, 2);
    ApplyPalette(Pal_08A295B4, 2);
    Decompress(Img_CommGameBgScreen, (void*)0x06001000);
    CallARM_FillTileRect(gBG0TilemapBuffer, Tsa_CommGameBgScreenInShop, 0x80);

    BG_EnableSyncByMask(1);

    ApplyPalettes(Pal_ExtramenuUnk, 0x11, 8);
    Decompress(Img_ExtramenuUnkJpText1, (void*)0x06010800);
    Decompress(Img_ExtramenuUnkJpText2, (void*)0x06014000);

    SetPrimaryHBlankHandler(ExtraMenuUnk_OnHBlank);

    gLCDControlBuffer.dispcnt.win0_on = 0;
    gLCDControlBuffer.dispcnt.win1_on = 0;
    gLCDControlBuffer.dispcnt.objWin_on = 0;

    gLCDControlBuffer.bg0cnt.priority = 3;
    gLCDControlBuffer.bg1cnt.priority = 0;
    gLCDControlBuffer.bg2cnt.priority = 2;
    gLCDControlBuffer.bg3cnt.priority = 2;

    return;
}

//! FE8U = 0x080B05C0
void nullsub_66(void) {
    return;
}

//! FE8U = 0x080B05C4
void ExtraMenuUnk_Loop(struct Proc8A21568* proc) {
    if (proc->unk_58 >= 0) {
        PutSpriteExt(4, 56, 8, Sprite_SaveMenuOptionBox, 0x2000);
        PutSpriteExt(4, 64, 16, SpriteArray_SaveMenuExtraOptionLabels[proc->unk_58], 0x3000);
    }
    return;
}

struct ProcCmd CONST_DATA gProcScr_ExtraMenuUnk[] = {
    PROC_CALL(ExtraMenuUnk_Init),
    PROC_CALL(ExtraMenuUnk_LoadGfx),

    PROC_SET_END_CB(nullsub_66),

    PROC_REPEAT(ExtraMenuUnk_Loop),

    PROC_END,
};

//! FE8U = 0x080B060C
void StartExtraMenuUnk(ProcPtr parent) {
    Proc_Start(gProcScr_ExtraMenuUnk, parent);
    return;
}

//! FE8U = 0x080B0620
void ExtraMenuUnk_SetLabel(int unk) {
    struct Proc8A21568* proc = Proc_Find(gProcScr_ExtraMenuUnk);
    proc->unk_58 = unk;

    return;
}
