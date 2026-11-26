#include "global.h"

/*
 * Reforged Options: "Update notice" (RFO_UPDATE_NOTICE, the Display screen's row; v1.31.0). Off by default,
 * display-only (never marks a save, never in an option set). Played builds only; empty in every test build.
 *
 * With the row on, the game asks GitHub once per run, on a thread of its own, which release of the public
 * port is the latest: one HTTPS GET of https://api.github.com/repos/FT775/fe8-pc-port/releases/latest
 * through the system's curl (port/platform.h Platform_HttpsGet), carrying nothing but a fixed User-Agent.
 * When that release's tag is newer than this build's version (PORT_VERSION, the first "## vX.Y.Z" heading
 * of docs/port/CHANGELOG.md when the exe was built), the title screen shows one line for 8 seconds:
 * "vX.Y.Z is out (GitHub releases)". Nothing is downloaded or run, and no answer (no network, a firewall,
 * no curl) shows nothing. The game never waits: the frame step only looks at a finished answer.
 *
 * Nothing here touches game memory: the title screen is found with Proc_Find (read only) and the line is
 * drawn on the window's picture by the save states' toast (port/reforged/reforged_savestate.c).
 *
 * Test switch: FE8PC_TEST_UPDATE_TAG=vX.Y.Z (no request: that tag is the answer).
 */

#if PORT_PLAYED_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proc.h"
#include "port_hw.h"
#include "port_reforged.h"
#include "port_state.h"
#include "../platform.h"

#ifndef PORT_VERSION
#define PORT_VERSION "v0.0.0"
#endif

#define UPD_URL   "https://api.github.com/repos/FT775/fe8-pc-port/releases/latest"
#define UPD_AGENT "fe8pc-update-check"
#define UPD_TOAST_FRAMES 480 /* 8 s */

extern const struct ProcCmd gProcScr_TitleScreen[];

enum { UPD_IDLE, UPD_ASKING, UPD_ANSWER, UPD_NONE };
static int sState;   /* UPD_*; written by the thread once (__atomic), read by the frame step */
static char sTag[32];
static int sStarted, sDone;

/* "vA.B.C" (or "A.B" ...) as A*1000000 + B*1000 + C; -1 when it is not one. */
static long VersionNumber(const char *s)
{
    long a = 0, b = 0, c = 0;

    if (s == NULL)
        return -1;
    if (*s == 'v' || *s == 'V')
        s++;
    if (sscanf(s, "%ld.%ld.%ld", &a, &b, &c) < 2 || a < 0 || b < 0 || c < 0 || a > 999 || b > 999 || c > 999)
        return -1;
    return a * 1000000 + b * 1000 + c;
}

/* The "tag_name" string of a GitHub release's JSON into out; 0 if there is none. */
static int TagFromJson(const char *json, char *out, size_t size)
{
    const char *p = strstr(json, "\"tag_name\"");
    size_t n = 0;

    if (p == NULL || (p = strchr(p + 10, ':')) == NULL || (p = strchr(p, '"')) == NULL)
        return 0;
    p++;
    while (p[n] != '\0' && p[n] != '"' && n + 1 < size)
        n++;
    if (p[n] != '"' || n == 0)
        return 0;
    memcpy(out, p, n);
    out[n] = '\0';
    return 1;
}

static int AskThread(void *unused)
{
    static char json[65536];
    static char tag[32];
    const char *test = getenv("FE8PC_TEST_UPDATE_TAG");
    int ok;

    (void)unused;
    if (test != NULL)
        ok = snprintf(tag, sizeof(tag), "%s", test) > 0;
    else
        ok = Platform_HttpsGet(UPD_URL, UPD_AGENT, json, sizeof(json)) && TagFromJson(json, tag, sizeof(tag));
    if (ok)
        memcpy(sTag, tag, sizeof(sTag));
    __atomic_store_n(&sState, ok ? UPD_ANSWER : UPD_NONE, __ATOMIC_RELEASE);
    return 0;
}

/* The row on (the settings read at start, or the row switched on): ask, once per run. */
void Reforged_UpdateCheck(void)
{
    if (sStarted || Reforged_GetOption(RFO_UPDATE_NOTICE) == 0)
        return;
    sStarted = 1;
    __atomic_store_n(&sState, UPD_ASKING, __ATOMIC_RELEASE);
    if (!Platform_RunDetached(AskThread, NULL))
        __atomic_store_n(&sState, UPD_NONE, __ATOMIC_RELEASE);
}

/* Each frame step (frame.c, before the picture is shown): the line on the title screen, once per run. */
void Reforged_UpdateFrame(void)
{
    int st;
    long have, latest;
    char line[64];

    if (sDone || !sStarted)
        return;
    st = __atomic_load_n(&sState, __ATOMIC_ACQUIRE);
    if (st == UPD_ASKING || st == UPD_IDLE)
        return;
    have = VersionNumber(PORT_VERSION);
    latest = st == UPD_ANSWER ? VersionNumber(sTag) : -1;
    if (st != UPD_ANSWER)
    {
        PortLog("[update] no answer from GitHub (no network, or no curl): nothing shown\n");
        sDone = 1;
        return;
    }
    if (latest <= have || have < 0)
    {
        PortLog("[update] latest release %s, this build %s: nothing newer\n", sTag, PORT_VERSION);
        sDone = 1;
        return;
    }
    if (Reforged_GetOption(RFO_UPDATE_NOTICE) == 0 || Proc_Find(gProcScr_TitleScreen) == NULL)
        return; /* shown on the next title screen */
    PortLog("[update] latest release %s, this build %s: shown on the title screen\n", sTag, PORT_VERSION);
    snprintf(line, sizeof(line), "%s is out (GitHub releases)", sTag);
    PortState_ToastText(line, 0, UPD_TOAST_FRAMES);
    sDone = 1;
}

/* Save states (port/include/port_state.h): not game state. */
PORT_STATE_SESSION(sState)
PORT_STATE_SESSION(sTag)
PORT_STATE_SESSION(sStarted)
PORT_STATE_SESSION(sDone)

#endif /* PORT_PLAYED_BUILD */
