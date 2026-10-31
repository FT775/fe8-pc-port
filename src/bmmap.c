#include "global.h"

#include "hardware.h"
#include "chapterdata.h"
#include "proc.h"
#include "event.h"
#include "bmunit.h"
#include "bmmap.h"
#include "bmidoten.h"
#include "bmtrick.h"
#include "bmlib.h"

#include "constants/terrains.h"
#include "constants/chapters.h"

#if defined(PC_PORT) && PORT_PLAYED_BUILD
#include "port_reforged.h" /* PC played builds: Reforged Options, danger zone on Select */
#endif
static void BmMapInit(void* buffer, u8*** outHandle, int width, int height);

static void RenderBmMapColumn(u16 xOffset);
static void RenderBmMapLine(u16 yOffset);

static void RefreshUnitsOnBmMap(void);
static void RefreshTorchlightsOnBmMap(void);
static void RefreshMinesOnBmMap(void);

EWRAM_OVERLAY(bmstart) u16 gBmMapBuffer[0x800 / 2] = {};

enum { MAP_POOL_SIZE = 0x7B8 };

// TODO: figure out what's up with this (overlaps with a lot of other objects?)


EWRAM_DATA struct Vec2 gBmMapSize = {};

EWRAM_DATA u8** gBmMapUnit     = NULL;
EWRAM_DATA u8** gBmMapTerrain  = NULL;
EWRAM_DATA u8** gBmMapMovement = NULL;
EWRAM_DATA u8** gBmMapRange    = NULL;
EWRAM_DATA u8** gBmMapFog      = NULL;
EWRAM_DATA u8** gBmMapHidden   = NULL;
EWRAM_DATA u8** gBmMapOther    = NULL;

EWRAM_DATA static u8 sBmMapUnitPool[MAP_POOL_SIZE] = {};
EWRAM_DATA static u8 sBmMapTerrainPool[MAP_POOL_SIZE] = {};
EWRAM_DATA static u8 sBmMapFogPool[MAP_POOL_SIZE] = {};
EWRAM_DATA static u8 sBmMapHiddenPool[MAP_POOL_SIZE] = {};
EWRAM_DATA static u8 sBmMapOtherPool[MAP_POOL_SIZE] = {};

EWRAM_DATA static u16 sTilesetConfig[0x1000 + 0x200] = {};

EWRAM_DATA static u16 sBmBaseTilesPool[MAP_POOL_SIZE] = {};
EWRAM_DATA u8 gWorkingMovementScript[0x40] = {};

static u8** sInitializingMap;

static u8 sBmMapMovementPool[MAP_POOL_SIZE];
static u8 sBmMapRangePool[MAP_POOL_SIZE];

u8*   gTilesetTerrainLookup = (u8*)(sTilesetConfig + 0x1000);
u16** gBmMapBaseTiles       = (u16**)(sBmBaseTilesPool);

void InitChapterMap(int chapterId) {
    UnpackChapterMap(gBmMapBuffer, chapterId);
    UnpackChapterMapGraphics(chapterId);

    BmMapInit(sBmMapUnitPool,     &gBmMapUnit,     gBmMapSize.x, gBmMapSize.y);
    BmMapInit(sBmMapTerrainPool,  &gBmMapTerrain,  gBmMapSize.x, gBmMapSize.y);
    BmMapInit(sBmMapMovementPool, &gBmMapMovement, gBmMapSize.x, gBmMapSize.y);
    BmMapInit(sBmMapRangePool,    &gBmMapRange,    gBmMapSize.x, gBmMapSize.y);
    BmMapInit(sBmMapFogPool,      &gBmMapFog,      gBmMapSize.x, gBmMapSize.y);
    BmMapInit(sBmMapHiddenPool,   &gBmMapHidden,   gBmMapSize.x, gBmMapSize.y);
    BmMapInit(sBmMapOtherPool,    &gBmMapOther,    gBmMapSize.x, gBmMapSize.y);

    BmMapFill(gBmMapUnit, 0);
    BmMapFill(gBmMapTerrain, 0);

    InitBaseTilesBmMap();
    ApplyEnabledMapChanges();
    RefreshTerrainBmMap();
    
    if (gPlaySt.chapterIndex == CHAPTER_75)
        sub_8019624();
}

void InitMapForMinimap(int chapterId) {
    UnpackChapterMap(gBmMapBuffer, chapterId);

    BmMapInit(sBmMapUnitPool,    &gBmMapUnit,    gBmMapSize.x, gBmMapSize.y);
    BmMapInit(sBmMapTerrainPool, &gBmMapTerrain, gBmMapSize.x, gBmMapSize.y);

    BmMapFill(gBmMapUnit, 0);
    BmMapFill(gBmMapTerrain, 0);

    InitBaseTilesBmMap();
    RefreshTerrainBmMap();
}

void sub_8019624(void) {
    int ix, iy;

    // Automatic water shadows?

    for (iy = 0; iy < gBmMapSize.y; ++iy) {
        for (ix = 0; ix < gBmMapSize.x; ++ix) {
            int connexion;

            if (gBmMapTerrain[iy][ix] != TERRAIN_WATER)
                continue;

            connexion = 0;

            if (ix > 0) {
                if (gBmMapTerrain[iy][ix - 1] == TERRAIN_FLOOR_REGULAR)
                    connexion = 1;

                if (gBmMapTerrain[iy][ix - 1] == TERRAIN_STAIRS)
                    connexion = 1;

                if (gBmMapTerrain[iy][ix - 1] == TERRAIN_CHEST_EMPTY)
                    connexion = 1;

                if (gBmMapTerrain[iy][ix - 1] == TERRAIN_CHEST_FULL)
                    connexion = 1;
            }

            if (iy > 0) {
                if (gBmMapTerrain[iy - 1][ix] == TERRAIN_FLOOR_REGULAR)
                    connexion += 2;

                if (gBmMapTerrain[iy - 1][ix] == TERRAIN_STAIRS)
                    connexion += 2;

                if (gBmMapTerrain[iy - 1][ix] == TERRAIN_CHEST_EMPTY)
                    connexion += 2;

                if (gBmMapTerrain[iy - 1][ix] == TERRAIN_CHEST_FULL)
                    connexion += 2;
            }

            if (ix > 0 && iy > 0)
                if ((gBmMapTerrain[iy]    [ix - 1] == TERRAIN_FLOOR_REGULAR) &&
                    (gBmMapTerrain[iy + 1][ix - 1] == TERRAIN_WATER) &&
                    (gBmMapTerrain[iy - 1][ix]     != TERRAIN_FLOOR_REGULAR))
                    connexion = 4;

            switch (connexion) {

            case 1: // straight shadow on the left
                gBmMapBaseTiles[iy][ix] = 0x2DC;
                break;

            case 2: // straight shadow on the top
                gBmMapBaseTiles[iy][ix] = 0x2D8;
                break;

            case 3: // shadow on both the left and the top
                gBmMapBaseTiles[iy][ix] = 0x358;
                break;

            case 4: // shadow on the left, but stronger at the top-left than bottom-left
                gBmMapBaseTiles[iy][ix] = 0x35C;
                break;

            } // switch (connexion)
        }
    }
}

void sub_8019778(void) {
    UnpackChapterMap(gBmMapBuffer, gPlaySt.chapterIndex);

    InitBaseTilesBmMap();
    ApplyEnabledMapChanges();
    RefreshTerrainBmMap();
    sub_8019624();
}

void BmMapInit(void* buffer, u8*** outHandle, int x, int y) {
    int i;
    u8* itBuffer;

    sInitializingMap = buffer;

    x  += 2; // two tiles on each edge (shared)
    y += 4; // two tiles on each edge

    // itBuffer = start of tile area (the first y * sizeof(u8*) bytes are reserved for row pointers)
    itBuffer = buffer + y * sizeof(u8*);

    // Setting up the row pointers
    for (i = 0; i < y; ++i) {
        sInitializingMap[i] = itBuffer;
        itBuffer += x;
    }

    // first row is actually the third, ensuring the top two map rows as safety
    *outHandle = sInitializingMap + 2;
}

void BmMapFill(u8** map, int value) {
    int size = (gBmMapSize.y + 4) * (gBmMapSize.x + 2);

    if (size % 2)
        size = size - 1;

    value = (0xFF & value);
    value += value << 8;

    CpuFill16(value, map[-2], size);

    SetWorkingBmMap(map);
}

void BmMapFillEdges(u8** map, u8 value) {
    int ix, iy;

    u8** theMap = map;

    // Set tile values for horizontal edges
    for (iy = 0; iy < gBmMapSize.y; ++iy) {
        theMap[iy][0]              = value;
        theMap[iy][gBmMapSize.x-1] = value;
    }

    // Set tile values for vertical edges
    for (ix = 0; ix < gBmMapSize.x; ++ix) {
        theMap[0]             [ix] = value;
        theMap[gBmMapSize.y-1][ix] = value;
    }
}

void UnpackChapterMap(void* into, int chapterId) {
    // Decompress map data
    Decompress(
        GetChapterMapPointer(chapterId), into);

    // Setting map size
    gBmMapSize.x = ((u8*)(into))[0];
    gBmMapSize.y = ((u8*)(into))[1];

    // Decompress tileset config
    Decompress(
        gChapterDataAssetTable[GetROMChapterStruct(chapterId)->map.tileConfigId], sTilesetConfig);

    // Setting max camera offsets (?) TODO: figure out
    gBmSt.cameraMax.x = gBmMapSize.x*16 - DISPLAY_WIDTH;
    gBmSt.cameraMax.y = gBmMapSize.y*16 - DISPLAY_HEIGHT;
}

void UnpackChapterMapGraphics(int chapterId) {
    // Decompress tileset graphics (part 1)
    Decompress(
        gChapterDataAssetTable[GetROMChapterStruct(chapterId)->map.obj1Id],
        (void*)(BG_VRAM + 0x20 * 0x400)); // TODO: tile id constant?

    // Decompress tileset graphics (part 2, if it exists)
    if (gChapterDataAssetTable[GetROMChapterStruct(chapterId)->map.obj2Id])
        Decompress(
            gChapterDataAssetTable[GetROMChapterStruct(chapterId)->map.obj2Id],
            (void*)(BG_VRAM + 0x20 * 0x600)); // TODO: tile id constant?

    // Apply tileset palette
    ApplyPalettes(
        gChapterDataAssetTable[GetROMChapterStruct(chapterId)->map.paletteId],
        6, 10); // TODO: palette id constant?
}

void UnpackChapterMapPalette(void) {
    ApplyPalettes(
        gChapterDataAssetTable[GetROMChapterStruct(gPlaySt.chapterIndex)->map.paletteId],
        6, 10); // TODO: palette id constant?
}

void InitBaseTilesBmMap(void) {
    int ix, iy;

    u16** rows;
    u16*  tiles;
    u16*  itBuffer;

    rows  = gBmMapBaseTiles;
    tiles = gBmMapBuffer;

    // Temporarily increment the size to account for an extra row of tiles
    // for the "subtitle help" when against the edge of the map
    gBmMapSize.y++;

    // Ignore first short (x, y byte pair)
    tiles++;

    // Tile buffer starts after the rows
    itBuffer = (u16*)(gBmMapBaseTiles + gBmMapSize.y);

    for (iy = 0; iy < gBmMapSize.y; ++iy) {
        // Set row buffer
        rows[iy] = itBuffer;
        itBuffer += gBmMapSize.x;

        // Set tiles
        for (ix = 0; ix < gBmMapSize.x; ++ix)
            gBmMapBaseTiles[iy][ix] = *tiles++;
    }

    // Fill "bottom" row with empty tiles (for the subtitle help scenario above)

    tiles = gBmMapBaseTiles[iy - 1];

    for (ix = 0; ix < gBmMapSize.x; ++ix)
        *tiles++ = 0;

    // Restore the original height of the map
    gBmMapSize.y--;
}

void RefreshTerrainBmMap(void) {
    int ix, iy;

    for (iy = 0; iy < gBmMapSize.y; ++iy)
        for (ix = 0; ix < gBmMapSize.x; ++ix)
            gBmMapTerrain[iy][ix] = gTilesetTerrainLookup[gBmMapBaseTiles[iy][ix] >> 2];

    RefreshAllLightRunes();
}

int GetTrueTerrainAt(int x, int y) {
    return gTilesetTerrainLookup[gBmMapBaseTiles[y][x] >> 2];
}

#ifdef PC_PORT
/*
 * D-27 (DisplayBmTile, NULL-probe group F, N-6 to N-21): RenderBmMap,
 * RenderBmMapColumn and RenderBmMapLine never check the map bounds. With a
 * map smaller than the screen (Castle Frelia, chapter 0x38, 2x2, at the
 * Chapter 1 -> 2 change) they ask for rows -8..9 and, through their u16
 * arithmetic, 65528..65538, and columns -13..14 and 65523. gBmMapBaseTiles[y]
 * and gBmMapFog[y] then read the words next to the row tables (terrain bytes,
 * tile ids, fog bytes, 0), and the tile, fog and tileset loads go through
 * those: EWRAM mirrors, the BIOS area, unused space, ROM past the image. The
 * GBA draws junk tiles into BG3 on a black screen; the host would fault.
 * Each PortGbaLoad below is the GBA load at the same place (GBA 0x08019B18),
 * with the GBA's 32-bit address arithmetic, so on the map it reads the same
 * memory as the retail lines. Its open-bus word is the THUMB opcode at the
 * load + 4 (GBA address of the load given), its BIOS latch the rule's
 * (PortOpenBusLatch, post-SWI on the routes). Every lane of 0xE3A02004,
 * 0xE55EC002 and 0x021B021B is nonzero, so the fog test off-RAM takes
 * palette 6 whatever the latch. See port-plan/DIVERGENCES.md D-27.
 */
#define BMTILE_OB_ROW  PORT_THUMB_OPEN_BUS(0x1840) /* ldr  r1, [r0]     0x08019B2C */
#define BMTILE_OB_ID   PORT_THUMB_OPEN_BUS(0x4912) /* ldrh r0, [r0]     0x08019B32 */
#define BMTILE_OB_FOGR PORT_THUMB_OPEN_BUS(0x7800) /* ldr  r0, [r4]     0x08019B40 */
#define BMTILE_OB_FOG  PORT_THUMB_OPEN_BUS(0x021B) /* ldrb r0, [r0]     0x08019B44 */
#define BMTILE_OB_T0   PORT_THUMB_OPEN_BUS(0x8028) /* ldrh r0, [r2]     0x08019B52 */
#define BMTILE_OB_T1   PORT_THUMB_OPEN_BUS(0x8068) /* ldrh r0, [r2]     0x08019B5A */
#define BMTILE_OB_T2   PORT_THUMB_OPEN_BUS(0x8008) /* ldrh r0, [r2]     0x08019B66 */
#define BMTILE_OB_T3   PORT_THUMB_OPEN_BUS(0x8008) /* ldrh r0, [r2, #2] 0x08019B6E */

void DisplayBmTile(u16* bg, int xTileMap, int yTileMap, int xBmMap, int yBmMap) {
    u16* out = bg + yTileMap * 0x40 + xTileMap * 2; // TODO: BG_LOCATED_TILE?
    u32 latch = PortOpenBusLatch();
    u32 row = PortGbaLoad32((u32)(uintptr_t)gBmMapBaseTiles + 4 * (u32)yBmMap, BMTILE_OB_ROW, latch);
    u32 tile = (u32)(uintptr_t)sTilesetConfig + 2 * PortGbaLoad16(row + 2 * (u32)xBmMap, BMTILE_OB_ID, latch);
    u32 fogRow = PortGbaLoad32((u32)(uintptr_t)gBmMapFog + 4 * (u32)yBmMap, BMTILE_OB_FOGR, latch);

    // TODO: palette id constants
    u16 base = PortGbaLoad8(fogRow + (u32)xBmMap, BMTILE_OB_FOG, latch) ? (6 << 12) : (11 << 12);

    out[0x00 + 0] = base + PortGbaLoad16(tile + 0, BMTILE_OB_T0, latch);
    out[0x00 + 1] = base + PortGbaLoad16(tile + 2, BMTILE_OB_T1, latch);
    out[0x20 + 0] = base + PortGbaLoad16(tile + 4, BMTILE_OB_T2, latch);
    out[0x20 + 1] = base + PortGbaLoad16(tile + 6, BMTILE_OB_T3, latch);
}

#undef BMTILE_OB_ROW
#undef BMTILE_OB_ID
#undef BMTILE_OB_FOGR
#undef BMTILE_OB_FOG
#undef BMTILE_OB_T0
#undef BMTILE_OB_T1
#undef BMTILE_OB_T2
#undef BMTILE_OB_T3
#else
void DisplayBmTile(u16* bg, int xTileMap, int yTileMap, int xBmMap, int yBmMap) {
    u16* out = bg + yTileMap * 0x40 + xTileMap * 2; // TODO: BG_LOCATED_TILE?
    u16* tile = sTilesetConfig + gBmMapBaseTiles[yBmMap][xBmMap];

    // TODO: palette id constants
    u16 base = gBmMapFog[yBmMap][xBmMap] ? (6 << 12) : (11 << 12);

    out[0x00 + 0] = base + *tile++;
    out[0x00 + 1] = base + *tile++;
    out[0x20 + 0] = base + *tile++;
    out[0x20 + 1] = base + *tile++;
}
#endif // PC_PORT

void nullsub_8(void) {}

void DisplayMovementViewTile(u16* bg, int xBmMap, int yBmMap, int xTileMap, int yTileMap) {
    bg = bg + 2*(yTileMap * 0x20 + xTileMap);

    if (!bg)
        nullsub_8();

    // TODO: tile macros?
    // TODO: are the movement and range maps s8[][]?

    if (gMapMovementSigned[yBmMap][xBmMap] >= 0) {
        bg[0x00 + 0] = 0x4280;
        bg[0x00 + 1] = 0x4281;
        bg[0x20 + 0] = 0x4282;
        bg[0x20 + 1] = 0x4283;

        return;
    }

    if (gMapRangeSigned[yBmMap][xBmMap]) {
        if (bg[0]) {
            bg[0x00 + 0] = 0x5284;
            bg[0x00 + 1] = 0x5285;
            bg[0x20 + 0] = 0x5286;
            bg[0x20 + 1] = 0x5287;

            return;
        } else {
            bg[0x00 + 0] = 0x5280;
            bg[0x00 + 1] = 0x5281;
            bg[0x20 + 0] = 0x5282;
            bg[0x20 + 1] = 0x5283;

            return;
        }
    }

    bg[0x00 + 0] = 0;
    bg[0x00 + 1] = 0;
    bg[0x20 + 0] = 0;
    bg[0x20 + 1] = 0;
}

void RenderBmMap(void) {
    int ix, iy;

    gBmSt.mapRenderOrigin.x = gBmSt.camera.x >> 4;
    gBmSt.mapRenderOrigin.y = gBmSt.camera.y >> 4;

    for (iy = (10 - 1); iy >= 0; --iy)
        for (ix = (15 - 1); ix >= 0; --ix)
            DisplayBmTile(gBG3TilemapBuffer, ix, iy,
                (short) gBmSt.mapRenderOrigin.x + ix, (short) gBmSt.mapRenderOrigin.y + iy);

    BG_EnableSyncByMask(BG3_SYNC_BIT);
    BG_SetPosition(BG_3, 0, 0);

    SetDispEnable(TRUE, TRUE, TRUE, TRUE, TRUE);
}

void RenderBmMapOnBg2(void) {
    int ix, iy;

    SetBackgroundTileDataOffset(BG_2, 0x8000);

    gBmSt.mapRenderOrigin.x = gBmSt.camera.x >> 4;
    gBmSt.mapRenderOrigin.y = gBmSt.camera.y >> 4;

    for (iy = (10 - 1); iy >= 0; --iy)
        for (ix = (15 - 1); ix >= 0; --ix)
            DisplayBmTile(gBG2TilemapBuffer, ix, iy,
                (short) gBmSt.mapRenderOrigin.x + ix, (short) gBmSt.mapRenderOrigin.y + iy);

    BG_EnableSyncByMask(BG2_SYNC_BIT);
    BG_SetPosition(BG_2, 0, 0);
}

void UpdateBmMapDisplay(void) {
    // TODO: figure out

    if (gBmSt.camera.x != gBmSt.cameraPrevious.x) {
        if (gBmSt.camera.x > gBmSt.cameraPrevious.x) {
            if (((gBmSt.camera.x - 1) ^ (gBmSt.cameraPrevious.x - 1)) & 0x10)
                RenderBmMapColumn(15);
        } else {
            if ((gBmSt.camera.x ^ gBmSt.cameraPrevious.x) & 0x10)
                RenderBmMapColumn(0);
        }
    }

    if (gBmSt.camera.y != gBmSt.cameraPrevious.y) {
        if (gBmSt.camera.y > gBmSt.cameraPrevious.y) {
            if (((gBmSt.camera.y - 1) ^ (gBmSt.cameraPrevious.y - 1)) & 0x10)
                RenderBmMapLine(10);
        } else {
            if ((gBmSt.camera.y ^ gBmSt.cameraPrevious.y) & 0x10)
                RenderBmMapLine(0);
        }
    }

    gBmSt.cameraPrevious = gBmSt.camera;

    BG_SetPosition(BG_3,
        gBmSt.camera.x - gBmSt.mapRenderOrigin.x * 16,
        gBmSt.camera.y - gBmSt.mapRenderOrigin.y * 16
    );

    if (gBmSt.gameStateBits & BM_FLAG_0) {
        BG_SetPosition(BG_2,
            gBmSt.camera.x - gBmSt.mapRenderOrigin.x * 16,
            gBmSt.camera.y - gBmSt.mapRenderOrigin.y * 16
        );
    }
}

void RenderBmMapColumn(u16 xOffset) {
    u16 xBmMap = (gBmSt.camera.x >> 4) + xOffset;
    u16 yBmMap = (gBmSt.camera.y >> 4);

    u16 xTileMap = ((gBmSt.camera.x >> 4) - gBmSt.mapRenderOrigin.x + xOffset) & 0xF;
    u16 yTileMap = ((gBmSt.camera.y >> 4) - gBmSt.mapRenderOrigin.y);

    int iy;

    if (!(gBmSt.gameStateBits & 1)) {
        for (iy = 10; iy >= 0; --iy) {
            DisplayBmTile(gBG3TilemapBuffer,
                xTileMap, (yTileMap + iy) & 0xF,
                xBmMap, (yBmMap + iy));
        }

        BG_EnableSyncByMask(BG3_SYNC_BIT);
    } else {
        for (iy = 10; iy >= 0; --iy) {
            DisplayBmTile(gBG3TilemapBuffer,
                xTileMap, (yTileMap + iy) & 0xF,
                xBmMap, (yBmMap + iy));

            DisplayMovementViewTile(gBG2TilemapBuffer,
                xBmMap, (yBmMap + iy),
                xTileMap, (yTileMap + iy) & 0xF);
        }

        BG_EnableSyncByMask(BG2_SYNC_BIT | BG3_SYNC_BIT);
    }
}

void RenderBmMapLine(u16 yOffset) {
    u16 xBmMap = (gBmSt.camera.x >> 4);
    u16 yBmMap = (gBmSt.camera.y >> 4) + yOffset;

    u16 xTileMap = ((gBmSt.camera.x >> 4) - gBmSt.mapRenderOrigin.x);
    u16 yTileMap = ((gBmSt.camera.y >> 4) - gBmSt.mapRenderOrigin.y + yOffset) & 0xF;

    int ix;

    if (!(gBmSt.gameStateBits & 1)) {
        for (ix = 15; ix >= 0; --ix) {
            DisplayBmTile(gBG3TilemapBuffer,
                (xTileMap + ix) & 0xF, yTileMap,
                (xBmMap + ix), yBmMap);
        }

        BG_EnableSyncByMask(BG3_SYNC_BIT);
    } else {
        for (ix = 15; ix >= 0; --ix) {
            DisplayBmTile(gBG3TilemapBuffer,
                (xTileMap + ix) & 0xF, yTileMap,
                (xBmMap + ix), yBmMap);

            DisplayMovementViewTile(gBG2TilemapBuffer,
                (xBmMap + ix), yBmMap,
                (xTileMap + ix) & 0xF, yTileMap);
        }

        BG_EnableSyncByMask(BG2_SYNC_BIT | BG3_SYNC_BIT);
    }
}

#if defined(PC_PORT) && PORT_PLAYED_BUILD
/*
 * PC played builds only, Reforged Options "Danger zone on Select"
 * (port/reforged/reforged_dangerzone.c): called right after a RenderBmMap
 * (the range view's init) that the retail game would not have run here.
 * RenderBmMap draws 15x10 squares at origin = camera >> 4; with the camera
 * between squares (a scroll step under way) 16 columns / 11 rows are on
 * screen, and the retail game drew the 16th column / 11th row when the step
 * began (UpdateBmMapDisplay -> RenderBmMapColumn(15) / RenderBmMapLine(10)).
 * Draw them now, with the same retail functions (BG2 too while BM_FLAG_0 is
 * set), so the picture is the one the retail game shows. RenderBmMapLine
 * covers the corner square.
 */
void Reforged_RenderBmMapEdges(void) {
    if (gBmSt.camera.x & 15)
        RenderBmMapColumn(15);

    if (gBmSt.camera.y & 15)
        RenderBmMapLine(10);
}
#endif

void RefreshUnitsOnBmMap(void) {
    struct Unit* unit;
    int i;

    // 1. Blue & Green units

    for (i = 1; i < FACTION_RED; ++i) {
        unit = GetUnit(i);

        if (!UNIT_IS_VALID(unit))
            continue;

        if (unit->state & US_HIDDEN)
            continue;

        // Put unit on unit map
#ifdef PC_PORT
        /*
         * D-69 (was working ID D-R10-GONE-3; port-plan/pending/folded/r10-gone.md): when the Link Arena's
         * team set-up ends (AtMenu_ResetBmUiEffect), no map is loaded and
         * gBmMapUnit is the last 15x10 table, but deployed units keep their
         * last squares, (-1,12) or (14,12) say. Past the row table the row
         * word is 0, so the GBA's strb (0x08019FD4) goes to 0xFFFFFFFF or to
         * the BIOS area, which ignore it (measured: no RAM byte changes).
         * PortBmMapWrite makes or drops the same store (row load 0x08019FD0,
         * 0x7007 at +4); the two red loops below have the same store.
         */
        PortBmMapWrite(gBmMapUnit, unit->xPos, unit->yPos, i, PORT_THUMB_OPEN_BUS(0x7007));
#else
        gBmMapUnit[unit->yPos][unit->xPos] = i;
#endif

        // If fog is enabled, apply unit vision to fog map
        if (gPlaySt.chapterVisionRange)
            MapAddInRange(unit->xPos, unit->yPos, GetUnitFogViewRange(unit), 1);
    }

    // 2. Red (& Purple) units

    if (gPlaySt.faction != FACTION_RED) {
        // 2.1. No red phase

        for (i = FACTION_RED + 1; i < FACTION_PURPLE + 6; ++i) {
            unit = GetUnit(i);

            if (!UNIT_IS_VALID(unit))
                continue;

            if (unit->state & US_HIDDEN)
                continue;

            // If unit is magic seal, set fog in range 0-10.
            // Magic seal set the fog map probably solely for the alternate map palette.
            if (UNIT_CATTRIBUTES(unit) & CA_MAGICSEAL)
                MapAddInRange(unit->xPos, unit->yPos, 10, -1);

            if (gPlaySt.chapterVisionRange && !gBmMapFog[unit->yPos][unit->xPos]) {
                // If in fog, set unit bit on the hidden map, and set the "hidden in fog" state

                gBmMapHidden[unit->yPos][unit->xPos] |= HIDDEN_BIT_UNIT;
                unit->state = unit->state | US_BIT9;
            } else {
                // If not in fog, put unit on the map, and update state accordingly

#ifdef PC_PORT
                /* D-69, as above (strb 0x0801A0A4, row load 0x0801A0A0) */
                PortBmMapWrite(gBmMapUnit, unit->xPos, unit->yPos, i, PORT_THUMB_OPEN_BUS(0x7007));
#else
                gBmMapUnit[unit->yPos][unit->xPos] = i;
#endif

                if (unit->state & US_BIT9)
                    unit->state = (unit->state &~ US_BIT9) | US_BIT8;
            }
        }
    } else {
        // 2.2. Yes red phase

        // This does mostly the same as the "No red phase" loop, except:
        // - It always puts the units on the unit map
        // - It never sets the "spotted" unit state bit (even if unit is seen)

        for (i = FACTION_RED + 1; i < FACTION_PURPLE + 6; ++i) {
            unit = GetUnit(i);

            if (!UNIT_IS_VALID(unit))
                continue;

            if (unit->state & US_HIDDEN)
                continue;

            // See above
            if (UNIT_CATTRIBUTES(unit) & CA_MAGICSEAL)
                MapAddInRange(unit->xPos, unit->yPos, 10, -1);

            if (gPlaySt.chapterVisionRange) {
                // Update unit state according to fog level

                if (!gBmMapFog[unit->yPos][unit->xPos])
                    unit->state = unit->state | US_BIT9;
                else
                    unit->state = unit->state &~ US_BIT9;
            }

            // Put on unit map
#ifdef PC_PORT
            /* D-69, as above (strb 0x0801A15C, row load 0x0801A158) */
            PortBmMapWrite(gBmMapUnit, unit->xPos, unit->yPos, i, PORT_THUMB_OPEN_BUS(0x7007));
#else
            gBmMapUnit[unit->yPos][unit->xPos] = i;
#endif
        }
    }
}

void RefreshTorchlightsOnBmMap(void) {
    struct Trap* trap;
    
    for (trap = GetTrap(0); trap->type != TRAP_NONE; ++trap) {
        switch (trap->type) {

        case TRAP_TORCHLIGHT:
            MapAddInRange(trap->xPos, trap->yPos, trap->extra, 1);
            break;

        } // switch (trap->type)
    }
}

void RefreshMinesOnBmMap(void) {
    struct Trap* trap;
    
    for (trap = GetTrap(0); trap->type != TRAP_NONE; ++trap) {
        switch (trap->type) {

        case TRAP_MINE:
            if (!gBmMapUnit[trap->yPos][trap->xPos])
                gBmMapHidden[trap->yPos][trap->xPos] |= HIDDEN_BIT_TRAP;

            break;

        } // switch (trap->type)
    }
}

void RefreshEntityBmMaps(void) {
    // 1. Clear unit & hidden maps

    BmMapFill(gBmMapUnit, 0);
    BmMapFill(gBmMapHidden, 0);

    // 2. Clear fog map, with 1 (visible) if no fog, with 0 (hidden) if yes fog

    BmMapFill(gBmMapFog, !gPlaySt.chapterVisionRange ? 1 : 0);

    // 3. Populate unit, fog & hidden maps

    RefreshTorchlightsOnBmMap();
    RefreshUnitsOnBmMap();
    RefreshMinesOnBmMap();
}

char* GetTerrainName(int terrainId) {
    return GetStringFromIndex(gUnknown_0880D374[terrainId]);
}

int GetTerrainHealAmount(int terrainId) {
    return TerrainTable_HealAmount[terrainId];
}

s8 GetTerrainHealsStatus(int terrainId) {
    return TerrainTable_HealsStatus[terrainId];
}

void sub_801A278(void) {
    const u16* tile = sTilesetConfig;

    if (!sub_800D208() || (gBmSt.gameStateBits & BM_FLAG_PREPSCREEN)) {
        // TODO: macros?
        RegisterBlankTile(0x400 + (*tile++ & 0x3FF));
        RegisterBlankTile(0x400 + (*tile++ & 0x3FF));
        RegisterBlankTile(0x400 + (*tile++ & 0x3FF));
        RegisterBlankTile(0x400 + (*tile++ & 0x3FF));
    }

    // TODO: macro?
    gPaletteBuffer[PAL_BACKDROP_OFFSET] = 0;
    EnablePaletteSync();
}

void RevertMapChange(int id) {
    const struct MapChange* mapChange;
    u8 ix, iy;

    Decompress(GetChapterMapPointer(gPlaySt.chapterIndex), gBmMapBuffer);

    mapChange = GetMapChange(id);

    for (iy = mapChange->yOrigin; iy < (mapChange->yOrigin + mapChange->ySize); ++iy) {
        u16* itSource = (iy * gBmMapSize.x) + mapChange->xOrigin + (gBmMapBuffer + 1);
        u16* itDest   = gBmMapBaseTiles[iy] + mapChange->xOrigin;

        for (ix = mapChange->xOrigin; ix < (mapChange->xOrigin + mapChange->xSize); ++ix)
            *itDest++ = *itSource++;
    }
}
