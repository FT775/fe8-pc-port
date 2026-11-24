#include "global.h"

/*
 * Reforged Options: "Mouse" (RFO_MOUSE, the Controls screen's row; v1.29.0, ~/fe8-notes/mouse-design.txt).
 * Off by default. Played builds only; empty in every test build.
 *
 * The mouse never touches game memory. Clicks, the wheel and menu hover become SYNTHETIC GBA KEY PRESSES
 * (D-pad taps, A, B, R) that the window backend ORs into the live keys, where the keyboard's and the
 * gamepads' go (port/sdl3/platform_sdl3.c Platform_ReadKeys -> port/hw/frame.c UpdateLiveKeys ->
 * REG_KEYINPUT). The game's own UpdateKeyStatus is the only reader of REG_KEYINPUT, so a session played
 * with the mouse is a key sequence a pad could have pressed: FE8PC_KEYREC records it as a key file, which
 * replays the same game with the mouse off (port/tools/mousecheck.sh). Nothing reads the clock; the
 * planner's clock is the game's key reads (Reforged_MouseKeyRead, src/hardware.c UpdateKeyStatus).
 *
 * To know WHAT to press, four hooks in the game code (src/playerphase.c, src/uimenu.c,
 * src/uiselecttarget.c; each under #if defined(PC_PORT) && PORT_PLAYED_BUILD) say which input screen ran
 * its idle loop in the OnMain that just read the keys, with a host copy of its geometry (menu items'
 * rows, the target list's squares); the map's cursor is read from gBmSt. Nothing is written to EWRAM,
 * IWRAM, I/O or gBmSt; a proc is never read after its hook (only copies).
 *
 * What a mouse event does (the owner's choices of 2026-11-24):
 *   right-click   B, everywhere (it first cancels what the mouse was doing)
 *   middle-click  R (unit info / help); on the map: the cursor to the square, then R
 *   wheel         one Up / Down tap per notch (a horizontal wheel: Left / Right)
 *   left-click    the free map: a unit's square, or the cursor's own square: the cursor goes there, then A;
 *                 an empty square: the cursor goes there (a second click on it: A, the map menu);
 *                 choosing the destination: the cursor goes there, then A;
 *                 another unit's range shown (an enemy's, or the danger zone): B closes it first, then the
 *                 click acts as on the free map;
 *                 the Controls screen: A only on the hand's own row (elsewhere nothing);
 *                 a menu (unit actions, items, the map menu, any game menu made by StartMenuCore with 9
 *                 items or fewer and a hand): the hand to the item, then A; outside its items nothing;
 *                 a target list (attack, staff, trade, rescue ...): a target that is not the current one
 *                 becomes the current one (the forecast shows); a click on the current target, after it
 *                 was clicked once, is A; a square that is no target: nothing;
 *                 any other screen (dialogue, battle, title, save menu, stat screen, prep, world map, ...):
 *                 A, inside the game's 240x160 picture
 *   hover         on a menu, moving the pointer onto another item moves the hand there (the pointer resting
 *                 still never presses anything)
 *   side buttons  nothing
 * Every press is a NEW press (press and release ticks alternate), the D-pad taps one square at a time
 * (the cursor travels as it would with the pad, 2 frames a square); A waits for the cursor to stand on its
 * square. Any key or gamepad input cancels the mouse's plan and queue. A plan is dropped silently when the
 * screen it was made for is gone, when a tap changes nothing (the map edge, a tutorial's ignore mask) or
 * when it runs too long; while no input screen runs for a moment (the camera moving onto a target holds
 * the target list's loop) it waits.
 * A double-click: when a click chose something (the cursor's A on the map, a menu item), a second left click
 * at the same place within MDBL_READS key reads of the first is dropped (so a double-clicked unit is
 * selected, not selected and then told to stay). The clock is the key reads, never the wall clock.
 *
 * FE8PC_MOUSE_LOG=1 (a test switch): each event, the screen it met, each press and each screen change
 * (with read-only details: the cursor, the menu's items, the targets) in the log.
 * FE8PC_MOUSE_SELFTEST=1|exit (a test switch): checks the pure helpers at the first key read (exit: then
 * leaves, 0 when they passed).
 */

#if PORT_PLAYED_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proc.h"
#include "hardware.h"
#include "bm.h"
#include "bmunit.h"
#include "bmmap.h"
#include "uimenu.h"
#include "uiselecttarget.h"
#include "playerphase.h"

#include "port_hw.h"
#include "port_reforged.h"
#include "port_state.h"
#include "../render/render.h"
#include "../platform.h"

#define MQ_MAX       24  /* queued mouse events */
#define MWHEEL_MAX   8   /* wheel notches queued at most */
#define MWAIT_CTX    30  /* key reads a click waits for an input screen's hook while one ran recently */
#define MWAIT_PLAN   120 /* key reads a running plan waits while no input screen's hook runs (a camera move) */
#define MDBL_READS   30  /* a double-click: the second click within this many key reads (~0.5 s) */
#define MDBL_PIXELS  4   /* ... and this near the first */
#define MTGT_MAX     MAX_TARGET_LIST_COUNT
#define MPIC_H       160
#define MGAME_W      240

enum { MCTX_NONE, MCTX_MAP_IDLE, MCTX_MAP_MOVE, MCTX_MENU, MCTX_TARGETS, MCTX_HANDROW };
static const char *const sCtxNames[] = { "none", "map", "move", "menu", "targets", "hand row" };
enum { MPLAN_NONE, MPLAN_TAP, MPLAN_TRAVEL, MPLAN_MENU, MPLAN_TARGET };
enum { MB_LEFT = 1, MB_MIDDLE = 2, MB_RIGHT = 3 };
enum { MCL_DONE, MCL_PLAN, MCL_WAIT, MCL_PLAN_KEEP };

struct MouseEv {
    unsigned char kind, button, inStrip, sqOk, waited;
    unsigned char closed; /* a left click on the map: the range shown was closed for it first */
    signed char steps; /* the wheel: +1 / -1 */
    short gx, gy;      /* the game's 240x160 screen (Widescreen: the strip's own x) */
    short sqx, sqy;    /* the map square under it (sqOk), from the camera of the picture clicked */
    unsigned at;       /* sM.reads when it came (the double-click rule) */
};

struct MouseSnap {
    int kind;
    unsigned stamp;    /* sM.reads when the hook ran */
    unsigned session;  /* menus / target lists: one number per uninterrupted run of the hook */
    /* menu */
    signed char rx, ry, rw, rh;
    unsigned char state, count, cur;
    short itemY[MENU_ITEM_MAX];
    unsigned short itemMsg[MENU_ITEM_MAX];
    const char *itemName[MENU_ITEM_MAX];
    /* targets */
    int ntargets;
    signed char tx[MTGT_MAX], ty[MTGT_MAX], tuid[MTGT_MAX];
    /* the hand's row (the Controls screen): game-screen pixel rows y0 <= y < y1 */
    short hy0, hy1;
};

struct MousePlan {
    int kind, ctx;
    unsigned session;
    int pressed;       /* the press went out; the next tick releases */
    int finalKey, finalSent;
    int tx, ty;        /* travel: the square; target: the target's square */
    int item;          /* menu: the item */
    int budget;
    int tapped;        /* a tap went out: the next decision checks it moved something */
    int bx, by;        /* what it was before that tap (cursor square, item, target square) */
    unsigned short tapKey;
};

static struct {
    unsigned reads;         /* the game's key reads (UpdateKeyStatus): the planner's clock */
    unsigned lastTick;      /* reads at the last tick */
    unsigned short mask;    /* the mask given since lastTick (held until a read consumed it) */
    unsigned short lastOut; /* the live keys of the last call (mouse | real): what the next read consumes */
    int on, logOn, logRead, selfTest;
    int haveCam;
    short camX, camY;       /* gBmSt.camera after the last OnMain: what the next picture shows */
    struct MouseSnap ctx;   /* the last hook's copy */
    unsigned lastCtxRead;   /* reads when any input screen's hook last ran */
    unsigned menuSession, menuLast, tgtSession;
    unsigned tgtSig;        /* the last target list's squares (order-free), to tell another list */
    int dblValid, dblGx, dblGy; /* the last left click that chose something (the double-click rule) */
    unsigned dblAt;
    struct MouseEv q[MQ_MAX];
    int qn;
    struct MousePlan plan;
    unsigned hoverSession;
    int lastHover;          /* the menu item the pointer was last over (-1: none) */
    int armed, armX, armY;  /* targets: the target clicked once; a second click on it is A */
    unsigned armSession;
    int abortPending;       /* a right-click: the plan is dropped at the next tick */
    int lastTargetUid, lastTargetHp;
    /* the log's last screen */
    int logKind;
    unsigned logSession;
    int logCur, logX, logY;
} sM;
PORT_STATE_SESSION(sM)

/* --- pure helpers (FE8PC_MOUSE_SELFTEST) --- */

static int FloorDiv(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

/* The game's screen pixel of a picture pixel: Widescreen's picture is wider, the 240 strip at x = off. */
static void PictureToGame(int px, int py, int presentW, int off, int *gx, int *gy)
{
    *gx = presentW > MGAME_W ? px - off : px;
    *gy = py;
}

/* The map square under a game-screen pixel with the camera at (camX, camY); 0 outside the map. */
static int GameToSquare(int gx, int gy, int camX, int camY, int mapW, int mapH, int *sx, int *sy)
{
    *sx = FloorDiv(camX + gx, 16);
    *sy = FloorDiv(camY + gy, 16);
    return mapW > 0 && mapH > 0 && *sx >= 0 && *sy >= 0 && *sx < mapW && *sy < mapH;
}

/* The shorter way round a list of n from cur to want: +k (Down / next) or -k (Up / prev). */
static int ListSteps(int cur, int want, int n)
{
    int fwd, back;

    if (n <= 0)
        return 0;
    fwd = ((want - cur) % n + n) % n;
    back = ((cur - want) % n + n) % n;
    return fwd <= back ? fwd : -back;
}

/* The menu item at a game-screen pixel, -1 for none. */
static int MenuHitTest(const struct MouseSnap *s, int gx, int gy)
{
    int i;

    if (gx < s->rx * 8 || gx >= (s->rx + s->rw) * 8)
        return -1;
    for (i = 0; i < s->count; i++)
        if (gy >= s->itemY[i] * 8 && gy < s->itemY[i] * 8 + 16)
            return i;
    return -1;
}

static void SelfTest(void)
{
    struct MouseSnap m;
    int fails = 0, gx, gy, sx, sy;
    const char *env = getenv("FE8PC_MOUSE_SELFTEST");

#define MCHECK(c) do { if (!(c)) { fails++; PortLog("[mouse] self-test FAIL: %s (line %d)\n", #c, __LINE__); } } while (0)
    MCHECK(FloorDiv(-1, 16) == -1 && FloorDiv(-16, 16) == -1 && FloorDiv(-17, 16) == -2 && FloorDiv(15, 16) == 0);
    PictureToGame(10, 20, 240, 22, &gx, &gy);
    MCHECK(gx == 10 && gy == 20);
    PictureToGame(10, 20, 284, 22, &gx, &gy);
    MCHECK(gx == -12 && gy == 20);
    PictureToGame(283, 0, 284, 44, &gx, &gy);
    MCHECK(gx == 239);
    MCHECK(GameToSquare(0, 0, 32, 16, 20, 20, &sx, &sy) && sx == 2 && sy == 1);
    MCHECK(GameToSquare(-12, 159, 32, 16, 20, 20, &sx, &sy) && sx == 1 && sy == 10);
    MCHECK(!GameToSquare(-12, 0, 0, 0, 20, 20, &sx, &sy) && sx == -1);
    MCHECK(!GameToSquare(239, 0, 16 * 6, 0, 20, 20, &sx, &sy) && sx == 20);
    MCHECK(ListSteps(0, 2, 5) == 2 && ListSteps(0, 4, 5) == -1 && ListSteps(3, 3, 5) == 0 && ListSteps(4, 0, 5) == 1);
    MCHECK(ListSteps(1, 3, 4) == 2); /* a tie goes forward */
    memset(&m, 0, sizeof(m));
    m.rx = 20, m.ry = 0, m.rw = 7, m.count = 3;
    m.itemY[0] = 1, m.itemY[1] = 3, m.itemY[2] = 5;
    MCHECK(MenuHitTest(&m, 160, 8) == 0 && MenuHitTest(&m, 215, 23) == 0 && MenuHitTest(&m, 216, 8) == -1);
    MCHECK(MenuHitTest(&m, 159, 30) == -1 && MenuHitTest(&m, 170, 24) == 1 && MenuHitTest(&m, 170, 55) == 2);
    MCHECK(MenuHitTest(&m, 170, 56) == -1 && MenuHitTest(&m, 170, 7) == -1);
#undef MCHECK
    PortLog("[mouse] self-test: %d failures\n", fails);
    if (env != NULL && strcmp(env, "exit") == 0)
        PortExit(fails ? 1 : 0);
}

/* --- the log --- */

static int LogOn(void)
{
    if (!sM.logRead)
    {
        const char *s = getenv("FE8PC_MOUSE_LOG");

        sM.logRead = 1;
        sM.logOn = s != NULL && atoi(s) != 0;
    }
    return sM.logOn;
}

#define MLOG(...) do { if (LogOn()) PortLog(__VA_ARGS__); } while (0)

static const char *KeyName(unsigned short k)
{
    switch (k)
    {
    case PLATFORM_KEY_A: return "A";
    case PLATFORM_KEY_B: return "B";
    case PLATFORM_KEY_R: return "R";
    case PLATFORM_KEY_UP: return "Up";
    case PLATFORM_KEY_DOWN: return "Down";
    case PLATFORM_KEY_LEFT: return "Left";
    case PLATFORM_KEY_RIGHT: return "Right";
    case 0: return "-";
    default: return "?";
    }
}

static int PlayerUnitsDone(void)
{
    int i, n = 0;

    for (i = 1; i < 0x40; i++)
    {
        struct Unit *u = GetUnit(i);

        if (u != NULL && u->pCharacterData != NULL && (u->state & US_UNSELECTABLE) && !(u->state & US_DEAD))
            n++;
    }
    return n;
}

/* A line when the screen the hooks report changes (and a menu's hand or the current target moves). */
static void LogScreen(void)
{
    const struct MouseSnap *c = &sM.ctx;
    int kind = c->stamp == sM.reads ? c->kind : MCTX_NONE, cur = -1, x = -1, y = -1, i;
    char buf[400];
    int len = 0;

    if (kind == MCTX_MAP_IDLE || kind == MCTX_MAP_MOVE)
        x = gBmSt.playerCursor.x, y = gBmSt.playerCursor.y;
    else if (kind == MCTX_MENU)
        cur = c->cur;
    else if (kind == MCTX_HANDROW)
        cur = c->hy0;
    else if (kind == MCTX_TARGETS && c->ntargets > 0)
        x = c->tx[0], y = c->ty[0];
    if (kind == MCTX_MAP_IDLE || kind == MCTX_MAP_MOVE)
    {
        /* the map: only a change of screen (the cursor's own moves are the plan's lines) */
        if (kind == sM.logKind)
            return;
    }
    else if (kind == sM.logKind && c->session == sM.logSession && cur == sM.logCur && x == sM.logX && y == sM.logY)
        return;
    sM.logKind = kind, sM.logSession = c->session, sM.logCur = cur, sM.logX = x, sM.logY = y;
    switch (kind)
    {
    case MCTX_MAP_IDLE:
        len = snprintf(buf, sizeof(buf), "map cursor (%d,%d) camera (%d,%d) done %d", x, y, gBmSt.camera.x,
            gBmSt.camera.y, PlayerUnitsDone());
        if (sM.lastTargetUid != 0 && GetUnit(sM.lastTargetUid) != NULL && len < (int)sizeof(buf))
        {
            struct Unit *u = GetUnit(sM.lastTargetUid);

            len += snprintf(buf + len, sizeof(buf) - len, "; last target 0x%02X hp %d (was %d)%s",
                sM.lastTargetUid, u->curHP, sM.lastTargetHp, (u->state & US_DEAD) ? " dead" : "");
            sM.lastTargetUid = 0;
        }
        break;
    case MCTX_MAP_MOVE:
        snprintf(buf, sizeof(buf), "move cursor (%d,%d) unit 0x%02X at (%d,%d)", x, y,
            gActiveUnit != NULL ? gActiveUnit->index & 0xFF : 0, gActiveUnit != NULL ? gActiveUnit->xPos : -1,
            gActiveUnit != NULL ? gActiveUnit->yPos : -1);
        break;
    case MCTX_MENU:
        len = snprintf(buf, sizeof(buf), "menu %u: %d items, hand on %d, box %d,%d %dx%d; unit 0x%02X at (%d,%d); items",
            c->session, c->count, c->cur, c->rx, c->ry, c->rw, c->rh,
            gActiveUnit != NULL ? gActiveUnit->index & 0xFF : 0, gActiveUnit != NULL ? gActiveUnit->xPos : -1,
            gActiveUnit != NULL ? gActiveUnit->yPos : -1);
        for (i = 0; i < c->count && len < (int)sizeof(buf); i++)
        {
            if (c->itemName[i] != NULL && c->itemMsg[i] == 0)
                len += snprintf(buf + len, sizeof(buf) - len, " %d:'%s'@%d", i, c->itemName[i], c->itemY[i]);
            else
                len += snprintf(buf + len, sizeof(buf) - len, " %d:0x%X@%d", i, c->itemMsg[i], c->itemY[i]);
        }
        break;
    case MCTX_TARGETS:
        len = snprintf(buf, sizeof(buf), "targets %u: %d, current (%d,%d)", c->session, c->ntargets, x, y);
        if (c->ntargets > 0 && GetUnit(c->tuid[0] & 0xFF) != NULL && len < (int)sizeof(buf))
            len += snprintf(buf + len, sizeof(buf) - len, " unit 0x%02X hp %d", c->tuid[0] & 0xFF,
                GetUnit(c->tuid[0] & 0xFF)->curHP);
        for (i = 1; i < c->ntargets && len < (int)sizeof(buf); i++)
            len += snprintf(buf + len, sizeof(buf) - len, " (%d,%d)", c->tx[i], c->ty[i]);
        break;
    case MCTX_HANDROW:
        snprintf(buf, sizeof(buf), "a screen that takes a click on the hand's row (y %d-%d)", c->hy0, c->hy1 - 1);
        break;
    default:
        snprintf(buf, sizeof(buf), "other screen");
        break;
    }
    PortLog("[mouse] frame %u: screen: %s\n", gPortFrameCount, buf);
}

/* --- the hooks (host copies only) --- */

void Reforged_MouseKeyRead(void)
{
    sM.reads++;
    if (!sM.selfTest)
    {
        sM.selfTest = 1;
        if (getenv("FE8PC_MOUSE_SELFTEST") != NULL)
            SelfTest();
    }
}

int Reforged_MouseOn(void)
{
    return Reforged_GetOption(RFO_MOUSE) != 0;
}

void Reforged_MouseMapIdle(int move)
{
    if (!Reforged_MouseOn())
        return;
    sM.ctx.kind = move ? MCTX_MAP_MOVE : MCTX_MAP_IDLE;
    sM.ctx.stamp = sM.lastCtxRead = sM.reads;
    sM.ctx.session = 0;
}

void Reforged_MouseMenu(struct MenuProc *proc)
{
    struct MouseSnap *c = &sM.ctx;
    int i;

    if (!Reforged_MouseOn())
        return;
    if ((sM.menuLast + 1 != sM.reads && sM.menuLast != sM.reads) || c->kind != MCTX_MENU ||
        c->rx != proc->rect.x || c->ry != proc->rect.y || c->rw != proc->rect.w || c->count != proc->itemCount)
        sM.menuSession++; /* not run at the last read, or not the same box: another menu, or the same one back */
    sM.menuLast = sM.reads;
    c->kind = MCTX_MENU;
    c->stamp = sM.lastCtxRead = sM.reads;
    c->session = sM.menuSession;
    c->rx = proc->rect.x, c->ry = proc->rect.y, c->rw = proc->rect.w, c->rh = proc->rect.h;
    c->state = proc->state;
    c->count = proc->itemCount <= MENU_ITEM_MAX ? proc->itemCount : MENU_ITEM_MAX;
    c->cur = proc->itemCurrent;
    for (i = 0; i < c->count; i++)
    {
        const struct MenuItemProc *item = proc->menuItems[i];

        c->itemY[i] = item != NULL ? item->yTile : -100;
        c->itemMsg[i] = item != NULL && item->def != NULL ? item->def->nameMsgId : 0;
        c->itemName[i] = item != NULL && item->def != NULL ? item->def->name : NULL;
    }
}

void Reforged_MouseTargets(struct SelectTargetProc *proc)
{
    struct MouseSnap *c = &sM.ctx;
    const struct SelectTarget *t;
    int n = 0, was;
    unsigned sig = 0;

    if (!Reforged_MouseOn())
        return;
    was = c->kind;
    for (t = proc->currentTarget; t != NULL && n < MTGT_MAX; t = t->next)
    {
        c->tx[n] = t->x, c->ty[n] = t->y, c->tuid[n] = t->uid;
        sig += (unsigned)(t->x * 131 + t->y + 1) * (unsigned)(t->x * 7 + t->y * 257 + 3); /* order-free */
        n++;
        if (t->next == proc->currentTarget)
            break;
    }
    c->ntargets = n;
    sig = sig * 67u + (unsigned)n;
    /* Another list: another input screen's hook ran since (a menu, the map), or other squares. Not a gap in
       the reads: the loop is held while the camera moves onto a new target (EnsureCameraOntoPosition), and
       that is the same list (a click there must still count as the first of two). */
    if (was != MCTX_TARGETS || sig != sM.tgtSig)
        sM.tgtSession++;
    sM.tgtSig = sig;
    c->kind = MCTX_TARGETS;
    c->stamp = sM.lastCtxRead = sM.reads;
    c->session = sM.tgtSession;
}

/* The Controls screen (port/reforged/reforged_controls_menu.c): a left click is A only on the hand's own row
   (game-screen pixel rows y0 <= y < y1), so a stray click does not switch the Mouse row or start a key capture.
   y0 >= y1: no row (a click does nothing). */
void Reforged_MouseHandRow(int y0, int y1)
{
    struct MouseSnap *c = &sM.ctx;

    if (!Reforged_MouseOn())
        return;
    c->kind = MCTX_HANDROW;
    c->stamp = sM.lastCtxRead = sM.reads;
    c->session = 0;
    c->hy0 = (short)y0, c->hy1 = (short)y1;
}

/* --- events (the platform, once per frame step, before Reforged_MouseKeys) --- */

static void Push(const struct MouseEv *e)
{
    if (sM.qn < MQ_MAX)
        sM.q[sM.qn++] = *e;
}

void Reforged_MouseEvent(int kind, int button, int px, int py, int steps, int presentW)
{
    struct MouseEv e;
    int gx, gy, sx = 0, sy = 0, inPic, i, wheels = 0;

    if (!Reforged_MouseOn())
        return;
    memset(&e, 0, sizeof(e));
    PictureToGame(px, py, presentW, gRenderWideOn ? gRenderWideOff : RENDER_WIDE_MARGIN, &gx, &gy);
    inPic = px >= 0 && px < presentW && py >= 0 && py < MPIC_H;
    e.kind = (unsigned char)kind;
    e.button = (unsigned char)button;
    e.at = sM.reads;
    e.gx = (short)gx, e.gy = (short)gy;
    e.inStrip = inPic && gx >= 0 && gx < MGAME_W;
    /* a map square: in the strip, or in a Widescreen side that shows the map (render.h gRenderWideMap) */
    if (sM.haveCam && (e.inStrip || (inPic && presentW > MGAME_W && gRenderWideMap)))
        e.sqOk = GameToSquare(gx, gy, sM.camX, sM.camY, gBmMapSize.x, gBmMapSize.y, &sx, &sy);
    e.sqx = (short)sx, e.sqy = (short)sy;
    switch (kind)
    {
    case REFORGED_MOUSE_MOTION:
        /* only the newest position counts */
        if (sM.qn > 0 && sM.q[sM.qn - 1].kind == REFORGED_MOUSE_MOTION)
            sM.q[sM.qn - 1] = e;
        else
            Push(&e);
        return;
    case REFORGED_MOUSE_DOWN:
        MLOG("[mouse] frame %u: %s click at picture (%d,%d): game (%d,%d)%s, square (%d,%d)%s\n", gPortFrameCount,
            button == MB_LEFT ? "left" : button == MB_MIDDLE ? "middle" : button == MB_RIGHT ? "right" : "other",
            px, py, gx, gy, e.inStrip ? "" : " (outside the game's 240x160)", sx, sy, e.sqOk ? "" : " (no square)");
        if (button == MB_RIGHT)
        {
            /* B, first cancelling what the mouse was doing */
            sM.qn = 0;
            sM.abortPending = 1;
            Push(&e);
            return;
        }
        if (button == MB_LEFT || button == MB_MIDDLE)
            Push(&e);
        return;
    case REFORGED_MOUSE_WHEEL:
    case REFORGED_MOUSE_WHEEL_X:
        MLOG("[mouse] frame %u: wheel %s %d\n", gPortFrameCount, kind == REFORGED_MOUSE_WHEEL ? "y" : "x", steps);
        for (i = 0; i < sM.qn; i++)
            wheels += sM.q[i].kind == REFORGED_MOUSE_WHEEL || sM.q[i].kind == REFORGED_MOUSE_WHEEL_X;
        for (i = 0; i < (steps < 0 ? -steps : steps) && wheels < MWHEEL_MAX; i++, wheels++)
        {
            e.steps = steps < 0 ? -1 : 1;
            Push(&e);
        }
        return;
    default:
        return;
    }
}

/* --- the planner --- */

static void EndPlan(const char *why)
{
    if (sM.plan.kind != MPLAN_NONE)
        MLOG("[mouse] frame %u: plan %s\n", gPortFrameCount, why);
    memset(&sM.plan, 0, sizeof(sM.plan));
}

void Reforged_MouseReset(void)
{
    EndPlan("dropped (reset)");
    sM.qn = 0;
    sM.mask = 0;
    sM.lastOut = 0;
    sM.lastTick = sM.reads;
    sM.abortPending = 0;
    sM.armed = 0;
    sM.dblValid = 0;
    sM.lastHover = -1;
    sM.ctx.kind = MCTX_NONE;
}

static void StartPlan(int kind, int ctx, int budget)
{
    memset(&sM.plan, 0, sizeof(sM.plan));
    sM.plan.kind = kind;
    sM.plan.ctx = ctx;
    sM.plan.session = sM.ctx.session;
    sM.plan.budget = budget;
}

static void StartTap(unsigned short key)
{
    StartPlan(MPLAN_TAP, MCTX_NONE, 4);
    sM.plan.tapKey = key;
}

static unsigned short Press(unsigned short key)
{
    sM.plan.pressed = 1;
    MLOG("[mouse] frame %u: press %s\n", gPortFrameCount, KeyName(key));
    return key;
}

static unsigned short PressFinal(unsigned short key)
{
    sM.plan.finalSent = 1;
    return Press(key);
}

static int CtxNow(void)
{
    return sM.ctx.stamp == sM.reads ? sM.ctx.kind : MCTX_NONE;
}

/* The next press of the running plan (0: none this tick; the plan may have ended). */
static unsigned short Decide(void)
{
    struct MousePlan *p = &sM.plan;
    const struct MouseSnap *c = &sM.ctx;
    int now = CtxNow(), ctxOk;

    /* No input screen ran in this read, but one did a moment ago: its loop is held (the camera moving onto
       the target just chosen holds TargetSelection_Loop): wait, without spending the budget (bounded: the
       reads since the last hook grow while it waits). */
    if (p->ctx != MCTX_NONE && now == MCTX_NONE && sM.reads - sM.lastCtxRead <= MWAIT_PLAN)
        return 0;
    ctxOk = now == p->ctx && (p->ctx == MCTX_MAP_IDLE || p->ctx == MCTX_MAP_MOVE || c->session == p->session);
    if (--p->budget < 0)
    {
        EndPlan("dropped (too long)");
        return 0;
    }
    switch (p->kind)
    {
    case MPLAN_TAP:
        return PressFinal(p->tapKey);
    case MPLAN_TRAVEL:
    {
        int cx = gBmSt.playerCursor.x, cy = gBmSt.playerCursor.y, dx, dy;

        if (!ctxOk)
        {
            EndPlan("dropped (the map is not taking input)");
            return 0;
        }
        if (p->tapped && cx == p->bx && cy == p->by)
        {
            EndPlan("dropped (the cursor did not move)");
            return 0;
        }
        p->tapped = 0;
        if (cx == p->tx && cy == p->ty)
        {
            /* the game takes A, B, R only with the cursor standing on its square (playerphase.c) */
            if (gBmSt.playerCursorDisplay.x != cx * 16 || gBmSt.playerCursorDisplay.y != cy * 16 ||
                gBmSt.cursorTarget.x != cx * 16 || gBmSt.cursorTarget.y != cy * 16)
                return 0;
            if (p->finalKey)
                return PressFinal((unsigned short)p->finalKey);
            EndPlan("done (cursor there)");
            return 0;
        }
        dx = p->tx - cx, dy = p->ty - cy;
        p->tapped = 1, p->bx = cx, p->by = cy;
        if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy))
            return Press(dx > 0 ? PLATFORM_KEY_RIGHT : PLATFORM_KEY_LEFT);
        return Press(dy > 0 ? PLATFORM_KEY_DOWN : PLATFORM_KEY_UP);
    }
    case MPLAN_MENU:
    {
        int steps;

        if (!ctxOk)
        {
            EndPlan("dropped (the menu is gone)");
            return 0;
        }
        if (p->tapped && c->cur == p->bx)
        {
            EndPlan("dropped (the hand did not move)");
            return 0;
        }
        p->tapped = 0;
        if (c->cur == p->item)
        {
            if (p->finalKey)
                return PressFinal((unsigned short)p->finalKey);
            EndPlan("done (hand there)");
            return 0;
        }
        steps = ListSteps(c->cur, p->item, c->count);
        p->tapped = 1, p->bx = c->cur;
        return Press(steps > 0 ? PLATFORM_KEY_DOWN : PLATFORM_KEY_UP);
    }
    case MPLAN_TARGET:
    {
        int j, n = c->ntargets;

        if (!ctxOk || n <= 0)
        {
            EndPlan("dropped (the target list is gone)");
            return 0;
        }
        if (p->tapped && c->tx[0] == p->bx && c->ty[0] == p->by)
        {
            EndPlan("dropped (the target did not change)");
            return 0;
        }
        p->tapped = 0;
        if (c->tx[0] == p->tx && c->ty[0] == p->ty)
        {
            EndPlan("done (target chosen; a click on it again confirms)");
            return 0;
        }
        for (j = 0; j < n; j++)
            if (c->tx[j] == p->tx && c->ty[j] == p->ty)
                break;
        if (j == n)
        {
            EndPlan("dropped (no such target)");
            return 0;
        }
        p->tapped = 1, p->bx = c->tx[0], p->by = c->ty[0];
        /* Left / Up: the next target; Right / Down: the previous one (uiselecttarget.c) */
        return Press(ListSteps(0, j, n) > 0 ? PLATFORM_KEY_LEFT : PLATFORM_KEY_RIGHT);
    }
    default:
        EndPlan("dropped");
        return 0;
    }
}

static int MenuUsable(const struct MouseSnap *c)
{
    return c->count > 0 && c->count <= 9 && !(c->state & (MENU_STATE_NOCURSOR | MENU_STATE_NOTSHOWN));
}

/* One queued event against the screen now: MCL_PLAN (a plan started), MCL_DONE (nothing to do, or done),
   MCL_WAIT (no input screen yet, but one ran a moment ago: keep the event). */
static int Classify(struct MouseEv *e)
{
    const struct MouseSnap *c = &sM.ctx;
    int ctx = CtxNow();

    if (e->kind == REFORGED_MOUSE_WHEEL)
    {
        StartTap(e->steps > 0 ? PLATFORM_KEY_UP : PLATFORM_KEY_DOWN);
        return MCL_PLAN;
    }
    if (e->kind == REFORGED_MOUSE_WHEEL_X)
    {
        StartTap(e->steps > 0 ? PLATFORM_KEY_RIGHT : PLATFORM_KEY_LEFT);
        return MCL_PLAN;
    }
    if (e->kind == REFORGED_MOUSE_DOWN && e->button == MB_RIGHT)
    {
        StartTap(PLATFORM_KEY_B);
        return MCL_PLAN;
    }
    if (e->kind == REFORGED_MOUSE_MOTION)
    {
        int i;

        if (ctx != MCTX_MENU || !MenuUsable(c))
            return MCL_DONE;
        if (c->session != sM.hoverSession)
            sM.hoverSession = c->session, sM.lastHover = -1;
        i = e->inStrip ? MenuHitTest(c, e->gx, e->gy) : -1;
        if (i == sM.lastHover)
            return MCL_DONE;
        sM.lastHover = i;
        if (i < 0 || i == c->cur)
            return MCL_DONE;
        MLOG("[mouse] frame %u: hover: menu item %d\n", gPortFrameCount, i);
        StartPlan(MPLAN_MENU, MCTX_MENU, 20 + 4 * c->count);
        sM.plan.item = i;
        return MCL_PLAN;
    }
    /* a left or middle click */
    if (e->button == MB_LEFT && sM.dblValid && e->at - sM.dblAt <= MDBL_READS &&
        abs(e->gx - sM.dblGx) <= MDBL_PIXELS && abs(e->gy - sM.dblGy) <= MDBL_PIXELS)
    {
        /* the second click of a double-click whose first click chose something: not a click on the screen
           that choice opened (a double-clicked unit is selected, not selected and then told to stay) */
        sM.dblValid = 0;
        MLOG("[mouse] frame %u: click: the second click of a double-click, ignored\n", gPortFrameCount);
        return MCL_DONE;
    }
    if (ctx == MCTX_NONE && sM.reads - sM.lastCtxRead <= MWAIT_CTX && e->waited < MWAIT_CTX)
    {
        e->waited++;
        return MCL_WAIT;
    }
    if (ctx == MCTX_MENU && !MenuUsable(c))
        ctx = MCTX_NONE;
    switch (ctx)
    {
    case MCTX_HANDROW:
        if (e->button == MB_MIDDLE)
        {
            StartTap(PLATFORM_KEY_R);
            return MCL_PLAN;
        }
        if (!e->inStrip || e->gy < c->hy0 || e->gy >= c->hy1)
        {
            MLOG("[mouse] frame %u: click: not on the hand's row, nothing\n", gPortFrameCount);
            return MCL_DONE;
        }
        MLOG("[mouse] frame %u: click: the hand's row: A\n", gPortFrameCount);
        StartTap(PLATFORM_KEY_A);
        return MCL_PLAN;
    case MCTX_MAP_IDLE:
    case MCTX_MAP_MOVE:
    {
        int fin;

        /* Another unit's range is shown (an enemy's or a green unit's, a unit whose turn ended, the danger
           zone): there A swaps or closes it wherever the cursor is (playerphase.c PlayerPhase_RangeDisplayIdle,
           the !CONTROL case), so a left click first closes it with B (A with no active unit, the case where
           retail's B would read through NULL), then counts as a click on the free map. */
        if (ctx == MCTX_MAP_MOVE && e->button == MB_LEFT && e->sqOk &&
            (gActiveUnit == NULL ||
             (GetPlayerSelectKind(gActiveUnit) != PLAYER_SELECT_CONTROL && !(gActiveUnit->state & US_HAS_MOVED))))
        {
            if (e->closed)
            {
                MLOG("[mouse] frame %u: click: the range shown did not close, nothing\n", gPortFrameCount);
                return MCL_DONE;
            }
            e->closed = 1;
            MLOG("[mouse] frame %u: click: a range is shown: %s closes it first\n", gPortFrameCount,
                gActiveUnit != NULL ? "B" : "A");
            StartTap(gActiveUnit != NULL ? PLATFORM_KEY_B : PLATFORM_KEY_A);
            return MCL_PLAN_KEEP;
        }
        if (!e->sqOk)
        {
            if (e->button == MB_MIDDLE)
            {
                StartTap(PLATFORM_KEY_R);
                return MCL_PLAN;
            }
            MLOG("[mouse] frame %u: click: not on the map\n", gPortFrameCount);
            return MCL_DONE;
        }
        if (e->button == MB_MIDDLE)
            fin = PLATFORM_KEY_R;
        else if (ctx == MCTX_MAP_MOVE)
            fin = PLATFORM_KEY_A;
        else if (gBmMapUnit[e->sqy][e->sqx] != 0 ||
                 (e->sqx == gBmSt.playerCursor.x && e->sqy == gBmSt.playerCursor.y))
            fin = PLATFORM_KEY_A; /* a unit, or the cursor's own square: A */
        else
            fin = 0; /* an empty square: the cursor only ("click selects, click again confirms") */
        MLOG("[mouse] frame %u: click: %s, the cursor to (%d,%d)%s%s\n", gPortFrameCount, sCtxNames[ctx], e->sqx,
            e->sqy, fin ? ", then " : "", fin ? KeyName((unsigned short)fin) : "");
        StartPlan(MPLAN_TRAVEL, ctx, 40 + 8 * (abs(e->sqx - gBmSt.playerCursor.x) + abs(e->sqy - gBmSt.playerCursor.y)));
        sM.plan.tx = e->sqx, sM.plan.ty = e->sqy, sM.plan.finalKey = fin;
        if (e->button == MB_LEFT && fin == PLATFORM_KEY_A)
            sM.dblValid = 1, sM.dblAt = e->at, sM.dblGx = e->gx, sM.dblGy = e->gy;
        return MCL_PLAN;
    }
    case MCTX_MENU:
    {
        int i;

        if (e->button == MB_MIDDLE)
        {
            StartTap(PLATFORM_KEY_R);
            return MCL_PLAN;
        }
        i = e->inStrip ? MenuHitTest(c, e->gx, e->gy) : -1;
        if (i < 0)
        {
            MLOG("[mouse] frame %u: click: menu, on no item\n", gPortFrameCount);
            return MCL_DONE;
        }
        MLOG("[mouse] frame %u: click: menu item %d, then A\n", gPortFrameCount, i);
        sM.hoverSession = c->session, sM.lastHover = i;
        StartPlan(MPLAN_MENU, MCTX_MENU, 20 + 4 * c->count);
        sM.plan.item = i, sM.plan.finalKey = PLATFORM_KEY_A;
        sM.dblValid = 1, sM.dblAt = e->at, sM.dblGx = e->gx, sM.dblGy = e->gy;
        return MCL_PLAN;
    }
    case MCTX_TARGETS:
    {
        int j;

        if (e->button == MB_MIDDLE)
        {
            StartTap(PLATFORM_KEY_R);
            return MCL_PLAN;
        }
        for (j = 0; e->sqOk && j < c->ntargets; j++)
            if (c->tx[j] == e->sqx && c->ty[j] == e->sqy)
                break;
        if (!e->sqOk || j == c->ntargets)
        {
            MLOG("[mouse] frame %u: click: targets, on no target\n", gPortFrameCount);
            return MCL_DONE;
        }
        if (j == 0)
        {
            if (sM.armed && sM.armSession == c->session && sM.armX == e->sqx && sM.armY == e->sqy)
            {
                MLOG("[mouse] frame %u: click: targets, the chosen target again: A\n", gPortFrameCount);
                sM.armed = 0;
                sM.lastTargetUid = c->tuid[0] & 0xFF;
                sM.lastTargetHp = GetUnit(sM.lastTargetUid) != NULL ? GetUnit(sM.lastTargetUid)->curHP : -1;
                StartTap(PLATFORM_KEY_A);
                return MCL_PLAN;
            }
            MLOG("[mouse] frame %u: click: targets, the current target chosen (click again: A)\n", gPortFrameCount);
            sM.armed = 1, sM.armSession = c->session, sM.armX = e->sqx, sM.armY = e->sqy;
            return MCL_DONE;
        }
        MLOG("[mouse] frame %u: click: targets, target (%d,%d) (click again: A)\n", gPortFrameCount, e->sqx, e->sqy);
        sM.armed = 1, sM.armSession = c->session, sM.armX = e->sqx, sM.armY = e->sqy;
        StartPlan(MPLAN_TARGET, MCTX_TARGETS, 20 + 4 * c->ntargets);
        sM.plan.tx = e->sqx, sM.plan.ty = e->sqy;
        return MCL_PLAN;
    }
    default:
        /* any other screen: A in the game's picture, R anywhere */
        if (e->button == MB_MIDDLE)
        {
            StartTap(PLATFORM_KEY_R);
            return MCL_PLAN;
        }
        if (!e->inStrip)
        {
            MLOG("[mouse] frame %u: click: outside the game's picture\n", gPortFrameCount);
            return MCL_DONE;
        }
        MLOG("[mouse] frame %u: click: %s screen: A\n", gPortFrameCount, ctx == MCTX_MENU ? "menu (no hand)" : "other");
        StartTap(PLATFORM_KEY_A);
        return MCL_PLAN;
    }
}

/* One tick: the previous mask was read by the game. */
static unsigned short Tick(unsigned short consumed)
{
    unsigned short m;

    if (sM.abortPending)
    {
        sM.abortPending = 0;
        EndPlan("dropped (right-click)");
    }
    if (sM.plan.kind != MPLAN_NONE && sM.plan.pressed)
    {
        sM.plan.pressed = 0; /* the release tick */
        if (sM.plan.finalSent)
            EndPlan("done");
        return 0;
    }
    if (sM.plan.kind != MPLAN_NONE)
    {
        m = Decide();
        if (m != 0 || sM.plan.kind != MPLAN_NONE)
            return m;
    }
    if (consumed != 0)
        return 0; /* a key was just read: its screen shows its effect first */
    while (sM.qn > 0)
    {
        int r = Classify(&sM.q[0]);

        if (r == MCL_WAIT)
            return 0;
        if (r == MCL_PLAN_KEEP)
            return Decide(); /* a first step (closing a range); the event stays at the queue's head */
        sM.qn--;
        memmove(&sM.q[0], &sM.q[1], sM.qn * sizeof(sM.q[0]));
        if (r == MCL_PLAN)
        {
            m = Decide();
            if (m != 0 || sM.plan.kind != MPLAN_NONE)
                return m;
        }
    }
    return 0;
}

unsigned short Reforged_MouseKeys(unsigned short realKeys)
{
    unsigned short consumed;

    if (!Reforged_MouseOn())
    {
        if (sM.on)
        {
            Reforged_MouseReset();
            sM.on = 0;
            sM.haveCam = 0;
        }
        return 0;
    }
    if (!sM.on)
    {
        sM.on = 1;
        Reforged_MouseReset();
        sM.lastHover = -1;
    }
    if (LogOn())
        LogScreen();
    /* the camera the picture shown next is drawn with (its OnVBlank flushed this OnMain's offsets) */
    sM.camX = gBmSt.camera.x, sM.camY = gBmSt.camera.y;
    sM.haveCam = 1;
    if (realKeys != 0)
    {
        if (sM.plan.kind != MPLAN_NONE || sM.qn > 0)
        {
            EndPlan("dropped (a key or button)");
            sM.qn = 0;
            MLOG("[mouse] frame %u: a key or button: the mouse's queue emptied\n", gPortFrameCount);
        }
        sM.abortPending = 0;
        sM.mask = 0;
        sM.lastOut = realKeys;
        return 0;
    }
    if (sM.reads == sM.lastTick)
    {
        sM.lastOut = sM.mask;
        return sM.mask; /* not read yet (a lag frame, or no screen reads keys): held */
    }
    sM.lastTick = sM.reads;
    consumed = sM.lastOut;
    sM.mask = Tick(consumed);
    sM.lastOut = sM.mask;
    return sM.mask;
}

#endif /* PORT_PLAYED_BUILD */
