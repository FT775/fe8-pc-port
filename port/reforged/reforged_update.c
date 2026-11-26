#include "global.h"

/*
 * Reforged Options: "Updates" (RFO_UPDATE_NOTICE, settings key "updates", the Display screen's row; v1.31.0 as
 * "Update notice", v1.33.0 on by default and able to install). Display-only: never marks a save, never in an
 * option set. Played builds only; empty in every test build.
 *
 * With the row on, the game asks GitHub once per run, on a thread of its own, for the newest release of the
 * public port: one HTTPS GET of https://api.github.com/repos/FT775/fe8-pc-port/releases/latest through the
 * system's curl (port/platform.h Platform_HttpsGet), carrying nothing but a fixed User-Agent. When that
 * release's tag is newer than this build's version (PORT_VERSION, the first "## vX.Y.Z" heading of
 * docs/port/CHANGELOG.md when the exe was built):
 *   - on Windows (Platform_CanInstallUpdates), the title screen says "vX.Y.Z is out: Select installs it" for as
 *     long as it is shown; Select there (the title screen itself takes only A, B and Start) downloads the
 *     release's Windows zip (its URL must be under github.com/FT775/fe8-pc-port/releases/download/), checks its
 *     SHA-256 against the digest GitHub gives for that file (no digest, or another one: nothing is installed),
 *     and Platform_InstallUpdate puts its fe8pc-sdl.exe and SDL3.dll in place of these (written beside them
 *     as .new first, then renamed in; the old ones renamed to .old, deleted at the next start); then, on the
 *     title screen only (a game started during the download is never cut short: the new version starts next
 *     time), the game starts again with the same command line, the new copy waiting for this one to end
 *     before it opens the save. Saves and settings are not touched. Any failure leaves the old files and
 *     says so;
 *   - elsewhere, "vX.Y.Z is out (GitHub releases)" for 8 seconds.
 * The game never waits for the network: the frame step only looks at finished answers. Nothing is shown when
 * there is no answer (no network, a firewall, no curl).
 *
 * Nothing here touches game memory: the title screen is found with Proc_Find and Select read from the game's
 * key status (read only); the lines are drawn on the window's picture by the save states' toast.
 *
 * Test switches: FE8PC_TEST_UPDATE_TAG=vX.Y.Z (no request: that tag is the newest, without a download),
 * FE8PC_TEST_UPDATE_JSON=path (the release's JSON read from that file instead of GitHub; its download URL may
 * then be a file:// URL), FE8PC_TEST_UPDATE_REAL=1 (ask GitHub in a scripted run). A scripted run
 * (FE8PC_FRAMES set: the test tools) asks nobody otherwise. FE8PC_UPDATED=1 is set for the game started after
 * an install: that run offers no install.
 */

#if PORT_PLAYED_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proc.h"
#include "hardware.h"
#include "port_hw.h"
#include "port_reforged.h"
#include "port_state.h"
#include "../platform.h"

#ifndef PORT_VERSION
#define PORT_VERSION "v0.0.0"
#endif

#define UPD_URL     "https://api.github.com/repos/FT775/fe8-pc-port/releases/latest"
#define UPD_DL_BASE "https://github.com/FT775/fe8-pc-port/releases/download/"
#define UPD_AGENT   "fe8pc-update-check"
#define UPD_NOTICE_FRAMES 480 /* 8 s */

extern const struct ProcCmd gProcScr_TitleScreen[];

/* written once by a thread (__atomic), read by the frame step */
enum { UPD_IDLE, UPD_ASKING, UPD_ANSWER, UPD_NONE };
enum { INS_IDLE, INS_RUNNING, INS_DONE, INS_FAILED };
static int sState, sInstall;
static char sTag[32];
static char sZipUrl[512];
static char sDigest[65];   /* lower-case hex SHA-256 of the zip, as GitHub gives it */
static char sFail[64];     /* why the install failed (the log has more) */
static int sStarted, sDone, sOffered, sDoneFrames, sToldLater;

/* --- small helpers --- */

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

/* The string value of "key" at or after p (JSON, any spacing) into out; the position after it, or NULL. */
static const char *JsonString(const char *p, const char *key, char *out, size_t size)
{
    char pat[48];
    size_t n = 0;

    snprintf(pat, sizeof(pat), "\"%s\"", key);
    p = strstr(p, pat);
    if (p == NULL)
        return NULL;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (*p++ != ':')
        return NULL;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (*p++ != '"')
        return NULL;
    while (p[n] != '\0' && p[n] != '"' && p[n] != '\\' && n + 1 < size)
        n++;
    if (p[n] != '"')
        return NULL;
    memcpy(out, p, n);
    out[n] = '\0';
    return p + n + 1;
}

/* The release's tag, and its Windows zip's URL and SHA-256 (an asset whose name ends in "-windows.zip"; its
   digest must come before its download URL, as GitHub writes an asset's fields, so it is that asset's). */
static int ParseRelease(const char *json, int allowFile)
{
    const char *p = json;
    char name[128], url[512], digest[80];

    if (JsonString(json, "tag_name", sTag, sizeof(sTag)) == NULL)
        return 0;
    sZipUrl[0] = sDigest[0] = '\0';
    while ((p = JsonString(p, "name", name, sizeof(name))) != NULL)
    {
        size_t n = strlen(name);
        const char *q, *d;

        if (n < 12 || strcmp(name + n - 12, "-windows.zip") != 0)
            continue;
        q = JsonString(p, "browser_download_url", url, sizeof(url));
        if (q == NULL)
            break;
        d = strstr(p, "\"digest\"");
        if (d != NULL && d < q && JsonString(p, "digest", digest, sizeof(digest)) != NULL &&
            strncmp(digest, "sha256:", 7) == 0 && strlen(digest + 7) == 64)
            snprintf(sDigest, sizeof(sDigest), "%s", digest + 7);
        if (strstr(url, "..") != NULL || strpbrk(url, "%?#\\") != NULL)
            break; /* no way out of the release folder, no escapes */
        if (strncmp(url, UPD_DL_BASE, strlen(UPD_DL_BASE)) == 0 || (allowFile && strncmp(url, "file://", 7) == 0))
            snprintf(sZipUrl, sizeof(sZipUrl), "%s", url);
        break;
    }
    return 1;
}

/* --- SHA-256 (FIPS 180-4), for the downloaded zip --- */

static const unsigned long sK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
    0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
    0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2,
};

#define M32 0xFFFFFFFFul
#define ROR32(x, n) ((((x) >> (n)) | ((x) << (32 - (n)))) & M32)

static void Sha256Block(unsigned long *h, const unsigned char *p)
{
    unsigned long w[64], a, b, c, d, e, f, g, k, t1, t2;
    int i;

    for (i = 0; i < 16; i++)
        w[i] = ((unsigned long)p[4 * i] << 24) | ((unsigned long)p[4 * i + 1] << 16) | ((unsigned long)p[4 * i + 2] << 8) |
               (unsigned long)p[4 * i + 3];
    for (i = 16; i < 64; i++)
    {
        unsigned long s0 = ROR32(w[i - 15], 7) ^ ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        unsigned long s1 = ROR32(w[i - 2], 17) ^ ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10);

        w[i] = (w[i - 16] + s0 + w[i - 7] + s1) & M32;
    }
    a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (i = 0; i < 64; i++)
    {
        t1 = (k + (ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25)) + ((e & f) ^ (~e & g & M32)) + sK[i] + w[i]) & M32;
        t2 = ((ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22)) + ((a & b) ^ (a & c) ^ (b & c))) & M32;
        k = g, g = f, f = e, e = (d + t1) & M32, d = c, c = b, b = a, a = (t1 + t2) & M32;
    }
    h[0] = (h[0] + a) & M32, h[1] = (h[1] + b) & M32, h[2] = (h[2] + c) & M32, h[3] = (h[3] + d) & M32;
    h[4] = (h[4] + e) & M32, h[5] = (h[5] + f) & M32, h[6] = (h[6] + g) & M32, h[7] = (h[7] + k) & M32;
}

/* The SHA-256 of data as 64 lower-case hex digits in hex[65]. */
static void Sha256Hex(const unsigned char *data, size_t len, char *hex)
{
    unsigned long h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    unsigned char last[128];
    size_t i, rest = len % 64, tail;
    unsigned long long bits = (unsigned long long)len * 8u;

    for (i = 0; i + 64 <= len; i += 64)
        Sha256Block(h, data + i);
    memset(last, 0, sizeof(last));
    memcpy(last, data + len - rest, rest);
    last[rest] = 0x80;
    tail = rest < 56 ? 64 : 128;
    for (i = 0; i < 8; i++)
        last[tail - 1 - i] = (unsigned char)(bits >> (8 * i));
    Sha256Block(h, last);
    if (tail == 128)
        Sha256Block(h, last + 64);
    for (i = 0; i < 8; i++)
        sprintf(hex + 8 * i, "%08lx", h[i]);
}

/* --- the threads --- */

static int AskThread(void *unused)
{
    static char json[131072];
    static char tag[32];
    const char *test = getenv("FE8PC_TEST_UPDATE_TAG"), *testJson = getenv("FE8PC_TEST_UPDATE_JSON");
    int ok = 0;

    (void)unused;
    if (test != NULL)
    {
        ok = snprintf(tag, sizeof(tag), "%s", test) > 0;
        if (ok)
            memcpy(sTag, tag, sizeof(sTag));
    }
    else if (testJson != NULL)
    {
        size_t size = 0;
        char *data = Platform_LoadFile(testJson, &size);

        if (data != NULL && size < sizeof(json))
        {
            memcpy(json, data, size);
            json[size] = '\0';
            ok = ParseRelease(json, 1);
        }
        if (data != NULL)
            Platform_FreeFile(data);
    }
    else
        ok = Platform_HttpsGet(UPD_URL, UPD_AGENT, json, sizeof(json)) && ParseRelease(json, 0);
    __atomic_store_n(&sState, ok ? UPD_ANSWER : UPD_NONE, __ATOMIC_RELEASE);
    return 0;
}

static void Fail(const char *why)
{
    snprintf(sFail, sizeof(sFail), "%s", why);
    __atomic_store_n(&sInstall, INS_FAILED, __ATOMIC_RELEASE);
}

static int InstallThread(void *unused)
{
    static char dir[512], zip[600], folder[80];
    char hex[65];
    size_t size = 0;
    unsigned char *data;

    (void)unused;
    if (!Platform_UpdateTempDir(dir, sizeof(dir)))
    {
        Fail("no folder for the download");
        return 0;
    }
    snprintf(zip, sizeof(zip), "%supdate.zip", dir);
    PortLog("[update] downloading %s\n", sZipUrl);
    if (!Platform_HttpsDownload(sZipUrl, zip))
    {
        Fail("the download failed");
        return 0;
    }
    data = Platform_LoadFile(zip, &size);
    if (data == NULL || size < 1024)
    {
        if (data != NULL)
            Platform_FreeFile(data);
        Fail("the download is empty");
        return 0;
    }
    Sha256Hex(data, size, hex);
    Platform_FreeFile(data);
    if (strcmp(hex, sDigest) != 0)
    {
        PortLog("[update] the zip's SHA-256 %s is not GitHub's %s: not installed\n", hex, sDigest);
        Fail("the download did not check out");
        return 0;
    }
    PortLog("[update] %lu bytes, SHA-256 %s as GitHub gives it\n", (unsigned long)size, hex);
    snprintf(folder, sizeof(folder), "FE8-PC-Port-%s-Windows", sTag);
    if (!Platform_InstallUpdate(zip, dir, folder))
    {
        Fail("could not put the new files in place");
        return 0;
    }
    __atomic_store_n(&sInstall, INS_DONE, __ATOMIC_RELEASE);
    return 0;
}

/* --- the frame step's side --- */

/* The row on (Reforged_ApplyDisplay: at start, or the row switched on): ask, once per run. Also, once, delete
   what an earlier install left (the .old files). */
void Reforged_UpdateCheck(void)
{
    static int sCleaned;

    if (!sCleaned)
    {
        sCleaned = 1;
        Platform_UpdateCleanup();
        if (getenv("FE8PC_UPDATED") != NULL)
            PortLog("[update] this run follows an install (%s)\n", PORT_VERSION);
    }
    if (sStarted || Reforged_GetOption(RFO_UPDATE_NOTICE) == 0)
        return;
    /* a scripted run (FE8PC_FRAMES: the test tools' runs) asks nobody, unless an update test switch is set */
    if (getenv("FE8PC_FRAMES") != NULL && getenv("FE8PC_TEST_UPDATE_TAG") == NULL && getenv("FE8PC_TEST_UPDATE_JSON") == NULL &&
        getenv("FE8PC_TEST_UPDATE_REAL") == NULL)
        return;
    sStarted = 1;
    __atomic_store_n(&sState, UPD_ASKING, __ATOMIC_RELEASE);
    if (!Platform_RunDetached(AskThread, NULL))
        __atomic_store_n(&sState, UPD_NONE, __ATOMIC_RELEASE);
}

static int CanInstall(void)
{
    return Platform_CanInstallUpdates() && sZipUrl[0] != '\0' && sDigest[0] != '\0' && getenv("FE8PC_UPDATED") == NULL;
}

/* Each frame step (frame.c, before the picture is shown). */
void Reforged_UpdateFrame(void)
{
    int st, ins;
    long have, latest;
    char line[64];

    if (!sStarted)
        return;
    ins = __atomic_load_n(&sInstall, __ATOMIC_ACQUIRE);
    if (ins == INS_RUNNING)
    {
        snprintf(line, sizeof(line), "Installing %s...", sTag);
        PortState_ToastText(line, 0, 2);
        return;
    }
    if (ins == INS_DONE)
    {
        /* the restart only on the title screen (nothing in play to lose); a game started meanwhile is left
           alone: the new version starts next time (or at the next visit to the title screen) */
        if (Proc_Find(gProcScr_TitleScreen) == NULL)
        {
            if (!sToldLater)
            {
                sToldLater = 1;
                PortLog("[update] %s installed while a game is on: it starts next time\n", sTag);
                snprintf(line, sizeof(line), "%s installed: starts next time", sTag);
                PortState_ToastText(line, 0, UPD_NOTICE_FRAMES);
            }
            sDoneFrames = 0;
            return;
        }
        if (sDoneFrames++ == 0)
            PortLog("[update] %s installed: starting it\n", sTag);
        snprintf(line, sizeof(line), "%s installed: restarting", sTag);
        PortState_ToastText(line, 0, 2);
        if (sDoneFrames >= 90)
        {
            Platform_RestartGame();
            PortExit(0);
        }
        return;
    }
    if (ins == INS_FAILED)
    {
        PortLog("[update] not installed: %s; the game is as it was\n", sFail);
        snprintf(line, sizeof(line), "Update failed: %s", sFail);
        PortState_ToastText(line, 1, UPD_NOTICE_FRAMES);
        __atomic_store_n(&sInstall, INS_IDLE, __ATOMIC_RELEASE);
        sDone = 1;
        return;
    }
    if (sDone)
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
    if (!CanInstall())
    {
        PortLog("[update] latest release %s, this build %s: named on the title screen%s\n", sTag, PORT_VERSION,
            Platform_CanInstallUpdates() ? " (no Windows zip with a digest, or just installed: no install offered)" : "");
        snprintf(line, sizeof(line), "%s is out (GitHub releases)", sTag);
        PortState_ToastText(line, 0, UPD_NOTICE_FRAMES);
        sDone = 1;
        return;
    }
    if (!sOffered)
    {
        sOffered = 1;
        PortLog("[update] latest release %s, this build %s: offered on the title screen (Select installs)\n", sTag,
            PORT_VERSION);
    }
    snprintf(line, sizeof(line), "%s is out: Select installs it", sTag);
    PortState_ToastText(line, 0, 2);
    if (gKeyStatusPtr->newKeys & SELECT_BUTTON)
    {
        PortLog("[update] Select on the title screen: installing %s\n", sTag);
        __atomic_store_n(&sInstall, INS_RUNNING, __ATOMIC_RELEASE);
        if (!Platform_RunDetached(InstallThread, NULL))
            Fail("no thread for the download");
    }
}

/* Save states (port/include/port_state.h): not game state. */
PORT_STATE_SESSION(sState)
PORT_STATE_SESSION(sInstall)
PORT_STATE_SESSION(sTag)
PORT_STATE_SESSION(sZipUrl)
PORT_STATE_SESSION(sDigest)
PORT_STATE_SESSION(sFail)
PORT_STATE_SESSION(sStarted)
PORT_STATE_SESSION(sDone)
PORT_STATE_SESSION(sOffered)
PORT_STATE_SESSION(sDoneFrames)
PORT_STATE_SESSION(sToldLater)

#endif /* PORT_PLAYED_BUILD */
