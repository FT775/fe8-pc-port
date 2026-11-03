/*
 * Reforged Options settings file (port-plan/reforged-options-plan.md section 3):
 * "reforged-options.txt" in Platform_UserDir (%APPDATA%\fe8pc\ on Windows,
 * $XDG_DATA_HOME/fe8pc/ or ~/.local/share/fe8pc/ on Linux), never in SRAM, so
 * the .sav stays the raw 32 KiB image mGBA reads.
 *
 * Plain text, one "key = value" per line, so players and modders can edit it:
 *     # comment
 *     danger_zone_select = 0
 * A value is 1/on/yes/true or 0/off/no/false (any case). Unknown keys, bad
 * values and lines without "=" are skipped (logged); an option the file does
 * not set keeps its default. A missing or unreadable file means every option
 * at its default: OFF, but fast-forward (ON, 4x, muted; sDefaults below) and
 * the original-game bug fixes (ON).
 *
 * "fast_forward = 0" counts only in a file written by a build that has
 * fast-forward (it writes "format = 2" and fast_forward_speed; either one
 * will do). The foundation build wrote every key, fast_forward = 0 among
 * them, whenever any option changed, while that row did nothing, so such a
 * file would turn the default-ON feature off with no choice of the player's
 * (rfo-ff refute bug 2): there it is ignored (logged), and the next write
 * of the file keeps fast-forward on.
 *
 * The Randomizer screen's keys (randomizer, randomizer_growths, ...) are
 * numbers in their own ranges (Reforged_OptionMax; a mode also takes its
 * name, a switch on/off), and the seed is a u32 of its own, randomizer_seed
 * (0x hex, exactly 8 hex digits as the screen shows it, or else decimal,
 * never octal; written back as 0x%08X; 0 = picked at New Game; ParseSeed),
 * outside the option table. They are the settings for the NEXT new game, so
 * they never mark a save here: each save keeps its own run's copy (the save
 * mark's <blk>_rando).
 *
 * Loaded once at start (src/main.c AgbMain, played builds only); written only
 * when the options screen changed a value, atomically as the save is
 * (savefile_pc.c): PATH.tmp.<pid>, then a rename over PATH
 * (Platform_SaveFileAtomic). The file is not made until a value is changed.
 *
 * Played builds only, like the rest of port/reforged: this file is compiled
 * with the game's flags (port/Makefile's port/reforged rule), so
 * PORT_PLAYED_BUILD is 0 in every test build, the Gate 0 binary included
 * (FE8PC_DEBUG, which port/test/pc/gate0.mk gives the game code only), and
 * the object is empty there. It used to sit in port/save, built with the
 * port layer's flags, which never see FE8PC_DEBUG: a test binary then carried
 * sValues, which savestate.py does not know, and refused every snapshot resume
 * (review rv-off-equals-gba F1). Every caller is played-build only.
 */

#if PORT_PLAYED_BUILD

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "port_hw.h"
#include "port_reforged.h"
#include "port_controls.h"
#include "../platform.h"

#define RFO_FILE_NAME "reforged-options.txt"
#define RFO_MAX_FILE  (64 * 1024)

static const char *const sKeys[RFO_COUNT] = {
    [RFO_DANGER_ZONE_SELECT] = "danger_zone_select",
    [RFO_FAST_FORWARD] = "fast_forward",
    [RFO_UNBREAKABLE] = "unbreakable_weapons",
    [RFO_CASUAL_MODE] = "casual_mode",
    /* fast-forward block */
    [RFO_FF_SPEED] = "fast_forward_speed",
    [RFO_FF_SOUND] = "fast_forward_sound",
    /* growth-rates block */
    [RFO_GROWTH_SELECT] = "growth_rates_select",
    /* original-game bug fixes block: 1 on (the default), 0 off */
    [RFO_BUG_FIXES] = "original_game_bug_fixes",
    /* map HP bars block (reforged_hpbars.c): 0 off, 1 bars, 2 warnings, 3 both
       (port-rfo-int6) */
    [RFO_MAP_HPBARS] = "map_hp_bars_warnings",
    /* Display submenu (reforged_display_menu.c) */
    [RFO_WINDOW_SCALE] = "window_size",
    [RFO_SCALE_MODE] = "scaling",
    [RFO_SCALE_SMOOTH] = "smoothing",
    [RFO_FULLSCREEN] = "fullscreen",
    /* --- Game fixes submenu (reforged_gamefixes.c): 0 off (the default), 1 on --- */
    [RFO_FIX_EARLY_EXP] = "fix_early_kill_exp",
    [RFO_FIX_PROMO_LEVELS] = "fix_promoted_enemy_levels",
    [RFO_FIX_AI_HEAL] = "fix_ai_heal_full_hp",
    [RFO_FIX_CANTO] = "fix_canto_after_trade",
    [RFO_FIX_DEVIL] = "fix_devil_high_luck",
    [RFO_FIX_CH15_DROP] = "fix_ch15_staff_drop",
    /* --- end of the Game fixes block --- */
    /* --- Cheats submenu (reforged_cheats.c) --- */
    [RFO_CHEAT_GOLD] = "cheat_infinite_gold",
    [RFO_CHEAT_GROWTHS] = "cheat_growths",
    [RFO_CHEAT_EXP] = "cheat_exp",
    /* --- end of the Cheats block --- */
    /* --- Randomizer submenu (reforged_randomizer_menu.c; the next new game's settings) --- */
    [RFO_RANDO_ENABLE] = "randomizer",
    [RFO_RANDO_GROWTHS] = "randomizer_growths",
    [RFO_RANDO_GROWTH_VAR] = "randomizer_growth_variance",
    [RFO_RANDO_GROWTH_MIN] = "randomizer_growth_min",
    [RFO_RANDO_GROWTH_MAX] = "randomizer_growth_max",
    [RFO_RANDO_GROWTH_HP] = "randomizer_adjust_hp_growths",
    [RFO_RANDO_BASES] = "randomizer_bases",
    [RFO_RANDO_BASE_VAR] = "randomizer_base_variance",
    [RFO_RANDO_MOV] = "randomizer_class_movement",
    [RFO_RANDO_CON] = "randomizer_constitution",
    [RFO_RANDO_AFFINITY] = "randomizer_affinity",
    [RFO_RANDO_CLASSES] = "randomizer_player_classes",
    [RFO_RANDO_LORDS] = "randomizer_include_lords",
    [RFO_RANDO_THIEVES] = "randomizer_include_thieves",
    [RFO_RANDO_SPECIAL] = "randomizer_include_dancer",
    [RFO_RANDO_MONSTERS] = "randomizer_monster_classes",
    [RFO_RANDO_EVENLY] = "randomizer_assign_evenly",
    [RFO_RANDO_GENDER] = "randomizer_match_gender",
    [RFO_RANDO_CLASS_GROWTHS] = "randomizer_class_growths",
    [RFO_RANDO_CLASS_BASES] = "randomizer_class_bases",
    [RFO_RANDO_PALETTES] = "randomizer_keep_colours",
    [RFO_RANDO_RECRUIT] = "randomizer_recruitment",
    [RFO_RANDO_RECRUIT_CLASS] = "randomizer_recruit_class",
    [RFO_RANDO_RECRUIT_STATS] = "randomizer_recruit_stats",
    [RFO_RANDO_RECRUIT_GENDER] = "randomizer_recruit_cross_gender",
    [RFO_RANDO_BOSSES] = "randomizer_boss_classes",
    [RFO_RANDO_ENEMIES] = "randomizer_enemy_classes",
    [RFO_RANDO_ENEMY_GROWTH] = "randomizer_enemy_growths",
    [RFO_RANDO_ENEMY_GROWTH_AMT] = "randomizer_enemy_buff",
    [RFO_RANDO_BOSS_BUFF] = "randomizer_boss_stats",
    [RFO_RANDO_BOSS_BUFF_AMT] = "randomizer_boss_max_boost",
    [RFO_RANDO_ENEMY_WEAPONS] = "randomizer_enemy_weapons",
    [RFO_RANDO_W_MIGHT] = "randomizer_weapon_might",
    [RFO_RANDO_W_HIT] = "randomizer_weapon_hit",
    [RFO_RANDO_W_WEIGHT] = "randomizer_weapon_weight",
    [RFO_RANDO_W_USES] = "randomizer_weapon_uses",
    [RFO_RANDO_W_EFFECTS] = "randomizer_weapon_traits",
    [RFO_RANDO_REWARDS] = "randomizer_rewards",
    [RFO_RANDO_DROPS] = "randomizer_enemy_drops",
    [RFO_RANDO_SHOPS] = "randomizer_shops",
    [RFO_RANDO_NUZLOCKE] = "randomizer_nuzlocke",
    /* --- end of the Randomizer block --- */
};

/* The seed's key: a u32 kept apart from sValues (a u8 each). */
#define RFO_SEED_KEY "randomizer_seed"

/* Each option's default: 0 (OFF) but the fast-forward block's (fast-forward
   ON at 4x, sound muted; DECISIONS.md 2026-10-28), the original-game bug
   fixes (ON; DECISIONS.md 2026-10-30, U10) and the Randomizer's amounts (UFER's
   defaults, port_reforged.h; they act only with the randomizer and their mode on).
   A missing file or key, or a bad value, gives the default. */
#define RFO_DEFAULTS { [RFO_FAST_FORWARD] = 1, [RFO_FF_SPEED] = 4, [RFO_BUG_FIXES] = 1, \
    [RFO_RANDO_GROWTH_VAR] = 30, [RFO_RANDO_GROWTH_MIN] = 5, [RFO_RANDO_GROWTH_MAX] = 80, \
    [RFO_RANDO_BASE_VAR] = 5, [RFO_RANDO_GENDER] = 1, [RFO_RANDO_PALETTES] = 1, [RFO_RANDO_ENEMY_GROWTH_AMT] = 10, \
    [RFO_RANDO_BOSS_BUFF_AMT] = 5 }
static const unsigned char sDefaults[RFO_COUNT] = RFO_DEFAULTS;
static unsigned char sValues[RFO_COUNT] = RFO_DEFAULTS;

int Reforged_OptionDefault(int id)
{
    if (id < 0 || id >= RFO_COUNT)
        return 0;
    return sDefaults[id];
}

int Reforged_GetOption(int id)
{
    if (id < 0 || id >= RFO_COUNT)
        return 0;
    return sValues[id];
}

/* fast-forward block: the speed is one of 2, 3, 4, 8. */
static int FfValueOk(int id, int value)
{
    return id != RFO_FF_SPEED || value == 2 || value == 3 || value == 4 || value == 8;
}

/* The smallest value of an option: 0 but the Randomizer's "Base variance" and
   "Boss max boost", 1 (UFER's ranges). */
static int OptionMin(int id)
{
    return id == RFO_RANDO_BASE_VAR || id == RFO_RANDO_BOSS_BUFF_AMT ? 1 : 0;
}

void Reforged_SetOption(int id, int value)
{
    if (id >= 0 && id < RFO_COUNT)
        sValues[id] = (value >= OptionMin(id) && value <= Reforged_OptionMax(id) && FfValueOk(id, value))
                    ? (unsigned char)value : sDefaults[id];
}

/* The largest value of an option: 1 for a switch; 2 for "Unbreakable
   weapons" (0 off, 1 your units, 2 everyone; the user's choice of 2026-10-29);
   8 for "Fast-forward speed" (2, 3, 4 or 8, the multiplier itself); 3 for
   "HP bars & warnings" (off, bars, warnings, both). */
int Reforged_OptionMax(int id)
{
    if (id == RFO_FF_SPEED)
        return 8;
    if (id == RFO_MAP_HPBARS) /* map HP bars block */
        return RFO_HPBARS_BOTH;
    if (id == RFO_WINDOW_SCALE) /* Display submenu: auto, 1x-8x; pixel-perfect, fill, stretch */
        return 8;
    if (id == RFO_SCALE_MODE)
        return 2;
    /* --- Cheats submenu: growths 0 off, 1 fixed, 2 +25%, 3 +50%; EXP 0 x1, 1 x2, 2 x3 --- */
    if (id == RFO_CHEAT_GROWTHS)
        return 3;
    if (id == RFO_CHEAT_EXP)
        return 2;
    /* --- end of the Cheats block --- */
    /* --- Randomizer submenu: modes, then the amounts (port_reforged.h); the rest are switches --- */
    switch (id)
    {
    case RFO_RANDO_GROWTHS:          /* off, redistribute, delta, full */
        return 3;
    case RFO_RANDO_BASES:            /* off, redistribute, delta */
    case RFO_RANDO_ENEMY_GROWTH:     /* off, flat, scaling */
    case RFO_RANDO_BOSS_BUFF:        /* off, linear, ease */
        return 2;
    case RFO_RANDO_GROWTH_VAR:
    case RFO_RANDO_GROWTH_MIN:
    case RFO_RANDO_ENEMY_GROWTH_AMT:
    case RFO_RANDO_ENEMY_WEAPONS:    /* the chances, in % */
    case RFO_RANDO_W_EFFECTS:
    case RFO_RANDO_DROPS:
        return 100;
    case RFO_RANDO_GROWTH_MAX:
        return 255;
    case RFO_RANDO_BASE_VAR:
        return 10;
    case RFO_RANDO_BOSS_BUFF_AMT:
        return 20;
    }
    /* --- end of the Randomizer block --- */
    return id == RFO_UNBREAKABLE ? 2 : 1;
}

/* --- Randomizer submenu (reforged_randomizer_menu.c) --- */
static int ParseBool(const char *v);

/* A mode's names, by value (NULL for an id that is not a mode). */
static const char *const *RandoModeNames(int id)
{
    static const char *const growths[] = { "off", "redistribute", "delta", "full" };
    static const char *const bases[] = { "off", "redistribute", "delta" };
    static const char *const classGrowths[] = { "keep", "transfer" };
    static const char *const classBases[] = { "final", "personal" };
    static const char *const recruitClass[] = { "own", "slot" };
    static const char *const recruitStats[] = { "autolevel", "slot" };
    static const char *const enemyGrowth[] = { "off", "flat", "scaling" };
    static const char *const bossBuff[] = { "off", "linear", "ease" };

    switch (id)
    {
    case RFO_RANDO_GROWTHS:
        return growths;
    case RFO_RANDO_BASES:
        return bases;
    case RFO_RANDO_CLASS_GROWTHS:
        return classGrowths;
    case RFO_RANDO_CLASS_BASES:
        return classBases;
    case RFO_RANDO_RECRUIT_CLASS:
        return recruitClass;
    case RFO_RANDO_RECRUIT_STATS:
        return recruitStats;
    case RFO_RANDO_ENEMY_GROWTH:
        return enemyGrowth;
    case RFO_RANDO_BOSS_BUFF:
        return bossBuff;
    }
    return NULL;
}

/* 1 for a Randomizer amount (a number, not a switch or a mode). */
static int RandoIsAmount(int id)
{
    return id >= RFO_RANDO_FIRST && id <= RFO_RANDO_LAST && RandoModeNames(id) == NULL && Reforged_OptionMax(id) > 1;
}

/* A Randomizer value from its number (in range; an amount may end in "%"), a
   mode's name, a switch's on/off, or "off" for a chance; -1 when bad. */
static int ParseRandoValue(int id, const char *v)
{
    const char *const *names = RandoModeNames(id);
    char *end;
    long n;
    int i;

    errno = 0;
    n = strtol(v, &end, 10);
    if (end != v && RandoIsAmount(id) && *end == '%')
        end++;
    if (end != v && *end == 0 && errno == 0)
        return n >= OptionMin(id) && n <= Reforged_OptionMax(id) ? (int)n : -1;
    if (names != NULL)
    {
        for (i = 0; i <= Reforged_OptionMax(id); i++)
            if (strcasecmp(v, names[i]) == 0)
                return i;
        if (id == RFO_RANDO_GROWTHS && strcasecmp(v, "full_random") == 0)
            return 3;
        if (id == RFO_RANDO_BOSS_BUFF && strcasecmp(v, "ease_in_out") == 0)
            return 2;
        return -1;
    }
    if (!RandoIsAmount(id))
        return ParseBool(v);
    if (OptionMin(id) == 0 && strcasecmp(v, "off") == 0)
        return 0;
    return -1;
}

static unsigned sRandoSeed; /* randomizer_seed; 0 = picked at New Game */

unsigned Reforged_GetRandoSeed(void)
{
    return sRandoSeed;
}

void Reforged_SetRandoSeed(unsigned seed)
{
    sRandoSeed = seed & 0xFFFFFFFFu;
}

/* The finaliser of splitmix64: every input bit reaches every output bit. */
static unsigned long long SeedMix(unsigned long long z)
{
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/* A new seed from the host: the monotonic clock (ns), the wall clock, the
   process id and a call count, mixed. Never the game's RNG (its state is in
   the suspend save and drives battles), never 0 (0 means "pick one"). */
unsigned Reforged_NewRandoSeed(void)
{
    static unsigned sCalls;
    unsigned long long z;
    unsigned seed;

    z = (unsigned long long)Platform_NowNs();
    z = SeedMix(z ^ ((unsigned long long)time(NULL) << 20));
    z = SeedMix(z ^ ((unsigned long long)getpid() << 32) ^ ++sCalls);
    seed = (unsigned)(z ^ (z >> 32));
    if (seed == 0)
        seed = 0x9E3779B9u;
    return seed;
}

/* A hex digit's value, or -1. */
static int SeedHexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* randomizer_seed's value: "0x"/"0X" and hex digits; exactly 8 hex digits
   (what the Randomizer screen shows, "01234567" or "00C0FFEE") as hex too;
   anything else as decimal, leading zeros allowed. Never octal: strtoull's
   base 0 read "01234567" (the screen's own digits) as octal, a silently
   different seed (review finding 9). No sign, no spaces, no junk, at most
   0xFFFFFFFF: 1 and *out, else 0. */
static int ParseSeed(const char *v, unsigned *out)
{
    unsigned long long seed = 0;
    int base = 10, i, d;

    if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X'))
    {
        base = 16;
        v += 2;
    }
    else
    {
        for (i = 0; i < 8 && SeedHexDigit(v[i]) >= 0; i++)
            ;
        if (i == 8 && v[8] == 0)
            base = 16;
    }
    if (*v == 0)
        return 0;
    for (; *v != 0; v++)
    {
        d = base == 16 ? SeedHexDigit(*v) : (*v >= '0' && *v <= '9' ? *v - '0' : -1);
        if (d < 0)
            return 0;
        seed = seed * base + d;
        if (seed > 0xFFFFFFFFull)
            return 0;
    }
    *out = (unsigned)seed;
    return 1;
}
/* --- end of the Randomizer block --- */

/* --- Cheats submenu (port-rfo-cheats) --- */
/* (The cheats are gameplay options: the saves-marked block's table below,
   where every option not listed as display-only is gameplay.) */

/* A cheat's value from its number (0 to Reforged_OptionMax) or its name
   ("fixed", "+25%", "x2", ...), or -1. */
static int ParseCheatValue(int id, const char *v)
{
    static const char *const growths[] = { "off", "fixed", "+25%", "+50%" };
    static const char *const growthsAlt[] = { "no", "on", "+25", "+50" };
    static const char *const exp[] = { "x1", "x2", "x3" };
    int i;

    for (i = 0; i <= Reforged_OptionMax(id); i++)
    {
        char num[2] = { (char)('0' + i), 0 };

        if (strcmp(v, num) == 0
            || (id == RFO_CHEAT_GROWTHS && (strcasecmp(v, growths[i]) == 0 || strcasecmp(v, growthsAlt[i]) == 0))
            || (id == RFO_CHEAT_EXP && strcasecmp(v, exp[i]) == 0))
            return i;
    }
    return -1;
}
/* --- end of the Cheats block --- */

const char *Reforged_OptionKey(int id)
{
    if (id < 0 || id >= RFO_COUNT)
        return NULL;
    return sKeys[id];
}

/* PATH of the settings file, or 0 when there is no per-user folder. */
static int SettingsPath(char *out, size_t size)
{
    const char *dir = Platform_UserDir();

    if (dir == NULL)
        return 0;
    return snprintf(out, size, "%s%s", dir, RFO_FILE_NAME) < (int)size;
}

static char *Trim(char *s)
{
    char *end;

    while (*s == ' ' || *s == '\t')
        s++;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        *--end = 0;
    return s;
}

/* 1 or 0, or -1 for a bad value. */
static int ParseBool(const char *v)
{
    static const char *const on[] = { "1", "on", "yes", "true" };
    static const char *const off[] = { "0", "off", "no", "false" };
    unsigned i;

    for (i = 0; i < sizeof(on) / sizeof(on[0]); i++)
        if (strcasecmp(v, on[i]) == 0)
            return 1;
    for (i = 0; i < sizeof(off) / sizeof(off[0]); i++)
        if (strcasecmp(v, off[i]) == 0)
            return 0;
    return -1;
}

/* A value's name for the log: off/on, and off/yours/everyone for
   unbreakable_weapons (the screen's Off/Yours/All). */
static const char *ValueName(int id, int value)
{
    static const char *const speeds[] = { "?", "?", "2x", "3x", "4x", "?", "?", "?", "8x" };

    /* --- Cheats submenu --- */
    static const char *const growths[] = { "off", "fixed", "+25%", "+50%" };
    static const char *const exp[] = { "x1", "x2", "x3" };

    if (id == RFO_CHEAT_GROWTHS && value >= 0 && value <= 3)
        return growths[value];
    if (id == RFO_CHEAT_EXP && value >= 0 && value <= 2)
        return exp[value];
    /* --- end of the Cheats block --- */
    /* --- Randomizer submenu: a mode's name, an amount's number --- */
    if (RandoModeNames(id) != NULL)
        return value >= 0 && value <= Reforged_OptionMax(id) ? RandoModeNames(id)[value] : "?";
    if (RandoIsAmount(id))
    {
        static char num[2][8];
        static int which;

        which ^= 1;
        snprintf(num[which], sizeof(num[which]), "%d", value);
        return num[which];
    }
    /* --- end of the Randomizer block --- */
    if (id == RFO_UNBREAKABLE)
        return value == 2 ? "everyone" : value == 1 ? "yours" : "off";
    if (id == RFO_FF_SPEED) /* fast-forward block */
        return value >= 0 && value <= 8 ? speeds[value] : "?";
    if (id == RFO_FF_SOUND)
        return value ? "sped up" : "mute";
    if (id == RFO_MAP_HPBARS) /* map HP bars block */
        return value == RFO_HPBARS_BOTH ? "both" : value == RFO_HPBARS_WARNINGS ? "warnings"
             : value == RFO_HPBARS_BARS ? "bars" : "off";
    if (id == RFO_WINDOW_SCALE) /* Display submenu */
    {
        static const char *const sizes[] = { "auto", "1x", "2x", "3x", "4x", "5x", "6x", "7x", "8x" };

        return value >= 0 && value <= 8 ? sizes[value] : "?";
    }
    if (id == RFO_SCALE_MODE)
        return value == 2 ? "stretch" : value == 1 ? "fill" : "pixel";
    return value ? "on" : "off";
}

/* --- Controls (port/include/port_controls.h): control_<action> = key | key 2 | pad | pad 2,
   each a binding name or "none"; a field left out keeps its default ("control_a = C" only
   changes the first key). A name the platform does not know keeps that slot's default
   (logged); names are kept in the platform's spelling. Controls_Tidy runs after the
   file (duplicates, essential actions). 1 when key is a control key. --- */
static int ParseControlLine(const char *key, char *val, const char *path, int lineNo)
{
    int a, s;
    char *p = val;

    for (a = 0; a < CTL_COUNT; a++)
        if (strcmp(key, Controls_SettingsKey(a)) == 0)
            break;
    if (a == CTL_COUNT)
        return 0;
    for (s = 0; s < CTL_SLOTS && p != NULL; s++)
    {
        char *bar = strchr(p, '|');
        const char *name;
        char canon[CTL_NAME_MAX];

        if (bar != NULL)
            *bar = 0;
        name = Trim(p);
        p = bar != NULL ? bar + 1 : NULL;
        if (strcasecmp(name, "none") == 0)
            name = "";
        if (name[0] != 0 && (strlen(name) >= CTL_NAME_MAX || !Platform_BindingValid(CTL_SLOT_IS_PAD(s), name)))
        {
            PortLog("[reforged] %s line %d: %s: '%s' is not a %s: %s kept\n", path, lineNo, key, name,
                CTL_SLOT_IS_PAD(s) ? "gamepad button" : "key",
                Controls_Default(a, s)[0] ? Controls_Default(a, s) : "none");
            continue;
        }
        Platform_CanonicalName(CTL_SLOT_IS_PAD(s), name, canon, sizeof(canon));
        Controls_Load(a, s, canon);
    }
    return 1;
}

/* Where a settings line comes from: the settings file, an option set's file
   (reforged_display_menu.c: an option set leaves out the controls and the
   Display options, which belong to the machine, not the run), or the command
   line (port/hw/main_pc.c --set and its short forms). */
enum { RS_FILE, RS_SET, RS_CLI };

static int IsDisplaySubmenu(int id)
{
    return id == RFO_WINDOW_SCALE || id == RFO_SCALE_MODE || id == RFO_SCALE_SMOOTH || id == RFO_FULLSCREEN;
}

/* One "key = value" (both trimmed) into the options; path and lineNo name it in the
   log. ffLine / ffBuild: the fast-forward block's check of an older file. */
static void ApplySettingLine(const char *key, char *val, const char *path, int lineNo, int scope, int *ffLine,
    int *ffBuild)
{
    int id, v;

    if (strcmp(key, "format") == 0)
    {
        *ffBuild |= atoi(val) >= 2; /* written by a build with fast-forward */
        return;
    }
    /* --- Randomizer submenu: the seed, a u32 (0x hex, 8 hex digits, or
       decimal; ParseSeed) --- */
    if (strcmp(key, RFO_SEED_KEY) == 0)
    {
        unsigned seed;

        if (!ParseSeed(val, &seed))
        {
            PortLog("[reforged] %s line %d: bad value '%s' for %s: 0 (Random)\n", path, lineNo, val, key);
            seed = 0;
        }
        sRandoSeed = seed;
        return;
    }
    /* --- end of the Randomizer block --- */
    if (scope == RS_SET && strncmp(key, "control_", 8) == 0)
        return; /* (an option set never changes the keys) */
    if (ParseControlLine(key, val, path, lineNo))
        return;
    for (id = 0; id < RFO_COUNT; id++)
        if (strcmp(key, sKeys[id]) == 0)
            break;
    if (id == RFO_COUNT)
    {
        PortLog("[reforged] %s line %d: unknown key '%s': skipped\n", path, lineNo, key);
        return;
    }
    if (scope == RS_SET && IsDisplaySubmenu(id))
        return; /* (nor the window) */
    v = ParseBool(val);
    /* unbreakable_weapons: 2/everyone/all also; "on" (and 1) reads as
       "your units", the safer meaning of an older file's "on" */
    if (id == RFO_UNBREAKABLE && v < 0)
    {
        if (strcmp(val, "2") == 0 || strcasecmp(val, "everyone") == 0 || strcasecmp(val, "all") == 0)
            v = 2;
        else if (strcasecmp(val, "your_units") == 0 || strcasecmp(val, "yours") == 0)
            v = 1;
    }
    /* fast-forward block: the speed as 2/3/4/8 (or 2x ...); the sound as
       mute/sped_up (or 0/1, off/on) */
    if (id == RFO_FF_SPEED)
    {
        *ffBuild = 1; /* only a build with fast-forward writes this key */
        v = atoi(val);
        if (!FfValueOk(id, v))
            v = -1;
    }
    if (id == RFO_FF_SOUND && v < 0)
    {
        if (strcasecmp(val, "mute") == 0 || strcasecmp(val, "muted") == 0)
            v = 0;
        else if (strcasecmp(val, "sped_up") == 0 || strcasecmp(val, "sped up") == 0)
            v = 1;
    }
    /* --- Cheats submenu: growths and EXP take more than on/off --- */
    if (id == RFO_CHEAT_GROWTHS || id == RFO_CHEAT_EXP)
        v = ParseCheatValue(id, val);
    /* --- end of the Cheats block --- */
    /* map HP bars block: 0-3 or off/bars/warnings/both (not on/yes/true:
       which of the four "on" would mean is not clear); a bad value gives
       the default, as for any key */
    if (id == RFO_MAP_HPBARS)
    {
        static const char *const names[] = { "off", "bars", "warnings", "both" };
        int k;

        for (v = -1, k = 0; k <= RFO_HPBARS_BOTH && v < 0; k++)
        {
            char num[2] = { (char)('0' + k), 0 };

            if (strcmp(val, num) == 0 || strcasecmp(val, names[k]) == 0)
                v = k;
        }
    }
    /* Display submenu: the window size as 0-8 (or auto, 3x and the like); the
       scaling as 0-2 or pixel/fill/stretch */
    if (id == RFO_WINDOW_SCALE || id == RFO_SCALE_MODE)
    {
        static const char *const modes[] = { "pixel", "fill", "stretch" };
        char *end;
        long n = strtol(val, &end, 10);

        v = -1;
        if (end != val && (*end == 0 || (id == RFO_WINDOW_SCALE && (*end == 'x' || *end == 'X') && end[1] == 0)) &&
            n >= 0 && n <= Reforged_OptionMax(id))
            v = (int)n;
        else if (id == RFO_WINDOW_SCALE && strcasecmp(val, "auto") == 0)
            v = 0;
        else if (id == RFO_SCALE_MODE)
            for (n = 0; n <= 2; n++)
                if (strcasecmp(val, modes[n]) == 0 || (n == 0 && strcasecmp(val, "pixel-perfect") == 0))
                    v = (int)n;
    }
    /* --- Randomizer submenu: numbers in range, mode names, switches --- */
    if (id >= RFO_RANDO_FIRST && id <= RFO_RANDO_LAST)
        v = ParseRandoValue(id, val);
    /* --- end of the Randomizer block --- */
    if (v < 0)
    {
        PortLog("[reforged] %s line %d: bad value '%s' for %s: %s\n", path, lineNo, val, key,
            ValueName(id, sDefaults[id]));
        v = sDefaults[id];
    }
    sValues[id] = (unsigned char)v;
    if (id == RFO_FAST_FORWARD)
        *ffLine = lineNo;
}

/* Every line of a file in the settings format into the options (scope: RS_*);
   0 when there is no such file (or it is too large: logged). */
static int ReadSettingsFile(const char *path, int scope, int *ffLine, int *ffBuild)
{
    size_t size = 0;
    char *data, *text, *line, *next;
    int lineNo = 0;

    data = Platform_LoadFile(path, &size);
    if (data == NULL)
        return 0;
    if (size > RFO_MAX_FILE)
    {
        PortLog("[reforged] %s is larger than %d bytes: not read\n", path, RFO_MAX_FILE);
        Platform_FreeFile(data);
        return 0;
    }
    text = malloc(size + 1);
    if (text == NULL)
    {
        Platform_FreeFile(data);
        return 0;
    }
    memcpy(text, data, size);
    text[size] = 0;
    Platform_FreeFile(data);

    for (line = text; line != NULL; line = next)
    {
        char *eq, *key, *val;

        next = strchr(line, '\n');
        if (next != NULL)
            *next++ = 0;
        lineNo++;

        line = Trim(line);
        if (*line == 0 || *line == '#' || *line == ';')
            continue;
        eq = strchr(line, '=');
        if (eq == NULL)
        {
            PortLog("[reforged] %s line %d: no '=': skipped\n", path, lineNo);
            continue;
        }
        *eq = 0;
        key = Trim(line);
        val = Trim(eq + 1);
        ApplySettingLine(key, val, path, lineNo, scope, ffLine, ffBuild);
    }
    free(text);
    return 1;
}

/* The command line's settings (port/hw/main_pc.c: --set KEY=VALUE, --scale N, ...),
   over the file's, for this run. (Written to the settings file like any value when
   the options screen next saves it.) */
static void ApplyCommandLine(void)
{
    int i, ffLine = 0, ffBuild = 1;

    for (i = 0; i < PortArgSettingCount(); i++)
    {
        char buf[256], *eq;

        snprintf(buf, sizeof(buf), "%s", PortArgSetting(i));
        eq = strchr(buf, '=');
        if (eq == NULL)
            continue;
        *eq = 0;
        PortLog("[reforged] command line: %s = %s\n", Trim(buf), Trim(eq + 1));
        ApplySettingLine(Trim(buf), Trim(eq + 1), "the command line", i + 1, RS_CLI, &ffLine, &ffBuild);
    }
}

void Reforged_LoadSettings(void)
{
    static int sLoadedOnce; /* a soft reset (AgbMain again) loads the settings again */
    char path[4096];
    int id, ffLine = 0, ffBuild = 0, haveFile;

    memcpy(sValues, sDefaults, sizeof(sValues));
    sRandoSeed = 0; /* Randomizer block: "Random" */
    Controls_ResetDefaults();

    haveFile = SettingsPath(path, sizeof(path)) && ReadSettingsFile(path, RS_FILE, &ffLine, &ffBuild);
    if (!haveFile) /* no file: every option at its default (the command line still counts) */
        snprintf(path, sizeof(path), "the defaults");

    if (haveFile && ffLine != 0 && !ffBuild && sValues[RFO_FAST_FORWARD] != sDefaults[RFO_FAST_FORWARD])
    {
        PortLog("[reforged] %s line %d: fast_forward = %s from a file an older build wrote (no format = 2, no "
                "fast_forward_speed): ignored, fast-forward stays %s (turn it off in Reforged Options to keep it "
                "off)\n", path, ffLine, ValueName(RFO_FAST_FORWARD, sValues[RFO_FAST_FORWARD]),
            ValueName(RFO_FAST_FORWARD, sDefaults[RFO_FAST_FORWARD]));
        sValues[RFO_FAST_FORWARD] = sDefaults[RFO_FAST_FORWARD];
    }

    for (id = 0; id < RFO_COUNT && haveFile; id++)
        if (sValues[id] != sDefaults[id])
            PortLog("[reforged] %s = %s (from %s)\n", sKeys[id], ValueName(id, sValues[id]), path);
    if (haveFile && sRandoSeed != 0) /* Randomizer block */
        PortLog("[reforged] %s = 0x%08X (from %s)\n", RFO_SEED_KEY, sRandoSeed, path);
    /* The command line and the window: at power-on only. After a soft reset the
       command line is not put back over choices made since, and the window keeps
       what the player did to it (F11, a size from the Display screen). */
    if (!sLoadedOnce)
    {
        ApplyCommandLine();
        Reforged_ApplyDisplay(-1); /* the window as the file (and the command line) want it */
    }
    sLoadedOnce = 1;
    {
        int dupes, restored;

        if (Controls_Tidy(&dupes, &restored) != 0)
            PortLog("[reforged] %s: controls tidied: %d key(s) or button(s) bound twice kept only their first "
                    "binding, %d default(s) put back so A, B, Start and the directions have a key and a button\n",
                path, dupes, restored);
    }
    for (id = 0; id < CTL_COUNT; id++) /* Controls */
    {
        int s, changed = 0;

        for (s = 0; s < CTL_SLOTS; s++)
            changed |= strcmp(Controls_Get(id, s), Controls_Default(id, s)) != 0;
        if (changed)
            PortLog("[reforged] %s = %s | %s | %s | %s (from %s)\n", Controls_SettingsKey(id),
                Controls_Get(id, 0)[0] ? Controls_Get(id, 0) : "none", Controls_Get(id, 1)[0] ? Controls_Get(id, 1) : "none",
                Controls_Get(id, 2)[0] ? Controls_Get(id, 2) : "none", Controls_Get(id, 3)[0] ? Controls_Get(id, 3) : "none",
                path);
    }
}

/* --- Saves marked (port-rfo-casual): the table's "gameplay" column ---
   A save written with a gameplay option on is marked (port/reforged/
   reforged_savemark.c). Every option is gameplay unless it is listed here as
   display-only, so a new option (every cheat) is gameplay by default; a new
   option that only shows or speeds things up must be added to this list. */
static const unsigned char sDisplayOnly[RFO_COUNT] = {
    [RFO_DANGER_ZONE_SELECT] = 1,
    [RFO_FAST_FORWARD] = 1,
    [RFO_FF_SPEED] = 1, /* merge (port-rfo-int3): fast-forward's speed and sound, display-only too */
    [RFO_FF_SOUND] = 1,
    [RFO_GROWTH_SELECT] = 1, /* growth rates on Select: shows numbers only */
    [RFO_MAP_HPBARS] = 1, /* map HP bars block: pictures only (reforged_hpbars.c) */
    [RFO_WINDOW_SCALE] = 1, /* Display submenu: the window only */
    [RFO_SCALE_MODE] = 1,
    [RFO_SCALE_SMOOTH] = 1,
    [RFO_FULLSCREEN] = 1,
    /* original-game bug fixes block: never marks a save. Off is the original game; on is the default (a mark
       is for a value not at its default). The Riev fix acts only where the original hangs for good; the Pierce
       fix also paces every Pierce hit (animations on) as Sure Shot; where it does not hang, the battle results,
       RNG and save are the original's, apart from the play clock (pending/retailfix.md; DECISIONS.md 2026-10-30).
       The Dancer fix (reforged_gamefixes.c) only shows a Dancer's battle with a weapon as a map animation. */
    [RFO_BUG_FIXES] = 1,
};

int Reforged_OptionIsGameplay(int id)
{
    /* Randomizer block: the menu's values are the NEXT new game's, not the game
       being saved; a save's own run is saved apart, as <blk>_rando
       (reforged_savemark.c), so these never go on the save's generic line */
    if (id >= RFO_RANDO_FIRST && id <= RFO_RANDO_LAST)
        return 0;
    return id >= 0 && id < RFO_COUNT && !sDisplayOnly[id];
}
/* --- end of the saves-marked block --- */

/* The settings file is built in one buffer. Base b99518e6 writes 775 bytes;
   each option adds its key line (and often a header line), and the Reforged
   branches together (growths + HP bars + ...) pass 1,024, so the buffer has
   room for several times that; a file that still does not fit is not written
   and says so in the log (it was a silent -1). The Randomizer block's 35 keys,
   its seed and its comment lines take the file to about 3,000 bytes, so the
   buffer is 8 KiB. */
#define RFO_SAVE_BUF 8192

static int WriteSettingsTo(const char *path);

int Reforged_SaveSettings(void)
{
    char path[4096];

    if (!SettingsPath(path, sizeof(path)))
    {
        PortLog("[reforged] no per-user folder: the options are not saved\n");
        return -1;
    }
    return WriteSettingsTo(path);
}

/* --- Option sets (reforged_display_menu.c): option-set-N.txt beside the settings
   file, in its format; a set holds every option and the seed, but not the controls
   or the Display options (written there, skipped when read: they belong to the
   machine). --- */
static int OptionSetPath(int n, char *out, size_t size)
{
    const char *dir = Platform_UserDir();

    return dir != NULL && n >= 1 && n <= 9 && snprintf(out, size, "%soption-set-%d.txt", dir, n) < (int)size;
}

int Reforged_OptionSetExists(int n)
{
    char path[4096];
    size_t size = 0;
    void *data;

    if (!OptionSetPath(n, path, sizeof(path)) || (data = Platform_LoadFile(path, &size)) == NULL)
        return 0;
    Platform_FreeFile(data);
    return 1;
}

int Reforged_SaveOptionSet(int n)
{
    char path[4096];

    return OptionSetPath(n, path, sizeof(path)) && WriteSettingsTo(path) == 0;
}

int Reforged_LoadOptionSet(int n)
{
    char path[4096];
    int id, ffLine = 0, ffBuild = 1;
    unsigned char keep[RFO_COUNT];
    unsigned keepSeed = sRandoSeed;

    if (!OptionSetPath(n, path, sizeof(path)) || !Reforged_OptionSetExists(n))
        return 0;
    memcpy(keep, sValues, sizeof(keep));
    for (id = 0; id < RFO_COUNT; id++) /* what the set leaves out is its default, but the window's */
        if (!IsDisplaySubmenu(id))
            sValues[id] = sDefaults[id];
    sRandoSeed = 0;
    if (!ReadSettingsFile(path, RS_SET, &ffLine, &ffBuild))
    {
        memcpy(sValues, keep, sizeof(keep));
        sRandoSeed = keepSeed;
        return 0;
    }
    PortLog("[reforged] option set %d loaded from %s\n", n, path);
    Reforged_SaveSettings();
    return 1;
}

static int WriteSettingsTo(const char *path)
{
    char tmp[4200], buf[RFO_SAVE_BUF];
    int len, id;

    if (snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid()) >= (int)sizeof(tmp))
        return -1;

    len = snprintf(buf, sizeof(buf),
        "# FE8 PC port: Reforged Options (Extras menu). One \"key = value\" per line;\n"
        "# 1 = on, 0 = off. A missing key, or a bad value, takes its default (off;\n"
        "# fast_forward and original_game_bug_fixes: on).\n"
        "# unbreakable_weapons: 0 = off, 1 = your units, 2 = everyone.\n"
        "# fast_forward (hold Space, or press in the right stick) is on unless set to 0;\n"
        "# fast_forward_speed: 2, 3, 4 or 8; fast_forward_sound: 0 = mute, 1 = sped up\n"
        "# (at 2x-4x; 8x is always muted).\n"
        "# growth_rates_select: 1 = Select on the stat screen shows growth rates.\n"
        "# map_hp_bars_warnings: 0 = off, 1 = HP bars on hurt units, 2 = the ! warnings\n"
        "# on enemies while you move a unit (white: effective, orange: a weapon with\n"
        "# more than 24 crit), 3 = both.\n"
        "# original_game_bug_fixes: 1 (the default) = fixes for the original game's\n"
        "# freezes (the Pierce freeze, Riev's Chapter 20 turn-1 softlock, a Dancer\n"
        "# fighting with a tome); 0 = they hang as on the GBA.\n"
        /* --- Game fixes submenu --- */
        "# fix_*: optional changes to the original's rules (the Game fixes screen),\n"
        "# 0 = off (the default, the original), 1 = on; each marks a save.\n"
        /* --- Cheats submenu --- */
        "# cheat_infinite_gold: 1 = gold stays at 999,999. cheat_growths: 0 = off, 1 = fixed,\n"
        "# 2 = +25%%, 3 = +50%% to every growth. cheat_exp: 0 = x1, 1 = x2, 2 = x3.\n"
        /* --- end of the Cheats block --- */
        /* --- Randomizer submenu --- */
        "# randomizer*: the settings for the NEXT New Game (each save keeps its own\n"
        "# run in the save's .reforged file). randomizer_seed: 0x and hex digits,\n"
        "# or exactly 8 hex digits (as the screen shows them), or else a decimal\n"
        "# number (never octal); 0 = a new one at New Game. Modes:\n"
        "# randomizer_growths 0 = off, 1 = redistribute, 2 = delta, 3 = full;\n"
        "# randomizer_bases 0 = off, 1 = redistribute, 2 = delta;\n"
        "# randomizer_class_growths 0 = keep, 1 = transfer;\n"
        "# randomizer_class_bases 0 = final, 1 = personal; randomizer_enemy_growths\n"
        "# 0 = off, 1 = flat, 2 = scaling; randomizer_boss_stats 0 = off, 1 = linear,\n"
        "# 2 = ease. Amounts: growth_variance, growth_min, enemy_buff 0-100;\n"
        "# growth_max 0-255; base_variance 1-10; boss_max_boost 1-20; the chances\n"
        "# enemy_weapons, weapon_traits, enemy_drops 0-100 (%%, 0 = off).\n"
        /* --- end of the Randomizer block --- */
        "format = 2\n");
    for (id = 0; id < RFO_COUNT && len < (int)sizeof(buf); id++)
        len += snprintf(buf + len, sizeof(buf) - len, "%s = %d\n", sKeys[id], sValues[id]);
    if (len < (int)sizeof(buf)) /* Randomizer block: the seed, after the options */
        len += snprintf(buf + len, sizeof(buf) - len, "%s = 0x%08X\n", RFO_SEED_KEY, sRandoSeed);
    /* Controls: every action, key | key 2 | pad | pad 2 */
    if (len < (int)sizeof(buf))
        len += snprintf(buf + len, sizeof(buf) - len,
            "# control_*: key | key 2 | gamepad | gamepad 2 (\"none\": unbound). Keys by name (Z, Return,\n"
            "# Keypad Enter, Left Shift, F11, ...); gamepad: a, b (the buttons marked A and B), west,\n"
            "# north, back, start, guide, l, r, zl, zr, dpup, dpdown, dpleft, dpright, lstick, rstick.\n");
    for (id = 0; id < CTL_COUNT && len < (int)sizeof(buf); id++)
    {
        const char *f[CTL_SLOTS];
        int s;

        for (s = 0; s < CTL_SLOTS; s++)
            f[s] = Controls_Get(id, s)[0] ? Controls_Get(id, s) : "none";
        len += snprintf(buf + len, sizeof(buf) - len, "%s = %s | %s | %s | %s\n", Controls_SettingsKey(id), f[0], f[1],
            f[2], f[3]);
    }
    if (len >= (int)sizeof(buf))
    {
        PortLog("[reforged] the options file needs more than %d bytes: not written (raise RFO_SAVE_BUF in "
                "port/reforged/reforged_settings.c)\n", (int)sizeof(buf));
        return -1;
    }

    Platform_MakeUserDir();
    if (Platform_SaveFileAtomic(path, tmp, buf, (size_t)len) != 0)
    {
        PortLog("[reforged] could not write %s\n", path);
        return -1;
    }
    PortLog("[reforged] options written to %s\n", path);
    return 0;
}

/* --- Original-game bug fixes (RFO_BUG_FIXES; port_reforged.h) ---
   Called from the game code through include/event.h and include/anime.h (played builds only). With the
   option on, each logs the place it acted (the event one: only where the original game hangs for good;
   the Pierce one: on every Pierce strike in a battle animation, freezing or not, as its pacing changes on
   each); off, the event one logs once that the original code runs (it hangs there, as on the GBA). */
int Reforged_FixEventUnitGone(int cmd, int pid)
{
    static int sOffLogged;

    if (!sValues[RFO_BUG_FIXES])
    {
        /* the original: the engine runs the same command again at once, for ever (one log line) */
        if (!sOffLogged)
            PortLog("[reforged] original-game bug fixes off: event command 0x%02X points at character 0x%02X, "
                    "who is not on the map: the original game hangs here\n", cmd, pid & 0xFFFF);
        sOffLogged = 1;
        return 0;
    }
    PortLog("[reforged] original-game bug fix: event command 0x%02X points at character 0x%02X, who is not on "
            "the map: skipped (the original game hangs here)\n", cmd, pid & 0xFFFF);
    return 1;
}

int Reforged_FixPierceFreeze(void)
{
    if (!sValues[RFO_BUG_FIXES])
        return 0;
    PortLog("[reforged] original-game bug fix: Pierce in a battle animation acts as Sure Shot (the right-hand "
            "anims' state is kept)\n");
    return 1;
}
/* --- end of the original-game bug fixes block --- */

#endif /* PORT_PLAYED_BUILD */
