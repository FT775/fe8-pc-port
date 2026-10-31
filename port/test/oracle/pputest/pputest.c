/*
 * PPU test ROM (port/test/README.md, "PPU test ROM"). Not FE8 code and not
 * ROM-derived: still screens that ask the oracle, mGBA, how it draws a few
 * cases the port's renderer must draw the same way. Built with the host's
 * arm-none-eabi-gcc into build/ (never committed) and shot in the oracle
 * runner; tools/pputest_check.py reads the pixels; port/render/test_obj.c RT19
 * (variants 0 and 1) and test_bg.c's mGBA BG clip case (variant 2) draw the
 * same scenes with the PC renderer and expect the same pixels.
 *
 * VARIANT 0 and 1: OBJ against OBJ. Test k sits on lines 8-15 at x = 16k + 4
 * (its 8x8 sprites are listed below, in OAM order; "clear" = all pixels
 * transparent). BG0: priority 1, red, clear on x 48-79 (tests 3 and 4). BG1:
 * priority 3, green. BLDCNT: alpha with BG1 as target 2, EVA = EVB = 8.
 * VARIANT 1 also turns the OBJ window on (DISPCNT bit 15, WINOUT 0x3F3F:
 * every layer and effects inside and outside), which takes mGBA's OBJ-window
 * drawing paths.
 *
 * VARIANT 2: text BG tiles seen through narrow window segments. Tile 1:
 * column c has colour index c + 1; BG0 and BG1 are all tile 1, horizontally
 * flipped. WIN0 [130, 135) on lines 0-79 shows BG0 (HOFS 0), WIN1 [130, 135)
 * on lines 80-119 shows BG1 (HOFS 3); outside, nothing (the backdrop).
 */
typedef unsigned short u16;
typedef unsigned int u32;
#define REG16(a) (*(volatile u16 *)(a))

#define A_COL 0x7C00   /* blue, OBJ palette bank 1 */
#define B_COL 0x03FF   /* yellow, bank 2 */
#define C_COL 0x7FFF   /* white, bank 3 */
#define BG0_COL 0x001F /* red */
#define BG1_COL 0x03E0 /* green */

enum { NORMAL, SEMI, OBJWIN };

static volatile u16 *const sPal = (volatile u16 *)0x05000000;
static volatile u16 *const sOam = (volatile u16 *)0x07000000;
static volatile int *const sNext = (volatile int *)0x03000000; /* no .data or .bss in this ROM */

/* The next OAM entry: an 8x8 sprite of test k at x offset dx. */
static void Obj(int k, int dx, int mode, int prio, int bank, int opaque, int affine)
{
    int i = (*sNext)++;

    sOam[i * 4 + 0] = (u16)(8 | (mode << 10) | (affine ? 0x100 : 0));
    sOam[i * 4 + 1] = (u16)((16 * k + 4 + dx) & 0x1FF);
    sOam[i * 4 + 2] = (u16)((opaque ? 1 : 0) | (prio << 10) | (bank << 12));
}

static void ObjScene(void)
{
    volatile u32 *vram = (volatile u32 *)0x06000000;
    volatile u16 *map0 = (volatile u16 *)0x0600F800, *map1 = (volatile u16 *)0x0600F000;
    int i;

    sOam[3] = 0x100; /* affine group 0: identity */
    sOam[15] = 0x100;
    sPal[1] = BG0_COL;
    sPal[2] = BG1_COL;
    sPal[256 + 16 * 1 + 1] = A_COL;
    sPal[256 + 16 * 2 + 1] = B_COL;
    sPal[256 + 16 * 3 + 1] = C_COL;
    for (i = 0; i < 8; i++)
    {
        vram[8 + i] = 0x11111111;                 /* BG tile 1: colour 1 */
        vram[16 + i] = 0x22222222;                /* BG tile 2: colour 2 */
        vram[0x10000 / 4 + 8 + i] = 0x11111111;   /* OBJ tile 1: colour 1; OBJ tile 0 is clear */
    }
    for (i = 0; i < 32 * 32; i++)
    {
        int col = i & 31;

        map0[i] = (col >= 6 && col < 10) ? 0 : 1;
        map1[i] = 2;
    }

    *sNext = 0;
    /* 0: A prio 2; clear prio 0; C prio 1. */
    Obj(0, 0, NORMAL, 2, 1, 1, 0);
    Obj(0, 0, NORMAL, 0, 2, 0, 0);
    Obj(0, 0, NORMAL, 1, 3, 1, 0);
    /* 1: A prio 2; C prio 1 (no clear sprite). */
    Obj(1, 0, NORMAL, 2, 1, 1, 0);
    Obj(1, 0, NORMAL, 1, 3, 1, 0);
    /* 2: A prio 2; clear prio 0 (BG0, prio 1, is in front of A unless A is lifted). */
    Obj(2, 0, NORMAL, 2, 1, 1, 0);
    Obj(2, 0, NORMAL, 0, 2, 0, 0);
    /* 3 (BG0 clear here): semi-transparent A prio 2; normal clear prio 0. */
    Obj(3, 0, SEMI, 2, 1, 1, 0);
    Obj(3, 0, NORMAL, 0, 2, 0, 0);
    /* 4 (BG0 clear here): normal A prio 2; semi-transparent clear prio 0. */
    Obj(4, 0, NORMAL, 2, 1, 1, 0);
    Obj(4, 0, SEMI, 0, 2, 0, 0);
    /* 5: A prio 2; OBJ-window sprite prio 0, clear. */
    Obj(5, 0, NORMAL, 2, 1, 1, 0);
    Obj(5, 0, OBJWIN, 0, 2, 0, 0);
    /* 6: A prio 2; OBJ-window sprite prio 0, opaque. */
    Obj(6, 0, NORMAL, 2, 1, 1, 0);
    Obj(6, 0, OBJWIN, 0, 2, 1, 0);
    /* 7: A prio 1; clear prio 1; C prio 1. */
    Obj(7, 0, NORMAL, 1, 1, 1, 0);
    Obj(7, 0, NORMAL, 1, 2, 0, 0);
    Obj(7, 0, NORMAL, 1, 3, 1, 0);
    /* 8: A prio 3; clear prio 1; C prio 2. */
    Obj(8, 0, NORMAL, 3, 1, 1, 0);
    Obj(8, 0, NORMAL, 1, 2, 0, 0);
    Obj(8, 0, NORMAL, 2, 3, 1, 0);
    /* 9: clear prio 0 first; B prio 2. */
    Obj(9, 0, NORMAL, 0, 1, 0, 0);
    Obj(9, 0, NORMAL, 2, 2, 1, 0);
    /* 10: A prio 2; affine (identity) clear prio 0. */
    Obj(10, 0, NORMAL, 2, 1, 1, 0);
    Obj(10, 0, NORMAL, 0, 2, 0, 1);
    /* 11: A prio 1; clear prio 3. */
    Obj(11, 0, NORMAL, 1, 1, 1, 0);
    Obj(11, 0, NORMAL, 3, 2, 0, 0);
    /* 12: A prio 2; clear prio 0; C prio 0. */
    Obj(12, 0, NORMAL, 2, 1, 1, 0);
    Obj(12, 0, NORMAL, 0, 2, 0, 0);
    Obj(12, 0, NORMAL, 0, 3, 1, 0);
    /* 13: A prio 2 at x + 0; clear prio 0 at x + 4: only A's right half is lifted. */
    Obj(13, 0, NORMAL, 2, 1, 1, 0);
    Obj(13, 4, NORMAL, 0, 2, 0, 0);

    REG16(0x04000008) = (u16)(1 | (31 << 8));       /* BG0: prio 1, char base 0, screen base 31 */
    REG16(0x0400000A) = (u16)(3 | (30 << 8));       /* BG1: prio 3, screen base 30 */
    REG16(0x04000050) = (u16)((1 << 6) | (1 << 9)); /* alpha, target 2 BG1 */
    REG16(0x04000052) = (u16)(8 | (8 << 8));
#if VARIANT == 1
    REG16(0x0400004A) = 0x3F3F;
    REG16(0x04000000) = (u16)(0x0100 | 0x0200 | 0x1000 | 0x0040 | 0x8000);
#else
    REG16(0x04000000) = (u16)(0x0100 | 0x0200 | 0x1000 | 0x0040);
#endif
}

static void BgClipScene(void)
{
    volatile u32 *vram = (volatile u32 *)0x06000000;
    volatile u16 *map0 = (volatile u16 *)0x0600F800, *map1 = (volatile u16 *)0x0600F000;
    int i;

    for (i = 1; i <= 8; i++)
        sPal[i] = (u16)(i * 3 | (i * 2) << 5 | (31 - i * 3) << 10);
    for (i = 0; i < 8; i++)
        vram[8 + i] = 0x87654321; /* tile 1: column c has colour index c + 1 */
    for (i = 0; i < 32 * 32; i++)
    {
        map0[i] = 0x0401; /* tile 1, horizontal flip */
        map1[i] = 0x0401;
    }
    REG16(0x04000008) = (u16)(0 | (31 << 8));
    REG16(0x0400000A) = (u16)(1 | (30 << 8));
    REG16(0x04000010) = 0;                       /* BG0HOFS */
    REG16(0x04000014) = 3;                       /* BG1HOFS */
    REG16(0x04000040) = (u16)(130 << 8 | 135);   /* WIN0 [130, 135) */
    REG16(0x04000044) = (u16)(0 << 8 | 80);      /* on lines 0-79 */
    REG16(0x04000042) = (u16)(130 << 8 | 135);   /* WIN1 [130, 135) */
    REG16(0x04000046) = (u16)(80 << 8 | 120);    /* on lines 80-119 */
    REG16(0x04000048) = (u16)(0x01 | 0x02 << 8); /* WIN0: BG0; WIN1: BG1 */
    REG16(0x0400004A) = 0x0000;                  /* outside: nothing */
    REG16(0x04000000) = (u16)(0x0100 | 0x0200 | 0x2000 | 0x4000);
}

void Main(void)
{
    volatile u32 *vram = (volatile u32 *)0x06000000;
    int i;

    REG16(0x04000000) = 0x0080;
    for (i = 0; i < 0x18000 / 4; i++)
        vram[i] = 0;
    for (i = 0; i < 128; i++)
    {
        sOam[i * 4 + 0] = 0x0200 | 160; /* disabled */
        sOam[i * 4 + 1] = 0;
        sOam[i * 4 + 2] = 0;
        sOam[i * 4 + 3] = 0;
    }
    sPal[0] = 0x0000;
#if VARIANT == 2
    BgClipScene();
#else
    ObjScene();
#endif
    for (;;)
        ;
}
