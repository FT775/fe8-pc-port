#include "global.h"

#include <string.h>

/*
 * Reforged Options, the Randomizer screen: opened by A on "Randomizer >" in the
 * options screen (reforged_menu.c). Played builds only; empty in every test build.
 * Its list code also draws the other list screens (reforged_listmenu.h:
 * StartReforgedListScreen with a table of rows; the Display and Option sets
 * screens, reforged_display_menu.c), each the same way as this one.
 * The randomizer itself is reforged_rando*.c (its interface: reforged_rando.h);
 * this screen only edits the settings (RFO_RANDO_* and the seed, stored by
 * reforged_settings.c). They are for the NEXT New Game: a new game copies them
 * into its run, and each save keeps its own run (the save mark's <blk>_rando),
 * so a change here never touches a game already started.
 *
 * A blocking child of the options screen's proc, as the Cheats screen is: it
 * fades out, draws its own frames and text on BG0/BG1 (the mural on BG3 is
 * started again for its palette), and on its way out (behind a fade out) has
 * the options screen draw itself again (Reforged_MenuRedraw), then fades in.
 *
 * The rows follow UFER's tabs: Randomizer and Seed, then the Stats, Classes,
 * Enemies and Items sections; a section's header row is skipped by the cursor.
 * The list is longer than the frame's 6 rows, so it scrolls with the cursor
 * (proc->top) as the options screen does, but it does not keep a Text per row:
 * 6 label and 6 value Texts are recycled, row top + i drawn into slot i, so the
 * screen's text tiles stay the same for any number of rows (below).
 *
 * Keys: Up/Down move; on a choice row Left/Right or A change it; on a number
 * row Left/Right step it (held: repeats); B goes back (and writes the settings
 * file if a value changed). Seed row: A edits it digit by digit (Left/Right
 * pick the digit, Up/Down change it, A or B end), Select rolls a new seed from
 * the host clock (Reforged_NewRandoSeed, never the game's RNG), L or R set it
 * to 0, shown "Random": a seed is then picked at New Game.
 */

#if PORT_PLAYED_BUILD

#include <stdio.h>

#include "proc.h"
#include "hardware.h"
#include "fontgrp.h"
#include "uiutils.h"
#include "bm.h"
#include "sysutil.h"
#include "soundwrapper.h"
#include "functions.h"
#include "constants/songs.h"

#include "port_reforged.h"
#include "reforged_listmenu.h"

enum {
    RND_ROWS_SHOWN = 5, /* the list frame holds 5 rows; more scroll (proc->top) */

    RND_LIST_TOP = 3,   /* tile row of the first entry shown */
    RND_LABEL_X = 4,    /* tile column of the labels */
    RND_LABEL_W = 13,   /* "Better enemy weapons" is 96 pixels */
    RND_VALUE_X = 17,   /* tile column of a row's value */
    RND_VALUE_W = 11,   /* to column 27, the frame's last inner column */
    RND_HEADER_X = 9,   /* a section header's Text (RND_LABEL_W wide, text centred) */
    RND_VALUE_GAP = 7,  /* pixels between two values shown side by side */
    RND_ARROW_GAP = 3,  /* pixels between an arrow and the value */
    /* Text tiles (InitText, from VRAM 0x6001000): title 18x2, two help lines
       24x2 each, 5 label slots 13x2 and 5 value slots 11x2: 372 tiles, up to
       0x6003E80, well below the BG0 map at 0x6006000 (640), whatever the number
       of rows. */
};

/* (row kinds, number formats, struct RandoRow and the RR_* macros: reforged_listmenu.h) */

struct ReforgedRandoProc {
    PROC_HEADER;
    /* 29 */ u8 cursor;
    /* 2A */ u8 dirty;
    /* 2B */ u8 top;     /* the first row shown */
    /* 2C */ u8 editing; /* 1 while the seed's digits are edited */
    /* 2D */ u8 digit;   /* the digit edited, 0 (the leftmost) to 7 */
    /* 30 */ const struct ReforgedListScreen *screen;
};

/* A help text is one or two lines ('\n' between), each at most 192 pixels (a
   help Text's 24 tiles), printable ASCII only; in plain words (no jargon), as
   the user asked. "UFER" gives the Universal FE Randomizer's own default. */
static const struct RandoRow sRandoRows[] = {
    RR_SWITCH(RFO_RANDO_ENABLE, "Randomizer", "Shuffles the game when you start New Game.\nGames already started never change."),
    { RR_SEED, 0, 0, 0, 0, 0, "Seed", "Same seed + same settings = same game.\nA: edit. Select: new seed. L/R: Random.", { NULL }, { 0 } },

    RR_HEAD("-- Stats --"),
    { RR_CHOICE, RFO_RANDO_GROWTHS, 4, 0, 0, 0, "Growths", "Level-up chances. Redistribute: similar\ntotal, new mix. Delta: nudge. Full: all new.",
      { "Off", "Redistribute", "Delta", "Full" }, { 0, 1, 2, 3 } },
    RR_NUM(RFO_RANDO_GROWTH_VAR, RN_PERCENT, 0, 5, "Growth variance", "Redistribute: how far the total moves.\nDelta: how far each chance moves."),
    RR_NUM(RFO_RANDO_GROWTH_MIN, RN_PERCENT, 0, 5, "Growth min", "No level-up chance goes below this,\nso nobody is hopeless. (UFER: 5%)"),
    RR_NUM(RFO_RANDO_GROWTH_MAX, RN_PERCENT, 0, 5, "Growth max", "No level-up chance goes above this,\nso nobody is too strong. (UFER: 80%)"),
    RR_SWITCH(RFO_RANDO_GROWTH_HP, "Adjust HP growths", "HP gets a bigger share of the level-up\nchances, so units aren't too fragile."),
    { RR_CHOICE, RFO_RANDO_BASES, 3, 0, 0, 0, "Bases", "Starting stats. Redistribute: similar\ntotal, new mix. Delta: nudge each a little.",
      { "Off", "Redistribute", "Delta" }, { 0, 1, 2 } },
    RR_NUM(RFO_RANDO_BASE_VAR, RN_PLAIN, 1, 1, "Base variance", "Redistribute: how far the total moves.\nDelta: how far each stat moves. (UFER: 5)"),
    RR_SWITCH(RFO_RANDO_MOV, "Class movement", "Each class walks 4 to 8 squares a turn.\nA promoted class never walks less."),
    RR_SWITCH(RFO_RANDO_CON, "Constitution", "Nudges Con (body size: who heavy weapons\nslow, who can carry whom). Never below 3."),
    RR_SWITCH(RFO_RANDO_AFFINITY, "Affinity", "Each unit gets a random support element\n(the bonus friends get standing close)."),

    RR_HEAD("-- Classes --"),
    RR_SWITCH(RFO_RANDO_CLASSES, "Player classes", "Your units get new jobs (a knight might\nbecome a mage). Promoted stay promoted."),
    RR_SWITCH(RFO_RANDO_LORDS, "Include lords", "Eirika and Ephraim get new jobs too.\nEirika keeps swords, Ephraim lances."),
    RR_SWITCH(RFO_RANDO_THIEVES, "Include thieves", "Colm and Rennac get new jobs and join\nRecruitment; others may become thieves."),
    RR_SWITCH(RFO_RANDO_SPECIAL, "Include dancer", "Tethys gets a new job (no more dancing)\nand joins Recruitment. Myrrh never changes."),
    RR_SWITCH(RFO_RANDO_MONSTERS, "Monster classes", "Your units may become monsters\n(skeletons, spiders, gargoyles and more)."),
    RR_SWITCH(RFO_RANDO_EVENLY, "Assign evenly", "Every job is used once before any repeats,\nso you don't get five archers."),
    RR_SWITCH(RFO_RANDO_GENDER, "Match gender", "Units get the male or female version of\na job to match them, where one exists."),
    { RR_CHOICE, RFO_RANDO_CLASS_GROWTHS, 2, 0, 0, 0, "Class growths", "Keep: units level up as they always did.\nTransfer: level-ups suit the new job.",
      { "Keep", "Transfer" }, { 0, 1 } },
    { RR_CHOICE, RFO_RANDO_CLASS_BASES, 2, 0, 0, 0, "Class bases", "Final: starting stats stay about the same.\nPersonal: own bonus plus the job's stats.",
      { "Final", "Personal" }, { 0, 1 } },
    RR_SWITCH(RFO_RANDO_PROMOTIONS, "Random promotions", "Each job promotes into three random jobs,\nmagic to magic (lords etc. keep theirs)."),
    { RR_CHOICE, RFO_RANDO_PROMO_MONSTERS, 2, 0, 0, 0, "Monster promos", "Separate: monsters and others promote\napart. Mixed: either can become either.",
      { "Separate", "Mixed" }, { 0, 1 } },
    RR_SWITCH(RFO_RANDO_FE7, "FE7/FE6 classes", "Rare FE7/FE6 jobs: 3rd promotion picks,\nsome units, late bosses. Needs the ROMs."),
    RR_SWITCH(RFO_RANDO_PALETTES, "Keep colours", "In battle scenes, units in a new job keep\ntheir own colours (monsters too)."),

    RR_HEAD("-- Recruitment --"),
    RR_SWITCH(RFO_RANDO_RECRUIT, "Recruitment", "Shuffles who joins and when. Lords and\nMyrrh stay; thieves/dancer if included."),
    { RR_CHOICE, RFO_RANDO_RECRUIT_CLASS, 2, 0, 0, 0, "Recruit class", "Own: they keep their own job.\nSlot: they take the job of who they replace.",
      { "Own", "Slot" }, { 0, 1 } },
    { RR_CHOICE, RFO_RANDO_RECRUIT_STATS, 2, 0, 0, 0, "Recruit stats", "Autolevel: own stats, scaled to when they\njoin. Slot: the stats of who they replace.",
      { "Autolevel", "Slot" }, { 0, 1 } },
    RR_SWITCH(RFO_RANDO_RECRUIT_GENDER, "Cross-gender", "A man may join in a woman's place and the\nreverse. (Text keeps he/she as written.)"),

    RR_HEAD("-- Enemies --"),
    RR_SWITCH(RFO_RANDO_BOSSES, "Boss classes", "Bosses get new jobs (never one that would\nbe stuck where they stand)."),
    RR_SWITCH(RFO_RANDO_ENEMIES, "Enemy classes", "Regular enemies get new jobs (never one\nstuck or slowed to a crawl by its terrain)."),
    { RR_CHOICE, RFO_RANDO_ENEMY_GROWTH, 3, 0, 0, 0, "Enemy growths", "Enemies level up stronger. Flat: same bonus\nfor all stats. Scaling: more for strong ones.",
      { "Off", "Flat", "Scaling" }, { 0, 1, 2 } },
    RR_NUM(RFO_RANDO_ENEMY_GROWTH_AMT, RN_PERCENT, 0, 5, "Enemy buff", "How big the Enemy growths bonus is.\n(UFER: 10)"),
    { RR_CHOICE, RFO_RANDO_BOSS_BUFF, 3, 0, 0, 0, "Boss stats", "Bosses gain stats later in the story.\nLinear: steady. Ease: slow, fast, then slow.",
      { "Off", "Linear", "Ease" }, { 0, 1, 2 } },
    RR_NUM(RFO_RANDO_BOSS_BUFF_AMT, RN_PLUS, 1, 1, "Boss max boost", "The most any one boss stat can gain.\n(UFER: 5)"),
    RR_NUM(RFO_RANDO_ENEMY_WEAPONS, RN_CHANCE, 0, 5, "Better enemy weapons", "Chance an enemy's weapons go up a rank\n(for example Iron to Steel)."),
    RR_SWITCH(RFO_RANDO_FLOOR_MONSTERS, "Tower & Ruins", "Tower, Ruins and skirmish monsters become\nothers of the same rank (no dragons)."),

    RR_HEAD("-- Items --"),
    RR_SWITCH(RFO_RANDO_W_MIGHT, "Weapon might", "Each weapon's damage moves by up to 3\n(kept between 0 and 23)."),
    RR_SWITCH(RFO_RANDO_W_HIT, "Weapon hit", "Each weapon's accuracy moves by up to 20\n(kept between 55 and 100)."),
    RR_SWITCH(RFO_RANDO_W_WEIGHT, "Weapon weight", "Each weapon's heaviness moves by up to 5\n(kept between 2 and 20)."),
    RR_SWITCH(RFO_RANDO_W_USES, "Weapon uses", "Each weapon's uses move by up to 20\n(15-60; long-range tomes may go lower)."),
    RR_NUM(RFO_RANDO_W_EFFECTS, RN_CHANCE, 0, 5, "Weapon traits", "Chance a weapon gets a bonus power: +5 to\na stat, hits twice, poison... (UFER: 25)"),
    RR_SWITCH(RFO_RANDO_REWARDS, "Rewards", "Chests, villages, houses and gifts give\ndifferent items, none twice in a chapter."),
    RR_NUM(RFO_RANDO_DROPS, RN_CHANCE, 0, 5, "Enemy drops", "Chance an enemy drops an item when\ndefeated. (UFER: 10)"),
    RR_SWITCH(RFO_RANDO_SHOPS, "Shops", "Shops and armories sell different things."),
    RR_SWITCH(RFO_RANDO_MASTER_SEALS, "Master Seals only", "Every promotion item is a Master Seal,\nwhich promotes any class."),

    RR_HEAD("-- Music --"),
    RR_SWITCH(RFO_RANDO_MUSIC, "Random music", "Each chapter gets random map themes and\nbattle themes from the game's own songs."),

    RR_HEAD("-- Challenge --"),
    RR_SWITCH(RFO_RANDO_NUZLOCKE, "Nuzlocke", "A unit who dies is gone for good, even if\nyou reset. A lord dying is still game over."),
};

static const struct ReforgedListScreen sRandoScreen = {
    "Reforged Options: Randomizer", sRandoRows, (int)(sizeof(sRandoRows) / sizeof(sRandoRows[0])),
    RFO_RANDO_ENABLE, 2, NULL,
};

static const char sSeedEditHelp[] = "Left/Right: digit. Up/Down: change. A: done.";

/* "FE7/FE6 classes" (v1.21.0; "FE7 classes" before) needs the player's own FE7 (USA) or FE6 ROM (reforged_fe7.c):
   with neither the row is shown grey (still editable, as the rows of a randomizer turned off) with this help
   instead, and New Game records it off (reforged_rando.c RomsAtNewGame, which records the ROMs loaded). */
static const char sFe7MissingHelp[] = "Needs your own FE7 or FE6 ROM (see the\nguide). None found: New Game skips it.";
int Rando_RomsLoaded(void); /* reforged_rando.c */
/* v1.20.0: "Myrrh look" (Helpers and the in-chapter screen) needs the player's own FE6 ROM (reforged_fe7.c): without
   it the row is grey (still editable: the value is kept for a run with the ROM) and its help says so; the look
   stays FE8's. */
static const char sFe6MissingHelp[] = "Needs your own FE6 ROM (see the\nguide). None found: FE8's look stays.";

/* The help shown instead of a row's own when the row needs a ROM that is not loaded, else NULL. */
static const char *RowMissingHelp(const struct RandoRow *r)
{
    if (r->kind == RR_CHOICE && r->option == RFO_RANDO_FE7 && Rando_RomsLoaded() == 0)
        return sFe7MissingHelp;
    if (r->kind == RR_CHOICE && r->option == RFO_MYRRH_LOOK && !Reforged_MyrrhLookAvailable())
        return sFe6MissingHelp;
    return NULL;
}

static int RowUnavailable(const struct RandoRow *r)
{
    return RowMissingHelp(r) != NULL;
}

static struct Text sRandoTitleText;
static struct Text sRandoHelpText[2]; /* the help box's two lines */
static struct Text sRandoLabelText[RND_ROWS_SHOWN]; /* slot i: row top + i */
static struct Text sRandoValueText[RND_ROWS_SHOWN];

/* The tile row of a list row, or -1 when it is scrolled out of the frame. */
static int RandoRowY(int row, int top)
{
    if (row < top || row >= top + RND_ROWS_SHOWN)
        return -1;
    return RND_LIST_TOP + (row - top) * 2;
}

/* The next row from `row` in direction dir (+1/-1) the cursor can stand on, or -1. */
static int RandoNextRow(const struct ReforgedListScreen *screen, int row, int dir)
{
    for (row += dir; row >= 0 && row < screen->count; row += dir)
        if (screen->rows[row].kind != RR_HEADER)
            return row;
    return -1;
}

/* "<" value ">" from x = 0: the arrows shown where Left/Right can go. */
static void DrawArrowValue(struct Text *t, const char *str, int canLeft, int canRight)
{
    int x = GetStringTextLen("<") + RND_ARROW_GAP;

    if (canLeft)
        Text_InsertDrawString(t, 0, TEXT_COLOR_SYSTEM_GRAY, "<");
    Text_InsertDrawString(t, x, TEXT_COLOR_SYSTEM_BLUE, str);
    x += GetStringTextLen(str) + RND_ARROW_GAP;
    if (canRight)
        Text_InsertDrawString(t, x, TEXT_COLOR_SYSTEM_GRAY, ">");
}

/* A choice row's position of the stored value (the last when it is none of them). */
static int ChoiceIndex(const struct RandoRow *r, int cur)
{
    int i;

    for (i = 0; i < r->count - 1 && r->order[i] != cur; i++)
        ;
    return i;
}

static void FormatNumber(const struct RandoRow *r, int v, char *buf, int size)
{
    if (r->format == RN_CHANCE && v == 0)
        snprintf(buf, size, "Off");
    else if (r->format == RN_SCALE)
        snprintf(buf, size, v == 0 ? "Auto" : "%dx", v);
    else if (r->format == RN_PERCENT || r->format == RN_CHANCE)
        snprintf(buf, size, "%d%%", v);
    else if (r->format == RN_PLUS)
        snprintf(buf, size, "+%d", v);
    else
        snprintf(buf, size, "%d", v);
}

/* A row's value into its slot's value Text (not put on the tilemap). */
static void DrawRandoValue(struct ReforgedRandoProc *proc, int row, struct Text *t)
{
    static const char hex[] = "0123456789ABCDEF";
    const struct RandoRow *r = &proc->screen->rows[row];
    char buf[12];
    int i, x, cur;

    ClearText(t);
    switch (r->kind)
    {
    case RR_CHOICE:
        cur = Reforged_GetOption(r->option);
        for (x = 0, i = 0; i < r->count; i++)
            x += GetStringTextLen(r->values[i]) + (i != 0 ? RND_VALUE_GAP : 0);
        if (x <= RND_VALUE_W * 8)
        {
            /* every value side by side, the stored one in blue (as the Cheats screen) */
            for (x = 0, i = 0; i < r->count; i++)
            {
                Text_InsertDrawString(t, x, r->order[i] == cur ? TEXT_COLOR_SYSTEM_BLUE : TEXT_COLOR_SYSTEM_GRAY,
                    r->values[i]);
                x += GetStringTextLen(r->values[i]) + RND_VALUE_GAP;
            }
        }
        else
        {
            /* too wide for the row: the stored value alone, between arrows */
            i = ChoiceIndex(r, cur);
            DrawArrowValue(t, r->values[i], i > 0, i < r->count - 1);
        }
        break;

    case RR_NUMBER:
        cur = Reforged_GetOption(r->option);
        FormatNumber(r, cur, buf, sizeof(buf));
        DrawArrowValue(t, buf, cur > r->min, cur < Reforged_OptionMax(r->option));
        break;

    case RR_ACTION:
        if (r->actionValue != NULL)
            Text_InsertDrawString(t, 0, TEXT_COLOR_SYSTEM_BLUE, r->actionValue(r->arg));
        break;

    case RR_SEED:
    {
        unsigned seed = Reforged_GetRandoSeed();

        if (seed == 0 && !proc->editing)
        {
            Text_InsertDrawString(t, 0, TEXT_COLOR_SYSTEM_BLUE, "Random");
            break;
        }
        /* one digit per 8 pixels (the font is proportional: 0-9 are 7 pixels,
           A-F 5), so the edited digit stays in place */
        for (i = 0; i < 8; i++)
        {
            int color = !proc->editing ? TEXT_COLOR_SYSTEM_BLUE
                      : i == proc->digit ? TEXT_COLOR_SYSTEM_GOLD : TEXT_COLOR_SYSTEM_WHITE;

            buf[0] = hex[(seed >> (28 - i * 4)) & 15];
            buf[1] = 0;
            Text_InsertDrawString(t, i * 8 + (8 - GetStringTextLen(buf)) / 2, color, buf);
        }
        break;
    }

    default:
        break;
    }
}

/* One row shown into its slot (row - top): label and value drawn and put. */
static void DrawRandoRow(struct ReforgedRandoProc *proc, int row)
{
    const struct ReforgedListScreen *screen = proc->screen;
    const struct RandoRow *r = &screen->rows[row];
    int y = RandoRowY(row, proc->top), slot = row - proc->top;
    struct Text *label, *value;

    if (y < 0)
        return;
    label = &sRandoLabelText[slot];
    value = &sRandoValueText[slot];

    ClearText(label);
    if (r->kind == RR_HEADER)
    {
        /* a section header: gold, centred over the list; no value */
        Text_InsertDrawString(label, (RND_LABEL_W * 8 - GetStringTextLen(r->label)) / 2, TEXT_COLOR_SYSTEM_GOLD,
            r->label);
        PutText(label, TILEMAP_LOCATED(gBG0TilemapBuffer, RND_HEADER_X, y));
    }
    else
    {
        /* (the Randomizer screen: with the randomizer off, the rows below Seed are
           shown grey, still editable) */
        int grey = (screen->greyOption >= 0 && row >= screen->greyFrom && !Reforged_GetOption(screen->greyOption)) ||
                   RowUnavailable(r);

        Text_InsertDrawString(label, 0, grey ? TEXT_COLOR_SYSTEM_GRAY : TEXT_COLOR_SYSTEM_WHITE, r->label);
        PutText(label, TILEMAP_LOCATED(gBG0TilemapBuffer, RND_LABEL_X, y));
        DrawRandoValue(proc, row, value);
        PutText(value, TILEMAP_LOCATED(gBG0TilemapBuffer, RND_VALUE_X, y));
    }
    BG_EnableSyncByMask(BG0_SYNC_BIT);
}

/* The rows shown from proc->top: the list's tilemap cleared and every slot drawn again. */
static void DrawRandoList(struct ReforgedRandoProc *proc)
{
    int row, x, y;

    for (y = RND_LIST_TOP; y < RND_LIST_TOP + RND_ROWS_SHOWN * 2; y++)
        for (x = 0; x < 30; x++)
            gBG0TilemapBuffer[TILEMAP_INDEX(x, y)] = 0;
    for (row = proc->top; row < proc->screen->count && row < proc->top + RND_ROWS_SHOWN; row++)
        DrawRandoRow(proc, row);
    BG_EnableSyncByMask(BG0_SYNC_BIT);
}

/* The help box (tile rows 14-19): line 1 at row 15, line 2 (after a '\n') at row 17. */
static void DrawRandoHelpText(const char *str)
{
    char line[64];
    const char *nl = str != NULL ? strchr(str, '\n') : NULL;
    int n;

    ClearText(&sRandoHelpText[0]);
    ClearText(&sRandoHelpText[1]);
    if (str != NULL)
    {
        n = nl != NULL ? (int)(nl - str) : (int)strlen(str);
        if (n > (int)sizeof(line) - 1)
            n = sizeof(line) - 1;
        memcpy(line, str, n);
        line[n] = '\0';
        Text_InsertDrawString(&sRandoHelpText[0], 0, TEXT_COLOR_SYSTEM_WHITE, line);
        if (nl != NULL)
            Text_InsertDrawString(&sRandoHelpText[1], 0, TEXT_COLOR_SYSTEM_WHITE, nl + 1);
    }
    PutText(&sRandoHelpText[0], TILEMAP_LOCATED(gBG0TilemapBuffer, 3, 15));
    PutText(&sRandoHelpText[1], TILEMAP_LOCATED(gBG0TilemapBuffer, 3, 17));
    BG_EnableSyncByMask(BG0_SYNC_BIT);
}

static void DrawRandoHelp(struct ReforgedRandoProc *proc, int row)
{
    const struct RandoRow *r = &proc->screen->rows[row];

    DrawRandoHelpText(RowUnavailable(r) ? RowMissingHelp(r) : r->help);
}

static void RandoMenu_Init(struct ReforgedRandoProc *proc)
{
    int i;

    /* as the Cheats screen: the options screen's set-up stays (backgrounds, BG
       priorities); its text and frames are replaced, and every graphic's palette
       is loaded again (the fade out left them black), the mural started again as
       the options screen's child */
    LoadObjUIGfx();
    LoadUiFrameGraphics();
    ResetTextFont();
    ResetText();
    InitSystemTextFont();
    EndMuralBackground();
    StartMuralBackground(proc->proc_parent, NULL, 0xe);

    BG_Fill(gBG0TilemapBuffer, 0);
    BG_Fill(gBG1TilemapBuffer, 0);

    proc->cursor = (u8)RandoNextRow(proc->screen, -1, +1);
    proc->dirty = 0;
    proc->top = 0;
    proc->editing = 0;
    proc->digit = 0;

    DrawUiFrame2(1, 2, 28, 12, 0); /* list: tile rows 2-13 */
    DrawUiFrame2(1, 14, 28, 6, 0); /* help: rows 14-19, two lines */

    InitText(&sRandoTitleText, 18);
    PutDrawText(&sRandoTitleText, TILEMAP_LOCATED(gBG0TilemapBuffer, 2, 0), TEXT_COLOR_SYSTEM_WHITE, 0, 18,
        proc->screen->title);

    for (i = 0; i < RND_ROWS_SHOWN; i++)
    {
        InitText(&sRandoLabelText[i], RND_LABEL_W);
        InitText(&sRandoValueText[i], RND_VALUE_W);
    }
    DrawRandoList(proc);

    InitText(&sRandoHelpText[0], 24);
    InitText(&sRandoHelpText[1], 24);
    DrawRandoHelp(proc, proc->cursor);

    /* BG3 too: the mural's tilemap (from the map, nothing else has put it on screen) */
    BG_EnableSyncByMask(BG0_SYNC_BIT | BG1_SYNC_BIT | BG3_SYNC_BIT);
}

/* The seed's digit edit: Left/Right pick the digit, Up/Down change it (wrapping
   0-F), A or B end it. It owns every key meanwhile, so the cursor stays. */
static void RandoMenu_EditSeed(struct ReforgedRandoProc *proc)
{
    u16 keys = gKeyStatusPtr->repeatedKeys;
    unsigned seed = Reforged_GetRandoSeed();
    int shift = 28 - proc->digit * 4, nibble = (seed >> shift) & 15;

    if (gKeyStatusPtr->newKeys & (A_BUTTON | B_BUTTON))
    {
        PlaySoundEffect(SONG_SE_SYS_WINDOW_CANSEL1);
        proc->editing = 0;
        DrawRandoHelp(proc, proc->cursor);
        DrawRandoRow(proc, proc->cursor);
        return;
    }
    if (keys & DPAD_LEFT)
    {
        if (proc->digit > 0)
        {
            proc->digit--;
            PlaySoundEffect(SONG_SE_SYS_CURSOR_LR1);
            DrawRandoRow(proc, proc->cursor);
        }
    }
    else if (keys & DPAD_RIGHT)
    {
        if (proc->digit < 7)
        {
            proc->digit++;
            PlaySoundEffect(SONG_SE_SYS_CURSOR_LR1);
            DrawRandoRow(proc, proc->cursor);
        }
    }
    else if (keys & (DPAD_UP | DPAD_DOWN))
    {
        nibble = (keys & DPAD_UP) ? (nibble + 1) & 15 : (nibble + 15) & 15;
        seed = (seed & ~(15u << shift)) | ((unsigned)nibble << shift);
        Reforged_SetRandoSeed(seed);
        proc->dirty = 1;
        PlaySoundEffect(SONG_SE_SYS_CURSOR_UD1);
        DrawRandoRow(proc, proc->cursor);
    }
}

/* Left/Right/A on a choice row, Left/Right on a number row: the value wanted, or -1. */
static int RandoWantedValue(const struct RandoRow *r)
{
    int cur = Reforged_GetOption(r->option), i, max;

    if (r->kind == RR_CHOICE)
    {
        i = ChoiceIndex(r, cur);
        if ((gKeyStatusPtr->newKeys & DPAD_LEFT) && i > 0)
            return r->order[i - 1];
        if ((gKeyStatusPtr->newKeys & DPAD_RIGHT) && i < r->count - 1)
            return r->order[i + 1];
        if (gKeyStatusPtr->newKeys & A_BUTTON)
            return r->order[(i + 1) % r->count];
        return -1;
    }
    if (r->kind == RR_NUMBER)
    {
        /* one step, to a multiple of the step (a value set in the file may be
           between two), kept in range */
        max = Reforged_OptionMax(r->option);
        if (gKeyStatusPtr->repeatedKeys & DPAD_LEFT)
        {
            i = cur % r->step != 0 ? cur - cur % r->step : cur - r->step;
            return i < r->min ? r->min : i;
        }
        if (gKeyStatusPtr->repeatedKeys & DPAD_RIGHT)
        {
            i = cur - cur % r->step + r->step;
            return i > max ? max : i;
        }
    }
    return -1;
}

static void RandoMenu_Loop(struct ReforgedRandoProc *proc)
{
    const struct ReforgedListScreen *screen = proc->screen;
    const struct RandoRow *r;
    int prev = proc->cursor, row, want, next;

    if (proc->editing)
    {
        RandoMenu_EditSeed(proc);
        DisplayUiHand(RND_LABEL_X * 8 - 8, RandoRowY(proc->cursor, proc->top) * 8);
        return;
    }

    if (gKeyStatusPtr->repeatedKeys & DPAD_UP)
    {
        next = RandoNextRow(screen, proc->cursor, -1);
        if (next >= 0)
            proc->cursor = next;
        else if (gKeyStatusPtr->newKeys & DPAD_UP)
            proc->cursor = RandoNextRow(screen, screen->count, -1);
    }
    else if (gKeyStatusPtr->repeatedKeys & DPAD_DOWN)
    {
        next = RandoNextRow(screen, proc->cursor, +1);
        if (next >= 0)
            proc->cursor = next;
        else if (gKeyStatusPtr->newKeys & DPAD_DOWN)
            proc->cursor = RandoNextRow(screen, -1, +1);
    }

    if (prev != proc->cursor)
    {
        int top = proc->top;

        PlaySoundEffect(SONG_SE_SYS_CURSOR_UD1);
        DrawRandoHelp(proc, proc->cursor);
        /* scroll so the cursor's row is shown (with its section header, going up) */
        if (proc->cursor < top)
        {
            top = proc->cursor;
            if (top > 0 && screen->rows[top - 1].kind == RR_HEADER)
                top--;
        }
        else if (proc->cursor >= top + RND_ROWS_SHOWN)
            top = proc->cursor - (RND_ROWS_SHOWN - 1);
        if (top != proc->top)
        {
            proc->top = top;
            DrawRandoList(proc);
        }
    }

    row = proc->cursor;
    r = &screen->rows[row];

    if (gKeyStatusPtr->newKeys & B_BUTTON)
    {
        PlaySoundEffect(SONG_SE_SYS_WINDOW_CANSEL1);
        if (proc->dirty)
            Reforged_SaveSettings();
        Proc_Break(proc);
        return;
    }

    if (r->kind == RR_SEED)
    {
        int changed = 0;

        if (gKeyStatusPtr->newKeys & A_BUTTON)
        {
            PlaySoundEffect(SONG_SE_SYS_WINDOW_SELECT1);
            proc->editing = 1;
            proc->digit = 0;
            DrawRandoHelpText(sSeedEditHelp);
            DrawRandoRow(proc, row);
        }
        else if (gKeyStatusPtr->newKeys & SELECT_BUTTON)
        {
            Reforged_SetRandoSeed(Reforged_NewRandoSeed()); /* the host clock, never the game's RNG */
            changed = 1;
        }
        else if ((gKeyStatusPtr->newKeys & (L_BUTTON | R_BUTTON)) && Reforged_GetRandoSeed() != 0)
        {
            Reforged_SetRandoSeed(0); /* "Random": picked at New Game */
            changed = 1;
        }
        if (changed)
        {
            proc->dirty = 1;
            PlaySoundEffect(SONG_SE_SYS_CURSOR_LR1);
            DrawRandoRow(proc, row);
        }
    }
    else if (r->kind == RR_ACTION)
    {
        if ((gKeyStatusPtr->newKeys & A_BUTTON) && r->action != NULL)
        {
            const char *msg = r->action(r->arg);

            PlaySoundEffect(SONG_SE_SYS_WINDOW_SELECT1);
            DrawRandoList(proc); /* an action may change any row (a loaded set) */
            DrawRandoHelpText(msg != NULL ? msg : r->help);
        }
    }
    else
    {
        want = RandoWantedValue(r);
        if (want >= 0 && want != Reforged_GetOption(r->option))
        {
            Reforged_SetOption(r->option, want);
            proc->dirty = 1;
            PlaySoundEffect(SONG_SE_SYS_CURSOR_LR1);
            if (r->option == screen->greyOption)
                DrawRandoList(proc); /* the other rows' grey labels */
            else
                DrawRandoRow(proc, row);
            if (screen->changed != NULL)
                screen->changed(r->option);
        }
    }

    DisplayUiHand(RND_LABEL_X * 8 - 8, RandoRowY(row, proc->top) * 8);
}

static void RandoMenu_End(struct ReforgedRandoProc *proc)
{
    if (proc->screen->ended != NULL)
        proc->screen->ended(proc->proc_parent); /* the map's screen (reforged_field_menu.c) */
    else
        Reforged_MenuRedraw(proc->proc_parent);
}

static const struct ProcCmd sProcScr_ReforgedRandomizer[] = {
    PROC_NAME("PC_reforged_list"),

    PROC_CALL_ARG(NewFadeOut, 8),
    PROC_WHILE(FadeOutExists),

    PROC_CALL(RandoMenu_Init),

    PROC_CALL_ARG(NewFadeIn, 8),
    PROC_WHILE(FadeInExists),

    PROC_REPEAT(RandoMenu_Loop),

    PROC_CALL_ARG(NewFadeOut, 8),
    PROC_WHILE(FadeOutExists),

    PROC_CALL(RandoMenu_End),

    PROC_CALL_ARG(NewFadeIn, 8),
    PROC_WHILE(FadeInExists),

    PROC_END,
};

void *StartReforgedListScreen(void *parent, const struct ReforgedListScreen *screen)
{
    struct ReforgedRandoProc *proc = Proc_StartBlocking(sProcScr_ReforgedRandomizer, parent);

    /* (the script's first use of the screen, RandoMenu_Init, is after the fade's
       PROC_WHILE, so after this has run) */
    proc->screen = screen;
    return proc;
}

void *StartReforgedRandomizerScreen(void *parent)
{
    return StartReforgedListScreen(parent, &sRandoScreen);
}

#endif /* PORT_PLAYED_BUILD */
