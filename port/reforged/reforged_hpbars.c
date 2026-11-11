#include "global.h"

/*
 * Reforged Options, "HP bars & warnings" (port-rfo-hpbars; port-rfo-int6:
 * Off / Bars / Warnings / Both; OFF by default, display-only:
 * reforged_settings.c sDisplayOnly). Played builds only; empty in every test
 * build. The idea, not the code, follows the community's FE8 "Map HP bars"
 * with effectiveness/crit warnings (circleseverywhere, with Tequila's
 * additions and Alusq's icons; THANK-YOU.md row 31), and its rules as
 * port-plan/reforged-qol-research.md "Map HP bars and '!' warnings" records
 * them (a reading of the threads and the Skill System's HpBars module,
 * THANK-YOU.md row 35).
 *
 * Bars (RFO_HPBARS_BARS): every unit whose map sprite is drawn (yours, green
 * and enemy) with 0 < curHP < GetUnitMaxHp (the effective maximum, with the
 * equipped weapon's HP bonus) shows a small bar in the bottom 4 pixel rows of
 * its map square; none at full HP. 11 fill steps, 11 - floor(11 * (max - HP)
 * / max) pixels (1 to 11), and the colour follows the fill: 9-11 green, 6-8
 * yellow, 4-5 orange, 1-3 red (HP above 8/11 = 72.7%, 5/11 = 45.5%, 3/11 =
 * 27.3% of the maximum, red below). Every fill shade is the game's own (the
 * green, yellow, red and empty ones since port-rfo-polish, HPB-1):
 * gPal_MiscUiGraphics colours 11 (green), 1 (yellow), 4 (orange), 12 (red)
 * and 13 (the empty part), from the ROM data (the map's OBJ palette 0,
 * src/bm.c), never read from palette RAM; only the dark frame is ours.
 *
 * Warnings (RFO_HPBARS_WARNINGS): only while the player moves one of their
 * units (the player phase proc in PlayerPhase_RangeDisplayIdle with BM_FLAG_1
 * set, the unit movement screen, gActiveUnit blue), only on units not allied
 * with it, and checked against it: at most one "!" in the right of the
 * enemy's square,
 *   - white (effective): the enemy's class is a slayer of the unit's class
 *     (IsUnitEffectiveAgainst), or a weapon in its 5 slots that it can equip
 *     (CanUnitUseWeapon, the game's equip test, GetUnitEquippedWeapon) is
 *     effective against the unit (IsItemEffectiveAgainst, which honours the
 *     Delphi Shield);
 *   - orange (high crit): a weapon it can equip whose own crit (GetItemCrit)
 *     is above RFO_HPB_WEAPON_CRIT.
 *   The class counts first, then the items in slot order: the first that
 *   qualifies decides the colour. Worked out once per selection (MarksFor),
 *   for every unit at once, not every frame.
 *
 * Nothing here writes a game value: plain reads, no battle struct, no RNG, no
 * unit field (GetUnitCurrentHp is not called: it clamps curHP). FE8PC_RFO_CHECK
 * proves it (below).
 *
 * How it is drawn: never with the game's OAM, VRAM or palettes. PutUnitSpritesOam
 * (src/bmudisp.c) calls Reforged_MapHpBarsCollect first: for each map sprite
 * it is about to push (the same tests: on screen, not hidden by config 0x80),
 * this file works out the OAM attribute words PutOamHi will write for it
 * (port/asm/arm_pc.c PutOamExt) and a 16x16 picture of the unit's square.
 * OnVBlank (src/bm.c) calls Reforged_MapHpBarsVBlank right after SyncHiOam,
 * the copy that puts those sprites in OAM: the pictures become the shown
 * ones (gReforgedOverlay), so they always belong to the sprites on screen, in
 * step while the camera scrolls. The renderer (port/render/obj_line.c, played
 * builds only) finds each owner sprite in OAM by its attribute words on each
 * line and draws its picture over that sprite's own pixels and over pixels
 * with no sprite, never over another sprite's, with the sprite's priority:
 * BG0/BG1 windows (menus, the battle forecast, the side windows) hide the
 * pictures as they hide the sprites; the cursor, the hand and every other
 * sprite pushed before stay on top, and so do sprites pushed after it. A
 * sprite not drawn (hidden, moving, off screen, dropped from OAM) has no
 * picture. With the option OFF the list is empty and nothing is drawn.
 */

#if PORT_PLAYED_BUILD

#include <string.h>

#include "proc.h"
#include "hardware.h"
#include "ctc.h"
#include "bmunit.h"
#include "bmitem.h"
#include "bmphase.h"
#include "bmreliance.h"
#include "bmbattle.h"
#include "playerphase.h"
#include "constants/items.h"

#include "port_hw.h"
#include "port_reforged.h"

/* High crit: the orange "!" for a weapon whose own crit (GetItemCrit) is above
   this. In retail FE8 (src/data_items.c, checked 2026-10-29) only the Killer
   weapons (30; the Killer Ballista is 10) and the Shamshir (35) are; the Wo
   Dao (20) is not. The community's threshold (reforged-qol-research.md). */
#define RFO_HPB_WEAPON_CRIT 24

#define RGB5(r, g, b) ((unsigned short)(RFO_OVERLAY_OPAQUE | (r) | ((g) << 5) | ((b) << 10)))
#define COL_FRAME  RGB5(2, 2, 3)
#define COL_WHITE  RGB5(31, 31, 31)
#define COL_ORANGE RGB5(31, 17, 0) /* the high-crit "!" */
/* The bar's fill shades: colours of gPal_MiscUiGraphics, the map's own OBJ
   palette 0 (src/bm.c ApplyPalettes(gPal_MiscUiGraphics, 0x10, 2)), read from
   the ROM data (BarColour). Retail values (ROM 0x59ED70, checked 2026-10-30):
   11 0x3BEB green, 1 0x23FF yellow, 4 0x2E5C orange (#E7945A), 12 0x2DDF red,
   13 0x496A navy. The community's bar art uses the orange too; the green,
   yellow, red and empty shades were picked by the first builder before
   port-rfo-polish (HPB-1, CHOICES-2026-10-30.md option 2). */
#define BAR_HIGH_INDEX   11
#define BAR_MID_INDEX    1
#define BAR_ORANGE_INDEX 4
#define BAR_LOW_INDEX    12
#define BAR_EMPTY_INDEX  13

#define BAR_ROW   12 /* the bar: rows 12-15 of the square, columns 1-13 */
#define BAR_FILL  11 /* fill steps, one pixel each: columns 2-12 */
#define MARK_ROW  4  /* the "!": rows 4-11, columns 12-14 */
#define MARK_X    12

#define MARK_NONE      0
#define MARK_EFFECTIVE 1 /* white */
#define MARK_CRIT      2 /* orange */

struct ReforgedOverlay gReforgedOverlay; /* shown (read by the renderer) */
static struct ReforgedOverlay sPending;  /* this OnMain's sprites */

/* The marks against the selected unit, by unit index (MarksFor), and what
   they were worked out for; unit -1: none (no unit selected). selUnit,
   selChapter, selTurn, selPhase, proc: SelectedUnit's one proc search per
   selection (the active unit, chapter, turn and phase it ran for, and the
   player phase proc it found, or NULL); selUnit -1: not run yet. */
static u8 sMark[0x100];
static struct {
    int unit, chapter, turn, phase;
    int selUnit, selChapter, selTurn, selPhase;
    struct Proc *proc;
} sMarkFor = { -1, -1, -1, -1, -1, -1, -1, -1, NULL };

extern struct SMSHandle gSMSHandleArray[RFO_OVERLAY_MAX]; /* src/bmudisp.c (100 entries) */

void PortLog(const char *fmt, ...);
char *getenv(const char *name);

/* FE8PC_RFO_CHECK=1: every collect hashes the GBA's RAM, I/O, palettes, VRAM
   and OAM before and after this file's work and logs any change (there must
   be none), and logs the pictures' summary each time it changes (bars, white
   and orange marks, the selected unit), each selection's marks; also
   CheckSurvey, CheckBattle and CheckShownInOam below. */
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

static u32 GbaHash(void)
{
    u32 h = 2166136261u;

    h = Fnv(h, (const void *)(uintptr_t)PORT_EWRAM_START, 0x40000);
    h = Fnv(h, (const void *)(uintptr_t)PORT_IWRAM_START, 0x8000);
    h = Fnv(h, (const void *)(uintptr_t)PORT_IO_START, 0x400);
    h = Fnv(h, (const void *)(uintptr_t)PORT_PLTT_START, 0x400);
    h = Fnv(h, (const void *)(uintptr_t)PORT_VRAM_START, 0x18000);
    h = Fnv(h, (const void *)(uintptr_t)PORT_OAM_START, 0x400);
    return h;
}

/* Forgets SelectedUnit's proc search (BM_FLAG_1 dropped, no blue unit
   active, or the warnings are off). Always inlined. What was seen (int6):
   while the proc search (3b092d4e) and later this call (3aa68f9d) ran on
   every frame, the suspend ALT block info's _pad_07 (SRAM 0xAB, P-18)
   differed from Off (3aa68f9d ch2 route: 0x6F, Off 0x6E); with the search
   once per selection and this inlined it matched Off on the runs checked.
   But the P-18 pad bytes also differ with no Reforged change (0x9B between
   two Off runs of one binary, rfo-ff; 0xAB between the Off runs of two
   binaries, int6), so the call is not proven to be the cause. */
static inline __attribute__((always_inline)) void SelectionDone(void)
{
    sMarkFor.selUnit = -1;
    sMarkFor.proc = NULL;
}

/* The unit the player is moving, or NULL (the unit movement screen only:
   BM_FLAG_1, a blue gActiveUnit, the player phase proc in
   PlayerPhase_RangeDisplayIdle). Proc_Find runs once per selection (a new
   active unit, chapter, turn or phase); on the other frames the found proc's
   two fields are read. One player phase proc lives through a phase, and an
   ended proc's script is NULL (src/proc.c), so the cached pointer holds the
   same answer Proc_Find would give. */
static struct Unit *SelectedUnit(void)
{
    struct Proc *proc;

    if (!(gBmSt.gameStateBits & BM_FLAG_1) || !UNIT_IS_VALID(gActiveUnit) ||
        UNIT_FACTION(gActiveUnit) != FACTION_BLUE)
    {
        SelectionDone();
        return NULL;
    }
    if (sMarkFor.selUnit != gActiveUnit->index || sMarkFor.selChapter != gPlaySt.chapterIndex ||
        sMarkFor.selTurn != gPlaySt.chapterTurnNumber || sMarkFor.selPhase != gPlaySt.faction)
    {
        sMarkFor.proc = (struct Proc *)Proc_Find(gProcScr_PlayerPhase);
        sMarkFor.selUnit = gActiveUnit->index;
        sMarkFor.selChapter = gPlaySt.chapterIndex;
        sMarkFor.selTurn = gPlaySt.chapterTurnNumber;
        sMarkFor.selPhase = gPlaySt.faction;
        if (CheckOn())
            PortLog("[rfo-hpbars] frame %u: proc search for 0x%02X: %s\n", gPortFrameCount,
                gActiveUnit->index & 0xFF, sMarkFor.proc != NULL ? "player phase proc found" : "none");
    }
    proc = sMarkFor.proc;
    if (proc == NULL || proc->proc_script != gProcScr_PlayerPhase ||
        proc->proc_idleCb != (ProcFunc)PlayerPhase_RangeDisplayIdle)
        return NULL;
    return gActiveUnit;
}

/* The mark of `enemy` against `target` (see the header); *slot: the item slot
   that decided it, -1 for the class (or no mark). */
static int MarkOf(struct Unit *enemy, struct Unit *target, int *slot)
{
    int i;

    *slot = -1;
    if (IsUnitEffectiveAgainst(enemy, target))
        return MARK_EFFECTIVE;
    for (i = 0; i < UNIT_ITEM_COUNT; i++)
    {
        int item = enemy->items[i];

        if (item == 0)
            break;
        if (!CanUnitUseWeapon(enemy, item))
            continue;
        *slot = i;
        if (IsItemEffectiveAgainst(item, target))
            return MARK_EFFECTIVE;
        if (GetItemCrit(item) > RFO_HPB_WEAPON_CRIT)
            return MARK_CRIT;
    }
    *slot = -1;
    return MARK_NONE;
}

static const char *MarkName(int mark)
{
    return mark == MARK_EFFECTIVE ? "white" : mark == MARK_CRIT ? "orange" : "none";
}

/* --- FE8PC_RFO_CHECK only: the battle's crit chance, for the check lines ---
   The marks no longer use it (port-rfo-int6: a weapon's own crit decides the
   orange "!"); CheckSurvey prints it beside each mark and CheckBattle compares
   it with the game's. It is the battle's (src/bmbattle.c:
   ComputeBattleUnitCritRate, the support, weapon rank and status bonuses,
   ComputeBattleUnitEffectiveCritRate, ComputeBattleUnitSpecialWeaponStats),
   worked out with plain reads and no battle unit: the weapon's crit + skill /
   2 (+15 with CA_CRITBONUS) + the support crit bonus (+5 at rank S in the
   weapon's type, +10 with the crit status), minus the unit's luck + support
   dodge bonus; 0 for the Monster Stone, the Runesword (always magic damage)
   and a HP-halving weapon, or when the unit carries a crit-negating item.
   Lightbrand and Wind Sword count with their 1-range crit. The support
   bonuses are those at the units' current squares. */
static int CritAgainst(struct Unit *enemy, int item, struct Unit *target)
{
    struct SupportBonuses eb, tb;
    int crit, dodge, type, i;

    if (GetItemIndex(item) == ITEM_MONSTER_STONE || GetItemIndex(item) == ITEM_SWORD_RUNESWORD)
        return 0;
    if (!(GetItemAttributes(item) & IA_MAGICDAMAGE) && GetItemWeaponEffect(item) == WPN_EFFECT_HPHALVE)
        return 0;

    crit = GetItemCrit(item) + GetUnitSkill(enemy) / 2;
    if (UNIT_CATTRIBUTES(enemy) & CA_CRITBONUS)
        crit += 15;
    GetUnitSupportBonuses(enemy, &eb);
    crit += eb.bonusCrit;
    type = GetItemType(item);
    if (type < 8 && enemy->ranks[type] >= WPN_EXP_S)
        crit += 5;
    if (enemy->statusIndex == UNIT_STATUS_CRIT)
        crit += 10;

    GetUnitSupportBonuses(target, &tb);
    dodge = GetUnitLuck(target) + tb.bonusDodge;

    crit -= dodge;
    if (crit < 0)
        crit = 0;
    for (i = 0; i < UNIT_ITEM_COUNT && target->items[i] != 0; i++)
        if (GetItemAttributes(target->items[i]) & IA_NEGATE_CRIT)
            return 0;
    return crit;
}

/* The highest battle crit `enemy` has against `target` with a weapon it can
   use now, -1 when none (asleep or stone: no weapon in a battle). */
static int BestCrit(struct Unit *enemy, struct Unit *target)
{
    int best = -1, i;

    if (enemy->statusIndex == UNIT_STATUS_SLEEP || enemy->statusIndex == UNIT_STATUS_PETRIFY ||
        enemy->statusIndex == UNIT_STATUS_13)
        return -1;
    for (i = 0; i < UNIT_ITEM_COUNT && enemy->items[i] != 0; i++)
        if (CanUnitUseWeaponNow(enemy, enemy->items[i]))
        {
            int crit = CritAgainst(enemy, enemy->items[i], target);

            if (crit > best)
                best = crit;
        }
    return best;
}
/* --- end of the check-only crit --- */

/* Works out sMark against `sel` for every unit not allied with it. */
static void MarksFor(struct Unit *sel)
{
    int i, white = 0, orange = 0;

    memset(sMark, 0, sizeof(sMark));
    sMarkFor.unit = sel->index;
    sMarkFor.chapter = gPlaySt.chapterIndex;
    sMarkFor.turn = gPlaySt.chapterTurnNumber;
    sMarkFor.phase = gPlaySt.faction;
    for (i = 1; i < FACTION_PURPLE + 6; i++)
    {
        struct Unit *unit = GetUnit(i);
        int slot;

        if (!UNIT_IS_VALID(unit) || AreUnitsAllied(sel->index, unit->index))
            continue;
        sMark[unit->index & 0xFF] = (u8)MarkOf(unit, sel, &slot);
        white += sMark[unit->index & 0xFF] == MARK_EFFECTIVE;
        orange += sMark[unit->index & 0xFF] == MARK_CRIT;
        if (CheckOn() && sMark[unit->index & 0xFF] != MARK_NONE)
            PortLog("[rfo-hpbars] frame %u: mark %s on unit 0x%02X (char 0x%02X class 0x%02X) at %d,%d vs 0x%02X "
                    "(class 0x%02X): decided by %s %d (item 0x%04X, its crit %d)\n",
                gPortFrameCount, MarkName(sMark[unit->index & 0xFF]), unit->index & 0xFF,
                unit->pCharacterData->number, unit->pClassData->number, unit->xPos, unit->yPos, sel->index & 0xFF,
                sel->pClassData->number, slot < 0 ? "its class; slot" : "slot", slot < 0 ? 0 : slot,
                unit->items[slot < 0 ? 0 : slot], GetItemCrit(unit->items[slot < 0 ? 0 : slot]));
    }
    if (CheckOn())
        PortLog("[rfo-hpbars] frame %u: marks worked out vs 0x%02X (chapter %d, turn %d): %d white, %d orange\n",
            gPortFrameCount, sel->index & 0xFF, sMarkFor.chapter, sMarkFor.turn, white, orange);
}

static struct {
    int bars, white, orange, selected, chapter;
} sLast = { -1, -1, -1, -1, -1 };

/* FE8PC_RFO_CHECK only, once per chapter, the first time the player moves a
   unit: every blue unit not dead (the moving one too) against every enemy
   whose sprite is drawn: its mark (MarkOf) and the best battle crit
   (BestCrit), for a mark or a crit of 10 or more. */
static void CheckSurvey(struct Unit **byHandle)
{
    int b, h, n = 0;

    if (sLast.chapter == gPlaySt.chapterIndex)
        return;
    for (h = 0; h < RFO_OVERLAY_MAX; h++)
        n += byHandle[h] != NULL && UNIT_FACTION(byHandle[h]) == FACTION_RED;
    if (n == 0)
        return; /* no enemy drawn yet */
    sLast.chapter = gPlaySt.chapterIndex;
    for (b = FACTION_BLUE + 1; b < FACTION_BLUE + 0x40; b++)
    {
        struct Unit *blue = GetUnit(b);

        if (!UNIT_IS_VALID(blue) || (blue->state & US_DEAD))
            continue;
        for (h = 0; h < RFO_OVERLAY_MAX; h++)
        {
            struct Unit *e = byHandle[h];
            int crit, mark, slot;

            if (e == NULL || AreUnitsAllied(blue->index, e->index))
                continue;
            mark = MarkOf(e, blue, &slot);
            crit = BestCrit(e, blue);
            if (mark != MARK_NONE || crit >= 10)
                PortLog("[rfo-hpbars] survey chapter %d: unit 0x%02X (class 0x%02X) at %d,%d vs blue 0x%02X "
                        "(char 0x%02X class 0x%02X%s): mark %s (slot %d), best battle crit %d\n",
                    gPlaySt.chapterIndex, e->index & 0xFF, e->pClassData->number, e->xPos, e->yPos,
                    blue->index & 0xFF, blue->pCharacterData->number, blue->pClassData->number,
                    blue == gActiveUnit ? ", moving" : "", MarkName(mark), slot, crit);
        }
    }
}

/* FE8PC_RFO_CHECK only: when the game's battle units change (a battle
   forecast, a battle), compare its crit chances with CritAgainst for the same
   two units and weapons (at the units' squares now; the game's at the
   battle's squares, which only the support bonuses can see). Not a
   comparison: a stat preview (BattleGenerateUiStats: config
   BATTLE_CONFIG_BIT2, no target class; the stat screen, the item menus), not
   printed; a side with no weapon, printed as "no weapon". */
static void CheckBattle(void)
{
    static u32 last;
    struct Unit *a = GetUnit(gBattleActor.unit.index), *t = GetUnit(gBattleTarget.unit.index);
    u32 sig = Fnv(Fnv(2166136261u, &gBattleActor, sizeof(gBattleActor)), &gBattleTarget, sizeof(gBattleTarget));
    int ga, gt, ma, mt;

    if (sig == last)
        return;
    last = sig;
    if ((gBattleStats.config & BATTLE_CONFIG_BIT2) || gBattleTarget.unit.pClassData == NULL)
        return; /* a stat preview */
    if (!UNIT_IS_VALID(a) || !UNIT_IS_VALID(t) || a == t)
        return;
    ga = gBattleActor.battleEffectiveCritRate;
    gt = gBattleTarget.battleEffectiveCritRate;
    ma = gBattleActor.weapon ? CritAgainst(a, gBattleActor.weapon, t) : -1;
    mt = gBattleTarget.weapon ? CritAgainst(t, gBattleTarget.weapon, a) : -1;
    PortLog("[rfo-hpbars] battle check frame %u: actor 0x%02X weapon 0x%04X crit game %d mine %d (%s); target 0x%02X "
            "weapon 0x%04X crit game %d mine %d (%s)\n",
        gPortFrameCount, a->index & 0xFF, gBattleActor.weapon, ga, ma,
        !gBattleActor.weapon ? "no weapon" : ga == ma ? "equal" : "MISMATCH", t->index & 0xFF, gBattleTarget.weapon,
        gt, mt, !gBattleTarget.weapon ? "no weapon" : gt == mt ? "equal" : "MISMATCH");
}

/* FE8PC_RFO_CHECK only: how many of the shown owners the OAM now on screen
   holds (each one should be there: the renderer finds them the same way),
   logged when it changes, with the first missing owner's words. */
static void CheckShownInOam(void)
{
    static int lastShown = -1, lastFound = -1;
    const u16 *oam = (const u16 *)(uintptr_t)PORT_OAM_START;
    int k, e, found = 0, miss = -1;

    for (k = 0; k < gReforgedOverlay.count; k++)
    {
        const struct ReforgedOverlayOwner *ow = &gReforgedOverlay.owner[k];

        for (e = 0; e < 128; e++)
            if (oam[e * 4] == ow->a0 && oam[e * 4 + 1] == ow->a1 && oam[e * 4 + 2] == ow->a2)
                break;
        if (e < 128)
            found++;
        else if (miss < 0)
            miss = k;
    }
    {
        static int lastAge = -1;
        int age = (int)(gPortFrameCount - gReforgedOverlay.frame);

        if ((age > 2) != (lastAge > 2))
            PortLog("[rfo-hpbars] frame %u: shown list made %d frame steps ago\n", gPortFrameCount, age);
        lastAge = age;
    }
    if (gReforgedOverlay.count == lastShown && found == lastFound)
        return;
    lastShown = gReforgedOverlay.count;
    lastFound = found;
    if (miss < 0)
        PortLog("[rfo-hpbars] frame %u: shown owners %d, all in OAM\n", gPortFrameCount, lastShown);
    else
        PortLog("[rfo-hpbars] frame %u: shown owners %d, %d in OAM; first missing %04X %04X %04X\n",
            gPortFrameCount, lastShown, found, gReforgedOverlay.owner[miss].a0, gReforgedOverlay.owner[miss].a1,
            gReforgedOverlay.owner[miss].a2);
}

/* Colour `index` of the map's OBJ palette 0, from the ROM data. */
static unsigned short BarColour(int index)
{
    return (unsigned short)(RFO_OVERLAY_OPAQUE | (gPal_MiscUiGraphics[index] & 0x7FFF));
}

static void DrawBar(unsigned short pix[16][16], int hp, int maxHp)
{
    int fill = BAR_FILL - BAR_FILL * (maxHp - hp) / maxHp; /* 1 to 11 for 0 < hp < maxHp */
    unsigned short col = BarColour(fill > 8 ? BAR_HIGH_INDEX
                                 : fill > 5 ? BAR_MID_INDEX
                                 : fill > 3 ? BAR_ORANGE_INDEX
                                 : BAR_LOW_INDEX);
    unsigned short empty = BarColour(BAR_EMPTY_INDEX);
    int x, y;

    for (x = 1; x <= BAR_FILL + 2; x++)
    {
        pix[BAR_ROW][x] = COL_FRAME;
        pix[BAR_ROW + 3][x] = COL_FRAME;
    }
    for (y = BAR_ROW + 1; y <= BAR_ROW + 2; y++)
    {
        pix[y][1] = COL_FRAME;
        pix[y][BAR_FILL + 2] = COL_FRAME;
        for (x = 0; x < BAR_FILL; x++)
            pix[y][2 + x] = x < fill ? col : empty;
    }
}

/* A "!" 3 pixels wide, rows MARK_ROW to MARK_ROW + 7, from column MARK_X. */
static void DrawMark(unsigned short pix[16][16], unsigned short col)
{
    static const char shape[8][4] = { "KKK", "KWK", "KWK", "KWK", "KWK", "KKK", "KWK", "KKK" };
    int r, c;

    for (r = 0; r < 8; r++)
        for (c = 0; c < 3; c++)
            pix[MARK_ROW + r][MARK_X + c] = shape[r][c] == 'W' ? col : COL_FRAME;
}

/* The OAM words PutOamExt (port/asm/arm_pc.c) writes for one entry of `list`
   at (x, y) with oam2: the same arithmetic. */
static void OamWords(const u16 *list, int x, int y, int oam2, unsigned short *a)
{
    u32 xy = ((u32)x & 0xFFFF) | ((u32)y << 16);
    u32 yy = xy >> 16;

    a[0] = (unsigned short)(((list[1] | yy) & 0xFF00) | ((list[1] + yy) & 0xFF));
    a[1] = (unsigned short)(((list[2] | xy) & 0xFE00) | ((list[2] + xy) & 0x1FF));
    a[2] = (unsigned short)(list[3] + oam2);
}

void Reforged_MapHpBarsCollect(void)
{
    struct Unit *byHandle[RFO_OVERLAY_MAX];
    struct SMSHandle *it;
    struct Unit *sel;
    u32 before = 0;
    int i, opt, bars = 0, white = 0, orange = 0;

    sPending.count = 0;
    opt = Reforged_GetOption(RFO_MAP_HPBARS);
    if (opt == RFO_HPBARS_OFF)
    {
        sMarkFor.unit = -1;
        SelectionDone();
        return;
    }
    if (CheckOn())
        before = GbaHash();

    /* each handle's unit (RefreshUnitSprites sets pMapSpriteHandle; traps have none) */
    memset(byHandle, 0, sizeof(byHandle));
    for (i = 1; i < FACTION_PURPLE + 6; i++)
    {
        struct Unit *unit = GetUnit(i);
        int h;

        if (!UNIT_IS_VALID(unit) || unit->pMapSpriteHandle == NULL)
            continue;
        h = unit->pMapSpriteHandle - gSMSHandleArray;
        if (h >= 0 && h < RFO_OVERLAY_MAX)
            byHandle[h] = unit;
    }

    /* the marks: once per selection (a new unit, chapter, turn or phase) */
    sel = NULL;
    if (opt & RFO_HPBARS_WARNINGS)
        sel = SelectedUnit();
    else
        SelectionDone();
    if (sel == NULL)
        sMarkFor.unit = -1;
    else if (sMarkFor.unit != sel->index || sMarkFor.chapter != gPlaySt.chapterIndex ||
             sMarkFor.turn != gPlaySt.chapterTurnNumber || sMarkFor.phase != gPlaySt.faction)
        MarksFor(sel);

    if (CheckOn() && sel != NULL)
        CheckSurvey(byHandle);
    if (CheckOn())
        CheckBattle();
    if (CheckOn())
        CheckShownInOam();

    /* the sprites PutUnitSpritesOam pushes, with its tests (src/bmudisp.c) */
    for (it = gSMSHandleArray->pNext; it != NULL; it = it->pNext)
    {
        struct ReforgedOverlayOwner *o;
        struct Unit *unit;
        int h = it - gSMSHandleArray;
        const u16 *list;
        int x = it->xDisplay - gBmSt.camera.x;
        int y = it->yDisplay - gBmSt.camera.y;
        int r3 = 0, layer, maxHp = 0, mark = MARK_NONE, bar = 0;

        if (x < -16 || x > DISPLAY_WIDTH || y < -32 || y > DISPLAY_HEIGHT)
            continue;
        if (it->config & 0x80)
            continue;
        if (h < 0 || h >= RFO_OVERLAY_MAX || (unit = byHandle[h]) == NULL)
            continue;
        if (it->config & 0x40)
            r3 = GetGameClock() & 2;

        if (opt & RFO_HPBARS_BARS)
        {
            maxHp = GetUnitMaxHp(unit);
            bar = maxHp > 0 && unit->curHP > 0 && unit->curHP < maxHp;
        }
        if (sel != NULL && unit != sel)
            mark = sMark[unit->index & 0xFF];
        if (!bar && mark == MARK_NONE)
            continue;
        if (sPending.count >= RFO_OVERLAY_MAX)
            break;

        o = &sPending.owner[sPending.count];
        layer = (it->config & 0xf) >= 3 ? 3 : 2;
        switch (it->config & 0xf) {
        case 0:
        case 3:
            list = gObject_16x16;
            OamWords(list, OAM1_X(x + r3 + 0x200), OAM0_Y(0x100 + y), it->oam2Base + OAM2_LAYER(layer), &o->a0);
            o->dx = 0;
            o->dy = 0;
            break;
        case 1:
        case 4:
            list = gObject_16x32;
            OamWords(list, OAM1_X(x + r3 + 0x200), OAM0_Y(0x100 + y - 16), it->oam2Base + OAM2_LAYER(layer), &o->a0);
            o->dx = 0;
            o->dy = 16;
            break;
        case 2:
        case 5:
            list = gObject_32x32;
            OamWords(list, OAM1_X((x - 8) + r3 + 0x200), OAM0_Y(0x100 + y - 16), it->oam2Base + OAM2_LAYER(layer),
                &o->a0);
            o->dx = 8;
            o->dy = 16;
            break;
        default:
            continue; /* PutUnitSpritesOam pushes nothing */
        }

        memset(o->pix, 0, sizeof(o->pix));
        if (bar)
        {
            DrawBar(o->pix, unit->curHP, maxHp);
            bars++;
        }
        if (mark != MARK_NONE)
            DrawMark(o->pix, mark == MARK_EFFECTIVE ? COL_WHITE : COL_ORANGE);
        white += mark == MARK_EFFECTIVE;
        orange += mark == MARK_CRIT;
        sPending.count++;
    }

    if (CheckOn())
    {
        u32 after = GbaHash();
        int s = sel != NULL ? sel->index : 0;

        if (after != before)
            PortLog("[rfo-hpbars] frame %u: GBA MEMORY CHANGED by the collect (0x%08X -> 0x%08X)\n",
                gPortFrameCount, before, after);
        if (bars != sLast.bars || white != sLast.white || orange != sLast.orange || s != sLast.selected)
            PortLog("[rfo-hpbars] frame %u: %d sprites drawn over: %d bars, %d white !, %d orange ! "
                    "(moving unit 0x%02X; option %d); GBA memory 0x%08X unchanged\n",
                gPortFrameCount, sPending.count, bars, white, orange, s & 0xFF, opt, after);
        sLast.bars = bars;
        sLast.white = white;
        sLast.orange = orange;
        sLast.selected = s;
    }
}

void Reforged_MapHpBarsVBlank(void)
{
    gReforgedOverlay.count = sPending.count;
    gReforgedOverlay.frame = gPortFrameCount;
    if (sPending.count != 0)
        memcpy(gReforgedOverlay.owner, sPending.owner, sizeof(sPending.owner[0]) * sPending.count);
    sPending.count = 0;
    Reforged_SkillBannerMapTick(); /* skill banners' map-animation popups (reforged_skillbanner.c) */
}

#endif /* PORT_PLAYED_BUILD */
