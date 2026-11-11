/*
 * Rebindable controls: the binding table (port/include/port_controls.h).
 * Library-neutral and the same in every build; only the played builds change
 * it (the Controls screen, the settings file).
 */

#include <string.h>
#include <strings.h>

#include "port_controls.h"

/* The keys and buttons the port has always used. */
static const char *const sDefaults[CTL_COUNT][CTL_SLOTS] = {
    [CTL_A]            = { "Z",         "",             "a",       ""   },
    [CTL_B]            = { "X",         "",             "b",       ""   },
    [CTL_SELECT]       = { "Tab",       "",             "back",    ""   }, /* v1.3.0: was Backspace, now Rewind's */
    [CTL_START]        = { "Return",    "Keypad Enter", "start",   ""   },
    [CTL_RIGHT]        = { "Right",     "",             "dpright", ""   },
    [CTL_LEFT]         = { "Left",      "",             "dpleft",  ""   },
    [CTL_UP]           = { "Up",        "",             "dpup",    ""   },
    [CTL_DOWN]         = { "Down",      "",             "dpdown",  ""   },
    [CTL_R]            = { "E",         "",             "r",       "zr" },
    [CTL_L]            = { "Q",         "",             "l",       "zl" },
    [CTL_FAST_FORWARD] = { "Space",     "",             "rstick",  ""   },
    [CTL_FULLSCREEN]   = { "F11",       "",             "",        ""   },
    [CTL_REWIND]       = { "Backspace", "",             "",        ""   }, /* held: Reforged "Rewind" */
};

static const char *const sKeys[CTL_COUNT] = {
    "control_a", "control_b", "control_select", "control_start", "control_right", "control_left",
    "control_up", "control_down", "control_r", "control_l", "control_fast_forward", "control_fullscreen", "control_rewind",
};

static const char *const sLabels[CTL_COUNT] = {
    "A", "B", "Select", "Start", "Right", "Left", "Up", "Down", "R", "L", "Fast-forward", "Fullscreen", "Rewind",
};

static char sBind[CTL_COUNT][CTL_SLOTS][CTL_NAME_MAX];
static int sInit;
static unsigned sGeneration = 1;

static void Init(void)
{
    int a, s;

    if (sInit)
        return;
    for (a = 0; a < CTL_COUNT; a++)
        for (s = 0; s < CTL_SLOTS; s++)
            strcpy(sBind[a][s], sDefaults[a][s]);
    sInit = 1;
}

static int Valid(int action, int slot)
{
    return action >= 0 && action < CTL_COUNT && slot >= 0 && slot < CTL_SLOTS;
}

const char *Controls_Get(int action, int slot)
{
    if (!Valid(action, slot))
        return "";
    Init();
    return sBind[action][slot];
}

const char *Controls_Default(int action, int slot)
{
    return Valid(action, slot) ? sDefaults[action][slot] : "";
}

int Controls_Set(int action, int slot, const char *name)
{
    char old[CTL_NAME_MAX];
    int a, s;

    if (!Valid(action, slot) || name == NULL || strlen(name) >= CTL_NAME_MAX)
        return 0;
    Init();
    strcpy(old, sBind[action][slot]);
    if (name[0] != 0)
        for (a = 0; a < CTL_COUNT; a++)
            for (s = 0; s < CTL_SLOTS; s++)
                if ((a != action || s != slot) && CTL_SLOT_IS_PAD(s) == CTL_SLOT_IS_PAD(slot) &&
                    strcasecmp(sBind[a][s], name) == 0)
                    strcpy(sBind[a][s], old); /* the swap */
    strcpy(sBind[action][slot], name);
    sGeneration++;
    return 1;
}

int Controls_Load(int action, int slot, const char *name)
{
    if (!Valid(action, slot) || name == NULL || strlen(name) >= CTL_NAME_MAX)
        return 0;
    Init();
    strcpy(sBind[action][slot], name);
    sGeneration++;
    return 1;
}

int Controls_IsDefault(void)
{
    int a, s;

    Init();
    for (a = 0; a < CTL_COUNT; a++)
        for (s = 0; s < CTL_SLOTS; s++)
            if (strcmp(sBind[a][s], sDefaults[a][s]) != 0)
                return 0;
    return 1;
}

int Controls_IsEssential(int action)
{
    switch (action)
    {
    case CTL_A: case CTL_B: case CTL_START: case CTL_RIGHT: case CTL_LEFT: case CTL_UP: case CTL_DOWN:
        return 1;
    }
    return 0;
}

/* The slots of `action` of slot's kind, but `but`, bound to something other than
   name (NULL: anything). */
int Controls_OthersOfKind(int action, int but, const char *name)
{
    int s, n = 0;

    Init();
    for (s = 0; s < CTL_SLOTS; s++)
        if (s != but && CTL_SLOT_IS_PAD(s) == CTL_SLOT_IS_PAD(but) && sBind[action][s][0] != 0 &&
            (name == NULL || strcasecmp(sBind[action][s], name) != 0))
            n++;
    return n;
}

int Controls_Tidy(int *dupes, int *restored)
{
    int a, s, a2, s2, k;

    Init();
    *dupes = *restored = 0;
    /* a key or button bound twice (of one kind): the first in action/slot order keeps it */
    for (a = 0; a < CTL_COUNT; a++)
        for (s = 0; s < CTL_SLOTS; s++)
            for (a2 = a; a2 < CTL_COUNT && sBind[a][s][0] != 0; a2++)
                for (s2 = a2 == a ? s + 1 : 0; s2 < CTL_SLOTS; s2++)
                    if (CTL_SLOT_IS_PAD(s2) == CTL_SLOT_IS_PAD(s) && strcasecmp(sBind[a2][s2], sBind[a][s]) == 0)
                    {
                        sBind[a2][s2][0] = 0;
                        (*dupes)++;
                    }
    /* an essential action with no key, or no pad button: its first default of that
       kind back, taken from the action that has it (when that one keeps another) */
    for (a = 0; a < CTL_COUNT; a++)
        for (k = 0; k < 2 && Controls_IsEssential(a); k++)
        {
            int first = k == 0 ? CTL_KEY1 : CTL_PAD1;
            const char *want = sDefaults[a][first];
            int blocked = 0;

            if (sBind[a][first][0] != 0 || sBind[a][first + 1][0] != 0 || want[0] == 0)
                continue;
            for (a2 = 0; a2 < CTL_COUNT; a2++)
                for (s2 = first; s2 < first + 2; s2++)
                    if (strcasecmp(sBind[a2][s2], want) == 0 && Controls_IsEssential(a2) &&
                        Controls_OthersOfKind(a2, s2, want) == 0)
                        blocked = 1;
            if (blocked)
                continue;
            for (a2 = 0; a2 < CTL_COUNT; a2++)
                for (s2 = first; s2 < first + 2; s2++)
                    if (strcasecmp(sBind[a2][s2], want) == 0)
                        sBind[a2][s2][0] = 0;
            strcpy(sBind[a][first], want);
            (*restored)++;
        }
    sGeneration++;
    return *dupes + *restored;
}

void Controls_ResetDefaults(void)
{
    sInit = 0;
    Init();
    sGeneration++;
}

unsigned Controls_Generation(void)
{
    return sGeneration;
}

const char *Controls_SettingsKey(int action)
{
    return action >= 0 && action < CTL_COUNT ? sKeys[action] : NULL;
}

const char *Controls_ActionLabel(int action)
{
    return action >= 0 && action < CTL_COUNT ? sLabels[action] : "";
}
