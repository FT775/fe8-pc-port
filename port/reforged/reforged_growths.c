#include "global.h"

/*
 * Reforged Options, "Growth rates on Select" (display only, OFF by default;
 * port-plan/pending/rfo-growth.md). Played builds only; empty in every test
 * build.
 *
 * With the option ON, Select on the unit stat screen's first page (personal
 * data, page 0) swaps the stat numbers for the unit's growth rates (percent),
 * and Select again swaps them back: the community convention of Tequila's
 * growth display (feuniverse.us/t/teqs-minor-assembly-shenanigans/1655; the
 * idea only, our own code; THANK-YOU.md). Retail gives Select no meaning on
 * that screen (src/statscreen.c StatScreen_OnIdle reads B, the D-pad, A with
 * a rescue, and R), so nothing is taken from the player. The other pages are
 * left alone (Select does nothing there, as retail); the view stays while the
 * player turns pages or moves to another unit (Up/Down) and is off again each
 * time the stat screen opens.
 *
 * The numbers are the ones the game's level-up rolls with (src/bmbattle.c
 * CheckBattleUnitLevelUp, and UnitLevelUp for the non-battle level-ups):
 * the character's own growths (pCharacterData->growthHP .. growthLck; FE8
 * adds no class growth there: class growths are only the enemies' autolevel
 * at load, src/bmunit.c UnitAutolevelCore), plus 5 on the first roll with
 * Metis' Tome (US_GROWTH_BOOST). The growth is drawn where the stat was, in
 * the game's own blue number font; the bonus is drawn after it as the game's
 * green "+N" (the stat bonus look): Metis' +5, and for your (blue) units the
 * Growths cheat's +25 or +50 when that cheat is on (reforged_cheats.c adds it
 * to every roll of a battle level-up; its Fixed mode adds nothing). The bar
 * under each stat shows the growth out of 100%. HP's growth takes the place
 * of "current HP / max HP" in the left panel. Mov and Con never grow and keep
 * their numbers.
 *
 * Stat names coloured by growth tier, as the same convention does (your units
 * only; the growth with its bonus): under 30% white, 30-39% grey, 40-49%
 * blue, 50-69% gold (the label's own colour), 70% and up green, all the
 * game's own system text colours.
 *
 * Display only, never a game value: nothing here reads the random numbers or
 * writes a unit, gActionData, gPlaySt or any other game field. The drawing is
 * the stat screen's own (DisplayPage, PutNumberOrBlank, PutNumberBonus,
 * DrawStatBarGfx, PutDrawText); the label strings are decoded into a buffer of
 * this file (GetStringFromIndexInBuffer), so the game's message cache
 * (sActiveMsg) is left as retail has it. No sound is played (a sound would
 * change the sound engine's state, and the swap shows plainly). Swapping back,
 * and closing the screen while the growths show (StatScreen_OnClose), draw the
 * retail page again; before any page draw the view's own special characters
 * (sSpecialCharStList entries, the font's counter, their glyph tiles) are
 * taken back (UndoSpecialChars), and before another page replaces the view,
 * page 0 is drawn once the retail way, so the stat screen's memory is what a
 * retail run has there once the view is gone. While the screen stays open the
 * one leftover is the icon draw counts (DrawnIconLookupTable: each page-0 draw
 * counts the aid and affinity icons again, as a retail page turn does), gone
 * once the screen closes and the map's icons are set up again. Scripted runs (pending/
 * rfo-growth.md) show the GBA RAM, I/O, palette, VRAM and OAM of an ON run
 * equal to an OFF run on every frame after each close.
 *
 * Room: those special characters share the stat screen's 208 text chrs with
 * its texts (184) and retail's own numbers (about 20 by page 2 in Chapter 2).
 * The view is drawn only when the characters it would add still fit before
 * the halo's graphics (ViewFits; first without HP's tier colour, then not at
 * all, the stats staying as they are); nothing past the text area is written.
 *
 * With the option OFF the hooks find the view off and change nothing (the
 * check below only logs): Select on the stat screen does nothing, as retail.
 *
 * One frame, one picture: the text glyphs and the stat-bar tiles are written
 * straight into VRAM, while the tilemaps that use them reach VRAM only at the
 * next VBlank's BG sync. When the view comes or goes (Select, a page turn away
 * from it, closing the screen while it shows), what this OnMain wrote into
 * those VRAM tiles is held back until right after that sync (DeferArm, then
 * Reforged_GrowthMainEnd and Reforged_GrowthAfterBgSync, called from src/bm.c
 * OnMain and OnVBlank), so the frame still drawn with the old tilemap keeps
 * the old glyphs and the next frame has the new map and the new glyphs
 * together. The list and the counter roll back at once (the allocations stay
 * retail); only the VRAM bytes wait one VBlank, and they are the bytes retail
 * has there, written before the VBlank's tile queue (FlushTiles) runs, as the
 * game's own direct writes are.
 */

#if PORT_PLAYED_BUILD

#include "fontgrp.h"
#include "hardware.h"
#include "bmunit.h"
#include "bmbattle.h"
#include "bmmind.h"
#include "bmsave.h"
#include "bm.h"
#include "rng.h"
#include "statscreen.h"
#include "functions.h"
#include "variables.h"
#include "constants/msg.h"

#include <string.h>

#include "port_reforged.h"
#include "port_hw.h" /* gPortFrameCount */

void PortLog(const char *fmt, ...);
char *getenv(const char *name);

extern struct Font *gActiveFont; /* src/fontgrp.c */

/* The text system's special characters (src/fontgrp.c, the same struct): each
   (colour, character) the screen draws gets the next text chr the first time,
   until ResetText. The stat screen's text chrs end at 208 (tile 0x80 + 2 x 208
   = 0x220, where StatScreen_InitDisplay puts the halo's graphics); its texts
   take 184 and retail's numbers and signs bring it to about 204 by page 2. A
   growth view needs some more (digits the stats did not use, the bonus's
   green "+N", HP's tier colour), so it is drawn only when they fit. */
struct SpecialCharSt
{
    s8 color;
    s8 id;
    s16 chr_position;
} AGBCC_STRUCT;
extern struct SpecialCharSt sSpecialCharStList[64];
#define STATSCREEN_TEXT_CHR_END 208
#define SPECIAL_CHAR_MAX        64

#define GROWTH_STATS 7 /* HP, Str/Mag, Skl, Spd, Def, Res, Lck: the level-up order */

static u8 sShown;   /* the growth view is on (Select on page 0) */
static u8 sHpShown; /* the left panel's HP row shows the HP growth now */
static u8 sLabelsColored; /* bit i: sRows[i]'s label is drawn in a tier colour */

/* What the view's own special characters overwrote, to put back when the view
   goes (UndoSpecialChars): the list's entries from the first one the view
   added (with the end marker and the old bytes after it), the font's counter
   and the glyph tiles of the chrs it took. A retail run never makes those
   characters, so it has the old bytes there. */
#define UNDO_MAX 28 /* the view adds 26 at most (MissingSpecialChars), + the end marker */
static struct {
    u8 valid;
    s8 first, end;     /* list entries [first, end) are the view's */
    u16 chr0, chr1;    /* and text chrs [chr0, chr1) */
    struct SpecialCharSt before[UNDO_MAX], after[UNDO_MAX];
    u8 tiles[UNDO_MAX][64];
} sUndo;

/* The VRAM held back for one VBlank when the view comes or goes (see the top
   of this file): the stat screen's text chrs (the font's vramDest, chrs 0 to
   STATSCREEN_TEXT_CHR_END, 64 bytes each) and page 0's six stat bars
   (DrawStatWithBar: tiles 0x401 + 6 x row, 6 tiles each). `shown` is what
   the screen showed when the transition began, `next` what this OnMain left
   there, `dirty` the chrs/tiles that differ. */
#define DEFER_BAR_TILE   0x401
#define DEFER_BAR_TILES  36
#define DEFER_TEXT_BYTES (STATSCREEN_TEXT_CHR_END * 64)
#define DEFER_BAR_BYTES  (DEFER_BAR_TILES * 32)
static struct {
    u8 armed;    /* a transition began in this OnMain */
    u8 pending;  /* `next` waits for the next VBlank's BG sync */
    u32 frame;   /* gPortFrameCount when armed */
    u8 *text;    /* the font's vramDest when armed */
    u8 shownText[DEFER_TEXT_BYTES], nextText[DEFER_TEXT_BYTES];
    u8 shownBar[DEFER_BAR_BYTES], nextBar[DEFER_BAR_BYTES];
    u8 dirtyText[STATSCREEN_TEXT_CHR_END], dirtyBar[DEFER_BAR_TILES];
} sDefer;

/* FE8PC_RFO_CHECK=1 (the test switch of reforged_dangerzone.c): on every
   Select on the stat screen, ON or OFF, a line with hashes of the game values
   the option must never change, after this file's step: every unit, the RNG
   seeds, gActionData, gPlaySt, gBmSt, the battle units, the BWL table. An ON
   run and an OFF run of the same key script must give the same hashes. Each
   growth view drawn is logged with the numbers shown and the text system's
   tile counter (gActiveFont->chr_counter: texts and special characters);
   each page drawn, with the counter and the special-character count.
   Unset: one getenv, the first time. */
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

static void LogGameHash(const char *when)
{
    u16 seeds[3];
    u32 units, rng, act, play, bmst, battle, bwl;

    if (!CheckOn())
        return;
    StoreRNState(seeds);
    units = Fnv(FNV_START, gUnitArrayBlue, sizeof(gUnitArrayBlue));
    units = Fnv(units, gUnitArrayRed, sizeof(gUnitArrayRed));
    units = Fnv(units, gUnitArrayGreen, sizeof(gUnitArrayGreen));
    rng = Fnv(FNV_START, seeds, sizeof(seeds));
    act = Fnv(FNV_START, &gActionData, sizeof(gActionData));
    play = Fnv(FNV_START, &gPlaySt, sizeof(gPlaySt));
    bmst = Fnv(FNV_START, &gBmSt, sizeof(gBmSt));
    battle = Fnv(FNV_START, &gBattleActor, sizeof(gBattleActor));
    battle = Fnv(battle, &gBattleTarget, sizeof(gBattleTarget));
    bwl = Fnv(FNV_START, gPidStatsData, sizeof(gPidStatsData));
    PortLog("[reforged] growths %s: option %d shown %d page %d unit 0x%02X | units %08X rng %08X act %08X "
            "playst %08X bmst %08X battle %08X bwl %08X\n",
        when, Reforged_GetOption(RFO_GROWTH_SELECT), sShown, gStatScreen.page,
        gStatScreen.unit != NULL ? gStatScreen.unit->index : 0, (unsigned)units, (unsigned)rng, (unsigned)act,
        (unsigned)play, (unsigned)bmst, (unsigned)battle, (unsigned)bwl);
}

/* The growths the level-up rolls with (HP, Str/Mag, Skl, Spd, Def, Res, Lck)
   and the bonus on top: Metis' +5, and the Growths cheat's +25/+50 for a blue
   unit (only blue units level up in battle, CanBattleUnitGainLevels). */
static int UnitGrowths(const struct Unit *unit, int growth[GROWTH_STATS])
{
    const struct CharacterData *c = unit->pCharacterData;
    int bonus = (unit->state & US_GROWTH_BOOST) ? 5 : 0;
    int cheat = Reforged_GetOption(RFO_CHEAT_GROWTHS);

    growth[0] = c->growthHP;
    growth[1] = c->growthPow;
    growth[2] = c->growthSkl;
    growth[3] = c->growthSpd;
    growth[4] = c->growthDef;
    growth[5] = c->growthRes;
    growth[6] = c->growthLck;
    if (UNIT_FACTION(unit) == FACTION_BLUE && (cheat == 2 || cheat == 3))
        bonus += cheat == 2 ? 25 : 50;
    return bonus;
}

/* The tier colour of a stat name (your units; the others keep the gold). */
static int TierColor(const struct Unit *unit, int total)
{
    if (UNIT_FACTION(unit) != FACTION_BLUE)
        return TEXT_COLOR_SYSTEM_GOLD;
    if (total >= 70)
        return TEXT_COLOR_SYSTEM_GREEN;
    if (total >= 50)
        return TEXT_COLOR_SYSTEM_GOLD;
    if (total >= 40)
        return TEXT_COLOR_SYSTEM_BLUE;
    if (total >= 30)
        return TEXT_COLOR_SYSTEM_GRAY;
    return TEXT_COLOR_SYSTEM_WHITE;
}

/* Page 0's six growing stats, top to bottom (DisplayPage0's DrawStatWithBar
   rows 0-5 at x 5): the growth index, the tile row, the label's text. */
static const struct { u8 stat, y, text; } sRows[6] = {
    { 1, 1, STATSCREEN_TEXT_POWLABEL },
    { 2, 3, STATSCREEN_TEXT_SKLLABEL },
    { 3, 5, STATSCREEN_TEXT_SPDLABEL },
    { 6, 7, STATSCREEN_TEXT_LCKLABEL },
    { 4, 9, STATSCREEN_TEXT_DEFLABEL },
    { 5, 11, STATSCREEN_TEXT_RESLABEL },
};

/* A row's label string (DisplayPage0's), decoded into `buf` (not the game's
   message cache). */
static const char *LabelString(const struct Unit *unit, int row, char *buf)
{
    int msg;

    switch (sRows[row].text)
    {
    case STATSCREEN_TEXT_POWLABEL:
        msg = UnitHasMagicRank((struct Unit *)unit) ? MSG_4FF : MSG_4FE; /* Mag / Str */
        break;
    case STATSCREEN_TEXT_SKLLABEL:
        msg = gMid_Skl;
        break;
    case STATSCREEN_TEXT_SPDLABEL:
        msg = gMid_Spd;
        break;
    case STATSCREEN_TEXT_LCKLABEL:
        msg = gMid_Lck;
        break;
    case STATSCREEN_TEXT_DEFLABEL:
        msg = gMid_Def;
        break;
    default:
        msg = gMid_Res;
        break;
    }
    return GetStringFromIndexInBuffer(msg, buf);
}

/* The labels drawn in a tier colour drawn gold again, as DisplayPage0 leaves
   them (its PutDrawText draws over a text's glyphs without clearing them, so
   a gold label drawn over a coloured one would mix the two): cleared, then the
   same cursor, colour and string. The tilemap is not touched. */
static void RestoreLabels(const struct Unit *unit)
{
    char str[0x40];
    int i;

    for (i = 0; i < 6; i++)
    {
        struct Text *text = &gStatScreen.text[sRows[i].text];

        if (!(sLabelsColored & (1 << i)))
            continue;
        ClearText(text);
        Text_SetCursor(text, 0);
        Text_SetColor(text, TEXT_COLOR_SYSTEM_GOLD);
        Text_DrawString(text, LabelString(unit, i, str));
    }
    sLabelsColored = 0;
}

static void ClearTiles(u16 *tm, int count)
{
    int i;

    for (i = 0; i < count; i++)
    {
        tm[i] = 0;
        tm[i + 0x20] = 0;
    }
}

/* Over the page DisplayPage0 just drew in gUiTmScratchA/C: each growing
   stat's number, bonus, bar and (your units) the label's colour. */
static void DrawPage0Growths(const struct Unit *unit)
{
    int growth[GROWTH_STATS], bonus = UnitGrowths(unit, growth), i;
    char str[0x40];

    for (i = 0; i < 6; i++)
    {
        int g = growth[sRows[i].stat], y = sRows[i].y, color = TierColor(unit, g + bonus);
        int base = g > 100 ? 100 : g, top = g + bonus > 100 ? 100 : g + bonus;
        struct Text *text = &gStatScreen.text[sRows[i].text];

        /* the label first: its PutText writes x 1-3, and a growth of 100 and
           up takes x 3 (as the stat screen draws three digits) */
        if (color != TEXT_COLOR_SYSTEM_GOLD || (sLabelsColored & (1 << i)))
        {
            ClearText(text); /* PutDrawText draws over the old glyphs */
            PutDrawText(text, gUiTmScratchA + TILEMAP_INDEX(1, y), color, 0, 0, LabelString(unit, i, str));
            if (color == TEXT_COLOR_SYSTEM_GOLD)
                sLabelsColored &= ~(1 << i);
            else
                sLabelsColored |= 1 << i;
        }

        /* the stat's number (x 4-5, 3-5 from 100) and bonus (x 6-8) */
        ClearTiles(gUiTmScratchA + TILEMAP_INDEX(4, y), 5);
        PutNumberOrBlank(gUiTmScratchA + TILEMAP_INDEX(5, y), TEXT_COLOR_SYSTEM_BLUE, g);
        PutNumberBonus(bonus, gUiTmScratchA + TILEMAP_INDEX(6, y));

        /* the bar: the growth out of 100%, the bonus in the bonus colour (as
           DrawStatWithBar draws a stat out of 30) */
        DrawStatBarGfx(0x401 + i * 6, 6, gUiTmScratchC + TILEMAP_INDEX(3, y + 1), TILEREF(0, STATSCREEN_BGPAL_6),
            41, base * 41 / 100, (top - base) * 41 / 100);
    }

    if (CheckOn())
        PortLog("[reforged] growths drawn: unit 0x%02X pid 0x%02X: HP %d Str/Mag %d Skl %d Spd %d Def %d Res %d "
                "Lck %d, bonus +%d (Metis %d, cheat %d); text chr %d\n",
            unit->index, unit->pCharacterData->number, growth[0], growth[1], growth[2], growth[3], growth[4],
            growth[5], growth[6], bonus, (unit->state & US_GROWTH_BOOST) ? 1 : 0,
            Reforged_GetOption(RFO_CHEAT_GROWTHS), gActiveFont->chr_counter);
}

/* The special characters the growth view would add (not made yet): the
   digits of each growth (blue), the bonus's green "+" and small digits, and
   with hpColor HP's two letters in their tier colour. */
static int MissingSpecialChars(const struct Unit *unit, int hpColor)
{
    int growth[GROWTH_STATS], bonus = UnitGrowths(unit, growth);
    u8 want[2][32]; /* 7 growths of up to 3 digits, "+" and 2 digits, HP's 2 letters: 26 at most */
    int n = 0, i, j, missing = 0;

    for (i = 0; i < GROWTH_STATS; i++)
    {
        int v = growth[i];

        do
        {
            want[0][n] = TEXT_COLOR_SYSTEM_BLUE;
            want[1][n++] = TEXT_SPECIAL_BIGNUM_0 + v % 10;
            v /= 10;
        } while (v != 0);
    }
    if (bonus != 0)
    {
        int v = bonus;

        want[0][n] = TEXT_COLOR_SYSTEM_GREEN;
        want[1][n++] = TEXT_SPECIAL_PLUS;
        while (v != 0)
        {
            want[0][n] = TEXT_COLOR_SYSTEM_GREEN;
            want[1][n++] = TEXT_SPECIAL_SMALLNUM_0 + v % 10;
            v /= 10;
        }
    }
    if (hpColor)
    {
        int color = TierColor(unit, growth[0] + bonus);

        want[0][n] = color;
        want[1][n++] = TEXT_SPECIAL_HP_A;
        want[0][n] = color;
        want[1][n++] = TEXT_SPECIAL_HP_B;
    }
    for (i = 0; i < n; i++)
    {
        int dup = 0, k;

        for (j = 0; j < i && !dup; j++)
            dup = want[0][j] == want[0][i] && want[1][j] == want[1][i];
        for (k = 0; !dup && k < SPECIAL_CHAR_MAX && sSpecialCharStList[k].color >= 0; k++)
            dup = sSpecialCharStList[k].color == want[0][i] && sSpecialCharStList[k].id == want[1][i];
        missing += !dup;
    }
    return missing;
}

static int SpecialCharCount(void)
{
    int k = 0;

    while (k < SPECIAL_CHAR_MAX && sSpecialCharStList[k].color >= 0)
        k++;
    return k;
}

/* 1 when the view's new special characters fit the stat screen's text chrs
   (and the list); *hpColor: 1 with HP's tier colour, 0 without it. */
static int ViewFits(const struct Unit *unit, int *hpColor)
{
    int room = STATSCREEN_TEXT_CHR_END - gActiveFont->chr_counter;
    int listRoom = SPECIAL_CHAR_MAX - 1 - SpecialCharCount();

    if (listRoom < room)
        room = listRoom;
    *hpColor = 1;
    if (MissingSpecialChars(unit, 1) <= room)
        return 1;
    *hpColor = 0;
    return MissingSpecialChars(unit, 0) <= room;
}

/* Writes the held-back bytes (`next`) into VRAM where VRAM still holds what
   Reforged_GrowthMainEnd left there (`shown`): a chr something else wrote
   since keeps that newer write, as it would on retail. */
static void DeferRelease(void)
{
    int i;

    if (!sDefer.pending)
        return;
    sDefer.pending = 0;
    for (i = 0; i < STATSCREEN_TEXT_CHR_END; i++)
        if (sDefer.dirtyText[i] && memcmp(sDefer.text + i * 64, sDefer.shownText + i * 64, 64) == 0)
            memcpy(sDefer.text + i * 64, sDefer.nextText + i * 64, 64);
    for (i = 0; i < DEFER_BAR_TILES; i++)
        if (sDefer.dirtyBar[i] && memcmp((u8 *)VRAM + (DEFER_BAR_TILE + i) * 32, sDefer.shownBar + i * 32, 32) == 0)
            memcpy((u8 *)VRAM + (DEFER_BAR_TILE + i) * 32, sDefer.nextBar + i * 32, 32);
}

/* A transition of the view begins (before its first VRAM write in this
   OnMain): what the screen shows now. */
static void DeferArm(void)
{
    if (sDefer.armed && sDefer.frame == gPortFrameCount)
        return; /* already armed in this OnMain */
    DeferRelease(); /* (never left pending past a BG sync; just in case) */
    sDefer.armed = 1;
    sDefer.frame = gPortFrameCount;
    sDefer.text = gActiveFont->vramDest;
    memcpy(sDefer.shownText, sDefer.text, DEFER_TEXT_BYTES);
    memcpy(sDefer.shownBar, (u8 *)VRAM + DEFER_BAR_TILE * 32, DEFER_BAR_BYTES);
}

/* src/bm.c OnMain, its procs done (before VBlankIntrWait draws the frame):
   the chrs and tiles this OnMain changed get back what the screen showed,
   their new bytes kept for Reforged_GrowthAfterBgSync. */
void Reforged_GrowthMainEnd(void)
{
    int i, n = 0;

    if (!sDefer.armed)
        return;
    sDefer.armed = 0;
    if (sDefer.frame != gPortFrameCount)
    {
        if (CheckOn()) /* armed outside OnMain: nothing held back */
            PortLog("[reforged] growths: VRAM hold armed at frame %u, not in this OnMain: dropped\n",
                (unsigned)sDefer.frame);
        return;
    }
    for (i = 0; i < STATSCREEN_TEXT_CHR_END; i++)
    {
        u8 *v = sDefer.text + i * 64;

        sDefer.dirtyText[i] = memcmp(v, sDefer.shownText + i * 64, 64) != 0;
        if (sDefer.dirtyText[i])
        {
            memcpy(sDefer.nextText + i * 64, v, 64);
            memcpy(v, sDefer.shownText + i * 64, 64);
            n++;
        }
    }
    for (i = 0; i < DEFER_BAR_TILES; i++)
    {
        u8 *v = (u8 *)VRAM + (DEFER_BAR_TILE + i) * 32;

        sDefer.dirtyBar[i] = memcmp(v, sDefer.shownBar + i * 32, 32) != 0;
        if (sDefer.dirtyBar[i])
        {
            memcpy(sDefer.nextBar + i * 32, v, 32);
            memcpy(v, sDefer.shownBar + i * 32, 32);
            n++;
        }
    }
    sDefer.pending = n != 0;
    if (CheckOn())
        PortLog("[reforged] growths: frame %u: %d VRAM chrs/tiles held until the BG sync\n",
            (unsigned)gPortFrameCount, n);
}

/* src/bm.c OnVBlank, right after FlushBackgrounds (the new tilemaps are in
   VRAM now) and before FlushTiles: the held-back glyphs and bar tiles. */
void Reforged_GrowthAfterBgSync(void)
{
    DeferRelease();
}

static int UndoCount(int first)
{
    int n = SPECIAL_CHAR_MAX - first;

    return n < UNDO_MAX ? n : UNDO_MAX;
}

/* Before the view draws: what it may overwrite. */
static void SaveSpecialChars(void)
{
    int i;

    sUndo.first = SpecialCharCount();
    sUndo.chr0 = gActiveFont->chr_counter;
    memcpy(sUndo.before, &sSpecialCharStList[sUndo.first], UndoCount(sUndo.first) * sizeof(struct SpecialCharSt));
    for (i = 0; i < UNDO_MAX && sUndo.chr0 + i < STATSCREEN_TEXT_CHR_END; i++)
        memcpy(sUndo.tiles[i], gActiveFont->vramDest + (sUndo.chr0 + i) * 64, 64);
}

/* After it drew: what it made, so the undo can tell nothing came after it. */
static void SavedSpecialCharsDrawn(void)
{
    sUndo.end = SpecialCharCount();
    sUndo.chr1 = gActiveFont->chr_counter;
    memcpy(sUndo.after, &sSpecialCharStList[sUndo.first], UndoCount(sUndo.first) * sizeof(struct SpecialCharSt));
    sUndo.valid = sUndo.end > sUndo.first;
}

/* The view's special characters taken back once nothing shows them (at the
   start of the next page draw, src/statscreen.c DisplayPage: the old page is
   gone then, and the HP row is drawn again in the same call), so the text
   system is left as a retail run leaves it:
   - still the list's last ones (a redraw of the page, a page turn): the list's
     entries, the counter and the tiles as they were before the view drew;
   - the list started again (ResetText: another unit): the entries and tiles
     the new draw has not reached yet get their old bytes back (a retail run
     has those there, past its end marker);
   - anything else (an allocation after the view's that was not a page draw):
     left as they are (logged with FE8PC_RFO_CHECK). */
static void UndoSpecialChars(void)
{
    int i, count, counter, from;

    if (!sUndo.valid)
        return;
    sUndo.valid = 0;
    count = SpecialCharCount();
    counter = gActiveFont->chr_counter;
    if (count == sUndo.end && counter == sUndo.chr1
        && memcmp(sUndo.after, &sSpecialCharStList[sUndo.first],
               UndoCount(sUndo.first) * sizeof(struct SpecialCharSt)) == 0)
    {
        from = sUndo.first; /* the whole of it, the end marker too */
        counter = sUndo.chr0;
        gActiveFont->chr_counter = sUndo.chr0;
    }
    else if (counter < sUndo.chr1) /* the counter only goes back at ResetText */
        from = count + 1 > sUndo.first ? count + 1 : sUndo.first; /* keep the new end marker */
    else
    {
        if (CheckOn())
            PortLog("[reforged] growths: the view's special characters are no longer the last ones: left as they "
                    "are@N");
        return;
    }
    for (i = from; i < sUndo.first + UndoCount(sUndo.first); i++)
        sSpecialCharStList[i] = sUndo.before[i - sUndo.first];
    for (i = counter > sUndo.chr0 ? counter : sUndo.chr0; i < sUndo.chr1 && i - sUndo.chr0 < UNDO_MAX; i++)
        memcpy(gActiveFont->vramDest + i * 64, sUndo.tiles[i - sUndo.chr0], 64);
}

/* src/statscreen.c DisplayPage, before the page is drawn: the view's text
   characters taken back; and when page 0 with the growths gives way to another
   page, page 0 drawn once more the retail way first (DisplayPage0 into the
   scratch maps the coming draw clears), so what only page 0 draws and the
   other pages keep (the stat bars' tiles, the labels' glyphs, the bar
   drawing's scratch, the message cache) is left as a retail page 0 left it. */
void Reforged_GrowthPageWillDraw(int pageid)
{
    int away = sHpShown && pageid != 0 && pageid != 3 && gStatScreen.unit != NULL;

    if (away)
        DeferArm(); /* the old page and the HP row show the view until the BG sync */
    UndoSpecialChars();
    if (away)
    {
        if (sLabelsColored)
            RestoreLabels(gStatScreen.unit);
        DisplayPage0();
    }
}

/* The left panel's HP row (tile row 17, x 1-8 of gBG0TilemapBuffer): the HP
   growth, or the retail "HP cur/max" (DisplayLeftPanel's own lines). */
static void DrawHpRow(const struct Unit *unit, int growthView, int hpColor)
{
    u16 *row = gBG0TilemapBuffer + TILEMAP_INDEX(0, 17);

    ClearTiles(row + 1, 8);
    if (growthView)
    {
        int growth[GROWTH_STATS], bonus = UnitGrowths(unit, growth);
        int color = hpColor ? TierColor(unit, growth[0] + bonus) : TEXT_COLOR_SYSTEM_GOLD;

        PutTwoSpecialChar(row + 1, color, TEXT_SPECIAL_HP_A, TEXT_SPECIAL_HP_B);
        PutNumberOrBlank(row + 5, TEXT_COLOR_SYSTEM_BLUE, growth[0]);
        PutNumberBonus(bonus, row + 6);
    }
    else
    {
        PutTwoSpecialChar(row + 1, TEXT_COLOR_SYSTEM_GOLD, TEXT_SPECIAL_HP_A, TEXT_SPECIAL_HP_B);
        PutSpecialChar(row + 5, TEXT_COLOR_SYSTEM_GOLD, TEXT_SPECIAL_SLASH);
        if (GetUnitCurrentHp((struct Unit *)unit) > 99)
            PutTwoSpecialChar(row + 3, TEXT_COLOR_SYSTEM_BLUE, TEXT_SPECIAL_DASH, TEXT_SPECIAL_DASH);
        else
            PutNumberOrBlank(row + 4, TEXT_COLOR_SYSTEM_BLUE, GetUnitCurrentHp((struct Unit *)unit));
        if (GetUnitMaxHp((struct Unit *)unit) > 99)
            PutTwoSpecialChar(row + 6, TEXT_COLOR_SYSTEM_BLUE, TEXT_SPECIAL_DASH, TEXT_SPECIAL_DASH);
        else
            PutNumberOrBlank(row + 7, TEXT_COLOR_SYSTEM_BLUE, GetUnitMaxHp((struct Unit *)unit));
    }
    sHpShown = growthView;
    BG_EnableSyncByMask(BG0_SYNC_BIT);
}

void Reforged_GrowthReset(void)
{
    sShown = 0;
    sHpShown = 0;
    sLabelsColored = 0;
    sUndo.valid = 0;
    /* (sDefer is left alone: a hold of this OnMain is still released at the
       next BG sync) */
}

/* src/statscreen.c DisplayPage, after the page's own drawing (a unit's first
   draw, a unit change, a page turn, and this file's redraws). */
void Reforged_GrowthPageDrawn(int pageid)
{
    struct Unit *unit = gStatScreen.unit;
    int page0 = pageid == 0 || pageid == 3; /* DisplayPage's lut: 3 is page 0 again */
    int hpColor, drawn = 0;

    if (unit == NULL)
        return;

    if (sShown && page0)
    {
        drawn = ViewFits(unit, &hpColor);
        if (drawn)
        {
            SaveSpecialChars();
            DrawPage0Growths(unit);
            DrawHpRow(unit, 1, hpColor);
            SavedSpecialCharsDrawn();
        }
        else if (CheckOn())
            PortLog("[reforged] growths: no room for the view's numbers (text chr %d, special chars %d): the "
                    "stats stay\n", gActiveFont->chr_counter, SpecialCharCount());
    }
    if (!drawn)
    {
        if (sLabelsColored)
            RestoreLabels(unit); /* another page now: the labels as retail leaves them */
        if (sHpShown)
            DrawHpRow(unit, 0, 0);
    }
    /* the text tiles in use: the stat screen's text area ends at chr 208
       (tile 0x80 + 2 x 208 = 0x220, where its halo graphics start) */
    if (CheckOn())
        PortLog("[reforged] growths page %d drawn (unit 0x%02X, shown %d): text chr %d, special chars %d\n", pageid,
            unit->index, drawn, gActiveFont->chr_counter, SpecialCharCount());
}

/* The page drawn again (DisplayPage, then the copy StatScreen_Display makes
   into the BG0/BG2 page area). */
static void Redraw(void)
{
    if (sLabelsColored && !sShown)
        RestoreLabels(gStatScreen.unit); /* before DisplayPage0 draws them gold */
    DisplayPage(gStatScreen.page);
    TileMap_CopyRect(gUiTmScratchA, gBG0TilemapBuffer + TILEMAP_INDEX(12, 2), 18, 18);
    TileMap_CopyRect(gUiTmScratchC, gBG2TilemapBuffer + TILEMAP_INDEX(12, 2), 18, 18);
    BG_EnableSyncByMask(BG0_SYNC_BIT | BG2_SYNC_BIT);
}

/* src/statscreen.c StatScreen_OnIdle: Select, when no retail key acted. */
void Reforged_GrowthSelect(void)
{
    if (Reforged_GetOption(RFO_GROWTH_SELECT) && gStatScreen.page == 0 && gStatScreen.unit != NULL)
    {
        DeferArm(); /* the old picture stays until the BG sync */
        sShown ^= 1;
        Redraw();
    }
    LogGameHash("select");
}

/* src/statscreen.c StatScreen_OnClose (the screen is dark by then): the
   retail page is drawn again, so the page's buffers are left as retail
   leaves them. */
void Reforged_GrowthClose(void)
{
    sShown = 0;
    if (sHpShown || sLabelsColored)
        DeferArm(); /* the screen still shows the view until the BG sync */
    if (sHpShown)
        Redraw(); /* the growths show: the retail page and HP row again */
    else if (sLabelsColored)
        RestoreLabels(gStatScreen.unit);
    sHpShown = 0;
    LogGameHash("close");
}

#endif /* PORT_PLAYED_BUILD */
