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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "port_hw.h"
#include "port_reforged.h"
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
    /* --- Cheats submenu (reforged_cheats.c) --- */
    [RFO_CHEAT_GOLD] = "cheat_infinite_gold",
    [RFO_CHEAT_GROWTHS] = "cheat_growths",
    [RFO_CHEAT_EXP] = "cheat_exp",
    /* --- end of the Cheats block --- */
};

/* Each option's default: 0 (OFF) but the fast-forward block's (fast-forward
   ON at 4x, sound muted; DECISIONS.md 2026-10-28) and the original-game bug
   fixes (ON; DECISIONS.md 2026-10-30, U10). A missing file or key, or a bad
   value, gives the default. */
#define RFO_DEFAULTS { [RFO_FAST_FORWARD] = 1, [RFO_FF_SPEED] = 4, [RFO_BUG_FIXES] = 1 }
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

void Reforged_SetOption(int id, int value)
{
    if (id >= 0 && id < RFO_COUNT)
        sValues[id] = (value >= 0 && value <= Reforged_OptionMax(id) && FfValueOk(id, value)) ? (unsigned char)value
                                                                                               : sDefaults[id];
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
    /* --- Cheats submenu: growths 0 off, 1 fixed, 2 +25%, 3 +50%; EXP 0 x1, 1 x2, 2 x3 --- */
    if (id == RFO_CHEAT_GROWTHS)
        return 3;
    if (id == RFO_CHEAT_EXP)
        return 2;
    /* --- end of the Cheats block --- */
    return id == RFO_UNBREAKABLE ? 2 : 1;
}

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
    if (id == RFO_UNBREAKABLE)
        return value == 2 ? "everyone" : value == 1 ? "yours" : "off";
    if (id == RFO_FF_SPEED) /* fast-forward block */
        return value >= 0 && value <= 8 ? speeds[value] : "?";
    if (id == RFO_FF_SOUND)
        return value ? "sped up" : "mute";
    if (id == RFO_MAP_HPBARS) /* map HP bars block */
        return value == RFO_HPBARS_BOTH ? "both" : value == RFO_HPBARS_WARNINGS ? "warnings"
             : value == RFO_HPBARS_BARS ? "bars" : "off";
    return value ? "on" : "off";
}

void Reforged_LoadSettings(void)
{
    char path[4096];
    size_t size = 0;
    char *data, *text, *line, *next;
    int lineNo = 0, id, ffLine = 0, ffBuild = 0;

    memcpy(sValues, sDefaults, sizeof(sValues));

    if (!SettingsPath(path, sizeof(path)))
        return;
    data = Platform_LoadFile(path, &size);
    if (data == NULL)
        return; /* no file: every option OFF */
    if (size > RFO_MAX_FILE)
    {
        PortLog("[reforged] %s is larger than %d bytes: not read, every option OFF\n", path, RFO_MAX_FILE);
        Platform_FreeFile(data);
        return;
    }

    text = malloc(size + 1);
    if (text == NULL)
    {
        Platform_FreeFile(data);
        return;
    }
    memcpy(text, data, size);
    text[size] = 0;
    Platform_FreeFile(data);

    for (line = text; line != NULL; line = next)
    {
        char *eq, *key, *val;
        int v;

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

        if (strcmp(key, "format") == 0)
        {
            ffBuild |= atoi(val) >= 2; /* written by a build with fast-forward */
            continue;
        }
        for (id = 0; id < RFO_COUNT; id++)
            if (strcmp(key, sKeys[id]) == 0)
                break;
        if (id == RFO_COUNT)
        {
            PortLog("[reforged] %s line %d: unknown key '%s': skipped\n", path, lineNo, key);
            continue;
        }
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
            ffBuild = 1; /* only a build with fast-forward writes this key */
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
        if (v < 0)
        {
            PortLog("[reforged] %s line %d: bad value '%s' for %s: %s\n", path, lineNo, val, key,
                ValueName(id, sDefaults[id]));
            v = sDefaults[id];
        }
        sValues[id] = (unsigned char)v;
        if (id == RFO_FAST_FORWARD)
            ffLine = lineNo;
    }
    free(text);

    if (ffLine != 0 && !ffBuild && sValues[RFO_FAST_FORWARD] != sDefaults[RFO_FAST_FORWARD])
    {
        PortLog("[reforged] %s line %d: fast_forward = %s from a file an older build wrote (no format = 2, no "
                "fast_forward_speed): ignored, fast-forward stays %s (turn it off in Reforged Options to keep it "
                "off)\n", path, ffLine, ValueName(RFO_FAST_FORWARD, sValues[RFO_FAST_FORWARD]),
            ValueName(RFO_FAST_FORWARD, sDefaults[RFO_FAST_FORWARD]));
        sValues[RFO_FAST_FORWARD] = sDefaults[RFO_FAST_FORWARD];
    }

    for (id = 0; id < RFO_COUNT; id++)
        if (sValues[id] != sDefaults[id])
            PortLog("[reforged] %s = %s (from %s)\n", sKeys[id], ValueName(id, sValues[id]), path);
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
    /* original-game bug fixes block: never marks a save. Off is the original game; on is the default (a mark
       is for a value not at its default). The Riev fix acts only where the original hangs for good; the Pierce
       fix also paces every Pierce hit (animations on) as Sure Shot; where it does not hang, the battle results,
       RNG and save are the original's, apart from the play clock (pending/retailfix.md; DECISIONS.md 2026-10-30). */
    [RFO_BUG_FIXES] = 1,
};

int Reforged_OptionIsGameplay(int id)
{
    return id >= 0 && id < RFO_COUNT && !sDisplayOnly[id];
}
/* --- end of the saves-marked block --- */

/* The settings file is built in one buffer. Base b99518e6 writes 775 bytes;
   each option adds its key line (and often a header line), and the Reforged
   branches together (growths + HP bars + ...) pass 1,024, so the buffer has
   room for several times that; a file that still does not fit is not written
   and says so in the log (it was a silent -1). */
#define RFO_SAVE_BUF 4096

int Reforged_SaveSettings(void)
{
    char path[4096], tmp[4200], buf[RFO_SAVE_BUF];
    int len, id;

    if (!SettingsPath(path, sizeof(path)))
    {
        PortLog("[reforged] no per-user folder: the options are not saved\n");
        return -1;
    }
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
        "# freezes (the Pierce freeze, Riev's Chapter 20 turn-1 softlock); 0 = they hang\n"
        "# as on the GBA.\n"
        /* --- Cheats submenu --- */
        "# cheat_infinite_gold: 1 = gold stays at 999,999. cheat_growths: 0 = off, 1 = fixed,\n"
        "# 2 = +25%%, 3 = +50%% to every growth. cheat_exp: 0 = x1, 1 = x2, 2 = x3.\n"
        /* --- end of the Cheats block --- */
        "format = 2\n");
    for (id = 0; id < RFO_COUNT && len < (int)sizeof(buf); id++)
        len += snprintf(buf + len, sizeof(buf) - len, "%s = %d\n", sKeys[id], sValues[id]);
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
