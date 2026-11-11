#include "global.h"

/*
 * Reforged Options, "Danger zone on Select" (port-plan/reforged-options-plan.md
 * section 4, item 1). Played builds only; empty in every test build.
 *
 * With the option ON, Select shows the enemies' danger zone, the retail display
 * of PlayerPhase_DisplayDangerZone (GenerateDangerZoneRange, the movement map
 * cleared, DisplayMoveRangeGraphics(3): the attack range in red), in:
 *   - the unit movement screen (PlayerPhase_RangeDisplayIdle with a unit
 *     selected; src/playerphase.c), and
 *   - every map target selection (TargetSelection_Loop, src/uiselecttarget.c:
 *     attack, staves, and the others that use it).
 * Select again restores the screen exactly as it was; A, B, R or L first
 * restore it, so the retail action that follows runs on exactly what it would
 * have run on. The path arrow is not drawn while the zone is shown, but it
 * keeps following the cursor on the unit's own maps (Reforged_DangerZonePathArrow,
 * every frame in place of retail's DrawUpdatedPathArrow), so A moves the unit
 * along the same path as in a retail run with the same cursor moves.
 *
 * Display only, never a game value (the user's priority): before the zone is
 * drawn, everything the zone's code writes is saved: the movement, range and
 * "other" maps, the movement code's working state (gWorkingBmMap,
 * gWorkingTerrainMoveCosts, gMovMapFillState), gBmSt.gameStateBits (the range
 * view's end clears BM_FLAG_0/1) and the range view's flags. The restore puts
 * every one back and redraws the saved view from the saved maps. The unit map
 * square the zone code touches is put back at once. No RNG call, no unit or
 * gActionData field is written.
 *
 * Choices: the selected unit stands on its square while the enemies' moves are
 * worked out (the retail display, reached with no unit selected, has every unit
 * on the map), and the weapon range is always shown (the retail swap shows the
 * staff range on odd counts).
 *
 * With the option OFF nothing here runs: Select keeps its retail meaning (none
 * in these screens).
 */

#if PORT_PLAYED_BUILD

#include "hardware.h"
#include "proc.h"
#include "bmunit.h"
#include "bmmap.h"
#include "bmidoten.h"
#include "soundwrapper.h"
#include "playerphase.h"
#include "bmpatharrowdisp.h"
#include "constants/songs.h"
#include "constants/terrains.h"

#include "bmmind.h"
#include "rng.h"
#include "bmudisp.h"
#include "mu.h"
#include "event.h"
#include "statscreen.h"
#include "player_interface.h"

#include "port_reforged.h"

extern struct ProcCmd sProcScr_MoveLimitView[]; /* src/playerphase.c */
extern struct ProcCmd sProcScr_MoveLimitViewChange[]; /* its grow-in child */

/* The range view's child (MoveLimitViewChange, src/playerphase.c) plays the
   squares' grow-in: with BM_FLAG_0 clear at its start it writes
   gOpenLimitViewImgLut[2..7] to VRAM 0x5000, one a frame (unk_4C counts
   2..7, then it ends); with FLAG_0 set it writes the finished image
   (Img_LimitViewSquareFull = the lut's last entry) at once and ends. */
#define GROW_IN_LAST 7
#define GROW_IN_DONE (GROW_IN_LAST + 1)

#define RFO_MAP_MAX (64 * 64)

static u8 sZoneShown;

static struct {
    u8 movement[RFO_MAP_MAX];
    u8 range[RFO_MAP_MAX];
    u8 other[RFO_MAP_MAX];
    u8 **workingMap;
    u8 moveCosts[TERRAIN_COUNT];
    struct MovMapFillState fill;
    u32 stateBits;
    s16 viewFlags;
    u8 hadView;
    /* the unit's grow-in step (unk_4C) when the zone was shown, GROW_IN_DONE
       if it was over, and the game clock then: the retail grow-in goes on
       while the zone is shown, so the restore puts it where retail has it */
    s16 growStep;
    u32 clock;
    struct Vec2u origin; /* gBmSt.mapRenderOrigin and camera, for the BG2 copy */
    struct Vec2 camera;
    /* the display the range view changes: its palettes (MoveLimitView_OnLoop
       copies to byte offsets 0x82 and 0xA2: entries 0x41-0x60, BG palettes
       4-6), the BG2 tilemap it draws in, the window/blend settings */
    u16 pal[0x30];
    u16 bg2[32 * 32];
    struct LCDControlBuffer lcd;
} sSaved;

int Reforged_DangerZoneShown(void)
{
    return sZoneShown;
}

static u8 sFreeZone; /* the free-map view (below) is open */

void Reforged_DangerZoneReset(void)
{
    sZoneShown = 0;
    sFreeZone = 0;
}

/* FE8PC_RFO_CHECK=1 (a test switch for scripted runs): log hashes of the
   game values the option must never change, and of everything the zone's code
   writes: gActionData; every unit; the RNG seeds; the unit, movement, range
   and "other" maps (row by row, gBmMapSize.x wide); the movement code's
   working state (gMovMapFillState, which map gWorkingBmMap is, the working
   move costs); gBmSt except mapRenderOrigin (rendering only; the zone's
   RenderBmMap resets it); the path arrow (gpPathArrowProc).
   - ON: "before" (the zone is shown next), "shown", "restored". With the cursor
     not moved while the zone is shown, before == restored (a cursor move
     changes the cursor, the arrow and the arrow's maps, as in retail).
   - ON or OFF: "check <screen> <key>" after this file's step, on every A, B or
     Select in the two screens. OFF is the retail game (Select does nothing
     there), so an ON run and an OFF run of the same key script must give the
     same A and B lines: the state retail's A or B handling then works on.
   The build is not PIE (port/Makefile: -no-pie, a fixed text segment), so the
   pointers inside the units and the fill state hash the same in two runs.
   Unset: one getenv, the first time. */
void PortLog(const char *fmt, ...);
char *getenv(const char *name);

static int CheckOn(void)
{
    static int on = -1;

    if (on < 0)
        on = getenv("FE8PC_RFO_CHECK") != NULL;
    return on;
}

static u32 Fnv(u32 h, const void *p, int n)
{
    const u8 *b = p;

    while (n-- > 0)
        h = (h ^ *b++) * 16777619u;
    return h;
}

#define FNV_START 2166136261u

static u32 FnvMap(u32 h, u8 **map)
{
    int y;

    for (y = 0; y < gBmMapSize.y; y++)
        h = Fnv(h, map[y], gBmMapSize.x);
    return h;
}

/* which map gWorkingBmMap is (an id, not the address) */
static u8 WorkingMapId(void)
{
    if (gWorkingBmMap == gBmMapMovement)
        return 1;
    if (gWorkingBmMap == gBmMapRange)
        return 2;
    if (gWorkingBmMap == gBmMapOther)
        return 3;
    if (gWorkingBmMap == gBmMapUnit)
        return 4;
    return gWorkingBmMap == NULL ? 0 : 5;
}

static void LogGameHash(const char *when, const char *key)
{
    u16 seeds[3];
    u32 act, units, rng, umap, maps, move, bmst, arrow;
    u8 id;

    if (!CheckOn())
        return;
    StoreRNState(seeds);
    act = Fnv(FNV_START, &gActionData, sizeof(gActionData));
    units = Fnv(FNV_START, gUnitArrayBlue, sizeof(gUnitArrayBlue));
    units = Fnv(units, gUnitArrayRed, sizeof(gUnitArrayRed));
    units = Fnv(units, gUnitArrayGreen, sizeof(gUnitArrayGreen));
    rng = Fnv(FNV_START, seeds, sizeof(seeds));
    umap = FnvMap(FNV_START, gBmMapUnit);
    maps = FnvMap(FNV_START, gBmMapMovement);
    maps = FnvMap(maps, gBmMapRange);
    maps = FnvMap(maps, gBmMapOther);
    id = WorkingMapId();
    move = Fnv(FNV_START, &gMovMapFillState, sizeof(gMovMapFillState));
    move = Fnv(move, &id, 1);
    move = Fnv(move, gWorkingTerrainMoveCosts, TERRAIN_COUNT);
    bmst = Fnv(FNV_START, &gBmSt, __builtin_offsetof(struct BmSt, mapRenderOrigin));
    bmst = Fnv(bmst, &gBmSt.cameraMax, sizeof(gBmSt) - __builtin_offsetof(struct BmSt, cameraMax));
    arrow = Fnv(FNV_START, &gpPathArrowProc->lastX,
        sizeof(*gpPathArrowProc) - __builtin_offsetof(struct PathArrowProc, lastX));
    PortLog("[reforged] danger zone %s%s: act %08X units %08X rng %08X umap %08X maps %08X "
            "move %08X bmst %08X arrow %08X | cursor %d,%d arrow end %d,%d len %d move %d,%d\n",
        when, key, (unsigned)act, (unsigned)units, (unsigned)rng, (unsigned)umap, (unsigned)maps,
        (unsigned)move, (unsigned)bmst, (unsigned)arrow,
        gBmSt.playerCursor.x, gBmSt.playerCursor.y,
        gpPathArrowProc->pathX[gpPathArrowProc->pathLen],
        gpPathArrowProc->pathY[gpPathArrowProc->pathLen], gpPathArrowProc->pathLen,
        gActionData.xMove, gActionData.yMove);
}

/* the ON-or-OFF check line (see above), after this file's step */
static void LogCheck(const char *screen)
{
    u16 keys = gKeyStatusPtr->newKeys;

    if (!CheckOn() || !(keys & (A_BUTTON | B_BUTTON | SELECT_BUTTON)))
        return;
    LogGameHash(screen, (keys & A_BUTTON) ? " A" : (keys & B_BUTTON) ? " B" : " Select");
}

static int MapFits(void)
{
    return gBmMapSize.x > 0 && gBmMapSize.y > 0 && gBmMapSize.x * gBmMapSize.y <= RFO_MAP_MAX;
}

static void CopyMap(u8 *flat, u8 **map, int save)
{
    int y;

    for (y = 0; y < gBmMapSize.y; y++)
    {
        if (save)
            memcpy(flat + y * gBmMapSize.x, map[y], gBmMapSize.x);
        else
            memcpy(map[y], flat + y * gBmMapSize.x, gBmMapSize.x);
    }
}

static void ShowZone(void)
{
    struct MoveLimitViewProc *view = Proc_Find(sProcScr_MoveLimitView);
    struct MoveLimitViewProc *change;
    u8 savedUnit;

    LogGameHash("before", "");
    CopyMap(sSaved.movement, gBmMapMovement, 1);
    CopyMap(sSaved.range, gBmMapRange, 1);
    CopyMap(sSaved.other, gBmMapOther, 1);
    sSaved.workingMap = gWorkingBmMap;
    memcpy(sSaved.moveCosts, gWorkingTerrainMoveCosts, TERRAIN_COUNT);
    sSaved.fill = gMovMapFillState;
    sSaved.stateBits = gBmSt.gameStateBits;
    sSaved.hadView = view != NULL;
    sSaved.viewFlags = view != NULL ? view->flags : 0;
    change = Proc_Find(sProcScr_MoveLimitViewChange);
    sSaved.growStep = (view != NULL && change != NULL) ? change->unk_4C : GROW_IN_DONE;
    sSaved.clock = GetGameClock();
    memcpy(sSaved.pal, gPaletteBuffer + 0x40, sizeof(sSaved.pal));
    memcpy(sSaved.bg2, gBG2TilemapBuffer, sizeof(sSaved.bg2));
    sSaved.origin = gBmSt.mapRenderOrigin;
    sSaved.camera = gBmSt.camera;
    sSaved.lcd = gLCDControlBuffer;

    savedUnit = gBmMapUnit[gActiveUnit->yPos][gActiveUnit->xPos];
    gBmMapUnit[gActiveUnit->yPos][gActiveUnit->xPos] = gActiveUnit->index;
    GenerateDangerZoneRange(0);
    gBmMapUnit[gActiveUnit->yPos][gActiveUnit->xPos] = savedUnit;

    BmMapFill(gBmMapMovement, -1);

    HideMoveRangeGraphics();
    DisplayMoveRangeGraphics(LIMITVIEW_BLUE | LIMITVIEW_RED);
    /* The view's init set BM_FLAG_0 (the range layer follows the camera:
       bmmap.c UpdateBmMapDisplay, RenderBmMapColumn/Line) and the end of the
       one it replaced cleared BM_FLAG_0/1. Keep the saved bits, plus FLAG_0
       so the zone follows the camera in a target selection that had no view
       (trade, rescue ...: FLAG_0 clear there). FLAG_0 is read by display code
       only; RestoreScreen puts the exact saved bits back. */
    gBmSt.gameStateBits = sSaved.stateBits | BM_FLAG_0;
    /* the view's RenderBmMap drew 15x10 squares; mid-scroll, draw the 16th
       column / 11th row too (BG3 and the zone in BG2) */
    Reforged_RenderBmMapEdges();
    sZoneShown = 1;
    LogGameHash("shown", "");
}

/* The step retail's grow-in child uses this frame: the saved step plus the
   frames the zone was shown (the child steps once a frame, after this code:
   proc tree 4 runs after trees 2 and 3, src/bm.c OnMain); GROW_IN_DONE when
   it is over (its last frame wrote the finished image). */
static int GrowInStep(void)
{
    u32 elapsed = GetGameClock() - sSaved.clock;

    if (sSaved.growStep >= GROW_IN_DONE || elapsed > (u32)(GROW_IN_LAST - sSaved.growStep))
        return GROW_IN_DONE;
    return sSaved.growStep + elapsed;
}

static void RestoreScreen(void)
{
    struct MoveLimitViewProc *change;
    int step;

    CopyMap(sSaved.movement, gBmMapMovement, 0);
    CopyMap(sSaved.range, gBmMapRange, 0);
    CopyMap(sSaved.other, gBmMapOther, 0);

    HideMoveRangeGraphics();
    if (sSaved.hadView)
    {
        /* The zone view's end cleared BM_FLAG_0, and a new view with FLAG_0
           clear plays the range's grow-in (MoveLimitViewChange_OnInit) from
           its first step. If the unit's own grow-in would be over by now
           (the usual case), FLAG_0 set makes the new view put the finished
           tiles in at once, as the unit's view had them; if it would still
           be running (the zone shown within its 6 frames), the new view's
           child is put at the step retail's child uses this frame, and it
           goes on from there. The exact saved bits go back below. */
        step = GrowInStep();
        if (step >= GROW_IN_DONE)
            gBmSt.gameStateBits |= BM_FLAG_0;
        else
            gBmSt.gameStateBits &= ~BM_FLAG_0;
        DisplayMoveRangeGraphics(sSaved.viewFlags);
        if (step < GROW_IN_DONE)
        {
            change = Proc_Find(sProcScr_MoveLimitViewChange);
            if (change != NULL)
                change->unk_4C = step;
        }
        /* its RenderBmMap drew 15x10 squares; mid-scroll, the 16th column /
           11th row of BG3 and BG2 too, as the retail scroll step drew them */
        Reforged_RenderBmMapEdges();
    }

    memcpy(gPaletteBuffer + 0x40, sSaved.pal, sizeof(sSaved.pal));
    EnablePaletteSync();
    /* BG2 is a ring buffer anchored at gBmSt.mapRenderOrigin (bmmap.c
       RenderBmMapColumn), and the zone's RenderBmMap reset the origin to the
       camera. The saved tilemap is put back only when the origin and the
       camera are those it was saved with; otherwise the redraw above (the
       saved view's init: RenderBmMap, then the visible squares from the
       restored maps at the current origin; with no view, the zone's end left
       BG2 clear, as it was) is the right picture. */
    if (gBmSt.mapRenderOrigin.x == sSaved.origin.x && gBmSt.mapRenderOrigin.y == sSaved.origin.y &&
        gBmSt.camera.x == sSaved.camera.x && gBmSt.camera.y == sSaved.camera.y)
    {
        memcpy(gBG2TilemapBuffer, sSaved.bg2, sizeof(sSaved.bg2));
        BG_EnableSyncByMask(BG2_SYNC_BIT);
    }
    gLCDControlBuffer = sSaved.lcd;

    gWorkingBmMap = sSaved.workingMap;
    memcpy(gWorkingTerrainMoveCosts, sSaved.moveCosts, TERRAIN_COUNT);
    gMovMapFillState = sSaved.fill;
    gBmSt.gameStateBits = sSaved.stateBits;
    sZoneShown = 0;
    LogGameHash("restored", "");
}

/* Exchanges the live maps and movement working state (the zone's while it is
   shown) with the saved ones (the unit's). */
static void SwapMap(u8 *flat, u8 **map)
{
    int x, y;
    u8 t;

    for (y = 0; y < gBmMapSize.y; y++)
    {
        for (x = 0; x < gBmMapSize.x; x++)
        {
            t = map[y][x];
            map[y][x] = flat[y * gBmMapSize.x + x];
            flat[y * gBmMapSize.x + x] = t;
        }
    }
}

static void SwapUnitState(void)
{
    u8 **workingMap = gWorkingBmMap;
    u8 moveCosts[TERRAIN_COUNT];
    struct MovMapFillState fill = gMovMapFillState;

    SwapMap(sSaved.movement, gBmMapMovement);
    SwapMap(sSaved.range, gBmMapRange);
    SwapMap(sSaved.other, gBmMapOther);
    gWorkingBmMap = sSaved.workingMap;
    sSaved.workingMap = workingMap;
    memcpy(moveCosts, gWorkingTerrainMoveCosts, TERRAIN_COUNT);
    memcpy(gWorkingTerrainMoveCosts, sSaved.moveCosts, TERRAIN_COUNT);
    memcpy(sSaved.moveCosts, moveCosts, TERRAIN_COUNT);
    gMovMapFillState = sSaved.fill;
    sSaved.fill = fill;
}

/* Called by PlayerPhase_RangeDisplayIdle (src/playerphase.c) in place of
   DrawUpdatedPathArrow while the zone is shown in the movement screen: the
   retail arrow update (UpdatePathArrowWithCursor), run on the unit's saved
   maps and working state, which it updates as it would have updated the live
   ones; nothing is drawn. So the arrow, gBmMapOther, gWorkingBmMap and the
   rest are, at the restore, what a retail run with the same cursor moves has,
   and A (ACT_MOVE: PlayerPhase_DisplayUnitMovement walks the arrow) moves the
   unit to the cursor square as retail does. */
void Reforged_DangerZonePathArrow(void)
{
    if (!sZoneShown)
        return;
    /* UpdatePathArrowWithCursor's own first test (nothing to do) */
    if (gpPathArrowProc->lastX == gBmSt.playerCursor.x && gpPathArrowProc->lastY == gBmSt.playerCursor.y)
        return;
    SwapUnitState();
    UpdatePathArrowWithCursor();
    SwapUnitState();
}

static void Step(void)
{
    u16 keys = gKeyStatusPtr->newKeys;

    if (!Reforged_GetOption(RFO_DANGER_ZONE_SELECT) || gActiveUnit == NULL || !MapFits())
        return;

    if (sZoneShown && (keys & (A_BUTTON | B_BUTTON | R_BUTTON | L_BUTTON)))
    {
        RestoreScreen();
        return;
    }

    if (keys & SELECT_BUTTON)
    {
        if (sZoneShown)
            RestoreScreen();
        else
            ShowZone();
        PlaySoundEffect(SONG_68);
    }
}

void Reforged_DangerZoneSelect(void)
{
    Step();
    LogCheck("check movement");
}

void Reforged_DangerZoneTargetSelect(void)
{
    Step();
    LogCheck("check target");
}

/*
 * Select on the free map (no unit picked): every enemy's range, as the
 * DS games and later show it. This is the retail game's own danger-zone view,
 * which no retail key or menu reaches (bmmenu.c MapMenu_DangerZone_UnusedEffect
 * is in no menu): gActiveUnit NULL, gBmSt.swapActionRangeCount 0, the player
 * phase at label 12 (PlayerPhase_DisplayDangerZone, then PlayerPhase_RangeDisplayIdle).
 * The cursor moves freely; R shows the stat screen of the unit under it (and
 * comes back to the zone, retail label 6 with no unit); A swaps to the
 * enemies' staff range and back when they have both (retail ACT_SWAP_RANGES),
 * else closes; B or Select closes. RangeDisplayIdle's B and R read gActiveUnit
 * (NULL here: an unreachable retail path), so this file handles B, R and the
 * closing A itself (Reforged_DangerZoneFreeMapIdle).
 *
 * Display only, as above: at the open, the movement, range and "other" maps,
 * the movement code's working state, gActiveUnit, swapActionRangeCount and
 * gBmSt.gameStateBits are saved; the close puts every one back, after the
 * retail cancel's redraw (HideMoveRangeGraphics, RefreshEntityBmMaps,
 * RefreshUnitSprites). With FE8PC_RFO_CHECK, "free before" and "free restored"
 * lines: equal with the cursor not moved in between.
 */
bool CanShowUnitStatScreen(struct Unit *unit); /* src/playerphase.c (the header's is commented out) */

static struct {
    struct Unit *activeUnit;
    u8 swapCount;
    u32 stateBits;
} sFreeSaved;

int Reforged_DangerZoneFreeMap(ProcPtr proc)
{
    struct Unit *unit;

    if (!Reforged_GetOption(RFO_DANGER_ZONE_SELECT) || !MapFits())
        return 0;
    if (!(gKeyStatusPtr->newKeys & SELECT_BUTTON) ||
        (gKeyStatusPtr->heldKeys & (A_BUTTON | B_BUTTON | START_BUTTON)))
        return 0;

    LogGameHash("free before", "");
    CopyMap(sSaved.movement, gBmMapMovement, 1);
    CopyMap(sSaved.range, gBmMapRange, 1);
    CopyMap(sSaved.other, gBmMapOther, 1);
    sSaved.workingMap = gWorkingBmMap;
    memcpy(sSaved.moveCosts, gWorkingTerrainMoveCosts, TERRAIN_COUNT);
    sSaved.fill = gMovMapFillState;
    sFreeSaved.activeUnit = gActiveUnit;
    sFreeSaved.swapCount = gBmSt.swapActionRangeCount;
    sFreeSaved.stateBits = gBmSt.gameStateBits;

    /* PlayerPhase_MainIdle's A on an empty square, before the map menu (from
       which the unused retail command would have run) */
    EndPlayerPhaseSideWindows();
    unit = GetUnit(gBmMapUnit[gBmSt.playerCursor.y][gBmSt.playerCursor.x]);
    if (unit)
    {
        EndAllMus();
        ShowUnitSprite(unit);
    }

    /* MapMenu_DangerZone_UnusedEffect */
    gActiveUnit = NULL;
    gBmSt.swapActionRangeCount = 0;
    sFreeZone = 1;
    Proc_Goto(proc, 12);
    return 1;
}

static void FreeZoneClose(ProcPtr proc)
{
    /* RangeDisplayIdle's ACT_CANCEL with no unit */
    gBmSt.gameStateBits &= ~BM_FLAG_3;
    HideMoveRangeGraphics();
    RefreshEntityBmMaps();
    RefreshUnitSprites();
    PlaySoundEffect(SONG_SE_SYS_WINDOW_CANSEL1);

    CopyMap(sSaved.movement, gBmMapMovement, 0);
    CopyMap(sSaved.range, gBmMapRange, 0);
    CopyMap(sSaved.other, gBmMapOther, 0);
    gWorkingBmMap = sSaved.workingMap;
    memcpy(gWorkingTerrainMoveCosts, sSaved.moveCosts, TERRAIN_COUNT);
    gMovMapFillState = sSaved.fill;
    gActiveUnit = sFreeSaved.activeUnit;
    gBmSt.swapActionRangeCount = sFreeSaved.swapCount;
    gBmSt.gameStateBits = sFreeSaved.stateBits;
    sFreeZone = 0;
    LogGameHash("free restored", "");

    Proc_Goto(proc, 9);
}

int Reforged_DangerZoneFreeMapIdle(ProcPtr proc)
{
    u16 keys = gKeyStatusPtr->newKeys;
    struct Unit *unit;

    if (!sFreeZone)
        return 0;
    if (gActiveUnit != NULL)
    {
        sFreeZone = 0; /* not this view (cannot happen: label 12 is ours alone) */
        return 0;
    }

    if ((keys & (B_BUTTON | SELECT_BUTTON)) ||
        ((keys & A_BUTTON) && GetCombinedEnemyWeaponUsabilityBits() != 3))
    {
        FreeZoneClose(proc);
        return 1;
    }

    if (keys & R_BUTTON)
    {
        /* RangeDisplayIdle's ACT_INFOSCREEN, without its unit's square. Every R is
           handled here: the retail one reads gActiveUnit (NULL here) when the cursor
           is on gActiveUnitMoveOrigin, so with no stat screen to show this draws the
           frame's cursor (RangeDisplayIdle's last step) and ends the frame. */
        unit = GetUnit(gBmMapUnit[gBmSt.playerCursor.y][gBmSt.playerCursor.x]);
        if (EventEngineExists() == 1 || gBmMapUnit[gBmSt.playerCursor.y][gBmSt.playerCursor.x] == 0 ||
            !CanShowUnitStatScreen(unit))
        {
            PutMapCursor(gBmSt.playerCursorDisplay.x, gBmSt.playerCursorDisplay.y, 1);
            return 1;
        }
        EndAllMus();
        SetStatScreenConfig(
            STATSCREEN_CONFIG_NONDEAD | STATSCREEN_CONFIG_NONBENCHED | STATSCREEN_CONFIG_NONUNK9 |
            STATSCREEN_CONFIG_NONROOFED | STATSCREEN_CONFIG_NONUNK16);
        StartStatScreen(unit, proc);
        Proc_Goto(proc, 6);
        return 1;
    }
    return 0;
}

#endif /* PORT_PLAYED_BUILD */
