#include "global.h"

/*
 * Reforged Options, Helpers: "Save states" (RFO_SAVE_STATES, v1.24.0; the owner's request of 2026-11-14:
 * "can we add a save state option? ctrl+S = save, ctrl+L = load"; one slot; on by default since v1.24.1,
 * the owner: "make the default option for the savestates working to be ON"). With the row
 * on, Ctrl+S takes a photo of the whole game and Ctrl+L puts it back, anywhere: the map, a battle
 * animation, a menu, an event, the world map, preparations. Loading a state changes play, so once one was
 * loaded the saves that game writes are marked save_states=1 (v1.24.1: Reforged_SaveMarkStateLoaded,
 * carried through the game's later saves and suspends; v1.24.0 marked every save written with the row
 * on; reforged_savemark.c), and it does
 * nothing in a Nuzlocke run (Reforged_RandoNuzlocke). port/include/port_state.h has the overview.
 *
 * Where. A key press only asks (PortState_Request, from the platform once a frame step); the photo is
 * taken, or put back, at the next program point: port/hw/frame.c VBlankIntrWait with gBmSt.main_loop_ended
 * set, the end of OnMain, MainUpdate_8055C68 or InBattleMainRoutine. Each of them returns right after that
 * call, so the host call stack there is always main, AgbMain, ExecMainUpdate, the main callback,
 * VBlankIntrWait, and nothing on it holds game state: the Gate 0 harness's sample point, where its save
 * states were proven (port/test/README.md "Save states"; this file shares no code with it).
 *
 * What. The GBA memory the port keeps: EWRAM, IWRAM, the I/O page, palette, VRAM and OAM (never SRAM, the
 * battery save: a load does not change the in-memory SRAM nor the .sav, and a save the game writes after
 * a load is that game's, as ever). And every byte of the host .data and .bss between the markers of
 * port/statemark/begin.c and end.c (the game's own objects, the port's, the generated stubs; Windows also
 * the sorted .data$* sections): the run's fixes and randomizer, the randomized tables, the palette caches,
 * the RNG and the procs live in GBA RAM or there. port/tools/check_state_ranges.py checks at every link
 * that every own object is inside and nothing of the C runtime or the libraries is. A load restores all of
 * it except what a file names with PORT_STATE_SESSION (port_state.h): the options and controls as the
 * settings file has them, the save file's and the save mark's records (they mirror the .sav), the window,
 * renderer and frame buffers, the audio device and its queue, the pacer and fast-forward, input, logs,
 * test switches, the FE7/FE6 loader's record and the FE6 copy (a state names the ROMs instead). So a new
 * global is in every state by default; one that must not be (a handle, a heap pointer) needs that line,
 * and FE8PC_STATE_CHECK=1 (below) finds what was missed.
 * Heap pointers: none of the game's state may point into the heap, whose addresses differ from run to
 * run. The FE7/FE6 loader's animation scripts and the FE6 copy moved to static buffers for that
 * (reforged_fe7.c sKeep, sFe6Rom); the exe's image and the GBA map are at fixed addresses (Linux: no PIE,
 * -Ttext-segment=0x10000000; Windows: --image-base=0x10000000 --disable-dynamicbase, no relocations), so
 * every code and data address in a state is the same in every run of the same exe.
 * Rewind's kept turns are the game's (reforged_rewind.c): they come back with a state.
 *
 * After a load: the keys held then reach the game only once let go (Platform_ReleaseKeys; REG_KEYINPUT
 * reads none until the next frame step), the sound joins without a click (Platform_AudioJoin: the music
 * and effects are the state's own, the PSG and FIFO state included, so nothing hangs on), the frame counter
 * is the photo's (a key file goes on from there; what times itself against it in session data does not:
 * savefile_pc.c's write retry counts frame steps itself, fast-forward's release line drops its frame count),
 * and the settings file is written if it differs from the options in use (Reforged_SettingsAfterStateLoad:
 * a Reforged screen's "changed" flag lives in GBA RAM and went back with the load).
 *
 * The file: one slot, "<the .sav the game writes>.state" (beside the save mark's ".reforged"; the per-user
 * folder's fe8pc.state when there is no save file), written atomically (a temporary file, then a rename).
 * Little-endian:
 *   "FE8STATE", u32 version (1), u32 header size, the exe's SHA-1 (41 bytes, hex and NUL), the FE8 ROM's,
 *   the FE7 ROM's and the FE6 ROM's ("" when not loaded), the save file's name (160), u32 frame, chapter,
 *   turn, save slot, randomized; u32 part count, then each part's u32 address and size; u32 payload size,
 *   u32 payload CRC-32; the payload: per part a bitmap of its 256-byte chunks (bit set: not all zero) and
 *   those chunks (the session data written as zeros).
 * A state loads only into the same exe (its build id: the SHA-1 of the exe file) and the same ROM set; a
 * state from another version, another ROM set or a missing ROM, or a damaged one, is refused with a toast
 * and changes nothing. States do not survive an update.
 *
 * Feedback: a toast in the window's top-left corner for 90 frame steps: "State saved", "State loaded",
 * "No state", "Refused (other version)" ("other ROMs", "damaged"), "Not in Nuzlocke", "State not saved";
 * with the row off (v1.24.1, the owner: "Yes, add the hint"), a press of Ctrl+S or Ctrl+L shows
 * "Save states are off (Helpers)" and does nothing else (PortState_OffHint).
 * It is drawn on the window's picture only (port/hw/frame.c), never into GBA memory nor a dumped frame.
 *
 * Test switches (DIVERGENCES.md PB-9; each does nothing unless set, and acts only with the row on, but
 * FE8PC_TEST_HOLD_KEYS, which presses keys with the row on or off):
 *   FE8PC_TEST_STATE_SAVE_AT=F[,F...]   a save at the first program point at or after frame F (each once);
 *   FE8PC_TEST_STATE_LOAD_AT=F[,F...]   a load likewise (a load puts the frame counter back: each fires once);
 *   FE8PC_TEST_STATE_DUMP_DIR=DIR       FE8PC_DUMP_FRAMES' frames after the first load go to DIR (DIR_2 ...
 *                                       after the next ones), so they do not replace the frames before it;
 *   FE8PC_TEST_STATE_TOAST_DUMP=1       the toast drawn into dumped frames too (for screenshots);
 *   FE8PC_TEST_STATE_EXIT_AFTER=N       quit N program points after a load (the frame limit counts the
 *                                       photo's frame number, which a load puts back);
 *   FE8PC_STATE_CHECK=1                 self-test 1: at the program point after a save each 256-byte chunk of
 *                                       the restored memory is hashed, and at the point after a load of that
 *                                       state the hashes are compared ("check: ... 0 of N chunks differ" when
 *                                       the load gave back exactly what followed the save: nothing the game
 *                                       reads is left out of the state);
 *   FE8PC_STATE_CHECK_FILE=PATH         self-test 2: each state saved is compared with PATH, a state another
 *                                       process saved at the same frame of the same key file ("check: this
 *                                       state against PATH: 0 of N chunks differ" when nothing of this
 *                                       process's own, a heap or library address, a handle, the clock, the
 *                                       environment, was taken into the state). A new global that holds such
 *                                       a value and lacks its PORT_STATE_SESSION line shows up here.
 *   FE8PC_TEST_HOLD_KEYS (port/sdl3/platform_sdl3.c): keys held, for the Ctrl+S / Ctrl+L path itself.
 */

#if PORT_PLAYED_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bm.h"
#include "fontgrp.h"
#include "variables.h"

#include "port_hw.h"
#include "port_reforged.h"
#include "port_state.h"
#include "../platform.h"

#define STATE_MAGIC   "FE8STATE"
#define STATE_VERSION 1
#define CHUNK         256
#define EXCL_MAX      512
#define PART_MAX      12
#define AT_MAX        16
#define TOAST_FRAMES  90
#define SAV_NAME      160

extern unsigned char gPortStateDataBegin[], gPortStateDataEnd[], gPortStateBssBegin[], gPortStateBssEnd[];
#ifdef _WIN32
extern unsigned char gPortStateData2Begin[], gPortStateData2End[];
#endif

struct Excl
{
    uintptr_t lo, hi;
    const char *name;
};

struct Part
{
    uint32_t addr, size;
    const char *name;
    int host; /* host data (else a GBA region) */
};

/* Everything this file keeps is session data (left out of every state, itself included). */
static struct
{
    int self;
    struct Excl excl[EXCL_MAX];
    int exclCount, exclDropped, exclMerged;
    struct Part part[PART_MAX];
    int partCount, inited, usable;
    int request; /* 1 save, 2 load, at the next program point */
    int envRead;
    unsigned saveAt[AT_MAX], loadAt[AT_MAX];
    int saveAtCount, loadAtCount;
    unsigned saveFired, loadFired;
    const char *dumpBase;
    char dumpDir[1100];
    int loads, toastDump, check, exitAfter, exitCountdown;
    uint32_t crcTable[256];
    int crcReady;
    const char *checkFile;
    char toast[40];
    int toastFrames, toastWarn;
    char exeSha1[41];
    int exeKnown; /* 0 not yet, 1 known, -1 not readable */
    uint32_t *digest;
    uint32_t digestCount;
    int checkPhase; /* 1: hash at the next point (a save was taken); 2: compare there (a load was made) */
} S;

/* ------------------------------------------------------------------ the session entries */

void PortState_Exclude(const void *p, unsigned long size, const char *name)
{
    if (!S.self)
    {
        S.self = 1;
        PortState_Exclude(&S, sizeof(S), "reforged_savestate.c S");
    }
    if (size == 0)
        return;
    if (S.exclCount == EXCL_MAX)
    {
        S.exclDropped++;
        return;
    }
    S.excl[S.exclCount].lo = (uintptr_t)p;
    S.excl[S.exclCount].hi = (uintptr_t)p + size;
    S.excl[S.exclCount].name = name;
    S.exclCount++;
    S.exclMerged = 0;
}

/* Sorted by address, overlaps merged. */
static void StMergeExcl(void)
{
    int i, j, n = 0;

    if (S.exclMerged)
        return;
    for (i = 1; i < S.exclCount; i++)
    {
        struct Excl e = S.excl[i];

        for (j = i; j > 0 && S.excl[j - 1].lo > e.lo; j--)
            S.excl[j] = S.excl[j - 1];
        S.excl[j] = e;
    }
    for (i = 0; i < S.exclCount; i++)
    {
        if (n > 0 && S.excl[i].lo <= S.excl[n - 1].hi)
        {
            if (S.excl[i].hi > S.excl[n - 1].hi)
                S.excl[n - 1].hi = S.excl[i].hi;
            continue;
        }
        S.excl[n++] = S.excl[i];
    }
    S.exclCount = n;
    S.exclMerged = 1;
}

static void StAddPart(uint32_t addr, uint32_t size, const char *name, int host)
{
    struct Part *p = &S.part[S.partCount++];

    p->addr = addr;
    p->size = size;
    p->name = name;
    p->host = host;
}

static void StAddHost(const unsigned char *begin, const unsigned char *end, const char *name)
{
    uintptr_t lo = (uintptr_t)begin, hi = (uintptr_t)end + 4;

    if (hi <= lo || hi - lo > 0x4000000)
    {
        PortLog("[savestate] the %s markers are out of order (0x%08X, 0x%08X): save states are off\n", name,
            (unsigned)lo, (unsigned)hi);
        S.usable = 0;
        return;
    }
    StAddPart((uint32_t)lo, (uint32_t)(hi - lo), name, 1);
}

/* The parts, once: 1 when states can be used. */
static int StInit(void)
{
    uint32_t hostBytes = 0, sessionBytes = 0;
    int i, j;

    if (S.inited)
        return S.usable;
    S.inited = 1;
    S.usable = 1;
    StAddPart(PORT_EWRAM_START, 0x40000, "EWRAM", 0);
    StAddPart(PORT_IWRAM_START, 0x8000, "IWRAM", 0);
    StAddPart(PORT_IO_START, 0x1000, "IO", 0);
    StAddPart(PORT_PLTT_START, 0x1000, "PLTT", 0);
    StAddPart(PORT_VRAM_START, 0x18000, "VRAM", 0);
    StAddPart(PORT_OAM_START, 0x1000, "OAM", 0);
    StAddHost(gPortStateDataBegin, gPortStateDataEnd, ".data");
    StAddHost(gPortStateBssBegin, gPortStateBssEnd, ".bss");
#ifdef _WIN32
    StAddHost(gPortStateData2Begin, gPortStateData2End, ".data$");
#endif
    /* the DMA channels' source and destination registers (write-only on the GBA; the control words are
       kept). DMA0 and DMA3: port/hw/dma.c runs a transfer when its control word is written, so they may
       hold a host address of this process only (a stack buffer's) that nothing reads again. DMA1 and DMA2:
       port/audio/fifo_pc.c PortAudio_OnVBlank reads them every VBlank to find the FIFO A/B sound lanes;
       they hold GBA addresses (m4a's pcmBuffer and REG_FIFO_A/B, src/m4a.c SoundInit, from m4aSoundInit),
       the same in every process from start-up on, so the session's values equal any photo's */
    for (i = 0; i < 4; i++)
        PortState_Exclude((const void *)(uintptr_t)(PORT_IO_START + 0xB0 + 12 * i), 8, "DMA source and destination");
    if (S.exclDropped)
    {
        PortLog("[savestate] %d session entries past the %d this file holds: save states are off\n", S.exclDropped,
            EXCL_MAX);
        S.usable = 0;
    }
    StMergeExcl();
    for (i = 0; i < S.partCount; i++)
        if (S.part[i].host)
            hostBytes += S.part[i].size;
    for (j = 0; j < S.exclCount; j++)
    {
        int in = 0;

        for (i = 0; i < S.partCount; i++)
            if (S.excl[j].lo >= S.part[i].addr && S.excl[j].hi <= S.part[i].addr + S.part[i].size)
                in = 1;
        if (in)
            sessionBytes += (uint32_t)(S.excl[j].hi - S.excl[j].lo);
        else
            PortLog("[savestate] session entry %s (0x%08X +0x%X) is outside the saved memory\n",
                S.excl[j].name ? S.excl[j].name : "?", (unsigned)S.excl[j].lo, (unsigned)(S.excl[j].hi - S.excl[j].lo));
    }
    PortLog("[savestate] save states: %u bytes of host data in %d parts, %d session ranges (%u bytes) left out%s\n",
        hostBytes, S.partCount - 6, S.exclCount, sessionBytes, S.usable ? "" : "; OFF");
    return S.usable;
}

/* bytes [p, p + n) of a part, with every session range in it set to zero */
static void StBlankSession(uint8_t *copy, uint32_t addr, uint32_t n)
{
    int j;

    for (j = 0; j < S.exclCount; j++)
    {
        uintptr_t lo = S.excl[j].lo > addr ? S.excl[j].lo : addr;
        uintptr_t hi = S.excl[j].hi < (uintptr_t)addr + n ? S.excl[j].hi : (uintptr_t)addr + n;

        if (lo < hi)
            memset(copy + (lo - addr), 0, hi - lo);
    }
}

/* The game's bytes of a part, as a state holds them (malloc'd; NULL when out of memory). */
static uint8_t *StTakePart(const struct Part *p)
{
    uint8_t *copy = malloc(p->size);

    if (copy == NULL)
        return NULL;
    StMergeExcl();
    memcpy(copy, (const void *)(uintptr_t)p->addr, p->size);
    StBlankSession(copy, p->addr, p->size);
    return copy;
}

/* Puts a part back, around the session ranges in it. */
static void StPutPart(const struct Part *p, const uint8_t *src)
{
    uintptr_t at = p->addr, end = (uintptr_t)p->addr + p->size;
    int j;

    StMergeExcl(); /* sorted, for the walk below */
    for (j = 0; j < S.exclCount && at < end; j++)
    {
        if (S.excl[j].hi <= at || S.excl[j].lo >= end)
            continue;
        if (S.excl[j].lo > at)
            memcpy((void *)at, src + (at - p->addr), S.excl[j].lo - at);
        at = S.excl[j].hi;
    }
    if (at < end)
        memcpy((void *)at, src + (at - p->addr), end - at);
}

/* ------------------------------------------------------------------ ids, path, toast */

static uint32_t StCrc32(uint32_t crc, const uint8_t *p, size_t n)
{
    size_t i;

    if (!S.crcReady)
    {
        uint32_t v, b;
        int k;

        for (b = 0; b < 256; b++)
        {
            for (v = b, k = 0; k < 8; k++)
                v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            S.crcTable[b] = v;
        }
        S.crcReady = 1;
    }
    crc = ~crc;
    for (i = 0; i < n; i++)
        crc = S.crcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

/* The exe's SHA-1, once: its build id (an update, or any rebuild, has another). */
static const char *StExeSha1(void)
{
    if (S.exeKnown == 0)
    {
        char path[4096];
        size_t size = 0;
        void *data = PortExePath(path, sizeof(path)) ? Platform_LoadFile(path, &size) : NULL;

        S.exeKnown = -1;
        if (data != NULL)
        {
            PortSha1Hex(data, size, S.exeSha1);
            Platform_FreeFile(data);
            S.exeKnown = 1;
        }
        else
            PortLog("[savestate] cannot read this exe to take its build id: save states are off\n");
    }
    return S.exeKnown == 1 ? S.exeSha1 : NULL;
}

static int StStatePath(char *out, size_t size)
{
    const char *sav = Reforged_SaveMarkWritePath(), *dir;

    if (sav != NULL && sav[0] != '\0')
        return snprintf(out, size, "%s.state", sav) < (int)size;
    dir = Platform_UserDir();
    if (dir != NULL)
        return snprintf(out, size, "%sfe8pc.state", dir) < (int)size;
    return 0;
}

static void StToast(const char *text, int warn)
{
    snprintf(S.toast, sizeof(S.toast), "%s", text);
    S.toastFrames = TOAST_FRAMES;
    S.toastWarn = warn;
}

/* The toast's pixels: a dark panel with a light rim, the text in the game's system font. */
enum { TC_PANEL, TC_RIM, TC_TEXT, TC_WARN, TC_SHADOW };
static const uint8_t sToastRgb[5][3] = {
    { 24, 32, 64 }, { 200, 208, 232 }, { 248, 248, 248 }, { 248, 200, 88 }, { 32, 32, 40 },
};

static void StToastDraw(void (*put)(void *dst, int x, int y, int c), void *dst)
{
    const char *s;
    int w = 0, x0 = 4, y0 = 4, h = 15, x, y;

    for (s = S.toast; *s; s++)
    {
        struct Glyph *g = TextGlyphs_System[(u8)*s] ? TextGlyphs_System[(u8)*s] : TextGlyphs_System['?'];

        w += g != NULL ? g->width : 0;
    }
    w += 10;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
        {
            int edgeX = x == 0 || x == w - 1, edgeY = y == 0 || y == h - 1;

            if (edgeX && edgeY)
                continue;
            put(dst, x0 + x, y0 + y, edgeX || edgeY ? TC_RIM : TC_PANEL);
        }
    x = x0 + 5;
    for (s = S.toast; *s; s++)
    {
        struct Glyph *g = TextGlyphs_System[(u8)*s] ? TextGlyphs_System[(u8)*s] : TextGlyphs_System['?'];
        int r, c;

        if (g == NULL)
            continue;
        for (r = 0; r < 16; r++)
            for (c = 0; c < 16 && c < g->width + 1; c++)
            {
                int v = (g->bitmap[r] >> (c * 2)) & 3;

                /* the system font's 2-bit pixels: 2 the stroke, 1 and 3 its light and dark edge */
                if (v == 1 || v == 2)
                    put(dst, x + c, y0 - 1 + r, S.toastWarn ? TC_WARN : TC_TEXT);
                else if (v == 3)
                    put(dst, x + c, y0 - 1 + r, TC_SHADOW);
            }
        x += g->width;
    }
}

static void StPutRgba(void *dst, int x, int y, int c)
{
    if (x >= 0 && y >= 0 && x < 240 && y < 160)
        ((uint32_t *)dst)[y * 240 + x] = ((uint32_t)sToastRgb[c][0] << 24) | ((uint32_t)sToastRgb[c][1] << 16) |
                                         ((uint32_t)sToastRgb[c][2] << 8) | 0xFF;
}

static void StPut555(void *dst, int x, int y, int c)
{
    if (x >= 0 && y >= 0 && x < 240 && y < 160)
        ((uint16_t *)dst)[y * 240 + x] = (uint16_t)((sToastRgb[c][2] >> 3) << 10 | (sToastRgb[c][1] >> 3) << 5 |
                                                    (sToastRgb[c][0] >> 3));
}

void PortState_ToastRgba(uint32_t *rgba)
{
    if (S.toastFrames <= 0)
        return;
    StToastDraw(StPutRgba, rgba);
    S.toastFrames--;
}

void PortState_ToastDump555(uint16_t *frame555)
{
    if (S.toastDump && S.toastFrames > 0)
        StToastDraw(StPut555, frame555);
}

const char *PortState_DumpDir(void)
{
    return S.loads > 0 && S.dumpBase != NULL ? S.dumpDir : NULL;
}

int PortState_KeysOn(void)
{
    return Reforged_GetOption(RFO_SAVE_STATES) != 0;
}

void PortState_Request(int what)
{
    if (!PortState_KeysOn() || (what != 1 && what != 2))
        return;
    S.request = what;
    PortLog("[savestate] Ctrl+%c at frame %u: %s at the next program point\n", what == 1 ? 'S' : 'L', gPortFrameCount,
        what == 1 ? "save" : "load");
}

/* v1.24.1: the row off, Ctrl+S or Ctrl+L pressed: say where the row is, nothing else (display only). */
void PortState_OffHint(int what)
{
    if (PortState_KeysOn() || (what != 1 && what != 2))
        return;
    StToast("Save states are off (Helpers)", 1);
    PortLog("[savestate] Ctrl+%c at frame %u: the Save states row is off (hint shown)\n", what == 1 ? 'S' : 'L',
        gPortFrameCount);
}

/* ------------------------------------------------------------------ the file */

struct Buf
{
    uint8_t *p;
    size_t n, cap;
    int bad;
};

static void StNeed(struct Buf *b, size_t more)
{
    if (b->bad || b->n + more <= b->cap)
        return;
    {
        size_t cap = b->cap ? b->cap : 1 << 20;
        uint8_t *q;

        while (cap < b->n + more)
            cap *= 2;
        q = realloc(b->p, cap);
        if (q == NULL)
        {
            b->bad = 1;
            return;
        }
        b->p = q;
        b->cap = cap;
    }
}

static void StPutBytes(struct Buf *b, const void *p, size_t n)
{
    StNeed(b, n);
    if (b->bad)
        return;
    memcpy(b->p + b->n, p, n);
    b->n += n;
}

static void StPut32(struct Buf *b, uint32_t v)
{
    StPutBytes(b, &v, 4);
}

static void StPutText(struct Buf *b, const char *s, size_t n)
{
    char field[SAV_NAME];

    memset(field, 0, sizeof(field));
    snprintf(field, n, "%s", s != NULL ? s : "");
    StPutBytes(b, field, n);
}

struct Ids
{
    char exe[41], fe8[41], fe7[41], fe6[41];
};

static int StTakeIds(struct Ids *ids)
{
    const char *exe = StExeSha1();

    if (exe == NULL)
        return 0;
    memcpy(ids->exe, exe, 41);
    snprintf(ids->fe8, sizeof(ids->fe8), "%s", PortRomSha1());
    Reforged_ForeignRomSha1(0, ids->fe7);
    Reforged_ForeignRomSha1(1, ids->fe6);
    return 1;
}

static const char *StBaseName(const char *path)
{
    const char *a = strrchr(path, '/'), *b = strrchr(path, '\\');

    if (b != NULL && (a == NULL || b > a))
        a = b;
    return a != NULL ? a + 1 : path;
}

static void StCheckAgainst(const char *path, const char *other);

static void StSave(void)
{
    struct Buf b = { NULL, 0, 0, 0 };
    struct Ids ids;
    char path[4200], tmp[4300];
    size_t payloadAt;
    int i;

    if (Reforged_RandoNuzlocke())
    {
        PortLog("[savestate] save refused: this run is a Nuzlocke run\n");
        StToast("Not in Nuzlocke", 1);
        return;
    }
    if (!StInit() || !StTakeIds(&ids) || !StStatePath(path, sizeof(path)))
    {
        PortLog("[savestate] save: no state can be written (see above, or no save file and no per-user folder)\n");
        StToast("State not saved", 1);
        return;
    }
    if (gPortIrqDepth != 0)
    {
        PortLog("[savestate] save asked inside an IRQ handler: not taken\n");
        return;
    }
    StPutBytes(&b, STATE_MAGIC, 8);
    StPut32(&b, STATE_VERSION);
    StPut32(&b, 0); /* the header's size, below */
    StPutBytes(&b, ids.exe, 41);
    StPutBytes(&b, ids.fe8, 41);
    StPutBytes(&b, ids.fe7, 41);
    StPutBytes(&b, ids.fe6, 41);
    StPutText(&b, StBaseName(Reforged_SaveMarkWritePath()), SAV_NAME);
    StPut32(&b, gPortFrameCount);
    StPut32(&b, (uint32_t)(int)gPlaySt.chapterIndex);
    StPut32(&b, gPlaySt.chapterTurnNumber);
    StPut32(&b, gPlaySt.gameSaveSlot);
    StPut32(&b, (uint32_t)Reforged_RandoActive());
    StPut32(&b, (uint32_t)S.partCount);
    for (i = 0; i < S.partCount; i++)
    {
        StPut32(&b, S.part[i].addr);
        StPut32(&b, S.part[i].size);
    }
    StPut32(&b, 0); /* payload size */
    StPut32(&b, 0); /* payload CRC-32 */
    if (!b.bad)
    {
        uint32_t v = (uint32_t)b.n;

        memcpy(b.p + 12, &v, 4);
    }
    payloadAt = b.n;
    for (i = 0; i < S.partCount && !b.bad; i++)
    {
        const struct Part *p = &S.part[i];
        uint8_t *copy = StTakePart(p);
        uint32_t chunks = (p->size + CHUNK - 1) / CHUNK, c;
        size_t mapAt;

        if (copy == NULL)
        {
            b.bad = 1;
            break;
        }
        mapAt = b.n;
        StNeed(&b, (chunks + 7) / 8);
        if (!b.bad)
        {
            memset(b.p + b.n, 0, (chunks + 7) / 8);
            b.n += (chunks + 7) / 8;
        }
        for (c = 0; c < chunks && !b.bad; c++)
        {
            uint32_t at = c * CHUNK, n = p->size - at < CHUNK ? p->size - at : CHUNK, k;

            for (k = 0; k < n && copy[at + k] == 0; k++)
                ;
            if (k == n)
                continue;
            StPutBytes(&b, copy + at, n);
            if (!b.bad)
                b.p[mapAt + c / 8] |= (uint8_t)(1 << (c % 8));
        }
        free(copy);
    }
    if (b.bad)
    {
        free(b.p);
        PortLog("[savestate] save: out of memory\n");
        StToast("State not saved", 1);
        return;
    }
    {
        uint32_t size = (uint32_t)(b.n - payloadAt), crc = StCrc32(0, b.p + payloadAt, size);

        memcpy(b.p + payloadAt - 8, &size, 4);
        memcpy(b.p + payloadAt - 4, &crc, 4);
    }
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (Platform_SaveFileAtomic(path, tmp, b.p, b.n) != 0)
    {
        PortLog("[savestate] save: cannot write %s\n", path);
        StToast("State not saved", 1);
        free(b.p);
        return;
    }
    PortLog("[savestate] saved %s (%u bytes): frame %u, chapter %d, turn %u%s\n", path, (unsigned)b.n, gPortFrameCount,
        (int)gPlaySt.chapterIndex, (unsigned)gPlaySt.chapterTurnNumber, Reforged_RandoActive() ? ", randomized" : "");
    free(b.p);
    StToast("State saved", 0);
    if (S.check)
        S.checkPhase = 1;
    if (S.checkFile != NULL)
        StCheckAgainst(path, S.checkFile);
}

/* Reading: each field in turn; 0 once past the end. */
struct Rd
{
    const uint8_t *p, *end;
    int bad;
};

static const uint8_t *StTake(struct Rd *r, size_t n)
{
    const uint8_t *p = r->p;

    if (r->bad || (size_t)(r->end - r->p) < n)
    {
        r->bad = 1;
        return NULL;
    }
    r->p += n;
    return p;
}

static uint32_t StTake32(struct Rd *r)
{
    const uint8_t *p = StTake(r, 4);
    uint32_t v = 0;

    if (p != NULL)
        memcpy(&v, p, 4);
    return v;
}

static int StSameText(const uint8_t *field, const char *want, size_t n)
{
    return field != NULL && memchr(field, 0, n) != NULL && strcmp((const char *)field, want) == 0;
}

/* A state file read and checked against this exe, its ROMs and its parts. 0: bufs[] hold each part (the
   caller frees them), *frame, *chapter, *turn its photo's; else why it is refused (nothing allocated):
   1 not a state file, 2 another version, 3 other ROMs, 4 damaged. */
enum { ST_OK, ST_NOT_STATE, ST_OTHER_VERSION, ST_OTHER_ROMS, ST_DAMAGED };

static int StParse(const char *path, const uint8_t *data, size_t size, const struct Ids *ids, uint8_t **bufs,
    uint32_t *frame, uint32_t *chapter, uint32_t *turn)
{
    struct Rd r;
    const uint8_t *exe, *fe8, *fe7, *fe6, *sav;
    uint32_t version, payloadSize, crc, parts;
    int i;

    r.p = data;
    r.end = data + size;
    r.bad = 0;
    memset(bufs, 0, sizeof(bufs[0]) * PART_MAX);
    if (size < 8 || memcmp(data, STATE_MAGIC, 8) != 0)
    {
        PortLog("[savestate] %s is not a state file\n", path);
        return ST_NOT_STATE;
    }
    StTake(&r, 8);
    version = StTake32(&r);
    StTake32(&r);
    exe = StTake(&r, 41);
    fe8 = StTake(&r, 41);
    fe7 = StTake(&r, 41);
    fe6 = StTake(&r, 41);
    sav = StTake(&r, SAV_NAME);
    *frame = StTake32(&r);
    *chapter = StTake32(&r);
    *turn = StTake32(&r);
    StTake32(&r);
    StTake32(&r);
    parts = StTake32(&r);
    if (r.bad && (size < 12 || version == STATE_VERSION))
    {
        /* cut short inside the header (this format's header, or too short to say which format): damaged,
           not another version */
        PortLog("[savestate] %s is damaged (it ends inside its header: %u bytes)\n", path, (unsigned)size);
        return ST_DAMAGED;
    }
    if (r.bad || version != STATE_VERSION || !StSameText(exe, ids->exe, 41))
    {
        PortLog("[savestate] %s was saved by another version (format %u, build %.12s; this one: format %u, build %.12s)\n",
            path, (unsigned)version, exe != NULL && memchr(exe, 0, 41) ? (const char *)exe : "?", STATE_VERSION,
            ids->exe);
        return ST_OTHER_VERSION;
    }
    if (!StSameText(fe8, ids->fe8, 41) || !StSameText(fe7, ids->fe7, 41) || !StSameText(fe6, ids->fe6, 41))
    {
        PortLog("[savestate] %s was saved with other ROMs (FE7 %s, FE6 %s; now FE7 %s, FE6 %s)\n", path,
            fe7[0] ? "loaded" : "none", fe6[0] ? "loaded" : "none", ids->fe7[0] ? "loaded" : "none",
            ids->fe6[0] ? "loaded" : "none");
        return ST_OTHER_ROMS;
    }
    if (parts != (uint32_t)S.partCount)
        r.bad = 1;
    for (i = 0; i < S.partCount && !r.bad; i++)
        if (StTake32(&r) != S.part[i].addr || StTake32(&r) != S.part[i].size)
            r.bad = 1;
    payloadSize = StTake32(&r);
    crc = StTake32(&r);
    if (r.bad || (size_t)(r.end - r.p) != payloadSize || StCrc32(0, r.p, payloadSize) != crc)
    {
        PortLog("[savestate] %s is damaged (its layout or CRC-32 does not match)\n", path);
        return ST_DAMAGED;
    }
    for (i = 0; i < S.partCount && !r.bad; i++)
    {
        const struct Part *p = &S.part[i];
        uint32_t chunks = (p->size + CHUNK - 1) / CHUNK, c;
        const uint8_t *map = StTake(&r, (chunks + 7) / 8);

        bufs[i] = calloc(1, p->size);
        if (bufs[i] == NULL || map == NULL)
        {
            r.bad = 1;
            break;
        }
        for (c = 0; c < chunks && !r.bad; c++)
        {
            uint32_t at = c * CHUNK, n = p->size - at < CHUNK ? p->size - at : CHUNK;
            const uint8_t *src;

            if (!(map[c / 8] >> (c % 8) & 1))
                continue;
            src = StTake(&r, n);
            if (src != NULL)
                memcpy(bufs[i] + at, src, n);
        }
    }
    if (r.bad || r.p != r.end)
    {
        PortLog("[savestate] %s is damaged (its chunks do not add up)\n", path);
        for (i = 0; i < S.partCount; i++)
        {
            free(bufs[i]);
            bufs[i] = NULL;
        }
        return ST_DAMAGED;
    }
    if (!StSameText(sav, StBaseName(Reforged_SaveMarkWritePath()), SAV_NAME))
        PortLog("[savestate] %s was saved beside '%.*s', the save file now is '%s'\n", path, SAV_NAME,
            memchr(sav, 0, SAV_NAME) ? (const char *)sav : "?", StBaseName(Reforged_SaveMarkWritePath()));
    return ST_OK;
}

static void StLoad(void)
{
    static const char *const sRefused[] = { "", "Refused (damaged)", "Refused (other version)",
                                            "Refused (other ROMs)", "Refused (damaged)" };
    struct Ids ids;
    char path[4200];
    size_t size = 0;
    uint8_t *data, *bufs[PART_MAX];
    uint32_t frame = 0, chapter = 0, turn = 0;
    int i, why;

    if (Reforged_RandoNuzlocke())
    {
        PortLog("[savestate] load refused: this run is a Nuzlocke run\n");
        StToast("Not in Nuzlocke", 1);
        return;
    }
    if (!StInit() || !StTakeIds(&ids) || !StStatePath(path, sizeof(path)))
    {
        StToast("No state", 1);
        return;
    }
    data = Platform_LoadFile(path, &size);
    if (data == NULL)
    {
        PortLog("[savestate] load: no state (%s)\n", path);
        StToast("No state", 1);
        return;
    }
    why = StParse(path, data, size, &ids, bufs, &frame, &chapter, &turn);
    Platform_FreeFile(data);
    if (why != ST_OK)
    {
        PortLog("[savestate] load refused: nothing changed\n");
        StToast(sRefused[why], 1);
        return;
    }
    for (i = 0; i < S.partCount; i++)
    {
        StPutPart(&S.part[i], bufs[i]);
        free(bufs[i]);
    }
    /* the keys held now are not the photo's: none until the next frame step, and the platform holds back
       what is still held */
    REG_KEYINPUT = 0x3FF;
    Platform_ReleaseKeys();
    Platform_AudioJoin();
    Reforged_FastForwardAfterStateLoad();
    Reforged_SettingsAfterStateLoad(); /* a Reforged screen's unsaved change: its flag went back with the load */
    Reforged_SaveMarkStateLoaded(1); /* v1.24.1: this game's saves are marked from now on */
    S.loads++;
    S.exitCountdown = S.exitAfter;
    if (S.dumpBase != NULL)
    {
        if (S.loads == 1)
            snprintf(S.dumpDir, sizeof(S.dumpDir), "%s", S.dumpBase);
        else
            snprintf(S.dumpDir, sizeof(S.dumpDir), "%s_%d", S.dumpBase, S.loads);
    }
    PortLog("[savestate] loaded %s: frame %u, chapter %d, turn %u\n", path, (unsigned)frame, (int)chapter, (unsigned)turn);
    StToast("State loaded", 0);
    if (S.check && S.digest != NULL)
        S.checkPhase = 2;
}

/* ------------------------------------------------------------------ the self-test (FE8PC_STATE_CHECK) */

/* FE8PC_STATE_CHECK_FILE: the state just saved against that file, a state another process saved at the same
   frame of the same key file: every restored byte must be the same. A difference is host data that is this
   process's own (a heap or library address, a handle, something read from the clock or the environment)
   and is not marked PORT_STATE_SESSION; each differing 256-byte chunk is listed. */
static void StCheckAgainst(const char *path, const char *other)
{
    struct Ids ids;
    size_t size = 0, size2 = 0;
    uint8_t *data, *mine, *bufs[PART_MAX], *bufs2[PART_MAX];
    uint32_t f, c, t, differ = 0, chunks = 0;
    int i;

    if (!StTakeIds(&ids))
        return;
    data = Platform_LoadFile(other, &size);
    mine = Platform_LoadFile(path, &size2);
    if (data == NULL || mine == NULL || StParse(other, data, size, &ids, bufs, &f, &c, &t) != ST_OK)
    {
        PortLog("[savestate] check: cannot compare with %s\n", other);
        if (data != NULL)
            Platform_FreeFile(data);
        if (mine != NULL)
            Platform_FreeFile(mine);
        return;
    }
    if (StParse(path, mine, size2, &ids, bufs2, &f, &c, &t) != ST_OK)
    {
        for (i = 0; i < S.partCount; i++)
            free(bufs[i]);
        Platform_FreeFile(data);
        Platform_FreeFile(mine);
        return;
    }
    for (i = 0; i < S.partCount; i++)
    {
        uint32_t at;

        for (at = 0; at < S.part[i].size; at += CHUNK, chunks++)
        {
            uint32_t n = S.part[i].size - at < CHUNK ? S.part[i].size - at : CHUNK;

            if (memcmp(bufs[i] + at, bufs2[i] + at, n) != 0)
            {
                uint32_t k = 0;

                while (bufs[i][at + k] == bufs2[i][at + k])
                    k++;
                if (differ < 64)
                    PortLog("[savestate] check: differs from %s: %s chunk 0x%08X, first byte 0x%08X\n", other,
                        S.part[i].name, S.part[i].addr + at, S.part[i].addr + at + k);
                differ++;
            }
        }
        free(bufs[i]);
        free(bufs2[i]);
    }
    Platform_FreeFile(data);
    Platform_FreeFile(mine);
    PortLog("[savestate] check: this state against %s: %u of %u chunks differ\n", other, differ, chunks);
}

/* FNV-1a of each 256-byte chunk of the restored memory (session data as zeros); NULL when out of memory. */
static uint32_t *StDigest(uint32_t *count)
{
    uint32_t total = 0, n = 0, *d;
    int i;

    for (i = 0; i < S.partCount; i++)
        total += (S.part[i].size + CHUNK - 1) / CHUNK;
    d = malloc(total * sizeof(*d));
    if (d == NULL)
        return NULL;
    for (i = 0; i < S.partCount; i++)
    {
        const struct Part *p = &S.part[i];
        uint8_t *copy = StTakePart(p);
        uint32_t at;

        if (copy == NULL)
        {
            free(d);
            return NULL;
        }
        for (at = 0; at < p->size; at += CHUNK)
        {
            uint32_t h = 2166136261u, k, end = p->size - at < CHUNK ? p->size : at + CHUNK;

            for (k = at; k < end; k++)
                h = (h ^ copy[k]) * 16777619u;
            d[n++] = h;
        }
        free(copy);
    }
    *count = n;
    return d;
}

static void StCheckStep(void)
{
    uint32_t count = 0, *d = StDigest(&count), n = 0, differ = 0;
    int i;

    if (d == NULL)
    {
        PortLog("[savestate] check: out of memory\n");
        S.checkPhase = 0;
        return;
    }
    if (S.checkPhase == 1)
    {
        free(S.digest);
        S.digest = d;
        S.digestCount = count;
        PortLog("[savestate] check: the program point after the save hashed (%u chunks)\n", count);
        S.checkPhase = 0;
        return;
    }
    for (i = 0; i < S.partCount; i++)
    {
        uint32_t at;

        for (at = 0; at < S.part[i].size; at += CHUNK, n++)
            if (n < count && n < S.digestCount && d[n] != S.digest[n])
            {
                if (differ < 64)
                    PortLog("[savestate] check: differs after the load: %s 0x%08X +0x%X\n", S.part[i].name,
                        S.part[i].addr + at, CHUNK);
                differ++;
            }
    }
    PortLog("[savestate] check: the program point after the load against the one after the save: %u of %u chunks "
            "differ\n", differ, count);
    free(d);
    S.checkPhase = 0;
}

/* ------------------------------------------------------------------ the program point */

static int StReadAt(const char *name, unsigned *at)
{
    const char *s = getenv(name);
    int n = 0;

    while (s != NULL && *s != '\0' && n < AT_MAX)
    {
        char *end;
        unsigned long v = strtoul(s, &end, 10);

        if (end == s)
            break;
        at[n++] = (unsigned)v;
        s = *end == ',' ? end + 1 : end;
    }
    return n;
}

static void StReadEnv(void)
{
    const char *s;

    S.envRead = 1;
    S.saveAtCount = StReadAt("FE8PC_TEST_STATE_SAVE_AT", S.saveAt);
    S.loadAtCount = StReadAt("FE8PC_TEST_STATE_LOAD_AT", S.loadAt);
    s = getenv("FE8PC_TEST_STATE_DUMP_DIR");
    S.dumpBase = s != NULL && s[0] != '\0' ? s : NULL;
    s = getenv("FE8PC_TEST_STATE_TOAST_DUMP");
    S.toastDump = s != NULL && strcmp(s, "1") == 0;
    s = getenv("FE8PC_STATE_CHECK");
    S.check = s != NULL && strcmp(s, "1") == 0;
    s = getenv("FE8PC_TEST_STATE_EXIT_AFTER");
    S.exitAfter = s != NULL ? atoi(s) : 0;
    s = getenv("FE8PC_STATE_CHECK_FILE");
    S.checkFile = s != NULL && s[0] != '0' && s[0] != 0 ? s : NULL;
    if (S.saveAtCount || S.loadAtCount)
        PortLog("[savestate] test switches: %d save(s), %d load(s) at frames%s%s\n", S.saveAtCount, S.loadAtCount,
            S.dumpBase ? "; frames after a load dumped to " : "", S.dumpBase ? S.dumpBase : "");
}

void PortState_Point(void)
{
    int i;

    if (!S.envRead)
        StReadEnv();
    if (S.checkPhase != 0)
        StCheckStep();
    if (S.exitCountdown > 0 && --S.exitCountdown == 0)
    {
        PortLog("[savestate] FE8PC_TEST_STATE_EXIT_AFTER: %d program points after the load, at frame %u\n", S.exitAfter,
            gPortFrameCount);
        PortExit(0);
    }
    for (i = 0; i < S.saveAtCount; i++)
        if (!(S.saveFired >> i & 1) && gPortFrameCount >= S.saveAt[i])
        {
            S.saveFired |= 1u << i;
            if (PortState_KeysOn())
            {
                PortLog("[savestate] FE8PC_TEST_STATE_SAVE_AT: save at frame %u\n", gPortFrameCount);
                StSave();
            }
            else
                PortLog("[savestate] FE8PC_TEST_STATE_SAVE_AT %u ignored: the Save states row is off\n", S.saveAt[i]);
        }
    for (i = 0; i < S.loadAtCount; i++)
        if (!(S.loadFired >> i & 1) && gPortFrameCount >= S.loadAt[i])
        {
            S.loadFired |= 1u << i;
            if (PortState_KeysOn())
            {
                PortLog("[savestate] FE8PC_TEST_STATE_LOAD_AT: load at frame %u\n", gPortFrameCount);
                StLoad();
            }
            else
                PortLog("[savestate] FE8PC_TEST_STATE_LOAD_AT %u ignored: the Save states row is off\n", S.loadAt[i]);
        }
    if (S.request != 0)
    {
        int what = S.request;

        S.request = 0;
        if (!PortState_KeysOn())
            return;
        if (what == 1)
            StSave();
        else
            StLoad();
    }
}

#endif /* PORT_PLAYED_BUILD */
