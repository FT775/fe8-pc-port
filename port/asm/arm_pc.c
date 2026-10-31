#include "global.h"

#include "port_hw.h"
#include "port_gbabus.h"

#include "bmmap.h"
#include "bmidoten.h"

/*
 * C versions of asm/arm.s and asm/arm_call.s (PLAN.md 3.5). They copy the asm
 * semantics exactly, including the odd parts: TmFillRect fills (w+1)x(h+1),
 * TmApplyTsa writes rows bottom to top, PutOamHi ORs x/y in unmasked,
 * ColorFadeTick clamps the unwrapped sum while storing the wrapped byte, and
 * MapFloodCoreStep reads the source cell's coordinates as unsigned bytes.
 * DrawGlyph and DecodeString are checked against the ARM code itself by
 * port/test/difftest (Unicorn).
 */

extern s8 gFadeComponentStep[0x20];
extern s8 gFadeComponents[0x600];
extern u16 gPaletteBuffer[0x200];
/* gOamHiPutIt (u32 *) and gOamLoPutIt (void *) come from variables.h. */

void ColorFadeTick(void)
{
    int pal, color;

    for (pal = 0x1F; pal >= 0; pal--)
    {
        int step = gFadeComponentStep[pal];
        u8 *comp;

        if (step == 0)
            continue;
        comp = (u8 *)gFadeComponents + 0x30 + pal * 0x30;
        for (color = 0xF; color >= 0; color--)
        {
            int out[3], i;

            comp -= 3;
            for (i = 0; i < 3; i++)
            {
                int sum = comp[i] + step;
                comp[i] = (u8)sum;
                sum -= 0x20;
                if (sum < 0)
                    sum = 0;
                if (sum >= 0x20)
                    sum = 0x1F;
                out[i] = sum;
            }
            gPaletteBuffer[pal * 0x10 + color] = (u16)(out[0] + (out[1] << 5) + (out[2] << 10));
        }
    }
}

void ClearOam(void *buf, int count)
{
    u32 *p = buf;
    int n = (int)((u32)count >> 4) - 1;
    int i;

    if ((uintptr_t)buf < 0x4000)
    {
        /* The GBA ignores writes to the BIOS area; the host has no page there.
           SyncHiOam does this before InitOam has run (sOamHi.buf == NULL). */
        static unsigned sLogged;
        PortStubHit("ClearOam write to BIOS area discarded", (u32)(uintptr_t)buf, &sLogged);
        return;
    }

    do
    {
        for (i = 0; i < 16; i++)
            p[i * 2] = 160; /* attr0 = y 160 (offscreen), attr1 = 0; attr2/3 untouched */
        p += 32;
    } while (--n >= 0);
}

u32 Checksum32(const void *buf, int size)
{
    const u16 *p = buf;
    u32 add = 0, xor = 0;
    int n = size - 2;

    do
    {
        add += *p;
        xor ^= *p;
        p++;
        n -= 2;
    } while (n >= 0);
    return (add & 0xFFFF) + (xor << 16);
}

void TmFillRect(u16 *dest, int width, int height, int fillValue)
{
    int h = height, w;

    do
    {
        u16 *row = dest;
        w = width;
        do
            *row++ = (u16)fillValue;
        while (--w >= 0);
        dest += 0x20;
    } while (--h >= 0);
}

void TmCopyRect(const u16 *src, u16 *dst, int width, int height)
{
    int h, w;

    if (width <= 0 || height <= 0)
        return;
    for (h = 0; h < height; h++)
    {
        for (w = 0; w < width; w++)
            *dst++ = *src++;
        src += 0x20 - width;
        dst += 0x20 - width;
    }
}

void TmApplyTsa(u16 *tilemap, const void *tsa, int tileref)
{
    const u8 *hdr = tsa;
    int width = hdr[0], height = hdr[1];
    const u16 *src = (const u16 *)(hdr + 2);
    u16 *dst = tilemap + height * 0x20;
    int h, w;

    for (h = height; h >= 0; h--)
    {
        for (w = width; w >= 0; w--)
            *dst++ = (u16)(*src++ + tileref);
        dst -= width + 1 + 0x20;
    }
}

static void PutOamExt(u16 **putIt, int x, int y, const u16 *list, int oam2)
{
    u16 *dst = *putIt;
    u32 count = list[0];
    u32 xy = ((u32)x & 0xFFFF) | ((u32)y << 16);
    u32 yy = xy >> 16;

    if (count == 0)
        return;
    list++;
    *putIt = dst + count * 4;
    while (count--)
    {
        u32 a0 = list[0], a1 = list[1];
        dst[0] = (u16)(((a0 | yy) & 0xFF00) | ((a0 + yy) & 0xFF));
        dst[1] = (u16)(((a1 | xy) & 0xFE00) | ((a1 + xy) & 0x1FF));
        dst[2] = (u16)(list[2] + oam2);
        list += 3;
        dst += 4;
    }
}

void PutOamHi(int x, int y, const u16 *list, int oam2)
{
    PutOamExt((u16 **)&gOamHiPutIt, x, y, list, oam2);
}

void PutOamLo(int x, int y, const u16 *list, int oam2)
{
    PutOamExt((u16 **)&gOamLoPutIt, x, y, list, oam2);
}

/*
 * DrawGlyph (asm/arm.s, GBA 0x08000564), called through DrawGlyphRam.
 * Draws one 16x16 2bpp glyph into 4bpp text tiles, ORed over what is there.
 * - 16 rows (r9 counts 15 down to 0 with subs/bpl).
 * - Each row is umull'd by bitTable[subx] (GBA 0x08000540, 1 << 2*subx): a
 *   32x32 -> 64-bit product, so pixels shifted past bit 31 land in the high
 *   word and give the third tile column.
 * - Each byte (4 pixels) indexes pal, read with ldr and masked: bits 0-15 of
 *   the first read, bits 0-15 of the second shifted up. For a 2-aligned pal
 *   that is pal[i] whether the ldr is aligned or rotated, so u16 reads match.
 * - Destinations are dst[0], dst[0x10] and dst[0x20] (+0, +0x40, +0x80
 *   bytes), read-modify-write in that order, after src[i] is read. The order
 *   is kept so overlapping buffers behave as on the GBA.
 * The game passes subx = text->x & 7 (fontgrp.c, DrawTextGlyph). For any
 * other value the asm reads the ROM word past the table; the host maps the
 * ROM at its GBA address (D1-a), so that word is read the same way. The asm
 * re-reads the table word every row; reading it once is the same, because
 * the routine only writes dst and the GBA ignores writes to ROM.
 */
static const u32 sDrawGlyphBitTable[8] =
{
    1 << 0, 1 << 2, 1 << 4, 1 << 6, 1 << 8, 1 << 10, 1 << 12, 1 << 14,
};

void DrawGlyph(const u16 *pal, u32 *dst, const u32 *src, int subx)
{
    u32 mul;
    int i;

    if ((u32)subx < 8)
        mul = sDrawGlyphBitTable[subx];
    else
    {
        static unsigned sLogged;

        if (sLogged++ == 0)
            PortLog("[port] DrawGlyph: subx %d is outside bitTable (0-7); reading the ROM word "
                "at 0x%08X as the asm does\n", subx, 0x08000540u + ((u32)subx << 2));
        mul = *(const u32 *)(uintptr_t)(0x08000540u + ((u32)subx << 2));
    }

    for (i = 0xF; i >= 0; i--)
    {
        u64 row = (u64)*src * mul;
        u32 lo = (u32)row;
        u32 hi = (u32)(row >> 32);

        dst[0x00] |= pal[lo & 0xFF] | ((u32)pal[(lo >> 8) & 0xFF] << 16);
        dst[0x10] |= pal[(lo >> 16) & 0xFF] | ((u32)pal[(lo >> 24) & 0xFF] << 16);
        dst[0x20] |= pal[hi & 0xFF] | ((u32)pal[(hi >> 8) & 0xFF] << 16);

        dst++;
        src++;
    }
}

/*
 * DecodeString (asm/arm.s, GBA 0x080006E4), called through
 * CallARM_DecompText. Huffman-decodes one message into dst.
 * - Bits are taken from each source byte (ldrb, unsigned) from bit 0 up,
 *   eight per byte. The bit buffer carries over from one character to the
 *   next; only the node pointer restarts at the root.
 * - gMsgHuffmanTableRoot is read once, on entry. A node is one u32 of the
 *   table: bit set picks the halfword at +2, clear the one at +0 (ldrh, little
 *   endian), and that halfword is the index of the next node.
 * - The root itself is never tested as a leaf. A node is a leaf when bit 31
 *   is set (tst/bpl); bits 16-30 of a leaf are ignored.
 * - Leaf bits 8-15 non-zero: both bytes are stored, low then high, even when
 *   the low byte is 0, and decoding goes on. Otherwise the low byte is
 *   stored; 0 ends the string (the 0 is written, dst is not advanced).
 */
extern const u32 gMsgHuffmanTable[];
extern const u32 *const gMsgHuffmanTableRoot;

void DecodeString(const char *src, char *dst)
{
    const u8 *in = (const u8 *)src;
    u8 *out = (u8 *)dst;
    const u8 *table = (const u8 *)gMsgHuffmanTable;
    const u8 *root = (const u8 *)gMsgHuffmanTableRoot;
    u32 bits = 0; /* r2: always loaded before its first use */
    int left = 0; /* r3: bits left in the current byte, minus one */

    for (;;)
    {
        const u8 *node = root;
        u32 word;

        do
        {
            if (--left < 0)
            {
                bits = *in++;
                left = 7;
            }
            if (bits & 1)
                word = *(const u16 *)(node + 2);
            else
                word = *(const u16 *)node;
            node = table + (word << 2);
            bits >>= 1;
            word = *(const u32 *)node;
        } while (!(word & 0x80000000));

        if (word & 0xFF00)
        {
            out[0] = (u8)word;
            out[1] = (u8)(word >> 8);
            out += 2;
            continue;
        }

        out[0] = (u8)word;
        if (!(word & 0xFF))
            return;
        out++;
    }
}

/*
 * The flood's cell loads and stores on the GBA bus (port-plan/DIVERGENCES.md
 * H-10, was working ID H-FLOODBUS). Its row-pointer loads always stay inside
 * the map pools (y is -1 to 256, the pools hold 1,976 bytes), but a row past
 * the map's last one holds whatever the pool's tail holds: 0 in the unused
 * tail, so the cell is in the BIOS area, or map bytes taken as an address.
 * The arena opponent's flood from (60, 2) (D-54, src/bmarena.c) reads
 * and writes such cells. A cell in EWRAM or IWRAM is read and written as
 * before. Any other cell is loaded as the ARM does (PortGbaLoad8: the BIOS
 * latch below 0x4000, by the rule of port_gbabus.h; elsewhere the mapped copy,
 * the mirrors or the open bus, which for ARM code in IWRAM is the opcode at
 * the load + 8, the words below), and a store there goes to the RAM mirrors
 * or is dropped, as the GBA drops it in the BIOS area, ROM and unused space.
 * What was measured (port-plan/pending/folded/r9-uiarena.md, sections 2, 8):
 * - Chapter 5's arena: the only cells off RAM are the BIOS area's
 *   0x00000035-0x00000047, all read before the VBlank IRQ that comes inside
 *   the flood, so the post-SWI latch. Exact: every value equals mGBA's.
 * - Eirika's Chapter 10 arena: also terrain cells in unused space
 *   (0x1717176D-0x1A1A3F5D). The words below are the ARM open bus (GBATEK)
 *   and what mGBA returns, except while its DMA rule applies: when a sound
 *   DMA happens just before the terrain load, mGBA returns a stale bus word
 *   at that load until the next sound DMA, 16 lines later (gba->bus and
 *   dmaPC in its GBALoadBad). That depends on cycle timing the port does not
 *   model, so those loads are not exact (oracle limit OL-7). Measured: only
 *   the flood's hidden scratch differs; the opponent's place and the random
 *   numbers do not depend on these cells on that map.
 * - Ephraim's Chapter 11 arena: not checked (no save).
 * - A BIOS-area cell read after an IRQ inside the flood would give the
 *   post-IRQ latch on the GBA; not reached in the measured floods.
 * Not modelled, not reached: a store to I/O, palette, VRAM, OAM or SRAM
 * (dropped here).
 */
#define FLOOD_OB_TERRAIN 0xE7D8A007u /* ldrb r7, [r7, r1] 0x080007AC, + 8 */
#define FLOOD_OB_SRC     0xE7979102u /* ldrb r9, [r9, r5] 0x080007C4, + 8 */
#define FLOOD_OB_DST     0x2A00001Au /* ldrb r9, [r9, r1] 0x080007D0, + 8 */
#define FLOOD_OB_UNIT    0x0A000003u /* ldrb r7, [r7, r1] 0x080007F4, + 8 */

static int FloodHostRam(u32 a)
{
    return a - PORT_EWRAM_START < 0x40000u || a - PORT_IWRAM_START < 0x8000u;
}

static u32 FloodLoad8(const u8 *p, u32 openBus)
{
    u32 a = (u32)(uintptr_t)p;

    if (FloodHostRam(a))
        return *p;
    return PortGbaLoad8(a, openBus,
        PORT_OPEN_BUS_HBLANK_IRQ_ON(REG_IME, REG_IE, REG_DISPSTAT) ? PORT_OPEN_BUS_IRQ : PORT_OPEN_BUS_SWI);
}

static void FloodStore8(u8 *p, u8 value)
{
    u32 a = (u32)(uintptr_t)p;

    if (FloodHostRam(a))
        *p = value;
    else if ((a >> 24) == 0x02)
        *(u8 *)(uintptr_t)(PORT_EWRAM_START | (a & 0x3FFFF)) = value;
    else if ((a >> 24) == 0x03)
        *(u8 *)(uintptr_t)(PORT_IWRAM_START | (a & 0x7FFF)) = value;
}

/*
 * MapFloodCoreStep (asm/arm.s, GBA 0x08000784). Follows the asm, not the C
 * comment above it in arm.s and not the C variant MapFloodCoreStepThumb (bmidoten.c):
 * - src->xPos/yPos are read with ldrb, so they are unsigned here.
 * - The terrain cost and the source cell are read as unsigned bytes, and the
 *   sum is not truncated before the two unsigned compares (bhs, bhi).
 * - Indexing uses the full 32-bit sums, so x or y = -1 reads the map's
 *   safety border (BmMapInit), exactly as the asm does.
 * - The unit test reads hasUnit (+8) and XORs the unit map with unitId (+0xA).
 * - Cells outside EWRAM and IWRAM go through the GBA bus (above).
 */
void MapFloodCoreStep(int connexion, int xOff, int yOff)
{
    struct MovMapFillState *st = &gMovMapFillState;
    u32 xSrc = (u8)st->src->xPos;
    u32 ySrc = (u8)st->src->yPos;
    int x = xOff + (int)xSrc;
    int y = yOff + (int)ySrc;
    u32 cost = (u32)gWorkingTerrainMoveCosts[FloodLoad8(&gBmMapTerrain[y][x], FLOOD_OB_TERRAIN)]
        + FloodLoad8(&gWorkingBmMap[ySrc][xSrc], FLOOD_OB_SRC);

    if (cost >= FloodLoad8(&gWorkingBmMap[y][x], FLOOD_OB_DST))
        return;

    if ((u8)st->hasUnit != 0)
    {
        u32 unit = FloodLoad8(&gBmMapUnit[y][x], FLOOD_OB_UNIT);

        if (unit != 0 && ((st->unitId ^ unit) & 0x80) != 0)
            return;
    }

    if (cost > st->movement)
        return;

    st->dst->xPos = (s8)x;
    st->dst->yPos = (s8)y;
    st->dst->connexion = (u8)connexion;
    st->dst->leastMoveCost = (u8)cost;
    st->dst++;

    FloodStore8(&gWorkingBmMap[y][x], (u8)cost);
}

/*
 * MapFloodCore (asm/arm.s, GBA 0x08000874). A breadth-first flood that swaps
 * the two pools on each pass. The asm dispatches on src->connexion through a
 * six-entry branch table at 0x080008D4; the cases below are that table, with
 * the steps in the asm's call order. connexion is the direction the cell was
 * entered from, and the flood does not step back that way (5 = start cell).
 */
void MapFloodCore(void)
{
    struct MovMapFillState *st = &gMovMapFillState;
    int pass = 0;

    for (;;)
    {
        pass ^= 1;
        if (pass)
        {
            st->src = gMovMapFillStPool1;
            st->dst = gMovMapFillStPool2;
        }
        else
        {
            st->src = gMovMapFillStPool2;
            st->dst = gMovMapFillStPool1;
        }

        if (st->src->connexion == 4)
            return;

        for (;;)
        {
            switch (st->src->connexion)
            {
            case 0:
                MapFloodCoreStep(3, 0, -1);
                MapFloodCoreStep(2, 0, 1);
                MapFloodCoreStep(0, -1, 0);
                break;

            case 1:
                MapFloodCoreStep(3, 0, -1);
                MapFloodCoreStep(2, 0, 1);
                MapFloodCoreStep(1, 1, 0);
                break;

            case 2:
                MapFloodCoreStep(2, 0, 1);
                MapFloodCoreStep(0, -1, 0);
                MapFloodCoreStep(1, 1, 0);
                break;

            case 3:
                MapFloodCoreStep(3, 0, -1);
                MapFloodCoreStep(0, -1, 0);
                MapFloodCoreStep(1, 1, 0);
                break;

            case 4:
                goto next_pass;

            case 5:
                MapFloodCoreStep(3, 0, -1);
                MapFloodCoreStep(2, 0, 1);
                MapFloodCoreStep(0, -1, 0);
                MapFloodCoreStep(1, 1, 0);
                break;

            default:
                /* The asm would branch past its table into the middle of
                   case 5. Only 0-5 are ever written, so stop loudly. */
                PortLog("[port] MapFloodCore: connexion %u at %p is outside the "
                    "arm.s branch table (0-5)\n", st->src->connexion, (void *)st->src);
                PortExit(1);
            }

            st->dst->connexion = 4;
            st->src++;
        }
    next_pass:;
    }
}

/* asm/arm_call.s: Thumb-to-ARM veneers. */
void ClearOAMBuffer(void *buf, int count)
{
    ClearOam(buf, count);
}

void CallARM_FillTileRect(u16 *tilemap, const void *tsa, int tileref)
{
    TmApplyTsa(tilemap, tsa, tileref);
}

void TileMap_FillRect(u16 *dest, int width, int height, int fillValue)
{
    TmFillRect(dest, width, height, fillValue);
}

void CALLARM_ColorFadeTick(void)
{
    ColorFadeTick();
}

void TileMap_CopyRect(u16 *src, u16 *dst, int width, int height)
{
    TmCopyRect(src, dst, width, height);
}

u32 ComputeChecksum32(const u32 *buf, int size)
{
    return Checksum32(buf, size);
}
