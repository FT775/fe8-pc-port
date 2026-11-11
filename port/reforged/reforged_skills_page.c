#include "global.h"

/*
 * Reforged Options, Game fixes: class skills, the stat screen's 4th page "Skills" (the owner's
 * answer of 2026-11-07: "Own page"; design "Skills proposal (draft).md" section 3b: what each
 * promotion keeps / gains / loses is visible without promoting).
 *
 * The page exists only with skills on (Reforged_SkillsOn) for a unit that has a skill or, for a
 * blue unit that can still promote, a promotion that changes its skills: otherwise the screen
 * keeps its 3 pages and is unchanged. Hooks in src/statscreen.c (all PC played builds only):
 *   - StatScreen_Display: pageAmt 3 + Reforged_SkillPageExists (the "1/4" counter's digit 4 is in
 *     the screen's own object graphics, chr 0x289 + 4); a page past the count (the page remembered
 *     in gPlaySt.chapterStateBits & 3, a unit without the page) becomes page 0;
 *   - DisplayPage's lut slot 3 (retail: DisplayPage0 again, never reached) draws this page;
 *     Reforged_SkillPageDrawn after every page draw puts or takes away the page's title;
 *   - DisplayPageNameSprite: for page 3 only the name plate (the sprite tables have 3 entries);
 *     the title "Skills" is text on BG0 over the plate (rows 0-1, which nothing else uses);
 *   - StartStatScreenHelp: R on page 3 starts at this page's help boxes (Reforged_SkillPageHelp).
 *
 * Text tiles: the stat screen's text area is full (184 of its 208 chrs are its Text slots, the
 * rest are numbers; see reforged_growths.c). Pages are drawn one at a time and every page draws
 * (and clears) its own slots, so this page borrows the chrs of the right-hand pages' slots, which
 * InitTextInitInfo allocates back to back: STATSCREEN_TEXT_UNUSUED up to STATSCREEN_TEXT_BWL (169
 * chrs). The left panel's two slots (name, class) are never touched. 8 lines of 16 tiles + the
 * 5-tile title = 133 chrs; if the run of chrs were ever shorter, fewer lines are drawn.
 *
 * R help: each line has a help box whose text is a virtual id 0x7F54 + line (Reforged_SkillPageMsg;
 * port/reforged/reforged_items.c Reforged_VirtualMsg must route 0x7F54-0x7F5F here).
 */

#if PORT_PLAYED_BUILD

#include <stdio.h>
#include <string.h>

#include "hardware.h"
#include "fontgrp.h"
#include "bmunit.h"
#include "statscreen.h"
#include "constants/msg.h"

#include "port_reforged.h"
#include "port_hw.h"
#include <stdlib.h>
#include "reforged_skills.h"

#define PAGE_LINES   8   /* rows 1, 3, ..., 15 of the page area, as the other pages */
#define LINE_TILES   16  /* tile x 1-16 of the 18-tile page area */
#define LINE_PX      (LINE_TILES * 8 - 2)
#define TITLE_TILES  5
#define TITLE_X      18  /* BG0 tile column; the name plate is x 111-217, centre 164 */
#define TITLE_CENTER 164
#define HELP_CHARS   32  /* help box line length (the talk font) */
#define HELP_LINES   3
#define MSG_BASE     0x7F54
#define MSG_IDS      12  /* 0x7F54-0x7F5F */
#define LIST_MAX     16

int Reforged_SkillPageExists(struct Unit *unit);
void Reforged_SkillPageDisplay(void);
void Reforged_SkillPageWillDraw(int pageid);
void Reforged_SkillPageDrawn(int pageid);
const struct HelpBoxInfo *Reforged_SkillPageHelp(const struct HelpBoxInfo *current);
int Reforged_SkillPageMsg(int index, char *buf);

static struct Text sLine[PAGE_LINES];
static struct Text sTitle;
static int sLines;      /* lines the borrowed chrs allow (<= PAGE_LINES) */
static int sRows;       /* lines drawn for the current unit (each has a help box) */
static u8 sTitleShown;
static char sHelp[PAGE_LINES][200];

static struct HelpBoxInfo sRowHb[PAGE_LINES];
static struct HelpBoxInfo sCharHb, sClassHb, sLevelHb, sExpHb, sHpHb;

/* --- what the page shows --- */

struct PromoLine {
    int jid;
    unsigned char gains[8], keeps[8], loses[8];
    int ng, nk, nl;
};

static int Choices(const struct Unit *unit, struct PromoLine *out)
{
    int jids[4], n, i;

    if (UNIT_FACTION(unit) != FACTION_BLUE || !Reforged_RunFix(RFO_FIX_SKILLS))
        return 0; /* only the player's units promote, and only with player skills on */
    n = Reforged_SkillPromoChoices(unit, jids);
    if (n > 3)
        n = 3;
    for (i = 0; i < n; i++)
    {
        out[i].jid = jids[i];
        Reforged_SkillPromoChange(unit, jids[i], out[i].gains, &out[i].ng, out[i].keeps, &out[i].nk,
            out[i].loses, &out[i].nl);
    }
    return n;
}

int Reforged_SkillPageExists(struct Unit *unit)
{
    unsigned char list[LIST_MAX];
    struct PromoLine promo[3];
    int n, i;

    if (unit == NULL || unit->pClassData == NULL || unit->pCharacterData == NULL || !Reforged_SkillsOn())
        return 0;
    if (Reforged_SkillUnitList(unit, list, LIST_MAX) > 0)
        return 1;
    n = Choices(unit, promo);
    for (i = 0; i < n; i++)
        if (promo[i].ng + promo[i].nl > 0)
            return 1;
    return 0;
}

/* --- text helpers --- */

/* Cuts s until it fits maxpx pixels: at the last space if there is one, else a character. */
static void Fit(char *s, int maxpx)
{
    int len = (int)strlen(s);

    while (len > 0 && GetStringTextLen(s) > maxpx)
    {
        char *sp = strrchr(s, ' ');

        if (sp != NULL && sp != s)
        {
            *sp = 0;
            len = (int)(sp - s);
            while (len > 0 && (s[len - 1] == ':' || s[len - 1] == ',' || s[len - 1] == ' '))
                s[--len] = 0;
        }
        else
            s[--len] = 0;
    }
}

/* Draws str at x (cut to the line), returns the cursor after it. */
static int Put(struct Text *t, int x, int color, const char *str)
{
    char tmp[64];

    if (x >= LINE_PX)
        return x;
    snprintf(tmp, sizeof(tmp), "%s", str);
    Fit(tmp, LINE_PX - x);
    if (tmp[0] == 0)
        return x;
    Text_InsertDrawString(t, x, color, tmp);
    return Text_GetCursor(t);
}

/* Greedy word wrap of src into dst: lines of HELP_CHARS characters at most, HELP_LINES lines at
   most (the rest is dropped), the game's line break "\x01" between lines. */
static void Wrap(char *dst, int size, const char *src)
{
    int len = 0, col = 0, lines = 1;
    const char *p = src;

    dst[0] = 0;
    while (*p)
    {
        const char *w = p;
        int wl;

        while (*w == ' ')
            w++;
        if (*w == 0)
            break;
        p = w;
        while (*p && *p != ' ')
            p++;
        wl = (int)(p - w);
        if (col > 0 && col + 1 + wl > HELP_CHARS)
        {
            if (lines == HELP_LINES || len + 1 >= size)
                break;
            dst[len++] = '\x01';
            lines++;
            col = 0;
        }
        else if (col > 0)
        {
            if (len + 1 >= size)
                break;
            dst[len++] = ' ';
            col++;
        }
        if (len + wl >= size)
            break;
        memcpy(dst + len, w, wl);
        len += wl;
        col += wl;
        dst[len] = 0;
    }
    dst[len] = 0;
}

/* The class the names are shown for (Str +2 or Mag +2): the unit's own while its skills are
   drawn, a promotion choice's while that choice's line is. */
static int sNameJid;

static const char *PName(int skill)
{
    return Reforged_SkillShownName(skill, sNameJid);
}

static const char *PHelp(int skill)
{
    return Reforged_SkillShownHelp(skill, sNameJid);
}

static void Names(char *out, int size, const char *label, const unsigned char *ids, int n)
{
    int i, len;

    len = snprintf(out, size, "%s", label);
    for (i = 0; i < n && len < size; i++)
        len += snprintf(out + len, size - len, "%s%s", i ? ", " : "", PName(ids[i]));
    if (len < size)
        snprintf(out + len, size - len, ". ");
}

/* --- the page --- */

static void SetupLines(void)
{
    int base = gStatScreen.text[STATSCREEN_TEXT_UNUSUED].chr_position;
    int end = gStatScreen.text[STATSCREEN_TEXT_BWL].chr_position + gStatScreen.text[STATSCREEN_TEXT_BWL].tile_width;
    int i, room = end - base - TITLE_TILES;

    sLines = room > 0 ? room / LINE_TILES : 0;
    if (sLines > PAGE_LINES)
        sLines = PAGE_LINES;
    for (i = 0; i < sLines; i++)
    {
        memset(&sLine[i], 0, sizeof(sLine[i]));
        sLine[i].chr_position = base + i * LINE_TILES;
        sLine[i].tile_width = LINE_TILES;
    }
    memset(&sTitle, 0, sizeof(sTitle));
    sTitle.chr_position = base + sLines * LINE_TILES;
    sTitle.tile_width = TITLE_TILES;
}

static struct Text *NewRow(void)
{
    struct Text *t = &sLine[sRows];

    ClearText(t);
    PutText(t, gUiTmScratchA + TILEMAP_INDEX(1, 1 + sRows * 2));
    return t;
}

static void SkillRow(int skill)
{
    struct Text *t = NewRow();
    const char *help = PHelp(skill);
    char text[200];
    int x = Put(t, 0, TEXT_COLOR_SYSTEM_WHITE, PName(skill));

    if (help != NULL && help[0] && x + 6 + 20 <= LINE_PX)
        Put(t, x + 6, TEXT_COLOR_SYSTEM_BLUE, help);
    snprintf(text, sizeof(text), "%s: %s", PName(skill), help ? help : "");
    Wrap(sHelp[sRows], sizeof(sHelp[0]), text);
    sRows++;
}

static void AlsoRow(const unsigned char *ids, int n)
{
    struct Text *t = NewRow();
    char text[200];
    int x = Put(t, 0, TEXT_COLOR_SYSTEM_GOLD, "Also:"), i, len = 0, lines = 0;

    for (i = 0; i < n; i++)
    {
        const char *name = PName(ids[i]);

        if (x + 4 + GetStringTextLen(name) > LINE_PX)
            break;
        x = Put(t, x + 4, TEXT_COLOR_SYSTEM_WHITE, name);
    }
    /* the help: "Name: effect" a line each, as many as fit */
    text[0] = 0;
    for (i = 0; i < n && lines < HELP_LINES && len < (int)sizeof(text); i++, lines++)
    {
        char one[64];

        snprintf(one, sizeof(one), "%s: %s", PName(ids[i]),
            PHelp(ids[i]) ? PHelp(ids[i]) : "");
        one[HELP_CHARS + 6] = 0; /* one line each: a long effect is cut */
        len += snprintf(text + len, sizeof(text) - len, "%s%s", lines ? "\x01" : "", one);
    }
    snprintf(sHelp[sRows], sizeof(sHelp[0]), "%s", text);
    sRows++;
}

static void PromoRow(const struct PromoLine *p)
{
    struct Text *t = NewRow();

    sNameJid = p->jid; /* the choice's names (Str or Mag +2) */
    const struct ClassData *cd = GetClassData(p->jid);
    char cls[40], buf[24], gains[64], keeps[64], loses[64], all[200];
    int x, i, stop = 0;

    snprintf(cls, sizeof(cls), "%s", cd ? GetStringFromIndex(cd->nameTextId) : "?");
    snprintf(buf, sizeof(buf), "%s:", cls);
    Fit(buf, 72);
    x = Put(t, 0, TEXT_COLOR_SYSTEM_WHITE, buf);
    if (p->ng + p->nk + p->nl == 0)
        Put(t, x + 4, TEXT_COLOR_SYSTEM_GRAY, "none");
    /* "+Gain" green, "=Keep" blue, "-Lose" gray; a mark that does not fit ends the line */
    for (i = 0; !stop && i < p->ng + p->nk + p->nl; i++)
    {
        int color;
        char mark;
        int skill;

        if (i < p->ng)
            mark = '+', color = TEXT_COLOR_SYSTEM_GREEN, skill = p->gains[i];
        else if (i < p->ng + p->nk)
            mark = '=', color = TEXT_COLOR_SYSTEM_BLUE, skill = p->keeps[i - p->ng];
        else
            mark = '-', color = TEXT_COLOR_SYSTEM_GRAY, skill = p->loses[i - p->ng - p->nk];
        snprintf(buf, sizeof(buf), "%c%s", mark, PName(skill));
        if (x + 4 + GetStringTextLen(buf) > LINE_PX)
            stop = 1;
        else
            x = Put(t, x + 4, color, buf);
    }
    gains[0] = keeps[0] = loses[0] = 0;
    if (p->ng) Names(gains, sizeof(gains), "Gains ", p->gains, p->ng);
    if (p->nk) Names(keeps, sizeof(keeps), "Keeps ", p->keeps, p->nk);
    if (p->nl) Names(loses, sizeof(loses), "Loses ", p->loses, p->nl);
    if (p->ng + p->nk + p->nl == 0)
        snprintf(all, sizeof(all), "%s: no skills before or after.", cls);
    else
        snprintf(all, sizeof(all), "%s: %s%s%s", cls, gains, keeps, loses);
    Wrap(sHelp[sRows], sizeof(sHelp[0]), all);
    sRows++;
}

static void BuildHelp(void)
{
    int i;

#define ROW(k) (&sRowHb[(k) < sRows ? (k) : sRows - 1])
    for (i = 0; i < sRows; i++)
    {
        memset(&sRowHb[i], 0, sizeof(sRowHb[i]));
        sRowHb[i].adjUp = i > 0 ? &sRowHb[i - 1] : NULL;
        sRowHb[i].adjDown = i + 1 < sRows ? &sRowHb[i + 1] : NULL;
        sRowHb[i].adjLeft = i < 5 ? &sCharHb : &sClassHb; /* as page 0: Pow-Def to the name */
        sRowHb[i].xDisplay = 102;
        sRowHb[i].yDisplay = 24 + i * 16;
        sRowHb[i].msgId = MSG_BASE + i;
    }
    /* the left panel, as each retail page has its own copy (sHelpInfo_Char ...) */
    memset(&sCharHb, 0, sizeof(sCharHb));
    sCharHb.adjDown = &sClassHb;
    sCharHb.adjRight = ROW(3);
    sCharHb.xDisplay = 24;
    sCharHb.yDisplay = 80;
    sCharHb.populate = HbPopulate_SSCharacter;

    memset(&sClassHb, 0, sizeof(sClassHb));
    sClassHb.adjUp = &sCharHb;
    sClassHb.adjDown = &sLevelHb;
    sClassHb.adjRight = ROW(5);
    sClassHb.xDisplay = 6;
    sClassHb.yDisplay = 104;
    sClassHb.msgId = MSG_6E8;
    sClassHb.populate = HbPopulate_SSClass;

    memset(&sLevelHb, 0, sizeof(sLevelHb));
    sLevelHb.adjUp = &sClassHb;
    sLevelHb.adjDown = &sHpHb;
    sLevelHb.adjRight = &sExpHb;
    sLevelHb.xDisplay = 6;
    sLevelHb.yDisplay = 120;
    sLevelHb.msgId = MSG_542;

    memset(&sExpHb, 0, sizeof(sExpHb));
    sExpHb.adjUp = &sClassHb;
    sExpHb.adjDown = &sHpHb;
    sExpHb.adjLeft = &sLevelHb;
    sExpHb.adjRight = ROW(6);
    sExpHb.xDisplay = 38;
    sExpHb.yDisplay = 120;
    sExpHb.msgId = MSG_543;

    memset(&sHpHb, 0, sizeof(sHpHb));
    sHpHb.adjUp = &sLevelHb;
    sHpHb.adjRight = ROW(7);
    sHpHb.xDisplay = 6;
    sHpHb.yDisplay = 136;
    sHpHb.msgId = MSG_544;
#undef ROW
}

/* src/statscreen.c DisplayPage, lut slot 3: the page into gUiTmScratchA (cleared by DisplayPage). */
/* The Skills page was the last page drawn: its glyphs are still in the borrowed text chrs, and the
   game's pages draw their labels without clearing (glyphs are ORed in), so they are cleared
   before another page is drawn (Reforged_SkillPageWillDraw). */
static int sPageDrawn;

/* src/statscreen.c DisplayPage, before every page: after the Skills page, its borrowed chrs back to
   zero (as InitText left them), so the page drawn next looks as retail draws it. */
void Reforged_SkillPageWillDraw(int pageid)
{
    int i;

    if (!sPageDrawn || pageid == STATSCREEN_PAGE_MAX)
        return;
    for (i = 0; i < sLines; i++)
        ClearText(&sLine[i]);
    ClearText(&sTitle);
    sPageDrawn = 0;
}

void Reforged_SkillPageDisplay(void)
{
    sPageDrawn = 1;
    struct Unit *unit = gStatScreen.unit;
    unsigned char list[LIST_MAX];
    struct PromoLine promo[3];
    int n = 0, nc = 0, promoRows, skillRows, i;

    SetupLines();
    sRows = 0;
    if (sLines == 0 || unit == NULL)
        return;
    sNameJid = unit->pClassData != NULL ? unit->pClassData->number : 0; /* the unit's own names */
    n = Reforged_SkillUnitList(unit, list, LIST_MAX);
    nc = Choices(unit, promo);
    promoRows = nc ? 1 + nc : 0;
    if (promoRows > sLines - 1)
    {
        nc = sLines - 2 > 0 ? sLines - 2 : 0;
        promoRows = nc ? 1 + nc : 0;
    }
    skillRows = sLines - promoRows;

    /* the unit's skills: "Name  effect" a line each; when there are more than the lines, the
       last line names the rest ("Also: ..."; R lists their effects) */
    if (n == 0)
    {
        struct Text *t = NewRow();

        Put(t, 0, TEXT_COLOR_SYSTEM_GRAY, "No skills");
        snprintf(sHelp[sRows], sizeof(sHelp[0]), "No skills in this class yet.");
        sRows++;
    }
    else if (n <= skillRows)
    {
        for (i = 0; i < n; i++)
            SkillRow(list[i]);
    }
    else
    {
        for (i = 0; i < skillRows - 1; i++)
            SkillRow(list[i]);
        AlsoRow(list + skillRows - 1, n - (skillRows - 1));
    }

    /* "On promotion": each choice's "+Gains =Keeps -Loses" (design 3b) */
    if (nc > 0)
    {
        struct Text *t = NewRow();

        Put(t, 0, TEXT_COLOR_SYSTEM_GOLD, "On promotion");
        snprintf(sHelp[sRows], sizeof(sHelp[0]), "What each promotion does to the" "\x01"
            "skills: + gains, = keeps, - loses.");
        sRows++;
        for (i = 0; i < nc; i++)
            PromoRow(&promo[i]);
    }
    BuildHelp();
}

/* src/statscreen.c DisplayPage, after every page: the title "Skills" on BG0 over the name plate
   for page 3, taken away for the others (only if it was put there: no change without the page). */
void Reforged_SkillPageDrawn(int pageid)
{
    u16 *tm = gBG0TilemapBuffer + TILEMAP_INDEX(TITLE_X, 0);
    int i;

    if (pageid == STATSCREEN_PAGE_MAX)
    {
        static const char title[] = "Skills";
        int x = TITLE_CENTER - TITLE_X * 8 - GetStringTextLen(title) / 2;

        if (sLines == 0)
            return;
        if (x < 0)
            x = 0;
        ClearText(&sTitle);
        Text_InsertDrawString(&sTitle, x, TEXT_COLOR_SYSTEM_WHITE, title);
        PutText(&sTitle, tm);
        sTitleShown = 1;
        BG_EnableSyncByMask(BG0_SYNC_BIT);
    }
    else if (sTitleShown)
    {
        for (i = 0; i < TITLE_TILES; i++)
            tm[i] = tm[i + 0x20] = 0;
        sTitleShown = 0;
        BG_EnableSyncByMask(BG0_SYNC_BIT);
    }
}

/* src/statscreen.c StartStatScreenHelp, page 3: the help box to start at (the one R last left,
   if it is still one of this page's, else the first line). */
const struct HelpBoxInfo *Reforged_SkillPageHelp(const struct HelpBoxInfo *current)
{
    if (sRows <= 0)
    {
        /* never drawn (cannot happen on page 3): one line with an empty text */
        sRows = 1;
        sHelp[0][0] = 0;
        BuildHelp();
    }
    if (current != NULL && ((current >= sRowHb && current < sRowHb + sRows) || current == &sCharHb ||
        current == &sClassHb || current == &sLevelHb || current == &sExpHb || current == &sHpHb))
        return current;
    return &sRowHb[0];
}

/* Virtual text ids 0x7F54 + index (port/reforged/reforged_items.c Reforged_VirtualMsg routes
   0x7F54-0x7F5F here): the help of the page's line index. 1 when buf was filled. */
int Reforged_SkillPageMsg(int index, char *buf)
{
    if (index < 0 || index >= MSG_IDS)
        return 0;
    if (index < sRows)
        strcpy(buf, sHelp[index]);
    else
        buf[0] = 0; /* a reserved id: never past gMsgTable's end */
    return 1;
}

#endif /* PORT_PLAYED_BUILD */
